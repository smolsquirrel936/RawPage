// core/render/RenderScheduler -- what gets rasterized, in what order, and what stops. Stage 2 T11,
// plan 5.3.
//
// ===========================================================================================
// I-05 IS CLOSED HERE, AND THE ARRANGEMENT IS **ONE RENDER THREAD WITH A QUEUE**
// ===========================================================================================
// Decision D2-9 said the tile pipeline holds no PDFium type and that `PdfiumRasterizer` is
// "thread-confined per worker". The first half stands. **The second half was measured false at
// T3** (issue I-05 in the Stage 2 guide's 8): what PDFium shares between threads is process-global
// -- its font mapper and its glyph caches -- so eight workers with a document and a page EACH
// corrupt the heap, and two are enough to do it. Confining a document to a worker confines the
// wrong thing.
//
// D2-9 named two arrangements that both keep D10's actual promise -- *the UI thread never parses or
// rasterizes* -- and left the choice to this task. **The choice is one render thread**, and it is
// made on the measurement rather than on taste:
//
//   * Eight workers behind the process-wide engine lock retire **1.05x** the tiles per second of
//     one worker (T3, `rawpage_bench_render`). The parallelism buys 5 %.
//   * Per-tile p95 is **0.532 ms against a 16.667 ms frame budget**. The engine is not the
//     bottleneck, so 5 % of it is not worth a concurrency bug.
//   * One consumer makes cancellation -- which plan 5.3 calls *the feature* -- provable rather than
//     probable, and makes PDFium's single-threadedness structural rather than a discipline every
//     future caller has to remember.
//
// **`Config::workerCount` still exists and still works**, defaulting to 1. It is kept for two
// reasons that are not hedging: T18 re-measures A2 through this pipeline and must be able to walk
// the worker count without a rebuild, and a future non-PDFium rasterizer (Stage 16's OCR is the
// nearest) is not bound by I-05. Each worker gets its OWN `IPageRasterizer` from the factory, which
// is what guide case A-45 already exercises -- and every one of them still serializes inside
// `pdf/render/PdfiumInit`'s engine lock, which is correct under either arrangement.
//
// ===========================================================================================
// DELIVERY IS ON THE PUMPING THREAD, AND NOTHING IS EVER CALLED FROM A WORKER
// ===========================================================================================
// A finished tile goes into a completion queue. `deliverReady()` -- called by the UI thread --
// drains it, inserts into the cache and calls the subscribers. A worker's only outward call is the
// WAKE handler, which takes no data and exists so the UI can post itself an event instead of
// polling; `ui/viewer/TilePresenter` (T13) wires it to a queued invocation.
//
// That is a stronger statement than "delivery is on the UI thread" and it is the one a test can
// assert: **no subscriber is ever entered from a worker thread**, which the suite checks by
// recording the thread id at every callback. It is also what makes `RenderCache` safe without a
// lock -- the cache is touched only from the pumping thread (`RenderCache.h`).
//
// Convention 10 (`CLAUDE.md`) in its threaded form: a `PageView` closed with four tiles in flight
// is Stage 1's I-12 with a thread in it. The destructor stops and JOINS before anything else is
// destroyed, and a subscription is removed by token rather than by hoping the lambda outlives
// nothing.
//
// ===========================================================================================
// STAGE 5 T2: A SECOND KIND OF JOB ON THE SAME THREAD (D5-3), AND A FOURTH LANE
// ===========================================================================================
// Text extraction is engine work, so it runs HERE -- on the one render thread, under the same
// engine lock -- as a job of its own kind beside the raster requests. A second thread that took the
// engine lock page by page would hold it for a whole page's text parse while a visible tile waited,
// and a mutex is not a priority queue: strict priority is what kept Stage 4's 900-page thumbnail
// pane from starving the page (D4-10), and a text job gets the same guarantee by being in the same
// queue. `Background`, below `Speculative`, is the lane for work nobody is watching -- an index
// build, a folder search, a comparison. A page a person is SELECTING on is asked for at `Visible`,
// because someone is waiting for it; that choice is the CALLER's (Stage 5 T5, T6), and this class
// only honours the lane it is given.
//
// **WHAT THIS DOES NOT GIVE: PREEMPTION.** A text job already inside PDFium runs to its end, as a
// speculative raster does, so one page's extraction time is the longest a visible tile can wait
// behind background work. That is the number Stage 5 T3 measures (D5-22), and why it is measured
// before any surface queues text here.
//
// What a text job is NOT: it is never looked up in or written to `RenderCache` (text is not tiles,
// and D5-4 keeps it out of the tile budget), it is never SUPERSEDED by `documentChanged()` (see
// `TextRequest`), and it is never reported to the latency probe, whose subject is the tile.

