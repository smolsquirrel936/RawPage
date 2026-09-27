// bench_surface -- A2 AT STAGE 4, RE-READ THROUGH THE SHIPPED PANES. Stage 4 T14, decision D4-16;
// guide case A-72 (`Stage4-ViewingSurface-Testing-Guide.md` §5.14).
//
// ---------------------------------------------------------------------------------------------
// WHY THIS EXISTS WHEN `bench_pipeline --stage4` ALREADY REPORTS THE SAME NUMBER
// ---------------------------------------------------------------------------------------------
// It does not report the same number. `bench_pipeline --stage4` was run at **T2, before a single
// pane existed**, and it issues the request pattern a viewer, a split and a thumbnails pane were
// EXPECTED to produce -- by hand, from the bench's own loop. That was the whole point of measuring
// then: a no-go had to be able to reshape the pane's design before the pane was built. It is also
// the whole of its bound, and `Tech-Stack-Plan.md` §3.3 says so in as many words: *"a synthetic
// request pattern issued before any pane exists ... T14 re-reads the same quantity through the
// shipped panes and is where the figure becomes the product's."*
//
// This is that re-read. The request stream here is produced by `ui/panes/ThumbnailsPane`,
// `ui/viewer/SplitViewContainer` and `ui/viewer/TilePresenter` -- the real widgets, over
// `app::CompositionRoot` and `ui::MainWindow` as `app/main.cpp` assembles them -- and nothing in
// this file decides which tile is asked for, at what priority, or when.
//
// **THE PROBE-VERSUS-PRODUCT LESSON IS THE REASON THE TWO ARE SEPARATE PROGRAMS** and not a flag
// on one. D2-1 asked A2 twice because the probe said 31x headroom and the product said 1.18x, and
// quoting the first as the second is the exact error that pair of measurements exists to prevent.
// A bench that could print either number under one name would make that error available again.
//
// ---------------------------------------------------------------------------------------------
// THE QUANTITY, AND HOW IT IS OBTAINED
// ---------------------------------------------------------------------------------------------
// **The viewport's p95 QUEUED LATENCY** -- from `RenderScheduler::enqueue()` to the delivery that
// hands the tile to its subscriber -- which is what A2's product figure has named since Stage 2
// T18 and what every column of T2's table reports. T2 could stamp its own enqueues because it made
// them; here `TilePresenter` makes them, so the scheduler carries a `setLatencyProbe` hook
// (`core/render/RenderScheduler.h`, S4 T14) and this file installs the only one in the tree.
//
// **The lane is the subject, not a filter.** A tile the person is looking at is enqueued at
// `Visible`; the thumbnails pane's are `Speculative` (D4-10); and a window that is NOT in front
// enqueues its viewport at `Prefetch` -- T11's named work. So grading the `Visible` lane IS
// grading "the window a person is actually looking at", which is what the verdict is about. The
// other two lanes are reported beside it and **are not graded**: a thumbnail arriving late is a
// thumbnail, and a background window's tile arriving late is the mechanism working.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS CANNOT SEE, SO IT IS NOT READ AS MORE THAN IT IS
// ---------------------------------------------------------------------------------------------
//   * **It is offscreen by default.** `--native` runs on the real platform. Neither draws to a
//     monitor at 60 Hz, so this measures the PIPELINE's latency and not whether a person perceives
//     a stutter -- manual case **M-06** is the only thing that can say that, and it is owed.
//   * **One rig, one generated corpus** (D2-11), like every A2 figure before it.
//   * **The two-window column is two windows in ONE process on ONE machine's displays.** T2's row
//     of the same name was two 1920x1080 viewports in a harness; this one is `window.new`, a real
//     second `MainWindow` with its own handle (D4-17), raised so the first is in front.
//   * It never fails on speed. Its exit code is about whether the measurement was POSSIBLE.

#include "app/CompositionRoot.h"

#include "core/Log.h"
#include "core/cmd/ActionId.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"

#include "ui/panes/ThumbnailsPane.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/SidebarDock.h"
#include "ui/viewer/LoupeRaster.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SplitViewContainer.h"
#include "ui/viewer/ViewportController.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMouseEvent>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <vector>

