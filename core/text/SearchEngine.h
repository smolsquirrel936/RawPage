// core/text/SearchEngine -- a query matched against a document's page text, off the UI thread and off the
// engine thread. Stage 5 T6, decisions D5-3, D5-4 and D5-5 (`Stage5-TextSearch-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// TWO HALVES
// ---------------------------------------------------------------------------------------------
// **The MATCH is pure**: `compile()` turns a `SearchQuery` into something matchable, and `findOnPage()`
// finds it in one `SearchablePage` -- no thread, no index, no engine, which is what `tst_search` and the
// fuzz body (D5-21) reach. **The RUN is asynchronous**: `start()` takes a query over one
// `DocumentIndex` entry, asks the index for every page in scope the index does not hold (at
// `Background`, D5-3), matches each page on a worker of its own as it is available, and delivers the
// outcome on the PUMPING thread from `deliverReady()` -- the scheduler's contract (§5.4's flow), so a
// handler is never entered from a worker. Matching is pure CPU and needs no engine, so it runs neither on
// the UI thread nor on the engine thread (guide A-67); the engine thread's only part in a search is the
// extraction of pages the index does not hold yet.
//
// ---------------------------------------------------------------------------------------------
// WHAT MATCHES WHAT (D5-5)
// ---------------------------------------------------------------------------------------------
// The page and the query go through the SAME `TextNormalizer` options -- NFKC, simple case folding unless
// `caseSensitive`, marks stripped when `ignoreDiacritics` -- so a query typed with a ligature, a
// full-width letter or a decomposed accent finds the page's other spelling. A LITERAL query is normalized
// as a query (whitespace collapsed) and found in each block of the page; a line break inside a block is a
// space, a line-final hyphen before a lowercase letter is joined, and nothing matches across a BLOCK
// (`SearchablePage`). A REGULAR EXPRESSION is PCRE2 (`QRegularExpression`) over the same normalized
// text: its pattern is NFKC-normalized and stripped of marks like the page -- both safe on a pattern,
// whose syntax is ASCII -- and never case-folded (folding `\S` would make it `\s`); case is
// `CaseInsensitiveOption` instead. A pattern that does not compile is REFUSED with an error naming it and
// searches nothing (guide A-57): an empty result would read as *not found*, which is a wrong answer, not
// an error. A match of no characters is never a hit.
//
// **WHOLE WORDS** is a boundary test at each end of a match: a boundary is where a word character meets
// anything else, or either side of an IDEOGRAPH. A word character is a letter, a number, a mark or a
// connector (`_`). That is UAX #29's rule without a dictionary, so **in CJK each ideograph is a word**:
// `中日` with whole words finds `中日` only where neither neighbour joins it -- and since every ideograph is
// a boundary, it finds every occurrence. That is the caveat the plan records (D5-5, guide §7), stated
// here at the rule.
//
// **UNMAPPED glyphs match nothing** (D5-2): the normalizer produces no text for them, so no query --
// literal or regular, not even `.` -- can claim one. What a search REPORTS instead is that they are
// there: every page in scope holding glyphs with no Unicode is listed with its count in the outcome, and
// the find bar says so (T7). That is guide A-21's find half: *find reports it, and nothing claims a match*.
//
// ---------------------------------------------------------------------------------------------
// THE BOUND, AND WHAT IS CANCELLED
// ---------------------------------------------------------------------------------------------
// `SearchQuery::maxResults` bounds the list to the first N hits in document order, and a run that
// reaches it says so (`Outcome::limitReached`) and stops asking the engine for pages that could not
// change the answer. A run is cancelled by `cancel()`, and **a newer query cancels the older** through
// `SearchSession` (A-67): the older run delivers nothing afterwards, its matching stops between pages, and
// its extraction requests are released -- a page the index build still wants for another owner keeps its
// ticket (`DocumentIndex::release`). A run asks the engine for at most `kExtractionWindow` pages at a
// time, so a 5,000-page document never fills the scheduler's bounded queue with text nobody is looking at
// and a cancelled search leaves at most that many requests to take back.

#ifndef RAWPAGE_CORE_TEXT_SEARCHENGINE_H
#define RAWPAGE_CORE_TEXT_SEARCHENGINE_H

