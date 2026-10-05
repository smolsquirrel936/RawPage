// `core/backend.h` -- the engine boundary itself. Stage 2 T1, split at T9.
//
// Guide case A-46 in Stage2-Rendering-Testing-Guide.md 5.6, whose subject is decision D2-7: all
// seven engine interfaces are declared, a backend says which of them it implements, and calling an
// unimplemented one returns an EXPLICIT not-implemented Result -- never a silent success and never
// an empty value.
//
// **THIS FILE WAS SPLIT AT T9 AND THE REASON IS LOAD-BEARING.** Until then it exercised
// `pdf::createBackend()` directly, because the backend that factory returned implemented nothing
// and needed no engine. A backend that opens documents IS the engine, so the factory moved into
// `rawpage_pdfium` -- which `rawpage_assert_headless()` forbids this suite from linking by name
// (D2-2). The concrete backend's census therefore moved to `tests/render/tst_backendimpl.cpp`,
// beside the engine.
//
// What stays here is the part that is genuinely portable, and it is not a leftover: the interface
// declarations, `notImplementedError`, `unimplementedBackendInterfaces`, and `RasterImage`'s own
// validation -- all asserted over a backend DEFINED IN THIS FILE. That is stronger than it sounds.
// A test backend written here is the only way to assert what an unimplemented backend does now
// that the real one implements things, and it keeps every one of those checks running under
// `portable-asan`, where no PDF engine exists at all.
//
// WHAT THIS FILE THEREFORE DOES NOT COVER (convention 19): anything about the SHIPPED backend.
// A-46's second half -- that the implemented set is `{IDocumentBackend, IPageRasterizer}` after
// Stage 2 -- is `tests/render/`'s, and this file cannot see it.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Destinations.h"
#include "core/doc/Metadata.h"
#include "core/doc/PageTransitions.h"
#include "core/doc/Outline.h"
#include "core/backend.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

using namespace rawpage;
using rawpage::core::BackendInterface;
using rawpage::core::backendInterfaceNames;
using rawpage::core::kBackendInterfaceCount;

namespace {

// THE BACKEND THAT IMPLEMENTS NOTHING, AND SAYS SO -- defined here rather than obtained from
// `pdf::createBackend()`, which now returns a real one.
//
// Every method answers `notImplementedError`, naming itself. Nothing returns an empty value, a
// default-constructed handle or a silent success: that is the whole point (D2-7, convention 6). A
// caller must be able to tell "this build does not do that" from "it did it and there was nothing
// there", because the second is data nobody produced and it surfaces stages later.
//
// `implementedInterfaces()` reports seven falses INCLUDING `IDocumentBackend` itself, and that is
// not a contradiction with deriving from it: the census is about what FUNCTIONS, not about what a
// vtable happens to satisfy.
class NothingBackend final : public core::IDocumentBackend {
public:
    std::array<bool, kBackendInterfaceCount> implementedInterfaces() const override
    {
        return {false, false, false, false, false, false, false};
    }

    core::Result<core::DocumentHandle> open(const QString&, const core::PasswordCallback&) override
    {
        return core::notImplementedError(BackendInterface::DocumentBackend, "open");
    }
    core::Status close(core::DocumentHandle) override
    {
        return core::notImplementedError(BackendInterface::DocumentBackend, "close");
    }
    // Null, like every other accessor: a backend that opens nothing has no page tree to hand
    // back, and an empty `PageTree` returned instead would let a caller believe it was looking at
    // a zero-page document rather than at nothing.
    const core::PageTree* pages(core::DocumentHandle) const override { return nullptr; }
    const core::RepairReport* repairReport(core::DocumentHandle) const override { return nullptr; }
    const core::Permissions* permissions(core::DocumentHandle) const override { return nullptr; }
    const core::DocumentId* documentId(core::DocumentHandle) const override { return nullptr; }
    // S4 T4 (D4-3, D4-4). This double reads no file, so it carries EMPTY models rather than null:
    // null means *not a handle of mine*, and empty means *this document has none*, which is the
    // distinction `core/backend.h` asks callers to make. A case that needs an outline fills these.
    const core::Outline* outline(core::DocumentHandle) const override { return &outline_; }
    const core::Destinations* destinations(core::DocumentHandle) const override
    {
        return &destinations_;
    }
    const core::Metadata* metadata(core::DocumentHandle) const override { return &metadata_; }

    // S4 T10 (D4-12): empty, which is the ordinary answer -- almost no document carries one.
    const core::AnnotationStore* annotations(core::DocumentHandle) const override { return nullptr; }
    const core::PageTransitions* pageTransitions(core::DocumentHandle) const override
    {
        return &transitions_;
    }

