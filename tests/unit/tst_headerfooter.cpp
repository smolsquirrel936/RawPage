// tst_headerfooter -- Stage 7 T14 (`Stage7-Assembly-Plan.md` D7-14, D7-44): the header/footer engine, pure -- every field
// expanded per page against hand-written tables, the lines placed on the page as shown at every rotation, the spec's
// bounds and its JSON, and the Bates allocator (`Stage7-Assembly-Testing-Guide.md` §5.16, A-92's and A-93's headless
// halves, A-96's refusal at input).
//
// WHAT IT DOES NOT COVER (convention 19): what a READER makes of the text. Every value here is the engine's own; that
// PDFium extracts each field's expansion from the drawn page, and draws it where it was placed, is
// `tests/render/tst_decorationengine`'s.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/doc/BatesAllocator.h"
#include "core/doc/HeaderFooter.h"
#include "core/doc/Page.h"
#include "core/doc/PageTree.h"

#include <QJsonDocument>

#include <vector>

using namespace rawpage;
using Catch::Approx;

namespace {

core::FieldValues fixedValues()
{
    return core::FieldValues{QDate(2026, 11, 23), QTime(14, 5, 9), QStringLiteral("contract.pdf"),
                             QStringLiteral("D:/cases/contract.pdf")};
}

core::Page letter(int rotation = 0)
{
    core::Page page(0, core::PdfRect{core::PdfPoint{0, 0}, core::PdfSize{612, 792}});
    page.setRotation(rotation);
    return page;
}

core::PageTree treeOf(int count)
{
    std::vector<core::Page> pages;
    for (int i = 0; i < count; ++i) pages.push_back(core::Page(i, core::PdfRect{core::PdfPoint{0, 0}, core::PdfSize{612, 792}}));
    return core::PageTree(std::move(pages));
}

QString textOf(const core::DecorationLine& line)
{
    QString out;
    for (const core::TypedRun& run : line.runs) out += run.text;
    return out;
}

} // namespace

// ==========================================================================================
// A-92 -- every field, on every page (headless half)
// ==========================================================================================

TEST_CASE("headerfooter.fields: every field expanded per page against a hand-written table", "[headerfooter][A-92]")
{
    const core::FieldValues values = fixedValues();
    const core::BatesFormat bates{QStringLiteral("ABC-"), QStringLiteral("-X"), 6, 41};
    struct Row {
        const char* typed;
        int index;
        int count;
        const char* label;
        std::optional<long long> number;
        const char* want;
    };
    const Row rows[] = {
        {"Page <<page>> of <<count>>", 0, 3, "", std::nullopt, "Page 1 of 3"},
        {"Page <<page>> of <<count>>", 2, 3, "", std::nullopt, "Page 3 of 3"},
        {"<<label>>", 1, 9, "iv", std::nullopt, "iv"},
        {"<<label>>", 4, 9, "", std::nullopt, "5"},   // no label: the page's number, as a reader shows it
        {"<<date>> <<time>>", 0, 1, "", std::nullopt, "2026-11-23 14:05"},
        {"<<date:dd MMM yyyy>>", 0, 1, "", std::nullopt, "23 Nov 2026"},   // the C locale, whatever the machine
        {"<<time:HH:mm:ss>>", 0, 1, "", std::nullopt, "14:05:09"},
        {"<<file>> | <<path>>", 0, 1, "", std::nullopt, "contract.pdf | D:/cases/contract.pdf"},
        {"<<bates>>", 0, 1, "", 41, "ABC-000041-X"},
        {"<<bates>>", 7, 9, "", 48, "ABC-000048-X"},
        {"<<bates>>", 0, 1, "", std::nullopt, "<<bates>>"},   // not numbering: left as typed
        {"<<nothing>> and << page >>", 0, 1, "", std::nullopt, "<<nothing>> and << page >>"},
        {"plain text", 0, 1, "", std::nullopt, "plain text"},
        {"open << only", 0, 1, "", std::nullopt, "open << only"},
    };
    for (const Row& row : rows) {
        INFO(row.typed << " on page " << row.index + 1);
        const core::FieldPage page{row.index, row.count, QString::fromUtf8(row.label), row.number};
        CHECK(core::expandFields(QString::fromUtf8(row.typed), values, page, bates).toStdString() == row.want);
    }
    CHECK(core::formatBates(core::BatesFormat{QString(), QString(), 3, 1}, 12345).toStdString() == "12345");   // wider than its digits
}

