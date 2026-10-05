// core/doc/AnnotationGeometry -- an annotation's geometry moved, resized and hit. Stage 6 T5, decisions
// D6-27 and D6-30 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// A MOVE OR A RESIZE CARRIES EVERY POINT WITH THE RECTANGLE
// ===========================================================================================
// ISO 32000-1 §12.5.5 draws an appearance by mapping its box onto the annotation's `/Rect`, so a
// rectangle moved or scaled carries the drawing with it -- and the geometry beside it (quadpoints,
// vertices, the line, ink samples, the callout) must move by THE SAME mapping, or a reader that draws
// from the geometry (PDFium regenerates a missing `/AP` from it; a later edit regenerates ours) puts the
// annotation somewhere the `/AP` does not. `mappedInto` is that one mapping. `translated` is the same
// move by addition rather than through a scale of one, so ten nudges add exactly what they say.
//
// `isMappedCopy` is the question `pdf/ops/MutationSinkImpl` asks before it decides whether an edit's
// appearance must be redrawn: an annotation that is another carried into its new rectangle, and
// otherwise the same, keeps the appearance it has -- the producer's, unredrawn (D6-5).
//
// ===========================================================================================
// AN ANNOTATION IS HIT WHERE IT IS DRAWN (guide case A-41, rule 6) -- D6-30
// ===========================================================================================
// Its rectangle is where it MAY be drawn, not where it is: an unfilled rectangle is its border, a line is
// a stroke across a box mostly empty, an ink scribble is its strokes. So each kind is hit by its own
// shape, at the border width it is drawn with plus a TOLERANCE the caller gives in page units -- the view
// converts a fixed number of device pixels, so a hit is as easy at 50 % as at 400 % (Stage 4's snapping
// threshold, D4-6, in the same units for the same reason).
//
// **Text markup is hit over its whole quadrilateral**, the words it marks, and that is a decision rather
// than a shape: an underline's ink is a line under the word, and a person clicks the word. Acrobat does
// the same. A box -- a note's icon, a stamp, free text, a caret, an attachment -- is hit over its box. A
// line's endings are hit over the wedge or disk they are drawn in, and a CLOUDY border over the band `/RD`
// gives its bumps (the bumps are `/BE`'s, which the model does not hold). Measured at T5 against the ink
// (guide A-41): a reach taken from the rectangle's margin instead let a plain polygon and a line's middle
// be hit three pixels off their strokes at 150 %.
// `Hidden` and `NoView` annotations are not drawn and are never hit (D6-27); a `Popup` is a UI surface
// (D6-28) and is hit by the overlay, never here.
//
// Pure: no engine, no display -- `tst_annotedit` in `rawpage_tests`; against the INK, `tst_annotink`.

#ifndef RAWPAGE_CORE_DOC_ANNOTATIONGEOMETRY_H
#define RAWPAGE_CORE_DOC_ANNOTATIONGEOMETRY_H

#include "core/Geometry.h"
#include "core/doc/Annotation.h"

#include <optional>

namespace rawpage::core {

// `a` with its rectangle `to`, and every point of its geometry carried by the mapping that takes its
// rectangle onto `to` -- an axis-aligned scale and translation. `/RD` scales with it; a border width does
// not (it is drawn, not placed). nullopt for a rectangle that is not finite, or a `to` that is not.
std::optional<Annotation> mappedInto(const Annotation& a, const PdfRect& to);

// `a` moved by (dx, dy) page units: every coordinate plus the offset, exactly.
Annotation translated(const Annotation& a, double dx, double dy);

// Whether `after` is `before` carried into `after.rect` by `mappedInto`, coordinates equal within
// `tolerance` page units, and every other field equal save `/M` -- an edit that only moved or resized.
bool isMappedCopy(const Annotation& before, const Annotation& after, double tolerance = 1e-6);

// Whether a move by (dx, dy) changes nothing a reader would see: both zero, or not finite.
bool isNoMove(double dx, double dy);

// Whether the overlay offers resize handles: kinds whose box a person sizes. A note, a caret, an
// attachment's pin and a sound are icons; text markup follows its words.
bool isResizable(AnnotationSubtype subtype);

// D6-30: whether `point` (page space) is on `a` as it is drawn, `tolerance` page units out.
bool hitsAnnotation(const Annotation& a, PdfPoint point, double tolerance);

// The width a kind's outline is drawn with: `/BS /W`, else ISO 32000-1's default of 1.
double drawnBorderWidth(const Annotation& a);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_ANNOTATIONGEOMETRY_H
