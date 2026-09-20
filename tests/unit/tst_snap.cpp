// Stage 4 T8 -- the snap resolver (`core/Snap.h`, decision D4-6) and the guide round trip.
// Guide cases A-45, A-46, A-47's model half and A-48 (`Stage4-ViewingSurface-Testing-Guide.md` §5.9).
//
// ---------------------------------------------------------------------------------------------
// WHY THE ARITHMETIC IS HERE AND NOT IN `tests/ui/`.
//
// `Stage4-ViewingSurface-Plan.md` §5.6 puts `tst_snap` in `rawpage_tests`, which links no display
// and no engine: the precedence and the device-pixel threshold are the part of this task that is
// easy to get wrong and impossible to see in a screenshot, so they are proved over synthetic
// geometry with no widget in the room. What a widget adds -- the ruler's readout, the drag, the
// repaint that must not become a render request -- is `tests/ui/tst_rulers.cpp`'s.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS SUITE CANNOT SEE, NAMED HERE RATHER THAN DISCOVERED (convention 19).
//
//   * **No product surface supplies an object edge.** Every case below that exercises the
//     `ObjectEdges` branch builds the span itself, and `GridGuideLayer::activeFlags()` never sets
//     the flag: the key that would is Stage 6's (D4-5), and at Stage 4 there is no annotation,
//     field or content object to be an edge. The branch is tested now because the PRECEDENCE is
//     what a later stage would otherwise have to discover under a drag.
//   * **A tolerance here is a number, not a hand.** These cases convert device pixels to points
//     with a `scale` they choose. That the scale a real view passes is `presentedScale()` is
//     asserted in the ui suite, and that the tolerance FEELS the same at 50 % and 400 % is manual
//     case M-07's.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/Snap.h"
#include "core/settings/SessionState.h"

#include <QDir>
#include <QTemporaryDir>

#include <vector>

using namespace rawpage;
using core::PdfPoint;
using core::SnapAxis;
using core::SnapContext;
using core::SnapEdge;
using core::SnapFlags;
using core::SnapGrid;
using core::SnapSource;
using Catch::Matchers::WithinAbs;

namespace {

// A grid on 72 pt majors with 4 subdivisions -- the declared defaults -- so the finest line is
// every 18 pt. The numbers below are chosen against THAT step, and a case that changed one would
// be changing what it measures.
SnapGrid defaultGrid()
{
    return SnapGrid{72.0, 4, 0.0, 0.0};
}

} // namespace

// =============================================================================================
// A-45 -- the precedence is object edges, then guides, then grid. Per axis.
// =============================================================================================

TEST_CASE("the snap precedence is object then guide then grid", "[snap]")
{
    // All three within the threshold of the same point, on both axes, and deliberately NOT in
    // distance order: the guide is nearest and the object edge is furthest, so a resolver that
    // took the nearest candidate regardless of kind would answer "guide" on every line below.
    const std::vector<SnapEdge> objects{{SnapAxis::X, 103.0}, {SnapAxis::Y, 203.0}};
    const std::vector<SnapEdge> guides{{SnapAxis::X, 100.5}, {SnapAxis::Y, 200.5}};

    SnapContext context;
    context.grid = defaultGrid();
    context.scale = 1.0;                  // 1 device pixel per point, so the threshold is 8 pt
    context.thresholdDevicePixels = 8.0;

    const PdfPoint at{101.0, 201.0};      // 90 and 198 are the nearest grid lines

    SECTION("with all three in reach, the object edge wins on both axes")
    {
        context.objectEdges = objects;
        context.guides = guides;

        const core::SnapResult result = core::snap(at, SnapFlags::All, context);
        CHECK(result.x == SnapSource::ObjectEdge);
        CHECK(result.y == SnapSource::ObjectEdge);
        CHECK_THAT(result.point.x, WithinAbs(103.0, 1e-9));
        CHECK_THAT(result.point.y, WithinAbs(203.0, 1e-9));
    }

    SECTION("with no object, the guide")
    {
        context.guides = guides;

        const core::SnapResult result = core::snap(at, SnapFlags::All, context);
        CHECK(result.x == SnapSource::Guide);
        CHECK(result.y == SnapSource::Guide);
        CHECK_THAT(result.point.x, WithinAbs(100.5, 1e-9));
    }

    SECTION("with neither, the grid")
    {
        const core::SnapResult result = core::snap(at, SnapFlags::All, context);
        CHECK(result.x == SnapSource::Grid);
        CHECK(result.y == SnapSource::Grid);
        CHECK_THAT(result.point.x, WithinAbs(108.0, 1e-9));   // 6 * 18
        CHECK_THAT(result.point.y, WithinAbs(198.0, 1e-9));   // 11 * 18
    }

    SECTION("the precedence is resolved PER AXIS, which is the half a whole-point rule loses")
    {
        // A guide on x only, an object edge on y only. A resolver that picked one kind for the
        // whole point would have to drop one of them, and the two answers below say which.
        const std::vector<SnapEdge> xGuide{{SnapAxis::X, 100.5}};
        const std::vector<SnapEdge> yObject{{SnapAxis::Y, 203.0}};
        context.guides = xGuide;
        context.objectEdges = yObject;

        const core::SnapResult result = core::snap(at, SnapFlags::All, context);
        CHECK(result.x == SnapSource::Guide);
        CHECK(result.y == SnapSource::ObjectEdge);
    }

    SECTION("a flag that is off excludes its kind even when a candidate is in reach")
    {
        context.objectEdges = objects;
        context.guides = guides;

        const core::SnapResult result =
            core::snap(at, SnapFlags::Guides | SnapFlags::Grid, context);
        CHECK(result.x == SnapSource::Guide);   // the object edge is nearer in kind, not in flags

        const core::SnapResult gridOnly = core::snap(at, SnapFlags::Grid, context);
        CHECK(gridOnly.x == SnapSource::Grid);

        // THE CONTROL: no flags, no snap, and the point comes back untouched.
        const core::SnapResult none = core::snap(at, SnapFlags::None, context);
        CHECK(none.x == SnapSource::None);
        CHECK(none.y == SnapSource::None);
        CHECK(none.point == at);
        CHECK_FALSE(none.snapped());
    }
}