TEST_CASE("headerfooter.fields: the pages a spec reaches -- a range, then odd or even counted from 1", "[headerfooter][A-92]")
{
    const core::PageTree tree = treeOf(7);
    core::HeaderFooterSpec spec;
    spec.areas[0] = QStringLiteral("x");
    CHECK(core::headerFooterPages(spec, tree).value() == std::vector<int>{0, 1, 2, 3, 4, 5, 6});
    spec.parity = core::PageParity::Odd;
    CHECK(core::headerFooterPages(spec, tree).value() == std::vector<int>{0, 2, 4, 6});
    spec.parity = core::PageParity::Even;
    CHECK(core::headerFooterPages(spec, tree).value() == std::vector<int>{1, 3, 5});
    spec.range = QStringLiteral("2-5");
    CHECK(core::headerFooterPages(spec, tree).value() == std::vector<int>{1, 3});
    spec.range = QStringLiteral("3");
    const auto none = core::headerFooterPages(spec, tree);
    REQUIRE_FALSE(none);
    CHECK(none.error().messageKey == QLatin1String("error.decorate.noPages"));
}

// ==========================================================================================
// Where the lines go -- the page as SHOWN, at every rotation
// ==========================================================================================

TEST_CASE("headerfooter.layout: each area's line placed on the page as shown, at each rotation", "[headerfooter][A-92]")
{
    core::HeaderFooterSpec spec;
    spec.areas[static_cast<int>(core::HeaderFooterArea::HeaderLeft)] = QStringLiteral("L");
    spec.areas[static_cast<int>(core::HeaderFooterArea::HeaderCentre)] = QStringLiteral("Centre");
    spec.areas[static_cast<int>(core::HeaderFooterArea::FooterRight)] = QStringLiteral("Page <<page>>");
    spec.size = 10;
    spec.top = 30;
    spec.bottom = 20;
    spec.left = 50;
    spec.right = 40;
    const core::FieldPage field{0, 1, QString(), std::nullopt};
    for (const int rotation : {0, 90, 180, 270}) {
        INFO("rotation " << rotation);
        const core::Page page = letter(rotation);
        const core::PdfSize shown = page.displaySize();
        auto laid = core::layoutHeaderFooter(spec, core::DecorationKind::HeaderFooter, page, fixedValues(), field);
        REQUIRE(laid);
        const core::PageDecoration& d = laid.value();
        REQUIRE(d.lines.size() == 3);
        CHECK(d.subtype.isEmpty());   // both bands
        CHECK(d.box == page.cropBox());
        // Each line's origin, mapped back through the page's turn, is where the spec says on the SHOWN page.
        const auto shownOrigin = [&](const core::DecorationLine& line) {
            // shownToPage is affine: inverted from where it takes the origin and the two unit steps.
            const core::PdfPoint o0 = core::shownToPage(page, 0, 0);
            const core::PdfPoint ox = core::shownToPage(page, 1, 0);
            const core::PdfPoint oy = core::shownToPage(page, 0, 1);
            const double ax = ox.x - o0.x, bx = ox.y - o0.y, ay = oy.x - o0.x, by = oy.y - o0.y;
            const double px = line.matrix[4] - o0.x, py = line.matrix[5] - o0.y;
            const double det = ax * by - ay * bx;
            return core::PdfPoint{(px * by - py * ay) / det, (ax * py - bx * px) / det};
        };
        const core::PdfPoint left = shownOrigin(d.lines[0]);
        CHECK(textOf(d.lines[0]) == QStringLiteral("L"));
        CHECK(left.x == Approx(50));
        CHECK(left.y == Approx(shown.height - 30 - core::kTypedAscent * 10));
        const core::PdfPoint centre = shownOrigin(d.lines[1]);
        CHECK(centre.x + d.lines[1].width / 2 == Approx(shown.width / 2));
        const core::PdfPoint right = shownOrigin(d.lines[2]);
        CHECK(textOf(d.lines[2]) == QStringLiteral("Page 1"));
        CHECK(right.x + d.lines[2].width == Approx(shown.width - 40));
        CHECK(right.y == Approx(20 + (core::kTypedLeading - core::kTypedAscent) * 10));
        // The text reads UPRIGHT on the shown page: its x axis is the shown page's.
        const core::PdfPoint o0 = core::shownToPage(page, 0, 0);
        const core::PdfPoint ox = core::shownToPage(page, 1, 0);
        CHECK(d.lines[0].matrix[0] == Approx(ox.x - o0.x));
        CHECK(d.lines[0].matrix[1] == Approx(ox.y - o0.y));
        // In front, wrapped -- identity without `shrink`, clipped to what the page shows.
        REQUIRE(d.wrap);
        CHECK(d.wrap->matrix == std::array<double, 6>{1, 0, 0, 1, 0, 0});
        CHECK(d.wrap->clip == page.cropBox());
        CHECK_FALSE(d.behind);
    }
}

