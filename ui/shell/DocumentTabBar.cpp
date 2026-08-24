#include "ui/shell/DocumentTabBar.h"

#include "ui/shell/Catalog.h"

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
    tearing_ = false;
    QTabBar::mousePressEvent(event);
}

void DocumentTabBar::mouseMoveEvent(QMouseEvent* event)
{
    if (tearing_ || pressedTab_ < 0 || !(event->buttons() & Qt::LeftButton)) {
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
    tearing_ = true;
    QMouseEvent release(QEvent::MouseButtonRelease, event->position(), event->globalPosition(),
                        Qt::LeftButton, Qt::NoButton, event->modifiers());
    QTabBar::mouseReleaseEvent(&release);

    emit tearOffRequested(pressedTab_, event->globalPosition().toPoint());
    pressedTab_ = -1;
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

    if (tearing_) {
        tearing_ = false;
        event->accept();
        return;
    }

    QTabBar::mouseReleaseEvent(event);
}

} // namespace rawpage::ui
