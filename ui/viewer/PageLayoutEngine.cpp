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
// I-28 IS CLOSED AND THE ARITHMETIC IS NO LONGER HERE. `core/render/FitPolicy` decides which
// rectangle a fit measures -- `Page` the sheet, `Visible` the CONTENT box with the CropBox as its
// fallback, the rest the CropBox -- and this file's job is now to supply the three rectangles and
// to apply the rotations, which are a view's business rather than a policy's.
//
// Why the move: the observable I-28 names is a number read off a real file, and only
// `rawpage_render_tests` can open one. It links no widgets. See `core/render/FitPolicy.h`.

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
                //
                // AND `QScreen::name()` IS THE MONITOR'S MODEL, NOT ITS DEVICE NAME. Qt 6 on
                // Windows reports what DisplayConfig calls the friendly name -- `MAG 272URDF` --
                // while `MonitorInfo::id` is `GetMonitorInfoW`'s `szDevice`, `\\.\DISPLAY1`.
                // Comparing those two could never be equal, so this branch never once executed in
                // the shipped product and every render silently used the fallback below
                // (**I-20**). `friendlyName` is carried for exactly this join; `id` is still
                // accepted because it is the stable identifier everything else keys on and a
                // platform that reports one and not the other must still match.
                if (monitor.friendlyName != name && monitor.id != name) continue;

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
    //
    // TIMES THE DEVICE PIXEL RATIO, AND THAT FACTOR IS THE WHOLE OF I-20. `physicalDotsPerInch()`
    // divides the panel's millimetres into `QScreen::geometry()`, and Qt's geometry is in
    // DEVICE-INDEPENDENT pixels -- 2560 for a 3840-wide panel at 150 %. So Qt answers 108.89 for
    // glass that measures 163: a DIP dpi wearing the name of a physical one, off by exactly the
    // scale factor. Feeding it to `DpiPolicy` as `PhysicalDpi` rendered Actual Size at two thirds
    // of its size on every scaled monitor, which is what a person beside Acrobat saw in the first
    // minutes of M-03. Multiplying restores the physical figure: 108.89 x 1.5 = 163.3 against the
    // topology's measured 163, and 108.80 x 1.0 = 108.8 against its 108.
    //
    // This is I-07's mistake at one remove -- there a rectangle documented as device-independent
    // was physical, here a dpi that IS device-independent was read as physical -- and it is why
    // `ScreenMetrics` carries its `Source` at all: the three answers must stay distinguishable,
    // because they disagree precisely where it matters and agree on the desktop everyone develops
    // on, where `physicalDpi == 96 * scale` and the ratio is 1.
    const double qtDpi = screen->physicalDotsPerInch() * metrics.scale.factor;
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
    const core::render::DpiPolicy policy(limits_);
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

std::optional<core::PdfRect> PageLayoutEngine::contentBox(int page) const
{
    if (!contentBox_) return std::nullopt;
    return contentBox_(page);
}

std::optional<core::PdfRect> PageLayoutEngine::fitRect(FitMode fit, int page) const
{
    if (tree_ == nullptr) return std::nullopt;
    const core::Page* target = tree_->at(page);
    if (target == nullptr) return std::nullopt;

    // **THE CONTENT BOX IS ASKED FOR ONLY WHEN A FIT WANTS IT.** `contentBox_` reaches the engine
    // through the backend, which loads the page and walks its objects; asking on every fit would
    // do that work for Fit Width, which does not use the answer.
    core::render::PageBoxes boxes;
    boxes.media = target->mediaBox();
    boxes.crop = target->cropBox();
    if (fit == FitMode::Visible) boxes.content = contentBox(page);

    return core::render::fitRect(boxes, fit);
}

Zoom PageLayoutEngine::zoomForFit(FitMode fit, core::DeviceSize viewportSize, int page,
                                  ScreenMetrics metrics, Zoom current) const
{
    if (fit == FitMode::None || tree_ == nullptr) return current;

    const core::Page* target = tree_->at(page);
    if (target == nullptr) return current;

    const std::optional<core::PdfRect> rect = fitRect(fit, page);
    if (!rect.has_value()) return current;

    // TWO ROTATIONS, AND BOTH ARE THE VIEW'S BUSINESS rather than the policy's. The PAGE's own
    // `/Rotate` decides whether a portrait box is presented landscape, and the SESSION's view
    // rotation turns it again on top of that -- `core/render/FitPolicy` is handed neither, because
    // a rectangle in PDF space does not know how anybody intends to show it.
    core::PdfSize box = rect->size;
    const bool swapped = core::swapsAxes(target->rotation()) != core::swapsAxes(viewRotation_);
    if (swapped) box = core::PdfSize{box.height, box.width};

    return core::render::zoomToFit(fit, box, viewportSize, presentation_.options().margin,
                                   metrics.physical, current);
}

int PageLayoutEngine::spreadStep(int from, int direction, core::render::LayoutMode mode) const
{
    const int count = pageCount();
    if (count <= 0) return 0;

    const int clamped = std::clamp(from, 0, count - 1);

    // THE EXTENT OF THE SPREAD, not the page. `spreadFor()` is the single place the cover's parity
    // and an odd page count are resolved -- `PagePresentation.cpp` says so of itself -- so asking
    // it is what keeps this step right for a cover that sits alone and for a last page with no
    // partner. In the single modes it answers one page and this is `from + direction`.
    int lowest = clamped;
    int highest = clamped;
    for (const int index : presentation_.spreadFor(clamped, count, mode)) {
        if (index < 0) continue;   // the empty column of a half-spread
        lowest = std::min(lowest, index);
        highest = std::max(highest, index);
    }

    return direction >= 0 ? highest + 1 : lowest - 1;
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
