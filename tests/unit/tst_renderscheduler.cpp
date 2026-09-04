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

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
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
