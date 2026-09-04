#include "platform/win/WinPowerSession.h"

#include "core/Log.h"
#include "platform/win/WinMessageWindow.h"

#include <windows.h>
#include <wtsapi32.h>

#include <cstring>
#include <utility>

namespace rawpage::platform::win {
namespace {

using core::LogSubsystem;

// GUID_ACDC_POWER_SOURCE, spelled out rather than linked.
//
// `winuser.h` declares it `extern const GUID` and the definition lives in an import library whose
// name differs between toolchains -- so naming the symbol makes the link depend on which SDK the
// compiler shipped with, which is precisely the class of surprise `cmake/toolchain-mingw.cmake`
// exists to keep out of this project. Sixteen bytes written here are a fixed, published constant.
constexpr GUID kAcDcPowerSource = {
    0x5d3e9a59, 0xe9d5, 0x4b00, {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}};

// The bit `GetSystemPowerStatus` sets when the machine has no battery at all.
constexpr BYTE kBatteryFlagNoSystemBattery = 128;

bool readPowerStatus(SYSTEM_POWER_STATUS& into)
{
    if (::GetSystemPowerStatus(&into) != 0) return true;

    RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
        << "power: GetSystemPowerStatus failed -- win32 error"
        << static_cast<int>(::GetLastError());
    return false;
}

} // namespace

const char* WinPowerSession::eventName(Event event) noexcept
{
    switch (event) {
    case Event::Suspending: return "Suspending";
    case Event::Resumed: return "Resumed";
    case Event::OnBattery: return "OnBattery";
    case Event::OnMains: return "OnMains";
    case Event::SessionLocked: return "SessionLocked";
    case Event::SessionUnlocked: return "SessionUnlocked";
    }
    return "Unknown";
}

WinPowerSession::WinPowerSession() = default;

WinPowerSession::~WinPowerSession()
{
    // The keep-awake first: it is per-thread state that outlives this object if it is not released,
    // and a crash-free process that never sleeps again is the worst kind of leak because nothing
    // ever reports it.
    if (presentationMode_) {
        ::SetThreadExecutionState(ES_CONTINUOUS);
        presentationMode_ = false;
    }

    if (powerNotification_ != nullptr) {
        ::UnregisterPowerSettingNotification(static_cast<HPOWERNOTIFY>(powerNotification_));
        powerNotification_ = nullptr;
    }

    if (sessionNotificationRegistered_ && window_) {
        ::WTSUnRegisterSessionNotification(reinterpret_cast<HWND>(window_->nativeHandle()));
        sessionNotificationRegistered_ = false;
    }

    window_.reset();
    handlers_.clear();
}

bool WinPowerSession::hasBattery() const
{
    SYSTEM_POWER_STATUS status{};
    if (!readPowerStatus(status)) return false;
    return (status.BatteryFlag & kBatteryFlagNoSystemBattery) == 0;
}

bool WinPowerSession::isOnBattery() const
{
    SYSTEM_POWER_STATUS status{};
    if (!readPowerStatus(status)) return false;

    // No battery is not "on battery", whatever `ACLineStatus` says. A desktop whose UPS briefly
    // reports 255 (unknown) must not make the product behave as though it were unplugged.
    if ((status.BatteryFlag & kBatteryFlagNoSystemBattery) != 0) return false;

    // 0 = offline (running from the battery), 1 = online, 255 = unknown. Unknown is treated as
    // MAINS: the conservative reading here is the one that keeps rendering at full speed, because
    // the cost of being wrong is battery life and the cost of the other error is a viewer that
    // feels broken for no reason the user can see.
    return status.ACLineStatus == 0;
}

Status WinPowerSession::subscribe(EventHandler handler)
{
    if (!handler) {
        return core::Error{core::ErrorCode::InvalidArgument,
                           QLatin1String("error.platform.powerSubscribe"),
                           QStringLiteral("null handler")};
    }

    if (!window_) {
        window_ = std::make_unique<WinMessageWindow>(
            QStringLiteral("PowerSession"),
            [this](unsigned int message, std::uint64_t wParam, std::int64_t lParam) {
                return onMessage(message, wParam, lParam);
            });

        if (!window_->isValid()) {
            window_.reset();
            return core::Error{core::ErrorCode::IoFailure,
                               QLatin1String("error.platform.powerSubscribe"),
                               QStringLiteral("could not create the notification window")};
        }

        auto hwnd = reinterpret_cast<HWND>(window_->nativeHandle());

        // AC/DC transitions. `WM_POWERBROADCAST` carries suspend and resume without a registration;
        // the power SOURCE requires one, and it is the half the throttle depends on.
        powerNotification_ =
            ::RegisterPowerSettingNotification(hwnd, &kAcDcPowerSource, DEVICE_NOTIFY_WINDOW_HANDLE);
        if (powerNotification_ == nullptr) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
                << "power: could not register for AC/DC notifications -- win32 error"
                << static_cast<int>(::GetLastError());
        }

        // Lock and unlock. NOTIFY_FOR_THIS_SESSION rather than ALL_SESSIONS: another user locking
        // their session is not an event about this document, and asking for it needs a privilege
        // this process has no business holding.
        sessionNotificationRegistered_ =
            ::WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION) != 0;
        if (!sessionNotificationRegistered_) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
                << "power: could not register for session lock notifications -- win32 error"
                << static_cast<int>(::GetLastError());
        }
    }

    handlers_.push_back(std::move(handler));
    return {};
}

