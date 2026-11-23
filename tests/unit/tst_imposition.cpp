// tst_imposition -- guide case A-102 (`Stage7-Assembly-Testing-Guide.md` §5.18), Stage 7 T16, decisions D7-8 and D7-46
// (`Stage7-Assembly-Plan.md` §3): `core/doc/Imposition`'s order and geometry, pure.
//
// THE ORACLE IS A TABLE WRITTEN BY HAND, never the code's own formula (D7-18): every booklet's sides at 4, 8, 12, 16 and
// 20 padded pages, for each binding, typed out page number by page number -- and, independently of the tables, a FOLD:
// the sides stacked as sheets, sheet 0 outermost, folded, and read front to back as a person reads a booklet, which
// must give 1 ... P with the blanks last. The n-up tables likewise, every count, across, down and reversed. Odd page
// counts run first: an even count hides an off-by-one in the padding. A 0 in a table is a blank.
//
// WHAT IT DOES NOT COVER (convention 19): whether the sides DRAW as planned -- `tst_impositionengine` (A-103) reads the
// identities PDFium extracts against `onSheet` -- and whether a printer turns the paper as `DuplexFlip` assumes, which is
// paper's (M-04).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/doc/Imposition.h"

#include <QJsonObject>

#include <cmath>
#include <map>
#include <string>
#include <vector>

using namespace rawpage;
using core::DuplexFlip;
using core::ImpositionBinding;
using core::ImpositionLayout;
using core::ImpositionOrder;
using core::ImpositionSpec;
using core::SideOrder;
using Catch::Matchers::WithinAbs;

namespace {

constexpr core::PdfSize kLetter{612.0, 792.0};

// The order's cells as page NUMBERS from 1, 0 a blank.
std::vector<std::vector<int>> numbers(const std::vector<SideOrder>& sides)
{
    std::vector<std::vector<int>> out;
    for (const SideOrder& side : sides) {
        std::vector<int> row;
        for (const int page : side.cells) row.push_back(page + 1);
        out.push_back(row);
    }
    return out;
}

// A hand table with every page past `count` made a blank.
std::vector<std::vector<int>> masked(std::vector<std::vector<int>> table, int count)
{
    for (auto& side : table) {
        for (int& page : side) {
            if (page > count) page = 0;
        }
    }
    return table;
}

// THE BOOKLET TABLES, BY HAND: per padded count, the sides in order -- front of sheet 0, back of sheet 0, front of
// sheet 1 ... -- each [left cell, right cell].
const std::map<int, std::vector<std::vector<int>>>& leftBound()
{
    static const std::map<int, std::vector<std::vector<int>>> tables{
        {4, {{4, 1}, {2, 3}}},
        {8, {{8, 1}, {2, 7}, {6, 3}, {4, 5}}},
        {12, {{12, 1}, {2, 11}, {10, 3}, {4, 9}, {8, 5}, {6, 7}}},
        {16, {{16, 1}, {2, 15}, {14, 3}, {4, 13}, {12, 5}, {6, 11}, {10, 7}, {8, 9}}},
        {20, {{20, 1}, {2, 19}, {18, 3}, {4, 17}, {16, 5}, {6, 15}, {14, 7}, {8, 13}, {12, 9}, {10, 11}}},
    };
    return tables;
}

const std::map<int, std::vector<std::vector<int>>>& rightBound()
{
    static const std::map<int, std::vector<std::vector<int>>> tables{
        {4, {{1, 4}, {3, 2}}},
        {8, {{1, 8}, {7, 2}, {3, 6}, {5, 4}}},
        {12, {{1, 12}, {11, 2}, {3, 10}, {9, 4}, {5, 8}, {7, 6}}},
        {16, {{1, 16}, {15, 2}, {3, 14}, {13, 4}, {5, 12}, {11, 6}, {7, 10}, {9, 8}}},
        {20, {{1, 20}, {19, 2}, {3, 18}, {17, 4}, {5, 16}, {15, 6}, {7, 14}, {13, 8}, {9, 12}, {11, 10}}},
    };
    return tables;
}

int paddedFor(int count) { return count <= 4 ? 4 : (count + 3) / 4 * 4; }

// THE FOLD, independent of every table: sheets nested, sheet 0 outside, folded on the spine and read page after page.
// Left binding (the spine on the left of the closed booklet, a page turned right to left): the first half reads each
// sheet's front RIGHT then its back LEFT, outermost first; the second half, innermost first, each back RIGHT then its
// front LEFT. Right binding is the mirror: every left read as right.
std::vector<int> folded(const std::vector<std::vector<int>>& sides, bool left)
{
    const int sheets = static_cast<int>(sides.size()) / 2;
    const int near = left ? 1 : 0;   // the cell a page turned from the fore edge shows first
    const int far = left ? 0 : 1;
    std::vector<int> read;
    for (int s = 0; s < sheets; ++s) {
        read.push_back(sides[static_cast<std::size_t>(2 * s)][near]);
        read.push_back(sides[static_cast<std::size_t>(2 * s + 1)][far]);
    }
    for (int s = sheets - 1; s >= 0; --s) {
        read.push_back(sides[static_cast<std::size_t>(2 * s + 1)][near]);
        read.push_back(sides[static_cast<std::size_t>(2 * s)][far]);
    }
    return read;
}

ImpositionSpec booklet(ImpositionBinding binding, DuplexFlip duplex = DuplexFlip::ShortEdge)
{
    ImpositionSpec spec;
    spec.layout = ImpositionLayout::Booklet;
    spec.binding = binding;
    spec.duplex = duplex;
    return spec;
}

ImpositionSpec nUp(int perSheet, ImpositionOrder order, bool reversed = false)
{
    ImpositionSpec spec;
    spec.perSheet = perSheet;
    spec.order = order;
    spec.reversed = reversed;
    return spec;
}

// Odd counts first (the plan's "odd ones first"), then every even one.
std::vector<int> countsOddFirst()
{
    std::vector<int> out;
    for (int n = 1; n <= 17; n += 2) out.push_back(n);
    for (int n = 2; n <= 16; n += 2) out.push_back(n);
    return out;
}

} // namespace

