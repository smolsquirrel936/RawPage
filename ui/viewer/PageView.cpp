#include "ui/viewer/PageView.h"

#include "core/Log.h"
#include "core/cmd/KeyContext.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/PageTree.h"
#include "core/doc/TypedText.h"
#include "core/render/RenderScheduler.h"
#include "core/settings/Settings.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/AnnotationTextEditor.h"
#include "ui/viewer/LoupeRaster.h"
#include "ui/viewer/PropertiesBar.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/viewer/SnapTargets.h"
#include "ui/viewer/TilePresenter.h"
#include "ui/viewer/ToolDefaults.h"
#include "ui/viewer/tools/LoupeTool.h"
#include "ui/viewer/tools/MarqueeZoomTool.h"
#include "ui/viewer/tools/RedactTool.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QInputMethod>
#include <QLocale>
#include <QDateTime>
#include <QGuiApplication>
#include <QStyleHints>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QTimer>
#include <QTransform>
#include <QCoreApplication>
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
    // is what makes `Features.md` §8's deliberate collisions mean anything at all.
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
    // through `ToolHost`. The host is `toolTarget_`, which is this widget in everything but the
    // controller a FROZEN pane's tools scroll -- the body's (I-20, `setFrozen`).
    tools_ = std::make_unique<ToolController>(toolTarget_, this);

    connect(tools_.get(), &ToolController::cursorChanged, this,
            [this](Qt::CursorShape shape) { viewport()->setCursor(shape); });

    // THE LOUPE'S SHARP LENS (S4 D4-20). Its raster arriving repaints, as a tile does and for the
    // same reason: a re-request inside a delivery would loop. The settle timer is what makes the
    // lens ask only once its region has been still -- see `paintSharpLens`.
    loupeRaster_ = std::make_unique<LoupeRaster>(scheduler_, this);
    connect(loupeRaster_.get(), &LoupeRaster::arrived, this, [this] { viewport()->update(); });
    loupeSettle_ = new QTimer(this);
    loupeSettle_->setSingleShot(true);
    loupeSettle_->setInterval(kLoupeSettleMs);
    connect(loupeSettle_, &QTimer::timeout, this, [this] {
        // ONLY IF THE LENS IS STILL THERE TO SHOW IT. A lens put away by `Esc`, or a loupe disarmed,
        // inside the interval would otherwise spend a rasterization on a region nobody will see.
        const bool shown = tools_->effective() == ToolKind::Loupe && tools_->loupe().isVisible();
        if (shown) loupeRaster_->aim(pendingLoupe_);
    });

    // THE SNAPSHOT'S CLIENT (S5 T10, D5-11): the loupe's shape, its request builder, its lane -- and the
    // tile cache BYPASSED, because a snapshot is consumed once and may be 200 MB (`LoupeRaster.h`).
    snapshotRaster_ = std::make_unique<LoupeRaster>(scheduler_, this, core::render::CachePolicy::Bypass);
    connect(snapshotRaster_.get(), &LoupeRaster::arrived, this, [this] { deliverSnapshot(); });
    connect(snapshotRaster_.get(), &LoupeRaster::failed, this, [this] {
        snapshotRaster_->clear();
        emit snapshotFailed();
    });

    connect(tools_.get(), &ToolController::armedChanged, this, [this](ToolKind kind) {
        // MOUSE TRACKING FOLLOWS THE TOOL. Only the loupe wants moves with no button down, and
        // leaving tracking on for every tool would deliver a move event per pixel to tools that
        // discard them -- and would make the hand tool's `isDragging()` the only thing standing
        // between a hover and a pan.
        updateMouseTracking();
        // AND THE CONTEXT (S6 T5, D6-9): an annotation tool armed puts the page in Annotation tools.
        updateKeyContext();
        viewport()->update();
        emit toolChanged(kind);
    });

    // THE GRID AND GUIDES (S4 T8). Built after the tools because its mouse gesture takes
    // PRECEDENCE over theirs -- see `mousePressEvent` -- and because `updateMouseTracking()` reads
    // both. A change to it repaints and REACHES NOTHING ELSE: the whole of D4-11 is that a grid
    // line never becomes a render request (guide case A-50).
    guides_ = std::make_unique<GridGuideLayer>(this);
    connect(guides_.get(), &GridGuideLayer::changed, this, [this] { viewport()->update(); });
    // S6 T12 (D4-5): an object edge is a side of an annotation on the page -- the overlay's store, read when asked.
    guides_->setObjectEdgeSource([this](int page) { return annotationEdges(page); });

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
    // flight*, and the ordering is the thing that case is about. The lens's client goes with it,
    // for the same reason: it too holds a subscription and may hold a ticket (D4-20).
    loupeRaster_.reset();
    snapshotRaster_.reset();
    presenter_.reset();
}

void PageView::setDocument(core::DocumentHandle document, core::Revision revision,
                           const core::PageTree* pages, const core::PageTree* enginePages)
{
    document_ = document;
    revision_ = revision;
    pages_ = pages;
    enginePages_ = enginePages;

    // A SNAPSHOT STILL ON ITS WAY IS THE OLD DOCUMENT'S, and is dropped rather than delivered into a
    // view that now shows another (S5 T10).
    if (snapshotRaster_) snapshotRaster_->clear();

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
    // **THE READER'S PLACE, TAKEN BEFORE THE ZOOM MOVES -- I-11.** A fit changes the zoom, and the
    // scroll offset is device pixels that do not scale with it: dropping from 400 % to a fit left
    // the viewport three times further down the document, on a page nobody was reading.
    const Place place = capturePlace();

    // THE ANCHOR, set where the mode is chosen and BEFORE the controller hears of it, because the
    // controller's change signal runs `refresh()`, which re-satisfies the fit -- see there.
    fitAnchorPage_ = (fit == FitMode::None) ? -1 : controller_->currentPage();
    controller_->setFitMode(fit);

    // **FIT VISIBLE IS A ZOOM AND A SCROLL** (D4-14). Every other fit leaves the page's own corner
    // under the viewport's, and that is where the thing being fitted is. The content box is not:
    // on `corpus/ordinary/cropbox-margins.pdf` it sits 72 pt inside the CropBox, which is itself
    // 72 pt inside the sheet, so a Fit Visible that only zoomed would size the ink correctly and
    // then show the reader the margin above and to the left of it.
    //
    // Done HERE and not in `refresh()`, which runs on every scroll, resize and tile arrival: a fit
    // that re-centred itself on each of those would drag the viewport back the moment anybody
    // scrolled, and the mode has to survive a resize without owning the scroll position.
    if (fit == FitMode::Visible) {
        refresh();
        scrollToFitRect(fitAnchorPage_);
        return;
    }

    // Every other fit puts the reader back on the page they fitted. Fit Page and Fit Height show
    // that page WHOLE, which is what they were asked for; Fit Width keeps how far down it they were.
    refresh();
    restorePlace(place, fit == FitMode::Page || fit == FitMode::Height);
}

PageView::Place PageView::capturePlace() const
{
    const int page = controller_->currentPage();
    const core::render::PageSlot* slot = engine_.layout().slotForPage(page);
    if (slot == nullptr || slot->rect.size.width <= 0.0 || slot->rect.size.height <= 0.0) return {};

    // THE VIEWPORT'S CENTRE, as a fraction of the current page. A fraction and not an offset,
    // because it is the one description of "here" that survives the page changing size.
    const core::DeviceRect visible = controller_->visibleRect();
    const double centreX = visible.left() + visible.size.width / 2.0;
    const double centreY = visible.top() + visible.size.height / 2.0;
    return Place{page,
                 std::clamp((centreX - slot->rect.left()) / slot->rect.size.width, 0.0, 1.0),
                 std::clamp((centreY - slot->rect.top()) / slot->rect.size.height, 0.0, 1.0)};
}

void PageView::restorePlace(const Place& place, bool wholePage)
{
    if (place.page < 0) return;
    const core::render::PageSlot* slot = engine_.layout().slotForPage(place.page);
    if (slot == nullptr) return;   // a paged mode showing another spread: nothing to restore into

    const core::DeviceSize viewport = controller_->viewportSize();
    const double fx = wholePage ? 0.5 : place.fx;
    const double x = slot->rect.left() + fx * slot->rect.size.width - viewport.width / 2.0;
    const double y = wholePage
                         ? slot->rect.top() - engine_.options().margin
                         : slot->rect.top() + place.fy * slot->rect.size.height - viewport.height / 2.0;
    controller_->setScroll(core::DevicePoint{x, y});
    refresh();
}

void PageView::setContentBoxSource(PageLayoutEngine::ContentBoxSource source)
{
    engine_.setContentBoxSource(std::move(source));
}

void PageView::scrollToFitRect(int pageIndex)
{
    const std::optional<core::PdfRect> rect = engine_.fitRect(FitMode::Visible, pageIndex);
    if (!rect.has_value()) return;

    // THE BOX'S TOP-LEFT IN PDF SPACE IS (left, top), because PDF's y grows UPWARDS and a device
    // rectangle's origin is its TOP-left. `PageLayout::toContent` is what knows that, along with
    // the page's rotation and a CropBox whose origin is not (0,0) -- which is why this is one call
    // and not four multiplications, and why `cropbox-margins.pdf` (origin 72,72) is the file that
    // would have caught the four multiplications.
    const auto corner = engine_.layout().toContent(pageIndex, core::PdfPoint{rect->left(), rect->top()});
    if (!corner.has_value()) return;

    const double margin = engine_.options().margin;
    controller_->setScroll(core::DevicePoint{corner->x - margin, corner->y - margin});
    refresh();
}

