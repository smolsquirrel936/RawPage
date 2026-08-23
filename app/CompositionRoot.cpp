#include "app/CompositionRoot.h"

#include "core/settings/SettingsSchema.h"

#include <string>
#include <utility>

namespace rawpage::app {
namespace {

using core::LogSubsystem;

} // namespace

CompositionRoot::CompositionRoot(Options options)
    : options_(std::move(options))
{
    settings_ = std::make_unique<core::Settings>(
        options_.settingsPath.isEmpty() ? core::Settings::defaultFilePath() : options_.settingsPath);

    // Before anything else logs: the level and subsystem mask the user asked for should apply to
    // this startup, not to the next one.
    applyLoggingSettings();
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
    importedOverrides_ = keyBindings_->importOverrides(overrides);
    if (!overrides.empty() || settings_->droppedShortcutKeyCount() > 0) {
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "shortcuts:" << static_cast<int>(importedOverrides_) << "of"
            << static_cast<int>(overrides.size()) << "user overrides applied;"
            << static_cast<int>(settings_->droppedShortcutKeyCount())
            << "keys named an action or context that no longer exists";
    }

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

    // Put the unfiltered sink back. The filtering lambda captures only copies, so leaving it
    // installed would not dangle -- it would do something subtler and worse: silently drop records
    // according to a settings snapshot from a session that has ended, exactly while shutdown and
    // last-rites logging are the only things left to read.
    if (options_.baseSink) core::setLogSink(options_.baseSink);
}

Services CompositionRoot::services()
{
    return Services{*settings_, *recentFiles_, *actions_, *keyBindings_, *platform_};
}

void CompositionRoot::applyLoggingSettings()
{
    core::setLogThreshold(settings_->get(core::keys::logging::level));

    // core/Log.h filters by LEVEL only; the subsystem mask is applied here, in the sink, because
    // core/ has no configuration of its own by design. The mask is captured BY VALUE so a record
    // emitted from a worker thread reads a consistent snapshot rather than racing a settings edit.
    const core::LogSubsystemMask mask = settings_->get(core::keys::logging::subsystems);
    if (!options_.baseSink) return;

    core::setLogSink([mask, sink = options_.baseSink](const core::LogRecordData& record) {
        if (!mask.contains(record.subsystem)) return;
        sink(record);
    });
}

} // namespace rawpage::app
