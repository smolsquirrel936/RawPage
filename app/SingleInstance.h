// First instance or second? -- flow B in Architecture-FileMap.md §5.
//
// This class is thin on purpose. All of the hard parts live in `platform::ISingleInstanceBus`
// (decision D1-11): the `Local\` mutex that decides who is primary, the named pipe whose name
// carries the session id, and `AllowSetForegroundWindow` called by the SECONDARY before it exits,
// because Windows refuses foreground activation to a process that received no input. What is left
// here is policy, and policy is app/'s job:
//
//   * whether single-instance behaviour is wanted at all (`general/singleInstance`);
//   * what the scope is -- which builds of RawPage on one desktop should consider each other the
//     same application;
//   * what a secondary does with its file list before it exits.
//
// WHY THE SCOPE IS DERIVED FROM THE EXECUTABLE PATH. Two builds of the same product on one desktop
// is a development reality, not an edge case: a debug build handing its file to a release build
// running from another directory would be baffling, and worse, the release build would open a
// document the developer meant to debug. Hashing the executable's own directory makes two installs
// two applications and one install one application, with no configuration to get wrong.

#ifndef RAWPAGE_APP_SINGLEINSTANCE_H
#define RAWPAGE_APP_SINGLEINSTANCE_H

#include "core/Result.h"
#include "platform/platform.h"

#include <QString>
#include <QStringList>

namespace rawpage::app {

class SingleInstance {
public:
    enum class Role {
        Primary,    // we claimed the name; carry on and listen for handoffs
        Secondary,  // somebody else has it; forward and exit
        Disabled,   // `general/singleInstance` is false, or the platform has no bus
    };

    // `bus` may be null -- an OS bundle that does not implement ISingleInstanceBus is a legal
    // state of `platform::Platform`, and the honest response is Disabled rather than a crash.
    SingleInstance(platform::ISingleInstanceBus* bus, QString scopeId);
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // The scope id for a given executable path. Static and pure so a test can assert that two
    // paths differ and one path is stable, without creating any OS object.
    static QString scopeIdForExecutable(const QString& executablePath);

    // `enabled` is `general/singleInstance`. A claim is attempted only when it is true.
    core::Result<Role> claim(bool enabled);

    Role role() const { return role_; }

    // Primary side. The handler is invoked on the thread that called this.
    core::Status listen(platform::ISingleInstanceBus::OpenFilesHandler handler);

    // Secondary side. Sends the file list AND grants the primary the right to take the foreground;
    // the bus does both, because either alone is the failure D1-11 exists to prevent.
    core::Status forward(const QStringList& files);

    void release();

private:
    platform::ISingleInstanceBus* bus_ = nullptr;
    QString scopeId_;
    Role    role_ = Role::Disabled;
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_SINGLEINSTANCE_H
