// tst_dpipolicy -- `core/render/DpiPolicy`. Stage 2 T10, guide cases A-28, A-29 and A-30.
//
// Every case here is a number a real rig cannot produce on demand: a 350 % monitor scale, a panel
// whose glass DPI disagrees with what Windows scales it by, 8 % and 6400 %. That is the argument
// for the file being pure -- `Architecture-FileMap.md` 3.4 calls it the one most likely to cause a
// blurry-text bug, and a defect that only appears on hardware nobody in the room owns is a defect
// that ships.
//
// HEADLESS BY CONSTRUCTION: `core/`, so this runs on `portable-asan` too.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/render/DpiPolicy.h"

#include <cmath>
#include <limits>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using rawpage::core::render::DpiPolicy;
using rawpage::core::render::MonitorScale;
using rawpage::core::render::PhysicalDpi;
using rawpage::core::render::Resolution;
using rawpage::core::render::Zoom;

TEST_CASE("render dpipolicy resolves Actual Size at the monitor's physical DPI", "[core][render]")
{
    const DpiPolicy policy;

    // THE CASE THE WHOLE FILE IS ABOUT. Features.md 1.2: Actual Size is 100 % at the monitor's
    // PHYSICAL DPI, not 100 % of a logical pixel. A ruler on the glass must agree with the ruler
    // drawn on the page, so a 1-inch square renders as `physicalDpi` pixels -- whatever Windows
    // happens to be scaling that monitor by.
    SECTION("the rig's two configurations")
    {
        const Resolution at100 =
            policy.resolve(Zoom::actualSize(), MonitorScale::percent(100.0), PhysicalDpi{96.0});
        CHECK_THAT(at100.dpi, WithinRel(96.0));
        CHECK_THAT(at100.devicePixelRatio, WithinRel(1.0));

        const Resolution at150 =
            policy.resolve(Zoom::actualSize(), MonitorScale::percent(150.0), PhysicalDpi{144.0});
        CHECK_THAT(at150.dpi, WithinRel(144.0));
        CHECK_THAT(at150.devicePixelRatio, WithinRel(1.5));
    }

    SECTION("175 % and 350 %, which the rig cannot produce on demand")
    {
        const Resolution at175 =
            policy.resolve(Zoom::actualSize(), MonitorScale::percent(175.0), PhysicalDpi{168.0});
        CHECK_THAT(at175.dpi, WithinRel(168.0));
        CHECK_THAT(at175.devicePixelRatio, WithinRel(1.75));

        const Resolution at350 =
            policy.resolve(Zoom::actualSize(), MonitorScale::percent(350.0), PhysicalDpi{336.0});
        CHECK_THAT(at350.dpi, WithinRel(336.0));
        CHECK_THAT(at350.devicePixelRatio, WithinRel(3.5));
    }

    SECTION("a panel whose glass DPI disagrees with its OS scale factor")
    {
        // The case a developer's own machine never produces, and the reason `resolve` takes three
        // inputs rather than two: a 157-DPI laptop panel that Windows drives at 150 %. Actual Size
        // must rasterize at 157, not at 144 -- a page laid out at 144 on this monitor is 8 % small,
        // which is exactly the amount nobody notices and everybody's ruler catches.
        const Resolution r =
            policy.resolve(Zoom::actualSize(), MonitorScale::percent(150.0), PhysicalDpi{157.0});
        CHECK_THAT(r.dpi, WithinRel(157.0));
        CHECK_THAT(r.devicePixelRatio, WithinRel(1.5));
    }

    SECTION("zoom multiplies the physical DPI and nothing else")
    {
        const Resolution r =
            policy.resolve(Zoom::percent(200.0), MonitorScale::percent(150.0), PhysicalDpi{192.0});
        CHECK_THAT(r.dpi, WithinRel(384.0));
        CHECK_THAT(r.devicePixelRatio, WithinRel(1.5));
        CHECK_FALSE(r.isCapped());
        // `rasterScale` is device pixels per PDF point, which is what `RenderRequest::deviceScale`
        // carries: 384 / 72 = 5.333..., the figure `RenderRequest.h` names for this exact rig.
        CHECK_THAT(r.rasterScale(), WithinRel(384.0 / 72.0));
    }

    SECTION("THE NEGATIVE CONTROL: the OS scale factor does not reach the raster DPI")
    {
        // Without this the case above passes against an implementation that multiplied by the
        // scale as well -- on the commodity desktop `physicalDpi == 96 * scale`, so the two are
        // indistinguishable there. Two monitors with the SAME glass and different scale factors
        // must produce identical pixels.
        const Resolution a =
            policy.resolve(Zoom::actualSize(), MonitorScale::percent(100.0), PhysicalDpi{192.0});
        const Resolution b =
            policy.resolve(Zoom::actualSize(), MonitorScale::percent(200.0), PhysicalDpi{192.0});
        CHECK_THAT(a.dpi, WithinRel(b.dpi));
        CHECK(a.devicePixelRatio != b.devicePixelRatio);
    }
}

