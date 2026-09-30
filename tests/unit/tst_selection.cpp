// tst_selection -- `core/text/TextSelection` on SYNTHETIC geometry, headless. Stage 5 T5, decisions D5-8
// and D5-9; the headless halves of guide cases A-43 … A-47, A-49 and A-41's selection clause
// (`Stage5-TextSearch-Testing-Guide.md` §5.7, §5.6).
//
// WHAT THIS FILE CANNOT CLAIM, named here (convention 19): every box in it is the case's own arithmetic,
// so nothing here says a highlight lands on INK. That is rule 6's, and it is
// `tests/composition/tst_selecttool.cpp`'s, over the real engine and the rendered raster. What this file
// owns is the arithmetic of a range -- which characters a caret, a unit or a rectangle takes, and what
// a copy of them writes -- with a control for every rule.

#include <catch2/catch_test_macros.hpp>

#include "core/text/LayoutAnalyzer.h"
#include "core/text/PageTextModel.h"
#include "core/text/TextSelection.h"

#include <QRegularExpression>

#include <cmath>
#include <limits>
#include <memory>
#include <string>

using rawpage::core::PdfPoint;
using rawpage::core::PdfRect;
using rawpage::core::PdfSize;
using namespace rawpage::core::text;

namespace {

constexpr double kAdvance = 0.55;

// Helvetica-like boxes, as `tst_layout.cpp` builds them: 0.55 em wide, 0.21 em below the baseline to
// 0.9 em above. A space is a shown character, as the content draws one.
void run(PageTextModel& model, const std::u32string& text, double x, double baseline, double size = 10.0,
         int font = -1, std::uint8_t flags = 0)
{
    for (const char32_t cp : text) {
        TextChar c;
        c.codepoint = cp;
        c.fontSize = size;
        c.font = font;
        c.flags = flags;
        c.origin = PdfPoint{x, baseline};
        c.box = PdfRect{PdfPoint{x, baseline - 0.21 * size}, PdfSize{kAdvance * size, 1.11 * size}};
        model.append(c);
        x += kAdvance * size;
    }
}

// Right-to-left, as producers draw it: the logical string's LAST letter leftmost.
void runRtl(PageTextModel& model, const std::u32string& logical, double rightEdge, double baseline, double size = 10.0)
{
    double x = rightEdge;
    for (const char32_t cp : logical) {
        x -= kAdvance * size;
        TextChar c;
        c.codepoint = cp;
        c.fontSize = size;
        c.origin = PdfPoint{x, baseline};
        c.box = PdfRect{PdfPoint{x, baseline - 0.21 * size}, PdfSize{kAdvance * size, 1.11 * size}};
        model.append(c);
    }
}

std::shared_ptr<const PageReading> read(PageTextModel model)
{
    return PageReading::build(std::make_shared<const PageTextModel>(std::move(model)));
}

// The centre of the n-th character of a run that starts at `x`.
PdfPoint charCentre(double x, int n, double baseline, double size = 10.0)
{
    return PdfPoint{x + (n + 0.5) * kAdvance * size, baseline + 0.3 * size};
}

QString textOf(const PageReading& page, Span span)
{
    QString out;
    for (int p = span.begin; p < span.end; ++p) {
        const char32_t cp = page.charAt(p).codepoint;
        out += QString::fromUcs4(&cp, 1);
    }
    return out;
}

DocumentText documentOf(std::vector<std::shared_ptr<const PageReading>> pages)
{
    DocumentText text(static_cast<int>(pages.size()));
    for (auto& page : pages) REQUIRE(text.insert(page));
    return text;
}

} // namespace

// =====================================================================================================
// Hit-testing -- a caret between characters, on the side the point falls
// =====================================================================================================

