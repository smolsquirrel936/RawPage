#include "ui/shell/MainWindow.h"

#include "core/Log.h"
// T9: `openFiles()` asks the backend whether a file is a PDF (guide case A-47). It is a `core/`
// header, so it widens nothing -- convention 9's list of what `ui/` looks OUTWARD to is unchanged.
#include "core/backend.h"
#include "core/cmd/ActionRegistry.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/panes/LogPane.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentTabBar.h"
#include "ui/dialogs/PasswordDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MessageBar.h"
#include "ui/viewer/PageView.h"
#include "ui/shell/QuickAccessToolbar.h"
#include "ui/shell/Ribbon.h"
#include "ui/shell/RibbonModel.h"
#include "ui/shell/RibbonTabBuilder.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/SidebarDock.h"
#include "ui/shell/StatusBar.h"
#include "ui/shell/ThemeManager.h"
#include "ui/shell/ToolFinder.h"
#include "ui/shell/UiCustomization.h"

#include <QEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QGuiApplication>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QShowEvent>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace rawpage::ui {
namespace {

using core::ActionId;
using core::KeyContext;
using core::LogSubsystem;

QToolButton* makeEdgeStrip(QWidget* parent, const QString& text, const QString& tooltip)
{
    auto* strip = new QToolButton(parent);
    strip->setText(text);
    strip->setToolTip(tooltip);
    strip->setAutoRaise(true);
    strip->setMaximumWidth(24);
    strip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    strip->hide();
    return strip;
}

// Qt has no "draw this button's label rotated", and a custom paintEvent for a 24-pixel strip is a
// DPI-scaling bug waiting to be written. One character per line is what the Windows shell does for
// the same control, and it is correct at every scale factor for free.
QString verticalize(const QString& text)
{
    QStringList characters;
    characters.reserve(text.size());
    for (const QChar character : text) characters << QString(character);
    return characters.join(QLatin1Char('\n'));
}

// THE WINDOW SET. File-scope rather than a member, because "which window is active" is a fact
// about the application and not about any one window -- and because a static member would put the
// same two variables in the class every window then has to keep in step. Creation order is the
// order `Ctrl+F6` walks, which is what a person expects: the window you made second is the second
// one you reach.
QList<MainWindow*>& shellWindowSet()
{
    static QList<MainWindow*> windows;
    return windows;
}

MainWindow* g_activeShell = nullptr;

// WHETHER THERE IS ANYBODY TO ASK. A modal file picker needs a person in front of it; under
// `QT_QPA_PLATFORM=offscreen` there is none, so the dialog is built, shown to nobody, and never
// returns. That is not a hypothetical -- it is a ten-minute CI hang that happened the first time a
// test dispatched `file.open` after T12 registered it (issue I-23).
//
// It is an ENABLEMENT PREDICATE rather than a check inside the handler, because the registry
// already has the vocabulary for exactly this: `dispatch` refuses a disabled command with
// `ErrorCode::Unavailable`, distinct from the `NotFound` of a command that does not exist. A
// headless caller -- a test today, Stage 14's batch runner tomorrow -- gets a clean, distinguishable
// refusal instead of a process that stops. The parameterized form of "open this file" is
// `MainWindow::openFiles`, which is what flow B and the CLI already use and which needs no user.
bool canPromptTheUser()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

} // namespace

const QList<MainWindow*>& MainWindow::shellWindows() { return shellWindowSet(); }

MainWindow* MainWindow::activeShell()
{
    // The last window activated, if it is still alive. The fallback is the last window CREATED
    // rather than the first: a command dispatched immediately after a tear-off belongs to the
    // window the user is now looking at.
    if (g_activeShell && shellWindowSet().contains(g_activeShell)) return g_activeShell;
    return shellWindowSet().isEmpty() ? nullptr : shellWindowSet().last();
}

void MainWindow::stepWindow(int delta)
{
    const QList<MainWindow*>& windows = shellWindowSet();
    if (windows.size() < 2) return;

    const int from = windows.indexOf(activeShell());
    if (from < 0) return;

    const int size = windows.size();
    const int to   = ((from + delta) % size + size) % size;   // wraps in both directions
    windows.at(to)->raiseToForeground();
}

MainWindow::MainWindow(const app::Services& services, QWidget* parent)
    : QMainWindow(parent), services_(services)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(catalog::translate(catalog::key::WindowTitle));
    resize(1280, 820);

    // Joins the set BEFORE anything else, because `registerActions()` below may be the first
    // registration in this registry and its handlers resolve `activeShell()` -- which has to be
    // able to answer with this window from the moment it exists.
    shellWindowSet().append(this);
    g_activeShell = this;

    theme_ = std::make_unique<ThemeManager>(services_.settings, this);
    theme_->apply();

    // THE KEYBOARD -- T13. Before `registerActions()` and before the ribbon, because the action
    // factory built there asks it which §8 context a dispatch happened in, and because a window
    // that is shown before its binder exists is a window with a dead keyboard for those frames.
    shortcuts_ = std::make_unique<ShortcutBinder>(services_.actions, services_.keyBindings, this);

    status_ = new StatusBar(this);
    setStatusBar(status_);

    // The page box has been an input with no consumer since T9. This is the consumer -- and it goes
    // through `goToPage`, which is the same path the `nav.goToPage` command takes, rather than
    // through a second implementation that could scroll somewhere slightly different.
    connect(status_, &StatusBar::pageRequested, this, [this](int oneBased) {
        if (PageView* view = currentView()) view->goToPage(oneBased - 1);
    });

    sidebar_ = new SidebarDock(services_.actions, this);
    addDockWidget(Qt::LeftDockWidgetArea, sidebar_);
    resizeDocks({sidebar_}, {sidebar_->rememberedWidth()}, Qt::Horizontal);

    // THE LOG PANE -- T14. Its own dock along the bottom rather than a pane in the sidebar: the
    // sidebar's subject is the document and the log's is the application. Hidden, because a shell
    // that opens with its log showing looks like a shell in trouble.
    logPane_ = new LogPane(services_.logBuffer, this);
    addDockWidget(Qt::BottomDockWidgetArea, logPane_);
    logPane_->hide();

    // ORDER MATTERS, and not for a cosmetic reason. `ActionFactory` reads each action's
    // registration state at the moment it builds the button, so anything this window registers has
    // to be registered BEFORE the ribbon is built -- otherwise the shell's own commands render as
    // permanently disabled "not built yet" buttons, which is the honest state for the other ~355
    // ids and a lie about these three.
    registerActions();
    buildHeader();
    buildCentralSurface();
    buildAutoHideStrips();

    // AFTER the header, because it is the header's widgets it restores into -- and after
    // registerActions(), because a restored QAT command is rendered by the same factory that reads
    // registration state as it builds a button.
    loadUiCustomization();

    connect(sidebar_, &SidebarDock::autoHiddenChanged, this, [this]() { updateAutoHideStrips(); });
    connect(sidebar_, &QDockWidget::dockLocationChanged, this, [this]() { updateAutoHideStrips(); });
    connect(sidebar_, &QDockWidget::visibilityChanged, this, [this]() { updateAutoHideStrips(); });
    updateAutoHideStrips();
}

MainWindow::~MainWindow()
{
    // THE KEYBOARD GOES FIRST. The binder filters the APPLICATION's events, so it is reached from
    // outside this object; leaving it installed while ~QWidget takes the window apart is the same
    // hazard as I-12's signal connection, arriving through a different door. Releasing it here
    // removes the filter while `this` is still a whole MainWindow.
    shortcuts_.reset();

    // AND THE STRIP FILTERS WITH IT, for the same reason. `~QWidget` hides this window's children,
    // and an event delivered to a strip while `this` is a half-destroyed MainWindow would dispatch
    // `eventFilter` on an object whose derived part is gone -- issue I-12's hazard reached through
    // the event system rather than through a signal.
    if (leftStrip_) leftStrip_->removeEventFilter(this);
    if (rightStrip_) rightStrip_->removeEventFilter(this);

    // NOT `= default`, and the reason is a real crash rather than a precaution.
    //
    // The lambdas below are connected to the SIDEBAR's signals with `this` as the receiver, and Qt
    // drops such a connection when the receiver's QObject part is destroyed. That happens LAST --
    // after ~MainWindow has run. In between, ~QWidget closes the window, which hides its children,
    // which makes QDockWidget emit visibilityChanged, which calls a MainWindow member function on
    // an object whose MainWindow part no longer exists. Offscreen it segfaults inside
    // qt_metacall; on a real desktop it would be whatever the freed memory happened to contain.
    //
    // Found by tests/ui on its first run (issue I-12), which is the same way T7 found its pipe
    // race: by running the thing, not by reading it.
    disconnect(sidebar_, nullptr, this, nullptr);

    // The same reasoning for T11's three connections. `~QWidget` takes the header down with the
    // window, and a QAT rebuild or a placement change reaching a half-destroyed MainWindow would be
    // I-12 a second time -- the class of bug that is invisible until the day it is not.
    if (qat_) disconnect(qat_, nullptr, this, nullptr);
    if (ribbon_) disconnect(ribbon_, nullptr, this, nullptr);
    if (tabs_) disconnect(tabs_, nullptr, this, nullptr);

    // OUT OF THE SET, and out of `g_activeShell` with it. The registry outlives every window and
    // holds handlers that resolve `activeShell()` at dispatch time; a destroyed window left in the
    // set would be dispatched into. This is the same class of bug as I-12 -- an object reached
    // after its destructor ran -- arriving through a different door.
    shellWindowSet().removeAll(this);
    if (g_activeShell == this)
        g_activeShell = shellWindowSet().isEmpty() ? nullptr : shellWindowSet().last();
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::ActivationChange && isActiveWindow()) g_activeShell = this;
}

