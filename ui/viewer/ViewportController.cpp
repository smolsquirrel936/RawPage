#include "ui/viewer/ViewportController.h"

#include <algorithm>
#include <cmath>

namespace rawpage::ui {
namespace {

using core::render::Zoom;

// Features.md 1.2's preset ladder. The rungs are the values the zoom box offers; `zoomIn` and
// `zoomOut` walk them, and an arbitrary zoom between two rungs steps to the next one in the
// direction asked rather than to the nearest, so a press always moves.
constexpr double kZoomSteps[] = {0.08, 0.125, 0.25, 0.333333, 0.50, 0.6667, 0.75, 1.00,
                                 1.25, 1.50,  2.00, 3.00,     4.00, 6.00,   8.00, 12.00,
                                 16.00, 24.00, 32.00, 48.00,  64.00};

constexpr double kEpsilon = 1e-9;

double clampTo(double value, double low, double high) noexcept
{
    return value < low ? low : (value > high ? high : value);
}

} // namespace

ViewportController::ViewportController(QObject* parent) : QObject(parent) {}

void ViewportController::setViewportSize(core::DeviceSize size)
{
    if (size == viewport_) return;
    viewport_ = size;
    clampScroll();
    emitChanged();
}

void ViewportController::setContentSize(core::DeviceSize size)
{
    if (size == content_) return;
    content_ = size;

    // RE-CLAMPED, not preserved. A zoom out makes the content shorter than the current offset, and
    // an offset past the end shows an empty grey band below the last page with no way to scroll
    // back -- the scrollbar being already at its maximum.
    clampScroll();
    emitChanged();
}

void ViewportController::setZoom(Zoom zoom)
{
    const Zoom clamped = zoom.clamped();
    if (clamped == zoom_) return;
    zoom_ = clamped;
    emitChanged();
}

Zoom ViewportController::nextZoomStep(Zoom from, int direction) noexcept
{
    const double current = from.clamped().factor;
    if (direction > 0) {
        for (const double step : kZoomSteps)
            if (step > current + kEpsilon) return Zoom{step};
        return Zoom{Zoom::kMax};
    }

    for (int i = static_cast<int>(std::size(kZoomSteps)) - 1; i >= 0; --i)
        if (kZoomSteps[i] < current - kEpsilon) return Zoom{kZoomSteps[i]};
    return Zoom{Zoom::kMin};
}

void ViewportController::zoomIn()
{
    fit_ = FitMode::None;
    setZoom(nextZoomStep(zoom_, +1));
}

void ViewportController::zoomOut()
{
    fit_ = FitMode::None;
    setZoom(nextZoomStep(zoom_, -1));
}

void ViewportController::zoomAbout(Zoom target, core::DevicePoint anchorInViewport)
{
    const Zoom clamped = target.clamped();
    const double ratio = clamped.factor / (zoom_.factor > 0.0 ? zoom_.factor : 1.0);
    if (std::abs(ratio - 1.0) < kEpsilon) return;

    // The content point currently under the anchor, moved to where the same content will be after
    // the scale change, then pulled back so it lands under the anchor again. Nothing here reads a
    // page or a layout: the content space scales uniformly, which is what makes this exact.
    const double contentX = scroll_.x + anchorInViewport.x;
    const double contentY = scroll_.y + anchorInViewport.y;

    zoom_ = clamped;
    fit_ = FitMode::None;

    // The content size scales with the zoom, and the caller has not recomputed the layout yet --
    // so it is scaled here rather than waited for. Doing it in the other order clamps against the
    // OLD extent, which pins the anchor to the bottom of the document at every zoom-in near the
    // end of a file.
    content_ = core::DeviceSize{content_.width * ratio, content_.height * ratio};

    scroll_ = core::DevicePoint{contentX * ratio - anchorInViewport.x,
                                contentY * ratio - anchorInViewport.y};
    clampScroll();
    emitChanged();
}

void ViewportController::setFitMode(FitMode mode)
{
    if (mode == fit_) return;
    fit_ = mode;
    emitChanged();
}

void ViewportController::setScroll(core::DevicePoint offset)
{
    const core::DevicePoint before = scroll_;
    scroll_ = offset;
    clampScroll();
    if (scroll_ == before) return;
    emitChanged();
}

void ViewportController::scrollBy(double dx, double dy)
{
    setScroll(core::DevicePoint{scroll_.x + dx, scroll_.y + dy});
}

core::DeviceRect ViewportController::visibleRect() const noexcept
{
    return core::DeviceRect{scroll_, viewport_};
}

void ViewportController::setLayoutMode(core::render::LayoutMode mode)
{
    if (mode == mode_) return;
    mode_ = mode;
    emitChanged();
}

void ViewportController::setViewRotation(core::PageRotation rotation)
{
    if (rotation == viewRotation_) return;
    viewRotation_ = rotation;
    emitChanged();
}

void ViewportController::rotateView(int quarterTurns)
{
    setViewRotation(core::rotateBy(viewRotation_, quarterTurns));
}

void ViewportController::setPageCount(int count)
{
    const int clamped = std::max(0, count);
    if (clamped == pageCount_) return;
    pageCount_ = clamped;

    // A shorter document pulls the current page back with it. Left alone, page 40 of a document
    // that now has 3 would make every later lookup answer null and the view would go blank with
    // nothing reporting a failure.
    currentPage_ = pageCount_ == 0 ? 0 : std::min(currentPage_, pageCount_ - 1);
    emitChanged();
}

void ViewportController::setCurrentPage(int page)
{
    const int clamped = pageCount_ == 0 ? 0 : static_cast<int>(clampTo(page, 0.0, pageCount_ - 1.0));
    if (clamped == currentPage_) return;
    currentPage_ = clamped;
    emitChanged();
}

void ViewportController::clampScroll()
{
    // A document smaller than the viewport does not scroll at all, and the maximum is zero rather
    // than negative -- a negative maximum would let the page drift up off the top edge, which reads
    // as content that cannot be reached.
    const double maxX = std::max(0.0, content_.width - viewport_.width);
    const double maxY = std::max(0.0, content_.height - viewport_.height);
    scroll_ = core::DevicePoint{clampTo(scroll_.x, 0.0, maxX), clampTo(scroll_.y, 0.0, maxY)};
}

ViewportController::ViewState ViewportController::currentView() const
{
    return ViewState{currentPage_, zoom_, scroll_};
}

void ViewportController::recordView()
{
    if (navigating_) return;

    const ViewState state = currentView();
    if (historyIndex_ >= 0 && history_[static_cast<std::size_t>(historyIndex_)] == state) return;

    // A new destination truncates the forward branch, which is what every Back/Forward pair in
    // every application does and what a user expects: going back three pages and then somewhere
    // new does not leave the old forward path reachable.
    history_.resize(static_cast<std::size_t>(historyIndex_ + 1));
    history_.push_back(state);
    historyIndex_ = static_cast<int>(history_.size()) - 1;
}

bool ViewportController::canGoForward() const noexcept
{
    return historyIndex_ >= 0 && historyIndex_ + 1 < static_cast<int>(history_.size());
}

void ViewportController::goBack()
{
    if (!canGoBack()) return;
    --historyIndex_;

    const ViewState& state = history_[static_cast<std::size_t>(historyIndex_)];
    navigating_ = true;
    currentPage_ = state.page;
    zoom_ = state.zoom;
    scroll_ = state.scroll;
    fit_ = FitMode::None;
    clampScroll();
    navigating_ = false;
    emitChanged();
}

void ViewportController::goForward()
{
    if (!canGoForward()) return;
    ++historyIndex_;

    const ViewState& state = history_[static_cast<std::size_t>(historyIndex_)];
    navigating_ = true;
    currentPage_ = state.page;
    zoom_ = state.zoom;
    scroll_ = state.scroll;
    fit_ = FitMode::None;
    clampScroll();
    navigating_ = false;
    emitChanged();
}

void ViewportController::emitChanged()
{
    emit changed();
}

} // namespace rawpage::ui
