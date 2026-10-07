// tst_typedtext -- Stage 6 T7 headless: typed text below every widget and every engine
// (`Stage6-Annotations-Testing-Guide.md` §5.9; A-56's rule half; D6-7, D6-34).
//
//   * WHICH FACE draws each character: WinAnsi in Helvetica; kana in Japanese and Hangul in Korean
//     whatever surrounds them; an ideograph in the run's language -- the Han preference -- and, where that
//     collection lacks it, the first that has it. The first CJK run decides the PRIMARY face `/DA` names.
//   * WHAT IS REFUSED, by name: Greek, Hebrew, Cyrillic, anything past the BMP -- and nothing A-56 types.
//   * WHERE LINES BREAK: at a space after a word, between CJK characters, inside a word only when it is
//     wider than the line; a soft break keeps its space; a block of n lines is n x 1.2 ems.
//   * `/DA` read and written; the free-text FRAME at each quarter turn; a box, a typewriter and a callout
//     FITTED to their text.
//
// WHAT IT DOES NOT COVER (convention 19): whether the ENGINE draws what the coverage table claims, or sets
// Helvetica on these advances -- `tests/render/tst_typedtext.cpp` holds both to PDFium -- and whether the
// appearance written says what was typed, which `tst_annotfreetext.cpp` decodes from a saved file (A-57).

#include <catch2/catch_test_macros.hpp>

#include "core/doc/TypedText.h"

#include <QStringList>

#include <cmath>

using namespace rawpage;
using core::TypedFont;
using core::TypedLayout;

namespace {

bool near(double a, double b, double tolerance = 1e-6)
{
    return std::abs(a - b) <= tolerance;
}

TypedLayout laid(const QString& text, std::optional<double> wrap = std::nullopt,
                 TypedFont han = TypedFont::Simplified, double size = 10.0)
{
    core::Result<TypedLayout> layout = core::layoutTypedText(text, size, wrap, han);
    INFO(text.toStdString());
    REQUIRE(layout);
    return std::move(layout).value();
}

// Every run of every line, as (face, text).
std::vector<std::pair<TypedFont, QString>> runsOf(const TypedLayout& layout)
{
    std::vector<std::pair<TypedFont, QString>> out;
    for (const core::TypedLine& line : layout.lines) {
        for (const core::TypedRun& run : line.runs) out.emplace_back(run.font, run.text);
    }
    return out;
}

QString joined(const TypedLayout& layout)
{
    QString out;
    for (const core::TypedLine& line : layout.lines) {
        for (const core::TypedRun& run : line.runs) out += run.text;
    }
    return out;
}

} // namespace

// ==========================================================================================
// Characters
// ==========================================================================================

TEST_CASE("WinAnsi holds Latin-1 and code page 1252's extra characters and nothing else", "[typedtext][A-56]")
{
    CHECK(core::winAnsiCode(U'A') == std::optional<std::uint8_t>(0x41));
    CHECK(core::winAnsiCode(U'é') == std::optional<std::uint8_t>(0xE9));   // e acute
    CHECK(core::winAnsiCode(U'€') == std::optional<std::uint8_t>(0x80));   // the euro
    CHECK(core::winAnsiCode(U'“') == std::optional<std::uint8_t>(0x93));   // the left double quote
    CHECK(core::winAnsiCode(U'—') == std::optional<std::uint8_t>(0x97));   // the em dash
    CHECK_FALSE(core::winAnsiCode(U'\u0081'));   // a C1 control
    CHECK_FALSE(core::winAnsiCode(U'Ε'));   // Greek
    CHECK_FALSE(core::winAnsiCode(U'中'));
    // Helvetica's AFM, spot-checked: the full table is held to the engine in `render.`.
    CHECK(core::helveticaWidth(0x20) == 278);
    CHECK(core::helveticaWidth('W') == 944);
    CHECK(core::helveticaWidth(0x80) == 556);
    CHECK(core::helveticaWidth(0x81) == 0);
}

