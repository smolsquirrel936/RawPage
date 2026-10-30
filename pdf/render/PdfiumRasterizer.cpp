#include "pdf/render/PdfiumRasterizer.h"

#include "pdf/render/PdfiumDocument.h"
#include "pdf/render/PdfiumInit.h"

#include <fpdf_edit.h>
#include <fpdf_formfill.h>
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

    // THE PAGE IS READ FROM THE TREE THE ENGINE DRAWS, UNDER THE LOCK, AFTER THE SWAP (Stage 7 T3, D7-3). A page
    // command reloads the engine with the pages in a new order, and the swap happens at the top of the first
    // engine call after it -- so the page read before taking the lock could be the old order's page at this
    // index, sized and turned for another page. Read inside, it is always the page the engine is about to draw.
    // The cost is that the bitmap's size is known only under the lock; the buffer is still allocated outside it.
    core::PdfSize pageSize;
    core::PageRotation pageOwnRotation = core::PageRotation::None;
    // S7 T4: a revision waiting for its verify is waited for HERE, holding no lock -- so other documents' jobs and
    // the UI thread's engine calls do not wait behind it (`PdfiumDocument::show`).
    document_.awaitShown();
    {
        const std::lock_guard<EngineMutex> guard(engineLock());
        if (document_.engineHandle() == nullptr) {
            return rasterError(core::ErrorCode::Corrupted, "error.render.failed",
                               QString::number(request.pageIndex));
        }
        const core::Page* page = document_.drawnPagesLocked().at(request.pageIndex);
        if (page == nullptr) {
            return rasterError(core::ErrorCode::NotFound, "error.render.noSuchPage",
                               QString::number(request.pageIndex));
        }
        pageSize = page->displaySize();
        pageOwnRotation = page->rotation();
    }

    // The page's size in device pixels, rounded UP. Rounding down loses the last row and column of
    // a page whose scaled size is fractional, which shows as a one-pixel white line along two
    // edges of every page at most zoom levels.
    // The page's own `/Rotate` is already in `displaySize()` AND is already applied by PDFium.
    // The VIEW rotation is ours and is passed to `FPDF_RenderPageBitmap` below as its `rotate`
    // argument, which is additive -- so here it only swaps the extent we ask for.
    // Stage 3 T8 (D3-15): an unsaved page rotation arrives as a DELTA on top of the `/Rotate` PDFium
    // already applies, and composes with the view rotation exactly as a second view turn would.
    const core::PageRotation drawn = core::combine(request.viewRotation, request.pageRotationDelta);
    const core::PdfSize display = core::swapsAxes(drawn)
                                      ? core::PdfSize{pageSize.height, pageSize.width}
                                      : pageSize;
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

    // TRANSPARENT OR WHITE, and the choice reaches the bitmap's FORMAT as well as its fill.
    // `Bgrx8888` has no alpha channel at all, so asking for a transparent background while
    // reporting that format would hand `ui/` four bytes whose fourth is padding -- and the page
    // would composite as opaque whatever was drawn.
    const bool transparent =
        core::render::hasFlag(request.flags, core::render::RenderFlags::TransparentBackground);

    core::RasterImage image;
    image.width = request.tile.width;
    image.height = request.tile.height;
    image.bytesPerLine = request.tile.width * 4;
    image.format = transparent ? core::PixelFormat::Bgra8888 : core::PixelFormat::Bgrx8888;

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
    if (core::render::hasFlag(request.flags, core::render::RenderFlags::NoSmoothPath))
        flags |= FPDF_RENDER_NO_SMOOTHPATH;
    if (core::render::hasFlag(request.flags, core::render::RenderFlags::NoNativeText))
        flags |= FPDF_NO_NATIVETEXT;

    {
        // EVERYTHING inside the lock -- LoadPage, bitmap creation, the render, and the destroy.
        // Locking only the render call leaves `FPDFBitmap_Create` and `_Destroy` racing on
        // PDFium's allocator, which is what the T3 probe crashed inside (I-05).
        const std::lock_guard<EngineMutex> guard(engineLock());

        auto* document = static_cast<FPDF_DOCUMENT>(document_.engineHandle());
        if (document == nullptr) {
            return rasterError(core::ErrorCode::Corrupted, "error.render.failed",
                               QString::number(request.pageIndex));
        }
        // ANOTHER SWAP MAY HAVE COME BETWEEN THE TWO LOCKS -- a content box asked on the UI thread enters the
        // engine too. A page that is no longer the one this bitmap was sized for is refused, and the tile is
        // asked for again at the next paint, rather than drawn into the other page's extent.
        const core::Page* page = document_.drawnPagesLocked().at(request.pageIndex);
        if (page == nullptr || page->displaySize() != pageSize || page->rotation() != pageOwnRotation) {
            return rasterError(core::ErrorCode::Unavailable, "error.render.failed",
                               QString::number(request.pageIndex));
        }

        FPDF_PAGE enginePage = FPDF_LoadPage(document, request.pageIndex);
        if (enginePage == nullptr) {
            return rasterError(core::ErrorCode::ParseFailure, "error.render.noSuchPage",
                               QString::number(request.pageIndex));
        }

        // Stage 6 T4 (D6-2): AFTER A RELOAD the engine may hold a newer `/Rotate` than `pages()`
        // describes -- the composed update carries any unsaved rotation as well as the annotations --
        // while the request's delta is relative to `pages()`. So the loaded page's own rotation is read
        // and the difference handed back here; before any reload the two agree and this adds nothing.
        // Without it, a rotation followed by a highlight on the same page turns that page twice.
        const int loadedQuarters = FPDFPage_GetRotation(enginePage);
        const int correction = static_cast<int>(page->rotation()) / 90 - (loadedQuarters < 0 ? 0 : loadedQuarters);
        const int rotate = static_cast<int>(core::rotateBy(drawn, correction)) / 90;

        // The bitmap is built over OUR buffer rather than PDFium's, so the pixels never have to be
        // copied out -- `FPDFBitmap_CreateEx` takes the stride, which is why `RasterImage` carries
        // one rather than assuming `width * 4` (I-03's argument, and the reason a padded buffer
        // draws diagonal garbage when a caller assumes otherwise).
        FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(
            image.width, image.height, transparent ? FPDFBitmap_BGRA : FPDFBitmap_BGRx,
            image.pixels.data(), image.bytesPerLine);
        if (bitmap == nullptr) {
            FPDF_ClosePage(enginePage);
            return rasterError(core::ErrorCode::IoFailure, "error.render.failed",
                               QString::number(request.pageIndex));
        }

        // WHITE unless the transparency grid asked otherwise. A page is paper, and leaving the
        // buffer uninitialized would render text onto whatever the allocator last held -- so the
        // fill is unconditional and only its COLOUR moves. `0x00000000` is fully transparent
        // black, which is what lets a viewer show what is behind an unpainted region.
        FPDFBitmap_FillRect(bitmap, 0, 0, image.width, image.height,
                            transparent ? 0x00000000 : 0xFFFFFFFF);

        // THE CALL THAT LOOKS WRONG AND IS NOT. The size arguments are the WHOLE scaled page and
        // the origin is NEGATIVE: that is how `FPDF_RenderPageBitmap` renders a region into a
        // smaller bitmap. Passing the tile's size as the page size scales the entire page into the
        // tile instead, putting a thumbnail of the page in every tile.
        // `rotate` IS THE VIEW ROTATION PLUS THE UNSAVED DELTA (and, after a reload, the correction
        // above), AND NOTHING ELSE. PDFium applies the
        // page's `/Rotate` itself, so passing the page's whole rotation here would rotate it twice --
        // which reads as "rotate view is broken for some documents and fine for others", the worst
        // shape of rendering bug because it depends on the file.
        FPDF_RenderPageBitmap(bitmap, enginePage, -request.tile.x, -request.tile.y, pageWidth,
                              pageHeight, rotate, flags);

        // AND THE WIDGETS, WHICH THE CALL ABOVE DOES NOT DRAW (I-21). PDFium renders every
        // annotation except `/Subtype /Widget` from `FPDF_RenderPageBitmap`; a widget's `/AP` is
        // reachable only through `FPDF_FFLDraw`. Without this, `forms-acroform.pdf` renders as its
        // labels and eleven blank spaces -- committed as a golden baseline for four days, because
        // a suite comparing our output with our own earlier output cannot see an absence.
        //
        // SAME GEOMETRY, SAME ROTATION, DELIBERATELY. The arguments mirror the render exactly: a
        // widget layer drawn at a different origin or rotation is worse than none, since it lands
        // on the page looking like a rendering fault rather than like a missing feature.
        //
        // GATED ON THE ANNOTATIONS FLAG, because a widget IS an annotation. Someone turning
        // annotations off and still getting form fields would be reading a flag that half works.
        auto* form = static_cast<FPDF_FORMHANDLE>(document_.formHandle());
        if (form != nullptr && (flags & FPDF_ANNOT) != 0) {
            // `FORM_OnAfterLoadPage` is what gives PDFium a per-page view of the form for this
            // page; `FPDF_FFLDraw` without it draws nothing at all on some documents and is the
            // kind of omission that looks like the feature simply not working. Its partner is
            // called before the page closes, below, and the pairing is why both are here rather
            // than one being folded into the draw.
            //
            // A FACT ABOUT THIS ARRANGEMENT, recorded because it is true rather than because
            // anything depends on it yet: the page's form view is created and torn down **inside
            // one tile's render**, so no form state survives between renders. That is exactly
            // right for drawing a `/AP` stream, which is what this is for, and it is the reason
            // nothing here is a foundation for interaction -- a field the user had focused or
            // typed into has nowhere to live across two tiles of the same page.
            FORM_OnAfterLoadPage(enginePage, form);
            FPDF_FFLDraw(form, bitmap, enginePage, -request.tile.x, -request.tile.y, pageWidth,
                         pageHeight, rotate, flags);
            FORM_OnBeforeClosePage(enginePage, form);
        }

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
