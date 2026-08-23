// The Windows `ISingleInstanceBus` -- decision D1-11.
//
// ---------------------------------------------------------------------------------------------
// THE THREE PARTS, AND WHY EACH IS THE PART IT IS.
//
//   1. A NAMED MUTEX in the `Local\` namespace decides who is primary. `Local\` rather than
//      `Global\` is a deliberate PER-SESSION scope: two users on one machine, or one user across
//      fast-user-switching, each get their own instance. A `Global\` name means the second user
//      cannot run the app at all -- a support incident nobody diagnoses quickly, which is why the
//      testing guide asserts the prefix (case A-62).
//
//   2. A NAMED PIPE carries the file list. **Its name carries the session id explicitly**, because
//      the `Local\` prefix does NOT apply to pipes: the pipe namespace is machine-wide, so two
//      sessions with correctly separated mutexes would still meet on one pipe. Measured at T7 and
//      recorded here rather than in a commit message, because the two halves look symmetric and
//      are not.
//
//   3. `AllowSetForegroundWindow` is called by the SECONDARY, before it exits, naming the primary's
//      process id. Windows refuses foreground activation to a process that received no input; the
//      secondary is the one the user just launched, so it is the only process that can hand the
//      permission over. Skip it and the file arrives, the window does not come forward, and the
//      user reports "I double-clicked and nothing happened".
//
// ---------------------------------------------------------------------------------------------
// THREADING. `listen()` starts a worker thread that blocks on the pipe. The handler is delivered on
// the thread that CALLED `listen()`, via a queued invocation, so a caller that owns a window does
// not have to marshal it themselves -- and does not accidentally touch widgets from a worker. That
// requires the calling thread to run an event loop, which every caller of this class does.

#ifndef RAWPAGE_PLATFORM_WIN_WINSINGLEINSTANCEBUS_H
#define RAWPAGE_PLATFORM_WIN_WINSINGLEINSTANCEBUS_H

#include "platform/platform.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <thread>

namespace rawpage::platform::win {

// The two OS object names, as pure functions of the scope id, so the naming rules above are
// testable without creating a kernel object. `scopeId` is sanitized: a backslash in a name would
// silently create an object in a different namespace, and a caller passing a path or a version
// string is entirely plausible.
QString singleInstanceMutexName(const QString& scopeId);
QString singleInstancePipeName(const QString& scopeId, unsigned long sessionId);

// The session this process runs in. Separated so the pipe-name test does not have to guess it.
unsigned long currentSessionId();

class WinSingleInstanceBus final : public ISingleInstanceBus {
public:
    WinSingleInstanceBus();
    ~WinSingleInstanceBus() override;

    Result<bool> claim(const QString& scopeId) override;
    bool         isPrimary() const override;
    Status       listen(OpenFilesHandler) override;
    Status       forward(const QStringList& files) override;
    void         release() override;

    // The names this instance resolved. Exposed for diagnostics and for guide case A-62, which has
    // to see the real name rather than a name rebuilt by the test.
    const QString& mutexName() const { return mutexName_; }
    const QString& pipeName() const { return pipeName_; }

private:
    void serverLoop();
    void* createPipeInstance();

    // Win32 handles as void* so this header stays free of `windows.h` -- the same rule
    // `platform/platform.h` is held to by the gate, applied one level down because this header is
    // included by `PlatformFactory.cpp` and by the tests.
    void* mutex_ = nullptr;
    void* stopEvent_ = nullptr;
    void* pendingPipe_ = nullptr;   // the instance already listening; see listen()

    QString scopeId_;
    QString mutexName_;
    QString pipeName_;

    bool primary_ = false;
    std::atomic<bool> listening_{false};
    std::thread serverThread_;

    // Owns the thread affinity of the handler delivery. Created on the thread that calls listen().
    std::unique_ptr<QObject> deliveryContext_;
    OpenFilesHandler handler_;
};

} // namespace rawpage::platform::win

#endif // RAWPAGE_PLATFORM_WIN_WINSINGLEINSTANCEBUS_H
