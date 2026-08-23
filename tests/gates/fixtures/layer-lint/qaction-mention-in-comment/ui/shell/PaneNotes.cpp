// FIXTURE -- THE SECOND CONTROL: prose, not code.
//
// This file talks about QAction at length and constructs none. It exists because the gates scan
// the part of a line the COMPILER sees, and a rule that also matched comments would make its own
// documentation unwritable -- which is Stage 0's I-20 in a different costume: a gate that counts
// its own explanation as a finding.
//
// Notes for the reader: we do not call new QAction(this) anywhere in ui/, and menu->addAction(...)
// with a literal is likewise forbidden. Ask ActionFactory::create() instead. A stack QAction a(x)
// is the same mistake with a different lifetime.
#include <QWidget>

namespace rawpage::ui {

void describe(QWidget* pane)
{
    pane->setEnabled(true);
}

} // namespace rawpage::ui
