// Stage 2 T13 -- ui/viewer/ViewportController and ui/viewer/PageLayoutEngine.
//
// Guide case A-54 (*scroll offsets clamp at document bounds; zoom about the cursor keeps the point
// under the cursor fixed; page-at-a-time paging snaps to boundaries*), plus the fit arithmetic and
// the view history, which A-54 does not name and which have the same failure shape: invisible in a
// screenshot, exact in an assertion.
//
// EVERY CASE HERE RUNS WITH NO WINDOW. That is the whole reason `ViewportController` is a separate
// object from `PageView` -- the arithmetic is four numbers, and a case that had to `show()` a
// widget to check a clamp would be testing Qt's layout as well as ours.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/ViewportController.h"

#include <cmath>
#include <vector>

using namespace rawpage;
using core::render::Zoom;

namespace {

// A4 in points, which is what `corpus/ordinary/` is generated at.
constexpr double kA4Width = 595.276;
constexpr double kA4Height = 841.89;

core::PageTree makeTree(int pages)
{
    std::vector<core::Page> built;
    built.reserve(static_cast<std::size_t>(pages));
    for (int i = 0; i < pages; ++i)
        built.emplace_back(i, core::PdfRect{core::PdfPoint{0.0, 0.0},
                                            core::PdfSize{kA4Width, kA4Height}});
    return core::PageTree(std::move(built));
}

ui::ScreenMetrics nominalMetrics(double scale = 1.0)
{
    ui::ScreenMetrics metrics;
    metrics.scale = core::render::MonitorScale{scale};
    metrics.physical = core::render::PhysicalDpi::nominal(metrics.scale);
    return metrics;
}

bool nearly(double a, double b, double tolerance = 1e-6)
{
    return std::abs(a - b) <= tolerance;
}

} // namespace

// =============================================================================================
// A-54, first clause: the scroll clamps at both bounds.
// =============================================================================================

TEST_CASE("scroll clamps at the document bounds in both directions", "[viewport]")
{
    ui::ViewportController controller;
    controller.setViewportSize(core::DeviceSize{800.0, 600.0});
    controller.setContentSize(core::DeviceSize{2000.0, 5000.0});

    controller.setScroll(core::DevicePoint{-500.0, -500.0});
    CHECK(controller.scroll() == core::DevicePoint{0.0, 0.0});

    controller.setScroll(core::DevicePoint{99999.0, 99999.0});

    // The maximum is content minus viewport, so the LAST screenful is fully visible and there is
    // no empty band past the end of the document.
    CHECK(controller.scroll() == core::DevicePoint{1200.0, 4400.0});

    // A document SMALLER than the viewport does not scroll at all, and the maximum is zero rather
    // than negative -- a negative maximum would let the page drift up off the top edge, which
    // reads as content that cannot be reached.
    controller.setContentSize(core::DeviceSize{400.0, 300.0});
    CHECK(controller.scroll() == core::DevicePoint{0.0, 0.0});
    controller.setScroll(core::DevicePoint{100.0, 100.0});
    CHECK(controller.scroll() == core::DevicePoint{0.0, 0.0});
}

TEST_CASE("a shorter document pulls the scroll back with it", "[viewport]")
{
    ui::ViewportController controller;
    controller.setViewportSize(core::DeviceSize{800.0, 600.0});
    controller.setContentSize(core::DeviceSize{800.0, 5000.0});
    controller.setScroll(core::DevicePoint{0.0, 4400.0});
    REQUIRE(controller.scroll().y == 4400.0);

    // Zooming out makes the content shorter. Left alone, the offset would sit past the end with the
    // scrollbar already at its maximum and no way back.
    controller.setContentSize(core::DeviceSize{800.0, 1200.0});
    CHECK(controller.scroll().y == 600.0);
}

// =============================================================================================
// A-54, second clause: zoom about the cursor keeps the point under the cursor fixed.
// =============================================================================================

