// tst_layout -- `core/text/LayoutAnalyzer` on SYNTHETIC geometry, headless. Stage 5 T4, decision D5-6;
// guide cases A-37, A-39 and A-41 (`Stage5-TextSearch-Testing-Guide.md` §5.6).
//
// Each case builds the geometry it claims about and nothing else, so a failure names the rule that
// broke. The same analyzer over real extraction and the generator's labels is `tst_layoutcorpus.cpp`
// (A-38); this file is where each threshold's CONTROL lives -- two lines far apart are not merged,
// aligned prose is not a table.

#include <catch2/catch_test_macros.hpp>

#include "core/text/LayoutAnalyzer.h"
#include "core/text/PageTextModel.h"
#include "core/text/TextNormalizer.h"
#include "tests/fuzz/TextPipelineFuzzBody.h"

#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <vector>

using rawpage::core::PdfPoint;
using rawpage::core::PdfRect;
using rawpage::core::PdfSize;
using rawpage::core::text::LayoutAnalyzer;
using rawpage::core::text::PageLayout;
using rawpage::core::text::PageTextModel;
using rawpage::core::text::TextChar;
using rawpage::core::text::TextNormalizer;
using rawpage::core::text::WritingDirection;

namespace {

// Helvetica-like boxes: 0.55 em wide, from 0.21 em below the baseline to 0.9 em above.
void run(PageTextModel& model, const std::u32string& text, double x, double baseline, double size = 10.0,
         double advance = 0.55)
{
    for (const char32_t cp : text) {
        TextChar c;
        c.codepoint = cp;
        c.fontSize = size;
        c.origin = PdfPoint{x, baseline};
        c.box = PdfRect{PdfPoint{x, baseline - 0.21 * size}, PdfSize{advance * size, 1.11 * size}};
        model.append(c);
        x += advance * size;
    }
}

QString text(const PageTextModel& model, const PageLayout& layout)
{
    return LayoutAnalyzer::readingText(model, layout);
}

} // namespace

TEST_CASE("layout keeps a jittered baseline and a superscript in one line", "[core][text][A-37]")
{
    PageTextModel model;
    run(model, U"alpha", 50, 700.0);
    run(model, U"beta", 80, 701.0);    // +1 pt
    run(model, U"gamma", 105, 699.0);  // -1 pt
    // A superscript: smaller, raised a third of an em.
    run(model, U"2", 134, 703.5, 6.0);
    const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
    REQUIRE(layout.lines.size() == 1);
    CHECK(layout.lines[0].chars.size() == model.size());

    SECTION("THE CONTROL: two lines 30 pt apart are two lines")
    {
        PageTextModel apart;
        run(apart, U"first line", 50, 700.0);
        run(apart, U"second line", 50, 670.0);
        const PageLayout two = LayoutAnalyzer::analyze(apart, nullptr);
        CHECK(two.lines.size() == 2);
        CHECK(text(apart, two) == QStringLiteral("first line\n\nsecond line"));
    }
}

TEST_CASE("layout separates two columns at a 12 pt gutter and not at a 2 pt gap", "[core][text][A-37]")
{
    // PROSE in each column -- six words a line. Two columns of two-word lines are, by the analyzer's
    // own rule, a two-column TABLE, and nothing in the geometry says otherwise.
    PageTextModel model;
    for (int row = 0; row < 6; ++row) {
        const double y = 700.0 - row * 12.0;
        const std::u32string tag(1, static_cast<char32_t>(U'a' + row));
        run(model, U"left column words of prose " + tag, 50, y);
        run(model, U"right column words of prose " + tag, 50 + 28 * 5.5 + 12, y);
    }
    const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
    CHECK(layout.lines.size() == 12);
    CHECK(layout.tables.empty());
    CHECK(layout.columns == 2);
    // Read column by column: every left line before any right one.
    const QString reading = text(model, layout);
    CHECK(reading.indexOf(QStringLiteral("prose f")) < reading.indexOf(QStringLiteral("right column words of prose a")));

    SECTION("THE CONTROL: a 2 pt gap does not split a line")
    {
        PageTextModel close;
        run(close, U"one", 50, 700.0);
        run(close, U"two", 50 + 3 * 5.5 + 2.0, 700.0);
        const PageLayout joined = LayoutAnalyzer::analyze(close, nullptr);
        REQUIRE(joined.lines.size() == 1);
        // ... but it IS a word gap, which the content drew no space for.
        CHECK(text(close, joined) == QStringLiteral("one two"));
    }
}

