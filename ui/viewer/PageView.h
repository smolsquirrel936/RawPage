// The page surface. Stage 2 T13, and the widget the whole stage is named for.
//
// ---------------------------------------------------------------------------------------------
// WHAT IT OWNS AND WHAT IT DOES NOT.
//
// It owns three objects and no policy: a `ViewportController` (zoom, scroll, page, history), a
// `PageLayoutEngine` (where each page sits, at what resolution) and a `TilePresenter` (which tiles,
// and their pixels). It parses nothing, rasterizes nothing, and mutates nothing -- `ui/`'s entire
// mutation vocabulary is *construct a `Command`*, and Stage 2 has none, so this class is read-only
// by construction rather than by discipline.
//
// It is a `QAbstractScrollArea` because Qt's scroll area already solves the scrollbar policy, the
// wheel routing, the kinetic behaviour and the viewport clipping, and every one of those is a place
// a hand-rolled surface gets subtly wrong. What it does NOT use is `QScrollArea`'s widget-in-a-
// viewport model: a 900-page document at 6400 % is a 14-gigapixel widget, and Qt lays out and
// clips real widgets.
//
// ---------------------------------------------------------------------------------------------
// IT IS THE FIRST WIDGET IN THE PRODUCT TO DECLARE A KEYBOARD CONTEXT (D2-8).
//
// `ShortcutBinder::declareContext(this, KeyContext::Viewer)`. Stage 1 shipped the entire context
// resolution mechanism with nothing declaring a context, so `activeContext()` answered `Global` for
// every keystroke the product had ever seen -- the honest state, and completely untested against a
// real second context. The twelve deliberate chord collisions in `Features.md` §8 exist precisely
// so one chord means two things, and until this line not one of them had ever resolved differently.
//
// The Viewer COMMANDS are T14's. What T13 owes is that the context resolves, and that it resolves
// **only here** -- guide case A-56's negative: a Viewer-bound chord must not fire when the focus is
// in the tool finder's search box.
//
// ---------------------------------------------------------------------------------------------
// TWO COORDINATE SPACES MEET IN THIS FILE AND NOWHERE ELSE.
//
// Content space is presented DEVICE pixels; Qt's widget space is LOGICAL pixels. The conversion is
// the device-pixel ratio, and it lives in `TilePresenter::paint()` and in the four scrollbar
// updates here. On a 100 % monitor the two are equal, so an implementation that confuses them is
// correct on the machine it was written on and shows the page at two thirds size on a 150 % one.

#ifndef RAWPAGE_UI_VIEWER_PAGEVIEW_H
#define RAWPAGE_UI_VIEWER_PAGEVIEW_H

#include "core/backend.h"
#include "core/doc/Destinations.h"
#include "core/doc/Revision.h"
#include "core/render/PagePresentation.h"
#include "core/render/RenderRequest.h"
#include "ui/viewer/GridGuideLayer.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/ViewportController.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QPainter>
#include <QPixmap>
#include <QPoint>
#include <QRectF>

#include <memory>
#include <optional>
#include <vector>

namespace rawpage::core {
class PageTree;
}

namespace rawpage::core::render {
class RenderScheduler;
}

namespace rawpage::platform {
class IDisplayTopology;
}

class QTimer;

namespace rawpage::ui {

class LoupeRaster;
class SelectionOverlay;
class TilePresenter;

// The eleven `pageDisplay/` preferences, resolved into what a viewer actually needs. Stage 2 T15.
//
// **A STRUCT OF PLAIN VALUES, NOT A `Settings&`.** `core/render/PresentationOptions` makes the same
// choice and states the same reason: the composition root reads `Settings` and hands values down,
// so a widget can be driven by a test that has no settings file, and so nothing below `app/` has to
// know that a preference is where these came from. `ui/shell/DocumentWindow` is what reads them and
// what re-reads them when one changes.
//
// The defaults here are the ones `SettingsKeys.def` declares, restated for a caller that constructs
// one without a settings store -- **and that IS a second copy of a default**, which
// `SettingsKeys.def` rule 2 forbids for the value a user sees. It is tolerable only because these
// are the *absent-store* fallbacks for a test rather than the product's defaults: `DocumentWindow`
// always overwrites every field from the table. A case asserts the two agree, so the day they drift
// is a failure rather than a surprise.
struct ViewSettings {
    core::render::LayoutMode layoutMode = core::render::LayoutMode::SingleContinuous;
    core::render::FitMode defaultZoom = core::render::FitMode::None;
    bool showCoverInTwoUp = false;
    bool rtlBinding = false;

