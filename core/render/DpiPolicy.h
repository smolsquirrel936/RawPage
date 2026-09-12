// core/render/DpiPolicy -- zoom, monitor scale and physical DPI in; the resolution a tile is
// rasterized at out. Stage 2 T10.
//
// `Architecture-FileMap.md` 3.4 calls this **the file most likely to be the root cause of a
// blurry-text bug**, and the reason is that all three of its inputs are plausible-looking numbers
// near 1.0 -- so every wrong combination produces a page that renders, looks nearly right, and is
// wrong by 25 % on somebody else's monitor.
//
// -------------------------------------------------------------------------------------------
// WHY THREE INPUTS AND NOT TWO
// -------------------------------------------------------------------------------------------
// **Actual Size means 100 % at the monitor's PHYSICAL DPI** (Features.md 1.2, and the plan's 5.2
// says so twice) -- not 100 % of a logical pixel. A ruler held against the glass at 100 % must
// agree with the ruler drawn on the page. So the rasterization resolution is the physical DPI
// times the zoom, and the OS scale factor does **not** enter it.
//
// The OS scale factor enters the OTHER output. Windows reports a scale (125 %, 150 %, 350 %) that
// says how many device pixels it wants per logical pixel, and Qt lays widgets out in logical
// pixels. So a tile rasterized at the physical DPI is presented with a device-pixel ratio, and
// that ratio is the scale. Two monitors with the same glass DPI and different scale factors
// therefore need **the same pixels** and a different presentation ratio -- which is why
// `requiresRerender()` compares `dpi` alone and answers *false* for that move. That is a real
// property of this design and it has a case; without the separation the same page would be
// re-rasterized every time a window crossed onto a differently-scaled monitor showing identical
// physical detail.
//
// The usual desktop has `physicalDpi == 96 * monitorScale`, and there the two collapse into the
// familiar 96-DPI logical result. The cases where they do not are exactly the ones a developer's
// own machine never produces: a 157-DPI panel that Windows drives at 150 %, a projector, a TV.
//
// -------------------------------------------------------------------------------------------
// THE CAP IS A TRADE THE USER MAKES, AND IT IS NOT FREE TO IGNORE
// -------------------------------------------------------------------------------------------
// 6400 % on a 192-DPI monitor asks for 12,288 DPI. Nothing on the display can show that detail,
// and a tile at that scale costs ~26x the work of one at 2400 DPI for pixels no eye receives. So
// `Resolution` carries an `upscale` alongside the DPI: the rasterizer produces `dpi`, the
// presenter paints the result `upscale` times larger, and the page ends up the size the user asked
// for. **Capping without the compensating factor silently shrinks the page**, which reads as a
// zoom bug -- so the two are one value type and never two arguments.
//
// PURE, and therefore tested with no display: guide cases A-28, A-29 and A-30 include a 350 %
// monitor scale and both zoom extremes, none of which a real rig produces on demand.

#ifndef RAWPAGE_CORE_RENDER_DPIPOLICY_H
#define RAWPAGE_CORE_RENDER_DPIPOLICY_H

#include <optional>
#include <string_view>

namespace rawpage::core::render {

// What the viewport is being fitted to, or `None` for a zoom the user chose outright.
//
// MOVED HERE FROM `ui/viewer/ViewportController.h` AT STAGE 2 T15, and the move is the point rather
// than tidiness: `pageDisplay/defaultZoom` is a settings key, `core/settings/SettingsKeys.def` may
// only name `core/` types, and the alternative was a second enum in `core/` listing four of these
// five values — two lists that agree until the first Friday, which is the shape `ActionId.def`'s
// rule 3 and `SettingsKeys.def`'s rule 2 both exist to prevent.
//
// It belongs beside `Zoom` in any case: a fit IS a zoom, computed from a page and a viewport
// instead of chosen, and `ui/viewer/PageLayoutEngine::zoomForFit()` is a policy question rather
// than a widget one.
//
// The four are Features.md 1.2's. As BUILT, `Page` fits the MediaBox -- the sheet -- and `Visible`
// fits the CropBox, so on a document whose CropBox equals its MediaBox they agree. **That is not
// Features.md 1.2's `Visible`**, which fits the content bounding box and ignores white margins;
// Stage 2 guide issue I-28 records the gap, and until it closes Ctrl+3 is Ctrl+0 on most files.
//
// `None` reads as "no fitting", which as a DEFAULT ZOOM means 100 % -- and that is why the settings
// row defaults to it: a settings task must not silently change what the product does.
enum class FitMode {
    None,
    Page,
    Width,
    Height,
    Visible,
};

// ASCII, stable, and **the token the settings file holds** -- `SettingsCodec<FitMode>` writes what
// this returns, so a combo box cannot offer a value the file cannot store.
const char* fitModeName(FitMode mode) noexcept;
std::optional<FitMode> fitModeFromName(std::string_view name) noexcept;


// The user's zoom, as a multiplier on Actual Size. 1.0 is 100 %.
//
// A struct rather than a bare `double` for `core/doc/Revision`'s reason: a zoom, a scale and a DPI
// are all doubles standing next to each other in every signature here, and two of them transposed
// compiles.
struct Zoom {
    double factor = 1.0;

    // The product's zoom range, from Features.md 1.2's preset ladder and guide case A-30. Values
    // outside it are CLAMPED rather than refused: a zoom arrives from a wheel gesture and from a
    // restored settings file as well as from a menu, and a viewer that refused to draw would be a
    // black window where a clamped one is a usable page.
    static constexpr double kMin = 0.08;   //    8 %
    static constexpr double kMax = 64.0;   // 6400 %

