#include "ui/viewer/HeldRaster.h"

namespace rawpage::ui {

QImage::Format imageFormatFor(core::PixelFormat format) noexcept
{
    // Both are 32 bits per pixel in BGRA byte order on a little-endian machine, which is what
    // `core/backend.h` says and what makes this a wrap rather than a conversion. The alpha byte is
    // the only difference, and getting it backwards produces a page that is either transparent or
    // opaque where it should be the other -- visible only over a non-white background, which is
    // precisely where `pageDisplay/transparencyGrid` puts it.
    return format == core::PixelFormat::Bgra8888 ? QImage::Format_ARGB32 : QImage::Format_RGB32;
}

HeldRaster holdRaster(const core::RasterImage& raster)
{
    HeldRaster held;
    if (!raster.isValid()) return held;

    held.bytes = raster.pixels;

    // `bytesPerLine`, NOT `width * 4`. See the header: the failure is a sheared page rather than an
    // error, so it is the kind of mistake that ships.
    held.image = QImage(reinterpret_cast<const uchar*>(held.bytes.constData()), raster.width,
                        raster.height, raster.bytesPerLine, imageFormatFor(raster.format));
    return held;
}

} // namespace rawpage::ui