    bool lineWeight = false;        // declared and inert -- Stage 2 guide issue I-11
    bool antialiasText = true;
    bool antialiasImages = true;
    bool antialiasLineArt = true;
    bool transparencyGrid = false;

    int maxRenderDpi = 2400;
    bool wheelZooms = false;
    int prefetchPages = 1;

    // S4 T7. Device pixels per second, and the SIGN IS THE DIRECTION -- see
    // `core/settings/SettingsKeys.def`. It rides here with the eleven `pageDisplay/` keys because
    // `DocumentWindow` re-reads that whole group on every change, so the speed follows a
    // Preferences edit into a running auto-scroll without a second subscription.
    int autoScrollSpeed = 60;

    // S5 T10, D5-11: `pageDisplay/snapshotDpi`. 0 is AS DISPLAYED -- the view's device scale -- and 72 to
    // 600 a resolution in dots per inch; `SnapshotTool::resolve` is the one reading of it. Read when a
    // snapshot is taken, so a Preferences change reaches the next one without a re-layout.
    int snapshotDpi = 0;
};

class PageView : public QAbstractScrollArea, public ToolHost {
    Q_OBJECT

public:
    // `topology` may be null -- a build with no `IDisplayTopology`, or a test. The consequence is
    // the nominal `96 * scale` physical DPI rather than the panel's real one, which is exactly the
    // approximation every caller made before Stage 2 T12 and is recorded as such rather than
    // silently substituted.
    PageView(core::render::RenderScheduler& scheduler,
             const platform::IDisplayTopology* topology, QWidget* parent = nullptr);
    ~PageView() override;

    // The document to draw. `pages` is owned by the backend and must outlive this view; passing
    // null (with an invalid handle) is how a view is emptied.
    //
    // Stage 3 T10 (D3-15): `pages` is the MODEL -- `Document::pages()`, whose rotations include the
    // unsaved ones -- and `enginePages` is what the engine loaded. Layout reads the model; every tile
    // request carries the difference as `RenderRequest::pageRotationDelta`. Null `enginePages` means
    // they are the same tree, which is what a document that cannot be edited passes.
    void setDocument(core::DocumentHandle document, core::Revision revision,
                     const core::PageTree* pages, const core::PageTree* enginePages = nullptr);

    // The model changed IN PLACE -- a rotation -- and is now at `revision`. Lays out again and asks for
    // tiles at the new revision, keeping the current page and the scroll where they are: unlike
    // `setDocument`, which is a document ARRIVING and starts at page 1.
    void modelChanged(core::Revision revision);

    core::DocumentHandle document() const noexcept { return document_; }

    // The MODEL's page tree -- the one layout reads, whose rotations include the unsaved ones. Null
    // where no document is set. Exposed at S4 T12 for the page LABELS: the status bar and the
    // *Go to Page* box both need them, and the alternative was the shell asking the backend for a
    // tree the view is already holding and which may be the journal's rather than the engine's.
    const core::PageTree* pages() const noexcept { return pages_; }
    core::Revision revision() const noexcept { return revision_; }
    int pageCount() const;

    // `controller()`, NOT `viewport()`. `QAbstractScrollArea::viewport()` is the scrolled WIDGET
    // and this class calls it constantly; a same-named accessor here would hide the base's and the
    // diagnostics would be about `->` on a non-pointer rather than about the collision. The same
    // trap `PageLayout::pageSlots()` records for Qt's `slots` macro, in a second spelling.
    ViewportController& controller() noexcept override { return *controller_; }
    const ViewportController& controller() const noexcept override { return *controller_; }
    const PageLayoutEngine& layoutEngine() const noexcept { return engine_; }
    const TilePresenter& tiles() const noexcept { return *presenter_; }

