// tst_foldersearch -- `core/text/FolderSearch` over a SCRATCH COPY of the corpus, through the real engine:
// `pdf::DocumentBackendImpl`'s staged open, the shipped `RenderScheduler` with `app/BackendTextExtractor`,
// `DocumentIndex` and `SearchEngine` -- the assembly `app/CompositionRoot` builds, minus the window.
// Stage 5 T9, decision D5-13; guide cases A-80, A-81, A-82 and A-88 (`Stage5-TextSearch-Testing-Guide.md`
// §5.11), and T9's *"a 900-page file's open never holds the engine longer than one page's extraction"*.
//
// A FOLDER SEARCH SEARCHES A SCRATCH FOLDER IT MADE (plan §5.6): a suite that searched the tester's own
// documents would depend on them. The files are copies of `corpus/`; the corpus itself is never opened in
// place.
//
// THE ORACLE IS THE GENERATOR (rule 7): a snippet is graded against the label's own text
// (`corpus/labels/`), never against what PDFium returns.
//
// WHAT IT CANNOT CLAIM (convention 19): the pane -- sorting, filtering, refine, export and activation are
// `tests/ui/tst_searchpane.cpp`'s and `tst_searchresults.cpp`'s -- and the viewport's latency while this
// runs, which is `bench_pipeline --stage5`'s folder column (A2, graded on the guide's §3.1).

#include <catch2/catch_test_macros.hpp>

#include "app/BackendTextExtractor.h"
#include "core/backend.h"
#include "core/render/RenderScheduler.h"
#include "core/text/DocumentIndex.h"
#include "core/text/FolderSearch.h"
#include "core/text/SearchEngine.h"
#include "core/text/SearchQuery.h"
#include "core/text/SearchResults.h"
#include "pdf/DocumentBackendImpl.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/Document.h"
#include "core/doc/SaveCoordinator.h"
#include "tests/unit/FakeDurableFiles.h"
#include "tests/render/TextOracle.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

using namespace rawpage;
using core::text::FileResult;
using core::text::FolderSearch;
using core::text::SearchQuery;
using core::text::SearchResults;

namespace {

// THE ENGINE THREAD'S IDENTITY and every page's extraction time, recorded on the worker.
struct Recorder {
    std::mutex mutex;
    std::set<std::thread::id> textThreads;
    std::vector<double> extractionMs;
};

class RecordingExtractor final : public core::ITextExtractor {
public:
    RecordingExtractor(core::IDocumentBackend& backend, Recorder& recorder) : inner_(backend), recorder_(recorder) {}
    core::Status extractPage(core::DocumentHandle document, int page, core::text::PageTextModel& into) override
    {
        const auto began = std::chrono::steady_clock::now();
        const core::Status status = inner_.extractPage(document, page, into);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        const std::lock_guard<std::mutex> lock(recorder_.mutex);
        recorder_.textThreads.insert(std::this_thread::get_id());
        recorder_.extractionMs.push_back(ms);
        return status;
    }

private:
    app::BackendTextExtractor inner_;
    Recorder& recorder_;
};

// Members in the order `app/CompositionRoot` builds them, so they are destroyed in the order it destroys
// them: the folder search, the engine, the index, the scheduler, the backend.
struct Rig {
    Rig()
    {
        index.setUnitReader([this](core::DocumentHandle handle, int part, std::vector<core::text::SearchUnit>& into) {
            return backend.searchUnits(handle, part, into);
        });
    }

    struct Run {
        std::vector<FileResult> files;
        std::optional<FolderSearch::Summary> summary;
    };

    Run search(FolderSearch::Request request, int budgetMs = 120000)
    {
        Run run;
        FolderSearch::Handlers handlers;
        handlers.file = [&run](FileResult file) { run.files.push_back(std::move(file)); };
        handlers.finished = [&run](const FolderSearch::Summary& summary) { run.summary = summary; };
        REQUIRE(folder.start(std::move(request), std::move(handlers)));
        pumpUntil([&] { return run.summary.has_value(); }, budgetMs);
        REQUIRE(run.summary.has_value());
        return run;
    }