TEST_CASE("zoom about a point keeps that point under the cursor", "[viewport]")
{
    ui::ViewportController controller;
    controller.setViewportSize(core::DeviceSize{1000.0, 800.0});
    controller.setContentSize(core::DeviceSize{4000.0, 8000.0});
    controller.setScroll(core::DevicePoint{500.0, 1000.0});
    controller.setZoom(Zoom{1.0});

    const core::DevicePoint anchor{300.0, 200.0};

    // The content point the cursor is over, before.
    const double contentX = controller.scroll().x + anchor.x;
    const double contentY = controller.scroll().y + anchor.y;

    controller.zoomAbout(Zoom{2.0}, anchor);

    // AN EQUALITY, NOT A TOLERANCE. The content space scales uniformly, so the answer is exact --
    // which is the whole reason the arithmetic is in a class with no widget rather than mixed into
    // a paint handler where a rounding to integer pixels would hide a real drift.
    CHECK(nearly(controller.scroll().x + anchor.x, contentX * 2.0));
    CHECK(nearly(controller.scroll().y + anchor.y, contentY * 2.0));

    // And back again lands where it started.
    controller.zoomAbout(Zoom{1.0}, anchor);
    CHECK(nearly(controller.scroll().x, 500.0));
    CHECK(nearly(controller.scroll().y, 1000.0));
}

TEST_CASE("zoom about a point at the top-left corner does not drift", "[viewport]")
{
    // THE NEGATIVE CONTROL for the case above. With the anchor at the origin and the scroll at
    // zero, a correct implementation moves nothing at all -- and an implementation that added the
    // viewport's centre, or that used the presented scale where it wanted the ratio, would move
    // something here and pass the case above by symmetry.
    ui::ViewportController controller;
    controller.setViewportSize(core::DeviceSize{1000.0, 800.0});
    controller.setContentSize(core::DeviceSize{4000.0, 8000.0});
    controller.setZoom(Zoom{1.0});

    controller.zoomAbout(Zoom{4.0}, core::DevicePoint{0.0, 0.0});
    CHECK(controller.scroll() == core::DevicePoint{0.0, 0.0});
    CHECK(controller.zoom() == Zoom{4.0});
}

TEST_CASE("the zoom ladder always moves and stops at the ends", "[viewport]")
{
    ui::ViewportController controller;

    // From a value BETWEEN two rungs, a step lands on the next rung in that direction rather than
    // on the nearest one -- so a press always changes the zoom. Nearest-rung rounding makes the
    // first press after an arbitrary zoom appear to do nothing.
    CHECK(ui::ViewportController::nextZoomStep(Zoom{1.10}, +1) == Zoom{1.25});
    CHECK(ui::ViewportController::nextZoomStep(Zoom{1.10}, -1) == Zoom{1.00});

    // Exactly on a rung, it moves to the next one and not to itself.
    CHECK(ui::ViewportController::nextZoomStep(Zoom{1.00}, +1) == Zoom{1.25});
    CHECK(ui::ViewportController::nextZoomStep(Zoom{1.00}, -1) == Zoom{0.75});

    // The ends clamp rather than wrap or refuse.
    CHECK(ui::ViewportController::nextZoomStep(Zoom{Zoom::kMax}, +1) == Zoom{Zoom::kMax});
    CHECK(ui::ViewportController::nextZoomStep(Zoom{Zoom::kMin}, -1) == Zoom{Zoom::kMin});

    // A zoom out of range is CLAMPED, never refused: a zoom arrives from a wheel gesture and from a
    // restored settings file as well as from a menu, and a viewer that refused would be a black
    // window where a clamped one is a usable page.
    controller.setZoom(Zoom{1000.0});
    CHECK(controller.zoom() == Zoom{Zoom::kMax});
    controller.setZoom(Zoom{0.0001});
    CHECK(controller.zoom() == Zoom{Zoom::kMin});
}

TEST_CASE("stepping the zoom clears the fit mode", "[viewport]")
{
    ui::ViewportController controller;
    controller.setFitMode(ui::FitMode::Width);
    REQUIRE(controller.fitMode() == ui::FitMode::Width);

    // The user chose a number, so the view is no longer fitting anything. A viewer that kept the
    // mode would snap back to fit-width on the next resize and lose the zoom that was just asked
    // for -- which reads as the zoom command not working.
    controller.zoomIn();
    CHECK(controller.fitMode() == ui::FitMode::None);
}

// =============================================================================================
// The fit arithmetic -- `PageLayoutEngine`, because a fit needs to know how big the page is.
// =============================================================================================