TEST_CASE("a caret lands on the side of the character the point is on", "[core][text][selection]")
{
    PageTextModel model(0);
    run(model, U"alpha beta", 50, 700.0);
    const auto page = read(std::move(model));
    REQUIRE(page->size() == 10);

    // The LEFT half of `b` (index 6) is the caret before it; the RIGHT half the caret after it.
    const PdfPoint b = charCentre(50, 6, 700.0);
    CHECK(page->caretAt(PdfPoint{b.x - 1.0, b.y}) == 6);
    CHECK(page->caretAt(PdfPoint{b.x + 1.0, b.y}) == 7);
    // Beyond the line's ends: the first and last carets, not a character somewhere in between.
    CHECK(page->caretAt(PdfPoint{10.0, 704.0}) == 0);
    CHECK(page->caretAt(PdfPoint{500.0, 704.0}) == 10);
    // Above the line and between its words still belongs to the line.
    CHECK(page->caretAt(PdfPoint{b.x - 1.0, 760.0}) == 6);

    SECTION("a right-to-left letter reads from its RIGHT half")
    {
        PageTextModel rtl(0);
        runRtl(rtl, U"שלום", 300.0, 700.0);   // שלום, shin rightmost
        const auto hebrew = read(std::move(rtl));
        REQUIRE(hebrew->size() == 4);
        // The reading order is logical: shin first.
        CHECK(hebrew->charAt(0).codepoint == U'ש');
        const PdfRect& shin = hebrew->charAt(0).box;
        const double cx = shin.left() + shin.size.width / 2.0;
        CHECK(hebrew->caretAt(PdfPoint{cx + 1.0, 703.0}) == 0);   // right half: BEFORE shin
        CHECK(hebrew->caretAt(PdfPoint{cx - 1.0, 703.0}) == 1);   // left half: after it
        // And the control: the same half rule read left to right would say the opposite.
        CHECK_FALSE(hebrew->caretAt(PdfPoint{cx + 1.0, 703.0}) == 1);
    }
}

// =====================================================================================================
// A-43's arithmetic -- word, line, block
// =====================================================================================================

TEST_CASE("a unit is the word the line or the block under the point", "[core][text][selection][A-43]")
{
    PageTextModel model(0);
    run(model, U"Lorem ipsum, dolor.", 50, 700.0);
    run(model, U"sit amet again", 50, 688.0);
    run(model, U"A second block here", 50, 640.0);
    const auto page = read(std::move(model));

    const PdfPoint onIpsum = charCentre(50, 8, 700.0);
    const TextUnit word = unitAt(*page, onIpsum, Granularity::Word);
    CHECK(textOf(*page, Span{word.begin.caret, word.end.caret}) == QStringLiteral("ipsum"));   // the comma is not in it

    const TextUnit line = unitAt(*page, onIpsum, Granularity::Line);
    CHECK(textOf(*page, Span{line.begin.caret, line.end.caret}) == QStringLiteral("Lorem ipsum, dolor."));

    const TextUnit block = unitAt(*page, onIpsum, Granularity::Block);
    CHECK(textOf(*page, Span{block.begin.caret, block.end.caret}) == QStringLiteral("Lorem ipsum, dolor.sit amet again"));

    // The control: the second block is not in the first's unit, and a click on punctuation is one character.
    const TextUnit other = unitAt(*page, charCentre(50, 3, 640.0), Granularity::Block);
    CHECK(textOf(*page, Span{other.begin.caret, other.end.caret}) == QStringLiteral("A second block here"));
    const TextUnit comma = unitAt(*page, charCentre(50, 11, 700.0), Granularity::Word);
    CHECK(textOf(*page, Span{comma.begin.caret, comma.end.caret}) == QStringLiteral(","));

    // A drag that started on a word extends by words: the union of the two units.
    const TextUnit toSit = unitAt(*page, charCentre(50, 1, 688.0), Granularity::Word);
    const TextSelection spanning = TextSelection::spanning(word, toSit);
    const DocumentText text = documentOf({page});
    CHECK(spanning.copy(text).plain == QStringLiteral("ipsum, dolor.\nsit"));
}

// =====================================================================================================
// A-44 / A-45's arithmetic -- across a page break, and in reading order across columns
// =====================================================================================================