    void pumpUntil(const std::function<bool()>& done, int budgetMs)
    {
        QElapsedTimer timer;
        timer.start();
        while (!done() && timer.elapsed() < budgetMs) pumpOnce();
    }
    void pumpOnce()
    {
        scheduler.waitForIdle(1);
        scheduler.deliverReady();
        engine.waitForIdle(1);
        engine.deliverReady();
        folder.waitForIdle(1);
        folder.deliverReady();
    }

    Recorder recorder;
    pdf::DocumentBackendImpl backend;
    core::render::RenderScheduler scheduler{
        nullptr, [this] { return std::unique_ptr<core::ITextExtractor>(new RecordingExtractor(backend, recorder)); },
        nullptr, core::render::RenderScheduler::Config{}};
    core::text::DocumentIndex index{scheduler};
    core::text::SearchEngine engine{index};
    FolderSearch folder{backend, scheduler, index, engine};
};

SearchQuery literal(const QString& pattern)
{
    SearchQuery query;
    query.pattern = pattern;
    return query;
}

// THE SCRATCH TREE: three levels of ordinary files, the encrypted set and the malformed set -- copies.
struct Tree {
    Tree()
    {
        REQUIRE(directory.isValid());
        copy("ordinary/search-classes.pdf", QString());
        copy("ordinary/columns-two.pdf", QString());
        copy("ordinary/hyphenated.pdf", QStringLiteral("nested"));
        copy("ordinary/columns-three.pdf", QStringLiteral("nested/deeper"));
        for (const QString& name : QDir(test_oracle::corpusPath("encrypted")).entryList({QStringLiteral("*.pdf")}, QDir::Files))
            copy(("encrypted/" + name).toStdString(), QStringLiteral("locked"));
        for (const QString& name : QDir(test_oracle::corpusPath("malformed")).entryList({QStringLiteral("*.pdf")}, QDir::Files))
            copy(("malformed/" + name).toStdString(), QStringLiteral("broken"));
    }

    void copy(const std::string& relative, const QString& into)
    {
        const QString source = test_oracle::corpusPath(relative);
        const QDir root(directory.path());
        REQUIRE(root.mkpath(into.isEmpty() ? QStringLiteral(".") : into));
        const QString target = root.filePath((into.isEmpty() ? QString() : into + QLatin1Char('/')) + QFileInfo(source).fileName());
        REQUIRE(QFile::copy(source, target));
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        ++count;
    }

    QString path() const { return directory.path(); }
    QString relative(const QString& file) const { return QDir(directory.path()).relativeFilePath(file); }

