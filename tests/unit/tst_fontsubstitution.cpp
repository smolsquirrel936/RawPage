// tst_fontsubstitution -- the substitution POLICY, with no display, no Windows and no engine.
// Stage 2 T16, and the headless half of guide case A-66.
//
// **THIS FILE IS WHY THE POLICY IS IN core/ AT ALL.** `platform/win/` makes no policy decisions, so
// the ordered list of families to try lives in `core/render/FontSubstitution` and
// `WinFontResolver` only walks it against what the machine has. The consequence is that A-66's
// claim -- *the substitute is chosen by OUR policy rather than by the engine's* -- is asserted here,
// in the suite that `rawpage_assert_headless()` guarantees links no engine and no OS layer, rather
// than only end to end where a passing run could also mean "the machine happened to have it".
//
// WHAT THIS SUITE DOES NOT COVER (convention 19): whether any of these families is INSTALLED. Every
// name below is a string this file compares against another string. `tests/platform/tst_fontresolver`
// asks the machine, and `tests/render/tst_fontbridge` drives the whole chain through PDFium.

#include <catch2/catch_test_macros.hpp>

#include "core/render/FontSubstitution.h"

using rawpage::core::render::isStandardFontName;
using rawpage::core::render::lastResortFamily;
using rawpage::core::render::normalizeBaseFontName;
using rawpage::core::render::substitutionCandidates;
using rawpage::platform::IFontResolver;

namespace {

IFontResolver::Request request(bool serif = false, bool fixedPitch = false, bool symbolic = false,
                               QString languageHint = {})
{
    IFontResolver::Request out;
    out.family = QStringLiteral("SomethingNobodyHas");
    out.serif = serif;
    out.fixedPitch = fixedPitch;
    out.symbolic = symbolic;
    out.languageHint = std::move(languageHint);
    return out;
}

} // namespace

// ==========================================================================================
// The shape of every answer
// ==========================================================================================

TEST_CASE("every candidate list is non-empty and ends with the last resort", "[unit][font]")
{
    // A resolver walks this list and gives up at the end of it, so an empty list is a document
    // rendered in the engine's default with no record of why -- and a list not ending in a face
    // that is on every Windows machine is the same failure on a stripped install.
    for (const auto& probe : {request(), request(true), request(false, true), request(false, false, true),
                              request(true, false, false, QStringLiteral("zh-TW")),
                              request(false, false, false, QStringLiteral("ja"))}) {
        const QStringList candidates = substitutionCandidates(probe);
        REQUIRE_FALSE(candidates.isEmpty());
        CHECK(candidates.last() == lastResortFamily());
    }
}

TEST_CASE("a candidate list never repeats a family", "[unit][font]")
{
    // The Han lists deliberately name a face twice -- PMingLiU is both the serif first choice and
    // the fallback after Microsoft JhengHei -- so de-duplication is doing real work rather than
    // guarding against a typo. A repeat costs a wasted OS query per occurrence, and worse, it makes
    // "we tried five candidates" a number that does not match the list.
    const QStringList candidates =
        substitutionCandidates(request(true, false, false, QStringLiteral("zh-Hant")));
    QStringList seen;
    for (const QString& family : candidates) {
        INFO("duplicate: " << family.toStdString());
        REQUIRE_FALSE(seen.contains(family, Qt::CaseInsensitive));
        seen.append(family);
    }
}

TEST_CASE("the candidate list depends on the request's attributes and not on its name",
          "[unit][font]")
{
    // WRITTEN TO ASSERT THE OPPOSITE, AND CORRECTED BY MEASURING IT. The first version of this case
    // asserted that the requested family is never in its own candidate list -- asking for it is the
    // resolver's first move, so repeating it looked redundant. It is in the list, and it has to be:
    // the guarantee that every list ENDS WITH the last resort is the one a resolver depends on, and
    // a filter that removed the requested name would empty the list for a document that asks for
    // Arial on a machine without it. Keeping it costs one lookup already known to miss.
    IFontResolver::Request named = request(true);
    named.family = QStringLiteral("Georgia");   // a real family that IS in the serif list
    IFontResolver::Request anonymous = request(true);
    anonymous.family = QStringLiteral("SomethingNobodyHas");

    CHECK(substitutionCandidates(named) == substitutionCandidates(anonymous));
    CHECK(substitutionCandidates(named).contains(QStringLiteral("Georgia")));
}

// ==========================================================================================
// The three branches, and the order between them
// ==========================================================================================

TEST_CASE("serif and sans requests differ in their first choice", "[unit][font]")
{
    // The assertion that makes A-66's end-to-end case meaningful: the fixture's two fonts differ in
    // exactly one /Flags bit, so a policy that answered the same thing for both would satisfy every
    // other check while proving the descriptor was never read.
    CHECK(substitutionCandidates(request(true)).first()
          != substitutionCandidates(request(false)).first());
    CHECK(substitutionCandidates(request(true)).first() == QLatin1String("Times New Roman"));
    CHECK(substitutionCandidates(request(false)).first() == QLatin1String("Segoe UI"));
}

