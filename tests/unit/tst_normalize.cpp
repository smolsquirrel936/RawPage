// tst_normalize -- `core/text/TextNormalizer`, headless. Stage 5 T4, decision D5-5; guide cases
// A-32 ... A-36 (`Stage5-TextSearch-Testing-Guide.md` §5.6).
//
// The models here are built by hand in the SHAPES T2 measured PDFium producing -- a `ﬁ` arrives as `f`
// and `i` sharing one box, a decomposed `é` as `e` then U+0301, a line-end hyphen flagged -- so the
// normalizer is tested against what the engine really hands it. `tst_layoutcorpus.cpp` runs the same
// claims over the real corpus files.
//
// "FINDS" here means: the normalized query occurs in the normalized page. That is the whole of what
// find, search and compare will do with the two strings (T6, T12); the matching itself -- whole words,
// regular expressions, the settings that switch each step -- is T6's. `search/caseSensitive` and
// `search/ignoreDiacritics` do not exist yet, so the cases set `NormalizeOptions` directly, which is
// what those keys will set (convention 19: the key-to-option wiring is NOT covered here).

#include <catch2/catch_test_macros.hpp>

#include "core/text/LayoutAnalyzer.h"
#include "core/text/PageTextModel.h"
#include "core/text/TextNormalizer.h"

#include <vector>

using rawpage::core::PdfPoint;
using rawpage::core::PdfRect;
using rawpage::core::PdfSize;
using rawpage::core::text::CharFlag;
using rawpage::core::text::NormalizedText;
using rawpage::core::text::NormalizeOptions;
using rawpage::core::text::PageTextModel;
using rawpage::core::text::ReadingItem;
using rawpage::core::text::TextChar;
using rawpage::core::text::TextNormalizer;

namespace {

// A page built line by line: each string is one line, and `\n` inside the vector's items is not used
// -- a new item is a new line. Returns the model and its reading stream, in the order written.
struct Page {
    PageTextModel model;
    std::vector<ReadingItem> stream;

    void line(const std::u32string& text, std::vector<std::uint8_t> flags = {})
    {
        if (!stream.empty()) stream.push_back({ReadingItem::Kind::LineBreak, -1});
        for (std::size_t i = 0; i < text.size(); ++i) {
            TextChar c;
            c.codepoint = text[i];
            c.box = PdfRect{PdfPoint{static_cast<double>(i) * 6.0, 700.0}, PdfSize{6.0, 12.0}};
            c.flags = i < flags.size() ? flags[i] : 0;
            stream.push_back({ReadingItem::Kind::Char, static_cast<int>(model.size())});
            model.append(c);
        }
    }

    NormalizedText normalized(NormalizeOptions options = {}) const
    {
        return TextNormalizer(options).normalize(model, stream);
    }
};

bool finds(const NormalizedText& page, const QString& query, NormalizeOptions options = {})
{
    return page.text.contains(TextNormalizer(options).normalizeQuery(query));
}

constexpr std::uint8_t kHyphen = static_cast<std::uint8_t>(CharFlag::Hyphen);

// The flags for a line whose LAST character the engine judged a line-end hyphen.
std::vector<std::uint8_t> hyphenAtEnd(const std::u32string& line)
{
    std::vector<std::uint8_t> flags;
    for (std::size_t i = 0; i < line.size(); ++i) flags.push_back(i + 1 == line.size() ? kHyphen : 0);
    return flags;
}

} // namespace

TEST_CASE("normalizer finds a ligature both ways", "[core][text][A-32]")
{
    // The engine decomposes U+FB01 into `f` `i` itself (T2), and a producer's /ToUnicode may say `fi`;
    // a person may TYPE the ligature. All three meet.
    Page page;
    page.line(U"\U0000FB01sh and chips");   // as a model that kept the presentation form would hold it
    page.line(U"a file of figures");
    const NormalizedText text = page.normalized();
    CHECK(finds(text, QStringLiteral("fish")));
    CHECK(finds(text, QStringLiteral("file")));
    CHECK(finds(text, QString::fromUtf8("\xEF\xAC\x81") + QStringLiteral("sh")));   // `ﬁsh` typed
    CHECK(finds(text, QString::fromUtf8("\xEF\xAC\x81") + QStringLiteral("le")));   // `ﬁle` typed

    SECTION("THE CONTROL: a word that is not there is not found")
    {
        CHECK_FALSE(finds(text, QStringLiteral("fjord")));
    }
}

TEST_CASE("normalizer folds case and says what simple folding does not do", "[core][text][A-33]")
{
    Page page;
    page.line(U"Lorem ipsum Straße");
    CHECK(finds(page.normalized(), QStringLiteral("LOREM")));
    // THE CONTROL: case-sensitive, it does not.
    NormalizeOptions sensitive;
    sensitive.foldCase = false;
    CHECK_FALSE(finds(page.normalized(sensitive), QStringLiteral("LOREM"), sensitive));
    CHECK(finds(page.normalized(sensitive), QStringLiteral("Lorem"), sensitive));
    // RECORDED, not discovered later: `toCaseFolded` is SIMPLE folding. `ß` does not become `ss`, so
    // `STRASSE` does not find `Straße` -- asserted as the behaviour this pipeline has.
    CHECK_FALSE(finds(page.normalized(), QStringLiteral("STRASSE")));
    CHECK(finds(page.normalized(), QString::fromUtf8("STRA\xC3\x9F" "E")));
}

