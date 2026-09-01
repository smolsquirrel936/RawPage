// Stage 2 T2 -- `pdf/render/PdfiumInit`, the one place PDFium's global state is owned.
//
// It carries the engine half of guide case A-07 (the library initializes in a process with NO
// DISPLAY -- this binary creates no QGuiApplication and no window at all) and the first half of
// A-45 (`PdfiumInit` is entered ONCE however many threads ask).
//
// EVERY CASE HERE STARTS FROM A COLD PROCESS, and that is what makes them worth writing.
// `catch_discover_tests` registers one CTest entry per case and each runs the binary afresh, so the
// process-global counter this file asserts against begins at zero every time. The first draft
// assumed a shared process, chained the cases, and two of them failed on their first run for a
// reason that had nothing to do with the engine -- worth recording, because the same assumption
// would have quietly weakened the eight-thread case below into a test of the fast path.
//
// WHAT IT DOES NOT COVER, said here rather than left to be inferred (convention 19). It does not
// open a document and it does not rasterize: `PdfiumDocument` and `PdfiumRasterizer` arrive at T9
// and bring `tst_pdfiumdoc` and `tst_rasterizer` with them. A2's raster throughput is measured by
// `tests/bench/bench_render`, which is a benchmark rather than a test for the reason
// tests/bench/CMakeLists.txt gives. What is asserted below is initialization and nothing else --
// which is the whole of what T2 built.

#include <catch2/catch_test_macros.hpp>

#include "pdf/render/PdfiumInit.h"

#include <atomic>
#include <thread>
#include <vector>

namespace render = rawpage::pdf::render;

TEST_CASE("the engine initializes with no display and reports the pinned version", "[pdfiuminit]")
{
    // The version is available before initialization, deliberately: it is the build-time pin, so a
    // diagnostic can print it without starting an engine. `--version` depends on exactly that.
    REQUIRE(render::version() == "153.0.8009.0");

    REQUIRE_FALSE(render::isInitialized());
    REQUIRE(render::initializationCount() == 0);

    const rawpage::core::Status started = render::ensure();
    REQUIRE(started);
    REQUIRE(render::isInitialized());
    REQUIRE(render::initializationCount() == 1);
}

TEST_CASE("ensure() is idempotent -- the second call initializes nothing", "[pdfiuminit]")
{
    REQUIRE(render::ensure());
    REQUIRE(render::ensure());
    REQUIRE(render::ensure());

    // The counter, not the return value, is the assertion that matters. Three successful calls
    // prove nothing on their own -- `FPDF_InitLibraryWithConfig` returns void, so an implementation
    // that called it three times would also return success three times, and the second call is the
    // one that corrupts the library's global state.
    REQUIRE(render::initializationCount() == 1);
}

TEST_CASE("eight threads racing from cold still enter the library once", "[pdfiuminit]")
{
    // The number is 8 because that is the worker-pool size A2 is specified against, and this is the
    // exact shape of the real race: a pool starting up, every worker wanting the engine, none of
    // them knowing whether it is already there. Because the process is cold, the threads race for
    // the FIRST initialization rather than all taking the fast path -- which is the half that
    // matters and the half a chained case would have missed.
    REQUIRE(render::initializationCount() == 0);

    constexpr int kWorkers = 8;
    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(kWorkers);

    for (int i = 0; i < kWorkers; ++i) {
        workers.emplace_back([&failures] {
            for (int call = 0; call < 50; ++call) {
                if (!render::ensure()) failures.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (std::thread& worker : workers) worker.join();

    REQUIRE(failures.load() == 0);
    REQUIRE(render::isInitialized());
    REQUIRE(render::initializationCount() == 1);
}

TEST_CASE("shutdown() puts the state back and ensure() can start it again", "[pdfiuminit]")
{
    REQUIRE(render::ensure());
    REQUIRE(render::isInitialized());

    render::shutdown();
    REQUIRE_FALSE(render::isInitialized());

    // Idempotent in this direction too: a second shutdown must not destroy a library that is
    // already gone, which is undefined behaviour rather than a no-op inside PDFium.
    render::shutdown();
    REQUIRE_FALSE(render::isInitialized());

    // The re-entry is the point, and it is why `ensure()` is a mutex and a flag rather than
    // std::call_once. call_once is a one-way latch: after a shutdown, a second ensure() would
    // silently do nothing and hand every caller a library that is not there -- a failure that
    // surfaces as a crash inside the engine rather than as an error anybody can read.
    REQUIRE(render::ensure());
    REQUIRE(render::isInitialized());
    REQUIRE(render::initializationCount() == 2);
}
