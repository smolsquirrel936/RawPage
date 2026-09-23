#include "app/Application.h"

#include "app/Services.h"

#include "core/Log.h"
#include "core/backend.h"
#include "core/doc/DocumentId.h"
#include "core/render/DpiPolicy.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"
#include "platform/platform.h"

#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/GridGuideLayer.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SplitViewContainer.h"
#include "ui/viewer/ViewportController.h"

#include <QCloseEvent>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QLocale>
#include <QTranslator>

#include <algorithm>
#include <utility>

namespace rawpage::app {
namespace {

using core::LogSubsystem;

constexpr const char* kSourceLocale = "en";

// "zh-TW" and "zh_TW" are the same locale spelled two ways -- BCP-47 uses the hyphen, Qt's file
// naming convention uses the underscore, and a person typing --lang will use whichever they saw
// last. Normalising here means the rest of the file has one spelling to reason about.
QString normalizeLocale(const QString& code)
{
    return QString(code).trimmed().replace(QLatin1Char('-'), QLatin1Char('_'));
}

QString languageOf(const QString& normalized)
{
    const qsizetype underscore = normalized.indexOf(QLatin1Char('_'));
    return underscore < 0 ? normalized : normalized.left(underscore);
}

} // namespace

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv)
{
    // Stage 1 has one window. Stage 4 brings a window set and a "last window closed" policy that
    // has to consider whether any document is still dirty; until then Qt's default -- quit when the
    // last window closes -- is both correct and the one people expect.
    setQuitOnLastWindowClosed(true);
}

Application::~Application() = default;

QString Application::catalogDirectory()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("i18n"));
}

bool Application::installCatalog(const QString& localeCode)
{
    const QString path =
        QDir(catalogDirectory()).filePath(QStringLiteral("rawpage_%1.qm").arg(localeCode));
    if (!QFileInfo::exists(path)) return false;

    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(path)) {
        // A .qm that exists and will not load is a build or deployment defect, not a missing
        // translation, and the two want different fixes -- so they get different log lines.
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "i18n: catalog present but unreadable:" << path;
        return false;
    }

    if (!installTranslator(translator.get())) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "i18n: Qt refused the catalog:" << path;
        return false;
    }

    translators_.push_back(std::move(translator));
    loadedCatalogs_ << path;
    return true;
}

QString Application::loadTranslators(const QString& requestedLocale, const QString& settingLocale)
{
    // The base layer. Installed FIRST so that everything installed after it wins where it has a
    // key -- see the header for why this is not the usual Qt arrangement.
    if (!installCatalog(QLatin1String(kSourceLocale))) {
        // Not fatal, and deliberately loud: every string in the product renders as its catalog key
        // from here, which looks like a catastrophic bug and is in fact a missing file.
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "i18n: the source catalog rawpage_en.qm is missing from" << catalogDirectory()
            << "-- the UI will render catalog keys";
    }

    QString wanted = normalizeLocale(requestedLocale);
    if (wanted.isEmpty()) wanted = normalizeLocale(settingLocale);
    if (wanted.isEmpty()) wanted = normalizeLocale(QLocale::system().name());

    activeLocale_ = QLatin1String(kSourceLocale);

    if (languageOf(wanted) == QLatin1String(kSourceLocale)) {
        // Already installed as the base; installing it twice would work and would make
        // loadedCatalogs() misleading.
        return activeLocale_;
    }

    // Exact first, then language-only: `zh_TW` and `zh` are different catalogs by decision D1-14
    // ("not a Traditional-to-Simplified conversion: the terminology differs"), so a request for
    // `zh_TW` must never be satisfied by a `zh_CN` catalog -- but a request for `pt_BR` against a
    // shipped `pt` legitimately is.
    if (installCatalog(wanted)) {
        activeLocale_ = wanted;
    } else if (const QString language = languageOf(wanted);
               language != wanted && installCatalog(language)) {
        activeLocale_ = language;
    } else {
        // The declared-but-unshipped locales (zh_CN, ja) land here, and so does a typo. Both are
        // reported the same way because from the running process they are the same fact: there is
        // no catalog for what was asked for.
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "i18n: no catalog for" << wanted << "in" << catalogDirectory()
            << "-- falling back to" << QLatin1String(kSourceLocale);
    }

    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "i18n: locale" << activeLocale_ << "--" << static_cast<int>(loadedCatalogs_.size())
        << "catalog(s) installed";
    return activeLocale_;
}

// ---------------------------------------------------------------------------------------------
// SessionShell -- Stage 4 T13, decision D4-9
// ---------------------------------------------------------------------------------------------

