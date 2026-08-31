// Stage 2 T1 -- `core/backend.h` and `pdf/BackendFactory`.
//
// Guide case A-46 in Stage2-Rendering-Testing-Guide.md 5.6, whose subject is decision D2-7: all
// seven engine interfaces are declared now, the backend says which of them it implements, and
// calling an unimplemented one returns an EXPLICIT not-implemented Result -- never a silent success
// and never an empty value.
//
// WHAT THIS CASE CANNOT COVER YET, said here rather than left to be inferred (convention 19). A-46
// also asserts that after Stage 2 the implemented set is {IDocumentBackend, IPageRasterizer}; that
// half becomes checkable at T9 and is not asserted below. What IS asserted is the opposite end of
// the same property -- that a backend implementing nothing reports nothing and refuses by name --
// because a census is only worth having if it can say no.
//
// It lives in the HEADLESS suite, which is the point: this file names the engine boundary, links
// rawpage_cos, and runs on portable-asan where no PDF engine exists at all.

#include <catch2/catch_test_macros.hpp>

#include "core/backend.h"
#include "pdf/BackendFactory.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

using namespace rawpage;
using rawpage::core::BackendInterface;
using rawpage::core::backendInterfaceNames;
using rawpage::core::kBackendInterfaceCount;

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

TEST_CASE("the T1 backend implements none of the seven and says so", "[backend]")
{
    const std::unique_ptr<core::IDocumentBackend> backend = pdf::createBackend();
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
    const std::unique_ptr<core::IDocumentBackend> backend = pdf::createBackend();

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
    const std::unique_ptr<core::IDocumentBackend> backend = pdf::createBackend();
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
