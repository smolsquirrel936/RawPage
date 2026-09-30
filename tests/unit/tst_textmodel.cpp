// tst_textmodel -- `core/text/PageTextModel`, headless. Stage 5 T2, decision D5-2.
//
// What the model promises on its own, with no engine: an UNMAPPED character can never carry a
// codepoint a consumer might present (guide case A-21's model half), `hasExtractableText()` answers
// the question every consumer asks first, the font table deduplicates, and bold and italic come from
// the best evidence there is.
//
// WHAT THIS DOES NOT COVER (convention 19): whether PDFium sets the flags on the right characters.
// That is `tests/render/tst_extract.cpp`, over the corpus, against the generator's labels -- this
// file builds its models by hand, so it proves the model's rules and nothing about the extractor.

#include <catch2/catch_test_macros.hpp>

#include "core/text/PageTextModel.h"

using rawpage::core::PdfPoint;
using rawpage::core::PdfRect;
using rawpage::core::PdfSize;
using rawpage::core::text::CharFlag;
using rawpage::core::text::FontIdentity;
using rawpage::core::text::kReplacementCharacter;
using rawpage::core::text::PageTextModel;
using rawpage::core::text::TextChar;

namespace {

TextChar shown(char32_t codepoint, double x = 0.0)
{
    TextChar c;
    c.codepoint = codepoint;
    c.box = PdfRect{PdfPoint{x, 0.0}, PdfSize{5.0, 10.0}};
    c.origin = PdfPoint{x, 2.0};
    return c;
}

TextChar generated(char32_t codepoint)
{
    TextChar c = shown(codepoint);
    c.flags = static_cast<std::uint8_t>(CharFlag::Generated);
    return c;
}

TextChar unmapped(char32_t guess)
{
    TextChar c = shown(guess);
    c.flags = static_cast<std::uint8_t>(CharFlag::Unmapped);
    return c;
}

} // namespace

TEST_CASE("text model stores an unmapped glyph as U+FFFD whatever it arrived as", "[core][text]")
{
    // The engine's guess for a font with no /ToUnicode is usually the glyph's CODE -- here 'A',
    // which is exactly the plausible-looking character that is not in the document.
    PageTextModel model(3);
    model.append(unmapped(U'A'));
    REQUIRE(model.size() == 1);
    CHECK(model.chars()[0].codepoint == kReplacementCharacter);
    CHECK(model.chars()[0].isUnmapped());
    CHECK(model.text() == QString(QChar(0xFFFD)));
    CHECK(model.pageIndex() == 3);

    SECTION("THE CONTROL: a mapped glyph keeps its codepoint")
    {
        model.append(shown(U'A'));
        CHECK(model.chars()[1].codepoint == U'A');
    }
}

TEST_CASE("text model has no extractable text when every shown glyph is unmapped", "[core][text]")
{
    // Guide case A-21's model half: the question every consumer asks before it copies or searches.
    PageTextModel empty;
    CHECK_FALSE(empty.hasExtractableText());

    PageTextModel unmappedOnly;
    unmappedOnly.append(unmapped(U'x'));
    unmappedOnly.append(generated(U' '));
    unmappedOnly.append(unmapped(U'y'));
    CHECK_FALSE(unmappedOnly.hasExtractableText());
    CHECK(unmappedOnly.shownCount() == 2);
    CHECK(unmappedOnly.unmappedCount() == 2);

    // Generated characters are the ENGINE's, not the document's: a page of inferred line breaks
    // alone has no text either.
    PageTextModel generatedOnly;
    generatedOnly.append(generated(U'\r'));
    generatedOnly.append(generated(U'\n'));
    CHECK_FALSE(generatedOnly.hasExtractableText());

    SECTION("THE CONTROL: one mapped shown glyph makes the page extractable")
    {
        unmappedOnly.append(shown(U'z'));
        CHECK(unmappedOnly.hasExtractableText());
        CHECK(unmappedOnly.unmappedCount() == 2);
    }
}

