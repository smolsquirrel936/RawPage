#pragma once
#include "core/settings/Settings.h"
#include "platform/platform.h"
namespace rawpage::app { struct Services { rawpage::core::Settings& settings; rawpage::platform::Platform& platform; }; }
