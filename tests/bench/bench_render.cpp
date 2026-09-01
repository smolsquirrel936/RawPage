// RISK A2, THE EARLY PROBE -- Stage 2 T3, decision D2-1 (Stage2-Rendering-Plan.md §3), first half.
//
// The question, from Tech-Stack-Plan.md §3.3: *is PDFium's `FPDF_RenderPageBitmap` throughput
// sufficient for smooth 60 Hz scrolling of a text-heavy A4 page at 200 % on a 150 %-scaled 4K
// monitor with an 8-worker pool?* It is a **go/no-go for D3's render half**: a no-go means D10's
// tiling policy is reopened before anything is built on top of it, and the stage stops at T3.
//
// ---------------------------------------------------------------------------------------------
// WHY IT IS MEASURED TWICE, AND WHY THIS ONE IS THE THROWAWAY HALF (D2-1).
//
// This file answers "is this physically possible" over RAW `FPDF_RenderPageBitmap`, early enough
// that a no-go can change the architecture rather than replace it. It is not a claim about the
// product: there is no `RenderScheduler`, no `RenderCache`, no `TileGrid`, no `DpiPolicy` between
// PDFium and this loop, and those three are exactly what stands between PDFium and the screen. T18
// re-measures through the shipped pipeline and THAT figure is the product's.
//
// The reason this is written down in advance rather than learned twice is Stage 1's own history:
// T2's ribbon spike reported R-9 at 0.28 ms, and T15 discovered the number said nothing about the
// shipped ribbon, because a spike's ribbon is as small as its author made it. A probe answers a
// physics question; only the shipped pipeline answers a product question. One figure without the
// other is either a late verdict or a flattering one.
//
// ---------------------------------------------------------------------------------------------
// WHAT IS ACTUALLY TIMED.
//
// One `FPDF_RenderPageBitmap` call per tile, into a bitmap the tile's own size, with the page
// already loaded. Bitmap allocation and the white fill are OUTSIDE the timed region: they are the
// cache's business at T11 and including them would measure `malloc`.
//
// The device resolution is A2's, computed rather than assumed:
//
//     200 % zoom on a monitor at 150 % scale
//       = 2.00 x (96 x 1.50) / 72   =  4.0 device pixels per PDF point
//     A4 595.276 x 841.89 pt        =  2381 x 3368 device pixels
//
// which is the whole page. Tiles are 512 x 512 -- a number this probe CHOOSES and states, because
// `core/render/TileGrid` does not exist until T10 and the tile size is its decision. A different
// tile size moves the per-tile figure roughly linearly in area, so the number below is only
// meaningful beside that 512.
//
// ---------------------------------------------------------------------------------------------
// THREE PHASES, AND THE MIDDLE ONE IS THE CONTROL.
//
//   1 worker    one thread, every tile. The floor: what a tile costs with nothing contending.
//   N workers   eight threads, each with its OWN FPDF_DOCUMENT and its OWN FPDF_PAGE.
//   throughput  tiles per second in each phase, printed rather than left to the reader. A parallel
//               measurement with no serial baseline cannot distinguish "fast" from "eight threads
//               taking turns", and per-tile percentiles alone cannot either -- they are service
//               times, so they stay flat whether or not the threads are queueing.
//
// That control is not decoration. It is the same lesson bench_ribbon's `layout` phase records: a
// number with only one explanation is a number nobody can act on. Here it earned its place
// immediately -- see below.
//
// ---------------------------------------------------------------------------------------------
// WHAT THE FIRST RUN FOUND, AND WHY THE DEFAULT CONFIGURATION IS NOT THE ONE A2 DESCRIBES.
//
// Eight workers, each with its own document and its own page -- exactly the thread-confinement D2-9
// specifies -- CRASH. Unguarded, two threads are enough: 1 worker exits 0, and 2, 4 and 8 all die
// with STATUS_HEAP_CORRUPTION (0xC0000374) inside the render call; before the pages were preloaded
// they died inside FPDF_LoadPage instead. Per-document confinement is NOT sufficient, because what
// PDFium shares between threads is process-global (its font mapper and glyph caches), not
// per-document.
//
// So the default here is a GLOBAL ENGINE LOCK, and `--unsafe-parallel` reproduces the crash for
// anyone who wants to see it rather than take it on trust. The measurement is still worth taking
// under the lock: A2 asks what the engine costs per tile, and the lock does not change that -- it
// changes how many tiles per second a pool can retire, which the throughput line reports and which
// is the finding rather than the measurement.
//
// ---------------------------------------------------------------------------------------------
// WHY IT IS NOT A PASS/FAIL CTEST CASE.
//
// It IS registered with CTest, so it compiles and runs on every Windows CI run -- a benchmark that
// only runs when somebody remembers it has rotted by the time its number matters. But its exit code
// is about whether the MEASUREMENT WAS POSSIBLE, never about whether the machine was fast: a shared
// runner under load must not turn a green build red for a reason unrelated to the change. It fails
// only on a measurement that cannot mean anything -- the engine did not start, the file did not
// open, a page did not load, or a render call returned no pixels.
//
// The 16.667 ms verdict is printed and is the testing guide's to record, against the scale fixed in
// Stage2-Rendering-Testing-Guide.md §3 before the measurement was taken.

