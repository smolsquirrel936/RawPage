// `core/doc/Metadata` -- the Info dictionary's model and its date parsing, headless. Stage 4 T4,
// decision D4-4; guide case A-28.
//
// The engine half -- that `FPDF_GetMetaText` fills these fields from a real file, and that a
// document with no `/Info` answers empty rather than failing -- is
// `tests/render/tst_docmodels.cpp`'s, over `corpus/minimal.pdf`, which carries no `/Info` at all.

#include "core/doc/Metadata.h"

#include <catch2/catch_test_macros.hpp>

using rawpage::core::Metadata;

TEST_CASE("an absent Info dictionary is empty rather than an error", "[metadata]")
{
    const Metadata metadata;
    REQUIRE(metadata.isEmpty());
    REQUIRE(metadata.title.isEmpty());

    Metadata withOne;
    withOne.producer = QStringLiteral("RawPage corpus generator");
    REQUIRE_FALSE(withOne.isEmpty());
}

TEST_CASE("the PDF version is not an Info field and is empty when unknown", "[metadata]")
{
    // It comes from the file header, and a document whose header the engine could not read leaves
    // it empty rather than claiming a version. `isEmpty()` is about the INFO DICTIONARY, so a
    // version alone does not make the metadata non-empty.
    Metadata metadata;
    metadata.pdfVersion = QStringLiteral("1.7");
    REQUIRE(metadata.isEmpty());
}

TEST_CASE("a PDF date parses in the forms producers actually write", "[metadata]")
{
    SECTION("the full form with a UTC offset")
    {
        const auto parsed = Metadata::parseDate(QStringLiteral("D:20260831000000Z"));
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->date() == QDate(2026, 8, 31));
        REQUIRE(parsed->time() == QTime(0, 0, 0));
    }
    SECTION("a positive offset is applied rather than ignored")
    {
        const auto parsed = Metadata::parseDate(QStringLiteral("D:20260919143000+08'00'"));
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->date() == QDate(2026, 9, 19));
        REQUIRE(parsed->time() == QTime(14, 30, 0));
        REQUIRE(parsed->offsetFromUtc() == 8 * 3600);
    }
    SECTION("a negative offset")
    {
        const auto parsed = Metadata::parseDate(QStringLiteral("D:20260919143000-05'30'"));
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->offsetFromUtc() == -(5 * 3600 + 30 * 60));
    }
    SECTION("no D: prefix, which plenty of producers omit")
    {
        REQUIRE(Metadata::parseDate(QStringLiteral("20260831120000Z")).has_value());
    }
    SECTION("a truncated date -- the year alone is legal")
    {
        const auto parsed = Metadata::parseDate(QStringLiteral("D:2026"));
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->date() == QDate(2026, 1, 1));
    }
    SECTION("no offset at all")
    {
        REQUIRE(Metadata::parseDate(QStringLiteral("D:20260831120000")).has_value());
    }
}

TEST_CASE("a malformed date does not parse and the stored text survives", "[metadata]")
{
    // **THE CASE A-28 IS ABOUT.** A date the product invented would be a fact about the document
    // that the document does not contain, so the answer is *no date* and the raw string is what a
    // properties dialog shows.
    Metadata metadata;
    metadata.creationDate = QStringLiteral("Tuesday, some time in the afternoon");
    REQUIRE_FALSE(Metadata::parseDate(metadata.creationDate).has_value());
    REQUIRE(metadata.creationDate == QStringLiteral("Tuesday, some time in the afternoon"));
    REQUIRE_FALSE(metadata.isEmpty());   // it is still a value the document carries

    REQUIRE_FALSE(Metadata::parseDate(QStringLiteral("D:20261345000000Z")).has_value());  // month 13
    REQUIRE_FALSE(Metadata::parseDate(QStringLiteral("D:20260231")).has_value());         // 31 Feb
    REQUIRE_FALSE(Metadata::parseDate(QStringLiteral("D:20260831256000Z")).has_value());  // hour 25
    REQUIRE_FALSE(Metadata::parseDate(QString{}).has_value());
    REQUIRE_FALSE(Metadata::parseDate(QStringLiteral("D:")).has_value());
}

TEST_CASE("the parser does not accept something merely date-shaped", "[metadata]")
{
    // The control for the case above: a parser lenient enough to accept anything would make
    // "malformed" unreachable, and every malformed date would silently become a wrong one.
    REQUIRE_FALSE(Metadata::parseDate(QStringLiteral("2026-08-31")).has_value());
    REQUIRE_FALSE(Metadata::parseDate(QStringLiteral("D:20260831T120000")).has_value());
    REQUIRE_FALSE(Metadata::parseDate(QStringLiteral("D:20260831000000Z junk")).has_value());
}