TEST_CASE("imposition: booklets at 1 to 17 pages against hand tables and a fold", "[imposition][A-102]")
{
    for (const bool left : {true, false}) {
        for (const int count : countsOddFirst()) {
            INFO((left ? "left" : "right") << " binding, " << count << " page(s)");
            const ImpositionSpec spec = booklet(left ? ImpositionBinding::Left : ImpositionBinding::Right);
            const std::vector<SideOrder> sides = core::impositionOrder(spec, count, core::impositionGrid(spec, true));
            const int padded = paddedFor(count);
            const auto& table = (left ? leftBound() : rightBound()).at(padded);
            CHECK(numbers(sides) == masked(table, count));
            // Each side's sheet and face, and the sheet count: P / 4.
            REQUIRE(static_cast<int>(sides.size()) == padded / 2);
            for (std::size_t k = 0; k < sides.size(); ++k) {
                CHECK(sides[k].sheet == static_cast<int>(k / 2));
                CHECK(sides[k].back == (k % 2 == 1));
            }
            // THE FOLD reads 1 ... P, the blanks (pages past the count) last and only last.
            std::vector<int> expected;
            for (int p = 1; p <= padded; ++p) expected.push_back(p);
            CHECK(folded(table, left) == expected);
            std::vector<int> read = folded(numbers(sides), left);
            for (int p = 0; p < padded; ++p) CHECK(read[static_cast<std::size_t>(p)] == (p < count ? p + 1 : 0));
        }
    }
}

