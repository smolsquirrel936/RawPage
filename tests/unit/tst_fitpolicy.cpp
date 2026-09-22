// Stage 4 T12 -- `core/render/FitPolicy`: which rectangle a fit measures, and the zoom that
// satisfies it. Guide cases **A-67**'s fallback half and **A-68**'s arithmetic half
// (`Stage4-ViewingSurface-Testing-Guide.md` §5.13). Closes Stage 2's **I-28**.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS FILE COVERS AND WHAT IT DELIBERATELY DOES NOT (convention 19).
//
// It covers the POLICY over synthetic geometry: three rectangles chosen so that the three fits
// cannot agree by accident, the fallbacks, and the arithmetic at both ends of the zoom range. It
// covers nothing about a real file -- no corpus page's objects are unioned here, because this
// target links no engine (`rawpage_assert_headless()`), and the engine half is `tests/render`'s
// `tst_contentbox.cpp`.
//
// **THE ASSERTION IS ON THE ZOOM, NEVER ON A MODE.** That is I-28's whole lesson: the case that was
// supposed to catch Fit Visible fitting the CropBox asserted `fitMode()` and was green for a month,
// because the mode was set correctly and the number was wrong.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/render/FitPolicy.h"

using namespace rawpage;
using core::PdfPoint;
using core::PdfRect;
using core::PdfSize;
using core::render::FitMode;
using core::render::PageBoxes;
using core::render::PhysicalDpi;
using core::render::Zoom;

namespace {

// THREE RECTANGLES, EACH STRICTLY INSIDE THE LAST, with 72 pt between each and the next on every
// side -- the same shape `corpus/ordinary/cropbox-margins.pdf` carries, so a defect that shows on
// the file shows here too and is diagnosed without an engine.
PageBoxes threeRectangles()
{
    PageBoxes boxes;
    boxes.media = PdfRect{PdfPoint{0.0, 0.0}, PdfSize{595.276, 841.89}};
    boxes.crop = PdfRect{PdfPoint{72.0, 72.0}, PdfSize{451.276, 697.89}};
    boxes.content = PdfRect{PdfPoint{144.0, 160.0}, PdfSize{307.276, 521.89}};
    return boxes;
}

constexpr double kMargin = 12.0;

} // namespace

// =============================================================================================
// A-68 -- the three fits are three DIFFERENT rectangles, and therefore three different zooms.
// =============================================================================================

TEST_CASE("fitRect measures three different rectangles", "[fitpolicy]")
{
    const PageBoxes boxes = threeRectangles();

    CHECK(core::render::fitRect(boxes, FitMode::Page) == boxes.media);
    CHECK(core::render::fitRect(boxes, FitMode::Visible) == *boxes.content);

    // Width, Height and None all measure what a reader actually sees drawn.
    CHECK(core::render::fitRect(boxes, FitMode::Width) == boxes.crop);
    CHECK(core::render::fitRect(boxes, FitMode::Height) == boxes.crop);
    CHECK(core::render::fitRect(boxes, FitMode::None) == boxes.crop);
}

TEST_CASE("the three fits give three different zooms", "[fitpolicy]")
{
    const PageBoxes boxes = threeRectangles();
    const core::DeviceSize viewport{1000.0, 800.0};
    const PhysicalDpi dpi{96.0};

    const Zoom page = core::render::zoomToFit(
        FitMode::Page, core::render::fitRect(boxes, FitMode::Page).size, viewport, kMargin, dpi,
        Zoom{1.0});
    const Zoom visible = core::render::zoomToFit(
        FitMode::Visible, core::render::fitRect(boxes, FitMode::Visible).size, viewport, kMargin,
        dpi, Zoom{1.0});
    const Zoom width = core::render::zoomToFit(
        FitMode::Width, core::render::fitRect(boxes, FitMode::Width).size, viewport, kMargin, dpi,
        Zoom{1.0});

    // THE NUMBERS, not the modes. **Fit Visible is compared against Fit PAGE and not against Fit
    // Width**, and the reason is worth stating because the first version of this case got it
    // backwards: Page and Visible both take `min(width, height)` and so measure the same question
    // over two rectangles, while Width constrains one axis only and is free to be the largest of
    // the three on a tall box. `visible > width` is therefore not a property of a correct product
    // at all -- here the content box is tall, and Fit Width is the biggest number on the page.
    CHECK(visible.factor > page.factor);

    // And not by a rounding error: the content box is 307 pt wide against a 595 pt sheet, so the
    // two whole-box zooms are more than half as much again apart.
    CHECK(visible.factor > page.factor * 1.5);

    // All three differ. A product that had quietly kept fitting the CropBox would show
    // `visible == width` here, which is exactly what I-28 was.
    CHECK(visible.factor != width.factor);
    CHECK(width.factor != page.factor);
}

// =============================================================================================
// A-68's CONTROL -- content that fills its CropBox makes Fit Visible and Fit Page agree.
// =============================================================================================

