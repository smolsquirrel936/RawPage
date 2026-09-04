#include "ui/viewer/PageLayoutEngine.h"

#include "core/Log.h"
#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "platform/platform.h"

#include <QScreen>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;
using core::render::PhysicalDpi;
using core::render::MonitorScale;
using core::render::Zoom;

// The size, in PDF points, that a fit must make fit. Post-rotation, because a landscape page in a
// portrait document is wider than it is tall and fitting its unrotated box would leave it hanging
// off the right edge.
core::PdfSize fitTarget(const core::Page& page, FitMode fit)
{
    const bool swapped = page.rotation() == core::PageRotation::Clockwise90
                         || page.rotation() == core::PageRotation::Clockwise270;

    // `Page` fits the SHEET and `Visible` fits what is drawn. They agree on a document whose
    // CropBox equals its MediaBox, which is most of them, and they differ on a trimmed page --
    // which is the case that makes them two commands in Features.md 1.2 rather than one.
    const core::PdfSize box =
        (fit == FitMode::Page) ? page.mediaBox().size : page.cropBox().size;

    return swapped ? core::PdfSize{box.height, box.width} : box;
}

} // namespace

const char* ScreenMetrics::sourceName(Source source) noexcept
{
    switch (source) {
    case Source::Topology: return "topology";
    case Source::QtScreen: return "qt-screen";
    case Source::Nominal: return "nominal";
    }
    return "nominal";
}

ScreenMetrics metricsFor(const QScreen* screen, const platform::IDisplayTopology* topology)
{
    ScreenMetrics metrics;

    if (screen == nullptr) {
        // No screen at all. Offscreen builds do have one, so this is the defensive branch rather
        // than the headless one -- and the nominal answer is the honest default.
        return metrics;
    }

    metrics.scale = MonitorScale{screen->devicePixelRatio()};
    metrics.physical = PhysicalDpi::nominal(metrics.scale);
    metrics.source = ScreenMetrics::Source::Nominal;

    if (topology != nullptr) {
        if (const core::Result<std::vector<platform::MonitorInfo>> monitors = topology->monitors()) {
            const QString name = screen->name();
            for (const platform::MonitorInfo& monitor : monitors.value()) {
                // BY NAME. See the header: the rectangles are in two different coordinate spaces
                // on precisely the mixed-DPI desktop this join exists for.
                if (monitor.id != name) continue;

                metrics.scale = MonitorScale{monitor.scaleFactor};
                metrics.physical = PhysicalDpi{static_cast<double>(monitor.physicalDpi)};
                metrics.source = ScreenMetrics::Source::Topology;
                return metrics;
            }
        }
    }

    // Qt's own answer, which is derived from the same EDID and is available with no platform layer
    // at all. It is preferred over the nominal figure and not over the topology's, because Qt
    // reports it for the screen as Qt understands it, which on a mixed-DPI desktop can be the
    // screen the window was created on rather than the one it is now over.
    const double qtDpi = screen->physicalDotsPerInch();
    if (qtDpi >= 48.0 && qtDpi <= 1200.0) {
        metrics.physical = PhysicalDpi{qtDpi};
        metrics.source = ScreenMetrics::Source::QtScreen;
    }

    return metrics;
}

void PageLayoutEngine::setDocument(const core::PageTree* tree)
{
    tree_ = tree;
    layout_ = core::render::PageLayout{};
}

int PageLayoutEngine::pageCount() const noexcept
{
    return tree_ != nullptr ? tree_->count() : 0;
}

void PageLayoutEngine::setOptions(core::render::PresentationOptions options)
{
    presentation_.setOptions(options);
}

