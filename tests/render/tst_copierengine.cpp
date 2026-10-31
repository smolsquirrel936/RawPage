// tst_copierengine -- Stage 7 T5 (`Stage7-Assembly-Plan.md` §6, D7-4): guide case A-38 (`Stage7-Assembly-Testing-Guide.md`
// §5.7, the bar of §3.4) and the engine half of A-40 and A-42. A COPIED PAGE DRAWS AS IT DREW.
//
// Every page of every editable corpus file, copied by `IDocumentBackend::copyPages` -- `pdf/ops/ObjectGraphCopier` --
// and inserted by `InsertPagesCommand` into a one-page blank target through the SHIPPED backend and its journal (shown
// as `DocumentWindow` shows it), is SAVED, reopened in a fresh engine document, and rasterized with annotations at 72
// and 144 dpi against the same page of its source opened alone: 0 differing pixels -- the same engine drawing the same
// content must agree exactly. The oracle is the source FILE drawn by PDFium, never anything the copier computed.
//
// WHAT IT DOES NOT COVER (convention 19), each named in the printed result:
//   * A PAGE CARRYING A WIDGET -- widgets stay with their fields until T10 (the user's choice, 2026-10-31), so such a
//     page draws without its form fields by design; it is skipped, and the count printed.
//   * A FILE THE COPY REFUSES BY KEY (a damaged page tree, D7-21) or the first-party writer does not open -- named.
//   * At 144 dpi, a file of more than 200 pages is drawn at every ninth page, the rest at 72 dpi only -- `pages-900.pdf`
//     and `text-900.pdf`, whose 1,800 pages at both scales would be minutes of a suite for pages generated alike.

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/CopiedPages.h"
#include "core/doc/Document.h"
#include "core/doc/SaveCoordinator.h"
#include "core/render/RenderRequest.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/ObjectGraphCopier.h"
#include "pdf/ops/PageOps.h"
#include "platform/win/WinDurableFiles.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using rawpage::test_oracle::corpusPath;

namespace {

std::string digestOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int page, double scale)
{
    const core::Page* entry = backend.pages(handle)->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfSize size = entry->displaySize();
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = page;
    request.deviceScale = scale;
    request.revision = core::Revision::initial();
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

// The target: one blank page, the blank payload being a file of its own -- "an empty target" with the one page a
// document must keep (D7-21).
QString writeTarget(const QTemporaryDir& scratch)
{
    auto blank = pdf::ops::ObjectGraphCopier::blank(612.0, 792.0, 1);
    REQUIRE(blank);
    const QString path = scratch.filePath(QStringLiteral("target.pdf"));
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(blank.value().pdf.data(), static_cast<qint64>(blank.value().pdf.size()));
    return path;
}

struct Target {
    QTemporaryDir scratch;
    QString path;
    pdf::DocumentBackendImpl backend;
    platform::win::WinDurableFiles durableFiles;
    core::DocumentHandle handle;
    std::unique_ptr<core::CommandJournal> journal;
    std::unique_ptr<core::SaveCoordinator> coordinator;
};

std::unique_ptr<Target> openTarget()
{
    auto target = std::make_unique<Target>();
    REQUIRE(target->scratch.isValid());
    target->path = writeTarget(target->scratch);
    auto handle = target->backend.open(target->path, nullptr);
    REQUIRE(handle);
    target->handle = handle.value();
    core::Document document;
    REQUIRE(document.hydrate(target->backend, target->handle));
    target->journal = std::make_unique<core::CommandJournal>(std::move(document), target->backend.mutationSink(target->handle));
    Target* self = target.get();
    target->journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); },
                                    [self] { return self->backend.settleEdits(self->handle); });
    target->coordinator = std::make_unique<core::SaveCoordinator>(target->backend, &target->durableFiles);
    return target;
}

