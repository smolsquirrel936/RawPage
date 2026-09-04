// Zoom, scroll, page snapping and the view history. Stage 2 T13.
//
// ---------------------------------------------------------------------------------------------
// WHY THIS IS A SEPARATE OBJECT FROM THE WIDGET, AND WHY IT HOLDS NO WIDGET.
//
// Everything here is arithmetic over four numbers -- a zoom, a scroll offset, a content size and a
// viewport size -- and every one of those arithmetics has a failure mode that is invisible in a
// screenshot and obvious in an assertion: a zoom about the cursor that drifts by a pixel per step,
// a scroll that clamps to the wrong edge at the bottom of the document, a fit-width that forgets
// the margins. `Stage1-AppShell-Testing-Guide.md`'s I-19 is the standing lesson about widget
// geometry: assert what the layout gave, then assert what depends on it. This class is the *what
// depends on it* half, extracted so it can be driven with no window at all.
//
// It therefore names no `QWidget` and reads no widget geometry. `PageView` measures and tells it.
//
// ---------------------------------------------------------------------------------------------
// CONTENT SPACE, AND WHY IT IS DEVICE PIXELS RATHER THAN LOGICAL ONES.
//
// Every coordinate here is in **presented device pixels** -- `Resolution::presentedScale()` times
// PDF points -- with the origin at the top-left of the whole laid-out document, which is exactly
// the space `core::render::PageLayout` produces. Qt lays widgets out in LOGICAL pixels, so
// `PageView` divides by the device-pixel ratio at the boundary and nowhere else.
//
// Mixing the two is the defect this note exists to prevent: on a 100 % monitor they are the same
// number, so an implementation that confuses them is correct on the developer's machine and puts
// the page at two thirds of its size on a 150 % one.
//
// ---------------------------------------------------------------------------------------------
// ZOOM ABOUT A POINT IS A RATIO, NOT A POSITION.
//
// `zoomAbout()` keeps one content point under one viewport point. The whole computation is
// `newScroll = anchorContent * (newScale / oldScale) - anchorInViewport`, and it needs no page, no
// layout and no rounding -- which is why it is here and not in the widget, and why guide case A-54's
// *"zoom about the cursor keeps the point under the cursor fixed"* is an equality rather than a
// tolerance.

#ifndef RAWPAGE_UI_VIEWER_VIEWPORTCONTROLLER_H
#define RAWPAGE_UI_VIEWER_VIEWPORTCONTROLLER_H

#include "core/Geometry.h"
#include "core/render/DpiPolicy.h"
#include "core/render/PagePresentation.h"

#include <QObject>

#include <vector>

namespace rawpage::ui {

// `FitMode` MOVED TO `core/render/DpiPolicy.h` AT S2 T15, because `pageDisplay/defaultZoom` is a
// settings key and `SettingsKeys.def` may only name `core/` types. It is used unqualified below
// through this using-declaration rather than aliased with a `using FitMode = ...`: an alias in a
// second namespace is a second name for one type, and the next reader has to find out which is
// which. `core::render::FitMode` is the type.
using core::render::FitMode;

class ViewportController : public QObject {
    Q_OBJECT

public:
    explicit ViewportController(QObject* parent = nullptr);

    // ---- What the widget measures and tells us ------------------------------------------------
    void setViewportSize(core::DeviceSize size);
    core::DeviceSize viewportSize() const noexcept { return viewport_; }

    // The scrollable extent, from `PageLayout::contentSize()`. Setting it RE-CLAMPS the scroll
    // offset, because a document that just got shorter must not leave the view past its end.
    void setContentSize(core::DeviceSize size);
    core::DeviceSize contentSize() const noexcept { return content_; }

    // ---- Zoom ---------------------------------------------------------------------------------
    core::render::Zoom zoom() const noexcept { return zoom_; }

    // Clamped to [8 %, 6400 %] by `Zoom::clamped()`, never refused -- a zoom arrives from a wheel
    // gesture and from a restored settings file as well as from a menu.
    void setZoom(core::render::Zoom zoom);

