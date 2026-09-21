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
#include "core/doc/Revision.h"
#include "core/render/PagePresentation.h"
#include "core/render/RenderRequest.h"
#include "ui/viewer/GridGuideLayer.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/ViewportController.h"

#include <QAbstractScrollArea>
#include <QPixmap>
#include <QPoint>

#include <memory>

namespace rawpage::core {
class PageTree;
}

namespace rawpage::core::render {
class RenderScheduler;
}

namespace rawpage::platform {
class IDisplayTopology;
}

namespace rawpage::ui {

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
    void mousePressEvent(QMouseEvent* event) override;
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


    // S4 T7: the tool state machine, and the frame the LOUPE magnifies. The frame is captured
    // only while the armed tool asks for one (`Tool::needsFrame()`), so an unarmed loupe costs
    // nothing -- see `ui/viewer/tools/LoupeTool.h` for what the choice buys and what it does not.
    std::unique_ptr<ToolController> tools_;
    QPixmap                         frame_;

    // S4 T8: the grid and the guides painted over the tiles, and the page-colour inversion that
    // is painted the same way and for the same reason (D4-11).
    std::unique_ptr<GridGuideLayer> guides_;
    bool                            invertColors_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_PAGEVIEW_H
