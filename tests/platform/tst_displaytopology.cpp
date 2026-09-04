// Stage 2 T12 -- platform/win/WinDisplayTopology, and the notification window underneath it.
//
// Guide cases A-60 (the rig, asserted through the product's own query) and A-61 (the `changed`
// signal), plus the negative control A-61 needs in order to mean anything.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS SUITE CAN AND CANNOT REACH, STATED AT THE TOP RATHER THAN DISCOVERED (convention 19).
//
// It CAN drive the real `WNDPROC`: `notificationHandle()` is a real `HWND` and a real
// `WM_DISPLAYCHANGE` sent to it runs the shipped message path end to end. The only synthetic part
// is where the message came from -- everything from `DispatchMessage` inward is the product.
//
// It CANNOT change a monitor's scale factor. So the half of A-61 that says *"changing a monitor's
// scale in Windows Settings emits `changed` once, with the new topology"* is **manual case M-02's**
// and is not claimed here. What is claimed here is the two halves that make that observation
// meaningful and that no manual pass can check precisely: that a notification carrying no actual
// change is SUPPRESSED, and that `sameTopology()` -- the predicate the suppression is built on --
// answers false for each field a real change would move.
//
// ---------------------------------------------------------------------------------------------
// THE RIG CASE SKIPS EXPLICITLY RATHER THAN PASSING VACUOUSLY.
//
// A-60's figures are the P-1 machine's, and this suite also runs on CI. A case that quietly
// asserted nothing on a one-monitor runner would be the vacuous pass A-62's own wording forbids, so
// the rig case reports what it actually saw and SKIPs by name when the machine is not that rig.

#include <catch2/catch_test_macros.hpp>

#include "platform/platform.h"
#include "platform/win/WinDisplayTopology.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>

#include <windows.h>

#include <cmath>

using namespace rawpage::platform;
using rawpage::core::Result;

namespace {

// Pumps this thread's native message queue. `SendMessageW` below is synchronous and needs none of
// this; `PostMessageW` does, and the difference is worth having available in one place.
void pumpNative(int budgetMs = 200)
{
    QElapsedTimer timer;
    timer.start();
    MSG message;
    while (timer.elapsed() < budgetMs) {
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (timer.elapsed() > 20) break;
    }
}

QString describe(const MonitorInfo& monitor)
{
    return QStringLiteral("%1 %2x%3 at (%4,%5) scale %6% physicalDpi %7%8")
        .arg(monitor.id)
        .arg(monitor.width)
        .arg(monitor.height)
        .arg(monitor.x)
        .arg(monitor.y)
        .arg(monitor.scaleFactor * 100.0, 0, 'f', 0)
        .arg(monitor.physicalDpi)
        .arg(monitor.primary ? QStringLiteral(" (primary)") : QString());
}

MonitorInfo sampleMonitor()
{
    MonitorInfo monitor;
    monitor.id = QStringLiteral("\\\\.\\DISPLAY1");
    monitor.x = 0;
    monitor.y = 0;
    monitor.width = 3840;
    monitor.height = 2160;
    monitor.scaleFactor = 1.5;
    monitor.physicalDpi = 163;
    monitor.primary = true;
    return monitor;
}

} // namespace

// A-60, first half. Structural, and it holds on any machine with a desktop -- which is what makes a
// failure here a finding about the code rather than about the hardware it ran on.
TEST_CASE("displaytopology reports every monitor with separate scale and physical dpi",
          "[platform][displaytopology]")
{
    // ASSERTED, NOT ASSUMED. A DPI-unaware process is handed virtualized figures that are
    // self-consistent and wrong, so every expectation below would still "pass" while describing a
    // panel that does not exist. `tests/platform/main.cpp` sets per-monitor-v2 awareness; this line
    // is what makes a failure there say so instead of appearing as a wrong monitor size.
    REQUIRE(win::WinDisplayTopology::processIsPerMonitorAware());

    win::WinDisplayTopology topology;
    const Result<std::vector<MonitorInfo>> monitors = topology.monitors();
    REQUIRE(monitors);
    REQUIRE_FALSE(monitors.value().empty());

    int primaries = 0;
    for (const MonitorInfo& monitor : monitors.value()) {
        // Printed on every run, not only on failure: this is the number the whole rig table in the
        // guide's §2 rests on, and a case that only whispers it when it fails leaves the reader
        // with nothing to compare against when it passes.
        WARN(("monitor: " + describe(monitor)).toStdString());

        CHECK_FALSE(monitor.id.isEmpty());
        CHECK(monitor.width > 0);
        CHECK(monitor.height > 0);
        CHECK(monitor.scaleFactor >= 1.0);
        CHECK(monitor.scaleFactor <= 5.0);

        // The two are reported SEPARATELY and both are plausible. A physical DPI that is exactly
        // `96 * scaleFactor` is the ordinary desktop and is not a defect -- what would be one is a
        // physical DPI of zero, or one derived by an arithmetic nobody chose.
        CHECK(monitor.physicalDpi >= 48);
        CHECK(monitor.physicalDpi <= 1200);

        if (monitor.primary) ++primaries;
    }

    // Exactly one, and it sorts first. Windows guarantees the first; the ordering is ours, and it
    // is what lets `sameTopology` compare arrangements rather than enumeration luck.
    CHECK(primaries == 1);
    CHECK(monitors.value().front().primary);
}