#ifndef RAWPAGE_CORE_RENDER_RENDERSCHEDULER_H
#define RAWPAGE_CORE_RENDER_RENDERSCHEDULER_H

#include "core/backend.h"
#include "core/doc/Revision.h"
#include "core/doc/Scope.h"
#include "core/render/RenderRequest.h"

#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rawpage::core::text {
class PageTextModel;
}

namespace rawpage::core::render {

class RenderCache;

// Strict priority: a visible tile always goes before a prefetched one. Prefetch CAN starve while
// the user scrolls fast, and that is correct -- speculative work that runs instead of visible work
// is the defect, not the starvation.
enum class RenderPriority {
    Visible = 0,
    Prefetch = 1,
    Speculative = 2,   // a page the user has not reached; Stage 4's thumbnails
    Background = 3,    // Stage 5 (D5-3): text work nobody is watching -- an index, a folder, a diff
};

const char* renderPriorityName(RenderPriority priority) noexcept;

// ONE PAGE'S TEXT, asked of the engine thread -- Stage 5 T2, D5-3. Deliberately smaller than a
// `RenderRequest`, and the absences are the design:
//
//   * NO `Revision`. The engine never reloads a document it opened (D3-15): an unsaved rotation
//     reaches the pixels as a render DELTA, and a page's text is in unrotated PAGE space (D5-2), so
//     what a page's text is depends on the handle and the index and on nothing a Stage 5 command
//     can change. A revision here would make every rotation throw away a 900-page index build for
//     text that did not move. When a later stage's command changes a page's CONTENT, it is
//     `core/text/DocumentIndex` (T6) that drops the page by `Scope` -- the index is where text is
//     kept, so the index is where it goes stale.
//   * NO SCALE, NO FLAGS. Text has no resolution and no layers to hide.
//
// Its equality is what coalesces two asks for one page into one extraction, as two viewports over
// one tile share one rasterization.
struct TextRequest {
    DocumentHandle document;
    int pageIndex = 0;

    bool isValid() const noexcept { return document.isValid() && pageIndex >= 0; }

    friend bool operator==(const TextRequest& a, const TextRequest& b) noexcept
    {
        return a.document == b.document && a.pageIndex == b.pageIndex;
    }
    friend bool operator!=(const TextRequest& a, const TextRequest& b) noexcept { return !(a == b); }
};

struct TextRequestHash {
    std::size_t operator()(const TextRequest& request) const noexcept
    {
        return std::hash<std::uint64_t>{}(request.document.id)
               ^ (static_cast<std::size_t>(request.pageIndex) * 0x9e3779b97f4a7c15ULL);
    }
};

// What a text job delivers: the page's model, SHARED and immutable -- `core/text/DocumentIndex`
// keeps it and a selection reads it, and neither should pay for a copy -- or the error that says
// why there is none. A FAILURE IS DELIVERED, unlike a raster's: a tile that fails is asked for
// again at the next paint, but a caller waiting on a page's text has nothing to repaint, and a
// failure it never heard about is a selection that never starts (guide case A-26).
using TextResult = Result<std::shared_ptr<const text::PageTextModel>>;

// A ticket for one caller's interest in one tile. Deliberately NOT the request: two viewports over
// one tile coalesce into one rasterization and get two handles, so that either can cancel without
// cancelling the other's work.
struct RenderTicket {
    std::uint64_t id = 0;

    bool isValid() const noexcept { return id != 0; }
    friend bool operator==(RenderTicket a, RenderTicket b) noexcept { return a.id == b.id; }
    friend bool operator!=(RenderTicket a, RenderTicket b) noexcept { return a.id != b.id; }
};

class RenderScheduler {
public:
    // One rasterizer per worker (see the header). A factory rather than an instance, because
    // sharing one across threads is exactly what I-05 says must not happen.
    using RasterizerFactory = std::function<std::unique_ptr<IPageRasterizer>()>;

    // Stage 5 T2 (D5-3). One per worker, made ON the worker, for the rasterizer's reason. Null is
    // legitimate -- every suite and bench before Stage 5 builds a scheduler with no text -- and a
    // text job run without one is DELIVERED as a failure naming the interface, never dropped.
    using TextExtractorFactory = std::function<std::unique_ptr<ITextExtractor>()>;