    QTemporaryDir directory;
    int count = 0;
};

// The label's page text, in the generator's reading order: the blocks of `order`, each block's lines, all
// joined by one space -- what a snippet of that page must be a piece of.
QString labelText(const std::string& name, int pageIndex)
{
    const QJsonObject label = test_oracle::loadLabel(name);
    for (const QJsonValue pageValue : label.value(QStringLiteral("pages")).toArray()) {
        const QJsonObject page = pageValue.toObject();
        if (page.value(QStringLiteral("index")).toInt(-1) != pageIndex) continue;
        const QJsonArray lines = page.value(QStringLiteral("lines")).toArray();
        const QJsonArray blocks = page.value(QStringLiteral("blocks")).toArray();
        QStringList parts;
        for (const QJsonValue order : page.value(QStringLiteral("order")).toArray()) {
            const QJsonObject block = blocks.at(order.toInt()).toObject();
            for (const QJsonValue line : block.value(QStringLiteral("lines")).toArray())
                parts << lines.at(line.toInt()).toObject().value(QStringLiteral("text")).toString();
        }
        return parts.join(QLatin1Char(' ')).simplified();
    }
    FAIL("no such label page");
    return {};
}

QStringList words(const QString& text)
{
    return text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

} // namespace

// =============================================================================================
// A-80 -- a folder, recursive and not
// =============================================================================================

TEST_CASE("a folder search finds the top folder alone or every level and names each hit's file and page",
          "[foldersearch][A-80]")
{
    Tree tree;
    Rig rig;
    // `RawPage` is in the furniture the text generator puts on every page it makes.
    FolderSearch::Request request;
    request.folder = tree.path();
    request.query = literal(QStringLiteral("RawPage"));

    SECTION("not recursive: the top folder's two files and nothing below")
    {
        request.recursive = false;
        const Rig::Run run = rig.search(request);
        REQUIRE(run.files.size() == 2);
        for (const FileResult& file : run.files) {
            CHECK_FALSE(tree.relative(file.path).contains(QLatin1Char('/')));
            CHECK_FALSE(file.hits.empty());
        }
    }
    SECTION("recursive: every level")
    {
        request.recursive = true;
        const Rig::Run run = rig.search(request);
        CHECK(static_cast<int>(run.files.size()) == tree.count);
        std::set<QString> levels;
        int hits = 0;
        for (const FileResult& file : run.files) {
            if (file.hits.empty()) continue;
            levels.insert(QFileInfo(tree.relative(file.path)).path());
            for (const auto& hit : file.hits) {
                // EVERY HIT NAMES ITS FILE AND ITS PAGE, the file one that exists and the page one it has.
                CHECK(QFileInfo::exists(file.path));
                CHECK(hit.hit.page >= 0);
                CHECK(hit.hit.page < file.pageCount);
                ++hits;
            }
        }
        CHECK(levels.count(QStringLiteral(".")) == 1);
        CHECK(levels.count(QStringLiteral("nested")) == 1);
        CHECK(levels.count(QStringLiteral("nested/deeper")) == 1);
        CHECK(run.summary->hits == hits);
    }
}

// =============================================================================================
// A-81 -- an encrypted file is skipped with a reason; a broken one is reported; the search goes on
// =============================================================================================

TEST_CASE("a folder search skips an encrypted file with a reason and goes on past a broken one",
          "[foldersearch][A-81]")
{
    Tree tree;
    Rig rig;
    FolderSearch::Request request;
    request.folder = tree.path();
    request.recursive = true;
    request.query = literal(QStringLiteral("RawPage"));
    const Rig::Run run = rig.search(request);

    // THE SEARCH WENT ON: one result for every file in the tree, searched or not.
    CHECK(static_cast<int>(run.files.size()) == tree.count);
    int encrypted = 0;
    int broken = 0;
    for (const FileResult& file : run.files) {
        const QString where = tree.relative(file.path);
        INFO(where.toStdString() << (file.refused ? " -- " + QString(file.refused->messageKey).toStdString() : ""));
        if (where.startsWith(QLatin1String("locked/"))) {
            // NEVER PROMPTED: a cancelled prompt would be `Cancelled`; a folder search asks nobody, so a file
            // that needs a password is REFUSED as encrypted -- and a file whose user password is empty opens.
            if (file.refused) {
                CHECK(file.isEncrypted());
                CHECK(file.refused->code != core::ErrorCode::Cancelled);
                ++encrypted;
            }
        }
        if (where.startsWith(QLatin1String("broken/")) && file.refused) {
            // A damaged file the engine could not repair is listed WITH ITS ERROR, not as encrypted.
            CHECK_FALSE(file.isEncrypted());
            CHECK_FALSE(QString(file.refused->messageKey).isEmpty());
            ++broken;
        }
    }
    CHECK(encrypted >= 1);
    CHECK(run.summary->filesRefused == encrypted + broken);
    CHECK(run.summary->filesSearched + run.summary->filesRefused == tree.count);
    std::printf("A-81: %d files, %d searched, %d skipped as encrypted, %d broken\n", tree.count,
                run.summary->filesSearched, encrypted, broken);
}

// =============================================================================================
// A-82 -- snippets, against the generator's text
// =============================================================================================

TEST_CASE("a folder search's snippets are the generator's words either side of the hit", "[foldersearch][A-82]")
{
    Tree tree;
    Rig rig;
    struct Probe {
        QString word;
        std::string label;
        QString file;
    };
    const std::vector<Probe> probes{{QStringLiteral("papaya"), "search-classes", QStringLiteral("search-classes.pdf")},
                                    {QStringLiteral("viewport"), "columns-two", QStringLiteral("columns-two.pdf")}};
    int judged = 0;
    for (const Probe& probe : probes) {
        FolderSearch::Request request;
        request.folder = tree.path();
        request.query = literal(probe.word);
        const Rig::Run run = rig.search(request);
        for (const FileResult& file : run.files) {
            if (QFileInfo(file.path).fileName() != probe.file) continue;
            REQUIRE_FALSE(file.hits.empty());
            for (const auto& hit : file.hits) {
                const QString page = labelText(probe.label, hit.hit.page);
                const QString shown = hit.snippet.plain().simplified();
                INFO(probe.word.toStdString() << " on page " << hit.hit.page + 1 << ": [" << hit.snippet.before.toStdString()
                     << "|" << hit.snippet.match.toStdString() << "|" << hit.snippet.after.toStdString() << "]");
                // THE HIT, MARKED: its own field, and the word searched for.
                CHECK(hit.snippet.match.compare(probe.word, Qt::CaseInsensitive) == 0);
                // AS THE GENERATOR WROTE IT: the snippet is a run of the label's text...
                CHECK(page.contains(shown));
                // ... five whole words before the hit and five after, cut at word boundaries.
                const QStringList before = words(hit.snippet.before);
                const QStringList after = words(hit.snippet.after);
                CHECK(before.size() <= SearchResults::kSnippetWords);
                CHECK(after.size() <= SearchResults::kSnippetWords);
                const QStringList all = words(page);
                const QStringList mine = words(shown);
                bool aligned = false;
                for (qsizetype at = 0; at + mine.size() <= all.size() && !aligned; ++at)
                    aligned = all.mid(at, mine.size()) == mine;
                CHECK(aligned);
                ++judged;
            }
        }
    }
    CHECK(judged >= 7);   // papaya once; viewport six times on page 1 and more
}

// =============================================================================================
// A-88 -- the UI thread opens nothing; cancelled within one file; bounded across files
// =============================================================================================

TEST_CASE("a folder search opens nothing on the calling thread and every engine piece on the engine thread",
          "[foldersearch][A-88]")
{
    Tree tree;
    Rig rig;
    std::mutex mutex;
    std::set<std::thread::id> enginePieces;
    std::set<std::thread::id> otherPieces;
    int pieces = 0;
    rig.folder.setPieceObserver([&](bool engine, double) {
        const std::lock_guard<std::mutex> lock(mutex);
        (engine ? enginePieces : otherPieces).insert(std::this_thread::get_id());
        ++pieces;
    });
    FolderSearch::Request request;
    request.folder = tree.path();
    request.recursive = true;
    request.query = literal(QStringLiteral("RawPage"));
    (void)rig.search(request);

    const std::lock_guard<std::mutex> lock(mutex);
    const std::lock_guard<std::mutex> recorded(rig.recorder.mutex);
    REQUIRE(rig.recorder.textThreads.size() == 1);
    const std::thread::id engineThread = *rig.recorder.textThreads.begin();
    // EVERY ENGINE PIECE on the thread the page text was extracted on -- the render worker.
    REQUIRE(enginePieces.size() == 1);
    CHECK(*enginePieces.begin() == engineThread);
    // EVERY OTHER PIECE (reading the files, the first-party reading) on the helper: one thread, neither the
    // engine's nor this one.
    REQUIRE(otherPieces.size() == 1);
    CHECK(*otherPieces.begin() != engineThread);
    // AND THE CALLING THREAD -- the UI thread's place -- ran none of them.
    CHECK(enginePieces.count(std::this_thread::get_id()) == 0);
    CHECK(otherPieces.count(std::this_thread::get_id()) == 0);
    std::printf("A-88: %d pieces over %d files\n", pieces, tree.count);
}

TEST_CASE("a cancelled folder search delivers nothing more and the next one runs", "[foldersearch][A-88]")
{
    Tree tree;
    Rig rig;
    std::vector<FileResult> files;
    bool finished = false;
    FolderSearch::Handlers handlers;
    handlers.file = [&](FileResult file) {
        files.push_back(std::move(file));
        // CANCELLED FROM INSIDE THE FIRST FILE'S DELIVERY -- between files, as the pane's Stop is.
        rig.folder.cancel();
    };
    handlers.finished = [&](const FolderSearch::Summary&) { finished = true; };
    FolderSearch::Request request;
    request.folder = tree.path();
    request.recursive = true;
    request.query = literal(QStringLiteral("RawPage"));
    REQUIRE(rig.folder.start(request, handlers));
    rig.pumpUntil([&] { return !files.empty(); }, 60000);
    REQUIRE(files.size() == 1);
    CHECK_FALSE(rig.folder.isRunning());
    // STOPPED: however long the pumps run, nothing more arrives -- not the second file, not a summary.
    for (int i = 0; i < 200; ++i) rig.pumpOnce();
    CHECK(files.size() == 1);
    CHECK_FALSE(finished);

    // THE CONTROL: the same search, not cancelled, delivers every file.
    const Rig::Run again = rig.search(request);
    CHECK(static_cast<int>(again.files.size()) == tree.count);
}

TEST_CASE("search/maxResults bounds a folder search across files and says so", "[foldersearch][A-88]")
{
    Tree tree;
    Rig rig;
    FolderSearch::Request request;
    request.folder = tree.path();
    request.recursive = true;
    request.query = literal(QStringLiteral("RawPage"));
    request.query.maxResults = 3;
    const Rig::Run bounded = rig.search(request);
    int hits = 0;
    for (const FileResult& file : bounded.files) hits += static_cast<int>(file.hits.size());
    CHECK(hits == 3);
    CHECK(bounded.summary->limitReached);
    // It stopped asking: fewer files than the tree holds were even opened.
    CHECK(static_cast<int>(bounded.files.size()) < tree.count);

    // THE CONTROL: unbounded, the same search finds more than three and says nothing of a bound.
    request.query.maxResults = 0;
    const Rig::Run all = rig.search(request);
    CHECK(all.summary->hits > 3);
    CHECK_FALSE(all.summary->limitReached);
}

// =============================================================================================
// T9's bound -- a 900-page file's open never holds the engine longer than one page's extraction
// =============================================================================================

TEST_CASE("a 900-page file's open never holds the engine longer than one page's extraction", "[foldersearch][T9][run-serial]")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    for (const char* name : {"ordinary/text-900.pdf", "ordinary/pages-900.pdf"}) {
        const QString source = test_oracle::corpusPath(name);
        REQUIRE(QFile::copy(source, QDir(directory.path()).filePath(QFileInfo(source).fileName())));
    }
    Rig rig;
    std::mutex mutex;
    std::vector<double> engineMs;
    std::vector<double> otherMs;
    rig.folder.setPieceObserver([&](bool engine, double ms) {
        const std::lock_guard<std::mutex> lock(mutex);
        (engine ? engineMs : otherMs).push_back(ms);
    });
    FolderSearch::Request request;
    request.folder = directory.path();
    // A word on every page, so every page of both files is extracted -- the measure each piece is held to.
    request.query = literal(QStringLiteral("of 900"));
    (void)rig.search(request, 300000);