TEST_CASE("each collection covers its own script and the coverage holds no Greek", "[typedtext][A-56]")
{
    // An ideograph every collection has.
    for (const TypedFont font : {TypedFont::Simplified, TypedFont::Traditional, TypedFont::Japanese, TypedFont::Korean})
        CHECK(core::cjkCovers(font, U'中'));
    // A simplified-only form, which Big5 does not hold.
    CHECK(core::cjkCovers(TypedFont::Simplified, U'语'));
    CHECK_FALSE(core::cjkCovers(TypedFont::Traditional, U'语'));
    // A traditional form (U+9AD4, the body of "traditional characters"), which every Chinese set holds.
    CHECK(core::cjkCovers(TypedFont::Traditional, U'體'));
    // Hangul syllables only in Korean; hiragana in Japanese.
    CHECK(core::cjkCovers(TypedFont::Korean, U'한'));
    CHECK_FALSE(core::cjkCovers(TypedFont::Japanese, U'한'));
    CHECK(core::cjkCovers(TypedFont::Japanese, U'あ'));
    // Greek is in GB 2312 and JIS X 0208, and refused all the same (D6-7, `DW-3`).
    for (const TypedFont font : {TypedFont::Simplified, TypedFont::Traditional, TypedFont::Japanese, TypedFont::Korean}) {
        CHECK_FALSE(core::cjkCovers(font, U'Ε'));
        CHECK_FALSE(core::cjkCovers(font, U'П'));   // Cyrillic
    }
    CHECK_FALSE(core::cjkCovers(TypedFont::Helvetica, U'中'));
}

TEST_CASE("the characters A-56 types are accepted and the scripts it refuses are named", "[typedtext][A-56]")
{
    CHECK_FALSE(core::firstRefusedCharacter(QStringLiteral("café € “quoted”")));
    CHECK_FALSE(core::firstRefusedCharacter(QStringLiteral("中文 繁體 日本語 한국어")));
    CHECK_FALSE(core::firstRefusedCharacter(QStringLiteral("two\nlines\r\nand a\ttab")));
    CHECK(core::firstRefusedCharacter(QStringLiteral("Ελληνικά")) == std::optional<char32_t>(0x0395));
    CHECK(core::firstRefusedCharacter(QStringLiteral("ok שלום")) == std::optional<char32_t>(0x05E9));
    CHECK(core::firstRefusedCharacter(QStringLiteral("При")) == std::optional<char32_t>(0x041F));
    // Past the BMP: no UCS-2 code for it.
    CHECK(core::firstRefusedCharacter(QString::fromUcs4(U"a\U0001F600")) == std::optional<char32_t>(0x1F600));
    // A lone surrogate is refused as itself.
    CHECK(core::firstRefusedCharacter(QString(QChar(0xD800))) == std::optional<char32_t>(0xD800));

    CHECK(core::describeCharacter(0x0395) == QStringLiteral("“Ε” (U+0395)"));
    CHECK(core::describeCharacter(0x1F600) == QString::fromUcs4(U"“\U0001F600” (U+1F600)"));
    CHECK(core::describeCharacter(0x0007) == QStringLiteral("U+0007"));

    // And layout refuses the same character, by the key the editor and the builder say it with.
    core::Result<TypedLayout> refused = core::layoutTypedText(QStringLiteral("abc Ε"), 10.0, std::nullopt, TypedFont::Simplified);
    REQUIRE_FALSE(refused);
    CHECK(QString(refused.error().messageKey) == QStringLiteral("error.edit.typedCharacterRefused"));
    CHECK(refused.error().context == core::describeCharacter(0x0395));
}

// ==========================================================================================
// Which face (D6-34)
// ==========================================================================================

TEST_CASE("a run of ideographs is drawn in the Han preference and kana and Hangul force their own", "[typedtext][D6-34]")
{
    const QString text = QStringLiteral("中文 繁體 日本語 한국어");
    const TypedLayout traditional = laid(text, std::nullopt, TypedFont::Traditional);
    using P = std::pair<TypedFont, QString>;
    CHECK(runsOf(traditional) == std::vector<P>{
        {TypedFont::Traditional, QStringLiteral("中文")}, {TypedFont::Helvetica, QStringLiteral(" ")},
        {TypedFont::Traditional, QStringLiteral("繁體")}, {TypedFont::Helvetica, QStringLiteral(" ")},
        {TypedFont::Traditional, QStringLiteral("日本語")}, {TypedFont::Helvetica, QStringLiteral(" ")},
        {TypedFont::Korean, QStringLiteral("한국어")}});
    CHECK(traditional.primary == TypedFont::Traditional);
    CHECK(traditional.hasCjk);

    const TypedLayout simplified = laid(text, std::nullopt, TypedFont::Simplified);
    CHECK(simplified.primary == TypedFont::Simplified);
    CHECK(runsOf(simplified).front().first == TypedFont::Simplified);

    // A run holding kana is Japanese -- its ideographs too -- and decides the primary face when it is first.
    const TypedLayout japanese = laid(QStringLiteral("漢字とかな 中"), std::nullopt, TypedFont::Traditional);
    CHECK(runsOf(japanese).front() == P{TypedFont::Japanese, QStringLiteral("漢字とかな")});
    CHECK(japanese.primary == TypedFont::Japanese);
    CHECK(runsOf(japanese).back().first == TypedFont::Japanese);   // a later plain run takes the primary face

    // A character the preferred collection lacks is drawn by the first that has it.
    const TypedLayout fallback = laid(QStringLiteral("语"), std::nullopt, TypedFont::Traditional);
    CHECK(runsOf(fallback) == std::vector<P>{{TypedFont::Simplified, QStringLiteral("语")}});
    CHECK(fallback.primary == TypedFont::Traditional);

    // Latin only: Helvetica, and no CJK face is primary.
    const TypedLayout latin = laid(QStringLiteral("café"));
    CHECK(latin.primary == TypedFont::Helvetica);
    CHECK_FALSE(latin.hasCjk);
}