void PageView::goToDestination(const core::Destination& destination)
{
    // **I-05, CLOSED.** T6's panes carried the fit and did not apply it, and said so at their own
    // headers: the arithmetic needed the page's slot rectangle and the resolution that produced it,
    // which is what this task built for Fit Visible.
    if (!destination.isResolved()) return;
    if (pageCount() <= 0) return;

    const int target = std::clamp(destination.pageIndex, 0, pageCount() - 1);

    // **THE ZOOM BEFORE THE PAGE, AND THIS ORDER IS LOAD-BEARING.** The obvious arrangement is the
    // other way round -- go to the page, then apply the fit -- and it is wrong twice over, and both
    // wrongs were measured rather than reasoned about.
    //
    //   1. `goToPage` computes its scroll offset from the layout AS IT IS, and a fit applied
    //      afterwards re-lays the document out at a different zoom -- so that offset now points at a
    //      different page, and `refresh()`'s dominant-page snap reports THAT page as current. The
    //      first version of this method did exactly that and a bookmark to page 2 landed on page 4.
    //   2. A fit is re-satisfied for whichever page is CURRENT, so once the snap has moved the
    //      current page the zoom is recomputed for a page the reader did not ask for. That is how a
    //      `/FitB` destination came out at the whole page's zoom with the content box never read.
    //
    // So: name the page, apply the fit, let one `refresh()` settle the zoom and the layout together,
    // and only then navigate -- by which time `goToPage`'s arithmetic is over a layout that already
    // matches the zoom it will keep.
    controller_->setCurrentPage(target);

    // Every fit a destination names is anchored to the destination's page (I-11). An `/XYZ` with a
    // zoom clears it through `setZoom`; one without keeps the fit in force, now on this page.
    fitAnchorPage_ = target;

    switch (destination.fit) {
    case core::Destination::Fit::Fit:
        // `/Fit` is the whole page and `/FitB` is its content bounding box -- which is precisely the
        // pair `FitMode::Page` and `FitMode::Visible` now are, so each is one line rather than a
        // second implementation of D4-14.
        controller_->setFitMode(FitMode::Page);
        break;
    case core::Destination::Fit::FitB:
        controller_->setFitMode(FitMode::Visible);
        break;
    case core::Destination::Fit::FitH:
    case core::Destination::Fit::FitBH:
        controller_->setFitMode(FitMode::Width);
        break;
    case core::Destination::Fit::FitV:
    case core::Destination::Fit::FitBV:
        controller_->setFitMode(FitMode::Height);
        break;
    case core::Destination::Fit::XYZ:
        // **A NULL IS "KEEP WHAT YOU HAVE", NOT ZERO** (`core/doc/Destinations.h`). `/XYZ null null
        // null` is the commonest destination a producer writes and means *go to this page and change
        // nothing else*; reading the three absent values as 0 would scroll every such link to the
        // page's bottom-left corner at 8 % zoom.
        //
        // A zoom of 0 is ALSO "keep what you have" -- ISO 32000-1 §12.3.2.2 says so in as many
        // words, and a file that writes `/XYZ 72 720 0` is asking for a position and not for a zoom
        // of nothing.
        if (destination.hasZoom && destination.zoom > 0.0) setZoom(core::render::Zoom{destination.zoom});
        break;

    case core::Destination::Fit::FitR:
        // `/FitR` fits a rectangle, and the rectangle is carried in four numbers where `/XYZ` uses
        // two. `core::Destination` has no fields to hold the other two, so the rectangle is not read
        // and this lands on the page at the current zoom -- the same honest partial `Unknown` gets.
        // Stage 7 owns the editing half of both tables and is where the vocabulary would grow;
        // recorded here rather than guessed at.
    case core::Destination::Fit::Unknown:
        break;
    }

    // ONE REFRESH TO SETTLE THE ZOOM, over the page named above. Its own dominant-page snap may move
    // the current page as it goes -- the scroll still points into the old layout -- and that does not
    // matter: `goToPage` below names the page again, in a layout that is now at the right zoom.
    refresh();

    // THE PAGE, through the one path every other navigation takes -- so a destination and a
    // `Ctrl+G` land in the same place and one history entry is recorded for the jump.
    goToPage(target);

    // AND THEN WHERE ON THE PAGE. Each of these is the destination's own refinement of the page
    // top `goToPage` just scrolled to, and each is skipped where the file said *keep what you have*.
    switch (destination.fit) {
    case core::Destination::Fit::FitB:
        scrollToFitRect(target);
        break;
    case core::Destination::Fit::FitH:
    case core::Destination::Fit::FitBH:
        // The `top` rides in `y` and is optional: `/FitH null` means *keep the current vertical
        // position*, which is what `hasY` false says and what leaving the scroll alone does.
        if (destination.hasY) scrollToPagePoint(target, destination.y, false);
        break;
    case core::Destination::Fit::FitV:
    case core::Destination::Fit::FitBV:
        if (destination.hasX) scrollToPagePoint(target, destination.x, true);
        break;
    case core::Destination::Fit::XYZ:
        if (destination.hasX || destination.hasY) {
            scrollToPageXY(target,
                           destination.hasX ? std::optional<double>{destination.x} : std::nullopt,
                           destination.hasY ? std::optional<double>{destination.y} : std::nullopt);
        }
        break;
    case core::Destination::Fit::Fit:
    case core::Destination::Fit::FitR:
    case core::Destination::Fit::Unknown:
        break;
    }
}

void PageView::scrollToPagePoint(int pageIndex, double pdfCoordinate, bool horizontal)
{
    scrollToPageXY(pageIndex, horizontal ? std::optional<double>{pdfCoordinate} : std::nullopt,
                   horizontal ? std::nullopt : std::optional<double>{pdfCoordinate});
}

std::optional<double> PageView::depthBelowTop(int pageIndex, core::PdfPoint point) const
{
    const auto content = engine_.layout().toContent(pageIndex, point);
    if (!content.has_value()) return std::nullopt;
    return content->y - controller_->visibleRect().top();
}

void PageView::putPageRectAtTop(int pageIndex, const core::PdfRect& rect)
{
    // THE CORNER HIGHEST ON SCREEN, whichever corner of the page's rectangle that is: the page's top only
    // on an upright page. Converting one height at the crop box's left edge, as this did at T11, put the
    // page's left edge at the top at 90 degrees and the rectangle's bottom at 180 (S5 guide I-06).
    const core::render::PageLayout& layout = engine_.layout();
    std::optional<double> highest;
    for (const core::PdfPoint corner : {core::PdfPoint{rect.left(), rect.bottom()}, core::PdfPoint{rect.right(), rect.bottom()},
                                        core::PdfPoint{rect.left(), rect.top()}, core::PdfPoint{rect.right(), rect.top()}}) {
        const auto content = layout.toContent(pageIndex, corner);
        if (!content.has_value()) return;
        highest = highest ? std::min(*highest, content->y) : content->y;
    }
    controller_->setCurrentPage(pageIndex);
    controller_->setScroll(core::DevicePoint{controller_->scroll().x, *highest});
    refresh();
}

void PageView::scrollToPageXY(int pageIndex, std::optional<double> pdfX, std::optional<double> pdfY)
{
    const core::Page* page = pages_ ? pages_->at(pageIndex) : nullptr;
    if (page == nullptr) return;

    // The axis that was not given keeps the page's own edge, so the conversion has a whole point to
    // work with while only the given axis moves the viewport.
    const core::PdfRect crop = page->cropBox();
    const core::PdfPoint target{pdfX.value_or(crop.left()), pdfY.value_or(crop.top())};

    const auto content = engine_.layout().toContent(pageIndex, target);
    if (!content.has_value()) return;

    const double margin = engine_.options().margin;
    const core::DevicePoint now = controller_->scroll();
    controller_->setScroll(core::DevicePoint{pdfX.has_value() ? content->x - margin : now.x,
                                            pdfY.has_value() ? content->y - margin : now.y});
    refresh();
}

// A ZOOM FROM A COMMAND KEEPS THE READER'S PLACE (I-11). The wheel's zoom has always done so, about
// the cursor (`ViewportController::zoomAbout`); the chords, the zoom box and Actual Size changed the
// zoom under an unchanged device offset and so moved the reader by the ratio of the two zooms.
void PageView::zoomIn()
{
    const Place place = capturePlace();
    fitAnchorPage_ = -1;
    controller_->zoomIn();
    restorePlace(place, false);
}

void PageView::zoomOut()
{
    const Place place = capturePlace();
    fitAnchorPage_ = -1;
    controller_->zoomOut();
    restorePlace(place, false);
}

