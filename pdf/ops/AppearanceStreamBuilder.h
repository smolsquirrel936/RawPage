// pdf/ops/AppearanceStreamBuilder -- the `/AP /N` normal appearance of an annotation RawPage writes.
// Stage 6 T3 builds HIGHLIGHT; every other type Stage 6 creates is added by the task that builds its
// tool (T6 … T14), and until then `supports()` says no and the sink refuses to write one (D6-6).
//
// ===========================================================================================
// WE DRAW EVERY APPEARANCE OURSELVES (D6-6)
// ===========================================================================================
// A reader that finds no `/AP` invents one, and every reader invents a different one -- PDFium draws a
// `Line` with no `/AP` as nothing at all (Stage 6 guide I-02). So RawPage writes the appearance of
// everything it creates, and the four oracles that judge it are never this file: golden rasters LOOKED
// AT before they are kept, PDFium's own generation as a tolerance control, `qpdf --check`, and Acrobat
// and Firefox by a person. What this file promises is narrower and checked here: the stream is
// well-formed (`ContentStreamBuilder::check`, before it is returned) and its form is self-contained --
// every resource it names is in its own `/Resources`.
//
// ===========================================================================================
// A HIGHLIGHT
// ===========================================================================================
// A form XObject whose `/BBox` IS the annotation's `/Rect`, so ISO 32000-1 §12.5.5's mapping of the
// form onto the rectangle is the identity and the stream draws in page space. Each quadrilateral is
// filled in `/C` (yellow when the annotation has none, Acrobat's default) through an ExtGState with
// `/BM /Multiply` and the opacity as `/ca` and `/CA` -- multiply is what keeps the text under a
// highlight black, and it is how Acrobat draws its own. The four corners are drawn AROUND their
// centre, not in the order written: Acrobat writes top-left, top-right, bottom-left, bottom-right and
// ISO's figure shows another order (guide A-20), and drawing either as written would be a bow tie.

#ifndef RAWPAGE_PDF_OPS_APPEARANCESTREAMBUILDER_H
#define RAWPAGE_PDF_OPS_APPEARANCESTREAMBUILDER_H

#include "core/Result.h"
#include "core/doc/Annotation.h"
#include "pdf/cos/CosObject.h"

#include <string>

namespace rawpage::pdf::ops {

struct Appearance {
    cos::CosObject dictionary;   // the form XObject: a Stream with an empty data descriptor; the
                                 //   writer adds `/Length` and writes `data` after it
    std::string data;            // the content stream, unencoded
};

class AppearanceStreamBuilder {
public:
    // Whether this build can draw `subtype`. Stage 6 T3: Highlight.
    static bool supports(core::AnnotationSubtype subtype) noexcept;

    // The normal appearance of `annotation`. `error.pdf.appearanceUnsupported` for a subtype this build
    // cannot draw; `error.pdf.appearanceGeometry` for geometry it cannot draw from (a highlight with no
    // whole quadrilateral, a non-finite coordinate, a colour of 2 components).
    static core::Result<Appearance> normal(const core::Annotation& annotation);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_APPEARANCESTREAMBUILDER_H