TEST_CASE("headerfooter.shrink: the content is scaled to clear both bands and centred across", "[headerfooter][A-95]")
{
    core::HeaderFooterSpec spec;
    spec.areas[1] = QStringLiteral("Header");
    spec.areas[4] = QStringLiteral("Footer\nsecond line");
    spec.size = 12;
    spec.top = 36;
    spec.bottom = 24;
    spec.shrink = true;
    const core::Page page = letter();
    auto laid = core::layoutHeaderFooter(spec, core::DecorationKind::HeaderFooter, page, fixedValues(), core::FieldPage{0, 1, {}, {}});
    REQUIRE(laid);
    REQUIRE(laid.value().wrap);
    const auto& m = laid.value().wrap->matrix;
    const double header = 36 + 1 * 1.2 * 12 + core::kShrinkGap;
    const double footer = 24 + 2 * 1.2 * 12 + core::kShrinkGap;
    const double s = (792 - header - footer) / 792;
    CHECK(m[0] == Approx(s));
    CHECK(m[3] == Approx(s));
    CHECK(m[4] == Approx((612 - 612 * s) / 2));   // centred across
    CHECK(m[5] == Approx(footer));                 // its foot on the footer band
    CHECK(792 * s + m[5] == Approx(792 - header));   // its head under the header band
    // A band that leaves no room is refused by key, with nothing laid out.
    spec.top = 700;
    spec.bottom = 60;
    auto none = core::layoutHeaderFooter(spec, core::DecorationKind::HeaderFooter, page, fixedValues(), core::FieldPage{0, 1, {}, {}});
    REQUIRE_FALSE(none);
    CHECK(none.error().messageKey == QLatin1String("error.decorate.noRoom"));
}

// ==========================================================================================
// A-96 -- a character no face can show, refused at input by name
// ==========================================================================================

TEST_CASE("decoration.characters: Latin and CJK accepted, a Cyrillic letter refused by name at input", "[headerfooter][A-96]")
{
    core::HeaderFooterSpec spec;
    spec.areas[0] = QStringLiteral("Résumé – 第一章 – ページ – 한국");
    CHECK(core::validate(spec));
    spec.areas[2] = QStringLiteral("Глава");
    const auto refused = core::validate(spec);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.decorate.characterRefused"));
    CHECK(refused.error().context.contains(QStringLiteral("U+0413")));
    // A Bates prefix too -- but only when the spec numbers.
    spec.areas[2] = QStringLiteral("<<bates>>");
    spec.bates.prefix = QStringLiteral("Ω-");
    const auto prefix = core::validate(spec);
    REQUIRE_FALSE(prefix);
    CHECK(prefix.error().context.contains(QStringLiteral("U+03A9")));
    // And what a FIELD expands to is the layout's to refuse: a file name in Cyrillic.
    spec.bates.prefix.clear();
    spec.areas[2] = QStringLiteral("<<file>>");
    core::FieldValues values = fixedValues();
    values.fileName = QStringLiteral("Договор.pdf");
    const auto laid = core::layoutHeaderFooter(spec, core::DecorationKind::HeaderFooter, letter(), values, core::FieldPage{0, 1, {}, {}});
    REQUIRE_FALSE(laid);
    CHECK(laid.error().messageKey == QLatin1String("error.decorate.characterRefused"));
}