std::vector<std::string> candidates()
{
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") continue;
        const std::string relative = std::filesystem::relative(entry.path(), RAWPAGE_CORPUS_DIR).generic_string();
        if (relative.rfind("encrypted/", 0) == 0) continue;   // refused by key: A-43's
        files.push_back(relative);
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

TEST_CASE("render.copier every page copied and inserted draws exactly as its source draws", "[render][copier]")
{
    int files = 0;
    int pagesCompared = 0;
    int renders = 0;
    int widgetPages = 0;
    std::vector<std::string> refused;
    std::vector<std::string> failures;
    for (const std::string& relative : candidates()) {
        pdf::DocumentBackendImpl sourceBackend;
        auto source = sourceBackend.open(corpusPath(relative), nullptr);
        if (!source) {
            refused.push_back(relative + " (does not open)");
            continue;
        }
        const int count = sourceBackend.pages(source.value())->count();
        std::vector<int> all;
        for (int i = 0; i < count; ++i) all.push_back(i);

        auto target = openTarget();
        auto copied = target->backend.copyPages(corpusPath(relative), all);
        if (!copied) {
            refused.push_back(relative + " (" + std::string(copied.error().messageKey.data()) + ")");
            continue;
        }
        std::set<int> withWidgets;
        for (const core::PageReference& r : copied.value().report.references) {
            if (r.kind == core::PageReference::Kind::WidgetNotCopied) withWidgets.insert(r.page);
        }
        core::Status ran = target->journal->run(std::make_unique<core::InsertPagesCommand>(1, copied.value()));
        if (ran) ran = target->journal->settle();   // a refusal by the verify, after the run returned (D7-27)
        if (!ran) {
            failures.push_back(relative + ": the insert was refused (" + std::string(ran.error().messageKey.data()) + " "
                               + ran.error().context.toStdString() + ")");
            continue;
        }
        auto saved = target->coordinator->save(*target->journal, target->path);
        if (!saved) {
            failures.push_back(relative + ": the save was refused (" + std::string(saved.error().messageKey.data()) + " "
                               + saved.error().context.toStdString() + ")");
            continue;
        }
        pdf::DocumentBackendImpl freshBackend;
        auto fresh = freshBackend.open(target->path, nullptr);
        REQUIRE(fresh);
        if (freshBackend.pages(fresh.value())->count() != count + 1) {
            failures.push_back(relative + ": the saved file has " + std::to_string(freshBackend.pages(fresh.value())->count()) + " pages");
            continue;
        }
        ++files;
        widgetPages += static_cast<int>(withWidgets.size());
        for (int i = 0; i < count; ++i) {
            if (withWidgets.count(i) != 0) continue;
            ++pagesCompared;
            for (const double scale : {1.0, 2.0}) {
                if (scale == 2.0 && count > 200 && i % 9 != 0) continue;
                ++renders;
                if (digestOf(sourceBackend, source.value(), i, scale) != digestOf(freshBackend, fresh.value(), i + 1, scale))
                    failures.push_back(relative + " page " + std::to_string(i + 1) + " at " + std::to_string(static_cast<int>(72 * scale)) + " dpi");
            }
        }
    }
    std::cout << "render.copier: " << files << " files, " << pagesCompared << " pages compared in " << renders
              << " renders, " << widgetPages << " pages with a widget skipped, " << failures.size() << " differ; refused:";
    for (const std::string& r : refused) std::cout << " " << r;
    std::cout << "\n";
    for (const std::string& f : failures) UNSCOPED_INFO(f);
    CHECK(failures.empty());
    CHECK(files > 100);
}

// A-42's engine half: a link copied with the page it goes to lands, in the saved file, on the page that DRAWS as that
// page drew in its source -- identified by its pixels, not by a number the copier computed.
TEST_CASE("render.copier a copied link lands on the copy of its page", "[render][copier][copier.annots]")
{
    auto target = openTarget();
    auto copied = target->backend.copyPages(corpusPath("assembly/annots-on-pages.pdf"), {0, 1});
    REQUIRE(copied);
    REQUIRE(target->journal->run(std::make_unique<core::InsertPagesCommand>(1, copied.value())));
    REQUIRE(target->journal->settle());
    REQUIRE(target->coordinator->save(*target->journal, target->path));

    // The saved file, first-party: the link on the first inserted page, and the page object it names.
    QFile file(target->path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    auto opened = pdf::cos::openWithRepair(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    REQUIRE(opened);
    auto walk = pdf::ops::PageOps::walk(opened.value().table);
    REQUIRE(walk);
    auto page = opened.value().table.fetch(walk.value().pages[1].number);
    REQUIRE(page);
    pdf::cos::CosObject annots = page.value().object.get("Annots");
    if (annots.isReference()) annots = opened.value().table.fetch(annots.reference().number).value().object;
    int landing = -1;
    for (const pdf::cos::CosObject& item : annots.items()) {
        auto a = opened.value().table.fetch(item.reference().number);
        REQUIRE(a);
        if (a.value().object.get("Subtype").text() != "Link") continue;
        pdf::cos::CosObject dest = a.value().object.has("Dest") ? a.value().object.get("Dest") : a.value().object.get("A").get("D");
        if (dest.isReference()) dest = opened.value().table.fetch(dest.reference().number).value().object;
        REQUIRE(dest.isArray());
        for (std::size_t i = 0; i < walk.value().pages.size(); ++i) {
            if (walk.value().pages[i].number == dest.items().front().reference().number) landing = static_cast<int>(i);
        }
    }
    REQUIRE(landing == 2);
    // ... and that page draws as the source's page 1, at the scale the case reads.
    pdf::DocumentBackendImpl sourceBackend;
    auto source = sourceBackend.open(corpusPath("assembly/annots-on-pages.pdf"), nullptr);
    REQUIRE(source);
    pdf::DocumentBackendImpl freshBackend;
    auto fresh = freshBackend.open(target->path, nullptr);
    REQUIRE(fresh);
    // A widget page: the source drawn without its widget is not to hand, so the page is compared on its CONTENT --
    // the identity text PDFium extracts -- rather than its pixels.
    core::text::PageTextModel want;
    core::text::PageTextModel got;
    REQUIRE(sourceBackend.textExtractor(source.value())->extractPage(source.value(), 1, want));
    REQUIRE(freshBackend.textExtractor(fresh.value())->extractPage(fresh.value(), landing, got));
    CHECK(got.text() == want.text());
    CHECK_FALSE(want.text().isEmpty());
}
