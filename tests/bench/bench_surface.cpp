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
//
// ---------------------------------------------------------------------------------------------
// A2 AT STAGE 5, RE-READ THROUGH THE SHIPPED SURFACES -- Stage 5 T14, decision D5-22; guide case
// A-112 (`Stage5-TextSearch-Testing-Guide.md` §5.16)
// ---------------------------------------------------------------------------------------------
// T3 measured Stage 5's workload with `bench_pipeline --stage5` BEFORE any surface queued text, and
// it issued that work itself: a whole viewport at `Visible` each round, 32 text jobs at a time
// behind it. This is the second reading D5-22 asked for, the same way the Stage 4 columns above are
// T2's: the work behind the viewport is started through the SHIPPED surface -- the find bar's field
// typed into key by key (`ui/write/FindReplaceBar`), the Advanced Search pane's own `search()` over
// a folder (`ui/panes/SearchPane`), `MainWindow::compareDocuments` as the Compare dialog's OK calls
// it (`ui/viewer/ComparisonView`) -- and every text job, open piece and raster behind it is the
// product's own, at the lane and the pace the product chose.
//
// **The graded samples are the tiles delivered WHILE THE WORK RAN**, and only those: a column
// whose search finished in its second round and then scrolled ten more over an idle engine would
// otherwise be graded mostly on the idle part. A Stage 5 column keeps scrolling until its work is
// done (and at least the column's rounds), and repeats from a fresh composition, so the graded
// figure pools every repetition's busy samples; the whole column is printed beside it.
//
// Its own bounds, beside the ones above:
//   * **The comparison column waits for its window's FIRST FILL before it scrolls.** The comparison
//     opens a window of its own whose two panes ask for their first page at `Visible`, ahead of the main
//     viewport's tiles. That is a document opening, which every column keeps out of its figure (the
//     settle), so it is waited for with the main window still and reported on a line of its own. Its
//     first readings at T14 were ~25 ms, and two things were behind them: that fill inside the grade,
//     and guide I-11 -- the comparison's window WIDENING under its growing progress line, so both panes
//     re-fitted and re-rasterized five times. After the fill, the main window goes on scrolling, which a
//     person looking at the comparison would not be doing, so the graded figure is an upper bound on what
//     that person sees.
//   * **Each work column is printed beside the viewer ALONE over the same rounds.** A column's p95
//     depends on how many rounds it is taken over -- the first rounds after the settle carry the slowest
//     tiles -- and the work columns run for different lengths, so the text viewer runs
//     `kStage5BaselineRounds` and is cut to each column's length to be its control.
//   * **The folder is a scratch copy of the corpus**, as T3's was, so the search does not depend on
//     whose machine runs it.
//   * T3 also counted priority inversions and the longest piece; those need the job stamps only the
//     pipeline bench owns, and are T3's and T9's figures, not re-measured here.
//
// ---------------------------------------------------------------------------------------------
// A2 AT STAGE 6, RE-READ THROUGH THE SHIPPED SURFACES -- Stage 6 T15, decision D6-24; guide case
// A-104 (`Stage6-Annotations-Testing-Guide.md` §5.18), graded on that guide's §3.1
// ---------------------------------------------------------------------------------------------
// T4 measured Stage 6's workload with `bench_pipeline --stage6` BEFORE any tool existed: it constructed each
// `AddAnnotationCommand` itself and asked for the tiles itself. This is the second reading. `--stage6` runs
// two columns over `pages-900.pdf` in a window split in two (`SplitViewContainer::Mode::Horizontal`, the
// scroll NOT synchronized -- what a person drawing in one pane and reading in the other has):
//   * **annotation viewer**: the top pane parked on page 450, the bottom pane scrolled a screen a round at
//     D4-16's pace. Nothing is drawn. It is the control.
//   * **+ pencil**: the same, and every 100 ms a STROKE in the top pane -- the pencil armed by its own
//     command (`annot.pencil`, with `annot.keepToolSelected` so it stays armed), a press, 199 moves a pixel
//     apart along a wave and a release, sent to the pane's viewport as a person's mouse would send them. The
//     release is the shipped `InkTool` making ONE `Ink` annotation of ~200 samples through the overlay and
//     the journal, whose show step composes, verifies and queues the reload (D6-2); `PageView` asks for the
//     page again; and every tile is the product's, at the lane the product chose. Nothing here enqueues.
//
// **What is graded is split by PANE**, which the latency probe alone cannot do: the scheduler calls the
// probe and then, for the SAME tile, every ready subscriber, so a subscriber of this file's reads the
// probe's last figure and files it under the tile's page. Page 450 is the drawing pane's; every other page
// is the scrolling pane's, which never reaches page 450 in a run. The SCROLLING pane's Visible p95 is §3.1's
// viewport figure -- T4's quantity -- and both panes together are printed beside it.
//
// **Edit-to-pixels** is from the release RETURNING (the command run, its compose and verify inside it) to
// the delivery of a page-450 tile at the edit's revision or later, at the drawing pane's scale, that covers
// the stroke and whose pixels CHANGED from that tile's previous delivery (rule 1). An edit no such tile ever
// reached is counted and printed.
//
// Its bounds, beside the ones above: one pencil, not every tool -- the pencil is §3.1's edit; the release's
// time includes the overlay's own work beside the journal's, which `bench_pipeline` does not have; the moves
// are sent back to back, so the stroke takes a few milliseconds where a hand takes most of a second, which
// puts the strokes closer together than a person's -- an upper bound on the rate, as §3.1's burst is.

#include "app/Application.h"
#include "app/CompositionRoot.h"

#include "core/Log.h"
#include "core/cmd/ActionId.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"
#include "core/text/CompareSensitivity.h"
#include "core/text/DocumentComparer.h"

#include "ui/dialogs/CompareDialog.h"
#include "ui/panes/SearchPane.h"
#include "ui/panes/ThumbnailsPane.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/SidebarDock.h"
#include "ui/viewer/ComparisonView.h"
#include "ui/viewer/LoupeRaster.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SplitViewContainer.h"
#include "ui/viewer/TilePresenter.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/ViewportController.h"
#include "ui/write/FindReplaceBar.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
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
//
// `everySlice` runs after each slice (Stage 5 T14): a Stage 5 column re-reads there whether its
// work is still running, so a tile delivered in the next slice is filed on the right side of the
// moment the work finished, to within one slice.
void pump(int milliseconds, const std::function<void()>& everySlice = {})
{
    QElapsedTimer timer;
    timer.start();
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (everySlice) everySlice();
    } while (timer.elapsed() < milliseconds);
}

struct Samples {
    std::vector<double> visible;
    std::vector<double> prefetch;
    std::vector<double> speculative;
    // Stage 5 T14: the VISIBLE tiles delivered while the column's text work was running -- the
    // graded subset of `visible` for a Stage 5 column.
    std::vector<double> visibleWhileBusy;
    bool busy = false;
    // THE ROUND each visible tile arrived in, so a Stage 5 work column can be set beside the viewer ALONE
    // over the same rounds: a column's p95 depends on how many rounds it is taken over (the first rounds
    // after the settle carry the slowest tiles), and the work columns run for different lengths.
    int round = 0;
    std::vector<std::pair<int, double>> visibleByRound;

