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
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
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

double millisSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
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
        sentAt[std::to_string(document.handle.id) + "/" + std::to_string(page) + "/"
               + std::to_string(tile.x) + "," + std::to_string(tile.y)] = Clock::now();
        scheduler.enqueue(request, RenderPriority::Visible);
        into.tiles += 1;
    }

    // Pump until idle. `waitForIdle` blocks the pumping thread, so it is interleaved with
    // `deliverReady()` rather than called once: a scheduler that finished everything while this
    // thread slept would report every latency as the whole batch's wall time.
    while (scheduler.pendingCount() > 0 || scheduler.runningCount() > 0) {
        scheduler.deliverReady();
        if (scheduler.pendingCount() == 0 && scheduler.runningCount() == 0) break;
        scheduler.waitForIdle(1);
    }
    scheduler.deliverReady();
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
    const QStringList arguments = QCoreApplication::arguments();
    for (int at = 1; at < arguments.size(); ++at) {
        if (arguments.at(at) == QLatin1String("--whole-page")) {
            wholePage = true;
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

        std::printf("\n  phase 1 -- A2's configuration, the probe's subject\n");
        std::printf("    %.0f %% zoom on a %.0f %%-scaled monitor  ->  %.1f dpi, %.3f px/pt\n",
                    kA2Zoom * 100.0, kA2MonitorScale * 100.0, a2.dpi, a2.rasterScale());

        for (int workers = 1; workers <= workerSweepMax; workers *= 2) {
            core::render::RenderCache cache;
            core::render::RenderScheduler::Config config;
            config.workerCount = workers;
            core::render::RenderScheduler scheduler(makeRasterizer, &cache, config);

            RunResult run;
            std::map<std::string, Clock::time_point> sentAt;
            const core::render::RenderScheduler::SubscriptionToken token = scheduler.subscribeReady(
                [&](const RenderRequest& request, const core::RasterImage&) {
                    const std::string key = std::to_string(request.document.id) + "/"
                                            + std::to_string(request.pageIndex) + "/"
                                            + std::to_string(request.tile.x) + ","
                                            + std::to_string(request.tile.y);
                    const auto at = sentAt.find(key);
                    if (at != sentAt.end()) run.latencies.push_back(millisSince(at->second));
                });

            const Clock::time_point start = Clock::now();
            renderPage(scheduler, wholePage, *document, 0, a2.dpi, sentAt, run);
            run.wallMs = millisSince(start);
            scheduler.unsubscribeReady(token);

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
                const std::string key = std::to_string(request.document.id) + "/"
                                        + std::to_string(request.pageIndex) + "/"
                                        + std::to_string(request.tile.x) + ","
                                        + std::to_string(request.tile.y);
                const auto at = sentAt.find(key);
                if (at != sentAt.end()) run.latencies.push_back(millisSince(at->second));
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