bool PageLayoutEngine::update(Zoom zoom, ScreenMetrics metrics, core::render::LayoutMode mode,
                              int currentPage, core::PageRotation viewRotation)
{
    const core::render::DpiPolicy policy;
    const core::render::Resolution resolution =
        policy.resolve(zoom, metrics.scale, metrics.physical);

    const bool rotationMoved = viewRotation != viewRotation_;
    viewRotation_ = viewRotation;

    if (tree_ == nullptr) {
        const bool changed = !layout_.isEmpty() || resolution != resolution_ || rotationMoved;
        layout_ = core::render::PageLayout{};
        resolution_ = resolution;
        return changed;
    }

    core::render::PageLayout next =
        presentation_.layout(*tree_, mode, resolution, currentPage, viewRotation);

    // Compared by RESULT rather than by input. Two different current pages produce the same
    // continuous layout, and a repaint triggered by that difference would re-render the visible
    // tiles of a 900-page document every time the status bar's page number ticked over.
    const bool changed = rotationMoved || resolution != resolution_
                         || next.mode() != layout_.mode()
                         || next.contentSize() != layout_.contentSize()
                         || next.pageSlots().size() != layout_.pageSlots().size()
                         || (!next.pageSlots().empty() && !layout_.pageSlots().empty()
                             && (next.pageSlots().front().pageIndex
                                     != layout_.pageSlots().front().pageIndex
                                 || next.pageSlots().front().rect
                                        != layout_.pageSlots().front().rect));

    layout_ = std::move(next);
    resolution_ = resolution;
    return changed;
}

Zoom PageLayoutEngine::zoomForFit(FitMode fit, core::DeviceSize viewportSize, int page,
                                  ScreenMetrics metrics, Zoom current) const
{
    if (fit == FitMode::None || tree_ == nullptr) return current;

    const core::Page* target = tree_->at(page);
    if (target == nullptr) return current;

    core::PdfSize box = fitTarget(*target, fit);
    if (core::swapsAxes(viewRotation_)) box = core::PdfSize{box.height, box.width};
    if (box.width <= 0.0 || box.height <= 0.0) return current;
    if (viewportSize.width <= 0.0 || viewportSize.height <= 0.0) return current;

    // The margins the presentation will add. A fit that ignored them produces a page one gutter
    // too wide, and the horizontal scrollbar that appears then makes the viewport narrower still --
    // which is a fit that oscillates by one scrollbar width every resize.
    const core::render::PresentationOptions& options = presentation_.options();
    const double chromeX = 2.0 * options.margin;
    const double chromeY = 2.0 * options.margin;

    const double availableWidth = std::max(1.0, viewportSize.width - chromeX);
    const double availableHeight = std::max(1.0, viewportSize.height - chromeY);

    // At zoom 1.0 the page occupies `box * (physicalDpi / 72)` device pixels. The zoom that makes
    // it occupy `available` is therefore the ratio of the two -- and it is computed against the
    // PHYSICAL DPI rather than against a nominal 96, which is the whole reason `DpiPolicy` takes
    // three inputs.
    const double baseScale = metrics.physical.dpi / 72.0;
    const double widthZoom = availableWidth / (box.width * baseScale);
    const double heightZoom = availableHeight / (box.height * baseScale);

    double factor = widthZoom;
    switch (fit) {
    case FitMode::Width:
        factor = widthZoom;
        break;
    case FitMode::Height:
        factor = heightZoom;
        break;
    case FitMode::Page:
    case FitMode::Visible:
        // The SMALLER of the two, so the whole page is inside the viewport. Taking the larger fits
        // one axis and crops the other, which is what "fit page" is defined not to do.
        factor = std::min(widthZoom, heightZoom);
        break;
    case FitMode::None:
        return current;
    }

    if (!std::isfinite(factor) || factor <= 0.0) return current;
    return Zoom{factor}.clamped();
}

int PageLayoutEngine::dominantPage(const core::DeviceRect& viewport) const
{
    const std::vector<core::render::PageSlot> touched = layout_.slotsIntersecting(viewport);
    if (touched.empty()) return -1;

    int best = touched.front().pageIndex;
    double bestArea = -1.0;

    for (const core::render::PageSlot& slot : touched) {
        const double left = std::max(slot.rect.left(), viewport.left());
        const double right = std::min(slot.rect.right(), viewport.right());
        const double top = std::max(slot.rect.top(), viewport.top());
        const double bottom = std::min(slot.rect.bottom(), viewport.bottom());

        const double area = std::max(0.0, right - left) * std::max(0.0, bottom - top);
        if (area > bestArea) {
            bestArea = area;
            best = slot.pageIndex;
        }
    }

    return best;
}

} // namespace rawpage::ui
