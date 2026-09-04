// tst_presentation -- `core/render/PagePresentation`. Stage 2 T10, guide cases A-32, A-33, A-34.
//
// The two asymmetries plan 5.5 names are where the off-by-one lives, so each is asserted as PAGE
// NUMBERS in spreads (which is what a person sees) as well as through the rectangles: a layout
// whose parity is wrong still produces plausible rectangles, and comparing coordinates alone would
// let it through.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "core/render/PagePresentation.h"

#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using rawpage::core::DevicePoint;
using rawpage::core::DeviceRect;
using rawpage::core::DeviceSize;
using rawpage::core::Page;
using rawpage::core::PageTree;
using rawpage::core::PdfPoint;
using rawpage::core::PdfRect;
using rawpage::core::PdfSize;
using rawpage::core::render::LayoutMode;
using rawpage::core::render::PageLayout;
using rawpage::core::render::PagePresentation;
using rawpage::core::render::PresentationOptions;
using rawpage::core::render::Resolution;

namespace {

constexpr double kW = 600.0;
constexpr double kH = 800.0;

// 72 DPI and no cap, so one PDF point is one device pixel and every expected number below is
// readable straight off the page size.
Resolution unitScale()
{
    return Resolution{72.0, 1.0, 1.0};
}

PageTree uniformTree(int count, double width = kW, double height = kH)
{
    std::vector<Page> pages;
    pages.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        pages.emplace_back(i, PdfRect{PdfPoint{0.0, 0.0}, PdfSize{width, height}});
    return PageTree(std::move(pages));
}

// No margins and no gaps, for the cases that are about WHICH page is where rather than about
// furniture. The gap cases set them deliberately.
PresentationOptions bare()
{
    PresentationOptions options;
    options.margin = 0.0;
    options.pageGap = 0.0;
    options.spreadGap = 0.0;
    return options;
}

} // namespace

TEST_CASE("render presentation lays the four modes out in document order", "[core][render]")
{
    // A-32. Each mode produces page rects in document order with correct gaps.
    PresentationOptions options = bare();
    options.pageGap = 10.0;
    options.spreadGap = 6.0;
    options.margin = 4.0;
    const PagePresentation presentation(options);
    const PageTree tree = uniformTree(6);

    SECTION("single continuous stacks every page with the gap between them")
    {
        const PageLayout layout =
            presentation.layout(tree, LayoutMode::SingleContinuous, unitScale());
        REQUIRE(layout.pageSlots().size() == 6);
        for (int i = 0; i < 6; ++i) {
            CHECK(layout.pageSlots()[static_cast<std::size_t>(i)].pageIndex == i);
            CHECK_THAT(layout.pageSlots()[static_cast<std::size_t>(i)].rect.top(),
                       WithinRel(4.0 + i * (kH + 10.0)));
        }
        CHECK_THAT(layout.contentSize().width, WithinRel(2 * 4.0 + kW));
        CHECK_THAT(layout.contentSize().height, WithinRel(2 * 4.0 + 6 * kH + 5 * 10.0));
    }

    SECTION("single shows ONE page, and the current page is which one")
    {
        const PageLayout layout = presentation.layout(tree, LayoutMode::Single, unitScale(), 3);
        REQUIRE(layout.pageSlots().size() == 1);
        CHECK(layout.pageSlots().front().pageIndex == 3);
        CHECK(layout.slotForPage(0) == nullptr);
        // Without a current page the two single modes would be indistinguishable, which is the
        // deviation the header records: four modes that behave as two is a mode nobody can select.
        CHECK(presentation.layout(tree, LayoutMode::Single, unitScale(), 0)
                  .pageSlots()
                  .front()
                  .pageIndex
              == 0);
    }

    SECTION("two-up continuous stacks spreads, two pages per row")
    {
        const PageLayout layout =
            presentation.layout(tree, LayoutMode::TwoUpContinuous, unitScale());
        REQUIRE(layout.pageSlots().size() == 6);
        // Three rows of two. Pages 0 and 1 share a top edge; page 2 starts one row down.
        CHECK_THAT(layout.pageSlots()[0].rect.top(), WithinRel(layout.pageSlots()[1].rect.top()));
        CHECK_THAT(layout.pageSlots()[2].rect.top(), WithinRel(4.0 + kH + 10.0));
        CHECK(layout.pageSlots()[0].rect.left() < layout.pageSlots()[1].rect.left());
        CHECK_THAT(layout.contentSize().width, WithinRel(2 * 4.0 + 2 * kW + 6.0));
        CHECK_THAT(layout.contentSize().height, WithinRel(2 * 4.0 + 3 * kH + 2 * 10.0));
    }

    SECTION("two-up shows ONE spread")
    {
        const PageLayout layout = presentation.layout(tree, LayoutMode::TwoUp, unitScale(), 3);
        REQUIRE(layout.pageSlots().size() == 2);
        CHECK(layout.pageSlots()[0].pageIndex == 2);
        CHECK(layout.pageSlots()[1].pageIndex == 3);
    }

    SECTION("an empty document lays out nothing rather than one empty page")
    {
        const PageTree empty;
        const PageLayout layout =
            presentation.layout(empty, LayoutMode::SingleContinuous, unitScale());
        CHECK(layout.isEmpty());
        CHECK(layout.pageAt(DevicePoint{0.0, 0.0}) == -1);
    }
}

