#include "pdf/DocumentBackendImpl.h"

#include "core/Log.h"
#include "core/doc/DocumentId.h"
#include "core/doc/Permissions.h"
#include "pdf/ops/MutationSinkImpl.h"
#include "pdf/ops/RevisionWriter.h"
#include "pdf/render/PdfiumDocument.h"
#include "pdf/render/PdfiumRasterizer.h"

#include <QDateTime>
#include <QFileInfo>

#include <optional>
#include <utility>

namespace rawpage::pdf {

// One open document and the rasterizer that serves it. The rasterizer holds the document by
// reference, so declaration order here is the destruction contract: `rasterizer` is destroyed
// first because members are destroyed in reverse order of declaration.
struct DocumentBackendImpl::Open {
    std::unique_ptr<render::PdfiumDocument> document;
    std::unique_ptr<render::PdfiumRasterizer> rasterizer;

    // Stage 3 T4. The bytes the engine loaded, held by this entry so the first-party sink's views into
    // them stay valid: a `QByteArray` copy shares the engine's buffer rather than duplicating it, and
    // nothing here ever writes to it, so its data pointer does not move.
    QByteArray bytes;
    core::DocumentId id;
    core::Permissions permissions;
    // Null unless `permissions` allows editing: a sink for a document this build refuses to edit
    // would be a way to edit it (D3-7).
    std::unique_ptr<ops::MutationSinkImpl> sink;

    // Stage 3 T8. Set once a save has been adopted: the repair report of the file NOW on disk, which
    // after a correct write is empty. The engine's report describes the bytes it loaded, and those
    // stop being the newest revision at the first save -- a repaired document must not be forced to
    // Save-As a second time because the engine still remembers the first file.
    std::optional<core::RepairReport> savedReport;
};

DocumentBackendImpl::DocumentBackendImpl() = default;
DocumentBackendImpl::~DocumentBackendImpl() = default;

std::array<bool, core::kBackendInterfaceCount> DocumentBackendImpl::implementedInterfaces() const
{
    std::array<bool, core::kBackendInterfaceCount> implemented{};
    implemented.fill(false);
    implemented[static_cast<std::size_t>(core::BackendInterface::DocumentBackend)] = true;
    implemented[static_cast<std::size_t>(core::BackendInterface::PageRasterizer)] = true;
    // Stage 3 T4: `pdf/ops/MutationSinkImpl`, handed out per document by `mutationSink()` -- and only
    // for a document `Permissions` lets this build edit.
    implemented[static_cast<std::size_t>(core::BackendInterface::Mutation)] = true;
    // The other five stay false and their accessors stay null. Convention 6: a stub that silently
    // succeeds surfaces stages later as data nobody produced.
    return implemented;
}

// Both `find` overloads require `mutex_` to be held; every public entry point below takes it.
DocumentBackendImpl::Open* DocumentBackendImpl::find(core::DocumentHandle document)
{
    const auto found = open_.find(document.id);
    return found == open_.end() ? nullptr : found->second.get();
}

const DocumentBackendImpl::Open* DocumentBackendImpl::find(core::DocumentHandle document) const
{
    const auto found = open_.find(document.id);
    return found == open_.end() ? nullptr : found->second.get();
}

core::Result<core::DocumentHandle> DocumentBackendImpl::open(const QString& path,
                                                             const core::PasswordCallback& password)
{
    // WHETHER THE BYTES ARE A PDF IS ANSWERED HERE, and this is the whole point of guide case
    // A-47. Stage 1 decided a file by its suffix and said so at the time (its case A-58, which
    // asserted the opposite and was correct then under D1-2). A `.dat` that is a PDF opens; a
    // `.pdf` that is not is refused with a named reason.
    auto document = render::PdfiumDocument::open(path, password);
    if (!document)
        return document.error();

    auto entry = std::make_unique<Open>();
    entry->document = std::move(document).value();
    entry->rasterizer = std::make_unique<render::PdfiumRasterizer>(*entry->document);

    // --- Stage 3 T4: identity, the second reading, and the pre-flight (D3-7, D3-16) -----------------
    // The identity is taken from the bytes the ENGINE loaded, not from a second read of the file, so
    // the hash is of the document the user is looking at. The modification time is a separate stat,
    // and a file rewritten between the read and the stat would carry the new time with the old hash --
    // which makes the pair disagree with the disk, the conservative direction for D3-16's check.
    entry->bytes = entry->document->bytes();
    entry->id = core::DocumentId::of(
        path, QFileInfo(path).lastModified().toMSecsSinceEpoch(), entry->bytes);

    // The FIRST-PARTY reading of the same bytes, through the sink that would carry any edit. Its page
    // walk and the engine's page tree must agree before editing is allowed: a rotation lands on the
    // object the walk calls page n and is drawn on the engine's page n.
    const std::string_view view(entry->bytes.constData(),
                                static_cast<std::size_t>(entry->bytes.size()));
    auto sink = ops::MutationSinkImpl::open(view);
    core::CosReading reading = core::CosReading::Read;
    std::vector<core::CosPageFacts> facts;
    if (!sink) {
        reading = sink.error().messageKey == QLatin1String("error.pdf.encrypted")
                      ? core::CosReading::Encrypted
                      : core::CosReading::NoPageMap;
    } else {
        for (const ops::PageEntry& page : sink.value()->pages().pages) {
            core::CosPageFacts fact;
            fact.rotation = page.effectiveRotate();
            if (page.mediaBox) {
                const auto& box = *page.mediaBox;
                fact.mediaSize = core::PdfSize{box[2] - box[0], box[3] - box[1]};
            }
            facts.push_back(fact);
        }
    }
    entry->permissions = core::Permissions::assess(entry->document->pages(), reading, facts);
    if (sink && entry->permissions.canEditPages())
        entry->sink = std::move(sink).value();

    const std::lock_guard<std::mutex> guard(mutex_);
    const core::DocumentHandle handle{nextHandle_++};
    RAWPAGE_LOG_INFO(core::LogSubsystem::Render)
        << "backend: opened" << path << "as handle" << static_cast<int>(handle.id) << "with"
        << entry->document->pageCount() << "pages;"
        << (entry->permissions.canEditPages()
                ? QStringLiteral("editable")
                : QStringLiteral("not editable (%1 %2)")
                      .arg(entry->permissions.refusalKey(), entry->permissions.refusalContext()));
    open_.emplace(handle.id, std::move(entry));
    return handle;
}

// THE CALLER'S PROTOCOL, because a mutex cannot express it: before closing, cancel the document's
// outstanding render jobs and let the scheduler go idle. `rasterizer()` hands out a pointer into
// the entry this call destroys, and a worker holding one across the destruction is a use-after-free
// inside PDFium. `ui/shell/DocumentWindow`'s destructor is the one caller and it does exactly that.
core::Status DocumentBackendImpl::close(core::DocumentHandle document)
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const auto found = open_.find(document.id);
    if (found == open_.end()) {
        // Closing a handle that is not open is a caller error worth naming, not a silent success:
        // it usually means two owners believe they own the same document, and the second close
        // would otherwise look like it worked.
        return core::Error{core::ErrorCode::NotFound, QLatin1String("error.open.notFound"),
                           QString::number(static_cast<qulonglong>(document.id))};
    }
    open_.erase(found);
    // The handle is NOT returned to a pool. `nextHandle_` only goes up, so a token held across
    // this call misses forever rather than starting to name a different document.
    return {};
}

