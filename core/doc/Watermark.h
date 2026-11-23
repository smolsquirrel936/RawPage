// core/doc/Watermark -- what a watermark IS and WHERE it goes on each page, before anything draws it. Stage 7 T15,
// decisions D7-7, D7-9 and D7-45 (`Stage7-Assembly-Plan.md` §3).
//
// ===========================================================================================
// THE SPEC
// ===========================================================================================
// A `WatermarkSpec` is a SOURCE -- text typed here, an image, or a page of a PDF file -- and how it is put on the page:
// its opacity, its rotation, its scale (ABSOLUTE, a factor on its natural size, or RELATIVE, the largest that keeps the
// turned watermark inside that fraction of the page as shown), an ANCHOR among nine and an OFFSET from it, or TILED
// across the page with a gap, BEHIND the page's content or in front of it, and WHEN it shows: always, only printed,
// or only on screen (D7-45: the watermark's own optional-content group). Pages are a range and a parity, as for a header.
//
// THE NATURAL SIZE of each source, which scale 1 draws (D7-45):
//   text   its lines in D6-7's faces at `size` points -- the widest line by the lines' leading (`core/doc/TypedText`);
//   image  its pixels at 96 to the inch (0.75 points each), as a screen shows them;
//   page   the page as it SHOWS -- its CropBox within its MediaBox turned by `/Rotate` (`pdf/ops/PageStamp`).
// An UPDATE that keeps a picture names the watermark whose source it keeps (`keepSourceOf`) and carries that source's
// natural size (`sourceSize`), read from what that watermark recorded (`DecorationEntry::spec`): the bytes are in the
// document, drawn by reference, and never travel again.
//
// ===========================================================================================
// WHERE, ON A PAGE AS IT IS SHOWN
// ===========================================================================================
// Everything is measured on the page as a reader SHOWS it -- its CropBox turned by `/Rotate` -- as a header's place is
// (`core::shownMatrix`). The source's natural box is scaled, turned counter-clockwise by `rotation` about its centre,
// and its turned bounds placed so their anchor point meets the page's -- the top-left corner at the top-left corner,
// the centre at the centre -- moved by the offset (x right, y up). TILED, that box is repeated across and down from
// there, `tileGap` points between copies, every copy that touches the page drawn and clipped to it; past
// `kMaxWatermarkTiles` it is refused by key rather than drawn thinly (`error.decorate.tooManyTiles`). Each copy is one
// matrix from the natural box into the page's own unturned space (`WatermarkDrawing::tiles`): all `pdf/ops` needs.
//
// Text is D6-7's: Helvetica for WinAnsi, the four CJK collections, anything else refused by name
// (`error.decorate.characterRefused`, Q5(e)) -- the same rule and key as a header's.
//
// Pure: QtCore and std only -- `tst_watermark` in `rawpage_tests`; against the engine, `tst_watermarkengine`.

#ifndef RAWPAGE_CORE_DOC_WATERMARK_H
#define RAWPAGE_CORE_DOC_WATERMARK_H

#include "core/Geometry.h"
#include "core/Result.h"
#include "core/doc/Annotation.h"
#include "core/doc/HeaderFooter.h"
#include "core/doc/PageRanges.h"
#include "core/doc/TypedText.h"

#include <QJsonObject>
#include <QString>
#include <QStringView>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace rawpage::core {

class PageTree;

enum class WatermarkScale : std::uint8_t { Absolute, Relative };
// The nine points of the page as shown a watermark can be anchored at, row by row from the top.
enum class WatermarkAnchor : std::uint8_t { TopLeft, Top, TopRight, Left, Centre, Right, BottomLeft, Bottom, BottomRight };
inline constexpr int kWatermarkAnchors = 9;