#include "core/Result.h"
#include "core/text/DocumentIndex.h"
#include "core/text/SearchQuery.h"
#include "core/text/TextSelection.h"

#include <QRegularExpression>
#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rawpage::core::text {

// One match: a page and the READING positions it covers, [begin, end) -- what a highlight and a selection
// already speak (`TextSelection`'s `Span`). The model characters are `PageReading::modelIndex` of them.
struct SearchHit {
    int page = -1;
    Span reading;

    friend bool operator==(const SearchHit& a, const SearchHit& b) noexcept
    {
        return a.page == b.page && a.reading == b.reading;
    }
};

// A query ready to match. Built by `SearchEngine::compile`, immutable, shared by every worker of a run.
class CompiledQuery {
public:
    const SearchQuery& query() const noexcept { return query_; }
    NormalizeOptions options() const noexcept { return query_.flags.normalizeOptions(); }
    bool isRegex() const noexcept { return query_.flags.useRegex; }
    // The normalized literal -- what a literal query looks for in a normalized page.
    const QString& literal() const noexcept { return literal_; }
    const QRegularExpression& expression() const noexcept { return expression_; }

private:
    friend class SearchEngine;
    SearchQuery query_;
    QString literal_;
    QRegularExpression expression_;
};

class SearchEngine {
public:
    // How many pages a run asks the engine for at once (the header).
    static constexpr int kExtractionWindow = 32;

    struct Config {
        // Matching threads. 0 is "automatic": half the hardware's threads, at least one and at most four
        // -- the first search of a 900-page file lays out and normalizes every page, and that is parallel
        // work; the second search is a few milliseconds on one.
        int workerCount = 0;
    };

    // What a finished run delivers.
    struct Outcome {
        std::vector<SearchHit> hits;       // document order: page, then reading position
        bool limitReached = false;         // there were more than `maxResults` (guide A-61)
        int pagesSearched = 0;
        int pagesInScope = 0;
        std::vector<int> unmappedPages;    // pages in scope with glyphs that have no Unicode (D5-2, A-21)
        int unmappedGlyphs = 0;
        std::vector<int> failedPages;      // pages the engine could not read
    };
    struct Progress {
        int pagesSearched = 0;
        int pagesInScope = 0;
        int hitsSoFar = 0;
    };
    struct Handlers {
        std::function<void(const Progress&)> progress;   // optional
        std::function<void(const Outcome&)> finished;
    };

    using SearchId = std::uint64_t;
    using WakeHandler = std::function<void()>;
    // A MEASUREMENT HOOK, AND NOTHING IN THE PRODUCT INSTALLS ONE -- `RenderScheduler::LatencyProbe`'s
    // precedent. Called ON THE WORKER for every page matched, so guide A-67 can read which thread did it.
    using MatchObserver = std::function<void(int pageIndex)>;

    explicit SearchEngine(DocumentIndex& index) : SearchEngine(index, Config{}) {}
    SearchEngine(DocumentIndex& index, Config config);
    ~SearchEngine();

    SearchEngine(const SearchEngine&) = delete;
    SearchEngine& operator=(const SearchEngine&) = delete;

    // ---- The match (pure) ------------------------------------------------------------------------
    // Refused: an empty pattern (`error.search.emptyPattern`), and a regular expression that does not
    // compile (`error.search.invalidPattern`, the pattern as its context).
    static Result<CompiledQuery> compile(const SearchQuery& query);
    // Every hit on one page, in reading order, at most `limit` (0: no bound). `scope` is honoured when it
    // is a SELECTION -- a hit must lie inside it -- and needs `reading` for that; a page range is the
    // caller's to have chosen.
    static std::vector<SearchHit> findOnPage(const CompiledQuery& query, const SearchablePage& page,
                                             const PageReading& reading, int limit = 0);

    // ---- The run (asynchronous) ------------------------------------------------------------------
    // Nothing is delivered from inside `start`: even a search whose every page is already indexed
    // delivers from the next `deliverReady()`. An error means NO run was started.
    Result<SearchId> start(DocumentIndex::EntryId entry, const SearchQuery& query, Handlers handlers);
    void cancel(SearchId id);
    bool isRunning(SearchId id) const;

