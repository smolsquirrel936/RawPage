// tst_fullwriterengine -- Stage 7 T6 (`Stage7-Assembly-Plan.md` §6, D7-25, D7-29): the ENGINE half of Save As that
// rewrites. Guide case A-122's raster bar and A-124's (`Stage7-Assembly-Testing-Guide.md` §5.8, §3.9), and A-126's
// encrypted copy, which only the shipped backend opens.
//
// THE ORACLE FOR "THE DOCUMENT BEFORE THE SAVE" IS WHAT SAVE WOULD WRITE: the same state written by the APPEND form
// -- the opened bytes and the update, which D6-2 holds to be what the screen shows -- reopened in a fresh engine. The
// rewrite, reopened in another, must draw every page of it with 0 differing pixels at 72 and 144 dpi, annotations on:
// the same engine drawing the same objects must agree exactly. Nothing the rewrite computed is the oracle.
//
// WHAT IT DOES NOT COVER (convention 19), each named in the printed result:
//   * At 144 dpi, a file of more than 200 pages is drawn at every ninth page, the rest at 72 dpi only -- `pages-900.pdf`,
//     `text-900.pdf` and `annots-900.pdf`, as T5's A-38 bounds them (`tst_copierengine.cpp`).
//   * A signed file, which nobody is asked about here and is therefore appended, not rewritten (convention 16); its
//     OpenSSL verdict is `render.a4_product.saveas.openssl`'s (A-126).
//   * Undo after a Save As is drawn on ONE fixture with references, `assembly/refs-every-kind.pdf`; the headless
//     identity half over its objects is `unit.fullwriter.identity` (A-124).

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Annotation.h"
#include "core/doc/AnnotationGeometry.h"
#include "core/doc/Document.h"
#include "core/doc/DocumentId.h"
#include "core/doc/SaveCoordinator.h"
#include "core/render/RenderRequest.h"
#include "pdf/DocumentBackendImpl.h"
#include "platform/win/WinDurableFiles.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace rawpage;
using rawpage::test_oracle::corpusPath;

namespace {

std::string digestOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int page, double scale,
                     core::Revision revision = core::Revision::initial())
{
    const core::Page* entry = backend.pages(handle)->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfSize size = entry->displaySize();
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = page;
    request.deviceScale = scale;
    request.revision = revision;
    request.flags = core::render::RenderFlags::Annotations;
    request.tile = core::render::TileRect{0, 0, std::max(1, static_cast<int>(std::ceil(size.width * scale))),
                                          std::max(1, static_cast<int>(std::ceil(size.height * scale)))};
    core::IPageRasterizer* rasterizer = backend.rasterizer(handle);
    REQUIRE(rasterizer != nullptr);
    auto image = rasterizer->raster(request);
    REQUIRE(image);
    QByteArray bytes;
    bytes.append(QByteArray::number(image.value().width)).append('x').append(QByteArray::number(image.value().height)).append(':');
    for (int row = 0; row < image.value().height; ++row)
        bytes.append(image.value().pixels.constData() + static_cast<qsizetype>(row) * image.value().bytesPerLine, image.value().width * 4);
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size())) + " " + error.context.toStdString();
}

// One document, opened by the SHIPPED backend from a copy, shown as `DocumentWindow` shows it.
struct Live {
    QTemporaryDir scratch;
    QString path;
    pdf::DocumentBackendImpl backend;
    platform::win::WinDurableFiles durableFiles;
    core::DocumentHandle handle;
    std::unique_ptr<core::CommandJournal> journal;
    std::unique_ptr<core::SaveCoordinator> coordinator;
};

std::unique_ptr<Live> openLive(const std::string& relative)
{
    auto live = std::make_unique<Live>();
    REQUIRE(live->scratch.isValid());
    live->path = live->scratch.filePath(QStringLiteral("document.pdf"));
    REQUIRE(QFile::copy(corpusPath(relative), live->path));
    QFile::setPermissions(live->path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    auto handle = live->backend.open(live->path, nullptr);
    if (!handle || live->backend.mutationSink(handle.value()) == nullptr) return nullptr;
    live->handle = handle.value();
    core::Document document;
    REQUIRE(document.hydrate(live->backend, live->handle));
    live->journal = std::make_unique<core::CommandJournal>(std::move(document), live->backend.mutationSink(live->handle));
    Live* self = live.get();
    live->journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); },
                                  [self] { return self->backend.settleEdits(self->handle); });
    live->coordinator = std::make_unique<core::SaveCoordinator>(live->backend, &live->durableFiles);
    return live;
}

