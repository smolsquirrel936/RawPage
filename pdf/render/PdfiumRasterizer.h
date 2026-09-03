// pdf/render/PdfiumRasterizer -- implements `core::IPageRasterizer`. Stage 2 T9.
//
// One `RenderRequest` in, one `core::RasterImage` out. **Nothing but buffers and callbacks crosses
// this boundary** (D2-9): no `FPDF_BITMAP`, no page handle, no engine ownership. The result is a
// `QByteArray` of BGRA bytes with an explicit stride, which `ui/` wraps in a `QImage` at the far
// side without a copy — `core/` may not name a QtGui type (I-03), and this is the seam that rule
// produced.
//
// -------------------------------------------------------------------------------------------
// HOW A TILE IS RENDERED, AND THE ARGUMENT THAT LOOKS WRONG
// -------------------------------------------------------------------------------------------
// `FPDF_RenderPageBitmap` has no "render this rectangle" parameter. A REGION is rendered by
// passing the WHOLE scaled page's size together with a NEGATIVE origin: the bitmap is the tile's
// size, the size arguments stay the full page, and `(-tile.x, -tile.y)` slides the page under the
// window. Passing the tile's size as the page size instead scales the whole page down into the
// tile, which produces a thumbnail of the page in every tile — a bug that looks like a zoom
// problem and is a coordinate one. The A2 probe (T3) established the shape; this is the same call.
//
// -------------------------------------------------------------------------------------------
// THE ENGINE LOCK IS NOT OPTIONAL, AND D2-9's SECOND HALF IS MEASURED FALSE
// -------------------------------------------------------------------------------------------
// D2-9 says *"thread-confined per worker"*. **That was measured false at T3 — issue I-05.** What
// PDFium shares between threads is process-global (its font mapper and glyph caches), so eight
// workers with eight documents and eight pages corrupt the heap, and two are enough. Confining a
// document to a worker confines the wrong thing.
//
// So every entry into the engine is serialized on one process-wide lock, which is the conservative
// half of the choice **T11** still has to make (one render thread with a queue, or N workers
// behind one lock). It is correct under either outcome, and it is why A-45 — eight workers,
// concurrently, no crash — is a test this class can pass at all.
//
// The throughput cost was measured rather than feared: eight workers behind one lock retire
// **1.05×** the tiles per second of one worker, and per-tile p95 stays at 0.532 ms against a
// 16.667 ms budget. A2 is unaffected.

#ifndef RAWPAGE_PDF_RENDER_PDFIUMRASTERIZER_H
#define RAWPAGE_PDF_RENDER_PDFIUMRASTERIZER_H

#include "core/backend.h"
#include "core/render/RenderRequest.h"

namespace rawpage::pdf::render {

class PdfiumDocument;

class PdfiumRasterizer final : public core::IPageRasterizer {
public:
    // Holds the document by reference: `DocumentBackendImpl` owns both and destroys the rasterizer
    // first. A `shared_ptr` here would let a rasterizer outlive the backend that vends it, which is
    // exactly the ownership crossing the boundary rule forbids.
    explicit PdfiumRasterizer(PdfiumDocument& document);

    core::Result<core::RasterImage> raster(const core::render::RenderRequest& request) override;

private:
    PdfiumDocument& document_;
};

} // namespace rawpage::pdf::render

#endif // RAWPAGE_PDF_RENDER_PDFIUMRASTERIZER_H
