// core/text/DocumentIndex -- WHERE EXTRACTED TEXT LIVES: every page's model, its reading and its searchable
// form, per document, shared by every window over that document. Stage 5 T6, decision D5-4
// (`Stage5-TextSearch-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// KEYED BY `DocumentId`, NOT BY `DocumentHandle` -- and not in `RenderCache`
// ---------------------------------------------------------------------------------------------
// Stage 4 T14 measured what keying by handle costs: `window.new` opens the same file a second time, so
// one document is two handles, and the tile cache counted **1,369 cross-document evictions** between
// them. Text keyed that way would be extracted twice for one file. So a window ATTACHES its handle with
// the document's identity (`core/doc/DocumentId` -- path, size, time and content hash), and two handles
// over one identity are ONE entry: the second window extracts nothing the first already has (guide
// A-66). A handle whose identity the backend cannot state is an entry of its own, which is the honest
// fallback -- sharing on a guess would be sharing text between two files.
//
// Text is kept for minutes and tiles for a scroll, so the index has its OWN budget
// (`search/indexCacheMB`) and never touches the tile cache: 900 pages of text models inside the tile
// budget would turn every first search into an eviction storm for the page a person is reading. That
// is O7's Stage 5 revisit answered by construction (`Implementation-Plan.md` §8), and T3 measured it --
// text inserted nothing into the tile cache (guide A-30).
//
// ---------------------------------------------------------------------------------------------
// WHAT IS IN IT, AND WHEN EACH PART IS MADE
// ---------------------------------------------------------------------------------------------
// One `IndexedPage` per extracted page, holding three things, each made at the latest moment it is
// needed and on whichever thread needs it:
//
//   * the MODEL, delivered by the engine thread (D5-3) -- stored as it arrives, on the pumping thread;
//   * the READING (`TextSelection`'s `PageReading`: `LayoutAnalyzer`'s order), built on first use -- by a
//     selection on the UI thread, as T5 built it, or by a search on `SearchEngine`'s worker;
//   * one SEARCHABLE form per normalization (`SearchablePage`: D5-5's pipeline, cut at every block
//     boundary), built on the search worker the first time a search asks for those options.
//
// **Laziness is the design, not an optimisation** (guide A-62): opening a 900-page file and scrolling
// it extracts NOTHING; selecting on a page extracts that page, at `Visible`; the first SEARCH asks for
// the rest at `Background`, and the second search finds every part already made -- which is what makes
// it *instant* (the guide's §3.3). The parts built after a page arrives are guarded per page, so a
// selection and a search reaching one page at once each get the same object and nobody reads half of
// one.
//
// ---------------------------------------------------------------------------------------------
// WHO ASKS THE ENGINE, AND WHO STOPS ASKING
// ---------------------------------------------------------------------------------------------
// Every text request in the product goes through `request()`, on behalf of an OWNER -- a selection, a
// search run -- at the lane that owner needs: `Visible` for a page a person is selecting on, `Background`
// for an index build (D5-3). One page has at most one ticket, raised when a more urgent owner asks (the
// scheduler raises the queued job; the old ticket is cancelled), and it is cancelled only when its LAST
// owner lets go (`release()`): a search cancelled by the next keystroke must not throw away the page a
// selection is waiting for, and a copy's `Esc` must not stop the index build. A request is sent through
// the entry's FIRST attached handle; when that handle detaches -- the leader window closing while its
// `window.new` companion stays -- every outstanding request is re-sent through the next, before the
// closing window cancels its handle's jobs, so nobody waits for a page that will never come.
//
// ---------------------------------------------------------------------------------------------
// WHEN TEXT GOES STALE, AND WHEN IT GOES
// ---------------------------------------------------------------------------------------------
// **A mutation's `Scope` drops the pages it names** (`invalidate()`), and the next search re-extracts
// exactly those (guide A-64). At Stage 5 the only command is a rotation, whose text does not move --
// page space is unrotated (D5-2) -- so the drop is honest rather than necessary; it is here because a
// later stage's command WILL change a page's text, and the index is where text is kept, so the index is
// where it goes stale (`core/render/RenderScheduler.h`'s `TextRequest` argues why the engine side cannot
// be the place).
//
// **The budget evicts WHOLE documents, least recently SEARCHED first** (guide A-65). A document never
// searched -- only selected on -- goes first. **The document a person is searching is never evicted to
// make room for itself**: when it alone is larger than the whole budget it is kept, and the index says so
// (`Stats::overBudgetKept` and the log) -- evicting it would make every search of it a first search,
// which is the one thing the index exists to prevent. Everything is **memory only** and an entry is
// dropped when its last handle detaches: `DiskTileStore` does not ship (O7), and a text cache on disk
// would be a copy of the document's words in a place nobody chose.
//
// A page a selection holds (`DocumentText` keeps its own `shared_ptr`) outlives its eviction here -- the
// selection's own pages are its own, and bounded by what a person selected.
//
// ---------------------------------------------------------------------------------------------
// THE OTHER FOUR CONTENT CLASSES (Stage 5 T8, D5-12)
// ---------------------------------------------------------------------------------------------
// A document's SEARCH UNITS -- comments, fields, bookmarks, attachments -- are kept here beside its pages,
// for the pages' reasons: one entry per identity, so a second window reads nothing twice; memory only;
// gone with the entry and evicted with it. They are read in PARTS (`core/text/SearchUnit.h`) through
// `requestUnits()`, which mirrors `request()` -- one outstanding read per part, owners, release -- and runs
// the reader `setUnitReader()` was given as an ENGINE TASK on the render thread (`RenderScheduler`'s third
// kind of job). Each part is read ONCE: a second search including the same class asks the engine for
// nothing (guide A-79). A page's part goes stale with its page (`invalidate()`); the outline's and the
// attachments' are kept, because nothing a Stage 5 command does can change them.
//
// ---------------------------------------------------------------------------------------------
// THREADS
// ---------------------------------------------------------------------------------------------
// Every member function is called on the PUMPING thread -- the one that calls
// `RenderScheduler::deliverReady()`, which is where pages arrive -- except `IndexedPage`'s getters, which
// any thread may call. Pure `core/`: no widget, no engine, no display (`tst_index` is headless).

