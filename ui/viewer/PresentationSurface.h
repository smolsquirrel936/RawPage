// The window a presentation is drawn in -- Stage 4 T10, D4-12. Split out of
// `ui/viewer/FullScreenController` because it is a WIDGET and the controller is a state machine:
// the controller decides what level the product is at, which monitor it is on and when a page
// turns, and this draws the result. A case can drive the whole of the controller's logic with this
// window never shown.
//
// ===========================================================================================
// A `PageView` PLUS AN OVERLAY, AND NOT A THIRD RASTERIZER.
// ===========================================================================================
// The page is drawn by an ordinary `ui/viewer/PageView` over the SAME `DocumentHandle` the tab
// holds -- the *N views, one document* shape D4-13 established for the split, for the same
// reasons and with no second handle. What this class adds is everything that is painted OVER the
// page and is not part of it: the blank, the laser pointer, and the transition.
//
// The overlay is a child widget rather than a paint inside `PageView`, so that none of it can
// reach the scheduler or a tile: a blank screen is not a render, and a document being blanked must
// not evict a single cached page (D4-11's argument, arriving at a third subject after the grid and
// the inversion).
//
// ===========================================================================================
// THE TRANSITION ANIMATES A CAPTURED FRAME, NOT A SECOND RENDER.
// ===========================================================================================
// On a page turn the outgoing page is captured into a pixmap ONCE and the animation is a paint of
// that pixmap over the new one at decreasing opacity, or clipped to a moving edge. The alternative
// -- rendering both pages every frame for the duration -- would put two full-page rasterizations
// behind one engine lock into the sixteen milliseconds a frame has, which is the same argument
// `ui/viewer/tools/LoupeTool` makes for magnifying the last frame rather than re-rasterizing.
//
// **What that costs, stated rather than absorbed:** the outgoing page is frozen at the resolution
// it was drawn at, so a transition that runs while the page is still arriving animates a partly
// blank frame. It is one pixmap of one viewport and it is released when the animation ends.

#ifndef RAWPAGE_UI_VIEWER_PRESENTATIONSURFACE_H
#define RAWPAGE_UI_VIEWER_PRESENTATIONSURFACE_H

#include "core/Transition.h"

#include <QPixmap>
#include <QPoint>
#include <QWidget>

namespace rawpage::core::render {
class RenderScheduler;
}

namespace rawpage::platform {
class IDisplayTopology;
}

namespace rawpage::ui {

class PageView;
class PresentationOverlay;

class PresentationSurface : public QWidget {
    Q_OBJECT

public:
    PresentationSurface(core::render::RenderScheduler& scheduler,
                        const platform::IDisplayTopology* topology, QWidget* parent = nullptr);
    ~PresentationSurface() override;

    PageView* view() const noexcept { return view_; }

    // Black, white, or neither. Painted over everything including the laser, because a blanked
    // screen that still showed a laser dot would be a screen that is not blank.
    void setBlank(int blank);

    // The dot follows the pointer while it is on. It is painted at the LAST KNOWN position when
    // the pointer has not moved, rather than hidden: a presenter who stops moving the mouse has
    // not stopped pointing at the thing they were pointing at.
    void setLaserPointer(bool on);
    bool laserPointer() const noexcept;

    void setCursorVisible(bool visible);

    // Begins an animation from the frame currently on screen to whatever is drawn next. `seconds`
    // is the transition's own duration. `None` captures nothing and repaints once, which is what
    // makes the common case free.
    void beginTransition(core::TransitionStyle style, double seconds);

    // Whether an animation is running. A case asserts this rather than sampling pixels, because
    // what a frame looks like half way through a dissolve is not a claim a headless run can make.
    bool transitionRunning() const noexcept;

signals:
    // The window was closed by the OS -- `Alt+F4`, or the session ending. `FullScreenController`
    // treats it as an exit path, which is the one in guide case A-58 that no chord reaches.
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    PageView*            view_ = nullptr;
    PresentationOverlay* overlay_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_PRESENTATIONSURFACE_H
