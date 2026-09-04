// tst_rendercache -- `core/render/RenderCache` and its two tiers. Stage 2 T11, guide cases A-40
// and A-41.
//
// A-41 is the one that matters most: `invalidate(Scope, Revision)` is the mechanism that makes
// Stage 3's mutations safe, and **Stage 2 is where it must already be right** because Stage 3 will
// build on it rather than check it. The half of it that is easy to get wrong is not the dropping --
// it is the RE-KEYING of everything the scope did not touch, without which `Scope` buys nothing at
// all and a one-page rotation re-renders a 900-page document.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Scope.h"
#include "core/render/DiskTileStore.h"
#include "core/render/RenderCache.h"

#include <QDir>
#include <QTemporaryDir>

#include <memory>
#include <vector>

using rawpage::core::DocumentHandle;
using rawpage::core::PixelFormat;
using rawpage::core::RasterImage;
using rawpage::core::Revision;
using rawpage::core::Scope;
using rawpage::core::render::DiskTileStore;
using rawpage::core::render::MemoryTileStore;
using rawpage::core::render::RenderCache;
using rawpage::core::render::RenderRequest;
using rawpage::core::render::TileRect;

namespace {

constexpr DocumentHandle kDocA{1};
constexpr DocumentHandle kDocB{2};

RenderRequest requestFor(DocumentHandle document, int page, int tileX, Revision revision)
{
    RenderRequest request;
    request.document = document;
    request.pageIndex = page;
    request.tile = TileRect{tileX, 0, 64, 64};
    request.deviceScale = 2.0;
    request.revision = revision;
    return request;
}

// A tile whose bytes are distinct per request, so a cache that returned the WRONG entry fails
// rather than passing because every tile looks alike.
RasterImage tileOf(int bytes, char fill = 'x')
{
    RasterImage image;
    image.width = 64;
    image.height = bytes / (64 * 4);
    image.bytesPerLine = 64 * 4;
    image.format = PixelFormat::Bgra8888;
    image.pixels = QByteArray(bytes, fill);
    return image;
}

// 64 x 16 x 4 = 4096 bytes, a convenient unit for the cap cases.
constexpr int kTileBytes = 64 * 16 * 4;

} // namespace

TEST_CASE("render cache hits, misses and refuses what cannot fit", "[core][render]")
{
    RenderCache::Config config;
    config.memoryCapBytes = 4 * kTileBytes;
    RenderCache cache(config);

    const RenderRequest a = requestFor(kDocA, 0, 0, Revision::initial());
    CHECK_FALSE(cache.lookup(a).has_value());
    CHECK(cache.stats().misses == 1);

    cache.put(a, tileOf(kTileBytes, 'a'));
    const auto hit = cache.lookup(a);
    REQUIRE(hit.has_value());
    CHECK(hit->pixels.at(0) == 'a');
    CHECK(cache.stats().memoryHits == 1);
    CHECK(cache.stats().insertions == 1);

    SECTION("a request that differs in ANY key field misses")
    {
        // D2-12's whole point, from the cache's side. Each of these differs from `a` by one field.
        CHECK_FALSE(cache.lookup(requestFor(kDocB, 0, 0, Revision::initial())).has_value());
        CHECK_FALSE(cache.lookup(requestFor(kDocA, 1, 0, Revision::initial())).has_value());
        CHECK_FALSE(cache.lookup(requestFor(kDocA, 0, 64, Revision::initial())).has_value());
        CHECK_FALSE(cache.lookup(requestFor(kDocA, 0, 0, Revision::initial().next())).has_value());

        RenderRequest scaled = a;
        scaled.deviceScale = 2.5;
        CHECK_FALSE(cache.lookup(scaled).has_value());
    }

    SECTION("an invalid request cannot hit, and is not a silent cold miss")
    {
        RenderRequest noRevision = a;
        noRevision.revision = Revision::invalid();
        CHECK_FALSE(noRevision.isValid());
        CHECK_FALSE(cache.lookup(noRevision).has_value());
        cache.put(noRevision, tileOf(kTileBytes));
        CHECK(cache.stats().refusals >= 1);
    }

    SECTION("a tile larger than the whole cap is REFUSED rather than admitted")
    {
        // Admitting it would evict everything else to store one tile, which the next insertion
        // would then evict -- a cache that holds nothing and reports 0 %, indistinguishable from a
        // cache that is merely cold.
        const RenderRequest huge = requestFor(kDocA, 9, 0, Revision::initial());
        cache.put(huge, tileOf(8 * kTileBytes));
        CHECK_FALSE(cache.lookup(huge).has_value());
        CHECK(cache.lookup(a).has_value());   // and the existing entry survived
    }
}