#include "pdf/render/PdfiumInit.h"

#include <fpdfview.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace render = rawpage::pdf::render;

// --- A2's parameters, one place ---------------------------------------------------------
constexpr double kZoom          = 2.00;    // 200 %
constexpr double kMonitorScale  = 1.50;    // the 4K panel: 3840x2160 physical, 2560x1440 logical
constexpr double kLogicalDpi    = 96.0;
constexpr int    kTileSize      = 512;     // this probe's choice; TileGrid decides for real at T10
constexpr int    kWorkers       = 8;       // the pool size A2 names; this machine has 16 cores
constexpr int    kRepeats       = 20;      // passes over the whole page, per phase
constexpr int    kWarmupRepeats = 3;       // first-touch: font resolution, glyph cache, page state
constexpr double kFrameBudgetMs = 16.667;  // one 60 Hz frame

// THE ENGINE LOCK, and it exists because of what this probe measured on its first run: PDFium
// cannot be called concurrently, even with one FPDF_DOCUMENT and one FPDF_PAGE per thread. Eight
// unguarded workers crashed inside FPDF_LoadPage and then, with pages preloaded, corrupted the heap
// inside the render call. See the issue log entry this produced.
//
// `g_serialize` selects which of the two configurations is being measured. It is a runtime switch
// rather than an #ifdef so that both figures come out of the same binary, and so that the crashing
// configuration stays REPRODUCIBLE by anybody who doubts the finding -- a finding that can only be
// reproduced by editing the source is one that gets re-litigated.
std::mutex g_engineMutex;
bool       g_serialize = true;

using Clock = std::chrono::steady_clock;

double millisSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Percentile over a COPY, for the reason bench_ribbon's own helper records: a caller that wanted
// its samples in order would otherwise find them sorted by a call it made for a different purpose.
double percentile(std::vector<double> samples, double fraction)
{
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(samples.size() - 1));
    return samples[std::min(index, samples.size() - 1)];
}

void report(const char* label, const std::vector<double>& samples, double wallMs)
{
    // Into locals FIRST, and this is not caution for its own sake: Stage 1's I-29 was three
    // percentile calls written as arguments to one printf, where argument evaluation order is
    // unspecified, and every figure that benchmark printed on its first run was that bug rather
    // than the ribbon.
    const double p50 = percentile(samples, 0.50);
    const double p95 = percentile(samples, 0.95);
    const double max = percentile(samples, 1.00);
    std::printf("  %-10s tiles %5zu   p50 %7.3f ms   p95 %7.3f ms   max %7.3f ms   wall %8.1f ms\n",
                label, samples.size(), p50, p95, max, wallMs);
}

