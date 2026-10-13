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

#include <algorithm>
#include <cmath>
#include <span>
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

// =============================================================================================
// Stage 6 T12 (D6-14) -- point targets, the headless half of guide A-89
// =============================================================================================
// The corpus's drawing as the generator wrote it (`corpus/make-annots.py`'s `measure-scale.pdf`): a 216 x 144
// rectangle with both diagonals, and a 288-point line crossed at its midpoint. The ENGINE's reading of the same
// paths, and a drag through the product's views, are `tests/render/tst_measuregeometry` and
// `tests/composition/tst_measureproduct` -- this case says what the sums do with segments given to them.

namespace {

core::PageGeometry corpusDrawing()
{
    using core::PathSegment;
    const double s[][4] = {{100, 500, 316, 500}, {316, 500, 316, 644}, {316, 644, 100, 644}, {100, 644, 100, 500},
                           {100, 500, 316, 644}, {100, 644, 316, 500}, {100, 400, 388, 400}, {244, 350, 244, 450}};
    std::vector<PathSegment> segments;
    for (const auto& v : s) segments.push_back(PathSegment{PdfPoint{v[0], v[1]}, PdfPoint{v[2], v[3]}, false});
    return core::PageGeometry(0, segments);
}

core::SnapResult snapPoint(const core::PageGeometry& g, PdfPoint p, double scale, bool onPath,
                           std::span<const SnapEdge> edges = {}, SnapFlags flags = SnapFlags::All)
{
    const double tolerance = core::kSnapThresholdDevicePixels / scale;
    const std::vector<core::SnapPoint> points = core::snapPointsNear(g, p, tolerance, onPath);
    SnapContext context;
    context.objectPoints = points;
    context.objectEdges = edges;
    context.scale = scale;
    context.thresholdDevicePixels = core::kSnapThresholdDevicePixels;
    return core::snap(p, flags, context);
}

} // namespace

TEST_CASE("a point near an endpoint a midpoint or a crossing snaps exactly onto it", "[snap][A-89][D6-14]")
{
    const core::PageGeometry g = corpusDrawing();
    // THREE DEVICE PIXELS off each target, at 100 % (1.389 px a point) and 400 % (5.556): the same aim in the hand.
    for (const double scale : {100.0 / 72.0, 400.0 / 72.0}) {
        INFO("scale " << scale);
        const double off = 3.0 / scale / std::sqrt(2.0);
        struct Want {
            PdfPoint at;
            core::SnapPointKind kind;
        };
        for (const Want& want : {Want{{100, 500}, core::SnapPointKind::Endpoint},
                                 Want{{316, 644}, core::SnapPointKind::Endpoint},
                                 Want{{208, 500}, core::SnapPointKind::Midpoint},
                                 Want{{316, 572}, core::SnapPointKind::Midpoint},
                                 Want{{388, 400}, core::SnapPointKind::Endpoint}}) {
            const core::SnapResult r = snapPoint(g, PdfPoint{want.at.x + off, want.at.y - off}, scale, true);
            INFO("target " << want.at.x << "," << want.at.y);
            CHECK(r.x == SnapSource::ObjectPoint);
            CHECK(r.y == SnapSource::ObjectPoint);
            CHECK(r.point.x == want.at.x);
            CHECK(r.point.y == want.at.y);
            REQUIRE(r.pointKind);
            CHECK(*r.pointKind == want.kind);
        }
        // The diagonals cross at the rectangle's centre, which is also each diagonal's midpoint -- one point, the
        // midpoint's kind first on the tie.
        const core::SnapResult centre = snapPoint(g, PdfPoint{208 + off, 572 + off}, scale, true);
        CHECK(centre.point.x == 208.0);
        CHECK(centre.point.y == 572.0);
        // A crossing that is no segment's end or midpoint, built here: a diagonal through a horizontal at (100, 100).
        const core::PageGeometry crossed(0, {core::PathSegment{PdfPoint{0, 0}, PdfPoint{300, 300}, false},
                                             core::PathSegment{PdfPoint{0, 100}, PdfPoint{300, 100}, false}});
        const core::SnapResult cross = snapPoint(crossed, PdfPoint{100 + off, 100 + off}, scale, false);
        REQUIRE(cross.pointKind);
        CHECK(*cross.pointKind == core::SnapPointKind::Intersection);
        CHECK(std::abs(cross.point.x - 100.0) < 1e-9);
        CHECK(std::abs(cross.point.y - 100.0) < 1e-9);
    }
}

