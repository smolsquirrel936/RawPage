// Stage 2 T12 -- platform/win/WinPowerSession. Guide case A-62.
//
// ---------------------------------------------------------------------------------------------
// "NOT APPLICABLE" IS AN OUTCOME THIS SUITE REPORTS, NOT ONE IT HIDES.
//
// A-62 says so in as many words: on a desktop the case must report *not applicable* **explicitly**
// rather than pass vacuously. A machine with no battery cannot exercise a battery transition, and a
// green check on "we are on mains" would be a case that asserts the tautology *this desktop is
// plugged in* while claiming to have tested a throttling policy.
//
// So `hasBattery()` is a separate question from `isOnBattery()`, and the battery cases SKIP by name
// on a machine that has none.
//
// ---------------------------------------------------------------------------------------------
// WHAT IS NOT HERE, AND WHERE IT IS.
//
// A-62's second half -- *"with `performance/throttleOnBattery` set and the machine on battery, the
// effective worker count drops"* -- names a settings key that does not exist until **T15**, and a
// `RenderScheduler` that the composition root does not build until **T13**. The interface this file
// covers is what that wire is made of; the wire itself is T15's, and claiming it here would be
// asserting a policy nothing implements.
//
// The suspend and resume events likewise cannot be produced without suspending this machine. The
// message path is driven for real through the notification window -- the same arrangement
// `tst_displaytopology` uses and for the same reason (convention 19): everything from
// `DispatchMessage` inward is the shipped code, and only the message's origin is the suite's.

#include <catch2/catch_test_macros.hpp>

#include "platform/platform.h"
#include "platform/win/WinPowerSession.h"

#include <windows.h>

#include <vector>

using namespace rawpage::platform;

namespace {

// The AC/DC power-source setting, as `WinPowerSession.cpp` spells it. Repeated here rather than
// exported, because a test that read the constant out of the code under test would agree with a
// typo in it -- the GUID is published, and this is the second, independent statement of it.
constexpr GUID kAcDcPowerSource = {
    0x5d3e9a59, 0xe9d5, 0x4b00, {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}};

// A `POWERBROADCAST_SETTING` carrying one DWORD, laid out the way Windows lays it out.
struct AcDcBroadcast {
    POWERBROADCAST_SETTING header;
    // `POWERBROADCAST_SETTING::Data` is declared as a one-element array, so a payload of exactly
    // one DWORD needs three more bytes behind it. Writing the struct this way rather than casting a
    // raw buffer is what keeps the alignment the OS's rather than ours.
    unsigned char tail[3];
};

AcDcBroadcast makeAcDc(DWORD source)
{
    AcDcBroadcast broadcast{};
    broadcast.header.PowerSetting = kAcDcPowerSource;
    broadcast.header.DataLength = sizeof(DWORD);
    std::memcpy(broadcast.header.Data, &source, sizeof(source));
    return broadcast;
}

} // namespace

TEST_CASE("powersession reports the machine's power source", "[platform][power]")
{
    win::WinPowerSession power;

    const bool battery = power.hasBattery();
    WARN((battery ? "this machine has a battery" : "this machine has NO battery (a desktop)"));

    if (!battery) {
        // NOT APPLICABLE, SAID OUT LOUD. `isOnBattery()` must still answer, and it must answer
        // false -- "no battery" is not "running on battery", and an implementation that read
        // `ACLineStatus` alone would report a UPS-backed desktop as unplugged during a blip.
        CHECK_FALSE(power.isOnBattery());
        SKIP("no battery on this machine -- the battery transition is not exercisable here");
    }

    // On a laptop both answers are legitimate; what is asserted is that the query works and that
    // the two questions are answered independently.
    WARN((power.isOnBattery() ? "currently on battery" : "currently on mains"));
    SUCCEED();
}

