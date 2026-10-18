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
// **PHASE 6, `--stage5`** (Stage 5 T3, D5-22) is A2 AT STAGE 5: the viewport while text extraction
// shares this pipeline's one engine thread -- see the phase's own header, below phase 5.
//
// **PHASE 7, `--stage6`** (Stage 6 T4, D6-24) is A2 AT STAGE 6: the viewport while annotation commands
// make the engine RELOAD the composed revision (D6-2), and how long an edit takes to reach the pixels --
// see the phase's own header, below phase 6.
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
// Phase 0 measures up to `kControlAttempts` times, each on a fresh scheduler, and the instrument is
// live when one attempt passes all three. Every attempt failing exits 1 BEFORE any figure is
// printed: under the exit-code rule below, a dead instrument is a measurement that was not possible. `--dead-instrument` and `--coarse-pump` are
// this control's own negative controls (convention 7), each breaking the instrument one way, and
// each must make the run fail on every attempt. `--stall-once` is the retry's positive control: one
// stalled tile, which a later attempt must read past.
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
#include "app/BackendTextExtractor.h"
#include "core/backend.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/Annotation.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "core/doc/Revision.h"
#include "core/render/DiskTileStore.h"
#include "core/render/DpiPolicy.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "core/render/TileGrid.h"
#include "core/text/DocumentIndex.h"
#include "core/text/FolderSearch.h"
#include "core/text/PageTextModel.h"
#include "core/text/SearchEngine.h"
#include "pdf/BackendFactory.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/render/PdfiumInit.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
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

// How many times phase 0 may measure before it calls the instrument dead. The quantized pump I-41
// found is a property of the CODE and reads quantized on every attempt, so a retry cannot pass it
// (`--coarse-pump` must still fail all of them). What a retry absorbs is a one-off STALL: on
// GitHub's hosted `windows-2025` runner (run 39144214181, 2026-10-18) one attempt read min 34.128,
// p50 40.661, max 80.777 ms at a measured 2.023 ms a tile: the first delivery read ~32 ms late
// while the LAST read on time (40 x 2 ms), so the worker kept pace and delivery stalled once and
// caught up -- not I-41's pump, which delays every tile to a tick, and not a slow worker, which
// would push the last tile out too. The same phase passed in three other benches of that run. One attempt made the suite's
// exit code depend on the runner's neighbours, which the file header's exit-code rule forbids.
constexpr int kControlAttempts = 3;

// The two NEGATIVE controls for phase 0 (convention 7), each breaking the instrument one way:
// `--dead-instrument` makes every latency read zero (I-29's shape), and `--coarse-pump` restores the
// tick-quantized pump I-41 found. Either must make the run fail at phase 0.
bool g_deadInstrument = false;
bool g_coarsePump = false;

// And its POSITIVE control: `--stall-once` adds `kStallOnceMs` to the first known-cost tile of the
// process only -- a one-off stall made on purpose. The runner's was in delivery and this one is in
// the raster, which phase 0's resolution bound reads the same way: a first tile far above its known
// cost (42.099 ms against 2.124 ms unstalled, measured 2026-10-19). Attempt 1 must then read not
// live and a later attempt live, so the run passes; with `kControlAttempts` at 1 the same flag must
// fail, which is what shows the retry is the thing that absorbed it.
std::atomic<bool> g_stallOnce{false};
constexpr double kStallOnceMs = 40.0;

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
        const double cost = kControlCostMs + (g_stallOnce.exchange(false) ? kStallOnceMs : 0.0);
        while (millisSince(start) < cost) {
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


// =============================================================================================
// PHASE 6 -- STAGE 5's WORKLOAD (D5-22): the viewport while TEXT shares the one engine thread
// =============================================================================================
//
// Stage 5 puts a second kind of job on the render thread (D5-3): extracting a page's text, which
// a first search, a folder search and a comparison each do by the hundred. It goes in the fourth
// lane, `Background`, below everything a person is looking at -- and strict priority is not
// preemption. **A text job already inside PDFium runs to its end**, so one page's extraction time is
// the longest a visible tile can wait behind background work. D5-22 says that has to be measured
// BEFORE the find bar, the search pane and the comparison queue anything here, because a no-go moves
// extraction off this thread and all three would be redesigned.
//
// WHAT A SAMPLE IS: the same as phases 1 and 5 -- the VIEWPORT's queued latency, enqueue to
// delivery, for the visible tiles of a 1920x1080 viewport at A2's configuration. Text jobs' own
// latency is not graded: a search result arriving late is a slow search; a tile arriving late is a
// stutter (`Stage5-TextSearch-Testing-Guide.md` §3.1).
//
// THE SHAPE OF A ROUND is a person reading while the machine works: a scroll -- a viewport's tiles
// on a page not seen before, all at `Visible` -- then `kStage5GapMs` of reading, during which the
// background work runs. Scrolling back to back would starve the background entirely (that is what
// strict priority does), measure nothing about sharing, and never finish the index.
//
// THE BACKGROUND IS FED, NOT DUMPED. At most `kBackgroundWindow` background jobs are queued at once,
// topped up as they complete -- which is how an index or a folder search has to feed the scheduler
// anyway: `Config::maxPending` is 4,096, a 900-page comparison is 3,600 jobs, and a queue that
// overflowed would DROP the lowest-priority work (`droppedOverflow`) silently. The count is printed.
//
// WHAT THE WORKER DID IS STAMPED ON THE WORKER. The rasterizer and the extractor the scheduler is
// given are thin timing wrappers around the shipped adapters, recording when each job STARTED and
// how long the engine took. That is what A-28's per-page extraction distribution is read from, and
// what the inversion count below is computed from -- not from the pump, which only sees deliveries.
//
// THE INVERSION COUNT (guide A-29): a background job that was PICKED while a viewport tile waited
// is the page waiting on work nobody is watching. The one background job already inside the engine
// when the tile arrives was picked BEFORE it, so it is not counted -- it is the bounded wait D5-3
// accepts, and the per-page maximum is its size. The bound on the count is therefore ZERO. It is the
// scheduler's own `Stats::startedWhileMoreUrgentWaited`, taken at the pick under the lock that makes
// it (S5 guide I-12): a count inferred from start stamps taken after the pick counted a column's
// first job whenever the first tile was queued between its pick and its stamp.

constexpr int kStage5GapMs = 50;
constexpr int kStage5ViewerRounds = 60;
constexpr int kStage5MinRounds = 20;
constexpr int kStage5MaxRounds = 250;
constexpr int kBackgroundWindow = 32;
constexpr int kStage5IndexRuns = 3;
constexpr double kComparisonDpi = 72.0;   // D5-17's appearance pass

struct ServiceLog {
    struct Entry {
        Clock::time_point start;
        double ms = 0.0;
        bool text = false;
        std::string key;
    };
    std::mutex mutex;
    std::vector<Entry> entries;

    void add(Entry entry)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        entries.push_back(std::move(entry));
    }
};

std::string textKey(core::DocumentHandle document, int page)
{
    return "text:" + std::to_string(document.id) + "/" + std::to_string(page);
}

class TimingRasterizer final : public core::IPageRasterizer {
public:
    TimingRasterizer(core::IDocumentBackend& backend, ServiceLog& log) : inner_(backend), log_(log) {}
    core::Result<core::RasterImage> raster(const RenderRequest& request) override
    {
        const Clock::time_point start = Clock::now();
        core::Result<core::RasterImage> result = inner_.raster(request);
        log_.add({start, millisSince(start), false, scaledTileKey(request)});
        return result;
    }

private:
    app::BackendRasterizer inner_;
    ServiceLog& log_;
};