Status WinPowerSession::setPresentationModeActive(bool active)
{
    if (active == presentationMode_) return {};

    // ES_CONTINUOUS is what makes the request STICK rather than count as a single nudge; the two
    // flags together are "the screen must stay on and the machine must stay awake". Dropping it is
    // the ordinary `ES_CONTINUOUS` alone, which reinstates the user's own power policy rather than
    // forcing sleep.
    const EXECUTION_STATE requested =
        active ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED) : ES_CONTINUOUS;

    if (::SetThreadExecutionState(requested) == 0) {
        return core::Error{core::ErrorCode::IoFailure,
                           QLatin1String("error.platform.presentationMode"),
                           QStringLiteral("SetThreadExecutionState: win32 error %1")
                               .arg(static_cast<int>(::GetLastError()))};
    }

    presentationMode_ = active;
    RAWPAGE_LOG_DEBUG(LogSubsystem::Ui)
        << "power: presentation mode" << (active ? "held" : "released");
    return {};
}

std::uintptr_t WinPowerSession::notificationHandle() const noexcept
{
    return window_ ? window_->nativeHandle() : 0;
}

bool WinPowerSession::onMessage(unsigned int message, std::uint64_t wParam, std::int64_t lParam)
{
    if (message == WM_POWERBROADCAST) {
        switch (static_cast<UINT>(wParam)) {
        case PBT_APMSUSPEND:
            deliver(Event::Suspending);

            // TRUE, and consumed. A window that returns the default answer to a suspend is
            // declining to acknowledge it; Windows proceeds either way on a modern machine, but
            // acknowledging is what the message is for.
            return true;

        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMESUSPEND:
            deliver(Event::Resumed);
            return true;

        case PBT_POWERSETTINGCHANGE: {
            const auto* setting = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
            if (setting == nullptr || setting->PowerSetting != kAcDcPowerSource
                || setting->DataLength < sizeof(DWORD)) {
                return false;
            }

            DWORD source = 0;
            std::memcpy(&source, setting->Data, sizeof(source));

            // 0 = AC, 1 = DC, 2 = a short-term source such as a UPS. Two is reported as mains: a
            // UPS is a machine that is plugged in and briefly is not, and throttling the renderer
            // for the length of a power blip is a visible stutter bought for nothing.
            deliver(source == 1 ? Event::OnBattery : Event::OnMains);
            return true;
        }

        default:
            return false;
        }
    }

    if (message == WM_WTSSESSION_CHANGE) {
        switch (static_cast<UINT>(wParam)) {
        case WTS_SESSION_LOCK:
            deliver(Event::SessionLocked);
            return false;
        case WTS_SESSION_UNLOCK:
            deliver(Event::SessionUnlocked);
            return false;
        default:
            return false;
        }
    }

    return false;
}

void WinPowerSession::deliver(Event event)
{
    ++delivered_;
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "power:" << eventName(event);

    // A copy, for `WinDisplayTopology::reevaluate()`'s reason: a handler may subscribe another.
    const std::vector<EventHandler> handlers = handlers_;
    for (const EventHandler& handler : handlers) {
        if (handler) handler(event);
    }
}

} // namespace rawpage::platform::win
