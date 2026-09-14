// FIXTURE, CONTROL -- pdf/ops/ including pdf/cos/. MUST PASS. Stage 3 T3, guide case A-05.
//
// The permitted direction. Without this control, both positive cases are equally satisfied by a rules
// file that forbade pdf/cos/ headers to everyone -- which would pass them and break the real tree.
#include "pdf/cos/XrefTable.h"

namespace rawpage::pdf::ops {
void walk() {}
}
