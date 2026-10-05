// tst_saveengine -- the shipped save path, end to end: the real backend, the real `WinDurableFiles`, and the
// engine reading back what was written. Stage 3 T8, guide cases A-54's real half, A-57's writing half and
// A-58 (`Stage3-Mutation-Testing-Guide.md` §5.9), decisions D3-1, D3-4 and D3-15.
//
// WHY HERE: the engine re-read needs PDFium, and the flush and replace need `platform/win/`; this target
// links both (D3-18). The byte-level round trip over the corpus is `tests/roundtrip/` and runs headless.
//
// WHAT IT DOES NOT COVER (convention 19):
//   * Acrobat's verdict on the signed files -- manual case M-03's product walk (T13);
//   * `ui/`: the document is driven through `CommandJournal` and `SaveCoordinator` directly, as T10's
//     `DocumentWindow` is to drive them;
//   * the pixels at a scale beyond the one rendered here, or a page other than the one rotated.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/AnnotationStore.h"
#include "core/doc/Document.h"
#include "core/doc/SaveCoordinator.h"
#include "core/render/RenderRequest.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/ops/RevisionWriter.h"
#include "platform/win/WinDurableFiles.h"
#include "platform/win/WinFontResolver.h"
#include "pdf/render/FontResolverBridge.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

using namespace rawpage;
using core::SaveOutcome;
using pdf::DocumentBackendImpl;