    // Called from `deliverReady()`, on the pumping thread, never from a worker -- the tile rule.
    using TextReadyHandler = std::function<void(const TextRequest&, const TextResult&)>;

    // Called from `deliverReady()`, on the pumping thread, never from a worker.
    using ReadyHandler = std::function<void(const RenderRequest&, const RasterImage&)>;

    // Called FROM A WORKER, carrying nothing: "there is something to deliver". It must be safe to
    // call from another thread and must not touch the scheduler -- post an event and return.
    using WakeHandler = std::function<void()>;

    using SubscriptionToken = std::uint64_t;

    // A MEASUREMENT HOOK, AND NOTHING IN THE PRODUCT INSTALLS ONE -- S4 T14 (D4-16). Called from
    // `deliverReady()`, once per delivered tile, with the lane it waited in and the milliseconds
    // from `enqueue()` to that delivery: the QUEUED LATENCY, which is the quantity A2's product
    // figure names (`Tech-Stack-Plan.md` §3.3) and the one T2's bench measured by timing its own
    // enqueues.
    //
    // It exists because T14 cannot do that. T2 issued the request pattern itself and could stamp
    // it; T14's subject is *the shipped widgets issuing it*, and `ui/viewer/TilePresenter` calls
    // `enqueue()` where no bench can reach. Without a hook here the figure the verdict rests on
    // could only ever be measured from a harness that is not the product -- which is the exact
    // gap between the probe and the product that D2-1 exists to stop us confusing.
    //
    // This is the same call T11 made for its lane counters and for the same stated reason: a claim
    // that cannot be observed from outside gets an observation rather than a comment. Null by
    // default, checked once per delivered tile, and it costs one `steady_clock::now()` per enqueue
    // whether or not anybody is listening -- which is under the noise of the rasterization it is
    // timing and is not worth a branch to avoid.
    using LatencyProbe = std::function<void(RenderPriority lane, double queuedMs)>;

    struct Config {
        // ONE. See the header for the measurement behind it. `performance/renderWorkers` (T15)
        // writes this, and until that row exists this is the default's only home.
        int workerCount = 1;

        // A bound on the queue, so a runaway prefetch cannot grow it without limit. When it is
        // full the LOWEST-priority pending job is dropped to make room, because refusing the new
        // one would mean refusing a visible tile in favour of a speculative one.
        std::size_t maxPending = 4096;
    };

    // `cache` may be null. When it is not, the scheduler looks a request up before queueing it and
    // inserts every completed tile -- both on the pumping thread, which is what keeps the cache
    // single-threaded.
    //
    // Two constructors rather than one with a defaulted `Config`: a nested class's default member
    // initializers are not available inside the enclosing class's own definition, so
    // `Config config = {}` in this signature does not compile -- a C++ rule that costs one line
    // here and reads as a puzzle if it is met without the explanation.
    RenderScheduler(RasterizerFactory factory, TextExtractorFactory textFactory, RenderCache* cache,
                    Config config);
    RenderScheduler(RasterizerFactory factory, RenderCache* cache, Config config)
        : RenderScheduler(std::move(factory), nullptr, cache, config)
    {
    }
    RenderScheduler(RasterizerFactory factory, RenderCache* cache)
        : RenderScheduler(std::move(factory), nullptr, cache, Config{})
    {
    }
    ~RenderScheduler();

    RenderScheduler(const RenderScheduler&) = delete;
    RenderScheduler& operator=(const RenderScheduler&) = delete;

    // An invalid request is refused with an invalid ticket rather than queued: D2-12's `isValid()`
    // exists so a request with no real `Revision` cannot become a cache key, and queueing one would
    // spend a rasterization to produce a tile nothing can ever find again.
    RenderTicket enqueue(const RenderRequest& request, RenderPriority priority);

    // ONE PAGE'S TEXT, on the engine thread (Stage 5 T2, D5-3), delivered through
    // `subscribeText()`'s handlers from `deliverReady()`. Same queue, same strict priority, same
    // tickets, same cancellation as a tile: a text job not started is removed and the extractor is
    // never entered, a running one is marked and dropped on delivery. An equal request coalesces,
    // and a `Visible` ask raises a queued `Background` one -- the person who started selecting on a
    // page the index build had not reached yet must not wait behind the other 899.
    RenderTicket enqueueText(const TextRequest& request, RenderPriority priority);

