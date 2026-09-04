// The Windows `IDisplayTopology` -- monitors, their real DPI, and change notification. Stage 2 T12.
//
// ---------------------------------------------------------------------------------------------
// WHY THIS IS THE FILE `DpiPolicy` HAS BEEN WAITING FOR.
//
// `core/render/DpiPolicy` takes THREE inputs and one of them has been a guess since T10: it needs
// the monitor's **physical** DPI -- the dots per inch of actual glass -- because *Actual Size means
// 100 % at the physical DPI* and a ruler held against the screen must agree with the ruler drawn on
// the page. Until this file existed, every caller passed `PhysicalDpi::nominal(scale)`, which is
// `96 * scale`. On the commodity desktop that is exactly right and the substitution is invisible.
// On a 157-DPI laptop panel that Windows drives at 150 %, the nominal answer is 144 and the truth
// is 157: a 9 % error that renders, looks nearly right, and is wrong.
//
// So `monitors()` reports the effective DPI and the raw DPI **separately**, and neither is derived
// from the other.
//
//   scaleFactor   = effective DPI / 96   -- what Windows wants: device pixels per logical pixel
//   physicalDpi   = raw DPI              -- what the glass has, from the panel's own EDID
//
// ---------------------------------------------------------------------------------------------
// THE NUMBERS ARE ONLY TRUE IF THE PROCESS IS PER-MONITOR AWARE, AND THAT IS CHECKABLE.
//
// A DPI-unaware process is handed **virtualized** figures by every one of these APIs: a 3840x2160
// panel at 150 % reports itself as 2560x1440 at 100 %, and the numbers are self-consistent, so
// nothing looks wrong. `app/main.cpp` sets per-monitor-v2 awareness before any Qt object exists, so
// the product is fine -- but a test binary, a diagnostic tool or a future satellite process is not,
// unless it does the same.
//
// The guide says this in as many words about the P-1 rig (case A-60): *a rig that has only been
// measured by the thing that might be broken is not a rig*. `processIsPerMonitorAware()` is what
// lets a caller state the precondition rather than assume it, and this class LOGS A WARNING when it
// is asked for monitors from a virtualized process -- because the alternative is plausible numbers
// with no indication that they are fiction.
//
// ---------------------------------------------------------------------------------------------
// `changed` FIRES ON A REAL DIFFERENCE, NOT ON A MESSAGE.
//
// Windows announces a topology change through several messages, more than one of which usually
// arrives for a single user action, and some of which arrive when nothing relevant moved at all
// (`WM_SETTINGCHANGE` is broadcast for a very great many things). Firing per message would make
// case A-61's *"emits `changed` **once**"* untrue by construction, and would make every subscriber
// re-lay-out a document because someone changed their mouse pointer speed.
//
// So every notification takes a fresh snapshot and compares it with the last one. Identical means
// silence. That is also what makes the negative control possible: the same message, with nothing
// actually changed, must produce nothing.

#ifndef RAWPAGE_PLATFORM_WIN_WINDISPLAYTOPOLOGY_H
#define RAWPAGE_PLATFORM_WIN_WINDISPLAYTOPOLOGY_H

#include "platform/platform.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace rawpage::platform::win {

class WinMessageWindow;

class WinDisplayTopology final : public IDisplayTopology {
public:
    WinDisplayTopology();
    ~WinDisplayTopology() override;

    // Never fails on a machine with a display: an enumeration that returns nothing is reported as
    // an error rather than as an empty list, because "no monitors" and "the query failed" lead a
    // caller to opposite conclusions and only one of them is survivable.
    Result<std::vector<MonitorInfo>> monitors() const override;

    // ADDS a subscriber; it does not replace one. Two consumers of a topology change are the
    // ordinary case -- the render policy and, from Stage 4, the session-state writer -- and an
    // interface whose second subscriber silently unhooks the first is a defect that only appears
    // once someone adds the second.
    //
    // The handler runs on the thread that constructed this object, from inside its message pump.
    Status subscribe(ChangedHandler) override;

    // Whether THIS PROCESS will be told the truth by the queries above. False means every figure
    // is virtualized; see the header. Static because it is a property of the process, and a caller
    // may want to assert it before constructing anything.
    static bool processIsPerMonitorAware();

    // The `HWND` the OS's change messages arrive at, as an opaque integer. Zero when the window
    // could not be created, which is also what makes `subscribe()` fail.
    std::uintptr_t notificationHandle() const noexcept;

    // How many times `changed` has actually been raised, and how many OS notifications were
    // examined and found to have changed nothing. The second number is the one that makes the
    // de-duplication above assertable rather than merely described.
    int changeCount() const noexcept { return changeCount_; }
    int suppressedNotificationCount() const noexcept { return suppressed_; }

private:
    bool onMessage(unsigned int message, std::uint64_t wParam, std::int64_t lParam);
    void reevaluate();

    std::unique_ptr<WinMessageWindow> window_;
    std::vector<ChangedHandler> handlers_;
    std::vector<MonitorInfo> last_;
    int changeCount_ = 0;
    int suppressed_ = 0;
};

// True when the two describe the same arrangement of the same monitors. Free rather than
// `operator==` on `MonitorInfo`, because `platform/platform.h` is a boundary header and an equality
// operator on a value type there would be a promise about every future field.
bool sameTopology(const std::vector<MonitorInfo>& a, const std::vector<MonitorInfo>& b) noexcept;

} // namespace rawpage::platform::win

#endif // RAWPAGE_PLATFORM_WIN_WINDISPLAYTOPOLOGY_H
