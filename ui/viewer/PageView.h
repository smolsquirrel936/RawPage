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
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/ViewportController.h"

#include <QAbstractScrollArea>
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
};

class PageView : public QAbstractScrollArea {
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
    void setDocument(core::DocumentHandle document, core::Revision revision,
                     const core::PageTree* pages);

    core::DocumentHandle document() const noexcept { return document_; }
    int pageCount() const;

    // `controller()`, NOT `viewport()`. `QAbstractScrollArea::viewport()` is the scrolled WIDGET
    // and this class calls it constantly; a same-named accessor here would hide the base's and the
    // diagnostics would be about `->` on a non-pointer rather than about the collision. The same
    // trap `PageLayout::pageSlots()` records for Qt's `slots` macro, in a second spelling.
    ViewportController& controller() noexcept { return *controller_; }
    const ViewportController& controller() const noexcept { return *controller_; }
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

    // ---- The tool (T14, and only as much of it as Stage 2 owns) -------------------------------
    // `Features.md` §8 binds `H`, `V` and `Z` in the Viewer context and `Implementation-Plan.md`
    // lists all three under Stage 2. What Stage 2 does NOT own is `ui/viewer/ToolController` and
    // the `ui/viewer/tools/` family — those are ○ (S4), and they are the sticky-tool state machine,
    // the temporary-hand modifier, `Esc` cancellation and seventeen tool classes.
    //
    // So this is the minimum that makes the three bindings honest rather than the maximum that
    // could be built: which tool is armed, what cursor it shows, and what a drag does. Hand pans,
    // Marquee Zoom zooms to the dragged rectangle, and Select is the default and selects nothing
    // until Stage 5 gives it text to select — which is stated here rather than discovered.
    enum class Tool { Select, Hand, MarqueeZoom };
    Tool tool() const noexcept { return tool_; }
    void setTool(Tool tool);

    // Forces the layout, the scrollbars and the tile set to agree with the controller. Public
    // because a test drives it after `show()` and `activate()`, which is convention 12's rule
    // about a widget test having to drive layout itself.
    void refresh();

signals:
    // For `ui/shell/StatusBar`: the page number and the zoom the user is looking at.
    void viewChanged(int currentPage, int pageCount, double zoomFactor);

    void toolChanged(Tool tool);

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

private:
    void updateScrollBars();
    void syncControllerFromScrollBars();
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

    Tool tool_ = Tool::Select;
    bool dragging_ = false;
    QPoint dragStart_;          // widget coordinates
    QPoint dragCurrent_;
    core::DevicePoint dragScrollOrigin_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_PAGEVIEW_H
