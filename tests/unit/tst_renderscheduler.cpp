// tst_renderscheduler -- `core/render/RenderScheduler`. Stage 2 T11, guide cases A-36, A-37, A-38
// and the `core/` half of A-39.
//
// The rasterizer here is a COUNTING FAKE, and that is what makes A-36 assertable at all: "the
// rasterizer is never entered" is a statement about a call that did not happen, and observing that
// no image arrived would be satisfied equally by a scheduler that rendered the tile and threw it
// away -- which is precisely the behaviour plan 5.3 is written against.
//
// WHAT THESE CASES DO NOT COVER, named at the assertion (convention 19): they drive the scheduler
// directly, so they prove the DESTINATION. They do not prove that scrolling reaches it -- that is
// manual case **M-04**, and it is manual because perceived smoothness has no offscreen proxy.

#include <catch2/catch_test_macros.hpp>

#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"
#include "core/text/PageTextModel.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using rawpage::core::DocumentHandle;
using rawpage::core::Error;
using rawpage::core::ErrorCode;
using rawpage::core::IPageRasterizer;
using rawpage::core::PixelFormat;
using rawpage::core::RasterImage;
using rawpage::core::Result;
using rawpage::core::Revision;
using rawpage::core::Scope;
using rawpage::core::render::RenderCache;
using rawpage::core::render::RenderPriority;
using rawpage::core::render::RenderRequest;
using rawpage::core::render::RenderScheduler;
using rawpage::core::render::RenderTicket;
using rawpage::core::render::TileRect;

namespace {

constexpr DocumentHandle kDocA{1};
constexpr DocumentHandle kDocB{2};

RenderRequest requestFor(int page, int tileX = 0, Revision revision = Revision::initial(),
                         DocumentHandle document = kDocA)
{
    RenderRequest request;
    request.document = document;
    request.pageIndex = page;
    request.tile = TileRect{tileX, 0, 8, 8};
    request.deviceScale = 1.0;
    request.revision = revision;
    return request;
}

// Shared between the fake rasterizers the factory hands out, because with more than one worker
// each thread gets its own instance and the counts have to be the suite's, not the instance's.
struct RasterizerProbe {
    std::mutex mutex;
    std::condition_variable entered;
    std::atomic<int> calls{0};
    std::atomic<int> instances{0};
    std::atomic<bool> fail{false};
    std::set<std::thread::id> callingThreads;
    std::vector<RenderRequest> seen;

    // A latch, so a case can hold a tile INSIDE the rasterizer and act while it is genuinely in
    // flight. Without it the two branches that matter most -- a job cancelled or superseded while
    // running -- are unreachable from a test: everything else either has not started or has
    // already finished, and both of those take a different path.
    std::atomic<bool> hold{false};
    std::mutex holdMutex;
    std::condition_variable released;

    void waitWhileHeld()
    {
        std::unique_lock<std::mutex> lock(holdMutex);
        released.wait(lock, [this] { return !hold.load(); });
    }

    void release()
    {
        {
            std::lock_guard<std::mutex> lock(holdMutex);
            hold.store(false);
        }
        released.notify_all();
    }

