#include "ui/viewer/PageView.h"

#include "core/Log.h"
#include "core/cmd/KeyContext.h"
#include "core/doc/PageTree.h"
#include "core/render/RenderScheduler.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/TilePresenter.h"

#include <QKeyEvent>
#include <QMouseEvent>
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

// A screen step moves this fraction of a viewport, so the line that was at the bottom edge is still
// on screen afterwards. Every document reader overlaps for this reason; a step of exactly 1.0 loses
// a line per press and the loss is invisible until somebody is actually reading.
constexpr double kScreenOverlap = 0.92;

// One arrow press, in LOGICAL pixels, and the same number `updateScrollBars` gives the scrollbars
// as their single step -- a key and a click on a scrollbar arrow must move the page by the same
// amount or one of them feels broken.
constexpr int kArrowStep = 24;

// Below this, a marquee drag is a click. Zooming to a two-pixel band would jump to 6400 % on a
// mis-click with no obvious way back.
constexpr int kMinMarqueePixels = 8;

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

core::render::RenderFlags PageView::renderFlags() const noexcept
{
    using core::render::RenderFlags;

    // INVERTED, and deliberately so at this boundary rather than in the settings table. The
    // preference is *smooth text*, which is what a person turns on; the engine's flag is
    // *no smooth text*, which is what PDFium understands. Naming the setting after the flag would
    // put a double negative in the Preferences dialog.
    RenderFlags flags = RenderFlags::Annotations;
    if (!settings_.antialiasText) flags = flags | RenderFlags::NoSmoothText;
    if (!settings_.antialiasImages) flags = flags | RenderFlags::NoSmoothImage;
    if (!settings_.antialiasLineArt) flags = flags | RenderFlags::NoSmoothPath;

    // NOT INVERTED, unlike the three above: the preference and the flag agree. Showing the grid
    // requires the page to be rendered onto transparency, or the rasterizer's white fill covers the
    // checkerboard and the option does nothing at all.
    if (settings_.transparencyGrid) flags = flags | RenderFlags::TransparentBackground;
    return flags;
}

