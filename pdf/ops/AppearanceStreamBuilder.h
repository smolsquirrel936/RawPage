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
//
// ===========================================================================================
// UNDERLINE, STRIKEOUT, SQUIGGLY, CARET, NOTE -- Stage 6 T6
// ===========================================================================================
// The three text lines are drawn in each quadrilateral's OWN frame -- along its bottom edge and across
// towards its top -- so a line of vertical text is underlined along its side, not across it. Which edge
// is the bottom is read from the corner order itself: Acrobat's two horizontal edges run the same way,
// ISO's opposite ways. An underline is PDFium's own generation's (a 1-point line one point above the bottom
// edge, up to text 14 points tall, growing with the text past it). A strikeout and a squiggle are NOT where
// PDFium puts them, because guide case A-50 measured PDFium's against the glyphs and found them wrong: a
// strikeout through the box's middle sits above the x-height's middle third, and a 2-point squiggle spills out
// of the descender band -- so the strikeout is 42 % of the way up the box and the squiggle between 7 % and
// 16 % of it (D6-32). No blend: only a highlight multiplies. A caret is Acrobat's notched wedge; a note is the `Comment` speech bubble (or the
// `Note` page, the default `/Name`) in `/C`, outlined in black. A `Popup` has no appearance at all.

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
    // Whether this build can draw `subtype`. Stage 6 T3: Highlight; T6: Underline, StrikeOut, Squiggly,
    // Caret, and Text (a note, its `Comment` and `Note` icons -- another `/Name` is refused by `normal`).
    static bool supports(core::AnnotationSubtype subtype) noexcept;

    // A kind that has NO appearance of its own: a `Popup` (ISO 32000-1 §12.5.6.14), which a reader draws as
    // its own window -- RawPage's overlay draws it, and PDFium draws none (Stage 6 T6). One is written with no
    // `/AP`, and verify asks it for none.
    static bool drawsNothing(core::AnnotationSubtype subtype) noexcept;

    // The normal appearance of `annotation`. `error.pdf.appearanceUnsupported` for a subtype this build
    // cannot draw; `error.pdf.appearanceGeometry` for geometry it cannot draw from (a highlight with no
    // whole quadrilateral, a non-finite coordinate, a colour of 2 components).
    static core::Result<Appearance> normal(const core::Annotation& annotation);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_APPEARANCESTREAMBUILDER_H
