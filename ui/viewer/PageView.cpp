#include "ui/viewer/PageView.h"

#include "core/Log.h"
#include "core/cmd/KeyContext.h"
#include "core/doc/PageTree.h"
#include "core/render/RenderScheduler.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/TilePresenter.h"

#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;

// One wheel notch is 120 eighths of a degree, which is Qt's unit and not a number worth
// rediscovering at the call site.
constexpr double kWheelNotch = 120.0;

} // namespace

PageView::PageView(core::render::RenderScheduler& scheduler,
                   const platform::IDisplayTopology* topology, QWidget* parent)
    : QAbstractScrollArea(parent), scheduler_(scheduler), topology_(topology)
{
    setObjectName(QStringLiteral("pageView"));
    setFrameShape(QFrame::NoFrame);

    // THE FIRST DECLARED CONTEXT IN THE PRODUCT (D2-8). Everything above this widget in the focus
    // chain still resolves to `Global`; everything at or below it now resolves to `Viewer`, which
    // is what makes `Features.md` §8's twelve deliberate collisions mean anything at all.
    ShortcutBinder::declareContext(this, core::KeyContext::Viewer);

    // Focusable by click AND by tab, because a viewer that cannot be reached from the keyboard is
    // a viewer whose entire §8 table is unreachable -- and keyboard operability is one of the
    // things explicitly KEPT when accessibility left the product's scope.
    setFocusPolicy(Qt::StrongFocus);

    controller_ = new ViewportController(this);
    presenter_ = std::make_unique<TilePresenter>(scheduler_, this);

    connect(controller_, &ViewportController::changed, this, &PageView::refresh);

    // A tile arriving repaints and nothing else. It must NOT re-request: `requestTiles` is what
    // produced the tile, and asking again inside the delivery would be a loop that renders the
    // document for as long as anything is left to render.
    connect(presenter_.get(), &TilePresenter::tilesArrived, this,
            [this] { viewport()->update(); });

    viewport()->setAutoFillBackground(true);
}

PageView::~PageView()
{
    // The presenter goes first and explicitly: its destructor unsubscribes from the scheduler and
    // cancels every ticket. Leaving it to the member's own destruction order would work today and
    // is written out because guide case A-39 is precisely *a `PageView` destroyed with tiles in
    // flight*, and the ordering is the thing that case is about.
    presenter_.reset();
}

void PageView::setDocument(core::DocumentHandle document, core::Revision revision,
                           const core::PageTree* pages)
{
    document_ = document;
    revision_ = revision;
    pages_ = pages;

    engine_.setDocument(pages);
    presenter_->setDocument(document, revision, pages);

    controller_->setPageCount(engine_.pageCount());
    controller_->setCurrentPage(0);
    controller_->setScroll(core::DevicePoint{0.0, 0.0});

    refresh();
}

int PageView::pageCount() const
{
    return pages_ != nullptr ? pages_->count() : 0;
}

ScreenMetrics PageView::screenMetrics() const
{
    return metricsFor(screen(), topology_);
}

void PageView::setLayoutMode(core::render::LayoutMode mode)
{
    controller_->setLayoutMode(mode);
}

void PageView::setPresentationOptions(core::render::PresentationOptions options)
{
    engine_.setOptions(options);
    refresh();
}

void PageView::setFitMode(FitMode fit)
{
    controller_->setFitMode(fit);
}

void PageView::goToPage(int pageIndex)
{
    controller_->setCurrentPage(pageIndex);

    if (const core::render::PageSlot* slot = engine_.layout().slotForPage(pageIndex)) {
        // The page's top, minus the margin, so the page does not sit flush against the top edge.
        controller_->setScroll(core::DevicePoint{controller_->scroll().x,
                                                slot->rect.top() - engine_.options().margin});
    }

    // A DELIBERATE navigation, so it is recorded. A scroll is not -- see
    // `ViewportController::recordView`, which is where the distinction is argued.
    controller_->recordView();
    refresh();
}

core::DeviceSize PageView::viewportSizeInDevicePixels() const
{
    const double dpr = devicePixelRatioF();
    return core::DeviceSize{viewport()->width() * dpr, viewport()->height() * dpr};
}