    // Drops this caller's interest. The job goes only when NO ticket is left on it -- two viewports
    // over one tile must not have one of them cancel the other's pixels.
    //
    // A job that has not started is removed from the queue and the rasterizer is never entered
    // (guide case A-36). A job already running cannot be stopped -- `FPDF_RenderPageBitmap` has no
    // cancellation -- so it is marked and its result is DROPPED on delivery rather than cached,
    // which is plan 5.3's own wording.
    void cancel(RenderTicket ticket);
    void cancelAll();

    // Everything of this document, in all THREE states: queued (removed), running (marked, and
    // dropped on delivery) and FINISHED BUT NOT YET DELIVERED (removed from the completion queue).
    // The third is what makes "cancel, drain, `RenderCache::dropDocument()`, close" leave nothing
    // behind -- without it the next `deliverReady()` cached the finished tiles of a closed
    // document, which is Stage 2 issue I-35.
    void cancelDocument(DocumentHandle document);

    // A mutation landed. One call rather than three, so the three things that must happen together
    // cannot drift apart: the cache is invalidated, the new revision is recorded, and every request
    // in flight or in the queue for the touched scope is superseded. A result that arrives for a
    // superseded request is dropped and never cached -- A-38's second half, which is how a stale
    // tile reaches the screen.
    void documentChanged(DocumentHandle document, const Scope& scope, Revision newRevision);

    // Removed by TOKEN, never by hoping. Convention 10's threaded form: a subscriber that outlives
    // its object is Stage 1's I-12, and a worker thread makes it a use-after-free rather than a
    // repaint of a dead widget.
    SubscriptionToken subscribeReady(ReadyHandler handler);
    void unsubscribeReady(SubscriptionToken token);
    SubscriptionToken subscribeText(TextReadyHandler handler);
    void unsubscribeText(SubscriptionToken token);
    void setWakeHandler(WakeHandler handler);
    void setLatencyProbe(LatencyProbe probe);

    // THE PUMP. Call it on the UI thread. Returns how many results it delivered.
    int deliverReady();

    // `platform::IPowerSession` throttling (plan 5.3): the number of workers allowed to run,
    // clamped to [1, workerCount]. T12 wires it to `performance/throttleOnBattery`. Never zero --
    // a viewer that stops rendering on battery is a viewer that looks broken.
    void setActiveWorkerLimit(int limit);
    int activeWorkerLimit() const;

    // Stops workers from STARTING anything. Existing work finishes. It is how guide case A-36
    // proves a cancelled request is never started rather than merely never delivered, and it is
    // also what a window that has just been minimized would use.
    void pause();
    void resume();
    bool isPaused() const;

    // Blocks until nothing is queued or running, or until the timeout. For tests and for an orderly
    // shutdown; it is NOT a way to render synchronously. "Running" lasts until the worker has
    // RETURNED from the wake handler, not merely until the rasterizer has: a caller that returns from
    // here has seen every wake its work produced.
    bool waitForIdle(int milliseconds);

    std::size_t pendingCount() const;
    std::size_t runningCount() const;

    // THE GENERAL COUNTERS COUNT BOTH KINDS OF JOB -- `enqueued`, the lanes, `coalesced`,
    // `started`, `completed`, the cancellations, `droppedOverflow`, `failed`, `delivered`,
    // `priorityRaised` -- because each describes the one queue and the one engine thread, which
    // is what a starvation question is about. `servedFromCache` and `droppedSuperseded` are TILE
    // counters: a text job is never cached and never superseded. The `text*` counters are the text
    // subset, so the tile figures are the general ones minus them.
    struct Stats {
        long long enqueued = 0;

        // WHICH LANE EACH ENQUEUE WENT INTO -- S4 T11, and their sum is `enqueued`. Added for the
        // A2 named work this stage inherited (`Tech-Stack-Plan.md` §3.3, *A2 at Stage 4*): a window
        // that does not have focus asks for its viewport at `Prefetch` rather than at `Visible`,
        // and that change is invisible from outside -- the tiles still arrive, the picture is
        // identical, and only the queue they waited in differs. A counter is the only thing that
        // can say so, which is why the claim gets one rather than a comment.
        long long enqueuedVisible = 0;
        long long enqueuedPrefetch = 0;
        long long enqueuedSpeculative = 0;
        long long enqueuedBackground = 0;   // Stage 5 T2
        long long coalesced = 0;          // an equal request joined a job already queued or running
        long long servedFromCache = 0;
        long long started = 0;
        long long completed = 0;
        long long cancelledBeforeStart = 0;   // A-36's number: the rasterizer was never entered
        long long droppedCancelled = 0;       // finished, but nobody wants it any more
        long long droppedSuperseded = 0;      // finished at a revision that has since moved
        long long droppedOverflow = 0;        // the queue was full and this was the lowest priority
        long long delivered = 0;
        long long failed = 0;
        long long priorityRaised = 0;     // a prefetch job that a visible request joined

