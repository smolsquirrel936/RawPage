// tst_remap -- Stage 7 T3 (`Stage7-Assembly-Plan.md` §6), decision D7-3: what a page command's map does to every
// index-keyed consumer in `core/`. Guide cases A-30, A-31 and A-34 (`Stage7-Assembly-Testing-Guide.md` §5.5), their
// headless halves.
//
// THE ENGINE HERE IS A FAKE WITH AN ORDER. `Engine::order[i]` is the IDENTITY of the page drawn at index i, and a
// "reload" replaces the order exactly as `IDocumentBackend::showEdits` swaps a reordered document in. Every tile the
// fake draws is filled with its page's identity and every text it extracts names it, so a case grades a tile or a
// hit by WHICH PAGE it shows (rule 7) -- never by an index our own map computed, which would only prove the map
// agrees with itself.
//
// WHAT THESE CASES DO NOT COVER (convention 19): PDFium, the backend's reload and its drawn tree, and every widget.
// `tests/render/tst_remap.cpp` grades the same claims through the real engine on `pages-900.pdf`, and
// `tests/composition/tst_assemblyshown.cpp` through the composed product.

#include <catch2/catch_test_macros.hpp>

#include "core/backend.h"
#include "core/doc/DocumentId.h"
#include "core/doc/PageMap.h"
#include "core/doc/Scope.h"
#include "core/render/DiskTileStore.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"
#include "core/text/DocumentIndex.h"
#include "core/text/PageTextModel.h"
#include "core/text/SearchEngine.h"
#include "core/text/SearchQuery.h"
#include "core/text/TextSelection.h"

#include <QElapsedTimer>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

using namespace rawpage;
using core::DocumentHandle;
using core::PageMap;
using core::RasterImage;
using core::Revision;
using core::Scope;
using core::render::RenderCache;
using core::render::RenderPriority;
using core::render::RenderRequest;
using core::render::RenderScheduler;
using core::render::TileRect;
using core::text::DocumentIndex;
using core::text::PageTextModel;

namespace {

constexpr DocumentHandle kDoc{7};

// THE FAKE ENGINE: which page is drawn at each index, and a latch to hold a job inside it.
struct Engine {
    std::mutex mutex;
    std::vector<int> order;   // order[index] = the identity of the page drawn there
    std::atomic<int> rasters{0};
    std::map<int, int> rastersOf;   // identity -> rasterizations
    std::atomic<int> extractions{0};
    std::atomic<bool> hold{false};
    std::atomic<bool> inside{false};
    std::condition_variable released;