TEST_CASE("render presentation shifts every pair's parity for a cover page", "[core][render]")
{
    // A-33, over an ODD page count -- five pages, so the last spread is a lone page as well.
    const PageTree tree = uniformTree(5);

    SECTION("cover off: 1-2, 3-4, 5")
    {
        const PagePresentation presentation(bare());
        CHECK(presentation.spreadCount(5, LayoutMode::TwoUp) == 3);
        CHECK(presentation.spreadFor(0, 5, LayoutMode::TwoUp) == std::vector<int>{0, 1});
        CHECK(presentation.spreadFor(1, 5, LayoutMode::TwoUp) == std::vector<int>{0, 1});
        CHECK(presentation.spreadFor(2, 5, LayoutMode::TwoUp) == std::vector<int>{2, 3});
        // The lone last page keeps its own column and leaves the other empty, rather than sliding
        // across -- which is what makes the spreads of a printed document line up.
        CHECK(presentation.spreadFor(4, 5, LayoutMode::TwoUp) == std::vector<int>{4, -1});
    }

    SECTION("cover on: 1, 2-3, 4-5")
    {
        PresentationOptions options = bare();
        options.showCoverInTwoUp = true;
        const PagePresentation presentation(options);
        CHECK(presentation.spreadCount(5, LayoutMode::TwoUp) == 3);
        // The cover is alone AND it is a recto: the left column of the first spread stays empty,
        // which is the entire reason a person turns this option on.
        CHECK(presentation.spreadFor(0, 5, LayoutMode::TwoUp) == std::vector<int>{-1, 0});
        CHECK(presentation.spreadFor(1, 5, LayoutMode::TwoUp) == std::vector<int>{1, 2});
        CHECK(presentation.spreadFor(2, 5, LayoutMode::TwoUp) == std::vector<int>{1, 2});
        CHECK(presentation.spreadFor(3, 5, LayoutMode::TwoUp) == std::vector<int>{3, 4});

        // And the same fact through the geometry, because the two can disagree: with the cover on,
        // pages 2 and 3 (indices 1 and 2) share a row, where without it pages 1 and 2 do.
        const PageLayout layout =
            presentation.layout(tree, LayoutMode::TwoUpContinuous, unitScale());
        REQUIRE(layout.pageSlots().size() == 5);
        const DeviceRect* cover = &layout.slotForPage(0)->rect;
        CHECK_THAT(cover->left(), WithinRel(kW));   // the right-hand column, left one empty
        CHECK_THAT(layout.slotForPage(1)->rect.top(), WithinRel(layout.slotForPage(2)->rect.top()));
        CHECK(layout.slotForPage(1)->rect.top() > cover->top());
    }

    SECTION("THE CONTROL: with the cover off, page 1 shares its row with page 2")
    {
        // Without this the case above is satisfied by a layout that always puts page 1 alone.
        const PagePresentation presentation(bare());
        const PageLayout layout =
            presentation.layout(tree, LayoutMode::TwoUpContinuous, unitScale());
        CHECK_THAT(layout.slotForPage(0)->rect.top(), WithinRel(layout.slotForPage(1)->rect.top()));
        CHECK_THAT(layout.slotForPage(0)->rect.left(), WithinAbs(0.0, 1e-9));
    }
}

