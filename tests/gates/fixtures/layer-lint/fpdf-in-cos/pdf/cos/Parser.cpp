// FIXTURE -- a violation by construction, and the one the OLD rule permitted. `pdf/` was the
// allowed path until Stage 2 T2, so this call sat inside it and passed clean. pdf/cos/ is the
// first-party half of D3 and never learns the engine exists: it is what portable-asan and
// portable-fuzz build on Linux, where PDFium is not present at all.
#include "pdf/cos/Parser.h"

namespace rawpage::pdf::cos {
void warmUp() { FPDF_InitLibrary(); }
}
