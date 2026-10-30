// tst_composegrowth -- Stage 7 T4 (`Stage7-Assembly-Plan.md` §6, Q4, `DW-4`'s second cause), through the SHIPPED
// backend: guide cases A-35, A-36 and A-37 (`Stage7-Assembly-Testing-Guide.md` §5.6).
//
// The journal is shown as `ui/shell/DocumentWindow` shows it -- `showEdits` after every operation, `settleEdits`
// before the next -- so the verify really runs on a thread of its own and the engine really waits behind its gate.
//
// A-35 IS THE MECHANISM, NOT §3.7's VERDICT: the user chose on 2026-10-31 to leave §3.7 open until T5 (inserts from
// `text-900.pdf`) and T11 (a bookmark rename) can measure its real workload. What is graded here does not depend on
// the machine: the objects a one-page move or a rotation BUILDS -- written anew rather than written again as the last
// compose wrote them -- are the same after 3,000 unsaved objects as after none. The times are printed beside them;
// the unsaved update is built of highlights, a stand-in for T5's copied pages.
//
// WHAT THIS DOES NOT COVER (convention 19): the window -- its settle posted when a verify finishes, its status line
// for a refusal -- which `tests/composition/tst_assemblyshown.cpp` composes; and the memo's correctness, which is
// `tests/unit/tst_composememo.cpp`'s (A-128).

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Annotation.h"
#include "core/doc/Document.h"
#include "core/doc/Outline.h"
#include "core/doc/SaveCoordinator.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/ops/RevisionWriter.h"
#include "platform/win/WinDurableFiles.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace rawpage;
using namespace rawpage::test_oracle;
using core::render::RenderRequest;

namespace {

struct Live {
    QTemporaryDir scratch;
    pdf::DocumentBackendImpl backend;
    platform::win::WinDurableFiles durableFiles;
    QString path;
    core::DocumentHandle handle;
    std::unique_ptr<core::CommandJournal> journal;
    std::unique_ptr<core::SaveCoordinator> coordinator;
    std::function<void()> onShow;   // A-37: told of each show, on this thread
};

std::unique_ptr<Live> openLive(const std::string& relative)
{
    auto live = std::make_unique<Live>();
    REQUIRE(live->scratch.isValid());
    live->path = live->scratch.filePath(QStringLiteral("document.pdf"));
    REQUIRE(QFile::copy(corpusPath(relative), live->path));
    QFile::setPermissions(live->path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    auto handle = live->backend.open(live->path, nullptr);
    REQUIRE(handle);
    live->handle = handle.value();
    core::Document document;
    REQUIRE(document.hydrate(live->backend, live->handle));
    live->journal = std::make_unique<core::CommandJournal>(std::move(document), live->backend.mutationSink(live->handle));
    Live* self = live.get();
    live->journal->setShowHandler(
        [self](const core::Scope&) {
            if (self->onShow) self->onShow();
            return self->backend.showEdits(self->handle, {});
        },
        [self] { return self->backend.settleEdits(self->handle); });
    live->coordinator = std::make_unique<core::SaveCoordinator>(live->backend, &live->durableFiles);
    return live;
}

core::Annotation highlightOn(const core::IDocumentBackend& backend, core::DocumentHandle handle, int page, int k)
{
    const core::Page* entry = backend.pages(handle)->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfRect box = entry->cropBox();
    const double left = box.left() + 20.0 + (k % 10) * 5.0;
    const double top = box.bottom() + 100.0 + (k % 30) * 20.0;
    core::Annotation a = core::newAnnotation(core::AnnotationSubtype::Highlight, page, QStringLiteral("A-35"), 1791504000);
    a.quadPoints = {left, top, left + 100.0, top, left, top - 12.0, left + 100.0, top - 12.0};
    a.rect = *core::boundsOfPoints(a.quadPoints);
    a.color = {1, 1, 0};
    a.opacity = 0.5;
    return a;
}

// A whole page at `scale` as the engine draws it now -- no unsaved rotation is in play where this is used.
RenderRequest wholePage(const core::IDocumentBackend& backend, core::DocumentHandle handle, int index, double scale,
                        core::Revision revision)
{
    RenderRequest request;
    request.document = handle;
    request.pageIndex = index;
    request.deviceScale = scale;
    request.revision = revision;
    request.flags = core::render::RenderFlags::Annotations;
    const core::Page* page = backend.pages(handle)->at(index);
    REQUIRE(page != nullptr);
    const core::PdfSize size = page->displaySize();
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(size.width * scale)),
                                          static_cast<int>(std::ceil(size.height * scale))};
    return request;
}

// What the live engine shows at `page`, as text (rule 7).
QString extractedText(core::IDocumentBackend& backend, core::DocumentHandle handle, int page)
{
    core::ITextExtractor* extractor = backend.textExtractor(handle);
    REQUIRE(extractor != nullptr);
    core::text::PageTextModel model;
    REQUIRE(extractor->extractPage(handle, page, model));
    return model.text();
}