    void clear()
    {
        visible.clear();
        prefetch.clear();
        speculative.clear();
        visibleWhileBusy.clear();
        visibleByRound.clear();
        round = 0;
    }

    void add(RenderPriority lane, double ms)
    {
        switch (lane) {
        case RenderPriority::Visible:
            visible.push_back(ms);
            if (busy) visibleWhileBusy.push_back(ms);
            visibleByRound.emplace_back(round, ms);
            break;
        case RenderPriority::Prefetch:    prefetch.push_back(ms); break;
        case RenderPriority::Speculative: speculative.push_back(ms); break;
        // Stage 5 T2 (D5-3): the lane below Speculative. The probe reports TILES only, and no
        // shipped surface queues a tile there -- text jobs use it and are never probed.
        case RenderPriority::Background:  break;
        }
    }
};

// Stage 5 T14 (D5-22, guide A-112): the text work a Stage 5 column starts through a shipped surface.
enum class TextWork {
    None,
    Find,      // the find bar's FIRST search of `text-900.pdf`, which builds its index at `Background`
    Folder,    // the Advanced Search pane over a scratch copy of the corpus
    Compare,   // a comparison of `text-900.pdf` with itself at *appearance*: every page's text and raster
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
    // Stage 5 T14: the document scrolled (`pages-900.pdf` for Stage 4's columns, `text-900.pdf` --
    // T3's -- for Stage 5's) and the work behind it.
    const char* document = "ordinary/pages-900.pdf";
    TextWork work = TextWork::None;
    bool stage5 = false;
    // Stage 6 T15 (D6-24, guide A-104): the split's top pane on page 450, the bottom pane scrolling; `pencil`
    // draws a stroke in the top pane every 100 ms.
    bool stage6 = false;
    bool pencil = false;
};

// The longest a Stage 5 column scrolls waiting for its work to finish, in rounds: two minutes at
// the default pace. A column that reaches it says so rather than grading a truncated run.
constexpr int kMaxBusyRounds = 300;

// A query with ONE hit, on the last page (`corpus/make-text.py`'s marker): the find bar takes the
// view to its current hit, and a hit found early would move the scroll before the index was built.
const QString kFindQuery = QStringLiteral("unique-page-0900");
// The folder search's query, T3's and T9's.
const QString kFolderQuery = QStringLiteral("RawPage");

// The text viewer's rounds -- long enough that every work column has the viewer ALONE over as many rounds
// as its work ran (the comparison's ran 26 at T14), which is the control it is printed beside.
constexpr int kStage5BaselineRounds = 40;

// One graded figure, pooled over a column's repetitions.
struct Pooled {
    std::vector<double> graded;
    std::vector<double> all;
    std::vector<double> perRunP95;
    std::vector<double> workMs;
    std::vector<int> busyRoundsPerRun;
    int busyRounds = 0;
    std::vector<std::pair<int, double>> byRound;   // the text viewer's: its control for the others
};

// ---------------------------------------------------------------------------------------------
// STAGE 6 (T15, guide A-104): THE SPLIT, ONE PANE DRAWN IN AND ONE SCROLLED
// ---------------------------------------------------------------------------------------------
constexpr int kStage6Page = 449;          // the guide's page 450, 0-based
constexpr int kStage6PeriodMs = 100;      // one stroke every 100 ms (§3.1)
constexpr int kStrokeMoves = 199;         // a press, 199 moves a logical pixel apart and a release: ~200 samples
constexpr int kStrokeAdvance = 210;       // logical pixels between two strokes' starts in a row
constexpr int kStrokeRow = 24;            // and between two rows

struct Stage6Run {
    std::vector<double> scroll;       // the SCROLLING pane's Visible tiles -- §3.1's viewport figure
    std::vector<double> scope;        // the drawing pane's (page 450) Visible tiles
    std::vector<double> all;          // both
    std::vector<double> toPixels;     // edit -> pixels
    std::vector<double> releaseMs;    // the release's whole handling: the stroke made, its command run
    int strokes = 0;
    int made = 0;
    long long unseen = 0;             // an edit no changed tile ever covered
    long long unchanged = 0;          // a page-450 tile delivered over an edit with the pixels it had before
    double longestStallMs = 0.0;      // the UI thread's longest gap between two event-loop slices
    int stallsOverBudget = 0;
    int rounds = 0;
    std::string refused;              // why the column could not be measured, if it could not
};

