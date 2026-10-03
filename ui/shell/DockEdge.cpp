#include "ui/shell/DockEdge.h"

#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QPainter>

namespace rawpage::ui {

DockEdge::DockEdge(QDockWidget* dock) : QWidget(dock), dock_(dock)
{
    setObjectName(QStringLiteral("dockEdge"));
    // A press on the strip is a press on what is beneath it: it marks the edge, it is not a control.
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    // Everything that moves the edge reaches the dock: a resize, a show, a child added above the strip (a
    // dock's `setWidget` after this was built), and a layout pass. The two signals are the side changing.
    dock_->installEventFilter(this);
    connect(dock_, &QDockWidget::dockLocationChanged, this, [this] { place(); });
    connect(dock_, &QDockWidget::topLevelChanged, this, [this] { place(); });
    place();
}

bool DockEdge::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == dock_) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::ChildAdded:
        case QEvent::LayoutRequest:
            place();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DockEdge::place()
{
    // THE SIDE FROM THE MAIN WINDOW'S LAYOUT -- the one fact that says which edge faces the page. A floating
    // dock, or one not (yet) in a main window, has no such edge.
    side_ = Qt::NoDockWidgetArea;
    if (!dock_->isFloating())
        if (auto* window = qobject_cast<QMainWindow*>(dock_->parentWidget())) side_ = window->dockWidgetArea(dock_);

    const QRect area = dock_->rect();
    switch (side_) {
    case Qt::RightDockWidgetArea:
        setGeometry(area.left(), area.top(), 1, area.height());
        break;
    case Qt::LeftDockWidgetArea:
        setGeometry(area.right(), area.top(), 1, area.height());
        break;
    case Qt::BottomDockWidgetArea:
        setGeometry(area.left(), area.top(), area.width(), 1);
        break;
    case Qt::TopDockWidgetArea:
        setGeometry(area.left(), area.bottom(), area.width(), 1);
        break;
    default:
        hide();
        return;
    }
    // ABOVE THE DOCK'S CONTENTS, every time -- a child added later stacks over an earlier one.
    raise();
    show();
}

void DockEdge::paintEvent(QPaintEvent* /*event*/)
{
    // `QPalette::Mid` is the theme's `Border` token (the header).
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Mid));
}

} // namespace rawpage::ui
