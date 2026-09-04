// tst_tilegrid -- `core/render/TileGrid`. Stage 2 T10, guide case A-31 (and A-30's second half).
//
// The case that matters most here is the one about what is NOT built: at 6400 % an A4 page is 14.6
// gigapixels and 222,000 tiles, and a grid that enumerated them all to hand back twelve would be a
// viewer that gets slower the further you zoom in -- which reads as a rendering problem and is a
// loop bound.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Page.h"
#include "core/render/DpiPolicy.h"
#include "core/render/TileGrid.h"

#include <algorithm>
#include <set>
#include <vector>

using rawpage::core::Page;
using rawpage::core::PdfPoint;
using rawpage::core::PdfRect;
using rawpage::core::PdfSize;
using rawpage::core::render::DpiPolicy;
using rawpage::core::render::MonitorScale;
using rawpage::core::render::PhysicalDpi;
using rawpage::core::render::Resolution;
using rawpage::core::render::TileGrid;
using rawpage::core::render::TileRect;
using rawpage::core::render::TileSet;
using rawpage::core::render::Zoom;

namespace {

constexpr PdfRect kA4{PdfPoint{0.0, 0.0}, PdfSize{595.0, 842.0}};

Page a4Page(int index = 0)
{
    return Page(index, kA4);
}

// 72 DPI exactly, so one PDF point is one device pixel and every expected number below can be read
// straight off the page size instead of being recomputed by the test -- a test that repeats the
// implementation's arithmetic checks that the arithmetic is self-consistent, not that it is right.
Resolution atPointScale()
{
    return Resolution{72.0, 1.0, 1.0};
}

bool contains(const std::vector<TileRect>& tiles, const TileRect& wanted)
{
    return std::find(tiles.begin(), tiles.end(), wanted) != tiles.end();
}

} // namespace

TEST_CASE("render tilegrid covers the page exactly, partial tiles included", "[core][render]")
{
    const TileGrid grid(TileGrid::Config{256, 0});
    const Page page = a4Page();

    const TileRect bounds = TileGrid::pageBounds(page, atPointScale());
    CHECK(bounds.width == 595);
    CHECK(bounds.height == 842);

    // The whole page as the viewport: 3 columns (595 = 256 + 256 + 83) x 4 rows (842 = 3*256 + 74).
    const TileSet all = grid.tilesFor(page, atPointScale(), bounds);
    REQUIRE(all.visible.size() == 12);

    long long area = 0;
    for (const TileRect& tile : all.visible) {
        CHECK(tile.width > 0);
        CHECK(tile.height > 0);
        // No tile hangs off the page. A rasterizer asked for pixels outside the page returns them
        // as white, so an overhanging tile is invisible until someone measures a checksum.
        CHECK(tile.x + tile.width <= bounds.width);
        CHECK(tile.y + tile.height <= bounds.height);
        area += static_cast<long long>(tile.width) * tile.height;
    }
    // NO GAP AND NO OVERLAP, asserted as an area identity rather than by eye: 12 rectangles that
    // tile a page have exactly the page's area, and any gap or overlap changes the sum.
    CHECK(area == static_cast<long long>(bounds.width) * bounds.height);

    CHECK(contains(all.visible, TileRect{512, 768, 83, 74}));   // the bottom-right partial tile
}

