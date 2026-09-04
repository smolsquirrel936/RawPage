// The `IPageRasterizer` a render worker holds. Stage 2 T13.
//
// ---------------------------------------------------------------------------------------------
// WHY AN ADAPTER EXISTS AT ALL.
//
// `RenderScheduler` builds ONE rasterizer per worker from a factory -- D2-9, and I-05's reason:
// nothing may hand the same engine object to two threads. `IDocumentBackend::rasterizer()` is per
// DOCUMENT, because that is what a rasterizer needs to be bound to in order to rasterize anything.
// A worker serves every open document and cannot be bound to one, so something has to turn a
// `RenderRequest`'s `DocumentHandle` back into an engine on each call. This is that something, and
// it is the only place in the program that does it.
//
// ---------------------------------------------------------------------------------------------
// WHY IT IS A HEADER IN `app/` RATHER THAN A CLASS INSIDE `CompositionRoot.cpp`.
//
// It was the second, and the consequence was that the ONE arrangement the shipped product actually
// runs -- the real PDFium rasterizer, reached through the real backend, driven by the real
// scheduler -- was the one arrangement nothing tested. `tests/ui` uses a fake rasterizer because
// `ui/` may not link the engine (D1-4); `tests/render` links the engine but had no reason to build
// a scheduler. Neither is wrong, and between them they left the join uncovered.
//
// It names only `core/` types, so a suite can include it without acquiring `app/`'s dependencies,
// and `tests/render/tst_renderpipeline.cpp` now drives THIS class rather than a second copy of it
// that could agree with a mistake in this one.
//
// ---------------------------------------------------------------------------------------------
// THREADING. With the shipped `workerCount` of **1** there is exactly one of these and exactly one
// thread reaching the backend. `pdf/DocumentBackendImpl` guards its own map because that thread is
// not the one that opens and closes documents, and `PdfiumInit`'s process-wide engine lock is what
// keeps the arrangement correct if T18's re-measurement walks the worker count up -- which is the
// guarantee A-45 passes on.
//
// What no lock can express is the lifetime: a `close()` while a worker is inside this document is a
// use-after-free in the engine. `IDocumentBackend::close()` states the caller's protocol and
// `ui/shell/DocumentWindow`'s destructor is the one caller that follows it.

#ifndef RAWPAGE_APP_BACKENDRASTERIZER_H
#define RAWPAGE_APP_BACKENDRASTERIZER_H

#include "core/Result.h"
#include "core/backend.h"
#include "core/render/RenderRequest.h"

#include <QLatin1String>
#include <QString>

namespace rawpage::app {

class BackendRasterizer final : public core::IPageRasterizer {
public:
    explicit BackendRasterizer(core::IDocumentBackend& backend) : backend_(backend) {}

    core::Result<core::RasterImage> raster(const core::render::RenderRequest& request) override
    {
        core::IPageRasterizer* rasterizer = backend_.rasterizer(request.document);
        if (rasterizer == nullptr) {
            // The document was closed while this tile was queued. `Unavailable` rather than
            // `NotFound`: the request was well-formed and is simply no longer answerable, and the
            // scheduler drops it either way -- what matters is that the log says which.
            return core::Error{core::ErrorCode::Unavailable,
                               QLatin1String("error.render.documentClosed"),
                               QString::number(static_cast<qulonglong>(request.document.id))};
        }
        return rasterizer->raster(request);
    }

private:
    core::IDocumentBackend& backend_;
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_BACKENDRASTERIZER_H
