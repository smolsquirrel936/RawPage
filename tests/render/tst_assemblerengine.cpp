// tst_assemblerengine -- Stage 7 T7 (`Stage7-Assembly-Plan.md` §6, D7-5, D7-30): new documents through the SHIPPED
// backend and its engine -- guide cases A-47, A-48, A-49 and A-50 (`Stage7-Assembly-Testing-Guide.md` §5.9).
//
// THE ORACLES ARE THE ENGINE AND THE LABELS (D7-18), never the assembler: a new document is opened by PDFium as any
// file is -- from its bytes, and from its path once written -- and must need NO repair; each of its pages is drawn at
// 72 dpi against the page it came from in its source, 0 differing pixels (the guide's §3.4 bar); and a split's files
// are graded by the IDENTITY PDFium extracts from each one's first and last page, against what
// `corpus/labels/assembly-*.json` says those pages are. A split runs through `core::SplitRunner` exactly as the
// product's does -- the blank pages read from the engine's content box on the render thread, a pattern matched by the
// search engine over the text index -- pumped here as `app/CompositionRoot` pumps it.
//
// WHAT IT DOES NOT COVER (convention 19), each named in the printed result:
//   * A PAGE CARRYING A WIDGET -- widgets stay with their fields until T10 (D7-4 as amended), so such a page draws
//     without its form fields by design; it is skipped, and the count printed.
//   * A FILE THE ASSEMBLER REFUSES BY KEY (a damaged page tree, D7-21; no page tree at all) -- named.
//   * Of a file of more than 200 pages, every ninth page is drawn -- `pages-900.pdf` and `text-900.pdf`.
//   * What ANOTHER reader makes of a new document: `qpdf --check` in the portable-asan job (the headless suite keeps
//     its files for it), and Acrobat and Firefox in the guide's manual walk.

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "app/BackendTextExtractor.h"
#include "core/backend.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/Assembly.h"
#include "core/doc/SplitRunner.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "core/text/DocumentIndex.h"
#include "core/text/SearchEngine.h"
#include "pdf/DocumentBackendImpl.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using rawpage::test_oracle::corpusPath;
using core::text::PageTextModel;

