#include "platform/win/PlatformFactory.h"

#include "core/Log.h"
#include "platform/win/WinShellIntegration.h"
#include "platform/win/WinSingleInstanceBus.h"

#include <QCoreApplication>

namespace rawpage::platform {
namespace {

using core::LogSubsystem;

} // namespace

namespace win {

std::unique_ptr<Platform> createWindowsPlatform(const PlatformOptions& options)
{
    auto platform = std::make_unique<Platform>();

    platform->singleInstanceBus = std::make_unique<WinSingleInstanceBus>();
    platform->shellIntegration =
        std::make_unique<WinShellIntegration>(options.executablePath, options.registryRoot);

    // Every other slot stays null, and the count is LOGGED rather than left implicit. "Fifteen of
    // seventeen interfaces are not built yet" is a fact a later stage should be able to see in a
    // support log without reading this file.
    const std::vector<std::string_view> missing = unimplementedInterfaces(*platform);
    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "platform: Windows bundle assembled --"
        << static_cast<int>(kPlatformInterfaceCount - missing.size()) << "of"
        << static_cast<int>(kPlatformInterfaceCount) << "interfaces implemented";

    for (const std::string_view name : missing) {
        RAWPAGE_LOG_DEBUG(LogSubsystem::Ui)
            << "platform: not implemented in this build --" << std::string(name).c_str();
    }

    return platform;
}

} // namespace win

std::unique_ptr<Platform> createPlatform()
{
    // The path comes from Qt rather than from `GetModuleFileNameW`, for the reason recorded at T1
    // and made a standing convention: nothing in RawPage may read a path out of `argv`, and Qt's
    // answer is sourced from the wide command line rather than the ANSI one (issue I-04).
    //
    // Before `QCoreApplication` exists this is empty, and that is handled rather than asserted:
    // `WinShellIntegration` refuses to REGISTER without a path and still answers questions about
    // the association, which is exactly what a diagnostic run needs.
    win::PlatformOptions options;
    options.executablePath = QCoreApplication::applicationFilePath();

    return win::createWindowsPlatform(options);
}

} // namespace rawpage::platform