TEST_CASE("powersession delivers the events its messages carry", "[platform][power]")
{
    win::WinPowerSession power;

    std::vector<IPowerSession::Event> seen;
    REQUIRE(power.subscribe([&seen](IPowerSession::Event event) { seen.push_back(event); }));

    const auto hwnd = reinterpret_cast<HWND>(power.notificationHandle());
    REQUIRE(hwnd != nullptr);

    // Suspend and resume, through the real window procedure.
    ::SendMessageW(hwnd, WM_POWERBROADCAST, PBT_APMSUSPEND, 0);
    ::SendMessageW(hwnd, WM_POWERBROADCAST, PBT_APMRESUMEAUTOMATIC, 0);

    // The power source. 1 is DC; 0 is AC; **2 is a short-term source such as a UPS and is reported
    // as MAINS** -- throttling the renderer for the length of a power blip is a visible stutter
    // bought for nothing, and that decision is only visible in a case that names it.
    AcDcBroadcast dc = makeAcDc(1);
    AcDcBroadcast ac = makeAcDc(0);
    AcDcBroadcast ups = makeAcDc(2);
    ::SendMessageW(hwnd, WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE,
                   reinterpret_cast<LPARAM>(&dc));
    ::SendMessageW(hwnd, WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE,
                   reinterpret_cast<LPARAM>(&ac));
    ::SendMessageW(hwnd, WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE,
                   reinterpret_cast<LPARAM>(&ups));

    // Session lock and unlock.
    ::SendMessageW(hwnd, WM_WTSSESSION_CHANGE, WTS_SESSION_LOCK, 0);
    ::SendMessageW(hwnd, WM_WTSSESSION_CHANGE, WTS_SESSION_UNLOCK, 0);

    REQUIRE(seen.size() == 7);
    CHECK(seen[0] == IPowerSession::Event::Suspending);
    CHECK(seen[1] == IPowerSession::Event::Resumed);
    CHECK(seen[2] == IPowerSession::Event::OnBattery);
    CHECK(seen[3] == IPowerSession::Event::OnMains);
    CHECK(seen[4] == IPowerSession::Event::OnMains);   // the UPS
    CHECK(seen[5] == IPowerSession::Event::SessionLocked);
    CHECK(seen[6] == IPowerSession::Event::SessionUnlocked);

    CHECK(power.deliveredEventCount() == 7);
}

TEST_CASE("powersession ignores a power broadcast for a setting it did not register for",
          "[platform][power]")
{
    win::WinPowerSession power;

    int delivered = 0;
    REQUIRE(power.subscribe([&delivered](IPowerSession::Event) { ++delivered; }));
    const auto hwnd = reinterpret_cast<HWND>(power.notificationHandle());
    REQUIRE(hwnd != nullptr);

    // THE NEGATIVE CONTROL for the case above. A registration is per-GUID, but a window may receive
    // settings it did not ask for -- another component in the process may register the same HWND,
    // and a handler that read `Data[0]` without checking `PowerSetting` would translate a monitor
    // power state or a lid switch into "we are on battery" and throttle the renderer for it.
    AcDcBroadcast other = makeAcDc(1);
    other.header.PowerSetting = GUID{0x02731015, 0x4510, 0x4526,
                                     {0x99, 0xe6, 0xe5, 0xa1, 0x7e, 0xbd, 0x1a, 0xea}};
    ::SendMessageW(hwnd, WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE,
                   reinterpret_cast<LPARAM>(&other));
    CHECK(delivered == 0);

    // A truncated payload is refused rather than read past.
    AcDcBroadcast truncated = makeAcDc(1);
    truncated.header.DataLength = 1;
    ::SendMessageW(hwnd, WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE,
                   reinterpret_cast<LPARAM>(&truncated));
    CHECK(delivered == 0);

    // A null payload -- which is what a malformed broadcast looks like from here.
    ::SendMessageW(hwnd, WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE, 0);
    CHECK(delivered == 0);

    // And the control on the control: the same message shape, correctly addressed, does arrive.
    AcDcBroadcast good = makeAcDc(1);
    ::SendMessageW(hwnd, WM_POWERBROADCAST, PBT_POWERSETTINGCHANGE,
                   reinterpret_cast<LPARAM>(&good));
    CHECK(delivered == 1);

    // An unrelated session change is not an event either.
    ::SendMessageW(hwnd, WM_WTSSESSION_CHANGE, WTS_SESSION_LOGON, 0);
    CHECK(delivered == 1);
}

TEST_CASE("powersession presentation mode is idempotent and released on destruction",
          "[platform][power]")
{
    {
        win::WinPowerSession power;
        CHECK_FALSE(power.isPresentationModeActive());

        REQUIRE(power.setPresentationModeActive(true));
        CHECK(power.isPresentationModeActive());

        // Idempotent. `SetThreadExecutionState` is not a counted resource, so a second hold that
        // took a second slot would be released by the first drop -- the bug being a machine that
        // sleeps in the middle of a presentation.
        REQUIRE(power.setPresentationModeActive(true));
        CHECK(power.isPresentationModeActive());

        REQUIRE(power.setPresentationModeActive(false));
        CHECK_FALSE(power.isPresentationModeActive());

        // Dropping one that is not held is not an error.
        REQUIRE(power.setPresentationModeActive(false));

        REQUIRE(power.setPresentationModeActive(true));
    }

    // The destructor above released it. Nothing in this process can observe the thread's execution
    // state directly, so what is asserted is that the sequence completes without a failure -- the
    // real check on the release is the one in the destructor, and it is unconditional.
    SUCCEED();
}