TEST_CASE("text model builds its string with and without the generated whitespace", "[core][text]")
{
    PageTextModel model;
    model.append(shown(U'a'));
    model.append(generated(U' '));
    model.append(shown(U'b'));
    model.append(generated(U'\r'));
    model.append(generated(U'\n'));
    // A scalar above U+FFFF is ONE entry and two QChars -- appended as a QChar it would be cut.
    model.append(shown(U'\U0001F600'));

    CHECK(model.text() == QStringLiteral("a b\r\n") + QString::fromUcs4(U"\U0001F600", 1));
    CHECK(model.text(PageTextModel::Whitespace::ShownOnly)
          == QStringLiteral("ab") + QString::fromUcs4(U"\U0001F600", 1));
    CHECK(model.size() == 6);
    CHECK(model.shownCount() == 3);
}

TEST_CASE("text model deduplicates fonts and refuses an index outside its table", "[core][text]")
{
    PageTextModel model;
    const FontIdentity helvetica{QStringLiteral("Helvetica"), 32, -1};
    const FontIdentity bold{QStringLiteral("Helvetica-Bold"), 32, -1};
    CHECK(model.addFont(helvetica) == 0);
    CHECK(model.addFont(bold) == 1);
    CHECK(model.addFont(helvetica) == 0);
    CHECK(model.fonts().size() == 2);

    TextChar c = shown(U'a');
    c.font = 1;
    model.append(c);
    REQUIRE(model.fontOf(model.chars()[0]) != nullptr);
    CHECK(model.fontOf(model.chars()[0])->name == QStringLiteral("Helvetica-Bold"));

    // An index a producer got wrong is "no font", never an out-of-range read.
    TextChar stray = shown(U'b');
    stray.font = 7;
    model.append(stray);
    CHECK(model.chars()[1].font == -1);
    CHECK(model.fontOf(model.chars()[1]) == nullptr);
}

TEST_CASE("font identity says bold and italic from weight then flags then the standard name", "[core][text]")
{
    // The weight when the engine knows it.
    CHECK(FontIdentity{QStringLiteral("Anything"), 0, 700}.isBold());
    CHECK_FALSE(FontIdentity{QStringLiteral("Anything-Bold"), 0, 400}.isBold());   // weight wins

    // ForceBold (Table 123 bit 19) when it does not.
    CHECK(FontIdentity{QStringLiteral("Anything"), 1 << 18, -1}.isBold());

    // The base-14 case, measured at Stage 5 T2: no descriptor, weight unknown, no flag.
    CHECK(FontIdentity{QStringLiteral("Helvetica-Bold"), 32, -1}.isBold());
    CHECK(FontIdentity{QStringLiteral("Courier-BoldOblique"), 32, -1}.isBold());
    CHECK(FontIdentity{QStringLiteral("Courier-BoldOblique"), 32, -1}.isItalic());
    CHECK(FontIdentity{QStringLiteral("ABCDEF+Arial,BoldItalic"), 32, -1}.isBold());
    CHECK(FontIdentity{QStringLiteral("Times-Italic"), 32, -1}.isItalic());
    CHECK(FontIdentity{QStringLiteral("Anything"), 1 << 6, -1}.isItalic());

    SECTION("THE CONTROLS: a regular face, and a FAMILY whose name merely contains the word")
    {
        CHECK_FALSE(FontIdentity{QStringLiteral("Helvetica"), 32, -1}.isBold());
        CHECK_FALSE(FontIdentity{QStringLiteral("Helvetica"), 32, -1}.isItalic());
        // No style part at all: "Boldface" is a family, not a weight.
        CHECK_FALSE(FontIdentity{QStringLiteral("Boldface"), 32, -1}.isBold());
        CHECK_FALSE(FontIdentity{QStringLiteral("ABCDEF+Italiana"), 32, -1}.isItalic());
    }
}
