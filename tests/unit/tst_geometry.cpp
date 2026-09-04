#include "core/Geometry.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <type_traits>

using namespace rawpage::core;

// =======================================================================================
// THE MOST IMPORTANT TESTS IN THIS FILE ARE THE COMPILE-TIME NEGATIVES.
//
// A runtime test can only check that a conversion we DID write is correct. These check that
// the conversion we must never write cannot be written at all. A silent PDF-point/device-
// pixel mix-up manifests as "slightly wrong at 150% scaling" -- correct on the author's
// monitor, wrong on the user's -- and is the most common bug class in this domain.
//
// If any static_assert below stops holding, the type separation has been dismantled and
// every runtime test in this file is exercising a system that no longer has the property
// it was built for.
// =======================================================================================

static_assert(!std::is_convertible_v<PdfPoint, DevicePoint>);
static_assert(!std::is_convertible_v<DevicePoint, PdfPoint>);
static_assert(!std::is_convertible_v<PdfRect, DeviceRect>);
static_assert(!std::is_convertible_v<DeviceRect, PdfRect>);
static_assert(!std::is_convertible_v<PdfSize, DeviceSize>);
static_assert(!std::is_convertible_v<DeviceSize, PdfSize>);

// Nor by construction: a converting constructor defeats the point just as thoroughly as an
// implicit conversion operator.
static_assert(!std::is_constructible_v<DevicePoint, PdfPoint>);
static_assert(!std::is_constructible_v<PdfPoint, DevicePoint>);

// And they share no base, so nothing can be laundered through a pointer to one.
static_assert(!std::is_base_of_v<PdfPoint, DevicePoint>);
static_assert(!std::is_base_of_v<DevicePoint, PdfPoint>);

namespace {
constexpr PdfSize kA4{595.276, 841.89};
}

TEST_CASE("scale combines dpi and zoom", "[tst_geometry]")
{
    CHECK(PageTransform(kA4, 1.0, 72.0).scale() == 1.0);
    CHECK(PageTransform(kA4, 1.0, 144.0).scale() == 2.0);
    CHECK(PageTransform(kA4, 2.0, 96.0).scale() == (96.0 / 72.0) * 2.0);
}

TEST_CASE("the y axis flips between the two spaces", "[tst_geometry]")
{
    const PageTransform t(kA4, 1.0, 72.0);

    // PDF origin is the BOTTOM-left; device origin is the TOP-left. So the bottom-left
    // corner of the page must land at the bottom of the device rect, not at (0,0).
    const DevicePoint bottomLeft = t.toDevice(PdfPoint{0.0, 0.0});
    CHECK(bottomLeft.x == 0.0);
    CHECK(bottomLeft.y == kA4.height);

    const DevicePoint topLeft = t.toDevice(PdfPoint{0.0, kA4.height});
    CHECK(topLeft.x == 0.0);
    CHECK(topLeft.y == 0.0);
}

TEST_CASE("device and pdf conversions are inverses", "[tst_geometry]")
{
    for (double zoom : {0.08, 1.0, 4.0, 64.0}) {
        for (double dpi : {96.0, 144.0, 192.0}) {
            for (PageRotation rotation : {PageRotation::None, PageRotation::Clockwise90,
                                          PageRotation::Half, PageRotation::Clockwise270}) {
                const PageTransform t(kA4, zoom, dpi, rotation);
                const PdfPoint source{123.5, 456.25};
                const PdfPoint round = t.toPdf(t.toDevice(source));
                CHECK(std::abs(round.x - source.x) < 1e-6);
                CHECK(std::abs(round.y - source.y) < 1e-6);
            }
        }
    }
}

TEST_CASE("rotation swaps the device extent", "[tst_geometry]")
{
    const PageTransform upright(kA4, 1.0, 72.0, PageRotation::None);
    const PageTransform turned(kA4, 1.0, 72.0, PageRotation::Clockwise90);

    CHECK(upright.deviceSize().width == kA4.width);
    CHECK(upright.deviceSize().height == kA4.height);
    CHECK(turned.deviceSize().width == kA4.height);
    CHECK(turned.deviceSize().height == kA4.width);
}

TEST_CASE("a rect keeps positive extents under every rotation", "[tst_geometry]")
{
    // Converting origin+size directly would give negative width or height at 90 and 270,
    // because the PDF bottom-left does not stay the device top-left once the page turns.
    const PdfRect box{PdfPoint{100.0, 200.0}, PdfSize{50.0, 25.0}};

    for (PageRotation rotation : {PageRotation::None, PageRotation::Clockwise90,
                                  PageRotation::Half, PageRotation::Clockwise270}) {
        const PageTransform t(kA4, 1.5, 96.0, rotation);
        const DeviceRect device = t.toDevice(box);
        CHECK(device.size.width > 0.0);
        CHECK(device.size.height > 0.0);
    }
}