TEST_CASE("render presentation's RTL binding swaps the spread, not the order", "[core][render]")
{
    // A-34, with the cover on and an odd page count, so all three interact at once -- which is
    // what `corpus/ordinary/odd-cover-rtl.pdf` exists for.
    PresentationOptions options = bare();
    options.showCoverInTwoUp = true;
    options.rtlBinding = true;
    const PagePresentation presentation(options);
    const PageTree tree = uniformTree(5);

    const PageLayout layout = presentation.layout(tree, LayoutMode::TwoUpContinuous, unitScale());
    REQUIRE(layout.pageSlots().size() == 5);

    // Page 2 (index 1) is on the RIGHT of the 2-3 spread and page 3 (index 2) on the left.
    CHECK(layout.slotForPage(1)->rect.left() > layout.slotForPage(2)->rect.left());
    CHECK_THAT(layout.slotForPage(1)->rect.left(), WithinRel(kW));
    CHECK_THAT(layout.slotForPage(2)->rect.left(), WithinAbs(0.0, 1e-9));

    // THE DOCUMENT ORDER IS UNCHANGED. `pageSlots()` still walks pages 0..4 in order, and the vertical
    // stacking is still 1 | 2-3 | 4-5: RTL moves pages across a gutter, never through the document.
    for (std::size_t i = 0; i < layout.pageSlots().size(); ++i)
        CHECK(layout.pageSlots()[i].pageIndex == static_cast<int>(i));
    CHECK_THAT(layout.slotForPage(1)->rect.top(), WithinRel(layout.slotForPage(2)->rect.top()));
    CHECK(layout.slotForPage(3)->rect.top() > layout.slotForPage(1)->rect.top());

    // The cover is a recto, so RTL puts it on the LEFT -- the mirror of the LTR case, and the one
    // place where "RTL swaps the columns" is visible on a spread holding one page.
    CHECK_THAT(layout.slotForPage(0)->rect.left(), WithinAbs(0.0, 1e-9));

    SECTION("RTL changes nothing in the single modes")
    {
        // The tempting wrong reading is that RTL does something everywhere. There is one column in
        // a single-page layout and nothing to swap; a layout that mirrored it would put every page
        // against the right edge of the viewport for no reason.
        PresentationOptions ltr = options;
        ltr.rtlBinding = false;
        const PagePresentation plain(ltr);
        const PageLayout a = presentation.layout(tree, LayoutMode::SingleContinuous, unitScale());
        const PageLayout b = plain.layout(tree, LayoutMode::SingleContinuous, unitScale());
        for (std::size_t i = 0; i < a.pageSlots().size(); ++i)
            CHECK_THAT(a.pageSlots()[i].rect.left(), WithinAbs(b.pageSlots()[i].rect.left(), 1e-9));
    }
}

