// core/doc/DrawnShapes -- the geometry of what the drawing tools make: line endings, a cloud's bumps, an ink
// stroke's curve, the eraser, constrained drags and the rectangle each shape needs. Stage 6 T8, decisions D6-15
// and D6-35 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// ONE PLACE FOR EACH SHAPE, READ BY EVERY PARTY THAT NEEDS IT
// ===========================================================================================
// The appearance builder (`pdf/ops/AppearanceStreamBuilder`) DRAWS these shapes, the tools (`ui/viewer/tools/
// DrawingTool`, `InkTool`) and the overlay MAKE annotations from them, and the hit test
// (`core/doc/AnnotationGeometry`) and the rectangle a shape is written with must agree with what is drawn --
// so a cloud's bumps, an arrowhead's barbs and an ink stroke's curve are computed here once. A rectangle worked
// out from a shape different from the one drawn is a stroke clipped in every reader (ISO 32000-1 Algorithm 8.1
// maps the form's box onto `/Rect`, and the builder's box IS the rectangle).
//
// ===========================================================================================
// INK KEEPS ITS SAMPLES; THE CURVE IS PRESENTATION (D6-15)
// ===========================================================================================
// `/InkList` holds the samples as a person drew them, deduplicated (`dedupedSamples`); the appearance draws a
// CENTRIPETAL Catmull-Rom curve through them (`inkCurve`) -- it passes through every sample and, unlike the
// uniform variant, does not overshoot or loop within a segment at a sharp turn. Each segment is a cubic Bezier
// whose control points lie inside the segment's neighbourhood, so the rectangle grown from the control points'
// bounds by half the stroke (`inkRect`) holds the whole stroke: a Bezier lies inside its control points' hull.
// The eraser (`erasedStrokes`) removes the SAMPLES it covers and splits a stroke where it did, so an undo puts
// the very samples back -- a smoothed curve stored instead could never be split where the eraser crossed it.
//
// ===========================================================================================
// LINE ENDINGS (ISO 32000-1 Table 176)
// ===========================================================================================
// Ten names. A head's size is not ISO's to fix: `lineEndingSize` is the reader's convention and the corpus
// generator's -- 4.5 times the border, never less than 9 points -- and an arrow's barbs are swept back 30
// degrees from the shaft (25 for a callout's, T7's). Every ending fits inside a disk of half its size round the
// tip but the arrows, which `AnnotationGeometry` hits over the wedge their barbs span.
//
// Pure: no engine, no display -- `tst_drawnshapes` in `rawpage_tests`.

#ifndef RAWPAGE_CORE_DOC_DRAWNSHAPES_H
#define RAWPAGE_CORE_DOC_DRAWNSHAPES_H

#include "core/Geometry.h"
#include "core/doc/Annotation.h"

#include <QString>

#include <cstddef>
#include <optional>
#include <vector>

