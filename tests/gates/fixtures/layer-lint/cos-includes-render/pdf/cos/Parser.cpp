// FIXTURE, POSITIVE -- pdf/cos/ including the PDFium facade's own header. MUST FAIL. Stage 3 T3, A-05.
//
// No FPDF_ token appears here, so the `fpdf` token rules are silent -- which is why this needs the
// `pdf-cos` include rule rather than being assumed covered. pdf/cos/ is portable by rule, and
// pdf/render/ does not exist on the portable presets.
#include "pdf/render/PdfiumDocument.h"

namespace rawpage::pdf::cos {
void parse() {}
}