void MainWindow::buildHeader()
{
    actionFactory_ = std::make_unique<ActionFactory>(
        services_.actions,
        // The context every ribbon dispatch is attributed to, computed from the focus chain by
        // `ShortcutBinder::activeContext()` -- T13, and it was one line exactly as the callback was
        // shaped to make it. Capturing `this` is safe here in a way it is NOT in a registry handler
        // (see the class comment): the factory is owned by this window and dies with it, whereas a
        // registration outlives every window that made it.
        [this] { return shortcuts_->activeContext(); });

    // setMenuWidget, not a QToolBar in the top dock area. A ribbon REPLACES the menu bar rather than
    // sitting under one, and the menu-widget slot is the only place QMainWindow puts a widget above
    // the dock areas -- so a docked pane cannot end up above the ribbon, which it can if the ribbon
    // is a toolbar. At T11 the slot holds a column rather than the ribbon alone, because the Quick
    // Access Toolbar has to be inside the same slot to be above or below the ribbon without a pane
    // ever landing between them.
    header_ = new QWidget(this);
    header_->setObjectName(QStringLiteral("shellHeader"));
    headerColumn_ = new QVBoxLayout(header_);
    headerColumn_->setContentsMargins(0, 0, 0, 0);
    headerColumn_->setSpacing(0);

    ribbon_ = new Ribbon(header_);
    headerColumn_->addWidget(ribbon_);

    qat_ = new QuickAccessToolbar(*actionFactory_, header_);
    applyQatPlacement();

    // Persist on every change rather than at shutdown. `Settings::sync()` runs at teardown
    // (T8/I-14), but a customization that only reaches the file if the process exits cleanly is a
    // customization a crash takes with it -- and this is the stage that built a crash handler.
    connect(qat_, &QuickAccessToolbar::commandsChanged, this, [this]() { saveUiCustomization(); });
    connect(qat_, &QuickAccessToolbar::lockedChanged, this, [this](bool) { saveUiCustomization(); });
    connect(qat_, &QuickAccessToolbar::placementChanged, this, [this](bool) {
        applyQatPlacement();
        saveUiCustomization();
    });

    // The tool finder rides in the tab strip's right-hand corner, which is where the reference
    // (P-4) puts it and where it does not compete with the tabs for the left edge.
    toolFinder_ = new ToolFinder(services_.actions, services_.keyBindings);
    ribbon_->setCornerWidget(toolFinder_);

    // Right-click a ribbon control -> offer to put THAT command on the toolbar. The ribbon knows
    // which control; the toolbar knows what to do with it; neither knows about the other. See
    // QuickAccessToolbar.h on the parameterized-command question this is the Stage 1 answer to.
    connect(ribbon_, &Ribbon::controlContextMenuRequested, this,
            [this](core::ActionId id, const QPoint& globalPos) {
                if (qat_) qat_->showAddMenu(globalPos, id);
            });

    setMenuWidget(header_);

    RibbonTabBuilder builder(*ribbon_, *actionFactory_);
    const QStringList problems = builder.build(ribbon::shipped());
    for (const QString& problem : problems)
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "ribbon:" << problem;

    ribbon_->setCurrentPage(0);
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);

    // D1-7(b), guide case A-54. Every action on this window must be a RawPageAction with exactly one
    // receiver on `triggered`, and that receiver dispatches through ActionRegistry.
    //
    // The two severities are deliberate. In a debug build a developer is present and an assert is
    // the fastest possible feedback; in a release build, killing a user's session over a wiring
    // mistake would be a worse failure than the mistake. Neither is silence.
    const QList<QString> complaints = ActionFactory::wiringComplaints(this);
    for (const QString& complaint : complaints)
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "action wiring:" << complaint;
    Q_ASSERT_X(complaints.isEmpty(), "MainWindow::showEvent",
               "an action on this window bypasses ActionRegistry -- see the log lines above");

    // T13's half of the same rule. A `QAction` carrying its own key sequence, or a free-standing
    // `QShortcut`, resolves its key through Qt against no context and no user remap -- it works,
    // and it makes §8's per-context bindings a document about something the product does not do.
    const QList<QString> keyComplaints = ShortcutBinder::shortcutComplaints(this);
    for (const QString& complaint : keyComplaints)
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui) << "shortcut wiring:" << complaint;
    Q_ASSERT_X(keyComplaints.isEmpty(), "MainWindow::showEvent",
               "a chord on this window bypasses ShortcutBinder -- see the log lines above");
}

void MainWindow::buildCentralSurface()
{
    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("centralSurface"));

    auto* row = new QHBoxLayout(central);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    const QString stripLabel = verticalize(catalog::translate(catalog::key::SidebarTitle));

    leftStrip_ = makeEdgeStrip(central, stripLabel, catalog::translate(catalog::key::SidebarShow));
    leftStrip_->setObjectName(QStringLiteral("sidebarStripLeft"));
    row->addWidget(leftStrip_);

    auto* body = new QFrame;
    body->setObjectName(QStringLiteral("placeholderSurface"));
    body->setFrameShape(QFrame::NoFrame);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(48, 48, 48, 48);
    column->setSpacing(12);
    column->addStretch(1);

    auto* heading = new QLabel(catalog::translate(catalog::key::PlaceholderHeading), body);
    heading->setObjectName(QStringLiteral("placeholderHeading"));
    QFont headingFont = heading->font();
    headingFont.setPointSizeF(headingFont.pointSizeF() * 2.0);
    heading->setFont(headingFont);
    heading->setAlignment(Qt::AlignHCenter);
    column->addWidget(heading);

    auto* bodyText = new QLabel(catalog::translate(catalog::key::PlaceholderBody), body);
    bodyText->setObjectName(QStringLiteral("placeholderBody"));
    bodyText->setWordWrap(true);
    bodyText->setAlignment(Qt::AlignHCenter);
    column->addWidget(bodyText);

    // What this build's OS bundle cannot do, named rather than implied. platform/platform.h makes
    // an unimplemented interface a NULL member precisely so that this is printable instead of being
    // discovered by a caller getting no result -- so the shell prints it.
    const std::vector<std::string_view> missing =
        platform::unimplementedInterfaces(services_.platform);
    auto* interfaces = new QLabel(body);
    interfaces->setObjectName(QStringLiteral("placeholderInterfaces"));
    interfaces->setAlignment(Qt::AlignHCenter);
    interfaces->setText(catalog::translate(catalog::key::PlaceholderInterfaces)
                            .arg(static_cast<int>(platform::kPlatformInterfaceCount - missing.size()))
                            .arg(static_cast<int>(platform::kPlatformInterfaceCount)));
    column->addWidget(interfaces);

    column->addStretch(2);
    placeholder_ = body;

    // THE DOCUMENT COLUMN -- T12. The tab strip over a two-page stack: the placeholder when there
    // is nothing open, the documents when there is. Two stacks rather than one with the placeholder
    // at index 0, so that a document's index in the strip and its index in the stack are the same
    // number -- arithmetic that has to be written twice is arithmetic that goes wrong quietly.
    auto* documentColumn = new QWidget(central);
    documentColumn->setObjectName(QStringLiteral("documentColumn"));
    auto* documentLayout = new QVBoxLayout(documentColumn);
    documentLayout->setContentsMargins(0, 0, 0, 0);
    documentLayout->setSpacing(0);

    tabs_ = new DocumentTabBar(documentColumn);
    documentLayout->addWidget(tabs_);

    // BETWEEN THE TAB STRIP AND THE PAGE (S2 T14). Above the strip it would be a statement about
    // the window, and it is a statement about the document in front -- which is also why
    // `updateDocumentChrome()` clears it on every tab change. It hides itself when empty.
    messageBar_ = new MessageBar(documentColumn);
    documentLayout->addWidget(messageBar_);

    // THE BANNER'S COLOURS COME FROM THIS WINDOW'S THEME, and follow it: `themeChanged` fires only
    // when the RESOLVED theme moves, which is the only thing the two tokens depend on (issue I-29).
    const auto paintMessageBar = [this] {
        messageBar_->setColors(theme_->color(ThemeToken::MessageBackground),
                               theme_->color(ThemeToken::MessageText));
        messageBar_->setFailureColors(theme_->color(ThemeToken::ErrorBackground),
                                      theme_->color(ThemeToken::ErrorText));
    };
    paintMessageBar();
    connect(theme_.get(), &ThemeManager::themeChanged, messageBar_, paintMessageBar);

    documents_ = new QStackedWidget;
    documents_->setObjectName(QStringLiteral("documentStack"));

    body_ = new QStackedWidget(documentColumn);
    body_->setObjectName(QStringLiteral("shellBody"));
    body_->addWidget(placeholder_);   // page 0
    body_->addWidget(documents_);     // page 1
    documentLayout->addWidget(body_, 1);

    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && index < documents_->count()) documents_->setCurrentIndex(index);
        updateDocumentChrome();
    });
    connect(tabs_, &DocumentTabBar::closeRequested, this, [this](int index) { closeDocument(index); });
    connect(tabs_, &QTabBar::tabMoved, this, [this](int from, int to) {
        // The strip reordered itself; the stack has to follow, or the tab at position 3 shows the
        // document that used to be there. QStackedWidget has no move, so the widget is taken out
        // and put back -- `removeWidget` does not delete it, which is what this relies on.
        QWidget* moved = documents_->widget(from);
        if (!moved) return;
        documents_->removeWidget(moved);
        documents_->insertWidget(to, moved);
        documents_->setCurrentIndex(tabs_->currentIndex());
    });
    connect(tabs_, &DocumentTabBar::tearOffRequested, this,
            [this](int index, const QPoint& globalPos) { detachDocument(index, globalPos); });

    row->addWidget(documentColumn, 1);

    rightStrip_ = makeEdgeStrip(central, stripLabel, catalog::translate(catalog::key::SidebarShow));
    rightStrip_->setObjectName(QStringLiteral("sidebarStripRight"));
    row->addWidget(rightStrip_);

    setCentralWidget(central);
    updateDocumentChrome();
}