// One stroke of the pencil on the drawing pane: a wave starting `index` strokes into a grid over the part of
// page 450 the pane shows, its phase moved a quarter turn a lap so a stroke never retraces an earlier one.
// Returns the device rectangle it covers, page-local -- the coordinates a tile's request is in -- or nothing
// when the page shows no room for it.
std::optional<QRect> drawStroke(ui::PageView& pane, int index)
{
    // THE PAGE'S PLACE COPIED, NOT POINTED AT: the release below runs the command, the view lays its pages out again,
    // and a `PageSlot*` taken here then points into a layout that is gone -- the first version of this file read one
    // and reported a stroke at y = 1,517,453 that no tile could ever cover.
    const core::render::PageSlot* found = pane.layoutEngine().layout().slotForPage(kStage6Page);
    if (found == nullptr) return std::nullopt;
    const core::DeviceRect page = found->rect;
    const core::DeviceRect visible = pane.controller().visibleRect();
    const double dpr = pane.devicePixelRatioF() > 0.0 ? pane.devicePixelRatioF() : 1.0;
    // The page's visible part, in the viewport's logical pixels.
    const QRectF sheet((page.left() - visible.left()) / dpr, (page.top() - visible.top()) / dpr,
                       page.size.width / dpr, page.size.height / dpr);
    const QRect region = sheet.toAlignedRect().intersected(pane.viewport()->rect()).adjusted(12, 12, -12, -12);
    const int columns = (region.width() - kStrokeMoves) / kStrokeAdvance + 1;
    const int rows = region.height() / kStrokeRow;
    if (columns < 1 || rows < 1) return std::nullopt;
    const int cell = index % (columns * rows);
    const int lap = index / (columns * rows);
    const int x0 = region.left() + (cell % columns) * kStrokeAdvance;
    const int middle = region.top() + (cell / columns) * kStrokeRow + kStrokeRow / 2;
    const auto pointAt = [&](int step) {
        return QPoint(x0 + step, middle + static_cast<int>(std::lround(7.0 * std::sin(step * 2.0 * 3.14159265358979 / 50.0
                                                                                        + lap * 3.14159265358979 / 2.0))));
    };
    const auto send = [&pane](QEvent::Type type, QPoint at) {
        QMouseEvent event(type, QPointF(at), pane.viewport()->mapToGlobal(QPointF(at)), Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(pane.viewport(), &event);
    };
    QRect logical(pointAt(0), QSize(1, 1));
    send(QEvent::MouseButtonPress, pointAt(0));
    for (int step = 1; step <= kStrokeMoves; ++step) {
        send(QEvent::MouseMove, pointAt(step));
        logical |= QRect(pointAt(step), QSize(1, 1));
    }
    send(QEvent::MouseButtonRelease, pointAt(kStrokeMoves));
    // Grown by the pen's half-width and anti-aliasing, then into the page's device pixels.
    logical.adjust(-4, -4, 4, 4);
    return QRect(static_cast<int>(std::floor(visible.left() + logical.left() * dpr - page.left())),
                 static_cast<int>(std::floor(visible.top() + logical.top() * dpr - page.top())),
                 static_cast<int>(std::ceil(logical.width() * dpr)), static_cast<int>(std::ceil(logical.height() * dpr)));
}

Stage6Run measureStage6(app::CompositionRoot& root, ui::DocumentWindow& tab, ui::PageView& drawPane,
                        ui::PageView& scrollPane, bool pencil, int seconds, int pumpMs)
{
    Stage6Run run;
    core::render::RenderScheduler& scheduler = root.renderScheduler();
    core::CommandJournal* journal = tab.journal();
    if (journal == nullptr) {
        run.refused = "the tab has no journal";
        return run;
    }

    // THE PROBE AND A SUBSCRIBER, PAIRED: the scheduler times a tile, then hands the same tile to every
    // subscriber, so the figure the probe gave last is this tile's.
    struct Last {
        bool valid = false;
        RenderPriority lane = RenderPriority::Visible;
        double ms = 0.0;
    } last;
    scheduler.setLatencyProbe([&last](RenderPriority lane, double ms) { last = Last{true, lane, ms}; });

    struct Pending {
        core::Revision revision;
        qint64 returnedNs;
        QRect device;
    };
    std::deque<Pending> pending;
    std::map<std::string, QByteArray> lastPixels;
    QElapsedTimer clock;
    clock.start();
    const double scale = drawPane.layoutEngine().layout().presentedScale();
    const core::render::RenderScheduler::SubscriptionToken token = scheduler.subscribeReady(
        [&](const core::render::RenderRequest& request, const core::RasterImage& image) {
            const Last probed = last;
            last.valid = false;
            if (probed.valid && probed.lane == RenderPriority::Visible) {
                run.all.push_back(probed.ms);
                (request.pageIndex == kStage6Page ? run.scope : run.scroll).push_back(probed.ms);
            }
            if (request.document != drawPane.document() || request.pageIndex != kStage6Page) return;
            if (std::abs(request.deviceScale - scale) > 1e-9) return;
            const std::string key = std::to_string(request.tile.x) + "," + std::to_string(request.tile.y) + ","
                                    + std::to_string(request.tile.width) + "," + std::to_string(request.tile.height) + "/"
                                    + std::to_string(static_cast<int>(request.viewRotation)) + "/"
                                    + std::to_string(static_cast<unsigned>(request.flags));
            const auto previous = lastPixels.find(key);
            const bool changed = previous == lastPixels.end() || previous->second != image.pixels;
            lastPixels[key] = image.pixels;
            const QRect tile(request.tile.x, request.tile.y, request.tile.width, request.tile.height);
            bool covers = false;
            for (const Pending& edit : pending)
                if (request.revision.value() >= edit.revision.value() && tile.intersects(edit.device)) covers = true;
            if (!covers) return;
            if (!changed) {
                ++run.unchanged;
                return;
            }
            const qint64 now = clock.nsecsElapsed();
            for (auto it = pending.begin(); it != pending.end();) {
                if (request.revision.value() >= it->revision.value() && tile.intersects(it->device)) {
                    run.toPixels.push_back(static_cast<double>(now - it->returnedNs) / 1.0e6);
                    it = pending.erase(it);
                } else {
                    ++it;
                }
            }
        });

    // THE EVENT LOOP, its slices timed: a gap well past a slice's 10 ms is the UI thread busy -- here, mostly
    // the release, whose command composes and verifies on this thread.
    qint64 lastSlice = clock.nsecsElapsed();
    const auto pumpUntil = [&](qint64 untilNs) {
        do {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            const qint64 now = clock.nsecsElapsed();
            const double gap = static_cast<double>(now - lastSlice) / 1.0e6;
            lastSlice = now;
            run.longestStallMs = std::max(run.longestStallMs, gap);
            if (gap > kFrameBudgetMs) ++run.stallsOverBudget;
        } while (clock.nsecsElapsed() < untilNs);
    };

    const std::size_t positionBefore = journal->position();
    const int beats = std::max(1, pumpMs / kStage6PeriodMs);
    run.rounds = std::max(1, seconds * 1000 / pumpMs);
    qint64 next = clock.nsecsElapsed();
    for (int round = 0; round < run.rounds; ++round) {
        scrollPane.nextScreen();
        for (int beat = 0; beat < beats; ++beat) {
            if (pencil) {
                const std::size_t before = journal->position();
                const qint64 began = clock.nsecsElapsed();
                const std::optional<QRect> device = drawStroke(drawPane, run.strokes);
                const qint64 returned = clock.nsecsElapsed();
                ++run.strokes;
                if (device) {
                    run.releaseMs.push_back(static_cast<double>(returned - began) / 1.0e6);
                    if (journal->position() == before + 1) {
                        ++run.made;
                        pending.push_back(Pending{journal->revision(), returned, *device});
                    }
                }
            }
            next += static_cast<qint64>(kStage6PeriodMs) * 1000000;
            pumpUntil(next);
        }
    }
    // Drain: every edit given up to three seconds to reach the pixels, the scroll still.
    const qint64 drainUntil = clock.nsecsElapsed() + 3000LL * 1000000;
    while (!pending.empty() && clock.nsecsElapsed() < drainUntil) pumpUntil(clock.nsecsElapsed() + 20LL * 1000000);
    run.unseen = static_cast<long long>(pending.size());
    // AN EDIT THAT NEVER REACHED THE PIXELS IS NAMED, with the tiles over it as they last arrived -- so a stroke
    // the product left unshown can be told from one this file looked for in the wrong place.
    for (const Pending& edit : pending) {
        std::printf("      unseen: revision %llu, device (%d, %d) %dx%d, made at %.0f ms\n",
                    static_cast<unsigned long long>(edit.revision.value()), edit.device.x(), edit.device.y(),
                    edit.device.width(), edit.device.height(), static_cast<double>(edit.returnedNs) / 1.0e6);
    }
    scheduler.unsubscribeReady(token);
    scheduler.setLatencyProbe({});

    if (pencil) {
        // THE PROOF THE PENCIL DID IT (convention 7): a command a stroke, EVERY stroke, every one an `Ink`. Its first
        // version compared the commands with the inks and the journal only, and planted P7 -- the pencil disarmed after
        // each stroke -- stayed green over "strokes 40, commands 1": one stroke drawn, a column graded as forty.
        int inks = 0;
        for (const core::Annotation& a : journal->document().annotations().all())
            if (a.id.kind == core::AnnotationId::Kind::Created && a.subtype == core::AnnotationSubtype::Ink) ++inks;
        if (run.made == 0 || run.made != run.strokes || journal->position() != positionBefore + static_cast<std::size_t>(run.made)
            || inks != run.made)
            run.refused = "the pencil made " + std::to_string(run.made) + " command(s) of " + std::to_string(run.strokes)
                          + " stroke(s) and the store holds " + std::to_string(inks) + " ink(s)";
        else if (run.toPixels.empty())
            run.refused = "no edit reached the pixels";
        else if (run.unseen > 0)
            run.refused = std::to_string(run.unseen) + " edit(s) never reached the pixels -- named above";
    }
    if (run.scroll.empty()) run.refused = "the scrolling pane delivered no visible tile";
    return run;
}

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

    // THE PRODUCT'S APPLICATION, AND ITS ENGLISH CATALOG -- Stage 5 T14. Until then this bench ran a
    // bare `QApplication` with no catalog, so every label was its catalog KEY, and the find bar's
    // keys (`prefs.setting.search.caseSensitive` and five like it) made its minimum width 2,966
    // logical pixels: the window could not be narrower, and the `+ find` column reached a 4428x1080
    // viewport -- a different workload, measured and caught on its first run. The shipped labels are
    // a word each. Loaded as `app/main.cpp` loads them, and the run refused without it.
    app::Application application(argc, argv);
    application.loadTranslators(QStringLiteral("en"), QString());
    if (application.loadedCatalogs().isEmpty()) {
        std::fprintf(stderr, "surface: no catalog under %s -- every label would be its key\n",
                     app::Application::catalogDirectory().toStdString().c_str());
        return 1;
    }
    // NOTE what the catalog does NOT fix offscreen: that platform's font metrics are about TWICE the
    // `windows` platform's (measured at T14: an average character 12 px against 6, with Segoe UI 9 pt
    // set or not), so chrome laid out here is wider than on Windows. It is why a find bar whose minimum
    // was its whole row (guide I-10) could not leave A2's viewport offscreen at all -- and the line
    // printed when the bar opens says what the bar and the window ask for now.

    int rounds = 12;
    int pumpMs = 400;
    int repeats = 3;   // Stage 5 columns only; Stage 4's run once each, as they did at its T14
    QString only;
    bool stage4Only = false;
    bool stage5Only = false;
    bool stage6Only = false;
    int stage6Seconds = 30;   // §3.1's 30 s a column
    // An ATTRIBUTION switch, not a workload: the comparison at *text only*, which asks for no raster,
    // so its difference from the shipped column is what the 72-dpi rasters cost the viewport.
    bool comparisonTextOnly = false;
    const QStringList arguments = QCoreApplication::arguments();
    for (int at = 1; at < arguments.size(); ++at) {
        const QString& argument = arguments.at(at);
        const bool hasValue = at + 1 < arguments.size();
        if (argument == QLatin1String("--native")) continue;
        if (argument == QLatin1String("--stage4")) { stage4Only = true; continue; }
        if (argument == QLatin1String("--stage5")) { stage5Only = true; continue; }
        if (argument == QLatin1String("--stage6")) { stage6Only = true; continue; }
        if (argument == QLatin1String("--seconds") && hasValue) { stage6Seconds = std::max(1, arguments.at(++at).toInt()); continue; }
        if (argument == QLatin1String("--comparison-text-only")) { comparisonTextOnly = true; continue; }
        if (argument == QLatin1String("--pump") && hasValue) { pumpMs = arguments.at(++at).toInt(); continue; }
        if (argument == QLatin1String("--repeat") && hasValue) { repeats = std::max(1, arguments.at(++at).toInt()); continue; }
        if (argument == QLatin1String("--only") && hasValue) { only = arguments.at(++at); continue; }
        bool numeric = false;
        const int value = argument.toInt(&numeric);
        if (numeric && value > 0) rounds = value;
    }

    for (const char* relative : {"ordinary/pages-900.pdf", "ordinary/text-900.pdf"}) {
        if (!QFileInfo::exists(corpusFile(relative))) {
            std::fprintf(stderr, "surface: %s is not there\n", corpusFile(relative).toStdString().c_str());
            return 1;
        }
    }

    std::printf("\nA2 THROUGH THE SHIPPED SURFACES -- Stage 4 T14 (D4-16, its guide A-72), Stage 5 T14"
                " (D5-22, its guide A-112) and Stage 6 T15 (D6-24, its guide A-104)\n");
    std::printf("    %d round(s), %d ms of event loop a round, Stage 5 columns %d time(s) each, platform %s\n",
                rounds, pumpMs, repeats, QGuiApplication::platformName().toStdString().c_str());
    std::printf("    A2's configuration, ASKED FOR: %.0f dpi over a %dx%d device-pixel viewport."
                " What each column REACHED is printed with it.\n",
                kA2Dpi, kDeviceViewportWidth, kDeviceViewportHeight);
    std::printf("    font: %s %.1f pt\n", QApplication::font().family().toStdString().c_str(),
                QApplication::font().pointSizeF());
    std::printf("    The graded quantity is the VISIBLE lane's p95 queued latency -- the window a\n"
                "    person is looking at. Speculative (thumbnails) and Prefetch (a background\n"
                "    window, T11's named work) are reported beside it and are NOT graded. A Stage 5\n"
                "    column grades only the tiles delivered WHILE ITS TEXT WORK RAN.\n");

    Column columns[11] = {
        {"viewer",       false, false, false},
        {"+ split",      true,  false, false},
        {"+ pane",       true,  true,  false},
        {"two windows",  true,  true,  true },
        {"+ loupe",      false, false, false, true},
        {"text viewer"},
        {"+ find"},
        {"+ folder search"},
        {"+ comparison"},
        {"annotation viewer", true},
        {"+ pencil",          true},
    };
    const TextWork stage5Work[] = {TextWork::None, TextWork::Find, TextWork::Folder, TextWork::Compare};
    for (int at = 0; at < 4; ++at) {
        columns[5 + at].document = "ordinary/text-900.pdf";
        columns[5 + at].work = stage5Work[at];
        columns[5 + at].stage5 = true;
    }
    columns[9].stage6 = true;
    columns[10].stage6 = true;
    columns[10].pencil = true;
    std::map<std::string, Stage6Run> stage6Runs;

    // THE FOLDER, ONCE: a scratch copy of the corpus tree, as T3's and T9's folder column searched,
    // because a search over the tester's own files would depend on them (`Stage5-TextSearch-Plan.md`
    // §5.6). Copied only when the folder column will run.
    QTemporaryDir folderScratch;
    int folderFiles = 0;
    if (!stage4Only && !stage6Only && (only.isEmpty() || only == QLatin1String("+ folder search"))) {
        if (!folderScratch.isValid()) {
            std::fprintf(stderr, "surface: no scratch directory for the folder\n");
            return 1;
        }
        const QDir corpus(QStringLiteral(RAWPAGE_CORPUS_DIR));
        QDirIterator walk(corpus.absolutePath(), QStringList{QStringLiteral("*.pdf")}, QDir::Files,
                          QDirIterator::Subdirectories);
        while (walk.hasNext()) {
            const QString source = walk.next();
            const QString target = folderScratch.filePath(corpus.relativeFilePath(source));
            QDir().mkpath(QFileInfo(target).absolutePath());
            if (QFile::copy(source, target)) ++folderFiles;
        }
        std::printf("    folder: a scratch copy of the corpus tree, %d PDFs, nested\n", folderFiles);
    }

    double viewerP95 = 0.0;
    double splitP95 = 0.0;
    double paneP95 = 0.0;
    double twoWindowP95 = 0.0;
    double loupeP95 = 0.0;
    std::map<std::string, Pooled> pooled;
    bool measuredAll = true;

    for (const Column& column : columns) {
        if (!only.isEmpty() && only != QLatin1String(column.label)) continue;
        if (stage4Only && (column.stage5 || column.stage6)) continue;
        if (stage5Only && !column.stage5) continue;
        if (stage6Only && !column.stage6) continue;
        const QString document = corpusFile(column.document);
        const int runs = column.stage5 ? repeats : 1;
        for (int run = 0; run < runs; ++run) {

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
            // STAGE 6: one pane drawn in, the other read -- so the two do NOT scroll together, or the pane
            // being drawn in would be scrolled away from its page under the pencil.
            if (column.stage6 && tab->split() != nullptr) tab->split()->setSynchronizedScrolling(false);

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

            // STAGE 5: THE SURFACE IS SHOWN BEFORE THE WINDOW IS GROWN, because the find bar sits above
            // the page and the search pane docks beside it, and either takes space from the viewport --
            // which the grow below must give back for the column to be A2's workload. Shown EMPTY: the
            // work starts after the settle, where the measurement begins.
            ui::FindReplaceBar* findBar = nullptr;
            ui::SearchPane* searchPane = nullptr;
            if (column.work == TextWork::Find) {
                findBar = tab->findBar();
                if (findBar == nullptr) {
                    std::fprintf(stderr, "surface: the tab has no find bar\n");
                    return 1;
                }
                const int closedMinimum = window->minimumSizeHint().width();
                findBar->open();
                pump(60);
                // GUIDE I-10, PRINTED: the bar must not raise the window's minimum, or this column's
                // viewport cannot be A2's -- which is how this bench found it.
                std::printf("    find bar open: its minimum %d px, the window's minimum %d px (%d with it closed)\n",
                            findBar->minimumSizeHint().width(), window->minimumSizeHint().width(), closedMinimum);
            } else if (column.work == TextWork::Folder) {
                searchPane = window->searchPane();
                if (searchPane == nullptr) {
                    std::fprintf(stderr, "surface: the window has no search pane\n");
                    return 1;
                }
                searchPane->showAndFocus();
            }
            pump(60);

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
            //
            // A STAGE 6 COLUMN GROWS UNTIL ITS TWO PANES TOGETHER ARE A2's VIEWPORT, because a split divides ONE
            // window (D4-13, and Stage 4 guide A-72's own correction at T2: two panes are 1920x540 each, the visible
            // pixel count unchanged). Up to six passes, as each pass grows two panes by half of it.
            for (int attempt = 0; attempt < (column.stage6 ? 6 : 2); ++attempt) {
                const ui::PageView* pane = tab->view();
                if (pane == nullptr) break;
                const double haveWidth = pane->controller().visibleRect().size.width;
                double haveHeight = pane->controller().visibleRect().size.height;
                if (column.stage6) {
                    haveHeight = 0.0;
                    for (const ui::PageView* each : tab->split()->views())
                        haveHeight += each->controller().visibleRect().size.height;
                }
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

            // STAGE 6: THE TOP PANE TO PAGE 450 AND THE PENCIL ARMED IN IT, by the product's own commands -- the
            // active pane is the one a command acts on, and it is the primary until a pane is focused.
            ui::PageView* drawPane = nullptr;
            ui::PageView* scrollPane = nullptr;
            if (column.stage6) {
                const std::vector<ui::PageView*>& panes = tab->split()->views();
                if (panes.size() < 2 || tab->view() != panes.front()) {
                    std::fprintf(stderr, "surface: %s -- no split of two panes, the top one active\n", column.label);
                    return 1;
                }
                drawPane = panes.front();
                scrollPane = panes.back();
                std::printf("    the split: the drawing pane %.0fx%.0f device px over the scrolling pane %.0fx%.0f -- ONE"
                            " window divided, so the two together are the viewport\n",
                            drawPane->controller().visibleRect().size.width, drawPane->controller().visibleRect().size.height,
                            scrollPane->controller().visibleRect().size.width, scrollPane->controller().visibleRect().size.height);
                drawPane->goToPage(kStage6Page);
                if (column.pencil) {
                    (void)root.actions().dispatch(core::ActionId::AnnotKeepToolSelected, core::KeyContext::Viewer);
                    (void)root.actions().dispatch(core::ActionId::AnnotPencil, core::KeyContext::Viewer);
                    pump(60);
                    if (drawPane->tools().armedAnnotation() != ui::AnnotationToolKind::Pencil
                        || !drawPane->tools().keepToolSelected()) {
                        std::fprintf(stderr, "surface: %s -- the pencil is not armed and kept in the top pane\n",
                                     column.label);
                        return 1;
                    }
                }
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

            // STAGE 6: ITS OWN MEASUREMENT (above main), then the same teardown as every column.
            if (column.stage6) {
                const Stage6Run measured = measureStage6(root, *tab, *drawPane, *scrollPane, column.pencil,
                                                         stage6Seconds, pumpMs);
                std::printf("\n    %-18s SCROLLING pane VISIBLE p95 %7.3f ms  median %7.3f  n %zu   (both panes p95 %.3f,"
                            " n %zu; page 450's p95 %.3f, n %zu)\n",
                            column.label, percentile(measured.scroll, 0.95), percentile(measured.scroll, 0.50),
                            measured.scroll.size(), percentile(measured.all, 0.95), measured.all.size(),
                            percentile(measured.scope, 0.95), measured.scope.size());
                std::printf("                       %d round(s) of a screen each in the bottom pane; the UI thread's longest"
                            " stall %.1f ms, %d over the frame budget\n",
                            measured.rounds, measured.longestStallMs, measured.stallsOverBudget);
                if (column.pencil) {
                    std::printf("                       strokes %d, commands %d; edit->pixels p95 %.1f ms  max %.1f  n %zu;"
                                " never seen changed %lld; page-450 tiles over an edit unchanged %lld\n",
                                measured.strokes, measured.made, percentile(measured.toPixels, 0.95),
                                percentile(measured.toPixels, 1.0), measured.toPixels.size(), measured.unseen,
                                measured.unchanged);
                    std::printf("                       the release (the stroke made and its command run) p50 %.1f  p95 %.1f"
                                "  max %.1f ms\n",
                                percentile(measured.releaseMs, 0.50), percentile(measured.releaseMs, 0.95),
                                percentile(measured.releaseMs, 1.0));
                }
                if (!measured.refused.empty()) {
                    std::fprintf(stderr, "surface: %s -- %s: no measurement\n", column.label, measured.refused.c_str());
                    measuredAll = false;
                }
                stage6Runs[column.label] = measured;
                root.renderScheduler().setLatencyProbe({});
                window.reset();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                pump(50);
                continue;
            }

            // THE WORK'S OWN COUNTERS from here, before it starts -- the lanes and the cache from where the
            // grading starts, below.
            const core::render::RenderScheduler::Stats beforeWork = root.renderScheduler().stats();
            QElapsedTimer workTimer;
            workTimer.start();

            // STAGE 5: THE WORK STARTS HERE, through the surface, and nothing below queues any of it.
            //   * Find: the query TYPED into the bar's field, a key event a character, so the bar's own
            //     `textEdited` handler and its 200 ms pause start the search as a person's typing would.
            //   * Folder: the pane's folder, subfolders and query set, then its own `search()` -- what
            //     its Search button calls.
            //   * Compare: `MainWindow::compareDocuments`, what the Compare dialog's OK calls, over the
            //     file in the tab against itself at *appearance*, T3's column -- and the main window is
            //     raised after it, so it stays the window in front and its viewport keeps `Visible`.
            QPointer<ui::ComparisonView> comparison;
            int comparisonRefits = 0;
            double fillMs = -1.0;
            std::size_t fillTiles = 0;
            double fillP95 = 0.0;
            double fillWorst = 0.0;
            if (findBar != nullptr) {
                for (const QChar character : kFindQuery) {
                    QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(character));
                    QCoreApplication::sendEvent(findBar->field(), &press);
                    QKeyEvent release(QEvent::KeyRelease, 0, Qt::NoModifier, QString(character));
                    QCoreApplication::sendEvent(findBar->field(), &release);
                }
                if (findBar->field()->text() != kFindQuery) {
                    std::fprintf(stderr, "surface: the find bar's field holds '%s' after typing -- no search\n",
                                 findBar->field()->text().toStdString().c_str());
                    return 1;
                }
            } else if (searchPane != nullptr) {
                searchPane->setFolder(folderScratch.path());
                searchPane->setRecursive(true);
                searchPane->setQuery(kFolderQuery);
                const core::Status started = searchPane->search();
                if (!started) {
                    std::fprintf(stderr, "surface: the folder search refused -- %s\n",
                                 started.error().messageKey.data());
                    return 1;
                }
            } else if (column.work == TextWork::Compare) {
                ui::CompareDialog::Choice choice;
                choice.olderPath = document;
                choice.newerPath = document;
                choice.options = core::text::CompareOptions::fromSettings(root.settings());
                choice.options.sensitivity = comparisonTextOnly ? core::text::CompareSensitivity::TextOnly
                                                                : core::text::CompareSensitivity::Appearance;
                const core::Result<ui::ComparisonView*> opened = window->compareDocuments(choice);
                if (!opened) {
                    std::fprintf(stderr, "surface: the comparison refused -- %s\n",
                                 opened.error().messageKey.data());
                    return 1;
                }
                comparison = opened.value();
                window->raiseToForeground();

                // THE COMPARISON WINDOW'S FIRST FILL, WAITED FOR WITH THE MAIN WINDOW STILL. Its two panes
                // are a document opened in a new window, and they ask for their first page at `Visible`
                // (measured at T14: 60-84 tiles a pane at ~125-157 dpi), every tile ahead of the main
                // viewport's. Every column keeps a document's first paint out of its figure -- the settle
                // above -- so this one waits for it before the scroll and the grading start. FILTERING the
                // fill's tiles out of a running scroll was tried first and is wrong: the probe cannot tell
                // the panes' tiles from the main window's, so the main window's first-round tiles -- the
                // slowest of any column -- went out with them and the column graded below the viewer
                // ALONE. Hiding the window does not stop the panes asking either (65 tiles a pane, hidden).
                // The text work runs from the open, so its first tens of milliseconds are before the grade.
                const auto filled = [&]() -> bool {
                    if (comparison == nullptr) return false;
                    for (const core::text::CompareSide side : {core::text::CompareSide::Base, core::text::CompareSide::Revised}) {
                        const ui::PageView* pane = comparison->view(side);
                        if (pane == nullptr) continue;
                        if (pane->tiles().heldTileCount() == 0 || pane->tiles().pendingTicketCount() > 0) return false;
                    }
                    return true;
                };
                QElapsedTimer fillTimer;
                fillTimer.start();
                while (!filled() && fillTimer.elapsed() < 10000) pump(10);
                if (!filled()) {
                    std::fprintf(stderr, "surface: the comparison's window did not fill in 10 s -- no measurement\n");
                    return 1;
                }
                fillMs = static_cast<double>(fillTimer.elapsed());
                fillTiles = samples.visible.size();
                fillP95 = percentile(samples.visible, 0.95);
                fillWorst = percentile(samples.visible, 1.0);

                // EVERY RE-FIT OF THE COMPARISON'S PAGES from here -- a zoom change is both pages re-rasterized
                // at `Visible`. Guide I-11: the window widened under its growing progress line, five re-fits
                // in one comparison until T14; it is printed so the bench says whether that is still so.
                for (const core::text::CompareSide side : {core::text::CompareSide::Base, core::text::CompareSide::Revised})
                    if (ui::PageView* pane = comparison->view(side))
                        QObject::connect(pane, &ui::PageView::viewChanged, pane,
                                         [&comparisonRefits, zoom = pane->controller().zoom().factor](int, int, double now) mutable {
                                             if (now != zoom) ++comparisonRefits;
                                             zoom = now;
                                         });
            }

            // THE GRADING STARTS HERE.
            samples.clear();
            root.renderCache().resetStats();
            const core::render::RenderScheduler::Stats before = root.renderScheduler().stats();

            // WHETHER THE WORK IS STILL RUNNING, asked of the surface itself. The find bar counts its
            // 200 ms pause as running: the search has been asked for and the index is about to build.
            const auto workRunning = [&]() -> bool {
                switch (column.work) {
                case TextWork::None:    return false;
                case TextWork::Find:    return findBar->isSearchPending() || findBar->isSearching();
                case TextWork::Folder:  return searchPane->isSearching();
                case TextWork::Compare:
                    return comparison != nullptr && comparison->state() != ui::ComparisonView::State::Finished
                           && comparison->state() != ui::ComparisonView::State::Cancelled;
                }
                return false;
            };
            double workMs = -1.0;
            int busyRounds = 0;
            // THE UI THREAD'S LONGEST STALL while the work ran: the gap between two slices of the event
            // loop. `processEvents` returns within its 10 ms unless one handler runs longer, so a gap
            // well past that is the UI thread busy with something -- and a tile is stamped when it is
            // DELIVERED on this thread, so a stall here is latency the engine did not cause.
            QElapsedTimer sliceTimer;
            sliceTimer.start();
            double longestSliceMs = 0.0;
            int slicesOverBudget = 0;
            const auto noteWork = [&] {
                const double slice = static_cast<double>(sliceTimer.nsecsElapsed()) / 1.0e6;
                sliceTimer.restart();
                if (samples.busy) {
                    longestSliceMs = std::max(longestSliceMs, slice);
                    if (slice > kFrameBudgetMs) ++slicesOverBudget;
                }
                const bool running = workRunning();
                if (samples.busy && !running && workMs < 0.0) workMs = static_cast<double>(workTimer.elapsed());
                samples.busy = running;
            };
            samples.busy = column.work != TextWork::None;
            noteWork();

            // THE WORKLOAD: a page-sized scroll a round, in every pane of every window, exactly as a
            // person reading would produce it. Nothing here enqueues a tile. A Stage 5 column goes on
            // past its rounds while its work runs, to `kMaxBusyRounds`; the text viewer runs
            // `kStage5BaselineRounds`, so it is the control for every one of them.
            const int columnRounds =
                column.stage5 && column.work == TextWork::None ? std::max(rounds, kStage5BaselineRounds) : rounds;
            for (int round = 0; round < columnRounds || (samples.busy && round < kMaxBusyRounds); ++round) {
                samples.round = round;
                if (samples.busy) ++busyRounds;
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
                    pump(pumpMs, noteWork);
                }
            }
            const bool workFinished = !samples.busy;

            const core::render::RenderScheduler::Stats after = root.renderScheduler().stats();
            const core::render::CacheStats cache = root.renderCache().stats();

            const double p95 = percentile(samples.visible, 0.95);
            if (column.stage5 && column.work != TextWork::None) {
                std::printf("\n    %-16s run %d  WHILE THE WORK RAN  VISIBLE p95 %7.3f ms   median %7.3f   n %zu"
                            "   (whole column p95 %.3f, n %zu)\n",
                            column.label, run + 1, percentile(samples.visibleWhileBusy, 0.95),
                            percentile(samples.visibleWhileBusy, 0.50), samples.visibleWhileBusy.size(), p95,
                            samples.visible.size());
            } else if (column.stage5) {
                std::printf("\n    %-16s run %d  VISIBLE p95 %7.3f ms   median %7.3f   n %zu\n", column.label,
                            run + 1, p95, percentile(samples.visible, 0.50), samples.visible.size());
            } else {
                std::printf("\n    %-14s VISIBLE  p95 %7.3f ms   median %7.3f   n %zu\n", column.label, p95,
                            percentile(samples.visible, 0.50), samples.visible.size());
            }
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

            // STAGE 5: WHAT THE WORK DID, and the proof that it did it. A column whose index was already
            // built, whose folder search opened nothing or whose comparison read no page measured a
            // viewport with nothing behind it and would be graded as if it had -- so each says what it
            // asked of the engine, and a column that asked nothing fails the run (convention 7).
            if (column.stage5 && column.work != TextWork::None) {
                // From before the work started -- a comparison asks for its text the moment it opens.
                const long long text = after.textEnqueued - beforeWork.textEnqueued;
                const long long tasks = after.taskEnqueued - beforeWork.taskEnqueued;
                const long long bypass = after.enqueuedBypassingCache - beforeWork.enqueuedBypassingCache;
                std::printf("                   the work: %s in %.0f ms, over %d round(s) of scrolling%s\n",
                            workFinished ? "FINISHED" : "STILL RUNNING", workFinished ? workMs : static_cast<double>(workTimer.elapsed()),
                            busyRounds, workFinished ? "" : " -- the cap was reached, so this run is truncated");
                if (column.work == TextWork::Compare)
                    std::printf("                   the comparison window's FIRST FILL, before the scroll (not graded): %.0f ms,"
                                " %zu visible tile(s), p95 %.1f ms, worst %.1f ms\n",
                                fillMs, fillTiles, fillP95, fillWorst);
                if (column.work == TextWork::Compare)
                    std::printf("                   the comparison's pages re-fitted %d time(s) after their first fill (guide I-11)\n",
                                comparisonRefits);
                std::printf("                   while it ran: worst visible tile %.1f ms; the UI thread's longest stall %.1f ms,"
                            " %d stall(s) over the frame budget\n",
                            samples.visibleWhileBusy.empty() ? 0.0
                                : *std::max_element(samples.visibleWhileBusy.begin(), samples.visibleWhileBusy.end()),
                            longestSliceMs, slicesOverBudget);
                std::printf("                   background: text jobs %lld   engine tasks %lld   rasters bypassing the cache %lld"
                            "   (lane total %lld)\n",
                            text, tasks, bypass, after.enqueuedBackground - beforeWork.enqueuedBackground);
                // THE SCHEDULER'S OWN INVERSION COUNT (S5 guide I-12): a job picked while a more urgent one
                // was pending. Strict priority makes it zero unless an exclusive task held a document back.
                std::printf("                   jobs started while a more urgent one waited: %lld\n",
                            after.startedWhileMoreUrgentWaited - beforeWork.startedWhileMoreUrgentWaited);
                std::printf("                   cache insertions %lld against %lld foreground tiles delivered\n",
                            cache.insertions,
                            static_cast<long long>(samples.visible.size() + samples.prefetch.size() + samples.speculative.size()));
                bool did = false;
                switch (column.work) {
                case TextWork::Find:
                    std::printf("                   find bar: %zu hit(s), %d search(es) finished -- %s\n",
                                findBar->hits().size(), findBar->searchesFinished(),
                                findBar->statusText().toStdString().c_str());
                    did = text > 0 && findBar->searchesFinished() > 0 && findBar->hits().size() == 1;
                    break;
                case TextWork::Folder:
                    std::printf("                   search pane: %zu row(s) listed -- %s\n", searchPane->listedRows().size(),
                                searchPane->statusText().toStdString().c_str());
                    did = tasks > 0 && text > 0 && !searchPane->listedRows().empty();
                    break;
                case TextWork::Compare:
                    std::printf("                   comparison: %s, %zu difference(s) -- %s\n",
                                comparison != nullptr && comparison->isFinished() ? "finished" : "not finished",
                                comparison != nullptr ? comparison->differences().size() : std::size_t{0},
                                comparison != nullptr ? comparison->statusText().toStdString().c_str() : "(closed)");
                    did = text > 0 && (bypass > 0 || comparisonTextOnly) && comparison != nullptr
                          && comparison->isFinished();
                    break;
                case TextWork::None:
                    break;
                }
                if (!did || samples.visibleWhileBusy.empty()) {
                    std::fprintf(stderr, "surface: %s run %d -- the work did not happen as asked, or no visible"
                                 " tile arrived while it ran: no measurement\n", column.label, run + 1);
                    measuredAll = false;
                }
            }
            if (column.stage5) {
                Pooled& into = pooled[column.label];
                const std::vector<double>& graded =
                    column.work == TextWork::None ? samples.visible : samples.visibleWhileBusy;
                into.graded.insert(into.graded.end(), graded.begin(), graded.end());
                into.all.insert(into.all.end(), samples.visible.begin(), samples.visible.end());
                into.perRunP95.push_back(percentile(graded, 0.95));
                if (column.work != TextWork::None) into.workMs.push_back(workFinished ? workMs : -1.0);
                into.busyRounds += busyRounds;
                if (column.work != TextWork::None) into.busyRoundsPerRun.push_back(busyRounds);
                else into.byRound.insert(into.byRound.end(), samples.visibleByRound.begin(), samples.visibleByRound.end());
            }

            if (std::strcmp(column.label, "viewer") == 0)            viewerP95 = p95;
            else if (std::strcmp(column.label, "+ split") == 0)      splitP95 = p95;
            else if (std::strcmp(column.label, "+ pane") == 0)       paneP95 = p95;
            else if (std::strcmp(column.label, "two windows") == 0)  twoWindowP95 = p95;
            else if (std::strcmp(column.label, "+ loupe") == 0)      loupeP95 = p95;

            // The probe outlives nothing: the scheduler is destroyed with the root below, but the
            // lambda holds a reference to a `Samples` on this stack frame, so it is removed first.
            root.renderScheduler().setLatencyProbe({});
            if (comparison != nullptr) comparison->close();
            if (second != nullptr) second->close();
            window.reset();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            pump(50);
        }
    }

    if (only.isEmpty() && !stage5Only && !stage6Only) {
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

    // A2 AT STAGE 5, THE PRODUCT'S FIGURE: each column's busy samples pooled over its repetitions,
    // beside T3's bench figure for the same workload -- and never instead of it.
    if (!pooled.empty()) {
        struct Row {
            const char* label;
            const char* text;
            const char* t3;
        };
        const Row rows[] = {
            {"text viewer",     "text-900.pdf, nothing behind it .....................", "T3's bench: 13.02-13.07"},
            {"+ find",          "+ the find bar's FIRST search, the index built ......", "T3's index build: 14.05-14.26"},
            {"+ folder search", "+ the search pane over the corpus copy ..............", "T3: 17.68-17.93, T9: 19.24-19.41"},
            {"+ comparison",    "+ a comparison at appearance, after its first fill ..", "T3: 13.67-13.76"},
        };
        std::printf("\n    A2 AT STAGE 5, THE PRODUCT'S FIGURE (guide A-112), pooled over each column's runs --"
                    " a work column\n    graded on the tiles delivered WHILE ITS WORK RAN:\n");
        for (const Row& row : rows) {
            const auto found = pooled.find(row.label);
            if (found == pooled.end()) continue;
            const Pooled& p = found->second;
            std::string runs;
            for (const double each : p.perRunP95) {
                char buffer[32];
                std::snprintf(buffer, sizeof buffer, "%s%.2f", runs.empty() ? "" : " / ", each);
                runs += buffer;
            }
            std::printf("      %s %7.3f ms  n %5zu   runs %s   (%s)\n", row.text, percentile(p.graded, 0.95),
                        p.graded.size(), runs.c_str(), row.t3);
            if (!p.workMs.empty()) {
                std::string took;
                for (const double each : p.workMs) {
                    char buffer[32];
                    std::snprintf(buffer, sizeof buffer, "%s%s", took.empty() ? "" : " / ",
                                  each < 0.0 ? "unfinished" : QByteArray::number(each / 1000.0, 'f', 1).append(" s").constData());
                    took += buffer;
                }
                std::printf("        the work took %s; %d round(s) scrolled while it ran; whole column p95 %.3f ms\n",
                            took.c_str(), p.busyRounds, percentile(p.all, 0.95));
                // THE CONTROL: the viewer alone over the SAME first rounds as each run's work, pooled the same
                // way -- so the difference is the work's, not the window's length.
                const auto baseline = pooled.find("text viewer");
                if (baseline != pooled.end() && !baseline->second.byRound.empty()) {
                    std::vector<double> matched;
                    for (const int k : p.busyRoundsPerRun)
                        for (const auto& [round, ms] : baseline->second.byRound)
                            if (round < k) matched.push_back(ms);
                    std::printf("        the viewer ALONE over the same rounds ............ %7.3f ms  n %5zu   -> the work adds %+.3f ms\n",
                                percentile(matched, 0.95), matched.size(),
                                percentile(p.graded, 0.95) - percentile(matched, 0.95));
                }
            }
        }
        std::printf("      frame budget ......................................... %7.3f ms\n", kFrameBudgetMs);
        std::printf("\n    Graded on Stage5-TextSearch-Testing-Guide.md 3.1 and recorded in Tech-Stack-Plan.md 3.3"
                    " beside T3's\n    figure, never instead of it. This bench never fails on speed.\n\n");
    }

    // A2 AT STAGE 6, THE PRODUCT'S FIGURE (guide A-104), graded on the guide's §3.1 beside T4's bench figure.
    const auto viewerRun = stage6Runs.find("annotation viewer");
    const auto pencilRun = stage6Runs.find("+ pencil");
    if (pencilRun != stage6Runs.end()) {
        const Stage6Run& p = pencilRun->second;
        const double scrollP95 = percentile(p.scroll, 0.95);
        const double toPixelsP95 = percentile(p.toPixels, 0.95);
        const double toPixelsMax = percentile(p.toPixels, 1.0);
        std::printf("\n    A2 AT STAGE 6, THE PRODUCT'S FIGURE (guide A-104) -- pages-900.pdf, a stroke every %d ms on page 450"
                    " for %d s:\n", kStage6PeriodMs, stage6Seconds);
        if (viewerRun != stage6Runs.end())
            std::printf("      the scrolling pane, nothing drawn ..................... %7.3f ms\n",
                        percentile(viewerRun->second.scroll, 0.95));
        std::printf("      the scrolling pane, the pencil drawing beside it ....... %7.3f ms  %s (frame budget %.3f)\n",
                    scrollP95, scrollP95 < kFrameBudgetMs ? "inside" : "OUTSIDE", kFrameBudgetMs);
        std::printf("      both panes ............................................. %7.3f ms\n", percentile(p.all, 0.95));
        std::printf("      edit -> pixels p95 ..................................... %7.1f ms  %s (<= 100)\n", toPixelsP95,
                    toPixelsP95 <= 100.0 ? "inside" : "OUTSIDE");
        std::printf("      edit -> pixels max ..................................... %7.1f ms  %s (<= 250)%s\n", toPixelsMax,
                    toPixelsMax <= 250.0 ? "inside" : "OUTSIDE",
                    p.unseen > 0 ? "   -- SOME EDITS NEVER REACHED THE PIXELS" : "");
        std::printf("\n    Graded on Stage6-Annotations-Testing-Guide.md 3.1 and recorded in Tech-Stack-Plan.md 3.3 beside"
                    " T4's\n    bench figure, never instead of it. This bench never fails on speed.\n\n");
    }

    return measuredAll ? 0 : 1;
}