void PageView::setZoom(core::render::Zoom zoom)
{
    const Place place = capturePlace();
    controller_->setFitMode(FitMode::None);
    fitAnchorPage_ = -1;
    controller_->setZoom(zoom);
    restorePlace(place, false);
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

core::DevicePoint PageView::toContentPoint(QPoint viewportPosition) const
{
    const double dpr = devicePixelRatioF();
    const core::DeviceRect visible = controller_->visibleRect();
    return core::DevicePoint{visible.left() + viewportPosition.x() * dpr,
                             visible.top() + viewportPosition.y() * dpr};
}

void PageView::updateMouseTracking()
{
    // The loupe wants moves with no button down; so do the rulers, which draw a marker at the
    // pointer. Anything else gets none, because a move per pixel delivered to a tool that
    // discards it is the cost this flag exists to avoid.
    viewport()->setMouseTracking(tools_->activeTool().tracksHover()
                                 || guides_->settings().showRulers
                                 || (annotations_ != nullptr && annotations_->wantsHover()));
}

void PageView::applyGuideSettings(const GuideSettings& settings)
{
    guides_->setSettings(settings);
    updateMouseTracking();
    viewport()->update();
}

void PageView::setSelectionOverlay(SelectionOverlay* selection)
{
    if (selection_ == selection) return;
    if (selection_ != nullptr) disconnect(selection_, nullptr, this, nullptr);
    selection_ = selection;

    // A CHANGE REPAINTS AND REACHES NOTHING ELSE -- no `refresh()`, no tile request. A selection is
    // painted over the pixels the scheduler already returned (D4-11), which is what guide A-53 asserts.
    if (selection_ != nullptr)
        connect(selection_, &SelectionOverlay::changed, this, [this] { viewport()->update(); });
    viewport()->update();
}

void PageView::setAnnotationOverlay(AnnotationOverlay* annotations)
{
    if (annotations_ == annotations) return;
    if (annotations_ != nullptr) disconnect(annotations_, nullptr, this, nullptr);
    annotations_ = annotations;
    // A CHANGE REPAINTS AND REACHES NOTHING ELSE (D4-11): the outline, the handles, a preview and the
    // pop-ups are painted over the tiles. A selection changes the context this pane declares (D6-9).
    if (annotations_ != nullptr) {
        connect(annotations_, &AnnotationOverlay::changed, this, [this] { viewport()->update(); });
        connect(annotations_, &AnnotationOverlay::selectionChanged, this, [this] {
            updateKeyContext();
            updateMouseTracking();
        });
    }
    updateKeyContext();
    updateMouseTracking();
    viewport()->update();
}

bool PageView::requestAnnotationText(const AnnotationTextRequest& request, std::function<void(const QString&)> commit)
{
    // ONE AT A TIME: asking again finishes the editor already open, as clicking away from it would.
    if (editor_ != nullptr && !editor_->isFinished()) (void)editor_->commit();

    auto* editor = new AnnotationTextEditor(viewport());
    editor_ = editor;
    const QSize room = viewport()->size();
    if (request.box) {
        // S6 T7: OVER THE BOX the text will be drawn in, at its size -- so it wraps where the appearance
        // will, and the person types where the comment goes (M-06). Never smaller than a line.
        QRect box = *request.box;
        const int line = request.fontPixels ? static_cast<int>(std::ceil(*request.fontPixels * core::kTypedLeading)) : 16;
        box.setWidth(std::max(box.width(), line * 2));
        box.setHeight(std::max(box.height(), line + 8));
        editor->setGeometry(box);
    } else {
        // Below and right of the point it was asked for, kept inside the visible page area.
        constexpr int kWidth = 240;
        constexpr int kHeight = 72;
        const int x = std::clamp(request.at.x() + 8, 0, std::max(0, room.width() - kWidth));
        const int y = std::clamp(request.at.y() + 8, 0, std::max(0, room.height() - kHeight));
        editor->setGeometry(x, y, kWidth, kHeight);
    }
    if (request.fontPixels) {
        // Arial is drawn to Helvetica's advances, so the editor breaks lines where the appearance will.
        QFont font(QStringLiteral("Arial"));
        font.setPixelSize(std::max(6, static_cast<int>(std::lround(*request.fontPixels))));
        editor->setFont(font);
    }
    editor->setLineWrapMode(request.wrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    if (!request.initial.isEmpty()) {
        editor->setPlainText(request.initial);
        editor->moveCursor(QTextCursor::End);
    }
    if (request.typed) {
        // D6-7: a character no face can show is refused AS IT IS TYPED, and the status bar names it.
        editor->setRefusesUnshowable(true);
        connect(editor, &AnnotationTextEditor::characterRefused, this, [this](uint character) {
            if (annotations_ != nullptr)
                annotations_->reportRefusal(core::Error{core::ErrorCode::InvalidArgument,
                                                        QLatin1String("error.edit.typedCharacterRefused"),
                                                        core::describeCharacter(static_cast<char32_t>(character))});
        });
    }

    // THE FOCUS COMES BACK to the page when the editor finishes inside it -- `Enter` or `Esc` -- so the page's
    // chords work again; a person who clicked somewhere else keeps the focus where they put it.
    const auto refocus = [this] {
        QWidget* focus = QApplication::focusWidget();
        if (focus == nullptr || (focus != this && isAncestorOf(focus))) setFocus(Qt::OtherFocusReason);
    };
    connect(editor, &AnnotationTextEditor::committed, this, [refocus, commit](const QString& text) {
        commit(text);
        refocus();
    });
    connect(editor, &AnnotationTextEditor::abandoned, this, refocus);
    editor->show();
    editor->setFocus(Qt::OtherFocusReason);
    return true;
}

AnnotationTextEditor* PageView::annotationTextEditor() const
{
    return editor_ != nullptr && !editor_->isFinished() ? editor_.data() : nullptr;
}

int PageView::displayedRotation(int page) const
{
    const core::render::PageSlot* slot = engine_.layout().slotForPage(page);
    return slot != nullptr ? static_cast<int>(slot->rotation) : 0;
}

core::TypedFont PageView::hanPreference() const
{
    // The INPUT language first -- the keyboard a person is typing Chinese with says which Chinese -- then the
    // system's. The rule is core's (convention 11); these are only the languages.
    QStringList languages;
    if (const QInputMethod* input = QGuiApplication::inputMethod()) languages << input->locale().bcp47Name();
    languages << QLocale::system().bcp47Name() << QLocale::system().name();
    return core::hanPreference(languages);
}

core::ToolDefaults PageView::toolDefaults(core::CommentingTool tool) const
{
    return effectiveToolDefaults(tool, toolSettings_);
}

std::optional<QPointF> PageView::widgetPointOf(int page, core::PdfPoint point) const
{
    const std::optional<core::DevicePoint> device = engine_.layout().toContent(page, point);
    if (!device) return std::nullopt;
    const core::DeviceRect visible = controller_->visibleRect();
    const double dpr = devicePixelRatioF() > 0.0 ? devicePixelRatioF() : 1.0;
    return QPointF((device->x - visible.left()) / dpr, (device->y - visible.top()) / dpr);
}

// ---- The measuring tools (S6 T12, D6-14) -----------------------------------------------------------

void PageView::setSnapTargets(SnapTargets* targets)
{
    if (snapTargets_ == targets) return;
    if (snapTargets_ != nullptr) disconnect(snapTargets_, nullptr, this, nullptr);
    snapTargets_ = targets;
    // A page's paths that arrive while a measuring tool is aimed: the snap it could not show is shown now.
    if (snapTargets_ != nullptr) connect(snapTargets_, &SnapTargets::arrived, this, [this] { viewport()->update(); });
}

std::vector<core::SnapEdge> PageView::annotationEdges(int page) const
{
    std::vector<core::SnapEdge> edges;
    const core::AnnotationStore* store = annotations_ != nullptr ? annotations_->store() : nullptr;
    if (store == nullptr || !store->status()) return edges;
    for (const std::size_t index : store->onPage(page)) {
        const core::Annotation& a = store->at(index);
        // What is DRAWN on the page: a pop-up is a window, and a hidden annotation is not there to aim at.
        if (a.subtype == core::AnnotationSubtype::Popup || a.hasFlag(core::AnnotationFlag::Hidden)
            || a.hasFlag(core::AnnotationFlag::NoView) || !a.rectFinite)
            continue;
        edges.push_back(core::SnapEdge{core::SnapAxis::X, a.rect.left()});
        edges.push_back(core::SnapEdge{core::SnapAxis::X, a.rect.right()});
        edges.push_back(core::SnapEdge{core::SnapAxis::Y, a.rect.bottom()});
        edges.push_back(core::SnapEdge{core::SnapAxis::Y, a.rect.top()});
    }
    return edges;
}

core::SnapResult PageView::snapMeasured(int page, core::PdfPoint point)
{
    const double scale = engine_.layout().presentedScale();
    const GuideSettings& settings = guides_->settings();
    std::vector<core::SnapPoint> points;
    // THE PAGE'S PATHS, near the point and within the one threshold -- asked of the index the first time, so the
    // first aim at a page may find none yet and the next one, after `arrived`, finds them (guide A-91).
    if (settings.snapToObjects && snapTargets_ != nullptr && scale > 0.0) {
        if (const std::shared_ptr<const core::PageGeometry> paths = snapTargets_->geometry(page))
            points = core::snapPointsNear(*paths, point, core::kSnapThresholdDevicePixels / scale, settings.snapToPaths);
    }
    return guides_->snapMeasured(page, point, points, scale);
}

ToolHost::MeasureSettings PageView::measureSettings() const
{
    MeasureSettings out;
    if (toolSettings_ == nullptr) return out;
    out.scale.ratio = toolSettings_->get(core::keys::measuring::scaleRatio);
    out.scale.unit = toolSettings_->get(core::keys::measuring::displayUnit);
    out.scale.precision = toolSettings_->get(core::keys::measuring::precision);
    out.writeToComment = toolSettings_->get(core::keys::measuring::writeToComment);
    return out;
}

bool PageView::calibrateFrom(double points)
{
    emit calibrationMeasured(points);
    return true;
}

void PageView::measurementMade(const QString& reading)
{
    emit measured(reading);
}

double PageView::pixelsPerPoint() const
{
    const double dpr = devicePixelRatioF() > 0.0 ? devicePixelRatioF() : 1.0;
    return engine_.layout().presentedScale() / dpr;
}

bool PageView::editTypedTextAt(QPoint pos)
{
    if (annotations_ == nullptr || !annotations_->isActive()) return false;
    const AnnotationOverlay::Viewport view = annotationViewport();
    const std::optional<core::AnnotationId> hit = annotations_->annotationAt(view, pos);
    if (!hit) return false;
    const std::optional<std::size_t> index = annotations_->store()->find(*hit);
    if (!index) return false;
    const core::Annotation& a = annotations_->store()->at(*index);
    if (a.subtype != core::AnnotationSubtype::FreeText) return false;
    // D6-27: text that may not be changed is not opened for changing; the status bar says why.
    if (const core::Status allowed = core::annotationChangeAllowed(a, false, true); !allowed) {
        annotations_->reportRefusal(allowed.error());
        return true;
    }
    const std::optional<core::FreeTextFrame> frame = core::freeTextFrame(a);
    const std::optional<QRectF> box = frame ? AnnotationOverlay::widgetRectOf(view, a.page, frame->box) : std::nullopt;
    if (!box) return false;

    AnnotationTextRequest request;
    request.box = box->toAlignedRect();
    request.fontPixels = core::freeTextStyle(a).size * pixelsPerPoint();
    request.wrap = core::freeTextKind(a) != core::FreeTextKind::Typewriter;
    request.initial = a.contents;
    request.typed = true;
    const core::AnnotationId id = a.id;
    AnnotationOverlay* annotations = annotations_;
    const core::TypedFont han = hanPreference();
    annotations_->select({id});
    return requestAnnotationText(request, [annotations, id, han](const QString& text) {
        (void)annotations->editFreeText(id, text, han);
    });
}

void PageView::annotationToolUsed()
{
    if (!tools_->keepToolSelected() && tools_->armed() == ToolKind::Annotate) tools_->arm(ToolKind::Select);
}

bool PageView::markSelectedText(AnnotationToolKind kind)
{
    if (selection_ == nullptr || annotations_ == nullptr || !selection_->hasSelection()) return false;
    const core::ToolDefaults defaults = toolDefaults(kind);
    const std::optional<core::AnnotationSubtype> marked = markupSubtype(kind);
    const AnnotationOverlay::Style style{defaults.colour, defaults.opacity,
                                         marked ? blendNameFor(*marked, defaults.blendMode) : QString(), std::nullopt};
    // S6 T14 (D6-25): text already selected is MARKED FOR REDACTION at once, as the markup commands mark it.
    if (kind == AnnotationToolKind::RedactMark) {
        (void)annotations_->markRedaction(*selection_, RedactTool::styleFor(*this));
        return true;
    }
    if (kind == AnnotationToolKind::ReplaceText) {
        // The editor at the END of what is selected -- where the caret will go.
        QPoint at(viewport()->width() / 2, viewport()->height() / 2);
        const core::text::TextSelection& text = selection_->selection();
        const std::vector<core::DeviceRect> runs = selection_->highlightRects(engine_.layout(), text.lastPage());
        if (!runs.empty()) {
            const core::DeviceRect visible = controller_->visibleRect();
            const double dpr = devicePixelRatioF();
            at = QPoint(static_cast<int>((runs.back().right() - visible.left()) / dpr),
                        static_cast<int>((runs.back().bottom() - visible.top()) / dpr));
        }
        SelectionOverlay* selection = selection_;
        AnnotationOverlay* annotations = annotations_;
        return requestAnnotationText(at, [selection, annotations, style](const QString& replacement) {
            (void)annotations->markText(*selection, core::AnnotationSubtype::StrikeOut, style, replacement);
        });
    }
    const std::optional<core::AnnotationSubtype> subtype = markupSubtype(kind);
    if (!subtype) return false;
    (void)annotations_->markText(*selection_, *subtype, style);
    return true;
}

AnnotationOverlay::Viewport PageView::annotationViewport() const
{
    return AnnotationOverlay::Viewport{&engine_.layout(), controller_->visibleRect(), devicePixelRatioF()};
}

bool PageView::overlayFirst() const
{
    if (annotations_ == nullptr) return false;
    if (tools_->armed() == ToolKind::Select) return true;
    if (tools_->armed() != ToolKind::Annotate) return false;
    // S6 T8: A SHAPE IN PROGRESS takes every click -- a polygon's next vertex may fall on an annotation -- and the
    // pencil and the eraser draw and erase OVER what is there: picking up the stroke under the pen would make a
    // second stroke across the first impossible.
    if (tools_->shapeInProgress()) return false;
    const std::optional<AnnotationToolKind> armed = tools_->armedAnnotation();
    // S6 T12: A MEASURING TOOL places its points over what is there, as the pencil draws: a measurement is so often
    // taken from where another one ends that picking that one up would make the commonest gesture impossible.
    return !(armed && (InkTool::handles(*armed) || MeasureTool::handles(*armed)));
}

void PageView::updateKeyContext()
{
    // A PRESENTATION'S SURFACE IS FULL SCREEN and stays so: that context is the controller's to undo.
    if (ShortcutBinder::declaredContext(this) == core::KeyContext::FullScreen) return;
    const bool annotating = (tools_ != nullptr && tools_->armed() == ToolKind::Annotate)
                            || (annotations_ != nullptr && annotations_->hasSelection());
    ShortcutBinder::declareContext(this, annotating ? core::KeyContext::AnnotationTools : core::KeyContext::Viewer);
}

PagePoint PageView::pagePointAt(QPoint pos) const
{
    const core::render::PageLayout& layout = engine_.layout();
    const core::DevicePoint content = toContentPoint(pos);
    if (const auto document = layout.toDocument(content)) return PagePoint{document->pageIndex, document->point};

    // OVER A GAP OR THE DESK: the nearest page, with the point pulled inside it. A drag from page 1
    // across the gap to page 2 goes on selecting to page 1's edge while the pointer is in the gap,
    // rather than freezing where the page ended or jumping to whatever `toDocument` found nothing at.
    const core::render::PageSlot* nearest = nullptr;
    double best = 0.0;
    for (const core::render::PageSlot& slot : layout.pageSlots()) {
        const double dx = std::max({slot.rect.left() - content.x, 0.0, content.x - slot.rect.right()});
        const double dy = std::max({slot.rect.top() - content.y, 0.0, content.y - slot.rect.bottom()});
        const double distance = dx * dx + dy * dy;
        if (nearest == nullptr || distance < best) {
            nearest = &slot;
            best = distance;
        }
    }
    if (nearest == nullptr) return {};
    const core::DevicePoint inside{std::clamp(content.x, nearest->rect.left(), nearest->rect.right()),
                                   std::clamp(content.y, nearest->rect.top(), nearest->rect.bottom())};
    const std::optional<core::PdfPoint> point = layout.toPage(nearest->pageIndex, inside);
    if (!point) return {};
    return PagePoint{nearest->pageIndex, *point};
}

PagePoint PageView::pagePointOn(int page, QPoint pos) const
{
    const std::optional<core::PdfPoint> point = engine_.layout().toPage(page, toContentPoint(pos));
    if (!point) return {};
    return PagePoint{page, *point};
}

void PageView::setInvertColors(bool invert)
{
    if (invertColors_ == invert) return;
    invertColors_ = invert;

    // A REPAINT AND NOTHING ELSE. No layout, no `refresh()`, no tile request: the pixels are the
    // ones already in the cache and the map is applied on the way to the screen (D4-11).
    viewport()->update();
}

ViewportController& PageView::ToolTarget::controller()
{
    return view_.scrollTarget_ != nullptr ? view_.scrollTarget_->controller() : view_.controller();
}

const ViewportController& PageView::ToolTarget::controller() const
{
    return view_.scrollTarget_ != nullptr ? view_.scrollTarget_->controller() : view_.controller();
}

void PageView::ToolTarget::recordView()
{
    // The history belongs to the view that moved, and a frozen pane's tools move the body.
    if (view_.scrollTarget_ != nullptr) view_.scrollTarget_->recordView();
    else view_.recordView();
}

void PageView::setFrozen(PageView* body)
{
    scrollTarget_ = (body == this) ? nullptr : body;
    const bool frozen = scrollTarget_ != nullptr;
    setFocusPolicy(frozen ? Qt::NoFocus : Qt::StrongFocus);

    // No scrollbars. A frozen pane's wheel never reaches them: `wheelEvent` re-sends it to the body.
    const Qt::ScrollBarPolicy policy = frozen ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded;
    setHorizontalScrollBarPolicy(policy);
    setVerticalScrollBarPolicy(policy);
    if (!frozen) setScrollBarReserve(0, 0);
}

void PageView::setScrollBarReserve(int right, int bottom)
{
    const QMargins wanted(0, 0, std::max(0, right), std::max(0, bottom));
    if (viewportMargins() == wanted) return;
    setViewportMargins(wanted);

    // Refreshed HERE rather than left to the viewport's resize event, which a widget that is not
    // yet shown receives only later -- and the layout, the scrollbar ranges and the clamp all read
    // the viewport's size.
    refresh();
}

void PageView::setBackgroundWindow(bool background)
{
    if (presenter_ == nullptr) return;

    // THE FLAG AND NOTHING ELSE -- no `refresh()`, no re-request. Two reasons, and the second is
    // the one that was measured rather than reasoned:
    //
    //   * **Nothing already queued should move.** `RenderScheduler::enqueue` raises a job's
    //     priority when a higher request joins it and never lowers one, so re-asking at `Prefetch`
    //     could not demote anything anyway -- it would only spend a pass building requests that
    //     coalesce with what is there. The lane is a property of the NEXT request, which every
    //     scroll, zoom and repaint makes.
    //   * **This is called for EVERY window, from `MainWindow::refreshWindowFocusState`.** A
    //     function that laid out and asked for tiles on behalf of a window the caller is not in
    //     would reach that window's backend and scheduler from a call about focus -- work in an
    //     object the caller has no business driving. Setting a flag cannot.
    //
    // The cost is stated rather than hidden: a tab adopted INTO a background window asks for its
    // first viewport before the sweep reaches it, so that one round goes into the `Visible` lane.
    // It is one viewport of one document, once, and the alternative is the paragraph above.
    presenter_->setBackground(background);
}

void PageView::mousePressEvent(QMouseEvent* event)
{
    // A GUIDE IS PICKED UP BEFORE THE TOOL SEES THE PRESS, and that order is the decision. Every
    // tool takes a press anywhere on the surface -- the hand pans from any pixel, and since Stage 5 T5
    // the Select tool starts a text selection from any pixel -- so a tool-first order would make a
    // guide impossible to grab at all. The guide is a thin line the person aimed at; the tool has the
    // rest of the page.
    if (event->button() == Qt::LeftButton) {
        const int guide = guides_->guideAt(engine_.layout(), toContentPoint(event->pos()));
        if (guide >= 0 && guides_->beginDrag(guide)) {
            event->accept();
            return;
        }
    }

    // THE CLICK COUNT (S5 T5, D5-8): a press within the platform's double-click interval and drag
    // distance of the last one is the next click of the same gesture, up to four -- word, line, block --
    // and a fifth starts again. Counted here rather than from Qt's double-click event, which stops at
    // two. Qt delivers the second press of each pair as a double click INSTEAD of a press, and
    // `QAbstractScrollArea` ignores one, so `mouseDoubleClickEvent` below hands it here -- until it did, a
    // double click selected nothing and a triple click a word (S5 guide I-14).
    if (event->button() == Qt::LeftButton) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const QStyleHints* hints = QGuiApplication::styleHints();
        const int interval = hints != nullptr ? hints->mouseDoubleClickInterval() : 400;
        const int slop = hints != nullptr ? hints->startDragDistance() : 4;
        const bool continues = clicks_ > 0 && clicks_ < 4 && now - lastPressMs_ <= interval
                               && (event->pos() - lastPressPos_).manhattanLength() <= slop;
        clicks_ = continues ? clicks_ + 1 : 1;
        lastPressMs_ = now;
        lastPressPos_ = event->pos();
    }

    // THE ANNOTATIONS BEFORE THE TOOL (S6 T5): with the Selection tool or an annotation tool armed, a press
    // on an annotation, one of its handles or a pop-up is the overlay's -- a pick, a drag, a resize. A press
    // anywhere else is the tool's, as it always was. A temporary hand still reaches the overlay first:
    // `Shift` over an annotation constrains its drag (§8), and over the page pans.
    // S6 T7: A DOUBLE CLICK ON FREE TEXT EDITS IT IN PLACE -- before the overlay, which would pick it up.
    if (event->button() == Qt::LeftButton && overlayFirst() && clicks_ == 2 && editTypedTextAt(event->pos())) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && overlayFirst()
        && annotations_->press(annotationViewport(), event->pos(), event->modifiers(), clicks_)) {
        event->accept();
        return;
    }

    if (!tools_->press(event->pos(), event->button(), event->modifiers(), clicks_)) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    event->accept();
}