// =======================================================================================
// Documents -- T12. Every one of them a stub (D1-2): a path, a name, and a placeholder.
// =======================================================================================

int MainWindow::documentCount() const { return documents_ ? documents_->count() : 0; }

DocumentWindow* MainWindow::documentAt(int index) const
{
    if (!documents_ || index < 0 || index >= documents_->count()) return nullptr;
    return qobject_cast<DocumentWindow*>(documents_->widget(index));
}

DocumentWindow* MainWindow::currentDocument() const
{
    return tabs_ ? documentAt(tabs_->currentIndex()) : nullptr;
}

int MainWindow::indexOfPath(const QString& absolutePath) const
{
    for (int index = 0; index < documentCount(); ++index)
        if (DocumentWindow* document = documentAt(index))
            if (document->path() == absolutePath) return index;
    return -1;
}

DocumentWindow* MainWindow::openDocument(const QString& absolutePath, core::DocumentHandle handle)
{
    if (const int existing = indexOfPath(absolutePath); existing >= 0) {
        // The same path twice in one window would give the user two identical tabs and no way to
        // tell them apart. Focusing the one already there is what every editor does.
        //
        // `openFiles()` checks for this BEFORE it asks the backend, so this branch is reached only
        // by a direct caller -- and a handle arriving here would leak, since nothing would ever own
        // it. Closing it is the honest recovery rather than an assertion nobody would see.
        if (handle.isValid()) {
            RAWPAGE_LOG_DEBUG(LogSubsystem::Ui)
                << "open: already open in this window; releasing the duplicate handle for"
                << absolutePath;
            services_.backend.close(handle);
        }
        activateDocument(existing);
        return documentAt(existing);
    }

    auto* document = new DocumentWindow(services_, absolutePath, handle);
    adoptDocument(document);
    return document;
}

void MainWindow::adoptDocument(DocumentWindow* document, int index)
{
    if (!document || !documents_ || !tabs_) return;

    // THE VIEW TELLS THE CHROME, and it has to: the status bar's page number changes when the user
    // SCROLLS, which no command and no tab change goes through. Without this the numbers were
    // correct exactly once, when the tab was adopted, and then stayed there while the document
    // moved under them -- which is worse than the em dash, because it is a confident wrong answer.
    //
    // Connected here rather than in `DocumentWindow` because a tab is re-adopted by another window
    // on a re-dock, and this window is the one whose status bar must follow.
    //
    // DISCONNECT THEN CONNECT, and not `Qt::UniqueConnection`: **Qt refuses a unique connection to
    // a lambda** -- "Unique connection requires the slot to be a pointer to a member function of a
    // QObject subclass" -- and it refuses it with an `ASSERT` that fail-fasts the process rather
    // than with a warning, so the mistake reads as 42 unrelated suites dying at once. The explicit
    // disconnect does the same job and says what it is doing.
    if (PageView* view = document->view()) {
        disconnect(view, &PageView::viewChanged, this, nullptr);
        connect(view, &PageView::viewChanged, this,
                [this](int, int, double) { updateViewChrome(); });
    }

    const int at = (index < 0 || index > documentCount()) ? documentCount() : index;
    documents_->insertWidget(at, document);
    tabs_->insertTab(at, document->title());
    tabs_->setTabToolTip(at, document->path());
    tabs_->setCurrentIndex(at);
    documents_->setCurrentIndex(at);
    updateDocumentChrome();
}

DocumentWindow* MainWindow::releaseDocument(int index)
{
    DocumentWindow* document = documentAt(index);
    if (!document) return nullptr;

    // REMOVED, NOT DELETED -- and reparented to nothing, so the caller genuinely owns it. A tab
    // moved between windows keeps its identity; at Stage 2 that identity is a document, a journal
    // and a scroll position, and a move that destroyed and rebuilt it would discard all three.
    documents_->removeWidget(document);
    document->setParent(nullptr);
    tabs_->removeTab(index);
    updateDocumentChrome();
    return document;
}

void MainWindow::closeIfEmptied()
{
    if (documentCount() > 0) return;

    // ONLY A WINDOW THAT OWNS ITSELF CAN GO. `WA_DeleteOnClose` is set on every window this class
    // creates -- a tear-off, and `window.new` -- and on no other. The PRIMARY window is a stack
    // object in `app/main.cpp`, which Qt may not delete and which `close()` would merely hide,
    // leaving a hidden entry in the window set for `Ctrl+F6` to walk into and for a single-instance
    // forward to call `openFiles()` on. So it stays, and that is not a shortfall: an empty primary
    // window is exactly the state a launch with no files produces, which is a place the product is
    // designed to sit.
    if (!testAttribute(Qt::WA_DeleteOnClose)) return;

    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "window closing: its last document was moved to another";

    // `close()` on a `WA_DeleteOnClose` window posts a DeferredDelete rather than destroying now,
    // which is what makes this safe from here: this call is reached from `DocumentTabBar`'s own
    // mouse handler, by way of a signal, and destroying the strip inside its own event would leave
    // that handler returning through freed memory. The event loop does it once the stack is gone.
    close();
}

void MainWindow::closeDocument(int index)
{
    if (DocumentWindow* document = releaseDocument(index)) document->deleteLater();
}

void MainWindow::closeAllDocuments()
{
    while (documentCount() > 0) closeDocument(0);
}

void MainWindow::activateDocument(int index)
{
    if (!tabs_ || index < 0 || index >= documentCount()) return;
    tabs_->setCurrentIndex(index);
}

void MainWindow::stepDocument(int delta)
{
    const int count = documentCount();
    if (count < 2) return;
    const int from = tabs_->currentIndex();
    activateDocument(((from + delta) % count + count) % count);
}

MainWindow* MainWindow::detachDocument(int index, const QPoint& globalPos)
{
    DocumentWindow* document = documentAt(index);
    if (!document) return nullptr;

    // DROPPED ON ANOTHER WINDOW'S STRIP, if that is where the pointer is. One gesture, two
    // outcomes, and the tab strip cannot tell them apart because it knows only its own tabs -- see
    // DocumentTabBar's class comment.
    for (MainWindow* other : shellWindowSet()) {
        if (other == this || !other->tabs_) continue;
        const QPoint local = other->tabs_->mapFromGlobal(globalPos);
        if (!other->tabs_->rect().contains(local)) continue;

        const int at = other->tabs_->tabAt(local);
        other->adoptDocument(releaseDocument(index), at);
        other->raiseToForeground();
        closeIfEmptied();   // I-35 -- nothing below this line may touch `this`
        return other;
    }

    // A WINDOW'S ONLY TAB HAS NOWHERE TO GO (issue I-33). Detaching it into a new window is a null
    // move: the same lone document ends up alone in a window either way, and the user is left with
    // a second, empty shell to close. Refuse, and the drag simply does nothing.
    //
    // THE PLACEMENT IS THE POINT -- this sits AFTER the loop above, not before it. A torn-off
    // window holds exactly one document by construction, so guarding at the top of this function
    // would make dragging it back the one re-dock nobody could perform, which is the commonest one
    // there is and a required step of M-03.
    if (documentCount() <= 1) {
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "tear-off refused: this window's only document has nowhere to go";
        return nullptr;
    }

    auto* window = new MainWindow(services_);
    window->setAttribute(Qt::WA_DeleteOnClose);   // a torn-off window closes for good
    window->adoptDocument(releaseDocument(index));
    if (!globalPos.isNull()) window->move(globalPos - QPoint(80, 20));
    window->show();
    window->raiseToForeground();
    return window;
}

