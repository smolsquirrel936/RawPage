#include "pdf/DocumentBackendImpl.h"

#include "core/Log.h"
#include "pdf/render/PdfiumDocument.h"
#include "pdf/render/PdfiumRasterizer.h"

#include <utility>

namespace rawpage::pdf {

// One open document and the rasterizer that serves it. The rasterizer holds the document by
// reference, so declaration order here is the destruction contract: `rasterizer` is destroyed
// first because members are destroyed in reverse order of declaration.
struct DocumentBackendImpl::Open {
    std::unique_ptr<render::PdfiumDocument> document;
    std::unique_ptr<render::PdfiumRasterizer> rasterizer;
};

DocumentBackendImpl::DocumentBackendImpl() = default;
DocumentBackendImpl::~DocumentBackendImpl() = default;

std::array<bool, core::kBackendInterfaceCount> DocumentBackendImpl::implementedInterfaces() const
{
    std::array<bool, core::kBackendInterfaceCount> implemented{};
    implemented.fill(false);
    implemented[static_cast<std::size_t>(core::BackendInterface::DocumentBackend)] = true;
    implemented[static_cast<std::size_t>(core::BackendInterface::PageRasterizer)] = true;
    // The other five stay false and their accessors stay null. Convention 6: a stub that silently
    // succeeds surfaces stages later as data nobody produced.
    return implemented;
}

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

    const core::DocumentHandle handle{nextHandle_++};
    RAWPAGE_LOG_INFO(core::LogSubsystem::Render)
        << "backend: opened" << path << "as handle" << static_cast<int>(handle.id) << "with"
        << entry->document->pageCount() << "pages";
    open_.emplace(handle.id, std::move(entry));
    return handle;
}

core::Status DocumentBackendImpl::close(core::DocumentHandle document)
{
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
    Open* entry = find(document);
    return entry ? entry->rasterizer.get() : nullptr;
}

core::ITextExtractor* DocumentBackendImpl::textExtractor(core::DocumentHandle)
{
    return nullptr;   // Stage 5
}

core::MutationSink* DocumentBackendImpl::mutationSink(core::DocumentHandle)
{
    return nullptr;   // Stage 3
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
    const Open* entry = find(document);
    return entry ? &entry->document->pages() : nullptr;
}

const core::RepairReport* DocumentBackendImpl::repairReport(core::DocumentHandle document) const
{
    const Open* entry = find(document);
    return entry ? &entry->document->repairReport() : nullptr;
}

} // namespace rawpage::pdf