TEST_CASE("a flow selection crosses a page boundary and copies in order", "[core][text][selection][A-44]")
{
    PageTextModel first(0);
    run(first, U"end of page one", 50, 120.0);
    PageTextModel second(1);
    run(second, U"start of page two", 50, 780.0);
    run(second, U"not selected", 50, 740.0);
    const auto page0 = read(std::move(first));
    const auto page1 = read(std::move(second));
    const DocumentText text = documentOf({page0, page1});

    const TextSelection selection = TextSelection::flow(TextPosition{1, page1->caretAt(charCentre(50, 16, 780.0)) },
                                                        TextPosition{0, page0->caretAt(charCentre(50, 0, 120.0))});
    // The anchor after the focus is the same selection: carets are ordered.
    CHECK(selection.start().page == 0);
    CHECK(selection.end().page == 1);
    const CopiedText copied = selection.copy(text);
    CHECK(copied.plain == QStringLiteral("end of page one\n\nstart of page tw"));
    CHECK(copied.missingPages.empty());

    SECTION("a page not extracted is reported and nothing is invented for it")
    {
        DocumentText partial(3);
        REQUIRE(partial.insert(page0));
        const TextSelection all = TextSelection::wholeDocument(3);
        const CopiedText some = all.copy(partial);
        CHECK(some.missingPages == std::vector<int>{1, 2});
        CHECK(some.plain == QStringLiteral("end of page one"));
    }
}

TEST_CASE("a drag between columns follows the reading order not the rectangle", "[core][text][selection][A-45]")
{
    PageTextModel model(0);
    // Two columns, a 20 pt gutter: column 1 at x=50, column 2 at x=250, four lines each.
    const std::u32string left[] = {U"col one line a", U"col one line b", U"col one line c", U"col one line d"};
    const std::u32string right[] = {U"col two line a", U"col two line b", U"col two line c", U"col two line d"};
    for (int i = 0; i < 4; ++i) {
        run(model, left[i], 50, 700.0 - 12.0 * i);
        run(model, right[i], 250, 700.0 - 12.0 * i);
    }
    const auto page = read(std::move(model));
    REQUIRE(page->layout().columns == 2);

    // From the foot of column 1 (inside line d) to the head of column 2 (inside line a).
    const TextPosition from{0, page->caretAt(charCentre(50, 8, 664.0))};
    const TextPosition to{0, page->caretAt(charCentre(250, 7, 700.0))};
    const CopiedText copied = TextSelection::flow(from, to).copy(documentOf({page}));
    CHECK(copied.plain == QStringLiteral("line d\n\ncol two"));

    // THE CONTROL -- what a selection by rectangle would have taken: column 2's lower lines. None of it.
    CHECK_FALSE(copied.plain.contains(QStringLiteral("line b")));
    CHECK_FALSE(copied.plain.contains(QStringLiteral("line c")));
}

// =====================================================================================================
// A-46's arithmetic -- a column selection is a table
// =====================================================================================================