core::Annotation highlightOn(core::IDocumentBackend& backend, core::DocumentHandle handle, int page)
{
    const core::Page* entry = backend.pages(handle)->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfRect box = entry->cropBox();
    const double left = box.left() + box.size.width * 0.1;
    const double right = left + box.size.width * 0.3;
    const double top = box.bottom() + box.size.height * 0.6;
    core::Annotation a = core::newAnnotation(core::AnnotationSubtype::Highlight, page, QStringLiteral("A-122"), 1791504000);
    a.quadPoints = {left, top, right, top, left, top - 14.0, right, top - 14.0};
    a.rect = *core::boundsOfPoints(a.quadPoints);
    a.color = {1.0, 0.8, 0.0};
    a.contents = QStringLiteral("rewritten");
    return a;
}

std::vector<std::string> candidates()
{
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") continue;
        const std::string relative = std::filesystem::relative(entry.path(), RAWPAGE_CORPUS_DIR).generic_string();
        if (relative.rfind("encrypted/", 0) == 0) continue;   // copied, not rewritten: the case below
        files.push_back(relative);
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Every page of `handle`, at both scales, as the bound above says.
std::vector<std::string> drawEvery(core::IDocumentBackend& backend, core::DocumentHandle handle, int& renders,
                                   core::Revision revision = core::Revision::initial())
{
    std::vector<std::string> out;
    const int count = backend.pages(handle)->count();
    for (int page = 0; page < count; ++page) {
        for (const double scale : {1.0, 2.0}) {
            if (scale == 2.0 && count > 200 && page % 9 != 0) continue;
            ++renders;
            out.push_back(digestOf(backend, handle, page, scale, revision));
        }
    }
    return out;
}

// Which of two drawings of every page differ, by render index -- empty when they are the same.
std::string differing(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    if (a.size() != b.size()) return "counts " + std::to_string(a.size()) + " and " + std::to_string(b.size());
    std::string out;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) out += (out.empty() ? "" : ",") + std::to_string(i);
    return out;
}

} // namespace

// ==========================================================================================
// A-122 -- every page of every rewritten file, against what Save would have written
// ==========================================================================================