TEST_CASE("imposition: n-up order across down and reversed against hand tables", "[imposition][A-102]")
{
    struct Row {
        int perSheet;
        bool landscape;
        ImpositionOrder order;
        bool reversed;
        std::vector<int> first;   // the first side of a full sheet, row-major from the top-left
    };
    using O = ImpositionOrder;
    const std::vector<Row> rows{
        {2, true, O::Across, false, {1, 2}},
        {2, true, O::Across, true, {2, 1}},
        {2, true, O::Down, false, {1, 2}},
        {2, true, O::Down, true, {2, 1}},
        {2, false, O::Across, false, {1, 2}},
        {2, false, O::Down, false, {1, 2}},
        {2, false, O::Down, true, {1, 2}},
        {4, false, O::Across, false, {1, 2, 3, 4}},
        {4, false, O::Down, false, {1, 3, 2, 4}},
        {4, false, O::Across, true, {2, 1, 4, 3}},
        {4, false, O::Down, true, {3, 1, 4, 2}},
        {6, true, O::Across, false, {1, 2, 3, 4, 5, 6}},
        {6, true, O::Down, false, {1, 3, 5, 2, 4, 6}},
        {6, true, O::Across, true, {3, 2, 1, 6, 5, 4}},
        {6, true, O::Down, true, {5, 3, 1, 6, 4, 2}},
        {6, false, O::Across, false, {1, 2, 3, 4, 5, 6}},
        {6, false, O::Down, false, {1, 4, 2, 5, 3, 6}},
        {6, false, O::Across, true, {2, 1, 4, 3, 6, 5}},
        {8, true, O::Across, false, {1, 2, 3, 4, 5, 6, 7, 8}},
        {8, true, O::Down, false, {1, 3, 5, 7, 2, 4, 6, 8}},
        {8, true, O::Across, true, {4, 3, 2, 1, 8, 7, 6, 5}},
        {8, false, O::Down, false, {1, 5, 2, 6, 3, 7, 4, 8}},
        {8, false, O::Down, true, {5, 1, 6, 2, 7, 3, 8, 4}},
        {9, false, O::Across, false, {1, 2, 3, 4, 5, 6, 7, 8, 9}},
        {9, false, O::Down, false, {1, 4, 7, 2, 5, 8, 3, 6, 9}},
        {9, false, O::Across, true, {3, 2, 1, 6, 5, 4, 9, 8, 7}},
        {9, false, O::Down, true, {7, 4, 1, 8, 5, 2, 9, 6, 3}},
        {16, false, O::Across, false, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}},
        {16, false, O::Down, false, {1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15, 4, 8, 12, 16}},
        {16, false, O::Across, true, {4, 3, 2, 1, 8, 7, 6, 5, 12, 11, 10, 9, 16, 15, 14, 13}},
        {16, false, O::Down, true, {13, 9, 5, 1, 14, 10, 6, 2, 15, 11, 7, 3, 16, 12, 8, 4}},
    };
    for (const Row& row : rows) {
        const ImpositionSpec spec = nUp(row.perSheet, row.order, row.reversed);
        const core::ImpositionGrid grid = core::impositionGrid(spec, row.landscape);
        REQUIRE(grid.cells() == row.perSheet);
        for (const int count : countsOddFirst()) {
            INFO(row.perSheet << "-up " << (row.landscape ? "landscape " : "portrait ") << (row.order == O::Across ? "across" : "down")
                              << (row.reversed ? " reversed" : "") << ", " << count << " page(s)");
            const std::vector<SideOrder> sides = core::impositionOrder(spec, count, grid);
            // Side k holds the first table shifted by k sides' pages, a page past the count a blank.
            const int expectedSides = (count + row.perSheet - 1) / row.perSheet;
            REQUIRE(static_cast<int>(sides.size()) == expectedSides);
            for (int k = 0; k < expectedSides; ++k) {
                std::vector<int> want;
                for (const int p : row.first) {
                    const int page = p + k * row.perSheet;
                    want.push_back(page <= count ? page : 0);
                }
                CHECK(numbers({sides[static_cast<std::size_t>(k)]}).front() == want);
                CHECK(sides[static_cast<std::size_t>(k)].sheet == k);
                CHECK_FALSE(sides[static_cast<std::size_t>(k)].back);
                CHECK_FALSE(sides[static_cast<std::size_t>(k)].turned);
            }
        }
    }
    // The grids themselves, by hand: columns x rows on a landscape sheet, the transpose on a portrait one.
    const std::map<int, std::pair<int, int>> wide{{2, {2, 1}}, {4, {2, 2}}, {6, {3, 2}}, {8, {4, 2}}, {9, {3, 3}}, {16, {4, 4}}};
    for (const auto& [n, shape] : wide) {
        CHECK(core::impositionGrid(nUp(n, O::Across), true) == core::ImpositionGrid{shape.first, shape.second, true});
        CHECK(core::impositionGrid(nUp(n, O::Across), false) == core::ImpositionGrid{shape.second, shape.first, false});
    }
    CHECK(core::impositionGrid(booklet(ImpositionBinding::Left), false) == core::ImpositionGrid{2, 1, false});
}