using namespace rawpage;
using core::render::RenderPriority;

namespace {

constexpr double kFrameBudgetMs = 1000.0 / 60.0;

// ---------------------------------------------------------------------------------------------
// A2'S OWN CONFIGURATION, REACHED THROUGH THE PRODUCT -- and getting here is most of what this
// file had to learn.
// ---------------------------------------------------------------------------------------------
// A2 asks about *a text-heavy A4 page at 200 % on a 150 %-scaled monitor*: 96 x 1.50 x 2.00 = **288
// dpi**, over a **1920x1080 DEVICE-pixel** viewport. Every A2 figure since the T3 probe is that
// workload, `bench_pipeline`'s Stage 4 columns included.
//
// **The first version of this bench ran at the offscreen default -- 96 dpi, zoom 1.0 -- and
// reported a viewport p95 of 0.361 ms against T2's 9.78 ms.** That is not the product being 27x
// faster than its own bench; it is a viewport of about five tiles being compared with one of
// several dozen. A figure like that, recorded beside T2's as "the product's", would have been
// D2-1's exact error committed in the document that exists to warn about it -- so the two knobs
// below are not configuration, they are what makes the comparison mean anything.
//
// **AND `QT_SCALE_FACTOR=1.5` IS NOT ENOUGH, which is the second thing this file had to learn.**
// Qt's scale factor moves `devicePixelRatio`; `core::render::DpiPolicy` computes the rasterization
// dpi from the monitor's PHYSICAL dpi, which `ui::metricsFor()` takes from `IDisplayTopology` on a
// real desktop and from `QScreen::physicalDotsPerInch()` when there is no topology to join against
// -- issue **I-20**, met here from the other side. Under `offscreen` that is about 100 dpi, not
// 144, so scale 1.5 and zoom 200 % reach **199.9 dpi and not 288**, and the second version of this
// bench reported that as A2's workload.
//
// So the zoom is SOLVED FOR rather than set: whatever the screen says its physical dpi is, the
// zoom that puts `resolution().dpi` at 288 is the one used, and the actual figure is printed. A2's
// question is about the rasterizer's workload at 288 dpi; the zoom is how this rig reaches it, and
// naming a zoom that reaches a different dpi would be naming the wrong thing.
//
// The viewport is grown the same way, for the same reason: a `MainWindow` has a ribbon, a status
// bar and a dock, so a 1280x720 window does not contain a 1920x1080 viewport. The window is resized
// until the viewport is at least A2's size or the platform refuses, and what was actually reached
// is printed beside what was asked for.
constexpr double kA2MonitorScale = 1.50;
constexpr double kA2Dpi = 288.0;
constexpr int kDeviceViewportWidth = 1920;
constexpr int kDeviceViewportHeight = 1080;

QString corpusFile(const char* relative)
{
    return QString::fromStdString((std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative).string());
}

double percentile(std::vector<double> samples, double fraction)
{
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    const auto at = static_cast<std::size_t>(fraction * static_cast<double>(samples.size() - 1));
    return samples.at(std::min(at, samples.size() - 1));
}

// Run the event loop for `milliseconds`, which is how a tile gets from a worker to a subscriber in
// this product: the worker wakes, `CompositionRoot` posts `deliverReady()`, and the queued call
// runs here. A `processEvents` without a deadline returns before the worker has finished.
void pump(int milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (timer.elapsed() < milliseconds);
}

struct Samples {
    std::vector<double> visible;
    std::vector<double> prefetch;
    std::vector<double> speculative;

    void clear()
    {
        visible.clear();
        prefetch.clear();
        speculative.clear();
    }

    void add(RenderPriority lane, double ms)
    {
        switch (lane) {
        case RenderPriority::Visible:     visible.push_back(ms); break;
        case RenderPriority::Prefetch:    prefetch.push_back(ms); break;
        case RenderPriority::Speculative: speculative.push_back(ms); break;
        }
    }
};

struct Column {
    const char* label;
    bool split = false;
    bool pane = false;
    bool secondWindow = false;
    // S4 D4-20 (guide A-100): the Loupe armed and its lens stopped twice a round, so its sharp
    // re-rasterizations are enqueued in the Visible lane beside the viewport's and are INSIDE the
    // graded figure rather than beside it.
    bool loupe = false;
};

} // namespace