namespace {

std::string digestOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int page)
{
    const core::Page* entry = backend.pages(handle)->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfSize size = entry->displaySize();
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = page;
    request.deviceScale = 1.0;
    request.revision = core::Revision::initial();
    request.flags = core::render::RenderFlags::Annotations;
    request.tile = core::render::TileRect{0, 0, std::max(1, static_cast<int>(std::ceil(size.width))),
                                          std::max(1, static_cast<int>(std::ceil(size.height)))};
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

std::vector<std::string> candidates()
{
    std::vector<std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") continue;
        const std::string relative = std::filesystem::relative(entry.path(), RAWPAGE_CORPUS_DIR).generic_string();
        if (relative.rfind("encrypted/", 0) == 0) continue;
        files.push_back(relative);
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Every token on a page that reads as an identity (`corpus/make-assembly.py`'s pattern), in the order extracted.
std::vector<std::string> identitiesOn(core::IDocumentBackend& backend, core::DocumentHandle handle, int page)
{
    core::ITextExtractor* extractor = backend.textExtractor(handle);
    REQUIRE(extractor != nullptr);
    PageTextModel model;
    REQUIRE(extractor->extractPage(handle, page, model));
    static const QRegularExpression pattern(QStringLiteral("(?<![A-Za-z0-9-])[A-Z][A-Z0-9]*-[0-9]{4}(?![A-Za-z0-9-])"));
    std::vector<std::string> found;
    auto matches = pattern.globalMatch(model.text());
    while (matches.hasNext()) found.push_back(matches.next().captured(0).toStdString());
    return found;
}

// The identity a page SHOWS -- the first token, which the generator draws largest and first.
std::string identityOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int page)
{
    const std::vector<std::string> found = identitiesOn(backend, handle, page);
    return found.empty() ? std::string("(none)") : found.front();
}

// A written file's first and last identity, opened by the ENGINE from its path -- with no repair.
std::pair<std::string, std::string> endsOf(const QString& path)
{
    pdf::DocumentBackendImpl backend;
    auto handle = backend.open(path, nullptr);
    INFO(path.toStdString());
    REQUIRE(handle);
    CHECK_FALSE(backend.repairReport(handle.value())->requiresSaveAs());
    CHECK(backend.repairReport(handle.value())->isEmpty());
    const int count = backend.pages(handle.value())->count();
    REQUIRE(count > 0);
    auto ends = std::make_pair(identityOf(backend, handle.value(), 0), identityOf(backend, handle.value(), count - 1));
    (void)backend.close(handle.value());
    return ends;
}

// The product's pipeline around one open document: the scheduler with its text extractor, the index, the search
// engine and the split runner -- and the pump.
struct Splitting {
    explicit Splitting(std::string_view relative)
    {
        auto opened = backend.open(corpusPath(relative), nullptr);
        REQUIRE(opened);
        handle = opened.value();
        pageCount = backend.pages(handle)->count();
        entry = index.attach(backend.documentId(handle), handle, pageCount);
        REQUIRE(entry != 0);
    }
    ~Splitting()
    {
        runner.cancel();
        index.detach(handle);
        scheduler.cancelDocument(handle);
        scheduler.waitForIdle(10000);
        (void)backend.close(handle);
    }

    core::Result<core::SplitOutcome> split(core::SplitRequest request)
    {
        std::optional<core::Result<core::SplitOutcome>> outcome;
        request.namePattern = QStringLiteral("%1 part %2.pdf");
        request.stem = QStringLiteral("doc");
        const core::Status started = runner.start(handle, entry, std::move(request),
                                                  [&outcome](core::Result<core::SplitOutcome> done) { outcome = std::move(done); });
        if (!started) return started.error();
        QElapsedTimer timer;
        timer.start();
        while (!outcome && timer.elapsed() < 120000) {
            scheduler.waitForIdle(1);
            scheduler.deliverReady();
            search.waitForIdle(1);
            search.deliverReady();
        }
        REQUIRE(outcome.has_value());
        return std::move(*outcome);
    }

    pdf::DocumentBackendImpl backend;
    core::render::RenderScheduler scheduler{
        nullptr, [this] { return std::unique_ptr<core::ITextExtractor>(new app::BackendTextExtractor(backend)); },
        nullptr, core::render::RenderScheduler::Config{}};
    core::text::DocumentIndex index{scheduler};
    core::text::SearchEngine search{index};
    core::SplitRunner runner{backend, scheduler, search};
    core::DocumentHandle handle;
    int pageCount = 0;
    core::text::DocumentIndex::EntryId entry = 0;
};

std::vector<std::pair<std::string, std::string>> writtenEnds(const core::SplitOutcome& outcome)
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const core::WrittenDocument& file : outcome.written) out.push_back(endsOf(file.path));
    return out;
}

// What a label says: page identities in order, and each object's page.
struct Labelled {
    explicit Labelled(const char* name) : json(test_oracle::loadLabel(name))
    {
        for (const QJsonValue& value : json.value(QStringLiteral("pages")).toArray()) {
            const QJsonObject page = value.toObject();
            const QJsonValue drawn = page.contains(QStringLiteral("drawnIdentity")) ? page.value(QStringLiteral("drawnIdentity"))
                                                                                    : page.value(QStringLiteral("identity"));
            identity.push_back(drawn.isNull() ? std::string("(none)") : drawn.toString().toStdString());
            pageOfObject[page.value(QStringLiteral("object")).toInt()] = page.value(QStringLiteral("index")).toInt();
        }
    }
    // Each part's first and last identity, the parts starting at `starts` (sorted, the first page implied).
    std::vector<std::pair<std::string, std::string>> partsFrom(std::set<int> starts, const std::set<int>& leftOut = {}) const
    {
        starts.insert(0);
        std::vector<std::pair<std::string, std::string>> out;
        std::vector<int> current;
        const auto flush = [&] {
            if (!current.empty()) out.emplace_back(identity[current.front()], identity[current.back()]);
            current.clear();
        };
        for (int page = 0; page < static_cast<int>(identity.size()); ++page) {
            if (starts.count(page) != 0 || leftOut.count(page) != 0) flush();
            if (leftOut.count(page) == 0) current.push_back(page);
        }
        flush();
        return out;
    }
    QJsonObject json;
    std::vector<std::string> identity;
    std::map<int, int> pageOfObject;
};