        // Stage 5 T2 -- the text subset of the counters above.
        long long textEnqueued = 0;
        long long textCompleted = 0;      // the extractor returned, success or not
        long long textFailed = 0;         // ... and it was a failure, which is still DELIVERED
        long long textDelivered = 0;
    };
    Stats stats() const;

private:
    enum class JobKind { Raster, Text };

    struct Job {
        JobKind kind = JobKind::Raster;
        RenderRequest request;   // meaningful for a Raster job
        TextRequest text;        // meaningful for a Text job
        RenderPriority priority = RenderPriority::Visible;
        std::uint64_t sequence = 0;

        // When `enqueue()` took it. The clock is monotonic because the quantity is a duration and
        // a wall clock can step backwards over one.
        std::chrono::steady_clock::time_point enqueuedAt{};
        std::vector<std::uint64_t> tickets;
        bool started = false;
        bool cancelled = false;
        bool superseded = false;
    };

    struct Completion {
        JobKind kind = JobKind::Raster;
        RenderRequest request;
        RasterImage image;
        // A Text completion's payload: the model or the error. A text completion is ALWAYS usable
        // -- a failure is delivered (see `TextResult`).
        TextRequest text;
        std::shared_ptr<TextResult> textResult;
        bool usable = false;
        // A hit is delivered through the same path as a render so that no subscriber has to know
        // where a tile came from -- but it must not be written BACK into the cache, or the
        // insertion and eviction counts O7 is measured on would grow with every lookup.
        bool fromCache = false;

        // Carried from the job so `deliverReady()` can report the queued latency without looking
        // the job up -- by then it has been erased. A CACHE HIT carries its own enqueue instant and
        // the lane it was asked for in, so the probe sees hits and renders alike: a hit is a
        // delivered tile, and excluding it would report the pipeline as slower than it is.
        RenderPriority priority = RenderPriority::Visible;
        std::chrono::steady_clock::time_point enqueuedAt{};
    };

    void workerLoop(int index);
    // Requires the lock. Forgets a job -- its tickets and its coalescing key, by kind -- and erases
    // it. It does not touch `pending_`; the caller knows whether the job is in it.
    void forgetJobLocked(std::unordered_map<std::uint64_t, Job>::iterator job);
    // Requires the lock. Returns 0 when nothing may start.
    std::uint64_t selectNextLocked(int workerIndex) const;
    void dropLowestPriorityLocked();
    bool isSupersededLocked(const RenderRequest& request) const;

    RasterizerFactory factory_;
    TextExtractorFactory textFactory_;
    RenderCache* cache_ = nullptr;
    Config config_;

    mutable std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::condition_variable idle_;

    std::unordered_map<std::uint64_t, Job> jobs_;             // by job id
    std::unordered_map<RenderRequest, std::uint64_t> byRequest_;
    std::unordered_map<TextRequest, std::uint64_t, TextRequestHash> byText_;
    std::unordered_map<std::uint64_t, std::uint64_t> ticketToJob_;
    std::vector<std::uint64_t> pending_;                       // job ids, chosen by priority
    std::size_t running_ = 0;
    // Workers between finishing a tile and returning from the wake handler. Counted for
    // `waitForIdle()` only, so `runningCount()` keeps meaning "inside the rasterizer".
    std::size_t handingOff_ = 0;

    std::deque<Completion> completed_;
    std::unordered_map<std::uint64_t, Revision> revisions_;

    std::uint64_t nextJobId_ = 1;
    std::uint64_t nextTicketId_ = 1;
    std::uint64_t nextSequence_ = 1;
    SubscriptionToken nextToken_ = 1;

    std::vector<std::pair<SubscriptionToken, ReadyHandler>> subscribers_;
    std::vector<std::pair<SubscriptionToken, TextReadyHandler>> textSubscribers_;
    WakeHandler wake_;
    LatencyProbe latencyProbe_;

    std::vector<std::thread> workers_;
    int activeWorkerLimit_ = 1;
    bool paused_ = false;
    bool stopping_ = false;
    Stats stats_;
};

} // namespace rawpage::core::render

#endif // RAWPAGE_CORE_RENDER_RENDERSCHEDULER_H
