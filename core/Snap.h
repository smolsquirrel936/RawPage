// The snap resolver -- Stage 4 T8, decision D4-6 (`Stage4-ViewingSurface-Plan.md` §3).
//
// ===========================================================================================
// ONE THRESHOLD, AND IT IS IN **DEVICE PIXELS**.
// ===========================================================================================
// A threshold in page units is a threshold that grabs from a centimetre away at 50 % and needs a
// steady hand at 400 %. The whole exit criterion for this part of the stage is that snapping
// *"behaves identically at 50 % and 400 %"*, and it does so because the tolerance is what the HAND
// does -- and the hand works in screen pixels. So the caller passes the threshold in device pixels
// and the `scale` that converts them (device pixels per PDF point, i.e.
// `PageLayout::presentedScale()`), and the arithmetic below converts ONCE, here, rather than at
// each of the three candidate kinds.
//
// ===========================================================================================
// THE PRECEDENCE IS FIXED: OBJECT EDGES, THEN GUIDES, THEN GRID -- PER AXIS.
// ===========================================================================================
// Per axis, not per point: a drag that is beside a guide horizontally and beside a grid line
// vertically should take the guide for x and the grid for y, which is what every editor does and
// what a whole-point precedence would get wrong in exactly the case a person notices.
//
// The kind decides first and the distance decides second. Among candidates of ONE kind the nearest
// wins; a guide 1 pt away does not beat an object edge 3 pt away, because the kinds are a
// statement about intent -- an object edge is a thing in the document, a guide is a thing the
// person placed, and a grid line is a thing the settings produced.
//
// ===========================================================================================
// THE OBJECT EDGES ARE A PARAMETER, AND AT STAGE 4 THE PRODUCT PASSES NONE.
// ===========================================================================================
// D4-6: `objectEdges` is a span the caller fills, and **every product caller passes an EMPTY span
// until Stage 6**, which is when there is an annotation, a field or a content object to snap to
// (`units/snapToObjects` moves to Stage 6 with them -- D4-5). Taking them as a parameter rather
// than reading a store is what lets the precedence -- the part that is easy to get wrong and
// impossible to see in a screenshot -- be tested exhaustively now, over synthetic geometry,
// instead of in Stage 6 under a drag. Every case that exercises the object branch says at its
// assertion that no product surface reaches it (convention 19).
//
// ===========================================================================================
// EVERYTHING HERE IS PAGE SPACE, AND THAT IS WHY IT IS IN `core/`.
// ===========================================================================================
// Points in, points out, with a scale for the tolerance and nothing else: no widget, no painter,
// no viewport. `rawpage_tests` links no display, so `tests/unit/tst_snap.cpp` exercises the whole
// of this file headless -- which is the reason the arithmetic is not inside
// `ui/viewer/GridGuideLayer`, the class D4-6 names. That class holds the guides, the appearance
// and the drag; the sums are here.

// ===========================================================================================
// STAGE 6 T12 (D6-14): POINT TARGETS, AND THEY COME FIRST
// ===========================================================================================
// A measurement snaps to the drawing it measures: a path's ENDPOINTS, the MIDPOINT of a straight segment, where
// two segments CROSS, and -- with `measuring/snapToPaths` -- the nearest point ON a path. A point target fixes
// both coordinates at once, so it is resolved before the per-axis kinds, and the precedence becomes **object
// point, then object edge, then guide, then grid** -- the threshold the same one, in device pixels. Among point
// targets the NEAREST wins, and on a tie the kind in that order; a point on a path counts only where no endpoint,
// midpoint or intersection is within reach, because a person aiming at a corner should not get the edge beside it.
//
// The paths are a PAGE'S, read once per page on the engine thread (`IDocumentBackend::pageGeometry`) into a
// `PageGeometry` and kept beside the page's text in `core/text/DocumentIndex`. A page of more than
// `PageGeometry::kMaxSegments` segments gives NO object snaps at all and says so once -- a hatched site plan is a
// page whose every query would otherwise sort a hundred thousand candidates under the pointer. The grid index
// below makes a query cost the segments near the pointer, not the page's.
//
// **Object edges since T12 are the page's ANNOTATIONS'** -- each rectangle's four sides -- passed in by the view;
// `units/snapToObjects` turns both kinds of object snap on (D4-5's key, Stage 6's).

