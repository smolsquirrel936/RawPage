#include "ui/shell/DocumentWindow.h"

#include "core/Log.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "core/settings/Settings.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"
#include "platform/platform.h"
#include "ui/shell/Catalog.h"
#include "ui/viewer/PageView.h"

#include <QDir>
#include <QFileInfo>
#include <QVBoxLayout>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;

// How long the destructor will wait for a tile that is already inside the engine. One tile is
// measured at p95 0.5 ms (A2's probe), so this is three orders of magnitude of headroom and is a
// bound rather than an expectation -- a closing tab that hung on a wedged worker would be a
// product that cannot be quit.
constexpr int kDrainBudgetMs = 2000;

} // namespace

DocumentWindow::DocumentWindow(app::Services services, QString path,
                               core::DocumentHandle handle, QWidget* parent)
    : QWidget(parent), services_(services), path_(std::move(path)), handle_(handle)
{
    sourcePath_ = path_;
    build(nullptr);
}

DocumentWindow::DocumentWindow(app::Services services, core::DocumentHandle handle, const Recovery& recovery,
                               QWidget* parent)
    : QWidget(parent), services_(services), handle_(handle)
{
    sourcePath_ = recovery.sourcePath;
    path_ = recovery.untitled ? QString() : recovery.sourcePath;
    build(&recovery);
}

void DocumentWindow::build(const Recovery* recovery)
{
    setObjectName(QStringLiteral("documentWindow"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    view_ = new PageView(services_.renderScheduler, services_.platform.displayTopology.get(), this);
    column->addWidget(view_);

    // THE PREFERENCES BEFORE THE DOCUMENT. `layoutMode` and `defaultZoom` are applied only on the
    // first `applySettings` call -- they are what a document OPENS at -- so they have to be in place
    // before there is a document to open at them.
    applyViewSettings();

    // THE DOCUMENT AND ITS JOURNAL (Stage 3 T10). Hydrated from the backend -- a copy of its page tree, so
    // a rotation changes the model and never the engine's reading (D3-15). A handle the backend does not
    // know hydrates nothing, and the view draws nothing.
    if (handle_.isValid()) {
        core::Document document;
        if (document.hydrate(services_.backend, handle_)) {
            journal_ = std::make_unique<core::CommandJournal>(std::move(document), services_.backend.mutationSink(handle_));
            coordinator_ = std::make_unique<core::SaveCoordinator>(services_.backend, services_.platform.durableFiles.get());

            // THE CRASHED SESSION, REPLAYED FIRST (Stage 3 T11): no recorder yet, because replay refuses a
            // recording journal and the replayed lines are handed to the new recovery journal whole; no
            // subscription yet, because nothing is on screen to update.
            QByteArray seedLines;
            if (recovery) {
                core::Result<core::ReplayReport> replayed = core::RecoveryJournal::replay(recovery->directory, *journal_);
                if (replayed) {
                    recoveryReport_ = replayed.value();
                    seedLines = recoveryReport_->appliedLines;
                } else {
                    recoveryFailure_ = replayed.error();
                }
            }

            if (isEditable()) {
                // `documents/recoveryDir` first (Stage 3 T12), read when the document opens: an open document
                // keeps the journal it has when the setting changes.
                const QString root = core::RecoveryJournal::resolveRoot(
                    services_.settings.get(core::keys::documents::recoveryDir), services_.recoveryRoot);
                recovery_ = core::RecoveryJournal::open(root, sourcePath_, journal_->document().id(),
                                                        services_.platform.durableFiles.get(), seedLines, isUntitled());
                journal_->setRecorder(recovery_.get());
            }
            journal_->subscribeMutated([this](const core::Scope& scope) {
                services_.renderScheduler.documentChanged(handle_, scope, journal_->revision());
                afterMutation();
            });
            journal_->subscribeDirty([this](bool dirty) { emit dirtyChanged(dirty); });
        }
    }

    // The MODEL's pages for layout, the ENGINE's for the delta each tile carries (D3-15).
    if (journal_) {
        view_->setDocument(handle_, journal_->revision(), &journal_->document().pages(),
                           services_.backend.pages(handle_));
    } else {
        view_->setDocument(handle_, revision_, services_.backend.pages(handle_));
    }

    // S2 T15. A `pageDisplay/` change reaches every open tab, not only the one in front: a person
    // who turns off image smoothing expects it off everywhere, and a tab that kept the old value
    // until it was next touched would be a preference that appears not to have applied.
    settingsSubscription_ = services_.settings.subscribe([this](const core::Settings::Change& change) {
        if (change.group == core::SettingsGroup::PageDisplay
            || change.group == core::SettingsGroup::PerformanceAndCache) {
            applyViewSettings();
        }
    });
}

void DocumentWindow::applyViewSettings()
{
    if (!view_) return;

    // ONE READ PER KEY, AND NO DEFAULTS SPELLED HERE. `Settings::get` never asks a caller for a
    // fallback -- `SettingsKeys.def` rule 2 -- so this cannot disagree with the table.
    const core::Settings& settings = services_.settings;

    ViewSettings view;
    view.layoutMode        = settings.get(core::keys::pageDisplay::layoutMode);
    view.defaultZoom       = settings.get(core::keys::pageDisplay::defaultZoom);
    view.showCoverInTwoUp  = settings.get(core::keys::pageDisplay::showCoverInTwoUp);
    view.rtlBinding        = settings.get(core::keys::pageDisplay::rtlBinding);
    view.lineWeight        = settings.get(core::keys::pageDisplay::lineWeight);
    view.antialiasText     = settings.get(core::keys::pageDisplay::antialiasText);
    view.antialiasImages   = settings.get(core::keys::pageDisplay::antialiasImages);
    view.antialiasLineArt  = settings.get(core::keys::pageDisplay::antialiasLineArt);
    view.transparencyGrid  = settings.get(core::keys::pageDisplay::transparencyGrid);
    view.maxRenderDpi      = settings.get(core::keys::pageDisplay::maxRenderResolution);
    view.wheelZooms        = settings.get(core::keys::pageDisplay::wheelZooms);
    view.prefetchPages     = settings.get(core::keys::performance::prefetchPages);

    view_->applySettings(view);
}

DocumentWindow::~DocumentWindow()
{
    // BEFORE ANYTHING ELSE. `Settings` belongs to the composition root and outlives every tab, so a
    // subscription left behind is a call into a destroyed widget on the next preference change --
    // convention 10, in the form where the caller is a settings store rather than a signal.
    if (settingsSubscription_ != 0) services_.settings.unsubscribe(settingsSubscription_);

    // NOTHING UNSAVED, NOTHING TO RECOVER. A document closed clean takes its recovery directory with it;
    // one closed dirty -- which only a Discard or a process ending reaches -- keeps it for the next
    // launch unless the Discard already removed it (D3-14).
    if (journal_ && recovery_) {
        journal_->setRecorder(nullptr);
        if (!journal_->isDirty())
            recovery_->discard();
    }

    // ORDER, and the header argues each step. Cancel first, then drain, then drop the tiles, then
    // close -- a `close()` that ran while a worker was inside this document's PDFium handle would
    // be a use-after-free in the engine, which `IDocumentBackend::close()` states as the caller's
    // protocol because no mutex can express it.
    if (handle_.isValid()) {
        services_.renderScheduler.cancelDocument(handle_);

        if (!services_.renderScheduler.waitForIdle(kDrainBudgetMs)) {
            // REPORTED, not ignored. Closing anyway is still the least bad option -- refusing to
            // close the tab would strand the user -- but a drain that timed out is the precondition
            // for the crash this sequence exists to prevent, and it must be in the log when it
            // happens rather than inferred from the dump afterwards.
            RAWPAGE_LOG_ERROR(LogSubsystem::Render)
                << "close: the render scheduler did not go idle within" << kDrainBudgetMs
                << "ms; closing" << path_ << "anyway";
        }

        services_.renderCache.dropDocument(handle_);

        if (const core::Status closed = services_.backend.close(handle_); !closed) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Render)
                << "close: backend refused" << path_ << "--" << closed.error().messageKey;
        }
    }
}