TEST_CASE("fit width and fit page account for the margins", "[viewport]")
{
    const core::PageTree tree = makeTree(3);

    ui::PageLayoutEngine engine;
    engine.setDocument(&tree);

    core::render::PresentationOptions options;
    options.margin = 12.0;
    engine.setOptions(options);

    const ui::ScreenMetrics metrics = nominalMetrics();          // 96 DPI
    const core::DeviceSize viewport{800.0, 600.0};

    const Zoom width = engine.zoomForFit(ui::FitMode::Width, viewport, 0, metrics, Zoom{1.0});

    // At 96 DPI an A4 page is `595.276 * 96/72` device pixels wide. Fitting 800 minus two 12-pixel
    // margins is that width over the available space -- computed here from the same inputs rather
    // than copied from the implementation, so the case is a check and not a restatement.
    const double expectedWidth = (800.0 - 24.0) / (kA4Width * (96.0 / 72.0));
    CHECK(nearly(width.factor, expectedWidth, 1e-9));

    // FIT PAGE TAKES THE SMALLER of the two, so the whole page is inside the viewport. Taking the
    // larger fits one axis and crops the other, which is what "fit page" is defined not to do.
    const Zoom page = engine.zoomForFit(ui::FitMode::Page, viewport, 0, metrics, Zoom{1.0});
    const double expectedHeight = (600.0 - 24.0) / (kA4Height * (96.0 / 72.0));
    CHECK(nearly(page.factor, std::min(expectedWidth, expectedHeight), 1e-9));
    CHECK(page.factor < width.factor);

    // `None` returns what it was handed, so a caller may pass it unconditionally.
    CHECK(engine.zoomForFit(ui::FitMode::None, viewport, 0, metrics, Zoom{0.37}) == Zoom{0.37});
}

TEST_CASE("a fit is computed against the PHYSICAL dpi, not a nominal 96", "[viewport]")
{
    // THE T12 PAYOFF, and it is a real difference rather than a tidier abstraction. The P-1 rig's
    // 4K panel is 163 DPI of glass driven at 150 %; the nominal answer for that monitor is 144. A
    // fit-width computed against 144 puts the page 13 % wider than the window.
    const core::PageTree tree = makeTree(1);
    ui::PageLayoutEngine engine;
    engine.setDocument(&tree);

    ui::ScreenMetrics nominal;
    nominal.scale = core::render::MonitorScale{1.5};
    nominal.physical = core::render::PhysicalDpi::nominal(nominal.scale);   // 144

    ui::ScreenMetrics real = nominal;
    real.physical = core::render::PhysicalDpi{163.0};
    real.source = ui::ScreenMetrics::Source::Topology;

    const core::DeviceSize viewport{2400.0, 1400.0};
    const Zoom withNominal = engine.zoomForFit(ui::FitMode::Width, viewport, 0, nominal, Zoom{1.0});
    const Zoom withReal = engine.zoomForFit(ui::FitMode::Width, viewport, 0, real, Zoom{1.0});

    CHECK(withReal.factor < withNominal.factor);
    CHECK(nearly(withNominal.factor / withReal.factor, 163.0 / 144.0, 1e-9));
}

TEST_CASE("a fit refuses to divide by zero and returns what it was given", "[viewport]")
{
    const core::PageTree tree = makeTree(1);
    ui::PageLayoutEngine engine;
    engine.setDocument(&tree);

    const ui::ScreenMetrics metrics = nominalMetrics();

    // A viewport with no area, and a page index that does not exist. Both are ordinary states --
    // a window being created, a document being replaced -- and neither may produce an infinity
    // that propagates into `DpiPolicy` as a rasterization resolution.
    CHECK(engine.zoomForFit(ui::FitMode::Width, core::DeviceSize{0.0, 0.0}, 0, metrics, Zoom{1.0})
          == Zoom{1.0});
    CHECK(engine.zoomForFit(ui::FitMode::Width, core::DeviceSize{800.0, 600.0}, 40, metrics,
                            Zoom{1.0})
          == Zoom{1.0});

    // And with no document at all.
    ui::PageLayoutEngine empty;
    CHECK(empty.zoomForFit(ui::FitMode::Page, core::DeviceSize{800.0, 600.0}, 0, metrics, Zoom{2.0})
          == Zoom{2.0});
}

