// core/doc/HeaderFooter -- what a header, a footer or a Bates number SAYS and WHERE it goes on each page, before
// anything draws it. Stage 7 T14, decisions D7-7, D7-14 and D7-44 (`Stage7-Assembly-Plan.md` §3).
//
// ===========================================================================================
// ONE ENGINE FOR HEADERS, FOOTERS AND BATES NUMBERS (D7-14)
// ===========================================================================================
// A `HeaderFooterSpec` is six areas -- left, centre and right of the header and of the footer -- each a line of
// text holding FIELDS, one style (size, colour, underline), the distance from each page edge, whether the page's
// content is shrunk to clear them, and the pages they go on (a range, odd or even). A Bates number is the same
// spec with `<<bates>>` in one area and a numbering (`BatesFormat`): what makes it Bates is the command that
// writes it and the KIND it is marked with (D7-7), so *Remove Bates numbering* finds it and leaves a header alone.
//
// THE FIELDS, expanded per page against `FieldValues` (`expandFields`):
//   <<page>>    the page's number, 1 ... count             <<count>>  the document's page count
//   <<label>>   the page's label, else its number         <<file>>   the file's name   <<path>>  its full path
//   <<date>>    yyyy-MM-dd   <<date:FORMAT>>  Qt's format (C locale, so `MMM` is English whatever the machine)
//   <<time>>    HH:mm        <<time:FORMAT>>
//   <<bates>>   the Bates number: prefix, the number zero-padded to its digits, suffix
// Anything else between `<<` and `>>` is left as typed. The date and the time are FIXED when the command is made
// (`FieldValues` travels in its journal line), so an undo, a redo and a crash replay draw the same text.
//
// ===========================================================================================
// WHERE, ON A PAGE AS IT IS SHOWN
// ===========================================================================================
// Every distance is measured on the page as a reader SHOWS it -- its CropBox turned by `/Rotate` -- so a header is
// across the top of a landscape page made by turning a portrait one, reading upright. `layoutHeaderFooter` places
// each area's lines (a line break in an area is a new line, the header's first line `top` points below the top
// edge, the footer's last `bottom` points above the bottom one) and maps each line into the page's own unturned
// space with a matrix (`DecorationLine::matrix`), which is all `pdf/ops/DecorationOps` needs to draw it. Text is
// D6-7's: Helvetica for WinAnsi, the four CJK collections, anything else REFUSED BY NAME (`core/doc/TypedText`,
// Q5(e) ✅) -- before the command exists for what was typed (`firstRefusedCharacter`), and by the command for what a
// field expands to (a file name in Cyrillic).
//
// ===========================================================================================
// WHAT A DECORATION IS TO THE SINK (D7-7)
// ===========================================================================================
// A `PageDecoration`: the lines it draws in the page's own space, the box they are drawn in (the CropBox), whether
// it goes BEHIND the page's content or in front, and -- in front -- the WRAPPER the page's content is drawn through:
// a `q ... Q` pair so the content cannot leave the header a transformed or clipped state, scaled and moved down to
// clear the header and footer when `shrink` asks (guide A-95). The sink writes each as streams of their own, marked,
// so removing one is a list operation on `/Contents` -- byte-exact (§3.8) -- and no content stream is parsed.
//
// Pure: QtCore and std only -- `tst_headerfooter` in `rawpage_tests`; against the engine, `tst_decorationengine`.

#ifndef RAWPAGE_CORE_DOC_HEADERFOOTER_H
#define RAWPAGE_CORE_DOC_HEADERFOOTER_H

#include "core/Geometry.h"
#include "core/Result.h"
#include "core/doc/Page.h"
#include "core/doc/PageRanges.h"
#include "core/doc/TypedText.h"

#include <QDate>
#include <QJsonObject>
#include <QString>
#include <QStringView>
#include <QTime>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace rawpage::core {

class PageTree;

// ---- What a decoration is to the sink (D7-7) ---------------------------------------------------------------------

// The three kinds Stage 7 writes, each its own mark: removing one kind never touches another.
enum class DecorationKind : std::uint8_t { HeaderFooter, Bates, Watermark };
// `HeaderFooter`, `Bates`, `Watermark` -- the name the mark writes (`/RawPageDecoration /Kind`).
QLatin1String decorationKindName(DecorationKind kind) noexcept;
std::optional<DecorationKind> decorationKindByName(QStringView name) noexcept;

