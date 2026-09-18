// RISK A2, THE PRODUCT'S FIGURE -- Stage 2 T18, decision D2-1 (Stage2-Rendering-Plan.md §3),
// second half. It also produces the numbers open decision **O7** and **D2-14** were waiting for.
//
// ===============================================================================================
// WHY THERE ARE TWO A2 MEASUREMENTS AND THIS IS THE ONE THAT IS A CLAIM ABOUT THE PRODUCT
// ===============================================================================================
//
// `bench_render.cpp` (T3) answers *is this physically possible* over raw `FPDF_RenderPageBitmap`,
// early enough that a no-go could still change the architecture. It has no scheduler, no cache, no
// tile grid and no DPI policy -- and those four are exactly what stands between PDFium and the
// screen. This file answers the other question, *is the PRODUCT fast*, by driving the shipped
// assembly: `pdf::createBackend()` → `app::BackendRasterizer` → `core::render::RenderScheduler`
// → `core::render::RenderCache`, with `TileGrid` deciding the tiles and `DpiPolicy` the resolution.
//
// The reason both exist was written down in advance rather than learned twice. Stage 1's ribbon
// spike reported R-9 at 0.28 ms; T15 re-measured against the shipped ribbon and got 0.471 ms,
// because *a spike's ribbon is as small as its author made it*. One figure without the other is
// either a late verdict or a flattering one.
//
// ===============================================================================================
// THIS BINARY DOES NOT LINK RAW PDFIUM, AND THAT IS THE STRUCTURAL HALF OF THE CLAIM
// ===============================================================================================
//
// `bench_render` links the `pdfium` target directly -- it is deliberately below the facade, and its
// CMakeLists says so. This one links `rawpage_pdfium` and stops there, so it CANNOT name an `FPDF_`
// symbol even by accident. A measurement of "the product's render path" that could reach past the
// boundary would be a measurement of something the product does not do.
//
// ===============================================================================================
// WHAT IS TIMED, AND WHY TWO DIFFERENT QUANTITIES ARE REPORTED
// ===============================================================================================
//
// **These are not the same number as the probe's, and reading them as comparable is the mistake
// this section exists to prevent.** The probe starts its timer *after* taking the engine lock, so
// each of its samples is the SERVICE TIME of one `FPDF_RenderPageBitmap` call. Through the shipped
// pipeline there is a queue in the way, and a queue is the point of the design, so:
//
//   * **latency** -- `enqueue()` to the moment `deliverReady()` hands the tile to a subscriber.
//     This is what a viewport actually waits, and with `workerCount = 1` (I-05's answer) the Nth
//     tile of a batch waits behind the other N-1. It is therefore a function of how many tiles are
//     asked for at once, which is a real property of the product and not an artifact.
//
//   * **cost** -- wall time for the whole batch divided by the tiles in it. This is the quantity
//     that IS comparable to the probe's service time, because both are "engine seconds per tile"
//     with no queueing in them.
//
// A2's criterion is *p95 per-tile render latency under ~16 ms at the target DPI*. Both figures are
// printed against the 16.667 ms frame budget, and the guide records the verdict against the scale
// it fixed before the measurement was taken.
//
// ===============================================================================================
// THE OTHER TWO QUESTIONS THIS ANSWERS, BECAUSE THE INSTRUMENTATION WAS BUILT WITH THE CACHE
// ===============================================================================================
//
// **O7** -- is the render cache's disk tier shared between documents or per-document? D2-14 says
// that is answered by MEASUREMENT over the corpus rather than by preference, and `CacheStats` plus
// `crossDocumentEvictions()` exist in `RenderCache` for it (T11) rather than being bolted on here.
// Phase 3 opens several documents at once and reports what a shared cache actually costs.
//
// **D2-14** -- the disk tier ships OFF (`performance/diskCacheBytes` = 0) until this task measures
// whether it earns its place. Phase 4 runs the same corpus pass with a `DiskTileStore` attached and
// prints both sides.
//
// ===============================================================================================
// PHASE 0 IS THE LATENCY INSTRUMENT'S CONTROL, AND EVERY OTHER FIGURE IS READ ONLY AFTER IT
// ===============================================================================================
//
// Stage 1's **I-29** printed nothing but its own argument-evaluation bug, and Stage 1 T15's rule is
// that *a small reading has two explanations: a cheap operation, or a dead instrument*. A tile here
// costs ~0.34 ms, which is exactly the size of number a dead instrument would also produce. Guide
// case A-69 found the probe had a control and this file did not: its O7 phase proved the cache
// COUNTERS live, but nothing had a KNOWN cost to catch a dead LATENCY instrument.
//
// So phase 0 runs the SAME instrument -- `measureLatency`, the one copy phases 1 and 2 also use --
// over a rasterizer whose cost is known by construction (`kControlCostMs`, spent in a busy wait
// because a Windows `Sleep` rounds up to the ~15.6 ms timer tick and would make the cost unknown
// again). With one worker, three things are checked:
//
//   * every latency is at least the known cost -- a tile cannot be delivered before its raster
//     returns. A zero here is I-29's shape.
//   * the FIRST tile reads within `kControlResolutionMs` of the known cost. This is the check that
//     fired on its first run: every tile had been delivered only on a ~15.6 ms timer tick, so phase
//     1's p50 and p95 were both the batch's wall time (Stage 2 I-41). It is the one bound that
//     depends on the machine, and it is set at a third of a tick so that it separates the two
//     states rather than grading speed.
//   * the slowest tile waited behind the rest of its batch -- at least half of (tiles - 1) known
//     costs. A figure near ONE known cost means the instrument is timing SERVICE rather than queued
//     latency, which is D2-1's confusion and would make phase 1 comparable to the probe when it is
//     not.
//
// Any failing exits 1 BEFORE any figure is printed: under the exit-code rule below, a dead
// instrument is a measurement that was not possible. `--dead-instrument` and `--coarse-pump` are
// this control's own negative controls (convention 7), each breaking the instrument one way, and
// each must make the run fail.
//
// ===============================================================================================
// WHY IT IS NOT A PASS/FAIL CTEST CASE
// ===============================================================================================
//
// It is registered with CTest so it cannot rot, but its exit code is about whether the MEASUREMENT
// WAS POSSIBLE -- the engine started, documents opened, tiles arrived -- and never about whether
// the machine was fast. A shared runner under load must not turn a green build red for a reason
// unrelated to the change. That rule is `bench_ribbon`'s and `bench_render`'s and is restated here
// because it is the thing most likely to be "improved" by someone adding a threshold.

