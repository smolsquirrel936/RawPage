// pdf/DocumentBackendImpl -- the real `core::IDocumentBackend`. Stage 2 T9.
//
// It replaces the T1 backend that implemented none of the seven interfaces and said so. **After
// this task the implemented set is `{IDocumentBackend, IPageRasterizer}` and the other five are
// still null** (guide case A-46's second half, which the file that asserts it has been waiting for
// since T1): `ITextExtractor` is Stage 5, `MutationSink` Stage 3, `ISigner` Stage 11, `IOcrEngine`
// Stage 16, `IIndexer` Stage 12. Each stays a **null accessor rather than an empty stub**, which is
// CLAUDE.md convention 6 applied to the second interface header — a stub that silently succeeds
// lets a caller believe a page was extracted or a signature applied, and that surfaces stages later
// as data nobody produced.
//
// -------------------------------------------------------------------------------------------
// THE HANDLE IS AN OPAQUE TOKEN, AND THAT IS WHAT KEEPS THE BOUNDARY HONEST
// -------------------------------------------------------------------------------------------
// `core::DocumentHandle` is a `std::uint64_t` in a struct — never a pointer. This class holds the
// map from token to `PdfiumDocument`, so nothing above `pdf/` can dereference its way into the
// engine's object graph even by accident, and a stale handle from a closed document is a lookup
// that MISSES rather than a use-after-free. Handles are never reused: the counter only goes up, so
// a token held across a close cannot silently start naming a different document.
//
// -------------------------------------------------------------------------------------------
// WHY THIS FILE, AND `pdf::createBackend()` WITH IT, MOVED TO `rawpage_pdfium`
// -------------------------------------------------------------------------------------------
// Until T9 the factory lived in `rawpage_cos`, because the backend it returned implemented nothing
// and needed no engine. **A backend that opens documents IS the engine**, so the factory had to
// move — and `tests/unit` links `rawpage_cos` under `rawpage_assert_headless()` (D2-2), which
// forbids `rawpage_pdfium` by name.
//
// The consequence is stated rather than absorbed: the concrete backend's census is no longer
// asserted on `portable-asan`. What stays portable is `core/backend.h` itself — the interface
// declarations, `notImplementedError`, and `unimplementedBackendInterfaces` over a test backend
// defined in the suite — because those are the parts the portable presets can actually check. The
// part that needs PDFium is asserted where PDFium is, in `tests/render/`.

#ifndef RAWPAGE_PDF_DOCUMENTBACKENDIMPL_H
#define RAWPAGE_PDF_DOCUMENTBACKENDIMPL_H

#include "core/backend.h"
#include "core/doc/PageTree.h"
#include "core/doc/RepairReport.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

namespace rawpage::pdf {

namespace render {
class PdfiumDocument;
class PdfiumRasterizer;
}

class DocumentBackendImpl final : public core::IDocumentBackend {
public:
    DocumentBackendImpl();
    ~DocumentBackendImpl() override;

    std::array<bool, core::kBackendInterfaceCount> implementedInterfaces() const override;

    core::Result<core::DocumentHandle> open(const QString& path,
                                            const core::PasswordCallback& password) override;
    core::Status close(core::DocumentHandle document) override;

    // Overrides as of Stage 2 T13 -- `pages()` moved onto `core::IDocumentBackend` because `ui/`
    // is handed that interface and may never name `pdf/` (Stage 2 guide issue I-08).
    const core::PageTree* pages(core::DocumentHandle document) const override;

    core::IPageRasterizer* rasterizer(core::DocumentHandle document) override;
    core::ITextExtractor*  textExtractor(core::DocumentHandle document) override;
    core::MutationSink*    mutationSink(core::DocumentHandle document) override;
    core::ISigner*         signer(core::DocumentHandle document) override;
    core::IOcrEngine*      ocrEngine() override;
    core::IIndexer*        indexer() override;

    // STILL beyond the interface, and now deliberately rather than accidentally: T14's message bar
    // is the only consumer of a repair report, and a service handed to every caller ahead of the
    // first one that reads it is the shape Stage 1 kept finding (I-34, I-42, I-46). It joins
    // `core/backend.h` when that consumer exists, exactly as `pages()` did here.
    const core::RepairReport* repairReport(core::DocumentHandle document) const;

private:
    struct Open;

    Open* find(core::DocumentHandle document);
    const Open* find(core::DocumentHandle document) const;

    // GUARDS `open_` AND `nextHandle_`, and it is not defensive tidiness (Stage 2 T13).
    // `rasterizer()` is called from the RENDER THREAD -- `RenderScheduler`'s worker asks the
    // backend for the rasterizer named by `RenderRequest::document` -- while `open()` and `close()`
    // are called from the UI thread. A `std::map` being rehashed by one thread and walked by
    // another is undefined behaviour whose symptom is an intermittent crash inside the renderer,
    // which is the least debuggable place in the program for one.
    //
    // WHAT THE MUTEX DOES NOT DO is keep a returned `IPageRasterizer*` alive across a `close()`.
    // That is the caller's protocol and it is stated on `close()` below: cancel the document's
    // jobs and let the scheduler go idle first. A lock held for the duration of a rasterization
    // would serialize the UI thread behind a tile.
    mutable std::mutex mutex_;

    std::map<std::uint64_t, std::unique_ptr<Open>> open_;

    // Monotonic and never reused, so a handle held across a close misses rather than aliasing a
    // different document. Starts at 1 because 0 is `DocumentHandle`'s invalid value.
    std::uint64_t nextHandle_ = 1;
};

} // namespace rawpage::pdf

#endif // RAWPAGE_PDF_DOCUMENTBACKENDIMPL_H