TEST_CASE("imposition: duplex sides alternate and a back is turned only for a flip across the sheet", "[imposition][A-102]")
{
    // BY HAND: which backs are turned, per orientation and flip. A back turned over about the vertical axis reads
    // upright; a printer flipping on the horizontal edges -- a landscape sheet's long ones, a portrait sheet's short ones
    // -- prints it upside down, so it is drawn half-turned.
    struct Row {
        bool landscape;
        DuplexFlip flip;
        bool backTurned;
    };
    const std::vector<Row> rows{
        {true, DuplexFlip::ShortEdge, false},
        {true, DuplexFlip::LongEdge, true},
        {false, DuplexFlip::LongEdge, false},
        {false, DuplexFlip::ShortEdge, true},
        {true, DuplexFlip::OneSided, false},
        {false, DuplexFlip::OneSided, false},
    };
    for (const Row& row : rows) {
        INFO((row.landscape ? "landscape" : "portrait") << " flip " << std::string(core::duplexFlipName(row.flip)));
        // A booklet: always fronts and backs.
        ImpositionSpec spec = booklet(ImpositionBinding::Left, row.flip);
        auto sides = core::impositionOrder(spec, 9, core::impositionGrid(spec, row.landscape));
        REQUIRE(sides.size() == 6);
        for (std::size_t k = 0; k < sides.size(); ++k) {
            CHECK(sides[k].back == (k % 2 == 1));
            CHECK(sides[k].turned == (k % 2 == 1 && row.backTurned));
        }
        // N-up: fronts and backs only when duplex -- 5 pages 2-up are three sides, two sheets.
        spec = nUp(2, ImpositionOrder::Across);
        spec.duplex = row.flip;
        sides = core::impositionOrder(spec, 5, core::impositionGrid(spec, row.landscape));
        REQUIRE(sides.size() == 3);
        const bool duplex = row.flip != DuplexFlip::OneSided;
        const std::vector<int> sheets = duplex ? std::vector<int>{0, 0, 1} : std::vector<int>{0, 1, 2};
        for (std::size_t k = 0; k < sides.size(); ++k) {
            CHECK(sides[k].sheet == sheets[k]);
            CHECK(sides[k].back == (duplex && k == 1));
            CHECK(sides[k].turned == (duplex && k == 1 && row.backTurned));
        }
    }
}