TEST_CASE("render.fullwriter every page of every Save As that rewrote draws as the save it replaces", "[render][fullwriter][A-122]")
{
    int files = 0;
    int rewritten = 0;
    int renders = 0;
    std::vector<std::string> notEditable;
    std::vector<std::string> failures;
    for (const std::string& relative : candidates()) {
        auto live = openLive(relative);
        if (!live) {
            notEditable.push_back(relative);
            continue;
        }
        const int pages = live->journal->document().pages().count();
        core::Status ran;
        if (pages >= 2) ran = live->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, pages - 1));
        if (ran && live->backend.permissions(live->handle)->mayAnnotate(0))
            ran = live->journal->run(std::make_unique<core::AddAnnotationCommand>(
                std::vector<core::Annotation>{highlightOn(live->backend, live->handle, 0)}));
        if (ran) ran = live->journal->settle();
        if (!ran) {
            failures.push_back(relative + ": an edit was refused (" + keyOf(ran.error()) + ")");
            continue;
        }
        // What Save would write -- the append -- and the Save As that rewrites.
        const QString appended = live->scratch.filePath(QStringLiteral("appended.pdf"));
        if (core::Status written = live->backend.writeRevision(live->handle, appended, core::SaveForm::Append); !written) {
            failures.push_back(relative + ": the append was refused (" + keyOf(written.error()) + ")");
            continue;
        }
        const QString target = live->scratch.filePath(QStringLiteral("rewritten.pdf"));
        auto saved = live->coordinator->saveAs(*live->journal, target);
        if (!saved || saved.value().kind != core::SaveOutcome::Kind::Saved) {
            failures.push_back(relative + ": Save As was refused" + (saved ? std::string() : " (" + keyOf(saved.error()) + ")"));
            continue;
        }
        ++files;
        if (saved.value().form == core::SaveForm::Rewrite) ++rewritten;

        pdf::DocumentBackendImpl appendedBackend;
        pdf::DocumentBackendImpl rewrittenBackend;
        auto before = appendedBackend.open(appended, nullptr);
        auto after = rewrittenBackend.open(target, nullptr);
        REQUIRE(before);
        REQUIRE(after);
        if (rewrittenBackend.repairReport(after.value())->requiresSaveAs()) failures.push_back(relative + ": the rewrite needs a repair");
        if (rewrittenBackend.pages(after.value())->count() != pages) {
            failures.push_back(relative + ": page count");
            continue;
        }
        const std::vector<std::string> a = drawEvery(appendedBackend, before.value(), renders);
        const std::vector<std::string> b = drawEvery(rewrittenBackend, after.value(), renders);
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) {
                failures.push_back(relative + ": render " + std::to_string(i) + " differs");
                break;
            }
        }
    }
    std::cout << "render.fullwriter: " << files << " files Saved As, " << rewritten << " rewritten, " << renders
              << " renders, " << failures.size() << " differ or refused; not editable:";
    for (const std::string& n : notEditable) std::cout << " " << n;
    std::cout << "\n";
    for (const std::string& f : failures) UNSCOPED_INFO(f);
    CHECK(failures.empty());
    CHECK(files > 100);
}

// ==========================================================================================
// A-124 -- each undo and redo after a Save As draws as the same step did before it
// ==========================================================================================

TEST_CASE("render.fullwriter.identity every undo and redo after a rewrite draws as its step did before it",
          "[render][fullwriter][fullwriter.identity][A-124]")
{
    auto live = openLive("assembly/refs-every-kind.pdf");
    REQUIRE(live);
    core::CommandJournal& journal = *live->journal;
    const int pages = journal.document().pages().count();
    REQUIRE(pages >= 4);
    int renders = 0;
    std::vector<std::vector<std::string>> drawn{drawEvery(live->backend, live->handle, renders, journal.revision())};
    auto step = [&](std::unique_ptr<core::Command> command) {
        REQUIRE(journal.run(std::move(command)));
        REQUIRE(journal.settle());
        drawn.push_back(drawEvery(live->backend, live->handle, renders, journal.revision()));
    };
    step(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{highlightOn(live->backend, live->handle, 1)}));
    step(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, pages - 1));
    step(std::make_unique<core::DeletePagesCommand>(std::vector<int>{0, 1}));
    const std::size_t steps = drawn.size() - 1;

    const QString target = live->scratch.filePath(QStringLiteral("rewritten.pdf"));
    auto saved = live->coordinator->saveAs(journal, target);
    REQUIRE(saved);
    REQUIRE(saved.value().form == core::SaveForm::Rewrite);
    CHECK(differing(drawEvery(live->backend, live->handle, renders, journal.revision()), drawn[steps]) == "");

    for (std::size_t at = steps; at > 0; --at) {
        INFO("undo to step " << at - 1);
        REQUIRE(journal.undo());
        REQUIRE(journal.settle());
        CHECK(differing(drawEvery(live->backend, live->handle, renders, journal.revision()), drawn[at - 1]) == "");
    }
    for (std::size_t at = 1; at <= steps; ++at) {
        INFO("redo to step " << at);
        REQUIRE(journal.redo());
        REQUIRE(journal.settle());
        CHECK(differing(drawEvery(live->backend, live->handle, renders, journal.revision()), drawn[at]) == "");
    }
    std::cout << "render.fullwriter.identity: " << steps << " steps, undone and redone after a rewrite, " << renders << " renders\n";
}

// ==========================================================================================
// A-126's encrypted half -- copied, not rewritten
// ==========================================================================================

