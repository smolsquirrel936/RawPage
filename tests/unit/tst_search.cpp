// tst_search -- `core/text/SearchEngine`'s MATCH on synthetic geometry, headless. Stage 5 T6, decision D5-5;
// guide cases A-56 … A-60's matching halves, A-21's find half and A-41's search clause
// (`Stage5-TextSearch-Testing-Guide.md` §5.8, §5.4, §5.6).
//
// WHAT THIS FILE CANNOT CLAIM, named here (convention 19): every page in it is the case's own arithmetic
// -- `tst_selection.cpp`'s Helvetica-like boxes -- so nothing here says a hit's span lands on INK (T7's
// highlight against the raster, rule 6) or that the corpus's real text is found (`tests/render/
// tst_searchcorpus.cpp`). What it owns is WHICH CHARACTERS A QUERY MATCHES under each flag, with a
// control for every rule written from what must not match (rule 5). The asynchronous run -- the index,
// the engine thread, the bound, cancellation -- is `tst_index.cpp`'s.

#include <catch2/catch_test_macros.hpp>

#include "core/text/DocumentIndex.h"
#include "core/text/LayoutAnalyzer.h"
#include "core/text/PageTextModel.h"
#include "core/text/SearchEngine.h"
#include "core/text/SearchQuery.h"
#include "core/text/TextSelection.h"

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using rawpage::core::PdfPoint;
using rawpage::core::PdfRect;
using rawpage::core::PdfSize;
using namespace rawpage::core::text;

namespace {

constexpr double kAdvance = 0.55;

// `tst_selection.cpp`'s run: 0.55 em boxes, 0.21 em below the baseline to 0.9 em above, a space a shown
// character as the content draws one.
void run(PageTextModel& model, const std::u32string& text, double x, double baseline, double size = 10.0,
         std::uint8_t flags = 0)
{
    for (const char32_t cp : text) {
        TextChar c;
        c.codepoint = cp;
        c.fontSize = size;
        c.flags = flags;
        c.origin = PdfPoint{x, baseline};
        c.box = PdfRect{PdfPoint{x, baseline - 0.21 * size}, PdfSize{kAdvance * size, 1.11 * size}};
        model.append(c);
        x += kAdvance * size;
    }
}

std::shared_ptr<const PageReading> page(const std::vector<std::u32string>& lines, double leading = 12.0)
{
    PageTextModel model(0);
    double baseline = 700.0;
    for (const std::u32string& line : lines) {
        run(model, line, 72.0, baseline);
        baseline -= leading;
    }
    return PageReading::build(std::make_shared<const PageTextModel>(std::move(model)));
}

// A span's characters as a reader sees them: a break inside it is one space.
QString textOf(const PageReading& reading, Span span)
{
    QString out;
    for (int p = span.begin; p < span.end; ++p) {
        const char32_t cp = reading.charAt(p).codepoint;
        if (p > span.begin && reading.breakBefore(p) != ReadingItem::Kind::Char && cp != U' ')
            out.append(QLatin1Char(' '));
        out.append(QString::fromUcs4(&cp, 1));
    }
    return out;
}

SearchQuery query(const QString& pattern, SearchFlags flags = {})
{
    SearchQuery q;
    q.pattern = pattern;
    q.flags = flags;
    return q;
}

std::vector<QString> find(const PageReading& reading, const SearchQuery& q)
{
    const auto compiled = SearchEngine::compile(q);
    REQUIRE(compiled);
    const auto searchable = SearchablePage::build(reading, compiled.value().options());
    std::vector<QString> out;
    for (const SearchHit& hit : SearchEngine::findOnPage(compiled.value(), *searchable, reading)) {
        REQUIRE(hit.page == reading.pageIndex());
        REQUIRE(hit.reading.begin >= 0);
        REQUIRE(hit.reading.end <= reading.size());
        REQUIRE(hit.reading.begin < hit.reading.end);
        out.push_back(textOf(reading, hit.reading));
    }
    return out;
}

using Hits = std::vector<QString>;

} // namespace

// =============================================================================================
// A-56 -- every flag, with its control
// =============================================================================================

TEST_CASE("whole words finds lore alone and not inside lorem", "[search][A-56]")
{
    const auto reading = page({U"lorem lore"});
    SearchFlags whole;
    whole.wholeWords = true;
    CHECK(find(*reading, query(QStringLiteral("lore"))) == Hits{QStringLiteral("lore"), QStringLiteral("lore")});
    // THE FLAG ON: only the word of its own. The control is the line above -- off, the `lore` inside
    // `lorem` IS a hit, so a flag that did nothing would leave two.
    CHECK(find(*reading, query(QStringLiteral("lore"), whole)) == Hits{QStringLiteral("lore")});
    CHECK(find(*reading, query(QStringLiteral("lorem"), whole)) == Hits{QStringLiteral("lorem")});
}