QStringList everythingUnder(const QString& root)
{
    QStringList out;
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) out << QDir(root).relativeFilePath(it.next());
    out.sort();
    return out;
}

} // namespace

// ==========================================================================================
// A-47 -- every new document opens with no repair, and draws its pages as their sources draw
// ==========================================================================================

TEST_CASE("render.assembler every new document opens with no repair and draws as its source", "[render][assembler][A-47]")
{
    int files = 0;
    int pagesCompared = 0;
    int widgetPages = 0;
    std::vector<std::string> refused;
    std::vector<std::string> failures;
    for (const std::string& relative : candidates()) {
        pdf::DocumentBackendImpl backend;
        auto source = backend.open(corpusPath(relative), nullptr);
        if (!source) {
            refused.push_back(relative + " (does not open)");
            continue;
        }
        const int count = backend.pages(source.value())->count();
        auto snapshot = backend.assemblySource(source.value());
        if (!snapshot) {
            refused.push_back(relative + " (" + std::string(snapshot.error().messageKey.data()) + ")");
            continue;
        }
        std::vector<int> all;
        for (int i = 0; i < count; ++i) all.push_back(i);
        auto made = snapshot.value()->assemble(all);
        if (!made) {
            failures.push_back(relative + ": " + std::string(made.error().messageKey.data()) + " " + made.error().context.toStdString());
            continue;
        }
        std::set<int> withWidgets;
        for (const core::PageReference& r : made.value().report.references) {
            if (r.kind == core::PageReference::Kind::WidgetNotCopied) withWidgets.insert(r.page);
        }
        // Opened as the product opens an extract: from its bytes, as an untitled document.
        auto opened = backend.openBytes(made.value().bytes, QStringLiteral("Untitled"), nullptr, core::BytesOrigin::Assembly);
        if (!opened) {
            failures.push_back(relative + ": the engine refused the new document");
            continue;
        }
        ++files;
        if (!backend.repairReport(opened.value())->isEmpty()) failures.push_back(relative + ": the new document needed a repair");
        if (backend.permissions(opened.value())->refusalKey() != QLatin1String("error.edit.unsavedAssembly"))
            failures.push_back(relative + ": not refused as an unsaved assembly");
        if (backend.pages(opened.value())->count() != count) {
            failures.push_back(relative + ": " + std::to_string(backend.pages(opened.value())->count()) + " pages");
            continue;
        }
        widgetPages += static_cast<int>(withWidgets.size());
        for (int i = 0; i < count; ++i) {
            if (withWidgets.count(i) != 0 || (count > 200 && i % 9 != 0)) continue;
            ++pagesCompared;
            if (digestOf(backend, source.value(), i) != digestOf(backend, opened.value(), i))
                failures.push_back(relative + " page " + std::to_string(i + 1));
        }
        (void)backend.close(opened.value());
        (void)backend.close(source.value());
    }
    std::cout << "render.assembler: " << files << " files, " << pagesCompared << " pages compared at 72 dpi, " << widgetPages
              << " pages with a widget skipped, " << failures.size() << " differ; refused:";
    for (const std::string& r : refused) std::cout << " " << r;
    std::cout << "\n";
    for (const std::string& f : failures) UNSCOPED_INFO(f);
    CHECK(failures.empty());
    CHECK(files > 100);
}

// ==========================================================================================
// A-48 -- extract: the right pages, by what they show
// ==========================================================================================