class TimingExtractor final : public core::ITextExtractor {
public:
    TimingExtractor(core::IDocumentBackend& backend, ServiceLog& log) : inner_(backend), log_(log) {}
    core::Status extractPage(core::DocumentHandle document, int page,
                             core::text::PageTextModel& into) override
    {
        const Clock::time_point start = Clock::now();
        core::Status status = inner_.extractPage(document, page, into);
        log_.add({start, millisSince(start), true, textKey(document, page)});
        return status;
    }

private:
    app::BackendTextExtractor inner_;
    ServiceLog& log_;
};

enum class Stage5Load { None, Index, Folder, Comparison };

struct Stage5Outcome {
    std::vector<double> viewport;
    std::vector<double> extractionMs;   // service time, per page, on the worker
    std::vector<double> tileMs;         // service time, per viewport tile, on the worker
    double    wallMs = 0.0;
    int       rounds = 0;
    long long viewportTiles = 0;
    long long viewportTilesRendered = 0;   // not served from the cache
    long long backgroundPlanned = 0;
    long long backgroundDone = 0;
    long long backgroundFailed = 0;
    long long backgroundUndone = 0;
    long long inversions = 0;
    long long filesOpened = 0;
    double    openMaxMs = 0.0;
    QString   slowestOpen;              // which file, so a stall can be traced to its cause
    double    slowestExtractionMs = 0.0;
    QString   slowestExtraction;        // "file, page n"
    // WHERE THE SLOWEST VIEWPORT TILE'S WAIT WENT: queued -> the worker started it -> the worker
    // finished it -> the pump delivered it. A long first leg is the engine thread busy; a long last
    // leg is the PUMP not running -- two different causes that one latency figure cannot separate.
    double    slowestTileWaitMs = 0.0, slowestTileServiceMs = 0.0, slowestTileDeliveryMs = 0.0;
    std::string slowestTileKey;
    std::string workerBefore;   // what the worker did last before the slowest tile, and when
    core::render::CacheStats cacheStats;
    core::render::RenderScheduler::Stats schedulerStats;
    std::size_t evictions = 0;
    std::size_t crossDocumentEvictions = 0;
};

const char* stage5LoadName(Stage5Load load)
{
    switch (load) {
    case Stage5Load::None: return "viewer";
    case Stage5Load::Index: return "+ index build";
    case Stage5Load::Folder: return "+ folder search";
    case Stage5Load::Comparison: return "+ comparison";
    }
    return "?";
}