TEST_CASE("case is folded unless match case is on", "[search][A-56]")
{
    const auto reading = page({U"Lorem ipsum"});
    SearchFlags exact;
    exact.caseSensitive = true;
    CHECK(find(*reading, query(QStringLiteral("lorem"))) == Hits{QStringLiteral("Lorem")});
    CHECK(find(*reading, query(QStringLiteral("LOREM"))) == Hits{QStringLiteral("Lorem")});
    // ON: the control is the folded hit above.
    CHECK(find(*reading, query(QStringLiteral("lorem"), exact)).empty());
    CHECK(find(*reading, query(QStringLiteral("Lorem"), exact)) == Hits{QStringLiteral("Lorem")});
}

TEST_CASE("diacritics are matched unless ignore diacritics is on", "[search][A-56]")
{
    // A precomposed é and a decomposed e + U+0301 -- the two spellings T1 put side by side.
    const auto reading = page({U"café au lait", U"café au lait"});
    SearchFlags ignoring;
    ignoring.ignoreDiacritics = true;
    // OFF (the default): `café` finds BOTH spellings -- NFKC composes the second -- and `cafe` finds
    // neither. That `cafe` finding nothing is the control for the flag.
    CHECK(find(*reading, query(QStringLiteral("café"))).size() == 2);
    CHECK(find(*reading, query(QStringLiteral("cafe"))).empty());
    // ON: `cafe` finds both, and so does `café`.
    CHECK(find(*reading, query(QStringLiteral("cafe"), ignoring)).size() == 2);
    CHECK(find(*reading, query(QStringLiteral("café"), ignoring)).size() == 2);
    // A hit on the decomposed spelling covers the base AND its mark, never half a glyph (A-35).
    const Hits decomposed = find(*reading, query(QStringLiteral("cafe"), ignoring));
    CHECK(decomposed.back() == QStringLiteral("café"));
}

TEST_CASE("a regular expression matches and the same text literally does not", "[search][A-56]")
{
    const auto reading = page({U"lorem and lo.em"});
    SearchFlags regex;
    regex.useRegex = true;
    CHECK(find(*reading, query(QStringLiteral("lo.em"), regex)) == Hits{QStringLiteral("lorem"), QStringLiteral("lo.em")});
    // REGEX OFF: `lo.em` is four letters and a dot, and must NOT match `lorem` -- the guide's own control.
    CHECK(find(*reading, query(QStringLiteral("lo.em"))) == Hits{QStringLiteral("lo.em")});
    // Case folding under a regex is an option, not a fold of the pattern: `\S` must stay `\S`.
    CHECK(find(*reading, query(QStringLiteral("L\\S+M"), regex)) == Hits{QStringLiteral("lorem"), QStringLiteral("lo.em")});
    SearchFlags regexExact = regex;
    regexExact.caseSensitive = true;
    CHECK(find(*reading, query(QStringLiteral("L\\S+M"), regexExact)).empty());
}

TEST_CASE("whole words applies to a regular expression too", "[search][A-56]")
{
    const auto reading = page({U"lorem lore"});
    SearchFlags flags;
    flags.useRegex = true;
    CHECK(find(*reading, query(QStringLiteral("lor."), flags)).size() == 2);
    flags.wholeWords = true;
    CHECK(find(*reading, query(QStringLiteral("lor."), flags)) == Hits{QStringLiteral("lore")});
}

TEST_CASE("a match of no characters is never a hit", "[search][A-56]")
{
    const auto reading = page({U"lorem"});
    SearchFlags regex;
    regex.useRegex = true;
    CHECK(find(*reading, query(QStringLiteral("x*"), regex)).empty());
    CHECK(find(*reading, query(QStringLiteral("o*"), regex)) == Hits{QStringLiteral("o")});
}

// =============================================================================================
// A-57 -- an invalid regular expression searches nothing and says so
// =============================================================================================

TEST_CASE("an invalid regular expression is refused naming the pattern", "[search][A-57]")
{
    SearchFlags regex;
    regex.useRegex = true;
    for (const QString& pattern : {QStringLiteral("("), QStringLiteral("[a-"), QStringLiteral("*x")}) {
        INFO(pattern.toStdString());
        const auto compiled = SearchEngine::compile(query(pattern, regex));
        REQUIRE_FALSE(compiled);
        CHECK(QString(compiled.error().messageKey) == QStringLiteral("error.search.invalidPattern"));
        CHECK(compiled.error().context == pattern);
    }
    // THE CONTROL: a valid pattern compiles, and the same characters as a LITERAL are not an error.
    CHECK(SearchEngine::compile(query(QStringLiteral("lo+rem"), regex)));
    CHECK(SearchEngine::compile(query(QStringLiteral("("))));
    CHECK(SearchEngine::compile(query(QStringLiteral("*x"))));
}