    // THE PUMP. Call it on the thread that pumps `RenderScheduler::deliverReady()` -- where pages arrive
    // -- and every handler runs there. Returns how many worker results it consumed.
    int deliverReady();
    // Called FROM A WORKER, carrying nothing; must post and return. `app/` wires it as it wires the
    // scheduler's.
    void setWakeHandler(WakeHandler handler);
    void setMatchObserver(MatchObserver observer);

    // Until no matching job is queued or running. For tests and an orderly shutdown.
    bool waitForIdle(int milliseconds);

    int workerCount() const noexcept { return static_cast<int>(workers_.size()); }

    struct Stats {
        long long started = 0;
        long long finished = 0;
        long long cancelled = 0;
        long long refused = 0;          // `start` returned an error
        long long pagesMatched = 0;     // across every run, counted on delivery
    };
    Stats stats() const noexcept { return stats_; }

private:
    struct PageResult {
        int page = -1;
        std::vector<SearchHit> hits;
        int unmapped = 0;
        bool failed = false;
    };

    struct Job {
        SearchId run = 0;
        std::shared_ptr<std::atomic<bool>> cancelled;
        std::shared_ptr<const CompiledQuery> query;
        std::vector<std::shared_ptr<const IndexedPage>> pages;
        int limitPerPage = 0;
    };

    struct Completion {
        SearchId run = 0;
        std::vector<PageResult> pages;
    };

    struct Run {
        SearchId id = 0;
        DocumentIndex::EntryId entry = 0;
        DocumentIndex::Owner owner = 0;
        std::shared_ptr<const CompiledQuery> query;
        std::shared_ptr<std::atomic<bool>> cancelled;
        Handlers handlers;
        int firstPage = 0;
        int pagesInScope = 0;
        // Per page in scope, by offset from `firstPage`.
        enum PageState : char { Untouched, Waiting, Submitted, Done };
        std::vector<char> state;
        std::map<int, PageResult> results;
        int nextToAsk = 0;               // the lowest offset not yet asked for or submitted
        int waiting = 0;                 // asked of the index and not arrived -- at most the window
        int doneCount = 0;
    };

    void onIndexEvent(const DocumentIndex::Event& event);
    // Hands the run every page in scope the index holds and it has not submitted, and asks the index for
    // the next pages up to the window.
    void advance(Run& run);
    void submit(Run& run, std::vector<std::shared_ptr<const IndexedPage>> pages);
    // True when the run is over (every page done, or the limit settled), and delivers it.
    bool maybeFinish(SearchId id);
    void finish(Run& run, bool limitReached);
    void retire(SearchId id);
    void workerLoop();

    DocumentIndex& index_;
    DocumentIndex::SubscriptionToken indexToken_ = 0;

    std::map<SearchId, Run> runs_;
    SearchId nextRun_ = 1;
    Stats stats_;

    // ---- Shared with the workers, under `mutex_` ---------------------------------------------------
    mutable std::mutex mutex_;
    std::condition_variable wake_;       // a job, or stop
    std::condition_variable idle_;       // nothing queued or running
    std::deque<Job> queue_;
    std::deque<Completion> completions_;
    int running_ = 0;
    bool stopping_ = false;
    WakeHandler wakeHandler_;
    MatchObserver matchObserver_;
    std::vector<std::thread> workers_;
};

// ONE CALLER'S CURRENT SEARCH. A find bar or a search pane holds one: `start` cancels whatever it started
// before -- AFTER the new run has asked for its pages, so an index build both runs want is not cancelled
// and re-asked on every keystroke -- and the destructor cancels, so a handler can never outlive the
// object it would call (convention 10's rule, for a run instead of a signal).
class SearchSession {
public:
    explicit SearchSession(SearchEngine& engine) : engine_(engine) {}
    ~SearchSession() { cancel(); }

    SearchSession(const SearchSession&) = delete;
    SearchSession& operator=(const SearchSession&) = delete;

    // An error cancels the previous run too: its results would answer a pattern nobody is asking for.
    Result<void> start(DocumentIndex::EntryId entry, const SearchQuery& query, SearchEngine::Handlers handlers);
    void cancel();
    bool isRunning() const { return current_ != 0 && engine_.isRunning(current_); }
    SearchEngine::SearchId current() const noexcept { return current_; }

private:
    SearchEngine& engine_;
    SearchEngine::SearchId current_ = 0;
};

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_SEARCHENGINE_H
