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
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/ViewportController.h"

#include <QAbstractScrollArea>

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

    // Scrolls so `pageIndex` is at the top of the viewport, and records a history entry --
    // deliberate navigation, unlike a scroll (see `ViewportController::recordView`).
    void goToPage(int pageIndex);

    void setFitMode(FitMode fit);

    // Forces the layout, the scrollbars and the tile set to agree with the controller. Public
    // because a test drives it after `show()` and `activate()`, which is convention 12's rule
    // about a widget test having to drive layout itself.
    void refresh();

signals:
    // For `ui/shell/StatusBar` at T14: the page number and the zoom the user is looking at.
    void viewChanged(int currentPage, int pageCount, double zoomFactor);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent* event) override;
    bool event(QEvent* event) override;

private:
    void updateScrollBars();
    void syncControllerFromScrollBars();
    core::DeviceSize viewportSizeInDevicePixels() const;

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
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_PAGEVIEW_H