TEST_CASE("an empty pattern is refused rather than matching everything", "[search][A-57]")
{
    for (const QString& pattern : {QString(), QStringLiteral("   ")}) {
        const auto compiled = SearchEngine::compile(query(pattern));
        REQUIRE_FALSE(compiled);
        CHECK(QString(compiled.error().messageKey) == QStringLiteral("error.search.emptyPattern"));
    }
}

// =============================================================================================
// A-58 -- CJK, and whole words in CJK
// =============================================================================================

TEST_CASE("CJK is found and with whole words each ideograph is a word", "[search][A-58]")
{
    const auto reading = page({U"中日目中日田"});   // 中日目中日田
    CHECK(find(*reading, query(QStringLiteral("中日"))).size() == 2);
    SearchFlags whole;
    whole.wholeWords = true;
    // THE CAVEAT D5-5 RECORDS, asserted as the behaviour (guide §7): with no dictionary every ideograph is
    // a word, so a run of ideographs has a boundary between each -- and `中日` with whole words is found
    // wherever it is, exactly as without.
    CHECK(find(*reading, query(QStringLiteral("中日"), whole)).size() == 2);
    // THE CONTROL that the boundary rule is not simply off: Latin inside a longer word is refused.
    const auto latin = page({U"lorem"});
    CHECK(find(*latin, query(QStringLiteral("ore"), whole)).empty());
}

// =============================================================================================
// A-59 -- across a line break, and not across a block
// =============================================================================================

TEST_CASE("a two word query is found across a line break and not across a block", "[search][A-59]")
{
    // Two lines of one paragraph at 1.2 em, then a second paragraph 3.8 em below -- `LayoutAnalyzer`
    // makes the first two one block and the third another (its step 6).
    PageTextModel model(0);
    run(model, U"the quick brown fox", 72.0, 700.0);
    run(model, U"jumps over the dog", 72.0, 688.0);
    run(model, U"lazy afternoon", 72.0, 650.0);
    const auto reading = PageReading::build(std::make_shared<const PageTextModel>(std::move(model)));
    REQUIRE(reading->layout().blocks.size() == 2);

    CHECK(find(*reading, query(QStringLiteral("fox jumps"))) == Hits{QStringLiteral("fox jumps")});
    // ACROSS THE BLOCK: not found.
    CHECK(find(*reading, query(QStringLiteral("dog lazy"))).empty());
    // THE CONTROL that it is the block and not the query: the same words on one line are found.
    const auto oneLine = page({U"the dog lazy afternoon"});
    CHECK(find(*oneLine, query(QStringLiteral("dog lazy"))) == Hits{QStringLiteral("dog lazy")});
}

TEST_CASE("a line end hyphen before a lowercase letter is joined and a compound is not", "[search][A-59]")
{
    PageTextModel model(0);
    run(model, U"the exam", 72.0, 700.0);
    run(model, U"-", 72.0 + 8 * kAdvance * 10.0, 700.0, 10.0, static_cast<std::uint8_t>(CharFlag::Hyphen));
    run(model, U"ple of a well-known trick", 72.0, 688.0);
    const auto reading = PageReading::build(std::make_shared<const PageTextModel>(std::move(model)));
    REQUIRE(reading->layout().blocks.size() == 1);

    const Hits joined = find(*reading, query(QStringLiteral("example")));
    REQUIRE(joined.size() == 1);
    CHECK(joined.front() == QStringLiteral("exam- ple"));   // the span holds the hyphen it joined over
    // THE CONTROL: a hyphen in the middle of a line is the word's own.
    CHECK(find(*reading, query(QStringLiteral("wellknown"))).empty());
    CHECK(find(*reading, query(QStringLiteral("well-known"))) == Hits{QStringLiteral("well-known")});
}

TEST_CASE("a ligature glyph is found by the letters it stands for", "[search][A-56]")
{
    const auto reading = page({U"ﬁsh and chips"});   // ﬁsh
    const Hits hits = find(*reading, query(QStringLiteral("fish")));
    REQUIRE(hits.size() == 1);
    CHECK(hits.front() == QStringLiteral("ﬁsh"));   // the whole ligature glyph, not half of it
    CHECK(find(*reading, query(QStringLiteral("ﬁsh"))).size() == 1);
}

// =============================================================================================
// A-60 -- a selection scope, at the match
// =============================================================================================

