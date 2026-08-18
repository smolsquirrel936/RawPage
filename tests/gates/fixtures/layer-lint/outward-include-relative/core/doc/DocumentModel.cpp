// LL-1, written the way an IDE inserts it. Same violation as outward-include/, spelled
// relatively -- which resolved to "core/doc/../../pdf/cos/CosDict.h" and was filed under
// core/ by a prefix match, so it passed clean until I-15.
#include "../../pdf/cos/CosDict.h"

namespace recto::core {
void loadPageCount() {}
}
