// tst_searchcorpus -- search over the CORPUS through the real engine: `pdf::DocumentBackendImpl`, the shipped
// `app/BackendTextExtractor` on the shipped `RenderScheduler`, `core/text/DocumentIndex` and `SearchEngine`
// -- the assembly `app/CompositionRoot` builds, minus the window. Stage 5 T6; guide cases A-63 (graded on
// the guide's §3.3), A-58, A-59's corpus half, A-21's find half and A-24's search lane
// (`Stage5-TextSearch-Testing-Guide.md` §5.8, §5.4).
//
// THE ORACLE IS THE GENERATOR (rule 7): every query here is a string `corpus/make-text.py` wrote down in
// `corpus/labels/` -- `text-tounicode.pdf`'s hanzi, `hyphenated.pdf`'s joins and its compound,
// `text-900.pdf`'s page markers -- never something read back out of PDFium first.
//
// WHAT IT CANNOT CLAIM (convention 19): that a hit is HIGHLIGHTED on its ink -- painting a hit is T7's (A-74,
// rule 6) -- and anything about a real document (D5-23). The §3.3 figure is THIS machine's, measured from
// submission to the last hit on the pumping thread with this case driving the pump; the product posts the
// same `deliverReady()` through Qt's event queue (`app/CompositionRoot`), which this does not include.

#include <catch2/catch_test_macros.hpp>

#include "app/BackendTextExtractor.h"
#include "core/backend.h"
#include "core/doc/PageTree.h"
#include "core/render/RenderScheduler.h"
#include "core/text/DocumentIndex.h"
#include "core/text/SearchEngine.h"
#include "core/text/SearchQuery.h"
#include "pdf/DocumentBackendImpl.h"

#include <QElapsedTimer>
#include <QString>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

using namespace rawpage;
using core::text::DocumentIndex;
using core::text::SearchEngine;
using core::text::SearchQuery;

namespace {

QString corpusFile(std::string_view relative)
{
    return QStringLiteral(RAWPAGE_CORPUS_DIR "/") + QString::fromUtf8(relative.data(), static_cast<qsizetype>(relative.size()));
}

// One opened corpus file over the product's assembly. Members in the order `CompositionRoot` builds them,
// so they are destroyed in the order it destroys them: the engine, the index, the scheduler, the backend.
struct Searching {
    explicit Searching(std::string_view relative)
    {
        auto opened = backend.open(corpusFile(relative), nullptr);
        REQUIRE(opened);
        handle = opened.value();
        const core::PageTree* pages = backend.pages(handle);
        REQUIRE(pages != nullptr);
        pageCount = pages->count();
        entry = index.attach(backend.documentId(handle), handle, pageCount);
        REQUIRE(entry != 0);
    }
    ~Searching()
    {
        index.detach(handle);
        scheduler.cancelDocument(handle);
        scheduler.waitForIdle(10000);
        (void)backend.close(handle);
    }

    SearchEngine::Outcome search(const SearchQuery& query, double* elapsedMs = nullptr)
    {
        std::optional<SearchEngine::Outcome> outcome;
        SearchEngine::Handlers handlers;
        handlers.finished = [&](const SearchEngine::Outcome& o) { outcome = o; };
        QElapsedTimer timer;
        timer.start();
        const auto started = engine.start(entry, query, handlers);
        REQUIRE(started);
        while (!outcome && timer.elapsed() < 120000) {
            scheduler.waitForIdle(1);
            scheduler.deliverReady();
            engine.waitForIdle(1);
            engine.deliverReady();
        }
        if (elapsedMs != nullptr) *elapsedMs = static_cast<double>(timer.nsecsElapsed()) / 1e6;
        REQUIRE(outcome.has_value());
        return *outcome;
    }

    // The hit's characters, read through the page's reading -- what a highlight would cover.
    QString textOf(const core::text::SearchHit& hit)
    {
        const auto page = index.page(entry, hit.page);
        REQUIRE(page != nullptr);
        const auto reading = page->reading();
        QString out;
        for (int p = hit.reading.begin; p < hit.reading.end; ++p) {
            const char32_t cp = reading->charAt(p).codepoint;
            out.append(QString::fromUcs4(&cp, 1));
        }
        return out;
    }

    pdf::DocumentBackendImpl backend;
    core::render::RenderScheduler scheduler{
        nullptr, [this] { return std::unique_ptr<core::ITextExtractor>(new app::BackendTextExtractor(backend)); },
        nullptr, core::render::RenderScheduler::Config{}};
    DocumentIndex index{scheduler};
    SearchEngine engine{index};
    core::DocumentHandle handle;
    int pageCount = 0;
    DocumentIndex::EntryId entry = 0;
};

SearchQuery literal(const QString& pattern)
{
    SearchQuery q;
    q.pattern = pattern;
    return q;
}

} // namespace

