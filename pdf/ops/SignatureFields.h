// pdf/ops/SignatureFields -- how many signatures a document carries, read first-party. Stage 7 T6, D7-20's reading
// (`Stage7-Assembly-Plan.md` §3), for D7-25's question: Save As that rewrites breaks every signature, so a signed
// document is asked about first.
//
// WHAT COUNTS: a field of the catalog's `/AcroForm /Fields` tree, `/Kids` followed, whose field type is `/Sig` --
// its own `/FT` or one inherited from a `/Parent` (ISO 32000-1 Table 220, *inheritable*) -- and whose `/V` holds
// something that is not null. An empty signature field is a place to sign, not a signature, and a rewrite breaks
// nothing in it.
//
// WHAT IT DOES NOT DO: validate. Whether a signature verifies, whom it names, what it covers are Stage 11's. This
// is a count of what a validator would be asked about, and its only reader is the question.
//
// BOUNDED: each field object is visited once, so a `/Kids` loop ends; the walk stops past `kMaxFields` fields.

#ifndef RAWPAGE_PDF_OPS_SIGNATUREFIELDS_H
#define RAWPAGE_PDF_OPS_SIGNATUREFIELDS_H

namespace rawpage::pdf::cos {
class XrefTable;
}

namespace rawpage::pdf::ops {

class SignatureFields {
public:
    static constexpr int kMaxFields = 100000;

    // The signatures `table`'s document carries, as above; 0 for one with no `/AcroForm`.
    static int count(cos::XrefTable& table);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_SIGNATUREFIELDS_H
