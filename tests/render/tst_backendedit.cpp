// tst_backendedit -- the SHIPPED backend's pre-flight, identity and sink. Stage 3 T4, guide cases
// A-36's corpus half, A-37, and A-34's engine half (`Stage3-Mutation-Testing-Guide.md` §5.6).
//
// WHY HERE: `pdf/DocumentBackendImpl` is the engine, and the headless suite may not link it (D2-2).
// `tst_permissions` plants each refusal into `Permissions::assess()`; this suite proves the backend
// feeds it the real readings -- PDFium's page tree and `pdf/ops/PageOps`' walk -- so that the encrypted
// corpus is refused, every ordinary file is editable, and the one file the readers disagree about is
// refused by name.
//
// WHAT IT DOES NOT COVER (convention 19): any mutation. `mutationSink()` is shown to be present and to
// accept a rotation; what that rotation writes is `tst_mutationsink` (T3) and the save is T8's.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Document.h"
#include "core/doc/DocumentId.h"
#include "core/doc/Permissions.h"
#include "pdf/DocumentBackendImpl.h"

#include <filesystem>
#include <string>

using rawpage::core::BackendInterface;
using rawpage::core::Document;
using rawpage::core::DocumentHandle;
using rawpage::pdf::DocumentBackendImpl;

namespace {

std::filesystem::path corpus(std::string_view relative)
{
    return std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative;
}

QString corpusPath(std::string_view relative)
{
    return QString::fromStdString(corpus(relative).string());
}

std::string keyOf(const rawpage::core::Permissions& permissions)
{
    return std::string(permissions.refusalKey().data(),
                       static_cast<std::size_t>(permissions.refusalKey().size()));
}

} // namespace

TEST_CASE("the shipped backend refuses editing every encrypted corpus file by name", "[render][backend][permissions]")
{
    // GUIDE CASE A-36: the engine OPENS these (with the public password), and the first-party reader
    // declines them (D2-6) -- so they are shown and not editable, refused up front rather than at Save.
    DocumentBackendImpl backend;
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus("encrypted"))) {
        if (entry.path().extension() != ".pdf")
            continue;
        INFO("file: " << entry.path().filename().string());
        auto handle = backend.open(QString::fromStdString(entry.path().string()),
                                   [](int) { return std::optional<QString>(QStringLiteral("user")); });
        REQUIRE(handle);
        const auto* permissions = backend.permissions(handle.value());
        REQUIRE(permissions != nullptr);
        CHECK_FALSE(permissions->canEditPages());
        CHECK(keyOf(*permissions) == "error.edit.encrypted");
        auto refusal = permissions->may(rawpage::core::Operation::EditPages);
        REQUIRE_FALSE(refusal);
        CHECK(refusal.error().code == rawpage::core::ErrorCode::PermissionDenied);
        // No sink for a document this build will not edit: a sink would be a way to edit it.
        CHECK(backend.mutationSink(handle.value()) == nullptr);
        CHECK(backend.close(handle.value()));
        ++files;
    }
    CHECK(files == 5);
}

TEST_CASE("the shipped backend lets every ordinary, signed and assembly corpus file be edited", "[render][backend][permissions]")
{
    // THE CONTROL for the refusals: an assessment that refused everything would pass the case above.
    // Stage 7 T1 added assembly/, whose inherit-resources.pdf was refused whole until Stage 7 guide I-01's
    // fix: the engine read its inherited boxes as US Letter and the first-party walk as A4.
    DocumentBackendImpl backend;
    std::size_t files = 0;
    for (const char* directory : {"ordinary", "signed", "assembly"}) {
        for (const auto& entry : std::filesystem::directory_iterator(corpus(directory))) {
            if (entry.path().extension() != ".pdf" || entry.path().filename() == "merge-not-a-pdf.pdf")
                continue;
            INFO("file: " << directory << "/" << entry.path().filename().string());
            auto handle = backend.open(QString::fromStdString(entry.path().string()), nullptr);
            REQUIRE(handle);
            const auto* permissions = backend.permissions(handle.value());
            REQUIRE(permissions != nullptr);
            INFO("refusal: " << keyOf(*permissions));
            CHECK(permissions->canEditPages());
            CHECK(backend.mutationSink(handle.value()) != nullptr);
            CHECK(backend.close(handle.value()));
            ++files;
        }
    }
    CHECK(files >= 36 + 21);   // Stage 7 T1: the 21 assembly PDFs
}

TEST_CASE("the shipped backend refuses editing the file its two readers count differently", "[render][backend][permissions]")
{
    // `malformed/pages-loop.pdf`: PDFium 2 pages, the walk 1 (Stage 3 guide issue I-07). The refusal
    // D3-7 exists for, on the corpus file that measured it -- with both counts in the context.
    DocumentBackendImpl backend;
    auto handle = backend.open(corpusPath("malformed/pages-loop.pdf"), nullptr);
    REQUIRE(handle);
    const auto* permissions = backend.permissions(handle.value());
    REQUIRE(permissions != nullptr);
    CHECK_FALSE(permissions->canEditPages());
    CHECK(keyOf(*permissions) == "error.edit.pageCountDisagrees");
    CHECK(permissions->refusalContext() == QStringLiteral("2/1"));
    CHECK(backend.mutationSink(handle.value()) == nullptr);
}