    // The metrics this view is currently rendering for. Exposed because "which monitor did it
    // think it was on" is the first question of every blurry-text report, and it is otherwise
    // invisible.
    ScreenMetrics screenMetrics() const;

    void setLayoutMode(core::render::LayoutMode mode);
    void setPresentationOptions(core::render::PresentationOptions options);

    // Applies the eleven `pageDisplay/` preferences. Called once when the document is adopted and
    // again on every change to that group.
    //
    // **`layoutMode` and `defaultZoom` are applied ONLY on the first call**, which is what makes
    // them *defaults* rather than commands: a person who has switched this tab to Two-Up and then
    // opens Preferences to change the anti-aliasing must not have their layout yanked back. The
    // other nine are live.
    void applySettings(const ViewSettings& settings);
    const ViewSettings& settings() const noexcept { return settings_; }

    // The flags every tile of this view is rasterized with, derived from the settings above. Public
    // because it is the one place three preferences become one bitmask, and a case that recomputed
    // it would be a copy of the mapping rather than a check on it.
    core::render::RenderFlags renderFlags() const noexcept;

    // Scrolls so `pageIndex` is at the top of the viewport, and records a history entry --
    // deliberate navigation, unlike a scroll (see `ViewportController::recordView`).
    void goToPage(int pageIndex);

    void setFitMode(FitMode fit);

    // WHERE THIS VIEW'S CONTENT BOXES COME FROM -- S4 T12, D4-14. Installed by
    // `ui/shell/DocumentWindow` over `core::IDocumentBackend::contentBox()`; unset in a widget case
    // that does not care, which gets Fit Visible's CropBox fallback rather than a crash.
    void setContentBoxSource(PageLayoutEngine::ContentBoxSource source);

    // GO TO A DESTINATION, FIT AND ALL -- S4 T12, closing **I-05**. T6's panes could reach the
    // destination's PAGE and deliberately did not guess its view; this is the arithmetic they were
    // waiting for, and it is here rather than in either pane because a bookmark and a named
    // destination pointing at the same place must land in the same place.
    //
    // An unresolved destination moves nothing at all: a pane shows it as broken (`core/doc/
    // Destinations.h`) and a viewer that scrolled to page 0 instead would be the product deciding
    // the document meant something it did not say.
    void goToDestination(const core::Destination& destination);

    // REVEAL A RECTANGLE OF A PAGE -- S5 T7, the find bar's current hit. Nothing moves when it is already
    // wholly on screen (stepping through three hits on one visible page must not jump the page under the
    // reader); otherwise the view scrolls the least it can on each axis, with the rectangle a third of the
    // way down when it has to move vertically, so the hit is not left on the bottom edge. Records no
    // history entry: stepping through hits is not navigating.
    void revealPageRect(int pageIndex, const core::PdfRect& rect);

    // PUT A PAGE-SPACE RECTANGLE'S TOP, AS DRAWN, AT THE VIEWPORT'S TOP EDGE -- S5 T11, Reflow view's way
    // back (D5-16): the block the reader was at in the reflow becomes the block at the top of the page.
    // EXACTLY at the edge, not a margin below it as a destination lands, because "the block at the top" is
    // then read back from this same edge (`core/text/ReflowText`'s `blockAtTop`). "Its top as drawn" is the
    // corner highest on SCREEN, which is the page-space top only on an upright page (S5 guide I-06). The
    // horizontal scroll is kept; no history entry -- leaving reflow is not navigating.
    void putPageRectAtTop(int pageIndex, const core::PdfRect& rect);
    // HOW FAR BELOW THE VIEWPORT'S TOP EDGE a point of a page is drawn, in device pixels, negative above it;
    // empty for a page not laid out. Reflow view reads its place along the screen's down with it (I-06).
    std::optional<double> depthBelowTop(int pageIndex, core::PdfPoint point) const;

