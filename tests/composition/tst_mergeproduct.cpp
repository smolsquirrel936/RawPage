// tst_mergeproduct -- Stage 7 T10 (`Stage7-Assembly-Plan.md` §6, D7-16): a merge through the PRODUCT --
// `app::CompositionRoot` and `ui::MainWindow` as `main.cpp` builds them, the shipped backend and `core::MergeRunner`'s
// thread. Guide case A-72, §3.5's four bounds (`Stage7-Assembly-Testing-Guide.md` §3.5, §5.12).
//
// THE WORKLOAD is §3.5's: `merge-a.pdf`, `merge-b.pdf` and `merge-c.pdf` ten times each, with `pages-900.pdf` as the
// thirty-first input, at the merge's defaults (a bookmark per file, colliding fields renamed). Measured here:
//   * WALL TIME, from the dispatch the dialog's answer starts to the untitled tab in front (≤ 30 s);
//   * the LIVE-HEAP PEAK (convention 23: busy heap blocks, never private bytes), sampled on this thread while the
//     merge runs and once the tab is open, less what was live before (≤ 1 GiB);
//   * CANCEL TO STOPPED, a second run cancelled once it is under way (≤ 1 s), no tab and no file left behind;
//   * THE UI THREAD: a 10 ms timer's largest gap while the merge runs (≤ 100 ms). The step after it -- the merged
//     document OPENED by the engine and its tab built, on this thread by design (D7-30(b)'s `openBytes`) -- is printed
//     beside it as its own figure, because §3.5 bounds the thread DURING the merge.
// The result is graded as A-47 and A-66 grade: opened with no repair, one bookmark per input landing on that input's
// first page by the identity PDFium extracts there.
//
// WHAT IT DOES NOT COVER (convention 19): no dialog is shown -- the choice is the dialog's twin's -- so the dialog with
// thirty files (M-03's *"without lag"*) is the walker's; a run on THIS machine, release build, `[run-serial]`
// (convention 25).

#include <catch2/catch_test_macros.hpp>

#include "AnnotProduct.h"
#include "core/cmd/ActionRegistry.h"
#include "core/doc/Outline.h"
#include "core/text/PageTextModel.h"
#include "tests/bench/HeapCensus.h"
#include "ui/dialogs/MergeDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <memory>

using namespace rawpage;
using annot_product::Product;
using core::ActionId;

namespace {

struct Answers {
    ~Answers() { ui::MergeDialog::setAnswerForTesting({}); }
};

QStringList filesUnder(const QString& root)
{
    QStringList out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) out << QDir(root).relativeFilePath(it.next());
    out.sort();
    return out;
}

std::vector<core::MergeInput> workload()
{
    std::vector<core::MergeInput> inputs;
    for (int round = 0; round < 10; ++round) {
        for (const char* name : {"assembly/merge-a.pdf", "assembly/merge-b.pdf", "assembly/merge-c.pdf"})
            inputs.push_back(core::MergeInput{test_oracle::corpusPath(name), {}});
    }
    inputs.push_back(core::MergeInput{test_oracle::corpusPath("ordinary/pages-900.pdf"), {}});
    return inputs;
}

// The UI thread's gaps: a 10 ms timer's ticks, and the largest interval between two.
struct Ticks {
    QTimer timer;
    QElapsedTimer clock;
    qint64 last = 0;
    qint64 largest = 0;
    explicit Ticks()
    {
        timer.setInterval(10);
        QObject::connect(&timer, &QTimer::timeout, [this] {
            const qint64 now = clock.elapsed();
            largest = std::max(largest, now - last);
            last = now;
        });
        clock.start();
        timer.start();
    }
};

} // namespace