TEST_CASE("render.assembler extract to one and per page: the right pages by their identities", "[render][assembler][extract][A-48]")
{
    pdf::DocumentBackendImpl backend;
    auto source = backend.open(corpusPath("assembly/odd-count-9.pdf"), nullptr);
    REQUIRE(source);
    Labelled label("assembly-odd-count-9");
    auto snapshot = backend.assemblySource(source.value());
    REQUIRE(snapshot);

    // To one: pages 2, 5 and 9, in order.
    auto made = snapshot.value()->assemble({1, 4, 8});
    REQUIRE(made);
    auto opened = backend.openBytes(made.value().bytes, QStringLiteral("Untitled"), nullptr, core::BytesOrigin::Assembly);
    REQUIRE(opened);
    REQUIRE(backend.pages(opened.value())->count() == 3);
    CHECK(identityOf(backend, opened.value(), 0) == label.identity[1]);
    CHECK(identityOf(backend, opened.value(), 1) == label.identity[4]);
    CHECK(identityOf(backend, opened.value(), 2) == label.identity[8]);

    // Per page: a file each, each opened from its path.
    QTemporaryDir root;
    auto written = snapshot.value()->write({{{0}, QStringLiteral("a.pdf")}, {{6}, QStringLiteral("b.pdf")}}, root.path());
    REQUIRE(written);
    REQUIRE(written.value().size() == 2);
    CHECK(endsOf(written.value()[0].path) == std::make_pair(label.identity[0], label.identity[0]));
    CHECK(endsOf(written.value()[1].path) == std::make_pair(label.identity[6], label.identity[6]));
}

TEST_CASE("render.assembler extract takes the shipped backend's document as it stands, unsaved edits included",
          "[render][assembler][extract][A-48]")
{
    QTemporaryDir scratch;
    const QString path = QDir(scratch.path()).filePath(QStringLiteral("odd-count-5.pdf"));
    REQUIRE(QFile::copy(corpusPath("assembly/odd-count-5.pdf"), path));
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    pdf::DocumentBackendImpl backend;
    auto handle = backend.open(path, nullptr);
    REQUIRE(handle);
    core::Document document;
    REQUIRE(document.hydrate(backend, handle.value()));
    core::CommandJournal journal(std::move(document), backend.mutationSink(handle.value()));
    const core::DocumentHandle h = handle.value();
    journal.setShowHandler([&backend, h](const core::Scope&) { return backend.showEdits(h, {}); },
                           [&backend, h] { return backend.settleEdits(h); });
    REQUIRE(journal.run(std::make_unique<core::RotatePageCommand>(2, 90)));
    REQUIRE(journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 4)));
    REQUIRE(journal.settle());
    Labelled label("assembly-odd-count-5");

    auto snapshot = backend.assemblySource(h);
    REQUIRE(snapshot);
    auto made = snapshot.value()->assemble({1, 4});   // now: the turned page, and the moved one
    REQUIRE(made);
    auto opened = backend.openBytes(made.value().bytes, QStringLiteral("Untitled"), nullptr, core::BytesOrigin::Assembly);
    REQUIRE(opened);
    CHECK(identityOf(backend, opened.value(), 0) == label.identity[2]);
    CHECK(backend.pages(opened.value())->at(0)->rotation() == core::PageRotation::Clockwise90);
    CHECK(identityOf(backend, opened.value(), 1) == label.identity[0]);
    // The file on disk is untouched: an extract writes nothing of its source.
    CHECK(QFileInfo(path).size() == QFileInfo(corpusPath("assembly/odd-count-5.pdf")).size());
    (void)backend.close(opened.value());
    (void)backend.close(h);
}

// ==========================================================================================
// A-49 -- the splits, graded against the labels by what each file's pages show
// ==========================================================================================