TEST_CASE("a column selection takes the cells under the rectangle and copies a table", "[core][text][selection][A-46]")
{
    PageTextModel model(0);
    // A three-column table of short cells, four rows, 18 pt apart.
    const char32_t* cells[4][3] = {{U"Item", U"Qty", U"Price"},
                                   {U"Apple", U"3", U"1.20"},
                                   {U"Pear", U"12", U"0.80"},
                                   {U"Plum", U"7", U"2.10"}};
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 3; ++c) run(model, cells[r][c], 60 + 120 * c, 700.0 - 18.0 * r);
    // A LINE THE RECTANGLE DOES NOT CROSS, although its characters lie inside the rectangle's width --
    // the rule's control (convention 22): a column takes the lines it crosses and no others. Without it
    // this case passed with the crossing test planted out, because every other line on the page is
    // crossed.
    run(model, U"A caption above the table, wider than it", 60, 760.0);
    const auto page = read(std::move(model));
    const DocumentText text = documentOf({page});

    // A rectangle over the SECOND column, top to bottom.
    const TextSelection column = TextSelection::column(0, PdfRect{PdfPoint{175, 640}, PdfSize{60, 80}});
    const CopiedText copied = column.copy(text);
    CHECK(copied.isTable);
    CHECK(copied.plain == QStringLiteral("Qty\n3\n12\n7"));
    CHECK(copied.html.count(QStringLiteral("<tr>")) == 4);
    CHECK(copied.html.count(QStringLiteral("<td>")) == 4);

    SECTION("two columns are a two-column table, tab-separated")
    {
        const CopiedText two = TextSelection::column(0, PdfRect{PdfPoint{175, 640}, PdfSize{150, 80}}).copy(text);
        CHECK(two.plain == QStringLiteral("Qty\tPrice\n3\t1.20\n12\t0.80\n7\t2.10"));
        CHECK(two.html.count(QStringLiteral("<td>")) == 8);
    }

    SECTION("THE CONTROL: the same two points as a flow select in reading order, not a column")
    {
        const TextPosition a{0, page->caretAt(PdfPoint{176, 703})};
        const TextPosition b{0, page->caretAt(PdfPoint{234, 649})};
        const CopiedText flow = TextSelection::flow(a, b).copy(text);
        CHECK_FALSE(flow.isTable);
        CHECK(flow.plain.contains(QStringLiteral("Price")));   // reading order passes through the rest of row 1
    }
}

// =====================================================================================================
// A-47's arithmetic -- right-to-left copies in logical order
// =====================================================================================================

TEST_CASE("a right-to-left line copies in logical order and keeps its digits in order", "[core][text][selection][A-47]")
{
    PageTextModel model(0);
    // `בית 123 שלום` in logical order, drawn right to left with the number left to right inside it.
    double x = 400.0;
    const auto glyph = [&model](char32_t cp, double left) {
        TextChar c;
        c.codepoint = cp;
        c.fontSize = 10.0;
        c.origin = PdfPoint{left, 700.0};
        c.box = PdfRect{PdfPoint{left, 697.9}, PdfSize{5.5, 11.1}};
        model.append(c);
    };
    for (const char32_t cp : std::u32string(U"בית")) glyph(cp, x -= 5.5);   // בית
    glyph(U' ', x -= 5.5);
    x -= 16.5;
    double digits = x;
    for (const char32_t cp : std::u32string(U"123")) { glyph(cp, digits); digits += 5.5; }
    glyph(U' ', x -= 5.5);
    for (const char32_t cp : std::u32string(U"שלום")) glyph(cp, x -= 5.5);   // שלום
    const auto page = read(std::move(model));

    const DocumentText text = documentOf({page});
    const CopiedText whole = TextSelection::wholeDocument(1).copy(text);
    CHECK(whole.plain == QString::fromUtf8("בית 123 שלום"));
    CHECK(whole.html.contains(QStringLiteral("<p dir=\"rtl\">")));

    // A drag from the line's RIGHT edge leftwards starts at its first logical character.
    const TextPosition right{0, page->caretAt(PdfPoint{401.0, 703.0})};
    CHECK(right.caret == 0);
}

// =====================================================================================================
// A-49's arithmetic -- one copy, three flavours
// =====================================================================================================

