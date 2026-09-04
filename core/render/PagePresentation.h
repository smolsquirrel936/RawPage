// core/render/PagePresentation -- where each page sits. Stage 2 T10, plan 5.5.
//
// Four layout modes, a cover page, right-to-left binding, and the mapping between a point on the
// screen and a point in a document. Pure arithmetic over `core/doc/PageTree`, so all of it is
// tested with no display (guide cases A-32, A-33, A-34).
//
// -------------------------------------------------------------------------------------------
// THE TWO ASYMMETRIES THE PLAN NAMES, AND THE ONE RULE THAT COVERS BOTH
// -------------------------------------------------------------------------------------------
// Plan 5.5: "a cover page shifts every subsequent pair's parity, and RTL binding swaps which side
// of the spread page n lands on without changing the page order". Both are handled by one rule
// rather than two special cases, because two special cases is where the off-by-one lives:
//
//   1. Pages are grouped into SPREADS. Without a cover: (1,2) (3,4) (5,6). With one: (1) (2,3)
//      (4,5) -- the cover takes a spread of its own and every pair after it shifts by one.
//   2. A page's COLUMN is its position within its spread, and a missing partner leaves its column
//      EMPTY rather than sliding its neighbour across. So the cover sits in the right-hand column
//      with the left one blank -- it is a recto, which is the entire reason the option exists --
//      and a lone final page sits in the left-hand column with the right one blank.
//   3. RTL binding maps column 0 to the right instead of the left. It changes NOTHING else: not
//      the spread grouping, not the page order, not which page is the cover.
//
// A document with an odd page count, a cover page and RTL binding exercises all three at once,
// which is what `corpus/ordinary/odd-cover-rtl.pdf` exists for.
//
// -------------------------------------------------------------------------------------------
// COLUMNS ARE UNIFORM, AND THAT IS WHY A MIXED-SIZE DOCUMENT SCROLLS WITHOUT SHIFTING
// -------------------------------------------------------------------------------------------
// Every column is as wide as the widest page in the document (`PageTree::largestDisplaySize()`,
// which exists for this) and each page is centred inside its column. Sizing each spread to its own
// contents instead would make the content width change as you scroll, so the horizontal scroll
// position -- and the page under the cursor -- would move on their own in a document whose page
// sizes vary. `corpus/ordinary/mixed-page-sizes.pdf` is the fixture.
//
// -------------------------------------------------------------------------------------------
// WHY THE SIGNATURE TAKES A `Resolution` AND A CURRENT PAGE
// -------------------------------------------------------------------------------------------
// `Implementation-Plan.md`'s component list writes `QVector<QRectF> layout(const PageTree&,
// LayoutMode, Zoom) const`. Three deviations, each recorded in the Stage 2 status block:
//
//   * **`Resolution`, not `Zoom`.** A zoom alone does not determine a pixel: the monitor's physical
//     DPI does too, and `DpiPolicy` is the file that combines them. Passing a zoom here would make
//     this the second place that arithmetic happens. It uses `presentedScale()`, so a resolution
//     capped by `maxRenderResolution` still lays the page out at the size the user asked for.
//   * **A current page.** Single and Two-Up are not continuous -- they show ONE page or ONE spread
//     -- so without it those two modes are indistinguishable from their continuous twins, and the
//     four modes would be two.
//   * **`DeviceRect`, not `QRectF`.** `core/Geometry.h` exists precisely so that a rectangle says
//     which coordinate space it is in; a `QRectF` here would be the PDF-point/device-pixel mix-up
//     that file was written to prevent.

#ifndef RAWPAGE_CORE_RENDER_PAGEPRESENTATION_H
#define RAWPAGE_CORE_RENDER_PAGEPRESENTATION_H

#include "core/Geometry.h"
#include "core/render/DpiPolicy.h"

#include <optional>
#include <vector>

namespace rawpage::core {
class PageTree;
}

namespace rawpage::core::render {

enum class LayoutMode {
    Single,             // one page, no scrolling past it
    SingleContinuous,   // every page stacked vertically
    TwoUp,              // one spread
    TwoUpContinuous,    // every spread stacked vertically
};

const char* layoutModeName(LayoutMode mode) noexcept;

// True for the two modes that lay out the whole document. Named rather than compared inline,
// because "is this mode continuous" is asked in four places and a missing `||` in one of them is a
// document that scrolls in three modes and not the fourth.
constexpr bool isContinuous(LayoutMode mode) noexcept
{
    return mode == LayoutMode::SingleContinuous || mode == LayoutMode::TwoUpContinuous;
}

constexpr bool isTwoUp(LayoutMode mode) noexcept
{
    return mode == LayoutMode::TwoUp || mode == LayoutMode::TwoUpContinuous;
}

struct PresentationOptions {
    // `pageDisplay/showCoverInTwoUp` and `pageDisplay/rtlBinding` (Stage 2 T15). Held here as
    // plain fields because this class is pure and `core/render/` does not read `Settings` -- the
    // composition root does, and hands the values down.
    bool showCoverInTwoUp = false;
    bool rtlBinding = false;

