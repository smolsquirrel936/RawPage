#include "core/Units.h"

#include <catch2/catch_test_macros.hpp>

using namespace recto::core;

TEST_CASE("the defining ratios are exact", "[tst_units]")
{
    // These four are definitions, not measurements. If any of them drifts, every ruler,
    // margin and paper size in the product is wrong by a constant factor.
    CHECK(Length::fromInches(1.0).points() == 72.0);
    CHECK(Length::fromPicas(1.0).points() == 12.0);
    CHECK(Length::fromInches(1.0).nearlyEquals(Length::fromMillimetres(25.4)));
    CHECK(Length::fromCentimetres(1.0).nearlyEquals(Length::fromMillimetres(10.0)));
}

TEST_CASE("A4 and US Letter come out at their documented point sizes", "[tst_units]")
{
    CHECK(Length::fromMillimetres(210.0).nearlyEquals(Length::fromPoints(595.2755905511812)));
    CHECK(Length::fromInches(8.5).points() == 612.0);
    CHECK(Length::fromInches(11.0).points() == 792.0);
}

TEST_CASE("every unit round-trips", "[tst_units]")
{
    const Length source = Length::fromPoints(123.456);
    for (Unit unit : {Unit::Points, Unit::Inches, Unit::Millimetres, Unit::Centimetres, Unit::Picas}) {
        const Length round = Length::from(source.in(unit), unit);
        CHECK(round.nearlyEquals(source, 1e-9));
    }
}

TEST_CASE("arithmetic stays in points", "[tst_units]")
{
    const Length a = Length::fromInches(1.0);
    const Length b = Length::fromPoints(36.0);
    CHECK((a + b).points() == 108.0);
    CHECK((a - b).points() == 36.0);
    CHECK((a * 2.0).points() == 144.0);
    CHECK((a / 2.0).points() == 36.0);
    CHECK((-a).points() == -72.0);
    CHECK(b < a);
}

TEST_CASE("parsing accepts a suffix, a space, or neither", "[tst_units]")
{
    Length out;
    REQUIRE(parseLength(QStringLiteral("12"), Unit::Millimetres, out));
    CHECK(out.nearlyEquals(Length::fromMillimetres(12.0)));

    REQUIRE(parseLength(QStringLiteral("1.5in"), Unit::Millimetres, out));
    CHECK(out.nearlyEquals(Length::fromInches(1.5)));

    REQUIRE(parseLength(QStringLiteral("  2 cm "), Unit::Points, out));
    CHECK(out.nearlyEquals(Length::fromCentimetres(2.0)));

    REQUIRE(parseLength(QStringLiteral("10PT"), Unit::Inches, out));
    CHECK(out.nearlyEquals(Length::fromPoints(10.0)));
}

TEST_CASE("parsing refuses garbage rather than guessing", "[tst_units]")
{
    Length out;
    CHECK_FALSE(parseLength(QStringLiteral(""), Unit::Points, out));
    CHECK_FALSE(parseLength(QStringLiteral("   "), Unit::Points, out));
    CHECK_FALSE(parseLength(QStringLiteral("wide"), Unit::Points, out));
    CHECK_FALSE(parseLength(QStringLiteral("12 furlongs"), Unit::Points, out));
    CHECK_FALSE(parseLength(QStringLiteral("mm"), Unit::Points, out));
}

TEST_CASE("formatting carries the unit symbol", "[tst_units]")
{
    CHECK(formatLength(Length::fromInches(1.0), Unit::Points, 0) == QStringLiteral("72pt"));
    CHECK(formatLength(Length::fromInches(1.0), Unit::Millimetres, 1) == QStringLiteral("25.4mm"));
}
