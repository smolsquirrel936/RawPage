#include "app/CompositionRoot.h"

#include "app/BackendRasterizer.h"
#include "app/CrashHandler.h"

#include "pdf/BackendFactory.h"

#include "core/render/RenderRequest.h"
#include "core/settings/SettingsSchema.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QtGlobal>

#include <string>
#include <utility>

namespace rawpage::app {
namespace {

using core::LogSubsystem;

} // namespace

CompositionRoot::CompositionRoot(Options options)
    : options_(std::move(options))
{
    // BEFORE Settings, and that ordering is the whole reason this line is first. The settings load
    // emits the D1-16 version note and any clamp or migration line, and those are precisely what a
    // user opens the log pane to read; a buffer built after them would start at the line that says
    // everything is fine.
    logBuffer_ = std::make_unique<core::LogBuffer>();
    applyLoggingSettings();

    settings_ = std::make_unique<core::Settings>(
        options_.settingsPath.isEmpty() ? core::Settings::defaultFilePath() : options_.settingsPath);

    // THE ROLLING FILE, before the version note below rather than after it. `logging/retainFiles`
    // has been a declared key with no reader since T5; this is the reader. It is created here
    // rather than in main.cpp because the retention count is a setting, and `Settings` is the
    // first thing this constructor builds.
    if (!options_.logDirectory.isEmpty()) {
        LogFile::Options fileOptions;
        fileOptions.directory   = options_.logDirectory;
        fileOptions.retainFiles = settings_->get(core::keys::logging::retainFiles);
        logFile_ = std::make_unique<LogFile>(std::move(fileOptions));
    }

    // Again, now that `Settings` and the file exist: the first call above could only install the
    // fan-out over the buffer, since the level, the subsystem mask and the retention count had not
    // been read yet. Calling it twice is cheaper than a second function that installs half of the
    // same sink.
    applyLoggingSettings();

    // Braced: RAWPAGE_LOG expands to an if/else, so an unbraced `if` above it is a dangling else
    // that -Wall reports and -Werror would refuse in core/.
    if (logFile_ && logFile_->isOpen()) {
        // A SESSION BANNER, and it is not decoration. Everything logged before this point -- most
        // importantly `CrashHandler::install()`'s build id -- reached the console and could not
        // reach the file, because the retention count is a setting and `Settings` did not exist
        // yet. A support bundle whose log cannot say which binary produced it is a bundle nobody
        // can symbolize a dump against, so the build id is restated here, where the file starts.
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "session: RawPage" << QLatin1String(RAWPAGE_VERSION) << "buildId"
            << CrashHandler::buildId() << "Qt" << QLatin1String(qVersion());
        RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "log file:" << logFile_->filePath();
    }
    settingsSubscription_ = settings_->subscribe([this](const core::Settings::Change& change) {
        if (change.group == core::SettingsGroup::Logging) applyLoggingSettings();
    });

    // D1-16 in both directions, said out loud at startup. "Your preferences did not take effect"
    // is otherwise indistinguishable from "your preferences file was written by a newer build and
    // is deliberately being left alone", and only one of those is a bug.
    const char* const versionNote =
        settings_->writesSuppressed() ? " (written by a newer build -- preserved, not rewritten)"
        : settings_->wasMigrated()    ? " (migrated to the current schema)"
                                      : "";
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "settings:" << settings_->filePath() + QLatin1String(versionNote);

    recentFiles_ = std::make_unique<core::RecentFiles>(*settings_);
    actions_     = std::make_unique<core::ActionRegistry>();

    // Defaults come from KeyBindingDefaults.def, which is Features.md §8 in machine-readable form;
    // the per-user layer comes from settings.ini on top of it. Importing REPORTS its count rather
    // than assuming success, because the interesting case is the one that drops entries.
    keyBindings_ = std::make_unique<core::KeyBindingStore>();
    const std::vector<core::KeyBindingStore::OverrideEntry> overrides = settings_->shortcutOverrides();

    // `importOverrides` RETURNS HOW MANY IT DROPPED, which its header says and this line read
    // backwards until 2026-08-31 (issue I-51): a session that applied the user's single remap
    // perfectly logged "0 of 1 user overrides applied". The number was right and the sentence was
    // its opposite, which is worse than no line at all -- it is a line that sends whoever reads it
    // hunting a defect that is not there, and it did.
    droppedOverrides_ = keyBindings_->importOverrides(overrides);
    if (!overrides.empty() || settings_->droppedShortcutKeyCount() > 0) {
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "shortcuts:" << static_cast<int>(overrides.size() - droppedOverrides_) << "of"
            << static_cast<int>(overrides.size()) << "user overrides applied;"
            << static_cast<int>(droppedOverrides_) << "dropped;"
            << static_cast<int>(settings_->droppedShortcutKeyCount())
            << "keys named an action or context that no longer exists";
    }

    // THE ONLY createBackend() CALL IN THE PROGRAM (Stage 2 T1), and the second half of what makes
    // flow A's two factory lines real. Like the platform bundle it logs its own interface census --
    // which read "0 of 7 interfaces implemented" from T1 until T9 implemented two of them. That
    // line is the point: a build that cannot open a document should say so in its log rather than
    // leave a caller to find out from an empty result (D2-7, convention 6).
    //
    // **IT IS IN `Services` AS OF T9**, and it arrived with its consumer exactly as the note here
    // said it would: `MainWindow::openFiles()` now asks the backend whether a file is a PDF instead
    // of looking at the suffix (guide case A-47). A service handed to every widget ahead of the
    // first widget that reads it is the shape Stage 1 kept finding (I-34, I-42, I-46), and waiting
    // is what avoided it.
    //
    // The census now reads "2 of 7 interfaces implemented" -- `IDocumentBackend` and
    // `IPageRasterizer`.
    backend_ = pdf::createBackend();

    // --- The render pipeline (Stage 2 T13) ----------------------------------------------------
    // One cache and one scheduler for the process. Both are `core/` types and neither knows what
    // an engine is: the scheduler is handed a FACTORY, and what it makes is the adapter above.
    renderCache_ = std::make_unique<core::render::RenderCache>();

    // The disk tier stays absent. D2-14 is explicit that whether it earns its place is a
    // MEASUREMENT T18 makes over the corpus, not a preference expressed here, and a tier switched
    // on by default would make that measurement about itself.
    renderScheduler_ = std::make_unique<core::render::RenderScheduler>(
        [backend = backend_.get()]() -> std::unique_ptr<core::IPageRasterizer> {
            return std::make_unique<BackendRasterizer>(*backend);
        },
        renderCache_.get());

    // THE PUMP, AND THERE IS EXACTLY ONE OF IT. `RenderScheduler::setWakeHandler` REPLACES rather
    // than chains -- `core/Log.h`'s single sink in a second disguise (convention 18) -- so a
    // `PageView` that installed its own would silence every other view the moment a second tab
    // opened. The composition root owns it, and `deliverReady()` fans out to every subscriber.
    //
    // The wake is called FROM A WORKER and must not touch the scheduler, so all it does is post.
    pumpAlive_ = std::make_shared<bool>(true);
    renderScheduler_->setWakeHandler(
        [scheduler = renderScheduler_.get(), alive = std::weak_ptr<bool>(pumpAlive_)] {
            if (QCoreApplication::instance() == nullptr) return;
            QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [scheduler, alive] {
                    // The flag is what makes a post that outlived this object harmless rather
                    // than a use-after-free on a scheduler that no longer exists.
                    if (alive.expired()) return;
                    scheduler->deliverReady();
                },
                Qt::QueuedConnection);
        });

