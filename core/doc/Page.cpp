#include "core/doc/Page.h"

#include <algorithm>
#include <utility>

namespace rawpage::core {

PdfRect intersect(PdfRect a, PdfRect b) noexcept
{
    const double left = std::max(a.left(), b.left());
    const double bottom = std::max(a.bottom(), b.bottom());
    const double right = std::min(a.right(), b.right());
    const double top = std::min(a.top(), b.top());

    // Non-overlapping rectangles give an EMPTY rect at the intersection's corner rather than a
    // negative-sized one. A negative size propagates: it multiplies through a scale into a
    // negative pixel count, which allocates nothing and reads as a blank page.
    if (right <= left || top <= bottom)
        return PdfRect{PdfPoint{left, bottom}, PdfSize{0.0, 0.0}};

    return PdfRect{PdfPoint{left, bottom}, PdfSize{right - left, top - bottom}};
}

Page::Page(int index, PdfRect mediaBox) : index_(index), mediaBox_(mediaBox) {}

PdfRect Page::cropBox() const noexcept
{
    // Defaults to the MediaBox, and is INTERSECTED with it. The specification requires the
    // intersection and real files violate it -- a CropBox larger than its MediaBox is common
    // enough that trusting it lays out pages of the wrong size against files that open correctly
    // everywhere else.
    return cropBox_ ? intersect(*cropBox_, mediaBox_) : mediaBox_;
}

PdfRect Page::bleedBox() const noexcept
{
    return bleedBox_ ? intersect(*bleedBox_, mediaBox_) : cropBox();
}

PdfRect Page::trimBox() const noexcept
{
    return trimBox_ ? intersect(*trimBox_, mediaBox_) : cropBox();
}

PdfRect Page::artBox() const noexcept
{
    return artBox_ ? intersect(*artBox_, mediaBox_) : cropBox();
}

void Page::setRotation(int degrees) noexcept
{
    // `/Rotate` is inheritable, a multiple of 90, and MAY BE NEGATIVE -- `-90` is a legal file and
    // means 270. C++'s `%` keeps the sign of the dividend, so the second modulo is what turns -90
    // into 270 rather than into -90.
    int normalized = ((degrees / 90) * 90) % 360;
    normalized = ((normalized % 360) + 360) % 360;

    switch (normalized) {
    case 90:  rotation_ = PageRotation::Clockwise90; break;
    case 180: rotation_ = PageRotation::Half; break;
    case 270: rotation_ = PageRotation::Clockwise270; break;
    default:  rotation_ = PageRotation::None; break;
    }
}

PdfSize Page::displaySize() const noexcept
{
    const PdfSize size = cropBox().size;
    const bool quarterTurn =
        rotation_ == PageRotation::Clockwise90 || rotation_ == PageRotation::Clockwise270;
    return quarterTurn ? PdfSize{size.height, size.width} : size;
}

bool operator==(const Page& a, const Page& b)
{
    // Compares the RESOLVED boxes rather than the optionals, because two pages that display
    // identically are the same page as far as anything above this layer is concerned -- and an
    // explicit CropBox equal to the MediaBox is exactly that case. `hasExplicitCropBox()` is where
    // the spelling is asked about, and it is Stage 3's writer that cares.
    return a.index_ == b.index_ && a.mediaBox_ == b.mediaBox_ && a.cropBox() == b.cropBox()
           && a.bleedBox() == b.bleedBox() && a.trimBox() == b.trimBox()
           && a.artBox() == b.artBox() && a.rotation_ == b.rotation_ && a.label_ == b.label_;
}

} // namespace rawpage::core