void PageView::applySettings(const ViewSettings& settings)
{
    const ViewSettings previous = settings_;
    settings_ = settings;

    engine_.setDpiLimits([&] {
        core::render::DpiPolicy::Limits limits;
        limits.maxRenderDpi = static_cast<double>(settings_.maxRenderDpi);
        return limits;
    }());

    core::render::PresentationOptions options = engine_.options();
    options.showCoverInTwoUp = settings_.showCoverInTwoUp;
    options.rtlBinding = settings_.rtlBinding;
    engine_.setOptions(options);

    if (presenter_) {
        presenter_->setRenderFlags(renderFlags());
        presenter_->setPrefetchPages(settings_.prefetchPages);
    }

    // ONLY ON THE FIRST APPLY. A default is what a document OPENS at; re-applying it on every
    // change to the group would yank a person's layout back to Single Continuous because they went
    // to Preferences to turn off image smoothing.
    if (!settingsApplied_) {
        settingsApplied_ = true;
        controller_->setLayoutMode(settings_.layoutMode);
        controller_->setFitMode(settings_.defaultZoom);
    }

    // A CHANGE TO THE RENDER FLAGS INVALIDATES NOTHING AUTOMATICALLY, because the flags are part of
    // `RenderRequest`'s key -- so the new flags simply miss the cache and the old tiles age out.
    // What has to happen is a re-request, which is what `refresh()` does. The old tiles are dropped
    // explicitly rather than left to the LRU: they are now unreachable by any request this view
    // will make, so keeping them is holding memory nothing can ever hit.
    if (presenter_ && previous.antialiasText != settings_.antialiasText) presenter_->clear();
    else if (presenter_ && previous.antialiasImages != settings_.antialiasImages) presenter_->clear();
    else if (presenter_ && previous.antialiasLineArt != settings_.antialiasLineArt) presenter_->clear();
    else if (presenter_ && previous.transparencyGrid != settings_.transparencyGrid) presenter_->clear();

    refresh();
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

void PageView::zoomIn()
{
    controller_->zoomIn();
}

void PageView::zoomOut()
{
    controller_->zoomOut();
}

void PageView::setZoom(core::render::Zoom zoom)
{
    controller_->setFitMode(FitMode::None);
    controller_->setZoom(zoom);
}

void PageView::firstPage()
{
    goToPage(0);
}

void PageView::lastPage()
{
    goToPage(pageCount() - 1);
}

void PageView::nextPage()
{
    goToPage(controller_->currentPage() + 1);
}

void PageView::prevPage()
{
    goToPage(controller_->currentPage() - 1);
}

void PageView::nextScreen()
{
    if (!core::render::isContinuous(controller_->layoutMode())) {
        // A PAGED MODE HAS NOTHING BELOW THE CURRENT PAGE. Scrolling would move by a viewport into
        // empty space and stop, which reads as `Page Down` being broken on the last screenful --
        // so in Single and Two-Up the screen step IS the page step.
        nextPage();
        return;
    }

    // A viewport less an overlap. The overlap is not decoration: a screen step of exactly one
    // viewport puts the line that was at the bottom edge one pixel above the top edge, and a reader
    // loses it. Every document reader in existence overlaps for this reason.
    const double step = std::max(1.0, controller_->viewportSize().height * kScreenOverlap);
    controller_->scrollBy(0.0, step);
}

void PageView::prevScreen()
{
    if (!core::render::isContinuous(controller_->layoutMode())) {
        prevPage();
        return;
    }
    const double step = std::max(1.0, controller_->viewportSize().height * kScreenOverlap);
    controller_->scrollBy(0.0, -step);
}

bool PageView::scrollOrTurnPage(double dx, double dy)
{
    const core::DevicePoint before = controller_->scroll();
    controller_->scrollBy(dx, dy);
    if (controller_->scroll() != before) return true;

    // NOTHING MOVED, so we are against a limit. `ViewportController::setScroll` clamps, which is
    // what makes "did not move" the reliable test rather than comparing against a computed
    // maximum that the layout, the zoom and the device pixel ratio all get a vote in.
    if (core::render::isContinuous(controller_->layoutMode())) return false;

    const bool forward = dy > 0.0 || dx > 0.0;
    const int from = controller_->currentPage();
    goToPage(from + (forward ? 1 : -1));
    if (controller_->currentPage() == from) return false;   // the document ended; leave the event

    // WHERE THE NEW PAGE LANDS. `goToPage` puts its top edge under the viewport, which is right
    // going forward and wrong coming back: `Up` at the top of page 5 would show the top of page 4
    // and read as a jump backwards through everything the reader just passed. Coming back lands at
    // the FAR edge, and the huge value is deliberate -- `setScroll` clamps, so this asks for "as
    // far as this page goes" without computing it.
    constexpr double kPastAnyLayout = 1.0e9;
    if (!forward) {
        controller_->setScroll(dx != 0.0
                                   ? core::DevicePoint{kPastAnyLayout, controller_->scroll().y}
                                   : core::DevicePoint{controller_->scroll().x, kPastAnyLayout});
    } else if (dx != 0.0) {
        controller_->setScroll(core::DevicePoint{0.0, controller_->scroll().y});
    }
    return true;
}

void PageView::goBackView()
{
    controller_->goBack();
}

void PageView::goForwardView()
{
    controller_->goForward();
}

void PageView::rotateViewClockwise()
{
    controller_->rotateView(+1);
}

void PageView::rotateViewCounterClockwise()
{
    controller_->rotateView(-1);
}

core::PageRotation PageView::viewRotation() const
{
    return controller_->viewRotation();
}

void PageView::setTool(Tool tool)
{
    if (tool == tool_) return;
    tool_ = tool;

    // The cursor IS the feedback at this stage -- there is no tool palette until Stage 4, so a
    // person pressing `H` has no other way to see that anything happened.
    switch (tool_) {
    case Tool::Hand:
        viewport()->setCursor(Qt::OpenHandCursor);
        break;
    case Tool::MarqueeZoom:
        viewport()->setCursor(Qt::CrossCursor);
        break;
    case Tool::Select:
        viewport()->setCursor(Qt::ArrowCursor);
        break;
    }

    emit toolChanged(tool_);
}

void PageView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || tool_ == Tool::Select) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    dragging_ = true;
    dragStart_ = event->pos();
    dragCurrent_ = dragStart_;
    dragScrollOrigin_ = controller_->scroll();
    if (tool_ == Tool::Hand) viewport()->setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void PageView::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }

    dragCurrent_ = event->pos();

    if (tool_ == Tool::Hand) {
        // The page follows the hand, so the SCROLL moves the other way. Getting the sign wrong here
        // is the classic hand-tool bug and it is not subtle to a user and completely invisible to a
        // test that only checks that something moved -- so the case asserts the direction.
        const double dpr = devicePixelRatioF();
        const QPoint moved = dragCurrent_ - dragStart_;
        controller_->setScroll(core::DevicePoint{dragScrollOrigin_.x - moved.x() * dpr,
                                                 dragScrollOrigin_.y - moved.y() * dpr});
    } else {
        viewport()->update();   // the marquee rubber band
    }

    event->accept();
}

void PageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (!dragging_ || event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }

    dragging_ = false;
    if (tool_ == Tool::Hand) {
        viewport()->setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }

    const QRect band = QRect(dragStart_, event->pos()).normalized();
    dragCurrent_ = event->pos();
    viewport()->update();

    // A CLICK IS NOT A ZOOM. A marquee smaller than a few pixels is somebody clicking, and zooming
    // to it would jump to 6400 % on a mis-click with no obvious way back.
    if (band.width() < kMinMarqueePixels || band.height() < kMinMarqueePixels) {
        event->accept();
        return;
    }

    const double dpr = devicePixelRatioF();
    const double bandWidth = std::max(1.0, band.width() * dpr);
    const double bandHeight = std::max(1.0, band.height() * dpr);

    // The zoom that makes the band fill the viewport, limited by the tighter axis so the whole band
    // stays visible -- the same rule fit-page follows, and for the same reason.
    const double factor = std::min(controller_->viewportSize().width / bandWidth,
                                   controller_->viewportSize().height / bandHeight);

    const core::DevicePoint centre{
        controller_->scroll().x + (band.center().x() * dpr),
        controller_->scroll().y + (band.center().y() * dpr)};

    const core::render::Zoom target =
        core::render::Zoom{controller_->zoom().factor * factor}.clamped();
    const double ratio = target.factor / controller_->zoom().factor;

    controller_->setFitMode(FitMode::None);
    controller_->setZoom(target);
    controller_->setScroll(
        core::DevicePoint{centre.x * ratio - controller_->viewportSize().width / 2.0,
                          centre.y * ratio - controller_->viewportSize().height / 2.0});
    controller_->recordView();
    event->accept();
}

