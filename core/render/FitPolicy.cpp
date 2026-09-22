#include "core/render/FitPolicy.h"

#include <algorithm>
#include <cmath>

namespace rawpage::core::render {

PdfRect fitRect(const PageBoxes& boxes, FitMode fit) noexcept
{
    switch (fit) {
    case FitMode::Page:
        return boxes.media;
    case FitMode::Visible:
        // THE FALLBACK IS THE OLD BEHAVIOUR, not an error path (see the header). A page with no
        // objects, and an engine that declined, both land here -- and land on what `Ctrl+3` did
        // for the whole of Stages 2 and 3.
        if (boxes.content.has_value() && boxes.content->size.width > 0.0
            && boxes.content->size.height > 0.0) {
            return *boxes.content;
        }
        return boxes.crop;
    case FitMode::Width:
    case FitMode::Height:
    case FitMode::None:
        break;
    }
    return boxes.crop;
}

Zoom zoomToFit(FitMode fit, PdfSize box, DeviceSize viewport, double margin, PhysicalDpi physical,
               Zoom current) noexcept
{
    if (fit == FitMode::None) return current;
    if (box.width <= 0.0 || box.height <= 0.0) return current;
    if (viewport.width <= 0.0 || viewport.height <= 0.0) return current;

    // The margins the presentation will add. A fit that ignored them produces a page one gutter
    // too wide, and the horizontal scrollbar that appears then makes the viewport narrower still --
    // which is a fit that oscillates by one scrollbar width every resize.
    const double availableWidth = std::max(1.0, viewport.width - 2.0 * margin);
    const double availableHeight = std::max(1.0, viewport.height - 2.0 * margin);

    // At zoom 1.0 the box occupies `box * (physicalDpi / 72)` device pixels. The zoom that makes it
    // occupy `available` is the ratio of the two.
    const double baseScale = physical.dpi / 72.0;
    if (!(baseScale > 0.0)) return current;

    const double widthZoom = availableWidth / (box.width * baseScale);
    const double heightZoom = availableHeight / (box.height * baseScale);

    double factor = widthZoom;
    switch (fit) {
    case FitMode::Width:
        factor = widthZoom;
        break;
    case FitMode::Height:
        factor = heightZoom;
        break;
    case FitMode::Page:
    case FitMode::Visible:
        // The SMALLER of the two, so the whole box is inside the viewport. Taking the larger fits
        // one axis and crops the other, which is what a "fit" is defined not to do.
        factor = std::min(widthZoom, heightZoom);
        break;
    case FitMode::None:
        return current;
    }

    if (!std::isfinite(factor) || factor <= 0.0) return current;
    return Zoom{factor}.clamped();
}

} // namespace rawpage::core::render