TEST_CASE("render.fullwriter.encrypted an encrypted document is Saved As as a byte copy", "[render][fullwriter][fullwriter.encrypted][A-126]")
{
    int copied = 0;
    for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(RAWPAGE_CORPUS_DIR) / "encrypted")) {
        if (entry.path().extension() != ".pdf") continue;
        const std::string relative = "encrypted/" + entry.path().filename().string();
        INFO(relative);
        QTemporaryDir scratch;
        REQUIRE(scratch.isValid());
        const QString path = scratch.filePath(QStringLiteral("document.pdf"));
        REQUIRE(QFile::copy(corpusPath(relative), path));
        pdf::DocumentBackendImpl backend;
        auto handle = backend.open(path, nullptr);
        if (!handle) continue;   // a password this case does not supply
        REQUIRE(backend.mutationSink(handle.value()) == nullptr);
        core::Document document;
        REQUIRE(document.hydrate(backend, handle.value()));
        core::CommandJournal journal(std::move(document), nullptr);
        platform::win::WinDurableFiles files;
        core::SaveCoordinator coordinator(backend, &files);
        const QString target = scratch.filePath(QStringLiteral("copy.pdf"));
        auto saved = coordinator.saveAs(journal, target);
        REQUIRE(saved);
        CHECK(saved.value().form == core::SaveForm::Append);
        QFile original(path);
        QFile copy(target);
        REQUIRE(original.open(QIODevice::ReadOnly));
        REQUIRE(copy.open(QIODevice::ReadOnly));
        const bool identical = original.readAll() == copy.readAll();   // a bool: a failed CHECK on QByteArrays crashes the reporter
        CHECK(identical);
        CHECK(QDir(scratch.path()).entryList(QStringList{QStringLiteral(".*.tmp")}, QDir::Files | QDir::Hidden).isEmpty());
        // ... and the document now lives at the copy.
        CHECK(backend.documentId(handle.value())->path == core::DocumentId::normalizePath(target));
        ++copied;
    }
    std::cout << "render.fullwriter.encrypted: " << copied << " encrypted files copied\n";
    CHECK(copied >= 1);
}

// ==========================================================================================
// Stage 7 guide I-10 -- a page an undo puts back after a save draws as it did before the delete
// ==========================================================================================
//
// Found by A-124's case above, and older than T6: after ANY save, a deleted page put back by an undo is in no disk
// revision, and the backend's page tree drew it as a page it never opened. Here after an ordinary APPEND save, which is
// how it shipped since Stage 7 T2; the case above holds the same after a rewrite.
TEST_CASE("render.fullwriter I-10 a page an undo puts back after an append save draws as it did", "[render][fullwriter][I-10]")
{
    auto live = openLive("assembly/refs-every-kind.pdf");
    REQUIRE(live);
    core::CommandJournal& journal = *live->journal;
    int renders = 0;
    const std::vector<std::string> before = drawEvery(live->backend, live->handle, renders, journal.revision());
    REQUIRE(journal.run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{1, 2})));
    REQUIRE(journal.settle());
    auto saved = live->coordinator->save(journal, live->path);
    REQUIRE(saved);
    REQUIRE(saved.value().form == core::SaveForm::Append);
    REQUIRE(journal.undo());
    REQUIRE(journal.settle());
    CHECK(differing(drawEvery(live->backend, live->handle, renders, journal.revision()), before) == "");
    // ... and the engine's tree carries each page as it read it at open: its boxes and its rotation.
    const core::PageTree* tree = live->backend.pages(live->handle);
    REQUIRE(tree != nullptr);
    pdf::DocumentBackendImpl fresh;
    auto original = fresh.open(corpusPath("assembly/refs-every-kind.pdf"), nullptr);
    REQUIRE(original);
    REQUIRE(tree->count() == fresh.pages(original.value())->count());
    for (int page = 0; page < tree->count(); ++page) {
        INFO("page " << page + 1);
        CHECK(tree->at(page)->displaySize().width == fresh.pages(original.value())->at(page)->displaySize().width);
        CHECK(tree->at(page)->displaySize().height == fresh.pages(original.value())->at(page)->displaySize().height);
        CHECK(tree->at(page)->rotation() == fresh.pages(original.value())->at(page)->rotation());
    }
}