namespace {

// Which tab is in front, as an INDEX into `documents`, which is the tab order. `MainWindow` answers
// with a pointer; the session stores a position, because a pointer means nothing across a relaunch.
int activeIndexOf(const ui::MainWindow& window)
{
    const ui::DocumentWindow* current = window.currentDocument();
    if (current == nullptr) return 0;
    for (int at = 0; at < window.documentCount(); ++at)
        if (window.documentAt(at) == current) return at;
    return 0;
}

// The document's identity as the ENGINE holds it now -- which is the whole of I-01's repair
// (`core/settings/SessionState.h` states it). `core::Document::refreshFileState` re-reads this after
// every successful save (D3-16), so a capture taken at quit names the bytes that are on disk rather
// than the ones the file had when it was opened.
core::DocumentId identityOf(const app::Services& services, const ui::DocumentWindow& document)
{
    const core::DocumentId* id = services.backend.documentId(document.handle());
    return id != nullptr ? *id : core::DocumentId{};
}

core::SessionDocument captureDocument(const ui::DocumentWindow& document)
{
    core::SessionDocument captured;
    captured.path = document.path();
    captured.splitState = document.splitState();

    const ui::PageView* view = document.view();
    if (view == nullptr) return captured;

    const ui::ViewportController& controller = view->controller();
    captured.layoutMode = controller.layoutMode();
    captured.fit        = controller.fitMode();
    captured.zoom       = controller.zoom().factor;
    captured.pageIndex  = controller.currentPage();
    captured.scrollX    = controller.scroll().x;
    captured.scrollY    = controller.scroll().y;

    // THE ACTIVE PANE'S GUIDES, not every pane's. A guide is a page fact in PDF points and
    // `DocumentWindow` already copies one onto the other panes of a split when it is made
    // (D4-7/D4-13), so the panes agree and reading them all would write the same guide twice.
    const std::vector<core::Guide>& guides = view->guides().guides();
    captured.guides.assign(guides.begin(), guides.end());
    return captured;
}

// A companion window -- `window.new`'s second view of one document (D4-17) -- is NOT captured as a
// document of its own. Restoring it would open a second handle on a file the leader is about to
// open anyway, and the session has no way to say "and this one is a view of that one": the pair is
// a live relationship between two windows rather than a property of either. Recorded here rather
// than silently skipped, because a second view vanishing across a relaunch is a thing somebody will
// otherwise report as a defect.
bool isRestorable(const ui::DocumentWindow& document)
{
    return !document.isCompanion() && !document.isUntitled() && !document.path().isEmpty();
}

} // namespace

SessionShell::SessionShell(core::SessionState& state, core::Settings& settings,
                           const platform::IDisplayTopology* topology, QObject* parent)
    : QObject(parent), state_(state), settings_(settings), topology_(topology)
{
}

void SessionShell::install(QCoreApplication& application)
{
    application.installEventFilter(this);
    connect(&application, &QCoreApplication::aboutToQuit, this, [this]() { captureNow(); });
}

bool SessionShell::eventFilter(QObject* watched, QEvent* event)
{
    // NOT `qobject_cast` first: this filter sees every event in the process, and the type test is a
    // comparison where the cast is a walk up a meta-object chain.
    if (event->type() == QEvent::Close) {
        if (const auto* window = qobject_cast<const ui::MainWindow*>(watched)) {
            // A window is `WA_DeleteOnClose`, and an address is reused. Sweeping the entry when the
            // object dies is what stops a later window at the same address from inheriting it.
            //
            // THE CONNECTION IS MADE ONCE, GUARDED BY THE HASH ITSELF RATHER THAN BY
            // `Qt::UniqueConnection` -- which is not available here at all: uniqueness is only
            // implemented for a pointer-to-member slot, and asking for it with a LAMBDA is a
            // `Q_ASSERT_X` in a debug build and silently no connection at all in a release one.
            // Guide issue **I-09**; it cost this task a fail-fast in `x64-debug` while the release
            // suite was green.
            if (!closing_.contains(window)) {
                connect(window, &QObject::destroyed, this,
                        [this](QObject* dead) { closing_.remove(dead); });
            }

            // AFTER the guard above and BEFORE `MainWindow::closeEvent` -- an application filter
            // runs ahead of the widget's own handler, which is the whole reason the stash is taken
            // here: the handler is what asks about each dirty document and REMOVES the tab.
            closing_.insert(window, captureWindow(*window));
        }
    }
    return QObject::eventFilter(watched, event);
}

