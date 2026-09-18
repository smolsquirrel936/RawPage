// The monitor-arrangement fingerprint and the restore clamp -- D4-8's arithmetic, on synthetic
// values, headless. Stage 4 T3; guide cases A-19 and A-21
// (`Stage4-ViewingSurface-Testing-Guide.md` §5.5).
//
// WHAT THESE CASES DO NOT COVER, said here so the next reader subtracts it rather than adds it
// (convention 19): every `MonitorInfo` below is BUILT BY THIS FILE. Nothing here proves that
// `WinDisplayTopology` reports what Windows believes, that a real 150 % panel arrives with
// `scaleFactor` 1.5, or that unplugging a monitor produces a different arrangement at all -- those
// are the manual walk's (M-04, M-05), on the P-1 rig, with `Win`+`P`. What is proved here is that
// GIVEN two arrangements, the hash distinguishes exactly the fields D4-8 says it should, and that
// GIVEN a desktop, a restored window lands somewhere a person can reach it.

#include "core/settings/SessionState.h"

#include <catch2/catch_test_macros.hpp>

#include <QRect>

#include <vector>

using rawpage::core::SessionState;
using rawpage::platform::MonitorInfo;

namespace {

// The P-1 rig, as `platform.h` documents it: physical pixels, a 4K panel at 150 % beside a
// 2560x1440 at 100 %.
MonitorInfo panel4K()
{
    MonitorInfo monitor;
    monitor.id = QStringLiteral("\\\\.\\DISPLAY1");
    monitor.friendlyName = QStringLiteral("MAG 272URDF");
    monitor.x = 0;
    monitor.y = 0;
    monitor.width = 3840;
    monitor.height = 2160;
    monitor.scaleFactor = 1.5;
    monitor.physicalDpi = 163.0;
    monitor.primary = true;
    return monitor;
}

MonitorInfo panel1440()
{
    MonitorInfo monitor;
    monitor.id = QStringLiteral("\\\\.\\DISPLAY2");
    monitor.friendlyName = QStringLiteral("DELL U2515H");
    monitor.x = 3840;
    monitor.y = 0;
    monitor.width = 2560;
    monitor.height = 1440;
    monitor.scaleFactor = 1.0;
    monitor.physicalDpi = 108.0;
    monitor.primary = false;
    return monitor;
}

} // namespace

TEST_CASE("the fingerprint is blind to the order monitors were enumerated in", "[session][fingerprint]")
{
    const std::vector<MonitorInfo> forwards{panel4K(), panel1440()};
    const std::vector<MonitorInfo> backwards{panel1440(), panel4K()};

    // The same desk, enumerated the other way round. Windows does not promise an order, and an
    // arrangement that hashed differently after a replug would make every fingerprint single-use.
    REQUIRE(SessionState::fingerprintOf(forwards) == SessionState::fingerprintOf(backwards));
    REQUIRE_FALSE(SessionState::fingerprintOf(forwards).isEmpty());
}

TEST_CASE("the fingerprint changes when the arrangement does", "[session][fingerprint]")
{
    const std::vector<MonitorInfo> rig{panel4K(), panel1440()};
    const QString baseline = SessionState::fingerprintOf(rig);

    SECTION("a monitor moved")
    {
        std::vector<MonitorInfo> moved = rig;
        moved[1].x = 3841;
        REQUIRE(SessionState::fingerprintOf(moved) != baseline);
    }
    SECTION("a resolution changed")
    {
        std::vector<MonitorInfo> resized = rig;
        resized[0].width = 2560;
        REQUIRE(SessionState::fingerprintOf(resized) != baseline);
    }
    SECTION("a scale factor changed -- 150 % to 125 %")
    {
        std::vector<MonitorInfo> rescaled = rig;
        rescaled[0].scaleFactor = 1.25;
        REQUIRE(SessionState::fingerprintOf(rescaled) != baseline);
    }
    SECTION("the primary monitor moved to the other panel")
    {
        std::vector<MonitorInfo> repointed = rig;
        repointed[0].primary = false;
        repointed[1].primary = true;
        REQUIRE(SessionState::fingerprintOf(repointed) != baseline);
    }
    SECTION("a monitor went away -- the case P-2's Win+P produces")
    {
        const std::vector<MonitorInfo> alone{panel4K()};
        REQUIRE(SessionState::fingerprintOf(alone) != baseline);
    }
    SECTION("a monitor is a different device at the same place")
    {
        std::vector<MonitorInfo> swapped = rig;
        swapped[1].id = QStringLiteral("\\\\.\\DISPLAY3");
        REQUIRE(SessionState::fingerprintOf(swapped) != baseline);
    }
}

TEST_CASE("the fingerprint ignores what platform.h says is not identity", "[session][fingerprint]")
{
    const std::vector<MonitorInfo> rig{panel4K(), panel1440()};
    const QString baseline = SessionState::fingerprintOf(rig);

    // `friendlyName` MAY BE EMPTY and is not unique across two identical panels -- `platform.h`
    // says both, and I-20 is what happened when `ui/` treated it as a key. A fingerprint that
    // depended on it would change the day a display driver started reporting a model name.
    std::vector<MonitorInfo> renamed = rig;
    renamed[0].friendlyName = QString{};
    renamed[1].friendlyName = QStringLiteral("something else entirely");
    REQUIRE(SessionState::fingerprintOf(renamed) == baseline);

    // `physicalDpi` is a property of the glass rather than of the arrangement: it cannot change
    // without the panel itself being a different panel, which `id` already says.
    std::vector<MonitorInfo> redpi = rig;
    redpi[0].physicalDpi = 96.0;
    REQUIRE(SessionState::fingerprintOf(redpi) == baseline);
}