    // Blocks until the rasterizer has been entered `count` times, or gives up. Returns false on
    // the timeout so a case fails rather than hanging a CI run.
    bool waitForCalls(int count, int milliseconds)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        while (calls.load() < count) {
            if (std::chrono::steady_clock::now() > deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    void note(const RenderRequest& request)
    {
        std::lock_guard<std::mutex> lock(mutex);
        callingThreads.insert(std::this_thread::get_id());
        seen.push_back(request);
    }
};

class FakeRasterizer final : public IPageRasterizer {
public:
    explicit FakeRasterizer(RasterizerProbe& probe) : probe_(probe) { ++probe_.instances; }

    Result<RasterImage> raster(const RenderRequest& request) override
    {
        ++probe_.calls;
        probe_.note(request);
        probe_.entered.notify_all();
        probe_.waitWhileHeld();
        if (probe_.fail.load())
            return Error{ErrorCode::IoFailure, QLatin1String("error.render.tileFailed")};

        RasterImage image;
        image.width = request.tile.width;
        image.height = request.tile.height;
        image.bytesPerLine = request.tile.width * 4;
        image.format = PixelFormat::Bgra8888;
        // The page index is written into the pixels, so a case can tell WHICH tile came back --
        // otherwise every fake tile is interchangeable and a scheduler that delivered the wrong one
        // would pass.
        image.pixels = QByteArray(image.bytesPerLine * image.height,
                                  static_cast<char>('A' + (request.pageIndex % 26)));
        return image;
    }

private:
    RasterizerProbe& probe_;
};

RenderScheduler::RasterizerFactory factoryFor(RasterizerProbe& probe)
{
    return [&probe] { return std::unique_ptr<IPageRasterizer>(new FakeRasterizer(probe)); };
}

} // namespace

TEST_CASE("render scheduler never starts a cancelled request", "[core][render]")
{
    // A-36, the stage's headline behaviour. The pool is PAUSED, so the cancel provably beats the
    // worker rather than racing it -- a case that enqueued and cancelled quickly would pass on a
    // fast machine and fail on a loaded CI runner for no reason anyone could act on.
    RasterizerProbe probe;
    RenderScheduler scheduler(factoryFor(probe), nullptr);
    scheduler.pause();

    const RenderTicket ticket = scheduler.enqueue(requestFor(0), RenderPriority::Visible);
    REQUIRE(ticket.isValid());
    CHECK(scheduler.pendingCount() == 1);

    scheduler.cancel(ticket);
    CHECK(scheduler.pendingCount() == 0);

    scheduler.resume();
    CHECK(scheduler.waitForIdle(2000));

    // THE ASSERTION THAT MATTERS: the rasterizer was never entered. Not "no image arrived".
    CHECK(probe.calls.load() == 0);
    CHECK(scheduler.deliverReady() == 0);
    CHECK(scheduler.stats().cancelledBeforeStart == 1);

    SECTION("THE CONTROL: an uncancelled request IS started")
    {
        // Without it this case is satisfied by a scheduler whose workers never run at all.
        scheduler.enqueue(requestFor(1), RenderPriority::Visible);
        REQUIRE(scheduler.waitForIdle(2000));
        CHECK(probe.calls.load() == 1);
        CHECK(scheduler.deliverReady() == 1);
    }
}

TEST_CASE("render scheduler coalesces duplicate requests into one rasterization", "[core][render]")
{
    // A-37. Two viewports over one tile is one rasterization.
    RasterizerProbe probe;
    RenderScheduler scheduler(factoryFor(probe), nullptr);
    scheduler.pause();

    const RenderRequest tile = requestFor(4);
    const RenderTicket first = scheduler.enqueue(tile, RenderPriority::Prefetch);
    const RenderTicket second = scheduler.enqueue(tile, RenderPriority::Visible);
    REQUIRE(first.isValid());
    REQUIRE(second.isValid());
    CHECK(first != second);
    CHECK(scheduler.pendingCount() == 1);
    CHECK(scheduler.stats().coalesced == 1);

    // A visible request landing on a queued prefetch RAISES it, or a tile that scrolled into view
    // waits behind every prefetch already queued and the page fills in from the wrong end.
    CHECK(scheduler.stats().priorityRaised == 1);

    SECTION("cancelling ONE holder does not take the other's pixels")
    {
        scheduler.cancel(first);
        CHECK(scheduler.pendingCount() == 1);
        scheduler.resume();
        REQUIRE(scheduler.waitForIdle(2000));
        CHECK(probe.calls.load() == 1);
        CHECK(scheduler.deliverReady() == 1);
    }

    SECTION("cancelling BOTH removes the job before it starts")
    {
        scheduler.cancel(first);
        scheduler.cancel(second);
        CHECK(scheduler.pendingCount() == 0);
        scheduler.resume();
        REQUIRE(scheduler.waitForIdle(2000));
        CHECK(probe.calls.load() == 0);
    }
}

TEST_CASE("render scheduler delivers on the pumping thread and nowhere else", "[core][render]")
{
    // A-38's first half, asserted in the strongest form the design allows: no subscriber is ever
    // ENTERED from a worker thread. "Delivery is on the UI thread" is not directly checkable
    // offscreen -- there is no UI thread -- but "the thread that called deliverReady()" is.
    RasterizerProbe probe;
    RenderScheduler scheduler(factoryFor(probe), nullptr);

    std::set<std::thread::id> deliveringThreads;
    std::atomic<int> wakes{0};
    std::set<std::thread::id> wakingThreads;
    std::mutex guard;

    const auto token = scheduler.subscribeReady(
        [&](const RenderRequest&, const RasterImage&) {
            std::lock_guard<std::mutex> lock(guard);
            deliveringThreads.insert(std::this_thread::get_id());
        });
    scheduler.setWakeHandler([&] {
        std::lock_guard<std::mutex> lock(guard);
        wakingThreads.insert(std::this_thread::get_id());
        ++wakes;
    });

    for (int page = 0; page < 6; ++page)
        scheduler.enqueue(requestFor(page), RenderPriority::Visible);
    REQUIRE(scheduler.waitForIdle(4000));

    CHECK(scheduler.deliverReady() == 6);
    {
        std::lock_guard<std::mutex> lock(guard);
        REQUIRE(deliveringThreads.size() == 1);
        CHECK(*deliveringThreads.begin() == std::this_thread::get_id());
        // And the wake handler DID come from somewhere else -- the control for the line above,
        // without which the case would pass against a scheduler that rendered on the caller's
        // thread and never used a worker at all.
        REQUIRE_FALSE(wakingThreads.empty());
        CHECK(wakingThreads.find(std::this_thread::get_id()) == wakingThreads.end());
    }
    CHECK(wakes.load() > 0);

    SECTION("an unsubscribed handler is not called again")
    {
        // Convention 10's threaded form: a subscriber that outlived its object would be a
        // use-after-free rather than a repaint of a dead widget.
        scheduler.unsubscribeReady(token);
        std::size_t before = 0;
        {
            std::lock_guard<std::mutex> lock(guard);
            before = deliveringThreads.size();
        }
        scheduler.enqueue(requestFor(9), RenderPriority::Visible);
        REQUIRE(scheduler.waitForIdle(2000));
        CHECK(scheduler.deliverReady() == 1);
        std::lock_guard<std::mutex> lock(guard);
        CHECK(deliveringThreads.size() == before);
    }
}

TEST_CASE("render scheduler drops a superseded result rather than caching it", "[core][render]")
{
    // A-38's second half, which the guide marks S1: this is how a stale tile reaches the screen.
    RenderCache cache;
    RasterizerProbe probe;
    RenderScheduler scheduler(factoryFor(probe), &cache);
    scheduler.pause();

    const Revision r1 = Revision::initial();
    const Revision r2 = r1.next();
    scheduler.enqueue(requestFor(2, 0, r1), RenderPriority::Visible);
    CHECK(scheduler.pendingCount() == 1);

    // The mutation lands while the tile is still queued. One call does all three things -- the
    // cache is invalidated, the revision is recorded and the queued request is superseded -- so
    // they cannot drift apart.
    scheduler.documentChanged(kDocA, Scope::page(2), r2);
    CHECK(scheduler.pendingCount() == 0);

    scheduler.resume();
    REQUIRE(scheduler.waitForIdle(2000));
    CHECK(probe.calls.load() == 0);
    CHECK(scheduler.deliverReady() == 0);
    CHECK_FALSE(cache.lookup(requestFor(2, 0, r1)).has_value());
    CHECK_FALSE(cache.lookup(requestFor(2, 0, r2)).has_value());

    SECTION("a result that finished before the mutation is dropped too, not delivered")
    {
        // The narrowest window in the class: the tile completed while the mutation was being
        // applied and is sitting in the completion queue. Delivering it puts a pre-mutation pixel
        // on screen after the mutation, which is the exact failure `Revision` exists to prevent.
        RenderScheduler second(factoryFor(probe), &cache);
        second.enqueue(requestFor(5, 0, r2), RenderPriority::Visible);
        REQUIRE(second.waitForIdle(2000));
        second.documentChanged(kDocA, Scope::page(5), r2.next());
        CHECK(second.deliverReady() == 0);
    }

    SECTION("A MUTATION THAT LANDS WHILE THE TILE IS IN THE RASTERIZER drops the result")
    {
        // The branch the other sections cannot reach: the request has STARTED, so it is not in the
        // queue to be removed and not in the completion queue to be filtered. It cannot be stopped
        // -- `FPDF_RenderPageBitmap` has no cancellation -- so the only defence is to throw the
        // result away on the way back, and this is the case that proves that happens.
        RasterizerProbe held;
        held.hold.store(true);
        RenderCache freshCache;
        RenderScheduler running(factoryFor(held), &freshCache);

        running.enqueue(requestFor(6, 0, r1), RenderPriority::Visible);
        REQUIRE(held.waitForCalls(1, 2000));
        CHECK(running.runningCount() == 1);

        running.documentChanged(kDocA, Scope::page(6), r2);
        held.release();
        REQUIRE(running.waitForIdle(2000));

        CHECK(running.stats().droppedSuperseded == 1);
        CHECK(running.deliverReady() == 0);
        CHECK(freshCache.entryCount() == 0);
    }

    SECTION("A TILE CANCELLED WHILE IN THE RASTERIZER is dropped rather than cached")
    {
        // Plan 5.3's own wording -- "if already running is abandoned on delivery rather than
        // cached". Caching it is the tempting optimization and is wrong twice: it spends memory on
        // a tile nobody asked for, and after a mutation it is a stale tile a later lookup would
        // happily serve.
        RasterizerProbe held;
        held.hold.store(true);
        RenderCache freshCache;
        RenderScheduler running(factoryFor(held), &freshCache);

        const RenderTicket ticket = running.enqueue(requestFor(8, 0, r1), RenderPriority::Visible);
        REQUIRE(held.waitForCalls(1, 2000));
        running.cancel(ticket);
        held.release();
        REQUIRE(running.waitForIdle(2000));

        CHECK(running.stats().droppedCancelled == 1);
        CHECK(running.stats().cancelledBeforeStart == 0);   // it HAD started -- the other branch
        CHECK(running.deliverReady() == 0);
        CHECK(freshCache.entryCount() == 0);
    }

    SECTION("a request for another document is untouched by the mutation")
    {
        RenderScheduler other(factoryFor(probe), &cache);
        other.pause();
        other.enqueue(requestFor(2, 0, r1, kDocB), RenderPriority::Visible);
        other.documentChanged(kDocA, Scope::wholeDocument(), r2);
        CHECK(other.pendingCount() == 1);
    }
}

TEST_CASE("render scheduler forgets a FINISHED tile of a document that was cancelled", "[core][render]")
{
    // Stage 2 issue I-35, found by M-09's growth clause: the product kept ~4 MB per round of ten
    // documents. A tile that had FINISHED but was still in the completion queue when its tab closed
    // survived `cancelDocument()` -- which cleared the queue and marked the running jobs, and never
    // looked at `completed_` -- so the next `deliverReady()` wrote it into the cache AFTER
    // `dropDocument()` had emptied it, keyed by a handle nothing would ask for again. It then sat in
    // the 256 MiB budget until eviction, charged to no open document.
    //
    // The sequence below is `ui/shell/DocumentWindow`'s destructor, step for step: cancel, drain,
    // drop, and only then does the pump run again. WHAT THIS DOES NOT COVER (convention 19): it
    // drives the pump by calling `deliverReady()`, so it proves the scheduler forgets the tile; that
    // a closed tab in the running product no longer leaves memory behind is the M-09 re-run's.
    RenderCache cache;
    RasterizerProbe probe;
    RenderScheduler scheduler(factoryFor(probe), &cache);

    scheduler.enqueue(requestFor(0, 0), RenderPriority::Visible);
    scheduler.enqueue(requestFor(0, 8), RenderPriority::Visible);
    // THE CONTROL, finished in the same queue: a tile of a document that stays open. Without it this
    // case is satisfied by a scheduler that drops every completion it holds (convention 22).
    scheduler.enqueue(requestFor(0, 0, Revision::initial(), kDocB), RenderPriority::Visible);
    REQUIRE(scheduler.waitForIdle(2000));
    REQUIRE(probe.calls.load() == 3);   // all three finished, and none has been delivered

    scheduler.cancelDocument(kDocA);
    REQUIRE(scheduler.waitForIdle(2000));
    cache.dropDocument(kDocA);

    std::vector<RenderRequest> delivered;
    const RenderScheduler::SubscriptionToken token = scheduler.subscribeReady(
        [&](const RenderRequest& request, const RasterImage&) { delivered.push_back(request); });
    CHECK(scheduler.deliverReady() == 1);
    scheduler.unsubscribeReady(token);

    // THE ASSERTION THAT MATTERS: nothing of the closed document reached the cache.
    CHECK_FALSE(cache.lookup(requestFor(0, 0)).has_value());
    CHECK_FALSE(cache.lookup(requestFor(0, 8)).has_value());
    CHECK(scheduler.stats().droppedCancelled == 2);

    // And the open document's tile was delivered and cached as if nothing had happened.
    REQUIRE(delivered.size() == 1);
    CHECK(delivered.front().document == kDocB);
    CHECK(cache.lookup(requestFor(0, 0, Revision::initial(), kDocB)).has_value());
    CHECK(cache.entryCount() == 1);
}

TEST_CASE("render scheduler WAKES for a tile it served from the cache", "[core][render]")
{
    // M-01's finding, reproduced without a monitor. Every case above drains the completion queue by
    // CALLING `deliverReady()` -- a back door no running product has (convention 19). In the product
    // the ONLY thing that posts `deliverReady()` is the wake handler, and the wake handler is called
    // from the worker loop. A batch of requests that are ALL cache hits therefore starts no worker,
    // wakes nobody, and strands its completions in the queue: the page goes blank and stays blank.
    RenderCache cache;
    RasterizerProbe probe;
    RenderScheduler scheduler(factoryFor(probe), &cache);

    std::atomic<int> wakes{0};
    scheduler.setWakeHandler([&] { ++wakes; });

    const RenderRequest tile = requestFor(3);

    // The MISS, which is the control: this one must wake, or the case below proves nothing about
    // the cache path -- it would merely be observing a wake handler that never fires at all.
    scheduler.enqueue(tile, RenderPriority::Visible);
    REQUIRE(scheduler.waitForIdle(2000));
    REQUIRE(wakes.load() > 0);
    REQUIRE(scheduler.deliverReady() == 1);

    // The HIT. Nothing else is enqueued, so nothing else can wake the pump on its behalf.
    const int before = wakes.load();
    scheduler.enqueue(tile, RenderPriority::Visible);
    REQUIRE(scheduler.stats().servedFromCache == 1);
    CHECK(wakes.load() > before);
}

TEST_CASE("render scheduler is not idle until the wake handler has returned", "[core][render]")
{
    // The race behind CI run 34772420519, made deterministic. The case above failed its control
    // under ASan -- `waitForIdle()` returned, and `wakes` was still 0 -- because the worker announced
    // idle BEFORE calling the handler, and a slow build lands in that gap. A handler that sleeps
    // before counting widens the gap to 100 ms, so a scheduler that announces idle early fails this
    // on every run rather than on the occasional one. THE CONTROL is the same count taken with no
    // wait at all: it must still be 0, or the sleep did not open the gap and this proves nothing.
    RasterizerProbe probe;
    RenderScheduler scheduler(factoryFor(probe), nullptr);

    std::atomic<bool> entered{false};
    std::atomic<int> wakes{0};
    scheduler.setWakeHandler([&] {
        entered = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++wakes;
    });

    scheduler.enqueue(requestFor(1), RenderPriority::Visible);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!entered.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    REQUIRE(entered.load());
    CHECK(wakes.load() == 0);   // the control: the handler is inside its sleep

    REQUIRE(scheduler.waitForIdle(2000));
    CHECK(wakes.load() == 1);
}

TEST_CASE("render scheduler serves a cached tile without rasterizing it", "[core][render]")
{
    RenderCache cache;
    RasterizerProbe probe;
    RenderScheduler scheduler(factoryFor(probe), &cache);

    const RenderRequest tile = requestFor(3);
    scheduler.enqueue(tile, RenderPriority::Visible);
    REQUIRE(scheduler.waitForIdle(2000));
    CHECK(scheduler.deliverReady() == 1);
    CHECK(probe.calls.load() == 1);

    // The second ask is served from the cache -- and still through `deliverReady()`, never handed
    // back from inside `enqueue()`, because a subscriber called from there would re-enter a paint
    // handler that has not finished asking for its tiles.
    scheduler.enqueue(tile, RenderPriority::Visible);
    CHECK(probe.calls.load() == 1);
    CHECK(scheduler.stats().servedFromCache == 1);
    CHECK(scheduler.deliverReady() == 1);

    // And it was not written back into the cache a second time: the insertion count is what O7's
    // eviction rate is measured against, and re-inserting on every lookup would make that number a
    // function of how often tiles were looked at.
    CHECK(cache.stats().insertions == 1);
}

TEST_CASE("render scheduler survives being destroyed with work in flight", "[core][render]")
{
    // The `core/` half of A-39. The widget half -- a `PageView` closed while four tiles are in
    // flight -- is T13's, in `tests/ui/`; what is provable here is that the scheduler itself joins
    // its workers before any member it hands them is destroyed. Fifty rounds, because a use-after-
    // free on shutdown is a race and one round proves very little.
    RasterizerProbe probe;
    for (int round = 0; round < 50; ++round) {
        RenderCache cache;
        auto scheduler = std::make_unique<RenderScheduler>(factoryFor(probe), &cache);
        scheduler->subscribeReady([](const RenderRequest&, const RasterImage&) {});
        for (int page = 0; page < 8; ++page)
            scheduler->enqueue(requestFor(page, round), RenderPriority::Visible);
        // Deliberately NOT waiting for idle: the point is to destroy it mid-flight.
        scheduler.reset();
    }
    SUCCEED("50 rounds of destroy-with-work-in-flight completed");
}

TEST_CASE("render scheduler refuses a request that could never be a cache key", "[core][render]")
{
    // D2-12 from the scheduler's side: a request with no real `Revision` would be rasterized once
    // and then missed on every lookup for ever, so the work is refused rather than wasted.
    RasterizerProbe probe;
    RenderScheduler scheduler(factoryFor(probe), nullptr);

    RenderRequest noRevision = requestFor(0);
    noRevision.revision = Revision::invalid();
    CHECK_FALSE(scheduler.enqueue(noRevision, RenderPriority::Visible).isValid());

    RenderRequest emptyTile = requestFor(0);
    emptyTile.tile = TileRect{0, 0, 0, 0};
    CHECK_FALSE(scheduler.enqueue(emptyTile, RenderPriority::Visible).isValid());

    CHECK(scheduler.waitForIdle(1000));
    CHECK(probe.calls.load() == 0);
}

TEST_CASE("render scheduler runs visible work before prefetched work", "[core][render]")
{
    RasterizerProbe probe;
    RenderScheduler scheduler(factoryFor(probe), nullptr);
    scheduler.pause();

    // Queued worst-first, so a FIFO scheduler would produce the opposite order and this case would
    // fail rather than agreeing with the implementation by accident.
    scheduler.enqueue(requestFor(0), RenderPriority::Speculative);
    scheduler.enqueue(requestFor(1), RenderPriority::Prefetch);
    scheduler.enqueue(requestFor(2), RenderPriority::Visible);
    scheduler.resume();
    REQUIRE(scheduler.waitForIdle(3000));

    std::lock_guard<std::mutex> lock(probe.mutex);
    REQUIRE(probe.seen.size() == 3);
    CHECK(probe.seen[0].pageIndex == 2);
    CHECK(probe.seen[1].pageIndex == 1);
    CHECK(probe.seen[2].pageIndex == 0);
}

TEST_CASE("render scheduler reports a failed tile rather than delivering an empty one",
          "[core][render]")
{
    // An empty image is indistinguishable from a blank page once it is in a cache -- the same
    // argument `tests/render/tst_rasterizer.cpp` records for the rasterizer's own refusals.
    RasterizerProbe probe;
    probe.fail.store(true);
    RenderCache cache;
    RenderScheduler scheduler(factoryFor(probe), &cache);

    scheduler.enqueue(requestFor(0), RenderPriority::Visible);
    REQUIRE(scheduler.waitForIdle(2000));
    CHECK(scheduler.deliverReady() == 0);
    CHECK(scheduler.stats().failed == 1);
    CHECK(cache.entryCount() == 0);
}

TEST_CASE("render scheduler gives each worker its own rasterizer", "[core][render]")
{
    // I-05's surviving half of D2-9, and the property guide case A-45 exercises against the real
    // engine: an `IPageRasterizer` is never shared between threads. The SHIPPED worker count is 1
    // -- see the header for the measurement -- so this case sets it explicitly rather than relying
    // on a default that says the opposite of what it is testing.
    RasterizerProbe probe;
    RenderScheduler::Config config;
    config.workerCount = 4;
    {
        RenderScheduler scheduler(factoryFor(probe), nullptr, config);
        for (int page = 0; page < 40; ++page)
            scheduler.enqueue(requestFor(page), RenderPriority::Visible);
        REQUIRE(scheduler.waitForIdle(5000));
    }
    CHECK(probe.instances.load() == 4);
    CHECK(probe.calls.load() == 40);

    SECTION("the default is ONE worker, which is the arrangement I-05 forced")
    {
        RasterizerProbe single;
        RenderScheduler scheduler(factoryFor(single), nullptr);
        scheduler.enqueue(requestFor(0), RenderPriority::Visible);
        REQUIRE(scheduler.waitForIdle(2000));
        CHECK(single.instances.load() == 1);

        std::lock_guard<std::mutex> lock(single.mutex);
        CHECK(single.callingThreads.size() == 1);
    }

    SECTION("the battery throttle clamps to at least one worker, never to zero")
    {
        // A viewer that stopped rendering on battery is a viewer that looks broken.
        RenderScheduler scheduler(factoryFor(probe), nullptr, config);
        scheduler.setActiveWorkerLimit(0);
        CHECK(scheduler.activeWorkerLimit() == 1);
        scheduler.setActiveWorkerLimit(99);
        CHECK(scheduler.activeWorkerLimit() == 4);
        scheduler.setActiveWorkerLimit(2);
        CHECK(scheduler.activeWorkerLimit() == 2);
        scheduler.enqueue(requestFor(0), RenderPriority::Visible);
        CHECK(scheduler.waitForIdle(2000));
    }
}

TEST_CASE("render scheduler bounds its queue by dropping the most speculative work",
          "[core][render]")
{
    RasterizerProbe probe;
    RenderScheduler::Config config;
    config.maxPending = 4;
    RenderScheduler scheduler(factoryFor(probe), nullptr, config);
    scheduler.pause();

    for (int page = 0; page < 4; ++page)
        scheduler.enqueue(requestFor(page), RenderPriority::Speculative);
    CHECK(scheduler.pendingCount() == 4);

    // The queue is full and a VISIBLE tile arrives. Refusing it would leave the user watching the
    // page in front of them stay blank while the machine rendered pages they are not looking at.
    const RenderTicket visible = scheduler.enqueue(requestFor(99), RenderPriority::Visible);
    CHECK(visible.isValid());
    CHECK(scheduler.pendingCount() == 4);
    CHECK(scheduler.stats().droppedOverflow == 1);

    scheduler.resume();
    REQUIRE(scheduler.waitForIdle(3000));
    std::lock_guard<std::mutex> lock(probe.mutex);
    REQUIRE_FALSE(probe.seen.empty());
    CHECK(probe.seen.front().pageIndex == 99);
}

// =============================================================================================
// STAGE 5 T2 -- THE TEXT JOB (D5-3): one engine thread, two kinds of job, one queue
// =============================================================================================
// A COUNTING FAKE EXTRACTOR, for the rasterizer fake's reason: "the extractor is never entered" is
// a statement about a call that did not happen. It can share an ORDER log with a logging
// rasterizer, so a case can say which of a tile and a text job went first -- A-29's claim.
//
// WHAT THESE CASES DO NOT COVER (convention 19): the real engine. They prove the queue's rules;
// that PDFium's text is extracted on this thread through the shipped adapter is
// `tests/render/tst_extract.cpp` (guide case A-24's engine half). And WHICH lane a selection or a
// search asks in is the CALLER's (Stage 5 T5, T6) -- these cases choose the lane themselves.

namespace {

struct ExtractorProbe {
    std::mutex mutex;
    std::atomic<int> calls{0};
    std::atomic<bool> fail{false};
    std::set<std::thread::id> callingThreads;
    std::vector<int> pages;
};

// One log both fakes write to, so an ordering between a tile and a text job is observable.
struct OrderLog {
    std::mutex mutex;
    std::vector<std::string> entries;

