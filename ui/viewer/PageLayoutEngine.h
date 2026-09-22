// Realizes `core::render::PagePresentation` geometry for the current document, zoom and monitor.
// Stage 2 T13.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS ADDS OVER `PagePresentation`, WHICH ALREADY LAYS PAGES OUT.
//
// Three things, and none of them is layout arithmetic -- that all stays in `core/render/`, where
// it is pure and where its suites already are.
//
//   1. **It resolves the three inputs `DpiPolicy` needs.** The zoom comes from
//      `ViewportController`; the OS scale factor and the monitor's real glass DPI come from
//      `platform::IDisplayTopology` (Stage 2 T12) matched against the widget's `QScreen`. Before
//      T12 every caller passed `PhysicalDpi::nominal(scale)` -- `96 * scale` -- which is exactly
//      right on a commodity desktop and 9 % wrong on the P-1 rig's 4K panel, whose glass is 163 DPI
//      while `96 * 1.5` says 144.
//   2. **It answers the fit questions**, because a fit needs the page's size and this is the object
//      that holds the `PageTree`. `ViewportController` records the fit INTENT; the zoom that
//      satisfies it is computed here and handed back, so a window resize re-satisfies the intent
//      instead of preserving a stale number.
//   3. **It caches the layout** and says whether recomputing changed it, so a paint that follows a
//      scroll does not re-lay-out a 900-page document.
//
// ---------------------------------------------------------------------------------------------
// THE MONITOR IS MATCHED BY NAME, AND THAT IS THE ONLY JOIN THAT WORKS.
//
// `MonitorInfo` reports virtual-desktop rectangles in PHYSICAL pixels; `QWidget::geometry()` is in
// Qt's logical pixels. On a mixed-DPI desktop those two spaces do not agree, so matching a window
// to a monitor by comparing rectangles is wrong in exactly the configuration the match exists for.
// `QScreen::name()` on Windows is the device name -- `\\.\DISPLAY1` -- and that is what
// `MonitorInfo::id` carries, so the join is an equality on a string rather than a geometric guess.
//
// No match is an ordinary outcome, not a failure: under `offscreen` there is no such monitor, and a
// remote session invents one. The fallback is `QScreen`'s own `physicalDotsPerInch()`, and failing
// that the nominal `96 * scale` -- the same approximation every caller used before T12, and it is
// recorded rather than hidden.

#ifndef RAWPAGE_UI_VIEWER_PAGELAYOUTENGINE_H
#define RAWPAGE_UI_VIEWER_PAGELAYOUTENGINE_H

#include "core/Geometry.h"
#include "core/render/DpiPolicy.h"
#include "core/render/FitPolicy.h"
#include "core/render/PagePresentation.h"
#include "ui/viewer/ViewportController.h"

#include <QString>

#include <functional>
#include <optional>

namespace rawpage::core {
class PageTree;
}

namespace rawpage::platform {
class IDisplayTopology;
}

class QScreen;

namespace rawpage::ui {

// The monitor a view is being drawn on, reduced to the two numbers `DpiPolicy` wants. Named so a
// test can supply one without a monitor, which is what makes the whole engine drivable offscreen.
struct ScreenMetrics {
    core::render::MonitorScale scale;
    core::render::PhysicalDpi physical;

    // How the physical DPI was arrived at, for the log and for a case that wants to assert the
    // fallback was NOT taken on a machine where it should not be.
    enum class Source { Topology, QtScreen, Nominal };
    Source source = Source::Nominal;

    static const char* sourceName(Source source) noexcept;
};

// Resolves the metrics for `screen`. `topology` may be null -- which is what a build with no
// `IDisplayTopology` has, and what a test that wants the fallback passes.
ScreenMetrics metricsFor(const QScreen* screen, const platform::IDisplayTopology* topology);

class PageLayoutEngine {
public:
    PageLayoutEngine() = default;

    // Not owned, and it may be null -- a tab whose document failed to give us a tree still has to
    // lay out, and it lays out nothing.
    void setDocument(const core::PageTree* tree);
    const core::PageTree* document() const noexcept { return tree_; }
    int pageCount() const noexcept;

    void setOptions(core::render::PresentationOptions options);

    // The rendering resolution cap, from `pageDisplay/maxRenderResolution` (S2 T15). It is a
    // `DpiPolicy::Limits` rather than a bare number because the resample tolerance travels with it,
    // and because `DpiPolicy` is the object that owns what a limit MEANS.
    void setDpiLimits(core::render::DpiPolicy::Limits limits) noexcept { limits_ = limits; }
    const core::render::DpiPolicy::Limits& dpiLimits() const noexcept { return limits_; }
    const core::render::PresentationOptions& options() const noexcept
    {
        return presentation_.options();
    }