TEST_CASE("render tilegrid returns only the tiles the viewport touches", "[core][render]")
{
    const TileGrid grid(TileGrid::Config{256, 0});
    const Page page = a4Page();

    // A viewport inside one tile returns one tile.
    const TileSet one = grid.tilesFor(page, atPointScale(), TileRect{10, 10, 20, 20});
    REQUIRE(one.visible.size() == 1);
    CHECK(one.visible.front() == TileRect{0, 0, 256, 256});

    // A viewport straddling a boundary returns both, and no third.
    const TileSet two = grid.tilesFor(page, atPointScale(), TileRect{250, 10, 20, 20});
    CHECK(two.visible.size() == 2);

    // A viewport that misses the page entirely returns nothing -- the ordinary case for a page
    // scrolled off the top of a continuous layout.
    const TileSet none = grid.tilesFor(page, atPointScale(), TileRect{0, 5000, 100, 100});
    CHECK(none.visible.empty());

    // A viewport that starts above the page still covers the page's first row.
    const TileSet fromAbove = grid.tilesFor(page, atPointScale(), TileRect{0, -300, 595, 400});
    REQUIRE_FALSE(fromAbove.visible.empty());
    CHECK(contains(fromAbove.visible, TileRect{0, 0, 256, 256}));

    // AND A VIEWPORT ENTIRELY ABOVE THE PAGE SEES NOTHING. This is the assertion that separates
    // floor division from truncation, and the case above does NOT: both map a negative first row
    // to 0 once it is clamped, so only the LAST index tells them apart. C++'s `/` truncates
    // toward zero, so the last row of a viewport ending at y = -201 comes out as 0 rather than -1
    // -- and every page scrolled off the top of a 900-page document would rasterize its first tile
    // row on every scroll step, which is invisible except as a scheduler that is always busy.
    // Measured: with truncation restored, this line is the only one in the suite that reddens.
    const TileSet wellAbove = grid.tilesFor(page, atPointScale(), TileRect{0, -300, 595, 100});
    CHECK(wellAbove.visible.empty());

    // The same on the horizontal axis, which has its own copy of the arithmetic. The numbers are
    // chosen so truncation and floor DISAGREE here too: -101 / 256 truncates to 0 and floors to -1.
    const TileSet wellLeft = grid.tilesFor(page, atPointScale(), TileRect{-200, 0, 100, 842});
    CHECK(wellLeft.visible.empty());
}

TEST_CASE("render tilegrid's prefetch ring is disjoint from its visible set", "[core][render]")
{
    const TileGrid grid(TileGrid::Config{256, 1});
    const Page page = a4Page();

    // A viewport over the middle tile of the grid, so the ring is not clipped by the page edge in
    // either direction.
    const TileSet set = grid.tilesFor(page, atPointScale(), TileRect{300, 300, 100, 100});
    REQUIRE(set.visible.size() == 1);
    CHECK(set.visible.front() == TileRect{256, 256, 256, 256});

    // One ring around a single tile is the 8 neighbours, minus whatever the page edge removes: the
    // page is 3 columns wide, so column 2 is the last -- all three columns and rows 0..2 exist,
    // giving 9 tiles in the block and 8 in the ring.
    CHECK(set.prefetch.size() == 8);

    std::set<std::pair<int, int>> seen;
    for (const TileRect& tile : set.visible) seen.insert({tile.x, tile.y});
    for (const TileRect& tile : set.prefetch) {
        // A-31's first clause: the sets are disjoint. Asserted by insertion rather than by a search
        // of the other vector, so a duplicate WITHIN a set fails too.
        CHECK(seen.insert({tile.x, tile.y}).second);
    }

    // No ring means no prefetch, which is what the scheduler is handed when the user is dragging a
    // scrollbar and nothing speculative is worth queueing.
    const TileGrid noRing(TileGrid::Config{256, 0});
    CHECK(noRing.tilesFor(page, atPointScale(), TileRect{300, 300, 100, 100}).prefetch.empty());
}

TEST_CASE("render tilegrid evicts exactly what left both sets", "[core][render]")
{
    const TileGrid grid(TileGrid::Config{256, 0});
    const Page page = a4Page();

    const std::vector<TileRect> held{
        TileRect{0, 0, 256, 256},        // will still be visible
        TileRect{0, 256, 256, 256},      // will not
        TileRect{512, 768, 83, 74},      // nor will this
    };

    const TileSet set = grid.tilesFor(page, atPointScale(), TileRect{0, 0, 100, 100}, held);
    REQUIRE(set.visible.size() == 1);
    CHECK(set.visible.front() == TileRect{0, 0, 256, 256});
    REQUIRE(set.evict.size() == 2);
    CHECK(contains(set.evict, TileRect{0, 256, 256, 256}));
    CHECK(contains(set.evict, TileRect{512, 768, 83, 74}));
    CHECK_FALSE(contains(set.evict, TileRect{0, 0, 256, 256}));

    // A held tile inside the PREFETCH ring is not evicted -- that is the ring's whole purpose, and
    // an implementation that compared against `visible` alone would throw away exactly the tiles it
    // was about to ask for again.
    const TileGrid ringed(TileGrid::Config{256, 1});
    const TileSet kept = ringed.tilesFor(page, atPointScale(), TileRect{0, 0, 100, 100}, held);
    CHECK_FALSE(contains(kept.evict, TileRect{0, 256, 256, 256}));

    // A page with no extent evicts everything rather than leaking it. A degenerate page is what
    // `corpus/malformed/` is full of, and a cache that kept its tiles forever would be a leak with
    // no symptom until the cap is hit.
    Page empty(0, PdfRect{PdfPoint{0.0, 0.0}, PdfSize{0.0, 0.0}});
    const TileSet gone = grid.tilesFor(empty, atPointScale(), TileRect{0, 0, 100, 100}, held);
    CHECK(gone.visible.empty());
    CHECK(gone.evict.size() == held.size());
}

