// FIXTURE, NEGATIVE CONTROL -- the two single-file exceptions ui/ is granted, exercised together.
// Without this, "ui/ may include core/ and nothing else" would be an equally valid reading of the
// positive cases, and the shell could not be built at all.
#include "app/Services.h"
#include "platform/platform.h"
namespace rawpage::ui { void build() {} }
