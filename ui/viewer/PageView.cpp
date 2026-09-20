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

    // THE TOOLS (S4 T7). Built after the controller, because it is the controller a tool reaches
    // through `ToolHost`, and this widget IS that host.
    tools_ = std::make_unique<ToolController>(*this, this);

    connect(tools_.get(), &ToolController::cursorChanged, this,
            [this](Qt::CursorShape shape) { viewport()->setCursor(shape); });

    connect(tools_.get(), &ToolController::armedChanged, this, [this](ToolKind kind) {
        // MOUSE TRACKING FOLLOWS THE TOOL. Only the loupe wants moves with no button down, and
        // leaving tracking on for every tool would deliver a move event per pixel to tools that
        // discard them -- and would make the hand tool's `isDragging()` the only thing standing
        // between a hover and a pan.
        viewport()->setMouseTracking(tools_->activeTool().tracksHover());
        viewport()->update();
        emit toolChanged(kind);
    });

    // The cursor the default tool shows, applied once at construction: `cursorChanged` only fires
    // on a CHANGE, so nothing would have set it otherwise and the viewer would open with the
    // window's arrow over a surface whose tool says I-beam.
    viewport()->setCursor(tools_->cursor());
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
                           const core::PageTree* pages, const core::PageTree* enginePages)
{
    document_ = document;
    revision_ = revision;
    pages_ = pages;
    enginePages_ = enginePages;

    engine_.setDocument(pages);
    presenter_->setDocument(document, revision, pages);

    controller_->setPageCount(engine_.pageCount());
    controller_->setCurrentPage(0);
    controller_->setScroll(core::DevicePoint{0.0, 0.0});

    refresh();
}

void PageView::modelChanged(core::Revision revision)
{
    revision_ = revision;
    // The tree is the same object, mutated, and `refresh()` lays out from it again -- which is what makes a
    // rotated page's display size, and every page below it, move. GEOMETRY FIRST, then tiles (Stage 1's
    // I-36). No `engine_.setDocument` here: it only forgets the cached layout, which `refresh()` rebuilds
    // anyway, and guide case A-75's control measured the call as doing nothing (Stage 3 T10).
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
    // S4 T7: LIVE, like the other nine. A speed change reaches a running auto-scroll -- and a
    // change to zero stops it, which `ToolController::setAutoScrollSpeed` owns rather than this
    // call site, so the rule holds wherever the speed comes from.
    if (tools_) tools_->setAutoScrollSpeed(settings.autoScrollSpeed);

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
    goToPage(pageStep(controller_->currentPage(), +1));
}

void PageView::prevPage()
{
    goToPage(pageStep(controller_->currentPage(), -1));
}

int PageView::pageStep(int from, int direction) const
{
    // ONE SPREAD, NOT ONE PAGE, and the difference only shows in the two-up modes -- where the
    // page beside the current one is already on screen, so stepping onto it shows the reader
    // nothing and, in a PAGED two-up, strands them there for good (S2 issue I-25).
    return engine_.spreadStep(from, direction, controller_->layoutMode());
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
    goToPage(pageStep(from, forward ? +1 : -1));
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

ToolKind PageView::tool() const noexcept
{
    // THE ARMED TOOL, not the effective one. A caller asking "which tool is the user in" means the
    // sticky choice; the temporary hand is a transient that `ToolController::effective()` answers
    // and that a case about the modifier must read instead (guide case A-41).
    return tools_->armed();
}

void PageView::setTool(ToolKind kind)
{
    tools_->arm(kind);
}

QSize PageView::viewportSize() const
{
    return viewport()->size();
}

void PageView::requestRepaint()
{
    viewport()->update();
}

void PageView::recordView()
{
    controller_->recordView();
}

const QPixmap* PageView::lastFrame() const
{
    return frame_.isNull() ? nullptr : &frame_;
}

void PageView::mousePressEvent(QMouseEvent* event)
{
    if (!tools_->press(event->pos(), event->button(), event->modifiers())) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    event->accept();
}

void PageView::mouseMoveEvent(QMouseEvent* event)
{
    if (!tools_->move(event->pos(), event->modifiers())) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    event->accept();
}

void PageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (!tools_->release(event->pos(), event->button())) {
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }
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

    presenter_->setDocument(document_, revision_, pages_, enginePages_);
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
    // `Esc` CANCELS THE TOOL'S GESTURE, and it is handled here rather than through `edit.cancel`
    // (S4 T7). See the note at that id's absent registration in `MainWindow::registerViewerActions`:
    // `Esc` is §8's `Common` row, four unbuilt contexts override it, and a registered-but-disabled
    // command CONSUMES the key -- which swallowed the tool finder's own `Esc` the first time this
    // was tried.
    //
    // A cancel that found nothing to cancel does NOT accept the event, so the key travels on to
    // whatever else would have had it. That is guide case A-42's control.
    if (event->key() == Qt::Key_Escape && !event->modifiers()) {
        if (tools_->cancel()) {
            event->accept();
            return;
        }
    }

    // EVERY KEY RE-DERIVES THE MODIFIER STATE, which is what makes the temporary hand reliable:
    // a modifier released while the pointer is outside this widget produces no release event
    // here, so the state is read from whatever arrives next rather than remembered (A-41).
    tools_->setModifiers(event->modifiers());

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

void PageView::paintContent(QPainter& painter, const QRect& area)
{
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // The surround, FROM THE PALETTE rather than a hard-coded grey. `ThemeManager` writes the
    // application palette from its tokens, so taking the desk colour from `QPalette::Window`
    // is taking it from the theme -- and a hard-coded light grey here would be a dark-theme
    // contrast defect in a brand-new widget, which is exactly what I-40 was (convention 20).
    painter.fillRect(area, palette().color(QPalette::Window));

    if (!document_.isValid() || pages_ == nullptr || pages_->isEmpty()) return;

    presenter_->paint(painter, engine_.layout(), engine_.resolution(), controller_->visibleRect(),
                      devicePixelRatioF(), settings_.transparencyGrid);
}

void PageView::paintEvent(QPaintEvent* event)
{
    // TWO PATHS, AND THE SECOND ONE COSTS SOMETHING (S4 T7). Ordinarily the page is painted
    // straight onto the viewport. When the armed tool needs a FRAME -- only the loupe does -- the
    // same content goes into a pixmap first, which is then blitted and handed to the tool to
    // magnify. That is one extra viewport-sized copy per paint, paid only while the loupe is
    // armed and never otherwise; `ui/viewer/tools/LoupeTool.h` says why the alternative (asking
    // the engine for a magnified raster) was refused.
    if (tools_->activeTool().needsFrame()) {
        const double ratio = devicePixelRatioF();
        const QSize pixels(static_cast<int>(viewport()->width() * ratio),
                           static_cast<int>(viewport()->height() * ratio));
        if (frame_.size() != pixels) frame_ = QPixmap(pixels);
        frame_.setDevicePixelRatio(ratio);
        {
            QPainter into(&frame_);
            paintContent(into, viewport()->rect());
        }
        QPainter painter(viewport());
        painter.drawPixmap(0, 0, frame_);
        tools_->paint(painter);
        return;
    }

    // The frame is dropped as soon as nothing wants it, so an armed-then-disarmed loupe does not
    // leave a viewport-sized pixmap resident for the life of the tab.
    if (!frame_.isNull()) frame_ = QPixmap();

    QPainter painter(viewport());
    paintContent(painter, event->rect());
    tools_->paint(painter);
}

} // namespace rawpage::ui