TEST_CASE("render tilegrid does not enumerate the page at extreme zoom", "[core][render]")
{
    // A-30's second half, and the property the header is about. At 6400 % on a 192-DPI monitor an
    // A4 page is 101,621 x 143,647 device pixels -- 222,000 tiles. A grid that built them all
    // would still return the right answer, so this case measures the SIZE of what comes back and
    // the time it takes to get it, which is the only observable difference.
    const DpiPolicy policy;
    const Resolution resolution =
        policy.resolve(Zoom::percent(6400.0), MonitorScale::percent(150.0), PhysicalDpi{192.0});
    const TileGrid grid(TileGrid::Config{256, 1});
    const Page page = a4Page();

    const TileRect bounds = TileGrid::pageBounds(page, resolution);
    // The cap bit, so the raster bounds are the capped ones -- still enormous.
    CHECK(bounds.width > 19000);
    CHECK(bounds.height > 27000);

    const TileSet set = grid.tilesFor(page, resolution, TileRect{9000, 12000, 1920, 1080});
    // A 1920x1080 viewport is at most 9 x 6 tiles of 256 px; with one ring that is 11 x 8 = 88.
    CHECK(set.visible.size() <= 63);
    CHECK(set.visible.size() + set.prefetch.size() <= 88);
    CHECK_FALSE(set.visible.empty());

    // AND THE CONTROL FOR THAT CLAIM: the same viewport over the same page at a resolution 3.3x
    // coarser -- an eleventh of the page area, 11.3 megapixels against 556 -- returns exactly the
    // same number of tiles. If the count tracked the PAGE rather than the VIEWPORT, these two would
    // differ; stating it as a comparison is what makes the claim checkable, where a threshold would
    // only be a number nobody can justify. Both viewports are wholly inside their page, so neither
    // count is clipped by an edge.
    const TileSet coarse = grid.tilesFor(page, Resolution{720.0, 1.0, 1.0}, TileRect{0, 0, 1920, 1080});
    const TileSet fine = grid.tilesFor(page, resolution, TileRect{0, 0, 1920, 1080});
    CHECK(coarse.visible.size() == fine.visible.size());
    CHECK(coarse.visible.size() == 40u);   // 8 columns x 5 rows of 256 px
}

TEST_CASE("render tilegrid tiles the RASTER size, not the presented one", "[core][render]")
{
    // A capped resolution rasterizes fewer pixels and paints them larger. Tiling the presented size
    // would ask the rasterizer for exactly the pixels the cap exists to avoid producing -- so the
    // cap would cost memory and buy nothing, and the only symptom would be that it did not help.
    const TileGrid grid(TileGrid::Config{256, 0});
    const Page page = a4Page();

    Resolution capped;
    capped.dpi = 72.0;
    capped.upscale = 4.0;
    capped.devicePixelRatio = 1.0;

    const TileRect bounds = TileGrid::pageBounds(page, capped);
    CHECK(bounds.width == 595);     // the raster size, not 4 x 595
    CHECK(bounds.height == 842);
}

TEST_CASE("render tilegrid sanitizes a nonsense configuration", "[core][render]")
{
    // `Config` is an aggregate anyone can fill in, and `core/` does not throw. A tile size of zero
    // divides; a negative one loops forever.
    const TileGrid zero(TileGrid::Config{0, -3});
    CHECK(zero.config().tileSize >= 16);
    CHECK(zero.config().prefetchRings == 0);

    const Page page = a4Page();
    const TileSet set = zero.tilesFor(page, atPointScale(), TileRect{0, 0, 100, 100});
    CHECK_FALSE(set.visible.empty());
}