TEST_CASE("a copy writes lines as seen blocks as paragraphs and each run's style", "[core][text][selection][A-49]")
{
    PageTextModel model(0);
    FontIdentity bold;
    bold.name = QStringLiteral("Helvetica-Bold");
    FontIdentity regular;
    regular.name = QStringLiteral("ABCDEF+Helvetica");
    const int boldFont = model.addFont(bold);
    const int regularFont = model.addFont(regular);
    run(model, U"A Heading", 50, 740.0, 18.0, boldFont);
    run(model, U"first line of body", 50, 700.0, 10.0, regularFont);
    run(model, U"second line of body", 50, 688.0, 10.0, regularFont);
    const auto page = read(std::move(model));
    const CopiedText copied = TextSelection::wholeDocument(1).copy(documentOf({page}));

    // PLAIN: the lines a person sees, a blank line between blocks.
    CHECK(copied.plain == QStringLiteral("A Heading\n\nfirst line of body\nsecond line of body"));

    // HTML: two paragraphs, the body's lines joined; the heading 18 pt and bold, the body 10 pt and not;
    // the subset tag and the style suffix gone from the family.
    CHECK(copied.html.count(QStringLiteral("<p>")) == 2);
    CHECK(copied.html.contains(QStringLiteral("first line of body second line of body")));
    const QRegularExpression heading(QStringLiteral("<span style=\"([^\"]*)\">A Heading</span>"));
    const QRegularExpressionMatch h = heading.match(copied.html);
    REQUIRE(h.hasMatch());
    CHECK(h.captured(1).contains(QStringLiteral("font-size:18pt")));
    CHECK(h.captured(1).contains(QStringLiteral("font-weight:bold")));
    CHECK(h.captured(1).contains(QStringLiteral("'Helvetica'")));
    const QRegularExpression body(QStringLiteral("<span style=\"([^\"]*)\">first line"));
    const QRegularExpressionMatch b = body.match(copied.html);
    REQUIRE(b.hasMatch());
    CHECK(b.captured(1).contains(QStringLiteral("font-size:10pt")));
    CHECK_FALSE(b.captured(1).contains(QStringLiteral("bold")));
    CHECK_FALSE(copied.html.contains(QStringLiteral("ABCDEF+")));
    // The fragment is marked, so a paste target reads exactly the copied part.
    CHECK(copied.html.contains(QStringLiteral("<!--StartFragment-->")));
    CHECK(copied.html.contains(QStringLiteral("<!--EndFragment-->")));
}

TEST_CASE("the HTML joins a line-final hyphen before a lowercase letter and nowhere else", "[core][text][selection][A-49]")
{
    PageTextModel model(0);
    const auto hyphen = static_cast<std::uint8_t>(CharFlag::Hyphen);
    run(model, U"an exam", 50, 700.0);
    run(model, U"-", 50 + 7 * kAdvance * 10.0, 700.0, 10.0, -1, hyphen);
    run(model, U"ple of North", 50, 688.0);
    run(model, U"-", 50 + 12 * kAdvance * 10.0, 688.0, 10.0, -1, hyphen);
    run(model, U"West and well-known", 50, 676.0);
    const auto page = read(std::move(model));
    const CopiedText copied = TextSelection::wholeDocument(1).copy(documentOf({page}));

    // Plain text keeps what a person sees -- the hyphens and the line breaks.
    CHECK(copied.plain == QStringLiteral("an exam-\nple of North-\nWest and well-known"));
    // HTML is the paragraph: `example` joined, `North-West` kept whole, `well-known` untouched (the control).
    CHECK(copied.html.contains(QStringLiteral("an example of North-West and well-known")));
}

// =====================================================================================================
// A-21's consumers -- an unmapped glyph is never copied
// =====================================================================================================

TEST_CASE("a copy of unmapped glyphs writes nothing and says so", "[core][text][selection][A-21]")
{
    PageTextModel model(0);
    const auto unmapped = static_cast<std::uint8_t>(CharFlag::Unmapped);
    run(model, U"中文字", 50, 700.0, 10.0, -1, unmapped);   // arrives as a guess; stored as U+FFFD
    run(model, U"Caption", 50, 660.0);
    const auto page = read(std::move(model));
    const DocumentText text = documentOf({page});

    const TextSelection overUnmapped = TextSelection::flow(TextPosition{0, 0}, TextPosition{0, 3});
    const CopiedText none = overUnmapped.copy(text);
    CHECK(none.isEmpty());
    CHECK(none.plain.isEmpty());
    CHECK(none.html.isEmpty());
    CHECK(none.unmappedSkipped == 3);
    CHECK_FALSE(none.plain.contains(QChar(0xFFFD)));

    // The control: the mapped caption beside them copies, and the unmapped are still left out.
    const CopiedText all = TextSelection::wholeDocument(1).copy(text);
    CHECK(all.plain == QStringLiteral("Caption"));
    CHECK(all.unmappedSkipped == 3);
}