// =============================================================================================
// A-46 -- ONE threshold, in device pixels, at every zoom. The stage's exit criterion as a number.
// =============================================================================================

TEST_CASE("the snap threshold is the same number of SCREEN pixels at 50 and at 400 per cent",
          "[snap]")
{
    // Two views of one page: 0.5 and 4.0 device pixels per point, an eightfold difference. The
    // candidate is in the same place in both -- it is a page fact -- so a threshold expressed in
    // POINTS would reach 8 pt at both and therefore 4 screen pixels at one and 32 at the other.
    const double halfScale = 0.5;
    const double fourTimes = 4.0;

    // THE CANDIDATE IS A LONE GUIDE, NOT A GRID LINE, and the reason is a measurement rather than
    // a preference: a grid line has NEIGHBOURS. At 0.5 device pixels per point the declared grid's
    // 18 pt step is 9 screen pixels, so no point on the page is more than 4.5 pixels from some
    // line and an 8-pixel threshold catches every one of them -- correct behaviour, and useless as
    // a control, because "one pixel further does not snap" cannot be said of a point that is
    // always within reach of the NEXT line. A single guide has no next line, so the claim below is
    // about the threshold and nothing else.
    const std::vector<SnapEdge> guide{{SnapAxis::X, 180.0}};

    const auto resolve = [&guide](double scale, double offsetPoints) {
        SnapContext context;
        context.guides = guide;
        context.scale = scale;
        context.thresholdDevicePixels = core::kSnapThresholdDevicePixels;
        return core::snap(PdfPoint{180.0 + offsetPoints, 500.0}, SnapFlags::Guides, context);
    };

    // 7 device pixels from the guide: inside 8 at both scales.
    CHECK(resolve(halfScale, 7.0 / halfScale).x == SnapSource::Guide);
    CHECK(resolve(fourTimes, 7.0 / fourTimes).x == SnapSource::Guide);

    // THE CONTROL, and it is the half the criterion does not ask for (convention 22): one device
    // pixel further out, nothing snaps -- at BOTH scales. A case that only proved the mechanism
    // fires would stay green if the threshold were a hundred pixels.
    CHECK(resolve(halfScale, 9.0 / halfScale).x == SnapSource::None);
    CHECK(resolve(fourTimes, 9.0 / fourTimes).x == SnapSource::None);

    // And the tolerance in POINTS really did differ by the zoom, which is what makes the two
    // results above a statement about screen pixels rather than a coincidence.
    CHECK_THAT(9.0 / halfScale, WithinAbs(18.0, 1e-9));
    CHECK_THAT(9.0 / fourTimes, WithinAbs(2.25, 1e-9));

    SECTION("a view with nothing presented in it snaps nothing rather than grabbing the page")
    {
        SnapContext context;
        context.grid = defaultGrid();
        context.scale = 0.0;
        const core::SnapResult result = core::snap(PdfPoint{181.0, 500.0}, SnapFlags::All, context);
        CHECK(result.x == SnapSource::None);
        CHECK(result.point.x == 181.0);
    }
}

// =============================================================================================
// The grid's own arithmetic -- the origin moves the LINES, and the subdivisions are what snaps.
// =============================================================================================