#ifndef RAWPAGE_CORE_TEXT_DOCUMENTINDEX_H
#define RAWPAGE_CORE_TEXT_DOCUMENTINDEX_H

#include "core/backend.h"
#include "core/doc/DocumentId.h"
#include "core/doc/Scope.h"
#include "core/render/RenderScheduler.h"
#include "core/text/PageTextModel.h"
#include "core/text/SearchUnit.h"
#include "core/text/TextNormalizer.h"
#include "core/text/TextSelection.h"

#include <QString>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace rawpage::core::text {

// ONE BLOCK OF ONE PAGE, NORMALIZED -- D5-5's pipeline over the block's reading stream. Its offsets are
// READING POSITIONS (`PageReading`'s), not model indices, because what a hit becomes is a highlight and a
// selection-shaped span, both of which speak positions. A unit the normalizer synthesized -- a word gap,
// a line break -- maps to -1.
struct SearchableBlock {
    QString text;
    std::vector<int> firstPos;   // per UTF-16 unit of `text`
    std::vector<int> lastPos;    // ... the last position of the same cluster, inclusive
};

// ONE PAGE READY TO BE SEARCHED WITH ONE SET OF OPTIONS. Cut at every BLOCK boundary, so nothing can
// match across one: a two-word query is found where a line break separates the words and not where a
// paragraph ends and the next begins (guide A-59). Immutable once built.
class SearchablePage {
public:
    static std::shared_ptr<const SearchablePage> build(const PageReading& reading, NormalizeOptions options);

    int pageIndex() const noexcept { return pageIndex_; }
    NormalizeOptions options() const noexcept { return options_; }
    const std::vector<SearchableBlock>& blocks() const noexcept { return blocks_; }
    std::size_t approxBytes() const noexcept { return bytes_; }

private:
    SearchablePage() = default;

    int pageIndex_ = -1;
    NormalizeOptions options_;
    std::vector<SearchableBlock> blocks_;
    std::size_t bytes_ = 0;
};

// ONE EXTRACTED PAGE and what has been made from it (see the header). Every getter is safe from any
// thread; the lazily-built parts are built OUTSIDE the page's lock and published under it, so a UI-thread
// selection never waits for a worker's normalization of the same page -- at worst both build and one is
// kept.
class IndexedPage {
public:
    // `failed`: the engine could not read the page. Stored as EMPTY rather than retried -- asked again it
    // would fail again, and a search waiting on it would wait for ever -- and reported by every consumer
    // (guide A-26's "fails alone").
    IndexedPage(std::shared_ptr<const PageTextModel> model, bool failed);

    int pageIndex() const noexcept { return model_->pageIndex(); }
    const PageTextModel& model() const noexcept { return *model_; }
    const std::shared_ptr<const PageTextModel>& sharedModel() const noexcept { return model_; }
    bool failed() const noexcept { return failed_; }

    std::shared_ptr<const PageReading> reading() const;
    std::shared_ptr<const SearchablePage> searchable(NormalizeOptions options) const;

    // Whether each lazy part exists yet -- what a case asserts to say the SECOND search built nothing.
    bool hasReading() const;
    bool hasSearchable(NormalizeOptions options) const;

