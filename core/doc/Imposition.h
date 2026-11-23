// core/doc/Imposition -- which page goes where on which side of which sheet, before anything draws it. Stage 7 T16,
// decisions D7-8 and D7-46 (`Stage7-Assembly-Plan.md` §3); guide cases A-102 ... A-104 (`Stage7-Assembly-Testing-Guide.md`
// §5.18).
//
// ===========================================================================================
// WHY THIS IS ARITHMETIC IN `core/` (D7-8)
// ===========================================================================================
// The order of pages in a booklet is the part that is wrong on paper and right on screen: a sheet whose back carries
// the wrong pair, or a back turned the wrong way for the printer's flip, looks like a fine PDF. So the order is a pure
// function of the spec and the page count, and `tst_imposition` holds it against tables written BY HAND for every
// n-up and every booklet at 1 ... 17 pages. `pdf/ops/ImpositionOps` draws exactly what the plan says and decides
// nothing.
//
// ===========================================================================================
// THE TWO LAYOUTS
// ===========================================================================================
// N-UP puts `perSheet` pages (2, 4, 6, 8, 9 or 16) on each side in a grid -- 2 is 2 x 1, 6 is 3 x 2, 8 is 4 x 2 on a
// LANDSCAPE sheet (columns x rows) and the transpose on a portrait one; 4, 9 and 16 are square. The pages fill the
// grid ACROSS (each row left to right, rows top to bottom) or DOWN (each column top to bottom, columns left to right),
// and REVERSED mirrors the columns -- right to left, for a document read that way. A last side not filled is left
// empty where its pages run out.
//
// A BOOKLET (saddle stitch) is two pages side by side on each side of a folded sheet. The page count is PADDED with
// blank pages AT THE END to a multiple of four, P; sheet s (0 = the outermost) carries, by page number from 1,
//     left binding:   front  P-2s | 2s+1       back  2s+2 | P-2s-1
//     right binding:  front  2s+1 | P-2s       back  P-2s-1 | 2s+2
// so the sheets stacked and folded read 1 ... P with the fold on the side chosen. Each page sits against the FOLD --
// its edge on the spine side of its cell -- and CREEP moves sheet s's pages `s * creep` further toward it: the inner
// sheets stand out at the fore edge when the booklet is folded, and are trimmed most. What creep pushes past a cell's
// edge is clipped to the cell.
//
// ===========================================================================================
// THE SHEET, AND ITS TWO SIDES
// ===========================================================================================
// The sheet is `sheet` turned to the orientation chosen; AUTO picks the one in which the first page fits its cell at
// the larger scale (portrait on a tie). `margin` is kept clear on every edge -- the marks go there -- and `gutter`
// separates the cells. Each page is scaled, keeping its shape, to the largest that fits its cell -- up or down -- and
// centred in it (a booklet's against the fold, above). A page is placed as it SHOWS: its CropBox turned by `/Rotate`.
// A blank page is the first page's size, so its marks fall where a page's would.
//
// DUPLEX. With `duplex` other than `OneSided`, the sides alternate front and back of one sheet (a booklet's always do;
// one-sided, a booklet's sides are still its sheets' fronts and backs -- fed through twice). A back side is printed so
// that a sheet turned over about its VERTICAL axis -- as a book's page is turned -- shows it upright; a printer that
// flips on the sheet's HORIZONTAL edges turns it upside down, so that back is drawn HALF-TURNED to undo it:
//     turned  =  back  and  ((landscape and LongEdge) or (portrait and ShortEdge))
// -- a booklet on a landscape sheet printed with *flip on short edge* is not turned, which is what Acrobat asks for.
// Each `ImposedCell` is in the side's own space; `onSheet` maps it through the half turn to where it lands.
//
// ===========================================================================================
// THE MARKS (all optional, all in the margin, which must then be `kMinMarkMargin` deep)
// ===========================================================================================
//   crop marks         ticks of `kCropMarkLength` points, `kCropMarkOffset` outside the box round every placed page,
//                      at every distinct x and y a page edge takes -- where the stack would be trimmed;
//   registration marks a circle and a cross at the middle of each of the four margins;
//   a colour bar       `kColourPatches` patches in the top margin, right-aligned;
//   a sheet label      along the sheet's bottom edge from the margin's left: which sheet of how many, front or back --
//                      its words the caller's (`pdf/ops` is handed the translated templates; `core/` holds no English).
//                      The crop ticks below the pages stop short of its band (`kSheetLabelBand`), so none crosses it;
//   frames             a hairline round each placed page, never round a blank.
//
// Pure: QtCore and std only -- `tst_imposition` in `rawpage_tests`; against the engine, `tst_impositionengine`.

