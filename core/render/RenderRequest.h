// core/render/RenderRequest -- one tile, described completely enough to be a cache key.
// Decision D2-12. **Built at T9 rather than T10, and that is a deviation with a reason**:
// `IPageRasterizer::raster()` takes one of these, `core/backend.h` has carried a forward
// declaration of it since T1, and T9's exit criterion is *"one page rasterizes to a `QImage` in a
// test"* -- which cannot be written without the type the method takes. T10 still owns `DpiPolicy`,
// `TileGrid` and `PagePresentation`, all of which are built ON this; only the value type moves.
//
// -------------------------------------------------------------------------------------------
// ITS EQUALITY IS THE CACHE KEY, AND EVERYTHING ELSE HERE FOLLOWS FROM THAT
// -------------------------------------------------------------------------------------------
// `RenderCache` (T11) is keyed on a `RenderRequest`. D2-12 draws two consequences and this file
// exists to make both structural rather than remembered:
//
//   1. **No pointers.** A single pointer member makes the key identity-based, and the cache then
//      misses on every lookup: nothing fails, nothing logs, and the only symptom is that scrolling
//      is slow for a reason no test can see. Every member below is a value. `DocumentHandle` is a
//      `std::uint64_t` in a struct, not a pointer to anything -- it is what lets two documents
//      share one cache without their tiles colliding (open decision **O7**).
//   2. **The `Revision` is MANDATORY, not defaulted.** A defaulted revision makes *"a stale tile
//      shown after a Stage 3 mutation"* the DEFAULT behaviour, which is why `core::Revision`'s
//      default-constructed value is deliberately invalid and why the constructor here takes one.
//      `isValid()` refuses a request that never got a real revision, so the failure is a refusal
//      rather than a wrong pixel.
//
// -------------------------------------------------------------------------------------------
// WHY THE TILE RECT IS IN DEVICE PIXELS AND THE SCALE IS SEPARATE
// -------------------------------------------------------------------------------------------
// `deviceScale` is device pixels per PDF point -- 1.0 at 72 DPI and 100 % zoom. The tile rect is
// in device pixels **within the scaled page**, so a tile is a literal pixel rectangle and its
// buffer size is `width * height * 4` with no rounding decision anywhere downstream. Carrying the
// rect in PDF space and scaling it per tile is the arrangement that produces seams: two adjacent
// tiles round their shared edge independently and disagree by a pixel, which shows as a hairline
// that moves when you zoom. `core/Geometry.h`'s `DeviceRect` is `double`-valued for layout, so the
// integral form is here rather than there.

#ifndef RAWPAGE_CORE_RENDER_RENDERREQUEST_H
#define RAWPAGE_CORE_RENDER_RENDERREQUEST_H

#include "core/Geometry.h"
#include "core/backend.h"
#include "core/doc/Revision.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace rawpage::core::render {

// What to include in the raster. Flags rather than booleans because they travel as one value into
// the cache key, and a bitmask compares and hashes as one word.
enum class RenderFlags : std::uint32_t {
    None = 0,
    Annotations = 1u << 0,   // draw annotation appearances
    FormFields = 1u << 1,    // draw form field appearances
    Grayscale = 1u << 2,     // render to grey, for print preview and low-ink modes
    NoSmoothText = 1u << 3,  // disable text antialiasing (a print path, and a golden-image lever)
    NoSmoothImage = 1u << 4,
    NoSmoothPath = 1u << 6,  // disable antialiasing on line art -- `pageDisplay/antialiasLineArt`

    // Render onto TRANSPARENT rather than onto white -- `pageDisplay/transparencyGrid`.
    //
    // IT HAS TO BE A RENDER FLAG, and finding that out is the whole story of the option. A
    // checkerboard painted behind an OPAQUE page is invisible: the rasterizer fills its bitmap with
    // white before drawing, so a transparent region of the page comes back white and covers the
    // grid completely. Drawing the grid without this flag is a feature that reads back as enabled
    // and shows nothing -- Stage 1's I-38 exactly, and the case that was written to assert the
    // grid appears is what caught it.
    //
    // Being in the flag set puts it in the KEY, which is also right: a page rendered onto
    // transparency is genuinely different pixels from the same page rendered onto white.
    TransparentBackground = 1u << 7,
    NoNativeText = 1u << 5,  // never use the OS text renderer -- required for reproducible goldens
};

