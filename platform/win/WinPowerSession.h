// The Windows `IPowerSession` -- battery, suspend/resume, session lock, and presentation mode.
// Stage 2 T12.
//
// ---------------------------------------------------------------------------------------------
// WHAT THE RENDER PIPELINE ACTUALLY WANTS FROM THIS FILE.
//
// One number: how many render workers may run. `core/render/RenderScheduler::setActiveWorkerLimit()`
// exists for it and clamps to `[1, workerCount]` -- never zero, because a viewer that stops
// rendering on battery is a viewer that looks broken. `performance/throttleOnBattery` is the switch
// and it arrives at **T15**, with the rest of the sixteen settings keys; until then this class
// reports the state and nothing consumes it, which is the honest ordering rather than a wire to a
// key that does not exist.
//
// The other two consumers are later and are named here so the surface is not mistaken for
// speculation: a suspend must be able to drop the disk cache's open handles (Stage 4's session
// state), and a session lock is when a protected document should stop being on screen (Stage 10).
//
// ---------------------------------------------------------------------------------------------
// "NO BATTERY" IS NOT "ON MAINS", AND THE DIFFERENCE IS A TEST RESULT.
//
// `GetSystemPowerStatus` answers `ACLineStatus` and a `BatteryFlag` that carries a distinct
// *no system battery* bit. A desktop is not a laptop that happens to be plugged in: a throttling
// policy on a desktop is dead code, and guide case A-62 requires the case to report **not
// applicable** explicitly rather than pass vacuously by observing that a desktop is on mains. So
// `hasBattery()` is a separate question from `isOnBattery()`, and the second is false on a machine
// that answers false to the first.
//
// ---------------------------------------------------------------------------------------------
// PRESENTATION MODE IS PER-THREAD, AND THAT IS A REAL CONSTRAINT.
//
// `SetThreadExecutionState` binds the request to the CALLING THREAD and Windows drops it when that
// thread exits. So this object must be used from one thread -- the UI thread, in the product -- and
// it says so rather than leaving a caller to discover that a keep-awake taken on a worker
// evaporated when the pool shrank. It is also strictly scoped: held during a presentation and at no
// other time, because a viewer that holds it permanently is a laptop that never sleeps.

#ifndef RAWPAGE_PLATFORM_WIN_WINPOWERSESSION_H
#define RAWPAGE_PLATFORM_WIN_WINPOWERSESSION_H

#include "platform/platform.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace rawpage::platform::win {

class WinMessageWindow;

class WinPowerSession final : public IPowerSession {
public:
    WinPowerSession();
    ~WinPowerSession() override;

    // False on a machine with no battery, as well as on one running from the mains. See the header:
    // the two are told apart by `hasBattery()`, and collapsing them is what makes a desktop's
    // throttling case pass without having tested anything.
    bool isOnBattery() const override;

    // ADDS a subscriber, for `WinDisplayTopology`'s reason. Handlers run on the thread that
    // constructed this object, from its message pump.
    Status subscribe(EventHandler) override;

    // Per-thread, and released on destruction. Idempotent: setting true twice is one request, and
    // setting false with none held is not an error.
    Status setPresentationModeActive(bool) override;

    // Whether this machine has a battery at all. `false` means every battery-conditional policy in
    // the product is inert here, which is a thing a test must be able to say out loud (A-62).
    bool hasBattery() const;

    bool isPresentationModeActive() const noexcept { return presentationMode_; }

    // The `HWND` the OS's power and session messages arrive at, as an opaque integer. Zero until
    // `subscribe()` has been called successfully.
    std::uintptr_t notificationHandle() const noexcept;

    // How many events this object has delivered. The counter is here rather than in a test's
    // closure so that the negative -- an unrelated message delivering nothing -- is assertable
    // against the object itself.
    int deliveredEventCount() const noexcept { return delivered_; }

    // The name of an `Event`, ASCII and stable. A log line and a test message; never user-facing,
    // for `core/Result.h`'s reason.
    static const char* eventName(Event event) noexcept;

private:
    bool onMessage(unsigned int message, std::uint64_t wParam, std::int64_t lParam);
    void deliver(Event event);

    std::unique_ptr<WinMessageWindow> window_;
    std::vector<EventHandler> handlers_;

    // Opaque `HPOWERNOTIFY`, kept as a void* so this header names no Win32 type.
    void* powerNotification_ = nullptr;
    bool sessionNotificationRegistered_ = false;
    bool presentationMode_ = false;
    int delivered_ = 0;
};

} // namespace rawpage::platform::win

#endif // RAWPAGE_PLATFORM_WIN_WINPOWERSESSION_H
