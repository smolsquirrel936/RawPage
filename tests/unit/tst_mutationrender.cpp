// tst_mutationrender -- a rotation through `CommandJournal` reaching `RenderScheduler` and `RenderCache`.
// Stage 3 T8, guide case A-59 (`Stage3-Mutation-Testing-Guide.md` §5.9), decisions D2-12 and D3-15.
//
// Stage 2's `tst_renderscheduler.cpp` proved the mechanism with a hand-built `Scope` and `Revision`
// (its A-38). This file proves the WIRING Stage 3 adds: the scope and revision come from a real
// rotation run through the journal, and a tile in flight at the old revision is the race.
//
// WHAT IT DOES NOT COVER (convention 19): `ui/`. The subscription from `CommandJournal::mutated` to
// `RenderScheduler::documentChanged` is made here by the test, the way `ui/shell/DocumentWindow` is to
// make it at T10; that DocumentWindow does make it is T10's. The rasterizer is a gated fake -- the
// engine drawing the delta is `tests/render/tst_saveengine.cpp` (A-58).

#include <catch2/catch_test_macros.hpp>

#include "CosBackend.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <thread>

using namespace rawpage;
using core::render::RenderCache;
using core::render::RenderPriority;
using core::render::RenderRequest;
using core::render::RenderScheduler;
using core::render::TileRect;

namespace {

struct Gate {
    std::atomic<int> calls{0};
    std::atomic<bool> hold{false};
    std::mutex mutex;
    std::condition_variable released;
    std::vector<RenderRequest> seen;

    bool waitForCalls(int count)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (calls.load() < count) {
            if (std::chrono::steady_clock::now() > deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
    void release()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            hold.store(false);
        }
        released.notify_all();
    }
};

class GatedRasterizer final : public core::IPageRasterizer {
public:
    explicit GatedRasterizer(Gate& gate) : gate_(gate) {}
    core::Result<core::RasterImage> raster(const RenderRequest& request) override
    {
        {
            std::lock_guard<std::mutex> lock(gate_.mutex);
            gate_.seen.push_back(request);
        }
        ++gate_.calls;
        {
            std::unique_lock<std::mutex> lock(gate_.mutex);
            gate_.released.wait(lock, [this] { return !gate_.hold.load(); });
        }
        core::RasterImage image;
        image.width = request.tile.width;
        image.height = request.tile.height;
        image.bytesPerLine = request.tile.width * 4;
        image.format = core::PixelFormat::Bgra8888;
        image.pixels = QByteArray(image.bytesPerLine * image.height, static_cast<char>('A' + request.pageIndex));
        return image;
    }

private:
    Gate& gate_;
};

RenderRequest tileOf(core::DocumentHandle document, int page, core::Revision revision)
{
    RenderRequest request;
    request.document = document;
    request.pageIndex = page;
    request.tile = TileRect{0, 0, 8, 8};
    request.deviceScale = 1.0;
    request.revision = revision;
    return request;
}

} // namespace

TEST_CASE("a rotation drops the in-flight tile of its page and re-keys the other pages' tiles", "[save][render][journal]")
{
    tests::CosBackend backend;
    auto opened = backend.open(QString::fromStdString((std::filesystem::path(RAWPAGE_CORPUS_DIR) / "ordinary/text-a4.pdf").string()), nullptr);
    REQUIRE(opened);
    core::Document document;
    REQUIRE(document.hydrate(backend, opened.value()));
    core::CommandJournal journal(std::move(document), backend.mutationSink(opened.value()));
    const core::DocumentHandle handle = opened.value();
    const core::Revision r = journal.revision();

    Gate gate;
    RenderCache cache;
    RenderScheduler scheduler([&gate] { return std::unique_ptr<core::IPageRasterizer>(new GatedRasterizer(gate)); }, &cache);

    // Pages 1 and 2 are rendered and cached at r.
    scheduler.enqueue(tileOf(handle, 0, r), RenderPriority::Visible);
    scheduler.enqueue(tileOf(handle, 1, r), RenderPriority::Visible);
    REQUIRE(scheduler.waitForIdle(2000));
    REQUIRE(scheduler.deliverReady() == 2);
    REQUIRE(cache.entryCount() == 2);

    // THE WIRING T10 will make in DocumentWindow.
    journal.subscribeMutated([&](const core::Scope& scope) { scheduler.documentChanged(handle, scope, journal.revision()); });

    // Page 3's tile is held INSIDE the rasterizer at r, and page 3 is rotated.
    gate.hold.store(true);
    scheduler.enqueue(tileOf(handle, 2, r), RenderPriority::Visible);
    REQUIRE(gate.waitForCalls(3));
    REQUIRE(journal.run(std::make_unique<core::RotatePageCommand>(2, 90)));
    const core::Revision next = journal.revision();
    REQUIRE(next == r.next());
    gate.release();
    REQUIRE(scheduler.waitForIdle(2000));

    // The delivery at r is DROPPED: never cached, never delivered.
    CHECK(scheduler.stats().droppedSuperseded == 1);
    CHECK(scheduler.deliverReady() == 0);
    CHECK_FALSE(cache.lookup(tileOf(handle, 2, r)).has_value());

    // The other pages' tiles are RE-KEYED to r+1, not re-rendered: a lookup at r+1 hits and nothing new
    // reached the rasterizer; a lookup at r misses, structurally.
    CHECK(cache.lookup(tileOf(handle, 0, next)).has_value());
    CHECK(cache.lookup(tileOf(handle, 1, next)).has_value());
    CHECK_FALSE(cache.lookup(tileOf(handle, 0, r)).has_value());
    CHECK(gate.calls.load() == 3);

    // Page 3's next tile is at r+1, carrying the unsaved rotation as its delta (D3-15) -- and is a
    // different key from the same tile without it.
    RenderRequest rotated = tileOf(handle, 2, next);
    rotated.pageRotationDelta = core::PageRotation::Clockwise90;
    RenderRequest unrotated = tileOf(handle, 2, next);
    CHECK_FALSE(rotated == unrotated);
    CHECK(std::hash<RenderRequest>{}(rotated) != std::hash<RenderRequest>{}(unrotated));
    scheduler.enqueue(rotated, RenderPriority::Visible);
    REQUIRE(scheduler.waitForIdle(2000));
    CHECK(scheduler.deliverReady() == 1);
    REQUIRE(gate.calls.load() == 4);
    CHECK(gate.seen.back().revision == next);
    CHECK(gate.seen.back().pageRotationDelta == core::PageRotation::Clockwise90);
}