core::IPageRasterizer* DocumentBackendImpl::rasterizer(core::DocumentHandle document)
{
    // CALLED FROM THE RENDER THREAD. See the mutex's note in the header.
    const std::lock_guard<std::mutex> guard(mutex_);
    Open* entry = find(document);
    return entry ? entry->rasterizer.get() : nullptr;
}

// THE LOCK IS HELD ONLY TO FIND THE ENTRY. A save of a large file takes seconds, and `rasterizer()`
// is asked from the render thread under the same mutex; holding it through the write would stall
// every tile behind the disk. The entry itself is only destroyed by `close()`, which the UI thread --
// the same thread that saves -- calls.
core::Status DocumentBackendImpl::writeRevision(core::DocumentHandle document, const QString& temporaryPath)
{
    Open* entry = nullptr;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        entry = find(document);
    }
    if (entry == nullptr || !entry->sink)
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), temporaryPath};
    const bool complete = entry->savedReport ? entry->savedReport->requiresSaveAs()
                                             : entry->document->repairReport().requiresSaveAs();
    return ops::RevisionWriter::write(*entry->sink, complete, temporaryPath);
}

core::Status DocumentBackendImpl::adoptRevision(core::DocumentHandle document, const QString& path)
{
    Open* entry = nullptr;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        entry = find(document);
    }
    if (entry == nullptr || !entry->sink)
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.save.notEditable"), path};

    auto adopted = ops::RevisionWriter::adopt(*entry->sink, path);
    if (!adopted)
        return adopted.error();
    // The sink now holds views into `adopted.bytes`; this assignment keeps that buffer and releases the
    // previous revision's (the engine keeps its own copy of the bytes it loaded).
    entry->bytes = std::move(adopted.value().bytes);
    entry->savedReport = adopted.value().report;
    entry->id = core::DocumentId::of(path, QFileInfo(path).lastModified().toMSecsSinceEpoch(), entry->bytes);
    RAWPAGE_LOG_INFO(core::LogSubsystem::Save)
        << "backend: handle" << static_cast<int>(document.id) << "adopted the revision saved at" << path
        << "(" << entry->bytes.size() << "bytes)";
    return core::Status{};
}

core::ITextExtractor* DocumentBackendImpl::textExtractor(core::DocumentHandle)
{
    return nullptr;   // Stage 5
}

core::MutationSink* DocumentBackendImpl::mutationSink(core::DocumentHandle document)
{
    // Called from the UI thread -- by `CommandJournal`, through a command's `apply()` -- but under
    // the same lock as `rasterizer()`, because `open()` and `close()` change the map it reads.
    const std::lock_guard<std::mutex> guard(mutex_);
    Open* entry = find(document);
    return entry ? entry->sink.get() : nullptr;
}

core::ISigner* DocumentBackendImpl::signer(core::DocumentHandle)
{
    return nullptr;   // Stage 11
}

core::IOcrEngine* DocumentBackendImpl::ocrEngine()
{
    return nullptr;   // Stage 16
}

core::IIndexer* DocumentBackendImpl::indexer()
{
    return nullptr;   // Stage 12
}

const core::PageTree* DocumentBackendImpl::pages(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    return entry ? &entry->document->pages() : nullptr;
}

const core::RepairReport* DocumentBackendImpl::repairReport(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    if (entry == nullptr)
        return nullptr;
    return entry->savedReport ? &*entry->savedReport : &entry->document->repairReport();
}

const core::Permissions* DocumentBackendImpl::permissions(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    return entry ? &entry->permissions : nullptr;
}

const core::DocumentId* DocumentBackendImpl::documentId(core::DocumentHandle document) const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const Open* entry = find(document);
    return entry ? &entry->id : nullptr;
}

} // namespace rawpage::pdf