void MainWindow::updateDocumentChrome()
{
    if (!tabs_ || !body_) return;

    const int count = documentCount();
    tabs_->setVisible(count > 0);
    body_->setCurrentIndex(count > 0 ? 1 : 0);

    DocumentWindow* current = currentDocument();
    setWindowTitle(current ? catalog::translate(catalog::key::WindowTitleWithDocument)
                                 .arg(current->title())
                           : catalog::translate(catalog::key::WindowTitle));

    if (status_) {
        // The status bar was built DISABLED at T9 because there was no document, and it stayed
        // that way through T13 because a page on screen is not the same as a page COUNTED. It is
        // counted now: `updateViewChrome()` below fills in the real numbers, and the em dash comes
        // back when the last tab closes rather than being replaced by a guess.
        status_->setDocumentPresent(current != nullptr);
        status_->setMessage(current ? current->title()
                                    : catalog::translate(catalog::key::StatusNoDocument));
    }

    if (messageBar_) {
        // THE BANNER BELONGS TO THE TAB, not to the window. A "this file was repaired" banner left
        // on screen over the document that replaced it is worse than no banner: it is a false
        // statement about the file the user is now looking at, and Stage 3 forces a Save-As on
        // exactly that statement.
        messageBar_->clearDocumentConditions();
        if (current) showDocumentConditions(current);
    }

    updateViewChrome();
}

void MainWindow::promptForZoom()
{
    PageView* view = currentView();
    if (!view) return;

    // THE DIALOG COLLECTS A PARAMETER AND NOTHING ELSE -- the standing rule about dialogs, and the
    // reason this is four lines. `QInputDialog` supplies its own translated chrome; the prompt is
    // ours and comes from the action's own label, so the box and the command cannot disagree.
    bool accepted = false;
    const int percent = QInputDialog::getInt(
        this, catalog::translateAction(core::ActionId::ViewZoomTo),
        catalog::translateAction(core::ActionId::ViewZoomTo),
        qRound(view->controller().zoom().factor * 100.0),
        qRound(core::render::Zoom::kMin * 100.0), qRound(core::render::Zoom::kMax * 100.0), 1,
        &accepted);
    if (!accepted) return;

    view->setZoom(core::render::Zoom::percent(percent));
    updateViewChrome();
}

void MainWindow::promptForPage()
{
    PageView* view = currentView();
    if (!view || view->pageCount() <= 0) return;

    bool accepted = false;
    const int page = QInputDialog::getInt(
        this, catalog::translateAction(core::ActionId::NavGoToPage),
        catalog::translateAction(core::ActionId::NavGoToPage),
        view->controller().currentPage() + 1, 1, view->pageCount(), 1, &accepted);
    if (!accepted) return;

    // ONE-BASED AS TYPED, zero-based in the model -- the same conversion the status bar's page box
    // makes, and it is here rather than inside `goToPage` so there is exactly one convention in
    // `core/` and exactly one place each surface converts.
    view->goToPage(page - 1);
    updateViewChrome();
}

