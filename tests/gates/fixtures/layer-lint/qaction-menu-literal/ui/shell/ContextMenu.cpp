// FIXTURE -- spelling 3 of 4: QMenu::addAction, which BUILDS a QAction for you.
//
// The construction is invisible in the source, which is exactly why it is in the pattern list. A
// reader scanning for `new QAction` sees nothing wrong here.
#include <QMenu>
#include <QWidget>

namespace rawpage::ui {

void fill(QMenu* menu)
{
    menu->addAction("Rotate Clockwise");
}

} // namespace rawpage::ui
