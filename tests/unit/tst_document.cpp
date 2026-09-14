// tst_document -- `core/doc/Document` hydration and `core/doc/DocumentId`. Stage 3 T4, guide cases
// A-34's headless half and A-35 (`Stage3-Mutation-Testing-Guide.md` §5.6).
//
// WHAT THIS SUITE IS TRYING TO CATCH:
//   * A model that is a VIEW of the backend's tree rather than a copy -- after a rotation the model
//     must differ and the backend must not (D3-15).
//   * A hydration that half-happens: a handle the backend does not know leaves the document as it was.
//   * An identity that misses a changed file (D3-16's guard) or splits one file into two.
//
// WHAT IT DOES NOT COVER, named at the assertion (convention 19):
//   * **The engine's page tree.** The backend here is `CosBackend` below, which builds its tree from
//     `pdf/ops/PageOps` over the same bytes. Hydrating from the SHIPPED backend, whose tree is
//     PDFium's, is `tests/render/tst_backendedit.cpp` (A-34's engine half).
//   * **A file on disk.** `DocumentId` is exercised as the value type it is; the backend's stat of a
//     real file is the render suite's.

#include <catch2/catch_test_macros.hpp>

#include "core/backend.h"
#include "core/doc/Document.h"
#include "core/doc/DocumentId.h"
#include "core/doc/Permissions.h"
#include "pdf/ops/MutationSinkImpl.h"

#include <QFile>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>

using namespace rawpage;
using core::DocumentId;

namespace {

std::filesystem::path corpus(std::string_view relative)
{
    return std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative;
}

// A backend whose page tree comes from the FIRST-PARTY reader, so hydration is testable with no engine.
class CosBackend final : public core::IDocumentBackend {
public:
    std::array<bool, core::kBackendInterfaceCount> implementedInterfaces() const override
    {
        std::array<bool, core::kBackendInterfaceCount> implemented{};
        implemented[static_cast<std::size_t>(core::BackendInterface::DocumentBackend)] = true;
        return implemented;
    }

    core::Result<core::DocumentHandle> open(const QString& path, const core::PasswordCallback&) override
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return core::Error{core::ErrorCode::NotFound, QLatin1String("error.open.notFound"), path};
        auto entry = std::make_unique<Entry>();
        entry->bytes = file.readAll();
        auto sink = pdf::ops::MutationSinkImpl::open(
            std::string_view(entry->bytes.constData(), static_cast<std::size_t>(entry->bytes.size())));
        if (!sink)
            return sink.error();

        std::vector<core::Page> pages;
        std::vector<core::CosPageFacts> facts;
        int index = 0;
        for (const auto& walked : sink.value()->pages().pages) {
            core::PdfRect box{core::PdfPoint{0.0, 0.0}, core::PdfSize{612.0, 792.0}};
            if (walked.mediaBox) {
                const auto& b = *walked.mediaBox;
                box = core::PdfRect{core::PdfPoint{b[0], b[1]}, core::PdfSize{b[2] - b[0], b[3] - b[1]}};
            }
            core::Page page(index++, box);
            page.setRotation(walked.effectiveRotate());
            pages.push_back(page);
            facts.push_back(core::CosPageFacts{walked.effectiveRotate(), box.size});
        }
        entry->tree = core::PageTree(std::move(pages));
        entry->permissions = core::Permissions::assess(entry->tree, core::CosReading::Read, facts);
        entry->id = DocumentId::of(path, 1'700'000'000'000LL, entry->bytes);

