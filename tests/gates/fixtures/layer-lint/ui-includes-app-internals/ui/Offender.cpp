// FIXTURE -- a violation by construction. app/Services.h is ui/'s ONE permitted look outward, and
// the whole value of that exception is that it is one file. A widget that reaches for the
// composition root itself can construct services, and a widget that can construct services is a
// widget the batch runner cannot reuse.
#include "app/CompositionRoot.h"
namespace rawpage::ui { void offend() {} }
