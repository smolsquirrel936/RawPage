// pdf/ops/TransitionReader -- the document's `/Trans`, `/Dur` and `/PageMode`, read first-party.
// Stage 4 T10, decision **D4-12** (`Stage4-ViewingSurface-Plan.md` §3).
//
// ===========================================================================================
// WHY THIS IS FIRST-PARTY WHEN D4-3 SAYS TO READ THROUGH PDFIUM.
// ===========================================================================================
// **Because PDFium cannot.** Its public C API exposes page transitions nowhere -- there is no
// `FPDFPage_GetTransition`, and `FPDFPage_HasTransparency` is a different subject wearing a
// similar name. D4-3's rule is *read through PDFium*, and its unstated remainder is *unless PDFium
// cannot*; this is the first read in the project to reach it, and `core/doc/PageTransitions.h`
// records the same thing from the model's side so a reader arriving from either direction finds
// the reason rather than an inconsistency.
//
// ===========================================================================================
// IT COSTS NO SECOND PARSE, WHICH IS WHY IT LIVES IN `pdf/ops/` AND TAKES A WALK.
// ===========================================================================================
// `pdf/DocumentBackendImpl::open` already runs `MutationSinkImpl::open` on **every** document,
// editable or not, to check that our reading of the page tree agrees with the engine's -- it is
// what `core::Permissions::assess` is handed. So this takes the `XrefTable` and `PageWalk` that
// already exist and reads four more keys out of dictionaries already fetched. A reader that opened
// the file again would double the parse cost of every document for a feature almost no document
// uses.
//
// It is in `pdf/ops/` rather than `pdf/cos/` because `pdf/cos/` is the object model and the xref,
// and knowing that `/Trans` means a page transition is document semantics -- the same line
// `PageOps` sits on.
//
// ===========================================================================================
// A TOLERANT READ, BECAUSE THE ALTERNATIVE IS A DOCUMENT THAT WILL NOT PRESENT.
// ===========================================================================================
// Every field is optional in the file and several are written wrongly in the wild. Nothing here
// returns an error: a `/Trans` that is not a dictionary, a `/S` that is not a name, a `/D` that is
// a string -- each yields *no transition on that page*, and the page falls through to the
// preference. A reader that refused the document would turn a cosmetic defect into a file that
// cannot be opened, which is the tolerance `pdf/cos/Lexer` is built around, one layer up.

#ifndef RAWPAGE_PDF_OPS_TRANSITIONREADER_H
#define RAWPAGE_PDF_OPS_TRANSITIONREADER_H

#include "core/doc/PageTransitions.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/PageOps.h"

namespace rawpage::pdf::ops {

class TransitionReader {
public:
    // Fills `out` from `table` over the pages `walk` found, in document order. Never fails: a
    // document with nothing to say yields an empty model (`PageTransitions::isEmpty()`), which is
    // the answer for almost every file.
    static void read(cos::XrefTable& table, const PageWalk& walk, core::PageTransitions& out);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_TRANSITIONREADER_H
