// Stage 2 T13 -- the REAL engine driven by the REAL scheduler, through the shipped adapter.
//
// ---------------------------------------------------------------------------------------------
// THE JOIN NOTHING ELSE COVERS, AND WHY IT WAS UNCOVERED.
//
// Three suites touch the render path and each is missing one end of it:
//
//   * `tests/unit` drives `RenderScheduler` and `RenderCache` over a fake rasterizer. Portable, no
//     engine -- `rawpage_assert_headless()` forbids one by name (D2-2).
//   * `tests/ui` drives `PageView` and `TilePresenter` over a fake rasterizer, because `ui/` may
//     not link the engine (D1-4).
//   * `tests/render` drives `PdfiumRasterizer` directly, one call at a time, with no scheduler.
//
// So the one arrangement the shipped product actually runs -- **the real PDFium rasterizer, reached
// through the real backend by `app::BackendRasterizer`, on a worker thread the real
// `RenderScheduler` owns** -- was the arrangement nothing exercised. That is the shape Stage 1 kept
// finding under a different name (I-34, I-42, I-43, I-46): every piece tested, the assembly not.
//
// This file closes it, and it drives `app::BackendRasterizer` itself rather than a copy -- a copy
// would agree with a mistake in the original.
//
// ---------------------------------------------------------------------------------------------
// WHAT IT STILL DOES NOT COVER (convention 19): there is no widget here. *A page is on screen* is
// `tests/ui`'s claim over a fake, and the two halves meeting on real glass is the manual pass.

#include <catch2/catch_test_macros.hpp>

#include "app/BackendRasterizer.h"
#include "core/backend.h"
#include "core/doc/PageTree.h"
#include "core/doc/Revision.h"
#include "core/render/DpiPolicy.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"
#include "core/render/TileGrid.h"
#include "pdf/BackendFactory.h"

#include <QDir>
#include <QString>

#include <memory>
#include <set>
#include <vector>

using namespace rawpage;

namespace {

QString corpusFile(const QString& relative)
{
    return QDir(QStringLiteral(RAWPAGE_CORPUS_DIR)).filePath(relative);
}

// Not every byte of a rendered page is white: a text page has ink on it. A tile that came back
// entirely one colour would satisfy "a buffer arrived" and prove nothing about rendering.
bool hasMoreThanOneColour(const core::RasterImage& image)
{
    std::set<quint32> seen;
    for (int y = 0; y < image.height; ++y) {
        const auto* row = reinterpret_cast<const unsigned char*>(image.pixels.constData())
                          + static_cast<std::size_t>(y) * image.bytesPerLine;
        for (int x = 0; x < image.width; ++x) {
            quint32 pixel = 0;
            std::memcpy(&pixel, row + x * 4, sizeof(pixel));
            seen.insert(pixel & 0x00ffffffu);
            if (seen.size() > 1) return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("the shipped rasterizer factory renders real tiles on a real worker",
          "[render][pipeline]")
{
    const std::unique_ptr<core::IDocumentBackend> backend = pdf::createBackend();
    REQUIRE(backend != nullptr);

    const core::Result<core::DocumentHandle> opened =
        backend->open(corpusFile(QStringLiteral("ordinary/text-a4.pdf")), nullptr);
    REQUIRE(opened);

    const core::PageTree* pages = backend->pages(opened.value());
    REQUIRE(pages != nullptr);
    REQUIRE(pages->count() > 0);

    core::render::RenderCache cache;

    // THE SHIPPED FACTORY, spelled exactly as `app/CompositionRoot` spells it -- including the
    // default `workerCount` of 1, which is I-05's answer and not a test's convenience.
    core::render::RenderScheduler scheduler(
        [&backend]() -> std::unique_ptr<core::IPageRasterizer> {
            return std::make_unique<app::BackendRasterizer>(*backend);
        },
        &cache);

    const core::render::DpiPolicy policy;
    const core::render::Resolution resolution =
        policy.resolve(core::render::Zoom::actualSize(), core::render::MonitorScale{1.0},
                       core::render::PhysicalDpi{96.0});

    const core::render::TileGrid grid;
    const core::render::TileRect bounds =
        core::render::TileGrid::pageBounds(*pages->at(0), resolution);
    REQUIRE(bounds.width > 0);

    const core::render::TileSet tiles = grid.tilesFor(*pages->at(0), resolution, bounds);
    REQUIRE_FALSE(tiles.visible.empty());

    std::vector<core::render::RenderRequest> delivered;
    std::vector<core::RasterImage> images;
    const core::render::RenderScheduler::SubscriptionToken token = scheduler.subscribeReady(
        [&delivered, &images](const core::render::RenderRequest& request,
                              const core::RasterImage& image) {
            delivered.push_back(request);
            images.push_back(image);
        });

    for (const core::render::TileRect& tile : tiles.visible) {
        core::render::RenderRequest request;
        request.document = opened.value();
        request.pageIndex = 0;
        request.tile = tile;
        request.deviceScale = resolution.rasterScale();
        request.revision = core::Revision::initial();
        REQUIRE(scheduler.enqueue(request, core::render::RenderPriority::Visible).isValid());
    }

    REQUIRE(scheduler.waitForIdle(30000));
    REQUIRE(scheduler.deliverReady() > 0);

    CHECK(delivered.size() == tiles.visible.size());
    CHECK(scheduler.stats().failed == 0);
    CHECK(cache.entryCount() > 0);

    // REAL PIXELS, and more than one colour among them: `corpus/ordinary/text-a4.pdf` has text on
    // it, so a tile covering the page body cannot be uniform. A fake that returned a blank buffer
    // would satisfy every assertion above and none of this one.
    bool sawInk = false;
    for (const core::RasterImage& image : images) {
        REQUIRE(image.isValid());
        CHECK(image.bytesPerLine >= image.width * 4);
        if (hasMoreThanOneColour(image)) sawInk = true;
    }
    CHECK(sawInk);

    scheduler.unsubscribeReady(token);
    scheduler.cancelAll();
    CHECK(backend->close(opened.value()));
}

TEST_CASE("a tile for a document that has been closed is refused, not crashed",
          "[render][pipeline]")
{
    // THE ADAPTER'S OTHER BRANCH, and it is the one that matters for safety rather than for
    // pixels. `BackendRasterizer` asks the backend per request, so a request that outlived its
    // document has to be refused BY NAME rather than dereference a null rasterizer.
    //
    // In the product this branch is reached only if the close protocol is not followed -- and the
    // protocol is what `DocumentWindow`'s destructor does. Building the failure deliberately is
    // what makes the recovery real rather than assumed.
    const std::unique_ptr<core::IDocumentBackend> backend = pdf::createBackend();

    const core::Result<core::DocumentHandle> opened =
        backend->open(corpusFile(QStringLiteral("ordinary/text-a4.pdf")), nullptr);
    REQUIRE(opened);
    REQUIRE(backend->close(opened.value()));

    // The page tree goes with it: a handle that has been closed names nothing.
    CHECK(backend->pages(opened.value()) == nullptr);

    app::BackendRasterizer rasterizer(*backend);

    core::render::RenderRequest request;
    request.document = opened.value();
    request.pageIndex = 0;
    request.tile = core::render::TileRect{0, 0, 64, 64};
    request.deviceScale = 1.0;
    request.revision = core::Revision::initial();
    REQUIRE(request.isValid());

    const core::Result<core::RasterImage> refused = rasterizer.raster(request);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == core::ErrorCode::Unavailable);
    CHECK(refused.error().messageKey == QLatin1String("error.render.documentClosed"));
}