    // ---- The §8 Viewer operations (T14) -------------------------------------------------------
    // Each is the BEHAVIOUR behind one registered command, and each is a public method rather than
    // a lambda inside the registration for the reason D1-7 gives about everything else: a
    // behaviour that exists only inside a command handler cannot be driven by a test, by the
    // Stage 14 batch runner, or by the next surface that wants it.
    void zoomIn();
    void zoomOut();
    void setZoom(core::render::Zoom zoom);

    void firstPage();
    void lastPage();
    void nextPage();
    void prevPage();

    // A screen at a time. In a continuous mode this scrolls by a viewport less an overlap, which is
    // what every reader does so a line is never lost across the seam; in the paged modes it steps
    // the page, because there is nothing below the current one to scroll to.
    void nextScreen();
    void prevScreen();

    void goBackView();
    void goForwardView();

    // The SESSION's rotation, never the document's. `page.rotate` is Stage 3's and is destructive.
    void rotateViewClockwise();
    void rotateViewCounterClockwise();
    core::PageRotation viewRotation() const;

    // ---- The tool (S2 T14's minimum; S4 T7's real one) ----------------------------------------
    // Stage 2 T14 carried a three-value enum and a `dragging_` flag here, and said it was *"the
    // minimum that makes the three bindings honest rather than the maximum that could be built"*.
    // **T7 built the maximum and this is now a forwarder**: `ui/viewer/ToolController` owns the
    // sticky arming, the temporary-hand modifier, `Esc` cancellation, the cursor and auto-scroll,
    // over four `ui/viewer/tools/` classes — and a fourth tool, the Loupe, that the enum never had.
    //
    // `PageView::Tool` is GONE rather than aliased to `ToolKind`: two names for one enum is D1-6's
    // complaint in miniature, and the two callers were a `setTool` line each.
    ToolKind tool() const noexcept;
    void     setTool(ToolKind kind);

    ToolController&       tools() noexcept { return *tools_; }
    const ToolController& tools() const noexcept { return *tools_; }

    // ---- The grid, the guides and the page colour (S4 T8) --------------------------------------
    // The layer is a view of THIS document's guides and of the `units/*` preferences; the shell
    // hands both in. It is owned here rather than by `DocumentWindow` because everything it paints
    // is in the page surface's own two coordinate spaces, and because a split (T9) gives one
    // document two of these and each needs its own drag state.
    GridGuideLayer&       guides() noexcept { return *guides_; }
    const GridGuideLayer& guides() const noexcept { return *guides_; }
    void applyGuideSettings(const GuideSettings& settings);

    // ---- The text selection (S5 T5, D5-8) ------------------------------------------------------
    // The DOCUMENT WINDOW's selection, which every pane of a split paints and drives -- so it is handed
    // in, not owned: `ui/shell/DocumentWindow` makes one and gives it to each pane it adopts. Null
    // detaches, which is what a window closing does before the object goes. Painted over the tiles and
    // never rasterized (D4-11).
    void setSelectionOverlay(SelectionOverlay* selection);
    SelectionOverlay* selection() const override { return selection_; }

    // ---- Marks (S5 T13, D5-18) -----------------------------------------------------------------
    // Rectangles in PAGE SPACE painted over the tiles, under the selection -- a comparison's differences on
    // this side. Painted and never rasterized, for D4-11's reason: changing which difference is current
    // re-renders nothing. Each corner goes through the layout's own mapping, so a mark sits on its glyphs
    // under any view rotation. An `outlined` mark is the current one, drawn with an edge as well.
    struct PageMark {
        int           page = -1;
        core::PdfRect rect;
        QColor        colour;
        bool          outlined = false;
    };
    void setPageMarks(std::vector<PageMark> marks);
    const std::vector<PageMark>& pageMarks() const noexcept { return marks_; }
    // Where a page-space rectangle is drawn, in viewport (widget) pixels; empty for a page not laid out.
    // What the marks paint, and what a case compares with the ink.
    std::optional<QRectF> widgetRectOf(int pageIndex, const core::PdfRect& rect) const;

    // S5 T5's two page mappings for the Selection tool (`ToolHost`).
    PagePoint pagePointAt(QPoint pos) const override;
    PagePoint pagePointOn(int page, QPoint pos) const override;