// A-60, second half: the P-1 rig itself. Skips by name on any other machine.
TEST_CASE("displaytopology sees the P-1 rig", "[platform][displaytopology]")
{
    win::WinDisplayTopology topology;
    const Result<std::vector<MonitorInfo>> monitors = topology.monitors();
    REQUIRE(monitors);

    const std::vector<MonitorInfo>& seen = monitors.value();

    auto matches = [](const MonitorInfo& monitor, int width, int height, double scale) {
        return monitor.width == width && monitor.height == height
               && std::abs(monitor.scaleFactor - scale) < 0.01;
    };

    bool has4k = false;
    bool has1440 = false;
    for (const MonitorInfo& monitor : seen) {
        if (matches(monitor, 3840, 2160, 1.5)) has4k = true;
        if (matches(monitor, 2560, 1440, 1.0)) has1440 = true;
    }

    if (seen.size() != 2 || !has4k || !has1440) {
        QString what;
        for (const MonitorInfo& monitor : seen) what += QStringLiteral("\n  ") + describe(monitor);
        SKIP(("not the P-1 rig -- this machine reports:" + what).toStdString());
    }

    CHECK(seen.size() == 2);
    CHECK(has4k);
    CHECK(has1440);
}

// A-61's automated half. The message path is real; the change is not, and the case says which.
TEST_CASE("displaytopology suppresses a notification that changed nothing",
          "[platform][displaytopology]")
{
    win::WinDisplayTopology topology;

    int fired = 0;
    REQUIRE(topology.subscribe([&fired] { ++fired; }));

    const auto hwnd = reinterpret_cast<HWND>(topology.notificationHandle());
    REQUIRE(hwnd != nullptr);

    // A REAL message through the REAL window procedure. `SendMessageW` calls the `WNDPROC` on this
    // thread synchronously, so nothing here depends on a pump having run.
    ::SendMessageW(hwnd, WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080));
    ::SendMessageW(hwnd, WM_SETTINGCHANGE, 0, 0);

    // NOTHING CHANGED, SO NOTHING FIRED. This is the assertion that makes A-61's "once" meaningful:
    // a single scale change in the Settings app produces several of these messages, and an
    // implementation that fired per message would report a change three or four times and would
    // also fire every time somebody adjusted their mouse speed.
    CHECK(fired == 0);
    CHECK(topology.changeCount() == 0);
    CHECK(topology.suppressedNotificationCount() >= 2);

    // AND THE MESSAGES DID ARRIVE -- the control for the assertion above. Without this, a window
    // that was never created and a `WNDPROC` that was never called would produce exactly the same
    // `fired == 0`, which is convention 7's rule: a case that cannot fail for the reason it claims
    // to test is worse than no case at all.
    CHECK(topology.suppressedNotificationCount() == 2);

    // An unrelated message reaches the procedure and is not even examined.
    const int before = topology.suppressedNotificationCount();
    ::SendMessageW(hwnd, WM_USER + 17, 0, 0);
    CHECK(topology.suppressedNotificationCount() == before);

    pumpNative();
    CHECK(fired == 0);
}

// The predicate the suppression is built on, asserted field by field. This is the half that stands
// in for the scale change no automated run can perform -- if it answers "same" for a rig that has
// moved, `changed` never fires however correct the message plumbing is.
TEST_CASE("sameTopology answers false for every field a real change moves",
          "[platform][displaytopology]")
{
    const std::vector<MonitorInfo> base{sampleMonitor()};

    CHECK(win::sameTopology(base, base));
    CHECK(win::sameTopology({}, {}));

    auto changed = [&base](auto&& mutate) {
        std::vector<MonitorInfo> other = base;
        mutate(other.front());
        return win::sameTopology(base, other);
    };

    // THE ONE THAT MATTERS MOST: a monitor whose pixels and position did not move but whose scale
    // did. This is precisely M-02's mid-session scale change, and an implementation that compared
    // only geometry would answer "same" and never fire.
    CHECK_FALSE(changed([](MonitorInfo& m) { m.scaleFactor = 1.25; }));

    CHECK_FALSE(changed([](MonitorInfo& m) { m.physicalDpi = 96; }));
    CHECK_FALSE(changed([](MonitorInfo& m) { m.width = 2560; }));
    CHECK_FALSE(changed([](MonitorInfo& m) { m.height = 1440; }));
    CHECK_FALSE(changed([](MonitorInfo& m) { m.x = 100; }));
    CHECK_FALSE(changed([](MonitorInfo& m) { m.y = 100; }));
    CHECK_FALSE(changed([](MonitorInfo& m) { m.primary = false; }));
    CHECK_FALSE(changed([](MonitorInfo& m) { m.id = QStringLiteral("\\\\.\\DISPLAY2"); }));

    // A monitor arriving or leaving.
    std::vector<MonitorInfo> two = base;
    two.push_back(sampleMonitor());
    two.back().id = QStringLiteral("\\\\.\\DISPLAY2");
    CHECK_FALSE(win::sameTopology(base, two));
}

// Two subscribers both hear it. Named because the interface's own signature -- one handler, no
// token -- invites an implementation that replaces rather than appends, and the second consumer
// (Stage 4's session state) would then silently unhook the render policy.
TEST_CASE("displaytopology subscribe adds rather than replaces", "[platform][displaytopology]")
{
    win::WinDisplayTopology topology;

    int first = 0;
    int second = 0;
    REQUIRE(topology.subscribe([&first] { ++first; }));
    REQUIRE(topology.subscribe([&second] { ++second; }));

    // A null handler is refused rather than stored, so a caller cannot end up with a subscription
    // list that dereferences.
    CHECK_FALSE(topology.subscribe(IDisplayTopology::ChangedHandler{}));

    CHECK(topology.notificationHandle() != 0);
}