void PageView::goToPage(int pageIndex)
{
    if (pageCount() <= 0) return;

    // CLAMPED, not refused. `nextPage()` on the last page and `prevPage()` on the first arrive
    // here out of range every time somebody holds the key down, and refusing would be an error
    // path taken during ordinary use.
    const int target = std::clamp(pageIndex, 0, pageCount() - 1);
    controller_->setCurrentPage(target);
    pageIndex = target;

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
                   controller_->currentPage(), controller_->viewRotation());

    controller_->setContentSize(engine_.layout().contentSize());

    updateScrollBars();

    presenter_->setDocument(document_, revision_, pages_);
    presenter_->requestTiles(engine_.layout(), engine_.resolution(), controller_->visibleRect(),
                             controller_->viewRotation());

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
    // text line at 100 %. `kArrowStep` is the same number because an arrow key and a click on a
    // scrollbar arrow are the same gesture to a reader.
    horizontalScrollBar()->setSingleStep(kArrowStep);
    verticalScrollBar()->setSingleStep(kArrowStep);

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
    // `pageDisplay/wheelZooms` SWAPS the two rather than making the wheel do both: with it on, a
    // bare wheel zooms and `Ctrl`+wheel scrolls. A modifier that does nothing is a modifier people
    // stop reaching for.
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    if (control != settings_.wheelZooms) {
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

    // THE SCROLLING HALF, and it has to notice when the scroll did not happen. `QAbstractScrollArea`
    // moves the scrollbar, whose range in a paged mode covers one page -- so at the bottom of that
    // page the wheel silently does nothing, which is a reader stuck on page 3 with the wheel in
    // their hand (I-24). One more notch there turns the page, exactly as it does in the reference
    // product, and the "already at the limit" test is what keeps a fast flick from flying through
    // pages: the first notch that cannot scroll is the one that pages.
    const int beforeY = verticalScrollBar()->value();
    const int beforeX = horizontalScrollBar()->value();
    QAbstractScrollArea::wheelEvent(event);
    if (verticalScrollBar()->value() != beforeY || horizontalScrollBar()->value() != beforeX) {
        return;
    }

    const double notches = event->angleDelta().y() / kWheelNotch;
    if (std::abs(notches) < 0.01) return;

    // A WHEEL AWAY FROM THE USER is a POSITIVE `angleDelta` and moves BACKWARD through the
    // document, which is the opposite sign from a scroll offset. Getting this backwards is a
    // one-character bug that reads as the page flipping the wrong way.
    if (scrollOrTurnPage(0.0, notches > 0.0 ? -1.0 : 1.0)) event->accept();
}

void PageView::keyPressEvent(QKeyEvent* event)
{
    // THE ARROW KEYS ARE `RAWPAGE_BINDING_NATIVE` ROWS (`core/cmd/KeyBindingDefaults.def`): the
    // focused widget owns them, so the boundary behaviour `Features.md` §8 specifies for them --
    // *"Scroll the page (paging at page boundaries)"* -- is this widget's to implement and not an
    // `ActionId`'s. Minting four permanent public names for a scroll gesture is what D1-16 warns
    // against; `NavNextPage` already exists for the dispatchable version.
    //
    // BARE ARROWS ONLY. A modifier belongs to whoever else wants it, and consuming `Ctrl+Left`
    // here would eat a chord some later stage binds.
    if (event->modifiers() == Qt::NoModifier) {
        // The scrollbar's own single step, so a key and a scrollbar arrow move by the same amount.
        const double step = kArrowStep * devicePixelRatioF();

        double dx = 0.0;
        double dy = 0.0;
        switch (event->key()) {
        case Qt::Key_Down:  dy = step; break;
        case Qt::Key_Up:    dy = -step; break;
        case Qt::Key_Right: dx = step; break;
        case Qt::Key_Left:  dx = -step; break;
        default: break;
        }

        if (dx != 0.0 || dy != 0.0) {
            if (scrollOrTurnPage(dx, dy)) {
                event->accept();
                return;
            }
            // AT THE DOCUMENT'S END, and deliberately NOT accepted: the base class will try the
            // same scroll and also fail, and leaving the key unconsumed is what the rest of the
            // shell does with a key it cannot use.
        }
    }

    QAbstractScrollArea::keyPressEvent(event);
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
                      devicePixelRatioF(), settings_.transparencyGrid);

    // The marquee, on top of everything. Drawn from the palette's highlight rather than a
    // hard-coded blue, so it is visible in both themes (convention 20).
    if (dragging_ && tool_ == Tool::MarqueeZoom) {
        const QRect band = QRect(dragStart_, dragCurrent_).normalized();
        QColor fill = palette().color(QPalette::Highlight);
        fill.setAlpha(60);
        painter.fillRect(band, fill);
        painter.setPen(palette().color(QPalette::Highlight));
        painter.drawRect(band);
    }
}

} // namespace rawpage::ui