        const core::DocumentHandle handle{next_++};
        entries_.emplace(handle.id, std::move(entry));
        return handle;
    }
    core::Status close(core::DocumentHandle handle) override
    {
        entries_.erase(handle.id);
        return {};
    }
    const core::PageTree* pages(core::DocumentHandle handle) const override
    {
        const auto* entry = find(handle);
        return entry ? &entry->tree : nullptr;
    }
    const core::RepairReport* repairReport(core::DocumentHandle handle) const override
    {
        const auto* entry = find(handle);
        return entry ? &entry->repair : nullptr;
    }
    const core::Permissions* permissions(core::DocumentHandle handle) const override
    {
        const auto* entry = find(handle);
        return entry ? &entry->permissions : nullptr;
    }
    const core::DocumentId* documentId(core::DocumentHandle handle) const override
    {
        const auto* entry = find(handle);
        return entry ? &entry->id : nullptr;
    }
    core::IPageRasterizer* rasterizer(core::DocumentHandle) override { return nullptr; }
    core::ITextExtractor* textExtractor(core::DocumentHandle) override { return nullptr; }
    core::MutationSink* mutationSink(core::DocumentHandle) override { return nullptr; }
    core::ISigner* signer(core::DocumentHandle) override { return nullptr; }
    core::IOcrEngine* ocrEngine() override { return nullptr; }
    core::IIndexer* indexer() override { return nullptr; }

private:
    struct Entry {
        QByteArray bytes;
        core::PageTree tree;
        core::RepairReport repair;
        core::Permissions permissions;
        DocumentId id;
    };
    const Entry* find(core::DocumentHandle handle) const
    {
        const auto found = entries_.find(handle.id);
        return found == entries_.end() ? nullptr : found->second.get();
    }
    std::map<std::uint64_t, std::unique_ptr<Entry>> entries_;
    std::uint64_t next_ = 1;
};

QString corpusPath(std::string_view relative)
{
    return QString::fromStdString(corpus(relative).string());
}

} // namespace

TEST_CASE("document hydrates a copy of the backend's pages for every corpus file", "[doc][document]")
{
    // GUIDE CASE A-34, headless half. NOT the engine's tree (see the header).
    CosBackend backend;
    std::size_t files = 0;
    for (const char* directory : {"ordinary", "signed"}) {
        for (const auto& entry : std::filesystem::directory_iterator(corpus(directory))) {
            if (entry.path().extension() != ".pdf")
                continue;
            INFO("file: " << entry.path().filename().string());
            auto handle = backend.open(QString::fromStdString(entry.path().string()), nullptr);
            REQUIRE(handle);

            core::Document document;
            REQUIRE(document.hydrate(backend, handle.value()));
            CHECK(document.handle() == handle.value());
            CHECK(document.pages().pages() == backend.pages(handle.value())->pages());
            CHECK(document.revision() == core::Revision::initial());
            CHECK(document.permissions().canEditPages());
            CHECK(document.id() == *backend.documentId(handle.value()));
            ++files;
        }
    }
    CHECK(files >= 36);
}

TEST_CASE("document rotation changes the model and not the backend", "[doc][document]")
{
    // D3-15: the engine keeps the rotation it loaded; the MODEL is what a save will write. A model
    // that referenced the backend's tree instead of copying it would change both here.
    CosBackend backend;
    auto handle = backend.open(corpusPath("ordinary/text-a4.pdf"), nullptr);
    REQUIRE(handle);
    core::Document document;
    REQUIRE(document.hydrate(backend, handle.value()));

    REQUIRE(document.pages().setRotation(1, 90));
    CHECK(document.pages().rotation(1) == core::PageRotation::Clockwise90);
    CHECK(backend.pages(handle.value())->rotation(1) == core::PageRotation::None);
    // The transposed display size follows the model's rotation.
    CHECK(document.pages().displaySize(1).width == backend.pages(handle.value())->displaySize(1).height);

    // Normalized as Page normalizes, and out of range changes nothing.
    REQUIRE(document.pages().setRotation(1, -90));
    CHECK(document.pages().rotation(1) == core::PageRotation::Clockwise270);
    CHECK_FALSE(document.pages().setRotation(3, 90));
    CHECK_FALSE(document.pages().setRotation(-1, 90));

    // Revisions only go up.
    document.advanceRevision();
    CHECK(document.revision() == core::Revision::initial().next());
}

