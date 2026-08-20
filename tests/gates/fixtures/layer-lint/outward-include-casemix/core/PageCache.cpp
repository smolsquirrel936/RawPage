// LL-1 with the folder names capitalised. Windows filesystems are case-insensitive, so this
// compiles and the include resolves -- but the resolved string came back spelled as written,
// and the layer lookup's prefix test was case-sensitive, so "Pdf/" never matched "pdf/" and
// the violation was filed as an external header (I-15).
#include "Pdf/Cos/CosDict.h"

namespace rawpage::core {
void warm() {}
}