Stage5Outcome runStage5Column(core::IDocumentBackend& backend, const OpenDocument& reading,
                              const std::vector<OpenDocument>& compared, const QString& folder,
                              const Resolution& view, std::size_t cap, Stage5Load load)
{
    Stage5Outcome outcome;
    ServiceLog log;
    std::vector<core::DocumentHandle> folderDocuments;

    {
        core::render::RenderCache::Config cacheConfig;
        cacheConfig.memoryCapBytes = cap;
        core::render::RenderCache cache(cacheConfig);
        core::render::RenderScheduler scheduler(
            [&]() -> std::unique_ptr<core::IPageRasterizer> {
                return std::make_unique<TimingRasterizer>(backend, log);
            },
            [&]() -> std::unique_ptr<core::ITextExtractor> {
                return std::make_unique<TimingExtractor>(backend, log);
            },
            &cache, core::render::RenderScheduler::Config{});

        // --- the background, as a backlog fed through a window ---------------------------------
        struct Work {
            bool text = true;
            core::render::TextRequest textRequest;
            RenderRequest raster;
        };
        std::deque<Work> backlog;
        long long outstanding = 0;

        const Resolution comparisonRes = resolutionAt(kComparisonDpi);
        const auto planDocument = [&](const OpenDocument& document, bool withRasters) {
            for (int page = 0; page < document.pages->count(); ++page) {
                backlog.push_back(Work{true, core::render::TextRequest{document.handle, page}, {}});
                ++outcome.backgroundPlanned;
                if (!withRasters) continue;
                const core::Page* info = document.pages->at(page);
                if (info == nullptr) continue;
                const TileRect bounds = TileGrid::pageBounds(*info, comparisonRes);
                if (bounds.isEmpty()) continue;
                RenderRequest request = requestFor(document.handle, page, bounds, kComparisonDpi);
                request.flags = RenderFlags::None;
                backlog.push_back(Work{false, {}, request});
                ++outcome.backgroundPlanned;
            }
        };

        if (load == Stage5Load::Index) planDocument(reading, false);
        if (load == Stage5Load::Comparison) {
            // Page by page across the two documents, as an aligner would ask for them.
            for (int page = 0; page < compared[0].pages->count(); ++page) {
                for (const OpenDocument& document : compared) {
                    if (page >= document.pages->count()) continue;
                    backlog.push_back(Work{true, core::render::TextRequest{document.handle, page}, {}});
                    ++outcome.backgroundPlanned;
                    const core::Page* info = document.pages->at(page);
                    const TileRect bounds = TileGrid::pageBounds(*info, comparisonRes);
                    RenderRequest request = requestFor(document.handle, page, bounds, kComparisonDpi);
                    request.flags = RenderFlags::None;
                    backlog.push_back(Work{false, {}, request});
                    ++outcome.backgroundPlanned;
                }
            }
        }

        // --- the folder search: THE SHIPPED `core/text/FolderSearch` (Stage 5 T9) ---------------
        // Until T9 a helper thread stood in for it here, opening each file whole off the pump -- the
        // arrangement T3 measured at 17.68-17.93 ms p95 with a worst tile of 23.5-24.8 ms. Since T9 the column
        // IS the product's folder search: every file opened in pieces, each engine piece a task on this
        // scheduler's worker at `Background`, the rest on the folder search's helper, every page's text asked
        // through `DocumentIndex` and matched by `SearchEngine` -- the assembly `ui/panes/SearchPane` runs.
        // Its open pieces are stamped on the thread that ran them and logged as background work, so the
        // inversion count sees them as it sees a page's text.
        std::mutex openedMutex;
        std::deque<OpenDocument> opened;
        std::atomic<bool> openerDone{load != Stage5Load::Folder};
        std::thread opener;
        std::vector<double> openMs;
        std::map<std::uint64_t, QString> names;
        names[reading.handle.id] = reading.name;
        for (const OpenDocument& document : compared) names[document.handle.id] = document.name;
        std::unique_ptr<core::text::DocumentIndex> folderIndex;
        std::unique_ptr<core::text::SearchEngine> folderEngine;
        std::unique_ptr<core::text::FolderSearch> folderSearch;
        if (load == Stage5Load::Folder) {
            folderIndex = std::make_unique<core::text::DocumentIndex>(scheduler);
            folderEngine = std::make_unique<core::text::SearchEngine>(*folderIndex);
            folderSearch = std::make_unique<core::text::FolderSearch>(backend, scheduler, *folderIndex, *folderEngine);
            folderSearch->setPieceObserver([&](bool engine, double ms) {
                const Clock::time_point end = Clock::now();
                const Clock::time_point began =
                    end - std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double, std::milli>(ms));
                if (!engine) return;
                log.add({began, ms, false, "open-piece"});
                const std::lock_guard<std::mutex> lock(openedMutex);
                openMs.push_back(ms);
                if (ms > outcome.openMaxMs) outcome.openMaxMs = ms;
            });
            core::text::FolderSearch::Request request;
            request.folder = folder;
            request.recursive = true;
            request.query.pattern = QStringLiteral("RawPage");
            core::text::FolderSearch::Handlers handlers;
            handlers.file = [&](core::text::FileResult) { ++outcome.filesOpened; };
            handlers.finished = [&](const core::text::FolderSearch::Summary&) { openerDone.store(true); };
            if (!folderSearch->start(request, handlers)) openerDone.store(true);
        }

        const auto feed = [&] {
            {
                const std::lock_guard<std::mutex> lock(openedMutex);
                while (!opened.empty()) {
                    folderDocuments.push_back(opened.front().handle);
                    planDocument(opened.front(), false);
                    opened.pop_front();
                }
            }
            while (outstanding < kBackgroundWindow && !backlog.empty()) {
                const Work work = backlog.front();
                backlog.pop_front();
                ++outstanding;
                if (work.text)
                    scheduler.enqueueText(work.textRequest, RenderPriority::Background);
                else
                    scheduler.enqueue(work.raster, RenderPriority::Background,
                                      core::render::CachePolicy::Bypass);
            }
        };

        // --- the viewport, and its samples -----------------------------------------------------
        std::map<std::string, Clock::time_point> viewportSent;
        std::map<std::string, Clock::time_point> awaitingViewport;
        std::map<std::string, Clock::time_point> viewportDelivered;
        const core::render::RenderScheduler::SubscriptionToken tiles = scheduler.subscribeReady(
            [&](const RenderRequest& request, const core::RasterImage&) {
                const std::string key = scaledTileKey(request);
                const auto at = awaitingViewport.find(key);
                if (at != awaitingViewport.end()) {
                    outcome.viewport.push_back(latencySince(at->second));
                    viewportDelivered[key] = Clock::now();
                    awaitingViewport.erase(at);
                    return;
                }
                // A comparison raster: consumed here and released, as D5-4 says.
                --outstanding;
                ++outcome.backgroundDone;
            });
        const core::render::RenderScheduler::SubscriptionToken texts = scheduler.subscribeText(
            [&](const core::render::TextRequest&, const core::render::TextResult& result) {
                --outstanding;
                ++outcome.backgroundDone;
                if (!result) ++outcome.backgroundFailed;
            });

        const auto pumpOnce = [&] {
            scheduler.deliverReady();
            if (folderSearch) {
                folderEngine->deliverReady();
                folderSearch->deliverReady();
            }
            feed();
            std::this_thread::yield();
        };
        const auto backgroundFinished = [&] {
            return openerDone.load() && backlog.empty() && outstanding <= 0 && [&] {
                const std::lock_guard<std::mutex> lock(openedMutex);
                return opened.empty();
            }();
        };

        const TileGrid grid;
        const Clock::time_point start = Clock::now();
        feed();
        for (int round = 0;; ++round) {
            const bool enough = load == Stage5Load::None ? round >= kStage5ViewerRounds
                                                         : round >= kStage5MinRounds
                                                               && backgroundFinished();
            if (enough || round >= kStage5MaxRounds) break;
            outcome.rounds = round + 1;

            // A scroll: a page not seen before, its viewport's visible tiles at Visible.
            const int page = (round * 7) % reading.pages->count();
            const core::Page* info = reading.pages->at(page);
            const TileRect bounds = TileGrid::pageBounds(*info, view);
            TileRect viewport = bounds;
            viewport.width = std::min(bounds.width, kViewportWidth);
            viewport.height = std::min(bounds.height, kViewportHeight);
            for (const TileRect& tile : grid.tilesFor(*info, view, viewport).visible) {
                const RenderRequest request = requestFor(reading.handle, page, tile, view.dpi);
                const std::string key = scaledTileKey(request);
                const long long servedBefore = scheduler.stats().servedFromCache;
                const Clock::time_point sent = Clock::now();
                awaitingViewport[key] = sent;
                scheduler.enqueue(request, RenderPriority::Visible);
                // When the tile was IN THE QUEUE -- after `enqueue()` returned -- for the slowest tile's
                // account of what the worker did before it. It was also the inversion count's reference
                // until S5 guide I-12, and moving it here from `sent` at T3 narrowed the same race that
                // I-12 then closed by counting at the pick, in the scheduler.
                viewportSent[key] = Clock::now();
                ++outcome.viewportTiles;
                if (scheduler.stats().servedFromCache == servedBefore) ++outcome.viewportTilesRendered;
            }
            while (!awaitingViewport.empty()) pumpOnce();

            // Reading: the background has the engine.
            const Clock::time_point reading0 = Clock::now();
            while (millisSince(reading0) < kStage5GapMs) pumpOnce();
        }
        outcome.wallMs = millisSince(start);

        // THE FOLDER SEARCH'S LAST CLOSES run before anything is abandoned: each file is closed by an
        // exclusive task, and `cancelAll` below would drop the ones still queued.
        if (folderSearch) {
            for (int i = 0; i < 200 && scheduler.pendingCount() + scheduler.runningCount() > 0; ++i) pumpOnce();
            // The index's text jobs went through the subscription below, which counts the bench's own work:
            // what the folder search planned is what it did.
            outcome.backgroundPlanned = outcome.backgroundDone;
            outstanding = 0;
        }

        // Whatever the background did not reach is reported, then abandoned.
        outcome.backgroundUndone = static_cast<long long>(backlog.size()) + std::max(0LL, outstanding);
        scheduler.cancelAll();
        if (opener.joinable()) opener.join();
        scheduler.waitForIdle(30000);
        scheduler.deliverReady();
        {
            const std::lock_guard<std::mutex> lock(openedMutex);
            for (const OpenDocument& document : opened) folderDocuments.push_back(document.handle);
            opened.clear();
        }
        scheduler.unsubscribeReady(tiles);
        scheduler.unsubscribeText(texts);

        outcome.cacheStats = cache.stats();
        outcome.schedulerStats = scheduler.stats();
        outcome.evictions = cache.memoryTier().evictionCount();
        outcome.crossDocumentEvictions = cache.crossDocumentEvictions();
        if (!folderSearch) outcome.filesOpened = static_cast<long long>(openMs.size());
        folderSearch.reset();   // before the scheduler: it holds a task subscription and a helper thread
        folderEngine.reset();
        folderIndex.reset();

        // --- what the worker did ------------------------------------------------------------
        std::map<std::string, Clock::time_point> viewportStarted;
        {
            const std::lock_guard<std::mutex> lock(log.mutex);
            for (const ServiceLog::Entry& entry : log.entries) {
                if (entry.text) {
                    outcome.extractionMs.push_back(entry.ms);
                    if (entry.ms > outcome.slowestExtractionMs) {
                        // "text:<handle>/<page>" back to a file name and a 1-based page number.
                        const std::size_t colon = entry.key.find(':');
                        const std::size_t slash = entry.key.find('/');
                        const std::uint64_t id = std::stoull(entry.key.substr(colon + 1, slash - colon - 1));
                        const int page = std::stoi(entry.key.substr(slash + 1));
                        outcome.slowestExtractionMs = entry.ms;
                        outcome.slowestExtraction = QStringLiteral("%1, page %2")
                                                        .arg(names.count(id) != 0 ? names[id] : QStringLiteral("?"))
                                                        .arg(page + 1);
                    }
                } else if (viewportSent.count(entry.key) != 0) {
                    outcome.tileMs.push_back(entry.ms);
                    viewportStarted[entry.key] = entry.start;
                    const auto delivered = viewportDelivered.find(entry.key);
                    if (delivered != viewportDelivered.end()) {
                        const double total = std::chrono::duration<double, std::milli>(
                                                 delivered->second - viewportSent[entry.key]).count();
                        if (total > outcome.slowestTileWaitMs + outcome.slowestTileServiceMs
                                        + outcome.slowestTileDeliveryMs) {
                            outcome.slowestTileWaitMs = std::chrono::duration<double, std::milli>(
                                                            entry.start - viewportSent[entry.key]).count();
                            outcome.slowestTileServiceMs = entry.ms;
                            outcome.slowestTileDeliveryMs = total - outcome.slowestTileWaitMs - entry.ms;
                            outcome.slowestTileKey = entry.key;
                        }
                    }
                }
            }
        }
        if (!outcome.slowestTileKey.empty()) {
            const std::lock_guard<std::mutex> lock(log.mutex);
            const Clock::time_point queued = viewportSent[outcome.slowestTileKey];
            const Clock::time_point began = viewportStarted[outcome.slowestTileKey];
            const ServiceLog::Entry* previous = nullptr;
            for (const ServiceLog::Entry& entry : log.entries)
                if (entry.start < began && (previous == nullptr || entry.start > previous->start))
                    previous = &entry;
            if (previous != nullptr) {
                char text[200];
                std::snprintf(text, sizeof(text), "%s started %.1f ms after the tile was queued, took %.1f ms",
                              previous->key.c_str(),
                              std::chrono::duration<double, std::milli>(previous->start - queued).count(),
                              previous->ms);
                outcome.workerBefore = text;
            }
        }
        // THE INVERSIONS ARE THE SCHEDULER'S OWN COUNT, taken at the pick (S5 guide I-12). Until I-12 they were
        // inferred here from the workers' START STAMPS -- a background job stamped between a tile's enqueue and
        // its start -- and the stamp is taken in the timing wrapper, after the pick: a column's first job,
        // picked before the first tile was queued and stamped 0.2-0.5 us after it, was counted 0-2 times a
        // run (probed: pick ordinal 1, every one).
        outcome.inversions = outcome.schedulerStats.startedWhileMoreUrgentWaited;
    }   // the scheduler is idle and gone before any document it served is closed

    for (const core::DocumentHandle handle : folderDocuments) backend.close(handle);
    return outcome;
}

