// Legitimate, and present so the fixture's failure is attributable to WHICH FILE included the OS
// boundary rather than to the include being denied everywhere under core/. core/settings/ has its
// own entry permitting exactly this, and the real Settings store uses it.
#include "platform/platform.h"
namespace rawpage::core { void store() {} }