void PageView::mouseDoubleClickEvent(QMouseEvent* event)
{
    // A double click is a PRESS -- the second of a gesture, which the counter above recognises by its
    // time and place like any other. `QAbstractScrollArea`'s own handler ignores the event, not
    // `QWidget`'s, which would have made it a press (S5 guide I-14).
    mousePressEvent(event);
}

void PageView::mouseMoveEvent(QMouseEvent* event)
{
    // The rulers' marker follows every move, dragging or not -- it is where the HAND is, not what
    // the hand is doing.
    emit cursorMoved(event->pos(), true);

    if (guides_->dragging()) {
        guides_->dragTo(engine_.layout(), toContentPoint(event->pos()));
        event->accept();
        return;
    }

    // An annotation gesture in flight is the overlay's; with no button down, a hover over a note (D6-28).
    if (annotations_ != nullptr) {
        if (annotations_->move(annotationViewport(), event->pos(), event->modifiers())) {
            event->accept();
            return;
        }
        if (event->buttons() == Qt::NoButton) annotations_->hover(annotationViewport(), event->pos());
    }

    if (!tools_->move(event->pos(), event->modifiers())) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    event->accept();
}

void PageView::mouseReleaseEvent(QMouseEvent* event)
{
    if (guides_->dragging() && event->button() == Qt::LeftButton) {
        guides_->endDrag();
        event->accept();
        return;
    }

    if (annotations_ != nullptr && event->button() == Qt::LeftButton
        && annotations_->release(annotationViewport(), event->pos(), event->modifiers())) {
        event->accept();
        return;
    }

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

    // **THE ZOOM IS SETTLED BEFORE THE PAGE IS MEASURED, and only where a fit is active.** A fit is
    // re-satisfied on every `refresh()` against whichever page is CURRENT, so a fit whose zoom
    // depends on the page changes the layout underneath a scroll offset taken from the old one --
    // and the dominant-page snap at the end of that refresh then reports a page nobody navigated to.
    //
    // It was invisible until T12. Fit Page and Fit Width give every page of a uniform document the
    // same zoom, so the stale offset was never stale by anything; **Fit Visible's zoom is per page
    // by definition** (D4-14), and the first version of this task's destination path walked a
    // bookmark to page 2 onto page 4 through exactly this feedback -- fit, re-layout, snap, re-fit.
    //
    // Guarded on the fit mode rather than run always: with no fit the zoom cannot move, the layout
    // cannot change, and a second `refresh()` per arrow key would be a tile pass nobody needs.
    if (controller_->fitMode() != FitMode::None) {
        // The anchor moves WITH the navigation: going to a page in a fit means fitting THAT page,
        // which is the whole gesture -- in every fit since I-11, not only in Fit Visible.
        fitAnchorPage_ = target;
        refresh();
        controller_->setCurrentPage(target);   // the snap inside that refresh may have moved it
    }

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
    if (controller_->fitMode() == FitMode::None) {
        // A zoom the controller chose by itself -- a step, the wheel, a marquee -- ends the fit, and
        // with it the anchor, so a later fit set by any path anchors afresh.
        fitAnchorPage_ = -1;
    } else {
        // **WHICH PAGE THE FIT MEASURES: THE ONE IT WAS CHOSEN ON, NOT THE CURRENT ONE.**
        //
        // A fit is re-satisfied here on every refresh -- that is what makes it a mode rather than a
        // remembered number, and what makes it survive a window resize. Measuring the CURRENT page
        // closes a loop -- zoom decides the layout, the layout decides which page is dominant, the
        // dominant page decides the zoom -- and the loop was measured twice, not imagined:
        //
        //   * T12, Fit Visible (D4-14), whose zoom differs page by page even in a uniform document:
        //     a `/FitB` destination to page 1 settled on page 4's fallback zoom, and a bookmark to
        //     page 2 landed on page 4. Fit Visible was anchored then, and only Fit Visible.
        //   * **I-11**, Fit Width and Fit Page, which T12 had called safe because a uniform document
        //     gives every page the same zoom. `mixed-page-sizes.pdf` is not uniform: `Ctrl+2` from
        //     400 % on its landscape page settled on the A5 page's width -- half as wide, so twice
        //     the zoom -- and drew the landscape page at twice the window's width. M-01, 2026-09-26.
        //
        // So EVERY fit is anchored to the page it was set for, and the anchor moves only when a
        // navigation moves it. The zoom is then stable, the layout is stable, and the snap has
        // nothing to feed back into -- and a reader scrolling through a mixed document keeps the zoom
        // they chose instead of having it jump at every page boundary. A fit set on the controller
        // directly (a path that never saw `setFitMode`) anchors to the page current when it arrives.
        if (fitAnchorPage_ < 0 || fitAnchorPage_ >= pageCount())
            fitAnchorPage_ = controller_->currentPage();
        const int fitPage = fitAnchorPage_;

        const core::render::Zoom fitted =
            engine_.zoomForFit(controller_->fitMode(), controller_->viewportSize(), fitPage,
                               metrics, controller_->zoom());

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
    // `Alt`+WHEEL IS THE LOUPE'S MAGNIFICATION while the Loupe is in effect (decision D4-22), and it
    // is taken FIRST -- before a frozen pane hands its wheel to the body -- because the lens being
    // magnified is this pane's. It scrolls and zooms nothing. `Ctrl` with it is left to the zoom.
    //
    // WHICHEVER AXIS MOVED: Qt reports an `Alt`+wheel as a HORIZONTAL one on some platforms, so the
    // notch is read from `y` and, failing that, from `x`. Sub-notch deltas from a precise wheel are
    // gathered until they make a notch, so a smooth wheel steps a rung per notch too.
    const Qt::KeyboardModifiers held = event->modifiers();
    if (held.testFlag(Qt::AltModifier) && !held.testFlag(Qt::ControlModifier)
        && tools_->effective() == ToolKind::Loupe) {
        const QPoint delta = event->angleDelta();
        loupeWheel_ += delta.y() != 0 ? delta.y() : delta.x();
        const int notches = static_cast<int>(loupeWheel_ / kWheelNotch);
        if (notches != 0) {
            loupeWheel_ -= notches * kWheelNotch;
            (void)tools_->stepLoupeMagnification(notches);
        }
        event->accept();
        return;
    }

    // A FROZEN PANE'S WHEEL IS THE BODY'S (I-20), scroll and zoom alike, and it is handed over
    // BEFORE this pane moves -- a pane frozen at the top of the document has nowhere to scroll up
    // to, and measuring its movement afterwards was I-19's first fix and the reason a wheel up over
    // the header row did nothing. The position is the pointer's, in the body's viewport, pulled
    // inside it: over the frozen row the x is exactly the body's (they share it), and a zoom about a
    // point above the body anchors on the body's top edge rather than on content it is not showing.
    if (scrollTarget_ != nullptr) {
        QWidget* target = scrollTarget_->viewport();
        const QPointF global = event->globalPosition();
        const QPointF local = target->mapFromGlobal(global);
        const QPointF inside(std::clamp(local.x(), 0.0, std::max(0.0, target->width() - 1.0)),
                             std::clamp(local.y(), 0.0, std::max(0.0, target->height() - 1.0)));
        QWheelEvent forwarded(inside, target->mapToGlobal(inside), event->pixelDelta(),
                              event->angleDelta(), event->buttons(), event->modifiers(),
                              event->phase(), event->inverted(), event->source());
        QCoreApplication::sendEvent(target, &forwarded);
        event->accept();
        return;
    }

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

void PageView::keyReleaseEvent(QKeyEvent* event)
{
    // LETTING GO OF THE MODIFIER ENDS THE TEMPORARY HAND AT ONCE (S4 guide I-27) -- the armed
    // tool's cursor and, for the loupe, its lens come back without waiting for the pointer to move.
    // The released key is counted as UP, whatever the platform reports on its own release, and a
    // release while the pointer is elsewhere still reaches nothing here, which A-41 already covers.
    tools_->setModifiers(event->key() == Qt::Key_Shift ? event->modifiers() & ~Qt::ShiftModifier
                                                       : event->modifiers());
    QAbstractScrollArea::keyReleaseEvent(event);
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
        // A GUIDE DRAG IS CANCELLED FIRST, for the reason the press is taken first: the two
        // gestures cannot both be live, and the guide's is the one in front of the person. It
        // puts the guide back where the drag started, which is the same promise `Esc` makes to a
        // tool (S4 T8).
        if (guides_->cancelDrag()) {
            event->accept();
            return;
        }
        // AN ANNOTATION GESTURE IN FLIGHT (S6 T5): abandoned, nothing constructed -- then the tool, which
        // puts an annotation tool away to the Selection tool (§8), then a selected annotation let go.
        if (annotations_ != nullptr && annotations_->cancel()) {
            event->accept();
            return;
        }
        if (tools_->cancel()) {
            event->accept();
            return;
        }
        if (annotations_ != nullptr && annotations_->clearSelection()) {
            event->accept();
            return;
        }

        // THEN THE SELECTION (S5 T5): a copy still extracting its pages is cancelled first -- the clipboard
        // is left as it was (guide A-51) -- and otherwise a selection is cleared. With neither, the key is
        // NOT consumed, which is Stage 4's A-42 control kept (guide A-54): `edit.cancel` stays
        // unregistered, and `Esc` is cleared "where the selection is", which is here (D5-8).
        if (selection_ != nullptr && (selection_->cancelPendingCopy() || selection_->clear())) {
            event->accept();
            return;
        }
    }

    // EVERY KEY RE-DERIVES THE MODIFIER STATE, which is what makes the temporary hand reliable:
    // a modifier released while the pointer is outside this widget produces no release event
    // here, so the state is read from whatever arrives next rather than remembered (A-41). The
    // modifier KEY ITSELF is counted as held, whatever the platform reports on its own press.
    tools_->setModifiers(event->key() == Qt::Key_Shift ? event->modifiers() | Qt::ShiftModifier
                                                       : event->modifiers());

    // THE ARROW KEYS ARE `RAWPAGE_BINDING_NATIVE` ROWS (`core/cmd/KeyBindingDefaults.def`): the
    // focused widget owns them, so the boundary behaviour `Features.md` §8 specifies for them --
    // *"Scroll the page (paging at page boundaries)"* -- is this widget's to implement and not an
    // `ActionId`'s. Minting four permanent public names for a scroll gesture is what D1-16 warns
    // against; `NavNextPage` already exists for the dispatchable version.
    //
    // THE ARROWS NUDGE A SELECTED ANNOTATION (S6 T5, D6-9): the overlay owns them while one is selected, a
    // bare arrow a point and `Ctrl` with it a tenth, along the SCREEN's directions; a burst is one move.
    // With nothing selected they scroll, below, exactly as before.
    if (annotations_ != nullptr && annotations_->hasSelection()
        && (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ControlModifier)) {
        int dx = 0;
        int dy = 0;
        switch (event->key()) {
        case Qt::Key_Down:  dy = 1; break;
        case Qt::Key_Up:    dy = -1; break;
        case Qt::Key_Right: dx = 1; break;
        case Qt::Key_Left:  dx = -1; break;
        default: break;
        }
        if ((dx != 0 || dy != 0)
            && annotations_->nudge(annotationViewport(), dx, dy, event->modifiers() == Qt::ControlModifier)) {
            event->accept();
            return;
        }
    }

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

void PageView::focusOutEvent(QFocusEvent* event)
{
    // A BURST OF NUDGES ENDS WHEN THE FOCUS LEAVES (D6-9), not half a second later in a pane nobody is in.
    if (annotations_ != nullptr) (void)annotations_->commitNudge();
    QAbstractScrollArea::focusOutEvent(event);
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

void PageView::contextMenuEvent(QContextMenuEvent* event)
{
    // The position arrives in THIS widget's coordinates; the guides are hit-tested in the
    // viewport's, which differ by the scroll area's frame.
    const QPoint inViewport = viewport()->mapFrom(this, event->pos());
    const int guide = guides_->guideAt(engine_.layout(), toContentPoint(inViewport));
    if (guide < 0) {
        // NOT SWALLOWED. Stage 5's text surface and Stage 6's annotations both want this event,
        // and a widget that accepted every right-click would be the reason their menus never
        // appeared (the same argument the unregistered `edit.cancel` records for a key).
        QAbstractScrollArea::contextMenuEvent(event);
        return;
    }

    emit guideContextMenuRequested(guide, event->globalPos());
    event->accept();
}

bool PageView::viewportEvent(QEvent* event)
{
    if (event->type() == QEvent::Leave) {
        emit cursorMoved(QPoint(), false);
        if (annotations_ != nullptr) annotations_->hover(annotationViewport(), std::nullopt);
    }
    return QAbstractScrollArea::viewportEvent(event);
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

    const core::DeviceRect visible = controller_->visibleRect();
    const double dpr = devicePixelRatioF();

    // INVERSION, OVER THE PAGES AND NOT OVER THE DESK (D4-11). `RasterOp_NotDestination` is an
    // exact bitwise complement, so toggling twice restores the pixels rather than approximately
    // restoring them -- which is what A-52 means by *"toggling back is exact"* and what a
    // composition mode with an alpha would not give. Each page's sheet is inverted separately:
    // inverting the whole viewport would turn the surround black and leave the page the only
    // thing that looked normal.
    if (invertColors_) {
        painter.save();
        painter.setCompositionMode(QPainter::RasterOp_NotDestination);
        for (const core::render::PageSlot& slot : engine_.layout().slotsIntersecting(visible)) {
            painter.fillRect(QRectF((slot.rect.left() - visible.left()) / dpr,
                                    (slot.rect.top() - visible.top()) / dpr,
                                    slot.rect.size.width / dpr, slot.rect.size.height / dpr),
                             Qt::white);
        }
        painter.restore();
    }

    // THE SELECTION, OVER THE INVERSION AND UNDER THE GUIDES (S5 T5). Painted, never rasterized (D4-11);
    // over the inversion so its colour is the theme's highlight on either page, and under the guides so a
    // guide stays readable through it.
    //
    // SEARCH HITS UNDER THE SELECTION (S5 T7): the find bar's highlights are painted the same way and for
    // the same reason, and under the selection so a selection made over a hit still reads as one.
    // A COMPARISON'S MARKS (S5 T13, D5-18), over the inversion and under the selection and the hits, for the
    // hits' reasons: painted, never rasterized, and a selection made over one still reads as a selection.
    if (!marks_.empty()) {
        painter.save();
        for (const PageMark& mark : marks_) {
            if (engine_.layout().slotForPage(mark.page) == nullptr) continue;
            const std::optional<QRectF> at = widgetRectOf(mark.page, mark.rect);
            if (!at || !at->intersects(QRectF(area))) continue;
            painter.fillRect(*at, mark.colour);
            if (mark.outlined) {
                QColor edge = mark.colour;
                edge.setAlpha(255);
                painter.setPen(QPen(edge, 2.0));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(at->adjusted(-1.0, -1.0, 1.0, 1.0));
            }
        }
        painter.restore();
    }

    if (selection_ != nullptr) {
        selection_->paintSearchHits(painter, engine_.layout(), visible, dpr, searchHitColour(),
                                    currentSearchHitColour());
        selection_->paint(painter, engine_.layout(), visible, dpr, selectionColour(), selectionCompositionMode());
    }

    // The grid and the guides go OVER the inversion, so a guide keeps its colour on an inverted
    // page rather than becoming its complement.
    guides_->paint(painter, engine_.layout(), visible, dpr);

    // THE ANNOTATIONS' SELECTION, A GESTURE'S PREVIEW AND THE POP-UPS (S6 T5), over everything: they are
    // what the person is handling. Painted, never rasterized (D4-11, D6-28) -- a snapshot has none of them.
    if (annotations_ != nullptr) annotations_->paint(painter, annotationViewport());
}

// THE PAGE'S COLOURS, NOT THE THEME'S -- and that is the argument for constants here where the selection
// takes the palette. A hit is drawn on the DOCUMENT, whose paper is the document's colour in both themes
// (convention 20 judges the chrome; the page is not chrome), so a translucent amber reads on white paper
// in either theme, and on an inverted page, where it is drawn over the inversion like the selection. The
// current hit is a deeper orange, so it stands out from the others when all are highlighted.
QColor PageView::searchHitColour()
{
    return QColor(255, 200, 0, 110);
}

QColor PageView::currentSearchHitColour()
{
    return QColor(255, 120, 0, 150);
}

void PageView::revealPageRect(int pageIndex, const core::PdfRect& rect)
{
    // ALL FOUR CORNERS through the layout's own mapping: under a quarter turn the PDF bottom-left is not
    // the device top-left (`SelectionOverlay`'s `toContent` makes the same argument).
    double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
    bool first = true;
    for (const core::PdfPoint corner : {core::PdfPoint{rect.left(), rect.bottom()}, core::PdfPoint{rect.right(), rect.top()},
                                        core::PdfPoint{rect.left(), rect.top()}, core::PdfPoint{rect.right(), rect.bottom()}}) {
        const std::optional<core::DevicePoint> device = engine_.layout().toContent(pageIndex, corner);
        if (!device) return;
        if (first) {
            left = right = device->x;
            top = bottom = device->y;
            first = false;
        } else {
            left = std::min(left, device->x);
            right = std::max(right, device->x);
            top = std::min(top, device->y);
            bottom = std::max(bottom, device->y);
        }
    }

    const core::DeviceRect visible = controller_->visibleRect();
    const core::DevicePoint now = controller_->scroll();
    const double margin = engine_.options().margin;
    double x = now.x;
    double y = now.y;
    if (left < visible.left() || right > visible.right()) {
        // The least horizontal move that shows it, its left edge kept in view when it is wider than the view.
        x = (right - left + 2 * margin > visible.size.width || left < visible.left()) ? left - margin
                                                                                       : right + margin - visible.size.width;
    }
    if (top < visible.top() || bottom > visible.bottom()) y = top - visible.size.height / 3.0;
    if (x == now.x && y == now.y) return;
    controller_->setScroll(core::DevicePoint{x, y});
    refresh();
}

void PageView::setPageMarks(std::vector<PageMark> marks)
{
    marks_ = std::move(marks);
    viewport()->update();
}

std::optional<QRectF> PageView::widgetRectOf(int pageIndex, const core::PdfRect& rect) const
{
    // ALL FOUR CORNERS through the layout's own mapping (`revealPageRect`'s argument): under a quarter turn
    // the PDF bottom-left is not the device top-left.
    double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
    bool first = true;
    for (const core::PdfPoint corner : {core::PdfPoint{rect.left(), rect.bottom()}, core::PdfPoint{rect.right(), rect.top()},
                                        core::PdfPoint{rect.left(), rect.top()}, core::PdfPoint{rect.right(), rect.bottom()}}) {
        const std::optional<core::DevicePoint> device = engine_.layout().toContent(pageIndex, corner);
        if (!device) return std::nullopt;
        if (first) {
            left = right = device->x;
            top = bottom = device->y;
            first = false;
        } else {
            left = std::min(left, device->x);
            right = std::max(right, device->x);
            top = std::min(top, device->y);
            bottom = std::max(bottom, device->y);
        }
    }
    const core::DeviceRect visible = controller_->visibleRect();
    const double dpr = devicePixelRatioF() > 0.0 ? devicePixelRatioF() : 1.0;
    return QRectF((left - visible.left()) / dpr, (top - visible.top()) / dpr, (right - left) / dpr,
                  (bottom - top) / dpr);
}

// THE SELECTION TINTS THE PAPER (S5 guide I-15) -- the page's colours, not the theme's, on
// `searchHitColour`'s argument. Until I-15 this was the palette's `Highlight` at alpha 96, and in the light
// theme that token is a pale tint meant to be drawn OPAQUE behind list text: over white paper it came out
// `#ECF3FB`, 1.12:1 from the page, and it greyed the glyphs to 5.1:1. Now the colour is MULTIPLIED onto
// the page (`selectionCompositionMode`): white paper becomes exactly this blue and black ink stays black.
// On an inverted page the paper is black, which a multiply leaves black, so there the colour is SCREENED
// on: the black paper becomes a dark blue and the white glyphs stay white. Each is measured by
// `tst_selectionink`'s `[I-15]` case against WCAG's ratios.
QColor PageView::selectionColour() const
{
    return invertColors_ ? QColor(0x1a, 0x44, 0x7a) : QColor(0x99, 0xc4, 0xf0);
}

QPainter::CompositionMode PageView::selectionCompositionMode() const
{
    return invertColors_ ? QPainter::CompositionMode_Screen : QPainter::CompositionMode_Multiply;
}

PageView::LoupeRegion PageView::loupeRegionFor(const QRectF& source) const
{
    LoupeRegion region;
    if (!document_.isValid() || pages_ == nullptr || pages_->isEmpty() || source.isEmpty()) return region;

    const core::render::PageLayout& layout = engine_.layout();
    const core::DeviceRect visible = controller_->visibleRect();
    const double dpr = devicePixelRatioF();
    const double magnification = tools_->loupe().magnification();   // the rung `Alt`+wheel set (D4-22)

    // THE LENS'S SOURCE IN CONTENT SPACE -- device pixels, the scroll added back. The inverse of the
    // one conversion `TilePresenter::paint` makes, and written the same way round.
    const QRectF content(visible.left() + source.x() * dpr, visible.top() + source.y() * dpr,
                         source.width() * dpr, source.height() * dpr);

    // THE PAGE THE LENS IS OVER: the one it overlaps most. A source over the gap between two pages,
    // or the desk beside one, asks for nothing, and one straddling an edge asks only for its part.
    const core::DeviceRect sourceRect{core::DevicePoint{content.left(), content.top()},
                                      core::DeviceSize{content.width(), content.height()}};
    const core::render::PageSlot* best = nullptr;
    double bestArea = 0.0;
    const std::vector<core::render::PageSlot> candidates = layout.slotsIntersecting(sourceRect);
    for (const core::render::PageSlot& slot : candidates) {
        const QRectF sheet(slot.rect.left(), slot.rect.top(), slot.rect.size.width, slot.rect.size.height);
        const QRectF part = sheet.intersected(content);
        const double area = part.width() * part.height();
        if (area > bestArea) {
            bestArea = area;
            best = &slot;
        }
    }
    if (best == nullptr || bestArea <= 0.0) return region;

    // THE REGION IN THE PAGE'S OWN RASTER SPACE AT THE MAGNIFIED SCALE: sheet-relative, times the
    // magnification, rounded OUTWARD so the raster covers the whole of what the lens shows, and
    // clamped to the page, whose raster at that scale is `ceil(extent x magnification)` -- the
    // rasterizer refuses a rectangle that starts beyond the page (`PdfiumRasterizer`).
    const QRectF sheet(best->rect.left(), best->rect.top(), best->rect.size.width, best->rect.size.height);
    const QRectF part = sheet.intersected(content);
    const int pageWidth = static_cast<int>(std::ceil(sheet.width() * magnification));
    const int pageHeight = static_cast<int>(std::ceil(sheet.height() * magnification));
    const int x0 = std::clamp(static_cast<int>(std::floor((part.left() - sheet.left()) * magnification)), 0, pageWidth);
    const int y0 = std::clamp(static_cast<int>(std::floor((part.top() - sheet.top()) * magnification)), 0, pageHeight);
    const int x1 = std::clamp(static_cast<int>(std::ceil((part.right() - sheet.left()) * magnification)), 0, pageWidth);
    const int y1 = std::clamp(static_cast<int>(std::ceil((part.bottom() - sheet.top()) * magnification)), 0, pageHeight);
    if (x1 <= x0 || y1 <= y0 || x0 >= pageWidth || y0 >= pageHeight) return region;

    const core::render::TileRect tile{x0, y0, x1 - x0, y1 - y0};
    region.request = presenter_->requestAt(best->pageIndex, tile, layout.presentedScale() * magnification,
                                           controller_->viewRotation());

    // WHERE THAT RASTER SITS IN THE VIEWPORT, in widget pixels: back to content space by dividing
    // out the magnification, then the same conversion `TilePresenter::paint` makes.
    region.widget = QRectF((sheet.left() + x0 / magnification - visible.left()) / dpr,
                           (sheet.top() + y0 / magnification - visible.top()) / dpr,
                           (tile.width / magnification) / dpr, (tile.height / magnification) / dpr);
    return region;
}

PageView::SnapshotRegion PageView::snapshotRegionFor(const QRect& band) const
{
    SnapshotRegion region;
    if (!document_.isValid() || pages_ == nullptr || pages_->isEmpty() || band.isEmpty()) return region;

    const core::render::PageLayout& layout = engine_.layout();
    const core::DeviceRect visible = controller_->visibleRect();
    const double dpr = devicePixelRatioF();
    const double presented = layout.presentedScale();
    if (presented <= 0.0) return region;

    // THE BAND IN CONTENT SPACE, and THE PAGE IT IS OVER -- the one it overlaps most, clipped to it: the
    // loupe's rule (`loupeRegionFor`), so a band straddling two pages copies the one it is mostly over and
    // a band over the desk copies nothing. A raster is of ONE page; the desk and the gap are not the
    // document's appearance.
    const QRectF content(visible.left() + band.x() * dpr, visible.top() + band.y() * dpr, band.width() * dpr,
                         band.height() * dpr);
    const core::DeviceRect bandRect{core::DevicePoint{content.left(), content.top()},
                                    core::DeviceSize{content.width(), content.height()}};
    const core::render::PageSlot* best = nullptr;
    double bestArea = 0.0;
    const std::vector<core::render::PageSlot> candidates = layout.slotsIntersecting(bandRect);
    for (const core::render::PageSlot& slot : candidates) {
        const QRectF sheet(slot.rect.left(), slot.rect.top(), slot.rect.size.width, slot.rect.size.height);
        const QRectF part = sheet.intersected(content);
        const double area = part.width() * part.height();
        if (area > bestArea) {
            bestArea = area;
            best = &slot;
        }
    }
    if (best == nullptr || bestArea <= 0.0) return region;

    const QRectF sheet(best->rect.left(), best->rect.top(), best->rect.size.width, best->rect.size.height);
    const QRectF part = sheet.intersected(content);

    // A SLIVER OF A PAGE IS NOT A SNAPSHOT, by the rule that makes a click not one: a band dragged over the desk
    // that clips a page's edge by two pixels would otherwise copy a two-pixel image. The tool refuses a band
    // under the marquee's threshold; the PART of it on the page is held to the same one here, in the same
    // logical pixels, because only the view knows where the page is.
    const double minDevice = MarqueeZoomTool::kMinDragPixels * dpr;
    if (part.width() < minDevice || part.height() < minDevice) return region;

    // THE RESOLUTION, from the region's size in POINTS -- the view's presented pixels divided out -- and
    // `pageDisplay/snapshotDpi`, 0 meaning this view's own device scale (D5-11).
    region.widthPoints = part.width() / presented;
    region.heightPoints = part.height() / presented;
    region.resolution = SnapshotTool::resolve(region.widthPoints, region.heightPoints, settings_.snapshotDpi,
                                              presented);
    const double factor = region.resolution.deviceScale / presented;

    // THE REGION IN THE PAGE'S RASTER AT THAT SCALE: sheet-relative, scaled, rounded OUTWARD and clamped to
    // the page, exactly as the loupe's -- the rasterizer refuses a rectangle that starts beyond the page.
    // The sheet is already in the view's rotation, and so is the raster `requestAt` asks for.
    const int pageWidth = static_cast<int>(std::ceil(sheet.width() * factor));
    const int pageHeight = static_cast<int>(std::ceil(sheet.height() * factor));
    const int x0 = std::clamp(static_cast<int>(std::floor((part.left() - sheet.left()) * factor)), 0, pageWidth);
    const int y0 = std::clamp(static_cast<int>(std::floor((part.top() - sheet.top()) * factor)), 0, pageHeight);
    const int x1 = std::clamp(static_cast<int>(std::ceil((part.right() - sheet.left()) * factor)), 0, pageWidth);
    const int y1 = std::clamp(static_cast<int>(std::ceil((part.bottom() - sheet.top()) * factor)), 0, pageHeight);
    if (x1 <= x0 || y1 <= y0 || x0 >= pageWidth || y0 >= pageHeight) return region;

    const core::render::TileRect tile{x0, y0, x1 - x0, y1 - y0};
    region.request = presenter_->requestAt(best->pageIndex, tile, region.resolution.deviceScale,
                                           controller_->viewRotation());
    region.widget = QRectF((sheet.left() + x0 / factor - visible.left()) / dpr,
                           (sheet.top() + y0 / factor - visible.top()) / dpr, (tile.width / factor) / dpr,
                           (tile.height / factor) / dpr);
    return region;
}

bool PageView::takeSnapshot(const QRect& band)
{
    const SnapshotRegion region = snapshotRegionFor(band);
    if (!region.request.isValid()) return false;

    // ONE SNAPSHOT AT A TIME: a second band before the first arrived replaces it, and the first is
    // cancelled rather than delivered over the second (`LoupeRaster::aim`).
    snapshotResolution_ = region.resolution;
    snapshotRaster_->clear();
    snapshotRaster_->aim(region.request);
    return true;
}

void PageView::deliverSnapshot()
{
    const QImage* held = snapshotRaster_->imageFor(snapshotRaster_->aimed());
    if (held == nullptr) return;

    // A COPY, because the held image BORROWS the raster's bytes (`HeldRaster`) and the clipboard outlives
    // this view -- and ONTO WHITE, because `pageDisplay/transparencyGrid` asks the engine for a transparent
    // page, and a snapshot pasted with an alpha channel shows black in most editors. The checkerboard is how
    // the reader chose to look at the page; the sheet the document is drawn on is white (D5-11).
    QImage image(held->size(), QImage::Format_RGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.drawImage(0, 0, *held);
    }
    snapshotRaster_->clear();

    if (QClipboard* clipboard = QGuiApplication::clipboard()) clipboard->setImage(image);
    emit snapshotCopied(image.width(), image.height(), snapshotResolution_.dpi(), snapshotResolution_.reduced,
                        snapshotResolution_.askedDpi());
}

bool PageView::paintSharpLens(QPainter& painter, const QRect& lens, const QRectF& source) const
{
    const LoupeRegion region = loupeRegionFor(source);

    // OVER NO PAGE: nothing to ask for, and nothing waiting either.
    if (!region.request.isValid()) {
        loupeSettle_->stop();
        pendingLoupe_ = core::render::RenderRequest{};
        return false;
    }

    const QImage* image = loupeRaster_->imageFor(region.request);
    if (image == nullptr) {
        // THE REGION MOVED ON, OR IS NEW. What was aimed at is cancelled at once rather than when the
        // new region is asked for -- a lens swept across a page must not leave a job in front of the
        // viewport's tiles (D4-20) -- and the new region starts its settle interval, restarted only
        // when the region CHANGES, so a repaint of a lens standing still does not postpone it.
        if (loupeRaster_->aimed().isValid() && !(loupeRaster_->aimed() == region.request))
            loupeRaster_->clear();
        if (!(pendingLoupe_ == region.request)) {
            pendingLoupe_ = region.request;
            loupeSettle_->start();
        }
        return false;
    }

    // THE SHARP REGION, COMPOSED AS THE PAGE IS. The same mapping the enlarged frame is drawn with --
    // `source` onto `lens` -- so the sharp raster lands exactly where the enlarged one was, and under
    // it the raster is 1:1 with the screen's device pixels. Then the inversion and the grid and
    // guides, as `paintContent` lays them over the tiles, or the lens would show a page the view
    // does not (D4-11: none of the three is in the raster).
    const double scaleX = lens.width() / source.width();
    const double scaleY = lens.height() / source.height();
    QTransform map;
    map.translate(lens.left() - source.left() * scaleX, lens.top() - source.top() * scaleY);
    map.scale(scaleX, scaleY);

    painter.save();
    painter.setTransform(map, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(region.widget, *image);
    if (invertColors_) {
        painter.setCompositionMode(QPainter::RasterOp_NotDestination);
        painter.fillRect(region.widget, Qt::white);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    // And the selection and the search hits, as `paintContent` lays them -- the lens shows the page the view
    // shows (S5 T5, T7).
    if (selection_ != nullptr) {
        selection_->paintSearchHits(painter, engine_.layout(), controller_->visibleRect(), devicePixelRatioF(),
                                    searchHitColour(), currentSearchHitColour());
        selection_->paint(painter, engine_.layout(), controller_->visibleRect(), devicePixelRatioF(),
                          selectionColour(), selectionCompositionMode());
    }
    guides_->paint(painter, engine_.layout(), controller_->visibleRect(), devicePixelRatioF());
    painter.restore();
    return true;
}

void PageView::paintEvent(QPaintEvent* event)
{
    // TWO PATHS, AND THE SECOND ONE COSTS SOMETHING (S4 T7). Ordinarily the page is painted
    // straight onto the viewport. When the armed tool needs a FRAME -- only the loupe does -- the
    // same content goes into a pixmap first, which is then blitted and handed to the tool to
    // magnify. That is one extra viewport-sized copy per paint, paid only while the loupe is
    // armed and never otherwise. The enlarged frame is what the lens shows the instant it moves;
    // since D4-20 a SHARP re-rasterization of the lens's region is painted over it once the region
    // has been still -- `paintSharpLens`, and `ui/viewer/tools/LoupeTool.h` for why both.
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
    // leave a viewport-sized pixmap resident for the life of the tab -- and so is the lens's sharp
    // raster, and any region still waiting out the settle interval (D4-20).
    if (!frame_.isNull()) {
        frame_ = QPixmap();
        loupeSettle_->stop();
        pendingLoupe_ = core::render::RenderRequest{};
        loupeRaster_->clear();
    }

    QPainter painter(viewport());
    paintContent(painter, event->rect());
    tools_->paint(painter);
}

} // namespace rawpage::ui