void MainWindow::registerViewerActions()
{
    // THE §8 VIEWER TABLE, or the subset `Implementation-Plan.md` § Stage 2 lists. The rest of that
    // table -- `Ctrl+4` Reflow, `Ctrl+R` rotate PAGES, `S`, `Ctrl+B`, `Ctrl+E`, `Ctrl+L`, `Ctrl+U`,
    // `Ctrl+'`, `Ctrl+;`, `Ctrl+Alt+C`, `Ctrl+.`/`Ctrl+,`, `Ctrl+]`/`Ctrl+[` -- belongs to Stages 3
    // to 13 and stays `NotFound`, which deliberately leaves the key unconsumed. That list is in
    // `Stage2-Rendering-Plan.md` §10 and is not re-decided here.
    //
    // EVERY HANDLER RESOLVES THE VIEW AT DISPATCH TIME, for convention 14's reason in its sharpest
    // form yet: a handler capturing a `PageView` would act on a tab that has since been closed or
    // torn off into a different window, and the registry is one object shared by every window.
    const auto registerView = [this](ActionId id, void (PageView::*method)(),
                                     core::ActionRegistry::Enable enable = {},
                                     std::string_view unavailableKey = {}) {
        if (services_.actions.isRegistered(id)) return;

        core::ActionRegistry::Registration registration;
        registration.handler = [method](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            if (PageView* view = window->currentView()) {
                (view->*method)();
                window->updateViewChrome();
            }
        };
        registration.enable = std::move(enable);
        registration.unavailableKey = unavailableKey;

        const core::Result<void> registered =
            services_.actions.register_(id, std::move(registration));
        if (registered) return;
        const std::string_view name = core::actionStringId(id);
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "viewer action registration refused for"
            << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()))
            << "--" << registered.error().messageKey;
    };

    // A view is the precondition for every one of these. It is also I-53's SUBJECT: with no
    // document these are registered and refused, which is the kind of disabled that said nothing at
    // all until this task -- and now says "needs a document to be open" rather than the generic
    // sentence, because the registration names a reason.
    const auto hasView = [] {
        MainWindow* window = activeShell();
        return window != nullptr && window->currentView() != nullptr
               && window->currentView()->pageCount() > 0;
    };

    // --- Zoom -----------------------------------------------------------------------------------
    registerView(ActionId::ViewZoomIn, &PageView::zoomIn, hasView,
                 "shell.tooltip.needsDocument");
    registerView(ActionId::ViewZoomOut, &PageView::zoomOut, hasView,
                 "shell.tooltip.needsDocument");

    const auto registerFit = [this, hasView](ActionId id, FitMode fit) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = [fit](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            if (PageView* view = window->currentView()) {
                view->setFitMode(fit);
                view->refresh();
                window->updateViewChrome();
            }
        };
        registration.enable = hasView;
        registration.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(id, std::move(registration));
    };

    registerFit(ActionId::ViewFitPage, FitMode::Page);
    registerFit(ActionId::ViewFitWidth, FitMode::Width);
    registerFit(ActionId::ViewFitVisible, FitMode::Visible);

    // ACTUAL SIZE IS NOT A FIT. It is 100 % at the monitor's PHYSICAL DPI -- a ruler on the glass
    // agreeing with the ruler on the page -- which is what `DpiPolicy` takes three inputs for, and
    // it is a fixed zoom rather than a mode that survives a resize.
    if (!services_.actions.isRegistered(ActionId::ViewActualSize)) {
        core::ActionRegistry::Registration actualSize;
        actualSize.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            if (PageView* view = window->currentView()) {
                view->setZoom(core::render::Zoom::actualSize());
                window->updateViewChrome();
            }
        };
        actualSize.enable = hasView;
        actualSize.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(ActionId::ViewActualSize, std::move(actualSize));
    }

    // `view.zoomTo` raises a dialog, so it is DISABLED WHERE THERE IS NO USER -- convention 16, and
    // Stage 1's I-23: a modal `exec()` under the offscreen platform is shown to nobody and never
    // returns, which stopped a whole `ctest` run dead with no failure and no output.
    if (!services_.actions.isRegistered(ActionId::ViewZoomTo)) {
        core::ActionRegistry::Registration zoomTo;
        zoomTo.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            window->promptForZoom();
        };
        zoomTo.enable = [hasView] { return hasView() && canPromptTheUser(); };
        zoomTo.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(ActionId::ViewZoomTo, std::move(zoomTo));
    }

    // --- Navigation -----------------------------------------------------------------------------
    // The two page steps carry their OWN tooltip reason. "Not available for this document" is true
    // of Next Page on the last page and is not the useful thing to say; `unavailableKey` exists
    // exactly so a registration can be more specific than the generic sentence, and this is the
    // first pair of commands in the product to use it. It is a SEPARATE field from `tooltipKey`
    // because the two are shown in mutually exclusive states and say opposite things -- one field
    // serving both put "needs a document to be open" on every enabled Viewer button, which is what
    // the first attempt at I-53 in this task actually did.
    const auto notLastPage = [] {
        MainWindow* window = activeShell();
        const PageView* view = window ? window->currentView() : nullptr;
        return view != nullptr && view->controller().currentPage() < view->pageCount() - 1;
    };
    const auto notFirstPage = [] {
        MainWindow* window = activeShell();
        const PageView* view = window ? window->currentView() : nullptr;
        return view != nullptr && view->controller().currentPage() > 0;
    };

    registerView(ActionId::NavFirstPage, &PageView::firstPage, notFirstPage,
                 "shell.tooltip.atFirstPage");
    registerView(ActionId::NavLastPage, &PageView::lastPage, notLastPage,
                 "shell.tooltip.atLastPage");
    registerView(ActionId::NavNextPage, &PageView::nextPage, notLastPage,
                 "shell.tooltip.atLastPage");
    registerView(ActionId::NavPrevPage, &PageView::prevPage, notFirstPage,
                 "shell.tooltip.atFirstPage");

    // A SCREEN STEP IS NOT A PAGE STEP and is enabled on the last page: there is usually more of it
    // below the fold. Giving it `notLastPage` would grey out `Page Down` halfway down the last page.
    registerView(ActionId::NavNextScreen, &PageView::nextScreen, hasView,
                 "shell.tooltip.needsDocument");
    registerView(ActionId::NavPrevScreen, &PageView::prevScreen, hasView,
                 "shell.tooltip.needsDocument");

    registerView(ActionId::NavPrevView, &PageView::goBackView, [] {
        MainWindow* window = activeShell();
        const PageView* view = window ? window->currentView() : nullptr;
        return view != nullptr && view->controller().canGoBack();
    }, "shell.tooltip.needsDocument");
    registerView(ActionId::NavNextView, &PageView::goForwardView, [] {
        MainWindow* window = activeShell();
        const PageView* view = window ? window->currentView() : nullptr;
        return view != nullptr && view->controller().canGoForward();
    }, "shell.tooltip.needsDocument");

    if (!services_.actions.isRegistered(ActionId::NavGoToPage)) {
        core::ActionRegistry::Registration goTo;
        goTo.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (window) window->promptForPage();
        };
        goTo.enable = [hasView] { return hasView() && canPromptTheUser(); };
        goTo.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(ActionId::NavGoToPage, std::move(goTo));
    }

    // --- The view's rotation, which is NOT the document's --------------------------------------
    registerView(ActionId::ViewRotateViewCw, &PageView::rotateViewClockwise, hasView,
                 "shell.tooltip.needsDocument");
    registerView(ActionId::ViewRotateViewCcw, &PageView::rotateViewCounterClockwise, hasView,
                 "shell.tooltip.needsDocument");

    // --- Layout modes ---------------------------------------------------------------------------
    // `registerMode` SELECTS one arrangement outright; it reports its own state so the ribbon
    // renders a check mark saying which is in force -- `ActionFactory` reads `checked` for that,
    // and `isToggle` decides checkability (I-21).
    //
    // **REPORTING STATE IS NOT THE SAME AS TOGGLING, and calling both of them "toggle" is what hid
    // S2 issue I-23 for eight days**: `ViewTwoUp` was registered here, so `Ctrl+Shift+T` set
    // `TwoUpContinuous` every time and could never leave it, while `checked` made it look like a
    // toggle to every surface that asked. The two chords below are the real toggles and neither
    // goes through this lambda.
    const auto registerMode = [this, hasView](ActionId id, core::render::LayoutMode mode) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = [mode](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            if (PageView* view = window->currentView()) {
                view->setLayoutMode(mode);
                window->updateViewChrome();
            }
        };
        registration.checked = [mode] {
            MainWindow* window = activeShell();
            const PageView* view = window ? window->currentView() : nullptr;
            return view != nullptr && view->controller().layoutMode() == mode;
        };
        registration.enable = hasView;
        registration.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(id, std::move(registration));
    };

    registerMode(ActionId::ViewSinglePage, core::render::LayoutMode::Single);
    registerMode(ActionId::ViewContinuous, core::render::LayoutMode::SingleContinuous);

    // `Ctrl+Shift+T` TOGGLES two-up and PRESERVES the continuity, which is the exact mirror of
    // `Ctrl+Shift+H` below -- one chord owns one axis of `LayoutMode` and leaves the other alone.
    // `Features.md` §8 calls it "Toggle Two-Up" and that table is the authority (I-23).
    //
    // `checked` asks `isTwoUp`, NOT equality with one mode: the button means *two-up is on*, and
    // `TwoUp` and `TwoUpContinuous` are both on. Equality is what `registerMode` wants and is why
    // this cannot go through it.
    if (!services_.actions.isRegistered(ActionId::ViewTwoUp)) {
        core::ActionRegistry::Registration twoUp;
        twoUp.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            PageView* view = window->currentView();
            if (!view) return;

            using core::render::LayoutMode;
            const LayoutMode now = view->controller().layoutMode();
            view->setLayoutMode(core::render::isTwoUp(now)
                                    ? (core::render::isContinuous(now) ? LayoutMode::SingleContinuous
                                                                       : LayoutMode::Single)
                                    : (core::render::isContinuous(now) ? LayoutMode::TwoUpContinuous
                                                                       : LayoutMode::TwoUp));
            window->updateViewChrome();
        };
        twoUp.checked = [] {
            MainWindow* window = activeShell();
            const PageView* view = window ? window->currentView() : nullptr;
            return view != nullptr && core::render::isTwoUp(view->controller().layoutMode());
        };
        twoUp.enable = hasView;
        twoUp.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(ActionId::ViewTwoUp, std::move(twoUp));
    }

    // `Ctrl+Shift+H` TOGGLES between paged and continuous rather than selecting one, which is what
    // its §8 row says ("toggle single-page <-> continuous scrolling") and is a different command
    // from the two selects above. It preserves the two-up-ness, exactly as `Ctrl+Shift+T`
    // preserves the continuity.
    if (!services_.actions.isRegistered(ActionId::ViewTogglePageMode)) {
        core::ActionRegistry::Registration toggle;
        toggle.handler = [](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            PageView* view = window->currentView();
            if (!view) return;

            using core::render::LayoutMode;
            const LayoutMode now = view->controller().layoutMode();
            view->setLayoutMode(core::render::isContinuous(now)
                                    ? (core::render::isTwoUp(now) ? LayoutMode::TwoUp
                                                                  : LayoutMode::Single)
                                    : (core::render::isTwoUp(now) ? LayoutMode::TwoUpContinuous
                                                                  : LayoutMode::SingleContinuous));
            window->updateViewChrome();
        };
        toggle.enable = hasView;
        toggle.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(ActionId::ViewTogglePageMode, std::move(toggle));
    }

    // --- The three tools, and only as much of them as Stage 2 owns ------------------------------
    // `ui/viewer/ToolController` and the `ui/viewer/tools/` family are ○ (S4) -- the sticky-tool
    // state machine, the temporary-hand modifier, `Esc` cancellation and seventeen tool classes.
    // What `Implementation-Plan.md` § Stage 2 lists is the three BINDINGS, and what makes them
    // honest is `PageView::setTool`: a cursor, and what a drag does. See `PageView.h`.
    const auto registerTool = [this, hasView](ActionId id, PageView::Tool tool) {
        if (services_.actions.isRegistered(id)) return;
        core::ActionRegistry::Registration registration;
        registration.handler = [tool](KeyContext) {
            MainWindow* window = activeShell();
            if (!window) return;
            if (PageView* view = window->currentView()) {
                view->setTool(tool);
                window->updateViewChrome();
            }
        };
        registration.checked = [tool] {
            MainWindow* window = activeShell();
            const PageView* view = window ? window->currentView() : nullptr;
            return view != nullptr && view->tool() == tool;
        };
        registration.enable = hasView;
        registration.unavailableKey = "shell.tooltip.needsDocument";
        services_.actions.register_(id, std::move(registration));
    };

    registerTool(ActionId::ToolHand, PageView::Tool::Hand);
    registerTool(ActionId::ToolSelect, PageView::Tool::Select);
    registerTool(ActionId::ToolMarqueeZoom, PageView::Tool::MarqueeZoom);
}

void MainWindow::showDocumentConditions(DocumentWindow* document)
{
    if (!messageBar_ || !document || !document->handle().isValid()) return;

    // THE REPAIR REPORT IS NOT REACHABLE FROM `ui/` YET, AND THAT IS ISSUE I-08 REPEATING ITSELF
    // EXACTLY AS T13 PREDICTED. `repairReport()` is declared on the concrete
    // `pdf::DocumentBackendImpl`, which `ui/` may never name -- the same position `pages()` was in
    // before T13 moved it onto `core::IDocumentBackend`. T14 makes the same two-line move, at this
    // first consumer rather than speculatively, which is the rule that produced the finding.
    if (const core::RepairReport* report = services_.backend.repairReport(document->handle())) {
        messageBar_->showRepairReport(*report, document->title(), this);
    }

    if (document->isProtected()) {
        MessageBar::Message protectedMessage;
        protectedMessage.kind = MessageBar::Kind::Protected;
        protectedMessage.text = catalog::translate(catalog::key::MessageBarProtected)
                                    .arg(document->title());
        messageBar_->showMessage(std::move(protectedMessage));
    }
}

PageView* MainWindow::currentView() const
{
    DocumentWindow* document = currentDocument();
    return document ? document->view() : nullptr;
}

void MainWindow::updateViewChrome()
{
    if (status_) {
        if (const PageView* view = currentView()) {
            status_->setPageState(view->controller().currentPage(), view->pageCount());
            status_->setZoom(view->controller().zoom().factor);
        }
    }

    // EVERY ACTION IS RE-ASKED, not only the ones that obviously moved. Enablement predicates read
    // the active window's state, so opening a document changes the answer for commands nothing in
    // this function mentions -- and `refresh()` re-computes the TOOLTIP too, which is I-53's half:
    // a control that has just become available must stop saying it is not.
    if (actionFactory_) actionFactory_->refresh();
}