    // How many presses in a row the last one was -- two, three and four select a word, a line and a
    // block. Counted here, where the presses arrive, from the platform's double-click interval and drag
    // distance; a fifth press starts again at one.
    int lastClickCount() const noexcept { return clicks_; }

    // **A PAINT, NOT A RENDER FLAG** (D4-11). Inversion is a per-pixel map over pixels the
    // scheduler already returned, so toggling it enqueues nothing and changes no cache key --
    // guide case A-52 asserts exactly that. `RenderFlags::Grayscale` is the one that stays a
    // render flag, because PDFium's grey is a conversion inside the rasterizer.
    void setInvertColors(bool invert);
    bool invertColors() const noexcept { return invertColors_; }

    // S4 T11 -- A2's named work, one line deep. `true` when this view's WINDOW is not the one in
    // front, which makes its viewport queue at `RenderPriority::Prefetch` rather than `Visible`.
    // The decision belongs to `ui/shell/MainWindow`, which is the only thing that knows which
    // window that is; `TilePresenter::setBackground` argues why the lane rather than the tile.
    void setBackgroundWindow(bool background);

    // S4 I-19 and I-20 -- a FROZEN pane of a spreadsheet split (`SplitViewContainer`), frozen onto
    // `body`; null makes it an ordinary pane again. It takes no focus, so a click in it leaves the
    // body active and every command on the body; it shows no scrollbars, because its scroll is
    // DERIVED from the body's and a scrollbar would offer a position the pane does not own; and
    // **its gestures are the BODY's before they move anything** -- the wheel is re-sent to the
    // body, and the tools drive the body's controller (`ToolTarget`).
    //
    // That last clause is I-20. I-19's first fix let the frozen pane move and then handed the
    // distance on -- and a pane frozen at the top of the document cannot move up, so a wheel up
    // over the header row did nothing; and the hand sets an ABSOLUTE scroll from its press, so a
    // pane snapped back after every move re-sent the whole drag each time and the body ran away.
    void setFrozen(PageView* body);
    bool isFrozen() const noexcept { return scrollTarget_ != nullptr; }

    // The room the BODY's scrollbars take, kept EMPTY on this pane's right and bottom so that its
    // viewport is the body's size to the pixel. A frozen row aligns with the body at the left edge
    // by construction; two viewports one scrollbar apart in width clamp one scrollbar apart at the
    // right edge, which is a misalignment the person sees only when scrolled all the way across.
    void setScrollBarReserve(int right, int bottom);

    // A point in the viewport's logical pixels, in CONTENT device space -- the space
    // `PageLayout` and `ViewportController::visibleRect` are in. Public because the shell maps a
    // ruler's drop into it, and because one place for *scroll offset plus device-pixel ratio* is
    // the whole point: those are the two things every caller gets half right.
    core::DevicePoint toContentPoint(QPoint viewportPosition) const;

    // Forces the layout, the scrollbars and the tile set to agree with the controller. Public
    // because a test drives it after `show()` and `activate()`, which is convention 12's rule
    // about a widget test having to drive layout itself.
    void refresh();

    // ---- The rest of `ToolHost` (S4 T7) -------------------------------------------------------
    // `controller()` above is the first member of this interface and has been on this class since
    // S2 T13 -- it is the override rather than a second accessor beside it. The others are here.
    //
    // Implemented on the widget rather than handed to each tool as a `PageView&`, so that the
    // seventeen tools `Architecture-FileMap.md` §3.14 schedules across Stages 4 to 10 name no
    // widget and can be driven by a case with no window. See `ui/viewer/tools/Tool.h`.
    QSize          viewportSize() const override;
    double         devicePixelRatio() const override { return devicePixelRatioF(); }
    void           requestRepaint() override;
    void           recordView() override;
    const QPixmap* lastFrame() const override;

signals:
    // For `ui/shell/StatusBar`: the page number and the zoom the user is looking at.
    void viewChanged(int currentPage, int pageCount, double zoomFactor);

    void toolChanged(ToolKind kind);