TEST_CASE("a language names a Han preference and the first CJK one wins", "[typedtext][D6-34]")
{
    CHECK(core::cjkFontForLanguage(u"zh-TW") == std::optional<TypedFont>(TypedFont::Traditional));
    CHECK(core::cjkFontForLanguage(u"zh_HK") == std::optional<TypedFont>(TypedFont::Traditional));
    CHECK(core::cjkFontForLanguage(u"zh-Hant-SG") == std::optional<TypedFont>(TypedFont::Traditional));
    CHECK(core::cjkFontForLanguage(u"zh-CN") == std::optional<TypedFont>(TypedFont::Simplified));
    CHECK(core::cjkFontForLanguage(u"zh") == std::optional<TypedFont>(TypedFont::Simplified));
    CHECK(core::cjkFontForLanguage(u"ja-JP") == std::optional<TypedFont>(TypedFont::Japanese));
    CHECK(core::cjkFontForLanguage(u"ko") == std::optional<TypedFont>(TypedFont::Korean));
    CHECK_FALSE(core::cjkFontForLanguage(u"en-US"));
    CHECK_FALSE(core::cjkFontForLanguage(u"zhx"));   // a prefix is not a language
    CHECK(core::hanPreference(QStringList{QStringLiteral("en-US"), QStringLiteral("zh-TW")}) == TypedFont::Traditional);
    CHECK(core::hanPreference(QStringList{QStringLiteral("ja"), QStringLiteral("zh-TW")}) == TypedFont::Japanese);
    CHECK(core::hanPreference(QStringList{QStringLiteral("fr-FR")}) == TypedFont::Simplified);
}

// ==========================================================================================
// Lines
// ==========================================================================================

TEST_CASE("lines break after a word and keep their spaces so the text is the text typed", "[typedtext][A-57]")
{
    const QString text = QStringLiteral("The quick brown fox jumps over the lazy dog");
    const TypedLayout layout = laid(text, 60.0);
    REQUIRE(layout.lines.size() > 1);
    for (const core::TypedLine& line : layout.lines) CHECK(line.width <= 60.0 + 1e-9);
    // Every character typed is in a run, the spaces at the soft breaks included.
    CHECK(joined(layout) == text);
    // The first line is "The quick " at 10 points: T h e space q u i c k = 611+556+556+278+556+556+222+500+500.
    CHECK(near(layout.lines.front().width, (611 + 556 + 556 + 278 + 556 + 556 + 222 + 500 + 500) / 100.0));
    CHECK(near(layout.height, static_cast<double>(layout.lines.size()) * 1.2 * 10.0));
}

TEST_CASE("a word wider than the line breaks between letters and CJK breaks anywhere", "[typedtext]")
{
    const TypedLayout word = laid(QStringLiteral("Supercalifragilistic"), 40.0);
    REQUIRE(word.lines.size() > 1);
    for (const core::TypedLine& line : word.lines) CHECK(line.width <= 40.0 + 1e-9);
    CHECK(joined(word) == QStringLiteral("Supercalifragilistic"));

    // Ten ideographs at 10 points are 100 points; 35 points hold three a line.
    const QString cjk = QStringLiteral("一二三四五六七八九十");
    const TypedLayout ideographs = laid(cjk, 35.0);
    REQUIRE(ideographs.lines.size() == 4);
    CHECK(near(ideographs.lines.front().width, 30.0));
    CHECK(joined(ideographs) == cjk);
}

