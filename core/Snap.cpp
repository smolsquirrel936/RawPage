#include "core/Snap.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace rawpage::core {
namespace {

// The nearest candidate of one kind on one axis, if any is within `tolerance`. Returns nothing
// rather than the nearest-regardless, because "nearest" with no bound is how a snap resolver comes
// to drag a point across the page.
std::optional<double> nearestEdge(std::span<const SnapEdge> edges, SnapAxis axis, double value,
                                  double tolerance)
{
    std::optional<double> best;
    double bestDistance = tolerance;

    for (const SnapEdge& edge : edges) {
        if (edge.axis != axis) continue;
        const double distance = std::abs(edge.position - value);
        // `<=` rather than `<`: a candidate exactly at the threshold is inside it, which is what
        // the case that puts a point AT the tolerance and one pixel beyond it asserts either side
        // of (guide case A-46).
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = edge.position;
        }
    }
    return best;
}

std::optional<double> nearestGridLine(const SnapGrid& grid, double origin, double value,
                                      double tolerance)
{
    const double step = grid.step();
    if (step <= 0.0) return std::nullopt;

    const double line = origin + std::round((value - origin) / step) * step;
    if (std::abs(line - value) > tolerance) return std::nullopt;
    return line;
}

// One axis, the whole precedence. The kind decides before the distance does -- see the header.
std::pair<double, SnapSource> resolveAxis(double value, SnapAxis axis, SnapFlags flags,
                                          const SnapContext& context, double tolerance)
{
    if (hasFlag(flags, SnapFlags::ObjectEdges)) {
        if (const auto hit = nearestEdge(context.objectEdges, axis, value, tolerance))
            return {*hit, SnapSource::ObjectEdge};
    }
    if (hasFlag(flags, SnapFlags::Guides)) {
        if (const auto hit = nearestEdge(context.guides, axis, value, tolerance))
            return {*hit, SnapSource::Guide};
    }
    if (hasFlag(flags, SnapFlags::Grid)) {
        const double origin = axis == SnapAxis::X ? context.grid.originX : context.grid.originY;
        if (const auto hit = nearestGridLine(context.grid, origin, value, tolerance))
            return {*hit, SnapSource::Grid};
    }
    return {value, SnapSource::None};
}

} // namespace

double SnapGrid::step() const noexcept
{
    if (!(spacing > 0.0)) return 0.0;              // written so a NaN spacing is also "no grid"
    return spacing / static_cast<double>(std::max(1, subdivisions));
}

const char* snapSourceName(SnapSource source) noexcept
{
    switch (source) {
    case SnapSource::None:        return "none";
    case SnapSource::ObjectEdge:  return "object";
    case SnapSource::Guide:       return "guide";
    case SnapSource::Grid:        return "grid";
    case SnapSource::ObjectPoint: return "point";
    }
    return "none";
}

const char* snapPointKindName(SnapPointKind kind) noexcept
{
    switch (kind) {
    case SnapPointKind::Endpoint:     return "endpoint";
    case SnapPointKind::Midpoint:     return "midpoint";
    case SnapPointKind::Intersection: return "intersection";
    case SnapPointKind::OnPath:       return "path";
    }
    return "endpoint";
}

// ---- PageGeometry (S6 T12) ---------------------------------------------------------------------

