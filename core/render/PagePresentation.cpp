#include "core/render/PagePresentation.h"

#include "core/doc/Page.h"
#include "core/doc/PageTree.h"

#include <algorithm>
#include <cmath>

namespace rawpage::core::render {
namespace {

double sanitizedScale(double scale) noexcept
{
    return (std::isfinite(scale) && scale > 0.0) ? scale : 1.0;
}

double sanitizedGap(double gap) noexcept
{
    return (std::isfinite(gap) && gap >= 0.0) ? gap : 0.0;
}

// The spread a page belongs to, and its column within that spread. ONE function, used by the
// layout, by `spreadFor()` and by `spreadCount()`, so the cover-page parity cannot be right in one
// of them and wrong in another -- which is exactly the shape of the off-by-one plan 5.5 warns
// about.
struct SpreadPosition {
    int spread = 0;
    int column = 0;   // 0 or 1, BEFORE any RTL swap: 0 is the first page of the spread
};

SpreadPosition positionOf(int pageIndex, bool showCover) noexcept
{
    if (!showCover) return SpreadPosition{pageIndex / 2, pageIndex % 2};
    // The cover takes a spread of its own AND the recto column, so the left column of the first
    // spread stays empty. Every pair after it shifts by one, which is the parity change.
    if (pageIndex == 0) return SpreadPosition{0, 1};
    return SpreadPosition{(pageIndex + 1) / 2, (pageIndex + 1) % 2};
}

// The inverse: which pages are in a spread, by column, with -1 for an empty column.
struct SpreadPages {
    int column[2] = {-1, -1};
};

SpreadPages pagesInSpread(int spread, int pageCount, bool showCover) noexcept
{
    SpreadPages out;
    auto take = [&](int slot, int index) {
        if (index >= 0 && index < pageCount) out.column[slot] = index;
    };
    if (!showCover) {
        take(0, 2 * spread);
        take(1, 2 * spread + 1);
        return out;
    }
    if (spread == 0) {
        take(1, 0);
        return out;
    }
    take(0, 2 * spread - 1);
    take(1, 2 * spread);
    return out;
}

bool intersects(const DeviceRect& a, const DeviceRect& b) noexcept
{
    return a.left() < b.right() && b.left() < a.right() && a.top() < b.bottom()
           && b.top() < a.bottom();
}

} // namespace

std::optional<LayoutMode> layoutModeFromName(std::string_view name) noexcept
{
    // `nullopt` for a name this function does not know -- `Settings` then falls back to the
    // declared default and logs it, rather than a typo silently meaning "single page".
    for (const LayoutMode mode : {LayoutMode::Single, LayoutMode::SingleContinuous,
                                  LayoutMode::TwoUp, LayoutMode::TwoUpContinuous}) {
        if (name == layoutModeName(mode)) return mode;
    }
    return std::nullopt;
}

const char* layoutModeName(LayoutMode mode) noexcept
{
    switch (mode) {
    case LayoutMode::Single:           return "single";
    case LayoutMode::SingleContinuous: return "single-continuous";
    case LayoutMode::TwoUp:            return "two-up";
    case LayoutMode::TwoUpContinuous:  return "two-up-continuous";
    }
    return "?";
}

// ---------------------------------------------------------------------------------------------
// PageLayout
// ---------------------------------------------------------------------------------------------

const PageSlot* PageLayout::slotForPage(int pageIndex) const noexcept
{
    for (const PageSlot& slot : slots_) {
        if (slot.pageIndex == pageIndex) return &slot;
    }
    return nullptr;
}

int PageLayout::pageAt(DevicePoint point) const noexcept
{
    for (const PageSlot& slot : slots_) {
        if (slot.rect.contains(point)) return slot.pageIndex;
    }
    return -1;
}

std::vector<PageSlot> PageLayout::slotsIntersecting(const DeviceRect& viewport) const
{
    std::vector<PageSlot> out;
    for (const PageSlot& slot : slots_) {
        if (intersects(slot.rect, viewport)) out.push_back(slot);
    }
    return out;
}

std::optional<PageLayout::DocumentPoint> PageLayout::toDocument(DevicePoint point) const
{
    for (const PageSlot& slot : slots_) {
        if (!slot.rect.contains(point)) continue;

        // The transform is built per call rather than cached in the slot: `PageTransform` is four
        // doubles and a switch, and a cached one is a fifth copy of the scale that can disagree
        // with `presentedScale_` after a zoom.
        const PageTransform transform(slot.cropBox.size, 1.0, presentedScale_ * 72.0,
                                      slot.rotation);
        const DevicePoint local{point.x - slot.rect.left(), point.y - slot.rect.top()};
        const PdfPoint inBox = transform.toPdf(local);
        // The CropBox origin is added back LAST. `PageTransform` works in a space whose page starts
        // at (0,0); a print-ready file's CropBox routinely does not, and forgetting this offset
        // gives a selection that is right on ordinary files and off by the bleed on the ones that
        // matter.
        return DocumentPoint{slot.pageIndex,
                             PdfPoint{inBox.x + slot.cropBox.left(), inBox.y + slot.cropBox.bottom()}};
    }
    return std::nullopt;
}

std::optional<DevicePoint> PageLayout::toContent(int pageIndex, PdfPoint point) const
{
    const PageSlot* slot = slotForPage(pageIndex);
    if (slot == nullptr) return std::nullopt;

    const PageTransform transform(slot->cropBox.size, 1.0, presentedScale_ * 72.0, slot->rotation);
    const PdfPoint inBox{point.x - slot->cropBox.left(), point.y - slot->cropBox.bottom()};
    const DevicePoint local = transform.toDevice(inBox);
    return DevicePoint{slot->rect.left() + local.x, slot->rect.top() + local.y};
}

// ---------------------------------------------------------------------------------------------
// PagePresentation
// ---------------------------------------------------------------------------------------------

int PagePresentation::spreadCount(int pageCount, LayoutMode mode) const noexcept
{
    if (pageCount <= 0) return 0;
    if (!isTwoUp(mode)) return pageCount;
    if (!options_.showCoverInTwoUp) return (pageCount + 1) / 2;
    return 1 + pageCount / 2;   // the cover, then ceil((pageCount - 1) / 2) pairs
}

std::vector<int> PagePresentation::spreadFor(int pageIndex, int pageCount, LayoutMode mode) const
{
    if (pageIndex < 0 || pageIndex >= pageCount) return {};
    if (!isTwoUp(mode)) return {pageIndex};

    const SpreadPosition position = positionOf(pageIndex, options_.showCoverInTwoUp);
    const SpreadPages pages = pagesInSpread(position.spread, pageCount, options_.showCoverInTwoUp);

    // Column order, and RTL reverses it. The PAGES do not move -- only which side of the gutter
    // each column is drawn on -- so the returned vector is what a caller draws left to right.
    if (options_.rtlBinding) return {pages.column[1], pages.column[0]};
    return {pages.column[0], pages.column[1]};
}

PageLayout PagePresentation::layout(const PageTree& tree, LayoutMode mode,
                                    const Resolution& resolution, int currentPage,
                                    PageRotation viewRotation) const
{
    // The view's quarter-turn, applied to every size this function reads. It is a lambda rather
    // than four `if`s because the layout asks for a page's size in four places -- the row height,
    // the column width, the slot, and the document's widest page -- and three of four would be a
    // document whose scrollbars disagree with its pages.
    const auto viewed = [viewRotation](PdfSize size) {
        return swapsAxes(viewRotation) ? PdfSize{size.height, size.width} : size;
    };

    PageLayout out;
    out.mode_ = mode;
    out.presentedScale_ = sanitizedScale(resolution.presentedScale());

    const int pageCount = tree.count();
    if (pageCount <= 0) return out;

    const double scale = out.presentedScale_;
    const double margin = sanitizedGap(options_.margin);
    const double pageGap = sanitizedGap(options_.pageGap);
    const double spreadGap = sanitizedGap(options_.spreadGap);
    const bool twoUp = isTwoUp(mode);
    const bool showCover = twoUp && options_.showCoverInTwoUp;

    // One column width for the whole document -- see the header. `largestDisplaySize()` measures
    // widest and tallest independently, which is the right answer here for the same reason it is
    // the right answer there.
    const double columnWidth = viewed(tree.largestDisplaySize()).width * scale;
    const double columnStride = columnWidth + spreadGap;
    const double contentWidth = 2.0 * margin + (twoUp ? columnWidth * 2.0 + spreadGap : columnWidth);

    const int spreads = spreadCount(pageCount, mode);
    int firstSpread = 0;
    int lastSpread = spreads - 1;
    if (!isContinuous(mode)) {
        // Single and Two-Up show ONE page or ONE spread. A current page outside the document is
        // clamped rather than refused: it arrives from a restored session and from a document that
        // has just been replaced under a view, and an empty layout would be a blank window with a
        // scrollbar.
        const int clamped = std::clamp(currentPage, 0, pageCount - 1);
        firstSpread = twoUp ? positionOf(clamped, showCover).spread : clamped;
        lastSpread = firstSpread;
    }

    double y = margin;
    bool laidOutAny = false;
    for (int spread = firstSpread; spread <= lastSpread; ++spread) {
        SpreadPages pages;
        if (twoUp) {
            pages = pagesInSpread(spread, pageCount, showCover);
        } else {
            pages.column[0] = spread;   // one page per "spread" in the single modes
        }

        double rowHeight = 0.0;
        for (const int index : pages.column) {
            if (index < 0) continue;
            rowHeight = std::max(rowHeight, viewed(tree.displaySize(index)).height * scale);
        }

        for (int column = 0; column < 2; ++column) {
            const int index = pages.column[column];
            if (index < 0) continue;
            const Page* page = tree.at(index);
            if (page == nullptr) continue;

            const PdfSize display = viewed(page->displaySize());
            const double width = display.width * scale;
            const double height = display.height * scale;

            // RTL swaps the COLUMN, not the page. In the single modes there is one column and the
            // swap is a no-op, which is why `rtlBinding` on a single-page layout changes nothing --
            // asserted, because "RTL does something everywhere" is the tempting wrong reading.
            const int drawnColumn = (twoUp && options_.rtlBinding) ? (1 - column) : column;
            const double columnLeft = margin + drawnColumn * columnStride;
            const double x = columnLeft + (columnWidth - width) / 2.0;

            PageSlot slot;
            slot.pageIndex = index;
            slot.rect = DeviceRect{DevicePoint{x, y}, DeviceSize{width, height}};
            slot.cropBox = page->cropBox();
            // THE COMBINED ROTATION, not the page's. `PageLayout::toDocument()` builds a
            // `PageTransform` from this, and a mapping that used only `/Rotate` would put a click
            // on a rotated view at the wrong point of the page -- which at Stage 5 is text
            // selection picking the wrong words, and at Stage 6 an annotation landing sideways.
            slot.rotation = combine(page->rotation(), viewRotation);
            out.slots_.push_back(slot);
            laidOutAny = true;
        }

        // NOTHING REORDERS `slots_`. The loop above walks columns in spread order and only the
        // drawn `x` changes, so slots come out in DOCUMENT order even under RTL -- which is what
        // A-34 means by "the document order is unchanged", and is the first question a reader of
        // this function asks.

        y += rowHeight + pageGap;
    }

    const double contentHeight = laidOutAny ? (y - pageGap + margin) : (2.0 * margin);
    out.contentSize_ = DeviceSize{contentWidth, contentHeight};
    return out;
}

} // namespace rawpage::core::render
