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

#ifndef RAWPAGE_CORE_RENDER_RENDERSCHEDULER_H
#define RAWPAGE_CORE_RENDER_RENDERSCHEDULER_H

#include "core/backend.h"
#include "core/doc/Revision.h"
#include "core/doc/Scope.h"
#include "core/render/RenderRequest.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rawpage::core::render {

class RenderCache;

// Strict priority: a visible tile always goes before a prefetched one. Prefetch CAN starve while
// the user scrolls fast, and that is correct -- speculative work that runs instead of visible work
// is the defect, not the starvation.
enum class RenderPriority {
    Visible = 0,
    Prefetch = 1,
    Speculative = 2,   // a page the user has not reached; Stage 4's thumbnails
};

const char* renderPriorityName(RenderPriority priority) noexcept;

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

    // Called from `deliverReady()`, on the pumping thread, never from a worker.
    using ReadyHandler = std::function<void(const RenderRequest&, const RasterImage&)>;

    // Called FROM A WORKER, carrying nothing: "there is something to deliver". It must be safe to
    // call from another thread and must not touch the scheduler -- post an event and return.
    using WakeHandler = std::function<void()>;

    using SubscriptionToken = std::uint64_t;

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
    RenderScheduler(RasterizerFactory factory, RenderCache* cache, Config config);
    RenderScheduler(RasterizerFactory factory, RenderCache* cache)
        : RenderScheduler(std::move(factory), cache, Config{})
    {
    }
    ~RenderScheduler();

    RenderScheduler(const RenderScheduler&) = delete;
    RenderScheduler& operator=(const RenderScheduler&) = delete;

    // An invalid request is refused with an invalid ticket rather than queued: D2-12's `isValid()`
    // exists so a request with no real `Revision` cannot become a cache key, and queueing one would
    // spend a rasterization to produce a tile nothing can ever find again.
    RenderTicket enqueue(const RenderRequest& request, RenderPriority priority);

    // Drops this caller's interest. The job goes only when NO ticket is left on it -- two viewports
    // over one tile must not have one of them cancel the other's pixels.
    //
    // A job that has not started is removed from the queue and the rasterizer is never entered
    // (guide case A-36). A job already running cannot be stopped -- `FPDF_RenderPageBitmap` has no
    // cancellation -- so it is marked and its result is DROPPED on delivery rather than cached,
    // which is plan 5.3's own wording.
    void cancel(RenderTicket ticket);
    void cancelAll();
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
    void setWakeHandler(WakeHandler handler);

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
    // shutdown; it is NOT a way to render synchronously.
    bool waitForIdle(int milliseconds);

    std::size_t pendingCount() const;
    std::size_t runningCount() const;

    struct Stats {
        long long enqueued = 0;
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
    };
    Stats stats() const;

private:
    struct Job {
        RenderRequest request;
        RenderPriority priority = RenderPriority::Visible;
        std::uint64_t sequence = 0;
        std::vector<std::uint64_t> tickets;
        bool started = false;
        bool cancelled = false;
        bool superseded = false;
    };

    struct Completion {
        RenderRequest request;
        RasterImage image;
        bool usable = false;
        // A hit is delivered through the same path as a render so that no subscriber has to know
        // where a tile came from -- but it must not be written BACK into the cache, or the
        // insertion and eviction counts O7 is measured on would grow with every lookup.
        bool fromCache = false;
    };

    void workerLoop(int index);
    // Requires the lock. Returns 0 when nothing may start.
    std::uint64_t selectNextLocked(int workerIndex) const;
    void dropLowestPriorityLocked();
    bool isSupersededLocked(const RenderRequest& request) const;

    RasterizerFactory factory_;
    RenderCache* cache_ = nullptr;
    Config config_;

    mutable std::mutex mutex_;
    std::condition_variable workAvailable_;
    std::condition_variable idle_;

    std::unordered_map<std::uint64_t, Job> jobs_;             // by job id
    std::unordered_map<RenderRequest, std::uint64_t> byRequest_;
    std::unordered_map<std::uint64_t, std::uint64_t> ticketToJob_;
    std::vector<std::uint64_t> pending_;                       // job ids, chosen by priority
    std::size_t running_ = 0;

    std::deque<Completion> completed_;
    std::unordered_map<std::uint64_t, Revision> revisions_;

    std::uint64_t nextJobId_ = 1;
    std::uint64_t nextTicketId_ = 1;
    std::uint64_t nextSequence_ = 1;
    SubscriptionToken nextToken_ = 1;

    std::vector<std::pair<SubscriptionToken, ReadyHandler>> subscribers_;
    WakeHandler wake_;

    std::vector<std::thread> workers_;
    int activeWorkerLimit_ = 1;
    bool paused_ = false;
    bool stopping_ = false;
    Stats stats_;
};

} // namespace rawpage::core::render

#endif // RAWPAGE_CORE_RENDER_RENDERSCHEDULER_H
