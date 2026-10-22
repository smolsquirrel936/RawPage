// A Ctrl+C or Ctrl+Break typed into the console RawPage was launched from: a request to close, not a kill.
//
// WHY THIS EXISTS (Stage 6 guide I-35, `Stage6-Annotations-Testing-Guide.md` section 8). `app/main.cpp` attaches this
// GUI-subsystem process to its parent's console so that a person can read `--version` and the log. Attaching has a
// second effect nobody asked for: Windows delivers that console's Ctrl+C to EVERY process attached to it, and a
// process with no handler gets the default one -- `ExitProcess(STATUS_CONTROL_C_EXIT)`, on a thread of the system's.
// No close protocol, no session captured, the recovery journal left on disk for the next launch to sweep. Found from
// the user's own terminal on 2026-10-22: a Ctrl+C pressed to get the prompt back ended the product, and its next launch
// logged `recovery: 1 journal(s) ... held nothing unsaved; 1 removed`.
//
// WHAT IT DOES INSTEAD. The first Ctrl+C asks the GUI thread to close every window, exactly as closing each by hand
// would: a dirty document asks its question, the session is captured at `QEvent::Close`, the event loop ends. If a
// window refuses -- Cancel on a dirty document -- the product stays up, and the next Ctrl+C asks again.
//
// A Ctrl+C that arrives while the first is STILL WAITING -- the GUI thread is hung, or the first one's question is still
// on screen -- ends the process with TerminateProcess and STATUS_CONTROL_C_EXIT. That is the familiar "press it again to
// force", and it is what a person debugging a hang needs; the recovery journal (D3-11) is what makes it safe for unsaved
// work. Not by handing the event on to Windows' default: that is ExitProcess, whose teardown races the GUI thread and
// crashed in 3 of 40 measured runs -- see the .cpp.
//
// CTRL_CLOSE_EVENT, LOGOFF and SHUTDOWN ARE NOT HANDLED: Windows ends the process when the handler returns, whatever it
// returns, so there is no close to wait for. The journal covers them as it covers a crash.
//
// THE HANDLER RUNS ON A THREAD WINDOWS CREATES, so the only Qt it touches is `QMetaObject::invokeMethod` with a queued
// connection -- a posted event, which is thread-safe -- and only under a mutex the destructor also takes, so it never
// posts to a target that is being destroyed. The close itself runs on the context's thread WITHOUT that mutex held,
// because it may sit in a dirty document's question for as long as the person likes, and the second Ctrl+C must still
// be able to look.
//
// ONE PER PROCESS: Windows keeps one handler list per process, and a second instance would be a second vote. A second
// construction while one is alive installs nothing and says so (`installed()`).

#ifndef RAWPAGE_APP_CONSOLEINTERRUPT_H
#define RAWPAGE_APP_CONSOLEINTERRUPT_H

#include <functional>
#include <memory>

class QObject;

namespace rawpage::app {

class ConsoleInterrupt final {
public:
    // `requestClose` runs on `context`'s thread, once for each Ctrl+C accepted, and returns true when the close went
    // ahead (the process is on its way out) and false when something refused it.
    ConsoleInterrupt(QObject& context, std::function<bool()> requestClose);
    ~ConsoleInterrupt();

    ConsoleInterrupt(const ConsoleInterrupt&) = delete;
    ConsoleInterrupt& operator=(const ConsoleInterrupt&) = delete;

    bool installed() const { return state_ != nullptr; }

    // The product's request: `QApplication::closeAllWindows()`, which sends each top-level window the `QEvent::Close`
    // its own title-bar button would, then true unless a window is still shown -- one whose close was refused.
    static bool closeAllWindows();

    struct State;   // defined in the .cpp, where the handler reads it

private:
    std::unique_ptr<State> state_;
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_CONSOLEINTERRUPT_H