TEST_CASE("render cache evicts least-recently-used under its cap", "[core][render]")
{
    // A-40. The cap is in BYTES, not entries: a cache that counted tiles would hold ten 4-megapixel
    // tiles as happily as ten small ones.
    RenderCache::Config config;
    config.memoryCapBytes = 3 * kTileBytes;
    RenderCache cache(config);

    const RenderRequest one = requestFor(kDocA, 0, 0, Revision::initial());
    const RenderRequest two = requestFor(kDocA, 0, 64, Revision::initial());
    const RenderRequest three = requestFor(kDocA, 0, 128, Revision::initial());
    const RenderRequest four = requestFor(kDocA, 0, 192, Revision::initial());

    cache.put(one, tileOf(kTileBytes, '1'));
    cache.put(two, tileOf(kTileBytes, '2'));
    cache.put(three, tileOf(kTileBytes, '3'));
    CHECK(cache.entryCount() == 3);
    CHECK(cache.bytesHeld() == 3u * kTileBytes);

    // Touch `one`, so the least recently used is now `two` rather than `one`. Without the touch
    // this case would pass against a first-in-first-out cache, which is a different policy that
    // happens to agree on an untouched sequence.
    REQUIRE(cache.lookup(one).has_value());

    cache.put(four, tileOf(kTileBytes, '4'));
    CHECK(cache.entryCount() == 3);
    CHECK(cache.bytesHeld() <= 3u * kTileBytes);
    CHECK(cache.lookup(one).has_value());
    CHECK_FALSE(cache.lookup(two).has_value());
    CHECK(cache.lookup(three).has_value());
    CHECK(cache.lookup(four).has_value());

    SECTION("lowering the cap frees memory NOW, not at the next insertion")
    {
        MemoryTileStore store(4 * kTileBytes);
        store.put(one, tileOf(kTileBytes));
        store.put(two, tileOf(kTileBytes));
        CHECK(store.bytesHeld() == 2u * kTileBytes);
        store.setCapacityBytes(kTileBytes);
        CHECK(store.bytesHeld() <= static_cast<std::size_t>(kTileBytes));
    }
}

TEST_CASE("render cache measures what open decision O7 is waiting for", "[core][render]")
{
    // D2-14: O7 -- shared between documents or per document -- is answered by MEASUREMENT, and the
    // number that answers it is how often one document's insertion throws away another's tiles.
    // Building the instrumentation with the cache rather than at T18 is the decision; this case is
    // what makes it real.
    RenderCache::Config config;
    config.memoryCapBytes = 2 * kTileBytes;
    RenderCache cache(config);

    cache.put(requestFor(kDocA, 0, 0, Revision::initial()), tileOf(kTileBytes));
    cache.put(requestFor(kDocA, 0, 64, Revision::initial()), tileOf(kTileBytes));
    CHECK(cache.crossDocumentEvictions() == 0);

    // Now document B arrives and the cache is full.
    cache.put(requestFor(kDocB, 0, 0, Revision::initial()), tileOf(kTileBytes));
    CHECK(cache.crossDocumentEvictions() == 1);

    // And the control: a document evicting its OWN tiles is not cross-document contention and must
    // not be counted as such, or the number would only measure how full the cache is.
    RenderCache solo(config);
    solo.put(requestFor(kDocA, 0, 0, Revision::initial()), tileOf(kTileBytes));
    solo.put(requestFor(kDocA, 0, 64, Revision::initial()), tileOf(kTileBytes));
    solo.put(requestFor(kDocA, 0, 128, Revision::initial()), tileOf(kTileBytes));
    CHECK(solo.crossDocumentEvictions() == 0);

    CHECK(cache.stats().hitRate() >= 0.0);
    CHECK(cache.stats().hitRate() <= 1.0);
}

