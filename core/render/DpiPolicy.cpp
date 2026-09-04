#include "core/render/DpiPolicy.h"

#include <algorithm>
#include <cmath>

namespace rawpage::core::render {
namespace {

// The fallbacks. Every one of them is a value that draws SOMETHING readable rather than a value
// that is arithmetically neutral: a zero DPI divides, and a resolution of 1 draws a page one pixel
// wide, and neither is diagnosable from a screenshot.
constexpr double kFallbackDpi = 96.0;
constexpr double kFallbackRatio = 1.0;

bool isUsable(double value) noexcept
{
    return std::isfinite(value) && value > 0.0;
}

} // namespace

bool Zoom::isValid() const noexcept
{
    return std::isfinite(factor) && factor >= kMin && factor <= kMax;
}

Zoom Zoom::clamped() const noexcept
{
    if (!std::isfinite(factor)) return Zoom{1.0};
    return Zoom{std::clamp(factor, kMin, kMax)};
}

bool Resolution::isValid() const noexcept
{
    return isUsable(dpi) && isUsable(devicePixelRatio) && isUsable(upscale) && upscale >= 1.0;
}

Resolution DpiPolicy::resolve(Zoom zoom, MonitorScale scale, PhysicalDpi physical) const noexcept
{
    // Sanitize first, and separately, so that one bad input does not discard two good ones. A
    // monitor whose reported DPI is nonsense still has a usable scale factor, and a settings file
    // with a corrupt zoom is still being shown on a real monitor.
    const double zoomFactor = zoom.clamped().factor;
    const double ratio = isUsable(scale.factor) ? scale.factor : kFallbackRatio;
    const double glassDpi = isUsable(physical.dpi) ? physical.dpi : kFallbackDpi;

    // ACTUAL SIZE IS PHYSICAL. The OS scale factor is deliberately absent from this line; it is
    // carried out separately as the presentation ratio. See the header.
    const double requested = glassDpi * zoomFactor;

    // The cap. `maxRenderDpi` itself comes from a settings row at T15 and from a user's hand-edited
    // INI after that, so it is sanitized on the same terms as everything else rather than trusted
    // because it is ours.
    const double cap = isUsable(limits_.maxRenderDpi) ? limits_.maxRenderDpi : requested;

    Resolution out;
    out.devicePixelRatio = ratio;
    if (requested > cap) {
        out.dpi = cap;
        // The compensating factor, so the page still occupies the space the zoom asked for. It is
        // computed from the two numbers rather than remembered, because `dpi * upscale` must equal
        // `requested` exactly -- a rounded `upscale` is a page that is a fraction of a percent the
        // wrong size at every zoom above the cap, which accumulates into a visible seam between a
        // capped page and an uncapped neighbour in a continuous layout.
        out.upscale = requested / cap;
    } else {
        out.dpi = requested;
        out.upscale = 1.0;
    }

    // Belt and braces: `requested` is a product of two sanitized positives and cannot be zero, but
    // this function is called on every paint and its post-condition is the thing three other files
    // divide by.
    if (!isUsable(out.dpi)) {
        out.dpi = kFallbackDpi;
        out.upscale = 1.0;
    }
    return out;
}

bool DpiPolicy::requiresRerender(const Resolution& current, const Resolution& target) const noexcept
{
    // Nothing has been rasterized yet, or what was rasterized is not describable. Either way there
    // is nothing to resample FROM, so the answer is yes.
    if (!current.isValid()) return true;
    if (!target.isValid()) return true;

    const double lo = std::min(current.dpi, target.dpi);
    const double hi = std::max(current.dpi, target.dpi);

    // Symmetric by construction: the ratio of the larger to the smaller is the same number whether
    // the move was up or down. A formulation such as `(target - current) / current` is not, and the
    // asymmetry it introduces is invisible in a one-directional test.
    const double tolerance = std::isfinite(limits_.resampleTolerance)
                                     && limits_.resampleTolerance >= 0.0
                                 ? limits_.resampleTolerance
                                 : 0.0;
    return (hi / lo) > (1.0 + tolerance);
}

} // namespace rawpage::core::render