    static constexpr Zoom actualSize() noexcept { return Zoom{1.0}; }
    static constexpr Zoom percent(double p) noexcept { return Zoom{p / 100.0}; }

    bool isValid() const noexcept;
    Zoom clamped() const noexcept;

    friend constexpr bool operator==(Zoom a, Zoom b) noexcept { return a.factor == b.factor; }
    friend constexpr bool operator!=(Zoom a, Zoom b) noexcept { return !(a == b); }
};

// The OS scale factor for the monitor a tile will be shown on: device pixels per logical pixel.
// 1.0 at 100 %, 1.5 at 150 %, 3.5 at 350 %.
struct MonitorScale {
    double factor = 1.0;

    static constexpr MonitorScale percent(double p) noexcept { return MonitorScale{p / 100.0}; }
};

// The monitor's real resolution, in dots per inch of glass. `platform::IDisplayTopology` reports it
// (Stage 2 T12); until then a caller passes the OS's nominal `96 * scale`, which is what the
// commodity desktop actually has.
struct PhysicalDpi {
    double dpi = 96.0;

    static constexpr PhysicalDpi nominal(MonitorScale scale) noexcept
    {
        return PhysicalDpi{96.0 * scale.factor};
    }
};

// What `resolve()` answers: how to rasterize, and how to present what comes back.
struct Resolution {
    // Dots per inch the RASTERIZER must produce. Capped by `DpiPolicy::Limits::maxRenderDpi`.
    double dpi = 96.0;

    // Device pixels per logical pixel, for the widget that paints the result. It is the monitor's
    // OS scale factor, and it never affects a single rendered pixel -- see the header note.
    double devicePixelRatio = 1.0;

    // How much larger than `dpi` the tile must be painted, because the cap bit. 1.0 whenever it did
    // not, which is every ordinary zoom.
    double upscale = 1.0;

    bool isCapped() const noexcept { return upscale > 1.0; }

    // Device pixels per PDF point for the RASTER. This is what `TileGrid` divides a page by, and
    // what `RenderRequest::deviceScale` carries.
    double rasterScale() const noexcept { return dpi / 72.0; }

    // Device pixels per PDF point as PRESENTED. `PagePresentation` lays out with this one, so a
    // capped render still occupies exactly the space the user's zoom asked for.
    double presentedScale() const noexcept { return (dpi * upscale) / 72.0; }

    bool isValid() const noexcept;

    friend bool operator==(const Resolution& a, const Resolution& b) noexcept
    {
        return a.dpi == b.dpi && a.devicePixelRatio == b.devicePixelRatio && a.upscale == b.upscale;
    }
    friend bool operator!=(const Resolution& a, const Resolution& b) noexcept { return !(a == b); }
};

class DpiPolicy {
public:
    struct Limits {
        // The rendering resolution cap of Features.md 1.2 -- "choose a rendering resolution cap,
        // trading fidelity for responsiveness".
        //
        // DELIBERATELY FAR ABOVE ANY MONITOR: 2400 DPI is 12.5x a 192-DPI panel, so no ordinary
        // zoom on any display this product will meet is capped, and the reference machine's own A2
        // case (200 % on a 150 %-scaled monitor, 384 DPI) is nowhere near it. The cap exists for
        // the top of the zoom ladder, where the request runs to five digits.
        //
        // **This constant is the default's only home until Stage 2 T15**, which adds
        // `pageDisplay/maxRenderResolution` to `core/settings/SettingsKeys.def` and makes that row
        // the owner -- `SettingsKeys.def` rule 2 is that a default lives in exactly one place, and
        // the row must carry THIS value when it lands.
        double maxRenderDpi = 2400.0;

        // Below this relative difference a resolution change is a resample rather than a
        // re-rasterization. Compared as a RATIO of the larger to the smaller, which is what makes
        // the answer identical going up and coming down -- see `requiresRerender()`.
        double resampleTolerance = 0.02;
    };

    DpiPolicy() = default;
    explicit DpiPolicy(Limits limits) noexcept : limits_(limits) {}

    const Limits& limits() const noexcept { return limits_; }

    // Never returns a zero, a negative or a non-finite resolution, whatever it is handed. A
    // nonsense input is replaced by the documented fallback and the result stays usable: this is
    // called on every paint, from a scale factor the OS supplied and a zoom a settings file
    // supplied, and a viewer that divided by zero would be a crash on a monitor we cannot obtain.
    Resolution resolve(Zoom zoom, MonitorScale scale, PhysicalDpi physical) const noexcept;

    // Does moving from `current` to `target` require the tiles to be rasterized again, or will
    // scaling the ones already in hand do?
    //
    // TWO WAYS TO GET THIS WRONG, AND THEY LOOK NOTHING ALIKE: too eager and every window drag
    // between monitors stutters through a full re-render; too lax and text is permanently soft on
    // the monitor you did not develop on. The predicate is deliberately SYMMETRIC -- it divides the
    // larger by the smaller -- because hysteresis here means a drag that re-rasterizes one way and
    // not the other, so the same two monitors disagree about which of them is sharp.
    //
    // Compares `dpi` ONLY. A different device-pixel ratio or a different `upscale` presents the
    // same pixels differently; neither changes what the rasterizer must produce.
    bool requiresRerender(const Resolution& current, const Resolution& target) const noexcept;

private:
    Limits limits_{};
};

} // namespace rawpage::core::render

#endif // RAWPAGE_CORE_RENDER_DPIPOLICY_H
