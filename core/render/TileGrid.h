// core/render/TileGrid -- one page, one resolution, one viewport, three sets of tiles. Stage 2 T10.
//
// -------------------------------------------------------------------------------------------
// THE PROPERTY THAT MATTERS IS WHAT IT DOES **NOT** ENUMERATE
// -------------------------------------------------------------------------------------------
// Guide case A-30 asks for "no allocation proportional to the square of the zoom", and this is the
// file where that is either true or false. An A4 page at 6400 % on a 192-DPI monitor is 101,621 x
// 143,647 device pixels -- 222,000 tiles of 256 px, 14.6 gigapixels. A grid that built its tile
// list and then filtered it by the viewport would allocate all 222,000 `TileRect`s to hand back
// twelve, and the symptom would be a viewer that gets slower the further you zoom in, which reads
// as a rendering problem and is a loop bound.
//
// So `tilesFor()` **computes the index range the viewport covers and iterates only that**. The
// page's total tile count is never materialized, never counted into a container, and the returned
// set is bounded by the viewport's area rather than the page's. That is asserted directly: the
// extreme-zoom case checks the size of what comes back, and a second case checks that the same
// viewport over a page 800x larger returns the same number of tiles.
//
// -------------------------------------------------------------------------------------------
// THE THREE SETS, AND WHY `evict` NEEDS AN ARGUMENT
// -------------------------------------------------------------------------------------------
// `visible` covers the viewport exactly -- every tile the viewport touches, and no tile it does
// not. `prefetch` is a ring of tiles around it, so scrolling one line does not stall on a
// rasterization. `evict` is what the CALLER is holding that is now in neither -- which is why it
// takes the held set as a parameter rather than being derivable from the page: a grid is pure
// arithmetic and has no memory of what was rendered a frame ago. The three are disjoint by
// construction (A-31), and `evict` is a suggestion to the cache rather than an instruction: the
// cache's own LRU and byte cap decide what actually goes.
//
// -------------------------------------------------------------------------------------------
// `performance/prefetchPages` COUNTS PAGES, AND THIS CLASS SEES ONE PAGE
// -------------------------------------------------------------------------------------------
// The guide's A-31 says the prefetch set "honours `performance/prefetchPages`". That key counts
// PAGES, and the decision *which pages to ask about* belongs one layer up, in the layout that knows
// where page 4 is relative to the viewport (`ui/viewer/PageLayoutEngine`, T13). Within a page the
// knob is a ring of tiles, which is `Config::prefetchRings`. Both halves are real and they are not
// the same number; naming only the first would have left the tile ring undecided and unmeasured.
// A caller prefetching a neighbouring page passes that page's viewport rect, which may not
// intersect the page at all -- then `visible` is empty and `prefetch` holds the leading edge.
//
// PURE, and `core/` throughout: no display, no engine, no Qt GUI type.

#ifndef RAWPAGE_CORE_RENDER_TILEGRID_H
#define RAWPAGE_CORE_RENDER_TILEGRID_H

#include "core/Geometry.h"
#include "core/render/DpiPolicy.h"
#include "core/render/RenderRequest.h"

#include <vector>

namespace rawpage::core {
class Page;
}

namespace rawpage::core::render {

// The three sets. `TileRect` is `RenderRequest`'s integral device-pixel rectangle, deliberately --
// a tile is a literal buffer, and two adjacent tiles that rounded a shared edge independently
// disagree by a pixel, which shows as a hairline seam that moves when you zoom (see
// `RenderRequest.h`).
struct TileSet {
    std::vector<TileRect> visible;
    std::vector<TileRect> prefetch;
    std::vector<TileRect> evict;

    bool isEmpty() const noexcept
    {
        return visible.empty() && prefetch.empty() && evict.empty();
    }
};

class TileGrid {
public:
    struct Config {
        // 256 x 256 device pixels: 256 KiB of BGRA per tile. Large enough that a 1920x1080 viewport
        // is ~40 tiles rather than hundreds of scheduler entries, small enough that a cancelled
        // tile wastes a quarter-megabyte rather than a page. It is a Stage 2 constant rather than a
        // setting because no user-facing question in Features.md 1.2 asks it.
        int tileSize = 256;

        // Rings of tiles around the visible set. One ring is a viewport's worth of scrolling in
        // every direction before anything stalls, at a cost of roughly one extra tile row and
        // column per edge.
        int prefetchRings = 1;
    };

    TileGrid() = default;
    explicit TileGrid(Config config) noexcept;

    const Config& config() const noexcept { return config_; }

    // The page's size in RASTER device pixels at `resolution` -- `Resolution::rasterScale()`, not
    // the presented scale: these are the pixels a rasterizer produces, and a capped resolution
    // produces fewer of them and paints them larger. The PAGE's own rotation is already accounted
    // for, because `Page::displaySize()` is post-rotation.
    //
    // `viewRotation` is the session's extra quarter-turn on top of that (Stage 2 T14) and it
    // SWAPS THE BOUNDS at 90 and 270. Defaulted, because the overwhelming majority of callers --
    // and every case written before T14 -- mean none, and a defaulted parameter that changes an
    // answer is only safe when its default is the identity. It is here.
    static TileRect pageBounds(const Page& page, const Resolution& resolution,
                               PageRotation viewRotation = PageRotation::None) noexcept;

    // `viewport` is in the SAME space: raster device pixels within this page, origin at the page's
    // top-left. The caller translates a global viewport into page-local coordinates; a rect that
    // does not intersect the page is legitimate and yields an empty visible set.
    //
    // `held` is what the caller already has tiles for, and produces `evict`. Passing an empty
    // vector is the ordinary first-paint case.
    TileSet tilesFor(const Page& page, const Resolution& resolution, const TileRect& viewport,
                     const std::vector<TileRect>& held = {},
                     PageRotation viewRotation = PageRotation::None) const;

private:
    Config config_{};
};

} // namespace rawpage::core::render

#endif // RAWPAGE_CORE_RENDER_TILEGRID_H