TEST_CASE("imposition: a booklet of Letter pages on a Letter sheet against hand geometry", "[imposition][A-102]")
{
    ImpositionSpec spec = booklet(ImpositionBinding::Left);
    spec.creep = 2.0;
    spec.cropMarks = true;
    // 17 pages: 20 padded, five sheets. Auto turns the sheet landscape: two Letter pages fit 0.588 against 0.455 portrait.
    auto plan = core::planImposition(spec, std::vector<core::PdfSize>(17, kLetter));
    REQUIRE(plan);
    const core::ImpositionPlan& p = plan.value();
    CHECK(p.sheet == core::PdfSize{792.0, 612.0});
    CHECK(p.sheets == 5);
    CHECK(p.pagesPadded == 20);
    REQUIRE(p.sides.size() == 10);
    // By hand: the area 720 x 540, each cell 360 x 540; a Letter page scaled 360/612 = 0.58824 is 360 x 465.88, centred
    // up and down (y 73.06) and against the fold: the left page's right edge at 396, the right page's left edge at 396.
    const double h = 792.0 * 360.0 / 612.0;
    const double y = 36.0 + (540.0 - h) / 2.0;
    for (const core::ImposedSide& side : p.sides) {
        INFO("sheet " << side.sheet << (side.back ? " back" : " front"));
        REQUIRE(side.cells.size() == 2);
        CHECK(side.cells[0].cell == core::PdfRect{{36.0, 36.0}, {360.0, 540.0}});
        CHECK(side.cells[1].cell == core::PdfRect{{396.0, 36.0}, {360.0, 540.0}});
        // Creep: sheet s's pages `2 s` points further toward the fold.
        const double shift = 2.0 * side.sheet;
        CHECK_THAT(side.cells[0].placed.left(), WithinAbs(36.0 + shift, 1e-9));
        CHECK_THAT(side.cells[1].placed.left(), WithinAbs(396.0 - shift, 1e-9));
        for (const core::ImposedCell& cell : side.cells) {
            CHECK_THAT(cell.placed.bottom(), WithinAbs(y, 1e-9));
            CHECK_THAT(cell.placed.size.width, WithinAbs(360.0, 1e-9));
            CHECK_THAT(cell.placed.size.height, WithinAbs(h, 1e-9));
        }
        // Crop marks where the stack is TRIMMED -- never shifted by creep: x at 36, 396, 756 above and below, y at the
        // pages' bottom and top left and right; ten ticks of 12 points, 3 points out.
        REQUIRE(side.cropMarks.size() == 10);
        CHECK_THAT(side.cropMarks[0].from.x, WithinAbs(36.0, 1e-9));
        CHECK_THAT(side.cropMarks[0].from.y, WithinAbs(y + h + 3.0, 1e-9));
        CHECK_THAT(side.cropMarks[0].to.y, WithinAbs(y + h + 15.0, 1e-9));
        CHECK_THAT(side.cropMarks[2].from.x, WithinAbs(396.0, 1e-9));
        CHECK_THAT(side.cropMarks[4].from.x, WithinAbs(756.0, 1e-9));
        CHECK_THAT(side.cropMarks[6].from.x, WithinAbs(33.0, 1e-9));
        CHECK_THAT(side.cropMarks[6].to.x, WithinAbs(21.0, 1e-9));
        CHECK_THAT(side.cropMarks[7].from.x, WithinAbs(759.0, 1e-9));
    }
    // A blank (page 18 on, here sheet 0's front left) is placed as the first page's size would be, so its marks fall in line.
    CHECK(p.sides[0].cells[0].page == -1);
    CHECK(p.sides[0].cells[1].page == 0);

    // WITH A LABEL, every tick below the pages stops short of the label's band: none crosses its text. The pages are tall
    // enough to fill their cells' height (300 x 800 in a 288 x 756 cell on the portrait sheet AUTO picks: 0.945), so the pages' bottom IS the margin's top
    // and a tick 3 + 12 points long would run to 3 points from the edge, through the band -- T16's planted P11 stayed green
    // over Letter pages, whose bottom stood 61 points up.
    spec.sheetLabels = true;
    spec.margin = 18.0;
    spec.creep = 0.0;
    plan = core::planImposition(spec, std::vector<core::PdfSize>(17, core::PdfSize{300.0, 800.0}));
    REQUIRE(plan);
    REQUIRE_THAT(plan.value().sides[0].cells[1].placed.bottom(), WithinAbs(18.0, 1e-9));
    for (const core::ImposedSide& side : plan.value().sides) {
        for (const core::ImposedLine& line : side.cropMarks)
            CHECK(std::min(line.from.y, line.to.y) >= core::kSheetLabelBand - 1e-9);
    }
}

TEST_CASE("imposition: n-up geometry with a gutter and the half turn of a back", "[imposition][A-102]")
{
    ImpositionSpec spec = nUp(4, ImpositionOrder::Across);
    spec.gutter = 12.0;
    spec.duplex = DuplexFlip::ShortEdge;   // a portrait sheet flipped on its short edge: every back turned
    auto plan = core::planImposition(spec, std::vector<core::PdfSize>(5, kLetter));
    REQUIRE(plan);
    const core::ImpositionPlan& p = plan.value();
    // Auto: 4-up Letter fits 0.431 portrait against 0.333 landscape.
    CHECK(p.sheet == kLetter);
    CHECK(p.grid == core::ImpositionGrid{2, 2, false});
    REQUIRE(p.sides.size() == 2);
    CHECK_FALSE(p.sides[0].turned);
    CHECK(p.sides[1].turned);
    // By hand: the area 540 x 720, cells (540 - 12) / 2 = 264 by (720 - 12) / 2 = 354; a Letter page at 264/612 is
    // 264 x 341.65, centred: the top-left cell at (36, 402), its page at y 408.18.
    const core::ImposedCell& topLeft = p.sides[0].cells[0];
    CHECK(topLeft.cell == core::PdfRect{{36.0, 402.0}, {264.0, 354.0}});
    CHECK(p.sides[0].cells[1].cell == core::PdfRect{{312.0, 402.0}, {264.0, 354.0}});
    CHECK(p.sides[0].cells[2].cell == core::PdfRect{{36.0, 36.0}, {264.0, 354.0}});
    const double h = 792.0 * 264.0 / 612.0;
    CHECK_THAT(topLeft.placed.left(), WithinAbs(36.0, 1e-9));
    CHECK_THAT(topLeft.placed.bottom(), WithinAbs(402.0 + (354.0 - h) / 2.0, 1e-9));
    // The back's page 5 is in its top-left cell in its own space -- and, half-turned, at the sheet's bottom right.
    const core::ImposedCell& five = p.sides[1].cells[0];
    CHECK(five.page == 4);
    const core::PdfRect landed = core::onSheet(five.placed, p.sides[1], p.sheet);
    CHECK_THAT(landed.left(), WithinAbs(612.0 - five.placed.right(), 1e-9));
    CHECK_THAT(landed.bottom(), WithinAbs(792.0 - five.placed.top(), 1e-9));
    CHECK(landed.left() > 306.0);
    CHECK(landed.top() < 396.0);
    // An empty cell of an n-up has no page and no place.
    CHECK(p.sides[1].cells[3].page == -1);
    CHECK(p.sides[1].cells[3].placed == core::PdfRect{});
}

