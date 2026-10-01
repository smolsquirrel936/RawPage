// tst_index -- `core/text/DocumentIndex` and `SearchEngine`'s asynchronous RUN, headless, over the shipped
// `core/render/RenderScheduler` with a fake extractor. Stage 5 T6, decisions D5-3 and D5-4; guide cases
// A-62 … A-67, A-60's and A-61's run halves, A-57's "no search run", A-24's search lane and A-21's report
// (`Stage5-TextSearch-Testing-Guide.md` §5.8).
//
// WHAT THIS FILE CANNOT CLAIM, named here (convention 19): the extractor is a fake that writes three lines
// per page, so nothing here says the CORPUS's text is found or that a 900-page file's second search is
// instant on real pages -- both are `tests/render/tst_searchcorpus.cpp`'s (A-63, over `text-900.pdf`
// through the real engine). And the "windows" here are two handles attached by hand: that the product's
// `window.new` attaches its companion to the SAME entry is `tests/ui/tst_documentindex.cpp`'s. What this
// file owns is the index's bookkeeping -- what is asked, once, at which lane, what is kept, what goes
// stale, what is evicted and in which order -- and the run's: the bound, the scopes, cancellation and the
// threads, each with a control.

#include <catch2/catch_test_macros.hpp>

#include "core/backend.h"
#include "core/doc/DocumentId.h"
#include "core/doc/Scope.h"
#include "core/render/RenderScheduler.h"
#include "core/settings/SettingsSchema.h"
#include "core/text/DocumentIndex.h"
#include "core/text/PageTextModel.h"
#include "core/text/SearchEngine.h"
#include "core/text/SearchQuery.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

using namespace rawpage;
using core::DocumentHandle;
using core::render::RenderPriority;
using core::render::RenderScheduler;
using core::text::DocumentIndex;
using core::text::PageTextModel;
using core::text::SearchEngine;
using core::text::SearchQuery;
using core::text::SearchScope;
using core::text::TextChar;

namespace {

// =============================================================================================
// The fake extractor: three lines a page, and a record of every call
// =============================================================================================
// Page N of any document holds `unique-page-NNNN`, then `lorem ipsum dolor`, then `sit amet ipsum` --
// so `lorem` is one hit a page and `ipsum` two. A page listed in `unmapped` gets three glyphs with no
// Unicode after its text; a page listed in `failing` is refused.
struct Probe {
    std::mutex mutex;
    std::map<std::pair<std::uint64_t, int>, int> calls;   // (handle, page) -> extractions
    std::set<std::thread::id> threads;
    std::set<std::pair<std::uint64_t, int>> unmapped;
    std::set<std::pair<std::uint64_t, int>> failing;

    int callsFor(DocumentHandle handle, int page)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        const auto found = calls.find({handle.id, page});
        return found == calls.end() ? 0 : found->second;
    }
    int total()
    {
        const std::lock_guard<std::mutex> lock(mutex);
        int sum = 0;
        for (const auto& [key, n] : calls) {
            (void)key;
            sum += n;
        }
        return sum;
    }
};

void appendLine(PageTextModel& model, const QString& text, double baseline, std::uint8_t flags = 0)
{
    double x = 72.0;
    for (const QChar ch : text) {
        TextChar c;
        c.codepoint = ch.unicode();
        c.fontSize = 10.0;
        c.flags = flags;
        c.origin = core::PdfPoint{x, baseline};
        c.box = core::PdfRect{core::PdfPoint{x, baseline - 2.1}, core::PdfSize{5.5, 11.1}};
        model.append(c);
        x += 5.5;
    }
}

class FakeExtractor final : public core::ITextExtractor {
public:
    explicit FakeExtractor(Probe& probe) : probe_(probe) {}

    core::Status extractPage(DocumentHandle handle, int page, PageTextModel& into) override
    {
        bool unmapped = false;
        {
            const std::lock_guard<std::mutex> lock(probe_.mutex);
            ++probe_.calls[{handle.id, page}];
            probe_.threads.insert(std::this_thread::get_id());
            if (probe_.failing.count({handle.id, page}) != 0)
                return core::Error{core::ErrorCode::ParseFailure, QLatin1String("error.text.extractFailed"),
                                   QString::number(page + 1)};
            unmapped = probe_.unmapped.count({handle.id, page}) != 0;
        }
        appendLine(into, QStringLiteral("unique-page-%1").arg(page + 1, 4, 10, QLatin1Char('0')), 760.0);
        appendLine(into, QStringLiteral("lorem ipsum dolor"), 746.0);
        appendLine(into, QStringLiteral("sit amet ipsum"), 732.0);
        if (unmapped) appendLine(into, QStringLiteral("xyz"), 718.0, static_cast<std::uint8_t>(core::text::CharFlag::Unmapped));
        return {};
    }

private:
    Probe& probe_;
};

core::DocumentId identity(const QString& name)
{
    return core::DocumentId::of(QStringLiteral("C:/scratch/") + name, 1000, name.toUtf8());
}

// The scheduler, the index and the engine in the order `app/CompositionRoot` builds them -- and destroys
// them: the engine, then the index, then the scheduler.
struct Rig {
    Probe probe;
    RenderScheduler scheduler{nullptr,
                              [this] { return std::unique_ptr<core::ITextExtractor>(new FakeExtractor(probe)); },
                              nullptr, RenderScheduler::Config{}};
    DocumentIndex index{scheduler};
    SearchEngine engine{index, SearchEngine::Config{2}};

    // Pages arrive on the scheduler's pump, hits on the engine's; both are this thread here, as both are
    // the UI thread in the product.
    void pumpUntil(const std::function<bool()>& done, int budgetMs = 20000)
    {
        QElapsedTimer timer;
        timer.start();
        while (!done() && timer.elapsed() < budgetMs) {
            scheduler.waitForIdle(20);
            scheduler.deliverReady();
            engine.waitForIdle(20);
            engine.deliverReady();
        }
    }
    void pumpIdle()
    {
        for (int i = 0; i < 4; ++i) {
            scheduler.waitForIdle(2000);
            scheduler.deliverReady();
            engine.waitForIdle(2000);
            engine.deliverReady();
        }
    }

