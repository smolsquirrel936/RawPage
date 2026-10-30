// tst_remap -- Stage 7 T3 (`Stage7-Assembly-Plan.md` §6), decision D7-3, through the REAL ENGINE: guide cases A-30
// and A-32 (`Stage7-Assembly-Testing-Guide.md` §5.5).
//
// The document is the shipped backend's, its journal SHOWN as `ui/shell/DocumentWindow` shows it (`showEdits` after
// every command), and the tiles go through the shipped scheduler and cache with a counting rasterizer between the
// scheduler and the engine -- the count is of calls INTO PDFium, by the index each was asked for.
//
// RULE 7: a page is graded by what it SHOWS. `pages-900.pdf` draws "page N of 900" at the top of page N; a moved
// page's tile is graded by the text the live engine extracts at its new index and by its pixels against the same
// page rasterized from the SAVED file, reopened in a fresh engine -- never by an index our map computed.
//
// WHAT THIS DOES NOT COVER (convention 19): the panes. The tiles here are asked for by this file, as
// `ui/viewer/TilePresenter` would ask; that the views ask for them at the new indices, keep their place and show no
// other page's pixels meanwhile is `tests/composition/tst_assemblyshown.cpp`'s.

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/Destinations.h"
#include "core/doc/Outline.h"
#include "core/doc/PageTree.h"
#include "core/doc/SaveCoordinator.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "core/text/SearchUnit.h"
#include "pdf/DocumentBackendImpl.h"
#include "platform/win/WinDurableFiles.h"

#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

using namespace rawpage;
using namespace rawpage::test_oracle;
using core::render::RenderPriority;
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
    int saves = 0;
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
    live->journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); });
    live->coordinator = std::make_unique<core::SaveCoordinator>(live->backend, &live->durableFiles);
    return live;
}

QString save(Live& live)
{
    auto result = live.coordinator->save(*live.journal, live.path);
    REQUIRE(result);
    REQUIRE(result.value().kind == core::SaveOutcome::Kind::Saved);
    const QString copy = live.scratch.filePath(QStringLiteral("saved-%1.pdf").arg(++live.saves));
    REQUIRE(QFile::copy(live.path, copy));
    return copy;
}

// A whole page at `scale`, as `TilePresenter` shapes a request: the unsaved rotation as the DELTA between the
// model's page and the page the engine's tree has at that index (D3-15) -- `pages` null for a file opened fresh.
RenderRequest wholePage(core::DocumentHandle handle, const core::PageTree& engine, const core::PageTree* model,
                        int index, double scale, core::Revision revision)
{
    RenderRequest request;
    request.document = handle;
    request.pageIndex = index;
    request.deviceScale = scale;
    request.revision = revision;
    request.flags = core::render::RenderFlags::Annotations;
    const core::Page* page = engine.at(index);
    REQUIRE(page != nullptr);
    core::PdfSize size = page->displaySize();
    if (model != nullptr) {
        const int delta = (((static_cast<int>(model->rotation(index)) - static_cast<int>(engine.rotation(index))) % 360) + 360) % 360;
        request.pageRotationDelta = core::rotateBy(core::PageRotation::None, delta / 90);
        if (core::swapsAxes(request.pageRotationDelta)) size = core::PdfSize{size.height, size.width};
    }
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(size.width * scale)),
                                          static_cast<int>(std::ceil(size.height * scale))};
    return request;
}

core::RasterImage rasterOf(core::IDocumentBackend& backend, const RenderRequest& request)
{
    auto image = backend.rasterizer(request.document)->raster(request);
    REQUIRE(image);
    return image.value();
}

// The live page as the screen would ask for it: the engine's tree and the model's rotations.
core::RasterImage livePage(Live& live, int index, double scale)
{
    return rasterOf(live.backend, wholePage(live.handle, *live.backend.pages(live.handle), &live.journal->document().pages(),
                                            index, scale, live.journal->revision()));
}

core::RasterImage savedPage(const QString& path, int index, double scale)
{
    pdf::DocumentBackendImpl fresh;
    auto handle = fresh.open(path, nullptr);
    REQUIRE(handle);
    const core::RasterImage image =
        rasterOf(fresh, wholePage(handle.value(), *fresh.pages(handle.value()), nullptr, index, scale, core::Revision::initial()));
    fresh.close(handle.value());
    return image;
}

