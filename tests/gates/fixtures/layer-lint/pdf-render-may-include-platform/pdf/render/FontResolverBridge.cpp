// FIXTURE, NEGATIVE CONTROL -- the facade doing the job T16 gave it. MUST PASS.
//
// Without this, "pdf/cos/ may not include platform/platform.h" is equally satisfied by a rules file
// that forbids it for the whole of pdf/, which would make FontResolverBridge impossible to write and
// would put the substitution policy back on the engine's side of the boundary (guide case A-66).
#include "platform/platform.h"

namespace rawpage::pdf::render {
void installFontHook() {}
}