    // An ESTIMATE of what this page holds now, from its containers' sizes. What the budget counts.
    std::size_t approxBytes() const noexcept { return bytes_.load(std::memory_order_relaxed); }

private:
    static std::size_t slot(NormalizeOptions options) noexcept
    {
        return (options.foldCase ? 1u : 0u) | (options.stripDiacritics ? 2u : 0u);
    }

    std::shared_ptr<const PageTextModel> model_;
    bool failed_ = false;

    mutable std::mutex mutex_;
    mutable std::shared_ptr<const PageReading> reading_;
    mutable std::array<std::shared_ptr<const SearchablePage>, 4> searchable_;
    mutable std::atomic<std::size_t> bytes_{0};
};

class DocumentIndex {
public:
    using EntryId = std::uint64_t;   // 0 is no entry
    using Owner = std::uint64_t;     // 0 is no owner
    using SubscriptionToken = std::uint64_t;

    // What changed, told to every subscriber on the pumping thread.
    struct Event {
        enum class Kind {
            PageArrived,        // `page` is in the index now (possibly as a failed, empty page)
            PagesInvalidated,   // a mutation's `Scope` dropped `pages`: their text may have CHANGED
            PagesEvicted,       // the budget dropped `pages`: their text is what it was, and is gone
                                //   from here only -- a selection holding them keeps them
            EntryDropped,       // the last handle detached; every page and every request went with it
            UnitsArrived,       // search-unit part `page` is in the index now (possibly failed, empty)
        };
        Kind kind = Kind::PageArrived;
        EntryId entry = 0;
        int page = -1;
        std::vector<int> pages;
    };
    using Listener = std::function<void(const Event&)>;

    // The budget before anybody sets one: `search/indexCacheMB`'s default, which `app/CompositionRoot`
    // writes over at startup and on every change. Kept equal to the row by `tst_index`; the row argues the
    // number (`text-900.pdf`'s complete index measured 835 MB at T6, guide A-63).
    static constexpr std::size_t kDefaultBudgetBytes = std::size_t{1024} * 1024 * 1024;

    explicit DocumentIndex(render::RenderScheduler& scheduler);
    ~DocumentIndex();

    DocumentIndex(const DocumentIndex&) = delete;
    DocumentIndex& operator=(const DocumentIndex&) = delete;

    // ---- Which documents ------------------------------------------------------------------------
    // `identity` may be null (the backend could not state one): the handle is then an entry of its own.
    // Attaching a handle already attached returns its entry and changes nothing.
    EntryId attach(const DocumentId* identity, DocumentHandle handle, int pageCount);
    // The entry goes with its LAST handle (`Event::EntryDropped`). Outstanding requests sent through this
    // handle are cancelled here, and re-sent through the entry's next handle when there is one.
    void detach(DocumentHandle handle);

    EntryId entryOf(DocumentHandle handle) const;
    bool isAttached(EntryId entry) const;
    int pageCount(EntryId entry) const;
    int handleCount(EntryId entry) const;

    // ---- The pages ------------------------------------------------------------------------------
    std::shared_ptr<const IndexedPage> page(EntryId entry, int page) const;
    bool has(EntryId entry, int page) const { return page >= 0 && this->page(entry, page) != nullptr; }
    int pagesHeld(EntryId entry) const;

    // ---- Asking the engine (D5-3) -----------------------------------------------------------------
    Owner newOwner() noexcept { return nextOwner_++; }
    // True when the page is ALREADY here and nothing was asked. Otherwise `owner` is recorded as wanting
    // it and the engine thread is asked at `lane` -- once per page, raised when a more urgent owner asks.
    bool request(Owner owner, EntryId entry, int page, render::RenderPriority lane);
    // Everything `owner` asked for and has not received. A page nobody else wants is cancelled.
    void release(Owner owner);
    // Pages and unit parts `owner` is still waiting for.
    std::size_t outstanding(Owner owner) const;

    // ---- The search units (Stage 5 T8, D5-12) ----------------------------------------------------
    // What reads one part, ON THE ENGINE THREAD: `app/` wires `IDocumentBackend::searchUnits`. With none
    // set, a part is delivered FAILED -- said, not dropped (the scheduler's rule for a missing extractor).
    using UnitReader = std::function<Status(DocumentHandle, int part, std::vector<SearchUnit>&)>;
    void setUnitReader(UnitReader reader);

    // A part the index holds, or null. `part` is a page index, `kOutlinePart` or `kAttachmentsPart`.
    std::shared_ptr<const SearchUnitPart> units(EntryId entry, int part) const;
    // `request()`'s contract for a part: true when it is already here, else `owner` waits for it and the
    // engine thread is asked -- once per part, raised by a more urgent ask.
    bool requestUnits(Owner owner, EntryId entry, int part, render::RenderPriority lane);
    // How many times THIS entry's unit parts were asked of the engine -- A-79 reads it.
    long long unitsRequestedFor(EntryId entry) const;