void reportStage5(const char* label, const Stage5Outcome& outcome)
{
    report(label, outcome.viewport, outcome.wallMs);
    std::printf("      background %lld of %lld jobs done (%lld failed, %lld not reached) over %d rounds,"
                " %.1f s",
                outcome.backgroundDone, outcome.backgroundPlanned, outcome.backgroundFailed,
                outcome.backgroundUndone, outcome.rounds, outcome.wallMs / 1000.0);
    if (outcome.filesOpened > 0)
        std::printf("; %lld files searched, the longest engine piece of an open %.2f ms", outcome.filesOpened,
                    outcome.openMaxMs);
    std::printf("\n");
    std::printf("      priority   %lld inversion(s) against a bound of 0 -- background jobs STARTED"
                " while a viewport tile waited\n", outcome.inversions);
    std::printf("      slowest    viewport tile: %.1f ms queued before the worker took it, %.1f ms in the"
                " engine, %.1f ms before the pump delivered it\n",
                outcome.slowestTileWaitMs, outcome.slowestTileServiceMs, outcome.slowestTileDeliveryMs);
    if (!outcome.workerBefore.empty())
        std::printf("                 the worker's job before it: %s\n", outcome.workerBefore.c_str());
    if (!outcome.extractionMs.empty()) {
        const double p50 = percentile(outcome.extractionMs, 0.50);
        const double p95 = percentile(outcome.extractionMs, 0.95);
        const double max = percentile(outcome.extractionMs, 1.00);
        std::printf("      extraction %zu pages on the worker: p50 %.3f  p95 %.3f  max %.3f ms (%s)\n",
                    outcome.extractionMs.size(), p50, p95, max, qPrintable(outcome.slowestExtraction));
    }
    // D5-4 AND O7: text work, and the comparison's bypassing rasters, insert NOTHING into the tile
    // cache -- so its insertions are exactly the viewport tiles that were rendered, and its lookups
    // exactly the viewport tiles that were asked for.
    const bool textKeptOut = outcome.cacheStats.insertions == outcome.viewportTilesRendered
                             && outcome.cacheStats.lookups == outcome.viewportTiles;
    std::printf("      cache      lookups %lld (viewport tiles asked %lld)   insertions %lld (viewport"
                " tiles rendered %lld)   evictions %zu   CROSS-DOCUMENT %zu   -> %s\n",
                outcome.cacheStats.lookups, outcome.viewportTiles, outcome.cacheStats.insertions,
                outcome.viewportTilesRendered, outcome.evictions, outcome.crossDocumentEvictions,
                textKeptOut ? "background work inserted NOTHING" : "BACKGROUND WORK REACHED THE CACHE");
    std::printf("      scheduler  enqueued %lld (background %lld, bypassing %lld)   started %lld"
                "   delivered %lld   droppedOverflow %lld   cancelledBeforeStart %lld"
                "   droppedCancelled %lld\n",
                outcome.schedulerStats.enqueued, outcome.schedulerStats.enqueuedBackground,
                outcome.schedulerStats.enqueuedBypassingCache, outcome.schedulerStats.started,
                outcome.schedulerStats.delivered, outcome.schedulerStats.droppedOverflow,
                outcome.schedulerStats.cancelledBeforeStart, outcome.schedulerStats.droppedCancelled);
}