    // The preset ladder of Features.md 1.2. Stepping from a value between two rungs lands on the
    // next rung in that direction rather than on the nearest, so repeated presses always move.
    void zoomIn();
    void zoomOut();
    static core::render::Zoom nextZoomStep(core::render::Zoom from, int direction) noexcept;

    // Keeps `anchorInViewport` -- a point in VIEWPORT coordinates, device pixels from the
    // viewport's top-left -- over the same content point it was over before. See the header.
    void zoomAbout(core::render::Zoom target, core::DevicePoint anchorInViewport);

    FitMode fitMode() const noexcept { return fit_; }

    // Records the intent. The zoom that satisfies it is computed by `PageLayoutEngine`, which is
    // the object that knows how big the page is; this class only remembers that a resize must
    // recompute it. A fit that stored a zoom and forgot the intent would stop fitting the moment
    // the window changed size, which is the whole point of a fit.
    void setFitMode(FitMode mode);

    // ---- Scroll --------------------------------------------------------------------------------
    core::DevicePoint scroll() const noexcept { return scroll_; }
    void setScroll(core::DevicePoint offset);
    void scrollBy(double dx, double dy);

    // The viewport rectangle in content space -- what `TileGrid` and `PageLayout` are asked about.
    core::DeviceRect visibleRect() const noexcept;

    // ---- Pages ----------------------------------------------------------------------------------
    core::render::LayoutMode layoutMode() const noexcept { return mode_; }
    void setLayoutMode(core::render::LayoutMode mode);

    // The SESSION's rotation of the whole view (`Ctrl+Shift+=` / `Ctrl+Shift+-`), never the
    // document's `/Rotate`. Rotating pages destructively is `page.rotate` and is Stage 3's, and
    // keeping the two apart here is what stops a view command from being mistaken for an edit.
    core::PageRotation viewRotation() const noexcept { return viewRotation_; }
    void setViewRotation(core::PageRotation rotation);
    void rotateView(int quarterTurns);

    int currentPage() const noexcept { return currentPage_; }
    int pageCount() const noexcept { return pageCount_; }
    void setPageCount(int count);

    // Sets the page WITHOUT scrolling. `PageView` follows it with a scroll to the page's slot;
    // separating the two is what lets a scroll-driven page change avoid scrolling itself, which
    // would otherwise be a feedback loop that fights the user's drag.
    void setCurrentPage(int page);

    // ---- View history: Alt+Left / Alt+Right (Features.md 8, Viewer) -------------------------------
    struct ViewState {
        int page = 0;
        core::render::Zoom zoom;
        core::DevicePoint scroll;

        friend bool operator==(const ViewState& a, const ViewState& b) noexcept
        {
            return a.page == b.page && a.zoom == b.zoom && a.scroll == b.scroll;
        }
    };

    // Pushed by a DELIBERATE navigation -- a go-to-page, a bookmark, a link -- and never by
    // scrolling. A history that recorded every wheel notch is a Back button that goes back one
    // notch, which is the same as no Back button at all.
    void recordView();
    bool canGoBack() const noexcept { return historyIndex_ > 0; }
    bool canGoForward() const noexcept;
    void goBack();
    void goForward();
    const std::vector<ViewState>& history() const noexcept { return history_; }

    ViewState currentView() const;

signals:
    // One signal for every state change, deliberately: `PageView` recomputes its layout, its
    // scrollbars and its tile set together, and three signals would let two of the three run
    // against a half-updated state.
    void changed();

private:
    void clampScroll();
    void emitChanged();

    core::DeviceSize viewport_;
    core::DeviceSize content_;
    core::DevicePoint scroll_;
    core::render::Zoom zoom_ = core::render::Zoom::actualSize();
    core::render::LayoutMode mode_ = core::render::LayoutMode::SingleContinuous;
    core::PageRotation viewRotation_ = core::PageRotation::None;
    FitMode fit_ = FitMode::None;
    int currentPage_ = 0;
    int pageCount_ = 0;

    std::vector<ViewState> history_;
    int historyIndex_ = -1;

    // True while `goBack`/`goForward` is applying a state, so the apply does not record itself.
    bool navigating_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_VIEWPORTCONTROLLER_H