TEST_CASE("the grid snaps to its finest line, measured from its origin", "[snap]")
{
    SnapContext context;
    context.scale = 1.0;
    context.thresholdDevicePixels = 8.0;

    SECTION("subdivisions divide the major interval, and the finest line is what catches")
    {
        context.grid = SnapGrid{72.0, 4, 0.0, 0.0};
        CHECK_THAT(context.grid.step(), WithinAbs(18.0, 1e-9));

        // 20 pt is 2 from the 18 pt minor line and 20 from the 0 pt major: a resolver that snapped
        // to majors only would answer 0 and move the point twenty points.
        const core::SnapResult result = core::snap(PdfPoint{20.0, 20.0}, SnapFlags::Grid, context);
        CHECK_THAT(result.point.x, WithinAbs(18.0, 1e-9));
    }

    SECTION("the origin offset moves the lines, not the page")
    {
        context.grid = SnapGrid{72.0, 4, 5.0, 0.0};
        const core::SnapResult result = core::snap(PdfPoint{22.0, 5.0}, SnapFlags::Grid, context);
        CHECK_THAT(result.point.x, WithinAbs(23.0, 1e-9));   // 5 + 18
        CHECK_THAT(result.point.y, WithinAbs(0.0, 1e-9));    // y's origin is still 0
    }

    SECTION("a degenerate grid is no grid, and is not a division by zero")
    {
        context.grid = SnapGrid{0.0, 4, 0.0, 0.0};
        CHECK(context.grid.step() == 0.0);
        const core::SnapResult result = core::snap(PdfPoint{3.0, 3.0}, SnapFlags::Grid, context);
        CHECK(result.x == SnapSource::None);

        // A subdivision count below 1 is clamped rather than refused: the settings row's range
        // starts at 1, and a hand-edited INI is the case `SettingsKeys.def` rule 3 is about.
        context.grid = SnapGrid{72.0, 0, 0.0, 0.0};
        CHECK_THAT(context.grid.step(), WithinAbs(72.0, 1e-9));
    }

    SECTION("the nearest of several candidates of one kind wins")
    {
        const std::vector<SnapEdge> guides{
            {SnapAxis::X, 96.0}, {SnapAxis::X, 101.0}, {SnapAxis::X, 104.0}};
        context.guides = guides;
        const core::SnapResult result = core::snap(PdfPoint{100.0, 0.0}, SnapFlags::Guides, context);
        CHECK_THAT(result.point.x, WithinAbs(101.0, 1e-9));
    }

    SECTION("a candidate on the other axis is not a candidate")
    {
        const std::vector<SnapEdge> guides{{SnapAxis::Y, 101.0}};
        context.guides = guides;
        const core::SnapResult result = core::snap(PdfPoint{100.0, 0.0}, SnapFlags::Guides, context);
        CHECK(result.x == SnapSource::None);
    }
}

// =============================================================================================
// A-48 -- guides round-trip through `SessionState`, and never through the file.
//
// THE DOCUMENT'S BYTES ARE THE UI SUITE'S TO ASSERT, and this case says so rather than implying
// the whole claim (convention 19): what is proved here is that a guide SURVIVES a capture, a save,
// a load and a restore with every field intact. That nothing about a guide reaches `pdf/` is
// structural -- `core/settings/SessionState` names no PDF type and D4-1's `mutation-lint` count is
// the standing check -- and the byte comparison over a real save is `tests/ui/tst_rulers.cpp`'s.
// =============================================================================================

TEST_CASE("guides survive a capture, a save and a restore", "[snap][session]")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    const QString path = QDir(directory.path()).filePath(QStringLiteral("session.json"));

    core::Guide horizontal;
    horizontal.orientation = core::Guide::Orientation::Horizontal;
    horizontal.position = 216.5;
    horizontal.pageIndex = 3;
    horizontal.locked = true;

    core::Guide vertical;
    vertical.orientation = core::Guide::Orientation::Vertical;
    vertical.position = 72.25;
    vertical.pageIndex = 0;
    vertical.locked = false;

    {
        core::SessionState state(path);
        core::SessionDocument document;
        document.path = QStringLiteral("D:/corpus/ordinary/pages-12.pdf");
        document.guides = {horizontal, vertical};

        core::SessionWindow window;
        window.geometry = QRect(10, 10, 800, 600);
        window.documents = {document};

        core::Session session;
        session.fingerprint = QStringLiteral("one-monitor");
        session.windows = {window};
        state.capture(session);
        REQUIRE(state.save());
    }

    core::SessionState reloaded(path);
    const core::SessionState::LoadReport report = reloaded.load();
    REQUIRE(report.loaded);

    const core::SessionState::Restored restored =
        reloaded.restore(QStringLiteral("one-monitor"), {QRect(0, 0, 1920, 1080)},
                         [](const QString&) { return true; });
    REQUIRE(restored.windows.size() == 1);
    REQUIRE(restored.windows.front().documents.size() == 1);

    const std::vector<core::Guide>& guides = restored.windows.front().documents.front().guides;
    REQUIRE(guides.size() == 2);

    // EVERY FIELD, not just the position: the lock and the page index are what a guide MEANS, and
    // a round trip that dropped either would leave a guide that reads back as unlocked on page 0.
    CHECK(guides[0] == horizontal);
    CHECK(guides[1] == vertical);

    // And the positions are still PDF points in page space -- the fractional part is the tell that
    // nothing rounded them into pixels on the way (D4-7).
    CHECK_THAT(guides[0].position, WithinAbs(216.5, 1e-9));
    CHECK_THAT(guides[1].position, WithinAbs(72.25, 1e-9));
}