// =============================================================================================
// A-63 -- the second search of a 900-page file is instant, graded on §3.3; A-24's search lane
// =============================================================================================

TEST_CASE("the second search of text-900 is instant by the guide's definition", "[render][search][A-63][A-24][run-serial]")
{
    Searching text("ordinary/text-900.pdf");
    REQUIRE(text.pageCount == 900);

    // THE FIRST SEARCH builds the index -- reported, NOT graded (it is A2's subject, §3.1).
    double firstMs = 0.0;
    const SearchEngine::Outcome first = text.search(literal(QStringLiteral("unique-page-0450")), &firstMs);
    REQUIRE(first.hits.size() == 1);
    CHECK(first.hits.front().page == 449);
    CHECK(text.textOf(first.hits.front()) == QStringLiteral("unique-page-0450"));
    CHECK(text.index.pagesHeld(text.entry) == 900);
    // A-24: a search asks at `Background`, every page of it, and nothing at `Visible`.
    const auto lanes = text.scheduler.stats();
    CHECK(lanes.enqueuedBackground == 900);
    CHECK(lanes.enqueuedVisible == 0);

    // TWENTY LITERAL QUERIES after the index is complete: ten page markers and ten body words, each timed
    // from submission to the last hit delivered on this, the pumping, thread.
    const std::vector<QString> queries{
        QStringLiteral("unique-page-0001"), QStringLiteral("unique-page-0100"), QStringLiteral("unique-page-0222"),
        QStringLiteral("unique-page-0333"), QStringLiteral("unique-page-0444"), QStringLiteral("unique-page-0555"),
        QStringLiteral("unique-page-0666"), QStringLiteral("unique-page-0777"), QStringLiteral("unique-page-0888"),
        QStringLiteral("unique-page-0900"), QStringLiteral("kerning"),          QStringLiteral("raster append"),
        QStringLiteral("viewport"),         QStringLiteral("incremental monitor"), QStringLiteral("typeface"),
        QStringLiteral("journal"),          QStringLiteral("throughput"),       QStringLiteral("annotation cache"),
        QStringLiteral("percentile"),       QStringLiteral("glyph frame")};
    const long long enqueued = text.scheduler.stats().textEnqueued;
    std::vector<double> times;
    std::size_t totalHits = 0;
    for (const QString& q : queries) {
        double ms = 0.0;
        const SearchEngine::Outcome outcome = text.search(literal(q), &ms);
        times.push_back(ms);
        totalHits += outcome.hits.size();
        CHECK(outcome.pagesSearched == 900);
        if (q.startsWith(QStringLiteral("unique-page-"))) CHECK(outcome.hits.size() == 1);
        else CHECK(!outcome.hits.empty());
    }
    // Nothing more was asked of the engine: the index answered every one.
    CHECK(text.scheduler.stats().textEnqueued == enqueued);

    std::vector<double> sorted = times;
    std::sort(sorted.begin(), sorted.end());
    // The 95th percentile of twenty, nearest-rank: the 19th.
    const double p95 = sorted[static_cast<std::size_t>(18)];
    std::size_t characters = 0;
    for (int page = 0; page < text.pageCount; ++page) characters += text.index.page(text.entry, page)->model().size();
    std::printf("A-63: text-900.pdf, %d pages, %zu characters; first search (index build) %.1f ms;"
                " 20 queries, %zu hits: p95 %.2f ms, max %.2f ms, min %.2f ms; index %.1f MB held"
                " (%zu bytes); %d matching workers\n",
                text.pageCount, characters, firstMs, totalHits, p95, sorted.back(), sorted.front(),
                static_cast<double>(text.index.bytesHeld()) / (1024.0 * 1024.0), text.index.bytesHeld(),
                text.engine.workerCount());
    // §3.3: every hit delivered within 100 ms at p95.
    CHECK(p95 <= 100.0);
    // And the complete index fits the default budget (`search/indexCacheMB`), or the second search of this
    // file would be a first search at the default -- the row's own claim.
    CHECK(text.index.bytesHeld() <= DocumentIndex::kDefaultBudgetBytes);
}

// =============================================================================================
// A-58 -- CJK on text-tounicode.pdf's CJK page
// =============================================================================================

