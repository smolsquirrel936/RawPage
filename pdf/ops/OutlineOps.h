// pdf/ops/OutlineOps -- what an outline item's dictionary becomes when a bookmark command changes it. Stage 7
// T11, decision D7-10 (`Stage7-Assembly-Plan.md` §3).
//
// -------------------------------------------------------------------------------------------
// AN EDIT CHANGES WHAT THE MODEL OWNS AND PRESERVES THE REST (D6-5's rule, D7-10)
// -------------------------------------------------------------------------------------------
// Every function here takes the item's dictionary as it stands and writes only what the model says
// differently: a title equal to the one written keeps its bytes (a producer's PDFDocEncoding stays PDFDocEncoding),
// a colour or a style the same keeps its key as written, and a key this build does not model -- `/SE`, a
// producer's private key -- is never touched. `pdf/ops/MutationSinkImpl` decides WHICH items are written (an
// item whose dictionary comes out the same is not); this file decides what one becomes.
//
// -------------------------------------------------------------------------------------------
// WHAT A TARGET IS WRITTEN AS
// -------------------------------------------------------------------------------------------
// A destination in this document is written as `/Dest` with an explicit array -- its page by REFERENCE, so a later
// move needs no repair (D7-2) -- and every other kind as an action dictionary in `/A`. Setting either removes the
// other: ISO 32000-1 Table 153 forbids both. **Written, never run** (D7-12): a JavaScript is a string here and
// nothing in the product executes it. An `Other` action -- a kind this build does not model -- is never written:
// only kept, as the file has it.

#ifndef RAWPAGE_PDF_OPS_OUTLINEOPS_H
#define RAWPAGE_PDF_OPS_OUTLINEOPS_H

#include "core/Result.h"
#include "core/doc/Outline.h"
#include "pdf/cos/CosObject.h"

#include <optional>

namespace rawpage::pdf::ops {

class OutlineOps {
public:
    // `view` as an explicit destination array naming `page` -- a reference, or an integer page of another file.
    // A fit this build cannot write with its parameters (`/FitR`, whose rectangle the model does not carry) is
    // written `/Fit`.
    static cos::CosObject destinationArray(cos::CosObject page, const core::Destination& view);

    // `dictionary` with its target set to `action`: `/Dest` and `/A` removed, then the one `action` needs.
    // `page` is the reference a `GoTo` names -- nullopt for a page the document no longer has, which writes no
    // target at all (what `ReferenceRepair` makes of a destination to a deleted page, D7-2). Refused for `Other`.
    static core::Result<cos::CosObject> withTarget(cos::CosObject dictionary, const core::OutlineAction& action,
                                                  std::optional<cos::CosObject> page);

    // `dictionary` with the model's title, colour and style written where they differ from what it holds.
    static cos::CosObject withProperties(cos::CosObject dictionary, const core::OutlineItem& item);

    // `/C` as the reader reads it, and `/F`'s two bits.
    static std::optional<std::array<double, 3>> colourOf(const cos::CosObject& dictionary);
    static long long flagsOf(const cos::CosObject& dictionary);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_OUTLINEOPS_H