TEST_CASE("a symbolic request never falls through to a text face first", "[unit][font]")
{
    // A symbolic font's byte values are not characters. Answering it with Arial draws Latin letters
    // where the document meant arrows -- legible, and wrong in a way that reads as our defect rather
    // than as a missing font. Arial is still last, because a wrong glyph beats no glyph.
    const QStringList candidates = substitutionCandidates(request(false, false, true));
    CHECK(candidates.first() == QLatin1String("Segoe UI Symbol"));
    CHECK(candidates.indexOf(QStringLiteral("Wingdings")) < candidates.indexOf(lastResortFamily()));
}

TEST_CASE("symbolic beats fixed pitch, and fixed pitch beats serif", "[unit][font]")
{
    // The order of the three tests IS the policy, so it is asserted rather than left to the reading
    // order of an if-chain that somebody may reorder for tidiness.
    IFontResolver::Request everything = request(true, true, true);
    CHECK(substitutionCandidates(everything).first() == QLatin1String("Segoe UI Symbol"));

    everything.symbolic = false;
    CHECK(substitutionCandidates(everything).first() == QLatin1String("Consolas"));

    everything.fixedPitch = false;
    CHECK(substitutionCandidates(everything).first() == QLatin1String("Times New Roman"));
}

// ==========================================================================================
// Han disambiguation -- the branch where being wrong is visible to a reader
// ==========================================================================================

TEST_CASE("the four Han regions get four different first choices", "[unit][font]")
{
    // The same code point is drawn with different strokes in Simplified Chinese, Traditional
    // Chinese, Japanese and Korean. A reader does not call the wrong one "slightly off"; they call
    // it broken. This is the only place in the render path where a language tag changes pixels.
    const QString simplified = substitutionCandidates(
        request(false, false, false, QStringLiteral("zh-CN"))).first();
    const QString traditional = substitutionCandidates(
        request(false, false, false, QStringLiteral("zh-TW"))).first();
    const QString japanese = substitutionCandidates(
        request(false, false, false, QStringLiteral("ja"))).first();
    const QString korean = substitutionCandidates(
        request(false, false, false, QStringLiteral("ko"))).first();

    CHECK(simplified == QLatin1String("Microsoft YaHei"));
    CHECK(traditional == QLatin1String("Microsoft JhengHei"));
    CHECK(japanese == QLatin1String("Yu Gothic"));
    CHECK(korean == QLatin1String("Malgun Gothic"));
}

TEST_CASE("the script subtag wins over the region, and a bare zh is Simplified", "[unit][font]")
{
    // "zh-Hant" carries no region and is still Traditional; "zh-Hant-CN" is a real tag and the
    // script is the half that decides. A resolver that matched on "CN" would answer Simplified for
    // a document that said otherwise in as many words.
    CHECK(substitutionCandidates(request(false, false, false, QStringLiteral("zh-Hant"))).first()
          == QLatin1String("Microsoft JhengHei"));
    CHECK(substitutionCandidates(request(false, false, false, QStringLiteral("zh-Hant-CN"))).first()
          == QLatin1String("Microsoft JhengHei"));
    CHECK(substitutionCandidates(request(false, false, false, QStringLiteral("zh"))).first()
          == QLatin1String("Microsoft YaHei"));
}

TEST_CASE("a CJK serif request tries a Ming face before a Gothic one", "[unit][font]")
{
    const QStringList candidates =
        substitutionCandidates(request(true, false, false, QStringLiteral("zh-TW")));
    CHECK(candidates.first() == QLatin1String("PMingLiU"));
    CHECK(candidates.indexOf(QStringLiteral("PMingLiU"))
          < candidates.indexOf(QStringLiteral("Microsoft JhengHei")));
}

TEST_CASE("a fixed-pitch CJK request prefers COVERAGE over a fixed advance", "[unit][font]")
{
    // Consolas has no Han glyphs. A monospaced Chinese document rendered in it is a page of boxes,
    // which is worse than a proportional page of the right characters -- so the Han candidates come
    // first even inside the fixed-pitch branch, and this is where that choice is written down.
    const QStringList candidates =
        substitutionCandidates(request(false, true, false, QStringLiteral("ja")));
    CHECK(candidates.indexOf(QStringLiteral("Yu Gothic"))
          < candidates.indexOf(QStringLiteral("Consolas")));
}

TEST_CASE("a non-CJK language hint changes nothing", "[unit][font]")
{
    // The control for every case above: "de-DE" and "fr" are language tags too, and a policy that
    // treated any non-empty hint as CJK would answer Microsoft YaHei for a German document.
    CHECK(substitutionCandidates(request(false, false, false, QStringLiteral("de-DE")))
          == substitutionCandidates(request(false)));
    CHECK(substitutionCandidates(request(false, false, false, QString()))
          == substitutionCandidates(request(false)));
}