namespace {

// The cells across the longer side of the page's paths. 128 keeps a query on a dense page to a few hundred
// candidates and the index to a few cells per segment.
constexpr int kCellsAcross = 128;

bool finitePoint(PdfPoint p) noexcept
{
    return std::isfinite(p.x) && std::isfinite(p.y);
}

double distance(PdfPoint a, PdfPoint b) noexcept
{
    return std::hypot(a.x - b.x, a.y - b.y);
}

PdfPoint nearestOnSegment(PdfPoint p, const PathSegment& s) noexcept
{
    const double dx = s.b.x - s.a.x, dy = s.b.y - s.a.y;
    const double length2 = dx * dx + dy * dy;
    if (!(length2 > 0.0)) return s.a;
    const double t = std::clamp(((p.x - s.a.x) * dx + (p.y - s.a.y) * dy) / length2, 0.0, 1.0);
    return PdfPoint{s.a.x + t * dx, s.a.y + t * dy};
}

// Where the two segments cross or touch, if they do at one point (parallel overlapping segments have no ONE
// point, and are left to their endpoints).
std::optional<PdfPoint> crossing(const PathSegment& s, const PathSegment& t) noexcept
{
    const double rx = s.b.x - s.a.x, ry = s.b.y - s.a.y;
    const double qx = t.b.x - t.a.x, qy = t.b.y - t.a.y;
    const double denominator = rx * qy - ry * qx;
    const double scale = std::hypot(rx, ry) * std::hypot(qx, qy);
    if (!(scale > 0.0) || std::abs(denominator) <= 1e-12 * scale) return std::nullopt;
    const double ux = t.a.x - s.a.x, uy = t.a.y - s.a.y;
    const double u = (ux * qy - uy * qx) / denominator;
    const double v = (ux * ry - uy * rx) / denominator;
    constexpr double slack = 1e-9;
    if (u < -slack || u > 1.0 + slack || v < -slack || v > 1.0 + slack) return std::nullopt;
    return PdfPoint{s.a.x + u * rx, s.a.y + u * ry};
}

} // namespace

PageGeometry::PageGeometry(int page, std::vector<PathSegment> segments, std::vector<PdfPoint> anchors)
    : page_(page), counted_(segments.size())
{
    // A non-finite point is no target: dropped rather than allowed to make the grid infinitely wide.
    segments.erase(std::remove_if(segments.begin(), segments.end(),
                                  [](const PathSegment& s) { return !finitePoint(s.a) || !finitePoint(s.b); }),
                   segments.end());
    anchors.erase(std::remove_if(anchors.begin(), anchors.end(), [](PdfPoint p) { return !finitePoint(p); }),
                  anchors.end());
    if (segments.size() > kMaxSegments) {
        *this = tooManySegments(page, segments.size());
        return;
    }
    segments_ = std::move(segments);
    anchors_ = std::move(anchors);
    if (segments_.empty()) return;

    double left = segments_.front().a.x, right = left, bottom = segments_.front().a.y, top = bottom;
    for (const PathSegment& s : segments_) {
        for (const PdfPoint& p : {s.a, s.b}) {
            left = std::min(left, p.x);
            right = std::max(right, p.x);
            bottom = std::min(bottom, p.y);
            top = std::max(top, p.y);
        }
    }
    cell_ = std::max({(right - left) / kCellsAcross, (top - bottom) / kCellsAcross, 1.0});
    originX_ = left;
    originY_ = bottom;
    columns_ = static_cast<int>(std::floor((right - left) / cell_)) + 1;
    rows_ = static_cast<int>(std::floor((top - bottom) / cell_)) + 1;
    cells_.assign(static_cast<std::size_t>(columns_) * static_cast<std::size_t>(rows_), {});

    // EACH SEGMENT IN THE CELLS IT PASSES THROUGH, sampled at half a cell -- a corner it clips between two samples
    // is in a neighbouring cell, and a query reaches one cell past its radius for exactly that reason.
    for (std::size_t index = 0; index < segments_.size(); ++index) {
        const PathSegment& s = segments_[index];
        const double length = distance(s.a, s.b);
        const int steps = std::max(1, static_cast<int>(std::ceil(length / (cell_ / 2.0))));
        int lastCell = -1;
        for (int k = 0; k <= steps; ++k) {
            const double t = static_cast<double>(k) / steps;
            const int column = std::clamp(static_cast<int>((s.a.x + t * (s.b.x - s.a.x) - originX_) / cell_), 0, columns_ - 1);
            const int row = std::clamp(static_cast<int>((s.a.y + t * (s.b.y - s.a.y) - originY_) / cell_), 0, rows_ - 1);
            const int cell = row * columns_ + column;
            if (cell == lastCell) continue;
            std::vector<std::uint32_t>& listed = cells_[static_cast<std::size_t>(cell)];
            if (listed.empty() || listed.back() != index) listed.push_back(static_cast<std::uint32_t>(index));
            lastCell = cell;
        }
    }
}