TEST_CASE("inside a selection only hits inside it are found", "[search][A-60]")
{
    const auto reading = page({U"lorem one lorem two lorem"});
    // The selection covers the middle `lorem` and nothing else.
    const int middle = 10;   // `lorem one ` is ten positions
    SearchQuery q = query(QStringLiteral("lorem"));
    q.scope = SearchScope::within(TextSelection::flow(TextPosition{0, middle}, TextPosition{0, middle + 5}));
    const auto compiled = SearchEngine::compile(q);
    REQUIRE(compiled);
    const auto searchable = SearchablePage::build(*reading, compiled.value().options());
    const auto hits = SearchEngine::findOnPage(compiled.value(), *searchable, *reading);
    REQUIRE(hits.size() == 1);
    CHECK(hits.front().reading == Span{middle, middle + 5});
    // THE CONTROL: the whole document finds all three.
    CHECK(find(*reading, query(QStringLiteral("lorem"))).size() == 3);
    // And a hit that only STARTS inside the selection is not inside it.
    q.scope = SearchScope::within(TextSelection::flow(TextPosition{0, middle}, TextPosition{0, middle + 3}));
    const auto partial = SearchEngine::compile(q);
    REQUIRE(partial);
    CHECK(SearchEngine::findOnPage(partial.value(), *searchable, *reading).empty());
}

// =============================================================================================
// A-21 (find's half) -- unmapped glyphs match nothing
// =============================================================================================

TEST_CASE("glyphs with no Unicode are matched by nothing", "[search][A-21]")
{
    PageTextModel model(0);
    run(model, U"ab", 72.0, 700.0);
    // Three unmapped glyphs -- stored as U+FFFD by the model's own rule, whatever they arrived as.
    run(model, U"中日目", 72.0 + 3 * kAdvance * 10.0, 700.0, 10.0,
        static_cast<std::uint8_t>(CharFlag::Unmapped));
    const auto reading = PageReading::build(std::make_shared<const PageTextModel>(std::move(model)));
    REQUIRE(reading->model().unmappedCount() == 3);

    // Not by the character it is stored as, not by the engine's guess, not even by `.`.
    CHECK(find(*reading, query(QStringLiteral("�"))).empty());
    CHECK(find(*reading, query(QStringLiteral("中"))).empty());
    SearchFlags regex;
    regex.useRegex = true;
    // THE CONTROL: `.` finds the two mapped letters, so it is matching -- and nothing else.
    CHECK(find(*reading, query(QStringLiteral("."), regex)) == Hits{QStringLiteral("a"), QStringLiteral("b")});
}

// =============================================================================================
// A-41 (search's clause) -- hostile geometry
// =============================================================================================

TEST_CASE("search survives hostile geometry within its bounds", "[search][A-41]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    PageTextModel model(0);
    const std::vector<PdfRect> boxes{
        PdfRect{PdfPoint{nan, 700.0}, PdfSize{5.0, 10.0}}, PdfRect{PdfPoint{72.0, inf}, PdfSize{5.0, 10.0}},
        PdfRect{PdfPoint{72.0, 700.0}, PdfSize{-5.0, -10.0}}, PdfRect{PdfPoint{72.0, 700.0}, PdfSize{0.0, 0.0}},
        PdfRect{PdfPoint{1e300, -1e300}, PdfSize{1e300, 1e300}}};
    const std::u32string letters = U"loremipsum";
    for (int i = 0; i < 200; ++i) {
        TextChar c;
        c.codepoint = letters[static_cast<std::size_t>(i) % letters.size()];
        c.fontSize = (i % 3 == 0) ? nan : 10.0;
        c.box = boxes[static_cast<std::size_t>(i) % boxes.size()];
        c.origin = c.box.origin;
        model.append(c);
    }
    run(model, U"lorem ipsum lorem", 72.0, 600.0);
    const auto reading = PageReading::build(std::make_shared<const PageTextModel>(std::move(model)));

    for (const bool regex : {false, true}) {
        for (const bool whole : {false, true}) {
            SearchFlags flags;
            flags.useRegex = regex;
            flags.wholeWords = whole;
            const auto compiled = SearchEngine::compile(query(regex ? QStringLiteral("l.r") : QStringLiteral("lor"), flags));
            REQUIRE(compiled);
            const auto searchable = SearchablePage::build(*reading, compiled.value().options());
            for (const SearchHit& hit : SearchEngine::findOnPage(compiled.value(), *searchable, *reading)) {
                CHECK(hit.reading.begin >= 0);
                CHECK(hit.reading.begin < hit.reading.end);
                CHECK(hit.reading.end <= reading->size());
            }
        }
    }
    // The finite line is still searched: the hostile characters around it did not take it with them.
    CHECK(find(*reading, query(QStringLiteral("lorem ipsum lorem"))).size() == 1);
}