    void add(std::string entry)
    {
        std::lock_guard<std::mutex> lock(mutex);
        entries.push_back(std::move(entry));
    }
};

class FakeExtractor final : public rawpage::core::ITextExtractor {
public:
    FakeExtractor(ExtractorProbe& probe, OrderLog* log) : probe_(probe), log_(log) {}

    rawpage::core::Status extractPage(DocumentHandle, int pageIndex,
                                      rawpage::core::text::PageTextModel& into) override
    {
        ++probe_.calls;
        {
            std::lock_guard<std::mutex> lock(probe_.mutex);
            probe_.callingThreads.insert(std::this_thread::get_id());
            probe_.pages.push_back(pageIndex);
        }
        if (log_ != nullptr) log_->add("text:" + std::to_string(pageIndex));
        if (probe_.fail.load())
            return Error{ErrorCode::ParseFailure, QLatin1String("error.text.extractFailed"),
                         QString::number(pageIndex)};
        rawpage::core::text::PageTextModel model(pageIndex);
        rawpage::core::text::TextChar c;
        c.codepoint = static_cast<char32_t>(0x61 + (pageIndex % 26));
        model.append(c);
        into = std::move(model);
        return {};
    }

private:
    ExtractorProbe& probe_;
    OrderLog* log_;
};

class LoggingRasterizer final : public IPageRasterizer {
public:
    explicit LoggingRasterizer(OrderLog& log) : log_(log) {}
    Result<RasterImage> raster(const RenderRequest& request) override
    {
        log_.add("tile:" + std::to_string(request.pageIndex));
        RasterImage image;
        image.width = request.tile.width;
        image.height = request.tile.height;
        image.bytesPerLine = request.tile.width * 4;
        image.pixels = QByteArray(image.bytesPerLine * image.height, '\0');
        return image;
    }

private:
    OrderLog& log_;
};

RenderScheduler::TextExtractorFactory extractorFor(ExtractorProbe& probe, OrderLog* log = nullptr)
{
    return [&probe, log] {
        return std::unique_ptr<rawpage::core::ITextExtractor>(new FakeExtractor(probe, log));
    };
}

RenderScheduler::RasterizerFactory loggingFactory(OrderLog& log)
{
    return [&log] { return std::unique_ptr<IPageRasterizer>(new LoggingRasterizer(log)); };
}

using rawpage::core::render::TextRequest;
using rawpage::core::render::TextResult;

TextRequest textFor(int page, DocumentHandle document = kDocA)
{
    return TextRequest{document, page};
}

} // namespace

TEST_CASE("render scheduler extracts text on its worker and delivers it on the pumping thread",
          "[core][render][text]")
{
    // A-24's headless half: the extractor is entered on the worker and never on this thread, and
    // the model comes back on this thread through `deliverReady()` -- the tile rule, for text.
    RasterizerProbe rasters;
    ExtractorProbe probe;
    RenderScheduler scheduler(factoryFor(rasters), extractorFor(probe), nullptr,
                              RenderScheduler::Config{});

    std::vector<int> delivered;
    std::set<std::thread::id> deliveringThreads;
    scheduler.subscribeText([&](const TextRequest& request, const TextResult& result) {
        deliveringThreads.insert(std::this_thread::get_id());
        REQUIRE(result);
        REQUIRE(result.value() != nullptr);
        CHECK(result.value()->pageIndex() == request.pageIndex);
        delivered.push_back(request.pageIndex);
    });

    for (int page = 0; page < 5; ++page)
        REQUIRE(scheduler.enqueueText(textFor(page), RenderPriority::Background).isValid());
    REQUIRE(scheduler.waitForIdle(3000));
    CHECK(scheduler.deliverReady() == 5);

    CHECK(delivered.size() == 5);
    REQUIRE(probe.callingThreads.size() == 1);
    CHECK(*probe.callingThreads.begin() != std::this_thread::get_id());
    REQUIRE(deliveringThreads.size() == 1);
    CHECK(*deliveringThreads.begin() == std::this_thread::get_id());

    const auto stats = scheduler.stats();
    CHECK(stats.enqueuedBackground == 5);
    CHECK(stats.textEnqueued == 5);
    CHECK(stats.textCompleted == 5);
    CHECK(stats.textDelivered == 5);
    CHECK(stats.textFailed == 0);
    // No tile was asked for, so the rasterizer was never entered -- text work is not tile work.
    CHECK(rasters.calls.load() == 0);
}

TEST_CASE("render scheduler runs a visible tile before background text queued ahead of it",
          "[core][render][text]")
{
    // A-29's queue half: strict priority across the two kinds of job. The pool is PAUSED so the
    // order the jobs START in is decided by the queue alone, not by a race with the worker.
    OrderLog log;
    ExtractorProbe probe;
    RenderScheduler scheduler(loggingFactory(log), extractorFor(probe, &log), nullptr,
                              RenderScheduler::Config{});
    scheduler.pause();

    for (int page = 0; page < 3; ++page)
        scheduler.enqueueText(textFor(page), RenderPriority::Background);
    scheduler.enqueue(requestFor(7), RenderPriority::Speculative);
    scheduler.enqueue(requestFor(9), RenderPriority::Visible);

    scheduler.resume();
    REQUIRE(scheduler.waitForIdle(3000));
    scheduler.deliverReady();

    std::lock_guard<std::mutex> lock(log.mutex);
    REQUIRE(log.entries.size() == 5);
    CHECK(log.entries[0] == "tile:9");        // Visible, although enqueued LAST
    CHECK(log.entries[1] == "tile:7");        // Speculative, above Background
    CHECK(log.entries[2] == "text:0");        // then the background text, FIFO
    CHECK(log.entries[3] == "text:1");
    CHECK(log.entries[4] == "text:2");
}

TEST_CASE("render scheduler raises a background text job a visible ask joins", "[core][render][text]")
{
    // D5-3's promise to a selection: the page a person is dragging over must not wait behind an
    // index build that happened to queue it at Background.
    OrderLog log;
    ExtractorProbe probe;
    RenderScheduler scheduler(loggingFactory(log), extractorFor(probe, &log), nullptr,
                              RenderScheduler::Config{});
    scheduler.pause();

    for (int page = 0; page < 4; ++page)
        scheduler.enqueueText(textFor(page), RenderPriority::Background);
    const auto selection = scheduler.enqueueText(textFor(3), RenderPriority::Visible);
    REQUIRE(selection.isValid());
    CHECK(scheduler.pendingCount() == 4);          // coalesced, not a fifth job
    CHECK(scheduler.stats().coalesced == 1);
    CHECK(scheduler.stats().priorityRaised == 1);

    int deliveries = 0;
    scheduler.subscribeText([&](const TextRequest&, const TextResult&) { ++deliveries; });
    scheduler.resume();
    REQUIRE(scheduler.waitForIdle(3000));
    scheduler.deliverReady();

    CHECK(probe.calls.load() == 4);                // page 3 extracted ONCE for two asks
    CHECK(deliveries == 4);
    std::lock_guard<std::mutex> lock(log.mutex);
    REQUIRE_FALSE(log.entries.empty());
    CHECK(log.entries.front() == "text:3");        // raised to the front of the queue

    SECTION("THE CONTROL: without the visible ask, page 3 goes last")
    {
        // Asserted on a fresh scheduler, because the log above already holds this run's order.
        OrderLog plainLog;
        ExtractorProbe plainProbe;
        RenderScheduler plain(loggingFactory(plainLog), extractorFor(plainProbe, &plainLog), nullptr,
                              RenderScheduler::Config{});
        plain.pause();
        for (int page = 0; page < 4; ++page)
            plain.enqueueText(textFor(page), RenderPriority::Background);
        plain.resume();
        REQUIRE(plain.waitForIdle(3000));
        std::lock_guard<std::mutex> plainLock(plainLog.mutex);
        REQUIRE(plainLog.entries.size() == 4);
        CHECK(plainLog.entries.back() == "text:3");
    }
}

TEST_CASE("render scheduler never enters the extractor for a cancelled text job", "[core][render][text]")
{
    // A-36's rule, for text: not "no model arrived" but "the extractor was never called".
    RasterizerProbe rasters;
    ExtractorProbe probe;
    RenderScheduler scheduler(factoryFor(rasters), extractorFor(probe), nullptr,
                              RenderScheduler::Config{});
    scheduler.pause();

    const auto kept = scheduler.enqueueText(textFor(0), RenderPriority::Background);
    const auto dropped = scheduler.enqueueText(textFor(1), RenderPriority::Background);
    scheduler.enqueueText(textFor(0, kDocB), RenderPriority::Background);
    scheduler.enqueueText(textFor(1, kDocB), RenderPriority::Background);
    REQUIRE(kept.isValid());
    scheduler.cancel(dropped);
    // A document's text jobs go with its tiles -- `close()`'s protocol is cancel, drain, close, and
    // a text job left behind would be inside the engine on a document being destroyed.
    scheduler.cancelDocument(kDocB);
    CHECK(scheduler.pendingCount() == 1);

    scheduler.resume();
    REQUIRE(scheduler.waitForIdle(3000));
    CHECK(scheduler.deliverReady() == 1);
    CHECK(probe.calls.load() == 1);
    std::lock_guard<std::mutex> lock(probe.mutex);
    REQUIRE(probe.pages.size() == 1);
    CHECK(probe.pages.front() == 0);
    CHECK(scheduler.stats().cancelledBeforeStart == 3);
}

TEST_CASE("render scheduler DELIVERS a failed extraction rather than dropping it", "[core][render][text]")
{
    // A caller waiting on a page's text has nothing to repaint -- a failure it never heard about is
    // a selection that never starts (guide case A-26).
    RasterizerProbe rasters;
    ExtractorProbe probe;
    probe.fail.store(true);
    RenderScheduler scheduler(factoryFor(rasters), extractorFor(probe), nullptr,
                              RenderScheduler::Config{});
    std::vector<QString> errors;
    scheduler.subscribeText([&](const TextRequest&, const TextResult& result) {
        REQUIRE_FALSE(result);
        errors.push_back(result.error().messageKey);
    });
    scheduler.enqueueText(textFor(2), RenderPriority::Visible);
    REQUIRE(scheduler.waitForIdle(3000));
    CHECK(scheduler.deliverReady() == 1);
    REQUIRE(errors.size() == 1);
    CHECK(errors.front() == QStringLiteral("error.text.extractFailed"));
    CHECK(scheduler.stats().textFailed == 1);

    SECTION("and a scheduler built with NO extractor says which interface is missing")
    {
        RenderScheduler bare(factoryFor(rasters), nullptr);
        std::vector<QString> contexts;
        bare.subscribeText([&](const TextRequest&, const TextResult& result) {
            REQUIRE_FALSE(result);
            CHECK(result.error().code == ErrorCode::Unavailable);
            contexts.push_back(result.error().context);
        });
        bare.enqueueText(textFor(0), RenderPriority::Background);
        REQUIRE(bare.waitForIdle(3000));
        CHECK(bare.deliverReady() == 1);
        REQUIRE(contexts.size() == 1);
        CHECK(contexts.front() == QStringLiteral("ITextExtractor"));
    }
}

TEST_CASE("render scheduler keeps text out of the tile cache and out of supersession",
          "[core][render][text]")
{
    // D5-4: text is never a tile, so the cache sees neither a lookup nor an insertion for it. And a
    // text job carries no revision, so a mutation does not throw it away (`TextRequest`'s note).
    RenderCache cache;
    RasterizerProbe rasters;
    ExtractorProbe probe;
    RenderScheduler scheduler(factoryFor(rasters), extractorFor(probe), &cache,
                              RenderScheduler::Config{});
    scheduler.pause();

    scheduler.enqueueText(textFor(0), RenderPriority::Background);
    scheduler.enqueueText(textFor(1), RenderPriority::Background);
    scheduler.documentChanged(kDocA, Scope::page(0), Revision::initial().next());
    CHECK(scheduler.pendingCount() == 2);

    SECTION("the text jobs survive the change and never touch the cache")
    {
        scheduler.resume();
        REQUIRE(scheduler.waitForIdle(3000));
        CHECK(scheduler.deliverReady() == 2);
        CHECK(probe.calls.load() == 2);
        CHECK(cache.stats().lookups == 0);
        CHECK(cache.stats().insertions == 0);
        CHECK(scheduler.stats().droppedSuperseded == 0);
    }

    SECTION("THE CONTROL: the same kind of change DOES supersede a queued tile")
    {
        scheduler.enqueue(requestFor(0, 0, Revision::initial().next()), RenderPriority::Visible);
        CHECK(scheduler.pendingCount() == 3);
        scheduler.documentChanged(kDocA, Scope::page(0), Revision::initial().next().next());
        CHECK(scheduler.pendingCount() == 2);
        scheduler.resume();
        REQUIRE(scheduler.waitForIdle(3000));
    }
}

TEST_CASE("render scheduler refuses an invalid text request by ticket", "[core][render][text]")
{
    RasterizerProbe rasters;
    ExtractorProbe probe;
    RenderScheduler scheduler(factoryFor(rasters), extractorFor(probe), nullptr,
                              RenderScheduler::Config{});
    CHECK_FALSE(scheduler.enqueueText(TextRequest{DocumentHandle{}, 0}, RenderPriority::Visible).isValid());
    CHECK_FALSE(scheduler.enqueueText(TextRequest{kDocA, -1}, RenderPriority::Visible).isValid());
    CHECK(scheduler.pendingCount() == 0);
    CHECK(scheduler.stats().textEnqueued == 0);
}

TEST_CASE("render scheduler delivers a bypassing tile without looking it up or keeping it",
          "[core][render][text]")
{
    // Stage 5 T3, for D5-4: a comparison's 72-dpi rasters are CONSUMED, not shown, and must not
    // enter the tile budget a person's page lives in -- nor appear in the cache's figures O7 is
    // read from.
    using rawpage::core::render::CachePolicy;
    RenderCache cache;
    RasterizerProbe probe;
    RenderScheduler scheduler(factoryFor(probe), &cache);

    int delivered = 0;
    scheduler.subscribeReady([&](const RenderRequest&, const RasterImage&) { ++delivered; });
    scheduler.enqueue(requestFor(3), RenderPriority::Background, CachePolicy::Bypass);
    REQUIRE(scheduler.waitForIdle(3000));
    CHECK(scheduler.deliverReady() == 1);
    CHECK(delivered == 1);
    CHECK(cache.stats().lookups == 0);
    CHECK(cache.stats().insertions == 0);
    CHECK(cache.entryCount() == 0);
    CHECK(scheduler.stats().enqueuedBypassingCache == 1);

    SECTION("THE CONTROL: the same tile asked for normally is looked up and kept")
    {
        scheduler.enqueue(requestFor(3), RenderPriority::Visible);
        REQUIRE(scheduler.waitForIdle(3000));
        scheduler.deliverReady();
        CHECK(cache.stats().lookups == 1);
        CHECK(cache.stats().insertions == 1);
        CHECK(probe.calls.load() == 2);   // the bypassing render left nothing to hit
    }

    SECTION("a holder that wants the tile KEPT keeps a coalesced bypassing job")
    {
        scheduler.pause();
        scheduler.enqueue(requestFor(5), RenderPriority::Background, CachePolicy::Bypass);
        scheduler.enqueue(requestFor(5), RenderPriority::Visible);
        CHECK(scheduler.pendingCount() == 1);
        scheduler.resume();
        REQUIRE(scheduler.waitForIdle(3000));
        scheduler.deliverReady();
        CHECK(cache.stats().insertions == 1);
    }
}

// =============================================================================================
// Stage 5 T8 (D5-12) -- the third kind of job, an ENGINE TASK: a document's search units, read part by part
// =============================================================================================

TEST_CASE("render scheduler runs an engine task on its worker and reports it on the pumping thread",
          "[core][render][task]")
{
    // A-79's engine-thread half: the task is entered on the worker and never on this thread, and its
    // ticket comes back on this thread through `deliverReady()` -- the text job's rule, for a task.
    OrderLog log;
    RenderScheduler scheduler(loggingFactory(log), nullptr, RenderScheduler::Config{});
    scheduler.pause();

    std::mutex mutex;
    std::set<std::thread::id> runningThreads;
    const auto task = [&](int part) {
        return [&, part] {
            {
                const std::lock_guard<std::mutex> lock(mutex);
                runningThreads.insert(std::this_thread::get_id());
            }
            log.add("task:" + std::to_string(part));
        };
    };
    const RenderTicket first = scheduler.enqueueTask(kDocA, RenderPriority::Background, task(0));
    const RenderTicket second = scheduler.enqueueTask(kDocA, RenderPriority::Background, task(1));
    REQUIRE(first.isValid());
    REQUIRE(second.isValid());
    // STRICT PRIORITY ACROSS THE THREE KINDS: a visible tile queued after the tasks still goes first.
    scheduler.enqueue(requestFor(9), RenderPriority::Visible);

    std::vector<std::uint64_t> reported;
    std::set<std::thread::id> reportingThreads;
    scheduler.subscribeTasks([&](RenderTicket ticket) {
        reportingThreads.insert(std::this_thread::get_id());
        reported.push_back(ticket.id);
    });
    scheduler.resume();
    REQUIRE(scheduler.waitForIdle(3000));
    CHECK(scheduler.deliverReady() == 3);

    CHECK(reported == std::vector<std::uint64_t>{first.id, second.id});
    REQUIRE(runningThreads.size() == 1);
    CHECK(*runningThreads.begin() != std::this_thread::get_id());
    REQUIRE(reportingThreads.size() == 1);
    CHECK(*reportingThreads.begin() == std::this_thread::get_id());
    {
        std::lock_guard<std::mutex> lock(log.mutex);
        REQUIRE(log.entries.size() == 3);
        CHECK(log.entries[0] == "tile:9");
        CHECK(log.entries[1] == "task:0");
        CHECK(log.entries[2] == "task:1");
    }
    const auto stats = scheduler.stats();
    CHECK(stats.taskEnqueued == 2);
    CHECK(stats.taskCompleted == 2);
    CHECK(stats.taskDelivered == 2);
    CHECK(stats.enqueuedBackground == 2);

    // AN EMPTY TASK IS REFUSED BY TICKET, as an invalid text request is -- and so is an EXCLUSIVE task of no
    // document, which has nothing to be exclusive against. A task of no document is not refused (S5 T9):
    // it is a folder search's open, which has no handle yet.
    CHECK_FALSE(scheduler.enqueueTask(kDocA, RenderPriority::Background, {}).isValid());
    CHECK_FALSE(scheduler.enqueueTask(DocumentHandle{}, RenderPriority::Background, task(2), true).isValid());
    CHECK(scheduler.enqueueTask(DocumentHandle{}, RenderPriority::Background, task(2)).isValid());
    REQUIRE(scheduler.waitForIdle(3000));
    scheduler.deliverReady();
}

TEST_CASE("render scheduler starts an exclusive task only when nothing of its document runs", "[core][render][task]")
{
    // S5 T9: a folder search CLOSES a document as an exclusive task after `cancelDocument()` -- and the job
    // already inside the engine for it must finish first, with any number of workers. Two workers here, so
    // without the rule the close would start beside the running job.
    RenderScheduler::Config config;
    config.workerCount = 2;
    RasterizerProbe rasters;
    RenderScheduler scheduler(factoryFor(rasters), nullptr, config);

    std::mutex mutex;
    std::condition_variable changed;
    bool release = false;
    bool longStarted = false;
    std::vector<std::string> order;
    const auto record = [&](const std::string& what) {
        const std::lock_guard<std::mutex> lock(mutex);
        order.push_back(what);
    };
    // A job of document A that holds its worker until the case lets it go.
    scheduler.enqueueTask(kDocA, RenderPriority::Background, [&] {
        std::unique_lock<std::mutex> lock(mutex);
        longStarted = true;
        changed.notify_all();
        changed.wait(lock, [&] { return release; });
        order.push_back("A-long");
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(3), [&] { return longStarted; }));
    }
    scheduler.enqueueTask(kDocA, RenderPriority::Background, [&] { record("A-close"); }, true);
    // ANOTHER DOCUMENT'S work runs on the free worker meanwhile -- the rule is per document.
    scheduler.enqueueTask(kDocB, RenderPriority::Background, [&] { record("B"); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    {
        const std::lock_guard<std::mutex> lock(mutex);
        CHECK(order == std::vector<std::string>{"B"});   // the close has NOT started beside A's job
        release = true;
    }
    changed.notify_all();
    REQUIRE(scheduler.waitForIdle(3000));
    scheduler.deliverReady();
    const std::lock_guard<std::mutex> lock(mutex);
    CHECK(order == std::vector<std::string>{"B", "A-long", "A-close"});
}

TEST_CASE("render scheduler counts a job started while a more urgent one waited and only then", "[core][render][I-12]")
{
    // S5 guide I-12: the inversion count is the scheduler's own, taken at the pick. Strict priority makes it
    // zero -- the control half -- and an exclusive task holding a document's VISIBLE tile back while another
    // document's background work starts is the one way it is not.
    SECTION("strict priority: every visible tile before any background job, and nothing counted")
    {
        RasterizerProbe rasters;
        RenderScheduler scheduler(factoryFor(rasters), nullptr, RenderScheduler::Config{});
        std::atomic<int> ran{0};
        scheduler.pause();
        scheduler.enqueueTask(kDocB, RenderPriority::Background, [&ran] { ++ran; });
        scheduler.enqueueTask(kDocB, RenderPriority::Background, [&ran] { ++ran; });
        scheduler.enqueue(requestFor(0), RenderPriority::Visible);
        scheduler.enqueue(requestFor(1), RenderPriority::Prefetch);
        scheduler.resume();
        REQUIRE(scheduler.waitForIdle(3000));
        scheduler.deliverReady();
        CHECK(ran.load() == 2);
        CHECK(rasters.calls.load() == 2);
        CHECK(scheduler.stats().started == 4);
        CHECK(scheduler.stats().startedWhileMoreUrgentWaited == 0);
    }

    SECTION("an exclusive task holds a visible tile back and another document's background job starts")
    {
        RenderScheduler::Config config;
        config.workerCount = 2;
        RasterizerProbe rasters;
        RenderScheduler scheduler(factoryFor(rasters), nullptr, config);
        std::mutex mutex;
        std::condition_variable changed;
        bool release = false;
        bool holding = false;
        bool backgroundRan = false;
        // AN EXCLUSIVE TASK OF A, held on its worker: nothing of A may start beside it.
        scheduler.enqueueTask(kDocA, RenderPriority::Background, [&] {
            std::unique_lock<std::mutex> lock(mutex);
            holding = true;
            changed.notify_all();
            changed.wait(lock, [&] { return release; });
        }, true);
        {
            std::unique_lock<std::mutex> lock(mutex);
            REQUIRE(changed.wait_for(lock, std::chrono::seconds(3), [&] { return holding; }));
        }
        // A's VISIBLE tile waits on the exclusive task; B's background job takes the free worker.
        scheduler.enqueue(requestFor(0), RenderPriority::Visible);
        scheduler.enqueueTask(kDocB, RenderPriority::Background, [&] {
            const std::lock_guard<std::mutex> lock(mutex);
            backgroundRan = true;
            changed.notify_all();
        });
        {
            std::unique_lock<std::mutex> lock(mutex);
            REQUIRE(changed.wait_for(lock, std::chrono::seconds(3), [&] { return backgroundRan; }));
            CHECK(rasters.calls.load() == 0);   // the visible tile has NOT started
            release = true;
        }
        changed.notify_all();
        REQUIRE(scheduler.waitForIdle(3000));
        scheduler.deliverReady();
        CHECK(rasters.calls.load() == 1);
        // ONE: B's job. The exclusive task started with nothing waiting, and A's tile when nothing was.
        CHECK(scheduler.stats().startedWhileMoreUrgentWaited == 1);
    }
}

TEST_CASE("render scheduler never runs a cancelled engine task and forgets a closed document's", "[core][render][task]")
{
    // A-36's rule, for a task: not "nothing was reported" but "the task never ran". And `cancelDocument()`
    // reaches a task in all three states, which is what lets `close()`'s cancel-drain-close hold for the
    // search-units reader too.
    RasterizerProbe rasters;
    RenderScheduler scheduler(factoryFor(rasters), nullptr, RenderScheduler::Config{});
    std::atomic<int> ran{0};
    const auto count = [&ran] { ++ran; };
    std::vector<std::uint64_t> reported;
    scheduler.subscribeTasks([&](RenderTicket ticket) { reported.push_back(ticket.id); });

    scheduler.pause();
    const RenderTicket kept = scheduler.enqueueTask(kDocA, RenderPriority::Background, count);
    const RenderTicket dropped = scheduler.enqueueTask(kDocA, RenderPriority::Background, count);
    scheduler.enqueueTask(kDocB, RenderPriority::Background, count);
    scheduler.cancel(dropped);
    scheduler.cancelDocument(kDocB);
    CHECK(scheduler.pendingCount() == 1);
    scheduler.resume();
    REQUIRE(scheduler.waitForIdle(3000));
    CHECK(scheduler.deliverReady() == 1);
    CHECK(ran.load() == 1);
    CHECK(reported == std::vector<std::uint64_t>{kept.id});

    SECTION("a task that FINISHED and was not yet reported goes with its document")
    {
        scheduler.enqueueTask(kDocB, RenderPriority::Background, count);
        REQUIRE(scheduler.waitForIdle(3000));
        CHECK(ran.load() == 2);              // it ran
        scheduler.cancelDocument(kDocB);     // ... and its document closed before the pump
        CHECK(scheduler.deliverReady() == 0);
        CHECK(reported.size() == 1);
    }
}