TEST_CASE("the point threshold is the same in device pixels at every zoom", "[snap][A-89][D6-14]")
{
    const core::PageGeometry g = corpusDrawing();
    for (const double scale : {50.0 / 72.0, 100.0 / 72.0, 400.0 / 72.0}) {
        INFO("scale " << scale);
        // AT the threshold it snaps; one device pixel beyond it, it does not -- in page units a different distance
        // at each zoom, in the hand the same.
        const double at = (core::kSnapThresholdDevicePixels - 0.01) / scale;   // a hundredth inside it
        const double beyond = (core::kSnapThresholdDevicePixels + 1.0) / scale;
        CHECK(snapPoint(g, PdfPoint{100 - at, 500}, scale, false).pointKind.has_value());
        CHECK_FALSE(snapPoint(g, PdfPoint{100 - beyond, 500}, scale, false).pointKind.has_value());
    }
}

TEST_CASE("a point on a path counts only where no corner midpoint or crossing is within reach", "[snap][A-89][D6-14]")
{
    const core::PageGeometry g = corpusDrawing();
    const double scale = 1.0;   // eight points of reach
    // On the lower edge, 40 points from any corner or midpoint: with paths on it lands on the edge; off, nothing.
    const core::SnapResult onPath = snapPoint(g, PdfPoint{140, 503}, scale, true);
    REQUIRE(onPath.pointKind);
    CHECK(*onPath.pointKind == core::SnapPointKind::OnPath);
    CHECK(onPath.point.y == 500.0);
    CHECK(std::abs(onPath.point.x - 140.0) < 1e-9);
    CHECK_FALSE(snapPoint(g, PdfPoint{140, 503}, scale, false).pointKind.has_value());
    // Near a corner, the corner -- although the edge is nearer.
    const core::SnapResult corner = snapPoint(g, PdfPoint{105, 501}, scale, true);
    REQUIRE(corner.pointKind);
    CHECK(*corner.pointKind == core::SnapPointKind::Endpoint);
    CHECK(corner.point.x == 100.0);
}

TEST_CASE("precedence is object point then object edge then guide then grid", "[snap][A-89][D6-14]")
{
    const core::PageGeometry g = corpusDrawing();
    const double scale = 1.0;
    const PdfPoint aim{102, 503};   // within reach of the corner (100, 500)
    // An annotation's side nearer on x: the POINT still wins, both coordinates at once.
    const std::vector<SnapEdge> edge{SnapEdge{SnapAxis::X, 101.5}};
    const core::SnapResult r = snapPoint(g, aim, scale, true, edge);
    CHECK(r.x == SnapSource::ObjectPoint);
    CHECK(r.point.x == 100.0);
    // With object POINTS off, the edge takes x and the y is left alone -- the per-axis precedence below them.
    const core::SnapResult edgeOnly = snapPoint(g, aim, scale, true, edge, SnapFlags::ObjectEdges | SnapFlags::Grid);
    CHECK(edgeOnly.x == SnapSource::ObjectEdge);
    CHECK(edgeOnly.point.x == 101.5);
    // And with `units/snapToObjects` off -- neither object kind -- nothing of the drawing.
    const core::SnapResult gridOnly = snapPoint(g, aim, scale, true, edge, SnapFlags::Grid);
    CHECK(gridOnly.x != SnapSource::ObjectPoint);
    CHECK(gridOnly.x != SnapSource::ObjectEdge);
}

TEST_CASE("a page of more paths than the bound gives no point targets", "[snap][A-91][D6-14]")
{
    std::vector<core::PathSegment> hatch;
    for (std::size_t i = 0; i <= core::PageGeometry::kMaxSegments; ++i) {
        const double y = static_cast<double>(i % 1000) * 0.5;
        hatch.push_back(core::PathSegment{PdfPoint{0, y}, PdfPoint{100, y}, false});
    }
    const core::PageGeometry dense(3, std::move(hatch));
    CHECK(dense.overLimit());
    CHECK(dense.counted() == core::PageGeometry::kMaxSegments + 1);
    CHECK(dense.segments().empty());
    CHECK(core::snapPointsNear(dense, PdfPoint{0, 0}, 8.0, true).empty());
    // One under the bound is held -- THE CONTROL.
    std::vector<core::PathSegment> fewer(core::PageGeometry::kMaxSegments,
                                         core::PathSegment{PdfPoint{0, 0}, PdfPoint{10, 10}, false});
    const core::PageGeometry held(3, std::move(fewer));
    CHECK_FALSE(held.overLimit());
    CHECK_FALSE(core::snapPointsNear(held, PdfPoint{0.5, 0.5}, 2.0, false).empty());
}