void MainWindow::buildAutoHideStrips()
{
    // HOVER REVEALS, CLICK ALSO REVEALS. The first is the gesture -- reaching for the edge is how a
    // person opens an auto-hidden pane, and it is what manual case M-04 expects (decided 2026-08-30;
    // the strip answered only to a click until then, which is a different product). The second stays
    // because a 24-pixel strip that looks like a button and does nothing when pressed reads as a
    // broken control, and `peek()` is idempotent, so a click on an already-peeked pane is a no-op.
    //
    // An event filter rather than a signal: `QToolButton` has no "the pointer arrived" signal, and
    // `enterEvent` is protected. Filtering the two strips costs one virtual call on their events and
    // nothing on anyone else's.
    leftStrip_->installEventFilter(this);
    rightStrip_->installEventFilter(this);

    connect(leftStrip_, &QToolButton::clicked, this, [this]() { sidebar_->peek(); });
    connect(rightStrip_, &QToolButton::clicked, this, [this]() { sidebar_->peek(); });

    // Which edge the pane would return to is named in the strip's tooltip rather than given a
    // button. Moving a pane between edges is a rare deliberate act, and two more permanent controls
    // on a 24-pixel strip would cost more than they earn; dragging the pane already does it.
    leftStrip_->setToolTip(leftStrip_->toolTip() + QLatin1String(" (")
                           + catalog::translate(catalog::key::SidebarDockLeft) + QLatin1Char(')'));
    rightStrip_->setToolTip(rightStrip_->toolTip() + QLatin1String(" (")
                            + catalog::translate(catalog::key::SidebarDockRight) + QLatin1Char(')'));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    // `peek()` does nothing unless the pane is auto-hidden, so there is no state to check here and
    // no way for a stray Enter to open a pinned pane. The event is NOT consumed: the strip is still
    // a button and still has a tooltip to show.
    if (event->type() == QEvent::Enter && (watched == leftStrip_ || watched == rightStrip_))
        sidebar_->peek();

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::updateAutoHideStrips()
{
    // A PEEKED PANE IS ON SCREEN, SO ITS STRIP IS NOT (issue I-39). The strip is what stands in for
    // the pane while the pane is away; showing both at once puts a 24-pixel bar beside the very pane
    // it exists to summon. `isAutoHidden()` stays true through a peek -- deliberately, because a peek
    // is a look and not a pin -- so it is the wrong question to ask here on its own.
    //
    // The pointer is standing on the strip at the moment it goes: that is fine and is the reason the
    // pane can be reached at all. The dock area sits OUTSIDE the central widget, so a revealed pane
    // takes the space the strip was drawn in and arrives under the pointer; leaving the pane is what
    // puts it away (`SidebarDock::leaveEvent`).
    const bool collapsed =
        (sidebar_->isAutoHidden() && !sidebar_->isPeeking())
        || (!sidebar_->isVisible() && !sidebar_->isFloating());
    const bool onLeft = dockWidgetArea(sidebar_) != Qt::RightDockWidgetArea;

    leftStrip_->setVisible(collapsed && onLeft);
    rightStrip_->setVisible(collapsed && !onLeft);
}

void MainWindow::applyQatPlacement()
{
    if (!qat_ || !headerColumn_) return;

    // The SAME widget, moved. Building a second toolbar for the second placement would give the two
    // positions two states, and the first thing a user would notice is a customization that only
    // appears in one of them.
    headerColumn_->removeWidget(qat_);
    if (qat_->isBelowRibbon())
        headerColumn_->addWidget(qat_);
    else
        headerColumn_->insertWidget(0, qat_);
}

void MainWindow::loadUiCustomization()
{
    if (!qat_) return;

    // Nothing written back while restoring. Without this, the first launch would persist the
    // factory defaults as though the user had chosen them -- and `isSet` below, which is how "never
    // customized" is told from "emptied on purpose", would answer the same for both from then on.
    suppressCustomizationSave_ = true;

    qat_->setBelowRibbon(services_.settings.get(core::keys::ui::qatBelowRibbon));
    qat_->setLocked(services_.settings.get(core::keys::ui::qatLocked));

    if (services_.settings.isSet(core::keys::ui::qatCommands)) {
        QStringList dropped;
        const std::vector<ActionId> commands = UiCustomization::resolveCommands(
            services_.settings.get(core::keys::ui::qatCommands), &dropped);
        for (const QString& name : dropped) {
            // A saved command whose id no longer exists and has no alias row. Named rather than
            // counted: this is the exact failure D1-16's alias table exists to prevent, so the day
            // one appears the log has to say which.
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
                << "quick access toolbar: dropping unknown command" << name;
        }
        qat_->setCommands(commands);
    }

    suppressCustomizationSave_ = false;
}

void MainWindow::saveUiCustomization()
{
    if (!qat_ || suppressCustomizationSave_) return;
    services_.settings.set(core::keys::ui::qatCommands,
                           UiCustomization::toStringIds(qat_->commands()));
    services_.settings.set(core::keys::ui::qatBelowRibbon, qat_->isBelowRibbon());
    services_.settings.set(core::keys::ui::qatLocked, qat_->isLocked());
}

void MainWindow::registerActions()
{
    // Stage 1 registers what a shell window can actually service. An ActionId that is DECLARED and
    // UNREGISTERED renders as a visible, disabled control with a tooltip naming what it is waiting
    // for (Stage1-AppShell-Plan.md 5.1) -- which is the honest state, and is why the other ~340 ids
    // are not given placeholder handlers here.
    //
    // ONCE PER REGISTRY, NOT ONCE PER WINDOW -- T12. There can be several of these windows now, and
    // `ActionRegistry` is one object they share; registration is deliberately not idempotent
    // (property 3), so the second window would collect one "duplicate registration" error per
    // command. Whichever window gets here first registers; the rest find the commands present.
    const auto registerRegistration = [this](ActionId id,
                                             core::ActionRegistry::Registration registration) {
        if (services_.actions.isRegistered(id)) return;

        const core::Result<void> registered = services_.actions.register_(id, std::move(registration));
        if (registered) return;
        const std::string_view name = core::actionStringId(id);
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "action registration refused for"
            << QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()))
            << "--" << registered.error().messageKey;
    };

    // EVERY HANDLER BELOW CAPTURES NOTHING AND RESOLVES THE ACTIVE WINDOW AT DISPATCH TIME. A
    // handler holding `this` would bind a Global command to the window that happened to register it
    // -- forever, including after that window was closed and the lambda was left pointing into
    // freed memory. See the class comment in MainWindow.h.
    const auto onActiveShell = [](void (MainWindow::*method)()) {
        return [method](KeyContext) {
            if (MainWindow* window = activeShell()) (window->*method)();
        };
    };

    const auto registerAction = [&registerRegistration](ActionId id,
                                                        core::ActionRegistry::Handler handler,
                                                        core::ActionRegistry::Enable enable = {}) {
        core::ActionRegistry::Registration registration;
        registration.handler = std::move(handler);
        registration.enable  = std::move(enable);
        registerRegistration(id, std::move(registration));
    };

    // --- The shell frame (T9, T10) ----------------------------------------------------------
    registerAction(ActionId::ViewToggleSidebar, onActiveShell(&MainWindow::toggleSidebar));
    registerAction(ActionId::ViewFullScreen, onActiveShell(&MainWindow::toggleFullScreen));

    // R-7 of the A3 criteria, reached as a command rather than as a keypress. F9 is its default
    // chord in Features.md 8 and it starts working at T13, when ShortcutBinder exists -- but the
    // BEHAVIOUR is registered now, which is what makes it reachable from the ribbon, from a test
    // and from the Stage 14 batch runner without a second implementation.
    registerAction(ActionId::ViewToggleRibbon, onActiveShell(&MainWindow::toggleRibbon));

    // R-5, AND THE COMMAND THAT MAKES `Alt` MEAN ANYTHING (issue I-42). The ribbon has drawn key
    // tips since T10 and nothing ever asked it to: `Alt` is bound to this id in
    // `KeyBindingDefaults.def`, the chord forms correctly, and the id was simply never registered --
    // so `dispatch` answered `NotFound`, left the key unconsumed, and pressing Alt did nothing in
    // every locale. R-5 had been recorded as met against the T2 spike, which had its own handler.
    // The follow-up letter is the ribbon's own: `showKeyTips()` takes focus, so it arrives in
    // `Ribbon::keyPressEvent` rather than through a second application-wide key path.
    registerAction(ActionId::AppRibbonKeyTips, [](KeyContext) {
        MainWindow* window = activeShell();
        if (!window || !window->ribbon()) return;
        if (window->ribbon()->keyTipsVisible()) window->ribbon()->hideKeyTips();
        else                                    window->ribbon()->showKeyTips();
    });

    // Quit closes EVERY shell window rather than the active one -- `file.close` is the command for
    // one window's document and `file.quit` is the application. Closing them in reverse order so
    // that the set is not mutated from under the loop.
    registerAction(ActionId::FileQuit, [](KeyContext) {
        const QList<MainWindow*> windows = shellWindows();
        for (auto it = windows.crbegin(); it != windows.crend(); ++it) (*it)->close();
    });

    // --- The Quick Access Toolbar and the tool finder (T11) ---------------------------------
    // `ui.customize` is deliberately NOT registered. The customization dialog is T14, and a handler
    // that opened nothing would make a menu item that does nothing look like one that works.

    registerAction(ActionId::AppToolFinder, [](KeyContext) {
        if (MainWindow* window = activeShell())
            if (ToolFinder* finder = window->toolFinder()) finder->focusSearchBox();
    });

    // ADD AND REMOVE TAKE A PARAMETER, and the registry has no channel for one. The command that
    // was under the cursor is held by the toolbar and read here; the enablement predicate is what
    // makes the absence of a target visible in the menu rather than a refusal after the click. See
    // QuickAccessToolbar.h, and open decision O11 in Implementation-Plan.md 8.
    const auto activeQat = []() -> QuickAccessToolbar* {
        MainWindow* window = activeShell();
        return window ? window->quickAccessToolbar() : nullptr;
    };

    registerAction(
        ActionId::UiQatAdd,
        [activeQat](KeyContext) {
            QuickAccessToolbar* qat = activeQat();
            if (qat && qat->pendingTarget()) qat->addCommand(*qat->pendingTarget());
        },
        [activeQat] {
            QuickAccessToolbar* qat = activeQat();
            return qat && qat->pendingTarget().has_value();
        });

    registerAction(
        ActionId::UiQatRemove,
        [activeQat](KeyContext) {
            QuickAccessToolbar* qat = activeQat();
            if (qat && qat->pendingTarget()) qat->removeCommand(*qat->pendingTarget());
        },
        [activeQat] {
            QuickAccessToolbar* qat = activeQat();
            if (!qat || !qat->pendingTarget()) return false;
            const std::vector<ActionId>& commands = qat->commands();
            return std::find(commands.begin(), commands.end(), *qat->pendingTarget())
                   != commands.end();
        });

    // REORDER, WHICH THE TOOLBAR COULD DO AND NOBODY COULD ASK IT TO (issue I-43). `moveCommand`
    // has been public since T11 and `UiCustomization` has persisted the order for just as long; the
    // only caller was `tst_qat`. Two commands rather than a drag: a drag reaches nothing but a
    // pointer, and the rule this toolbar was built under is that every one of its operations is a
    // command the batch runner and a chord can reach too (flow K).
    //
    // The index is resolved at DISPATCH time from the pending target, never captured: the same
    // registry serves every window (convention 14), and the button that was right-clicked is the
    // parameter (O11).
    const auto qatIndexOfTarget = [activeQat]() -> int {
        QuickAccessToolbar* qat = activeQat();
        if (!qat || !qat->pendingTarget()) return -1;
        const std::vector<ActionId>& commands = qat->commands();
        const auto found = std::find(commands.begin(), commands.end(), *qat->pendingTarget());
        return found == commands.end() ? -1 : static_cast<int>(found - commands.begin());
    };

    const auto registerQatMove = [&](ActionId id, int delta) {
        core::ActionRegistry::Registration move;
        move.handler = [activeQat, qatIndexOfTarget, delta](KeyContext) {
            QuickAccessToolbar* qat = activeQat();
            const int from = qatIndexOfTarget();
            if (qat && from >= 0) qat->moveCommand(from, from + delta);
        };
        // Disabled at the end it cannot move towards, rather than offered and silently refused --
        // `moveCommand` already rejects an out-of-range index, and a menu item that does nothing
        // when clicked is how a user learns to distrust the menu.
        move.enable = [activeQat, qatIndexOfTarget, delta] {
            QuickAccessToolbar* qat = activeQat();
            if (!qat) return false;
            const int from = qatIndexOfTarget();
            const int to   = from + delta;
            return from >= 0 && to >= 0 && to < static_cast<int>(qat->commands().size());
        };
        registerRegistration(id, std::move(move));
    };
    registerQatMove(ActionId::UiQatMoveLeft, -1);
    registerQatMove(ActionId::UiQatMoveRight, +1);

    // THE FIRST TOGGLE IN THE PRODUCT. `checked` is what makes `ActionFactory` mark the action
    // checkable at all -- and getting here is what showed that the factory had never done so
    // (I-21). `tooltipKey` is the other half: the label is a noun, so the check mark needs a
    // sentence saying which way it points.
    core::ActionRegistry::Registration placement;
    placement.handler    = [activeQat](KeyContext) {
        if (QuickAccessToolbar* qat = activeQat()) qat->setBelowRibbon(!qat->isBelowRibbon());
    };
    placement.checked    = [activeQat] {
        QuickAccessToolbar* qat = activeQat();
        return qat && qat->isBelowRibbon();
    };
    placement.tooltipKey = "shell.qat.placementHint";
    registerRegistration(ActionId::UiQatPlacement, std::move(placement));

    // LOCK, a toggle like placement. Drag-to-reorder is the gesture the toolbar now offers, and a
    // lock is the guard against performing it by accident -- so it is offered in the same menu and
    // persisted with the rest of the customization.
    core::ActionRegistry::Registration lock;
    lock.handler = [activeQat](KeyContext) {
        if (QuickAccessToolbar* qat = activeQat()) qat->setLocked(!qat->isLocked());
    };
    lock.checked = [activeQat] {
        QuickAccessToolbar* qat = activeQat();
        return qat && qat->isLocked();
    };
    registerRegistration(ActionId::UiQatLock, std::move(lock));

    registerAction(ActionId::UiResetCustomization, [activeQat](KeyContext) {
        if (QuickAccessToolbar* qat = activeQat()) {
            qat->resetToDefaults();
            qat->setLocked(false);   // a reset that left the toolbar locked would not be one
        }
    });

    // --- Preferences and the log pane (T14) -------------------------------------------------
    // `file.preferences` needs NO enablement predicate, and that is the difference between this
    // dialog and `file.open`'s. A modeless dialog blocks nothing, so there is no headless hazard
    // to guard against -- issue I-23's `canPromptTheUser` exists for a modal `exec()` that never
    // returns when there is nobody to answer it. See ui/dialogs/PreferencesDialog.h.
    // Not through `onActiveShell`: that helper takes a `void ()` member and `showPreferences`
    // returns the dialog, which is what makes it usable from a test without reaching for
    // `findChild`. One lambda is cheaper than a second overload of the helper.
    registerAction(ActionId::FilePreferences, [](KeyContext) {
        if (MainWindow* window = activeShell()) window->showPreferences();
    });

    core::ActionRegistry::Registration logPane;
    logPane.handler = [](KeyContext) {
        MainWindow* window = activeShell();
        if (!window || !window->logPane()) return;
        window->logPane()->setVisible(!window->logPane()->isVisible());
    };
    // A toggle, so the ribbon renders it checkable and the check mark says whether the pane is up.
    // `ActionFactory` reads `checked` to decide that -- see I-21.
    logPane.checked = [] {
        MainWindow* window = activeShell();
        return window && window->logPane() && window->logPane()->isVisible();
    };
    registerRegistration(ActionId::PaneLog, std::move(logPane));

    // `log.openFolder` is DECLARED AND NOT REGISTERED, and the reason is a layer boundary rather
    // than an oversight. The folder belongs to `app/LogFile`, and `ui/` looks outward to exactly
    // two files -- `platform/platform.h` and `app/Services.h` -- each constrained by its own
    // `layer-lint` entry (issue I-15). Including a third to open a directory would widen that list
    // for a convenience, and widening it is an architectural decision rather than a diff. The
    // command stays a visible, disabled button saying it is not built yet, which is the same
    // honest state the other ~340 ids are in.

    // --- The §8 Viewer commands (S2 T14) ----------------------------------------------------
    registerViewerActions();

    // --- Documents and windows (T12) --------------------------------------------------------
    // `file.open` raises a file dialog and hands what it collects to `openFiles`, which checks a
    // name and a suffix and reads not one byte (D1-2, guide case A-58). It is registered here
    // rather than at T14 with the rest of the dialogs because `openFiles` now has somewhere to put
    // a file: a dead Open button above working machinery is worse than a missing one.
    registerAction(ActionId::FileOpen, onActiveShell(&MainWindow::promptForFiles),
                   [] { return canPromptTheUser(); });

    const auto hasDocument = [] {
        MainWindow* window = activeShell();
        return window && window->documentCount() > 0;
    };

    registerAction(
        ActionId::FileClose,
        [](KeyContext) {
            if (MainWindow* window = activeShell())
                window->closeDocument(window->documentTabs()->currentIndex());
        },
        hasDocument);

    registerAction(
        ActionId::FileCloseAll,
        [](KeyContext) {
            if (MainWindow* window = activeShell()) window->closeAllDocuments();
        },
        hasDocument);

    // The four navigation commands. Their enablement is what makes `Ctrl+Tab` in a window with one
    // tab report "not applicable right now" rather than appearing to work -- the distinction T3's
    // `ErrorCode::Unavailable` exists for, and the one a batch report has to be able to make.
    const auto stepDocuments = [](int delta) {
        return [delta](KeyContext) {
            if (MainWindow* window = activeShell()) window->stepDocument(delta);
        };
    };
    const auto manyDocuments = [] {
        MainWindow* window = activeShell();
        return window && window->documentCount() > 1;
    };

    registerAction(ActionId::WindowNextTab, stepDocuments(1), manyDocuments);
    registerAction(ActionId::WindowPrevTab, stepDocuments(-1), manyDocuments);

    const auto manyWindows = [] { return shellWindows().size() > 1; };
    registerAction(ActionId::WindowNext, [](KeyContext) { stepWindow(1); }, manyWindows);
    registerAction(ActionId::WindowPrev, [](KeyContext) { stepWindow(-1); }, manyWindows);

    // A new EMPTY window, which is what `window.new` means -- tearing a tab out is a gesture on the
    // tab, not this command. `WA_DeleteOnClose` because nothing else owns it.
    registerAction(ActionId::WindowNew, [](KeyContext) {
        MainWindow* from = activeShell();
        if (!from) return;
        auto* window = new MainWindow(from->services_);
        window->setAttribute(Qt::WA_DeleteOnClose);
        window->show();
        window->raiseToForeground();
    });
}