    explicit Engine(int count)
    {
        for (int i = 0; i < count; ++i) order.push_back(100 + i);
    }
    int identityAt(int index)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        return index >= 0 && index < static_cast<int>(order.size()) ? order[static_cast<std::size_t>(index)] : -1;
    }
    // A RELOAD: from now on the engine draws `map`'s order, which is what `showEdits` hands it.
    void reload(const PageMap& map)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        std::vector<int> next;
        int fresh = 900;
        for (int to = 0; to < map.newCount(); ++to) {
            const int from = map.oldIndexOf(to);
            next.push_back(from == PageMap::kNone ? fresh++ : order[static_cast<std::size_t>(from)]);
        }
        order = std::move(next);
    }
    void waitWhileHeld()
    {
        std::unique_lock<std::mutex> lock(mutex);
        inside = true;
        released.wait(lock, [this] { return !hold.load(); });
        inside = false;
    }
    void release()
    {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            hold = false;
        }
        released.notify_all();
    }
    bool waitInside(int milliseconds)
    {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        while (!inside.load()) {
            if (std::chrono::steady_clock::now() > until) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
};

RasterImage tileOf(int identity)
{
    RasterImage image;
    image.width = 4;
    image.height = 4;
    image.bytesPerLine = 16;
    image.format = core::PixelFormat::Bgrx8888;
    image.pixels = QByteArray(64, static_cast<char>(identity & 0xff));
    return image;
}

int identityOf(const RasterImage& image)
{
    return image.pixels.isEmpty() ? -1 : static_cast<unsigned char>(image.pixels[0]);
}

class FakeRasterizer final : public core::IPageRasterizer {
public:
    explicit FakeRasterizer(Engine& engine) : engine_(engine) {}
    core::Result<RasterImage> raster(const RenderRequest& request) override
    {
        if (engine_.hold.load()) engine_.waitWhileHeld();
        const int identity = engine_.identityAt(request.pageIndex);   // read AFTER the hold: the engine may have reloaded
        ++engine_.rasters;
        {
            const std::lock_guard<std::mutex> lock(engine_.mutex);
            ++engine_.rastersOf[identity];
        }
        return tileOf(identity);
    }

private:
    Engine& engine_;
};

void appendText(PageTextModel& model, const QString& text)
{
    double x = 72.0;
    for (const QChar ch : text) {
        core::text::TextChar c;
        c.codepoint = ch.unicode();
        c.fontSize = 10.0;
        c.origin = core::PdfPoint{x, 760.0};
        c.box = core::PdfRect{core::PdfPoint{x, 757.9}, core::PdfSize{5.5, 11.1}};
        model.append(c);
        x += 5.5;
    }
}

class FakeExtractor final : public core::ITextExtractor {
public:
    explicit FakeExtractor(Engine& engine) : engine_(engine) {}
    core::Status extractPage(DocumentHandle, int page, PageTextModel& into) override
    {
        // READ BEFORE THE HOLD: an extraction already inside the engine when the reload is queued finishes on the
        // OLD document, and is delivered after the command -- the case the index's epoch exists for.
        const int identity = engine_.identityAt(page);
        if (engine_.hold.load()) engine_.waitWhileHeld();
        ++engine_.extractions;
        appendText(into, QStringLiteral("ident%1").arg(identity));
        return {};
    }

private:
    Engine& engine_;
};

RenderRequest tile(int page, Revision revision)
{
    RenderRequest request;
    request.document = kDoc;
    request.pageIndex = page;
    request.tile = TileRect{0, 0, 4, 4};
    request.deviceScale = 1.0;
    request.revision = revision;
    return request;
}

PageMap moveMap(int count, std::vector<int> pages, int to)
{
    const std::optional<PageMap> map = PageMap::move(count, pages, to);
    REQUIRE(map);
    return *map;
}

void pump(RenderScheduler& scheduler, const std::function<bool()>& done, int budgetMs = 10000)
{
    QElapsedTimer timer;
    timer.start();
    do {
        scheduler.waitForIdle(20);
        scheduler.deliverReady();
    } while (!done() && timer.elapsed() < budgetMs);
}

} // namespace

// =============================================================================================
// A-30's core half -- the cache and the scheduler move what they hold
// =============================================================================================

TEST_CASE("remap: the tile cache carries a moved page's tiles to its new index and drops a removed page's",
          "[remap][A-30]")
{
    Engine engine(8);
    RenderCache cache;
    const Revision before = Revision::initial();
    for (int page = 0; page < 8; ++page) cache.put(tile(page, before), tileOf(engine.identityAt(page)));

    // Pages 1 and 2 to the end: the identities at 1 … 7 shift, and every tile goes with its page.
    const PageMap moved = moveMap(8, {1, 2}, 6);
    const Revision after = before.next();
    cache.invalidate(kDoc, Scope::pageCommand(moved), after);
    engine.reload(moved);
    for (int page = 0; page < 8; ++page) {
        const std::optional<RasterImage> held = cache.lookup(tile(page, after));
        INFO("new index " << page);
        REQUIRE(held);
        CHECK(identityOf(*held) == engine.identityAt(page));   // the page that now stands there (rule 7)
    }
    CHECK(cache.stats().remappedEntries == 8);
    CHECK_FALSE(cache.lookup(tile(1, before)));   // nothing is left at the old revision

    // A removal: page 3's tile goes, nothing else does.
    const PageMap removal = *PageMap::removal(8, {3});
    const Revision third = after.next();
    cache.invalidate(kDoc, Scope::pageCommand(removal), third);
    engine.reload(removal);
    CHECK(cache.entryCount() == 7);
    for (int page = 0; page < 7; ++page) {
        const std::optional<RasterImage> held = cache.lookup(tile(page, third));
        REQUIRE(held);
        CHECK(identityOf(*held) == engine.identityAt(page));
    }

    // CONTROL (rule 5): the same move answered as a plain `WholeDocument` -- a consumer that cannot remap --
    // keeps nothing, so the counts above are the remap's and not a cache that never drops.
    RenderCache control;
    for (int page = 0; page < 8; ++page) control.put(tile(page, before), tileOf(100 + page));
    control.invalidate(kDoc, Scope::wholeDocument(), after);
    CHECK(control.entryCount() == 0);
}

