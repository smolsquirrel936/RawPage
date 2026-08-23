// FIXTURE -- spelling 2 of 4: a stack-allocated QAction.
//
// A DIFFERENT SPELLING OF THE SAME MISTAKE, and the reason this fixture exists at all. Stage 0's
// third S1 was a gate that passed real violations because every fixture used one spelling; a rule
// tested against one spelling is a rule that catches one spelling.
#include <QAction>
#include <QWidget>

namespace rawpage::ui {

void configure(QWidget* parent)
{
    QAction placeholder(parent);
    placeholder.setEnabled(false);
}

} // namespace rawpage::ui