#ifndef RAWPAGE_CORE_SNAP_H
#define RAWPAGE_CORE_SNAP_H

#include "core/Geometry.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rawpage::core {

// Which coordinate a candidate constrains. A VERTICAL line on the page constrains x; a horizontal
// one constrains y. Named by the axis rather than by the line's direction, because the line's
// direction is what a reader gets backwards.
enum class SnapAxis { X, Y };

// One candidate: a page-space coordinate on one axis. An object's left edge, its centre, a guide,
// a grid line -- all the same shape, which is what makes the precedence a loop rather than three.
struct SnapEdge {
    SnapAxis axis = SnapAxis::X;
    double   position = 0.0;   // PDF points, page space

    constexpr bool operator==(const SnapEdge&) const = default;
};

// What a coordinate was snapped to, per axis. `None` means nothing was within the threshold and
// the input coordinate was returned unchanged -- which a caller must be able to tell from a snap
// that happened to land on the input.
// `ObjectPoint` (S6 T12) is a point target: both coordinates at once, and `SnapResult::pointKind` says which.
enum class SnapSource { None, ObjectEdge, Guide, Grid, ObjectPoint };

const char* snapSourceName(SnapSource source) noexcept;

// Which kinds may participate. A flag OFF is not the same as an empty list: `units/snapToGrid` off
// with a grid configured must not snap, and that is this enum rather than a cleared grid.
enum class SnapFlags : unsigned {
    None         = 0u,
    Grid         = 1u << 0,
    Guides       = 1u << 1,
    ObjectEdges  = 1u << 2,
    ObjectPoints = 1u << 3,   // S6 T12
    All          = Grid | Guides | ObjectEdges | ObjectPoints,
};