int runStage5Workload(core::IDocumentBackend& backend, const OpenFn& open, const Resolution& a2)
{
    const Resolution view = resolutionAt(a2.dpi);
    std::printf("\n  phase 6 -- STAGE 5's WORKLOAD (D5-22): the viewport while text shares the engine"
                " thread\n");
    std::printf("    viewport      at A2's configuration -> %.1f dpi, %.3f px/pt, %dx%d, at Visible,"
                " a fresh page each round\n",
                view.dpi, view.rasterScale(), kViewportWidth, kViewportHeight);
    std::printf("    rounds        a scroll, then %d ms of reading while the background runs;"
                " background fed %d jobs at a time\n", kStage5GapMs, kBackgroundWindow);

    const std::optional<OpenDocument> reading = open(QStringLiteral("ordinary/text-900.pdf"), QString{});
    const std::optional<OpenDocument> v1 = open(QStringLiteral("ordinary/text-900.pdf"), QString{});
    const std::optional<OpenDocument> v2 = open(QStringLiteral("ordinary/text-900.pdf"), QString{});
    const std::optional<OpenDocument> a2page = open(QStringLiteral("a2-text-page.pdf"), QString{});
    if (!reading || !v1 || !v2 || !a2page) {
        std::fprintf(stderr, "A2 at Stage 5: text-900.pdf or a2-text-page.pdf would not open --"
                             " no measurement\n");
        return 1;
    }
    std::printf("    document      %s, %d pages -- read in the viewport and indexed in the"
                " background, ONE handle\n",
                qPrintable(reading->name), reading->pages->count());
    std::printf("    comparison    the same file opened TWICE, two handles: every page's text and a"
                " %.0f-dpi raster of it, at Background, bypassing the cache (D5-4, D5-17)\n",
                kComparisonDpi);

    // The folder: a SCRATCH COPY of the corpus tree, because a search over the tester's own files
    // would depend on them (`Stage5-TextSearch-Plan.md` §5.6).
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        std::fprintf(stderr, "A2 at Stage 5: no scratch directory -- no measurement\n");
        return 1;
    }
    int copied = 0;
    {
        const QDir corpus(QStringLiteral(RAWPAGE_CORPUS_DIR));
        QDirIterator walk(corpus.absolutePath(), QStringList{QStringLiteral("*.pdf")}, QDir::Files,
                          QDirIterator::Subdirectories);
        while (walk.hasNext()) {
            const QString source = walk.next();
            const QString target = scratch.filePath(corpus.relativeFilePath(source));
            QDir().mkpath(QFileInfo(target).absolutePath());
            if (QFile::copy(source, target)) ++copied;
        }
    }
    std::printf("    folder        a scratch copy of the corpus tree, %d PDFs, nested; searched by the shipped"
                " FolderSearch -- each file opened in pieces at Background, every page's text\n", copied);

    // A-28's second subject: the dense A2 page, extracted twenty times -- one page, so its
    // distribution is the engine's variance on it.
    std::vector<double> a2Extraction;
    {
        core::ITextExtractor* extractor = backend.textExtractor(a2page->handle);
        for (int i = 0; i < 20 && extractor != nullptr; ++i) {
            core::text::PageTextModel model;
            const Clock::time_point start = Clock::now();
            (void)extractor->extractPage(a2page->handle, 0, model);
            a2Extraction.push_back(millisSince(start));
        }
    }

    const std::vector<OpenDocument> compared{*v1, *v2};
    const std::size_t caps[] = {256u * 1024u * 1024u, 24u * 1024u * 1024u};
    double viewerP95 = 0.0, folderP95 = 0.0, comparisonP95 = 0.0;
    std::vector<double> indexP95;
    long long totalInversions = 0;
    bool keptOut = true;
    Stage5Outcome firstIndex;
    for (const std::size_t cap : caps) {
        std::printf("\n    cap %4zu MiB\n", cap / (1024u * 1024u));
        const bool shipped = cap == caps[0];
        const struct { Stage5Load load; int runs; } columns[] = {
            {Stage5Load::None, 1},
            {Stage5Load::Index, shipped ? kStage5IndexRuns : 1},
            {Stage5Load::Folder, 1},
            {Stage5Load::Comparison, 1},
        };
        for (const auto& column : columns) {
            for (int run = 0; run < column.runs; ++run) {
                const Stage5Outcome outcome = runStage5Column(backend, *reading, compared,
                                                              scratch.path(), view, cap, column.load);
                if (outcome.viewport.empty()) {
                    std::fprintf(stderr, "A2 at Stage 5: no viewport tile was delivered --"
                                         " no measurement\n");
                    return 1;
                }
                char label[40];
                std::snprintf(label, sizeof(label), "%s%s VIEWPORT", stage5LoadName(column.load),
                              column.runs > 1 ? (" #" + std::to_string(run + 1)).c_str() : "");
                reportStage5(label, outcome);
                totalInversions += outcome.inversions;
                keptOut = keptOut && outcome.cacheStats.insertions == outcome.viewportTilesRendered
                          && outcome.cacheStats.lookups == outcome.viewportTiles;
                if (!shipped) continue;
                const double p95 = percentile(outcome.viewport, 0.95);
                switch (column.load) {
                case Stage5Load::None: viewerP95 = p95; break;
                case Stage5Load::Index:
                    indexP95.push_back(p95);
                    if (run == 0) firstIndex = outcome;
                    break;
                case Stage5Load::Folder: folderP95 = p95; break;
                case Stage5Load::Comparison: comparisonP95 = p95; break;
                }
            }
        }
    }

    std::printf("\n    A-28 -- ONE PAGE's EXTRACTION, the longest a visible tile can wait behind"
                " background work (no preemption, D5-3):\n");
    {
        const double p50 = percentile(firstIndex.extractionMs, 0.50);
        const double p95 = percentile(firstIndex.extractionMs, 0.95);
        const double max = percentile(firstIndex.extractionMs, 1.00);
        std::printf("      text-900.pdf, %zu pages ............ p50 %7.3f  p95 %7.3f  max %7.3f ms\n",
                    firstIndex.extractionMs.size(), p50, p95, max);
        const double a50 = percentile(a2Extraction, 0.50);
        const double a95 = percentile(a2Extraction, 0.95);
        const double amax = percentile(a2Extraction, 1.00);
        std::printf("      a2-text-page.pdf, x%zu ............... p50 %7.3f  p95 %7.3f  max %7.3f ms\n",
                    a2Extraction.size(), a50, a95, amax);
        const double t50 = percentile(firstIndex.tileMs, 0.50);
        const double t95 = percentile(firstIndex.tileMs, 0.95);
        const double tmax = percentile(firstIndex.tileMs, 1.00);
        std::printf("      beside it, one viewport TILE ...... p50 %7.3f  p95 %7.3f  max %7.3f ms\n",
                    t50, t95, tmax);
        std::printf("      the largest single extraction is %s the %.3f ms frame budget\n",
                    std::max(max, amax) < kFrameBudgetMs ? "INSIDE" : "OUTSIDE", kFrameBudgetMs);
    }

    std::printf("\n    WHAT MOVED THE FIGURE, at the shipped 256 MiB cap -- the viewport's p95:\n");
    std::printf("      one viewport, nothing behind it ...................... %7.3f ms\n", viewerP95);
    for (std::size_t i = 0; i < indexP95.size(); ++i)
        std::printf("      + text-900's whole index at Background, run %zu ........ %7.3f ms   (%+.3f)\n",
                    i + 1, indexP95[i], indexP95[i] - viewerP95);
    std::printf("      + a folder search over the corpus copy ............... %7.3f ms   (%+.3f)\n",
                folderP95, folderP95 - viewerP95);
    std::printf("      + a 900-page comparison's text and rasters ........... %7.3f ms   (%+.3f)\n",
                comparisonP95, comparisonP95 - viewerP95);
    std::printf("      frame budget ......................................... %7.3f ms\n",
                kFrameBudgetMs);
    std::printf("      priority inversions, every column and cap ............ %lld (bound 0)\n",
                totalInversions);
    std::printf("      text work in the tile cache, every column and cap .... %s\n",
                keptOut ? "none" : "SOME -- D5-4 is not holding");
    std::printf("      Stage 4 T2, + pane (phase 5) ......................... p95 10.59-10.74 ms\n");
    std::printf("      Stage 4 T14, the shipped panes (bench_surface) ....... p95 7.03-8.56 ms\n");
    std::printf("      Stage 2 T18, one viewport ............................ p95 13.03-13.20 ms\n");
    std::printf("\n    Grade on Stage5-TextSearch-Testing-Guide.md 3.1 and record it in Tech-Stack-Plan.md"
                " 3.3, LABELLED\n    as Stage 5's workload; O7's row gains the cache lines above"
                " (guide A-30). This bench never fails\n    on speed: its exit code is about whether"
                " the measurement was possible.\n\n");

    for (const OpenDocument* document : {&*reading, &*v1, &*v2, &*a2page}) backend.close(document->handle);
    return 0;
}


// =============================================================================================
// PHASE 7 -- A2 AT STAGE 6 (Stage 6 T4, D6-24), `--stage6`
// =============================================================================================
//
// D6-2 makes every annotation command RELOAD the engine: the journal's show step composes the update
// Save would write, verifies it as Save does (on the UI thread, inside `CommandJournal::run`), and the
// next engine call swaps `original ⧺ update` in through `FPDF_LoadCustomDocument`. So two costs land on
// the pipeline that Stages 2 to 5 never had: the compose-and-verify on the thread that also pumps the
// tiles, and the reload on the one engine thread. This phase measures both against the scale
// `Stage6-Annotations-Testing-Guide.md` §3.1 fixed before it ran.
//
// THE WORKLOAD, per file: for `g_stage6Seconds` (30 by default -- §3.1's figure), ONE `AddAnnotation`
// every 100 ms on the scope page, and half a period later a SCROLL -- the visible tiles of a 1920x1080
// viewport over another page, at A2's configuration, at `Visible`. After each edit the scope page's own
// viewport is asked for again at `Visible`, as the page view showing it would.
//
//   * THE VIEWPORT -- enqueue to delivery of the scroll's tiles: D2-1's quantity, comparable with every
//     earlier stage's. A column with no edits ("viewer") is run first over the same file, so the two
//     subtract.
//   * EDIT-TO-PIXELS -- from `CommandJournal::run` RETURNING to the delivery of the first tile of the
//     scope page that covers the new annotation, at the edit's revision or later, WITH ITS PIXELS
//     CHANGED from that tile's previous delivery (rule 1: a tile that arrived unchanged is counted and is
//     not a sample of anything).
//   * `run` (the journal's whole call, compose and verify inside it), compose and verify alone, and the
//     reload under the engine lock -- reported, not graded.
//
// **A DEVIATION FROM §3.1, STATED WHERE IT IS MADE:** §3.1's annotation is an ink stroke of 200 samples;
// the sink writes Highlight alone until T8, so each edit here is a HIGHLIGHT OF 50 QUADRILATERALS -- 400
// coordinates, as 200 ink samples are -- in its own 24 x 18 pt cell of a 20 x 15 grid over the scope
// page's top-left 480 x 270 pt, which is what the viewport shows at A2's 4 px/pt. Each edit therefore
// lands on paper no earlier edit covered, so its pixels change. The annotations ACCUMULATE: by the end
// of 30 s the page carries 300 and the update every compose writes holds them all, which is the honest
// shape of a burst. **The path-geometry time (D6-14) is not reported: `pageGeometry` is T12's.**
//
// The page geometry assumes the scope page is unrotated with its crop box at the media box's corner,
// which `pages-900.pdf`, `text-900.pdf` and `objstm-xrefstream.pdf` are; the bench prints which page
// and box it used.

