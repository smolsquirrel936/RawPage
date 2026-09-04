#include "core/render/TileGrid.h"

#include "core/doc/Page.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace rawpage::core::render {
namespace {

// A tile size of zero divides; a negative one loops forever. Sanitized rather than asserted,
// because `Config` is an aggregate anyone can fill in and this is `core/`, which does not throw.
constexpr int kMinTileSize = 16;
constexpr int kMaxTileSize = 4096;

int sanitizedTileSize(int requested) noexcept
{
    if (requested < kMinTileSize) return kMinTileSize;
    if (requested > kMaxTileSize) return kMaxTileSize;
    return requested;
}

// The page's extent in device pixels. `std::ceil` rather than a round or a truncation: a page
// 100.4 px wide has content in its 101st pixel column, and truncating loses it -- a one-pixel white
// stripe down the right-hand edge of every page, which is the kind of defect that gets attributed
// to the monitor.
int deviceExtent(double points, double scale) noexcept
{
    if (!std::isfinite(points) || !std::isfinite(scale) || points <= 0.0 || scale <= 0.0) return 0;
    const double pixels = std::ceil(points * scale);
    // Guard the cast. At 6400 % an A4 page is ~101,000 px, so this is not reachable from any zoom
    // the product offers -- but `scale` arrives from arithmetic over three doubles, and a silent
    // wrap here would produce a NEGATIVE extent and an empty page.
    constexpr double kMaxExtent = 1'000'000'000.0;
    return static_cast<int>(std::min(pixels, kMaxExtent));
}

// Floor-division that behaves for negative numerators. A viewport scrolled above the top of a page
// has a negative y, and C++'s `/` truncates toward zero -- so -1 / 256 is 0 rather than -1, which
// would silently drop the tile row that is half on screen.
int floorDiv(int value, int divisor) noexcept
{
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    return (remainder != 0 && ((remainder < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

struct TileRectHash {
    std::size_t operator()(const TileRect& tile) const noexcept
    {
        std::size_t seed = static_cast<std::size_t>(tile.x);
        auto mix = [&seed](int value) {
            seed ^= static_cast<std::size_t>(value) + 0x9e3779b97f4a7c15ULL + (seed << 6)
                    + (seed >> 2);
        };
        mix(tile.y);
        mix(tile.width);
        mix(tile.height);
        return seed;
    }
};

} // namespace

TileGrid::TileGrid(Config config) noexcept : config_(config)
{
    config_.tileSize = sanitizedTileSize(config_.tileSize);
    if (config_.prefetchRings < 0) config_.prefetchRings = 0;
}

TileRect TileGrid::pageBounds(const Page& page, const Resolution& resolution) noexcept
{
    // The RASTER scale, not the presented one. A capped resolution rasterizes fewer pixels and
    // paints them larger; tiling the presented size would ask the rasterizer for pixels the cap
    // exists to avoid producing.
    const double scale = resolution.rasterScale();
    const PdfSize size = page.displaySize();
    return TileRect{0, 0, deviceExtent(size.width, scale), deviceExtent(size.height, scale)};
}

TileSet TileGrid::tilesFor(const Page& page, const Resolution& resolution, const TileRect& viewport,
                           const std::vector<TileRect>& held) const
{
    TileSet out;

    const int tile = sanitizedTileSize(config_.tileSize);
    const TileRect bounds = pageBounds(page, resolution);
    if (bounds.isEmpty()) {
        // A page with no extent has no tiles, so everything held for it is stale. Returning an
        // empty set instead would leak every tile of a page that has just been rotated to nothing
        // -- and a degenerate page is exactly the kind of thing `corpus/malformed/` contains.
        out.evict = held;
        return out;
    }

    // The last row and column are PARTIAL. Clipping every tile to the page bounds is what makes
    // "covers the viewport exactly with no gap and no unnecessary tile" true at the edges, and it
    // is also what stops the rasterizer being asked for pixels that are not on the page.
    const int columns = (bounds.width + tile - 1) / tile;
    const int rows = (bounds.height + tile - 1) / tile;

    // The index range the viewport covers -- computed, never searched. This is the loop bound the
    // header is about: it depends on the VIEWPORT's size, not the page's.
    auto rangeFor = [&](const TileRect& rect, int rings, int& firstCol, int& lastCol, int& firstRow,
                        int& lastRow) {
        if (rect.isEmpty()) {
            firstCol = firstRow = 0;
            lastCol = lastRow = -1;
            return;
        }
        firstCol = floorDiv(rect.x, tile) - rings;
        lastCol = floorDiv(rect.x + rect.width - 1, tile) + rings;
        firstRow = floorDiv(rect.y, tile) - rings;
        lastRow = floorDiv(rect.y + rect.height - 1, tile) + rings;
        firstCol = std::max(firstCol, 0);
        firstRow = std::max(firstRow, 0);
        lastCol = std::min(lastCol, columns - 1);
        lastRow = std::min(lastRow, rows - 1);
    };

    auto tileAt = [&](int column, int row) {
        const int x = column * tile;
        const int y = row * tile;
        return TileRect{x, y, std::min(tile, bounds.width - x), std::min(tile, bounds.height - y)};
    };

    int visFirstCol = 0, visLastCol = -1, visFirstRow = 0, visLastRow = -1;
    rangeFor(viewport, 0, visFirstCol, visLastCol, visFirstRow, visLastRow);
    for (int row = visFirstRow; row <= visLastRow; ++row) {
        for (int column = visFirstCol; column <= visLastCol; ++column)
            out.visible.push_back(tileAt(column, row));
    }

    if (config_.prefetchRings > 0) {
        int preFirstCol = 0, preLastCol = -1, preFirstRow = 0, preLastRow = -1;
        rangeFor(viewport, config_.prefetchRings, preFirstCol, preLastCol, preFirstRow, preLastRow);
        for (int row = preFirstRow; row <= preLastRow; ++row) {
            for (int column = preFirstCol; column <= preLastCol; ++column) {
                // Disjoint by construction (A-31): the prefetch ring is the outer range minus the
                // inner one, tested by index rather than by searching the visible vector -- which
                // would make this quadratic in the viewport's tile count for no gain.
                const bool insideVisible = column >= visFirstCol && column <= visLastCol
                                           && row >= visFirstRow && row <= visLastRow;
                if (!insideVisible) out.prefetch.push_back(tileAt(column, row));
            }
        }
    }

    if (!held.empty()) {
        std::unordered_set<TileRect, TileRectHash> keep;
        keep.reserve(out.visible.size() + out.prefetch.size());
        for (const TileRect& t : out.visible) keep.insert(t);
        for (const TileRect& t : out.prefetch) keep.insert(t);
        for (const TileRect& t : held) {
            if (keep.find(t) == keep.end()) out.evict.push_back(t);
        }
    }

    return out;
}

} // namespace rawpage::core::render
