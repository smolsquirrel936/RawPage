// FIXTURE, POSITIVE -- pdf/cos/ reaching for the OS boundary. MUST FAIL.
//
// The `pdf-render` layer entry added at Stage 2 T16 lets pdf/render/ include platform/platform.h,
// because that is where the engine boundary and the OS boundary meet (FontResolverBridge). THE
// NARROWING IS THE POINT: pdf/cos/ is portable BY RULE -- it is half of what portable-asan and
// portable-fuzz build on Linux -- and it must not inherit the widening through the longest-prefix
// match. Without this case, a rules file that had put `platform/platform.h` on the `pdf` layer
// instead of on `pdf-render` would look identical from the outside.
#include "platform/platform.h"

namespace rawpage::pdf::cos {
void parse() {}
}