int g_stage6Seconds = 30;
constexpr int kStage6PeriodMs = 100;
constexpr int kStage6ViewerSeconds = 10;
constexpr int kStage6GridColumns = 20;
constexpr int kStage6GridRows = 15;
constexpr double kStage6CellWidth = 24.0;
constexpr double kStage6CellHeight = 18.0;
constexpr int kStage6Quads = 50;

struct Stage6Outcome {
    std::vector<double> viewport;
    std::vector<double> editToPixels;
    std::vector<double> runMs;
    std::vector<double> composeMs;
    std::vector<double> verifyMs;
    std::vector<double> reloadMs;
    long long edits = 0;
    long long editsRefused = 0;
    long long editsUnseen = 0;        // an edit whose scope tile never arrived changed
    long long tilesUnchanged = 0;     // a scope tile delivered covering an edit, with the same pixels as before
    long long reloads = 0;
    long long coalesced = 0;
    long long reloadFailures = 0;
    double wallMs = 0.0;
    int scrolls = 0;
    core::render::RenderScheduler::Stats schedulerStats;
    // WHERE EACH VIEWPORT TILE'S WAIT WENT (phase 6's account): queued until the worker took it; in the
    // engine; finished until the pump delivered it. A long first leg is the engine thread busy -- the
    // scope page's re-render, the reload -- and a long last leg is the PUMP not running: the UI thread
    // inside `journal.run`, composing and verifying.
    std::vector<double> queuedLeg, engineLeg, pumpLeg;
    double scopeEngineMs = 0.0;    // worker time on the scope page's tiles
    double scrollEngineMs = 0.0;   // worker time on the scroll's tiles
    std::vector<double> scopeTileMs;   // one scope-page tile in the engine, each
    long long reRequested = 0;     // viewport tiles waiting when an edit landed, asked again
};

core::Annotation stage6Highlight(int page, const core::Page& info, int index)
{
    const core::PdfRect crop = info.cropBox();
    const int cell = index % (kStage6GridColumns * kStage6GridRows);
    const double left = crop.left() + 6.0 + (cell % kStage6GridColumns) * kStage6CellWidth;
    const double top = crop.top() - 6.0 - (cell / kStage6GridColumns) * kStage6CellHeight;
    const double width = kStage6CellWidth - 4.0;
    const double band = (kStage6CellHeight - 4.0) / kStage6Quads;
    core::Annotation a = core::newAnnotation(core::AnnotationSubtype::Highlight, page, QStringLiteral("A2 Stage 6"),
                                             1791504000 + index);
    for (int q = 0; q < kStage6Quads; ++q) {
        const double y0 = top - q * band;
        const double y1 = y0 - band * 0.8;
        a.quadPoints.insert(a.quadPoints.end(), {left, y0, left + width, y0, left, y1, left + width, y1});
    }
    a.rect = *core::boundsOfPoints(a.quadPoints);
    a.color = {1.0, 0.85, 0.0};
    a.opacity = 0.6;
    return a;
}