TEST_CASE("Fit Visible agrees with Fit Page when the content fills the sheet", "[fitpolicy]")
{
    // THIS IS THE CONTROL AND IT IS WHY THE CORPUS FILE HAD TO BE BUILT. Every document whose ink
    // reaches its crop makes these two identical, and on such a file the defect I-28 names is
    // invisible -- which is how it survived every existing case. A green suite over ordinary
    // documents says nothing about Fit Visible at all.
    PageBoxes boxes;
    boxes.media = PdfRect{PdfPoint{0.0, 0.0}, PdfSize{612.0, 792.0}};
    boxes.crop = boxes.media;
    boxes.content = boxes.media;

    const core::DeviceSize viewport{1000.0, 800.0};
    const PhysicalDpi dpi{96.0};

    const Zoom visible = core::render::zoomToFit(
        FitMode::Visible, core::render::fitRect(boxes, FitMode::Visible).size, viewport, kMargin,
        dpi, Zoom{1.0});
    const Zoom page = core::render::zoomToFit(
        FitMode::Page, core::render::fitRect(boxes, FitMode::Page).size, viewport, kMargin, dpi,
        Zoom{1.0});

    CHECK(visible.factor == page.factor);
}

// =============================================================================================
// A-67's fallback half -- an absent content box, and a degenerate one.
// =============================================================================================

TEST_CASE("a page with no content box falls back to the CropBox", "[fitpolicy]")
{
    PageBoxes boxes = threeRectangles();
    boxes.content.reset();

    // THE FALLBACK IS THE OLD BEHAVIOUR, not an error path: an empty page and an engine that
    // declined both land here, and both get exactly what `Ctrl+3` did for the whole of Stages 2
    // and 3. A blank viewport or a zero-sized box would be the degradation this refuses.
    CHECK(core::render::fitRect(boxes, FitMode::Visible) == boxes.crop);

    // A content box the engine reported with no area is the same absence in another spelling -- a
    // page holding one zero-width rule, say. Treating it as a rectangle would divide the viewport
    // by zero and clamp to 6400 %.
    boxes.content = PdfRect{PdfPoint{200.0, 200.0}, PdfSize{0.0, 0.0}};
    CHECK(core::render::fitRect(boxes, FitMode::Visible) == boxes.crop);
}

// =============================================================================================
// The arithmetic's own guards -- every one of these is reached in the shipped product.
// =============================================================================================

TEST_CASE("zoomToFit refuses nonsense rather than propagating it", "[fitpolicy]")
{
    const PdfSize box{595.276, 841.89};
    const core::DeviceSize viewport{1000.0, 800.0};
    const PhysicalDpi dpi{96.0};
    const Zoom current{0.37};

    // `None` is passed unconditionally by `PageView::refresh`, on every scroll.
    CHECK(core::render::zoomToFit(FitMode::None, box, viewport, kMargin, dpi, current) == current);

    // A viewport with no area is what a widget has before its first layout, which is most of a
    // window's construction.
    CHECK(core::render::zoomToFit(FitMode::Page, box, core::DeviceSize{0.0, 0.0}, kMargin, dpi,
                                  current)
          == current);

    // A page with no area is `corpus/malformed/`'s, and a zero DPI is a monitor the OS has not
    // described yet.
    CHECK(core::render::zoomToFit(FitMode::Page, PdfSize{0.0, 0.0}, viewport, kMargin, dpi, current)
          == current);
    CHECK(core::render::zoomToFit(FitMode::Page, box, viewport, kMargin, PhysicalDpi{0.0}, current)
          == current);
}

TEST_CASE("a fit is computed against the PHYSICAL dpi rather than a nominal 96", "[fitpolicy]")
{
    const PdfSize box{595.276, 841.89};
    const core::DeviceSize viewport{1000.0, 800.0};

    const Zoom at96 = core::render::zoomToFit(FitMode::Width, box, viewport, kMargin,
                                              PhysicalDpi{96.0}, Zoom{1.0});
    const Zoom at144 = core::render::zoomToFit(FitMode::Width, box, viewport, kMargin,
                                               PhysicalDpi{144.0}, Zoom{1.0});

    // EXACTLY THE SCALE FACTOR APART. Stage 2's **I-20** was this arithmetic getting the DPI from
    // the wrong place and being wrong by precisely this ratio on every scaled display -- and both
    // manual cases that could have caught it passed, because the render was crisp at two thirds of
    // its size.
    CHECK_THAT(at96.factor / at144.factor, Catch::Matchers::WithinRel(1.5, 1e-12));
}

TEST_CASE("a fit clamps into the product's zoom range", "[fitpolicy]")
{
    // A postage-stamp page in a wall-sized viewport asks for more than 6400 %, and a billboard page
    // in a thumbnail asks for less than 8 %. Both are CLAMPED rather than refused: a zoom arrives
    // from a settings file and a restored session as well as from a menu, and a viewer that refused
    // to draw would be a black window where a clamped one is a usable page.
    const Zoom huge = core::render::zoomToFit(FitMode::Width, PdfSize{0.01, 0.01},
                                              core::DeviceSize{4000.0, 4000.0}, kMargin,
                                              PhysicalDpi{96.0}, Zoom{1.0});
    CHECK(huge.factor == Zoom::kMax);

    const Zoom tiny = core::render::zoomToFit(FitMode::Page, PdfSize{500000.0, 500000.0},
                                              core::DeviceSize{100.0, 100.0}, kMargin,
                                              PhysicalDpi{96.0}, Zoom{1.0});
    CHECK(tiny.factor == Zoom::kMin);
}