TEST_CASE("document hydration of an unknown handle changes nothing", "[doc][document]")
{
    CosBackend backend;
    auto handle = backend.open(corpusPath("ordinary/text-a4.pdf"), nullptr);
    REQUIRE(handle);
    core::Document document;
    REQUIRE(document.hydrate(backend, handle.value()));
    REQUIRE(document.pages().setRotation(0, 180));

    auto refused = document.hydrate(backend, core::DocumentHandle{999});
    REQUIRE_FALSE(refused);
    CHECK(std::string(refused.error().messageKey.data()) == "error.open.notFound");
    CHECK(document.handle() == handle.value());
    CHECK(document.pages().rotation(0) == core::PageRotation::Half);

    // A never-hydrated document refuses editing rather than allowing it.
    core::Document empty;
    CHECK_FALSE(empty.permissions().canEditPages());
    CHECK(std::string(empty.permissions().refusalKey().data()) == "error.edit.notAssessed");
    CHECK_FALSE(empty.revision().isValid());
}

TEST_CASE("document id notices a changed file and not a respelled path", "[doc][documentid]")
{
    // GUIDE CASE A-35.
    const QByteArray bytes("%PDF-1.7\nthe same bytes\n");
    const qint64 when = 1'757'808'000'000LL;
    const DocumentId base = DocumentId::of(QStringLiteral("C:\\Docs\\Report.pdf"), when, bytes);
    REQUIRE(base.isValid());

    SECTION("the same file twice is equal")
    {
        CHECK(DocumentId::of(QStringLiteral("C:\\Docs\\Report.pdf"), when, bytes) == base);
        CHECK(DocumentId::of(QStringLiteral("C:\\Docs\\Report.pdf"), when, bytes).key() == base.key());
    }
    SECTION("a touched modification time is different")
    {
        const DocumentId touched = DocumentId::of(QStringLiteral("C:\\Docs\\Report.pdf"), when + 1, bytes);
        CHECK(touched != base);
        CHECK(touched.key() != base.key());
    }
    SECTION("a same-size same-time content change is different because of the hash")
    {
        QByteArray changed = bytes;
        changed[changed.size() - 2] = 'S';
        REQUIRE(changed.size() == bytes.size());
        const DocumentId edited = DocumentId::of(QStringLiteral("C:\\Docs\\Report.pdf"), when, changed);
        CHECK(edited.size == base.size);
        CHECK(edited.modifiedMsecs == base.modifiedMsecs);
        CHECK(edited != base);
    }
    SECTION("two spellings of one Windows path are equal")
    {
        for (const char* spelling : {"c:/docs/report.pdf", "C:\\Docs\\.\\Report.PDF",
                                     "C:\\Docs\\Sub\\..\\Report.pdf", "C:\\\\Docs//Report.pdf"}) {
            INFO("spelling: " << spelling);
            CHECK(DocumentId::of(QString::fromUtf8(spelling), when, bytes) == base);
        }
        // THE CONTROL: a different file is not folded onto it.
        CHECK(DocumentId::of(QStringLiteral("C:\\Docs\\Report2.pdf"), when, bytes) != base);
    }
}

TEST_CASE("document id normalizes paths by hand and identically on every host", "[doc][documentid]")
{
    // Convention 11: these are literal strings in, literal strings out -- no QDir, no host separator.
    // The same assertions run on portable-asan (Linux), where a host rule would give other answers.
    CHECK(DocumentId::normalizePath(QStringLiteral("C:\\A\\B\\..\\..\\..\\x.pdf")) == QStringLiteral("c:/x.pdf"));
    CHECK(DocumentId::normalizePath(QStringLiteral("\\\\Server\\Share\\..\\x.pdf"))
          == QStringLiteral("//server/share/x.pdf"));
    CHECK(DocumentId::normalizePath(QStringLiteral("C:/Docs/")) == QStringLiteral("c:/docs"));
    CHECK(DocumentId::normalizePath(QStringLiteral("/home/u/./a.pdf")) == QStringLiteral("/home/u/a.pdf"));
}