TEST_CASE("normalizer matches precomposed and decomposed accents as D5-5 says", "[core][text][A-34]")
{
    Page page;
    page.line(U"café au lait");      // precomposed
    page.line(U"café au lait");     // decomposed, as T2 measured it arriving
    const NormalizedText plain = page.normalized();
    // NFKC composes the decomposed form, so `café` finds BOTH and appears twice.
    const QString cafe = TextNormalizer().normalizeQuery(QString::fromUtf8("caf\xC3\xA9"));
    CHECK(plain.text.count(cafe) == 2);
    // Without diacritic folding, `cafe` finds neither.
    CHECK_FALSE(finds(plain, QStringLiteral("cafe au")));

    NormalizeOptions folding;
    folding.stripDiacritics = true;
    const NormalizedText stripped = page.normalized(folding);
    CHECK(stripped.text.count(TextNormalizer(folding).normalizeQuery(QStringLiteral("cafe"))) == 2);
}

TEST_CASE("normalizer maps a hit back to whole glyphs", "[core][text][A-35]")
{
    // The ligature as T2 measured it: TWO model characters, `f` and `i`, one box.
    PageTextModel model;
    std::vector<ReadingItem> stream;
    const auto add = [&](char32_t cp) {
        TextChar c;
        c.codepoint = cp;
        stream.push_back({ReadingItem::Kind::Char, static_cast<int>(model.size())});
        model.append(c);
    };
    add(U'\U0000FB01');   // index 0: a kept presentation form -- NFKC makes it two units
    add(U's');            // 1
    add(U' ');            // 2
    add(U'e');            // 3
    add(U'́');       // 4: its mark
    add(U'x');            // 5
    const NormalizedText text = TextNormalizer().normalize(model, stream);
    REQUIRE(text.text == QString::fromUtf8("fis \xC3\xA9x"));

    // A hit on the `f` alone -- normalized unit 0 -- highlights the whole ligature glyph, index 0.
    CHECK(text.sourceRange(0, 1) == std::pair<int, int>{0, 0});
    // ... and one on its `i` the same glyph: no highlight starts or ends mid-glyph.
    CHECK(text.sourceRange(1, 2) == std::pair<int, int>{0, 0});
    // A hit on the composed `é` covers the base AND its mark.
    const qsizetype e = text.text.indexOf(QChar(0x00E9));
    CHECK(text.sourceRange(e, e + 1) == std::pair<int, int>{3, 4});
    // A synthesized space maps to nothing; a range across it spans the glyphs either side.
    CHECK(text.sourceRange(3, 4) == std::pair<int, int>{-1, -1});
    CHECK(text.sourceRange(2, 5) == std::pair<int, int>{1, 4});
}

TEST_CASE("normalizer joins a line-final hyphen before a lowercase letter and nothing else", "[core][text][A-36]")
{
    Page page;
    page.line(U"the exam-", hyphenAtEnd(U"the exam-"));   // PDFium's judgement, which T2 measured
    page.line(U"ple and a well-known trick");
    page.line(U"North-", hyphenAtEnd(U"North-"));
    page.line(U"West edge");
    const NormalizedText text = page.normalized();

    CHECK(finds(text, QStringLiteral("example")));
    CHECK(finds(text, QStringLiteral("well-known")));
    // THE CONTROL: the mid-line hyphen is a real compound and is never joined.
    CHECK_FALSE(finds(text, QStringLiteral("wellknown")));
    // Before a capital the hyphen stays and NO space is added -- the compound the label names.
    CHECK(finds(text, QStringLiteral("north-west")));
    CHECK_FALSE(finds(text, QStringLiteral("northwest")));
    // Every other line break is one space.
    CHECK(finds(text, QStringLiteral("trick north")));

    SECTION("an unflagged hyphen at a line end is not joined -- the flag is the evidence")
    {
        Page plain;
        plain.line(U"the exam-");
        plain.line(U"ple");
        CHECK_FALSE(finds(plain.normalized(), QStringLiteral("example")));
    }
}

TEST_CASE("normalizer drops unmapped glyphs and collapses whitespace", "[core][text]")
{
    Page page;
    page.line(U"ab  cd");
    TextChar unmapped;
    unmapped.codepoint = U'Z';
    unmapped.flags = static_cast<std::uint8_t>(CharFlag::Unmapped);
    page.stream.push_back({ReadingItem::Kind::WordGap, -1});
    page.stream.push_back({ReadingItem::Kind::Char, static_cast<int>(page.model.size())});
    page.model.append(unmapped);
    page.stream.push_back({ReadingItem::Kind::BlockBreak, -1});
    page.line(U"ef");
    const NormalizedText text = page.normalized();
    CHECK(text.text == QStringLiteral("ab cd ef"));
    CHECK_FALSE(text.text.contains(QChar(0xFFFD)));
    CHECK(TextNormalizer().normalizeQuery(QStringLiteral("  Ab \t CD ")) == QStringLiteral("ab cd"));
}