TEST_CASE("layout reads a column longer than its neighbour whole before the next column", "[core][text][A-37][I-07]")
{
    // S5 guide I-07: the RIGHT column ends above a paragraph break of the LEFT one, so a full-width gap
    // exists there -- and horizontal-first XY-cut read the left column's top, the right column, then the rest
    // of the left. Lines 13 pt apart, paragraphs 26; the right column is one paragraph of four lines.
    const double right = 50 + 36 * 5.5 + 12;   // the left lines are 36 glyphs wide, then a 12 pt gutter
    const auto paragraph = [](PageTextModel& model, const std::u32string& tag, double x, double top, int lines) {
        for (int line = 0; line < lines; ++line)
            run(model, U"words of prose in paragraph " + tag + U" " + std::u32string(1, static_cast<char32_t>(U'a' + line)), x,
                top - line * 13.0);
    };
    PageTextModel model;
    paragraph(model, U"pone", 50, 700.0, 3);     // baselines 700 .. 674
    paragraph(model, U"ptwo", 50, 648.0, 3);     // 648 .. 622, reaching below the right column's end
    paragraph(model, U"pthree", 50, 596.0, 3);   // after a 15 pt gap no right-column text crosses
    paragraph(model, U"qone", right, 700.0, 4);  // 700 .. 661
    const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
    const QString reading = text(model, layout);
    INFO(reading.toStdString());
    CHECK(reading.indexOf(QStringLiteral("pone")) < reading.indexOf(QStringLiteral("ptwo")));
    CHECK(reading.indexOf(QStringLiteral("ptwo")) < reading.indexOf(QStringLiteral("pthree")));
    // THE LEFT COLUMN WHOLE, then the right.
    CHECK(reading.indexOf(QStringLiteral("pthree")) < reading.indexOf(QStringLiteral("qone")));

    SECTION("THE CONTROL: a line set well below the columns, and a spanning one, still end them")
    {
        PageTextModel apart = model;
        run(apart, U"spanning line across both columns of the page body", 50, 545.0);   // a paragraph gap below
        run(apart, U"footer", 50, 470.0);                                                // far below
        const PageLayout ended = LayoutAnalyzer::analyze(apart, nullptr);
        const QString read = text(apart, ended);
        INFO(read.toStdString());
        CHECK(read.indexOf(QStringLiteral("qone")) < read.indexOf(QStringLiteral("spanning")));
        CHECK(read.indexOf(QStringLiteral("spanning")) < read.indexOf(QStringLiteral("footer")));
        // ... and under the left column alone, but far below it, a footer is read after BOTH columns.
        PageTextModel footed = model;
        run(footed, U"footer", 50, 520.0);   // 39 pt below the left column
        const QString last = text(footed, LayoutAnalyzer::analyze(footed, nullptr));
        INFO(last.toStdString());
        CHECK(last.indexOf(QStringLiteral("qone")) < last.indexOf(QStringLiteral("footer")));
    }
}

TEST_CASE("layout finds a ruled table and an aligned one and not aligned prose", "[core][text][A-37]")
{
    SECTION("ruled cells form a grid")
    {
        PageTextModel model;
        const double xs[] = {50, 150, 250, 350};
        for (int r = 0; r <= 3; ++r)
            model.addRule(PdfRect{PdfPoint{50, 600.0 + r * 20}, PdfSize{300, 0.5}});
        for (const double x : xs) model.addRule(PdfRect{PdfPoint{x, 600}, PdfSize{0.5, 60}});
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                run(model, U"cell" + std::u32string(1, static_cast<char32_t>(U'a' + r * 3 + c)), xs[c] + 4,
                    606.0 + r * 20);
        const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
        REQUIRE(layout.tables.size() == 1);
        CHECK(layout.tables[0].ruled);
        CHECK(layout.tables[0].rows == 3);
        CHECK(layout.tables[0].columns == 3);
        // One block, cells row by row: the top row first.
        CHECK(text(model, layout).startsWith(QStringLiteral("cellg\ncellh\ncelli")));
    }
    SECTION("an unruled table is found by alignment")
    {
        PageTextModel model;
        for (int r = 0; r < 4; ++r) {
            const double y = 600.0 - r * 15.0;
            run(model, U"key" + std::u32string(1, static_cast<char32_t>(U'a' + r)), 50, y);
            run(model, U"on", 150, y);
            run(model, U"Page", 220, y);
        }
        const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
        REQUIRE(layout.tables.size() == 1);
        CHECK_FALSE(layout.tables[0].ruled);
        CHECK(layout.tables[0].rows == 4);
        CHECK(layout.tables[0].columns == 3);
    }
    SECTION("THE CONTROL: aligned prose is not a table")
    {
        // Two columns of long lines align exactly as table cells do; their length is what differs.
        PageTextModel model;
        for (int r = 0; r < 6; ++r) {
            const double y = 600.0 - r * 12.0;
            run(model, U"several words of running prose here", 50, y);
            run(model, U"and more words of prose beside it", 50 + 35 * 5.5 + 14, y);
        }
        const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
        CHECK(layout.tables.empty());
        CHECK(layout.columns == 2);
    }
}

