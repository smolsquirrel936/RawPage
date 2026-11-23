// pdf/ops/ImpositionOps -- a document's pages drawn onto sheets, n-up or as a booklet, as a NEW document. Stage 7 T16,
// decisions D7-8 and D7-46 (`Stage7-Assembly-Plan.md` §3); guide cases A-102 ... A-104.
//
// -------------------------------------------------------------------------------------------
// THE PLAN IS `core/`'s; THIS FILE DRAWS IT AND DECIDES NOTHING
// -------------------------------------------------------------------------------------------
// Which page goes in which cell of which side, where it is placed, what is turned and where every mark goes is
// `core::planImposition`'s (`core/doc/Imposition.h`), held by hand tables in `tst_imposition`. Here:
//   * EVERY PAGE IS COPIED ONCE by `ObjectGraphCopier` -- its inheritance made its own, the page tree left behind -- and
//     each copied page is made ONE form XObject by `PageStamp::pageForm` (its CropBox the `/BBox`, its resources and
//     transparency group the page's, its content its own bytes), every object those reach written once for the whole
//     document: a font two pages share is one object (guide A-104's census). A page's ANNOTATIONS are not drawn -- a
//     form draws content -- and are counted (`ImposedDocument::annotationsNotDrawn`), as a page stamp's are not drawn
//     (D7-36(c)); its links, fields, outline and tags stay behind with them.
//   * EACH SIDE IS ONE PAGE of the new document, the sheet's size, with one content stream: each page's form drawn
//     through `PageStamp::showMatrix` -- upright as a reader shows it, scaled onto its placed box -- inside a clip to its
//     cell; then the frames, the crop and registration marks in REGISTRATION colour (`/Separation /All`, so they print on
//     every plate), the colour bar in DeviceCMYK, and the sheet label in D6-7's faces (`core::layoutTypedText` -- a
//     character none can show refuses the imposition by its key). A side the plan turns is drawn inside one half turn
//     about the sheet's centre, marks and label with it.
//   * The payload -- catalog 1, node 2, the sides 3 ... in order, then every other object -- is BUILT by
//     `DocumentAssembler` (a balanced tree, a fresh `/ID`, `FullWriter`) and VERIFIED against it as an extract's is
//     (guide A-47), so an imposition is a new document like any other: an untitled tab until Saved As (Q1).
//
// REFUSED by key, with nothing made: the plan's (`error.imposition.*`), a page whose box or content a form cannot carry
// (`error.imposition.pageUnsupported`, its number and what was met), the copier's and the verify's.
//
// PORTABLE, in `rawpage_ops`: no engine -- `portable-asan` builds it and `qpdf --check`s what `tst_impositionops` keeps.

#ifndef RAWPAGE_PDF_OPS_IMPOSITIONOPS_H
#define RAWPAGE_PDF_OPS_IMPOSITIONOPS_H

#include "core/Result.h"
#include "core/doc/Assembly.h"
#include "core/doc/Imposition.h"

#include <string>
#include <string_view>

namespace rawpage::pdf::cos {
class XrefTable;
}

namespace rawpage::pdf::ops {

struct PageWalk;

class ImpositionOps {
public:
    struct Payload {
        std::string pdf;                 // catalog 1, node 2, the sides from 3 -- not yet built
        core::ImpositionPlan plan;
        int annotationsNotDrawn = 0;
        long long forms = 0;             // form XObjects written: one per source page
        long long objects = 0;           // every object the payload holds, catalog and node excluded
    };

    // Every page of `source` (walked as `walk`) imposed by `spec` -- the payload, not built.
    static core::Result<Payload> payload(cos::XrefTable& source, const PageWalk& walk, const core::ImpositionSpec& spec,
                                         const core::ImpositionText& text);

    // The payload built and verified, as a new document. `idSeed` is folded into its `/ID`; the caller makes it unique.
    static core::Result<core::ImposedDocument> impose(cos::XrefTable& source, const PageWalk& walk, const core::ImpositionSpec& spec,
                                                      const core::ImpositionText& text, std::string_view idSeed);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_IMPOSITIONOPS_H