constexpr SnapFlags operator|(SnapFlags a, SnapFlags b) noexcept
{
    return static_cast<SnapFlags>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr bool hasFlag(SnapFlags set, SnapFlags flag) noexcept
{
    return (static_cast<unsigned>(set) & static_cast<unsigned>(flag)) != 0u;
}

// The grid, as the four `units/grid*` keys describe it. The origin is the page-space point the
// grid is measured FROM -- `units/gridOriginX` and `units/gridOriginY` -- so a non-zero offset
// moves the lines and not the page.
struct SnapGrid {
    double spacing      = 36.0;   // PDF points between MAJOR lines
    int    subdivisions = 1;      // minor lines per major interval; 1 means no minor lines
    double originX      = 0.0;
    double originY      = 0.0;

    // THE FINEST LINE IS WHAT SNAPS, and it is what is drawn: a grid showing minor lines that a
    // drag passes straight through would be a grid lying about where it is. Zero when the grid is
    // degenerate (a non-positive spacing), which every caller must treat as "no grid" rather than
    // dividing by.
    double step() const noexcept;
};

// ---- Point targets (S6 T12) ------------------------------------------------------------------------

// What a point target is, in its order of precedence on a tie.
enum class SnapPointKind : std::uint8_t { Endpoint, Midpoint, Intersection, OnPath };
const char* snapPointKindName(SnapPointKind kind) noexcept;

struct SnapPoint {
    PdfPoint      at;
    SnapPointKind kind = SnapPointKind::Endpoint;
};

// One straight piece of a path, in page space. `curve` marks a piece of a flattened Bézier curve: its ends are
// not corners and it has no midpoint, but it can be crossed and it is a path to snap ON.
struct PathSegment {
    PdfPoint a;
    PdfPoint b;
    bool     curve = false;
};

// ONE PAGE'S PATHS, as the engine read them, with a grid over the page so a query costs the segments near the
// pointer. Immutable once built, so any thread may read one. `anchors` are the curves' own ends -- endpoints a
// flattened curve's pieces do not mark.
class PageGeometry {
public:
    // Past this many segments on one page there are NO object snaps on it (see the header).
    static constexpr std::size_t kMaxSegments = 100000;

    PageGeometry() = default;
    PageGeometry(int page, std::vector<PathSegment> segments, std::vector<PdfPoint> anchors = {});
    // A page whose paths came to more than `kMaxSegments`: nothing is held, and `counted` says how many there were
    // when the reading stopped.
    static PageGeometry tooManySegments(int page, std::size_t counted);

    int page() const noexcept { return page_; }
    bool overLimit() const noexcept { return overLimit_; }
    std::size_t counted() const noexcept { return counted_; }
    const std::vector<PathSegment>& segments() const noexcept { return segments_; }
    const std::vector<PdfPoint>& anchors() const noexcept { return anchors_; }

    // The segments that may pass within `radius` of `point` -- every one that does, and some that do not -- at
    // most `cap` of them, nearest cells first.
    std::vector<std::size_t> segmentsNear(PdfPoint point, double radius, std::size_t cap = 256) const;
    std::size_t approxBytes() const noexcept;

private:
    int page_ = -1;
    bool overLimit_ = false;
    std::size_t counted_ = 0;
    std::vector<PathSegment> segments_;
    std::vector<PdfPoint> anchors_;

    // The index: `columns_` x `rows_` cells of `cell_` points from (`originX_`, `originY_`), each listing the
    // segments that pass through it.
    double originX_ = 0.0;
    double originY_ = 0.0;
    double cell_ = 1.0;
    int columns_ = 0;
    int rows_ = 0;
    std::vector<std::vector<std::uint32_t>> cells_;
};

// The point targets within `tolerance` (page units) of `point`: the endpoints and midpoints of the straight
// segments near it, the curves' ends, where two of those segments cross -- and, with `onPath`, the nearest point
// on each. Sorted nearest first.
std::vector<SnapPoint> snapPointsNear(const PageGeometry& geometry, PdfPoint point, double tolerance, bool onPath);

struct SnapResult {
    PdfPoint   point;                     // the snapped point; the input where a coordinate did not snap
    SnapSource x = SnapSource::None;
    SnapSource y = SnapSource::None;
    std::optional<SnapPointKind> pointKind;   // S6 T12: set when both are `ObjectPoint`

    bool snapped() const noexcept { return x != SnapSource::None || y != SnapSource::None; }
};

// Everything the resolution depends on, so no part of it can be assumed -- the same argument
// `PageTransform` makes about a conversion that silently supplies its own dpi.
struct SnapContext {
    std::span<const SnapPoint> objectPoints{};   // S6 T12: a page's path targets near the point
    std::span<const SnapEdge> objectEdges{};     // S6 T12: the page's annotations' sides (empty before Stage 6, D4-6)
    std::span<const SnapEdge> guides{};
    SnapGrid                  grid{};

    // Device pixels per PDF point -- `PageLayout::presentedScale()`. A non-positive scale means
    // there is nothing on screen to measure against and nothing snaps, rather than a division
    // producing an infinite tolerance that grabs the whole page.
    double scale = 1.0;

    double thresholdDevicePixels = 8.0;
};

// The threshold, in device pixels, that every product surface uses. Eight is a little under a
// tenth of an inch on a 96-dpi panel and about that on any other, because it is in DEVICE pixels
// on a product that rasterizes at the monitor's real resolution -- a person's aim, not a number of
// document units.
inline constexpr double kSnapThresholdDevicePixels = 8.0;

SnapResult snap(PdfPoint point, SnapFlags flags, const SnapContext& context);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_SNAP_H