TEST_CASE("layout reorders a right-to-left line into logical order", "[core][text][A-37]")
{
    // As T2 measured the ENGINE would not: glyphs in page order, left to right, `תיב םולש` -- the
    // visual form of `שלום בית` -- plus a number inside, which stays left to right.
    PageTextModel model;
    run(model, U"תיב 123 םולש", 300, 700.0);
    const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
    REQUIRE(layout.lines.size() == 1);
    CHECK(layout.lines[0].direction == WritingDirection::RightToLeft);
    CHECK(text(model, layout) == QString::fromUtf8("\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D 123 "
                                                   "\xD7\x91\xD7\x99\xD7\xAA"));   // שלום 123 בית
}

TEST_CASE("layout reads vertical CJK top to bottom and its columns right to left", "[core][text][A-37]")
{
    PageTextModel model;
    const auto column = [&](const std::u32string& glyphs, double x) {
        double top = 700.0;
        for (const char32_t cp : glyphs) {
            TextChar c;
            c.codepoint = cp;
            c.fontSize = 20.0;
            c.origin = PdfPoint{x, top - 17.0};
            c.box = PdfRect{PdfPoint{x, top - 20.0}, PdfSize{20.0, 20.0}};
            model.append(c);
            top -= 20.0;
        }
    };
    column(U"一二三十", 200);   // written first, read SECOND
    column(U"日田中目", 240);   // the right-hand column, read first
    const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
    REQUIRE(layout.lines.size() == 2);
    CHECK(layout.lines[0].direction == WritingDirection::TopToBottom);
    CHECK(text(model, layout) == QString::fromUtf8("\xE6\x97\xA5\xE7\x94\xB0\xE4\xB8\xAD\xE7\x9B\xAE\n"
                                                   "\xE4\xB8\x80\xE4\xBA\x8C\xE4\xB8\x89\xE5\x8D\x81"));

    SECTION("THE CONTROL: three lines that each begin with a wide letter are not a vertical line")
    {
        PageTextModel latin;
        for (int r = 0; r < 3; ++r) {
            // A square-ish `M` at the start of each line, stacked with no gap -- and a letter beside it.
            run(latin, U"Mark", 50, 700.0 - r * 11.0, 10.0, 0.9);
        }
        const PageLayout layout2 = LayoutAnalyzer::analyze(latin, nullptr);
        for (const auto& line : layout2.lines) CHECK(line.direction == WritingDirection::LeftToRight);
        CHECK(layout2.lines.size() == 3);
    }
}

TEST_CASE("layout is deterministic under a shuffled model and passes no tag tree", "[core][text][A-39]")
{
    // EVERY STAGE 5 CALLER PASSES `nullptr` FOR THE TAG TREE (D5-6): `core/doc/TagTree` is Stage 7's,
    // so a tagged document is ordered by geometry like any other. This case passes null as they all
    // do; what a tree would change is Stage 7's to measure (convention 19).
    PageTextModel model;
    for (int r = 0; r < 8; ++r) run(model, U"row text " + std::u32string(1, static_cast<char32_t>(U'a' + r)), 50, 700 - r * 12.0);
    run(model, U"שלום", 300, 500.0);
    const QString original = text(model, LayoutAnalyzer::analyze(model, nullptr));

    std::vector<TextChar> chars = model.chars();
    std::mt19937 random(1234);
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::shuffle(chars.begin(), chars.end(), random);
        PageTextModel shuffled;
        for (const TextChar& c : chars) shuffled.append(c);
        CHECK(text(shuffled, LayoutAnalyzer::analyze(shuffled, nullptr)) == original);
    }
}