namespace rawpage::core {

// ---- Line endings --------------------------------------------------------------------------------
// Whether `name` is one of the ten names ISO 32000-1 Table 176 gives a line ending.
bool isLineEndingName(const QString& name);

// How long a line's head is drawn for a border `width` wide: max(9, 4.5 w).
double lineEndingSize(double width);
inline constexpr double kArrowSpreadDegrees = 30.0;

struct EndingShape {
    // Open: a polyline, stroked. Closed: a polygon, filled with the interior colour when there is one and
    // stroked. Circle: a circle round `centre`, likewise.
    enum class Kind { Open, Closed, Circle };
    Kind kind = Kind::Open;
    std::vector<double> points;   // x y pairs (Open, Closed)
    PdfPoint centre;              // Circle
    double radius = 0.0;          // Circle
};

// The ending `name` at `tip` of a line arriving from `from`, `size` long, its barbs `spreadDegrees` off the shaft.
// Nullopt for `None`, an unknown name, or a line of no length (no direction to point).
std::optional<EndingShape> lineEnding(const QString& name, PdfPoint tip, PdfPoint from, double size,
                                      double spreadDegrees = kArrowSpreadDegrees);

// ---- Curves ----------------------------------------------------------------------------------------
struct CubicSegment {
    PdfPoint p0, c1, c2, p3;
};

// THE CURVE AN INK STROKE IS DRAWN AS: centripetal Catmull-Rom through every sample of `stroke` (x y pairs),
// one cubic per pair of neighbouring samples. A stroke of one sample is no segment (it is drawn as a dot).
std::vector<CubicSegment> inkCurve(const std::vector<double>& stroke);

// Samples further than `minimum` from the last one kept, in order -- the first always kept (D6-15's
// deduplication; the tool passes 0.25 device pixels in page units).
std::vector<double> dedupedSamples(const std::vector<double>& xy, double minimum);

// ---- Shapes drawn in steps along their length ----------------------------------------------------------
// The most steps one annotation's border or mark drawn in repeated steps may take, summed over the annotation --
// a cloud's bumps (here) and a squiggly's zigzag (`pdf/ops/AppearanceStreamBuilder`). Past it the shape is NOT
// DRAWN, and the builder refuses it by key: a length read from a file is any finite number, and a step of a fixed
// size along 10^13 points is a path no memory holds (S6 guide I-36, the fuzz soak's out-of-memory). A million is
// far past anything on a page: a squiggly's step is at least 0.6 point, so 600,000 points of squiggle -- about 30
// diagonals of the largest page ISO 32000-1 Annex C has a reader keep, 14,400 units a side, and some 25 Letter
// pages of 8-point text squiggled whole (67 lines of 468 points at a step of 0.81, ~39,000 steps a page); and a
// cloud's bump is at least 5 points.
inline constexpr std::size_t kMaxDrawnSteps = 1000000;

// ---- Clouds (ISO 32000-1 Table 167) ----------------------------------------------------------------
// A cloudy border's bump: each edge is cut into spans no shorter than this, each span a half circle bulging
// OUTWARD. 5 points plus 5 per unit of intensity: intensity 1 is 10 points, the corpus generator's, and 2 is 15.
double cloudBump(double intensity);

// The closed path of a cloudy border round `vertices` (x y pairs, at least three corners), as cubics -- two per
// bump, each a quarter circle. Empty for fewer than three corners, no area, or more than `kMaxDrawnSteps` bumps
// (S6 guide I-37). The bumps bulge away from the polygon's inside, whichever way round it is written.
std::vector<CubicSegment> cloudPath(const std::vector<double>& vertices, double intensity);

// How far a cloud's bumps reach past its edges: the largest bump's radius.
double cloudReach(const std::vector<double>& vertices, double intensity);

// ---- The rectangle a shape is written with ----------------------------------------------------------
// The rectangle `a` needs for what the builder draws of it -- a Line's ends and its endings, a polygon's
// vertices (and a cloud's bumps, which it also writes as `/RD`), an ink stroke's curve -- grown by half its
// border and a point for anti-aliasing. A Square or a Circle keeps its own rectangle. Returns `a` with its
// rectangle (and `/RD`) set; nullopt for geometry that draws nothing (a line of two points that are one, a
// polygon of fewer than three corners, an ink list with no sample).
std::optional<Annotation> fittedShape(Annotation a);

// ---- Gestures -----------------------------------------------------------------------------------------
// `to`, moved so the line from `from` runs at a multiple of 45 degrees -- `Shift` on a line's drag -- keeping
// its length along that direction.
PdfPoint constrainedTo45(PdfPoint from, PdfPoint to);
// `to`, moved so the box from `from` is a square -- `Shift` on a rectangle's or an oval's drag -- its side the
// drag's longer one, in the drag's own direction.
PdfPoint constrainedSquare(PdfPoint from, PdfPoint to);

// THE ERASER (D6-15): every sample of `strokes` within `radius` of the polyline `path` (x y pairs; a single
// point is a dot) removed, and each stroke split where samples were removed. A piece of ONE sample is a
// fragment and goes too; a stroke that was one sample (a dot) stays unless it is covered. `changed` is false
// when nothing was covered.
struct ErasedInk {
    bool changed = false;
    std::vector<std::vector<double>> strokes;
};
ErasedInk erasedStrokes(const std::vector<std::vector<double>>& strokes, const std::vector<double>& path, double radius);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_DRAWNSHAPES_H