TEST_CASE("rect containment respects each axis direction", "[tst_geometry]")
{
    const PdfRect pdf{PdfPoint{10.0, 10.0}, PdfSize{100.0, 100.0}};
    CHECK(pdf.contains(PdfPoint{50.0, 50.0}));
    CHECK_FALSE(pdf.contains(PdfPoint{5.0, 50.0}));
    CHECK(pdf.top() == 110.0);
    CHECK(pdf.bottom() == 10.0);

    const DeviceRect device{DevicePoint{10.0, 10.0}, DeviceSize{100.0, 100.0}};
    CHECK(device.contains(DevicePoint{50.0, 50.0}));
    // In device space "top" is the SMALLER y -- the opposite of the PDF rect above.
    CHECK(device.top() == 10.0);
    CHECK(device.bottom() == 110.0);
}

TEST_CASE("page geometry can be built from user-facing units", "[tst_geometry]")
{
    const PdfSize a4 = pdfSizeFrom(Length::fromMillimetres(210.0), Length::fromMillimetres(297.0));
    CHECK(std::abs(a4.width - 595.2755905511812) < 1e-9);
    CHECK(std::abs(a4.height - 841.8897637795277) < 1e-9);
}

// =============================================================================================
// Rotation arithmetic -- Stage 2 T14, for the VIEW rotation (`Ctrl+Shift+=`).
// =============================================================================================

TEST_CASE("rotateBy is correct for negative turns, which is the half that is easy to get wrong",
          "[geometry]")
{
    using rawpage::core::PageRotation;
    using rawpage::core::rotateBy;

    CHECK(rotateBy(PageRotation::None, 1) == PageRotation::Clockwise90);
    CHECK(rotateBy(PageRotation::Clockwise90, 1) == PageRotation::Half);
    CHECK(rotateBy(PageRotation::Clockwise270, 1) == PageRotation::None);

    // C++'s `%` yields a NEGATIVE remainder for a negative left operand, so a bare `% 4` turns one
    // press of Rotate Counter-Clockwise into an out-of-range value -- which is a `switch` falling
    // through to `None` and a command that silently does nothing in one direction only.
    CHECK(rotateBy(PageRotation::None, -1) == PageRotation::Clockwise270);
    CHECK(rotateBy(PageRotation::Clockwise90, -1) == PageRotation::None);
    CHECK(rotateBy(PageRotation::None, -5) == PageRotation::Clockwise270);

    // Four turns in either direction is the identity, from every starting point.
    for (const PageRotation from : {PageRotation::None, PageRotation::Clockwise90,
                                    PageRotation::Half, PageRotation::Clockwise270}) {
        CHECK(rotateBy(from, 4) == from);
        CHECK(rotateBy(from, -4) == from);
        CHECK(rotateBy(from, 0) == from);
    }
}

TEST_CASE("combine adds two rotations without ever producing 450 degrees", "[geometry]")
{
    using rawpage::core::PageRotation;
    using rawpage::core::combine;
    using rawpage::core::swapsAxes;

    // THE TRAP THIS FUNCTION EXISTS FOR: a page carries its own `/Rotate` and the view may be
    // turned on top of it, and adding the degrees gives 270 + 180 = 450, which is not a
    // `PageRotation`. Stage 2 T14 is the first place two rotations meet.
    CHECK(combine(PageRotation::Clockwise270, PageRotation::Half) == PageRotation::Clockwise90);
    CHECK(combine(PageRotation::Clockwise90, PageRotation::Clockwise270) == PageRotation::None);
    CHECK(combine(PageRotation::None, PageRotation::Half) == PageRotation::Half);
    CHECK(combine(PageRotation::Half, PageRotation::Half) == PageRotation::None);

    // Commutative, which it must be: a landscape page in a rotated view and a rotated view over a
    // landscape page are the same thing on screen.
    for (const PageRotation a : {PageRotation::None, PageRotation::Clockwise90,
                                 PageRotation::Half, PageRotation::Clockwise270}) {
        for (const PageRotation b : {PageRotation::None, PageRotation::Clockwise90,
                                     PageRotation::Half, PageRotation::Clockwise270}) {
            CHECK(combine(a, b) == combine(b, a));
        }
    }

    CHECK(swapsAxes(PageRotation::Clockwise90));
    CHECK(swapsAxes(PageRotation::Clockwise270));
    CHECK_FALSE(swapsAxes(PageRotation::None));
    CHECK_FALSE(swapsAxes(PageRotation::Half));
}