core::SessionWindow SessionShell::captureWindow(const ui::MainWindow& window)
{
    core::SessionWindow captured;

    // `normalGeometry()` and not `geometry()`, because a maximized window's geometry IS the screen
    // and restoring that as a normal rectangle would leave a person who un-maximizes with a window
    // exactly the size of their desktop and no way back to the one they had.
    captured.geometry   = window.isMaximized() ? window.normalGeometry() : window.geometry();
    captured.maximized  = window.isMaximized();
    captured.paneLayout = window.paneLayoutState();

    for (int at = 0; at < window.documentCount(); ++at) {
        const ui::DocumentWindow* document = window.documentAt(at);
        if (document == nullptr || !isRestorable(*document)) continue;
        captured.documents.push_back(captureDocument(*document));
    }

    // The active index is a position in the list ABOVE, which skipped the companions and the
    // untitled documents -- so it is re-derived from the kept list rather than taken from the tab
    // bar, which counts them.
    const int active = activeIndexOf(window);
    captured.activeDocument =
        active < static_cast<int>(captured.documents.size()) ? active : 0;
    return captured;
}

QString SessionShell::fingerprintNow() const
{
    if (topology_ == nullptr) return {};
    const core::Result<std::vector<platform::MonitorInfo>> monitors = topology_->monitors();
    if (!monitors) return {};
    return core::SessionState::fingerprintOf(monitors.value());
}

std::vector<QRect> SessionShell::screensNow() const
{
    std::vector<QRect> screens;
    if (topology_ == nullptr) return screens;
    const core::Result<std::vector<platform::MonitorInfo>> monitors = topology_->monitors();
    if (!monitors) return screens;
    screens.reserve(monitors.value().size());
    for (const platform::MonitorInfo& monitor : monitors.value())
        screens.emplace_back(monitor.x, monitor.y, monitor.width, monitor.height);
    return screens;
}

core::Session SessionShell::snapshot() const
{
    core::Session session;
    session.fingerprint = fingerprintNow();

    for (const ui::MainWindow* window : ui::MainWindow::shellWindows()) {
        if (window == nullptr) continue;

        // THE VISIBILITY TEST, and it is the whole of the rule the header states. A hidden window
        // closed; its stash is what it held before the close protocol emptied it. A visible one
        // either never closed or had its close refused, and the stash would be stale.
        const auto stashed = closing_.constFind(window);
        core::SessionWindow captured = (!window->isVisible() && stashed != closing_.constEnd())
                                           ? *stashed
                                           : captureWindow(*window);
        if (captured.documents.empty()) continue;
        session.windows.push_back(std::move(captured));
    }
    return session;
}

void SessionShell::captureNow()
{
    // OFF MEANS NOTHING IS WRITTEN, not "written and ignored on the way back in". See the header:
    // the setting is a person asking the product to stop keeping a list of what they read.
    if (!settings_.get(core::keys::general::restoreSession)) return;

    state_.capture(snapshot());
    if (const core::Result<void> saved = state_.save(); !saved) {
        // A session that could not be written is a convenience lost, never a reason to hold up a
        // quit -- so it is a log line and not a dialog, and the product exits either way.
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "session: not saved --" << saved.error().messageKey << saved.error().context;
    }
}

// ---------------------------------------------------------------------------------------------
// Restore
// ---------------------------------------------------------------------------------------------

namespace {

// Everything one restored tab needs, applied in the order a view can actually take it: the layout
// mode first, because it decides where the pages are; then the zoom or the fit; then the page,
// which scrolls; then the exact offset on top of that.
void applyDocument(const app::Services& services, ui::DocumentWindow& document,
                   const core::SessionDocument& session, bool* guidesDropped)
{
    document.applySplitState(session.splitState);

    ui::PageView* view = document.view();
    if (view == nullptr) return;

    view->setLayoutMode(session.layoutMode);

    // `FitMode::None` is what an explicit zoom looks like; anything else is an INTENT the view
    // re-satisfies against its own width (`core::SessionDocument`'s note says why the pair is
    // stored rather than only the number).
    if (session.fit == core::render::FitMode::None)
        view->setZoom(core::render::Zoom{session.zoom}.clamped());
    else
        view->setFitMode(session.fit);

    view->goToPage(session.pageIndex);

    // THE OFFSET IS DEVICE PIXELS AT THE ZOOM IT WAS CAPTURED AT, applied as a delta because the
    // controller scrolls by one and not to one. **What this does not cover** (convention 19): a
    // window restored at a different size or onto a different scale factor clamps to a different
    // maximum, so the offset lands near rather than exactly where it was -- and the PAGE INDEX
    // above is the anchor that survives that, which is why it is applied first and separately.
    const core::DevicePoint at = view->controller().scroll();
    view->controller().scrollBy(session.scrollX - at.x, session.scrollY - at.y);

    // THE GUIDES, AND I-01'S ONE LIVE CONSEQUENCE. The identity was captured from the engine at
    // quit, so a save during that session cannot orphan them; what CAN still differ is a file
    // somebody else changed between the quit and this launch, and in that case the guides are
    // dropped and the document is not. A guide placed against page 4 of a document that has since
    // been re-paginated is not a guide, it is a line in the wrong place.
    if (session.guides.empty()) return;
    const core::DocumentId now = identityOf(services, document);
    if (session.id.isValid() && now.isValid() && now != session.id) {
        if (guidesDropped != nullptr) *guidesDropped = true;
        RAWPAGE_LOG_INFO(core::LogSubsystem::Ui)
            << "session: the file changed since it was captured; its guides are not restored:"
            << session.path;
        return;
    }
    if (ui::SplitViewContainer* split = document.split()) {
        for (ui::PageView* pane : split->views()) pane->guides().setGuides(session.guides);
    } else {
        view->guides().setGuides(session.guides);
    }
}

} // namespace

