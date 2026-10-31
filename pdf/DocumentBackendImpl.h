// pdf/DocumentBackendImpl -- the real `core::IDocumentBackend`. Stage 2 T9.
//
// It replaces the T1 backend that implemented none of the seven interfaces and said so. **After
// this task the implemented set is `{IDocumentBackend, IPageRasterizer}` and the other five are
// still null** (guide case A-46's second half, which the file that asserts it has been waiting for
// since T1): `ITextExtractor` is Stage 5, `MutationSink` Stage 3, `ISigner` Stage 11, `IOcrEngine`
// Stage 16, `IIndexer` Stage 12. **Stage 5 T2 added `ITextExtractor`** (`pdf/render/PdfiumTextExtractor`),
// so the census reads 4 of 7. Each unimplemented one stays a **null accessor rather than an empty stub**, which is
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
#include "core/doc/PageTransitions.h"
#include "core/doc/PageTree.h"
#include "core/doc/RepairReport.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

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

    // S5 T9: `open()` IS the staged open below, run to its end in one call -- one open, not two.
    core::Result<core::DocumentHandle> open(const QString& path,
                                            const core::PasswordCallback& password) override;
    // S5 T9 (D5-13 amended): the pieces, in order -- READ the file (no engine); the engine's pieces
    // (`PdfiumDocument::Opening`: load, form environment, count, the page walk a few pages a piece, the
    // readers) and its first-party repair reading (no engine); and ASSEMBLE (no engine): the identity,
    // the first-party walk with its sink, transitions and attachment descriptions, the permissions, and
    // the handle.
    core::Result<core::OpenProgress> beginOpen(const QString& path,
                                               const core::PasswordCallback& password) override;
    core::Result<core::OpenProgress> continueOpen(core::OpeningId opening) override;
    void abandonOpen(core::OpeningId opening) override;
    core::Status close(core::DocumentHandle document) override;

    // Overrides as of Stage 2 T13 -- `pages()` moved onto `core::IDocumentBackend` because `ui/`
    // is handed that interface and may never name `pdf/` (Stage 2 guide issue I-08).
    const core::PageTree* pages(core::DocumentHandle document) const override;
    const core::RepairReport* repairReport(core::DocumentHandle document) const override;
    // Stage 3 T4 (D3-7, D3-16): assessed and taken once, in `open()`.
    const core::Permissions* permissions(core::DocumentHandle document) const override;
    const core::DocumentId* documentId(core::DocumentHandle document) const override;

    // S4 T4 (D4-3, D4-4). Each forwards to what `PdfiumDocument` read once at open.
    const core::Outline* outline(core::DocumentHandle document) const override;
    const core::Destinations* destinations(core::DocumentHandle document) const override;
    const core::Metadata* metadata(core::DocumentHandle document) const override;

    // S4 T10 (D4-12). The first reader that is not PDFium's -- S5 T8's attachment descriptions are the
    // second (`searchUnits` below): the engine has no `/Trans` call, so
    // this is hydrated from the first-party walk `open()` already runs -- `pdf/ops/TransitionReader`.
    const core::PageTransitions* pageTransitions(core::DocumentHandle document) const override;

    // S6 T2 (D6-3). The third first-party reading off the same walk: `pdf/ops/AnnotationReader`, held
    // to agree with the engine page by page through `core::Permissions::assessAnnotations`.
    const core::AnnotationStore* annotations(core::DocumentHandle document) const override;

    // S6 T11 (D6-17). The fourth first-party reading off the walk -- `pdf/ops/EmbeddedFileReader`, which since S5 T8
    // read the descriptions the engine cannot -- and the bytes through the sink, or a table loaded for the call when
    // the document has none. The extract is written by `pdf/ops/AttachmentFiles`: no waiver.
    const core::Attachments* attachments(core::DocumentHandle document) const override;
    core::Result<QByteArray> attachmentData(core::DocumentHandle document, const core::AttachmentRef& attachment) override;
    // Stage 6 T14: a Sound's samples, from the sink -- the document as it stands now.
    core::Result<QByteArray> soundSamples(core::DocumentHandle document, const core::AnnotationId& annotation) override;
    core::Status extractAttachment(core::DocumentHandle document, const core::AttachmentRef& attachment,
                                   const QString& path, bool overwrite) override;
    // `open()`'s pieces with the read skipped; the document names no file and is never editable.
    core::Result<core::DocumentHandle> openBytes(const QByteArray& bytes, const QString& name,
                                                 const core::PasswordCallback& password) override;

    // S6 T13 (D6-20). Both files through `pdf/ops/FdfCodec` -- an export's FileAttachments given their bytes by
    // `attachmentData` first -- and the writability the sink asks, `MutationSinkImpl::canCreate`.
    core::Result<core::Fdf> readAnnotationExchange(const QString& path) override;
    core::Status writeAnnotationExchange(core::DocumentHandle document, const core::Fdf& exchange,
                                         core::ExchangeFormat format, const QString& path, bool overwrite) override;
    core::Status annotationWritable(const core::Annotation& annotation) const override;
    // Stage 7 T5 (D7-4, D7-6) -- see core/backend.h.
    core::Result<core::CopiedPages> copyPages(const QString& path, const std::vector<int>& pages) override;
    core::Result<core::CopiedPages> blankPages(core::PdfSize size, int count) override;

    // S4 T12 (D4-14). Forwards to `PdfiumDocument::contentBox`, which computes on demand and caches
    // per page -- the one reader on this interface that is not hydrated at open, and the header of
    // `core/backend.h` says why.
    core::Result<core::PdfRect> contentBox(core::DocumentHandle document, int pageIndex) override;

    // S5 T8 (D5-12). Forwards to `PdfiumDocument::searchUnits`, with the attachment descriptions the
    // first-party walk read at open (`pdf/ops/EmbeddedFileReader` -- PDFium's attachment API has no /Desc).
    // CALLED FROM THE RENDER THREAD, as an engine task; `close()`'s protocol covers it like a text job.
    core::Status searchUnits(core::DocumentHandle document, int part,
                             std::vector<core::text::SearchUnit>& into) override;

    // S6 T12 (D6-14). Forwards to `PdfiumDocument::pageGeometry`, `contentBox()`'s arrangement. CALLED FROM THE
    // RENDER THREAD, as an engine task, like `searchUnits`.
    core::Status pageGeometry(core::DocumentHandle document, int pageIndex, core::PageGeometry& into) override;

    // Stage 3 T8 (D3-4): through `pdf/ops/RevisionWriter`, the same code the headless round trip runs.
    core::Status writeRevision(core::DocumentHandle document, const QString& temporaryPath) override;
    core::Status adoptRevision(core::DocumentHandle document, const QString& path) override;
    // Stage 6 T4 (D6-2): `RevisionWriter::compose` and `verify` -- Save's own -- then each engine's
    // `PdfiumDocument::show`.
    core::Status showEdits(core::DocumentHandle document, const std::vector<core::DocumentHandle>& viewers) override;
    // Stage 7 T4: the verify `showEdits` started, waited for and answered -- see the definition.
    core::Status settleEdits(core::DocumentHandle document) override;
    void setEditsSettled(core::DocumentHandle document, std::function<void()> settled) override;

    // D6-2's record, for the cases and the bench that grade it (guide A-37, A-38): the reloads the
    // engine of `document` has made. Not on the interface -- a diagnostic of this engine, as
    // `PdfiumDocument` is.
    struct ReloadRecord {
        long long reloads = 0;
        long long failures = 0;
        long long coalesced = 0;
        std::chrono::nanoseconds lastLoad{0};
        std::thread::id lastThread;
        std::uint64_t pendingGeneration = 0;
        std::uint64_t shownGeneration = 0;
        std::chrono::nanoseconds lastCompose{0};   // the last `showEdits`: its compose, on its caller (S7 T4)
        std::chrono::nanoseconds lastVerify{0};    // ...and its verify, on a thread of its own (S7 T4)
        std::size_t lastObjectsWritten = 0;        // S7 T4: the objects the last update holds...
        std::size_t lastObjectsReused = 0;         // ...of which written again as the compose before wrote them
        const void* engineBase = nullptr;          // the data the engine reads the original from...
        const void* newestOnDisk = nullptr;        // ...and the backend's own buffer: equal when nothing was copied
    };
    std::optional<ReloadRecord> reloadRecord(core::DocumentHandle document) const;

    core::IPageRasterizer* rasterizer(core::DocumentHandle document) override;
    core::ITextExtractor*  textExtractor(core::DocumentHandle document) override;
    core::MutationSink*    mutationSink(core::DocumentHandle document) override;
    core::ISigner*         signer(core::DocumentHandle document) override;
    core::IOcrEngine*      ocrEngine() override;
    core::IIndexer*        indexer() override;

private:
    struct Open;
    struct Opening;

    Open* find(core::DocumentHandle document);
    // S7 T4: the verify of `entry`'s last `showEdits`, finished -- before anything reads the sink again.
    static void awaitVerify(Open& entry);
    const Open* find(core::DocumentHandle document) const;
    // The last piece: everything `open()` did after the engine opened the bytes, and the registration.
    core::Result<core::DocumentHandle> assemble(Opening& opening);

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
    // S5 T9: the opens in progress, by `OpeningId` -- under `mutex_` like `open_`, and a piece runs with
    // the lock released, as `contentBox` does.
    std::map<core::OpeningId, std::unique_ptr<Opening>> openings_;
    core::OpeningId nextOpening_ = 1;

    // Monotonic and never reused, so a handle held across a close misses rather than aliasing a
    // different document. Starts at 1 because 0 is `DocumentHandle`'s invalid value.
    std::uint64_t nextHandle_ = 1;
};

} // namespace rawpage::pdf

#endif // RAWPAGE_PDF_DOCUMENTBACKENDIMPL_H