TEST_CASE("the shipped backend implements three of seven and hands out a sink per document", "[render][backend]")
{
    // GUIDE CASE A-37. FOUR of seven since Stage 5 T2 added `ITextExtractor` (Stage 5 guide case
    // A-25); the case's NAME still says three and is kept, because Stage 5 guide case A-109 requires
    // every Stage 4 name to stay registered.
    DocumentBackendImpl backend;
    const auto implemented = backend.implementedInterfaces();
    CHECK(implemented[static_cast<std::size_t>(BackendInterface::DocumentBackend)]);
    CHECK(implemented[static_cast<std::size_t>(BackendInterface::PageRasterizer)]);
    CHECK(implemented[static_cast<std::size_t>(BackendInterface::Mutation)]);
    CHECK(implemented[static_cast<std::size_t>(BackendInterface::TextExtractor)]);
    const auto missing = rawpage::core::unimplementedBackendInterfaces(backend);
    REQUIRE(missing.size() == 3);
    CHECK(missing[0] == "ISigner");
    CHECK(missing[1] == "IOcrEngine");
    CHECK(missing[2] == "IIndexer");

    auto first = backend.open(corpusPath("ordinary/text-a4.pdf"), nullptr);
    auto second = backend.open(corpusPath("ordinary/text-a4.pdf"), nullptr);
    REQUIRE(first);
    REQUIRE(second);
    rawpage::core::MutationSink* sink = backend.mutationSink(first.value());
    REQUIRE(sink != nullptr);
    CHECK(sink != backend.mutationSink(second.value()));   // one per document, not one per file
    CHECK(sink->setPageRotation(0, 90));

    // Null for an unknown handle and for a closed one; handles are never reused.
    CHECK(backend.mutationSink(DocumentHandle{999}) == nullptr);
    REQUIRE(backend.close(first.value()));
    CHECK(backend.mutationSink(first.value()) == nullptr);
    CHECK(backend.permissions(first.value()) == nullptr);
    CHECK(backend.documentId(first.value()) == nullptr);
    auto third = backend.open(corpusPath("ordinary/text-a4.pdf"), nullptr);
    REQUIRE(third);
    CHECK(third.value().id != first.value().id);
    CHECK(third.value().id != second.value().id);
}

TEST_CASE("the shipped backend takes the identity of the bytes it opened", "[render][backend][documentid]")
{
    DocumentBackendImpl backend;
    auto first = backend.open(corpusPath("ordinary/text-a4.pdf"), nullptr);
    auto again = backend.open(corpusPath("ordinary/text-a4.pdf"), nullptr);
    auto other = backend.open(corpusPath("ordinary/text-letter.pdf"), nullptr);
    REQUIRE(first);
    REQUIRE(again);
    REQUIRE(other);
    const auto* id = backend.documentId(first.value());
    REQUIRE(id != nullptr);
    CHECK(id->isValid());
    CHECK(id->size == static_cast<qint64>(std::filesystem::file_size(corpus("ordinary/text-a4.pdf"))));
    CHECK(*id == *backend.documentId(again.value()));
    CHECK(*id != *backend.documentId(other.value()));
    CHECK(id->path.endsWith(QStringLiteral("/corpus/ordinary/text-a4.pdf")));
}

TEST_CASE("a document hydrated from the shipped backend copies the engine's pages", "[render][backend][document]")
{
    // GUIDE CASE A-34, engine half: the model equals PDFium's tree for every ordinary file, and a
    // model rotation leaves the engine's reading alone (D3-15).
    DocumentBackendImpl backend;
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus("ordinary"))) {
        if (entry.path().extension() != ".pdf")
            continue;
        INFO("file: " << entry.path().filename().string());
        auto handle = backend.open(QString::fromStdString(entry.path().string()), nullptr);
        REQUIRE(handle);
        Document document;
        REQUIRE(document.hydrate(backend, handle.value()));
        CHECK(document.pages().pages() == backend.pages(handle.value())->pages());
        CHECK(document.permissions().canEditPages());
        CHECK(document.id() == *backend.documentId(handle.value()));

        const auto before = backend.pages(handle.value())->rotation(0);
        const int turned = (static_cast<int>(before) + 90) % 360;
        REQUIRE(document.pages().setRotation(0, turned));
        CHECK(static_cast<int>(document.pages().rotation(0)) == turned);
        CHECK(backend.pages(handle.value())->rotation(0) == before);
        CHECK(backend.close(handle.value()));
        ++files;
    }
    CHECK(files >= 32);
}
