// The bundle of interfaces app/ hands DOWNWARD, and the reason MainWindow needs no constructor
// with twelve parameters.
//
// WHY REFERENCES AND NOT POINTERS. Every service in this struct is required to exist for the
// window to work at all, and a reference says so in the type system rather than in a comment. It is
// the opposite decision from `platform::Platform`, whose members are owning pointers precisely
// because a null there is the honest state of an interface nobody has implemented yet. The two
// structs look similar and mean different things: `Platform` models "which parts of the OS do we
// speak to", `Services` models "what does the UI tree need in order to be constructible".
//
// WHY NOT A SERVICE LOCATOR. There is no `Services::instance()` and no lookup by type. A widget
// gets what it was handed, which is what makes ui/shell/MainWindow constructible in a test against
// mocks with no PDFium, no Win32 and no display -- the property flow A in Architecture-FileMap.md
// §5 exists to demonstrate.
//
// Stage 2 adds `core::IDocumentBackend&` and `core::render::RenderScheduler&` here -- both `core/`
// types, which is what keeps this header inside what `ui/` is allowed to name (it has its own
// layer-lint entry saying so).
//
// THE BACKEND EXISTS AS OF STAGE 2 T1 AND IS STILL NOT HERE, deliberately. `core/backend.h` now
// declares all seven engine interfaces and `app/CompositionRoot` owns the one instance, but nothing
// in `ui/` reads it until T9 gives `MainWindow::openFiles()` a reason to. A service handed to every
// widget ahead of the first widget that uses it is how a capability ends up built and unreachable,
// which Stage 1 found five separate times (I-34, I-42, I-43, I-46, I-47). It arrives with its
// consumer. (Note the namespace: the interfaces are `rawpage::core`, not `rawpage::pdf` -- the
// header is `core/backend.h`, and `core/render/` holds an `IPageRasterizer*` of its own.)

#ifndef RAWPAGE_APP_SERVICES_H
#define RAWPAGE_APP_SERVICES_H

namespace rawpage::core {
class ActionRegistry;
class KeyBindingStore;
class LogBuffer;
class RecentFiles;
class Settings;
}

namespace rawpage::platform {
struct Platform;
}

namespace rawpage::app {

struct Services {
    core::Settings&        settings;
    core::RecentFiles&     recentFiles;
    core::ActionRegistry&  actions;
    core::KeyBindingStore& keyBindings;

    // The in-memory tail of the log, fed from inside the one sink `CompositionRoot` installs.
    // `ui/panes/LogPane` subscribes to it. It is a service rather than something the pane reaches
    // for because `core::setLogSink` REPLACES rather than chains -- see `core/LogBuffer.h`, which
    // owns that reasoning.
    core::LogBuffer&       logBuffer;

    // The OS bundle, whose own members are null wherever an interface is not implemented yet.
    // `unimplementedInterfaces()` names them, so a Stage 1 window can print what it cannot do
    // rather than discover it by dereferencing.
    platform::Platform&    platform;
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_SERVICES_H