    // ---- Staleness and memory -------------------------------------------------------------------
    // Drops every stored page `scope` touches (`Scope::touchesPage`'s rule, rounded outwards).
    void invalidate(EntryId entry, const Scope& scope);

    // A search of this entry started: it is now the most recently searched (the eviction order).
    void markSearched(EntryId entry);

    void setBudgetBytes(std::size_t bytes);
    std::size_t budgetBytes() const noexcept { return budgetBytes_; }
    std::size_t bytesHeld() const;
    std::size_t bytesHeld(EntryId entry) const;
    // Evicts whole documents, least recently searched first, until the index is within its budget --
    // never `keep`. Called by the index itself as pages arrive, and by `SearchEngine` when a search ends.
    void enforceBudget(EntryId keep = 0);

    SubscriptionToken subscribe(Listener listener);
    void unsubscribe(SubscriptionToken token);

    struct Stats {
        long long requested = 0;           // `enqueueText` calls this index made
        long long stored = 0;              // pages that arrived and were kept
        long long failed = 0;              // ... of which the engine could not read
        long long pagesDropped = 0;        // by `invalidate`
        long long documentsEvicted = 0;    // by the budget
        long long overBudgetKept = 0;      // times the kept document alone was over the budget
        long long unitsRequested = 0;      // search-unit parts asked of the engine (T8)
        long long unitsStored = 0;         // ... that arrived and were kept
        long long unitsFailed = 0;         // ... of which the reader failed
    };
    Stats stats() const noexcept { return stats_; }
    // How many times THIS entry's pages were asked of the engine -- A-64 and A-66 read it.
    long long requestedFor(EntryId entry) const;

private:
    struct Pending {
        render::RenderTicket ticket;
        render::RenderPriority lane = render::RenderPriority::Background;
        DocumentHandle sentThrough;
        std::set<Owner> owners;
    };

    // A KEY FOR WHAT IS WAITED FOR: a page's text is its index (>= 0); a unit part is mapped below zero,
    // so one map of pending reads and one owner ledger serve both and `release()` has one path.
    static int unitKey(int part) noexcept { return -3 - part; }
    static bool isUnitKey(int key) noexcept { return key < 0; }
    static int partOfKey(int key) noexcept { return -3 - key; }

    // An engine task's result, written on the worker and read on the pumping thread once the scheduler
    // says the task ran -- the scheduler's own lock orders the two.
    struct UnitRead {
        EntryId entry = 0;
        int part = 0;
        std::shared_ptr<SearchUnitPart> result;
    };

    struct Entry {
        EntryId id = 0;
        DocumentId identity;          // invalid when the backend could not state one
        std::vector<DocumentHandle> handles;   // requests go through the first
        int pageCount = 0;
        std::vector<std::shared_ptr<const IndexedPage>> pages;
        std::map<int, std::shared_ptr<const SearchUnitPart>> units;   // by part
        std::map<int, Pending> pending;        // by key: a page, or `unitKey(part)`
        std::uint64_t lastSearched = 0;        // 0: never searched
        long long requested = 0;
        long long unitsRequested = 0;
    };

    Entry* find(EntryId entry);
    const Entry* find(EntryId entry) const;
    Entry* entryForHandle(DocumentHandle handle);

    void onText(const render::TextRequest& request, const render::TextResult& result);
    void onTask(render::RenderTicket ticket);
    // `key` is a page or a unit key; the right kind of job is sent for it.
    void send(Entry& entry, int key, Pending& pending, render::RenderPriority lane);
    void settle(Entry& entry, int key);   // whoever waited for `key` has it now
    void cancelTicket(render::RenderTicket ticket);
    void dropEntry(EntryId entry);
    void evict(Entry& entry);
    void notify(const Event& event);

    render::RenderScheduler& scheduler_;
    render::RenderScheduler::SubscriptionToken schedulerToken_ = 0;
    render::RenderScheduler::SubscriptionToken taskToken_ = 0;
    UnitReader unitReader_;
    std::map<std::uint64_t, UnitRead> unitReads_;   // by ticket id: the tasks in flight

    std::map<EntryId, Entry> entries_;
    std::map<std::uint64_t, EntryId> byHandle_;          // DocumentHandle::id -> entry
    std::map<Owner, std::set<std::pair<EntryId, int>>> owners_;

    std::map<SubscriptionToken, Listener> listeners_;
    SubscriptionToken nextToken_ = 1;

    EntryId nextEntry_ = 1;
    Owner nextOwner_ = 1;
    std::uint64_t searchTick_ = 0;
    std::size_t budgetBytes_ = kDefaultBudgetBytes;
    bool overBudget_ = false;   // the kept document alone is over the budget -- said once per episode
    Stats stats_;
};

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_DOCUMENTINDEX_H