core::Revision DocumentWindow::revision() const noexcept
{
    return journal_ ? journal_->revision() : revision_;
}

bool DocumentWindow::isEditable() const
{
    return journal_ && services_.backend.mutationSink(handle_) != nullptr
           && journal_->document().permissions().canEditPages();
}

bool DocumentWindow::isDirty() const
{
    return journal_ && journal_->isDirty();
}

void DocumentWindow::afterMutation()
{
    view_->modelChanged(journal_->revision());
    if (recovery_ && !recovery_->isRecoverable() && !recoveryReported_) {
        recoveryReported_ = true;
        emit recoveryUnavailable(catalog::translateError(QString::fromLatin1(recovery_->unrecoverableKey()),
                                                         recovery_->unrecoverableContext()));
    }
    emit mutated();
}

core::Result<core::SaveOutcome> DocumentWindow::save()
{
    if (!journal_ || !coordinator_)
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), path_};
    if (isUntitled()) {
        // NO FILE OF ITS OWN, SO NOTHING TO SAVE IN PLACE (Stage 3 T11). Its source is not its file: writing
        // there would be the very write "recover to a new file" promised not to make.
        core::SaveOutcome outcome;
        outcome.kind = core::SaveOutcome::Kind::SaveAsRequired;
        outcome.reasonKey = QLatin1String("error.save.untitled");
        return outcome;
    }
    return coordinator_->save(*journal_, path_);
}

core::Result<core::SaveOutcome> DocumentWindow::saveAs(const QString& target)
{
    if (!journal_ || !coordinator_)
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), target};
    auto result = coordinator_->saveAs(*journal_, target);
    if (result && result.value().kind == core::SaveOutcome::Kind::Saved) {
        path_ = QFileInfo(target).absoluteFilePath();
        sourcePath_ = path_;                     // an untitled document now has a file of its own
    }
    return result;
}

QString DocumentWindow::suggestedSavePath() const
{
    if (!isUntitled())
        return path_;
    const QFileInfo source(sourcePath_);
    const QString name = catalog::translate(catalog::key::RecoverySuggestedName).arg(source.completeBaseName());
    return source.dir().filePath(name + QLatin1String(".pdf"));
}

void DocumentWindow::discardRecovery()
{
    if (!recovery_)
        return;
    if (journal_)
        journal_->setRecorder(nullptr);
    recovery_->discard();
}

int DocumentWindow::pageCount() const
{
    const core::PageTree* pages = services_.backend.pages(handle_);
    return pages != nullptr ? pages->count() : 0;
}

QString DocumentWindow::title() const
{
    const QFileInfo info(path_);
    const QString name = info.fileName();
    return name.isEmpty() ? catalog::translate(catalog::key::DocumentUntitled) : name;
}

} // namespace rawpage::ui