    // Where the pointer is over the page surface, in VIEWPORT logical pixels -- what the two
    // rulers draw their cursor marker at (S4 T8). `onSurface` false means it left, and the
    // markers are cleared rather than frozen at the last place it was seen.
    void cursorMoved(QPoint viewportPosition, bool onSurface);

    // S5 T10 (D5-11): a snapshot reached the clipboard -- its size in pixels, the resolution it was taken
    // at, and, when the 50-megapixel bound lowered it, that it did and from what. The words are the
    // shell's (`ui/viewer/` holds no catalog key); `snapshotFailed` is the raster coming back invalid.
    void snapshotCopied(int width, int height, int dpi, bool reduced, int askedDpi);
    void snapshotFailed();

    // A right-click that landed ON a guide. The menu itself is the shell's -- it is words, and
    // `ui/viewer/` holds no catalog key (the same split `RulerWidget::contextMenuRequested`
    // makes). A right-click anywhere else is left alone rather than swallowed.
    void guideContextMenuRequested(int guideIndex, QPoint global);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;

    // The pointer LEAVING the surface, which is what clears the rulers' cursor marker (S4 T8).
    // `QEvent::Leave` goes to the viewport, not to this widget, and `viewportEvent` is the hook
    // `QAbstractScrollArea` provides for exactly that -- an event filter would be a second
    // mechanism for what the base class already routes.
    bool viewportEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    // The page and its surround, with no tool overlay. Split out of `paintEvent` at S4 T7 so the
    // same content can go either straight onto the viewport or into the pixmap the loupe
    // magnifies -- one painting path, two destinations, rather than two paths that could drift.
    void paintContent(QPainter& painter, const QRect& area);

    // Moves with no button down are delivered only while something wants them -- the loupe, or a
    // ruler tracking the cursor. See the comment at the connection in the constructor.
    void updateMouseTracking();

    void updateScrollBars();
    void syncControllerFromScrollBars();
    // The page a step of one SPREAD from `from` lands on, in `direction` (+1 / -1). What
    // `nextPage()`, `prevPage()` and the edge-of-page scroll all step by -- see
    // `PageLayoutEngine::spreadStep()` for why a page step is the wrong one in `TwoUp`.
    int pageStep(int from, int direction) const;

    core::DeviceSize viewportSizeInDevicePixels() const;

    // S4 T12. Puts the rectangle Fit Visible measured under the viewport's top-left corner, and
    // puts a destination's PDF-space point there. Both go through `PageLayout::toContent`, which
    // is the one place that knows PDF's y grows upwards and that a CropBox's origin need not be
    // (0,0) -- a second conversion written here would be a second reader to keep in agreement, and
    // a wrong one scrolls the reader silently to the wrong place.
    // WHICH PAGE THE FIT IS ANCHORED TO, or -1 when no fit is in force. See the long note in
    // `PageView.cpp`'s `refresh()`: a fit re-satisfied against the drifting current page feeds back
    // into the layout that decides which page is current. T12 anchored Fit Visible alone, whose zoom
    // is per page by definition (D4-14); **I-11** anchored the rest, whose zoom is per page on any
    // document of mixed sizes. It is also what a reader expects, a zoom that stays put while they
    // scroll.
    int fitAnchorPage_ = -1;

    // WHERE THE READER IS, as the viewport centre's fraction of the current page -- I-11. Taken
    // before a command changes the zoom and put back after, because the scroll offset is device
    // pixels and does not scale with the zoom. `page` -1 is "nowhere", and restores nothing.
    struct Place {
        int page = -1;
        double fx = 0.5;
        double fy = 0.0;
    };
    Place capturePlace() const;
    // `wholePage` puts the page's top at the viewport's (less the margin) and centres it across --
    // what Fit Page and Fit Height are asked for -- instead of putting the fraction back.
    void restorePlace(const Place& place, bool wholePage);

    void scrollToFitRect(int pageIndex);
    void scrollToPagePoint(int pageIndex, double pdfCoordinate, bool horizontal);
    void scrollToPageXY(int pageIndex, std::optional<double> pdfX, std::optional<double> pdfY);

