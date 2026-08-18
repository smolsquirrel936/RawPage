// NEGATIVE CONTROL for I-15's widened matching. Naming the forbidden things in prose must
// stay legal, or every design comment in the tree becomes a violation and the gate gets
// switched off:
//   #include "pdf/cos/CosDict.h"
//   FPDF_InitLibrary();
//   QIODevice::WriteOnly
namespace recto::core {
void documented() {}
}
