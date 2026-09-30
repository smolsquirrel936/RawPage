// pdf/render/PdfiumTextExtractor -- `core::ITextExtractor` over PDFium's text page. Stage 5 T2,
// decision D5-2 (`Stage5-TextSearch-Plan.md` §3).
//
// *We read through PDFium and write through our own code*: extraction is a read, so it is the
// engine's, and this file is the whole of it. It fills a `core::text::PageTextModel` one Unicode
// scalar at a time -- box, origin, angle, font, size and the three flags -- and nothing crosses back
// out but that model.
//
// ---------------------------------------------------------------------------------------------
// WHAT IS DELIBERATELY NOT CALLED
// ---------------------------------------------------------------------------------------------
// `FPDFText_GetText` and `FPDFText_Find*`. The first is a string without geometry, and every
// consumer needs the geometry; the second matches one page at a time with no diacritic folding, no
// regular expression, no content classes and no index -- ON THE ENGINE THREAD, behind the lock the
// rendering depends on (D5-2). Find is `core/text/`'s, over models this file produced.
//
// ---------------------------------------------------------------------------------------------
// THREADING -- the rasterizer's rule, unchanged
// ---------------------------------------------------------------------------------------------
// Everything that touches PDFium is inside `engineLock()`, and the product calls this from ONE
// thread: the render scheduler's worker, as a text job (D5-3). The UI thread never extracts. Both
// facts are asserted rather than trusted -- the thread by guide case A-24, the lock by its absence
// being I-05's heap corruption.
//
// ---------------------------------------------------------------------------------------------
// THE HANDLE
// ---------------------------------------------------------------------------------------------
// One extractor per open document, handed out by `IDocumentBackend::textExtractor(handle)` like the
// rasterizer. It knows its own handle and REFUSES another with `InvalidArgument` (guide case A-25):
// an extractor asked about a document it does not serve would otherwise answer with the text of
// the one it does -- a wrong answer with no error, which is worse than no answer.

#ifndef RAWPAGE_PDF_RENDER_PDFIUMTEXTEXTRACTOR_H
#define RAWPAGE_PDF_RENDER_PDFIUMTEXTEXTRACTOR_H

#include "core/backend.h"

namespace rawpage::pdf::render {

class PdfiumDocument;

class PdfiumTextExtractor final : public core::ITextExtractor {
public:
    PdfiumTextExtractor(PdfiumDocument& document, core::DocumentHandle self);

    // Fills `into` with page `pageIndex`'s characters, or leaves it UNTOUCHED and fails:
    // `InvalidArgument` for a foreign handle or a page outside the tree, `ParseFailure`
    // (`error.text.extractFailed`) for a page the engine cannot load, `Unavailable` for a closed
    // document. A page with no text is SUCCESS and an empty model -- an image-only page is a value,
    // not an error.
    core::Status extractPage(core::DocumentHandle document, int pageIndex,
                             core::text::PageTextModel& into) override;

private:
    PdfiumDocument& document_;
    core::DocumentHandle self_;
};

} // namespace rawpage::pdf::render

#endif // RAWPAGE_PDF_RENDER_PDFIUMTEXTEXTRACTOR_H