long long differingBytes(const core::RasterImage& a, const core::RasterImage& b)
{
    if (a.width != b.width || a.height != b.height) return std::max(a.pixels.size(), b.pixels.size());
    long long n = 0;
    for (qsizetype i = 0; i < a.pixels.size(); ++i)
        if (a.pixels[i] != b.pixels[i]) ++n;
    return n;
}

// Every call into the engine, by the index it was asked for.
struct EngineCount {
    std::mutex mutex;
    std::map<int, int> byIndex;
    std::atomic<int> total{0};
    std::atomic<int> warm{0};
    int of(int index)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        const auto found = byIndex.find(index);
        return found == byIndex.end() ? 0 : found->second;
    }
};

class CountingRasterizer final : public core::IPageRasterizer {
public:
    CountingRasterizer(core::IPageRasterizer& inner, EngineCount& count) : inner_(inner), count_(count) {}
    core::Result<core::RasterImage> raster(const RenderRequest& request) override
    {
        // The one-pixel tile a page command asks for to warm the reloaded document (A2 at Stage 7's named work) is
        // counted apart: it is no page anybody looks at.
        if (request.tile.width == 1 && request.tile.height == 1) {
            ++count_.warm;
            return inner_.raster(request);
        }
        {
            const std::lock_guard<std::mutex> lock(count_.mutex);
            ++count_.byIndex[request.pageIndex];
        }
        ++count_.total;
        return inner_.raster(request);
    }

private:
    core::IPageRasterizer& inner_;
    EngineCount& count_;
};

} // namespace

TEST_CASE("remap: a moved page is not drawn again -- its tiles come from the cache at its new index", "[remap][A-30]")
{
    auto live = openLive("ordinary/pages-900.pdf");
    EngineCount count;
    core::render::RenderCache cache;
    core::render::RenderScheduler scheduler(
        [&]() -> std::unique_ptr<core::IPageRasterizer> {
            return std::make_unique<CountingRasterizer>(*live->backend.rasterizer(live->handle), count);
        },
        &cache);
    // THE PRODUCT'S MUTATION HANDLER, as `DocumentWindow` subscribes it: the scheduler (and with it the cache) told
    // the scope and the engine's tree after every command.
    live->journal->subscribeMutated([&](const core::Scope& scope) {
        scheduler.documentChanged(live->handle, scope, live->journal->revision(), live->backend.pages(live->handle));
    });
    std::map<int, core::RasterImage> delivered;
    const auto token = scheduler.subscribeReady([&](const RenderRequest& request, const core::RasterImage& image) {
        if (request.tile.width > 1) delivered[request.pageIndex] = image;   // not the warm-up's one pixel
    });
    const auto pumpFor = [&](const std::vector<int>& pages) {
        QElapsedTimer timer;
        timer.start();
        const auto done = [&] {
            for (const int page : pages)
                if (delivered.count(page) == 0) return false;
            return true;
        };
        while (!done() && timer.elapsed() < 30000) {
            scheduler.waitForIdle(50);
            scheduler.deliverReady();
        }
        REQUIRE(done());
    };
    constexpr double kScale = 0.5;
    const auto ask = [&](int page) {
        scheduler.enqueue(wholePage(live->handle, *live->backend.pages(live->handle), &live->journal->document().pages(),
                                    page, kScale, live->journal->revision()),
                          RenderPriority::Visible);
    };

    // A VIEWPORT OVER PAGES 10 … 19, rendered and cached.
    std::vector<int> viewport;
    for (int page = 10; page <= 19; ++page) viewport.push_back(page);
    for (const int page : viewport) ask(page);
    pumpFor(viewport);
    std::map<int, core::RasterImage> before;
    for (const int page : viewport) before[page] = delivered.at(page);
    REQUIRE(count.total.load() == 10);

    // MOVED AFTER PAGE 500: the old 500 is the new 490, so the run lands at 491 … 500.
    REQUIRE(live->journal->run(std::make_unique<core::MovePagesCommand>(viewport, 491)));
    const long long servedBefore = scheduler.stats().servedFromCache;
    delivered.clear();
    std::vector<int> moved;
    for (int page = 491; page <= 500; ++page) moved.push_back(page);
    for (const int page : moved) ask(page);
    pumpFor(moved);
    scheduler.unsubscribeReady(token);

    // EVERY TILE FROM THE CACHE, UNDER ITS NEW INDEX, AND THE ENGINE NEVER ENTERED FOR ONE OF THEM.
    CHECK(scheduler.stats().servedFromCache - servedBefore == 10);
    int rasterized = 0;
    for (const int page : moved) rasterized += count.of(page);
    CHECK(rasterized == 0);
    CHECK(count.total.load() == 10);
    scheduler.waitForIdle(10000);
    CHECK(count.warm.load() == 1);   // the reload warmed once, off the path of the tiles above
    CHECK(cache.stats().remappedEntries >= 10);

    // AND EACH SHOWS THE PAGE IT SHOULD (rule 7): the text the reloaded engine extracts there, and the pixels the
    // SAVED file draws there, reopened in a fresh engine.
    core::ITextExtractor* extractor = live->backend.textExtractor(live->handle);
    REQUIRE(extractor != nullptr);
    for (int i = 0; i < 10; ++i) {
        const int page = 491 + i;
        core::text::PageTextModel model;
        REQUIRE(extractor->extractPage(live->handle, page, model));
        INFO("new index " << page);
        CHECK(model.text().contains(QStringLiteral("page %1 of 900").arg(11 + i)));
        CHECK(differingBytes(delivered.at(page), before.at(10 + i)) == 0);
    }
    const QString saved = save(*live);
    for (int i = 0; i < 10; i += 3) {
        INFO("saved index " << 491 + i);
        CHECK(differingBytes(delivered.at(491 + i), savedPage(saved, 491 + i, kScale)) == 0);
    }
    // A PAGE THE MOVE SHIFTED (old 20, now 10) is not the moved page's picture: the control that the comparison can
    // tell two pages of this file apart.
    CHECK(differingBytes(savedPage(saved, 10, kScale), before.at(10)) > 0);
}