TEST_CASE("the grid index finds every segment near a point", "[snap][D6-14]")
{
    // A thousand short segments scattered on a page: the index's answer, against a scan of all of them.
    std::vector<core::PathSegment> segments;
    for (int i = 0; i < 1000; ++i) {
        const double x = (i * 37) % 600, y = (i * 53) % 800;
        segments.push_back(core::PathSegment{PdfPoint{x, y}, PdfPoint{x + 9.0, y + ((i % 3) - 1) * 7.0}, false});
    }
    const core::PageGeometry g(0, segments);
    for (const PdfPoint p : {PdfPoint{100, 100}, PdfPoint{333, 444}, PdfPoint{599, 799}}) {
        const std::vector<std::size_t> near = g.segmentsNear(p, 12.0, 100000);
        for (std::size_t i = 0; i < segments.size(); ++i) {
            const core::PathSegment& s = segments[i];
            const double dx = s.b.x - s.a.x, dy = s.b.y - s.a.y;
            const double t = std::clamp(((p.x - s.a.x) * dx + (p.y - s.a.y) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
            const double d = std::hypot(s.a.x + t * dx - p.x, s.a.y + t * dy - p.y);
            if (d <= 12.0) CHECK(std::find(near.begin(), near.end(), i) != near.end());
        }
    }
}

TEST_CASE("pieces of a path that meet end to end are no crossing", "[snap][I-19][D6-14]")
{
    // S6 guide I-19: a curve's flattened pieces share their ends, and each junction was offered as an INTERSECTION --
    // found by `tests/render/tst_measuregeometry`'s curve, whose middle snapped to a crossing that is not there.
    const core::PageGeometry curve(0, {core::PathSegment{PdfPoint{0, 0}, PdfPoint{10, 4}, true},
                                       core::PathSegment{PdfPoint{10, 4}, PdfPoint{20, 6}, true},
                                       core::PathSegment{PdfPoint{20, 6}, PdfPoint{30, 6}, true}});
    for (const core::SnapPoint& p : core::snapPointsNear(curve, PdfPoint{10, 4}, 3.0, true))
        CHECK(p.kind != core::SnapPointKind::Intersection);
    // A polyline's corner is an ENDPOINT, never a crossing either.
    const core::PageGeometry corner(0, {core::PathSegment{PdfPoint{0, 0}, PdfPoint{10, 0}, false},
                                        core::PathSegment{PdfPoint{10, 0}, PdfPoint{10, 10}, false}});
    const std::vector<core::SnapPoint> at = core::snapPointsNear(corner, PdfPoint{10, 0}, 1.0, false);
    REQUIRE_FALSE(at.empty());
    for (const core::SnapPoint& p : at) CHECK(p.kind != core::SnapPointKind::Intersection);
    // THE CONTROL: two segments that do cross, and one that ends on another's middle (a T), are crossings.
    const core::PageGeometry crossed(0, {core::PathSegment{PdfPoint{0, 0}, PdfPoint{10, 10}, false},
                                         core::PathSegment{PdfPoint{0, 10}, PdfPoint{10, 0}, false},
                                         core::PathSegment{PdfPoint{20, 0}, PdfPoint{40, 0}, false},
                                         core::PathSegment{PdfPoint{30, 0}, PdfPoint{30, 10}, false}});
    const auto kinds = [&](PdfPoint p) {
        std::vector<core::SnapPointKind> out;
        for (const core::SnapPoint& s : core::snapPointsNear(crossed, p, 0.5, false)) out.push_back(s.kind);
        return out;
    };
    const std::vector<core::SnapPointKind> x = kinds(PdfPoint{5, 5});
    CHECK(std::find(x.begin(), x.end(), core::SnapPointKind::Intersection) != x.end());
    const std::vector<core::SnapPointKind> t = kinds(PdfPoint{30, 0});
    CHECK(std::find(t.begin(), t.end(), core::SnapPointKind::Intersection) != t.end());
}