TEST_CASE("a line break is a line and an empty one is kept", "[typedtext]")
{
    const TypedLayout layout = laid(QStringLiteral("one\ntwo\r\n\nfour"));
    REQUIRE(layout.lines.size() == 4);
    CHECK(layout.lines[2].runs.empty());
    CHECK(joined(layout) == QStringLiteral("onetwofour"));
    // No wrap: a long line stays one line.
    CHECK(laid(QStringLiteral("a very long line that is never wrapped at all")).lines.size() == 1);
}

// ==========================================================================================
// /DA
// ==========================================================================================

TEST_CASE("a default appearance string is read in any order and written in one", "[typedtext][A-56]")
{
    const auto da = core::DefaultAppearance::parse(QStringLiteral("0 0 1 rg /Helv 10 Tf"));
    REQUIRE(da);
    CHECK(da->font == QStringLiteral("Helv"));
    CHECK(da->size == 10.0);
    CHECK(da->colour == std::vector<double>{0.0, 0.0, 1.0});
    CHECK(da->toString() == QStringLiteral("/Helv 10 Tf 0 0 1 rg"));
    const auto gray = core::DefaultAppearance::parse(QStringLiteral("/STSong 12.5 Tf 0.25 g"));
    REQUIRE(gray);
    CHECK(gray->font == QStringLiteral("STSong"));
    CHECK(gray->toString() == QStringLiteral("/STSong 12.5 Tf 0.25 g"));
    CHECK_FALSE(core::DefaultAppearance::parse(QStringLiteral("nothing here")));
    // The style a free text is drawn with, and its Han preference from a CJK face.
    core::Annotation a;
    a.defaultAppearance = QStringLiteral("/MSung 14 Tf 1 0 0 rg");
    const core::FreeTextStyle style = core::freeTextStyle(a);
    CHECK(style.size == 14.0);
    CHECK(style.textColour == std::vector<double>{1.0, 0.0, 0.0});
    CHECK(style.han == std::optional<TypedFont>(TypedFont::Traditional));
    a.defaultAppearance.clear();
    CHECK(core::freeTextStyle(a).size == core::kFreeTextDefaultSize);
    CHECK_FALSE(core::freeTextStyle(a).han);
}

// ==========================================================================================
// The free-text frame and fitting
// ==========================================================================================

TEST_CASE("the text's frame is upright at every quarter turn and its top-left is where it reads", "[typedtext]")
{
    core::Annotation a;
    a.subtype = core::AnnotationSubtype::FreeText;
    a.rect = core::PdfRect{core::PdfPoint{100, 200}, core::PdfSize{150, 60}};
    a.borderWidth = 1.0;
    for (const int rotate : {0, 90, 180, 270, -90}) {
        a.rotate = rotate;
        const auto frame = core::freeTextFrame(a);
        REQUIRE(frame);
        const bool turned = rotate == 90 || rotate == 270 || rotate == -90;
        CHECK(frame->width == (turned ? 60.0 : 150.0));
        CHECK(frame->height == (turned ? 150.0 : 60.0));
        // The frame's four corners are the box's four.
        for (const auto& [x, y] : {std::pair{0.0, 0.0}, std::pair{frame->width, frame->height}}) {
            const core::PdfPoint p = frame->mapped(x, y);
            CHECK((near(p.x, 100) || near(p.x, 250)));
            CHECK((near(p.y, 200) || near(p.y, 260)));
        }
        // freeTextBoxAt is the inverse: the box whose frame has this top-left is the box itself.
        const core::PdfRect again = core::freeTextBoxAt(frame->mapped(0.0, frame->height), frame->width, frame->height, frame->rotate);
        CHECK(again == a.rect);
    }
    a.rotate = 45;
    CHECK_FALSE(core::freeTextFrame(a));
    a.rotate = 0;
    a.rectDifferences = {200, 0, 0, 0};   // leaves nothing
    CHECK_FALSE(core::freeTextFrame(a));
}

