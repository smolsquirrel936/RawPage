// FIXTURE -- THE POSITIVE CONTROL. This is the one file the rule allows, and it must pass.
//
// Without this case, "the rule fires on every spelling" is equally satisfied by a rule that
// rejects everything, everywhere -- which would make the mechanism it is supposed to protect
// impossible to implement. The control is what distinguishes a working gate from a broken one.
#include <QAction>
#include <QMenu>
#include <QObject>

namespace rawpage::ui {

QAction* create(QObject* parent)
{
    QAction* action = new QAction(parent);
    return action;
}

QAction* nonCommandItem(QMenu* menu, const QString& text)
{
    return menu->addAction(text);
}

} // namespace rawpage::ui
