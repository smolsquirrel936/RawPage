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
// Stage 2 adds `pdf::IDocumentBackend&` and `core::render::RenderScheduler&` here. They are NOT
// declared now as null members: unlike a platform interface, those types do not exist yet, and a
// forward declaration of a type nobody can define is a promise rather than a boundary.

#ifndef RAWPAGE_APP_SERVICES_H
#define RAWPAGE_APP_SERVICES_H

namespace rawpage::core {
class ActionRegistry;
class KeyBindingStore;
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

    // The OS bundle, whose own members are null wherever an interface is not implemented yet.
    // `unimplementedInterfaces()` names them, so a Stage 1 window can print what it cannot do
    // rather than discover it by dereferencing.
    platform::Platform&    platform;
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_SERVICES_H
