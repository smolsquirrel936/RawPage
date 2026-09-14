// FIXTURE, POSITIVE -- pdf/cos/ reaching UP into pdf/ops/. MUST FAIL. Stage 3 T3, guide case A-05.
//
// D3-3's `pdf-cos` layer entry: pdf/cos/ is the bottom of pdf/, and the mutation layer is built on it.
// Before the entry existed this include passed, because the `pdf` entry lets anything under pdf/
// include anything else under pdf/.
#include "pdf/ops/PageOps.h"

namespace rawpage::pdf::cos {
void parse() {}
}
