#include "ui/shell/DocumentTabBar.h"

#include "ui/shell/Catalog.h"

#include <QCursor>
#include <QMouseEvent>

namespace rawpage::ui {

DocumentTabBar::DocumentTabBar(QWidget* parent) : QTabBar(parent)
{
    setObjectName(QStringLiteral("documentTabs"));
    setMovable(true);          // reorder by dragging
    setTabsClosable(true);
    setUsesScrollButtons(true);
    setElideMode(Qt::ElideRight);
    setExpanding(false);
    setDocumentMode(true);
    setFocusPolicy(Qt::TabFocus);   // keyboard operability, retained
    setToolTip(catalog::translate(catalog::key::TabStripHint));

    // QTabBar's own close button already emits this; middle-click below funnels into the same
    // signal so that a tab closed two ways is a tab closed one way.
    connect(this, &QTabBar::tabCloseRequested, this,
            [this](int index) { emit closeRequested(index); });
}

void DocumentTabBar::mousePressEvent(QMouseEvent* event)
{
    // Which tab the gesture started on, remembered here because by the time the pointer has left
    // the strip `tabAt()` no longer knows -- it answers about where the pointer IS.
    pressedTab_ = tabAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && tearingTab_ >= 0) {
        // A fresh press while a tear is still recorded means the previous gesture never got its
        // release -- a lost grab, which is rare but leaves the strip unable to reorder until it is
        // cleared. The button cannot already be down here, so this can only be recovery.
        tearingTab_ = -1;
        unsetCursor();
    }
    QTabBar::mousePressEvent(event);
}

void DocumentTabBar::mouseMoveEvent(QMouseEvent* event)
{
    if (tearingTab_ >= 0) {
        // Already carrying a tab: Qt's move-drag was ended at the threshold and must not be
        // restarted, so these moves are swallowed rather than forwarded. The pointer is tracked by
        // the release, not by us.
        event->accept();
        return;
    }

    if (pressedTab_ < 0 || !(event->buttons() & Qt::LeftButton)) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    // VERTICAL distance only. Dragging sideways past the last tab is how a person reorders to the
    // end, and treating that as a tear-off would make the commonest gesture in the strip open a
    // window nobody asked for.
    const int below = event->position().toPoint().y() - rect().bottom();
    const int above = rect().top() - event->position().toPoint().y();
    if (qMax(below, above) < kTearOffDistance) {
        QTabBar::mouseMoveEvent(event);
        return;
    }

    // End Qt's internal move-drag before anyone removes the tab under it. QTabBar tracks the
    // dragged tab by index, and taking that index away mid-drag leaves it dragging something that
    // is not there; a synthetic release puts it back in a clean state first.
    tearingTab_ = pressedTab_;
    pressedTab_ = -1;
    QMouseEvent release(QEvent::MouseButtonRelease, event->position(), event->globalPosition(),
                        Qt::LeftButton, Qt::NoButton, event->modifiers());
    QTabBar::mouseReleaseEvent(&release);

    // AND NOW WAIT FOR THE BUTTON. The gesture is not over -- the real button is still down, so
    // this widget keeps the mouse grab and keeps receiving moves, and where the pointer ENDS is
    // the only position that answers "which window did you mean". See the signal's comment (I-34).
    setCursor(Qt::DragMoveCursor);
}

void DocumentTabBar::mouseReleaseEvent(QMouseEvent* event)
{
    const int pressed = pressedTab_;
    pressedTab_ = -1;

    if (event->button() == Qt::MiddleButton && pressed >= 0
        && tabAt(event->position().toPoint()) == pressed) {
        // Press and release on the SAME tab, as every other middle-click close in every other
        // product works: pressing on one tab and releasing on another is a slip, not a close.
        emit closeRequested(pressed);
        event->accept();
        return;
    }

    if (tearingTab_ >= 0) {
        const int tab = tearingTab_;
        tearingTab_ = -1;
        unsetCursor();
        // Cleared BEFORE the emit: a receiver may delete this strip's tab, adopt it into another
        // window, or -- when the move is refused -- do nothing at all, and none of those may find
        // a half-finished gesture still recorded here.
        emit tearOffRequested(tab, event->globalPosition().toPoint());
        event->accept();
        return;
    }

    QTabBar::mouseReleaseEvent(event);
}

} // namespace rawpage::ui