TEST_CASE("CJK is found on text-tounicode's CJK page and whole words finds it too", "[render][search][A-58]")
{
    Searching text("ordinary/text-tounicode.pdf");
    // The page's three hanzi lines (corpus/labels/text-tounicode.json, page 2): `中日` twice in the
    // second and once in the third.
    const SearchEngine::Outcome found = text.search(literal(QStringLiteral("中日")));
    REQUIRE(found.hits.size() == 3);
    for (const auto& hit : found.hits) {
        CHECK(hit.page == 1);
        CHECK(text.textOf(hit) == QStringLiteral("中日"));
    }
    CHECK(found.unmappedGlyphs == 0);   // the same glyphs WITH /ToUnicode: text, not a report

    // WHOLE WORDS: each ideograph is a word (D5-5's caveat, guide §7), so the same three.
    SearchQuery whole = literal(QStringLiteral("中日"));
    whole.flags.wholeWords = true;
    CHECK(text.search(whole).hits.size() == 3);
    // The Latin page's control for whole words: `file` inside `a file of figures` is a word; `fil` is not.
    SearchQuery fil = literal(QStringLiteral("fil"));
    fil.flags.wholeWords = true;
    CHECK(text.search(fil).hits.empty());
    CHECK(!text.search(literal(QStringLiteral("fil"))).hits.empty());
}

TEST_CASE("text-tounicode's ligatures and decomposed accents are found by their letters", "[render][search][A-56]")
{
    Searching text("ordinary/text-tounicode.pdf");
    // `ﬁsh and chips` is drawn with a ligature glyph mapped to U+FB01 (the engine decomposes it, T2), and
    // `café au lait` twice -- once precomposed, once e + U+0301.
    CHECK(text.search(literal(QStringLiteral("fish"))).hits.size() == 1);
    CHECK(text.search(literal(QStringLiteral("café"))).hits.size() == 2);
    // Diacritics matched by default: `cafe` finds neither until the flag says so.
    CHECK(text.search(literal(QStringLiteral("cafe"))).hits.empty());
    SearchQuery ignoring = literal(QStringLiteral("cafe"));
    ignoring.flags.ignoreDiacritics = true;
    CHECK(text.search(ignoring).hits.size() == 2);
    // `LIFT THE FILE` and `a file of figures`: case folded by default, not with match case.
    CHECK(text.search(literal(QStringLiteral("file"))).hits.size() == 2);
    SearchQuery cased = literal(QStringLiteral("file"));
    cased.flags.caseSensitive = true;
    CHECK(text.search(cased).hits.size() == 1);
}

// =============================================================================================
// A-21 (find's half) -- cjk-embedded.pdf's unmapped glyphs are reported and match nothing
// =============================================================================================

TEST_CASE("cjk-embedded's unmapped glyphs are reported by a search and matched by nothing", "[render][search][A-21]")
{
    Searching text("ordinary/cjk-embedded.pdf");
    const SearchEngine::Outcome hanzi = text.search(literal(QStringLiteral("中日")));
    CHECK(hanzi.hits.empty());
    // THE REPORT: every one of the 90 glyphs T2 measured, named by page.
    CHECK(hanzi.unmappedGlyphs == 90);
    CHECK_FALSE(hanzi.unmappedPages.empty());

    // NOTHING CLAIMS ONE, not even `.`: every hit `.` makes is a mapped character of the Helvetica captions.
    SearchQuery any = literal(QStringLiteral("."));
    any.flags.useRegex = true;
    const SearchEngine::Outcome dots = text.search(any);
    REQUIRE_FALSE(dots.hits.empty());   // the control: the captions ARE text
    for (const auto& hit : dots.hits) {
        const auto reading = text.index.page(text.entry, hit.page)->reading();
        for (int p = hit.reading.begin; p < hit.reading.end; ++p) CHECK_FALSE(reading->charAt(p).isUnmapped());
    }
}

// =============================================================================================
// A-59 (the corpus half) -- hyphenated.pdf's joins, its compound, and a block
// =============================================================================================

TEST_CASE("hyphenated's joins are found and its compound is not broken", "[render][search][A-59]")
{
    Searching text("ordinary/hyphenated.pdf");
    // The generator's own record (corpus/labels/hyphenated.json): `example` and `incremental` join,
    // `North-West` does not, and `well-known` is a mid-line compound.
    CHECK(text.search(literal(QStringLiteral("example"))).hits.size() == 1);
    CHECK(text.search(literal(QStringLiteral("incremental"))).hits.size() == 1);
    CHECK(text.search(literal(QStringLiteral("North-West"))).hits.size() == 1);
    CHECK(text.search(literal(QStringLiteral("well-known"))).hits.size() == 1);
    CHECK(text.search(literal(QStringLiteral("wellknown"))).hits.empty());
    CHECK(text.search(literal(QStringLiteral("NorthWest"))).hits.empty());
    // ACROSS A LINE BREAK inside the paragraph -- `exam-` / `ple document` -- found.
    CHECK(text.search(literal(QStringLiteral("example document"))).hits.size() == 1);
    // ACROSS A BLOCK -- the title line's last word and the body's first -- not found; the control is the
    // two words found on their own.
    CHECK(text.search(literal(QStringLiteral("compound the"))).hits.empty());
    CHECK(text.search(literal(QStringLiteral("compound"))).hits.size() == 1);
    CHECK(!text.search(literal(QStringLiteral("the renderer"))).hits.empty());
}