namespace {

namespace fs = std::filesystem;

QByteArray readAll(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

// Counts the real implementation's calls, and changes nothing about them.
class CountingDurableFiles final : public platform::IDurableFiles {
public:
    core::Status syncToDisk(const QString& path) override { ++syncs; return real.syncToDisk(path); }
    core::Status replaceFile(const QString& target, const QString& replacement) override
    {
        ++replaces;
        return real.replaceFile(target, replacement);
    }
    core::Status removeFile(const QString& path) override { return real.removeFile(path); }
    bool processExists(qint64 processId) const override { return real.processExists(processId); }

    platform::win::WinDurableFiles real;
    int syncs = 0;
    int replaces = 0;
};

// One document, opened by the SHIPPED backend from a copy.
struct EngineSession {
    DocumentBackendImpl backend;
    CountingDurableFiles durableFiles;
    QString path;
    QByteArray openedBytes;
    core::DocumentHandle handle;
    std::unique_ptr<core::CommandJournal> journal;
    std::unique_ptr<core::SaveCoordinator> coordinator;
};

std::unique_ptr<EngineSession> openCopy(const QString& source, const QString& copyPath)
{
    auto session = std::make_unique<EngineSession>();
    REQUIRE(QFile::copy(source, copyPath));
    QFile::setPermissions(copyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    session->path = copyPath;
    session->openedBytes = readAll(copyPath);
    auto handle = session->backend.open(copyPath, nullptr);
    if (!handle || session->backend.mutationSink(handle.value()) == nullptr)
        return nullptr;
    session->handle = handle.value();
    core::Document document;
    REQUIRE(document.hydrate(session->backend, session->handle));
    session->journal = std::make_unique<core::CommandJournal>(std::move(document),
                                                               session->backend.mutationSink(session->handle));
    session->coordinator = std::make_unique<core::SaveCoordinator>(session->backend, &session->durableFiles);
    return session;
}

// Saves, taking D3-6's Save-As to `saveAsPath` when the document requires it. Returns the file written.
QString saveOrSaveAs(EngineSession& session, const QString& saveAsPath)
{
    auto result = session.coordinator->save(*session.journal, session.path);
    INFO("save error: " << (result ? std::string() : std::string(result.error().messageKey.data()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    if (result.value().kind == SaveOutcome::Kind::Saved)
        return session.path;
    auto asResult = session.coordinator->saveAs(*session.journal, saveAsPath);
    INFO("save-as error: " << (asResult ? std::string() : std::string(asResult.error().messageKey.data()) + " " + asResult.error().context.toStdString()));
    REQUIRE(asResult);
    REQUIRE(asResult.value().kind == SaveOutcome::Kind::Saved);
    return saveAsPath;
}

// The whole of page `page` at a small scale, drawn with `delta` on top of what the engine loaded.
core::RasterImage renderPage(DocumentBackendImpl& backend, core::DocumentHandle handle, int page,
                             core::PageRotation delta)
{
    const core::PageTree* tree = backend.pages(handle);
    REQUIRE(tree != nullptr);
    const core::Page* entry = tree->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfSize size = entry->displaySize();
    const bool swap = core::swapsAxes(delta);
    const double scale = 0.25;

    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = page;
    request.deviceScale = scale;
    request.revision = core::Revision::initial();
    request.pageRotationDelta = delta;
    request.tile = core::render::TileRect{0, 0,
                                          std::max(1, static_cast<int>(std::ceil((swap ? size.height : size.width) * scale))),
                                          std::max(1, static_cast<int>(std::ceil((swap ? size.width : size.height) * scale)))};
    core::IPageRasterizer* rasterizer = backend.rasterizer(handle);
    REQUIRE(rasterizer != nullptr);
    auto image = rasterizer->raster(request);
    REQUIRE(image);
    return image.value();
}

struct PixelDifference {
    qsizetype bytes = 0;   // channels that differ at all
    int largest = 0;       // the largest difference in any one channel, 0-255
};

// Compared by hand into numbers: a failed CHECK on two QByteArrays crashes Catch2's console reporter.
PixelDifference compare(const core::RasterImage& a, const core::RasterImage& b)
{
    PixelDifference difference;
    const qsizetype count = std::min(a.pixels.size(), b.pixels.size());
    for (qsizetype i = 0; i < count; ++i) {
        const int d = std::abs(static_cast<int>(static_cast<unsigned char>(a.pixels[i]))
                               - static_cast<int>(static_cast<unsigned char>(b.pixels[i])));
        if (d != 0) {
            ++difference.bytes;
            difference.largest = std::max(difference.largest, d);
        }
    }
    return difference;
}

core::PageRotation deltaOf(int modelRotation, int engineRotation)
{
    return core::rotateBy(core::PageRotation::None, ((modelRotation - engineRotation) % 360 + 360) % 360 / 90);
}

} // namespace

// ==========================================================================================
// A-58 -- the engine re-reads what we wrote, and draws the same page (D3-15)
// ==========================================================================================

TEST_CASE("save engine reopens every saved corpus file and draws the saved rotation as the unsaved delta drew it", "[render][save]")
{
    // THE PRODUCT'S ASSEMBLY: the font bridge installed, as app/CompositionRoot installs it. Measured at T8:
    // WITHOUT it, PDFium's own fallback draws `malformed/bad-name-hex-escape.pdf` (whose broken name leaves
    // an unembedded font that is not one of the standard fourteen) differently on every open in one
    // process -- four backends, four different images -- which is the engine's fallback, not anything
    // saved (guide issue I-09). With it, every open draws the same pixels.
    platform::win::WinFontResolver resolver;
    REQUIRE(pdf::render::FontResolverBridge::install(resolver));
    struct Uninstall { ~Uninstall() { pdf::render::FontResolverBridge::uninstall(); } } uninstall;

    std::size_t files = 0;
    std::size_t notEditable = 0;
    for (const char* directory : {"ordinary", "signed", "malformed"}) {
        for (const auto& item : fs::directory_iterator(fs::path(RAWPAGE_CORPUS_DIR) / directory)) {
            if (item.path().extension() != ".pdf")
                continue;
            const std::string label = std::string(directory) + "/" + item.path().filename().string();
            INFO("file: " << label);
            QTemporaryDir dir;
            REQUIRE(dir.isValid());
            auto session = openCopy(QString::fromStdString(item.path().string()), dir.filePath(QStringLiteral("doc.pdf")));
            if (!session) {
                ++notEditable;
                continue;
            }
            const int page = session->journal->document().pages().count() - 1;
            const int engineRotation = static_cast<int>(session->backend.pages(session->handle)->rotation(page));
            REQUIRE(session->journal->run(std::make_unique<core::RotatePageCommand>(page, 90)));
            const int modelRotation = static_cast<int>(session->journal->document().pages().rotation(page));

            const core::RasterImage unsaved = renderPage(session->backend, session->handle, page, deltaOf(modelRotation, engineRotation));
            const QString written = saveOrSaveAs(*session, dir.filePath(QStringLiteral("saved-as.pdf")));
            CHECK(QDir(dir.path()).entryList(QStringList{QStringLiteral(".*.rawpage-*.tmp")}, QDir::Files | QDir::Hidden).isEmpty());

            // The ENGINE opens the file on disk: same page count, and its rotation is the model's.
            DocumentBackendImpl reader;
            auto reopened = reader.open(written, nullptr);
            REQUIRE(reopened);
            REQUIRE(reader.pages(reopened.value())->count() == session->journal->document().pages().count());
            CHECK(static_cast<int>(reader.pages(reopened.value())->rotation(page)) == modelRotation);
            CHECK_FALSE(reader.repairReport(reopened.value())->requiresSaveAs());

            // The same page: the saved file with no delta, the original with the unsaved delta.
            const core::RasterImage saved = renderPage(reader, reopened.value(), page, core::PageRotation::None);
            REQUIRE(saved.width == unsaved.width);
            REQUIRE(saved.height == unsaved.height);
            REQUIRE(saved.pixels.size() == unsaved.pixels.size());
            const PixelDifference difference = compare(saved, unsaved);
            if (difference.bytes > 0)
                std::cout << "save engine: " << label << " differs in " << difference.bytes << " of "
                          << saved.pixels.size() << " bytes, by at most " << difference.largest << std::endl;
            // WITHIN ONE LEVEL IN 255, NOT IDENTICAL -- measured at T8 (guide issue I-09): a rotation passed as
            // FPDF_RenderPageBitmap's argument and the same rotation read from /Rotate reach the anti-aliaser
            // through different matrices, and curves and hairlines round a coverage value differently. The
            // control below shows a wrong rotation is nowhere near this tolerance.
            CHECK(difference.largest <= 1);
            ++files;
        }
    }
    std::cout << "save engine: " << files << " files saved and re-read by PDFium; " << notEditable
              << " not editable in the shipped backend" << std::endl;
    CHECK(files >= 45);
}

TEST_CASE("save engine control: a wrong rotation is far outside the pixel tolerance", "[render][save][control]")
{
    // THE CONTROL for the comparison above, on the trap PdfiumRasterizer.cpp warns about. Two wrong
    // answers: a page whose file already says /Rotate 90 drawn with that rotation AGAIN as a delta
    // (different dimensions), and the same page drawn at 180 degrees where 0 was right (the same
    // dimensions, so the pixel tolerance itself is what must refuse it).
    platform::win::WinFontResolver resolver;
    REQUIRE(pdf::render::FontResolverBridge::install(resolver));
    struct Uninstall { ~Uninstall() { pdf::render::FontResolverBridge::uninstall(); } } uninstall;

    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto session = openCopy(QString::fromStdString((fs::path(RAWPAGE_CORPUS_DIR) / "ordinary/rotated-pages.pdf").string()),
                            dir.filePath(QStringLiteral("doc.pdf")));
    REQUIRE(session);
    int page = -1;
    for (int i = 0; i < session->backend.pages(session->handle)->count(); ++i) {
        if (static_cast<int>(session->backend.pages(session->handle)->rotation(i)) == 90) {
            page = i;
            break;
        }
    }
    REQUIRE(page >= 0);

    const core::RasterImage correct = renderPage(session->backend, session->handle, page, core::PageRotation::None);
    const core::RasterImage twice = renderPage(session->backend, session->handle, page, core::PageRotation::Clockwise90);
    const bool dimensionsDiffer = correct.width != twice.width || correct.height != twice.height;
    CHECK(dimensionsDiffer);

    const core::RasterImage upsideDown = renderPage(session->backend, session->handle, page, core::PageRotation::Half);
    REQUIRE(upsideDown.width == correct.width);
    REQUIRE(upsideDown.pixels.size() == correct.pixels.size());
    CHECK(compare(correct, upsideDown).largest > 1);
}

// ==========================================================================================
// A-54 -- the re-read guards the REAL replace
// ==========================================================================================

TEST_CASE("save engine never reaches ReplaceFileW with a file its own reader rejects", "[render][save]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto session = openCopy(QString::fromStdString((fs::path(RAWPAGE_CORPUS_DIR) / "signed/approval.pdf").string()),
                            dir.filePath(QStringLiteral("approval.pdf")));
    REQUIRE(session);
    REQUIRE(session->journal->run(std::make_unique<core::RotatePageCommand>(0, 90)));
    const std::string hashBefore = QCryptographicHash::hash(readAll(session->path), QCryptographicHash::Sha256).toHex().toStdString();

    const qsizetype keep = session->openedBytes.size() + 16;
    pdf::ops::RevisionWriter::setComposeFaultForTesting([keep](QByteArray& bytes) { bytes.truncate(keep); });
    auto result = session->coordinator->save(*session->journal, session->path);
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});

    REQUIRE_FALSE(result);
    CHECK(std::string(result.error().messageKey.data()) == "error.save.verifyFailed");
    CHECK(session->durableFiles.replaces == 0);
    CHECK(session->durableFiles.syncs == 0);
    // As hex text: a failed CHECK on two QByteArrays crashes Catch2's reporter (measured at T8).
    CHECK(QCryptographicHash::hash(readAll(session->path), QCryptographicHash::Sha256).toHex().toStdString() == hashBefore);
    CHECK(QDir(dir.path()).entryList(QStringList{QStringLiteral(".*.rawpage-*.tmp")}, QDir::Files | QDir::Hidden).isEmpty());

    // THE CONTROL: without the fault the same save reaches the real replace once.
    auto saved = session->coordinator->save(*session->journal, session->path);
    REQUIRE(saved);
    CHECK(saved.value().kind == SaveOutcome::Kind::Saved);
    CHECK(session->durableFiles.replaces == 1);
}

// ==========================================================================================
// A-57 -- A4 PRODUCT, the writing half: OpenSSL judges these files in render.a4_product.openssl
// ==========================================================================================

TEST_CASE("A4 product saves a rotation into every signed corpus file", "[render][save][a4]")
{
    // The files go to RAWPAGE_A4_PRODUCT_DIR, in the BUILD tree, for tests/render/RunA4Product.cmake to
    // hand to OpenSSL -- controls first -- and for manual case M-03's product walk. That the replace keeps
    // a planted Zone.Identifier stream is A-47's, over the same WinDurableFiles.
    const QString outDir = QStringLiteral(RAWPAGE_A4_PRODUCT_DIR);
    REQUIRE(QDir().mkpath(outDir));
    std::size_t written = 0;
    for (const char* name : {"approval.pdf", "approval-xrefstream.pdf", "certified-nochanges.pdf", "approval-then-update.pdf"}) {
        INFO("file: " << name);
        const QString target = QDir(outDir).filePath(QString::fromLatin1(name));
        if (QFile::exists(target)) {
            QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            REQUIRE(QFile::remove(target));
        }
        auto session = openCopy(QString::fromStdString((fs::path(RAWPAGE_CORPUS_DIR) / "signed" / name).string()), target);
        REQUIRE(session);
        REQUIRE(session->journal->run(std::make_unique<core::RotatePageCommand>(0, 90)));
        auto result = session->coordinator->save(*session->journal, target);
        INFO("save error: " << (result ? std::string() : std::string(result.error().messageKey.data())));
        REQUIRE(result);
        REQUIRE(result.value().kind == SaveOutcome::Kind::Saved);

        const QByteArray onDisk = readAll(target);   // FROM THE PATH
        CHECK(onDisk.startsWith(session->openedBytes));
        CHECK(onDisk.size() > session->openedBytes.size());
        ++written;
    }
    CHECK(written == 4);
}

// ==========================================================================================
// Stage 6 T3 -- A-33: A4 through the product with the PERMITTED edit, and the engine reading what we wrote
// ==========================================================================================

namespace {

core::Annotation testHighlight(int page)
{
    core::Annotation a = core::newAnnotation(core::AnnotationSubtype::Highlight, page, QStringLiteral("A4 Probe"),
                                             1791504000);
    a.quadPoints = {72, 160, 272, 160, 72, 146, 272, 146};
    a.rect = *core::boundsOfPoints(a.quadPoints);
    a.color = {1, 1, 0};
    a.opacity = 0.5;
    a.contents = QStringLiteral("A comment added after the signature");
    return a;
}

// The saved file, opened FRESH by the shipped backend: the engine and the first-party reader must read
// every page's /Annots alike (D6-3) -- on a file whose newest annotations OUR writer wrote -- and the
// added one must be in the store by its /NM.
void requireEngineReadsItAlike(const QString& path, const core::Annotation& added)
{
    DocumentBackendImpl fresh;
    auto handle = fresh.open(path, nullptr);
    REQUIRE(handle);
    const core::Permissions* permissions = fresh.permissions(handle.value());
    REQUIRE(permissions != nullptr);
    CHECK(permissions->annotationDisagreements().empty());
    CHECK(permissions->mayAnnotate(added.page));
    const core::AnnotationStore* store = fresh.annotations(handle.value());
    REQUIRE(store != nullptr);
    const std::optional<std::size_t> found = [&]() -> std::optional<std::size_t> {
        for (std::size_t i = 0; i < store->size(); ++i)
            if (store->at(i).nm == added.nm) return i;
        return std::nullopt;
    }();
    REQUIRE(found);
    CHECK(store->at(*found).page == added.page);
    CHECK(store->at(*found).subtype == core::AnnotationSubtype::Highlight);
    CHECK(fresh.close(handle.value()));
}

} // namespace

TEST_CASE("A4 product saves an annotation into every signed corpus file", "[render][save][a4][annotops]")
{
    // RAWPAGE_A4_ANNOT_DIR, for tests/render/RunA4Product.cmake to hand to OpenSSL (A-33) -- controls
    // first -- and for manual case M-03's Acrobat walk.
    const QString outDir = QStringLiteral(RAWPAGE_A4_ANNOT_DIR);
    REQUIRE(QDir().mkpath(outDir));
    std::size_t written = 0;
    for (const char* name : {"approval.pdf", "approval-xrefstream.pdf", "certified-nochanges.pdf", "approval-then-update.pdf"}) {
        INFO("file: " << name);
        const QString target = QDir(outDir).filePath(QString::fromLatin1(name));
        if (QFile::exists(target)) {
            QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            REQUIRE(QFile::remove(target));
        }
        auto session = openCopy(QString::fromStdString((fs::path(RAWPAGE_CORPUS_DIR) / "signed" / name).string()), target);
        REQUIRE(session);
        const core::Annotation added = testHighlight(0);
        auto run = session->journal->run(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{added}));
        INFO("run error: " << (run ? std::string() : std::string(run.error().messageKey.data())));
        REQUIRE(run);
        auto result = session->coordinator->save(*session->journal, target);
        INFO("save error: " << (result ? std::string() : std::string(result.error().messageKey.data())));
        REQUIRE(result);
        REQUIRE(result.value().kind == SaveOutcome::Kind::Saved);

        const QByteArray onDisk = readAll(target);   // FROM THE PATH
        CHECK(onDisk.startsWith(session->openedBytes));
        CHECK(onDisk.size() > session->openedBytes.size());
        requireEngineReadsItAlike(target, added);
        ++written;
    }
    CHECK(written == 4);
}

TEST_CASE("the engine reads a highlight our writer saved as the first-party reader does", "[render][save][annotops]")
{
    // D6-3 over OUR output: a page with no annotations, one with nine, a page whose /Annots sits in an
    // object stream, and a document with a direct annotation dictionary.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    std::size_t files = 0;
    for (const char* name : {"ordinary/text-a4.pdf", "ordinary/annots-mixed.pdf", "ordinary/objstm-xrefstream.pdf",
                             "ordinary/annots-direct.pdf", "ordinary/annots-every-type.pdf"}) {
        INFO("file: " << name);
        const QString copy = dir.filePath(QString::fromStdString(fs::path(name).filename().string()));
        auto session = openCopy(QString::fromStdString((fs::path(RAWPAGE_CORPUS_DIR) / name).string()), copy);
        REQUIRE(session);
        const core::Annotation added = testHighlight(0);
        REQUIRE(session->journal->run(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{added})));
        const QString saved = saveOrSaveAs(*session, dir.filePath(QStringLiteral("saved-as-") + QFileInfo(copy).fileName()));
        requireEngineReadsItAlike(saved, added);
        ++files;
    }
    CHECK(files == 5);
}
