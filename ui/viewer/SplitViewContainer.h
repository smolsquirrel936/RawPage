// N page surfaces over ONE document -- Stage 4 T9, `Architecture-FileMap.md` §3.14, decision
// **D4-13** (`Stage4-ViewingSurface-Plan.md` §3).
//
// ===========================================================================================
// IT OWNS VIEWS AND NEVER A DOCUMENT, AND THAT IS THE WHOLE OF D4-13.
// ===========================================================================================
// A split is N `PageView`s inside ONE `ui/shell/DocumentWindow`, over one `core::DocumentHandle`,
// one `core::Document`, one `core::CommandJournal` and one revision. This class constructs the
// views and arranges them; `DocumentWindow` is what hands each one the document, the settings and
// the revision, because `DocumentWindow` is the only thing that holds them.
//
// The alternative -- a split that opened a second handle -- was refused for two reasons that are
// structural rather than stylistic. `DocumentWindow`'s destructor holds a three-step close
// protocol that keeps a worker out of a document being destroyed (cancel, drain, drop the tiles,
// close), and a second handle would need a second copy of it. It would also double the document's
// memory to show the same pages. And one journal is what makes *"the same document in two panes,
// one of them rotated"* correct rather than a race: `DocumentWindow::afterMutation` tells EVERY
// view the new revision, so both panes re-key their tiles together (guide case A-54).
//
// **Two views of one document in two WINDOWS is a different thing** -- `window.new`,
// `Features.md` §1.1 -- and is T11's, where the second window has its own handle by necessity.
//
// ===========================================================================================
// WHICH WAY IS "HORIZONTAL". THIS IS THE COIN FLIP EVERY READER GETS BACKWARDS.
// ===========================================================================================
// **`Horizontal` is a horizontal DIVIDER: two panes, one above the other.** `Vertical` is a
// vertical divider: two panes side by side. The mode is named for the split line, not for the axis
// the panes are stacked along, which is why the `QSplitter` orientations below read inverted --
// `Mode::Horizontal` builds a `QSplitter(Qt::Vertical)`, and that is correct.
//
// It matters because `Features.md` §1.2 lists both rows separately, and a product that swapped
// them would look like it worked.
//
// ===========================================================================================
// SYNCHRONIZED SCROLLING IS MIRRORED IN PDF POINTS, NOT IN DEVICE PIXELS.
// ===========================================================================================
// `ViewportController::scroll()` is in presented DEVICE pixels, so it scales with the zoom. Two
// panes at the same zoom have the same scale and mirroring the raw offset is exact -- but a person
// who zooms one pane of a split is doing the one gesture synchronized scrolling is FOR (an
// overview beside a detail), and mirroring raw pixels there walks the panes apart. So the mirror
// goes through `PageLayout::presentedScale()`, which is device pixels per PDF point: the
// follower's offset is the leader's times the ratio of the two scales. When the scales are equal
// that ratio is exactly 1.0 and the multiply is exact, so the common case loses nothing.
//
// **It synchronizes the SCROLL and not the zoom.** The name says scroll, and two panes locked to
// one zoom would make the gesture above impossible.
//
// ===========================================================================================
// THE SPREADSHEET SPLIT IS A CONSTRAINED SCROLL, NOT A SECOND DOCUMENT.
// ===========================================================================================
// Four panes, and three of them have no scroll of their own: their offsets are DERIVED from the
// body's (D4-13). With `origin` the place the freeze was taken at --
//
//     corner  (top-left)      (origin.x, origin.y)    frozen in both axes
//     row     (top-right)     (body.x,   origin.y)    follows the body in x only
//     column  (bottom-left)   (origin.x, body.y)      follows the body in y only
//     body    (bottom-right)  (body.x,   body.y)      the one the person scrolls
//
// -- which is guide case A-55's *"the frozen regions scroll only in their own axis"* written as
// four assignments. Synchronized scrolling is not an option in this mode: the derivation IS the
// mode, and a switch that could turn it off would be a spreadsheet split that is not one.
//
// **THE BODY LEADS EVERYTHING, NOT ONLY THE SCROLL -- I-18 and I-19, M-08's walk, 2026-09-27.**
// T9 derived the three panes' SCROLL and nothing else, and said in this header that a frozen pane
// "cannot be scrolled by the person" while nothing stopped it. The walk found both halves:
//
//   * **I-18.** `Ctrl+=` zoomed the body alone, because every view command acts on the focused
//     pane, and the frozen panes slid to the body's new edge at their OLD zoom. So the body's zoom,
//     fit, layout mode and view rotation are now copied onto the frozen panes on every change of
//     the body (`followBody`) -- with the fit resolved to the body's NUMBER, because three panes
//     each fitting their own size would be three zooms. And the freeze is held as a place in the
//     DOCUMENT (`FreezeAnchor`), not in device pixels, which change meaning with the zoom.
//   * **I-19.** A frozen pane takes no focus (`PageView::setFrozen`), so commands reach the body;
//     and **its gestures are the body's before they move anything** -- `PageView` re-sends its
//     wheel to the body and gives its tools the body's controller -- so a wheel over the frozen
//     header row scrolls the sheet, and `Ctrl`+wheel there zooms it.
//   * **I-20, the second fix of I-19.** The first let a frozen pane move and handed the distance
//     to the body. A pane frozen at the document's top cannot move up, so a wheel up over the
//     header row did nothing; and the hand sets an ABSOLUTE scroll from its press, so a pane put
//     back after every move re-sent the whole drag each time. Now nothing measures a frozen pane's
//     movement: whatever moves one is undone (`restoreFrozen`).
//
// ===========================================================================================
// THE ACTIVE VIEW IS THE FOCUSED ONE, AND EVERY VIEW COMMAND ACTS ON IT.
// ===========================================================================================
// `MainWindow::currentView()` is `DocumentWindow::view()` is `activeView()`, so the twenty-five §8
// Viewer commands Stage 2 T14 registered reach the pane a person is in with no change to any of
// them. Tracked by an event filter watching `QEvent::FocusIn` rather than by
// `QApplication::focusChanged`, so the mechanism is local to this container and a split in another
// window cannot see it.
//
// ===========================================================================================
// A RETIRED VIEW IS EMPTIED BEFORE IT IS DELETED, AND IT IS DELETED LATER.
// ===========================================================================================
// `deleteLater()`, never `delete`: `ui/shell/ShortcutBinder` dispatches from an application event
// filter, so `window.removeSplit` runs while a key event is still being delivered to the very view
// the command destroys. But `deleteLater` alone would leave that view's render tickets outstanding
// until the event loop got round to it, which is exactly what guide case A-56 asserts against --
// so the view is handed an EMPTY document first, which drops its tiles and cancels its tickets now
// (`TilePresenter::setDocument`), and the widget's destruction is then merely bookkeeping.
//
// A caller that asserts on the widget being GONE -- rather than on its tickets -- must flush
// `QEvent::DeferredDelete`, which is the trap `CLAUDE.md` already records for a closing tab.