TEST_CASE("render dpipolicy answers the same going up as coming down", "[core][render]")
{
    // A-29. Hysteresis here means a window dragged between two monitors re-rasterizes in one
    // direction and resamples in the other, so the two monitors disagree about which of them shows
    // sharp text -- and the person reporting it can only say "it depends which way I drag it".
    DpiPolicy::Limits limits;
    limits.resampleTolerance = 0.02;
    const DpiPolicy policy(limits);

    const Resolution base{144.0, 1.5, 1.0};

    SECTION("one step below the threshold is a resample, in both directions")
    {
        const Resolution justUnder{144.0 * 1.01, 1.5, 1.0};
        CHECK_FALSE(policy.requiresRerender(base, justUnder));
        CHECK_FALSE(policy.requiresRerender(justUnder, base));
    }

    SECTION("one step above it is a re-render, in both directions")
    {
        const Resolution justOver{144.0 * 1.03, 1.5, 1.0};
        CHECK(policy.requiresRerender(base, justOver));
        CHECK(policy.requiresRerender(justOver, base));
    }

    SECTION("the answer is symmetric across a large move too")
    {
        const Resolution far{336.0, 3.5, 1.0};
        CHECK(policy.requiresRerender(base, far));
        CHECK(policy.requiresRerender(far, base));
    }

    SECTION("a change of presentation alone is not a re-render")
    {
        // Same glass DPI, different OS scale factor: the pixels are identical and only the ratio
        // they are painted at differs. This is the move `resolve()`'s separation exists to make
        // free, and without the case the separation could be deleted with everything still green.
        const Resolution sameGlass{144.0, 2.0, 1.0};
        CHECK_FALSE(policy.requiresRerender(base, sameGlass));
        CHECK_FALSE(policy.requiresRerender(sameGlass, base));
    }

    SECTION("nothing rendered yet is a re-render")
    {
        CHECK(policy.requiresRerender(Resolution{0.0, 1.0, 1.0}, base));
        CHECK(policy.requiresRerender(base, Resolution{0.0, 1.0, 1.0}));
    }
}

TEST_CASE("render dpipolicy survives both zoom extremes", "[core][render]")
{
    // A-30. 6400 % of an A4 page at 192 DPI is where an integer overflow lives, and 8 % is where a
    // zero lives.
    const DpiPolicy policy;

    SECTION("8 % resolves to something a rasterizer can use")
    {
        const Resolution r =
            policy.resolve(Zoom::percent(8.0), MonitorScale::percent(150.0), PhysicalDpi{192.0});
        CHECK(r.isValid());
        CHECK(r.dpi > 0.0);
        CHECK_THAT(r.dpi, WithinRel(192.0 * 0.08));
        CHECK_FALSE(r.isCapped());
    }

    SECTION("6400 % is capped, and the cap carries its own compensation")
    {
        const Resolution r =
            policy.resolve(Zoom::percent(6400.0), MonitorScale::percent(150.0), PhysicalDpi{192.0});
        CHECK(r.isValid());
        CHECK_THAT(r.dpi, WithinRel(policy.limits().maxRenderDpi));
        CHECK(r.isCapped());

        // THE HALF THAT IS EASY TO FORGET: capping without the compensating factor silently
        // shrinks the page, which reads as a zoom bug rather than as a cap. `dpi * upscale` must
        // reproduce the request exactly.
        CHECK_THAT(r.dpi * r.upscale, WithinRel(192.0 * 64.0));
        CHECK_THAT(r.presentedScale(), WithinRel(192.0 * 64.0 / 72.0));
        CHECK(r.rasterScale() < r.presentedScale());
    }

    SECTION("an uncapped resolution presents at exactly what it rasterizes")
    {
        const Resolution r =
            policy.resolve(Zoom::percent(200.0), MonitorScale::percent(100.0), PhysicalDpi{96.0});
        CHECK_FALSE(r.isCapped());
        CHECK_THAT(r.rasterScale(), WithinRel(r.presentedScale()));
    }

    SECTION("a zoom outside the range is clamped rather than refused")
    {
        CHECK_THAT(Zoom{1000.0}.clamped().factor, WithinRel(Zoom::kMax));
        CHECK_THAT(Zoom{0.0001}.clamped().factor, WithinRel(Zoom::kMin));
        CHECK_FALSE(Zoom{1000.0}.isValid());
        CHECK(Zoom::actualSize().isValid());

        const Resolution r =
            policy.resolve(Zoom{1.0e9}, MonitorScale::percent(100.0), PhysicalDpi{96.0});
        CHECK(r.isValid());
        CHECK_THAT(r.dpi, WithinRel(policy.limits().maxRenderDpi));
    }
}

TEST_CASE("render dpipolicy never returns an unusable resolution", "[core][render]")
{
    // It is called on every paint, from a scale factor the OS supplied and a zoom a settings file
    // supplied. A zero here divides three files away, in `TileGrid`, and the stack trace names
    // neither this file nor the monitor that caused it.
    const DpiPolicy policy;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    CHECK(policy.resolve(Zoom{nan}, MonitorScale{1.0}, PhysicalDpi{96.0}).isValid());
    CHECK(policy.resolve(Zoom{1.0}, MonitorScale{0.0}, PhysicalDpi{96.0}).isValid());
    CHECK(policy.resolve(Zoom{1.0}, MonitorScale{-2.0}, PhysicalDpi{96.0}).isValid());
    CHECK(policy.resolve(Zoom{1.0}, MonitorScale{1.0}, PhysicalDpi{0.0}).isValid());
    CHECK(policy.resolve(Zoom{1.0}, MonitorScale{1.0}, PhysicalDpi{nan}).isValid());
    CHECK(policy.resolve(Zoom{inf}, MonitorScale{inf}, PhysicalDpi{inf}).isValid());

    // One bad input does not discard the good ones: a monitor with a nonsense reported DPI still
    // has a usable scale factor, and the page must still come out at the zoom that was asked for.
    const Resolution r = policy.resolve(Zoom::percent(200.0), MonitorScale::percent(150.0),
                                        PhysicalDpi{nan});
    CHECK_THAT(r.devicePixelRatio, WithinRel(1.5));
    CHECK_THAT(r.dpi, WithinRel(192.0));   // the 96-DPI fallback, times the zoom that was fine
}