TEST_CASE("render cache invalidation drops the scope and re-keys the rest", "[core][render]")
{
    // A-41, and the re-key is the half that is not obvious. See `RenderCache.h`.
    RenderCache::Config config;
    config.memoryCapBytes = 64 * kTileBytes;
    RenderCache cache(config);

    const Revision r1 = Revision::initial();
    const Revision r2 = r1.next();

    const RenderRequest page0 = requestFor(kDocA, 0, 0, r1);
    const RenderRequest page3 = requestFor(kDocA, 3, 0, r1);
    const RenderRequest page7 = requestFor(kDocA, 7, 0, r1);
    const RenderRequest otherDoc = requestFor(kDocB, 3, 0, r1);
    cache.put(page0, tileOf(kTileBytes, '0'));
    cache.put(page3, tileOf(kTileBytes, '3'));
    cache.put(page7, tileOf(kTileBytes, '7'));
    cache.put(otherDoc, tileOf(kTileBytes, 'b'));

    cache.invalidate(kDocA, Scope::page(3), r2);

    SECTION("the scoped page is gone at both revisions")
    {
        CHECK_FALSE(cache.lookup(requestFor(kDocA, 3, 0, r1)).has_value());
        CHECK_FALSE(cache.lookup(requestFor(kDocA, 3, 0, r2)).has_value());
        CHECK(cache.stats().invalidatedEntries == 1);
    }

    SECTION("EVERY OTHER PAGE SURVIVES, AT THE NEW REVISION")
    {
        // The whole reason `Scope` exists. Without the re-key these two would miss, and rotating
        // one page of a 900-page catalogue would re-rasterize all of it -- which is not a
        // performance regression anybody would find, because nothing would be wrong on screen.
        const auto zero = cache.lookup(requestFor(kDocA, 0, 0, r2));
        REQUIRE(zero.has_value());
        CHECK(zero->pixels.at(0) == '0');
        CHECK(cache.lookup(requestFor(kDocA, 7, 0, r2)).has_value());
    }

    SECTION("a lookup at the OLD revision misses")
    {
        // The clause that stops a stale consumer being served pixels from before the mutation.
        CHECK_FALSE(cache.lookup(requestFor(kDocA, 0, 0, r1)).has_value());
        CHECK(cache.currentRevision(kDocA) == r2);
    }

    SECTION("the OTHER document is untouched, which is why the handle is in the signature")
    {
        // `Implementation-Plan.md` writes `invalidate(Scope, Revision)`. Without the document,
        // invalidating this document's page 3 would drop page 3 of every open document -- and
        // `RenderRequest` carries a handle precisely so a cache can be shared.
        const auto other = cache.lookup(requestFor(kDocB, 3, 0, r1));
        REQUIRE(other.has_value());
        CHECK(other->pixels.at(0) == 'b');
        CHECK(cache.currentRevision(kDocB).isValid() == false);
    }

    SECTION("a whole-document scope drops everything for that document and nothing else")
    {
        cache.invalidate(kDocA, Scope::wholeDocument(), r2.next());
        CHECK_FALSE(cache.lookup(requestFor(kDocA, 0, 0, r2.next())).has_value());
        CHECK_FALSE(cache.lookup(requestFor(kDocA, 7, 0, r2.next())).has_value());
        CHECK(cache.lookup(requestFor(kDocB, 3, 0, r1)).has_value());
    }

    SECTION("an OBJECT scope drops conservatively, because it cannot know which pages")
    {
        // `core/doc/Scope`'s widening rule, exercised from its intended caller: a wasted repaint is
        // the correct failure direction and a stale tile is not, so an object scope touches every
        // page of its document.
        RenderCache fresh(config);
        fresh.put(page0, tileOf(kTileBytes));
        fresh.put(page7, tileOf(kTileBytes));
        fresh.invalidate(kDocA, Scope::objects({12}), r2);
        CHECK(fresh.entryCount() == 0);
    }

    SECTION("closing a document drops its tiles and forgets its revision")
    {
        cache.dropDocument(kDocA);
        CHECK_FALSE(cache.currentRevision(kDocA).isValid());
        CHECK(cache.lookup(requestFor(kDocB, 3, 0, r1)).has_value());
    }
}