    // One search, run to its end. A refused start is a test failure here.
    SearchEngine::Outcome search(DocumentIndex::EntryId entry, SearchQuery query)
    {
        std::optional<SearchEngine::Outcome> outcome;
        SearchEngine::Handlers handlers;
        handlers.finished = [&](const SearchEngine::Outcome& o) { outcome = o; };
        const auto started = engine.start(entry, query, handlers);
        REQUIRE(started);
        pumpUntil([&] { return outcome.has_value(); });
        REQUIRE(outcome.has_value());
        return *outcome;
    }
};

SearchQuery literal(const QString& pattern)
{
    SearchQuery q;
    q.pattern = pattern;
    return q;
}

} // namespace

// =============================================================================================
// A-62 -- built lazily; A-24's search lane
// =============================================================================================

TEST_CASE("the index holds only what was asked for until a search asks for the rest", "[index][A-62][A-24]")
{
    Rig rig;
    const DocumentHandle handle{11};
    const auto id = identity(QStringLiteral("lazy.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, handle, 20);
    REQUIRE(entry != 0);
    CHECK(rig.index.pagesHeld(entry) == 0);

    // A SELECTION ON ONE PAGE: that page, at `Visible`, and nothing else.
    const DocumentIndex::Owner selection = rig.index.newOwner();
    CHECK_FALSE(rig.index.request(selection, entry, 5, RenderPriority::Visible));
    rig.pumpIdle();
    CHECK(rig.index.pagesHeld(entry) == 1);
    CHECK(rig.index.has(entry, 5));
    // THE CONTROL: no search, no full extraction -- however long the pump runs.
    rig.pumpIdle();
    CHECK(rig.probe.total() == 1);
    CHECK(rig.scheduler.stats().enqueuedBackground == 0);
    // A page already here is answered `true` and asks nothing.
    CHECK(rig.index.request(selection, entry, 5, RenderPriority::Visible));
    CHECK(rig.scheduler.stats().textEnqueued == 1);

    // THE FIRST SEARCH asks for the other nineteen, at `Background` -- the lane D5-3 gives work nobody is
    // watching -- and never for the page it already has.
    const SearchEngine::Outcome outcome = rig.search(entry, literal(QStringLiteral("lorem")));
    CHECK(outcome.hits.size() == 20);
    CHECK(rig.index.pagesHeld(entry) == 20);
    const auto stats = rig.scheduler.stats();
    CHECK(stats.enqueuedVisible == 1);
    CHECK(stats.enqueuedBackground == 19);
    CHECK(rig.probe.callsFor(handle, 5) == 1);
    CHECK(rig.probe.total() == 20);
}

// =============================================================================================
// A-63's headless half -- the second search builds nothing and extracts nothing
// =============================================================================================

TEST_CASE("the second search extracts nothing and normalizes nothing", "[index][A-63]")
{
    Rig rig;
    const DocumentHandle handle{12};
    const auto id = identity(QStringLiteral("second.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, handle, 30);

    const SearchEngine::Outcome first = rig.search(entry, literal(QStringLiteral("ipsum")));
    CHECK(first.hits.size() == 60);
    const long long enqueued = rig.scheduler.stats().textEnqueued;
    const core::text::NormalizeOptions options = literal(QString()).flags.normalizeOptions();
    for (int page = 0; page < 30; ++page) {
        const auto held = rig.index.page(entry, page);
        REQUIRE(held != nullptr);
        CHECK(held->hasReading());
        CHECK(held->hasSearchable(options));
    }

    // THE SECOND SEARCH, a different word over the same options: nothing asked of the engine, and every
    // part it reads was made by the first (the index's laziness is what makes it instant, §3.3).
    const SearchEngine::Outcome second = rig.search(entry, literal(QStringLiteral("dolor")));
    CHECK(second.hits.size() == 30);
    CHECK(rig.scheduler.stats().textEnqueued == enqueued);
    CHECK(rig.probe.total() == 30);
    // THE CONTROL that a new normalization IS built when asked for: match case is another form, made once.
    core::text::NormalizeOptions exact = options;
    exact.foldCase = false;
    CHECK_FALSE(rig.index.page(entry, 0)->hasSearchable(exact));
    SearchQuery cased = literal(QStringLiteral("Lorem"));
    cased.flags.caseSensitive = true;
    CHECK(rig.search(entry, cased).hits.empty());
    CHECK(rig.index.page(entry, 0)->hasSearchable(exact));
    CHECK(rig.probe.total() == 30);
}

// =============================================================================================
// A-64 -- a mutation's Scope invalidates exactly its pages
// =============================================================================================

TEST_CASE("a mutation's scope re-extracts exactly the pages it names", "[index][A-64]")
{
    Rig rig;
    const DocumentHandle handle{13};
    const auto id = identity(QStringLiteral("rotate.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, handle, 10);
    (void)rig.search(entry, literal(QStringLiteral("lorem")));
    REQUIRE(rig.probe.total() == 10);

    // THE CONTROL: a scope of nothing drops nothing.
    rig.index.invalidate(entry, core::Scope::nothing());
    CHECK(rig.index.pagesHeld(entry) == 10);

    // `page.rotate` on page 4 (index 3) -- the scope Stage 3's command reports.
    rig.index.invalidate(entry, core::Scope::page(3));
    CHECK(rig.index.pagesHeld(entry) == 9);
    CHECK_FALSE(rig.index.has(entry, 3));
    CHECK(rig.index.stats().pagesDropped == 1);

    (void)rig.search(entry, literal(QStringLiteral("lorem")));
    CHECK(rig.probe.callsFor(handle, 3) == 2);
    for (int page = 0; page < 10; ++page)
        if (page != 3) CHECK(rig.probe.callsFor(handle, page) == 1);

    // And a scope the index cannot narrow -- objects, pages unknown -- rounds OUTWARDS to every page.
    rig.index.invalidate(entry, core::Scope::objects({42}));
    CHECK(rig.index.pagesHeld(entry) == 0);
}

// =============================================================================================
// A-65 -- the budget evicts whole documents, least recently searched first
// =============================================================================================

TEST_CASE("the budget evicts whole documents least recently searched first", "[index][A-65]")
{
    Rig rig;
    const auto idA = identity(QStringLiteral("a.pdf"));
    const auto idB = identity(QStringLiteral("b.pdf"));
    const auto idC = identity(QStringLiteral("c.pdf"));
    const DocumentIndex::EntryId a = rig.index.attach(&idA, DocumentHandle{21}, 10);
    const DocumentIndex::EntryId b = rig.index.attach(&idB, DocumentHandle{22}, 10);
    const DocumentIndex::EntryId c = rig.index.attach(&idC, DocumentHandle{23}, 10);

    // One document's index, measured -- every page holds the same text, so the three are the same size.
    (void)rig.search(a, literal(QStringLiteral("lorem")));
    const std::size_t one = rig.index.bytesHeld(a);
    REQUIRE(one > 0);

    // ROOM FOR TWO AND A HALF.
    rig.index.setBudgetBytes(one * 5 / 2);
    CHECK(rig.index.pagesHeld(a) == 10);   // within the budget: nothing to do

    (void)rig.search(b, literal(QStringLiteral("lorem")));
    CHECK(rig.index.bytesHeld() <= rig.index.budgetBytes());
    CHECK(rig.index.pagesHeld(a) == 10);
    CHECK(rig.index.pagesHeld(b) == 10);

    (void)rig.search(c, literal(QStringLiteral("lorem")));
    // A WAS SEARCHED FIRST, SO A GOES -- whole, not a page at a time -- and B and C stay.
    CHECK(rig.index.pagesHeld(a) == 0);
    CHECK(rig.index.pagesHeld(b) == 10);
    CHECK(rig.index.pagesHeld(c) == 10);
    CHECK(rig.index.bytesHeld() <= rig.index.budgetBytes());
    CHECK(rig.index.stats().documentsEvicted == 1);

    // Searching A again re-extracts it, and now B is the least recently searched.
    (void)rig.search(a, literal(QStringLiteral("lorem")));
    CHECK(rig.index.pagesHeld(a) == 10);
    CHECK(rig.index.pagesHeld(b) == 0);
    CHECK(rig.index.pagesHeld(c) == 10);
    CHECK(rig.index.bytesHeld() <= rig.index.budgetBytes());
    CHECK(rig.probe.callsFor(DocumentHandle{21}, 0) == 2);

    // THE CONTROL: a budget with room for all three evicts nothing.
    rig.index.setBudgetBytes(one * 10);
    (void)rig.search(b, literal(QStringLiteral("lorem")));
    CHECK(rig.index.pagesHeld(a) == 10);
    CHECK(rig.index.pagesHeld(b) == 10);
    CHECK(rig.index.pagesHeld(c) == 10);
}

TEST_CASE("a document never searched is evicted before any that was", "[index][A-65]")
{
    Rig rig;
    const auto idA = identity(QStringLiteral("searched.pdf"));
    const auto idB = identity(QStringLiteral("selected.pdf"));
    const DocumentIndex::EntryId searched = rig.index.attach(&idA, DocumentHandle{31}, 10);
    const DocumentIndex::EntryId selected = rig.index.attach(&idB, DocumentHandle{32}, 10);

    // B: pages a selection read, never searched -- and created AFTER A, so age alone would keep it.
    const DocumentIndex::Owner owner = rig.index.newOwner();
    for (int page = 0; page < 10; ++page) (void)rig.index.request(owner, selected, page, RenderPriority::Visible);
    rig.pumpIdle();
    REQUIRE(rig.index.pagesHeld(selected) == 10);
    (void)rig.search(searched, literal(QStringLiteral("lorem")));

    rig.index.setBudgetBytes(rig.index.bytesHeld(searched) + 1);
    CHECK(rig.index.pagesHeld(searched) == 10);
    CHECK(rig.index.pagesHeld(selected) == 0);
}

TEST_CASE("a document larger than the whole budget is kept while it is searched and says so", "[index][A-65]")
{
    Rig rig;
    const auto id = identity(QStringLiteral("huge.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, DocumentHandle{41}, 12);
    rig.index.setBudgetBytes(1);

    (void)rig.search(entry, literal(QStringLiteral("lorem")));
    // KEPT -- evicting the document being searched would make every search of it a first search -- and
    // counted once for the episode, not once per page.
    CHECK(rig.index.pagesHeld(entry) == 12);
    CHECK(rig.index.stats().overBudgetKept == 1);
    (void)rig.search(entry, literal(QStringLiteral("lorem")));
    CHECK(rig.probe.total() == 12);

    // A SECOND document searched now makes the first the one that goes.
    const auto other = identity(QStringLiteral("other.pdf"));
    const DocumentIndex::EntryId second = rig.index.attach(&other, DocumentHandle{42}, 3);
    (void)rig.search(second, literal(QStringLiteral("lorem")));
    CHECK(rig.index.pagesHeld(entry) == 0);
    CHECK(rig.index.pagesHeld(second) == 3);
}

TEST_CASE("the index budget's default is the search index setting's default", "[index][A-65]")
{
    // Two homes for one number is the defect convention 17 warns of; this case is what keeps them one.
    const std::size_t fromRow = static_cast<std::size_t>(core::keys::search::indexCacheMB.defaultValue) * 1024 * 1024;
    CHECK(DocumentIndex::kDefaultBudgetBytes == fromRow);
}

// =============================================================================================
// A-66 -- two windows over one file share ONE index
// =============================================================================================

TEST_CASE("two handles over one file share one entry and extract nothing twice", "[index][A-66]")
{
    Rig rig;
    const auto id = identity(QStringLiteral("shared.pdf"));
    const DocumentHandle leader{51};
    const DocumentHandle companion{52};
    const DocumentIndex::EntryId first = rig.index.attach(&id, leader, 15);
    const DocumentIndex::EntryId second = rig.index.attach(&id, companion, 15);
    CHECK(first == second);
    CHECK(rig.index.handleCount(first) == 2);
    CHECK(rig.index.entryOf(companion) == first);

    const SearchEngine::Outcome fromLeader = rig.search(first, literal(QStringLiteral("ipsum")));
    const int extracted = rig.probe.total();
    REQUIRE(extracted == 15);
    // THE SECOND WINDOW'S SEARCH extracts nothing the first already extracted, and the hits agree.
    const SearchEngine::Outcome fromCompanion = rig.search(rig.index.entryOf(companion), literal(QStringLiteral("ipsum")));
    CHECK(rig.probe.total() == extracted);
    CHECK(fromCompanion.hits == fromLeader.hits);

    // THE CONTROLS: another file is another entry; and a handle whose identity the backend cannot state
    // is an entry of its own -- sharing on a guess would share text between two files.
    const auto otherId = identity(QStringLiteral("other.pdf"));
    CHECK(rig.index.attach(&otherId, DocumentHandle{53}, 15) != first);
    const DocumentIndex::EntryId anonymousA = rig.index.attach(nullptr, DocumentHandle{54}, 15);
    const DocumentIndex::EntryId anonymousB = rig.index.attach(nullptr, DocumentHandle{55}, 15);
    CHECK(anonymousA != anonymousB);
}

TEST_CASE("the entry goes with its last handle and a leaving handle's requests are re-sent", "[index][A-66]")
{
    Rig rig;
    const auto id = identity(QStringLiteral("handover.pdf"));
    const DocumentHandle leader{61};
    const DocumentHandle companion{62};
    const DocumentIndex::EntryId entry = rig.index.attach(&id, leader, 6);
    REQUIRE(rig.index.attach(&id, companion, 6) == entry);

    // Six pages asked for while the engine thread is paused -- all through the FIRST handle.
    rig.scheduler.pause();
    const DocumentIndex::Owner owner = rig.index.newOwner();
    for (int page = 0; page < 6; ++page) (void)rig.index.request(owner, entry, page, RenderPriority::Visible);
    CHECK(rig.index.outstanding(owner) == 6);

    // THE LEADER CLOSES, in `DocumentWindow`'s order: the index first, then the scheduler's jobs on that
    // handle. Without the re-send, the six would be cancelled and the owner would wait for ever.
    rig.index.detach(leader);
    rig.scheduler.cancelDocument(leader);
    rig.scheduler.resume();
    rig.pumpIdle();
    CHECK(rig.index.pagesHeld(entry) == 6);
    CHECK(rig.index.outstanding(owner) == 0);
    for (int page = 0; page < 6; ++page) {
        CHECK(rig.probe.callsFor(leader, page) == 0);
        CHECK(rig.probe.callsFor(companion, page) == 1);
    }

    // THE LAST HANDLE: the entry and its pages go, and nothing is kept on disk or anywhere else.
    rig.index.detach(companion);
    CHECK_FALSE(rig.index.isAttached(entry));
    CHECK(rig.index.bytesHeld() == 0);
}

TEST_CASE("a page another owner still wants keeps its request when one lets go", "[index][A-66]")
{
    Rig rig;
    const auto id = identity(QStringLiteral("owners.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, DocumentHandle{71}, 4);
    rig.scheduler.pause();
    const DocumentIndex::Owner search = rig.index.newOwner();
    const DocumentIndex::Owner selection = rig.index.newOwner();
    (void)rig.index.request(search, entry, 2, RenderPriority::Background);
    (void)rig.index.request(selection, entry, 2, RenderPriority::Visible);
    // ONE request for the page, RAISED to the more urgent lane.
    CHECK(rig.scheduler.pendingCount() == 1);
    CHECK(rig.scheduler.stats().priorityRaised == 1);

    rig.index.release(search);
    CHECK(rig.scheduler.pendingCount() == 1);   // the selection still wants it
    rig.index.release(selection);
    CHECK(rig.scheduler.pendingCount() == 0);   // THE CONTROL: nobody does now
    rig.scheduler.resume();
    rig.pumpIdle();
    CHECK(rig.probe.total() == 0);
    CHECK(rig.index.pagesHeld(entry) == 0);
}

// =============================================================================================
// A-67 -- matching runs off the UI thread and off the engine thread; a newer query cancels the older
// =============================================================================================

TEST_CASE("matching runs on its own worker and a newer query cancels the older", "[index][A-67]")
{
    Rig rig;
    std::mutex mutex;
    std::set<std::thread::id> matchThreads;
    rig.engine.setMatchObserver([&](int) {
        const std::lock_guard<std::mutex> lock(mutex);
        matchThreads.insert(std::this_thread::get_id());
    });
    const auto id = identity(QStringLiteral("threads.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, DocumentHandle{81}, 40);
    (void)rig.search(entry, literal(QStringLiteral("lorem")));

    // AND THE SECOND SEARCH, whose pages are all in the index before it starts -- a different path into the
    // workers from the first's, where every page arrived during the search.
    (void)rig.search(entry, literal(QStringLiteral("dolor")));
    {
        const std::lock_guard<std::mutex> lock(mutex);
        REQUIRE_FALSE(matchThreads.empty());
        CHECK(matchThreads.count(std::this_thread::get_id()) == 0);   // not the UI thread
        for (const std::thread::id engine : rig.probe.threads) CHECK(matchThreads.count(engine) == 0);   // nor the engine's
    }

    // A NEWER QUERY: the older is started while the engine thread is paused, so it cannot have finished.
    core::text::SearchSession session(rig.engine);
    const auto otherId = identity(QStringLiteral("newer.pdf"));
    const DocumentIndex::EntryId fresh = rig.index.attach(&otherId, DocumentHandle{82}, 40);
    rig.scheduler.pause();
    const long long enqueuedBefore = rig.scheduler.stats().textEnqueued;
    int olderDelivered = 0;
    std::optional<SearchEngine::Outcome> newer;
    SearchEngine::Handlers older;
    older.finished = [&](const SearchEngine::Outcome&) { ++olderDelivered; };
    REQUIRE(session.start(fresh, literal(QStringLiteral("lorem")), older));
    const SearchEngine::SearchId olderId = session.current();
    SearchEngine::Handlers latest;
    latest.finished = [&](const SearchEngine::Outcome& o) { newer = o; };
    REQUIRE(session.start(fresh, literal(QStringLiteral("dolor")), latest));
    CHECK_FALSE(rig.engine.isRunning(olderId));
    rig.scheduler.resume();
    rig.pumpUntil([&] { return newer.has_value(); });
    rig.pumpIdle();
    REQUIRE(newer.has_value());
    CHECK(newer->hits.size() == 40);
    CHECK(olderDelivered == 0);
    // And the pages both wanted were asked of the engine ONCE: the newer asked before the older let go, so
    // the first window's 32 tickets were kept rather than cancelled and sent again -- 40 asks, not 72.
    CHECK(rig.scheduler.stats().textEnqueued - enqueuedBefore == 40);
    CHECK(rig.scheduler.stats().cancelledBeforeStart == 0);
    for (int page = 0; page < 40; ++page) CHECK(rig.probe.callsFor(DocumentHandle{82}, page) == 1);
}

TEST_CASE("a cancelled search delivers nothing and releases what it asked for", "[index][A-67]")
{
    Rig rig;
    const auto id = identity(QStringLiteral("cancel.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, DocumentHandle{91}, 100);
    rig.scheduler.pause();
    int delivered = 0;
    SearchEngine::Handlers handlers;
    handlers.finished = [&](const SearchEngine::Outcome&) { ++delivered; };
    const auto started = rig.engine.start(entry, literal(QStringLiteral("lorem")), handlers);
    REQUIRE(started);
    // AT MOST THE WINDOW is asked for at once -- a long document does not flood the bounded queue.
    CHECK(rig.scheduler.pendingCount() == static_cast<std::size_t>(SearchEngine::kExtractionWindow));
    rig.engine.cancel(started.value());
    CHECK(rig.scheduler.pendingCount() == 0);
    rig.scheduler.resume();
    rig.pumpIdle();
    CHECK(delivered == 0);
    CHECK(rig.probe.total() == 0);
    CHECK(rig.engine.stats().cancelled == 1);
}

// =============================================================================================
// A-61 -- search/maxResults bounds the list and says so
// =============================================================================================

TEST_CASE("the bound keeps the first hits in document order and says it was reached", "[index][A-61]")
{
    Rig rig;
    const auto id = identity(QStringLiteral("bounded.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, DocumentHandle{101}, 900);
    SearchQuery q = literal(QStringLiteral("lorem"));
    q.maxResults = 10;
    const SearchEngine::Outcome bounded = rig.search(entry, q);
    REQUIRE(bounded.hits.size() == 10);
    CHECK(bounded.limitReached);
    for (int i = 0; i < 10; ++i) CHECK(bounded.hits[static_cast<std::size_t>(i)].page == i);
    // AND IT STOPPED ASKING: the later pages could not change the answer.
    CHECK(rig.probe.total() < 900);

    // THE CONTROL: no bound, every hit, and nothing said.
    q.maxResults = 0;
    const SearchEngine::Outcome all = rig.search(entry, q);
    CHECK(all.hits.size() == 900);
    CHECK_FALSE(all.limitReached);
    // And a bound the hits do not reach says nothing either.
    q.maxResults = 900;
    CHECK_FALSE(rig.search(entry, q).limitReached);
}

// =============================================================================================
// A-60 -- scope: a page range and a selection, through the run
// =============================================================================================

TEST_CASE("a page range searches and extracts only its pages", "[index][A-60]")
{
    Rig rig;
    const auto id = identity(QStringLiteral("range.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, DocumentHandle{111}, 12);
    SearchQuery q = literal(QStringLiteral("lorem"));
    q.scope = SearchScope::pages(3, 5);   // pages 4 to 6, as a person counts them
    const SearchEngine::Outcome ranged = rig.search(entry, q);
    REQUIRE(ranged.hits.size() == 3);
    for (const auto& hit : ranged.hits) CHECK((hit.page >= 3 && hit.page <= 5));
    CHECK(ranged.pagesInScope == 3);
    CHECK(rig.probe.total() == 3);
    // THE CONTROL: the whole document finds hits outside the range.
    CHECK(rig.search(entry, literal(QStringLiteral("lorem"))).hits.size() == 12);
    // A range wholly outside the document searches nothing, and still FINISHES.
    q.scope = SearchScope::pages(40, 50);
    const SearchEngine::Outcome outside = rig.search(entry, q);
    CHECK(outside.hits.empty());
    CHECK(outside.pagesInScope == 0);
}

TEST_CASE("a selection scope searches its pages and only inside it", "[index][A-60]")
{
    Rig rig;
    const auto id = identity(QStringLiteral("selected-scope.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, DocumentHandle{121}, 8);
    // From the start of page 3 to the end of page 4: `lorem` is on the second line of each.
    SearchQuery q = literal(QStringLiteral("lorem"));
    q.scope = SearchScope::within(core::text::TextSelection::flow(
        core::text::TextPosition{2, 0}, core::text::TextPosition{3, core::text::TextPosition::kEndOfPage}));
    const SearchEngine::Outcome inside = rig.search(entry, q);
    REQUIRE(inside.hits.size() == 2);
    CHECK(inside.hits[0].page == 2);
    CHECK(inside.hits[1].page == 3);
    CHECK(rig.probe.total() == 2);
}

// =============================================================================================
// A-57 (the run's half), A-21 (the report), and a failed page
// =============================================================================================

TEST_CASE("an invalid query starts no run and asks the engine for nothing", "[index][A-57]")
{
    Rig rig;
    const auto id = identity(QStringLiteral("invalid.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, DocumentHandle{131}, 5);
    SearchQuery q = literal(QStringLiteral("("));
    q.flags.useRegex = true;
    bool delivered = false;
    SearchEngine::Handlers handlers;
    handlers.finished = [&](const SearchEngine::Outcome&) { delivered = true; };
    const auto started = rig.engine.start(entry, q, handlers);
    REQUIRE_FALSE(started);
    CHECK(QString(started.error().messageKey) == QStringLiteral("error.search.invalidPattern"));
    rig.pumpIdle();
    CHECK_FALSE(delivered);
    CHECK(rig.scheduler.stats().textEnqueued == 0);
    CHECK(rig.engine.stats().refused == 1);
    // THE CONTROL: the valid pattern after it searches normally.
    q.pattern = QStringLiteral("lo.em");
    CHECK(rig.search(entry, q).hits.size() == 5);
}

TEST_CASE("a search reports the pages whose glyphs have no Unicode and matches none of them", "[index][A-21]")
{
    Rig rig;
    const DocumentHandle handle{141};
    {
        const std::lock_guard<std::mutex> lock(rig.probe.mutex);
        rig.probe.unmapped.insert({handle.id, 1});
    }
    const auto id = identity(QStringLiteral("unmapped.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, handle, 3);
    // `xyz` is what the unmapped glyphs arrived claiming to be; they are stored as U+FFFD and match nothing.
    const SearchEngine::Outcome claimed = rig.search(entry, literal(QStringLiteral("xyz")));
    CHECK(claimed.hits.empty());
    CHECK(claimed.unmappedPages == std::vector<int>{1});
    CHECK(claimed.unmappedGlyphs == 3);
    // THE CONTROL: the page's mapped text is found, and the report is the same whatever was asked.
    const SearchEngine::Outcome mapped = rig.search(entry, literal(QStringLiteral("dolor")));
    CHECK(mapped.hits.size() == 3);
    CHECK(mapped.unmappedPages == std::vector<int>{1});
}

TEST_CASE("a page the engine cannot read is reported and the rest are searched", "[index][A-26]")
{
    Rig rig;
    const DocumentHandle handle{151};
    {
        const std::lock_guard<std::mutex> lock(rig.probe.mutex);
        rig.probe.failing.insert({handle.id, 2});
    }
    const auto id = identity(QStringLiteral("failing.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, handle, 4);
    const SearchEngine::Outcome outcome = rig.search(entry, literal(QStringLiteral("lorem")));
    CHECK(outcome.hits.size() == 3);
    CHECK(outcome.failedPages == std::vector<int>{2});
    CHECK(rig.index.stats().failed == 1);
    // Stored as empty, not retried: asked again it would fail again.
    (void)rig.search(entry, literal(QStringLiteral("lorem")));
    CHECK(rig.probe.callsFor(handle, 2) == 1);
}

TEST_CASE("a search of a document that closes under it is dropped silently", "[index][A-67]")
{
    Rig rig;
    const DocumentHandle handle{161};
    const auto id = identity(QStringLiteral("closing.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, handle, 50);
    rig.scheduler.pause();
    bool delivered = false;
    SearchEngine::Handlers handlers;
    handlers.finished = [&](const SearchEngine::Outcome&) { delivered = true; };
    const auto started = rig.engine.start(entry, literal(QStringLiteral("lorem")), handlers);
    REQUIRE(started);
    rig.index.detach(handle);
    rig.scheduler.cancelDocument(handle);
    CHECK_FALSE(rig.engine.isRunning(started.value()));
    rig.scheduler.resume();
    rig.pumpIdle();
    CHECK_FALSE(delivered);
}

// =============================================================================================
// Stage 5 T8 (D5-12) -- the other four content classes: A-76's run half and A-79
// =============================================================================================

namespace {

using core::text::ContentClass;
using core::text::SearchUnit;

// A FAKE READER of search units, standing where `IDocumentBackend::searchUnits` stands in the product: every
// call recorded with its thread. Three pages: a comment on page 1, the field `marmot` = `walrus` shown by
// TWO widgets (pages 1 and 2), a bookmark and an attachment; `failing` parts are refused.
struct UnitProbe {
    std::mutex mutex;
    std::map<int, int> calls;   // part -> reads
    std::set<std::thread::id> threads;
    std::set<int> failing;

    int callsFor(int part)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        const auto found = calls.find(part);
        return found == calls.end() ? 0 : found->second;
    }
    int total()
    {
        const std::lock_guard<std::mutex> lock(mutex);
        int sum = 0;
        for (const auto& [part, n] : calls) {
            (void)part;
            sum += n;
        }
        return sum;
    }
};

SearchUnit makeUnit(ContentClass contentClass, SearchUnit::Field field, const QString& text, int page,
                    const QString& locator)
{
    SearchUnit unit;
    unit.contentClass = contentClass;
    unit.field = field;
    unit.text = text;
    unit.page = page;
    unit.locator = locator;
    if (page >= 0) {
        unit.rect = core::PdfRect{core::PdfPoint{100.0, 600.0}, core::PdfSize{50.0, 20.0}};
        unit.hasRect = true;
    }
    return unit;
}

void wireUnits(Rig& rig, UnitProbe& probe)
{
    rig.index.setUnitReader([&probe](DocumentHandle, int part, std::vector<SearchUnit>& into) -> core::Status {
        {
            const std::lock_guard<std::mutex> lock(probe.mutex);
            ++probe.calls[part];
            probe.threads.insert(std::this_thread::get_id());
            if (probe.failing.count(part) != 0)
                return core::Error{core::ErrorCode::ParseFailure, QLatin1String("error.text.extractFailed")};
        }
        std::vector<SearchUnit> units;
        switch (part) {
        case 1:
            units.push_back(makeUnit(ContentClass::Comments, SearchUnit::Field::CommentContents,
                                     QStringLiteral("quokka lorem"), 1, QStringLiteral("0")));
            units.push_back(makeUnit(ContentClass::Fields, SearchUnit::Field::FieldName, QStringLiteral("marmot"), 1,
                                     QStringLiteral("marmot")));
            units.push_back(makeUnit(ContentClass::Fields, SearchUnit::Field::FieldValue, QStringLiteral("walrus lorem"),
                                     1, QStringLiteral("marmot")));
            break;
        case 2:
            // THE SAME FIELD, shown by a second widget: one field, one answer (the engine's dedupe).
            units.push_back(makeUnit(ContentClass::Fields, SearchUnit::Field::FieldName, QStringLiteral("marmot"), 2,
                                     QStringLiteral("marmot")));
            units.push_back(makeUnit(ContentClass::Fields, SearchUnit::Field::FieldValue, QStringLiteral("walrus lorem"),
                                     2, QStringLiteral("marmot")));
            break;
        case core::text::kOutlinePart:
            units.push_back(makeUnit(ContentClass::Bookmarks, SearchUnit::Field::BookmarkTitle,
                                     QStringLiteral("zeppelin lorem"), -1, QStringLiteral("0")));
            break;
        case core::text::kAttachmentsPart:
            units.push_back(makeUnit(ContentClass::Attachments, SearchUnit::Field::AttachmentName,
                                     QStringLiteral("lemur lorem.pdf"), -1, QStringLiteral("lemur lorem.pdf")));
            break;
        default:
            break;
        }
        into = std::move(units);
        return {};
    });
}

SearchQuery withClasses(const QString& pattern, bool comments, bool bookmarks, bool fields, bool attachments)
{
    SearchQuery q = literal(pattern);
    q.classes.comments = comments;
    q.classes.bookmarks = bookmarks;
    q.classes.fields = fields;
    q.classes.attachments = attachments;
    return q;
}

std::vector<ContentClass> classesOf(const SearchEngine::Outcome& outcome)
{
    std::vector<ContentClass> out;
    for (const auto& hit : outcome.hits) out.push_back(hit.contentClass());
    return out;
}

} // namespace

TEST_CASE("search units are read once per part on the engine thread and only for the classes asked",
          "[index][A-79]")
{
    Rig rig;
    UnitProbe units;
    wireUnits(rig, units);
    const DocumentHandle handle{171};
    const auto id = identity(QStringLiteral("units.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, handle, 3);

    // NO CLASS ON: page text only, and the reader is never asked -- THE CONTROL for everything below.
    (void)rig.search(entry, literal(QStringLiteral("lorem")));
    CHECK(units.total() == 0);
    CHECK(rig.index.unitsRequestedFor(entry) == 0);

    // COMMENTS ON: every page's part, once each, and neither the outline's nor the attachments'.
    (void)rig.search(entry, withClasses(QStringLiteral("quokka"), true, false, false, false));
    for (int page = 0; page < 3; ++page) CHECK(units.callsFor(page) == 1);
    CHECK(units.callsFor(core::text::kOutlinePart) == 0);
    CHECK(units.callsFor(core::text::kAttachmentsPart) == 0);
    // ON THE ENGINE THREAD: the thread the page text was extracted on, and not this one.
    {
        const std::lock_guard<std::mutex> lockUnits(units.mutex);
        const std::lock_guard<std::mutex> lockText(rig.probe.mutex);
        REQUIRE(units.threads.size() == 1);
        CHECK(*units.threads.begin() != std::this_thread::get_id());
        CHECK(rig.probe.threads.count(*units.threads.begin()) == 1);
    }
    CHECK(rig.scheduler.stats().taskEnqueued == 3);

    // THE SAME SEARCH AGAIN, AND FIELDS (the same parts): nothing is read twice.
    (void)rig.search(entry, withClasses(QStringLiteral("quokka"), true, false, true, false));
    CHECK(units.total() == 3);

    // BOOKMARKS AND ATTACHMENTS: their two parts, once each -- and again, never again.
    (void)rig.search(entry, withClasses(QStringLiteral("lorem"), true, true, true, true));
    (void)rig.search(entry, withClasses(QStringLiteral("lorem"), true, true, true, true));
    CHECK(units.callsFor(core::text::kOutlinePart) == 1);
    CHECK(units.callsFor(core::text::kAttachmentsPart) == 1);
    CHECK(units.total() == 5);
    CHECK(rig.index.unitsRequestedFor(entry) == 5);
}

TEST_CASE("each content class is found only with its switch on and in document order", "[index][A-76]")
{
    Rig rig;
    UnitProbe units;
    wireUnits(rig, units);
    const DocumentHandle handle{181};
    const auto id = identity(QStringLiteral("classes.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, handle, 3);

    // EACH WORD ONLY WITH ITS OWN SWITCH -- and THE CONTROL, every switch off, finds none of them.
    struct Row {
        QString word;
        ContentClass contentClass;
    };
    const std::vector<Row> rows{{QStringLiteral("quokka"), ContentClass::Comments},
                                {QStringLiteral("zeppelin"), ContentClass::Bookmarks},
                                {QStringLiteral("walrus"), ContentClass::Fields},
                                {QStringLiteral("lemur"), ContentClass::Attachments}};
    for (const Row& row : rows) {
        for (int on = 0; on < 5; ++on) {   // 0..3: one switch on; 4: all off
            const SearchQuery q = withClasses(row.word, on == 0, on == 1, on == 2, on == 3);
            const SearchEngine::Outcome outcome = rig.search(entry, q);
            const bool expected = on < 4 && q.classes.includes(row.contentClass);
            INFO(row.word.toStdString() << " with switch " << on);
            CHECK(outcome.hits.empty() == !expected);
            for (const auto& hit : outcome.hits) CHECK(hit.contentClass() == row.contentClass);
        }
    }

    // DOCUMENT ORDER, every switch on: the document's own units first, then page by page -- each page's
    // text before its comments and fields -- and the field shown by two widgets is ONE hit, at the first.
    const SearchEngine::Outcome all = rig.search(entry, withClasses(QStringLiteral("lorem"), true, true, true, true));
    CHECK(classesOf(all) == std::vector<ContentClass>{ContentClass::Bookmarks, ContentClass::Attachments,
                                                      ContentClass::PageText, ContentClass::PageText,
                                                      ContentClass::Comments, ContentClass::Fields,
                                                      ContentClass::PageText});
    std::vector<int> pages;
    for (const auto& hit : all.hits) pages.push_back(hit.page);
    CHECK(pages == std::vector<int>{-1, -1, 0, 1, 1, 1, 2});
    // THE DEDUPE'S CONTROL: the second widget's part WAS read, and its hit is the one dropped.
    CHECK(units.callsFor(2) == 1);

    // A PAGE RANGE searches what is ON its pages -- the comment and the field there -- and not the document's
    // bookmark or attachment.
    SearchQuery ranged = withClasses(QStringLiteral("lorem"), true, true, true, true);
    ranged.scope = SearchScope::pages(1, 2);
    CHECK(classesOf(rig.search(entry, ranged))
          == std::vector<ContentClass>{ContentClass::PageText, ContentClass::Comments, ContentClass::Fields,
                                       ContentClass::PageText});
    // A SELECTION is page text and nothing else.
    SearchQuery selected = withClasses(QStringLiteral("lorem"), true, true, true, true);
    // All of page 1: from its first position to the first of page 2.
    selected.scope = SearchScope::within(core::text::TextSelection::flow(core::text::TextPosition{1, 0},
                                                                         core::text::TextPosition{2, 0}));
    CHECK(classesOf(rig.search(entry, selected)) == std::vector<ContentClass>{ContentClass::PageText});
}

TEST_CASE("a search waiting for a page's units asks again for its text when the text is dropped meanwhile",
          "[index][A-79]")
{
    // A SLOT NEEDS TWO PIECES when comments are included -- the page's text and its unit part -- and the
    // index may drop the first while the second is still coming (a command's `Scope`, or the budget). The
    // run must ask for the dropped piece again; one that did not would wait for ever.
    Rig rig;
    std::mutex gate;
    std::condition_variable opened;
    bool open = false;
    std::atomic<int> reads{0};
    rig.index.setUnitReader([&](DocumentHandle, int, std::vector<SearchUnit>& into) -> core::Status {
        // HELD on the engine thread until the case says, so the text arrives FIRST and alone.
        std::unique_lock<std::mutex> lock(gate);
        opened.wait(lock, [&] { return open; });
        ++reads;
        into = {makeUnit(ContentClass::Comments, SearchUnit::Field::CommentContents, QStringLiteral("quokka"), 0,
                         QStringLiteral("0"))};
        return {};
    });
    const DocumentHandle handle{201};
    const auto id = identity(QStringLiteral("dropped.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, handle, 1);

    std::optional<SearchEngine::Outcome> outcome;
    SearchEngine::Handlers handlers;
    handlers.finished = [&](const SearchEngine::Outcome& o) { outcome = o; };
    REQUIRE(rig.engine.start(entry, withClasses(QStringLiteral("lorem"), true, false, false, false), handlers));

    // The text arrives; the units are held. The slot waits for its second piece.
    rig.pumpUntil([&] { return rig.index.has(entry, 0); }, 5000);
    REQUIRE(rig.index.has(entry, 0));
    CHECK_FALSE(outcome.has_value());

    // THE TEXT IS DROPPED WHILE THE SLOT WAITS -- and only then do the units arrive.
    rig.index.invalidate(entry, core::Scope::page(0));
    REQUIRE_FALSE(rig.index.has(entry, 0));
    {
        const std::lock_guard<std::mutex> lock(gate);
        open = true;
    }
    opened.notify_all();
    rig.pumpUntil([&] { return outcome.has_value(); }, 5000);
    REQUIRE(outcome.has_value());
    CHECK(outcome->hits.size() == 1);            // the page's `lorem`; the comment says `quokka`
    CHECK(rig.probe.callsFor(handle, 0) == 2);   // the text, asked again after it was dropped
    CHECK(reads.load() == 1);
}

TEST_CASE("a search unit part the reader cannot read is reported and the rest are searched", "[index][A-79]")
{
    Rig rig;
    UnitProbe units;
    {
        const std::lock_guard<std::mutex> lock(units.mutex);
        units.failing.insert(1);
    }
    wireUnits(rig, units);
    const DocumentHandle handle{191};
    const auto id = identity(QStringLiteral("unitfail.pdf"));
    const DocumentIndex::EntryId entry = rig.index.attach(&id, handle, 3);

    const SearchEngine::Outcome outcome = rig.search(entry, withClasses(QStringLiteral("walrus"), false, false, true, false));
    CHECK(outcome.failedUnitParts == 1);
    // Page 1's part failed; page 2's widget still answers -- the field is found where it still can be.
    REQUIRE(outcome.hits.size() == 1);
    CHECK(outcome.hits.front().page == 2);
    CHECK(rig.index.stats().unitsFailed == 1);
    // Stored as failed, not retried.
    (void)rig.search(entry, withClasses(QStringLiteral("walrus"), false, false, true, false));
    CHECK(units.callsFor(1) == 1);

    SECTION("and an index with NO reader delivers every part failed rather than waiting for ever")
    {
        Rig bare;
        const DocumentHandle other{192};
        const auto otherId = identity(QStringLiteral("noreader.pdf"));
        const DocumentIndex::EntryId otherEntry = bare.index.attach(&otherId, other, 2);
        const SearchEngine::Outcome none =
            bare.search(otherEntry, withClasses(QStringLiteral("lorem"), true, true, true, true));
        CHECK(none.failedUnitParts == 4);
        CHECK(none.hits.size() == 2);   // the page text is still searched
    }
}