#include "app/BackendRasterizer.h"
#include "core/backend.h"
#include "core/doc/PageTree.h"
#include "core/doc/Revision.h"
#include "core/render/DiskTileStore.h"
#include "core/render/DpiPolicy.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "core/render/TileGrid.h"
#include "pdf/BackendFactory.h"
#include "pdf/render/PdfiumInit.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QString>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <functional>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace rawpage;
using core::render::RenderFlags;
using core::render::RenderPriority;
using core::render::RenderRequest;
using core::render::Resolution;
using core::render::TileGrid;
using core::render::TileRect;

using Clock = std::chrono::steady_clock;

// --- The parameters, in one place --------------------------------------------------------
constexpr double kFrameBudgetMs = 16.667;   // one 60 Hz frame

// A2's own configuration, so phase 1 is directly comparable with the T3 probe: 200 % zoom on a
// 150 %-scaled monitor. `DpiPolicy` computes it here rather than the arithmetic being repeated,
// which is the difference between measuring the product and measuring a copy of it.
constexpr double kA2Zoom         = 2.00;
constexpr double kA2MonitorScale = 1.50;
constexpr double kA2PhysicalDpi  = 144.0;   // 96 x 1.50, the nominal the probe used

// The corpus sweep's resolutions -- the same three `tests/golden/` pins, so a reader comparing a
// slow file against its baseline is looking at the same pixels.
constexpr int kSweepDpi[] = {96, 144, 192};

// Phase 0's known cost per tile. Large beside a real tile (~0.34 ms) so the control cannot pass on
// the pipeline's own overhead, small enough that the phase takes well under a second.
constexpr double kControlCostMs = 2.0;

// --- Stage 4's workload (phase 5, D4-16) --------------------------------------------------
//
// A thumbnail is one request for a whole page at a size a pane shows, NOT a tiled surface: at
// `kThumbnailDpi` an A4 page is ~149 px wide, which is one tile, and `TileGrid` says so rather than
// this file assuming it.
constexpr double kThumbnailDpi = 18.0;

// D4-10's viewport bound, and the number the plan's §5.4 put in writing: "20-40 speculative
// requests in flight". THIRTY is a screenful of thumbnails plus a margin -- and over
// `pages-900.pdf` it is the whole point, because the pane that enqueued 900 would be the defect
// this decision exists to prevent.
constexpr int kThumbnailSlots = 30;

// Four scroll positions. Each round moves both viewports and the pane onto pages they have not
// rendered, because a workload that asks for the same tiles twice measures the cache and calls it
// the pipeline.
constexpr int kStage4Rounds = 4;

double millisSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// How far above the known cost phase 0's FIRST tile may read. A live instrument over a yielding
// pump reads it within a fraction of a millisecond (2.07-2.08 ms on 2026-09-14); a pump quantized to
// the ~15.6 ms timer tick reads ~15.7 ms. Five sits well clear of both.
constexpr double kControlResolutionMs = 5.0;

// The two NEGATIVE controls for phase 0 (convention 7), each breaking the instrument one way:
// `--dead-instrument` makes every latency read zero (I-29's shape), and `--coarse-pump` restores the
// tick-quantized pump I-41 found. Either must make the run fail at phase 0.
bool g_deadInstrument = false;
bool g_coarsePump = false;

// THE LATENCY READING, in one place, so the control and the figures cannot use different ones.
double latencySince(Clock::time_point sent)
{
    return g_deadInstrument ? millisSince(Clock::now()) : millisSince(sent);
}

// The key that joins an enqueue to its delivery -- one function for both ends, so they cannot drift
// apart and silently drop every sample.
std::string tileKey(core::DocumentHandle document, int page, const TileRect& tile)
{
    return std::to_string(document.id) + "/" + std::to_string(page) + "/"
           + std::to_string(tile.x) + "," + std::to_string(tile.y);
}

// Phase 5's key. `tileKey` alone is NOT enough here and the difference is a real collision: the
// thumbnail stream and the viewports ask for the same (document, page) at different SCALES, and
// tile (0,0) of a 18 dpi thumbnail would otherwise share a key with tile (0,0) of a 288 dpi
// viewport -- one consumer's latency landing in the other's column.
std::string scaledTileKey(const RenderRequest& request)
{
    char scale[32];
    std::snprintf(scale, sizeof(scale), "%.4f", request.deviceScale);
    return tileKey(request.document, request.pageIndex, request.tile) + "@" + scale;
}

double percentile(std::vector<double> samples, double fraction)
{
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(samples.size() - 1));
    return samples[std::min(index, samples.size() - 1)];
}

// Into locals FIRST. Stage 1's **I-29** was three percentile calls written as arguments to one
// `printf`, where argument evaluation order is unspecified, and every figure that benchmark printed
// on its first run was that bug rather than the ribbon.
void report(const char* label, const std::vector<double>& latencies, double wallMs)
{
    const double p50 = percentile(latencies, 0.50);
    const double p95 = percentile(latencies, 0.95);
    const double max = percentile(latencies, 1.00);
    const double cost = latencies.empty() ? 0.0 : wallMs / static_cast<double>(latencies.size());
    std::printf("  %-18s tiles %6zu   latency p50 %7.3f  p95 %7.3f  max %8.3f ms"
                "   cost %6.3f ms/tile\n",
                label, latencies.size(), p50, p95, max, cost);
}

// --- One rendering run over the shipped pipeline -----------------------------------------

struct RunResult {
    std::vector<double> latencies;   // enqueue -> delivered, per tile
    double              wallMs = 0.0;
    long long           tiles = 0;
    bool                ok = false;
};

struct OpenDocument {
    core::DocumentHandle handle;
    const core::PageTree* pages = nullptr;
    QString               name;
};

// Phase 5 is a function rather than a block inside `main` -- it is the only phase with its own
// bookkeeping -- so the opener `main` builds is handed to it by type.
using OpenFn = std::function<std::optional<OpenDocument>(const QString&, const QString&)>;

Resolution resolutionAt(double dpi)
{
    Resolution resolution;
    resolution.dpi = dpi;
    resolution.devicePixelRatio = 1.0;
    resolution.upscale = 1.0;
    return resolution;
}

RenderRequest requestFor(core::DocumentHandle document, int page, TileRect tile, double dpi)
{
    RenderRequest request;
    request.document = document;
    request.pageIndex = page;
    request.tile = tile;
    request.deviceScale = resolutionAt(dpi).rasterScale();
    request.revision = core::Revision::initial();
    // `Annotations` is what `ui/viewer/PageView` asks for by default, so the measurement is of the
    // configuration users run rather than of a cheaper one.
    request.flags = RenderFlags::Annotations;
    return request;
}