TEST_CASE("remap.fallback: a tier that does not remap drops the document's tiles and never serves another page's",
          "[remap][remap.fallback][A-34]")
{
    // THE DISK TIER IS THE CONSUMER WITHOUT A REMAP (`RenderCache.h`): it answers a page command's scope as the
    // `WholeDocument` it is. The memory tier is given no room, so every tile lives on disk alone.
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    Engine engine(6);
    RenderCache::Config config;
    config.memoryCapBytes = 1;   // refuses every tile: they live on the disk tier
    RenderCache cache(config);
    auto disk = std::make_unique<core::render::DiskTileStore>(directory.path(), std::size_t{1} << 20);
    REQUIRE(disk->isUsable());
    cache.setDiskTier(std::move(disk));
    const Revision before = Revision::initial();
    for (int page = 0; page < 6; ++page) cache.put(tile(page, before), tileOf(engine.identityAt(page)));
    REQUIRE(cache.diskTier()->entryCount() == 6);

    const PageMap moved = moveMap(6, {0}, 5);
    const Revision after = before.next();
    cache.invalidate(kDoc, Scope::pageCommand(moved), after);
    engine.reload(moved);
    CHECK(cache.diskTier()->entryCount() == 0);
    CHECK(cache.diskTier()->remappedCount() == 0);
    // NOT ONE TILE OF THE WRONG IDENTITY at any index, before or after the move's revision: each is a miss, and
    // the page is drawn again.
    for (int page = 0; page < 6; ++page) {
        CHECK_FALSE(cache.lookup(tile(page, after)));
        CHECK_FALSE(cache.lookup(tile(page, before)));
    }
}