Stage6Outcome runStage6Column(const QString& relative, int scopePage, const Resolution& view, bool edits,
                              int seconds)
{
    Stage6Outcome outcome;
    pdf::DocumentBackendImpl backend;
    const core::Result<core::DocumentHandle> opened =
        backend.open(QDir(QStringLiteral(RAWPAGE_CORPUS_DIR)).filePath(relative), core::PasswordCallback{});
    if (!opened) return outcome;
    const core::DocumentHandle handle = opened.value();
    const core::PageTree* pages = backend.pages(handle);
    core::Document document;
    if (pages == nullptr || backend.mutationSink(handle) == nullptr || !document.hydrate(backend, handle)) {
        backend.close(handle);
        return outcome;
    }
    core::CommandJournal journal(std::move(document), backend.mutationSink(handle));
    // THE PRODUCT'S SHOW STEP, as `ui/shell/DocumentWindow` sets it.
    journal.setShowHandler([&](const core::Scope&) { return backend.showEdits(handle, {}); });

    ServiceLog log;
    {
        core::render::RenderCache cache;
        core::render::RenderScheduler scheduler(
            [&]() -> std::unique_ptr<core::IPageRasterizer> { return std::make_unique<TimingRasterizer>(backend, log); },
            &cache);
        // Filled below; the mutated handler needs it, so it is declared before the subscription.
        std::map<std::string, std::pair<Clock::time_point, RenderRequest>> awaitingViewport;
        journal.subscribeMutated([&](const core::Scope& scope) {
            scheduler.documentChanged(handle, scope, journal.revision(), backend.pages(handle));
            // THE VIEWS ASK AGAIN (`PageView::modelChanged`): the page view re-requests what it shows at the new
            // revision. So does this, keeping the time each was FIRST asked for, because the person has been
            // waiting since. A tile the scheduler kept (re-keyed, S6 T4) is joined rather than queued twice; one
            // the edit reached was cancelled, and is queued again.
            for (auto& [key, waiting] : awaitingViewport) {
                (void)key;
                waiting.second.revision = journal.revision();
                scheduler.enqueue(waiting.second, RenderPriority::Visible);
                ++outcome.reRequested;
            }
        });

        const TileGrid grid;
        const double scale = view.rasterScale();
        const core::Page* scopeInfo = pages->at(scopePage);

        struct PendingEdit {
            core::Revision revision;
            Clock::time_point returned;
            TileRect device;   // the annotation's rectangle in the page's device pixels
        };
        std::deque<PendingEdit> pendingEdits;
        std::map<std::string, Clock::time_point> viewportSent;
        std::map<std::string, Clock::time_point> viewportDelivered;
        std::map<std::string, QByteArray> lastScopePixels;
        long long reloadsSeen = 0;

        const auto intersects = [](const TileRect& a, const TileRect& b) {
            return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;
        };
        const auto takeReload = [&] {
            const auto record = backend.reloadRecord(handle);
            if (record && record->reloads > reloadsSeen) {
                reloadsSeen = record->reloads;
                outcome.reloadMs.push_back(std::chrono::duration<double, std::milli>(record->lastLoad).count());
            }
        };

        const core::render::RenderScheduler::SubscriptionToken token = scheduler.subscribeReady(
            [&](const RenderRequest& request, const core::RasterImage& image) {
                const std::string key = scaledTileKey(request);
                if (const auto at = awaitingViewport.find(key); at != awaitingViewport.end()) {
                    outcome.viewport.push_back(latencySince(at->second.first));
                    viewportDelivered[key] = Clock::now();
                    awaitingViewport.erase(at);
                }
                if (request.document != handle || request.pageIndex != scopePage) return;
                const auto previous = lastScopePixels.find(key);
                const bool changed = previous == lastScopePixels.end() || previous->second != image.pixels;
                lastScopePixels[key] = image.pixels;
                bool covers = false;
                for (const PendingEdit& edit : pendingEdits)
                    if (request.revision.value() >= edit.revision.value() && intersects(request.tile, edit.device)) covers = true;
                if (!covers) return;
                if (!changed) {
                    ++outcome.tilesUnchanged;
                    return;
                }
                takeReload();
                const Clock::time_point now = Clock::now();
                for (auto it = pendingEdits.begin(); it != pendingEdits.end();) {
                    if (request.revision.value() >= it->revision.value() && intersects(request.tile, it->device)) {
                        outcome.editToPixels.push_back(std::chrono::duration<double, std::milli>(now - it->returned).count());
                        it = pendingEdits.erase(it);
                    } else {
                        ++it;
                    }
                }
            });

        const auto enqueueViewport = [&](int page, bool sample) {
            const core::Page* info = pages->at(page);
            const TileRect bounds = TileGrid::pageBounds(*info, view);
            TileRect viewport = bounds;
            viewport.width = std::min(bounds.width, kViewportWidth);
            viewport.height = std::min(bounds.height, kViewportHeight);
            for (const TileRect& tile : grid.tilesFor(*info, view, viewport).visible) {
                RenderRequest request = requestFor(handle, page, tile, view.dpi);
                request.revision = journal.revision();
                if (sample) awaitingViewport[scaledTileKey(request)] = {Clock::now(), request};
                scheduler.enqueue(request, RenderPriority::Visible);
                if (sample) viewportSent[scaledTileKey(request)] = Clock::now();
            }
        };

        const Clock::time_point start = Clock::now();
        Clock::time_point nextEdit = start;
        Clock::time_point nextScroll = start + std::chrono::milliseconds(kStage6PeriodMs / 2);
        const auto end = start + std::chrono::seconds(seconds);
        int scroll = 0;
        enqueueViewport(scopePage, false);
        while (Clock::now() < end) {
            if (Clock::now() >= nextScroll) {
                // Another page, never the scope page: the scroll measures the viewport.
                int page = (scroll * 7 + 1) % pages->count();
                if (page == scopePage) page = (page + 1) % pages->count();
                enqueueViewport(page, true);
                ++scroll;
                nextScroll += std::chrono::milliseconds(kStage6PeriodMs);
            }
            if (edits && Clock::now() >= nextEdit) {
                const core::Annotation added = stage6Highlight(scopePage, *scopeInfo, static_cast<int>(outcome.edits));
                const Clock::time_point began = Clock::now();
                const core::Status ran = journal.run(
                    std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{added}));
                const Clock::time_point returned = Clock::now();
                ++outcome.edits;
                outcome.runMs.push_back(std::chrono::duration<double, std::milli>(returned - began).count());
                if (!ran) {
                    ++outcome.editsRefused;
                } else {
                    if (const auto record = backend.reloadRecord(handle)) {
                        outcome.composeMs.push_back(std::chrono::duration<double, std::milli>(record->lastCompose).count());
                        outcome.verifyMs.push_back(std::chrono::duration<double, std::milli>(record->lastVerify).count());
                    }
                    const core::PdfRect crop = scopeInfo->cropBox();
                    TileRect device;
                    device.x = static_cast<int>((added.rect.left() - crop.left()) * scale);
                    device.y = static_cast<int>((crop.top() - added.rect.top()) * scale);
                    device.width = std::max(1, static_cast<int>(added.rect.size.width * scale));
                    device.height = std::max(1, static_cast<int>(added.rect.size.height * scale));
                    pendingEdits.push_back(PendingEdit{journal.revision(), returned, device});
                    enqueueViewport(scopePage, false);
                }
                nextEdit += std::chrono::milliseconds(kStage6PeriodMs);
            }
            scheduler.deliverReady();
            std::this_thread::yield();
        }
        // Drain: what was asked is delivered before the counts are taken.
        for (int i = 0; i < 2000 && (scheduler.pendingCount() > 0 || scheduler.runningCount() > 0); ++i) {
            scheduler.deliverReady();
            scheduler.waitForIdle(5);
        }
        scheduler.deliverReady();
        takeReload();
        outcome.wallMs = millisSince(start);
        outcome.scrolls = scroll;
        outcome.editsUnseen = static_cast<long long>(pendingEdits.size());
        scheduler.unsubscribeReady(token);
        outcome.schedulerStats = scheduler.stats();
        if (const auto record = backend.reloadRecord(handle)) {
            outcome.reloads = record->reloads;
            outcome.coalesced = record->coalesced;
            outcome.reloadFailures = record->failures;
        }
        scheduler.cancelDocument(handle);
        scheduler.waitForIdle(30000);

        // --- what the worker did -------------------------------------------------------------
        const std::string scopeMark = "/" + std::to_string(scopePage) + "/";
        const std::lock_guard<std::mutex> lock(log.mutex);
        for (const ServiceLog::Entry& entry : log.entries) {
            const bool scope = entry.key.find(scopeMark) != std::string::npos;
            (scope ? outcome.scopeEngineMs : outcome.scrollEngineMs) += entry.ms;
            if (scope) outcome.scopeTileMs.push_back(entry.ms);
            const auto sent = viewportSent.find(entry.key);
            const auto delivered = viewportDelivered.find(entry.key);
            if (scope || sent == viewportSent.end() || delivered == viewportDelivered.end()) continue;
            if (entry.start < sent->second || delivered->second < entry.start) continue;
            const double queued = std::chrono::duration<double, std::milli>(entry.start - sent->second).count();
            const double total = std::chrono::duration<double, std::milli>(delivered->second - sent->second).count();
            outcome.queuedLeg.push_back(queued);
            outcome.engineLeg.push_back(entry.ms);
            outcome.pumpLeg.push_back(total - queued - entry.ms);
        }
    }
    backend.close(handle);
    return outcome;
}

void reportDistribution(const char* label, const std::vector<double>& samples)
{
    if (samples.empty()) {
        std::printf("      %-16s no samples\n", label);
        return;
    }
    // Into locals first (I-29).
    const double p50 = percentile(samples, 0.50);
    const double p95 = percentile(samples, 0.95);
    const double max = percentile(samples, 1.00);
    std::printf("      %-16s %5zu   p50 %8.3f  p95 %8.3f  max %8.3f ms\n", label, samples.size(), p50, p95, max);
}