TEST_CASE("render.assembler split at blank pages, at a pattern and at bookmarks: split-blanks.pdf", "[render][assembler][split][A-49]")
{
    Splitting doc("assembly/split-blanks.pdf");
    Labelled label("assembly-split-blanks");
    std::set<int> blank;
    std::set<int> parts;
    for (const QJsonValue& value : label.json.value(QStringLiteral("pages")).toArray()) {
        if (value.toObject().value(QStringLiteral("contentBoxEmpty")).toBool()) blank.insert(value.toObject().value(QStringLiteral("index")).toInt());
    }
    for (const QJsonValue& value : label.json.value(QStringLiteral("partStarts")).toArray()) parts.insert(value.toInt());
    REQUIRE(blank.size() == 2);   // the label's claim: two pages with no object; the white page is not one of them

    QTemporaryDir root;
    {
        core::SplitRequest request;
        request.method = core::SplitMethod::BlankPages;
        request.folder = QDir(root.path()).filePath(QStringLiteral("blank"));
        auto outcome = doc.split(request);
        REQUIRE(outcome);
        CHECK(outcome.value().blankPagesLeftOut == 2);
        CHECK(writtenEnds(outcome.value()) == label.partsFrom({}, blank));
        // The white-ink page, blank to the eye, opens the third file: by the content box it is not blank (guide A-49).
        CHECK(writtenEnds(outcome.value()).back().first == label.identity[6]);
    }
    {
        core::SplitRequest request;
        request.method = core::SplitMethod::TextPattern;
        request.pattern.pattern = label.json.value(QStringLiteral("headingPattern")).toString();
        request.pattern.flags.useRegex = true;
        request.pattern.flags.caseSensitive = true;
        request.folder = QDir(root.path()).filePath(QStringLiteral("pattern"));
        auto outcome = doc.split(request);
        REQUIRE(outcome);
        CHECK(writtenEnds(outcome.value()) == label.partsFrom(parts));
    }
    {
        core::SplitRequest request;
        request.method = core::SplitMethod::TopLevelBookmarks;
        request.folder = QDir(root.path()).filePath(QStringLiteral("bookmarks"));
        auto outcome = doc.split(request);
        REQUIRE(outcome);
        std::set<int> starts;
        for (const QJsonValue& value : label.json.value(QStringLiteral("outline")).toArray())
            starts.insert(label.pageOfObject.at(value.toObject().value(QStringLiteral("targetPage")).toInt()));
        CHECK(writtenEnds(outcome.value()) == label.partsFrom(starts));
    }
}

TEST_CASE("render.assembler split at every top-level bookmark and at a chosen level: refs-every-kind.pdf", "[render][assembler][split][A-49]")
{
    Splitting doc("assembly/refs-every-kind.pdf");
    Labelled label("assembly-refs-every-kind");
    // The label's outline, read here: each item's page at its depth.
    std::map<int, std::set<int>> byDepth;
    std::function<void(const QJsonArray&, int)> walk = [&](const QJsonArray& items, int depth) {
        for (const QJsonValue& value : items) {
            const QJsonObject item = value.toObject();
            byDepth[depth].insert(label.pageOfObject.at(item.value(QStringLiteral("targetPage")).toInt()));
            walk(item.value(QStringLiteral("children")).toArray(), depth + 1);
        }
    };
    walk(label.json.value(QStringLiteral("outline")).toArray(), 1);
    REQUIRE(byDepth.size() == 2);

    QTemporaryDir root;
    core::SplitRequest top;
    top.method = core::SplitMethod::TopLevelBookmarks;
    top.folder = QDir(root.path()).filePath(QStringLiteral("top"));
    auto first = doc.split(top);
    REQUIRE(first);
    CHECK(writtenEnds(first.value()) == label.partsFrom(byDepth[1]));

    core::SplitRequest second;
    second.method = core::SplitMethod::BookmarkLevel;
    second.level = 2;
    second.folder = QDir(root.path()).filePath(QStringLiteral("level2"));
    auto deeper = doc.split(second);
    REQUIRE(deeper);
    std::set<int> both = byDepth[1];
    both.insert(byDepth[2].begin(), byDepth[2].end());
    CHECK(writtenEnds(deeper.value()) == label.partsFrom(both));
}

TEST_CASE("render.assembler split at a heading pattern: tagged-headings.pdf", "[render][assembler][split][A-49]")
{
    Splitting doc("assembly/tagged-headings.pdf");
    Labelled label("assembly-tagged-headings");
    std::set<int> starts;
    for (const QJsonValue& value : label.json.value(QStringLiteral("headings")).toArray()) {
        const QJsonObject heading = value.toObject();
        if (heading.value(QStringLiteral("level")).toInt() == 1) starts.insert(heading.value(QStringLiteral("pageIndex")).toInt());
    }
    REQUIRE(starts.size() >= 2);
    QTemporaryDir root;
    core::SplitRequest request;
    request.method = core::SplitMethod::TextPattern;
    request.pattern.pattern = QStringLiteral("^[0-9]+ [A-Z]");   // a level-1 heading: a number with no dot, then the title
    request.pattern.flags.useRegex = true;
    request.pattern.flags.caseSensitive = true;
    request.folder = root.path();
    auto outcome = doc.split(request);
    REQUIRE(outcome);
    CHECK(writtenEnds(outcome.value()) == label.partsFrom(starts));
}

