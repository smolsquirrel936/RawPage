// A `core::RasterImage` as a `QImage`, together with the bytes that image borrows. Stage 4 T5.
//
// ---------------------------------------------------------------------------------------------
// WHY THIS IS A FILE RATHER THAN SIX LINES IN EACH CALLER.
//
// `ui/viewer/TilePresenter` carried this conversion alone from Stage 2 T13, and its header said so
// in as many words: *the only place in the product where a `RasterImage` becomes a `QImage`*. Stage
// 4 T5 gives the engine boundary a second consumer -- `ui/panes/ThumbnailsPane` asks the same
// scheduler for a whole page at thumbnail scale -- and a second consumer is the moment that claim
// either moves into one function or becomes false. It moved here; `TilePresenter`'s header now
// points at this file rather than describing a rule it no longer solely enforces.
//
// The conversion is small and both of its mistakes are silent, which is exactly why it is not
// copied:
//
//   * **`bytesPerLine` is passed to `QImage`, never `width * 4`.** A rasterizer is entitled to
//     align its rows -- `core/backend.h` carries the stride for that reason, and the widget suite's
//     fake pads deliberately so the assumption is never untested -- and a `QImage` built over a
//     padded buffer without its stride draws the page sheared into diagonal garbage.
//   * **The bytes must outlive every paint.** `QImage`'s borrowing constructor does not copy, so
//     returning a bare `QImage` over a `QByteArray` that then goes out of scope is a use-after-free
//     that paints plausible-looking rubbish rather than crashing. That is why this returns a struct
//     holding BOTH and why nothing here hands out the image on its own: the type makes the pairing
//     the only thing a caller can store.
//
// The alternative -- a deep copy into a `QImage` that owns its pixels -- was not taken, and the
// reason is the 256 MiB cache the tiles already live in (open decision **O7**): a copy per delivery
// would double the resident bytes of every tile on screen to buy nothing this arrangement does not
// already give.

#ifndef RAWPAGE_UI_VIEWER_HELDRASTER_H
#define RAWPAGE_UI_VIEWER_HELDRASTER_H

#include "core/backend.h"

#include <QByteArray>
#include <QImage>

namespace rawpage::ui {

// A `QImage` and the buffer it borrows, held together. **Declaration order is load-bearing**:
// `image` is destroyed before `bytes`, which is the order that keeps a paint in flight from
// reading a freed buffer during the destructor.
struct HeldRaster {
    QByteArray bytes;
    QImage     image;

    bool isNull() const noexcept { return image.isNull(); }
};

// Wraps `raster` without copying its pixels. An invalid raster yields a null `HeldRaster` rather
// than an empty-but-valid image: *nothing arrived* and *a zero-sized page arrived* are different
// facts, and only the first is a delivery a caller should drop.
HeldRaster holdRaster(const core::RasterImage& raster);

// The `QImage` format for one of the two pixel formats `core/backend.h` defines. Exposed because a
// case that asserts an alpha channel survived the boundary should compare against this rather than
// against a second reading of the same rule.
QImage::Format imageFormatFor(core::PixelFormat format) noexcept;

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_HELDRASTER_H