    // Scroll by one step, and IF THE SCROLL WAS ALREADY AT THE LIMIT, turn the page instead --
    // landing at the near edge of the new page so the gesture reads as continuing rather than as
    // jumping. Returns whether anything moved; false means the document really has ended and the
    // caller should leave the event alone.
    //
    // **ONLY A PAGED MODE CAN TURN A PAGE HERE.** In a continuous mode the layout already holds
    // every page, so the scroll limit IS the end of the document and there is nothing past it; in
    // `Single` and `Two-Up` the layout holds one page or one spread (`PagePresentation.cpp`), so
    // the limit is the page's edge and the next page is exactly what is past it. That difference
    // is the whole of I-24.
    bool scrollOrTurnPage(double dx, double dy);

    core::render::RenderScheduler& scheduler_;
    const platform::IDisplayTopology* topology_ = nullptr;

    ViewportController* controller_ = nullptr;
    PageLayoutEngine engine_;
    std::unique_ptr<TilePresenter> presenter_;

    core::DocumentHandle document_;
    core::Revision revision_;
    const core::PageTree* pages_ = nullptr;
    const core::PageTree* enginePages_ = nullptr;   // Stage 3 T10: the engine's reading, for the delta

    // Guards the scrollbar <-> controller round trip. Setting a scrollbar's value emits
    // `scrollContentsBy`, which would write the controller, which would set the scrollbar again --
    // the classic two-way binding loop, and here it would also re-request every tile.
    bool syncing_ = false;

    // Guards `refresh()` against ITSELF. Every `ViewportController` setter emits `changed`, which
    // is connected to `refresh()`, and `refresh()` calls four of those setters -- so without this
    // the first resize recurses until the stack runs out. A plain return is the right recovery
    // rather than a queued re-run: the outer call reads the controller AFTER each setter, so it
    // already sees whatever the inner one would have acted on.
    bool refreshing_ = false;

    ViewSettings settings_;
    bool settingsApplied_ = false;


    // S4 I-20: the body a frozen pane's gestures belong to, or null. A raw pointer to a sibling:
    // the body is the split's primary view, which outlives every mode change and so every frozen
    // pane (`SplitViewContainer::primaryView`).
    PageView* scrollTarget_ = nullptr;

    // S4 I-20: the host the tools are given. THIS VIEW for everything a tool draws or measures --
    // the drag arrives in this pane's coordinates, and its overlay is painted here -- and the
    // BODY's controller, when frozen, for what a tool scrolls or zooms. A separate object rather
    // than a second meaning of `controller()`, which the container and the shell read and write
    // as THIS pane's own. Declared before `tools_`, which holds a reference to it.
    class ToolTarget final : public ToolHost {
    public:
        explicit ToolTarget(PageView& view) : view_(view) {}
        ViewportController& controller() override;
        const ViewportController& controller() const override;
        QSize viewportSize() const override { return view_.viewportSize(); }
        double devicePixelRatio() const override { return view_.devicePixelRatio(); }
        void requestRepaint() override { view_.requestRepaint(); }
        void recordView() override;
        const QPixmap* lastFrame() const override { return view_.lastFrame(); }
        bool paintSharpLens(QPainter& painter, const QRect& lens,
                            const QRectF& source) const override
        {
            return view_.paintSharpLens(painter, lens, source);
        }
        // A FROZEN PANE SELECTS IN ITS OWN COORDINATES (S5 T5): the text under the pointer is this
        // pane's, and the selection is the window's whichever pane made it.
        SelectionOverlay* selection() const override { return view_.selection(); }
        PagePoint pagePointAt(QPoint pos) const override { return view_.pagePointAt(pos); }
        PagePoint pagePointOn(int page, QPoint pos) const override { return view_.pagePointOn(page, pos); }
        // A FROZEN PANE SNAPSHOTS WHAT IT SHOWS (S5 T10): the band is in this pane's coordinates.
        bool takeSnapshot(const QRect& band) override { return view_.takeSnapshot(band); }

    private:
        PageView& view_;
    };
    ToolTarget toolTarget_{*this};