// Compared as digests: a failed CHECK on two `QByteArray`s crashes Catch2's reporter (`CLAUDE.md` -> Traps).
std::string digest(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

// Every outline item's page, depth first -- what the bookmarks pane opens.
void outlinePages(const std::vector<core::OutlineItem>& items, std::vector<int>& into)
{
    for (const core::OutlineItem& item : items) {
        into.push_back(item.destination.pageIndex);
        outlinePages(item.children, into);
    }
}

double percentile(std::vector<double> values, double p)
{
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1))];
}

} // namespace

// ==========================================================================================
// A-35 -- what a page command builds does not grow with the unsaved update
// ==========================================================================================

TEST_CASE("a page command after thousands of unsaved objects builds only what it changed", "[compose][A-35][run-serial]")
{
    struct Size {
        std::size_t objects = 0;     // the unsaved update's, measured
        std::size_t moveBuilt = 0;   // objects a one-page move built anew, the most of ten
        std::size_t rotateBuilt = 0;
        std::vector<double> moveMs, rotateMs, verifyMs;
    };
    std::vector<Size> sizes;
    // 0, then about 300 and 3,000 objects: each highlight is its dictionary and its appearance, and each page it lands
    // on one page dictionary more -- one highlight a page, from page 101 on.
    for (const int highlights : {0, 100, 1100}) {
        auto live = openLive("ordinary/text-900.pdf");
        core::CommandJournal& journal = *live->journal;
        if (highlights > 0) {
            std::vector<core::Annotation> all;
            for (int k = 0; k < highlights; ++k) all.push_back(highlightOn(live->backend, live->handle, 100 + k % 800, k));
            REQUIRE(journal.run(std::make_unique<core::AddAnnotationCommand>(std::move(all))));
            REQUIRE(journal.settle());
        }
        Size size;
        size.objects = live->backend.reloadRecord(live->handle)->lastObjectsWritten;
        for (int i = 0; i < 10; ++i) {
            // Settled first and untimed: a person's next command comes after the verify of the last (§3.7).
            REQUIRE(journal.settle());
            QElapsedTimer timer;
            timer.start();
            REQUIRE(journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{(i % 2) ? 7 : 5}, (i % 2) ? 5 : 7)));
            size.moveMs.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
            auto record = live->backend.reloadRecord(live->handle);
            size.moveBuilt = std::max(size.moveBuilt, record->lastObjectsWritten - record->lastObjectsReused);
            REQUIRE(journal.settle());
            size.verifyMs.push_back(std::chrono::duration<double, std::milli>(live->backend.reloadRecord(live->handle)->lastVerify).count());
            timer.restart();
            REQUIRE(journal.run(std::make_unique<core::RotatePageCommand>(1, 90)));
            size.rotateMs.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
            record = live->backend.reloadRecord(live->handle);
            size.rotateBuilt = std::max(size.rotateBuilt, record->lastObjectsWritten - record->lastObjectsReused);
        }
        REQUIRE(journal.settle());
        sizes.push_back(std::move(size));
        live->backend.close(live->handle);
    }

    for (const Size& size : sizes) {
        std::printf("A-35 [reported, not graded -- S7 guide §3.7 at T5/T11]: %zu unsaved objects: one-page move builds "
                    "%zu, journal.run p95 %.2f ms max %.2f | rotation builds %zu, journal.run p95 %.2f ms max %.2f | "
                    "verify (off the thread) p95 %.2f ms\n",
                    size.objects, size.moveBuilt, percentile(size.moveMs, 0.95), percentile(size.moveMs, 1.0),
                    size.rotateBuilt, percentile(size.rotateMs, 0.95), percentile(size.rotateMs, 1.0),
                    percentile(size.verifyMs, 0.95));
    }
    REQUIRE(sizes.size() == 3);
    CHECK(sizes[1].objects >= 300);
    CHECK(sizes[2].objects >= 3000);
    // THE MECHANISM: what the command builds is what it changed, whatever else is unsaved.
    CHECK(sizes[1].moveBuilt == sizes[0].moveBuilt);
    CHECK(sizes[2].moveBuilt == sizes[0].moveBuilt);
    CHECK(sizes[1].rotateBuilt == sizes[0].rotateBuilt);
    CHECK(sizes[2].rotateBuilt == sizes[0].rotateBuilt);
    CHECK(sizes[0].moveBuilt <= 4);
    CHECK(sizes[0].rotateBuilt <= 4);
}

// ==========================================================================================
// A-36 -- a composition Save could not write is still refused
// ==========================================================================================

