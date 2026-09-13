// rawpage_bench_openclose -- does memory grow with each document opened and closed, and in which
// layer? The reproducer for Stage 2 issue I-35 (Stage2-Rendering-Testing-Guide.md §8), added the
// day M-09's growth clause found it.
//
// KEPT RATHER THAN THROWN AWAY for bench_render's reason: a finding that needs a source edit to
// reproduce is one that gets re-litigated. NOT A CTEST CASE, because it reports a trend over dozens
// of rounds and no threshold for it has been decided.
//
// The M-09 driven runs measured the whole product growing ~4 MB of private bytes per round of ten
// opens. This program repeats the same ten files with NO window and NO ui/ code, through the same
// pieces the product composes -- pdf::createBackend(), app::BackendRasterizer, one RenderScheduler,
// one RenderCache -- so the answer splits the product in two:
//
//   grows here      -> the retained memory is below ui/: pdf/, core/render/, or PDFium itself
//   flat here       -> it is in ui/ (or in something only the GUI process composes)
//
// Switches, each removing one suspect:
//   --no-render   open and close only, no tile is ever rasterized
//   --bridge      install FontResolverBridge over platform::createPlatform()'s resolver, as
//                 app/CompositionRoot does; without it PDFium resolves missing fonts on its own
//   <N>           rounds (default 20)

#include "app/BackendRasterizer.h"
#include "core/doc/PageTree.h"
#include "core/doc/Revision.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "core/render/TileGrid.h"
#include "pdf/BackendFactory.h"
#include "pdf/render/FontResolverBridge.h"
#include "platform/platform.h"

#include <QCoreApplication>
#include <QDir>
#include <QString>
#include <QStringList>

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>

namespace {

using namespace rawpage;

struct ProcessFigures {
    double privateMB = 0.0;
    unsigned long handles = 0;
};

ProcessFigures measure()
{
    ProcessFigures figures;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof(counters))) {
        figures.privateMB = static_cast<double>(counters.PrivateUsage) / (1024.0 * 1024.0);
    }
    DWORD handles = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handles)) figures.handles = handles;
    return figures;
}

// The ten files the M-09 driver opened, in its order.
const char* const kFiles[] = {
    "minimal.pdf",
    "ordinary/text-a4.pdf",
    "ordinary/cjk-embedded.pdf",
    "ordinary/image-jpx.pdf",
    "ordinary/image-ccitt-g4.pdf",
    "ordinary/transparency-groups.pdf",
    "ordinary/pages-900.pdf",
    "malformed/xref-missing.pdf",
    "malformed/startxref-past-eof.pdf",
    "encrypted/aes-256.pdf",
};

// 150 % zoom on a 150 %-scaled monitor, roughly what the driver's screenshots showed.
constexpr double kDpi = 96.0 * 1.5 * 1.5;
constexpr int kPagesPerDocument = 7;