    // All three in DEVICE pixels, and therefore already scaled by the caller if they are meant to
    // grow with zoom. They do not grow here: a 12-pixel gutter is a piece of window furniture, and
    // a gutter that scaled with the zoom would be a hand's width apart at 400 %.
    double pageGap = 12.0;     // between consecutive pages or spreads
    double spreadGap = 4.0;    // between the two pages of one spread
    double margin = 12.0;      // around the whole content
};

// One page's place. It carries the page's own geometry as well as its rectangle so that the
// mapping below needs nothing but the layout -- a mapping that had to be handed the `PageTree`
// again would be a second chance for the caller to pass a different one.
struct PageSlot {
    int pageIndex = 0;
    DeviceRect rect;                            // in content space, presented pixels
    PdfRect cropBox;                            // what is drawn (Page::cropBox), for the mapping
    PageRotation rotation = PageRotation::None;
};

class PageLayout {
public:
    PageLayout() = default;

    // `pageSlots()`, NOT `slots()`. Qt defines `slots` as an empty macro, so a method by that name
    // becomes `()` and the diagnostic is `expected unqualified-id before ')' token` on the line
    // that declares it -- the same class of trap as the `emit` collision T7 recorded. `core/`
    // links QtCore, so this applies to every file here.
    const std::vector<PageSlot>& pageSlots() const noexcept { return slots_; }
    bool isEmpty() const noexcept { return slots_.empty(); }

    // The scrollable extent, including the margins.
    DeviceSize contentSize() const noexcept { return contentSize_; }

    // Device pixels per PDF point as PRESENTED -- `Resolution::presentedScale()`. Carried so the
    // mapping does not have to be told again.
    double presentedScale() const noexcept { return presentedScale_; }

    LayoutMode mode() const noexcept { return mode_; }

    // Null when the page is not laid out -- which is the ordinary answer in `Single` and `TwoUp`,
    // where only the current page or spread has a place at all.
    const PageSlot* slotForPage(int pageIndex) const noexcept;

    // The page under a point in content space, or -1 for the gaps and the margins. A point in the
    // gutter belongs to no page, and answering with the nearest one would make a click in the
    // margin select text on a page the cursor was not over.
    int pageAt(DevicePoint point) const noexcept;

    // The slots a viewport rectangle touches, in document order. What `ui/viewer/PageLayoutEngine`
    // (T13) walks before asking `TileGrid` for each page's tiles.
    std::vector<PageSlot> slotsIntersecting(const DeviceRect& viewport) const;

    // ---- The mapping, both ways (guide case A-32's round trip) --------------------------------
    // Content point -> the page it is on and the PDF point under it. `std::nullopt` in a gap.
    // Rotation and a non-zero CropBox origin are both handled here, which is the whole reason this
    // is not two multiplications at the call site.
    struct DocumentPoint {
        int pageIndex = 0;
        PdfPoint point;
    };
    std::optional<DocumentPoint> toDocument(DevicePoint point) const;

    // And back. `std::nullopt` when that page is not in this layout.
    std::optional<DevicePoint> toContent(int pageIndex, PdfPoint point) const;

private:
    friend class PagePresentation;

    std::vector<PageSlot> slots_;
    DeviceSize contentSize_;
    double presentedScale_ = 1.0;
    LayoutMode mode_ = LayoutMode::SingleContinuous;
};

class PagePresentation {
public:
    PagePresentation() = default;
    explicit PagePresentation(PresentationOptions options) noexcept : options_(options) {}

    const PresentationOptions& options() const noexcept { return options_; }
    void setOptions(PresentationOptions options) noexcept { options_ = options; }

    // `viewRotation` is the SESSION's extra quarter-turn (`Ctrl+Shift+=`, Stage 2 T14), not the
    // page's own `/Rotate` -- the two are different things owned by different layers, and the
    // document never learns about this one. It rotates the whole layout: every page swaps its
    // width and height at 90 and 270, so the content extent, the column width and the scrollbars
    // all follow, and each `PageSlot::rotation` comes out as the COMBINED rotation so the
    // point mapping below needs no second argument.
    //
    // Defaulted, and safe to default because the default is the identity: every caller written
    // before T14 means none, and means it correctly.
    PageLayout layout(const PageTree& tree, LayoutMode mode, const Resolution& resolution,
                      int currentPage = 0,
                      PageRotation viewRotation = PageRotation::None) const;

    // The spread a page belongs to, as the page indices in COLUMN order (left column first in LTR)
    // with -1 for an empty column. Exposed because `ui/viewer/ViewportController` (T13) pages by
    // spread rather than by page in the two-up modes, and because it is the half of the layout
    // that carries both asymmetries -- so it is asserted directly rather than inferred from
    // rectangles.
    std::vector<int> spreadFor(int pageIndex, int pageCount, LayoutMode mode) const;

    // How many spreads a document of `pageCount` pages has in `mode`. 0 for an empty document.
    int spreadCount(int pageCount, LayoutMode mode) const noexcept;

private:
    PresentationOptions options_{};
};

} // namespace rawpage::core::render

#endif // RAWPAGE_CORE_RENDER_PAGEPRESENTATION_H
