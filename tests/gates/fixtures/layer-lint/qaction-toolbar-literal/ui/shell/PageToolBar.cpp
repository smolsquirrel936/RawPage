// FIXTURE -- spelling 4 of 4: QToolBar::addAction with an icon and a literal.
//
// The same hidden construction as the QMenu spelling, through a different class. Both conveniences
// are named in the rule because catching one and not the other is a rule that half works.
#include <QIcon>
#include <QToolBar>

namespace rawpage::ui {

void fill(QToolBar* bar, const QIcon& icon)
{
    bar->addAction(icon, "Delete Page");
}

} // namespace rawpage::ui