    // S4 T12 (D4-14): this fake has no engine and therefore no objects to bound. An explicit
    // not-implemented, never an empty rectangle -- `core/backend.h`'s rule, and the difference
    // between *this build does not do that* and *the page is blank*, which `FitPolicy` treats
    // differently only because the two are distinguishable here.
    core::Result<core::PdfRect> contentBox(core::DocumentHandle, int) override
    {
        return core::notImplementedError(core::BackendInterface::DocumentBackend, "contentBox");
    }
    core::Status searchUnits(core::DocumentHandle, int, std::vector<core::text::SearchUnit>&) override
    {
        return core::notImplementedError(core::BackendInterface::DocumentBackend, "searchUnits");
    }
    core::Result<core::OpenProgress> beginOpen(const QString&, const core::PasswordCallback&) override
    {
        return core::notImplementedError(core::BackendInterface::DocumentBackend, "beginOpen");
    }
    core::Result<core::OpenProgress> continueOpen(core::OpeningId) override
    {
        return core::notImplementedError(core::BackendInterface::DocumentBackend, "continueOpen");
    }
    void abandonOpen(core::OpeningId) override {}

    core::Outline outline_;
    core::Destinations destinations_;
    core::Metadata metadata_;
    core::PageTransitions transitions_;


    // Stage 3 T8: nothing here writes; a save through this backend fails by key.
    core::Status writeRevision(core::DocumentHandle, const QString&) override
    {
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.save.notEditable"), QString()};
    }
    core::Status adoptRevision(core::DocumentHandle, const QString&) override
    {
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.save.notEditable"), QString()};
    }

    core::IPageRasterizer* rasterizer(core::DocumentHandle) override { return nullptr; }
    core::ITextExtractor* textExtractor(core::DocumentHandle) override { return nullptr; }
    core::MutationSink* mutationSink(core::DocumentHandle) override { return nullptr; }
    core::ISigner* signer(core::DocumentHandle) override { return nullptr; }
    core::IOcrEngine* ocrEngine() override { return nullptr; }
    core::IIndexer* indexer() override { return nullptr; }
};

std::unique_ptr<core::IDocumentBackend> makeNothingBackend()
{
    return std::make_unique<NothingBackend>();
}

} // namespace

// =======================================================================================
// The census
// =======================================================================================

TEST_CASE("all seven engine interfaces are declared and named", "[backend]")
{
    // Seven, not "however many are listed": D2-7 names the set, and a count read off the array it
    // is asserting against would be a copy of the table rather than a check on it.
    REQUIRE(kBackendInterfaceCount == 7);
    REQUIRE(backendInterfaceNames().size() == kBackendInterfaceCount);

    // The enum and the name table are two lists that must stay in step, and nothing in the language
    // makes them: this is what notices when a later stage adds an interface to one of them only.
    REQUIRE(core::backendInterfaceName(BackendInterface::DocumentBackend) == "IDocumentBackend");
    REQUIRE(core::backendInterfaceName(BackendInterface::PageRasterizer) == "IPageRasterizer");
    REQUIRE(core::backendInterfaceName(BackendInterface::TextExtractor) == "ITextExtractor");
    REQUIRE(core::backendInterfaceName(BackendInterface::Mutation) == "MutationSink");
    REQUIRE(core::backendInterfaceName(BackendInterface::Signer) == "ISigner");
    REQUIRE(core::backendInterfaceName(BackendInterface::OcrEngine) == "IOcrEngine");
    REQUIRE(core::backendInterfaceName(BackendInterface::Indexer) == "IIndexer");

    // No duplicates and no empty entry -- a name table is what a log line and a diagnostic print,
    // and two interfaces sharing a name is a census that cannot be acted on.
    for (const std::string_view name : backendInterfaceNames()) {
        REQUIRE_FALSE(name.empty());
        REQUIRE(std::count(backendInterfaceNames().begin(), backendInterfaceNames().end(), name) == 1);
    }
}

TEST_CASE("a backend implementing none of the seven says so", "[backend]")
{
    const std::unique_ptr<core::IDocumentBackend> backend = makeNothingBackend();
    REQUIRE(backend != nullptr);

    const std::array<bool, kBackendInterfaceCount> implemented = backend->implementedInterfaces();
    REQUIRE(std::none_of(implemented.begin(), implemented.end(), [](bool b) { return b; }));

    // And the census reports it by NAME, in the order of the enum -- which is what makes "not built
    // yet" printable rather than something a caller learns by dereferencing null.
    const std::vector<std::string_view> missing = core::unimplementedBackendInterfaces(*backend);
    REQUIRE(missing.size() == kBackendInterfaceCount);
    REQUIRE(missing.front() == "IDocumentBackend");
    REQUIRE(missing.back() == "IIndexer");
}

