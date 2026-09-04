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
#include "app/LogFile.h"
#include "core/LogBuffer.h"
#include "core/Result.h"
#include "core/backend.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"
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

        // Where the rolling log file goes. EMPTY MEANS NO FILE, and that is the default on
        // purpose: `main.cpp` passes `LogFile::defaultDirectory()`, and every other caller --
        // a test, a diagnostic run -- gets no file rather than writing into the profile of
        // whoever happened to run it. The same call `Options::settingsPath` makes.
        QString logDirectory;

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
    core::LogBuffer&       logBuffer() { return *logBuffer_; }

    // Null when `Options::logDirectory` was empty, or when the directory could not be written.
    // Both are legitimate states rather than failures -- see `app/LogFile.h`.
    LogFile*               logFile() { return logFile_.get(); }
    platform::Platform&    platform() { return *platform_; }

    // The engine, behind its interface -- `app/` is the only layer that knows a concrete one
    // exists. Always non-null: an engine that implements nothing is a backend that SAYS it
    // implements nothing (`implementedInterfaces()`), not an absent one, which is why there is no
    // `Options::createBackend` mirroring `createPlatform`. Nothing but this class reads it before
    // Stage 2 T9 gives `MainWindow::openFiles()` a reason to.
    core::IDocumentBackend& backend() { return *backend_; }

    // THE RENDER PIPELINE (Stage 2 T13). One cache and one scheduler for the whole process, which
    // is O7's "shared between documents" arrangement built so the question can be MEASURED --
    // `RenderCache` counts cross-document evictions precisely so T18 can answer it with a number
    // rather than a preference (D2-14).
    //
    // The scheduler is destroyed FIRST of the three (see the destructor): its worker holds a
    // rasterizer that reaches into the backend, and the backend must outlive it.
    core::render::RenderCache&     renderCache() { return *renderCache_; }
    core::render::RenderScheduler& renderScheduler() { return *renderScheduler_; }

    // How many per-user shortcut overrides were read out of settings.ini and accepted. A key naming
    // an ActionId that no longer exists is dropped with a log line rather than retained silently --
    // `core::Settings::droppedShortcutKeyCount()` counts those, and the two numbers together are
    // what makes a lost customization diagnosable instead of mysterious.
    // HOW MANY WERE DROPPED, which is what `KeyBindingStore::importOverrides` returns. Named for
    // what it holds after I-51, where the old name `importedOverrides_` had produced a log line
    // saying the exact opposite of what had happened.
    std::size_t droppedShortcutOverrides() const { return droppedOverrides_; }

private:
    void applyLoggingSettings();

    Options options_;

    std::unique_ptr<core::Settings>        settings_;
    std::unique_ptr<core::RecentFiles>     recentFiles_;
    std::unique_ptr<core::ActionRegistry>  actions_;
    std::unique_ptr<core::KeyBindingStore> keyBindings_;

    // Built FIRST of all the services and destroyed last, because it is what every other one logs
    // into: a buffer that arrived after `Settings` would miss the migration and version lines that
    // are exactly what someone opens the log pane to read.
    std::unique_ptr<core::LogBuffer>       logBuffer_;
    std::unique_ptr<LogFile>               logFile_;
    std::unique_ptr<platform::Platform>    platform_;
    std::unique_ptr<core::IDocumentBackend> backend_;

    std::unique_ptr<core::render::RenderCache>     renderCache_;
    std::unique_ptr<core::render::RenderScheduler> renderScheduler_;

    // THE WAKE PUMP'S LIFE FLAG. `RenderScheduler`'s wake handler is called FROM A WORKER and
    // posts `deliverReady()` onto the UI thread; a post already in Qt's event queue when this
    // object is destroyed would run against a dead scheduler. The queued lambda captures a weak
    // reference to this flag and does nothing when it has expired -- convention 10's rule in the
    // form where the subscriber is a posted event rather than a signal connection.
    std::shared_ptr<bool> pumpAlive_;

    int         settingsSubscription_ = 0;
    std::size_t droppedOverrides_ = 0;
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_COMPOSITIONROOT_H
