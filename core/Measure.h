// core/Measure -- what a measurement MEASURES, and how it is written down. Stage 6 T12, decision D6-14
// (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// A MEASUREMENT IS AN ANNOTATION, AND ITS SCALE TRAVELS WITH IT
// ===========================================================================================
// A distance is a `Line` with `/IT /LineDimension`, a perimeter a `PolyLine` with `/IT /PolyLineDimension`, an area
// a `Polygon` with `/IT /PolygonDimension` -- each carrying a `/Measure` dictionary (ISO 32000-1 §12.9, `/Subtype
// /RL`) that states the scale in words (`/R`, "1 in = 4 ft") and the factors that turn page units into the
// drawing's (`/X`, then `/D` for a distance and `/A` for an area). A scale held only in a preference would be lost
// the moment the file was opened anywhere else; the dictionary is what Acrobat reads.
//
// ===========================================================================================
// THE SCALE IS ONE NUMBER AND A UNIT
// ===========================================================================================
// `measuring/scaleRatio` is DIMENSIONLESS: real length over paper length, both in one unit -- 1 in on paper
// standing for 4 ft is 48, a 1:100 plan is 100, actual size is 1. `measuring/displayUnit` is the unit a reading is
// given in and `measuring/precision` its decimals. So the scale string is DERIVED, never stored: its paper side is
// one unit of the display unit's own system (an inch for inches, feet, yards and miles; a millimetre for millimetres
// and centimetres; a centimetre for metres and kilometres; a point for points), and its real side that times the
// ratio in the display unit -- 48 with feet gives "1 in = 4 ft". The dialog that sets a scale from two lengths
// (`ui/dialogs/MeasurementScaleDialog`) reduces them to the ratio, and calibration does the same from a length
// measured on the page and the length it is known to be.
//
// ===========================================================================================
// THE READING IS COMPUTED FROM THE ANNOTATION, NOT STORED IN IT
// ===========================================================================================
// `measured(a)` reads a measurement from its geometry and its own `/Measure` -- the length of `/L` (the points the
// leader lines start from, ISO 32000-1 Table 175, so the leaders never change it), the sum of a polyline's
// segments, a polygon's area -- through the dictionary's first number format of each quantity. The appearance
// draws that reading, so a measurement moved or resized reads what it now spans; `/Contents` holds it too when
// `measuring/writeToComment` is on, which is what keeps it in the Comments pane and in a reader that draws
// nothing. A measurement this build cannot display -- a fraction, a chain of units ("4 ft 3 in"), axes scaled
// differently, a `/GEO` measure -- answers an error by key, and is never redrawn.
//
// ===========================================================================================
// A SELF-INTERSECTING POLYGON HAS NO AREA THIS BUILD WILL STATE
// ===========================================================================================
// The shoelace formula over a figure-eight sums its two loops with opposite signs and reports their difference, a
// number that is the area of nothing on the page. `polygonArea` refuses it BY NAME (`error.measure.selfIntersecting`)
// rather than report it (guide A-87).
//
// ===========================================================================================
// WHERE A DIMENSION'S PARTS ARE DRAWN
// ===========================================================================================
// `dimensionGeometry` is the one place a measured line's drawn parts are worked out -- the dimension line offset
// from `/L` by `/LL`, its two leader lines (from `/LLO` short of the points to `/LLE` past the line), and the
// caption's box -- and `measureLabel` a polyline's and a polygon's reading. The builder draws them, the hit test hits
// them and the rectangle a tool writes holds them, so the three cannot disagree (D6-35(a)'s rule, met again).
//
// Pure: QtCore only, no engine, no display -- `tests/unit/tst_measure.cpp` in `rawpage_tests`.

#ifndef RAWPAGE_CORE_MEASURE_H
#define RAWPAGE_CORE_MEASURE_H

#include "core/Geometry.h"
#include "core/Result.h"
#include "core/doc/Annotation.h"

#include <QLatin1String>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rawpage::core {

// ---- Units ---------------------------------------------------------------------------------------

// The units a reading is given in -- `Features.md` §4.1's display unit. Each is an exact rational number of points
// (an inch is 72, by the definition of PDF user space), so a conversion is exact to floating point.
enum class MeasureUnit : std::uint8_t {
    Points, Inches, Feet, Yards, Miles, Millimetres, Centimetres, Metres, Kilometres,
};
inline constexpr int kMeasureUnitCount = 9;

