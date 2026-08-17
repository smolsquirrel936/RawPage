// PDF space and device space, as types that cannot be confused for one another.
//
// THE BUG THIS PREVENTS (Implementation-Plan.md, Stage 0): a silent PDF-point/device-pixel
// mix-up is the most common class of bug in this domain, and it does not announce itself.
// It manifests as "slightly wrong at 150% scaling" -- correct on the developer's monitor,
// wrong on the user's, and invisible in every unit test written by the person who made it.
//
// Two coordinate systems, three differences, none of them optional:
//
//                     PDF space                      Device space
//   origin            bottom-left of the page        top-left of the viewport
//   y axis            UP                             DOWN
//   unit              points (1/72 inch), fixed      physical pixels, depends on DPI+zoom
//
// So the types below share no base class, define no converting constructor, and offer no
// operator that mixes them. The ONLY way across is PageTransform, which must be handed the
// zoom, the rotation and the DPI -- the three things a wrong conversion silently assumes.
// The friction is the feature: you cannot write the bug without noticing you wrote it.

#ifndef RECTO_CORE_GEOMETRY_H
#define RECTO_CORE_GEOMETRY_H

#include "core/Units.h"

#include <cmath>

namespace recto::core {

// --- PDF space --------------------------------------------------------------------------
struct PdfPoint {
    double x = 0.0;   // points from the left edge
    double y = 0.0;   // points from the BOTTOM edge

    constexpr bool operator==(const PdfPoint&) const = default;
};

struct PdfSize {
    double width = 0.0;
    double height = 0.0;

    constexpr bool operator==(const PdfSize&) const = default;
};

struct PdfRect {
    PdfPoint origin;   // bottom-left corner
    PdfSize size;

    constexpr double left() const { return origin.x; }
    constexpr double bottom() const { return origin.y; }
    constexpr double right() const { return origin.x + size.width; }
    constexpr double top() const { return origin.y + size.height; }

    constexpr bool contains(PdfPoint p) const {
        return p.x >= left() && p.x <= right() && p.y >= bottom() && p.y <= top();
    }

    constexpr bool operator==(const PdfRect&) const = default;
};

// --- Device space -----------------------------------------------------------------------
struct DevicePoint {
    double x = 0.0;   // pixels from the left edge
    double y = 0.0;   // pixels from the TOP edge

    constexpr bool operator==(const DevicePoint&) const = default;
};

struct DeviceSize {
    double width = 0.0;
    double height = 0.0;

    constexpr bool operator==(const DeviceSize&) const = default;
};

struct DeviceRect {
    DevicePoint origin;   // top-left corner
    DeviceSize size;

    constexpr double left() const { return origin.x; }
    constexpr double top() const { return origin.y; }
    constexpr double right() const { return origin.x + size.width; }
    constexpr double bottom() const { return origin.y + size.height; }

    constexpr bool contains(DevicePoint p) const {
        return p.x >= left() && p.x <= right() && p.y >= top() && p.y <= bottom();
    }

    constexpr bool operator==(const DeviceRect&) const = default;
};

// --- The only bridge ---------------------------------------------------------------------
enum class PageRotation { None = 0, Clockwise90 = 90, Half = 180, Clockwise270 = 270 };

// Carries everything a conversion needs, so none of it can be assumed.
//
// `dpi` is the PHYSICAL dpi of the monitor the tile will be drawn on, not a nominal 96:
// Tech-Stack-Plan.md's render design rasterizes at the active monitor's real resolution,
// and passing 96 here is exactly the "blurry text on a 150% monitor" defect.
class PageTransform {
public:
    constexpr PageTransform() = default;

    constexpr PageTransform(PdfSize pageSize, double zoom, double dpi,
                            PageRotation rotation = PageRotation::None)
        : pageSize_(pageSize), zoom_(zoom), dpi_(dpi), rotation_(rotation) {}

    // Points -> pixels. 72 points per inch, `dpi` pixels per inch, times zoom.
    constexpr double scale() const { return (dpi_ / 72.0) * zoom_; }

    constexpr DeviceSize deviceSize() const {
        const bool swapped = rotation_ == PageRotation::Clockwise90
                          || rotation_ == PageRotation::Clockwise270;
        const double w = swapped ? pageSize_.height : pageSize_.width;
        const double h = swapped ? pageSize_.width : pageSize_.height;
        return DeviceSize{w * scale(), h * scale()};
    }

    DevicePoint toDevice(PdfPoint p) const {
        // Flip y first, in PDF units, then rotate, then scale. Doing the flip after the
        // rotation gives a mirrored page at 90 and 270 -- a bug that looks like a rendering
        // problem and is arithmetic.
        const double xFlipped = p.x;
        const double yFlipped = pageSize_.height - p.y;

        double rx = xFlipped;
        double ry = yFlipped;
        switch (rotation_) {
        case PageRotation::None: break;
        case PageRotation::Clockwise90:
            rx = pageSize_.height - yFlipped;
            ry = xFlipped;
            break;
        case PageRotation::Half:
            rx = pageSize_.width - xFlipped;
            ry = pageSize_.height - yFlipped;
            break;
        case PageRotation::Clockwise270:
            rx = yFlipped;
            ry = pageSize_.width - xFlipped;
            break;
        }
        return DevicePoint{rx * scale(), ry * scale()};
    }

    PdfPoint toPdf(DevicePoint p) const {
        const double ux = p.x / scale();
        const double uy = p.y / scale();

        double xFlipped = ux;
        double yFlipped = uy;
        switch (rotation_) {
        case PageRotation::None: break;
        case PageRotation::Clockwise90:
            xFlipped = uy;
            yFlipped = pageSize_.height - ux;
            break;
        case PageRotation::Half:
            xFlipped = pageSize_.width - ux;
            yFlipped = pageSize_.height - uy;
            break;
        case PageRotation::Clockwise270:
            xFlipped = pageSize_.width - uy;
            yFlipped = ux;
            break;
        }
        return PdfPoint{xFlipped, pageSize_.height - yFlipped};
    }

    DeviceRect toDevice(PdfRect r) const {
        // Convert two opposite corners and normalize: under rotation the PDF bottom-left
        // does not stay the device top-left, so taking origin+size directly would produce
        // negative extents at 90 and 270.
        const DevicePoint a = toDevice(PdfPoint{r.left(), r.bottom()});
        const DevicePoint b = toDevice(PdfPoint{r.right(), r.top()});
        const double left = std::min(a.x, b.x);
        const double top = std::min(a.y, b.y);
        return DeviceRect{DevicePoint{left, top},
                          DeviceSize{std::abs(b.x - a.x), std::abs(b.y - a.y)}};
    }

    constexpr PdfSize pageSize() const { return pageSize_; }
    constexpr double zoom() const { return zoom_; }
    constexpr double dpi() const { return dpi_; }
    constexpr PageRotation rotation() const { return rotation_; }

private:
    PdfSize pageSize_;
    double zoom_ = 1.0;
    double dpi_ = 72.0;
    PageRotation rotation_ = PageRotation::None;
};

// Convenience for page geometry expressed in user-facing units (paper sizes, margins).
constexpr PdfSize pdfSizeFrom(Length width, Length height) {
    return PdfSize{width.points(), height.points()};
}

} // namespace recto::core

#endif // RECTO_CORE_GEOMETRY_H