void renderDocument(core::render::RenderScheduler& scheduler, core::DocumentHandle handle,
                    const core::PageTree& pages)
{
    const int count = std::min(pages.count(), kPagesPerDocument);
    const core::render::TileGrid grid;
    for (int page = 0; page < count; ++page) {
        const core::Page* info = pages.at(page);
        if (info == nullptr) continue;
        core::render::Resolution resolution;
        resolution.dpi = kDpi;
        resolution.devicePixelRatio = 1.0;
        resolution.upscale = 1.0;
        const core::render::TileRect bounds = core::render::TileGrid::pageBounds(*info, resolution);
        core::render::TileRect viewport = bounds;
        viewport.width = std::min(bounds.width, 1920);
        viewport.height = std::min(bounds.height, 1080);
        const core::render::TileSet tiles = grid.tilesFor(*info, resolution, viewport);
        for (const core::render::TileRect& tile : tiles.visible) {
            core::render::RenderRequest request;
            request.document = handle;
            request.pageIndex = page;
            request.tile = tile;
            request.deviceScale = resolution.rasterScale();
            request.revision = core::Revision::initial();
            request.flags = core::render::RenderFlags::Annotations;
            scheduler.enqueue(request, core::render::RenderPriority::Visible);
        }
        while (scheduler.pendingCount() > 0 || scheduler.runningCount() > 0) {
            scheduler.deliverReady();
            if (scheduler.pendingCount() == 0 && scheduler.runningCount() == 0) break;
            scheduler.waitForIdle(1);
        }
        scheduler.deliverReady();
    }
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);

    int rounds = 20;
    bool render = true;
    bool bridge = false;
    const QStringList arguments = QCoreApplication::arguments();
    for (int at = 1; at < arguments.size(); ++at) {
        if (arguments.at(at) == QLatin1String("--no-render")) { render = false; continue; }
        if (arguments.at(at) == QLatin1String("--bridge")) { bridge = true; continue; }
        bool numeric = false;
        const int value = arguments.at(at).toInt(&numeric);
        if (numeric && value > 0) rounds = value;
    }

    std::unique_ptr<platform::Platform> platformBundle;
    if (bridge) {
        platformBundle = platform::createPlatform();
        if (!platformBundle || !platformBundle->fontResolver) {
            std::fprintf(stderr, "openclose: the platform has no font resolver\n");
            return 1;
        }
        if (const core::Status installed =
                pdf::render::FontResolverBridge::install(*platformBundle->fontResolver);
            !installed) {
            std::fprintf(stderr, "openclose: FontResolverBridge::install failed\n");
            return 1;
        }
    }

    {
        const std::unique_ptr<core::IDocumentBackend> backend = pdf::createBackend();
        if (!backend) {
            std::fprintf(stderr, "openclose: pdf::createBackend() returned nothing\n");
            return 1;
        }
        const auto makeRasterizer = [&backend]() -> std::unique_ptr<core::IPageRasterizer> {
            return std::make_unique<app::BackendRasterizer>(*backend);
        };
        // ONE cache and ONE scheduler for the whole run, as app/CompositionRoot holds them.
        core::render::RenderCache cache;
        core::render::RenderScheduler scheduler(makeRasterizer, &cache);

        // Convention 7: a render loop that delivered nothing would report "no growth" for free.
        long long delivered = 0;
        double peakMB = 0.0;
        const core::render::RenderScheduler::SubscriptionToken token = scheduler.subscribeReady(
            [&](const core::render::RenderRequest&, const core::RasterImage&) { ++delivered; });

        const QDir corpus(QStringLiteral(RAWPAGE_CORPUS_DIR));
        std::printf("openclose: %d rounds x %zu files, render %s, font bridge %s\n", rounds,
                    std::size(kFiles), render ? "ON" : "off", bridge ? "ON" : "off");
        const ProcessFigures start = measure();
        std::printf("round,privateMB,handles,deltaMB,peakOpenMB,tilesDelivered,cacheBytes,opened\n");
        std::printf("0,%.1f,%lu,0.0,0.0,0,%zu,0\n", start.privateMB, start.handles, cache.bytesHeld());

        double previous = start.privateMB;
        for (int round = 1; round <= rounds; ++round) {
            int opened = 0;
            delivered = 0;
            peakMB = 0.0;
            for (const char* relative : kFiles) {
                const QString file = QString::fromLatin1(relative);
                const bool encrypted = file.startsWith(QLatin1String("encrypted/"));
                const core::PasswordCallback password =
                    encrypted ? core::PasswordCallback([](int attempt) -> std::optional<QString> {
                                    return attempt > 1 ? std::nullopt
                                                       : std::optional<QString>(QStringLiteral("user"));
                                })
                              : core::PasswordCallback{};
                const core::Result<core::DocumentHandle> handle =
                    backend->open(corpus.filePath(file), password);
                if (!handle) continue;
                ++opened;
                if (render) {
                    if (const core::PageTree* pages = backend->pages(handle.value())) {
                        renderDocument(scheduler, handle.value(), *pages);
                    }
                }
                peakMB = std::max(peakMB, measure().privateMB);
                // The product's close protocol: ui/shell/DocumentWindow drops the tiles, then closes.
                cache.dropDocument(handle.value());
                backend->close(handle.value());
            }
            const ProcessFigures now = measure();
            std::printf("%d,%.1f,%lu,%+.1f,%.1f,%lld,%zu,%d\n", round, now.privateMB, now.handles,
                        now.privateMB - previous, peakMB, delivered, cache.bytesHeld(), opened);
            std::fflush(stdout);
            previous = now.privateMB;
        }
        scheduler.unsubscribeReady(token);
    }

    if (bridge) pdf::render::FontResolverBridge::uninstall();
    return 0;
}