TEST_CASE("merge.scale a 30-way merge, inside its bound", "[merge][merge.scale][A-72][run-serial]")
{
    Answers answers;
    Product product;
    const std::vector<core::MergeInput> inputs = workload();
    ui::MergeDialog::setAnswerForTesting([&inputs](const core::MergeOptions& defaults) {
        return std::optional<ui::MergeDialog::Choice>(ui::MergeDialog::Choice{inputs, defaults});
    });
    const QStringList filesBefore = filesUnder(product.scratch.path());
    const std::size_t heapBefore = bench::heapCensus().busyBytes;

    // ---- The merge, to its tab --------------------------------------------------------------------------------------
    std::size_t peak = heapBefore;
    QElapsedTimer wall;
    wall.start();
    qint64 duringMerge = 0;
    qint64 finishing = 0;
    {
        Ticks ticks;
        REQUIRE(product.root->services().actions.dispatch(ActionId::MergeDocuments, core::KeyContext::Global));
        REQUIRE(product.window->isMerging());
        ticks.last = ticks.clock.elapsed();
        int samples = 0;
        while (product.window->isMerging() && wall.elapsed() < 120000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if (product.window->isMerging()) {
                duringMerge = std::max(duringMerge, ticks.largest);
                if (++samples % 20 == 0) peak = std::max(peak, bench::heapCensus().busyBytes);
            }
            QThread::msleep(1);
        }
        // The step after the merge -- the done callback opening the document -- ran inside the last processEvents.
        finishing = ticks.clock.elapsed() - ticks.last;
    }
    QCoreApplication::processEvents();
    const qint64 seconds = wall.elapsed();
    peak = std::max(peak, bench::heapCensus().busyBytes);
    REQUIRE_FALSE(product.window->isMerging());
    REQUIRE(product.window->documentCount() == 1);
    ui::DocumentWindow* merged = product.window->currentDocument();
    REQUIRE(merged != nullptr);
    CHECK(merged->isAssembly());

    // ---- Graded as A-47 and A-66 grade --------------------------------------------------------------------------------
    core::IDocumentBackend& backend = product.root->services().backend;
    CHECK(backend.repairReport(merged->handle())->isEmpty());
    REQUIRE(merged->pageCount() == 10 * (3 + 2 + 4) + 900);
    const core::Outline* outline = backend.outline(merged->handle());
    REQUIRE(outline != nullptr);
    REQUIRE(outline->roots().size() == inputs.size());
    static const QRegularExpression identity(QStringLiteral("(?<![A-Za-z0-9-])[A-Z][A-Z0-9]*-[0-9]{4}(?![A-Za-z0-9-])"));
    int first = 0;
    int landed = 0;
    for (std::size_t k = 0; k < inputs.size(); ++k) {
        const core::OutlineItem& item = outline->roots()[k];
        CHECK(item.title == QFileInfo(inputs[k].path).completeBaseName());
        CHECK(item.destination.pageIndex == first);
        if (k + 1 < inputs.size()) {   // the identity pages: each input's first page shows its first identity
            core::text::PageTextModel model;
            REQUIRE(backend.textExtractor(merged->handle())->extractPage(merged->handle(), item.destination.pageIndex, model));
            const QString want = QStringLiteral("M%1-0001").arg(QChar(QLatin1Char("ABC"[k % 3])));
            CHECK(identity.match(model.text()).captured(0) == want);
            ++landed;
        }
        first += k + 1 < inputs.size() ? std::vector<int>{3, 2, 4}[k % 3] : 900;
    }

    // ---- Cancel, once under way -------------------------------------------------------------------------------------
    REQUIRE(product.root->services().actions.dispatch(ActionId::MergeDocuments, core::KeyContext::Global));
    QElapsedTimer settle;
    settle.start();
    // Under way: a few inputs in, far short of the end (the whole run above took `seconds`).
    while (settle.elapsed() < 30) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    REQUIRE(product.window->isMerging());
    QElapsedTimer stop;
    stop.start();
    product.window->cancelMerge();
    while (product.window->isMerging() && stop.elapsed() < 10000) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    const qint64 cancelled = stop.elapsed();
    QCoreApplication::processEvents();
    CHECK(product.window->documentCount() == 1);   // no second tab
    CHECK(filesUnder(product.scratch.path()) == filesBefore);   // and nothing written

    const double peakMB = (peak > heapBefore ? peak - heapBefore : 0) / 1048576.0;
    std::printf("A-72: %d inputs, %d pages -- wall %.2f s, live-heap peak +%.1f MB (before %.1f MB), UI gap during the merge %lld ms, "
                "the open after it %lld ms, cancel to stopped %lld ms; %d bookmarks landed by identity\n",
                static_cast<int>(inputs.size()), merged->pageCount(), seconds / 1000.0, peakMB, heapBefore / 1048576.0,
                static_cast<long long>(duringMerge), static_cast<long long>(finishing), static_cast<long long>(cancelled), landed);
    std::fflush(stdout);
    CHECK(seconds <= 30000);
    CHECK(peakMB <= 1024.0);
    CHECK(duringMerge <= 100);
    CHECK(cancelled <= 1000);
}