int runStage6Workload(const Resolution& a2)
{
    const Resolution view = resolutionAt(a2.dpi);
    std::printf("\n  phase 7 -- STAGE 6's WORKLOAD (D6-24): the viewport while annotation commands reload the"
                " engine (D6-2)\n");
    std::printf("    viewport      at A2's configuration -> %.1f dpi, %.3f px/pt, %dx%d, at Visible\n",
                view.dpi, view.rasterScale(), kViewportWidth, kViewportHeight);
    std::printf("    edits         one AddAnnotation every %d ms for %d s: a highlight of %d quadrilaterals"
                " (400 coordinates, for a 200-sample ink stroke -- the sink writes Highlight alone until T8),"
                " each in a fresh %.0fx%.0f pt cell;\n                  a scroll to another page half a period"
                " later; the scope page's viewport asked again after each edit\n",
                kStage6PeriodMs, g_stage6Seconds, kStage6Quads, kStage6CellWidth, kStage6CellHeight);
    std::printf("    not reported  the path-geometry time (D6-14): `pageGeometry` is Stage 6 T12's\n");
    std::printf("    re-requests   after each edit the scroll's waiting tiles are asked again at the new revision, timed"
                " from when they were FIRST asked, as `PageView::modelChanged` asks again\n");

    struct File {
        const char* relative;
        int scopePage;   // 0-based; the guide's page 450 where the file has one
    };
    const File files[] = {{"ordinary/pages-900.pdf", 449}, {"ordinary/text-900.pdf", 449},
                          {"ordinary/objstm-xrefstream.pdf", 0}};
    struct Verdict {
        const char* file;
        double viewerP95, editsP95, toPixelsP95, toPixelsMax;
        long long missing;
    };
    std::vector<Verdict> verdicts;
    for (const File& file : files) {
        {
            pdf::DocumentBackendImpl probe;
            const auto opened = probe.open(
                QDir(QStringLiteral(RAWPAGE_CORPUS_DIR)).filePath(QString::fromLatin1(file.relative)), core::PasswordCallback{});
            if (!opened) {
                std::fprintf(stderr, "A2 at Stage 6: %s would not open -- no measurement\n", file.relative);
                return 1;
            }
            const core::Page* info = probe.pages(opened.value())->at(file.scopePage);
            std::printf("\n    %s, %d pages -- edits on page %d, crop box %.0fx%.0f pt at (%.0f, %.0f), /Rotate %d\n",
                        file.relative, probe.pages(opened.value())->count(), file.scopePage + 1,
                        info->cropBox().size.width, info->cropBox().size.height, info->cropBox().left(),
                        info->cropBox().bottom(), static_cast<int>(info->rotation()));
            probe.close(opened.value());
        }

        const Stage6Outcome viewer = runStage6Column(QString::fromLatin1(file.relative), file.scopePage, view, false,
                                                     std::min(kStage6ViewerSeconds, g_stage6Seconds));
        const Stage6Outcome edited = runStage6Column(QString::fromLatin1(file.relative), file.scopePage, view, true,
                                                     g_stage6Seconds);
        if (viewer.viewport.empty() || edited.viewport.empty() || edited.edits == 0) {
            std::fprintf(stderr, "A2 at Stage 6: %s delivered no viewport tile or made no edit -- no measurement\n",
                         file.relative);
            return 1;
        }
        report("viewer VIEWPORT", viewer.viewport, viewer.wallMs);
        report("+ edits VIEWPORT", edited.viewport, edited.wallMs);
        reportDistribution("edit->pixels", edited.editToPixels);
        reportDistribution("journal.run", edited.runMs);
        reportDistribution("compose+verify", edited.composeMs);
        reportDistribution("  the verify", edited.verifyMs);
        reportDistribution("reload", edited.reloadMs);
        std::printf("      edits %lld (%lld refused, %lld never seen changed), %d scrolls; reloads %lld, coalesced"
                    " %lld, failed %lld; scope tiles delivered over an edit unchanged %lld\n",
                    edited.edits, edited.editsRefused, edited.editsUnseen, edited.scrolls, edited.reloads,
                    edited.coalesced, edited.reloadFailures, edited.tilesUnchanged);
        reportDistribution("viewport queued", edited.queuedLeg);
        reportDistribution("  in the engine", edited.engineLeg);
        reportDistribution("  before pump", edited.pumpLeg);
        reportDistribution("viewer queued", viewer.queuedLeg);
        reportDistribution("  in the engine", viewer.engineLeg);
        reportDistribution("  before pump", viewer.pumpLeg);
        reportDistribution("scope tile", edited.scopeTileMs);
        std::printf("      worker     %.0f ms on the scope page's tiles, %.0f ms on the scroll's, over %.0f ms;"
                    " %lld viewport tiles waiting when an edit landed were asked again\n",
                    edited.scopeEngineMs, edited.scrollEngineMs, edited.wallMs, edited.reRequested);
        std::printf("      scheduler  enqueued %lld   started %lld   from cache %lld   droppedSuperseded %lld"
                    "   cancelledBeforeStart %lld   failed %lld\n",
                    edited.schedulerStats.enqueued, edited.schedulerStats.started,
                    edited.schedulerStats.servedFromCache, edited.schedulerStats.droppedSuperseded,
                    edited.schedulerStats.cancelledBeforeStart, edited.schedulerStats.failed);
        const double viewerP95 = percentile(viewer.viewport, 0.95);
        const double editsP95 = percentile(edited.viewport, 0.95);
        const double toPixelsP95 = percentile(edited.editToPixels, 0.95);
        const double toPixelsMax = percentile(edited.editToPixels, 1.00);
        verdicts.push_back(Verdict{file.relative, viewerP95, editsP95, toPixelsP95, toPixelsMax,
                                   edited.editsUnseen + edited.editsRefused});
    }

    std::printf("\n    AGAINST Stage6-Annotations-Testing-Guide.md 3.1 -- viewport p95 inside %.3f ms; edit->pixels"
                " p95 <= 100 ms and max <= 250 ms:\n", kFrameBudgetMs);
    for (const Verdict& v : verdicts) {
        std::printf("      %-32s viewport p95 %7.3f (viewer alone %7.3f) %-7s  edit->pixels p95 %7.3f %-7s"
                    " max %7.3f %-7s%s\n",
                    v.file, v.editsP95, v.viewerP95, v.editsP95 < kFrameBudgetMs ? "inside" : "OUTSIDE",
                    v.toPixelsP95, v.toPixelsP95 <= 100.0 ? "inside" : "OUTSIDE", v.toPixelsMax,
                    v.toPixelsMax <= 250.0 ? "inside" : "OUTSIDE",
                    v.missing > 0 ? "   -- SOME EDITS NEVER REACHED THE PIXELS" : "");
    }
    std::printf("\n    Grade on the guide's 3.1 and record it in Tech-Stack-Plan.md 3.3, LABELLED as Stage 6's"
                " workload. This bench never fails on speed: its exit code is about whether the measurement was"
                " possible.\n\n");
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
    bool stage5 = false;
    bool stage6 = false;
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
        if (arguments.at(at) == QLatin1String("--stage5")) {
            stage5 = true;
            continue;
        }
        if (arguments.at(at) == QLatin1String("--stage6")) {
            stage6 = true;
            continue;
        }
        // `--seconds N`: phase 7's edit columns run N seconds instead of the guide's 30 (CTest's short run).
        if (arguments.at(at) == QLatin1String("--seconds") && at + 1 < arguments.size()) {
            const int value = arguments.at(++at).toInt();
            if (value > 0) g_stage6Seconds = value;
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
        if (arguments.at(at) == QLatin1String("--stall-once")) {
            g_stallOnce = true;
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
        bool instrumentLive = false;
        for (int attempt = 1; attempt <= kControlAttempts && !instrumentLive; ++attempt) {
            // A fresh cache and scheduler each attempt, so a retry is a second measurement rather
            // than a warm re-read of the first.
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
                        " a tile, 1 worker, attempt %d of %d\n", kControlCostMs, attempt,
                        kControlAttempts);
            report("known cost", run.latencies, run.wallMs);
            std::printf("    tiles timed %zu of %lld   min %.3f ms (must be %.3f..%.3f)   max %.3f ms"
                        " (must be >= %.3f)   cost %.3f ms/tile\n",
                        run.latencies.size(), run.tiles, minMs, kControlCostMs,
                        kControlCostMs + kControlResolutionMs, maxMs, queueFloorMs, cost);
            if (!everyTileTimed || !noneBelowCost || !resolved || !queueingSeen) {
                std::printf("    %s: %s%s%s%s%s\n",
                            attempt < kControlAttempts ? "NOT LIVE ON THIS ATTEMPT" : "DEAD INSTRUMENT",
                            everyTileTimed ? "" : "tiles went untimed; ",
                            noneBelowCost ? "" : "a tile read below its known cost; ",
                            resolved ? "" : "the first tile read far above its known cost, so"
                                            " delivery is quantized or stalled; ",
                            queueingSeen ? "" : "no tile read as having queued; ",
                            attempt < kControlAttempts
                                ? "-- measuring again on a fresh scheduler."
                                : "-- no attempt read live, so no latency figure below would be"
                                  " evidence, and none is taken.");
                continue;
            }
            std::printf("    the instrument is live: it reads the known cost and the queue behind it\n");
            instrumentLive = true;
        }
        if (!instrumentLive) {
            std::fprintf(stderr, "A2 product: the latency control failed\n");
            backend->close(document->handle);
            return 1;
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
        if (stage5) {
            // Stage 5 T3 (D5-22). Phase 0 has run, for phase 5's reason; the rest is phase 6.
            backend->close(document->handle);
            return runStage5Workload(*backend, open, a2);
        }
        if (stage6) {
            // Stage 6 T4 (D6-24). Phase 0 has run; the rest is phase 7, on a backend of its own per column.
            backend->close(document->handle);
            return runStage6Workload(a2);
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