TEST_CASE("render.assembler split every N pages and by size: pages-900.pdf", "[render][assembler][split][A-49]")
{
    Splitting doc("ordinary/pages-900.pdf");
    REQUIRE(doc.pageCount == 900);
    QTemporaryDir root;
    {
        core::SplitRequest request;
        request.method = core::SplitMethod::EveryN;
        request.pagesPerFile = 250;
        request.folder = QDir(root.path()).filePath(QStringLiteral("every"));
        auto outcome = doc.split(request);
        REQUIRE(outcome);
        REQUIRE(outcome.value().written.size() == 4);
        // Each file's first and last page draws as the source's page it was.
        const int firsts[] = {0, 250, 500, 750};
        for (std::size_t at = 0; at < 4; ++at) {
            pdf::DocumentBackendImpl engine;
            auto file = engine.open(outcome.value().written[at].path, nullptr);
            REQUIRE(file);
            CHECK(engine.repairReport(file.value())->isEmpty());
            const int pages = engine.pages(file.value())->count();
            CHECK(pages == (at < 3 ? 250 : 150));
            CHECK(digestOf(engine, file.value(), 0) == digestOf(doc.backend, doc.handle, firsts[at]));
            CHECK(digestOf(engine, file.value(), pages - 1) == digestOf(doc.backend, doc.handle, firsts[at] + pages - 1));
        }
    }
    {
        const qint64 bound = 40 * 1024;
        core::SplitRequest request;
        request.method = core::SplitMethod::MaxSize;
        request.maxBytes = bound;
        request.folder = QDir(root.path()).filePath(QStringLiteral("size"));
        auto outcome = doc.split(request);
        REQUIRE(outcome);
        int pages = 0;
        std::cout << "render.assembler by size: " << outcome.value().written.size() << " files of pages-900.pdf under "
                  << bound << " bytes:";
        for (std::size_t at = 0; at < outcome.value().written.size(); ++at) {
            const QFileInfo file(outcome.value().written[at].path);
            std::cout << " " << file.size();
            CHECK((file.size() <= bound || outcome.value().parts[at].overBound));
            pages += outcome.value().written[at].pageCount;
        }
        std::cout << "\n";
        CHECK(pages == 900);
        CHECK(outcome.value().written.size() >= 3);
        // The fewest files: no two neighbours would have fit as one.
        for (std::size_t at = 0; at + 1 < outcome.value().parts.size(); ++at) {
            std::vector<int> joined = outcome.value().parts[at].pages;
            joined.push_back(outcome.value().parts[at + 1].pages.front());
            auto snapshot = doc.backend.assemblySource(doc.handle);
            REQUIRE(snapshot);
            CHECK(snapshot.value()->assemble(joined).value().bytes.size() > bound);
        }
    }
}

// ==========================================================================================
// A-50 -- nothing overwritten, nothing outside the chosen folder, through the split itself
// ==========================================================================================

TEST_CASE("render.assembler split.collision a split refused before it writes anything", "[render][assembler][split.collision][A-50]")
{
    Splitting doc("assembly/odd-count-9.pdf");
    QTemporaryDir root;
    const QString folder = QDir(root.path()).filePath(QStringLiteral("parts"));
    REQUIRE(QDir().mkpath(folder));
    {
        QFile existing(QDir(folder).filePath(QStringLiteral("doc part 2.pdf")));   // the second name a split of 3 writes
        REQUIRE(existing.open(QIODevice::WriteOnly));
        existing.write("someone else's");
    }
    const QStringList before = everythingUnder(root.path());
    core::SplitRequest request;
    request.method = core::SplitMethod::EveryN;
    request.pagesPerFile = 3;
    request.folder = folder;
    auto outcome = doc.split(request);
    REQUIRE_FALSE(outcome);
    CHECK(outcome.error().messageKey == QLatin1String("error.assembly.exists"));
    CHECK(outcome.error().context == QDir(folder).filePath(QStringLiteral("doc part 2.pdf")));
    CHECK(everythingUnder(root.path()) == before);
}
