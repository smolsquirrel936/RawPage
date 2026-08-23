// FIXTURE -- a violation by construction, spelling 1 of 4: heap construction.
//
// This is what breaking D1-7 actually looks like. The button works, it looks right, and the
// command is unreachable from the CLI, from a chord, from the tool finder and from the Stage 14
// batch runner -- because nothing but this widget knows it exists.
#include <QAction>
#include <QWidget>

namespace rawpage::ui {

void buildPane(QWidget* parent)
{
    QAction* zoom = new QAction(parent);
    QObject::connect(zoom, &QAction::triggered, parent, [] { /* does the work right here */ });
}

} // namespace rawpage::ui