PageGeometry PageGeometry::tooManySegments(int page, std::size_t counted)
{
    PageGeometry geometry;
    geometry.page_ = page;
    geometry.overLimit_ = true;
    geometry.counted_ = counted;
    return geometry;
}

std::vector<std::size_t> PageGeometry::segmentsNear(PdfPoint point, double radius, std::size_t cap) const
{
    std::vector<std::size_t> out;
    if (cells_.empty() || !finitePoint(point) || !(radius >= 0.0)) return out;
    const auto clampColumn = [this](double x) {
        return static_cast<int>(std::clamp(std::floor((x - originX_) / cell_), -1.0, static_cast<double>(columns_)));
    };
    const auto clampRow = [this](double y) {
        return static_cast<int>(std::clamp(std::floor((y - originY_) / cell_), -1.0, static_cast<double>(rows_)));
    };
    const int c0 = std::max(clampColumn(point.x - radius) - 1, 0), c1 = std::min(clampColumn(point.x + radius) + 1, columns_ - 1);
    const int r0 = std::max(clampRow(point.y - radius) - 1, 0), r1 = std::min(clampRow(point.y + radius) + 1, rows_ - 1);
    if (c0 > c1 || r0 > r1) return out;

    // NEAREST CELLS FIRST, so a cap drops the far ones.
    std::vector<std::pair<double, int>> cells;
    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c) {
            const double cx = originX_ + (c + 0.5) * cell_, cy = originY_ + (r + 0.5) * cell_;
            cells.emplace_back(std::hypot(cx - point.x, cy - point.y), r * columns_ + c);
        }
    }
    std::sort(cells.begin(), cells.end());
    std::vector<bool> seen;
    for (const auto& [d, cell] : cells) {
        (void)d;
        for (const std::uint32_t index : cells_[static_cast<std::size_t>(cell)]) {
            if (seen.size() <= index) seen.resize(static_cast<std::size_t>(index) + 1, false);
            if (seen[index]) continue;
            seen[index] = true;
            out.push_back(index);
            if (out.size() >= cap) return out;
        }
    }
    return out;
}

std::size_t PageGeometry::approxBytes() const noexcept
{
    std::size_t bytes = sizeof(PageGeometry) + segments_.capacity() * sizeof(PathSegment)
                        + anchors_.capacity() * sizeof(PdfPoint) + cells_.capacity() * sizeof(std::vector<std::uint32_t>);
    for (const std::vector<std::uint32_t>& cell : cells_) bytes += cell.capacity() * sizeof(std::uint32_t);
    return bytes;
}