TEST_CASE("remap.rotation: an unsaved rotation survives a move and each undo", "[remap][A-32]")
{
    constexpr double kScale = 0.75;
    // TWO FIXTURES. `refs-every-kind.pdf`'s pages share one size and one /Rotate, so a page read from the wrong tree
    // is sized and turned like the right one; `inherit-resources.pdf`'s pages inherit /Rotate 90 and 180 from
    // different nodes, except one that overrides it -- there, a delta read against another page's /Rotate, or a page
    // drawn turned by another page's correction, is a picture of the wrong shape.
    struct Fixture {
        const char* file;
        int page;   // rotated, then moved to `to`
        int to;
    };
    for (const Fixture& fixture : {Fixture{"assembly/refs-every-kind.pdf", 5, 2}, Fixture{"assembly/inherit-resources.pdf", 4, 0}}) {
        INFO(fixture.file);
        // THE REFERENCES, each from a saved file: the page rotated and saved alone; the original.
        auto reference = openLive(fixture.file);
        const core::RasterImage original = savedPage(reference->path, fixture.page, kScale);
        const core::RasterImage before = savedPage(reference->path, fixture.page - 1, kScale);
        REQUIRE(reference->journal->run(std::make_unique<core::RotatePageCommand>(fixture.page, 90)));
        const core::RasterImage turned = savedPage(save(*reference), fixture.page, kScale);
        REQUIRE((turned.width == original.height && turned.height == original.width));

        auto live = openLive(fixture.file);
        REQUIRE(live->journal->run(std::make_unique<core::RotatePageCommand>(fixture.page, 90)));
        CHECK(differingBytes(livePage(*live, fixture.page, kScale), turned) == 0);

        // MOVED, UNSAVED: drawn turned ONCE, at its new index -- not twice (the delta and the reloaded /Rotate both
        // applied), not zero (the delta read against the wrong page).
        REQUIRE(live->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{fixture.page}, fixture.to)));
        CHECK(differingBytes(livePage(*live, fixture.to, kScale), turned) == 0);
        // ... and the page now at its old index is the one that was before it, as it was.
        CHECK(differingBytes(livePage(*live, fixture.page, kScale), before) == 0);

        // THE MOVE UNDONE: back where it was, still turned once.
        REQUIRE(live->journal->undo());
        CHECK(differingBytes(livePage(*live, fixture.page, kScale), turned) == 0);
        // THE ROTATION UNDONE: as it opened.
        REQUIRE(live->journal->undo());
        CHECK(differingBytes(livePage(*live, fixture.page, kScale), original) == 0);

        // AND REDONE BOTH, then SAVED: the file draws what the screen drew.
        REQUIRE(live->journal->redo());
        REQUIRE(live->journal->redo());
        const core::RasterImage shown = livePage(*live, fixture.to, kScale);
        CHECK(differingBytes(shown, turned) == 0);
        CHECK(differingBytes(savedPage(save(*live), fixture.to, kScale), shown) == 0);
        // After the save the engine is not reloaded, and its tree is the order just written: still turned once.
        CHECK(differingBytes(livePage(*live, fixture.to, kScale), turned) == 0);
    }
}