PreferencesDialog* MainWindow::showPreferences()
{
    // Built on first use and KEPT, so that reopening it returns to the page the user was last on.
    // Parented to this window, so a torn-off window gets its own -- which is right: the pages read
    // one `Settings`, and two dialogs showing the same values is not a conflict, it is two views.
    if (!preferences_) preferences_ = new PreferencesDialog(services_, this);

    preferences_->show();
    preferences_->raise();
    preferences_->activateWindow();
    return preferences_;
}

void MainWindow::toggleSidebar()
{
    if (sidebar_->isVisible()) {
        sidebar_->setRememberedWidth(sidebar_->width());
        sidebar_->hide();
    } else {
        sidebar_->setAutoHidden(false);
        sidebar_->show();
        resizeDocks({sidebar_}, {sidebar_->rememberedWidth()}, Qt::Horizontal);
    }
    updateAutoHideStrips();
}

void MainWindow::toggleFullScreen()
{
    // Full screen at Stage 1 is the WINDOW STATE ONLY. Presentation mode -- auto-advance, the laser
    // pointer, blank-to-black, choosing a monitor -- is Stage 4's ui/viewer/FullScreenController,
    // and it needs a page surface to present.
    setWindowState(windowState() ^ Qt::WindowFullScreen);
}

void MainWindow::toggleRibbon()
{
    if (ribbon_) ribbon_->setBodyCollapsed(!ribbon_->isBodyCollapsed());
}