constexpr RenderFlags operator|(RenderFlags a, RenderFlags b) noexcept
{
    return static_cast<RenderFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr RenderFlags operator&(RenderFlags a, RenderFlags b) noexcept
{
    return static_cast<RenderFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
constexpr bool hasFlag(RenderFlags set, RenderFlags flag) noexcept
{
    return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(flag)) != 0u;
}

// An integral pixel rectangle. Deliberately NOT `core::DeviceRect`, which is `double`-valued for
// layout: a tile is a literal buffer, and a fractional tile is not a thing a rasterizer can fill.
struct TileRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    constexpr bool isEmpty() const noexcept { return width <= 0 || height <= 0; }
    friend constexpr bool operator==(const TileRect& a, const TileRect& b) noexcept
    {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
    friend constexpr bool operator!=(const TileRect& a, const TileRect& b) noexcept
    {
        return !(a == b);
    }
};

struct RenderRequest {
    DocumentHandle document;
    int pageIndex = 0;
    TileRect tile;

    // Device pixels per PDF point. 1.0 is 72 DPI at 100 %; the reference machine's 192-DPI monitor
    // at 200 % zoom is 5.333…, which is why this is a double and why `DpiPolicy` (T10) owns the
    // arithmetic that produces it rather than each caller doing it again.
    double deviceScale = 1.0;

    // MANDATORY. A default-constructed `Revision` is invalid, so a request that never got one
    // cannot be mistaken for a valid cache key -- see the note at the top and D2-12.
    Revision revision;

    RenderFlags flags = RenderFlags::None;

    // THE VIEW ROTATION, and it is NOT the page's own (Stage 2 T14). A page carries `/Rotate` in
    // the file and the rasterizer already honours it; this is the extra quarter-turn the user asked
    // for with `Ctrl+Shift+=`, which belongs to this session and never to the document.
    //
    // It is part of the KEY because a rotated tile is different pixels. Leaving it out would mean
    // rotating the view served the unrotated tiles straight back out of the cache — a stale-tile
    // bug with no error, no failing test and no symptom except that the command appears not to
    // work. That is D2-12's argument, arriving for the first time on a field the original design
    // did not have.
    PageRotation viewRotation = PageRotation::None;

    // THE UNSAVED PAGE ROTATION, as a DELTA (Stage 3 T8, decision D3-15): the model's rotation of this
    // page minus the rotation in the ENGINE's page tree (`IDocumentBackend::pages`), which is read once at
    // open and never changes. It is a DELTA, not the page's rotation, because PDFium already applies a
    // `/Rotate` itself. **Since Stage 6 T4 the engine RELOADS the composed revision after an edit**
    // (D6-2), so the `/Rotate` it holds may be a newer one than the tree's; the rasterizer corrects for
    // the difference, and the delta keeps meaning *relative to the tree*, before and after a save.
    //
    // Part of the KEY, for `viewRotation`'s reason: the same tile at a different delta is different
    // pixels, and a tile cached before a rotation must not be served after it.
    PageRotation pageRotationDelta = PageRotation::None;

    // Layer visibility, as the set of optional-content group numbers that are OFF. Empty is the
    // ordinary case and hashes to nothing; Stage 4 populates it. It is part of the key because two
    // renders of one tile with different layers hidden are different pixels.
    std::vector<long long> hiddenLayers;

    // A request is usable when it names a real document, a non-negative page, a non-empty tile, a
    // positive scale and a REAL revision. Checked rather than assumed because every one of those
    // has a silent failure: a zero scale divides, an empty tile allocates nothing and returns a
    // valid-looking empty image, and an invalid revision is the stale-tile bug D2-12 names.
    bool isValid() const noexcept
    {
        return document.isValid() && pageIndex >= 0 && !tile.isEmpty() && deviceScale > 0.0
               && revision.isValid();
    }

    friend bool operator==(const RenderRequest& a, const RenderRequest& b) noexcept
    {
        return a.document == b.document && a.pageIndex == b.pageIndex && a.tile == b.tile
               && a.deviceScale == b.deviceScale && a.revision == b.revision && a.flags == b.flags
               && a.viewRotation == b.viewRotation && a.pageRotationDelta == b.pageRotationDelta
               && a.hiddenLayers == b.hiddenLayers;
    }
    friend bool operator!=(const RenderRequest& a, const RenderRequest& b) noexcept
    {
        return !(a == b);
    }
};

} // namespace rawpage::core::render

// Supplied beside `operator==` so the two cannot disagree. A hash that disagreed with equality
// produces a cache that misses silently -- D2-12's named failure with no symptom but slowness --
// and `core/doc/Revision.h` carries the same reasoning for the same reason.
//
// `deviceScale` is hashed through its bit pattern rather than through `std::hash<double>`'s
// treatment of -0.0 and NaN, because equality above compares it with `==`: a key that compared
// equal and hashed differently would be worse than either mistake alone.
template <>
struct std::hash<rawpage::core::render::RenderRequest> {
    std::size_t operator()(const rawpage::core::render::RenderRequest& request) const noexcept
    {
        auto mix = [](std::size_t seed, std::size_t value) noexcept {
            return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2));
        };
        std::size_t seed = std::hash<std::uint64_t>{}(request.document.id);
        seed = mix(seed, static_cast<std::size_t>(request.pageIndex));
        seed = mix(seed, static_cast<std::size_t>(request.tile.x));
        seed = mix(seed, static_cast<std::size_t>(request.tile.y));
        seed = mix(seed, static_cast<std::size_t>(request.tile.width));
        seed = mix(seed, static_cast<std::size_t>(request.tile.height));
        std::uint64_t scaleBits = 0;
        static_assert(sizeof(scaleBits) == sizeof(request.deviceScale));
        std::memcpy(&scaleBits, &request.deviceScale, sizeof(scaleBits));
        seed = mix(seed, std::hash<std::uint64_t>{}(scaleBits));
        seed = mix(seed, std::hash<rawpage::core::Revision>{}(request.revision));
        seed = mix(seed, static_cast<std::size_t>(
                          static_cast<std::uint32_t>(request.flags)));
        seed = mix(seed, static_cast<std::size_t>(
                          static_cast<int>(request.viewRotation)));
        seed = mix(seed, static_cast<std::size_t>(
                          static_cast<int>(request.pageRotationDelta)));
        for (const long long layer : request.hiddenLayers)
            seed = mix(seed, std::hash<long long>{}(layer));
        return seed;
    }
};

#endif // RAWPAGE_CORE_RENDER_RENDERREQUEST_H