// The TOKEN is the unit's symbol -- `pt`, `in`, `ft`, `yd`, `mi`, `mm`, `cm`, `m`, `km` -- and it is three things at
// once: what `measuring/displayUnit` writes, what a reading shows after its number, and the `/U` a measure
// dictionary writes. A symbol, never translated (`core/Units.h` argues why). Unknown text answers nullopt.
std::string_view measureUnitToken(MeasureUnit unit) noexcept;
std::optional<MeasureUnit> measureUnitFromToken(std::string_view token) noexcept;
double pointsPerMeasureUnit(MeasureUnit unit) noexcept;
// The paper side of a scale string for a reading in `unit` -- see the header.
MeasureUnit paperUnitFor(MeasureUnit unit) noexcept;

// ---- The scale -------------------------------------------------------------------------------------

inline constexpr double kMinScaleRatio = 1.0e-6;
inline constexpr double kMaxScaleRatio = 1.0e9;
inline constexpr int kMaxMeasurePrecision = 6;

// What the three `measuring/*` scale keys say.
struct MeasureScale {
    double ratio = 1.0;                       // real length over paper length, dimensionless
    MeasureUnit unit = MeasureUnit::Inches;   // what a reading is given in
    int precision = 2;                        // decimals

    bool isValid() const noexcept;
    // `points` of page measured at this scale, in `unit`.
    double toUnits(double points) const noexcept;
    bool operator==(const MeasureScale&) const = default;
};

// "1 in = 4 ft" -- the scale in words, as `/R` writes it: one paper unit, and what it stands for in `scale.unit` to
// at most four decimals with trailing zeros dropped.
QString scaleString(const MeasureScale& scale);

// The `/Measure` a new measurement at `scale` carries: `/R` the scale string; `/X` converting points into
// `scale.unit` at the ratio; `/D` and `/A` converting from it unchanged -- each to `10^precision`.
MeasureDictionary measureDictionaryFor(const MeasureScale& scale);

// THE RATIO THAT CALIBRATES: a length measured on the page as `measuredPoints` is known to be `known` `unit`s.
// `error.measure.calibration` for a length that is not positive and finite on either side, or a ratio outside the
// scale key's range.
Result<double> calibratedRatio(double measuredPoints, double known, MeasureUnit unit);

// Whether a measure dictionary from a hostile journal line is small and finite enough to hold: chains of at most
// sixteen formats, a unit of at most 64 characters and separators of at most 8, a scale string of at most 256, every
// factor finite. Exactly what `pdf/ops/AnnotationReader` keeps of a file's, so a measurement read can always be moved
// and edited (S6 guide I-18's lesson); whether it can be DISPLAYED is `measured`'s question.
bool isBoundedMeasure(const MeasureDictionary& measure) noexcept;

// ---- The arithmetic -------------------------------------------------------------------------------

// The length of the path through `xy` (x y pairs), closed back to its start when `closed`.
double polylineLength(const std::vector<double>& xy, bool closed) noexcept;
// Whether the closed polygon `xy` crosses or touches itself anywhere but at the corners its edges share.
bool selfIntersects(const std::vector<double>& xy) noexcept;
// The area the closed polygon `xy` encloses, in square page units. `error.measure.tooFewPoints` for fewer than three
// corners, `error.measure.geometry` for a non-finite one, `error.measure.selfIntersecting` for a figure that
// crosses itself (see the header).
Result<double> polygonArea(const std::vector<double>& xy);

// ---- Measurements as annotations --------------------------------------------------------------------

enum class MeasureKind : std::uint8_t { Distance, Perimeter, Area };

// The kind an annotation's `/IT` makes it -- `LineDimension` on a Line, `PolyLineDimension` on a PolyLine,
// `PolygonDimension` on a Polygon -- or none.
std::optional<MeasureKind> measureKindOf(const Annotation& a) noexcept;
QLatin1String measureIntent(MeasureKind kind) noexcept;
AnnotationSubtype measureSubtype(MeasureKind kind) noexcept;

// A reading: its number, the unit it is labelled with ("ft", "sq ft"), and the text shown -- "12.00 ft".
struct MeasureReading {
    double value = 0.0;
    QString unit;
    QString text;
};

// The number formatted as a number format displays it: `decimals` places, `thousands` between orders of thousands,
// `decimal` before the decimals -- "1,234.50".
QString formatMeasureNumber(double value, int decimals, const QString& thousands = QStringLiteral(","),
                            const QString& decimal = QStringLiteral("."));