#ifndef RAWPAGE_CORE_DOC_IMPOSITION_H
#define RAWPAGE_CORE_DOC_IMPOSITION_H

#include "core/Geometry.h"
#include "core/Result.h"

#include <QJsonObject>
#include <QString>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace rawpage::core {

enum class ImpositionLayout : std::uint8_t { NUp, Booklet };
enum class ImpositionOrder : std::uint8_t { Across, Down };
enum class ImpositionBinding : std::uint8_t { Left, Right };
enum class SheetOrientation : std::uint8_t { Auto, Portrait, Landscape };
enum class DuplexFlip : std::uint8_t { OneSided, LongEdge, ShortEdge };

inline constexpr std::array<int, 6> kNUpCounts{2, 4, 6, 8, 9, 16};
inline constexpr double kMinSheetSide = 72.0;        // one inch
inline constexpr double kMaxSheetSide = 14400.0;     // ISO 32000-1 Annex C
inline constexpr double kMaxImpositionMargin = 144.0;
inline constexpr double kMaxImpositionGutter = 144.0;
inline constexpr double kMaxCreep = 12.0;            // points per sheet
inline constexpr double kMinCell = 36.0;             // a cell smaller than half an inch either way is refused
inline constexpr double kMinMarkMargin = 18.0;       // what a mark needs; asked for with less, refused
inline constexpr double kCropMarkOffset = 3.0;
inline constexpr double kCropMarkLength = 12.0;
inline constexpr double kRegistrationRadius = 5.0;
inline constexpr int kColourPatches = 10;            // C, M, Y, K, C+M, C+Y, M+Y, 75 %, 50 %, 25 % K
inline constexpr double kColourPatch = 9.0;
inline constexpr double kSheetLabelSize = 7.0;
inline constexpr double kSheetLabelBaseline = 2.0;   // points above the sheet's bottom edge
// The band the label is drawn in, which a crop tick below the pages stops short of.
inline constexpr double kSheetLabelBand = kSheetLabelBaseline + kSheetLabelSize + 1.5;
inline constexpr int kMaxImposedPages = 100000;

struct ImpositionSpec {
    ImpositionLayout layout = ImpositionLayout::NUp;
    int perSheet = 2;                                   // n-up only: one of `kNUpCounts`
    ImpositionOrder order = ImpositionOrder::Across;    // n-up only
    bool reversed = false;                              // n-up only
    ImpositionBinding binding = ImpositionBinding::Left;   // booklet only
    PdfSize sheet{612.0, 792.0};                        // the paper, either way round -- `orientation` turns it
    SheetOrientation orientation = SheetOrientation::Auto;
    double margin = 36.0;
    double gutter = 0.0;
    double creep = 0.0;                                 // booklet only, points per sheet
    DuplexFlip duplex = DuplexFlip::OneSided;
    bool cropMarks = false;
    bool registrationMarks = false;
    bool colourBar = false;
    bool sheetLabels = false;
    bool frames = false;

    bool anyMarks() const noexcept { return cropMarks || registrationMarks || colourBar || sheetLabels; }
    bool operator==(const ImpositionSpec&) const = default;
};