TEST_CASE("remap: the scheduler moves a queued tile with its page and drops one that was running", "[remap][A-30]")
{
    Engine engine(6);
    RenderCache cache;
    RenderScheduler scheduler([&engine] { return std::unique_ptr<core::IPageRasterizer>(new FakeRasterizer(engine)); },
                              &cache);
    std::vector<std::pair<RenderRequest, int>> delivered;
    std::vector<RenderRequest> warmed;   // the one-pixel tile a page command asks for at `Background` (A2's named work)
    const auto token = scheduler.subscribeReady([&](const RenderRequest& request, const RasterImage& image) {
        if (request.tile.width == 1 && request.tile.height == 1) {
            warmed.push_back(request);
            return;
        }
        delivered.emplace_back(request, identityOf(image));
    });

    // ONE TILE INSIDE THE ENGINE, and four queued behind it, when the page command lands.
    const Revision before = Revision::initial();
    engine.hold = true;
    scheduler.enqueue(tile(0, before), RenderPriority::Visible);
    REQUIRE(engine.waitInside(5000));
    for (int page = 1; page <= 4; ++page) scheduler.enqueue(tile(page, before), RenderPriority::Visible);

    // Page 2 to the front, page 4 removed -- as one map (the move, then the removal).
    const PageMap moved = moveMap(6, {2}, 0);
    const PageMap both = *moved.then(*PageMap::removal(6, {4}));
    const Revision after = before.next();
    engine.reload(both);   // the reload is queued BEFORE the scheduler hears (the journal's order)
    scheduler.documentChanged(kDoc, Scope::pageCommand(both), after);
    engine.release();
    pump(scheduler, [&] { return scheduler.pendingCount() == 0 && scheduler.runningCount() == 0; });
    pump(scheduler, [] { return false; }, 200);
    scheduler.unsubscribeReady(token);

    // The running tile (old page 0, drawn after the reload at index 0 -- another page now) was DROPPED; the
    // queued ones arrived at their pages' new indices and revision, each showing the page it was asked for.
    CHECK(scheduler.stats().remapped == 3);   // old 1, 2, 3 -- old 4 is removed
    REQUIRE(delivered.size() == 3);
    // THE PAGES ASKED FOR ARE THE PAGES DELIVERED -- 101, 102 and 103, the queued pages the command kept -- each at
    // the index it now stands at. Graded by identity against what was ASKED, not only against the index it came
    // back under: a tile drawn at its old index would also show "the page at that index", only another page.
    std::multiset<int> identities;
    for (const auto& [request, identity] : delivered) {
        INFO("delivered at " << request.pageIndex);
        CHECK(request.revision == after);
        CHECK(identity == engine.identityAt(request.pageIndex));
        identities.insert(identity);
    }
    CHECK(identities == std::multiset<int>{101, 102, 103});
    // THE RELOAD WARMED (A2 at Stage 7's named work): one pixel of the first page the command moved -- index 0, where
    // page 2 now stands -- at the new revision, drawn after every tile anyone was waiting for and never cached.
    CHECK(scheduler.stats().warmed == 1);
    REQUIRE(warmed.size() == 1);
    CHECK(warmed.front().pageIndex == 0);
    CHECK(warmed.front().revision == after);
    CHECK_FALSE(cache.lookup(warmed.front()));
    // CONTROL: the running tile WOULD have shown another page's pixels under page 0's key, had it been kept --
    // the engine drew it after the reload, at index 0, which is now the page that was 2.
    CHECK(engine.rastersOf[engine.identityAt(0)] >= 1);
}

// =============================================================================================
// A-31's core half -- the text index, a search, a selection, the find bar's hits
// =============================================================================================

namespace {

struct TextRig {
    Engine engine{20};
    RenderScheduler scheduler{nullptr,
                              [this] { return std::unique_ptr<core::ITextExtractor>(new FakeExtractor(engine)); },
                              nullptr, RenderScheduler::Config{}};
    DocumentIndex index{scheduler};
    core::text::SearchEngine search{index, core::text::SearchEngine::Config{1}};
    core::DocumentId id = core::DocumentId::of(QStringLiteral("C:/scratch/remap.pdf"), 1000, QByteArray("remap"));
    DocumentIndex::EntryId entry = index.attach(&id, kDoc, 20);

    void pumpUntil(const std::function<bool()>& done, int budgetMs = 10000)
    {
        QElapsedTimer timer;
        timer.start();
        while (!done() && timer.elapsed() < budgetMs) {
            scheduler.waitForIdle(20);
            scheduler.deliverReady();
            search.waitForIdle(20);
            search.deliverReady();
        }
    }
    std::vector<int> pagesHitting(const QString& word)
    {
        std::optional<core::text::SearchEngine::Outcome> outcome;
        core::text::SearchEngine::Handlers handlers;
        handlers.finished = [&](const core::text::SearchEngine::Outcome& o) { outcome = o; };
        core::text::SearchQuery query;
        query.pattern = word;
        REQUIRE(search.start(entry, query, handlers));
        pumpUntil([&] { return outcome.has_value(); });
        REQUIRE(outcome);
        std::vector<int> pages;
        for (const core::text::SearchHit& hit : outcome->hits) pages.push_back(hit.page);
        return pages;
    }
};

} // namespace