TEST_CASE("headerfooter.spec: every field round-trips through JSON, and out-of-range values are refused by field", "[headerfooter]")
{
    core::HeaderFooterSpec spec;
    spec.areas[0] = QStringLiteral("<<date>>");
    spec.areas[5] = QStringLiteral("<<bates>>");
    spec.size = 11.5;
    spec.colour = {0.2, 0.4, 0.6};
    spec.underline = true;
    spec.top = 10;
    spec.bottom = 11;
    spec.left = 12;
    spec.right = 13;
    spec.shrink = true;
    spec.range = QStringLiteral("1-3, 7");
    spec.parity = core::PageParity::Even;
    spec.han = core::TypedFont::Japanese;
    spec.bates = core::BatesFormat{QStringLiteral("P"), QStringLiteral("S"), 9, 123};
    const QJsonObject json = core::specToJson(spec);
    auto back = core::specFromJson(QJsonDocument::fromJson(QJsonDocument(json).toJson()).object());
    REQUIRE(back);
    CHECK(back.value() == spec);

    for (const auto& [field, value] : {std::pair{"size", 200.0}, std::pair{"top", -1.0}, std::pair{"right", 1000.0}}) {
        INFO(field);
        QJsonObject bad = json;
        bad.insert(QLatin1String(field), value);
        auto refused = core::specFromJson(bad);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().messageKey == QLatin1String("error.decorate.badSpec"));
    }
    QJsonObject empty = json;
    empty.insert(QStringLiteral("areas"), QJsonObject{});
    CHECK(core::specFromJson(empty).error().messageKey == QLatin1String("error.decorate.empty"));

    const core::FieldValues values = fixedValues();
    CHECK(core::fieldValuesFromJson(core::fieldValuesToJson(values)).value() == values);
}

// ==========================================================================================
// A-93 -- one Bates number line across documents (headless half)
// ==========================================================================================

TEST_CASE("bates.allocator: three documents take consecutive numbers in the order given, and a refusal leaves no gap", "[bates][A-93]")
{
    core::BatesAllocator run(core::BatesFormat{QStringLiteral("CASE-"), QString(), 6, 100});
    auto a = run.take(QStringLiteral("merge-a.pdf"), 3);
    auto b = run.take(QStringLiteral("merge-b.pdf"), 5);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a.value() == core::BatesShare{QStringLiteral("merge-a.pdf"), 3, 100, 102});
    CHECK(b.value() == core::BatesShare{QStringLiteral("merge-b.pdf"), 5, 103, 107});
    // The third document's command refused: its share given back, and the next starts where it would have.
    auto c = run.take(QStringLiteral("merge-c.pdf"), 2);
    REQUIRE(c);
    REQUIRE(run.giveBack(c.value()));
    CHECK_FALSE(run.giveBack(a.value()));   // only the last share
    auto d = run.take(QStringLiteral("merge-d.pdf"), 1);
    CHECK(d.value().first == 108);
    CHECK(run.report().size() == 3);
    CHECK(run.next() == 109);

    // Past the digits: refused, nothing taken.
    core::BatesAllocator tight(core::BatesFormat{QString(), QString(), 2, 95});
    CHECK(tight.take(QStringLiteral("x"), 5).value().last == 99);
    auto over = tight.take(QStringLiteral("y"), 1);
    REQUIRE_FALSE(over);
    CHECK(over.error().messageKey == QLatin1String("error.decorate.batesOverflow"));
    CHECK(tight.next() == 100);
    CHECK(tight.take(QStringLiteral("z"), 0).error().messageKey == QLatin1String("error.decorate.noPages"));
    CHECK(core::largestBates(6) == 999999);
}