void PageView::refresh()
{
    if (syncing_ || refreshing_) return;
    refreshing_ = true;

    const ScreenMetrics metrics = screenMetrics();

    controller_->setViewportSize(viewportSizeInDevicePixels());

    // THE FIT IS RE-SATISFIED HERE, not remembered as a number. A window resize changes what "fit
    // width" means, and a viewer that stored the zoom would stop fitting the moment the window
    // moved -- which is the whole point of a fit being a mode rather than a value.
    if (controller_->fitMode() != FitMode::None) {
        const core::render::Zoom fitted =
            engine_.zoomForFit(controller_->fitMode(), controller_->viewportSize(),
                               controller_->currentPage(), metrics, controller_->zoom());

        // Written through the private path so the mode is not cleared: `setZoom` keeps the mode,
        // `zoomIn`/`zoomOut`/`zoomAbout` clear it, because those are the user choosing a number.
        if (fitted != controller_->zoom()) {
            const FitMode mode = controller_->fitMode();
            controller_->setZoom(fitted);
            controller_->setFitMode(mode);
        }
    }

    engine_.update(controller_->zoom(), metrics, controller_->layoutMode(),
                   controller_->currentPage());

    controller_->setContentSize(engine_.layout().contentSize());

    updateScrollBars();

    presenter_->setDocument(document_, revision_, pages_);
    presenter_->requestTiles(engine_.layout(), engine_.resolution(), controller_->visibleRect());

    // The page the user is actually looking at, by AREA rather than by which one starts highest --
    // see `PageLayoutEngine::dominantPage`. T14's status bar is the consumer.
    if (const int dominant = engine_.dominantPage(controller_->visibleRect()); dominant >= 0)
        controller_->setCurrentPage(dominant);

    refreshing_ = false;

    emit viewChanged(controller_->currentPage(), pageCount(), controller_->zoom().factor);
    viewport()->update();
}

void PageView::updateScrollBars()
{
    const double dpr = devicePixelRatioF();
    const core::DeviceSize content = controller_->contentSize();
    const core::DeviceSize view = controller_->viewportSize();

    // LOGICAL pixels: a scrollbar is a widget, and Qt lays widgets out in logical pixels. The
    // content is in device pixels, so both the range and the position are divided -- and the
    // position is set inside `syncing_` because writing it emits `scrollContentsBy`.
    const int maxX = static_cast<int>(std::max(0.0, (content.width - view.width) / dpr));
    const int maxY = static_cast<int>(std::max(0.0, (content.height - view.height) / dpr));

    syncing_ = true;
    horizontalScrollBar()->setRange(0, maxX);
    verticalScrollBar()->setRange(0, maxY);
    horizontalScrollBar()->setPageStep(viewport()->width());
    verticalScrollBar()->setPageStep(viewport()->height());

    // A single step is a comfortable scroll rather than one pixel: three of them is roughly a
    // text line at 100 %.
    horizontalScrollBar()->setSingleStep(24);
    verticalScrollBar()->setSingleStep(24);

    horizontalScrollBar()->setValue(static_cast<int>(controller_->scroll().x / dpr));
    verticalScrollBar()->setValue(static_cast<int>(controller_->scroll().y / dpr));
    syncing_ = false;
}

void PageView::syncControllerFromScrollBars()
{
    const double dpr = devicePixelRatioF();
    controller_->setScroll(core::DevicePoint{horizontalScrollBar()->value() * dpr,
                                            verticalScrollBar()->value() * dpr});
}

void PageView::scrollContentsBy(int dx, int dy)
{
    QAbstractScrollArea::scrollContentsBy(dx, dy);
    if (syncing_) return;

    syncControllerFromScrollBars();
}

void PageView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    refresh();
}

void PageView::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const double notches = event->angleDelta().y() / kWheelNotch;
        if (std::abs(notches) < 0.01) {
            event->accept();
            return;
        }

        const core::render::Zoom target = ViewportController::nextZoomStep(
            controller_->zoom(), notches > 0.0 ? +1 : -1);

        // ABOUT THE CURSOR, in device pixels from the viewport's top-left. Zooming about the
        // centre instead is the difference between magnifying what someone is pointing at and
        // magnifying the middle of the window while what they wanted slides off the edge.
        const double dpr = devicePixelRatioF();
        const QPointF local = event->position();
        controller_->zoomAbout(target, core::DevicePoint{local.x() * dpr, local.y() * dpr});

        event->accept();
        return;
    }

    QAbstractScrollArea::wheelEvent(event);
}

bool PageView::event(QEvent* event)
{
    switch (event->type()) {
    // THE MONITOR CHANGED UNDER US. Qt raises these when the widget moves to another screen or
    // when its screen's scale factor changes mid-session, which is the mixed-DPI drag manual case
    // M-01 walks. The physical DPI is then re-read from `IDisplayTopology`, and `DpiPolicy`
    // decides whether that is a re-render or merely a resample.
    case QEvent::ScreenChangeInternal:
    case QEvent::DevicePixelRatioChange:
    case QEvent::WindowChangeInternal:
        refresh();
        break;
    default:
        break;
    }

    return QAbstractScrollArea::event(event);
}

void PageView::paintEvent(QPaintEvent* event)
{
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // The surround, FROM THE PALETTE rather than a hard-coded grey. `ThemeManager` writes the
    // application palette from its eleven tokens, so taking the desk colour from `QPalette::Window`
    // is taking it from the theme -- and a hard-coded light grey here would be a dark-theme
    // contrast defect in a brand-new widget, which is exactly what I-40 was (convention 20).
    painter.fillRect(event->rect(), palette().color(QPalette::Window));

    if (!document_.isValid() || pages_ == nullptr || pages_->isEmpty()) return;

    presenter_->paint(painter, engine_.layout(), engine_.resolution(), controller_->visibleRect(),
                      devicePixelRatioF());
}

} // namespace rawpage::ui
