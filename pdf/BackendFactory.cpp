#include "pdf/BackendFactory.h"

#include "core/Log.h"

namespace rawpage::pdf {
namespace {

using core::BackendInterface;
using core::DocumentHandle;
using core::IDocumentBackend;
using core::IIndexer;
using core::IOcrEngine;
using core::IPageRasterizer;
using core::ISigner;
using core::LogSubsystem;
using core::MutationSink;
using core::ITextExtractor;
using core::kBackendInterfaceCount;
using core::notImplementedError;

// THE BACKEND THAT IMPLEMENTS NOTHING, AND SAYS SO.
//
// Every method answers `notImplementedError`, naming itself. Nothing here returns an empty value,
// a default-constructed handle or a silent success -- that is the whole point (D2-7, convention 6):
// a caller must be able to tell "this build does not do that" from "it did it and there was
// nothing there", because the second is data nobody produced and it surfaces stages later.
//
// `implementedInterfaces()` reports seven falses INCLUDING `IDocumentBackend` itself, and that is
// not a contradiction with this class deriving from it. The census is about what FUNCTIONS, not
// about what a vtable happens to satisfy -- a document cannot be opened by this object, so
// reporting `IDocumentBackend` as implemented would be the same lie in a different place. T9 flips
// two of the seven to true by replacing this class with `pdf/DocumentBackendImpl`.
class NullBackend final : public IDocumentBackend {
public:
    std::array<bool, kBackendInterfaceCount> implementedInterfaces() const override
    {
        return {false, false, false, false, false, false, false};
    }

    core::Result<DocumentHandle> open(const QString& path,
                                      const core::PasswordCallback& password) override
    {
        // The parameters are named rather than elided so the signature reads as the real one it
        // will be at T9, and are consumed here because -Werror is on in pdf/.
        (void)path;
        (void)password;
        return notImplementedError(BackendInterface::DocumentBackend, "open");
    }

    core::Status close(DocumentHandle document) override
    {
        (void)document;
        return notImplementedError(BackendInterface::DocumentBackend, "close");
    }

    IPageRasterizer* rasterizer(DocumentHandle document) override { (void)document; return nullptr; }
    ITextExtractor*  textExtractor(DocumentHandle document) override { (void)document; return nullptr; }
    MutationSink*    mutationSink(DocumentHandle document) override { (void)document; return nullptr; }
    ISigner*         signer(DocumentHandle document) override { (void)document; return nullptr; }
    IOcrEngine*      ocrEngine() override { return nullptr; }
    IIndexer*        indexer() override { return nullptr; }
};

} // namespace

std::unique_ptr<IDocumentBackend> createBackend()
{
    auto backend = std::make_unique<NullBackend>();

    // Logged rather than left implicit, for the reason `platform/win/PlatformFactory` logs its own
    // census: "none of the seven engine interfaces is built yet" is a fact a support log should be
    // able to state, and the count is what a later stage's log will be compared against.
    const std::vector<std::string_view> missing = core::unimplementedBackendInterfaces(*backend);
    RAWPAGE_LOG_INFO(LogSubsystem::Render)
        << "backend: assembled --"
        << static_cast<int>(kBackendInterfaceCount - missing.size()) << "of"
        << static_cast<int>(kBackendInterfaceCount) << "interfaces implemented";

    for (const std::string_view name : missing) {
        RAWPAGE_LOG_DEBUG(LogSubsystem::Render)
            << "backend: not implemented in this build --" << std::string(name).c_str();
    }

    return backend;
}

} // namespace rawpage::pdf