TEST_CASE("render cache's disk tier is the same interface and honours its own cap", "[core][render]")
{
    // D2-14: built behind the SAME interface as the memory tier, so it can be absent, present or
    // replaced without `RenderCache` knowing which. It is OFF by default -- whether it earns its
    // place is a measurement T18 makes -- so this case installs one explicitly.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    RenderCache::Config config;
    config.memoryCapBytes = kTileBytes;          // room for exactly one tile in memory
    RenderCache cache(config);
    auto disk = std::make_unique<DiskTileStore>(dir.path(), 3 * kTileBytes);
    REQUIRE(disk->isUsable());
    const DiskTileStore* diskTier = disk.get();
    cache.setDiskTier(std::move(disk));

    const RenderRequest one = requestFor(kDocA, 0, 0, Revision::initial());
    const RenderRequest two = requestFor(kDocA, 0, 64, Revision::initial());
    cache.put(one, tileOf(kTileBytes, '1'));
    cache.put(two, tileOf(kTileBytes, '2'));

    // `one` has been pushed out of memory by `two`, and is found on disk -- WITH ITS OWN BYTES.
    // Comparing the payload rather than merely "something came back" is what would catch a store
    // that served the wrong file, which is the failure a cache must never have.
    const auto recovered = cache.lookup(one);
    REQUIRE(recovered.has_value());
    CHECK(recovered->pixels.at(0) == '1');
    CHECK(recovered->width == 64);
    CHECK(recovered->bytesPerLine == 64 * 4);
    CHECK(cache.stats().diskHits == 1);

    // And it was PROMOTED back into memory, so the second look does not pay for the disk again.
    CHECK(cache.stats().promotions == 1);
    CHECK(cache.lookup(one).has_value());
    CHECK(cache.stats().memoryHits >= 1);

    SECTION("the disk cap evicts, and the files go with the entries")
    {
        for (int i = 0; i < 8; ++i)
            cache.put(requestFor(kDocA, i, 0, Revision::initial()), tileOf(kTileBytes));
        CHECK(diskTier->bytesHeld() <= 3u * kTileBytes);
        // The FILES are removed too, not just the index entries -- a tier that forgot an entry and
        // left its file behind would grow the directory without bound while reporting a cap that
        // was being honoured.
        const QStringList remaining =
            QDir(dir.path()).entryList({QStringLiteral("tile-*.bin")}, QDir::Files);
        CHECK(remaining.size() == static_cast<int>(diskTier->entryCount()));
        CHECK(diskTier->ioFailureCount() == 0);
    }

    SECTION("invalidation reaches both tiers")
    {
        cache.invalidate(kDocA, Scope::wholeDocument(), Revision::initial().next());
        CHECK(diskTier->entryCount() == 0);
        CHECK(QDir(dir.path()).entryList({QStringLiteral("tile-*.bin")}, QDir::Files).isEmpty());
    }
}

TEST_CASE("render cache's disk tier refuses rather than pretends when unusable", "[core][render]")
{
    // A tier that silently accepted writes it could not make would report a hit rate of zero and
    // look exactly like a cold cache, which is the one thing that must not be ambiguous about it.
    DiskTileStore store(QStringLiteral("/:/nonexistent-volume/rawpage-tiles"), kTileBytes);
    if (store.isUsable()) {
        // Some filesystems will happily create that path. The claim under test is the refusal, so
        // the case says so rather than asserting something the environment decides.
        SUCCEED("the platform created the directory; the unusable path is not reachable here");
        return;
    }
    const RenderRequest one = requestFor(kDocA, 0, 0, Revision::initial());
    CHECK_FALSE(store.put(one, tileOf(kTileBytes)));
    CHECK_FALSE(store.lookup(one).has_value());
    CHECK(store.entryCount() == 0);
}
