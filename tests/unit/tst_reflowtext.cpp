// Stage 5 T11 -- `core/text/ReflowText`, decision D5-16 (`Stage5-TextSearch-Plan.md` §3). The headless half
// of guide cases A-92 and A-93 (`Stage5-TextSearch-Testing-Guide.md` §5.13): the paragraphs Reflow view
// shows, in the analyzer's reading order, each knowing its block; and the block at the top of a page view.
//
// WHAT IT DOES NOT COVER (convention 19): the widget -- the toggle, the re-wrap, the scroll, the zoom -- is
// `tests/ui/tst_reflow`'s, and a REAL page's order is `tests/composition/tst_reflowink`'s over
// `columns-two.pdf`. A table block is asserted only as one paragraph per line, on `table-ruled.pdf` there.

#include <catch2/catch_test_macros.hpp>

#include "core/text/LayoutAnalyzer.h"
#include "core/text/PageTextModel.h"
#include "core/text/ReflowText.h"
#include "core/text/TextSelection.h"

#include <memory>
#include <string>

using rawpage::core::PdfPoint;
using rawpage::core::PdfRect;
using rawpage::core::PdfSize;
using namespace rawpage::core::text;

namespace {

constexpr double kAdvance = 0.55;

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

std::shared_ptr<const PageReading> read(PageTextModel model)
{
    return PageReading::build(std::make_shared<const PageTextModel>(std::move(model)));
}

// The analyzer's reading text with its LINE breaks inside a block made spaces -- what re-wrapping means --
// and its block breaks kept as blank lines. The oracle for "the reflow's text is the analyzer's order".
QString rewrapped(const PageReading& page)
{
    QString text = LayoutAnalyzer::readingText(page.model(), page.layout());
    text.replace(QStringLiteral("\n\n"), QStringLiteral("\u0001"));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QChar(0x0001), QStringLiteral("\n\n"));
    return text;
}

} // namespace

TEST_CASE("reflow writes each block as one paragraph in reading order", "[core][text][reflow][A-92]")
{
    PageTextModel model(3);
    FontIdentity bold;
    bold.name = QStringLiteral("Helvetica-Bold");
    const int boldFont = model.addFont(bold);
    run(model, U"A Heading", 50, 740.0, 18.0, boldFont);
    run(model, U"first line of body", 50, 700.0);
    run(model, U"second line of body", 50, 688.0);
    const auto page = read(std::move(model));

    const ReflowPage reflow = reflowParagraphs(*page);
    REQUIRE(reflow.paragraphs.size() == 2);
    CHECK(reflow.paragraphs[0].text() == QStringLiteral("A Heading"));
    // THE LINES JOINED: re-wrapping is the point.
    CHECK(reflow.paragraphs[1].text() == QStringLiteral("first line of body second line of body"));
    // EACH KNOWS ITS PAGE AND BLOCK, in the analyzer's order.
    CHECK(reflow.paragraphs[0].page == 3);
    CHECK(reflow.paragraphs[0].block == page->layout().order[0]);
    CHECK(reflow.paragraphs[1].block == page->layout().order[1]);
    // SIZE, WEIGHT, ITALIC -- never a face (D5-16).
    REQUIRE_FALSE(reflow.paragraphs[0].runs.empty());
    CHECK(reflow.paragraphs[0].runs.front().size == 18.0);
    CHECK(reflow.paragraphs[0].runs.front().bold);
    CHECK(reflow.paragraphs[1].runs.front().size == 10.0);
    CHECK_FALSE(reflow.paragraphs[1].runs.front().bold);

    CHECK(reflowText(reflow.paragraphs) == rewrapped(*page));
}

TEST_CASE("reflow reads two columns as the analyzer does, not across", "[core][text][reflow][A-92]")
{
    PageTextModel model(0);
    const std::u32string left[] = {U"col one line a", U"col one line b", U"col one line c", U"col one line d"};
    const std::u32string right[] = {U"col two line a", U"col two line b", U"col two line c", U"col two line d"};
    for (int i = 0; i < 4; ++i) {
        run(model, left[i], 50, 700.0 - 12.0 * i);
        run(model, right[i], 250, 700.0 - 12.0 * i);
    }
    const auto page = read(std::move(model));
    REQUIRE(page->layout().columns == 2);

    const QString text = reflowText(reflowParagraphs(*page).paragraphs);
    CHECK(text == rewrapped(*page));
    CHECK(text == QStringLiteral("col one line a col one line b col one line c col one line d\n\n"
                                 "col two line a col two line b col two line c col two line d"));
    // THE CONTROL: across the gutter would put column 2's first line second.
    CHECK_FALSE(text.startsWith(QStringLiteral("col one line a col two")));
}

TEST_CASE("reflow joins a line-final hyphen before a lowercase letter and nowhere else", "[core][text][reflow]")
{
    PageTextModel model(0);
    const auto hyphen = static_cast<std::uint8_t>(CharFlag::Hyphen);
    run(model, U"an exam", 50, 700.0);
    run(model, U"-", 50 + 7 * kAdvance * 10.0, 700.0, 10.0, -1, hyphen);
    run(model, U"ple of North", 50, 688.0);
    run(model, U"-", 50 + 12 * kAdvance * 10.0, 688.0, 10.0, -1, hyphen);
    run(model, U"West and well-known", 50, 676.0);
    const auto page = read(std::move(model));

    const ReflowPage reflow = reflowParagraphs(*page);
    REQUIRE(reflow.paragraphs.size() == 1);
    // `example` joined, `North-West` kept whole with no space, `well-known` untouched -- the copy's HTML
    // reading of D5-5 (`tst_selection`), asserted on the same lines.
    CHECK(reflow.paragraphs[0].text() == QStringLiteral("an example of North-West and well-known"));
}

TEST_CASE("reflow shows no unmapped glyph and counts what it left out", "[core][text][reflow][A-21]")
{
    PageTextModel model(0);
    const auto unmapped = static_cast<std::uint8_t>(CharFlag::Unmapped);
    run(model, U"before ", 50, 700.0);
    run(model, U"中文", 50 + 7 * kAdvance * 10.0, 700.0, 10.0, -1, unmapped);
    run(model, U" after", 50 + 9 * kAdvance * 10.0, 700.0);
    const auto page = read(std::move(model));

    const ReflowPage reflow = reflowParagraphs(*page);
    REQUIRE(reflow.paragraphs.size() == 1);
    CHECK(reflow.unmappedSkipped == 2);
    CHECK_FALSE(reflow.paragraphs[0].text().contains(QChar(0xFFFD)));
    CHECK(reflow.paragraphs[0].text() == QStringLiteral("before after"));
}

TEST_CASE("the block at the top of a page view is the first not wholly above it", "[core][text][reflow][A-93]")
{
    PageTextModel model(0);
    run(model, U"A Heading", 50, 740.0, 18.0);
    run(model, U"first line of body", 50, 700.0);
    run(model, U"second line of body", 50, 688.0);
    run(model, U"a later paragraph", 50, 640.0);
    const auto page = read(std::move(model));
    const std::vector<int>& order = page->layout().order;
    REQUIRE(order.size() == 3);

    // The page's top on screen: its first block.
    CHECK(blockAtTop(*page, 842.0) == order[0]);
    // Scrolled so the heading is gone and the body's first line is cut: the body.
    CHECK(blockAtTop(*page, 702.0) == order[1]);
    // Scrolled past the body: the later paragraph.
    CHECK(blockAtTop(*page, 660.0) == order[2]);
    // Past everything: the place is on a later page.
    CHECK(blockAtTop(*page, 100.0) == -1);
}