struct WatermarkSpec {
    WatermarkSource source = WatermarkSource::Text;
    // Text
    QString text;
    double size = 72.0;                               // points
    std::array<double, 3> colour{0.5, 0.5, 0.5};      // DeviceRGB, each 0 ... 1
    TypedFont han = TypedFont::Simplified;            // the CJK forms an ideograph is drawn in (the host's language)
    // Image, Page
    StampImage image;
    StampPage page;
    QString keepSourceOf;                             // an update keeping that watermark's picture: its id
    PdfSize sourceSize;                               // with `keepSourceOf`: that picture's natural size
    QString sourceName;                               // the file it came from, as the list shows it; may be empty
    // Placement
    double opacity = 0.5;                             // 0.01 ... 1
    double rotation = 45.0;                           // degrees, counter-clockwise on the page as shown
    WatermarkScale scaleMode = WatermarkScale::Relative;
    double scale = 0.5;                               // Absolute: a factor; Relative: a fraction of the page
    WatermarkAnchor anchor = WatermarkAnchor::Centre;
    double offsetX = 0.0;                             // points, right
    double offsetY = 0.0;                             // points, up
    bool tile = false;
    double tileGap = 36.0;                            // points between tiled copies, across and down
    bool behind = true;
    WatermarkUsage usage = WatermarkUsage::Always;
    QString range;                                    // `parsePageRanges`; empty: every page
    PageParity parity = PageParity::Any;
    friend bool operator==(const WatermarkSpec&, const WatermarkSpec&) = default;
};

inline constexpr double kWatermarkPixelPoints = 0.75;   // an image's pixel: 96 to the inch
inline constexpr double kMinWatermarkTextSize = 4.0;
inline constexpr double kMaxWatermarkTextSize = 720.0;
inline constexpr int kMaxWatermarkTextLength = 512;
inline constexpr double kMinWatermarkOpacity = 0.01;
inline constexpr double kMaxWatermarkRotation = 360.0;
inline constexpr double kMinWatermarkScale = 0.01;
inline constexpr double kMaxWatermarkAbsoluteScale = 20.0;
inline constexpr double kMaxWatermarkRelativeScale = 1.0;
inline constexpr double kMaxWatermarkOffset = 14400.0;   // ISO 32000-1 Annex C's largest page
inline constexpr double kMaxWatermarkTileGap = 1440.0;
inline constexpr int kMaxWatermarkTiles = 400;
inline constexpr int kMaxSourceNameLength = 1024;

QLatin1String watermarkSourceName(WatermarkSource source) noexcept;
QLatin1String watermarkUsageName(WatermarkUsage usage) noexcept;
QLatin1String watermarkAnchorName(WatermarkAnchor anchor) noexcept;

// The spec's own bounds -- `error.decorate.badSpec` naming the field -- and its source: text with something in it
// (`error.decorate.empty`) and every character one D6-7's faces can show (`error.decorate.characterRefused`, naming
// it); an image or a page `core/doc/Stamps` would refuse is `error.decorate.badSpec` naming `image` or `page`.
Status validate(const WatermarkSpec& spec);

// The pages `spec` decorates: its range, then its parity. `error.decorate.noPages` when that leaves none.
Result<std::vector<int>> watermarkPages(const WatermarkSpec& spec, const PageTree& pages);

// The source's natural box, in points (see the header).
Result<PdfSize> watermarkNaturalSize(const WatermarkSpec& spec);

// THE SPEC AS JSON. `withSource`: with the image's or page's bytes -- a journal line and a profile. Without them, the
// natural size stands for them (`sourceSize`): what the mark records (`WatermarkDrawing::spec`), so a list can show it
// and an update keep it. `watermarkFromJson` checks the bounds with `validate`; `sourceRequired` false accepts a
// picture with no bytes (a mark's record), which it answers as an update keeping it would need it.
QJsonObject watermarkToJson(const WatermarkSpec& spec, bool withSource);
Result<WatermarkSpec> watermarkFromJson(const QJsonObject& json, bool sourceRequired);

// What `spec` draws on `page`. Refused by key, with the field: a source with no extent, too many tiles.
Result<PageDecoration> layoutWatermark(const WatermarkSpec& spec, const Page& page);

// The matrix of a natural box `natural` scaled by `scale`, turned by `degrees`, its turned bounds' corner at
// (`left`, `bottom`) on the page as shown -- and those bounds' size. Exposed for the preview and the cases.
std::array<double, 6> watermarkShownMatrix(PdfSize natural, double scale, double degrees, double left, double bottom) noexcept;
PdfSize watermarkTurnedSize(PdfSize natural, double scale, double degrees) noexcept;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_WATERMARK_H
