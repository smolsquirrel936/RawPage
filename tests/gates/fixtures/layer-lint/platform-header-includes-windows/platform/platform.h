// The violation: an OS header in the interface file every other layer includes.
#ifndef FIXTURE_PLATFORM_H
#define FIXTURE_PLATFORM_H

#include <windows.h>

namespace rawpage::platform {
class IThing { public: virtual ~IThing() = default; virtual HWND handle() const = 0; };
}
#endif
