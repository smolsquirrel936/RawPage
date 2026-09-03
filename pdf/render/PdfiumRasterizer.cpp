#include "pdf/render/PdfiumRasterizer.h"

#include "pdf/render/PdfiumDocument.h"
#include "pdf/render/PdfiumInit.h"

#include <fpdfview.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace rawpage::pdf::render {
namespace {

core::Error rasterError(core::ErrorCode code, const char* key, const QString& context)
{
    return core::Error{code, QLatin1String(key), context};
}

} // namespace

PdfiumRasterizer::PdfiumRasterizer(PdfiumDocument& document) : document_(document) {}

core::Result<core::RasterImage> PdfiumRasterizer::raster(const core::render::RenderRequest& request)
{
    if (!request.isValid()) {
        // Refused rather than rendered. Every field `isValid()` checks has a silent failure mode:
        // a zero scale divides, an empty tile allocates nothing and returns a valid-LOOKING empty
        // image, and an invalid `Revision` is D2-12's stale-tile bug arriving as a cache key.
        return rasterError(core::ErrorCode::InvalidArgument, "error.render.badRequest",
                           QString::number(request.pageIndex));
    }

    const core::Page* page = document_.pages().at(request.pageIndex);
    if (page == nullptr) {
        return rasterError(core::ErrorCode::NotFound, "error.render.noSuchPage",
                           QString::number(request.pageIndex));
    }

    // The page's size in device pixels, rounded UP. Rounding down loses the last row and column of
    // a page whose scaled size is fractional, which shows as a one-pixel white line along two
    // edges of every page at most zoom levels.
    const core::PdfSize display = page->displaySize();
    const int pageWidth =
        std::max(1, static_cast<int>(std::ceil(display.width * request.deviceScale)));
    const int pageHeight =
        std::max(1, static_cast<int>(std::ceil(display.height * request.deviceScale)));

    // A tile entirely outside the page is a caller error rather than an empty image, because an
    // empty image is indistinguishable from a blank page and would be cached as one.
    if (request.tile.x >= pageWidth || request.tile.y >= pageHeight) {
        return rasterError(core::ErrorCode::InvalidArgument, "error.render.badRequest",
                           QString::number(request.pageIndex));
    }

    core::RasterImage image;
    image.width = request.tile.width;
    image.height = request.tile.height;
    image.bytesPerLine = request.tile.width * 4;
    image.format = core::PixelFormat::Bgrx8888;

    // Allocated before the lock is taken. A large allocation inside the engine lock would serialize
    // every worker on the allocator as well as on PDFium, which is the difference between a lock
    // that costs 5 % and one that costs half the throughput.
    image.pixels = QByteArray(static_cast<qsizetype>(image.bytesPerLine) * image.height,
                              Qt::Uninitialized);

    int flags = FPDF_ANNOT;   // annotation appearances are part of the page unless asked otherwise
    if (!core::render::hasFlag(request.flags, core::render::RenderFlags::Annotations))
        flags = 0;
    if (core::render::hasFlag(request.flags, core::render::RenderFlags::Grayscale))
        flags |= FPDF_GRAYSCALE;
    if (core::render::hasFlag(request.flags, core::render::RenderFlags::NoSmoothText))
        flags |= FPDF_RENDER_NO_SMOOTHTEXT;
    if (core::render::hasFlag(request.flags, core::render::RenderFlags::NoSmoothImage))
        flags |= FPDF_RENDER_NO_SMOOTHIMAGE;
    if (core::render::hasFlag(request.flags, core::render::RenderFlags::NoNativeText))
        flags |= FPDF_NO_NATIVETEXT;

    {
        // EVERYTHING inside the lock -- LoadPage, bitmap creation, the render, and the destroy.
        // Locking only the render call leaves `FPDFBitmap_Create` and `_Destroy` racing on
        // PDFium's allocator, which is what the T3 probe crashed inside (I-05).
        const std::lock_guard<std::mutex> guard(engineLock());

        auto* document = static_cast<FPDF_DOCUMENT>(document_.engineHandle());
        if (document == nullptr) {
            return rasterError(core::ErrorCode::Corrupted, "error.render.failed",
                               QString::number(request.pageIndex));
        }

        FPDF_PAGE enginePage = FPDF_LoadPage(document, request.pageIndex);
        if (enginePage == nullptr) {
            return rasterError(core::ErrorCode::ParseFailure, "error.render.noSuchPage",
                               QString::number(request.pageIndex));
        }

        // The bitmap is built over OUR buffer rather than PDFium's, so the pixels never have to be
        // copied out -- `FPDFBitmap_CreateEx` takes the stride, which is why `RasterImage` carries
        // one rather than assuming `width * 4` (I-03's argument, and the reason a padded buffer
        // draws diagonal garbage when a caller assumes otherwise).
        FPDF_BITMAP bitmap =
            FPDFBitmap_CreateEx(image.width, image.height, FPDFBitmap_BGRx,
                                image.pixels.data(), image.bytesPerLine);
        if (bitmap == nullptr) {
            FPDF_ClosePage(enginePage);
            return rasterError(core::ErrorCode::IoFailure, "error.render.failed",
                               QString::number(request.pageIndex));
        }

        // White, not transparent. A page is paper; `Bgrx8888` carries no alpha, and leaving the
        // buffer uninitialized would render text onto whatever the allocator last held.
        FPDFBitmap_FillRect(bitmap, 0, 0, image.width, image.height, 0xFFFFFFFF);

        // THE CALL THAT LOOKS WRONG AND IS NOT. The size arguments are the WHOLE scaled page and
        // the origin is NEGATIVE: that is how `FPDF_RenderPageBitmap` renders a region into a
        // smaller bitmap. Passing the tile's size as the page size scales the entire page into the
        // tile instead, putting a thumbnail of the page in every tile.
        FPDF_RenderPageBitmap(bitmap, enginePage, -request.tile.x, -request.tile.y, pageWidth,
                              pageHeight, /*rotate=*/0, flags);

        FPDFBitmap_Destroy(bitmap);
        FPDF_ClosePage(enginePage);
    }

    if (!image.isValid()) {
        return rasterError(core::ErrorCode::Corrupted, "error.render.failed",
                           QString::number(request.pageIndex));
    }
    return image;
}

} // namespace rawpage::pdf::render