int main(int argc, char** argv)
{
    bool native = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--native") == 0) native = true;
    if (!native) qputenv("QT_QPA_PLATFORM", "offscreen");

    // BEFORE `QApplication`, and it has to be: Qt reads the scale factor once, when the platform
    // integration is created, and a `setenv` after this line changes nothing while looking as
    // though it did. See the constants above for why 1.5 rather than whatever the box reports.
    if (qEnvironmentVariableIsEmpty("QT_SCALE_FACTOR"))
        qputenv("QT_SCALE_FACTOR", QByteArray::number(kA2MonitorScale));

    QApplication application(argc, argv);

    int rounds = 12;
    int pumpMs = 400;
    QString only;
    const QStringList arguments = QCoreApplication::arguments();
    for (int at = 1; at < arguments.size(); ++at) {
        const QString& argument = arguments.at(at);
        const bool hasValue = at + 1 < arguments.size();
        if (argument == QLatin1String("--native")) continue;
        if (argument == QLatin1String("--pump") && hasValue) { pumpMs = arguments.at(++at).toInt(); continue; }
        if (argument == QLatin1String("--only") && hasValue) { only = arguments.at(++at); continue; }
        bool numeric = false;
        const int value = argument.toInt(&numeric);
        if (numeric && value > 0) rounds = value;
    }

    const QString document = corpusFile("ordinary/pages-900.pdf");
    if (!QFileInfo::exists(document)) {
        std::fprintf(stderr, "surface: %s is not there\n", document.toStdString().c_str());
        return 1;
    }

    std::printf("\nA2 AT STAGE 4, RE-READ THROUGH THE SHIPPED PANES -- T14, D4-16 (guide A-72)\n");
    std::printf("    %s, %d round(s), %d ms of event loop a round, platform %s\n",
                QFileInfo(document).fileName().toStdString().c_str(), rounds, pumpMs,
                QGuiApplication::platformName().toStdString().c_str());
    std::printf("    A2's configuration, ASKED FOR: %.0f dpi over a %dx%d device-pixel viewport."
                " What each column REACHED is printed with it.\n",
                kA2Dpi, kDeviceViewportWidth, kDeviceViewportHeight);
    std::printf("    The graded quantity is the VISIBLE lane's p95 queued latency -- the window a\n"
                "    person is looking at. Speculative (thumbnails) and Prefetch (a background\n"
                "    window, T11's named work) are reported beside it and are NOT graded.\n");

    const Column columns[] = {
        {"viewer",       false, false, false},
        {"+ split",      true,  false, false},
        {"+ pane",       true,  true,  false},
        {"two windows",  true,  true,  true },
        {"+ loupe",      false, false, false, true},
    };

    double viewerP95 = 0.0;
    double splitP95 = 0.0;
    double paneP95 = 0.0;
    double twoWindowP95 = 0.0;
    double loupeP95 = 0.0;
    bool measuredAll = true;

    for (const Column& column : columns) {
        if (!only.isEmpty() && only != QLatin1String(column.label)) continue;

        // A COMPOSITION PER COLUMN, torn down between them. One cache and one scheduler carried
        // across the four would make every column after the first a measurement of how warm the
        // cache was left by the one before it -- which is a real property of the product and is
        // not the property being compared here.
        QTemporaryDir scratch;
        if (!scratch.isValid()) {
            std::fprintf(stderr, "surface: no scratch directory\n");
            return 1;
        }
        app::CompositionRoot::Options options;
        options.settingsPath = QDir(scratch.path()).filePath(QStringLiteral("settings.ini"));
        options.sessionPath  = QDir(scratch.path()).filePath(QStringLiteral("session.json"));
        options.recoveryRoot = QDir(scratch.path()).filePath(QStringLiteral("recovery"));
        app::CompositionRoot root(std::move(options));
        root.settings().set(core::keys::logging::level, core::LogLevel::Error);

        Samples samples;
        root.renderScheduler().setLatencyProbe(
            [&samples](RenderPriority lane, double ms) { samples.add(lane, ms); });

        auto window = std::make_unique<ui::MainWindow>(root.services());
        window->resize(static_cast<int>(kDeviceViewportWidth / kA2MonitorScale),
                       static_cast<int>(kDeviceViewportHeight / kA2MonitorScale));
        window->show();
        window->openFiles({document});
        if (window->documentCount() != 1) {
            std::fprintf(stderr, "surface: %s did not open\n", document.toStdString().c_str());
            return 1;
        }
        ui::DocumentWindow* tab = window->currentDocument();

        if (column.split && tab->split() != nullptr)
            tab->split()->setMode(ui::SplitViewContainer::Mode::Horizontal);

        // THE PANE IS THERE UNLESS THIS COLUMN HIDES IT, and that is a correction rather than a
        // refinement. A `MainWindow` is built with the sidebar OPEN and `SidebarDock::addPane`
        // shows the first pane registered -- which is Thumbnails (T5). So the first two columns of
        // this bench had a live thumbnails pane in them, the `+ pane` column added nothing, and the
        // three figures were three measurements of the same workload. Hiding it is what makes the
        // column mean what its label says.
        if (window->sidebar() != nullptr) {
            if (column.pane) {
                window->sidebar()->show();
                window->sidebar()->showPane(core::ActionId::PaneThumbnails);
            } else {
                window->sidebar()->hide();
            }
        }

        // A2'S RESOLUTION, ON EVERY PANE OF EVERY WINDOW. The zoom is solved from what this rig's
        // screen actually reports (see the constants above), and set after the split exists so both
        // panes get it. `setZoom` is the product's own path -- the same call `Ctrl+=` makes.
        double a2Zoom = 1.0;
        if (const ui::PageView* pane = tab->view()) {
            const double dpiAtCurrentZoom = pane->layoutEngine().resolution().dpi;
            const double zoomNow = pane->controller().zoom().factor;
            if (dpiAtCurrentZoom > 0.0 && zoomNow > 0.0)
                a2Zoom = kA2Dpi * zoomNow / dpiAtCurrentZoom;
        }
        const auto zoomAll = [a2Zoom](ui::MainWindow* shell) {
            if (shell == nullptr) return;
            ui::DocumentWindow* current = shell->currentDocument();
            if (current == nullptr || current->split() == nullptr) return;
            for (ui::PageView* pane : current->split()->views())
                pane->setZoom(core::render::Zoom{a2Zoom}.clamped());
        };
        zoomAll(window.get());

        // AND GROW THE WINDOW UNTIL THE VIEWPORT IS A2's, or until the platform stops giving. Two
        // passes, because the ribbon and the status bar take their height from the first layout.
        for (int attempt = 0; attempt < 2; ++attempt) {
            const ui::PageView* pane = tab->view();
            if (pane == nullptr) break;
            const double haveWidth = pane->controller().visibleRect().size.width;
            const double haveHeight = pane->controller().visibleRect().size.height;
            if (haveWidth >= kDeviceViewportWidth && haveHeight >= kDeviceViewportHeight) break;
            const int growX = static_cast<int>((kDeviceViewportWidth - haveWidth) / kA2MonitorScale);
            const int growY = static_cast<int>((kDeviceViewportHeight - haveHeight) / kA2MonitorScale);
            window->resize(window->width() + std::max(0, growX),
                           window->height() + std::max(0, growY));
            pump(60);
        }

        ui::MainWindow* second = nullptr;
        if (column.secondWindow) {
            const core::Result<ui::MainWindow*> opened = window->openSecondView();
            if (!opened) {
                std::fprintf(stderr, "surface: window.new refused -- %s\n",
                             opened.error().messageKey.data());
                return 1;
            }
            second = opened.value();
            // THE SAME SIZE THE FIRST WINDOW ENDED UP AT, not the size it was asked for: the first
            // was grown above until its viewport reached A2's, and a second window a third smaller
            // would make "two windows" mean two different workloads.
            second->resize(window->size());
            second->show();

            // AND THE FIRST IS RAISED, which is what makes the first window the one in front and
            // the second a BACKGROUND window whose viewport queues at `Prefetch` (T11's named
            // work). `raiseToForeground()` sets the active shell directly and refreshes the lanes,
            // because offscreen no activation event ever arrives -- `MainWindow.cpp` says so at
            // the call it makes for the same reason.
            zoomAll(second);
            window->raiseToForeground();
        }

        // THE RESOLUTION ACTUALLY REACHED, printed rather than assumed. If the scale factor did
        // not take -- a platform that ignores it, a `QT_SCALE_FACTOR` already in the environment --
        // this line is what says so, instead of a comparison quietly being made against a
        // different workload. It is the mistake this bench made on its first run.
        if (const ui::PageView* pane = tab->view()) {
            std::printf("    resolution: %.1f dpi at zoom %.0f %% (device scale %.2f), viewport"
                        " %.0fx%.0f device px -- A2's is 288.0 dpi over 1920x1080\n",
                        pane->layoutEngine().resolution().dpi,
                        pane->controller().zoom().factor * 100.0,
                        pane->layoutEngine().resolution().devicePixelRatio,
                        pane->controller().visibleRect().size.width,
                        pane->controller().visibleRect().size.height);
        }

        // THE LOUPE, ARMED THROUGH THE PRODUCT'S OWN PATH -- `PageView::setTool`, which `tool.loupe`
        // dispatches to -- so the lens is the shipped one, fed by the shipped scheduler.
        ui::PageView* lensView = column.loupe ? tab->view() : nullptr;
        if (lensView != nullptr) lensView->setTool(ui::ToolKind::Loupe);
        const auto lensTo = [lensView](QPoint at) {
            if (lensView == nullptr) return;
            QMouseEvent move(QEvent::MouseMove, QPointF(at),
                             QPointF(lensView->viewport()->mapToGlobal(at)), Qt::NoButton,
                             Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(lensView->viewport(), &move);
        };

        // Settle: the first paint, the first tiles, the pane's first stream. Everything measured
        // below is steady-state scrolling, and counting the open would measure a document opening.
        pump(pumpMs * 2);
        samples.clear();
        root.renderCache().resetStats();
        const core::render::RenderScheduler::Stats before = root.renderScheduler().stats();

        // THE WORKLOAD: a page-sized scroll a round, in every pane of every window, exactly as a
        // person reading would produce it. Nothing here enqueues a tile.
        for (int round = 0; round < rounds; ++round) {
            const auto scrollAll = [](ui::MainWindow* shell) {
                if (shell == nullptr) return;
                ui::DocumentWindow* current = shell->currentDocument();
                if (current == nullptr || current->split() == nullptr) return;
                for (ui::PageView* pane : current->split()->views())
                    pane->nextScreen();
            };
            scrollAll(window.get());
            scrollAll(second);

            // AND THE THUMBNAILS PANE MOVES TOO, which the first version of this bench did not do
            // -- so the pane streamed once at open and then sat still, and its column measured a
            // pane that was not working. T2's workload says it in as many words: *each round moves
            // both viewports AND THE PANE onto pages they have not rendered*, because a workload
            // that asks for the same tiles twice measures the cache and calls it the pipeline.
            if (column.pane && window->thumbnailsPane() != nullptr)
                window->thumbnailsPane()->setCurrentPage((round + 1) * 30);

            // AND THE LENS, TWICE A ROUND: swept across the page onto a spot, still long enough to
            // settle (`PageView::kLoupeSettleMs`), then onto a second spot and still again -- two
            // sharp requests a round, each over a page the viewport has only just asked for.
            if (lensView != nullptr) {
                const QSize area = lensView->viewport()->size();
                const int x = area.width() / 3 + (round % 4) * 40;
                for (int step = 0; step < 6; ++step) {
                    lensTo(QPoint(x + step * 12, area.height() / 3));
                    pump(8);
                }
                pump(ui::PageView::kLoupeSettleMs + 60);
                lensTo(QPoint(x + 200, area.height() * 2 / 3));
                pump(std::max(0, pumpMs - ui::PageView::kLoupeSettleMs - 60 - 48));
            } else {
                pump(pumpMs);
            }
        }

        const core::render::RenderScheduler::Stats after = root.renderScheduler().stats();
        const core::render::CacheStats cache = root.renderCache().stats();

        const double p95 = percentile(samples.visible, 0.95);
        std::printf("\n    %-14s VISIBLE  p95 %7.3f ms   median %7.3f   n %zu\n", column.label, p95,
                    percentile(samples.visible, 0.50), samples.visible.size());
        if (!samples.speculative.empty())
            std::printf("                   speculative (thumbnails, not graded)  p95 %7.3f ms   n %zu\n",
                        percentile(samples.speculative, 0.95), samples.speculative.size());
        if (!samples.prefetch.empty())
            std::printf("                   prefetch (the background window, not graded)  p95 %7.3f ms   n %zu\n",
                        percentile(samples.prefetch, 0.95), samples.prefetch.size());
        std::printf("                   lanes enqueued: visible %lld   prefetch %lld   speculative %lld\n",
                    after.enqueuedVisible - before.enqueuedVisible,
                    after.enqueuedPrefetch - before.enqueuedPrefetch,
                    after.enqueuedSpeculative - before.enqueuedSpeculative);
        if (lensView != nullptr) {
            std::printf("                   lens requests %lld (the Loupe's sharp regions, in the VISIBLE lane)\n",
                        lensView->loupeRaster().enqueuedCount());
            if (lensView->loupeRaster().enqueuedCount() == 0) {
                std::fprintf(stderr, "surface: %s made no lens request -- it measured nothing\n",
                             column.label);
                measuredAll = false;
            }
        }
        std::printf("                   cache lookups %lld   hits %lld (%.1f %%)   CROSS-DOCUMENT evictions %zu\n",
                    cache.lookups, cache.hits(), cache.hitRate() * 100.0,
                    root.renderCache().crossDocumentEvictions());

        if (samples.visible.empty()) {
            std::fprintf(stderr, "surface: %s delivered no visible tile -- no measurement\n",
                         column.label);
            measuredAll = false;
        }

        if (std::strcmp(column.label, "viewer") == 0)            viewerP95 = p95;
        else if (std::strcmp(column.label, "+ split") == 0)      splitP95 = p95;
        else if (std::strcmp(column.label, "+ pane") == 0)       paneP95 = p95;
        else if (std::strcmp(column.label, "two windows") == 0)  twoWindowP95 = p95;
        else if (std::strcmp(column.label, "+ loupe") == 0)      loupeP95 = p95;

        // The probe outlives nothing: the scheduler is destroyed with the root below, but the
        // lambda holds a reference to a `Samples` on this stack frame, so it is removed first.
        root.renderScheduler().setLatencyProbe({});
        if (second != nullptr) second->close();
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        pump(50);
    }

    if (only.isEmpty()) {
        std::printf("\n    THE PRODUCT'S OWN FIGURE, beside T2's bench figure (Tech-Stack-Plan.md 3.3):\n");
        std::printf("      one viewport ........................................ %7.3f ms"
                    "   (T2's bench: 9.78-9.82)\n", viewerP95);
        std::printf("      + a split -- the SAME window divided in two .......... %7.3f ms   (%+.3f)"
                    "   (T2: 9.91-9.99)\n", splitP95, splitP95 - viewerP95);
        std::printf("      + the thumbnails pane at Speculative ................. %7.3f ms   (%+.3f)"
                    "   (T2: 10.59-10.74)  <- D4-16's workload\n",
                    paneP95, paneP95 - splitP95);
        std::printf("      two WINDOWS, the second in the background ............ %7.3f ms   (%+.3f)"
                    "   (T2: 18.91-19.33, BEFORE T11's named work)\n",
                    twoWindowP95, twoWindowP95 - paneP95);
        std::printf("      one viewport + the Loupe's SHARP lens (D4-20, A-100) . %7.3f ms   (%+.3f)"
                    "   against the one viewport\n", loupeP95, loupeP95 - viewerP95);
        std::printf("      frame budget ......................................... %7.3f ms\n",
                    kFrameBudgetMs);
        std::printf("\n    Record this in Tech-Stack-Plan.md 3.3 as THE PRODUCT'S, beside T2's bench"
                    " figure and\n    never instead of it (D2-1's lesson). This bench never fails on"
                    " speed.\n\n");
    }

    return measuredAll ? 0 : 1;
}