TEST_CASE("remap: the text index carries every page's text to its new index without extracting", "[remap][A-31]")
{
    TextRig rig;
    // Every page read once, by a search for a word every page has.
    REQUIRE(rig.pagesHitting(QStringLiteral("ident")).size() == 20);
    const int extracted = rig.engine.extractions.load();
    REQUIRE(extracted == 20);

    // Pages 3 … 5 to the end, as `MovePagesCommand` makes the map.
    const PageMap moved = moveMap(20, {3, 4, 5}, 17);
    rig.engine.reload(moved);
    rig.index.invalidate(rig.entry, Scope::pageCommand(moved));

    // A FIND FOR A MOVED PAGE'S IDENTITY HITS IT AT ITS NEW INDEX, and nothing was extracted to find it.
    CHECK(rig.pagesHitting(QStringLiteral("ident103")) == std::vector<int>{17});
    CHECK(rig.pagesHitting(QStringLiteral("ident106")) == std::vector<int>{3});
    CHECK(rig.engine.extractions.load() == extracted);
    CHECK(rig.index.stats().pagesRemapped == 20);
    for (int page = 0; page < 20; ++page) {
        const std::shared_ptr<const core::text::IndexedPage> held = rig.index.page(rig.entry, page);
        REQUIRE(held);
        CHECK(held->pageIndex() == page);
        CHECK(held->model().text().contains(QStringLiteral("ident%1").arg(rig.engine.identityAt(page))));
    }

    // A REMOVAL drops the page's text and the count with it; an insertion is read when asked.
    const PageMap removal = *PageMap::removal(20, {0});
    rig.engine.reload(removal);
    rig.index.invalidate(rig.entry, Scope::pageCommand(removal));
    CHECK(rig.index.pageCount(rig.entry) == 19);
    CHECK(rig.pagesHitting(QStringLiteral("ident100")).empty());
    const PageMap copy = *PageMap::duplication(19, {0});
    rig.engine.reload(copy);
    rig.index.invalidate(rig.entry, Scope::pageCommand(copy));
    CHECK(rig.index.pageCount(rig.entry) == 20);
    CHECK_FALSE(rig.index.has(rig.entry, 1));   // the copy: not extracted yet
    CHECK(rig.pagesHitting(QStringLiteral("ident900")) == std::vector<int>{1});
    CHECK(rig.engine.extractions.load() == extracted + 1);   // the copy alone

    // CONTROL (rule 5): the same move as a `WholeDocument` drops every page and the next search reads them all.
    rig.index.invalidate(rig.entry, Scope::wholeDocument());
    (void)rig.pagesHitting(QStringLiteral("ident"));
    CHECK(rig.engine.extractions.load() == extracted + 1 + 20);
}

TEST_CASE("remap: a page read before a page command is refused and read again at its new index", "[remap][A-31]")
{
    TextRig rig;
    const DocumentIndex::Owner owner = rig.index.newOwner();
    // BOTH READS FINISH BEFORE THE COMMAND AND ARE DELIVERED AFTER IT -- extracted, waiting for the pump, which has
    // not run. That is the read the EPOCH exists for: one still running is cancelled and never delivered at all.
    REQUIRE_FALSE(rig.index.request(owner, rig.entry, 2, RenderPriority::Visible));
    REQUIRE_FALSE(rig.index.request(owner, rig.entry, 5, RenderPriority::Visible));
    REQUIRE(rig.scheduler.waitForIdle(5000));
    REQUIRE(rig.engine.extractions.load() == 2);

    // Page 2 to the end and page 0 to index 2: the index's 2 and 5 now name other pages.
    const PageMap moved = *moveMap(20, {2}, 19).then(moveMap(20, {0}, 2));
    rig.engine.reload(moved);
    rig.index.invalidate(rig.entry, Scope::pageCommand(moved));
    rig.pumpUntil([&] { return rig.index.outstanding(owner) == 0; });
    rig.pumpUntil([] { return false; }, 200);

    // THE READS FOLLOWED THEIR PAGES: what was 2 is read again at 19, what was 5 at 4 -- each showing ITS page --
    // and the two deliveries of the old epoch were refused, not filed under 2 and 5.
    CHECK(rig.index.outstanding(owner) == 0);
    CHECK(rig.engine.extractions.load() == 4);
    REQUIRE(moved.newIndexOf(2) == 19);
    REQUIRE(moved.newIndexOf(5) == 4);
    for (const int page : {19, 4}) {
        const std::shared_ptr<const core::text::IndexedPage> held = rig.index.page(rig.entry, page);
        INFO("page " << page);
        REQUIRE(held);
        CHECK(held->model().text() == QStringLiteral("ident%1").arg(rig.engine.identityAt(page)));
    }
    // Indices 2 and 5 hold nothing -- never ident102 and ident105, the old pages' text under the new pages' numbers.
    CHECK_FALSE(rig.index.has(rig.entry, 2));
    CHECK_FALSE(rig.index.has(rig.entry, 5));
}