TEST_CASE("a page command its verify refuses is taken back and the engine never draws it", "[compose][A-36]")
{
    auto live = openLive("ordinary/pages-900.pdf");
    core::CommandJournal& journal = *live->journal;
    const core::RasterImage first = live->backend.rasterizer(live->handle)->raster(
        wholePage(live->backend, live->handle, 0, 0.25, journal.revision())).value();
    const QByteArray modelBefore = journal.document().modelHash();
    const auto recordBefore = live->backend.reloadRecord(live->handle);
    REQUIRE(recordBefore);
    std::atomic<int> settledCalls{0};
    live->backend.setEditsSettled(live->handle, [&settledCalls] { ++settledCalls; });

    // THE FAULT: the update's page tree renamed (`/Kids` to `/Kidz`), which every reader but the verify would take.
    const std::size_t original = static_cast<std::size_t>(QFile(live->path).size());
    pdf::ops::RevisionWriter::setComposeFaultForTesting([original](QByteArray& bytes) {
        const qsizetype at = bytes.indexOf("/Kids", static_cast<qsizetype>(original));
        if (at >= 0) bytes[at + 4] = 'z';
    });
    REQUIRE(journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 899)));
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});
    // THE RUN RETURNED, and the backend's tree already says the new order -- what the screen asks against.
    CHECK(journal.document().pages().count() == 900);
    // BETWEEN THE REFUSAL AND THE SETTLE -- the verify over, the journal not yet told -- a job finds NO ENGINE rather
    // than draw the old order's page at the index the refused move gave page 1.
    for (int waited = 0; settledCalls.load() == 0 && waited < 1000; ++waited)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    REQUIRE(settledCalls.load() == 1);
    CHECK_FALSE(live->backend.rasterizer(live->handle)->raster(wholePage(live->backend, live->handle, 899, 0.25, journal.revision())));

    const core::Status refused = journal.settle();
    REQUIRE_FALSE(refused);
    CHECK(std::string(refused.error().messageKey.data()) == "error.edit.notShown");
    CHECK(refused.error().context.startsWith(QStringLiteral("error.save.verifyFailed")));
    // TAKEN BACK: the model, the history, and the engine's tree and its record as they were -- nothing swapped in.
    CHECK(journal.document().modelHash() == modelBefore);
    CHECK_FALSE(journal.canUndo());
    const auto recordAfter = live->backend.reloadRecord(live->handle);
    REQUIRE(recordAfter);
    CHECK(recordAfter->reloads == recordBefore->reloads);
    CHECK(recordAfter->pendingGeneration == recordAfter->shownGeneration);
    // ...and page 1 is page 1 again, by what the engine shows there.
    CHECK(extractedText(live->backend, live->handle, 0).contains(QStringLiteral("page 1 of 900")));
    const core::RasterImage again = live->backend.rasterizer(live->handle)->raster(
        wholePage(live->backend, live->handle, 0, 0.25, journal.revision())).value();
    CHECK(digest(again.pixels) == digest(first.pixels));

    // THE CONTROL: the same move with no fault is made, and drawn.
    REQUIRE(journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 899)));
    REQUIRE(journal.settle());
    CHECK(extractedText(live->backend, live->handle, 899).contains(QStringLiteral("page 1 of 900")));
    live->backend.setEditsSettled(live->handle, {});
}

TEST_CASE("a page command its verify refuses puts the outline back on the pages it names", "[compose][A-36]")
{
    // What the refused show had already re-pointed -- the bookmarks, by page -- is put back with the tree.
    auto live = openLive("assembly/refs-every-kind.pdf");
    core::CommandJournal& journal = *live->journal;
    std::vector<int> before;
    outlinePages(live->backend.outline(live->handle)->roots(), before);
    REQUIRE_FALSE(before.empty());
    const int last = journal.document().pages().count() - 1;

    pdf::ops::RevisionWriter::setComposeFaultForTesting([](QByteArray& bytes) {
        const qsizetype at = bytes.lastIndexOf("/Kids");
        if (at >= 0) bytes[at + 4] = 'z';
    });
    REQUIRE(journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, last)));
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});
    std::vector<int> moved;
    outlinePages(live->backend.outline(live->handle)->roots(), moved);
    CHECK(moved != before);   // the run returned, and the readers already follow the new order

    REQUIRE_FALSE(journal.settle());
    std::vector<int> after;
    outlinePages(live->backend.outline(live->handle)->roots(), after);
    CHECK(after == before);
}

// ==========================================================================================
// A-37 -- commands, a tile and a save wait for a verify in flight
// ==========================================================================================

