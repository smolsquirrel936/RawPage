// FIXTURE -- a violation by construction, and the ONE that distinguishes the core-backend entry
// from the core entry it sits inside. core/ at large may include platform/platform.h; the engine
// contract may not. It is the contract for whatever reads and writes PDF bytes, and an OS interface
// reaching it means every implementor of the engine boundary -- including a test double -- has just
// acquired the platform layer.
#pragma once
#include "platform/platform.h"
namespace rawpage::core { class IDocumentBackend { public: virtual ~IDocumentBackend() = default; }; }