    const std::lock_guard<std::mutex> lock(mutex);
    const std::lock_guard<std::mutex> recorded(rig.recorder.mutex);
    REQUIRE_FALSE(engineMs.empty());
    REQUIRE(rig.recorder.extractionMs.size() >= 900);
    std::vector<double> extraction = rig.recorder.extractionMs;
    std::sort(extraction.begin(), extraction.end());
    const double extractionMax = extraction.back();
    const double extractionP95 = extraction[extraction.size() * 95 / 100];
    std::sort(engineMs.begin(), engineMs.end());
    const double pieceMax = engineMs.back();
    std::printf("T9 bound: %zu engine pieces, max %.3f ms, p95 %.3f ms; %zu off-engine pieces, max %.3f ms; one "
                "page's extraction over %zu pages: p95 %.3f ms, max %.3f ms\n",
                engineMs.size(), pieceMax, engineMs[engineMs.size() * 95 / 100], otherMs.size(),
                otherMs.empty() ? 0.0 : *std::max_element(otherMs.begin(), otherMs.end()), extraction.size(),
                extractionP95, extractionMax);
    // EVERY PIECE THE ENGINE HELD is no longer than the longest page extraction in the same run -- the
    // job D5-3 already accepts a visible tile waiting behind.
    CHECK(pieceMax <= extractionMax);
}