TEST_CASE("remap: a page read running across a page command is read again at its page's new index", "[remap][A-31]")
{
    TextRig rig;
    const DocumentIndex::Owner owner = rig.index.newOwner();
    // Page 2's read is INSIDE the engine when the command lands, on the old document (`FakeExtractor` reads before
    // the hold).
    rig.engine.hold = true;
    REQUIRE_FALSE(rig.index.request(owner, rig.entry, 2, RenderPriority::Visible));
    REQUIRE(rig.engine.waitInside(5000));
    const PageMap moved = moveMap(20, {2}, 19);
    rig.engine.reload(moved);
    rig.index.invalidate(rig.entry, Scope::pageCommand(moved));
    rig.engine.release();
    rig.pumpUntil([&] { return rig.index.outstanding(owner) == 0; });
    rig.pumpUntil([] { return false; }, 200);
    // The owner got ITS page, at 19; the running read's result went nowhere.
    const std::shared_ptr<const core::text::IndexedPage> held = rig.index.page(rig.entry, 19);
    REQUIRE(held);
    CHECK(held->model().text() == QStringLiteral("ident102"));
    CHECK_FALSE(rig.index.has(rig.entry, 2));
}

TEST_CASE("remap: a search running across a page command is ended", "[remap][A-31]")
{
    TextRig rig;
    rig.engine.hold = true;
    std::optional<core::text::SearchEngine::Outcome> outcome;
    core::text::SearchEngine::Handlers handlers;
    handlers.finished = [&](const core::text::SearchEngine::Outcome& o) { outcome = o; };
    core::text::SearchQuery query;
    query.pattern = QStringLiteral("ident");
    const auto started = rig.search.start(rig.entry, query, handlers);
    REQUIRE(started);
    REQUIRE(rig.engine.waitInside(5000));

    const PageMap moved = moveMap(20, {0}, 19);
    rig.engine.reload(moved);
    rig.index.invalidate(rig.entry, Scope::pageCommand(moved));
    rig.engine.release();
    rig.pumpUntil([] { return false; }, 300);
    // ENDED, NOT FINISHED: its slots were the old numbering, and an outcome would name pages by them.
    CHECK_FALSE(rig.search.isRunning(started.value()));
    CHECK_FALSE(outcome.has_value());
}

TEST_CASE("remap: a selection follows its pages or is cleared", "[remap][A-31]")
{
    using core::text::TextPosition;
    using core::text::TextSelection;
    const TextSelection flow = TextSelection::flow(TextPosition{3, 2}, TextPosition{4, 7});

    // The run 3-4 moved whole, to the end of ten: the carets go with it.
    const TextSelection moved = flow.remapped(moveMap(10, {3, 4}, 8));
    CHECK(moved.start() == (TextPosition{8, 2}));
    CHECK(moved.end() == (TextPosition{9, 7}));
    // Page 0 moved past them: they shift down by one, still a run.
    CHECK(flow.remapped(moveMap(10, {0}, 9)).start() == (TextPosition{2, 2}));
    // The run BROKEN -- page 4 alone moved away -- and a page of it REMOVED: cleared, never guessed.
    CHECK(flow.remapped(moveMap(10, {4}, 9)).isEmpty());
    CHECK(flow.remapped(*PageMap::removal(10, {3})).isEmpty());
    // A copy inserted inside the run (page 3 duplicated) breaks it too.
    CHECK(flow.remapped(*PageMap::duplication(10, {3})).isEmpty());
    // A column follows its page; `Ctrl+A` stays the whole document, whatever its count.
    const TextSelection column = TextSelection::column(6, core::PdfRect{core::PdfPoint{10, 10}, core::PdfSize{50, 50}});
    CHECK(column.remapped(moveMap(10, {6}, 0)).columnPage() == 0);
    CHECK(column.remapped(*PageMap::removal(10, {6})).isEmpty());
    const TextSelection all = TextSelection::wholeDocument(10).remapped(*PageMap::removal(10, {2}));
    CHECK(all.start() == (TextPosition{0, 0}));
    CHECK(all.end() == (TextPosition{8, TextPosition::kEndOfPage}));
}