    // S4 T7: the tool state machine, and the frame the LOUPE magnifies. The frame is captured
    // only while the armed tool asks for one (`Tool::needsFrame()`), so an unarmed loupe costs
    // nothing -- see `ui/viewer/tools/LoupeTool.h` for what the choice buys and what it does not.
    std::unique_ptr<ToolController> tools_;
    QPixmap                         frame_;

    // S4 D4-20: the Loupe's SHARP lens. The region under the lens, re-rasterized at the magnified
    // scale by the scheduler's third client -- asked for only once the region has been still for
    // `kLoupeSettleMs`, one request at a time. `pendingLoupe_` is the region waiting out that
    // interval; it and the timer are touched from the const paint path, which is where the lens's
    // region is first known, hence `mutable`. See `paintSharpLens`.
    std::unique_ptr<LoupeRaster>        loupeRaster_;
    QTimer*                             loupeSettle_ = nullptr;
    mutable core::render::RenderRequest pendingLoupe_;

    // S4 D4-22: `Alt`+wheel's delta not yet a whole notch -- a precise wheel reports fractions.
    double loupeWheel_ = 0.0;

    // S5 T10: the snapshot's client -- a second `LoupeRaster`, bypassing the tile cache -- and the
    // resolution the pending one was asked at, which the status bar is told when it arrives.
    std::unique_ptr<LoupeRaster> snapshotRaster_;
    SnapshotTool::Resolution     snapshotResolution_;
    void deliverSnapshot();

public:
    // The lens region's request, and the part of the viewport it covers in widget pixels -- or an
    // invalid request when the region is over no page. Public for the cases (A-99), which assert
    // WHAT was asked for against this, rather than re-deriving the arithmetic a second time.
    struct LoupeRegion {
        core::render::RenderRequest request;
        QRectF                      widget;
    };
    LoupeRegion loupeRegionFor(const QRectF& source) const;

    // The lens's scheduler client, read-only, for the same cases.
    const LoupeRaster& loupeRaster() const noexcept { return *loupeRaster_; }

    // D4-20's settle interval: how long the lens's region must be still before it is asked for.
    static constexpr int kLoupeSettleMs = 100;

    // ---- The Snapshot (S5 T10, D5-11) ----------------------------------------------------------------
    // What a band copies: ONE page -- the one the band overlaps most, the loupe's rule -- clipped to it,
    // at `SnapshotTool::resolve`'s scale, with the request `TilePresenter::requestAt` builds. `widget` is
    // the part of the viewport that page region covers; `points` its size in PDF points. Public for the
    // cases (A-90, A-114), which assert WHAT was asked for against this rather than re-deriving it.
    struct SnapshotRegion {
        core::render::RenderRequest request;
        QRectF                      widget;
        double                      widthPoints = 0.0;
        double                      heightPoints = 0.0;
        SnapshotTool::Resolution    resolution;
    };
    SnapshotRegion snapshotRegionFor(const QRect& band) const;

    // `ToolHost`'s: asks for `band`'s region and returns whether it did. The image reaches the clipboard
    // when the raster arrives, and `snapshotCopied` says so.
    bool takeSnapshot(const QRect& band) override;

    // The snapshot's scheduler client, read-only, for the cases.
    const LoupeRaster& snapshotRaster() const noexcept { return *snapshotRaster_; }

private:
    bool paintSharpLens(QPainter& painter, const QRect& lens, const QRectF& source) const override;

    // S4 T8: the grid and the guides painted over the tiles, and the page-colour inversion that
    // is painted the same way and for the same reason (D4-11).
    std::unique_ptr<GridGuideLayer> guides_;
    bool                            invertColors_ = false;

    QColor selectionColour() const;
    QPainter::CompositionMode selectionCompositionMode() const;
    // S5 T7: the find bar's hits -- every hit, and the current one over it.
    static QColor searchHitColour();
    static QColor currentSearchHitColour();

    // S5 T5: the window's selection (not owned), and the click counter.
    SelectionOverlay* selection_ = nullptr;

    // S5 T13: a comparison's marks on this side (`setPageMarks`).
    std::vector<PageMark> marks_;
    int               clicks_ = 0;
    qint64            lastPressMs_ = 0;
    QPoint            lastPressPos_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_PAGEVIEW_H