    if (options_.createPlatform) {
        // THE ONLY createPlatform() CALL IN THE PROGRAM. It logs its own interface census.
        platform_ = platform::createPlatform();
    } else {
        // An empty bundle rather than a null pointer: every member is already null, which is the
        // vocabulary platform/platform.h defines for "not available", so a caller needs no second
        // way to ask.
        platform_ = std::make_unique<platform::Platform>();
    }
}

CompositionRoot::~CompositionRoot()
{
    // THE RENDER PIPELINE GOES FIRST, and the order inside it matters twice over. Dropping the
    // wake handler stops new posts; releasing `pumpAlive_` makes any post already in Qt's queue a
    // no-op; destroying the scheduler joins its worker, which is what guarantees no thread is
    // inside `BackendRasterizer` -- and therefore inside the backend -- when the backend is
    // destroyed further down.
    if (renderScheduler_) {
        renderScheduler_->setWakeHandler(nullptr);
        pumpAlive_.reset();
        renderScheduler_->cancelAll();
        renderScheduler_.reset();
    }
    renderCache_.reset();

    // THE WRITE. `Settings` accumulates dirty keys and commits them on sync(), which nothing called
    // until T8 -- so before this line the recent-files list and every preference were correct for
    // the life of the process and gone at the end of it (issue I-14). Found by running the flow,
    // not by reading it: a forwarded file appeared in the primary's log and not in settings.ini.
    //
    // Here rather than in Application::aboutToQuit because a secondary instance exits before any
    // event loop runs, and a diagnostic run exits before that. A process that changed nothing
    // writes nothing, so the cost of the wider placement is zero.
    if (const core::Status written = settings_->sync(); !written) {
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "settings: could not be saved --" << written.error().messageKey
            << written.error().context;
    }

    if (settingsSubscription_ != 0) settings_->unsubscribe(settingsSubscription_);

    // Put the unfiltered sink back. This is no longer only about a stale mask: the fan-out lambda
    // holds RAW POINTERS to the buffer and the log file, both of which are members about to be
    // destroyed, so leaving it installed would be a use-after-free on the next line that logs --
    // and the next line that logs during teardown is the one that says why teardown failed.
    core::setLogSink(options_.baseSink ? options_.baseSink : core::LogSink{});
}

Services CompositionRoot::services()
{
    return Services{*settings_,  *recentFiles_,     *actions_, *keyBindings_,
                    *logBuffer_, *backend_,         *renderScheduler_, *renderCache_,
                    *platform_};
}

void CompositionRoot::applyLoggingSettings()
{
    // Called once before `Settings` exists (to install the fan-out) and again after it does, and
    // whenever a `logging/` key changes. Both halves have to survive being run with no settings.
    core::LogSubsystemMask mask = core::LogSubsystemMask::all();
    if (settings_) {
        core::setLogThreshold(settings_->get(core::keys::logging::level));

        // core/Log.h filters by LEVEL only; the subsystem mask is applied here, in the sink,
        // because core/ has no configuration of its own by design. The mask is captured BY VALUE
        // so a record emitted from a worker thread reads a consistent snapshot rather than racing
        // a settings edit.
        mask = settings_->get(core::keys::logging::subsystems);
    }

    // ONE SINK, FANNING OUT. Everything that wants records is fed from here rather than by
    // chaining `setLogSink`, because chaining does not survive this function being called again --
    // which it is, on every logging settings change. `core/LogBuffer.h` states that reasoning at
    // length; this is the line that depends on it.
    core::setLogSink([mask, sink = options_.baseSink, buffer = logBuffer_.get(),
                      file = logFile_.get()](const core::LogRecordData& record) {
        if (!mask.contains(record.subsystem)) return;
        if (sink) sink(record);
        if (file) file->append(record);
        if (buffer) buffer->append(record);
    });
}

} // namespace rawpage::app
