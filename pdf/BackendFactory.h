// pdf::createBackend() -- the one symbol this layer exports.
//
// `app/CompositionRoot` is the only caller, exactly as `platform::createPlatform()` has exactly one
// caller: nothing else in the program may name a concrete backend, because the moment two places
// choose an engine, "the engine is replaceable" stops being true. Everything downstream of the
// composition root sees `core::IDocumentBackend&` and never learns that PDFium exists.
//
// WHAT IT RETURNS TODAY (Stage 2 T1): a backend that implements NONE of the seven interfaces and
// says so -- `implementedInterfaces()` is seven falses, every method returns the not-implemented
// `Result` core/backend.h defines, and construction logs the census the way the Windows platform
// bundle logs its own. T9 replaces it with `pdf/DocumentBackendImpl` over `pdf/render/`.
//
// That is not a placeholder in the Stage 0 sense and it is not scaffolding to delete: it is the
// honest implementation of "declared, not built yet" (D2-7, and CLAUDE.md convention 6). The
// alternative -- returning null, or nothing at all until T9 -- would leave `app/` with a branch for
// "no backend" that no later build ever exercises, which is how a code path rots.

#ifndef RAWPAGE_PDF_BACKENDFACTORY_H
#define RAWPAGE_PDF_BACKENDFACTORY_H

#include "core/backend.h"

#include <memory>

namespace rawpage::pdf {

std::unique_ptr<core::IDocumentBackend> createBackend();

} // namespace rawpage::pdf

#endif // RAWPAGE_PDF_BACKENDFACTORY_H