// ==========================================================================================
// Stage 6 T9 (D6-11, guide A-74's folder clause) -- a closed file is searched as SAVED
// ==========================================================================================

TEST_CASE("a folder search finds a comment only once it is saved", "[foldersearch][A-74][D6-11]")
{
    // Comments of an OPEN document are its store's (the find bar's run); a folder search opens files of its own
    // and reads what the FILE holds -- so a comment made and not saved is not there, and once saved it is.
    QTemporaryDir folder;
    REQUIRE(folder.isValid());
    const QString path = QDir(folder.path()).filePath(QStringLiteral("text-a4.pdf"));
    REQUIRE(QFile::copy(QStringLiteral(RAWPAGE_CORPUS_DIR "/ordinary/text-a4.pdf"), path));
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    pdf::DocumentBackendImpl editor;
    tests::FakeDurableFiles durable;
    auto handle = editor.open(path, nullptr);
    REQUIRE(handle);
    core::Document document;
    REQUIRE(document.hydrate(editor, handle.value()));
    core::CommandJournal journal(std::move(document), editor.mutationSink(handle.value()));
    core::Annotation note = core::newAnnotation(core::AnnotationSubtype::Text, 0, QStringLiteral("Ana"), 1791590400);
    note.rect = core::PdfRect{core::PdfPoint{500, 700}, core::PdfSize{20, 20}};
    note.icon = QStringLiteral("Comment");
    note.flags = core::AnnotationFlag::Print | core::AnnotationFlag::NoZoom | core::AnnotationFlag::NoRotate;
    note.contents = QStringLiteral("A marmoset was here");
    REQUIRE(journal.run(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{note})));

    const auto marmosets = [&] {
        Rig rig;
        FolderSearch::Request request;
        request.folder = folder.path();
        request.query = literal(QStringLiteral("marmoset"));
        request.query.classes.comments = true;
        const Rig::Run run = rig.search(std::move(request));
        int hits = 0;
        for (const FileResult& file : run.files) hits += static_cast<int>(file.hits.size());
        return hits;
    };
    CHECK(marmosets() == 0);   // made, NOT saved

    core::SaveCoordinator coordinator(editor, &durable);
    auto saved = coordinator.save(journal, path);
    REQUIRE(saved);
    REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);
    (void)editor.close(handle.value());
    CHECK(marmosets() == 1);   // saved: in the file, and found there
}