// The grid on one side: columns by rows, and whether the sheet is landscape.
struct ImpositionGrid {
    int columns = 1;
    int rows = 1;
    bool landscape = false;
    int cells() const noexcept { return columns * rows; }
    bool operator==(const ImpositionGrid&) const = default;
};

// One side's ORDER: the source page in each cell, row-major from the top-left in the side's own space (-1 a blank).
struct SideOrder {
    int sheet = 0;        // the physical sheet, from 0
    bool back = false;    // the back of a duplex sheet
    bool turned = false;  // drawn half-turned (the duplex rule in the header)
    std::vector<int> cells;
    bool operator==(const SideOrder&) const = default;
};

struct ImposedCell {
    int page = -1;        // the source page, -1 a blank
    PdfRect cell;         // the grid cell, in the side's own space
    PdfRect placed;       // where the page is drawn -- fitted, creep applied; clipped to `cell`
};

struct ImposedLine {
    PdfPoint from;
    PdfPoint to;
};

struct ImposedSide {
    int sheet = 0;
    bool back = false;
    bool turned = false;
    std::vector<ImposedCell> cells;            // as `SideOrder::cells`
    std::vector<ImposedLine> cropMarks;        // in the side's own space
};

struct ImpositionPlan {
    PdfSize sheet;                             // as oriented
    ImpositionGrid grid;
    int sheets = 0;                            // physical sheets
    int pagesPadded = 0;                       // a booklet's P; an n-up's page count
    std::vector<ImposedSide> sides;            // the new document's pages, in order
    std::vector<PdfPoint> registration;        // centres, in every side's own space (empty: none)
    PdfRect colourBar;                         // the bar's box -- `kColourPatches` patches -- when asked for
    PdfPoint labelOrigin;                      // the label's baseline start, when asked for
};

// The spec's own bounds -- `error.imposition.badSpec` naming the field; marks with too little margin
// `error.imposition.marginTooSmall`.
Status validate(const ImpositionSpec& spec);

// The grid on a sheet of either orientation.
ImpositionGrid impositionGrid(const ImpositionSpec& spec, bool landscape) noexcept;
// Which orientation the plan takes, for a first page that shows `firstPage`.
bool impositionLandscape(const ImpositionSpec& spec, PdfSize firstPage) noexcept;

// THE ORDER ALONE -- what `tst_imposition`'s hand tables hold: every side for `pageCount` pages on `grid`.
std::vector<SideOrder> impositionOrder(const ImpositionSpec& spec, int pageCount, const ImpositionGrid& grid);

// THE WHOLE PLAN for pages that show `shown` (one size each, in order). Refused by `validate`'s keys, and by
// `error.imposition.noPages`, `error.imposition.tooManyPages`, `error.imposition.sheetTooSmall` (a cell under
// `kMinCell`) and `error.imposition.creepTooLarge` (an inner sheet's creep past half its cell).
Result<ImpositionPlan> planImposition(const ImpositionSpec& spec, const std::vector<PdfSize>& shown);

// Where `rect`, in `side`'s own space, lands on the sheet -- through the half turn when the side is turned.
PdfRect onSheet(const PdfRect& rect, const ImposedSide& side, PdfSize sheet) noexcept;

// The spec as a profile stores it, and back -- checked by `validate`, `error.imposition.badSpec` naming the field.
QJsonObject impositionToJson(const ImpositionSpec& spec);
Result<ImpositionSpec> impositionFromJson(const QJsonObject& json);

std::string_view impositionLayoutName(ImpositionLayout layout) noexcept;
std::string_view duplexFlipName(DuplexFlip flip) noexcept;

// THE WORDS ON A SHEET LABEL, translated by the caller: `%1` the sheet's number, `%2` how many there are. `single`
// for a one-sided n-up's sheet; `front` and `back` for a side of a duplex sheet or a booklet's.
struct ImpositionLabels {
    QString single;
    QString front;
    QString back;
};
QString sheetLabel(const ImpositionLabels& labels, const ImpositionSpec& spec, const ImposedSide& side, int sheets);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_IMPOSITION_H