void MainWindow::promptForFiles()
{
    // The dialog COLLECTS PARAMETERS and nothing else -- the standing rule about dialogs, and the
    // reason this is three lines. Where the user was last is a Stage 14 preference; until then the
    // dialog opens wherever Qt opens it.
    const QStringList chosen = QFileDialog::getOpenFileNames(
        this, catalog::translate(catalog::key::DialogOpenTitle), QString(),
        catalog::translate(catalog::key::DialogPdfFilter));
    if (!chosen.isEmpty()) openFiles(chosen);
}

void MainWindow::openFiles(const QStringList& paths)
{
    // EVERY REFUSAL IN THIS CALL, for one window-level banner at the end (Stage 2 issue I-32). The
    // status bar kept the only copy until 2026-09-13, bottom-left, in the grey of everything else:
    // a double-click on a damaged file produced no tab, no dialog and no banner, and the user
    // reasonably concluded nothing had happened. One banner per call rather than per file, so a
    // handoff of six damaged files is one strip listing six sentences rather than six strips.
    QStringList refusals;

    for (const QString& path : paths) {
        const QFileInfo info(path);

        // Existence is still ours: "there is no such file" is a different message from "that is
        // not a PDF", and asking the engine to open something that is not there would turn the
        // first into the second.
        if (!info.exists() || !info.isFile()) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "open: no such file:" << path;
            const QString reason =
                catalog::translateError(QStringLiteral("error.open.notFound"), path);
            status_->setMessage(reason);
            refusals << reason;
            continue;
        }

        // **T9: THE SUFFIX CHECK IS GONE, AND THE BACKEND DECIDES.** Guide case A-47, which
        // replaces Stage 1's A-58 -- that case asserted the OPPOSITE and was correct then, because
        // D1-2 said the shell reads no bytes and there was no engine to ask. There is now.
        //
        // The consequence is user-visible in both directions: a PDF someone saved as `.dat` opens,
        // and a `.pdf` that is really a spreadsheet is refused **with the reason the engine gave**
        // rather than with a guess from its name. Deciding whether bytes are a PDF is
        // `IDocumentBackend::open()`'s question and nowhere else's.
        //
        // ALREADY OPEN IN THIS WINDOW? Then the backend is not asked at all, and that is a change
        // T13 made deliberately: T9 opened first and discovered the duplicate afterwards, which
        // was harmless while the handle was thrown away and is a leaked engine handle now that a
        // tab owns one. Focusing the existing tab is also what the user meant.
        if (const int existing = indexOfPath(info.absoluteFilePath()); existing >= 0) {
            activateDocument(existing);
            continue;
        }

        // THE PASSWORD PROMPT (T14), which the T9 comment here said was a dialog and therefore
        // T14's. The callback is built PER FILE, so a handoff of six encrypted documents names
        // each one rather than asking the same anonymous question six times -- and it answers
        // `std::nullopt` immediately when there is nobody to ask, which is `Cancelled` rather than
        // a failure and is what stops a headless run from hanging on a modal nobody can see
        // (Stage 1's I-23).
        int passwordAsks = 0;
        const core::PasswordCallback prompt =
            [&passwordAsks, callback = PasswordDialog::callbackFor(info.fileName(), this)](
                int attempt) {
                ++passwordAsks;
                return callback(attempt);
            };

        const core::Result<core::DocumentHandle> opened =
            services_.backend.open(info.absoluteFilePath(), prompt);
        if (!opened) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
                << "open: backend refused" << info.absoluteFilePath() << "--"
                << opened.error().messageKey.data();
            const QString reason =
                catalog::translateError(opened.error().messageKey, opened.error().context);
            status_->setMessage(reason);

            // A CANCELLED PROMPT IS NOT A FAILURE (A-43): the person pressed Escape, and a red
            // banner telling them the file could not be opened would be telling them off for it.
            if (opened.error().code != core::ErrorCode::Cancelled) refusals << reason;
            continue;
        }

        // The recent list is a list of paths (T5's deviation (c)). It is noted only AFTER the
        // backend accepted the file: Stage 1 noted every path it was handed because it could not
        // tell whether one was openable, and a recent list full of things that will not open is
        // worse than a short one.
        services_.recentFiles.note(info.absoluteFilePath());

        // Whether the file was ENCRYPTED is not something the backend reports separately, and it
        // does not need to be: the callback was asked, so it was. That is the honest signal rather
        // than a second query, and it is why the counter exists.
        DocumentWindow* document = openDocument(info.absoluteFilePath(), opened.value());
        if (document && passwordAsks > 0) document->setProtected(true);
        updateDocumentChrome();
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "open: accepted" << info.absoluteFilePath() << "as handle"
            << static_cast<int>(opened.value().id);
    }

    if (!refusals.isEmpty() && messageBar_) {
        MessageBar::Message failed;
        failed.kind = MessageBar::Kind::OpenFailed;
        failed.text = refusals.join(QLatin1Char('\n'));
        messageBar_->showMessage(std::move(failed));
    }
}

void MainWindow::openHandoff(const QStringList& files)
{
    // RAISE FIRST. The secondary granted this process the right to come forward before it exited
    // (`AllowSetForegroundWindow`, D1-11), and spending it here is what lets the password prompt that
    // `openFiles()` may raise be shown by the FOREGROUND process, in front, parented to a window
    // that is already there. A refused file benefits too: its reason lands in the status bar of a
    // window the person is looking at, rather than of one still behind whatever they clicked in.
    raiseToForeground();
    openFiles(files);
}

void MainWindow::raiseToForeground()
{
    // The permission to come forward was granted by the SECONDARY process before it exited
    // (AllowSetForegroundWindow, decision D1-11): Windows refuses it to a process that received no
    // input, so without that grant these three calls do nothing at all and the symptom is "I
    // double-clicked a PDF and nothing happened".
    if (isMinimized()) setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();

    // AND SET THE ACTIVE SHELL DIRECTLY, rather than waiting for the ActivationChange that
    // `activateWindow()` normally causes. Measured at T12: under `offscreen` that event never
    // arrives, so `activeShell()` went on naming the previous window and a Global command
    // dispatched into the window nobody had raised. Bringing a window forward IS making it the
    // active shell -- deriving it from the platform's event was the indirection, not this.
    g_activeShell = this;
}

} // namespace rawpage::ui