// A 1080p viewport's worth of device pixels. **The first version of this benchmark asked for the
// WHOLE PAGE and called the result a latency, and the number was 42 ms against a 16.667 ms budget
// -- which reads as a failed A2 and is not one.** At 288 dpi an A4 page is 140 tiles, one worker
// serves them in order, and the 140th therefore waits behind 139 others: what was being measured
// was *how long a whole page takes*, reported per tile.
//
// A2's question is about a tile the user is waiting on, and a user is waiting on the tiles under
// their viewport. So the default subject is a viewport, `--whole-page` asks for the other quantity,
// and both are printed -- because a per-tile figure comfortably inside the budget can still add up
// to a visible stall, which is the scheduler's problem rather than PDFium's and is exactly why the
// two numbers belong side by side rather than one of them being chosen.
constexpr int kViewportWidth  = 1920;
constexpr int kViewportHeight = 1080;

// Enqueues the tiles a viewport covers and pumps until they have all been delivered. An empty
// `viewport` means the whole page.
//
// THE PUMP IS THIS THREAD, which is the product's arrangement: `RenderCache` is not thread-safe on
// purpose, and only the thread that calls `deliverReady()` ever touches it (T11). A benchmark that
// invented its own delivery path would be measuring something the product does not have.
void renderPage(core::render::RenderScheduler& scheduler, bool wholePage,
                const OpenDocument& document, int page, double dpi,
                std::map<std::string, Clock::time_point>& sentAt, RunResult& into)
{
    const core::Page* pageInfo = document.pages->at(page);
    if (pageInfo == nullptr) return;

    const Resolution resolution = resolutionAt(dpi);
    const TileRect bounds = TileGrid::pageBounds(*pageInfo, resolution);
    if (bounds.isEmpty()) return;

    // The viewport, clipped to the page: a page smaller than 1920x1080 is entirely visible, which
    // is the ordinary case at 96 dpi and is not a special case anywhere in the product either.
    TileRect viewport = bounds;
    if (!wholePage) {
        viewport.width = std::min(bounds.width, kViewportWidth);
        viewport.height = std::min(bounds.height, kViewportHeight);
    }

    const TileGrid grid;
    const core::render::TileSet tiles = grid.tilesFor(*pageInfo, resolution, viewport);

    for (const TileRect& tile : tiles.visible) {
        const RenderRequest request = requestFor(document.handle, page, tile, dpi);
        sentAt[tileKey(document.handle, page, tile)] = Clock::now();
        scheduler.enqueue(request, RenderPriority::Visible);
        into.tiles += 1;
    }

    // Pump until idle, delivering between YIELDS -- never between timed waits. A scheduler that
    // finished everything while this thread slept would report every latency as the whole batch's
    // wall time, and until 2026-09-14 that is what happened: the pump waited in `waitForIdle(1)`,
    // whose 1 ms `wait_for` rounds up to the ~15.6 ms Windows timer tick on this toolchain, so a
    // tile was handed over only at a tick or when the batch went idle. Phase 1's 40 tiles finish in
    // ~14 ms, inside one tick, and all 40 read 14.08 ms (Stage 2 I-41, found by phase 0's control).
    // `--coarse-pump` restores that wait, which is how the control's resolution check is shown to
    // fire.
    while (scheduler.pendingCount() > 0 || scheduler.runningCount() > 0) {
        scheduler.deliverReady();
        if (scheduler.pendingCount() == 0 && scheduler.runningCount() == 0) break;
        if (g_coarsePump)
            scheduler.waitForIdle(1);
        else
            std::this_thread::yield();
    }
    scheduler.deliverReady();
}

// One page's latencies through `scheduler` -- the instrument phases 0 and 1 share.
RunResult measureLatency(core::render::RenderScheduler& scheduler, bool wholePage,
                         const OpenDocument& document, int page, double dpi)
{
    RunResult run;
    std::map<std::string, Clock::time_point> sentAt;
    const core::render::RenderScheduler::SubscriptionToken token = scheduler.subscribeReady(
        [&](const RenderRequest& request, const core::RasterImage&) {
            const auto at = sentAt.find(tileKey(request.document, request.pageIndex, request.tile));
            if (at != sentAt.end()) run.latencies.push_back(latencySince(at->second));
        });

    const Clock::time_point start = Clock::now();
    renderPage(scheduler, wholePage, document, page, dpi, sentAt, run);
    run.wallMs = millisSince(start);
    scheduler.unsubscribeReady(token);
    return run;
}

// Phase 0's subject: a rasterizer that costs `kControlCostMs` per tile and never touches the engine.
// A BUSY wait, because `Sleep` on Windows rounds up to the timer tick and the cost would stop being
// known. The pixels are real and valid, so the scheduler and the cache treat the tile like any other.
class KnownCostRasterizer final : public core::IPageRasterizer {
public:
    core::Result<core::RasterImage> raster(const RenderRequest& request) override
    {
        const Clock::time_point start = Clock::now();
        core::RasterImage image;
        image.width = request.tile.width;
        image.height = request.tile.height;
        image.bytesPerLine = request.tile.width * 4;
        image.format = core::PixelFormat::Bgrx8888;
        image.pixels = QByteArray(static_cast<qsizetype>(image.bytesPerLine) * image.height,
                                  static_cast<char>(0xFF));
        while (millisSince(start) < kControlCostMs) {
        }
        return image;
    }
};


