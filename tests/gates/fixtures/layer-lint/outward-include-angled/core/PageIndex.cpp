// LL-1 with angle brackets. Every layer target puts the source root on its include path,
// so this compiles exactly like the quoted form. layer-lint used to treat any angled
// include as external "by convention" and never looked (I-15).
#include <pdf/cos/CosDict.h>

namespace recto::core {
void index() {}
}