TEST_CASE("render presentation maps a point both ways and back", "[core][render]")
{
    // A-32's second clause: page-rect <-> document-point mapping round-trips. Both the y flip and
    // a non-zero CropBox origin are in play, and each of them alone produces a mapping that is
    // correct at the page's centre and wrong at its corners.
    std::vector<Page> pages;
    Page plain(0, PdfRect{PdfPoint{0.0, 0.0}, PdfSize{kW, kH}});
    Page offset(1, PdfRect{PdfPoint{0.0, 0.0}, PdfSize{kW, kH}});
    offset.setCropBox(PdfRect{PdfPoint{20.0, 30.0}, PdfSize{500.0, 700.0}});
    Page turned(2, PdfRect{PdfPoint{0.0, 0.0}, PdfSize{kW, kH}});
    turned.setRotation(90);
    pages.push_back(plain);
    pages.push_back(offset);
    pages.push_back(turned);
    const PageTree tree(std::move(pages));

    const PagePresentation presentation(bare());
    const PageLayout layout = presentation.layout(tree, LayoutMode::SingleContinuous, unitScale());
    REQUIRE(layout.pageSlots().size() == 3);

    SECTION("the top-left of a page is its top-left in PDF space")
    {
        // PDF y runs UP from the bottom, so the device top-left is (left, top) in PDF terms -- the
        // single most common sign error in this domain, and the reason `core/Geometry.h` exists.
        // The probe is the slot's OWN origin rather than (0,0): the rotated page in this tree is
        // the widest, so every other page is centred in its column and the content origin is in
        // the margin beside page 0 rather than on it.
        const DeviceRect& first = layout.slotForPage(0)->rect;
        const auto document = layout.toDocument(DevicePoint{first.left(), first.top()});
        REQUIRE(document.has_value());
        CHECK(document->pageIndex == 0);
        CHECK_THAT(document->point.x, WithinAbs(0.0, 1e-9));
        CHECK_THAT(document->point.y, WithinAbs(kH, 1e-9));
    }

    SECTION("every page round-trips, rotation and CropBox offset included")
    {
        for (int index = 0; index < 3; ++index) {
            const Page* page = tree.at(index);
            REQUIRE(page != nullptr);
            const PdfRect box = page->cropBox();
            // Three points, none of them the centre: a wrong flip or a dropped offset is exact at
            // the centre of a symmetric page and wrong everywhere else.
            const std::vector<PdfPoint> probes{
                PdfPoint{box.left() + 1.0, box.bottom() + 1.0},
                PdfPoint{box.right() - 1.0, box.top() - 1.0},
                PdfPoint{box.left() + 7.0, box.top() - 13.0},
            };
            for (const PdfPoint& probe : probes) {
                const auto device = layout.toContent(index, probe);
                REQUIRE(device.has_value());
                const auto back = layout.toDocument(*device);
                REQUIRE(back.has_value());
                CHECK(back->pageIndex == index);
                CHECK_THAT(back->point.x, WithinAbs(probe.x, 1e-6));
                CHECK_THAT(back->point.y, WithinAbs(probe.y, 1e-6));
            }
        }
    }

    SECTION("a point in a gap belongs to no page")
    {
        PresentationOptions gapped = bare();
        gapped.pageGap = 20.0;
        gapped.margin = 20.0;
        const PagePresentation spaced(gapped);
        const PageLayout spacedLayout =
            spaced.layout(tree, LayoutMode::SingleContinuous, unitScale());
        CHECK(spacedLayout.pageAt(DevicePoint{5.0, 5.0}) == -1);
        CHECK_FALSE(spacedLayout.toDocument(DevicePoint{5.0, 5.0}).has_value());
        const DeviceRect& firstPage = spacedLayout.slotForPage(0)->rect;
        CHECK(spacedLayout.pageAt(DevicePoint{firstPage.left() + 1.0, firstPage.top() + 1.0}) == 0);
        // And the gap BETWEEN two pages, which is the one a person actually clicks in.
        CHECK(spacedLayout.pageAt(DevicePoint{firstPage.left() + 1.0, firstPage.bottom() + 5.0})
              == -1);
    }

    SECTION("a page that is not laid out cannot be mapped to")
    {
        const PageLayout single = presentation.layout(tree, LayoutMode::Single, unitScale(), 0);
        CHECK_FALSE(single.toContent(2, PdfPoint{10.0, 10.0}).has_value());
        CHECK(single.toContent(0, PdfPoint{10.0, 10.0}).has_value());
    }
}

