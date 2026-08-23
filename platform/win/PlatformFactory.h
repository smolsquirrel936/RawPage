// The Windows bundle assembly.
//
// `platform::createPlatform()` is declared in `platform/platform.h` and defined here. This header
// exists for the one thing the free function cannot express: the executable path that
// `WinShellIntegration` needs in the command it registers.
//
// WHY THE PATH IS NOT RESOLVED INSIDE THE FACTORY. `GetModuleFileNameW` would work and would be
// wrong: `app/` already knows where it launched from, a portable install may want to register a
// different path than the running image, and a test must be able to assemble a bundle without
// registering anything at all. One question, answered in one place -- the composition root.

#ifndef RAWPAGE_PLATFORM_WIN_PLATFORMFACTORY_H
#define RAWPAGE_PLATFORM_WIN_PLATFORMFACTORY_H

#include "platform/platform.h"

#include <QString>

#include <memory>

namespace rawpage::platform::win {

struct PlatformOptions {
    // Written into the registered shell command. Empty means `WinShellIntegration` can still be
    // asked about the association but refuses to register one -- which is the correct behaviour for
    // a process that does not know what it is.
    QString executablePath;

    // Defaults to `Software\Classes`. A test points it at a scratch subkey so the registration path
    // runs for real without touching the association a person is using.
    QString registryRoot = QStringLiteral("Software\\Classes");
};

// The bundle, with exactly the two Stage 1 interfaces non-null. Everything else is null on purpose
// and is listed by `unimplementedInterfaces()` -- see the note in `platform/platform.h` about why a
// null beats a silently-succeeding stub.
std::unique_ptr<Platform> createWindowsPlatform(const PlatformOptions& options);

} // namespace rawpage::platform::win

#endif // RAWPAGE_PLATFORM_WIN_PLATFORMFACTORY_H