TEST_CASE("an empty topology hashes to something stable rather than crashing", "[session][fingerprint]")
{
    // A session captured before any screen was reported is not a crash: it is a fingerprint nothing
    // will match, which falls back and then clamps.
    REQUIRE_FALSE(SessionState::fingerprintOf({}).isEmpty());
    REQUIRE(SessionState::fingerprintOf({}) == SessionState::fingerprintOf({}));
    REQUIRE(SessionState::fingerprintOf({}) != SessionState::fingerprintOf({panel4K()}));
}

// ---------------------------------------------------------------------------------------------
// The clamp (A-21). The S1 this stage most plausibly ships is a window nobody can reach.
// ---------------------------------------------------------------------------------------------

TEST_CASE("a window already on screen is not moved by a single pixel", "[session][clamp]")
{
    // THE CONTROL. A clamp that repositions everything would pass every other case in this file
    // and would quietly move a window the user had placed.
    //
    // **WHAT THIS CASE DOES NOT TEST, measured rather than assumed** (conventions 19 and 22):
    // replacing `clampInto`'s early `return window;` with a `break` leaves this case GREEN. For
    // a window wholly inside a screen the fallback arithmetic is a no-op, so the two paths agree
    // and this is an assertion about BEHAVIOUR that two mechanisms happen to satisfy -- not a
    // test of the early return. The branch's own subject is the straddling window in the next
    // case, where planting that same `break` does turn the suite red.
    const std::vector<QRect> screens{QRect(0, 0, 1920, 1080)};
    const QRect placed(100, 80, 1200, 800);
    REQUIRE(SessionState::clampInto(placed, screens) == placed);

    // Flush against each edge, still untouched.
    REQUIRE(SessionState::clampInto(QRect(0, 0, 1920, 1080), screens) == QRect(0, 0, 1920, 1080));
    REQUIRE(SessionState::clampInto(QRect(720, 0, 1200, 800), screens) == QRect(720, 0, 1200, 800));
}

TEST_CASE("a window straddling two adjacent screens is left alone", "[session][clamp]")
{
    // Reachable is the question, not contained-in-one-rectangle. A window pulled across the seam of
    // a two-monitor desktop is perfectly usable, and moving it would be the defect.
    //
    // **THIS is the case that holds the early return up**: no single screen contains this window,
    // so without that branch the arithmetic below it would drag the window wholly onto one monitor.
    // Verified by planting exactly that bug (see the previous case).
    const std::vector<QRect> screens{QRect(0, 0, 1920, 1080), QRect(1920, 0, 1920, 1080)};
    const QRect straddling(1600, 200, 900, 600);
    REQUIRE(SessionState::clampInto(straddling, screens) == straddling);
}

TEST_CASE("a window on a monitor that is gone comes back onto one that exists", "[session][clamp]")
{
    // The undocked case: geometry captured on a 5760x2160 desktop, restored onto 1920x1080.
    const std::vector<QRect> screens{QRect(0, 0, 1920, 1080)};
    const QRect offscreen(3200, 1400, 1200, 800);

    const QRect clamped = SessionState::clampInto(offscreen, screens);
    REQUIRE(clamped != offscreen);
    REQUIRE(screens.front().contains(clamped));
    // The size is kept where it fits -- a window that came back a different size would be a second
    // surprise on top of the first.
    REQUIRE(clamped.size() == offscreen.size());
}

TEST_CASE("a window larger than the desktop is shrunk onto it", "[session][clamp]")
{
    const std::vector<QRect> screens{QRect(0, 0, 1280, 720)};
    const QRect huge(-400, -200, 2600, 1500);

    const QRect clamped = SessionState::clampInto(huge, screens);
    REQUIRE(screens.front().contains(clamped));
    REQUIRE(clamped.width() <= 1280);
    REQUIRE(clamped.height() <= 720);
}

TEST_CASE("a window whose title bar is off the top is brought down", "[session][clamp]")
{
    // The specific unreachable shape: the body is visible, the title bar is above the desktop, and
    // there is nothing to grab. `kTitleBarHeight` is what makes this a case rather than an opinion.
    const std::vector<QRect> screens{QRect(0, 0, 1920, 1080)};
    const QRect titleAbove(300, -200, 900, 700);

    const QRect clamped = SessionState::clampInto(titleAbove, screens);
    REQUIRE(clamped.y() >= 0);
    REQUIRE(screens.front().contains(clamped));
}

TEST_CASE("a window with only a sliver on screen is pulled back", "[session][clamp]")
{
    // Ten pixels of title bar is not something a person can grab, which is why the rule is
    // `kMinimumVisibleWidth` rather than "intersects at all".
    const std::vector<QRect> screens{QRect(0, 0, 1920, 1080)};
    const QRect sliver(1910, 500, 900, 600);

    const QRect clamped = SessionState::clampInto(sliver, screens);
    REQUIRE(clamped != sliver);
    REQUIRE(screens.front().contains(clamped));
}

TEST_CASE("with no screens at all nothing is moved", "[session][clamp]")
{
    // There is nothing to clamp onto, and inventing a rectangle would be worse than leaving the
    // geometry for the next launch that can see a screen.
    const QRect placed(100, 100, 800, 600);
    REQUIRE(SessionState::clampInto(placed, {}) == placed);
}
