// The `ITextExtractor` a render worker holds. Stage 5 T2, decision D5-3.
//
// `app/BackendRasterizer.h`'s argument, for text: the scheduler builds ONE extractor per worker from a
// factory, `IDocumentBackend::textExtractor()` is per DOCUMENT, and a worker serves every open
// document -- so something turns a `TextRequest`'s handle back into that document's extractor on each
// call. This is it, and it is a header naming only `core/` types for the same reason
// `BackendRasterizer` is one: `tests/render/tst_extract.cpp` drives THIS class through the real
// scheduler, so the assembly the product runs is the assembly the suite runs.
//
// Its lifetime rule is the rasterizer's too: `close()` while a worker is inside this document's
// extraction is a use-after-free in the engine, and `RenderScheduler::cancelDocument()` -- which
// cancels text jobs as well as tiles since Stage 5 T2 -- followed by a drain is what the one caller
// that closes (`ui/shell/DocumentWindow`) already does.

#ifndef RAWPAGE_APP_BACKENDTEXTEXTRACTOR_H
#define RAWPAGE_APP_BACKENDTEXTEXTRACTOR_H

#include "core/Result.h"
#include "core/backend.h"

#include <QLatin1String>
#include <QString>

namespace rawpage::app {

class BackendTextExtractor final : public core::ITextExtractor {
public:
    explicit BackendTextExtractor(core::IDocumentBackend& backend) : backend_(backend) {}

    core::Status extractPage(core::DocumentHandle document, int pageIndex,
                             core::text::PageTextModel& into) override
    {
        core::ITextExtractor* extractor = backend_.textExtractor(document);
        if (extractor == nullptr) {
            // Closed while the job was queued -- `BackendRasterizer`'s `Unavailable`, for its
            // reason. A text job's failure is DELIVERED, so the caller hears this one.
            return core::Error{core::ErrorCode::Unavailable,
                               QLatin1String("error.render.documentClosed"),
                               QString::number(static_cast<qulonglong>(document.id))};
        }
        return extractor->extractPage(document, pageIndex, into);
    }

private:
    core::IDocumentBackend& backend_;
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_BACKENDTEXTEXTRACTOR_H