// =============================================================================================
// PHASE 5 -- STAGE 4's WORKLOAD, and the verdict D4-16 owes
// =============================================================================================
//
// A2's product figure (phase 1) was measured with ONE viewport. Stage 4 puts three consumers on the
// same scheduler, the same worker and the same cache -- a viewer, a split view's second pane, and a
// thumbnails pane over a 900-page document -- and D4-16 says the question *does the software path
// still hold the frame budget* has to be asked again of that workload, BEFORE the panes are built,
// because a no-go reshapes the pane rather than the stage.
//
// What a split IS matters to the answer and is easy to get wrong: it divides one window between N
// views rather than adding a second window's worth of pixels. See the column table below, where
// getting it wrong the first time is recorded rather than quietly corrected.
//
// WHAT A SAMPLE IS, AND WHY IT IS THE SAME SAMPLE AS PHASE 1'S. The reported quantity is the
// VIEWPORT's queued latency: enqueue to delivery, for the visible tiles of a 1920x1080 viewport at
// A2's own configuration. That is exactly what phase 1 measures, so the two figures are comparable
// and the difference between them is the load Stage 4 adds and nothing else. The thumbnail stream's
// own latencies are collected and reported SEPARATELY and are not mixed in: nobody waits on a
// thumbnail at frame rate, and averaging them into the viewport's column would flatter or damn the
// figure depending only on how many thumbnails were asked for.
//
// THE ENQUEUE ORDER IS THE PANE'S, NOT THE FLATTERING ONE. Each round asks for the thumbnails
// FIRST and the viewports immediately afterwards, with no pump in between -- which is the moment
// that matters: a person scrolls the pane and then the page, and the queue already has 30
// speculative jobs in it when the viewport's tiles arrive. Enqueueing the viewport first would
// measure a scheduler that never had to make the choice.
//
// THE PRIORITY CHECK IS COUNTED, NOT ASSUMED (guide A-16). A speculative tile delivered while a
// visible tile is still outstanding is an INVERSION -- the pane running instead of the page. With
// one worker exactly one speculative job can already be inside the rasterizer when the viewport's
// tiles are enqueued, so the honest bound is one per document per round, and that bound is printed
// beside the count rather than left for a reader to derive. **Cache-served speculative tiles are
// excluded from the count**, measured through `servedFromCache` before and after each enqueue: they
// cost no worker time, so counting them would report an inversion where no work was stolen. What
// this therefore does NOT cover (convention 19): a scheduler that reorders only among CACHED
// deliveries, which costs nothing and which no case here claims to catch.
//
// TWO CAPS, for the reason phase 3 uses two: at the shipped 256 MiB the workload may simply fit, and
// a measurement that only ever fits has not asked what sharing costs. The small cap is where the
// thumbnail stream and the viewports compete, and `crossDocumentEvictions` is what one document's
// pane costs the other document that did nothing wrong.
int runStage4Workload(core::IDocumentBackend& backend, const OpenFn& open,
                      const core::render::RenderScheduler::RasterizerFactory& makeRasterizer,
                      const Resolution& a2)
{
    const Resolution view = resolutionAt(a2.dpi);
    const Resolution thumb = resolutionAt(kThumbnailDpi);

    std::printf("\n  phase 5 -- STAGE 4's WORKLOAD (D4-16): one viewer, one split, one thumbnails pane\n");
    std::printf("    viewports     at A2's configuration -> %.1f dpi, %.3f px/pt, %dx%d each,"
                " all at Visible\n",
                view.dpi, view.rasterScale(), kViewportWidth, kViewportHeight);
    std::printf("    thumbnails    %d slots a round at %.0f dpi, all at Speculative (D4-10)\n",
                kThumbnailSlots, thumb.dpi);
    std::printf("    rounds        %d, each moving every viewport and the pane onto fresh pages\n",
                kStage4Rounds);

    std::vector<OpenDocument> documents;
    for (const QString& name : {QStringLiteral("ordinary/pages-900.pdf"),
                                QStringLiteral("ordinary/mixed-page-sizes.pdf")}) {
        if (const std::optional<OpenDocument> document = open(name, QString{}))
            documents.push_back(*document);
        else
            std::fprintf(stderr, "A2 at Stage 4: corpus/%s would not open\n", qPrintable(name));
    }
    if (documents.size() != 2) {
        std::fprintf(stderr, "A2 at Stage 4: the workload needs both documents -- no measurement\n");
        for (const OpenDocument& document : documents) backend.close(document.handle);
        return 1;
    }
    std::printf("    documents     %s (%d pages) and %s (%d pages), ONE cache between them\n",
                qPrintable(documents[0].name), documents[0].pages->count(),
                qPrintable(documents[1].name), documents[1].pages->count());

    struct Awaiting {
        Clock::time_point sent;
        RenderPriority    priority = RenderPriority::Visible;
        bool              fromCache = false;
    };

    struct Outcome {
        std::vector<double> viewport;
        std::vector<double> thumbnails;
        double    wallMs = 0.0;
        long long inversions = 0;
        long long thumbnailsFromCache = 0;
        long long undelivered = 0;
        core::render::CacheStats cacheStats;
        core::render::RenderScheduler::Stats schedulerStats;
        std::size_t evictions = 0;
        std::size_t crossDocumentEvictions = 0;
        std::size_t bytesHeld = 0;
        std::size_t entries = 0;
    };

    // ONE run of the workload at one cap, with `viewports` panes open and the thumbnails pane
    // either present or not. Everything else is identical between calls, which is the only way the
    // three columns below subtract.
    const auto run = [&](std::size_t cap, int viewports, int paneHeight, bool withPane) {
        core::render::RenderCache::Config cacheConfig;
        cacheConfig.memoryCapBytes = cap;
        core::render::RenderCache cache(cacheConfig);
        core::render::RenderScheduler scheduler(makeRasterizer, &cache);   // workerCount 1, I-05

        Outcome outcome;
        std::map<std::string, Awaiting> awaiting;
        long long outstandingVisible = 0;

        const core::render::RenderScheduler::SubscriptionToken token = scheduler.subscribeReady(
            [&](const RenderRequest& request, const core::RasterImage&) {
                const auto at = awaiting.find(scaledTileKey(request));
                if (at == awaiting.end()) return;
                const double latency = latencySince(at->second.sent);
                if (at->second.priority == RenderPriority::Speculative) {
                    outcome.thumbnails.push_back(latency);
                    // The pane ran instead of the page -- unless the tile cost no worker time.
                    if (outstandingVisible > 0 && !at->second.fromCache) ++outcome.inversions;
                } else {
                    outcome.viewport.push_back(latency);
                    if (outstandingVisible > 0) --outstandingVisible;
                }
                awaiting.erase(at);
            });

        // One enqueue path for both consumers, so the two cannot drift apart in how they are timed.
        // `servedFromCache` is read either side of the call because that is the only exact way to
        // know a tile never reached a worker -- the ready handler is given the request, not its
        // provenance.
        const auto enqueue = [&](const RenderRequest& request, RenderPriority priority) {
            const long long servedBefore = scheduler.stats().servedFromCache;
            awaiting[scaledTileKey(request)] = Awaiting{Clock::now(), priority, false};
            scheduler.enqueue(request, priority);
            const bool fromCache = scheduler.stats().servedFromCache > servedBefore;
            const auto at = awaiting.find(scaledTileKey(request));
            if (at != awaiting.end()) at->second.fromCache = fromCache;
            if (priority == RenderPriority::Visible) ++outstandingVisible;
            else if (fromCache) ++outcome.thumbnailsFromCache;
        };

        const TileGrid grid;
        const Clock::time_point start = Clock::now();
        for (int round = 0; round < kStage4Rounds; ++round) {
            for (const OpenDocument& document : documents) {
                const int pageCount = document.pages->count();

                // The pane, FIRST and bounded (D4-10): the slots it can show plus a margin, never
                // the document. Thirty of nine hundred is the decision, made visible. First,
                // because that is the moment the scheduler has to choose -- see the header.
                if (withPane) {
                    for (int slot = 0; slot < kThumbnailSlots; ++slot) {
                        const int page = (round * kThumbnailSlots + slot) % pageCount;
                        const core::Page* info = document.pages->at(page);
                        if (info == nullptr) continue;
                        const TileRect bounds = TileGrid::pageBounds(*info, thumb);
                        if (bounds.isEmpty()) continue;
                        for (const TileRect& tile : grid.tilesFor(*info, thumb, bounds).visible) {
                            RenderRequest request =
                                requestFor(document.handle, page, tile, thumb.dpi);
                            // A thumbnail draws no annotations -- `RenderFlags::None`, as §5.4's
                            // sketch has it -- which also keeps its cache key distinct from a
                            // viewport's for the same page.
                            request.flags = RenderFlags::None;
                            enqueue(request, RenderPriority::Speculative);
                        }
                    }
                }

                // Then the viewports: `viewports` panes on different pages of the SAME document,
                // one handle, all Visible (D4-13, guide A-15). Visible tiles only, because that is
                // what phase 1 samples and changing the subject would end the comparison.
                for (int pane = 0; pane < viewports; ++pane) {
                    const int page = (round * 2 + pane) % pageCount;
                    const core::Page* info = document.pages->at(page);
                    if (info == nullptr) continue;
                    const TileRect bounds = TileGrid::pageBounds(*info, view);
                    if (bounds.isEmpty()) continue;
                    TileRect viewport = bounds;
                    viewport.width = std::min(bounds.width, kViewportWidth);
                    viewport.height = std::min(bounds.height, paneHeight);
                    for (const TileRect& tile : grid.tilesFor(*info, view, viewport).visible)
                        enqueue(requestFor(document.handle, page, tile, view.dpi),
                                RenderPriority::Visible);
                }

                while (scheduler.pendingCount() > 0 || scheduler.runningCount() > 0) {
                    scheduler.deliverReady();
                    if (scheduler.pendingCount() == 0 && scheduler.runningCount() == 0) break;
                    std::this_thread::yield();
                }
                scheduler.deliverReady();

                // Anything still awaiting was dropped, superseded or cancelled; it is reported
                // rather than left to inflate the next round's inversion count.
                outcome.undelivered += static_cast<long long>(awaiting.size());
                awaiting.clear();
                outstandingVisible = 0;
            }
        }
        outcome.wallMs = millisSince(start);
        scheduler.unsubscribeReady(token);
        outcome.cacheStats = cache.stats();
        outcome.schedulerStats = scheduler.stats();
        outcome.evictions = cache.memoryTier().evictionCount();
        outcome.crossDocumentEvictions = cache.crossDocumentEvictions();
        outcome.bytesHeld = cache.bytesHeld();
        outcome.entries = cache.entryCount();
        return outcome;
    };

    // THE COLUMNS, AND WHY THERE ARE FOUR. The Stage 4 workload changes two things at once against
    // phase 1 -- more viewports, and a pane adding speculative work -- and a single number that
    // moved cannot say which of them moved it. The verdict has to be actionable at T5, where what
    // can be redesigned is the PANE, so the pane's cost is measured on its own.
    //
    // **A SPLIT DIVIDES A WINDOW; IT DOES NOT DOUBLE ONE.** `SplitViewContainer` puts N `PageView`s
    // inside ONE `DocumentWindow` (D4-13), so two panes in a 1920x1080 window are 1920x540 each and
    // the VISIBLE PIXEL COUNT IS UNCHANGED -- what changes is that the same area now spans two
    // pages, so the tiles are drawn from two page rasters instead of one. The first version of this
    // phase gave each pane a full 1920x1080 viewport and reported the result as "the split's cost":
    // that is a 2x window, not a split, and it measured a workload the product cannot produce by
    // splitting. It is kept, correctly labelled, as the last column -- because TWO WINDOWS on the
    // P-1 rig's two monitors (D4-17, `window.new`) really is 2x, and it is the pessimistic case the
    // verdict should be read against even though it is not what D4-16 names.
    struct Column { const char* label; int viewports; int paneHeight; bool pane; };
    const Column columns[] = {
        {"viewer",        1, kViewportHeight,     false},   // phase 1's subject, in this harness
        {"+ split",       2, kViewportHeight / 2, false},   // ONE window divided (D4-13)
        {"+ pane",        2, kViewportHeight / 2, true},    // <- the workload D4-16 names
        {"two windows",   2, kViewportHeight,     true},    // D4-17 on two monitors: the 2x case
    };

    const std::size_t caps[] = {256u * 1024u * 1024u, 24u * 1024u * 1024u};
    double shippedCapPaneP95 = 0.0;
    double shippedCapSplitP95 = 0.0;
    double shippedCapViewerP95 = 0.0;
    double shippedCapTwoWindowP95 = 0.0;
    for (const std::size_t cap : caps) {
        std::printf("\n    cap %4zu MiB\n", cap / (1024u * 1024u));
        for (const Column& column : columns) {
            const Outcome outcome =
                run(cap, column.viewports, column.paneHeight, column.pane);
            if (outcome.viewport.empty()) {
                std::fprintf(stderr,
                             "A2 at Stage 4: no viewport tile was delivered -- no measurement\n");
                for (const OpenDocument& document : documents) backend.close(document.handle);
                return 1;
            }

            char label[32];
            std::snprintf(label, sizeof(label), "%s VIEWPORT", column.label);
            report(label, outcome.viewport, outcome.wallMs);
            if (column.pane) {
                report("  its thumbnails", outcome.thumbnails, outcome.wallMs);
                std::printf("      priority   %lld inversion(s) against a bound of %lld"
                            " (one in-flight speculative per document per round);"
                            " %lld thumbnail(s) came from the cache and are excluded\n",
                            outcome.inversions,
                            static_cast<long long>(kStage4Rounds)
                                * static_cast<long long>(documents.size()),
                            outcome.thumbnailsFromCache);
                if (outcome.inversions > static_cast<long long>(kStage4Rounds)
                                             * static_cast<long long>(documents.size()))
                    std::printf("      PRIORITY INVERSION: the pane ran instead of the page more"
                                " often than one in-flight job per round explains -- guide A-16"
                                " grades this S1\n");
            }
            std::printf("      cache      lookups %lld   hits %lld (%.1f %%)   evictions %zu"
                        "   CROSS-DOCUMENT %zu   held %zu KiB in %zu\n",
                        outcome.cacheStats.lookups, outcome.cacheStats.hits(),
                        outcome.cacheStats.hitRate() * 100.0, outcome.evictions,
                        outcome.crossDocumentEvictions, outcome.bytesHeld / 1024, outcome.entries);
            std::printf("      scheduler  enqueued %lld   coalesced %lld   fromCache %lld"
                        "   started %lld   delivered %lld   droppedOverflow %lld"
                        "   priorityRaised %lld   undelivered %lld\n",
                        outcome.schedulerStats.enqueued, outcome.schedulerStats.coalesced,
                        outcome.schedulerStats.servedFromCache, outcome.schedulerStats.started,
                        outcome.schedulerStats.delivered, outcome.schedulerStats.droppedOverflow,
                        outcome.schedulerStats.priorityRaised, outcome.undelivered);

            if (cap == caps[0]) {
                const double p95 = percentile(outcome.viewport, 0.95);
                if (column.pane && column.paneHeight == kViewportHeight)
                    shippedCapTwoWindowP95 = p95;
                else if (column.pane) shippedCapPaneP95 = p95;
                else if (column.viewports == 2) shippedCapSplitP95 = p95;
                else shippedCapViewerP95 = p95;
            }
        }
    }

    std::printf("\n    WHAT MOVED THE FIGURE, at the shipped 256 MiB cap -- the viewport's p95:\n");
    std::printf("      one viewport (phase 1's subject) ..................... %7.3f ms\n",
                shippedCapViewerP95);
    std::printf("      + a split -- the SAME window divided in two .......... %7.3f ms   (%+.3f)\n",
                shippedCapSplitP95, shippedCapSplitP95 - shippedCapViewerP95);
    std::printf("      + the thumbnails pane at Speculative ................. %7.3f ms   (%+.3f)"
                "  <- D4-10's cost, and the figure D4-16 asks for\n",
                shippedCapPaneP95, shippedCapPaneP95 - shippedCapSplitP95);
    std::printf("      two full WINDOWS on two monitors, with the pane ...... %7.3f ms   (%+.3f)"
                "  <- D4-17's case, not D4-16's\n",
                shippedCapTwoWindowP95, shippedCapTwoWindowP95 - shippedCapPaneP95);
    std::printf("      frame budget ......................................... %7.3f ms\n",
                kFrameBudgetMs);
    std::printf("      Stage 2 T18, one viewport, measured there ............ p95 13.03-13.20 ms"
                " (Tech-Stack-Plan.md 3.3)\n");
    std::printf("\n    Record the verdict in Tech-Stack-Plan.md 3.3 beside A2's other figures,"
                " LABELLED as Stage 4's\n    workload, and answer WinAcceleratedSurface's FileMap"
                " row with it (D4-16, guide A-17).\n"
                "    This bench never fails on speed: its exit code is about whether the"
                " measurement was possible.\n\n");

    for (const OpenDocument& document : documents) backend.close(document.handle);
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);

    // Worker count from the command line, because I-05's answer is `workerCount = 1` and
    // `RenderScheduler::Config` kept the knob precisely so this task could walk it WITHOUT A
    // REBUILD. A finding that needs a source edit to reproduce is one that gets re-litigated --
    // which is `bench_render --unsafe-parallel`'s argument, applied to the shipped scheduler.
    int workerSweepMax = 1;
    bool wholePage = false;
    bool stage4 = false;
    const QStringList arguments = QCoreApplication::arguments();
    for (int at = 1; at < arguments.size(); ++at) {
        if (arguments.at(at) == QLatin1String("--whole-page")) {
            wholePage = true;
            continue;
        }
        if (arguments.at(at) == QLatin1String("--stage4")) {
            stage4 = true;
            continue;
        }
        if (arguments.at(at) == QLatin1String("--dead-instrument")) {
            g_deadInstrument = true;
            continue;
        }
        if (arguments.at(at) == QLatin1String("--coarse-pump")) {
            g_coarsePump = true;
            continue;
        }
        bool numeric = false;
        const int value = arguments.at(at).toInt(&numeric);
        if (numeric && value > 0) workerSweepMax = value;
    }

    std::printf("\nA2 -- THE PRODUCT'S FIGURE (Stage 2 T18, D2-1 second half)\n");
    std::printf("  the shipped pipeline: backend -> BackendRasterizer -> RenderScheduler -> "
                "RenderCache\n");
    std::printf("  pdfium        %s\n", pdf::render::version().data());
    std::printf("  subject       %s\n",
                wholePage ? "the WHOLE PAGE -- how long a page takes, reported per tile"
                          : "a 1920x1080 VIEWPORT -- what a user actually waits for "
                            "(--whole-page for the other)");

    const std::unique_ptr<core::IDocumentBackend> backend = pdf::createBackend();
    if (backend == nullptr) {
        std::fprintf(stderr, "A2 product: pdf::createBackend() returned nothing\n");
        return 1;
    }

    const QDir corpus(QStringLiteral(RAWPAGE_CORPUS_DIR));

    // Opens a corpus file, or returns nullopt. A file the engine refuses is skipped rather than
    // fatal: five of the 61 are refused BY DESIGN (`corpus/malformed/`), and a benchmark that
    // stopped on them would be asserting a correctness property that belongs to A-42 and A-43.
    auto open = [&](const QString& relative,
                    const QString& password) -> std::optional<OpenDocument> {
        const core::PasswordCallback callback =
            password.isEmpty() ? core::PasswordCallback{}
                               : [password](int attempt) -> std::optional<QString> {
                                     return attempt > 1 ? std::nullopt
                                                        : std::optional<QString>(password);
                                 };
        const core::Result<core::DocumentHandle> opened =
            backend->open(corpus.filePath(relative), callback);
        if (!opened) return std::nullopt;
        const core::PageTree* pages = backend->pages(opened.value());
        if (pages == nullptr || pages->isEmpty()) {
            backend->close(opened.value());
            return std::nullopt;
        }
        return OpenDocument{opened.value(), pages, relative};
    };

    const auto makeRasterizer = [&backend]() -> std::unique_ptr<core::IPageRasterizer> {
        return std::make_unique<app::BackendRasterizer>(*backend);
    };

    // =====================================================================================
    // PHASE 1 -- A2's own page at A2's own resolution, so the probe's figure has a partner
    // =====================================================================================
    {
        const std::optional<OpenDocument> document =
            open(QStringLiteral("a2-text-page.pdf"), QString{});
        if (!document) {
            std::fprintf(stderr, "A2 product: corpus/a2-text-page.pdf would not open\n");
            return 1;
        }

        // Through `DpiPolicy`, not through arithmetic repeated here. The probe computed
        // 2.00 x (96 x 1.50) / 72 = 4.0 px/pt by hand; this asks the policy the product asks.
        const core::render::DpiPolicy policy;
        const Resolution a2 = policy.resolve(core::render::Zoom::percent(kA2Zoom * 100.0),
                                             core::render::MonitorScale{kA2MonitorScale},
                                             core::render::PhysicalDpi{kA2PhysicalDpi});

        // ---------------------------------------------------------------------------------
        // PHASE 0 -- the latency instrument's control (see the file header). Same page, same
        // tiles, same instrument as phase 1; only the rasterizer's cost is known.
        // ---------------------------------------------------------------------------------
        {
            core::render::RenderCache cache;
            core::render::RenderScheduler::Config config;
            config.workerCount = 1;   // the queueing prediction below is for exactly one worker
            core::render::RenderScheduler scheduler(
                []() -> std::unique_ptr<core::IPageRasterizer> {
                    return std::make_unique<KnownCostRasterizer>();
                },
                &cache, config);

            const RunResult run = measureLatency(scheduler, wholePage, *document, 0, a2.dpi);

            // Into locals first (I-29).
            const double minMs = percentile(run.latencies, 0.00);
            const double maxMs = percentile(run.latencies, 1.00);
            const double cost =
                run.latencies.empty() ? 0.0 : run.wallMs / static_cast<double>(run.latencies.size());
            const double queueFloorMs =
                0.5 * static_cast<double>(std::max<long long>(run.tiles - 1, 0)) * kControlCostMs;
            const bool everyTileTimed =
                !run.latencies.empty() && static_cast<long long>(run.latencies.size()) == run.tiles;
            const bool noneBelowCost = minMs >= kControlCostMs;
            const bool resolved = minMs <= kControlCostMs + kControlResolutionMs;
            const bool queueingSeen = maxMs >= queueFloorMs;

            std::printf("\n  phase 0 -- CONTROL: the same instrument over a rasterizer costing %.1f ms"
                        " a tile, 1 worker\n", kControlCostMs);
            report("known cost", run.latencies, run.wallMs);
            std::printf("    tiles timed %zu of %lld   min %.3f ms (must be %.3f..%.3f)   max %.3f ms"
                        " (must be >= %.3f)   cost %.3f ms/tile\n",
                        run.latencies.size(), run.tiles, minMs, kControlCostMs,
                        kControlCostMs + kControlResolutionMs, maxMs, queueFloorMs, cost);
            if (!everyTileTimed || !noneBelowCost || !resolved || !queueingSeen) {
                std::printf("    DEAD INSTRUMENT: %s%s%s%s-- no latency figure below would be"
                            " evidence, so none is taken.\n",
                            everyTileTimed ? "" : "tiles went untimed; ",
                            noneBelowCost ? "" : "a tile read below its known cost; ",
                            resolved ? "" : "the first tile read far above its known cost, so"
                                            " delivery is quantized; ",
                            queueingSeen ? "" : "no tile read as having queued; ");
                std::fprintf(stderr, "A2 product: the latency control failed\n");
                backend->close(document->handle);
                return 1;
            }
            std::printf("    the instrument is live: it reads the known cost and the queue behind it\n");
        }

        if (stage4) {
            // Phase 0 has run -- the latency instrument is live -- and that is the only thing
            // phase 5 needs from this block. Phases 1 to 4 are Stage 2 T18's measurement and are
            // skipped rather than re-run: `--stage4` asks a different question of the same pipeline,
            // and re-measuring T18's figure here would invite the two to be compared as though one
            // had moved.
            backend->close(document->handle);
            return runStage4Workload(*backend, open, makeRasterizer, a2);
        }

        std::printf("\n  phase 1 -- A2's configuration, the probe's subject\n");
        std::printf("    %.0f %% zoom on a %.0f %%-scaled monitor  ->  %.1f dpi, %.3f px/pt\n",
                    kA2Zoom * 100.0, kA2MonitorScale * 100.0, a2.dpi, a2.rasterScale());

        for (int workers = 1; workers <= workerSweepMax; workers *= 2) {
            core::render::RenderCache cache;
            core::render::RenderScheduler::Config config;
            config.workerCount = workers;
            core::render::RenderScheduler scheduler(makeRasterizer, &cache, config);

            const RunResult run = measureLatency(scheduler, wholePage, *document, 0, a2.dpi);

            if (run.latencies.empty()) {
                std::fprintf(stderr, "A2 product: no tile was delivered -- no measurement\n");
                backend->close(document->handle);
                return 1;
            }
            char label[32];
            std::snprintf(label, sizeof(label), "%d worker%s", workers, workers == 1 ? "" : "s");
            report(label, run.latencies, run.wallMs);
        }
        backend->close(document->handle);
    }

    // =====================================================================================
    // PHASE 2 -- the whole corpus at three resolutions (D2-1: "over the whole corpus")
    // =====================================================================================
    std::vector<QString> corpusFiles;
    {
        QDirIterator walk(corpus.absolutePath(), QStringList{QStringLiteral("*.pdf")}, QDir::Files,
                          QDirIterator::Subdirectories);
        while (walk.hasNext()) corpusFiles.push_back(corpus.relativeFilePath(walk.next()));
        std::sort(corpusFiles.begin(), corpusFiles.end());
    }

    std::printf("\n  phase 2 -- the whole corpus, page 0 of every file that opens\n");
    long long corpusTiles = 0;
    int opened = 0;
    for (const int dpi : kSweepDpi) {
        core::render::RenderCache cache;
        core::render::RenderScheduler scheduler(makeRasterizer, &cache);

        RunResult run;
        std::map<std::string, Clock::time_point> sentAt;
        const core::render::RenderScheduler::SubscriptionToken token = scheduler.subscribeReady(
            [&](const RenderRequest& request, const core::RasterImage&) {
                const auto at =
                    sentAt.find(tileKey(request.document, request.pageIndex, request.tile));
                if (at != sentAt.end()) run.latencies.push_back(latencySince(at->second));
            });

        opened = 0;
        const Clock::time_point start = Clock::now();
        for (const QString& file : corpusFiles) {
            const QString password =
                file.startsWith(QLatin1String("encrypted/")) ? QStringLiteral("user") : QString{};
            const std::optional<OpenDocument> document = open(file, password);
            if (!document) continue;
            ++opened;
            renderPage(scheduler, wholePage, *document, 0, static_cast<double>(dpi), sentAt,
                       run);
            backend->close(document->handle);
        }
        run.wallMs = millisSince(start);
        scheduler.unsubscribeReady(token);
        corpusTiles = run.tiles;

        char label[32];
        std::snprintf(label, sizeof(label), "%d dpi", dpi);
        report(label, run.latencies, run.wallMs);
    }
    std::printf("    %d of %zu corpus files opened and rendered; the rest are refused BY DESIGN\n",
                opened, corpusFiles.size());

    // =====================================================================================
    // PHASE 3 -- O7's numbers: what a SHARED cache costs when several documents are open
    // =====================================================================================
    std::printf("\n  phase 3 -- O7: one cache, several documents (D2-14's instrumentation)\n");
    {
        // Eight documents open at once, rendered round-robin twice -- which is what a person with
        // eight tabs does, and is the only arrangement in which "shared or per-document" means
        // anything at all.
        std::vector<OpenDocument> documents;
        for (const QString& file : corpusFiles) {
            if (documents.size() >= 8) break;
            if (!file.startsWith(QLatin1String("ordinary/"))) continue;
            if (const std::optional<OpenDocument> document = open(file, QString{}))
                documents.push_back(*document);
        }

        // TWO CAPS, AND MEASURING ONLY ONE WOULD ANSWER O7 BY NOT ASKING IT. At the shipped 256 MiB
        // this workload fits, so a shared cache costs nothing and the answer is "shared, obviously".
        // At a cap small enough to evict, the same workload shows what sharing costs when it costs
        // something. The decision is about the second case, and reporting only the first is the
        // shape of measurement that confirms whatever the measurer already believed.
        const std::size_t caps[] = {256u * 1024u * 1024u, 24u * 1024u * 1024u};
        for (const std::size_t cap : caps) {
            core::render::RenderCache::Config config;
            config.memoryCapBytes = cap;
            core::render::RenderCache cache(config);
            core::render::RenderScheduler scheduler(makeRasterizer, &cache);

            RunResult run;
            std::map<std::string, Clock::time_point> sentAt;
            const core::render::RenderScheduler::SubscriptionToken token = scheduler.subscribeReady(
                [&](const RenderRequest&, const core::RasterImage&) {});

            for (int pass = 0; pass < 2; ++pass) {
                for (const OpenDocument& document : documents)
                    renderPage(scheduler, false, document, 0, 144.0, sentAt, run);
            }
            scheduler.unsubscribeReady(token);

            const core::render::CacheStats stats = cache.stats();
            std::printf("    cap %4zu MiB  lookups %lld   hits %lld (%.1f %%)   evictions %zu"
                        "   CROSS-DOCUMENT %zu   held %zu KiB in %zu\n",
                        cap / (1024u * 1024u), stats.lookups, stats.hits(),
                        stats.hitRate() * 100.0, cache.memoryTier().evictionCount(),
                        cache.crossDocumentEvictions(), cache.bytesHeld() / 1024,
                        cache.entryCount());
        }
        std::printf("    documents     %zu open at once, two passes each, 144 dpi\n",
                    documents.size());
        std::printf("    -> O7's number is the CROSS-DOCUMENT eviction count: what a shared cache\n"
                    "       costs a document that did nothing wrong.\n");

        for (const OpenDocument& document : documents) backend->close(document.handle);
    }

    // =====================================================================================
    // PHASE 4 -- D2-14: does the disk tier earn its place?
    // =====================================================================================
    std::printf("\n  phase 4 -- D2-14: the disk tier, which ships OFF until this measurement\n");
    {
        QTemporaryDir scratch;
        if (!scratch.isValid()) {
            std::fprintf(stderr, "A2 product: no scratch directory for the disk tier\n");
            return 1;
        }

        std::vector<OpenDocument> documents;
        for (const QString& file : corpusFiles) {
            if (documents.size() >= 8) break;
            if (!file.startsWith(QLatin1String("ordinary/"))) continue;
            if (const std::optional<OpenDocument> document = open(file, QString{}))
                documents.push_back(*document);
        }

        // The SAME workload twice, once without a disk tier and once with, so the two numbers
        // differ in exactly one thing.
        for (int withDisk = 0; withDisk <= 1; ++withDisk) {
            core::render::RenderCache::Config config;
            config.memoryCapBytes = 24u * 1024u * 1024u;
            core::render::RenderCache cache(config);
            if (withDisk != 0) {
                cache.setDiskTier(std::make_unique<core::render::DiskTileStore>(
                    scratch.path(), 256u * 1024u * 1024u));
            }
            core::render::RenderScheduler scheduler(makeRasterizer, &cache);

            RunResult run;
            std::map<std::string, Clock::time_point> sentAt;
            const core::render::RenderScheduler::SubscriptionToken token =
                scheduler.subscribeReady([&](const RenderRequest&, const core::RasterImage&) {});

            const Clock::time_point start = Clock::now();
            for (int pass = 0; pass < 2; ++pass) {
                for (const OpenDocument& document : documents)
                    renderPage(scheduler, false, document, 0, 144.0, sentAt, run);
            }
            run.wallMs = millisSince(start);
            scheduler.unsubscribeReady(token);

            const core::render::CacheStats stats = cache.stats();
            // THE NUMBER THAT DECIDES D2-14 is the per-tile cost, not the hit rate. A second tier
            // that raises the hit rate and the wall time together is not a cache, it is a slower
            // way to get the same pixels -- and a rendered tile here costs a fraction of a
            // millisecond, which is a very low bar for a disk read to beat.
            std::printf("    %-12s hits %lld of %lld (%.1f %%)   memory %lld   disk %lld"
                        "   promotions %lld   wall %.0f ms   %.3f ms/tile\n",
                        withDisk != 0 ? "with disk" : "memory only", stats.hits(), stats.lookups,
                        stats.hitRate() * 100.0, stats.memoryHits, stats.diskHits,
                        stats.promotions, run.wallMs,
                        run.tiles > 0 ? run.wallMs / static_cast<double>(run.tiles) : 0.0);
        }

        for (const OpenDocument& document : documents) backend->close(document.handle);
    }

    // =====================================================================================
    // The verdict is PRINTED, not returned
    // =====================================================================================
    std::printf("\n  budget        %.3f ms per tile for one 60 Hz frame\n", kFrameBudgetMs);
    std::printf("  corpus tiles  %lld per resolution\n", corpusTiles);
    std::printf("\n  Record the A2 verdict against the scale Stage2-Rendering-Testing-Guide.md 3\n"
                "  fixed BEFORE this measurement was taken. The probe's figure (T3, raw PDFium)\n"
                "  and this one measure different quantities: service time versus queued latency.\n"
                "  The comparable pair is the probe's p95 and this run's cost per tile.\n\n");

    return 0;
}