struct Tile {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

std::vector<Tile> tilesFor(int pageWidth, int pageHeight)
{
    std::vector<Tile> tiles;
    for (int y = 0; y < pageHeight; y += kTileSize) {
        for (int x = 0; x < pageWidth; x += kTileSize) {
            Tile tile;
            tile.x = x;
            tile.y = y;
            tile.width  = std::min(kTileSize, pageWidth - x);
            tile.height = std::min(kTileSize, pageHeight - y);
            tiles.push_back(tile);
        }
    }
    return tiles;
}

// Renders one tile and returns the milliseconds it took, or a negative number if the call could not
// produce pixels -- which is a failed MEASUREMENT rather than a slow one, and the difference is the
// whole of this program's exit-code policy.
double renderTile(FPDF_PAGE page, const Tile& tile, int pageWidth, int pageHeight, bool timed,
                  std::vector<double>* into)
{
    // BGRA, allocated by PDFium so that PDFium frees it: the DLL statically links its own C runtime
    // (measured at Stage 0 T12 and recorded in third_party/pdfium/README.md), so a buffer released
    // by our `free()` would be handed to a different allocator's heap. That is also why the real
    // facade will copy into a core::RasterImage rather than handing this pointer outward.
    // The whole tile -- allocate, fill, render, read back, destroy -- is one critical section when
    // serializing. Locking only the render call would leave FPDFBitmap_Create and _Destroy racing on
    // PDFium's own allocator, which is where the heap corruption showed up.
    std::unique_lock<std::mutex> guard(g_engineMutex, std::defer_lock);
    if (g_serialize) guard.lock();

    FPDF_BITMAP bitmap = FPDFBitmap_Create(tile.width, tile.height, /*alpha=*/0);
    if (bitmap == nullptr) return -1.0;

    // White, outside the timed region. A page is composited onto white by every viewer, and the
    // fill is the cache's business at T11 rather than the rasterizer's.
    FPDFBitmap_FillRect(bitmap, 0, 0, tile.width, tile.height, 0xFFFFFFFF);

    const Clock::time_point start = Clock::now();

    // THE TILING CALL. Negative start_x/start_y shift the whole page under a bitmap that is only
    // the tile's size, which is how FPDF_RenderPageBitmap renders a region: the size arguments stay
    // the FULL page in device pixels, and the bitmap is the window onto it.
    FPDF_RenderPageBitmap(bitmap, page, -tile.x, -tile.y, pageWidth, pageHeight, /*rotate=*/0,
                          /*flags=*/0);

    const double elapsed = millisSince(start);

    // Proof that pixels were actually written, and it is not paranoia: FPDF_RenderPageBitmap returns
    // void, so a call that did nothing at all is indistinguishable from a very fast one -- which is
    // exactly the shape of the measurement bug this project has already had once (I-29). A tile of
    // this page always contains some non-white pixel except in the margins, so the check is that
    // the buffer is readable and its stride is sane rather than that it is non-blank; a blankness
    // test would fail on a legitimately empty margin tile.
    const void* pixels = FPDFBitmap_GetBuffer(bitmap);
    const int stride = FPDFBitmap_GetStride(bitmap);
    const bool usable = pixels != nullptr && stride >= tile.width * 4;

    FPDFBitmap_Destroy(bitmap);

    if (!usable) return -1.0;
    if (timed && into != nullptr) into->push_back(elapsed);
    return elapsed;
}

// One worker's whole job: its own document, its own page, its own share of the tiles. Nothing is
// shared but the immutable file bytes and the results vector, which is appended under a lock after
// the work is done rather than during it.
struct WorkerResult {
    std::vector<double> samples;
    bool ok = false;
};

WorkerResult renderTiles(FPDF_PAGE page, const std::vector<Tile>& tiles, int pageWidth,
                         int pageHeight, int repeats, int warmup)
{
    WorkerResult result;
    if (page == nullptr) return result;

    for (int pass = 0; pass < warmup + repeats; ++pass) {
        const bool timed = pass >= warmup;
        for (const Tile& tile : tiles) {
            if (renderTile(page, tile, pageWidth, pageHeight, timed, &result.samples) < 0.0)
                return result;
        }
    }

    result.ok = true;
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    // The page is an argument so that a developer can point the probe at another file, and it has a
    // default so that CTest can run it with none. It is NOT read out of a corpus directory scan:
    // A2 names one page shape, and a probe that silently measured whatever was in a folder would
    // produce a figure whose meaning changed when the folder did.
    // usage: rawpage_bench_render [page.pdf] [workers] [--unsafe-parallel]
    //
    // Every argument has a default, so CTest runs it with none and gets the configuration the
    // recorded figure was taken in. `--unsafe-parallel` removes the engine lock and is how the
    // concurrency finding is reproduced; it is expected to crash, which is why it is not the
    // default and why the exit code below cannot be trusted in that mode.
    std::string path = RAWPAGE_A2_PROBE_PAGE;
    int workers = kWorkers;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--unsafe-parallel") {
            g_serialize = false;
        } else if (!argument.empty() && std::all_of(argument.begin(), argument.end(),
                                                    [](unsigned char c) { return std::isdigit(c); })) {
            workers = std::max(1, std::atoi(argument.c_str()));
        } else {
            path = argument;
        }
    }

    // UNBUFFERED, and it earned the line on this program's first run. Redirected, stdout is fully
    // buffered, so a crash discards every figure printed before it -- the probe died with an access
    // violation and reported NOTHING AT ALL, which made "how far did it get" the first question and
    // an unanswerable one. A measurement that can crash has to narrate as it goes.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    std::printf("\nA2 probe -- raw FPDF_RenderPageBitmap, Stage 2 T3 (D2-1, first half)\n");
    std::printf("  page          %s\n", path.c_str());

    if (const rawpage::core::Status started = render::ensure(); !started) {
        std::fprintf(stderr, "A2 probe: the engine did not start -- nothing can be measured\n");
        return 1;
    }
    std::printf("  engine        PDFium %s\n", render::version().data());

    // Read once into memory and load every document from that buffer. Loading from the path would
    // put file I/O inside the setup of eight workers and, worse, would make the eight loads race
    // over the same handle for no reason the measurement cares about.
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "A2 probe: cannot read %s\n", path.c_str());
        return 1;
    }
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        std::fprintf(stderr, "A2 probe: %s is empty\n", path.c_str());
        return 1;
    }

    // --- the device resolution A2 specifies ---------------------------------------------
    FPDF_DOCUMENT probe = FPDF_LoadMemDocument(bytes.data(), static_cast<int>(bytes.size()), nullptr);
    if (probe == nullptr) {
        std::fprintf(stderr, "A2 probe: FPDF_LoadMemDocument failed, FPDF_GetLastError=%lu\n",
                     FPDF_GetLastError());
        return 1;
    }
    double widthPt = 0.0;
    double heightPt = 0.0;
    if (FPDF_GetPageSizeByIndex(probe, 0, &widthPt, &heightPt) == 0) {
        std::fprintf(stderr, "A2 probe: page 0 has no size\n");
        FPDF_CloseDocument(probe);
        return 1;
    }

    const double pixelsPerPoint = kZoom * (kLogicalDpi * kMonitorScale) / 72.0;
    const int pageWidth  = static_cast<int>(std::lround(widthPt * pixelsPerPoint));
    const int pageHeight = static_cast<int>(std::lround(heightPt * pixelsPerPoint));
    const std::vector<Tile> tiles = tilesFor(pageWidth, pageHeight);

    std::printf("  page size     %.2f x %.2f pt\n", widthPt, heightPt);
    std::printf("  device        %d x %d px  (%.0f %% zoom on a %.0f %%-scaled monitor = %.2f px/pt)\n",
                pageWidth, pageHeight, kZoom * 100.0, kMonitorScale * 100.0, pixelsPerPoint);
    std::printf("  tiles         %zu of %d x %d px\n", tiles.size(), kTileSize, kTileSize);
    std::printf("  passes        %d timed, %d warm-up\n\n", kRepeats, kWarmupRepeats);

    // --- phase 1: serial, the floor ------------------------------------------------------
    FPDF_PAGE probePage = FPDF_LoadPage(probe, 0);
    if (probePage == nullptr) {
        std::fprintf(stderr, "A2 probe: page 0 would not load\n");
        FPDF_CloseDocument(probe);
        return 1;
    }
    const Clock::time_point serialStart = Clock::now();
    const WorkerResult serial = renderTiles(probePage, tiles, pageWidth, pageHeight, kRepeats,
                                            kWarmupRepeats);
    const double serialWall = millisSince(serialStart);
    FPDF_ClosePage(probePage);
    FPDF_CloseDocument(probe);

    if (!serial.ok || serial.samples.empty()) {
        std::fprintf(stderr, "A2 probe: the serial pass could not render -- no measurement\n");
        return 1;
    }

    // --- phase 2: eight workers, one document each ---------------------------------------
    // Documents are opened SERIALLY here and rendered concurrently below. Concurrent rendering with
    // one document per worker is the product's design (D2-9, thread confinement) and is what A2
    // names; concurrent LOADING is not part of that design and would put an untested race in the
    // way of the number this program exists to produce.
    std::vector<FPDF_DOCUMENT> documents;
    std::vector<FPDF_PAGE> pages;
    documents.reserve(static_cast<std::size_t>(workers));
    pages.reserve(static_cast<std::size_t>(workers));
    for (int i = 0; i < workers; ++i) {
        FPDF_DOCUMENT document =
            FPDF_LoadMemDocument(bytes.data(), static_cast<int>(bytes.size()), nullptr);
        FPDF_PAGE page = document != nullptr ? FPDF_LoadPage(document, 0) : nullptr;
        if (page == nullptr) {
            std::fprintf(stderr, "A2 probe: worker %d could not open the document\n", i);
            for (FPDF_PAGE open : pages) FPDF_ClosePage(open);
            for (FPDF_DOCUMENT open : documents) FPDF_CloseDocument(open);
            if (document != nullptr) FPDF_CloseDocument(document);
            return 1;
        }
        documents.push_back(document);
        pages.push_back(page);
    }

    std::vector<double> parallelSamples;
    std::mutex samplesMutex;
    std::atomic<int> failures{0};

    const Clock::time_point parallelStart = Clock::now();
    {
        std::vector<std::thread> threads;
        threads.reserve(static_cast<std::size_t>(workers));
        for (int i = 0; i < workers; ++i) {
            threads.emplace_back([&, i] {
                // Every worker renders the WHOLE page, not a slice of it. Eight workers each
                // rendering a seventh of the tiles would measure a shorter queue rather than a
                // contended engine, and contention is the question: the product's workers are
                // rendering different pages of the same document at the same moment.
                const WorkerResult one = renderTiles(pages[static_cast<std::size_t>(i)], tiles,
                                                     pageWidth, pageHeight, kRepeats,
                                                     kWarmupRepeats);
                if (!one.ok) {
                    failures.fetch_add(1, std::memory_order_relaxed);
                    return;
                }
                const std::lock_guard<std::mutex> guard(samplesMutex);
                parallelSamples.insert(parallelSamples.end(), one.samples.begin(),
                                       one.samples.end());
            });
        }
        for (std::thread& thread : threads) thread.join();
    }
    const double parallelWall = millisSince(parallelStart);

    for (FPDF_PAGE page : pages) FPDF_ClosePage(page);
    for (FPDF_DOCUMENT document : documents) FPDF_CloseDocument(document);

    if (failures.load() != 0 || parallelSamples.empty()) {
        std::fprintf(stderr, "A2 probe: %d of %d workers could not render -- no measurement\n",
                     failures.load(), kWorkers);
        return 1;
    }

    // --- the figures ---------------------------------------------------------------------
    //
    // WHAT THE PER-TILE NUMBERS ARE, PRECISELY, because the distinction decides how they may be
    // read. The timer starts AFTER the engine lock is taken, so each sample is the SERVICE TIME of
    // one FPDF_RenderPageBitmap call -- what the engine costs -- and not the latency a worker
    // experienced, which in the serialized configuration includes waiting behind seven other
    // workers. That is the right thing to measure for A2, whose question is about PDFium's
    // throughput rather than about a scheduler that does not exist until T11.
    //
    // The consequence is that a "parallel p95 / serial p95" ratio would be nonsense here: it reads
    // BELOW 1.0, which would look like better-than-perfect scaling and is really just the engine
    // being equally fast under a lock. So the scaling question is answered with THROUGHPUT instead,
    // which cannot be read the wrong way round: if eight workers finish 8x the tiles in 8x the
    // wall time, they achieved nothing, and the number says 1.0.
    std::printf("per-tile service time (the engine's own cost, measured inside the lock)\n");
    report("1 worker", serial.samples, serialWall);
    report(g_serialize ? "N, locked" : "N, unsafe", parallelSamples, parallelWall);

    const double parallelP95 = percentile(parallelSamples, 0.95);

    const double serialRate =
        serialWall > 0.0 ? static_cast<double>(serial.samples.size()) / (serialWall / 1000.0) : 0.0;
    const double parallelRate =
        parallelWall > 0.0
            ? static_cast<double>(parallelSamples.size()) / (parallelWall / 1000.0)
            : 0.0;
    const double scaling = serialRate > 0.0 ? parallelRate / serialRate : 0.0;

    // A whole page is what a reader actually feels when a page scrolls into view, so it is printed
    // as well: a per-tile figure comfortably inside the budget can still add up to a visible stall
    // if a page is 35 tiles and nothing is prefetched. That is the scheduler's problem rather than
    // PDFium's, which is exactly why the number belongs beside the verdict rather than inside it.
    const double pageP95 = parallelP95 * static_cast<double>(tiles.size());

    std::printf("\n  throughput    1 worker %.0f tiles/s   |   %d workers %.0f tiles/s"
                "   =  %.2f x scaling\n", serialRate, workers, parallelRate, scaling);
    std::printf("  whole page    %.1f ms of engine time, over %zu tiles\n", pageP95, tiles.size());
    std::printf("  budget        %.3f ms per tile for one 60 Hz frame\n", kFrameBudgetMs);
    std::printf("  headroom      %.1f x at p95\n", kFrameBudgetMs / parallelP95);
    if (g_serialize) {
        std::printf("  concurrency   SERIALIZED behind one engine lock -- see --unsafe-parallel,\n"
                    "                which is the configuration D2-9 assumed and which crashes\n");
    }

    // The verdict is PRINTED, not returned. Stage2-Rendering-Testing-Guide.md §3 fixed the scale
    // before the measurement was taken -- go / go-with-named-custom-work / no-go -- and recording it
    // against that scale is a person's job.
    std::printf("\n  A2 (probe, raw PDFium, one text page): p95 %.3f ms against %.3f ms -- %s\n",
                parallelP95, kFrameBudgetMs,
                parallelP95 < kFrameBudgetMs ? "INSIDE the frame budget" : "OVER the frame budget");
    std::printf("  This is the PROBE figure. The product's is T18's, through the shipped "
                "scheduler and cache (D2-1).\n\n");

    render::shutdown();
    return 0;
}