TEST_CASE("a box keeps its width and top and grows to its lines and a typewriter fits both ways", "[typedtext][A-55]")
{
    core::Annotation box;
    box.subtype = core::AnnotationSubtype::FreeText;
    box.rect = core::PdfRect{core::PdfPoint{100, 500}, core::PdfSize{120, 20}};
    box.borderWidth = 1.0;
    box.defaultAppearance = QStringLiteral("/Helv 10 Tf 0 g");
    box.contents = QStringLiteral("A text box that wraps onto several lines of its own width");
    const auto fitted = core::fittedFreeText(box, TypedFont::Simplified);
    REQUIRE(fitted);
    CHECK(near(fitted.value().rect.left(), 100));
    CHECK(near(fitted.value().rect.size.width, 120));
    CHECK(near(fitted.value().rect.top(), 520));
    const TypedLayout lines = laid(box.contents, 120.0 - 2 * 3.0);
    CHECK(near(fitted.value().rect.size.height, lines.height + 2 * 3.0));
    CHECK(fitted.value().defaultAppearance == QStringLiteral("/Helv 10 Tf 0 g"));

    // CJK text names its face in /DA -- the Han preference the next redraw reads back.
    box.contents = QStringLiteral("繁體中文");
    const auto cjk = core::fittedFreeText(box, TypedFont::Traditional);
    REQUIRE(cjk);
    CHECK(cjk.value().defaultAppearance == QStringLiteral("/MSung 10 Tf 0 g"));
    CHECK(core::freeTextStyle(cjk.value()).han == std::optional<TypedFont>(TypedFont::Traditional));
    // And refitted later under another person's language, it keeps its own face.
    const auto again = core::fittedFreeText(cjk.value(), TypedFont::Japanese);
    REQUIRE(again);
    CHECK(again.value().defaultAppearance == cjk.value().defaultAppearance);

    core::Annotation typewriter = box;
    typewriter.intent = QStringLiteral("FreeTextTypeWriter");
    typewriter.borderWidth = 0.0;
    typewriter.contents = QStringLiteral("Typed\nhere");
    const auto fit = core::fittedFreeText(typewriter, TypedFont::Simplified);
    REQUIRE(fit);
    const TypedLayout two = laid(typewriter.contents);
    CHECK(near(fit.value().rect.size.width, two.width + 2 * 2.0));
    CHECK(near(fit.value().rect.size.height, two.height + 2 * 2.0));
    CHECK(near(fit.value().rect.left(), 100));
    CHECK(near(fit.value().rect.top(), 520));

    // A refused character fits nothing.
    box.contents = QStringLiteral("ש");
    const auto refused = core::fittedFreeText(box, TypedFont::Simplified);
    REQUIRE_FALSE(refused);
    CHECK(QString(refused.error().messageKey) == QStringLiteral("error.edit.typedCharacterRefused"));
}

TEST_CASE("a callout's rectangle holds its box its leader and its arrowhead and RD says where the box is", "[typedtext][A-55]")
{
    core::Annotation callout;
    callout.subtype = core::AnnotationSubtype::FreeText;
    callout.intent = QStringLiteral("FreeTextCallout");
    callout.borderWidth = 1.0;
    callout.defaultAppearance = QStringLiteral("/Helv 10 Tf 1 0 0 rg");
    callout.contents = QStringLiteral("Look here");
    callout.rect = core::PdfRect{core::PdfPoint{200, 500}, core::PdfSize{140, 20}};
    callout.callout = core::calloutLeader(core::PdfPoint{150, 450}, callout.rect);
    const auto fitted = core::fittedFreeText(callout, TypedFont::Simplified);
    REQUIRE(fitted);
    const core::Annotation& c = fitted.value();
    REQUIRE(c.callout.size() == 6);
    CHECK(c.callout[0] == 150);   // where it points does not move
    CHECK(c.callout[1] == 450);
    REQUIRE(c.rectDifferences.size() == 4);
    const auto frame = core::freeTextFrame(c);
    REQUIRE(frame);
    CHECK(near(frame->box.left(), 200));    // the box keeps its top-left and width
    CHECK(near(frame->box.top(), 520));
    CHECK(near(frame->box.size.width, 140));
    // The leader ends on the box's side that faces the target -- the left -- at its middle.
    CHECK(near(c.callout[4], frame->box.left()));
    CHECK(near(c.callout[5], (frame->box.bottom() + frame->box.top()) / 2.0));
    for (std::size_t k = 0; k < 6; k += 2) {
        CHECK(c.rect.contains(core::PdfPoint{c.callout[k] - core::kCalloutHead, c.callout[k + 1] - core::kCalloutHead}));
        CHECK(c.rect.contains(core::PdfPoint{c.callout[k] + core::kCalloutHead, c.callout[k + 1] + core::kCalloutHead}));
    }
}