TEST_CASE("render presentation gives every column the widest page's width", "[core][render]")
{
    // A mixed-size document must not change its content width as it scrolls, or the horizontal
    // scroll position -- and the page under the cursor -- move on their own.
    // `corpus/ordinary/mixed-page-sizes.pdf` is the fixture this rule was written for.
    std::vector<Page> pages;
    pages.emplace_back(0, PdfRect{PdfPoint{0.0, 0.0}, PdfSize{300.0, 400.0}});
    pages.emplace_back(1, PdfRect{PdfPoint{0.0, 0.0}, PdfSize{900.0, 400.0}});
    const PageTree tree(std::move(pages));

    const PagePresentation presentation(bare());
    const PageLayout layout = presentation.layout(tree, LayoutMode::SingleContinuous, unitScale());
    CHECK_THAT(layout.contentSize().width, WithinRel(900.0));
    // The narrow page is CENTRED in that width rather than left-aligned.
    CHECK_THAT(layout.slotForPage(0)->rect.left(), WithinRel(300.0));
    CHECK_THAT(layout.slotForPage(1)->rect.left(), WithinAbs(0.0, 1e-9));

    // A taller neighbour does not stretch its partner: a spread's row is as tall as the taller
    // page, and the shorter one keeps its own height and sits at the top of the row.
    const PageLayout spread = presentation.layout(tree, LayoutMode::TwoUp, unitScale(), 0);
    REQUIRE(spread.pageSlots().size() == 2);
    CHECK_THAT(spread.pageSlots()[0].rect.size.height, WithinRel(400.0));
}

TEST_CASE("render presentation lays out at the PRESENTED scale", "[core][render]")
{
    // A capped resolution rasterizes fewer pixels and paints them larger. The layout must use the
    // presented scale, or a capped page occupies less space than the zoom asked for -- which reads
    // as the zoom control being broken rather than as the cap doing its job.
    const PagePresentation presentation(bare());
    const PageTree tree = uniformTree(1);

    Resolution capped;
    capped.dpi = 72.0;
    capped.upscale = 4.0;
    const PageLayout layout = presentation.layout(tree, LayoutMode::Single, capped);
    CHECK_THAT(layout.presentedScale(), WithinRel(4.0));
    CHECK_THAT(layout.contentSize().width, WithinRel(kW * 4.0));

    // A nonsense resolution still produces a usable layout rather than a zero-sized one: this is
    // reached from a restored settings file and from a monitor that has just been unplugged.
    const PageLayout degenerate =
        presentation.layout(tree, LayoutMode::Single, Resolution{0.0, 0.0, 0.0});
    CHECK(degenerate.contentSize().width > 0.0);
}

TEST_CASE("render presentation reports the slots a viewport touches", "[core][render]")
{
    const PagePresentation presentation(bare());
    const PageTree tree = uniformTree(10);
    const PageLayout layout = presentation.layout(tree, LayoutMode::SingleContinuous, unitScale());

    // A viewport over the boundary between pages 3 and 4 sees exactly those two.
    const DeviceRect viewport{DevicePoint{0.0, 4 * kH - 10.0}, DeviceSize{kW, 20.0}};
    const std::vector<rawpage::core::render::PageSlot> touched = layout.slotsIntersecting(viewport);
    REQUIRE(touched.size() == 2);
    CHECK(touched[0].pageIndex == 3);
    CHECK(touched[1].pageIndex == 4);

    // And a viewport past the end of the document sees nothing -- rather than clamping to the last
    // page, which would keep a page painted after it had been scrolled away.
    const DeviceRect past{DevicePoint{0.0, 100 * kH}, DeviceSize{kW, 20.0}};
    CHECK(layout.slotsIntersecting(past).empty());
}
