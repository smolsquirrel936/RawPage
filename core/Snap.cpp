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
    case SnapSource::None:       return "none";
    case SnapSource::ObjectEdge: return "object";
    case SnapSource::Guide:      return "guide";
    case SnapSource::Grid:       return "grid";
    }
    return "none";
}

SnapResult snap(PdfPoint point, SnapFlags flags, const SnapContext& context)
{
    SnapResult result{point, SnapSource::None, SnapSource::None};
    if (flags == SnapFlags::None) return result;

    // THE ONE CONVERSION, and the reason the threshold is a device-pixel number everywhere else in
    // the product. A non-positive scale is a view with nothing presented in it; snapping to a
    // tolerance of infinity is the alternative, and it grabs the whole page.
    if (!(context.scale > 0.0)) return result;
    const double tolerance = context.thresholdDevicePixels / context.scale;
    if (!(tolerance > 0.0)) return result;

    const auto [x, xSource] = resolveAxis(point.x, SnapAxis::X, flags, context, tolerance);
    const auto [y, ySource] = resolveAxis(point.y, SnapAxis::Y, flags, context, tolerance);

    result.point = PdfPoint{x, y};
    result.x = xSource;
    result.y = ySource;
    return result;
}

} // namespace rawpage::core