#ifndef RAWPAGE_UI_VIEWER_SPLITVIEWCONTAINER_H
#define RAWPAGE_UI_VIEWER_SPLITVIEWCONTAINER_H

#include "core/Geometry.h"

#include <QWidget>

#include <vector>

class QSplitter;

namespace rawpage::core::render {
class RenderScheduler;
}

namespace rawpage::platform {
class IDisplayTopology;
}

namespace rawpage::ui {

class PageView;

class SplitViewContainer : public QWidget {
    Q_OBJECT

public:
    // `Features.md` §1.2's four *Split views* rows, as one enum. Read the header on which way
    // `Horizontal` divides before assuming.
    enum class Mode {
        None,           // one view -- what a document opens as, and what removing a split returns to
        Horizontal,     // a horizontal divider: two panes, one above the other
        Vertical,       // a vertical divider: two panes side by side
        Spreadsheet     // a frozen top row and left column, and a body that scrolls
    };

    // `topology` may be null, for the reason and with the consequence `PageView`'s constructor
    // states: the nominal `96 * scale` physical DPI rather than the panel's real one.
    SplitViewContainer(core::render::RenderScheduler& scheduler,
                       const platform::IDisplayTopology* topology, QWidget* parent = nullptr);
    ~SplitViewContainer() override;

    Mode mode() const noexcept { return mode_; }

    // Builds the split, or removes it. Setting the mode it is already in does nothing -- a person
    // who presses Split twice has not asked for four panes.
    void setMode(Mode mode);

    // The view that outlives every mode change: the one the document opened into, and the one that
    // remains when the split is removed. Never null.
    PageView* primaryView() const noexcept { return views_.front(); }

    // The view a command acts on -- the focused one, falling back to the primary. Never null.
    PageView* activeView() const noexcept { return active_; }

    // Every view, primary first. `DocumentWindow` walks this to hand each one the document, the
    // preferences and each new revision.
    const std::vector<PageView*>& views() const noexcept { return views_; }

    // Whether the panes of a `Horizontal` or `Vertical` split scroll together. **Ignored in
    // `Spreadsheet`**, where the derivation is the mode (see the header). Defaults to true, which
    // is what `Features.md` §1.2 means by *"two synchronized panes"*.
    bool synchronizedScrolling() const noexcept { return synchronized_; }
    void setSynchronizedScrolling(bool on);