SessionShell::Restored SessionShell::restore(ui::MainWindow& first, const Services& services)
{
    Restored outcome;
    if (!settings_.get(core::keys::general::restoreSession)) {
        outcome.disabled = true;
        return outcome;
    }

    const bool rememberWindows = settings_.get(core::keys::general::rememberWindowLayout);
    const bool rememberPanes   = settings_.get(core::keys::general::rememberPaneLayout);

    const core::SessionState::Restored session = state_.restore(fingerprintNow(), screensNow());
    outcome.droppedPaths            = session.droppedPaths;
    outcome.clamped                 = session.clamped;
    outcome.usedFallbackArrangement = session.usedFallbackArrangement;

    // WHAT IS ALREADY OPEN, TAKEN ONCE AND BEFORE ANYTHING IS OPENED -- `RecoveryPrompt`'s
    // documents and the command line's. Re-asking each window per path would give the same answer
    // and would give a different one halfway through, as this loop opens things.
    QStringList alreadyOpen;
    for (const ui::MainWindow* window : ui::MainWindow::shellWindows()) {
        if (window == nullptr) continue;
        for (int at = 0; at < window->documentCount(); ++at) {
            const ui::DocumentWindow* document = window->documentAt(at);
            if (document == nullptr) continue;

            // `sourcePath()` and not `path()`: a document recovered into an UNTITLED tab has no
            // path of its own and its bytes are the source file's (Stage 3 T11). Opening that
            // source again beside it is exactly the "twice" this skip exists to prevent.
            const QString open = document->path().isEmpty() ? document->sourcePath()
                                                            : document->path();
            if (!open.isEmpty()) alreadyOpen << QFileInfo(open).absoluteFilePath();
        }
    }

    ui::MainWindow* target = &first;
    for (const core::SessionWindow& window : session.windows) {
        if (target == nullptr) {
            // A second window is made the way a tear-off makes one, and owns itself for the same
            // reason: it is a window the user closes for good rather than one `main.cpp` holds.
            target = new ui::MainWindow(services);
            target->setAttribute(Qt::WA_DeleteOnClose);
        }

        int opened = 0;
        for (const core::SessionDocument& document : window.documents) {
            const QString path = QFileInfo(document.path).absoluteFilePath();
            if (alreadyOpen.contains(path)) {
                outcome.skipped.push_back(document.path);
                continue;
            }

            target->openFiles({path});
            const int at = target->indexOfPath(path);
            if (at < 0) continue;   // the backend refused it; the window has already said so

            alreadyOpen << path;
            ++opened;
            ++outcome.documents;

            bool guidesDropped = false;
            if (ui::DocumentWindow* tab = target->documentAt(at))
                applyDocument(services, *tab, document, &guidesDropped);
            if (guidesDropped) ++outcome.guidesDropped;
        }

        // AN EMPTY WINDOW IS NOT SHOWN. Every document it held was dropped, or is already open in
        // the window recovery put it in; a bare shell beside that one would be a window the person
        // has to close to find out it was never theirs.
        if (opened == 0 && target != &first) {
            delete target;
            target = nullptr;
            continue;
        }

        // `rememberPaneLayout` off restores NO panes rather than leaving the window as built, which
        // opens with the sidebar showing -- `MainWindow::applyPaneLayoutState` states why an empty
        // string is a layout rather than an absence.
        target->applyPaneLayoutState(rememberPanes ? window.paneLayout : QString());

        if (rememberWindows && !window.geometry.isEmpty()) {
            target->setGeometry(window.geometry);
            if (window.maximized) target->showMaximized();
        }
        if (opened > 0) target->activateDocument(window.activeDocument);

        if (target != &first) target->show();
        ++outcome.windows;
        target = nullptr;
    }

    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "session: restored" << outcome.documents << "document(s) in" << outcome.windows
        << "window(s);" << static_cast<int>(outcome.skipped.size()) << "already open,"
        << static_cast<int>(outcome.droppedPaths.size()) << "no longer there"
        << (outcome.usedFallbackArrangement ? "; geometry from a different monitor arrangement"
                                            : "")
        << (outcome.clamped ? "; a window was clamped onto the desktop" : "");
    return outcome;
}

} // namespace rawpage::app