TEST_CASE("imposition: a page is fitted as it shows and a mixed set each to its own size", "[imposition][A-102]")
{
    ImpositionSpec spec = nUp(2, ImpositionOrder::Across);
    spec.orientation = core::SheetOrientation::Landscape;
    spec.margin = 0.0;
    // A landscape page (a Letter page turned by /Rotate shows 792 x 612) in a 396 x 612 cell: 396 / 792 = 0.5.
    auto plan = core::planImposition(spec, {core::PdfSize{792.0, 612.0}, kLetter});
    REQUIRE(plan);
    const core::ImposedCell& wide = plan.value().sides[0].cells[0];
    CHECK(wide.placed == core::PdfRect{{0.0, (612.0 - 306.0) / 2.0}, {396.0, 306.0}});
    const core::ImposedCell& tall = plan.value().sides[0].cells[1];
    CHECK_THAT(tall.placed.size.width, WithinAbs(396.0, 1e-9));
    CHECK_THAT(tall.placed.size.height, WithinAbs(792.0 * 396.0 / 612.0 > 612.0 ? 612.0 : 792.0 * 396.0 / 612.0, 1e-9));
    // ... scaled UP as well as down: a 100 x 100 page fills its cell's shorter side.
    plan = core::planImposition(spec, {core::PdfSize{100.0, 100.0}});
    REQUIRE(plan);
    CHECK_THAT(plan.value().sides[0].cells[0].placed.size.width, WithinAbs(396.0, 1e-9));
}

TEST_CASE("imposition: the marks are placed in the margin", "[imposition][A-102]")
{
    ImpositionSpec spec = nUp(2, ImpositionOrder::Across);
    spec.registrationMarks = true;
    spec.colourBar = true;
    spec.sheetLabels = true;
    auto plan = core::planImposition(spec, {kLetter});
    REQUIRE(plan);
    const core::ImpositionPlan& p = plan.value();
    REQUIRE(p.registration.size() == 4);
    CHECK(p.registration[0] == core::PdfPoint{396.0, 18.0});
    CHECK(p.registration[1] == core::PdfPoint{396.0, 594.0});
    CHECK(p.registration[2] == core::PdfPoint{18.0, 306.0});
    CHECK(p.registration[3] == core::PdfPoint{774.0, 306.0});
    CHECK(p.colourBar == core::PdfRect{{792.0 - 36.0 - 90.0, 612.0 - 18.0 - 4.5}, {90.0, 9.0}});
    CHECK(p.labelOrigin == core::PdfPoint{36.0, core::kSheetLabelBaseline});
    // No crop marks asked for, none planned.
    CHECK(p.sides[0].cropMarks.empty());
}