// One line of text, set at `matrix` (a b c d e f: text space -> the page's own space, its origin the baseline's left
// end), in runs of one face each at `size` points.
struct DecorationLine {
    std::array<double, 6> matrix{1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    double size = 10.0;
    std::array<double, 3> colour{0.0, 0.0, 0.0};   // DeviceRGB
    bool underline = false;
    std::vector<TypedRun> runs;
    double width = 0.0;                             // points, the runs' advances
    friend bool operator==(const DecorationLine& a, const DecorationLine& b);
};

struct PageDecoration {
    DecorationKind kind = DecorationKind::HeaderFooter;
    PdfRect box;                                    // the form's /BBox: what the page shows, in its own space
    std::vector<DecorationLine> lines;
    bool behind = false;                            // drawn before the page's content; else after it
    // The artifact's `/Subtype` (ISO 32000-1 §14.8.2.2.2): `Header` or `Footer` when it draws in one band only,
    // `Watermark`, or empty.
    QString subtype;
    // In front: the page's content drawn through this -- identity and clipped to `box`, or scaled to clear the bands.
    std::optional<PageContentWrap> wrap;
    friend bool operator==(const PageDecoration&, const PageDecoration&) = default;
};

// ---- What the person asks for ------------------------------------------------------------------------------------

enum class HeaderFooterArea : std::uint8_t { HeaderLeft, HeaderCentre, HeaderRight, FooterLeft, FooterCentre, FooterRight };
inline constexpr int kHeaderFooterAreas = 6;

struct BatesFormat {
    QString prefix;
    QString suffix;
    int digits = 6;
    long long start = 1;
    friend bool operator==(const BatesFormat&, const BatesFormat&) = default;
};
inline constexpr int kMinBatesDigits = 1;
inline constexpr int kMaxBatesDigits = 15;
inline constexpr long long kMaxBatesNumber = 999999999999999LL;   // fifteen nines
inline constexpr int kMaxBatesAffix = 64;                          // characters in a prefix or a suffix

struct HeaderFooterSpec {
    std::array<QString, kHeaderFooterAreas> areas;   // indexed by HeaderFooterArea
    double size = 10.0;                               // points
    std::array<double, 3> colour{0.0, 0.0, 0.0};      // DeviceRGB, each 0 ... 1
    bool underline = false;
    double top = 36.0;                                // points from each edge of the page as shown
    double bottom = 36.0;
    double left = 72.0;
    double right = 72.0;
    bool shrink = false;                              // scale the content to clear the header and the footer
    QString range;                                    // `parsePageRanges`; empty: every page
    PageParity parity = PageParity::Any;
    TypedFont han = TypedFont::Simplified;            // the CJK forms an ideograph is drawn in (the host's language)
    BatesFormat bates;                                // what `<<bates>>` expands to
    friend bool operator==(const HeaderFooterSpec&, const HeaderFooterSpec&) = default;
};

inline constexpr double kMinDecorationSize = 4.0;
inline constexpr double kMaxDecorationSize = 144.0;
inline constexpr double kMaxDecorationMargin = 720.0;
inline constexpr int kMaxAreaLength = 512;

// The spec's own bounds -- sizes, margins, colours, the Bates format, the areas' lengths -- and every character
// TYPED (areas, prefix, suffix) one D6-7's faces can show: `error.decorate.characterRefused` naming the first that
// is not (guide A-96). `error.decorate.badSpec` with the field for anything else, and `error.decorate.empty` for a
// spec with nothing in any area.
Status validate(const HeaderFooterSpec& spec);
// Whether any area holds `<<bates>>`.
bool usesBates(const HeaderFooterSpec& spec);

// The pages `spec` decorates in a document of `pages`: its range, then its parity -- odd and even counted as the
// person counts pages, from 1. `error.decorate.noPages` when that leaves none.
Result<std::vector<int>> headerFooterPages(const HeaderFooterSpec& spec, const PageTree& pages);

// The spec as a profile and a journal line hold it, and back -- every field, bounds checked by `validate` after.
QJsonObject specToJson(const HeaderFooterSpec& spec);
Result<HeaderFooterSpec> specFromJson(const QJsonObject& json);

// ---- The fields ---------------------------------------------------------------------------------------------------

// What does not change from page to page, fixed when the command is made.
struct FieldValues {
    QDate date;
    QTime time;
    QString fileName;
    QString path;
    friend bool operator==(const FieldValues&, const FieldValues&) = default;
};
QJsonObject fieldValuesToJson(const FieldValues& values);
Result<FieldValues> fieldValuesFromJson(const QJsonObject& json);

// What changes: the page, and its Bates number when the spec numbers.
struct FieldPage {
    int index = 0;                                    // 0-based
    int count = 0;
    QString label;
    std::optional<long long> bates;
};

// `text` with every field expanded. Unknown fields are left as typed.
QString expandFields(QStringView text, const FieldValues& values, const FieldPage& page, const BatesFormat& bates);
// `prefix`, `number` zero-padded to `digits`, `suffix`.
QString formatBates(const BatesFormat& bates, long long number);

// ---- Layout --------------------------------------------------------------------------------------------------------

// The point on page `page` (its own space) at (`x`, `y`) on the page AS SHOWN -- its CropBox turned by its rotation,
// measured from the shown bottom-left corner, `x` right and `y` up; and the matrix a line set there is drawn with.
PdfPoint shownToPage(const Page& page, double x, double y) noexcept;
std::array<double, 6> shownMatrix(const Page& page, double x, double y) noexcept;

// What `spec` draws on `page`, each area's fields expanded for it. `error.decorate.characterRefused` for a
// character no face can show (a file name in Cyrillic). `kind` is the mark it is written with.
Result<PageDecoration> layoutHeaderFooter(const HeaderFooterSpec& spec, DecorationKind kind, const Page& page,
                                          const FieldValues& values, const FieldPage& field);

// The gap between a band and the content shrunk away from it, in points.
inline constexpr double kShrinkGap = 6.0;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_HEADERFOOTER_H
