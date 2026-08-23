#include "platform/platform.h"

#include <windows.h>
#include <shlobj.h>

namespace rawpage::platform::win {
class Thing final : public IThing {
public:
    std::uintptr_t handle() const override { return reinterpret_cast<std::uintptr_t>(GetDesktopWindow()); }
};
}
