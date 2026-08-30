// The document tab strip -- reorder, middle-click close, tear-off, and overflow.
//
// ---------------------------------------------------------------------------------------------
// IT REPORTS GESTURES; IT MOVES NOTHING.
//
// Every operation here ends in a signal, and `MainWindow` is what actually closes a tab or moves a
// document to another window. That is not ceremony: a tear-off has to decide between "make a new
// window" and "drop into that other window's strip", and neither answer is available to a widget
// that only knows about its own tabs. It also keeps this class constructible and testable on its
// own, which is the same reason `SidebarDock` does not own its edge strips.
//
// ---------------------------------------------------------------------------------------------
// WHY THIS ONE USES QTabBar's OWN OVERFLOW WHEN THE RIBBON DOES NOT.
//
// The ribbon replaced the scroll arrows with a chevron menu (criterion R-3, named custom work #2)
// because a ribbon has a small fixed set of tabs and a chevron listing the four that did not fit is
// what a ribbon does. A document strip has an unbounded, user-driven number of tabs whose names the
// user chose, and every product with one -- browsers, editors, the reference at P-4 -- shrinks and
// then scrolls. Copying the ribbon's chevron here would be consistency with the wrong thing.

#ifndef RAWPAGE_UI_SHELL_DOCUMENTTABBAR_H
#define RAWPAGE_UI_SHELL_DOCUMENTTABBAR_H

#include <QPoint>
#include <QTabBar>

namespace rawpage::ui {

class DocumentTabBar : public QTabBar {
    Q_OBJECT

public:
    explicit DocumentTabBar(QWidget* parent = nullptr);

    // How far the pointer must leave the strip before a drag stops being a reorder and becomes a
    // tear-off. Public so the manual case (M-03) and any later test describe the same number rather
    // than each guessing at "a bit below the tabs".
    static constexpr int kTearOffDistance = 40;

signals:
    // Middle-click, and the close button on a tab. One signal for both: they are the same request,
    // and giving them separate signals would let the two paths drift into doing different things.
    void closeRequested(int index);

    // A tab dragged clear of the strip AND released. `globalPos` is where the pointer was when the
    // button came up, which is what lets the receiver decide between a new window and another
    // window's strip -- see the class comment.
    //
    // EMITTED ON RELEASE, NOT WHEN THE THRESHOLD IS CROSSED (issue I-34). It used to fire from
    // `mouseMoveEvent` the instant the drag went `kTearOffDistance` clear, carrying the position at
    // that one instant -- which made "drop onto another window's strip" unreachable in practice: the
    // target strip is ~25 px tall and had to be under the pointer on the single move event that
    // crossed the band, and an ordinary drag steps straight over it. The branch was correct and
    // could not be reached by hand. Deciding on release is also what a person means by a drag.
    void tearOffRequested(int index, const QPoint& globalPos);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    int  pressedTab_ = -1;

    // The tab a tear-off gesture is carrying, or -1. Set when the drag first goes clear of the
    // strip and read when the button comes up; `pressedTab_` cannot serve both, because Qt's own
    // move-drag has to be ended at the threshold and that is what clears it.
    int  tearingTab_ = -1;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_DOCUMENTTABBAR_H