// ==========================================================================================
// The base-14 decline -- issue I-13
// ==========================================================================================

TEST_CASE("the five standard families are declined and their look-alikes are not", "[unit][font]")
{
    // PDFium asks the font hook about EVERY unembedded font, standard ones included, and uses its
    // own metric-compatible copies only when the hook declines. Before this predicate existed the
    // product substituted Segoe UI for Helvetica on every page of every document (issue I-13).
    CHECK(isStandardFontName(QStringLiteral("Helvetica")));
    CHECK(isStandardFontName(QStringLiteral("Times")));
    CHECK(isStandardFontName(QStringLiteral("Courier")));
    CHECK(isStandardFontName(QStringLiteral("Symbol")));
    CHECK(isStandardFontName(QStringLiteral("ZapfDingbats")));
    CHECK(isStandardFontName(QStringLiteral("helvetica")));   // /BaseFont casing is not guaranteed

    // THE OTHER HALF, and it is the one that would be easy to get wrong: Arial, Times New Roman and
    // Courier New are REAL INSTALLED FAMILIES, not standard-14 aliases. Declining them would refuse
    // a font we can serve exactly, and hand the page to the engine's approximation instead.
    CHECK_FALSE(isStandardFontName(QStringLiteral("Arial")));
    CHECK_FALSE(isStandardFontName(QStringLiteral("TimesNewRoman")));
    CHECK_FALSE(isStandardFontName(QStringLiteral("Courier New")));
    CHECK_FALSE(isStandardFontName(QStringLiteral("Garamond")));
}

TEST_CASE("a styled standard face normalizes onto its standard family", "[unit][font]")
{
    // The decline is checked against the NORMALIZED name, so "Times-BoldItalic" has to reduce to
    // "Times" for the branch to fire. If it did not, three quarters of the base-14 set would be
    // substituted while the fourth passed the case above.
    for (const char* name : {"Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic",
                             "Helvetica-Oblique", "Helvetica-BoldOblique", "Courier-Bold"}) {
        INFO(name);
        CHECK(isStandardFontName(normalizeBaseFontName(QString::fromLatin1(name))));
    }
}

// ==========================================================================================
// /BaseFont normalization
// ==========================================================================================

TEST_CASE("a subset tag is stripped and a family containing a plus is not", "[unit][font]")
{
    CHECK(normalizeBaseFontName(QStringLiteral("ABCDEF+Garamond")) == QLatin1String("Garamond"));

    // Anchored, six capitals exactly. "Foo+Bar" is a family whose name contains a plus, not a
    // subset of "Bar", and five or seven capitals is not the tag ISO 32000-1 9.6.4 defines.
    CHECK(normalizeBaseFontName(QStringLiteral("Foo+Bar")) == QLatin1String("Foo+Bar"));
    CHECK(normalizeBaseFontName(QStringLiteral("ABCDE+Garamond")) == QLatin1String("ABCDE+Garamond"));
    CHECK(normalizeBaseFontName(QStringLiteral("ABCDEFG+Garamond"))
          == QLatin1String("ABCDEFG+Garamond"));
}

TEST_CASE("the comma form and the PostScript suffixes are stripped", "[unit][font]")
{
    CHECK(normalizeBaseFontName(QStringLiteral("Arial,BoldItalic")) == QLatin1String("Arial"));
    CHECK(normalizeBaseFontName(QStringLiteral("TimesNewRomanPSMT"))
          == QLatin1String("TimesNewRoman"));
    CHECK(normalizeBaseFontName(QStringLiteral("ArialMT")) == QLatin1String("Arial"));
}

TEST_CASE("a doubled style suffix is stripped in full", "[unit][font]")
{
    // "Garamond-Bold-Italic" is real, and one pass leaves "-Bold" behind -- which then fails to
    // match any installed family and falls through to a substitute for a font that was present.
    CHECK(normalizeBaseFontName(QStringLiteral("Garamond-Bold-Italic")) == QLatin1String("Garamond"));
    CHECK(normalizeBaseFontName(QStringLiteral("Garamond-BoldItalic")) == QLatin1String("Garamond"));
}

TEST_CASE("a leading slash is dropped and a hyphenated family keeps its hyphen", "[unit][font]")
{
    CHECK(normalizeBaseFontName(QStringLiteral("/Helvetica")) == QLatin1String("Helvetica"));

    // The control for the style stripping: the part after the hyphen is not a style word, so the
    // name survives whole. A rule that cut at the first hyphen would turn every such family into a
    // request for a font nobody has.
    CHECK(normalizeBaseFontName(QStringLiteral("Frutiger-Next")) == QLatin1String("Frutiger-Next"));
}
