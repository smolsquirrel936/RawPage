// The wiring hub -- flow A in Architecture-FileMap.md §5.
//
// This is the ONLY translation unit in the program permitted to name `platform::createPlatform()`,
// and from Stage 2 the only one permitted to name `pdf::createBackend()`. Everything below it
// receives interfaces. That is not a preference about dependency injection: it is what makes
// `ui/shell/MainWindow` constructible in a test with no PDFium, no Win32 and no display, and it is
// the property the layer split exists to produce.
//
// WHAT IT DOES NOT DO. No domain logic, no policy, no document knowledge. It owns object lifetimes
// and it connects one object's output to another's input. The one judgement call it makes -- and it
// is wiring, not policy -- is applying the logging settings to core/Log.h's global sink, because
// core/ cannot write files and app/ is where the sink lives.
//
// ORDER MATTERS IN ONE PLACE ONLY: `Settings` is built first, because `general/singleInstance`
// decides whether `app/SingleInstance` claims at all and `language/uiLocale` decides which catalog
// `app/Application` loads. Flow A draws `SingleInstance::claim()` above `CompositionRoot::build()`;
// in practice the claim needs a setting, so the services are built first and the claim follows.
// A secondary instance therefore constructs `Settings` before it exits -- which reads the file and
// never writes it, since `Settings` only writes on `sync()`.

#ifndef RAWPAGE_APP_COMPOSITIONROOT_H
#define RAWPAGE_APP_COMPOSITIONROOT_H

#include "app/Services.h"
#include "core/Log.h"
#include "core/Result.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"

#include <QString>

#include <memory>

namespace rawpage::app {

class CompositionRoot {
public:
    struct Options {
        // Empty means `core::Settings::defaultFilePath()`. A test points it at a scratch file so
        // that running the suite does not rewrite the settings of the person running it.
        QString settingsPath;

        // Where a log record goes once it has passed the level and subsystem filters. Supplied by
        // main.cpp, which owns the process's output channels (the debugger, stderr, and from
        // Stage 14 the rolling file). Empty leaves whatever sink is already installed alone.
        core::LogSink baseSink;

        // False assembles every service EXCEPT the OS bundle, and `platform()` then returns an
        // empty `Platform` whose members are all null. Used by a headless or diagnostic run that
        // has no business creating a named mutex.
        bool createPlatform = true;
    };

    explicit CompositionRoot(Options options);
    ~CompositionRoot();

    CompositionRoot(const CompositionRoot&) = delete;
    CompositionRoot& operator=(const CompositionRoot&) = delete;

    // The aggregate handed downward. Valid for the lifetime of this object, which is the lifetime
    // of the process: main.cpp keeps it on the stack around `exec()`.
    Services services();

    core::Settings&        settings() { return *settings_; }
    core::RecentFiles&     recentFiles() { return *recentFiles_; }
    core::ActionRegistry&  actions() { return *actions_; }
    core::KeyBindingStore& keyBindings() { return *keyBindings_; }
    platform::Platform&    platform() { return *platform_; }

    // How many per-user shortcut overrides were read out of settings.ini and accepted. A key naming
    // an ActionId that no longer exists is dropped with a log line rather than retained silently --
    // `core::Settings::droppedShortcutKeyCount()` counts those, and the two numbers together are
    // what makes a lost customization diagnosable instead of mysterious.
    std::size_t importedShortcutOverrides() const { return importedOverrides_; }

private:
    void applyLoggingSettings();

    Options options_;

    std::unique_ptr<core::Settings>        settings_;
    std::unique_ptr<core::RecentFiles>     recentFiles_;
    std::unique_ptr<core::ActionRegistry>  actions_;
    std::unique_ptr<core::KeyBindingStore> keyBindings_;
    std::unique_ptr<platform::Platform>    platform_;

    int         settingsSubscription_ = 0;
    std::size_t importedOverrides_ = 0;
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_COMPOSITIONROOT_H
