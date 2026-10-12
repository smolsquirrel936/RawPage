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

#include <QString>

#include <functional>

namespace rawpage::core {
class ActionRegistry;
class IDocumentBackend;
class KeyBindingStore;
class LogBuffer;
class ProfileStore;
class RecentFiles;
class Settings;

namespace render {
class RenderCache;
class RenderScheduler;
}
namespace text {
class DocumentIndex;
class SearchEngine;
}
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

    // THE ENGINE, and it arrived at T9 with its first consumer -- exactly as the note above said
    // it would. `MainWindow::openFiles()` asks it whether a file is a PDF instead of looking at
    // the suffix, which is guide case A-47 and the end of Stage 1's D1-2.
    //
    // Note the namespace: `core::IDocumentBackend`, not `pdf::`. The interface is declared in
    // `core/backend.h`, so handing it to `ui/` widens nothing -- `ui/` looks outward to exactly two
    // files (convention 9) and this is not a third.
    core::IDocumentBackend& backend;

    // THE RENDER PIPELINE, and it arrived at T13 with `ui/viewer/PageView` -- the note at the top
    // of this file predicted the scheduler and not the cache, so the second one is argued here
    // rather than slipped in.
    //
    // `ui/viewer/TilePresenter` enqueues into the scheduler and takes a ready-subscription from it.
    // The **cache** is a second service because closing a document has to drop its tiles, and
    // nothing else can: the scheduler holds a `RenderCache*` privately and has no
    // "forget this document" of its own, so a viewer that only cancelled would leave a closed
    // document's megabytes resident for the life of the process. `DocumentWindow`'s destructor is
    // the one caller.
    //
    // Both are `core::` types, so this widens nothing -- `ui/` still looks outward to exactly two
    // files (convention 9).
    core::render::RenderScheduler& renderScheduler;
    core::render::RenderCache&     renderCache;

    // WHERE EXTRACTED TEXT LIVES (Stage 5 T6, D5-4), and it arrived with its consumers: every
    // `DocumentWindow` attaches its handle and hands a mutation's `Scope` to it, and the window's
    // `SelectionOverlay` reads its pages from it rather than keeping its own. ONE per process, keyed
    // by document identity, so a `window.new` companion reads the pages its leader already extracted.
    core::text::DocumentIndex&     documentIndex;

    // THE SEARCH OVER IT (Stage 5 T7), arriving with its first widget as this file's rule asks: each
    // document window's find bar (`ui/write/FindReplaceBar`) runs its searches here. ONE per process --
    // its workers match for every window -- and its results are delivered on the UI thread by the pump
    // `app/CompositionRoot` installs, as the scheduler's are.
    core::text::SearchEngine&      searchEngine;

    // The OS bundle, whose own members are null wherever an interface is not implemented yet.
    // `unimplementedInterfaces()` names them, so a Stage 1 window can print what it cannot do
    // rather than discover it by dereferencing.
    platform::Platform&    platform;

    // WHERE RECOVERY JOURNALS GO (Stage 3 T10). Empty means `core::RecoveryJournal::defaultRoot()` --
    // `%LOCALAPPDATA%\RawPage\recovery`. A value rather than a reference, and last with a default, so
    // every existing aggregate initialiser still compiles; a test points it at a temporary directory so
    // a suite never writes into the user's own recovery folder. `documents/recoveryDir` replaces it at T12.
    QString recoveryRoot = {};

    // WHO THE PERSON AT THE KEYBOARD IS, as the operating system names them (Stage 6 T6, D6-13): what
    // `identity/author` means when it is empty, through `core::identityAuthor`. A value the HOST supplies --
    // `app/CompositionRoot` reads it -- so `core/` reads no environment (convention 11), and a case composing
    // the product sets it, so a comment's `/T` never depends on who ran the suite.
    QString loginName = {};

    // THE CUSTOM-STAMP LIBRARY (Stage 6 T10, D6-16): ONE per process, so every window's palette shows the same
    // stamps and a stamp made in one is in all of them. NULL when the composition was given no root
    // (`CompositionRoot::Options::stampRoot`) -- a test, a diagnostic run -- and the palette then offers the
    // built-in stamps alone.
    core::ProfileStore* stamps = nullptr;

    // WHERE AN OPENED ATTACHMENT IS WRITTEN (Stage 6 T11, D6-17): this session's folder,
    // `%LOCALAPPDATA%\RawPage\attachments\<process id>`, written into by `pdf/` alone (`IDocumentBackend::
    // extractAttachment`) and EMPTIED AT A CLEAN QUIT (`app/CompositionRoot`). EMPTY when the composition was given
    // none (`CompositionRoot::Options::attachmentRoot`) -- a test, a diagnostic run -- and opening an attachment that
    // is not a PDF is then refused by key: nothing is written outside a folder somebody chose.
    QString attachmentRoot = {};

    // HANDS A FILE TO THE SHELL (Stage 6 T11): what opens an extracted attachment with the program Windows uses for
    // it -- `QDesktopServices::openUrl`, installed by `app/CompositionRoot`. EMPTY in a composition that installs
    // none, and a case installs a recorder: nothing a suite does may launch a program.
    std::function<bool(const QString& path)> openExternally = {};
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_SERVICES_H