TEST_CASE("imposition: the spec's bounds and the plan's refusals", "[imposition][A-102]")
{
    const auto key = [](const core::Status& status) { return status ? QString() : status.error().messageKey; };
    ImpositionSpec spec;
    CHECK(key(core::validate(spec)).isEmpty());
    spec.perSheet = 5;
    CHECK(key(core::validate(spec)) == QLatin1String("error.imposition.badSpec"));
    spec.layout = ImpositionLayout::Booklet;   // a booklet ignores the count
    CHECK(key(core::validate(spec)).isEmpty());
    spec = ImpositionSpec{};
    spec.margin = 10.0;
    CHECK(key(core::validate(spec)).isEmpty());
    spec.cropMarks = true;   // marks need 18 points of margin
    CHECK(key(core::validate(spec)) == QLatin1String("error.imposition.marginTooSmall"));
    spec = ImpositionSpec{};
    spec.sheet = core::PdfSize{50.0, 792.0};
    CHECK(key(core::validate(spec)) == QLatin1String("error.imposition.badSpec"));
    spec = ImpositionSpec{};
    spec.creep = -1.0;
    CHECK(key(core::validate(spec)) == QLatin1String("error.imposition.badSpec"));
    spec.creep = std::nan("");
    CHECK(key(core::validate(spec)) == QLatin1String("error.imposition.badSpec"));

    // A cell under half an inch: 16-up on A5 with a 144-point margin, (420 - 288) / 4 = 33.
    spec = nUp(16, ImpositionOrder::Across);
    spec.sheet = core::PdfSize{420.0, 595.0};
    spec.margin = 144.0;
    auto refused = core::planImposition(spec, {kLetter});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.imposition.sheetTooSmall"));
    // Creep past half a cell: a hundred pages, 25 sheets, 24 x 12 = 288 > 180.
    spec = booklet(ImpositionBinding::Left);
    spec.creep = 12.0;
    refused = core::planImposition(spec, std::vector<core::PdfSize>(100, kLetter));
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.imposition.creepTooLarge"));
    refused = core::planImposition(ImpositionSpec{}, {});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.imposition.noPages"));
    refused = core::planImposition(ImpositionSpec{}, {core::PdfSize{0.0, 10.0}});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.imposition.badSpec"));
}

TEST_CASE("imposition: a spec as a profile stores it and back", "[imposition][A-97][A-102]")
{
    ImpositionSpec spec = booklet(ImpositionBinding::Right, DuplexFlip::LongEdge);
    spec.sheet = core::PdfSize{842.0, 1191.0};
    spec.orientation = core::SheetOrientation::Landscape;
    spec.margin = 24.0;
    spec.gutter = 6.5;
    spec.creep = 0.25;
    spec.cropMarks = true;
    spec.colourBar = true;
    spec.frames = true;
    const QJsonObject json = core::impositionToJson(spec);
    auto back = core::impositionFromJson(json);
    REQUIRE(back);
    CHECK(back.value() == spec);
    ImpositionSpec other = nUp(9, ImpositionOrder::Down, true);
    other.registrationMarks = true;
    other.sheetLabels = true;
    auto again = core::impositionFromJson(core::impositionToJson(other));
    REQUIRE(again);
    CHECK(again.value() == other);
    // Each field refused by name.
    for (const char* field : {"layout", "order", "binding", "orientation", "duplex", "perSheet", "sheet", "margin", "cropMarks"}) {
        QJsonObject broken = json;
        broken.insert(QLatin1String(field), QStringLiteral("nonsense"));
        auto refused = core::impositionFromJson(broken);
        INFO(field);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().messageKey == QLatin1String("error.imposition.badSpec"));
        CHECK(refused.error().context == QLatin1String(field));
    }
    QJsonObject tooSmall = json;
    tooSmall.insert(QStringLiteral("margin"), 4.0);   // with marks
    auto refused = core::impositionFromJson(tooSmall);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.imposition.marginTooSmall"));
}

TEST_CASE("imposition: the sheet label names the sheet and its face", "[imposition][A-102]")
{
    const core::ImpositionLabels labels{QStringLiteral("S%1/%2"), QStringLiteral("F%1/%2"), QStringLiteral("B%1/%2")};
    core::ImposedSide side;
    side.sheet = 2;
    CHECK(core::sheetLabel(labels, nUp(4, ImpositionOrder::Across), side, 7) == QLatin1String("S3/7"));
    ImpositionSpec duplex = nUp(4, ImpositionOrder::Across);
    duplex.duplex = DuplexFlip::LongEdge;
    CHECK(core::sheetLabel(labels, duplex, side, 7) == QLatin1String("F3/7"));
    side.back = true;
    CHECK(core::sheetLabel(labels, duplex, side, 7) == QLatin1String("B3/7"));
    // A booklet's sides are fronts and backs, duplex or not.
    CHECK(core::sheetLabel(labels, booklet(ImpositionBinding::Left, DuplexFlip::OneSided), side, 7) == QLatin1String("B3/7"));
}