TEST_CASE("layout survives hostile geometry within its bounds", "[core][text][A-41]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    PageTextModel model;
    const auto add = [&](PdfRect box, PdfPoint origin, double size) {
        TextChar c;
        c.codepoint = U'x';
        c.box = box;
        c.origin = origin;
        c.fontSize = size;
        model.append(c);
    };
    add(PdfRect{PdfPoint{nan, 0}, PdfSize{1, 1}}, PdfPoint{0, 0}, 10);
    add(PdfRect{PdfPoint{0, 0}, PdfSize{inf, 1}}, PdfPoint{0, 0}, 10);
    add(PdfRect{PdfPoint{0, 0}, PdfSize{1, 1}}, PdfPoint{-inf, 0}, 10);
    add(PdfRect{PdfPoint{5, 5}, PdfSize{0, 0}}, PdfPoint{5, 5}, 0);        // zero size
    add(PdfRect{PdfPoint{5, 5}, PdfSize{-3, -4}}, PdfPoint{5, 5}, -12);    // negative size
    add(PdfRect{PdfPoint{1e300, 1e300}, PdfSize{1e300, 1e300}}, PdfPoint{1e300, 1e300}, 1e300);
    for (int i = 0; i < 20; ++i) add(PdfRect{PdfPoint{10, 10}, PdfSize{5, 5}}, PdfPoint{10, 10}, 5);   // identical
    model.addRule(PdfRect{PdfPoint{nan, 0}, PdfSize{100, 0.5}});

    const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
    CHECK(layout.unplaced.size() >= 3);
    std::size_t placed = 0;
    for (const auto& line : layout.lines) placed += line.chars.size();
    CHECK(placed + layout.unplaced.size() == model.size());
    const auto stream = LayoutAnalyzer::readingStream(layout);
    (void)TextNormalizer().normalize(model, stream);

    SECTION("a million characters on one line finish, and past the bound the page degrades")
    {
        PageTextModel huge;
        for (int i = 0; i < 1000000; ++i) {
            TextChar c;
            c.codepoint = U'a' + static_cast<char32_t>(i % 26);
            c.fontSize = 10;
            c.origin = PdfPoint{i * 5.5, 700};
            c.box = PdfRect{PdfPoint{i * 5.5, 698}, PdfSize{5.5, 11}};
            huge.append(c);
        }
        const auto start = std::chrono::steady_clock::now();
        const PageLayout big = LayoutAnalyzer::analyze(huge, nullptr);
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        CHECK(big.degraded);   // 1,000,000 > kMaxCharsLaidOut
        std::size_t total = 0;
        for (const auto& line : big.lines) total += line.chars.size();
        CHECK(total == huge.size());
        CHECK(seconds < 10.0);
    }
}

TEST_CASE("the text pipeline fuzz body holds its invariants on adversarial bytes", "[core][text][A-40]")
{
    // `fuzz_text_pipeline`'s body, replayed here because this machine cannot build libFuzzer
    // (`tests/fuzz/TextPipelineFuzzBody.h`). NOT fuzzing: a fixed seed and a fixed count, so a run is
    // repeatable and a Linux finding can be added below and reproduced in x64-debug.
    std::mt19937 random(20260930);
    std::size_t characters = 0;
    for (int round = 0; round < 3000; ++round) {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(random() % 1500));
        for (auto& b : bytes) b = static_cast<std::uint8_t>(random());
        // Every tenth input is dense in the special coordinates: NaN, +inf, -inf.
        if (round % 10 == 0)
            for (std::size_t i = 2; i + 1 < bytes.size(); i += 12) {
                bytes[i] = 0xFD + static_cast<std::uint8_t>(random() % 3);
                bytes[i + 1] = 0x7F;
            }
        const rawpage::fuzz::TextPipelineVerdict verdict =
            rawpage::fuzz::textPipelineFuzzOnce(bytes.data(), bytes.size());
        INFO("round " << round << ": " << verdict.failure);
        REQUIRE(verdict.ok);
        characters += verdict.characters;
    }
    CHECK(characters > 100000);

    SECTION("every box identical, and one stacked column of them")
    {
        std::vector<std::uint8_t> same;
        for (int i = 0; i < 500; ++i) {
            const std::uint8_t record[12] = {0x05, static_cast<std::uint8_t>(i), 40, 0, 40, 0, 20, 0, 20, 0, 10, 0};
            same.insert(same.end(), record, record + 12);
        }
        const rawpage::fuzz::TextPipelineVerdict verdict = rawpage::fuzz::textPipelineFuzzOnce(same.data(), same.size());
        INFO(verdict.failure);
        CHECK(verdict.ok);
    }
}