// The decimals a decimal display's `/D` asks for: 100 is 2. Nullopt for a denominator that is not a power of ten.
std::optional<int> decimalsOfDenominator(int denominator) noexcept;
// What a reading of `kind` is labelled: the unit's symbol, and "sq " before it for an area.
QString measureUnitLabel(MeasureKind kind, MeasureUnit unit);

// WHAT `a` MEASURES, from its geometry and its own `/Measure` (see the header). `error.measure.notMeasurement` for an
// annotation that is not one; `error.measure.unsupportedScale` for a measure this build does not display -- not
// `RL`, a chain of more than one unit, a `/Y`, a format that is not plain; the polygon's errors for an area.
Result<MeasureReading> measured(const Annotation& a);
// What a measurement through `points` (x y pairs: two for a distance, the corners of a perimeter or an area) WOULD
// read at `scale` -- a tool's running reading, before anything is made.
Result<MeasureReading> measureAt(MeasureKind kind, const std::vector<double>& points, const MeasureScale& scale);

// A NEW MEASUREMENT: `base` -- `core::newAnnotation`'s identity, author and instant, and a style -- given `kind`'s
// subtype, `/IT`, geometry through `points`, `/Measure` at `scale` and, for a distance, closed arrows at both ends,
// a caption on top of the line and `leaderLength` as its `/LL` (a dimension callout's offset, its leaders running
// `kDimensionLeaderExtension` past the line; 0 for none). Its
// reading goes in `/Contents` when `writeToComment` (D6-14). Its rectangle is the caller's to fit
// (`core::fittedShape`). Refused as `measured` refuses.
Result<Annotation> newMeasurement(Annotation base, MeasureKind kind, const std::vector<double>& points,
                                  const MeasureScale& scale, double leaderLength, bool writeToComment);

// A measurement's `/Contents` after its geometry changed from `before`'s: the new reading where it held the old
// one, so a measurement moved or resized does not say what it used to measure; anything a person wrote stays.
Annotation withFreshReading(const Annotation& before, Annotation after);

// ---- Where a measurement's parts are drawn ----------------------------------------------------------

// The size its caption is set in, in points.
inline constexpr double kMeasureCaptionSize = 9.0;
// The gap between a line and a caption drawn on top of it, past half its stroke.
inline constexpr double kMeasureCaptionGap = 2.0;
// How far a dimension callout's leader lines run past its line, in points (`/LLE`).
inline constexpr double kDimensionLeaderExtension = 3.0;

// A caption: Helvetica text set from `origin` along `direction` (a unit vector, never pointing left of straight down
// -- so it reads upright), its box in page space.
struct MeasureCaption {
    QString text;
    double size = kMeasureCaptionSize;
    double width = 0.0;                  // in points
    PdfPoint origin;                     // the baseline's start
    PdfPoint direction{1.0, 0.0};        // along the baseline
    std::array<PdfPoint, 4> corners{};   // the box: baseline start and end, then the top's end and start
};

// THE DRAWN PARTS OF A LINE WITH LEADERS OR A CAPTION (ISO 32000-1 Table 175): the dimension line itself -- `/L`
// moved `/LL` along its clockwise normal -- the two leaders from `/LLO` short of `/L`'s points to `/LLE` past it, and
// the caption: the reading for a measurement, else `/Contents`' first line, on top of the line (`/CP /Top`) or
// centred in a gap of it (`Inline`), moved by `/CO`. Nullopt for a Line without two finite points.
struct DimensionGeometry {
    PdfPoint start;                          // the dimension line as drawn
    PdfPoint end;
    std::vector<double> leaders;             // x1 y1 x2 y2 per leader, empty when there are none
    std::optional<MeasureCaption> caption;
    // Inline: the line is drawn in two parts with the caption between -- these are each part's inner end.
    std::optional<std::pair<PdfPoint, PdfPoint>> gap;
};
std::optional<DimensionGeometry> dimensionGeometry(const Annotation& a);

// The reading a measured POLYLINE or POLYGON draws -- on top of a polyline's longest segment, centred on a polygon's
// centroid -- or none for any other annotation, or one whose reading this build cannot state.
std::optional<MeasureCaption> measureLabel(const Annotation& a);

// The caption `text` set on top of the segment `a`-`b` at its middle, `offset` from it -- what both of the above use.
MeasureCaption captionAbove(PdfPoint a, PdfPoint b, const QString& text, double offset);

// The width of `text` set in Helvetica at `size` points (its AFM advances; a character outside WinAnsi counts
// nothing -- the builder refuses it).
double helveticaTextWidth(const QString& text, double size) noexcept;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_MEASURE_H
