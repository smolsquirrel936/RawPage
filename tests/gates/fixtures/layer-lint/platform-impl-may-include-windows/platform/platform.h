// NEGATIVE CONTROL. The interface header stays clean -- a Win32 handle crosses as an opaque
// integer -- and platform/win/ includes the OS freely. Without this control, the rule above is
// equally satisfied by one that rejects windows.h anywhere in platform/, which would make the layer
// unable to do the one job it exists for.
#ifndef FIXTURE_PLATFORM_H
#define FIXTURE_PLATFORM_H

#include <cstdint>

namespace rawpage::platform {
class IThing { public: virtual ~IThing() = default; virtual std::uintptr_t handle() const = 0; };
}
#endif