// =======================================================================================
// Refusal, by name -- convention 6's argument
// =======================================================================================

TEST_CASE("an unimplemented backend refuses by name rather than succeeding quietly", "[backend]")
{
    const std::unique_ptr<core::IDocumentBackend> backend = makeNothingBackend();

    // A password callback that would be a defect if it ran: nothing here reaches the point of
    // asking, and a case that could not tell the difference would pass against a backend that
    // opened the file and returned an empty document.
    bool asked = false;
    const core::PasswordCallback password = [&asked](int) -> std::optional<QString> {
        asked = true;
        return std::nullopt;
    };

    const core::Result<core::DocumentHandle> opened =
        backend->open(QStringLiteral("C:/nonexistent/whatever.pdf"), password);

    REQUIRE_FALSE(opened);
    REQUIRE(opened.error().code == core::ErrorCode::Unsupported);
    REQUIRE(opened.error().messageKey == core::kBackendNotImplementedKey);
    // The context names the interface AND the method, because "the backend does not implement
    // something" is not a diagnostic anyone can act on.
    REQUIRE(opened.error().context == QStringLiteral("IDocumentBackend::open"));
    REQUIRE_FALSE(asked);

    const core::Status closed = backend->close(core::DocumentHandle{});
    REQUIRE_FALSE(closed);
    REQUIRE(closed.error().context == QStringLiteral("IDocumentBackend::close"));
}

TEST_CASE("an unimplemented sub-interface is null, not an empty stub", "[backend]")
{
    const std::unique_ptr<core::IDocumentBackend> backend = makeNothingBackend();
    const core::DocumentHandle handle{};

    // The direct analogue of platform::Platform's null members. A stub returning an object whose
    // every method quietly did nothing is the failure mode convention 6 exists to prevent: a caller
    // cannot tell "not built" from "built, and there was nothing there".
    REQUIRE(backend->rasterizer(handle) == nullptr);
    REQUIRE(backend->textExtractor(handle) == nullptr);
    REQUIRE(backend->mutationSink(handle) == nullptr);
    REQUIRE(backend->signer(handle) == nullptr);
    REQUIRE(backend->ocrEngine() == nullptr);
    REQUIRE(backend->indexer() == nullptr);
}

// =======================================================================================
// The error helper itself, including the negative control
// =======================================================================================

TEST_CASE("notImplementedError names the interface and carries a catalog key", "[backend]")
{
    const core::Error error = core::notImplementedError(BackendInterface::Signer, "sign");
    REQUIRE(error.code == core::ErrorCode::Unsupported);
    REQUIRE(error.messageKey == core::kBackendNotImplementedKey);
    REQUIRE(error.context == QStringLiteral("ISigner::sign"));

    // The key is a KEY and not a sentence -- core/Result.h's rule, and the reason the product can
    // ship in zh-TW. A test that only checked the code would pass against an English sentence.
    REQUIRE(QString(error.messageKey).startsWith(QStringLiteral("error.")));

    // THE CONTROL. Without a method name the context is still meaningful, and without this case
    // an implementation that returned an empty context for every call would satisfy everything
    // above by carrying a name that happened to be right in one place.
    const core::Error bare = core::notImplementedError(BackendInterface::Indexer, {});
    REQUIRE(bare.context == QStringLiteral("IIndexer"));
}

// =======================================================================================
// The buffer contract -- what may cross this boundary
// =======================================================================================

TEST_CASE("a RasterImage validates its own stride rather than trusting width", "[backend]")
{
    // The boundary carries BYTES, not a QImage -- core/ may not name a QtGui type (convention 3),
    // and this is the type that replaces it. bytesPerLine is carried rather than computed because a
    // rasterizer may align its rows, and a consumer assuming width * 4 on a padded buffer draws
    // diagonal garbage -- a defect that reads as a rendering bug rather than an arithmetic one.
    core::RasterImage image;
    REQUIRE_FALSE(image.isValid());

    image.width = 4;
    image.height = 2;
    image.bytesPerLine = 24;                 // padded: 16 bytes of pixels, 8 of alignment
    image.pixels = QByteArray(48, '\0');
    REQUIRE(image.isValid());

    // One byte short of a full last row is the shape of the bug this guard exists for.
    image.pixels.chop(1);
    REQUIRE_FALSE(image.isValid());

    // And a stride that cannot hold the row it claims.
    image.pixels = QByteArray(48, '\0');
    image.bytesPerLine = 8;
    REQUIRE_FALSE(image.isValid());
}