std::vector<SnapPoint> snapPointsNear(const PageGeometry& geometry, PdfPoint point, double tolerance, bool onPath)
{
    std::vector<SnapPoint> out;
    if (geometry.overLimit() || !finitePoint(point) || !(tolerance > 0.0)) return out;
    const auto offer = [&](PdfPoint at, SnapPointKind kind) {
        if (distance(at, point) <= tolerance) out.push_back(SnapPoint{at, kind});
    };
    const std::vector<std::size_t> near = geometry.segmentsNear(point, tolerance);
    const std::vector<PathSegment>& segments = geometry.segments();
    for (const std::size_t i : near) {
        const PathSegment& s = segments[i];
        if (s.curve) continue;
        offer(s.a, SnapPointKind::Endpoint);
        offer(s.b, SnapPointKind::Endpoint);
        offer(PdfPoint{(s.a.x + s.b.x) / 2.0, (s.a.y + s.b.y) / 2.0}, SnapPointKind::Midpoint);
    }
    for (const PdfPoint& anchor : geometry.anchors()) offer(anchor, SnapPointKind::Endpoint);
    // Where two NEARBY segments cross -- both near, so the pair count is the cap's square at most, and the cap is
    // what a dense page's hatching reaches.
    const std::size_t pairs = std::min<std::size_t>(near.size(), 64);
    // TWO SEGMENTS THAT ONLY MEET END TO END DO NOT CROSS: a path's consecutive pieces share their ends, and a curve
    // flattened into pieces would otherwise be a crossing every eighth of its length (a corner is an endpoint, and
    // offered as one above).
    const auto isEnd = [](PdfPoint p, const PathSegment& s) { return distance(p, s.a) <= 1e-9 || distance(p, s.b) <= 1e-9; };
    for (std::size_t i = 0; i < pairs; ++i) {
        for (std::size_t j = i + 1; j < pairs; ++j) {
            const PathSegment& s = segments[near[i]];
            const PathSegment& t = segments[near[j]];
            if (const std::optional<PdfPoint> at = crossing(s, t); at && !(isEnd(*at, s) && isEnd(*at, t)))
                offer(*at, SnapPointKind::Intersection);
        }
    }
    if (onPath) {
        for (const std::size_t i : near) offer(nearestOnSegment(point, segments[i]), SnapPointKind::OnPath);
    }
    std::sort(out.begin(), out.end(), [point](const SnapPoint& a, const SnapPoint& b) {
        const double da = distance(a.at, point), db = distance(b.at, point);
        return da != db ? da < db : static_cast<int>(a.kind) < static_cast<int>(b.kind);
    });
    return out;
}

SnapResult snap(PdfPoint point, SnapFlags flags, const SnapContext& context)
{
    SnapResult result{point, SnapSource::None, SnapSource::None, std::nullopt};
    if (flags == SnapFlags::None) return result;

    // THE ONE CONVERSION, and the reason the threshold is a device-pixel number everywhere else in
    // the product. A non-positive scale is a view with nothing presented in it; snapping to a
    // tolerance of infinity is the alternative, and it grabs the whole page.
    if (!(context.scale > 0.0)) return result;
    const double tolerance = context.thresholdDevicePixels / context.scale;
    if (!(tolerance > 0.0)) return result;

    // S6 T12: A POINT TARGET FIRST, and it fixes both coordinates. The nearest within the threshold; a point ON a
    // path only where no corner, midpoint or crossing is within it (see the header).
    if (hasFlag(flags, SnapFlags::ObjectPoints)) {
        const SnapPoint* best = nullptr;
        double bestDistance = 0.0;
        for (const bool path : {false, true}) {
            for (const SnapPoint& candidate : context.objectPoints) {
                if ((candidate.kind == SnapPointKind::OnPath) != path) continue;
                const double d = distance(candidate.at, point);
                if (d > tolerance) continue;
                if (best == nullptr || d < bestDistance
                    || (d == bestDistance && static_cast<int>(candidate.kind) < static_cast<int>(best->kind))) {
                    best = &candidate;
                    bestDistance = d;
                }
            }
            if (best != nullptr) break;
        }
        if (best != nullptr) {
            result.point = best->at;
            result.x = SnapSource::ObjectPoint;
            result.y = SnapSource::ObjectPoint;
            result.pointKind = best->kind;
            return result;
        }
    }

    const auto [x, xSource] = resolveAxis(point.x, SnapAxis::X, flags, context, tolerance);
    const auto [y, ySource] = resolveAxis(point.y, SnapAxis::Y, flags, context, tolerance);

    result.point = PdfPoint{x, y};
    result.x = xSource;
    result.y = ySource;
    return result;
}

} // namespace rawpage::core