    // Recomputes the layout for this state. Returns true when the RESULT changed -- not when the
    // inputs did -- so a caller can skip a repaint that would draw the same thing.
    bool update(core::render::Zoom zoom, ScreenMetrics metrics, core::render::LayoutMode mode,
                int currentPage, core::PageRotation viewRotation = core::PageRotation::None);

    // The rotation the cached layout was built with. `TilePresenter` needs it to key its requests,
    // and reading it back from here rather than being handed it again is what stops the layout and
    // the tiles being computed for two different rotations for one frame.
    core::PageRotation viewRotation() const noexcept { return viewRotation_; }

    const core::render::PageLayout& layout() const noexcept { return layout_; }
    const core::render::Resolution& resolution() const noexcept { return resolution_; }

    // WHERE A PAGE'S CONTENT BOX COMES FROM -- S4 T12, D4-14. `ui/` may never name `pdf/`, so the
    // engine is reached through a function the shell installs over `core::IDocumentBackend`
    // (`ui/shell/DocumentWindow::handDocumentTo`). `std::nullopt` means *this page has no objects,
    // or nobody can answer*, and `core/render/FitPolicy` falls back to the CropBox for it -- which
    // is exactly what Fit Visible did for the whole of Stages 2 and 3.
    //
    // Unset by default, so a view with no shell behind it (every widget case that does not care)
    // gets the fallback and not a crash.
    using ContentBoxSource = std::function<std::optional<core::PdfRect>(int pageIndex)>;
    void setContentBoxSource(ContentBoxSource source) { contentBox_ = std::move(source); }
    bool hasContentBoxSource() const noexcept { return static_cast<bool>(contentBox_); }
    std::optional<core::PdfRect> contentBox(int page) const;

    // WHICH RECTANGLE `fit` MEASURES for `page`, in PDF space and BEFORE any rotation.
    // `std::nullopt` when there is no such page. It is public because Fit Visible has to be
    // SCROLLED to as well as zoomed to -- a content box in the middle of a sheet is not on screen
    // until somebody moves the viewport onto it, and `ui/viewer/PageView` is what moves it.
    std::optional<core::PdfRect> fitRect(FitMode fit, int page) const;

    // The zoom that satisfies `fit` for `page` in a viewport of `viewportSize` DEVICE pixels.
    // `FitMode::None` returns `current` unchanged, so a caller may pass it unconditionally.
    //
    // Returns a clamped zoom; a viewport with no area, or a page with none, returns `current`
    // rather than a division by zero or an infinity that would propagate into `DpiPolicy`.
    core::render::Zoom zoomForFit(FitMode fit, core::DeviceSize viewportSize, int page,
                                  ScreenMetrics metrics, core::render::Zoom current) const;

    // The page one SPREAD away from `from`, in `direction` (+1 forward, -1 back) -- and the page
    // step of every paged mode, because in `TwoUp` the page BESIDE the current one is already on
    // the glass. Stepping to it moves nothing, and the dominant-page snap at the end of
    // `PageView::refresh()` then puts the current page back on the spread's first page, so the
    // view can never leave the spread it is on. That was S2 issue I-25, and this is the call
    // `PagePresentation::spreadFor()` was exposed for at T13 and never given.
    //
    // Returns a page index that may be OUT OF RANGE at either end -- `PageView::goToPage()` clamps,
    // and a caller that compares the resulting current page against the one it started on is how
    // "the document ends here" is told from "the page turned".
    int spreadStep(int from, int direction, core::render::LayoutMode mode) const;

    // The page whose slot covers the most of `viewport`, or -1 when none does. This is what makes
    // the status bar say "page 7 of 900" while scrolling: the topmost intersecting page is the
    // wrong answer when a spread's first page is one row of pixels tall at the top of the screen.
    int dominantPage(const core::DeviceRect& viewport) const;

private:
    const core::PageTree* tree_ = nullptr;
    core::render::PagePresentation presentation_;
    core::render::PageLayout layout_;
    core::render::Resolution resolution_;
    core::PageRotation viewRotation_ = core::PageRotation::None;
    core::render::DpiPolicy::Limits limits_{};
    ContentBoxSource contentBox_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_PAGELAYOUTENGINE_H