TEST_CASE("commands a tile and a save wait for the verify in flight", "[compose][A-37][run-serial]")
{
    auto live = openLive("ordinary/pages-900.pdf");
    core::CommandJournal& journal = *live->journal;
    using Clock = std::chrono::steady_clock;

    // A SLOW VERIFY, and a log: the seam corrupts nothing -- it waits, and keeps the file it was handed.
    std::mutex mutex;
    std::vector<std::string> events;
    QByteArray thirdVerified;   // the last SHOW's -- the save's own compose passes the seam too
    int verifies = 0;
    Clock::time_point firstVerifyEnded{};
    const auto log = [&](const std::string& event) {
        const std::lock_guard<std::mutex> guard(mutex);
        events.push_back(event);
    };
    live->onShow = [&] { log("show"); };
    pdf::ops::RevisionWriter::setComposeFaultForTesting([&](QByteArray& whole) {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const std::lock_guard<std::mutex> guard(mutex);
        events.push_back("verified");
        ++verifies;
        if (verifies == 1) firstVerifyEnded = Clock::now();
        if (verifies == 3) thirdVerified = whole;
    });

    // The tiles through the shipped scheduler, told of every command as `DocumentWindow` tells it; the rasterizer
    // stamps when it has drawn.
    struct Stamped final : core::IPageRasterizer {
        Stamped(core::IPageRasterizer& inner, std::mutex& mutex, Clock::time_point& at) : inner_(inner), mutex_(mutex), at_(at) {}
        core::Result<core::RasterImage> raster(const RenderRequest& request) override
        {
            auto image = inner_.raster(request);
            if (request.tile.width > 1) {
                const std::lock_guard<std::mutex> guard(mutex_);
                at_ = Clock::now();
            }
            return image;
        }
        core::IPageRasterizer& inner_;
        std::mutex& mutex_;
        Clock::time_point& at_;
    };
    Clock::time_point drawn{};
    std::optional<core::RasterImage> moved;
    core::render::RenderCache cache;
    core::render::RenderScheduler scheduler(
        [&]() -> std::unique_ptr<core::IPageRasterizer> {
            return std::make_unique<Stamped>(*live->backend.rasterizer(live->handle), mutex, drawn);
        },
        &cache);
    journal.subscribeMutated([&](const core::Scope& scope) {
        scheduler.documentChanged(live->handle, scope, journal.revision(), live->backend.pages(live->handle));
    });
    const auto token = scheduler.subscribeReady([&](const RenderRequest& request, const core::RasterImage& image) {
        if (request.tile.width > 1 && request.pageIndex == 10) moved = image;   // not the warm-up's one pixel
    });

    // 1. A MOVE RETURNS while its verify runs.
    const core::RasterImage pageOne = live->backend.rasterizer(live->handle)->raster(
        wholePage(live->backend, live->handle, 0, 0.25, journal.revision())).value();
    const auto issued = Clock::now();
    REQUIRE(journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 10)));
    const auto returned = Clock::now();
    CHECK(std::chrono::duration<double, std::milli>(returned - issued).count() < 100.0);

    // 2. A TILE OF THE MOVED PAGE, asked for now through the shipped scheduler, waits on the render thread for the
    // verify and then shows THAT page -- never the page the old order had at index 10.
    REQUIRE(scheduler.enqueue(wholePage(live->backend, live->handle, 10, 0.25, journal.revision()),
                              core::render::RenderPriority::Visible).isValid());
    REQUIRE(scheduler.waitForIdle(10000));
    scheduler.deliverReady();
    {
        const std::lock_guard<std::mutex> guard(mutex);
        CHECK(drawn >= firstVerifyEnded);
    }
    REQUIRE(moved.has_value());
    CHECK(digest(moved->pixels) == digest(pageOne.pixels));

    // 3. TWO MORE COMMANDS: the second waits for the first's verify -- shown in the order issued, never ahead of one.
    REQUIRE(journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{1}, 20)));
    const auto third = Clock::now();
    REQUIRE(journal.run(std::make_unique<core::RotatePageCommand>(30, 90)));
    CHECK(std::chrono::duration<double, std::milli>(Clock::now() - third).count() >= 100.0);

    // 4. A SAVE waits for the last verify, and writes exactly the file it verified.
    auto saved = live->coordinator->save(journal, live->path);
    REQUIRE(saved);
    REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});
    QFile file(live->path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray onDisk = file.readAll();

    scheduler.unsubscribeReady(token);
    const std::lock_guard<std::mutex> guard(mutex);
    INFO("events: " << [&] { std::string all; for (const auto& e : events) all += e + " "; return all; }());
    // show, verified, show, verified, show, verified -- then the save's own compose, which is not a show's.
    REQUIRE(events.size() >= 6);
    const std::vector<std::string> expected{"show", "verified", "show", "verified", "show", "verified"};
    CHECK(std::vector<std::string>(events.begin(), events.begin() + 6) == expected);
    CHECK(onDisk.size() == thirdVerified.size());
    CHECK(digest(onDisk) == digest(thirdVerified));
}