// =============================================================================================
// Which page the user is looking at -- by AREA, not by which one starts highest.
// =============================================================================================

TEST_CASE("the dominant page is the one covering most of the viewport", "[viewport]")
{
    const core::PageTree tree = makeTree(5);
    ui::PageLayoutEngine engine;
    engine.setDocument(&tree);

    REQUIRE(engine.update(Zoom{1.0}, nominalMetrics(), core::render::LayoutMode::SingleContinuous,
                          0));

    const core::render::PageSlot* second = engine.layout().slotForPage(1);
    REQUIRE(second != nullptr);

    // A viewport whose top edge is one pixel below page 1's top: page 0 contributes a single row of
    // pixels and page 1 contributes the rest. The topmost INTERSECTING page is page 0, and it is
    // the wrong answer -- which is the whole reason this is an area computation and not a `front()`.
    const core::DeviceRect viewport{core::DevicePoint{0.0, second->rect.top() - 1.0},
                                    core::DeviceSize{800.0, 600.0}};
    CHECK(engine.dominantPage(viewport) == 1);

    // Above the first page there is still a page under most of the viewport.
    CHECK(engine.dominantPage(core::DeviceRect{core::DevicePoint{0.0, 0.0},
                                               core::DeviceSize{800.0, 600.0}})
          == 0);

    // Entirely past the end: nothing, reported as -1 rather than as page 0.
    CHECK(engine.dominantPage(core::DeviceRect{core::DevicePoint{0.0, 9e6},
                                               core::DeviceSize{800.0, 600.0}})
          == -1);
}

// =============================================================================================
// The view history behind Alt+Left / Alt+Right.
// =============================================================================================

TEST_CASE("the view history records deliberate navigation and not scrolling", "[viewport]")
{
    ui::ViewportController controller;
    controller.setViewportSize(core::DeviceSize{800.0, 600.0});
    controller.setContentSize(core::DeviceSize{800.0, 9000.0});
    controller.setPageCount(10);

    CHECK_FALSE(controller.canGoBack());
    CHECK_FALSE(controller.canGoForward());

    controller.setCurrentPage(0);
    controller.recordView();

    controller.setCurrentPage(4);
    controller.setScroll(core::DevicePoint{0.0, 3000.0});
    controller.recordView();

    // A SCROLL RECORDS NOTHING. A history that captured every wheel notch is a Back button that
    // goes back one notch, which is the same as no Back button at all -- so this is asserted rather
    // than described.
    controller.scrollBy(0.0, 40.0);
    controller.scrollBy(0.0, 40.0);
    CHECK(controller.history().size() == 2);

    REQUIRE(controller.canGoBack());
    controller.goBack();
    CHECK(controller.currentPage() == 0);
    CHECK(controller.scroll().y == 0.0);
    CHECK(controller.canGoForward());

    controller.goForward();
    CHECK(controller.currentPage() == 4);
    CHECK(controller.scroll().y == 3000.0);

    // A new destination truncates the forward branch, which is what every Back/Forward pair does.
    controller.goBack();
    controller.setCurrentPage(8);
    controller.recordView();
    CHECK_FALSE(controller.canGoForward());
    CHECK(controller.history().size() == 2);
}

TEST_CASE("recording the same view twice does not grow the history", "[viewport]")
{
    ui::ViewportController controller;
    controller.setPageCount(3);
    controller.recordView();
    controller.recordView();
    controller.recordView();

    // Otherwise Back appears to do nothing for as many presses as the same page was visited,
    // which is indistinguishable from a broken Back button.
    CHECK(controller.history().size() == 1);
}

TEST_CASE("a shorter document pulls the current page back with it", "[viewport]")
{
    ui::ViewportController controller;
    controller.setPageCount(50);
    controller.setCurrentPage(40);
    REQUIRE(controller.currentPage() == 40);

    // Page 40 of a document that now has 3 would make every later lookup answer null and the view
    // would go blank with nothing reporting a failure.
    controller.setPageCount(3);
    CHECK(controller.currentPage() == 2);

    controller.setPageCount(0);
    CHECK(controller.currentPage() == 0);
}