namespace {

// The identity a page SHOWS (rule 7): the `P-0004`-shaped token its text carries, as the reloaded engine extracts it.
QString shownIdentity(Live& live, int index)
{
    core::text::PageTextModel model;
    if (!live.backend.textExtractor(live.handle)->extractPage(live.handle, index, model)) return {};
    static const QRegularExpression pattern(QStringLiteral("(?<![A-Za-z0-9-])[A-Z][A-Z0-9]*-[0-9]{4}(?![A-Za-z0-9-])"));
    const QRegularExpressionMatch match = pattern.match(model.text());
    return match.hasMatch() ? match.captured(0) : QString();
}

void collect(const std::vector<core::OutlineItem>& items, const QString& path, std::map<QString, int>& into)
{
    for (std::size_t i = 0; i < items.size(); ++i) {
        const QString at = path + QLatin1Char('/') + QString::number(i);
        into[at] = items[i].destination.pageIndex;
        collect(items[i].children, at, into);
    }
}

} // namespace

TEST_CASE("remap: the bookmarks and the named destinations open the pages they named", "[remap][A-31]")
{
    auto live = openLive("assembly/refs-every-kind.pdf");
    // WHAT EACH NAMED AT OPEN, by the identity its page shows.
    std::map<QString, int> items;
    collect(live->backend.outline(live->handle)->roots(), QString(), items);
    REQUIRE_FALSE(items.empty());
    std::map<QString, QString> itemShows;
    for (const auto& [path, page] : items)
        if (page >= 0) itemShows[path] = shownIdentity(*live, page);
    std::map<QString, QString> nameShows;
    for (const core::NamedDestination& entry : live->backend.destinations(live->handle)->all())
        if (entry.destination.pageIndex >= 0) nameShows[entry.name] = shownIdentity(*live, entry.destination.pageIndex);
    REQUIRE_FALSE(nameShows.empty());

    // The first page to the end; the (new) first page duplicated; the page now at 3 deleted.
    const QString deleted = [&] {
        REQUIRE(live->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 11)));
        REQUIRE(live->journal->run(std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{0})));
        const QString gone = shownIdentity(*live, 3);
        REQUIRE(live->journal->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{3})));
        return gone;
    }();

    const auto grade = [&](int page, const QString& shows, const QString& what) {
        INFO(what.toStdString() << " named " << shows.toStdString());
        if (shows == deleted) {
            CHECK(page == -1);   // its page is gone: unresolved, as the file's repair leaves it
            return;
        }
        REQUIRE(page >= 0);
        CHECK(shownIdentity(*live, page) == shows);
        // THE ORIGINAL, NOT THE COPY: the first page showing that identity (a copy lands right after its source).
        for (int before = 0; before < page; ++before) CHECK(shownIdentity(*live, before) != shows);
    };
    std::map<QString, int> after;
    collect(live->backend.outline(live->handle)->roots(), QString(), after);
    for (const auto& [path, shows] : itemShows) grade(after.at(path), shows, QStringLiteral("bookmark ") + path);
    for (const core::NamedDestination& entry : live->backend.destinations(live->handle)->all())
        if (nameShows.count(entry.name) != 0) grade(entry.destination.pageIndex, nameShows.at(entry.name), entry.name);

    // A BOOKMARK FOUND BY A SEARCH is read from the same outline: its destination lands where the pane's does.
    std::vector<core::text::SearchUnit> units;
    REQUIRE(live->backend.searchUnits(live->handle, core::text::kOutlinePart, units));
    std::size_t graded = 0;
    for (const core::text::SearchUnit& unit : units) {
        if (unit.destination.pageIndex < 0) continue;
        CHECK(!shownIdentity(*live, unit.destination.pageIndex).isEmpty());
        CHECK(shownIdentity(*live, unit.destination.pageIndex) != deleted);
        ++graded;
    }
    CHECK(graded > 0);

    // UNDONE, every one names its page again where it opened.
    REQUIRE(live->journal->undo());
    REQUIRE(live->journal->undo());
    REQUIRE(live->journal->undo());
    std::map<QString, int> back;
    collect(live->backend.outline(live->handle)->roots(), QString(), back);
    CHECK(back == items);
}