    // One of a spreadsheet split's three frozen panes (I-19). Never focused, so never the active
    // view -- which is why `DocumentWindow` asks, to mirror a guide edited inside one.
    bool isFrozen(const PageView* view) const noexcept;

signals:
    // A view was constructed and is not yet showing anything. `DocumentWindow` answers by giving it
    // the document, the two settings groups and the position its source pane is at -- the whole of
    // adoption, in one place, so a pane added by a split and the pane a document opened into cannot
    // drift apart.
    void viewAdded(PageView* view);

    // A view is about to be emptied and deleted. Anything holding a pointer to it must drop it NOW:
    // after this returns the view has no document, and shortly afterwards no existence.
    void viewRetired(PageView* view);

    // The focused pane changed. `DocumentWindow` re-points the rulers at it and `MainWindow`
    // re-points the status bar, because both report on the view a person is in.
    void activeViewChanged(PageView* view);

    // A pane was scrolled BY SOMETHING OTHER THAN THIS CONTAINER'S OWN MIRROR -- S4 T11. Emitted
    // for a split of one pane as well as for a split of four, because its consumer is not the
    // split: `ui/shell/WindowArranger` mirrors a scroll onto the second view of the same document
    // in ANOTHER WINDOW (`window.new`, `Features.md` §1.1), and the commonest arrangement by far is
    // two unsplit windows.
    //
    // **It fires after the echo guard, never before.** A follower written by either mirror must not
    // lead a third view, or two windows each following the other is a loop with no fixed point.
    void scrolled(PageView* leader);

protected:
    // `QEvent::FocusIn` on any view, which is what makes a click in a pane activate it.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    PageView* makeView();
    void      retire(PageView* view);
    void      teardown();                       // back to one view, in this widget's own layout

    void buildTwoPane(Mode mode);
    void buildSpreadsheet();

    void setActive(PageView* view);

    // Mirrors `leader`'s scroll onto the panes that follow it. One function for both mechanisms,
    // because the guard against the mirror echoing back is the same guard and two copies of it is
    // how one of them gets forgotten.
    void propagateScroll(PageView* leader);

    // `Spreadsheet` only (I-18, I-19, I-20). `followBody` copies the body's zoom, fit, layout mode
    // and rotation onto the three frozen panes and derives their scroll; `restoreFrozen` undoes
    // whatever moved a frozen pane, which is never a person's gesture (see there).
    void followBody();
    void restoreFrozen();
    void updateScrollBarReserves();
    void takeFreeze();
    core::DevicePoint freezeOriginIn(const PageView& pane) const;

    void observe(PageView* view);               // focus, the controller's `changed`, `viewChanged`

    core::render::RenderScheduler&    scheduler_;
    const platform::IDisplayTopology* topology_ = nullptr;

    Mode                   mode_ = Mode::None;
    std::vector<PageView*> views_;
    PageView*              active_ = nullptr;

    // The splitters, so `teardown()` can take the primary back out of them. The two inner ones are
    // the spreadsheet's rows, whose handles are kept in step with each other so the frozen column
    // is the same width above and below.
    QSplitter* outer_ = nullptr;
    QSplitter* innerTop_ = nullptr;
    QSplitter* innerBottom_ = nullptr;

    // `Spreadsheet` only, in the header's order: corner, frozen row, frozen column. The body is
    // `primaryView()`.
    PageView* corner_ = nullptr;
    PageView* frozenRow_ = nullptr;
    PageView* frozenColumn_ = nullptr;

    // WHERE THE FREEZE WAS TAKEN, AS A PLACE IN THE DOCUMENT (I-18). A PDF point on the page
    // nearest the body's scroll at the moment of the split, plus the part of the offset that lies in
    // a margin or a gap -- which `PresentationOptions` keeps in device pixels at every zoom, so it
    // is carried as pixels. `fallback` is the raw offset, for a pane whose layout does not hold the
    // page (a paged mode on another page).
    struct FreezeAnchor {
        int               page = -1;
        core::PdfPoint    point;
        core::DevicePoint residual;
        core::DevicePoint fallback;
    };
    FreezeAnchor freeze_;

    bool synchronized_ = true;

    // Guards the mirror against echoing: writing a follower's scroll emits its controller's
    // `changed`, which would mirror back onto the leader. `PageView` records the same trap for the
    // scrollbar round trip, one layer down.
    bool mirroring_ = false;

    // The same shape of guard for the spreadsheet's two linked handles, and a SECOND flag rather
    // than a second user of the one above: they guard unrelated round trips, and one flag held
    // across both is a state where a scroll arriving mid-resize is silently dropped.
    bool linkingHandles_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_SPLITVIEWCONTAINER_H