// =====================================================================================================
// Highlights -- one rectangle per line, the line's height
// =====================================================================================================

TEST_CASE("a highlight is one rectangle per line at the line's height", "[core][text][selection]")
{
    PageTextModel model(0);
    run(model, U"first line", 50, 700.0);
    run(model, U"second line", 50, 688.0);
    const auto page = read(std::move(model));
    const TextSelection selection = TextSelection::flow(TextPosition{0, 6}, TextPosition{0, 16});
    const std::vector<PdfRect> rects = selection.highlightOn(*page);
    REQUIRE(rects.size() == 2);
    // The first runs from `line`'s `l` to the end of its line; the second from the start of the next.
    CHECK(std::abs(rects[0].left() - (50 + 6 * kAdvance * 10.0)) < 1e-9);
    CHECK(std::abs(rects[1].left() - 50.0) < 1e-9);
    CHECK(std::abs(rects[1].right() - (50 + 6 * kAdvance * 10.0)) < 1e-9);
    // The line's own extent across it.
    CHECK(std::abs(rects[0].bottom() - (700.0 - 2.1)) < 1e-9);
    CHECK(std::abs(rects[0].top() - (700.0 + 9.0)) < 1e-9);

    // An empty selection -- a click -- paints nothing.
    CHECK(TextSelection::flow(TextPosition{0, 4}, TextPosition{0, 4}).highlightOn(*page).empty());
    CHECK(TextSelection::flow(TextPosition{0, 4}, TextPosition{0, 4}).isEmpty());
}

// =====================================================================================================
// A-41's selection clause -- hostile geometry is survived
// =====================================================================================================

TEST_CASE("selection survives hostile geometry within its bounds", "[core][text][selection][A-41]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    PageTextModel model(0);
    run(model, U"ordinary", 50, 700.0);
    for (const PdfRect& hostile : {PdfRect{PdfPoint{nan, 5}, PdfSize{5, 5}}, PdfRect{PdfPoint{inf, -inf}, PdfSize{1, 1}},
                                   PdfRect{PdfPoint{10, 10}, PdfSize{-5, -5}}, PdfRect{PdfPoint{0, 0}, PdfSize{0, 0}},
                                   PdfRect{PdfPoint{1e300, 1e300}, PdfSize{1e300, 1e300}}}) {
        TextChar c;
        c.codepoint = U'x';
        c.box = hostile;
        c.origin = hostile.origin;
        c.fontSize = nan;
        model.append(c);
    }
    const auto page = read(std::move(model));
    const DocumentText text = documentOf({page});

    for (const PdfPoint probe : {PdfPoint{nan, nan}, PdfPoint{inf, -inf}, PdfPoint{1e300, 0}, PdfPoint{55, 703}}) {
        const int caret = page->caretAt(probe);
        CHECK(caret >= 0);
        CHECK(caret <= page->size());
        for (const Granularity g : {Granularity::Character, Granularity::Word, Granularity::Line, Granularity::Block}) {
            const TextUnit unit = unitAt(*page, probe, g);
            CHECK(unit.begin.caret <= unit.end.caret);
            CHECK(unit.end.caret <= page->size());
        }
    }
    for (const TextSelection& selection :
         {TextSelection::wholeDocument(1), TextSelection::column(0, PdfRect{PdfPoint{nan, 0}, PdfSize{10, 10}}),
          TextSelection::column(0, PdfRect{PdfPoint{-1e300, -1e300}, PdfSize{inf, inf}})}) {
        const CopiedText copied = selection.copy(text);
        CHECK(copied.characters + copied.unmappedSkipped <= page->size());
        for (const PdfRect& r : selection.highlightOn(*page)) {
            CHECK(std::isfinite(r.left()));
            CHECK(std::isfinite(r.top()));
        }
    }
    // A NaN rectangle is refused outright rather than stored.
    CHECK(TextSelection::column(0, PdfRect{PdfPoint{nan, 0}, PdfSize{10, 10}}).kind() == TextSelection::Kind::None);
}