TEST_CASE("remap: the find bar's hits follow their pages in document order", "[remap][A-31]")
{
    using core::text::SearchHit;
    std::vector<SearchHit> hits;
    for (const int page : {1, 2, 2, 5, 7}) hits.push_back(SearchHit{page, core::text::Span{static_cast<int>(hits.size()), 9}, nullptr, {}});
    // Page 5 to the front; the current hit is on page 5.
    int current = core::text::remapHits(hits, 3, moveMap(8, {5}, 0));
    REQUIRE(hits.size() == 5);
    CHECK(hits[0].page == 0);
    CHECK(hits[0].reading.begin == 3);   // the hit that was on page 5
    CHECK(current == 0);                 // ... still the current one
    CHECK(hits[1].page == 2);
    CHECK(hits[2].page == 3);
    CHECK(hits[3].page == 3);
    CHECK(hits[2].reading.begin < hits[3].reading.begin);   // a page's own order kept
    CHECK(hits[4].page == 7);
    // A removed page's hits go; a current hit on it is no hit.
    current = core::text::remapHits(hits, 0, *PageMap::removal(8, {0}));
    CHECK(hits.size() == 4);
    CHECK(current == -1);
}

TEST_CASE("remap: the index reads its bookmarks again after a page command and keeps its attachments", "[remap][A-31]")
{
    TextRig rig;
    std::atomic<int> reads{0};
    rig.index.setUnitReader([&reads](DocumentHandle, int part, std::vector<core::text::SearchUnit>& into) {
        ++reads;
        core::text::SearchUnit unit;
        unit.text = QStringLiteral("part %1").arg(part);
        into.push_back(unit);
        return core::Status{};
    });
    const DocumentIndex::Owner owner = rig.index.newOwner();
    for (const int part : {core::text::kOutlinePart, core::text::kAttachmentsPart, 3})
        (void)rig.index.requestUnits(owner, rig.entry, part, RenderPriority::Visible);
    rig.pumpUntil([&] { return rig.index.outstanding(owner) == 0; });
    REQUIRE(reads.load() == 3);

    // A BOOKMARK NAMES A PAGE, so the outline's part goes with a page command and is read again when asked; the
    // attachments' names none and stays; page 3's moved with its page, so it goes too.
    const PageMap moved = moveMap(20, {0}, 19);
    rig.engine.reload(moved);
    rig.index.invalidate(rig.entry, Scope::pageCommand(moved));
    CHECK(rig.index.units(rig.entry, core::text::kOutlinePart) == nullptr);
    CHECK(rig.index.units(rig.entry, core::text::kAttachmentsPart) != nullptr);
    CHECK(rig.index.units(rig.entry, 3) == nullptr);
    CHECK(rig.index.units(rig.entry, 2) == nullptr);   // page 3 is 2 now, and was never read there
    REQUIRE_FALSE(rig.index.requestUnits(owner, rig.entry, core::text::kOutlinePart, RenderPriority::Visible));
    rig.pumpUntil([&] { return rig.index.outstanding(owner) == 0; });
    CHECK(reads.load() == 4);
    CHECK(rig.index.units(rig.entry, core::text::kOutlinePart) != nullptr);
}
