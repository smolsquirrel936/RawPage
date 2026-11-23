// tst_watermark -- Stage 7 T15 (`Stage7-Assembly-Plan.md` D7-45), the watermark's own arithmetic, pure: each source's
// natural size, each anchor, offset, rotation and scale placed on the page AS SHOWN, tiling, and the spec's bounds and
// JSON -- every matrix against a value worked by hand in the comment beside it (guide A-99's headless half).
//
// WHAT IT DOES NOT COVER (convention 19): what is DRAWN. That the matrices put the ink where they say, at the opacity
// asked, behind or in front, is `tests/render/tst_watermarkengine`'s and the golden suite's; what the sink writes is
// `tst_watermarksink`'s.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/doc/PageTree.h"
#include "core/doc/Stamps.h"
#include "core/doc/Watermark.h"

#include <QJsonArray>

#include <array>

using namespace rawpage;
using Catch::Approx;

namespace {

core::StampImage picture(int width, int height)
{
    QByteArray rgb(width * height * 3, '\x40');
    auto image = core::stampImageFromPixels(width, height, rgb, QByteArray());
    REQUIRE(image);
    return image.value();
}

core::WatermarkSpec imageSpec(int width = 400, int height = 200)
{
    core::WatermarkSpec spec;
    spec.source = core::WatermarkSource::Image;
    spec.image = picture(width, height);   // 300 x 150 points
    spec.rotation = 0;
    spec.scaleMode = core::WatermarkScale::Absolute;
    spec.scale = 1.0;
    return spec;
}

core::Page page600x800(int rotate = 0)
{
    core::Page page(0, core::PdfRect{core::PdfPoint{0, 0}, core::PdfSize{600, 800}});
    page.setRotation(rotate);
    return page;
}

void checkMatrix(const std::array<double, 6>& got, const std::array<double, 6>& want)
{
    for (std::size_t i = 0; i < 6; ++i) {
        INFO("element " << i);
        CHECK(got[i] == Approx(want[i]).margin(1e-9));
    }
}

std::vector<std::array<double, 6>> tiles(const core::WatermarkSpec& spec, const core::Page& page)
{
    auto laid = core::layoutWatermark(spec, page);
    REQUIRE(laid);
    REQUIRE(laid.value().watermark);
    return laid.value().watermark->tiles;
}

} // namespace

TEST_CASE("unit.watermark: each source's natural size", "[watermark][A-99]")
{
    // An image: its pixels at 96 to the inch -- 400 x 200 pixels are 300 x 150 points.
    auto size = core::watermarkNaturalSize(imageSpec());
    REQUIRE(size);
    CHECK(size.value().width == Approx(300.0));
    CHECK(size.value().height == Approx(150.0));
    // Text: Helvetica's advances -- D 722, R 722, A 667, F 611, T 611 = 3333 thousandths -- at 72 points, one line of
    // 1.2 ems.
    core::WatermarkSpec text;
    text.text = QStringLiteral("DRAFT");
    text.size = 72;
    size = core::watermarkNaturalSize(text);
    REQUIRE(size);
    CHECK(size.value().width == Approx(3.333 * 72));
    CHECK(size.value().height == Approx(1.2 * 72));
    // Two lines: the wider, and two leadings.
    text.text = QStringLiteral("DRAFT\nA");
    size = core::watermarkNaturalSize(text);
    REQUIRE(size);
    CHECK(size.value().width == Approx(3.333 * 72));
    CHECK(size.value().height == Approx(2.4 * 72));
    // A page: as it shows.
    core::WatermarkSpec page;
    page.source = core::WatermarkSource::Page;
    page.page = core::StampPage{QByteArray("%PDF-1.7 a page"), 200.0, 100.0};
    size = core::watermarkNaturalSize(page);
    REQUIRE(size);
    CHECK(size.value().width == 200.0);
    CHECK(size.value().height == 100.0);
    // An update keeping a picture: the size it recorded.
    core::WatermarkSpec kept = imageSpec();
    kept.image = {};
    kept.keepSourceOf = QString(32, QLatin1Char('a'));
    kept.sourceSize = core::PdfSize{50, 60};
    size = core::watermarkNaturalSize(kept);
    REQUIRE(size);
    CHECK(size.value().width == 50.0);
    CHECK(size.value().height == 60.0);
}

TEST_CASE("unit.watermark: anchors, offsets, rotation and scale on the page as shown", "[watermark][A-99]")
{
    core::WatermarkSpec spec = imageSpec();   // 300 x 150 on a 600 x 800 page
    // Centred: its box from (150, 325).
    REQUIRE(tiles(spec, page600x800()).size() == 1);
    checkMatrix(tiles(spec, page600x800()).front(), {1, 0, 0, 1, 150, 325});
    // Top-left, moved 10 right and 20 down: the box's top-left at (10, 780), its bottom at 630.
    spec.anchor = core::WatermarkAnchor::TopLeft;
    spec.offsetX = 10;
    spec.offsetY = -20;
    checkMatrix(tiles(spec, page600x800()).front(), {1, 0, 0, 1, 10, 630});
    // Bottom-right: its box from (300, 0).
    spec.anchor = core::WatermarkAnchor::BottomRight;
    spec.offsetX = spec.offsetY = 0;
    checkMatrix(tiles(spec, page600x800()).front(), {1, 0, 0, 1, 300, 0});
    // Top (centre across): from (150, 650). Right (centre down): from (300, 325).
    spec.anchor = core::WatermarkAnchor::Top;
    checkMatrix(tiles(spec, page600x800()).front(), {1, 0, 0, 1, 150, 650});
    spec.anchor = core::WatermarkAnchor::Right;
    checkMatrix(tiles(spec, page600x800()).front(), {1, 0, 0, 1, 300, 325});
    // Turned 90 counter-clockwise, centred: the turned box 150 x 300 from (225, 250), so the natural origin lands at
    // its bottom-RIGHT corner, (375, 250), the width running up.
    spec.anchor = core::WatermarkAnchor::Centre;
    spec.rotation = 90;
    checkMatrix(tiles(spec, page600x800()).front(), {0, 1, -1, 0, 375, 250});
    // Half the size, unturned: 150 x 75 from (225, 362.5).
    spec.rotation = 0;
    spec.scale = 0.5;
    checkMatrix(tiles(spec, page600x800()).front(), {0.5, 0, 0, 0.5, 225, 362.5});
    // RELATIVE: the largest that keeps it inside that fraction of the page -- 0.5 of min(600/300, 800/150) is 1.
    spec.scaleMode = core::WatermarkScale::Relative;
    spec.scale = 0.5;
    checkMatrix(tiles(spec, page600x800()).front(), {1, 0, 0, 1, 150, 325});
    // Relative and turned 90: the turned box is 150 x 300 a unit, so 1.0 of min(600/150, 800/300) = 8/3.
    spec.scale = 1.0;
    spec.rotation = 90;
    const double k = 800.0 / 300.0;
    // The turned box 400 x 800 from (100, 0): the natural origin at its bottom-right, (500, 0).
    checkMatrix(tiles(spec, page600x800()).front(), {0, k, -k, 0, 500, 0});
    // 45 degrees, absolute 1: the turned box (300 + 150)/sqrt 2 on each side, centred on (300, 400).
    spec.scaleMode = core::WatermarkScale::Absolute;
    spec.rotation = 45;
    const double c = std::sqrt(0.5);
    const auto m = tiles(spec, page600x800()).front();
    checkMatrix(m, {c, c, -c, c, 300 - (c * 150 - c * 75), 400 - (c * 150 + c * 75)});
}

TEST_CASE("unit.watermark: a page turned by /Rotate is placed as it shows", "[watermark][A-99]")
{
    // Turned 90 the page shows 800 x 600; centred, the shown box is from (250, 225). The shown space maps into the
    // page's own by (x, y) -> (600 - y, x), so the natural origin lands at (375, 250) and the width runs up the page --
    // which, turned clockwise for showing, reads left to right.
    core::WatermarkSpec spec = imageSpec();
    checkMatrix(tiles(spec, page600x800(90)).front(), {0, 1, -1, 0, 375, 250});
    // 180: shown 600 x 800, from (150, 325); (x, y) -> (600 - x, 800 - y): the origin at (450, 475), reversed both ways.
    checkMatrix(tiles(spec, page600x800(180)).front(), {-1, 0, 0, -1, 450, 475});
    // 270: shown 800 x 600, from (250, 225); (x, y) -> (y, 800 - x): the origin at (225, 550), the width running down.
    checkMatrix(tiles(spec, page600x800(270)).front(), {0, -1, 1, 0, 225, 550});
}

TEST_CASE("unit.watermark: tiling covers the page from the anchored copy, and too many copies are refused", "[watermark][A-99]")
{
    core::WatermarkSpec spec = imageSpec();
    spec.tile = true;
    spec.tileGap = 50;
    // Anchored at (150, 325), a copy every 350 across and 200 down: lefts -200, 150, 500 and bottoms -75 ... 725 --
    // every copy that touches the 600 x 800 page, 3 x 5, the top row first, each row left to right.
    const auto all = tiles(spec, page600x800());
    REQUIRE(all.size() == 15);
    checkMatrix(all.front(), {1, 0, 0, 1, -200, 725});
    checkMatrix(all[1], {1, 0, 0, 1, 150, 725});
    checkMatrix(all.back(), {1, 0, 0, 1, 500, -75});
    // A 3-point picture with no gap would be 200 x 267 copies: refused by key, not drawn thinly.
    core::WatermarkSpec tiny = imageSpec(4, 4);
    tiny.tile = true;
    tiny.tileGap = 0;
    auto refused = core::layoutWatermark(tiny, page600x800());
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.decorate.tooManyTiles"));
}

TEST_CASE("unit.watermark: text lines are centred in the natural box, and behind has no wrapper while in front does", "[watermark][A-99]")
{
    core::WatermarkSpec spec;
    spec.text = QStringLiteral("DRAFT\nA");
    spec.size = 10;
    spec.colour = {1, 0, 0};
    auto laid = core::layoutWatermark(spec, page600x800());
    REQUIRE(laid);
    const core::PageDecoration& d = laid.value();
    CHECK(d.kind == core::DecorationKind::Watermark);
    CHECK(d.behind);
    CHECK_FALSE(d.wrap);
    CHECK(d.subtype == QLatin1String("Watermark"));
    REQUIRE(d.watermark);
    REQUIRE(d.watermark->lines.size() == 2);
    // Natural box 33.33 x 24: the first baseline 0.9 em below its top, the second a leading lower; each centred.
    CHECK(d.watermark->lines[0].matrix[4] == Approx(0.0).margin(1e-9));
    CHECK(d.watermark->lines[0].matrix[5] == Approx(24.0 - 9.0));
    CHECK(d.watermark->lines[1].matrix[4] == Approx((33.33 - 6.67) / 2.0));
    CHECK(d.watermark->lines[1].matrix[5] == Approx(24.0 - 9.0 - 12.0));
    CHECK(d.watermark->lines[0].colour == std::array<double, 3>{1, 0, 0});
    spec.behind = false;
    laid = core::layoutWatermark(spec, page600x800());
    REQUIRE(laid);
    REQUIRE(laid.value().wrap);
    CHECK(laid.value().wrap->clip == page600x800().cropBox());
    CHECK(laid.value().wrap->matrix == std::array<double, 6>{1, 0, 0, 1, 0, 0});
}

TEST_CASE("unit.watermark: the spec's bounds, refused by key", "[watermark][A-99]")
{
    core::WatermarkSpec spec;
    spec.text = QStringLiteral("OK");
    CHECK(core::validate(spec));
    const auto refusedBy = [](const core::WatermarkSpec& s) { return core::validate(s).error().messageKey; };
    const auto context = [](const core::WatermarkSpec& s) { return core::validate(s).error().context; };
    core::WatermarkSpec bad = spec;
    bad.opacity = 0;
    CHECK(refusedBy(bad) == QLatin1String("error.decorate.badSpec"));
    CHECK(context(bad) == QLatin1String("opacity"));
    bad = spec;
    bad.scale = 2;   // relative: at most the whole page
    CHECK(context(bad) == QLatin1String("scale"));
    bad.scaleMode = core::WatermarkScale::Absolute;
    CHECK(core::validate(bad));
    bad = spec;
    bad.rotation = 361;
    CHECK(context(bad) == QLatin1String("rotation"));
    bad = spec;
    bad.text = QStringLiteral("   ");
    CHECK(refusedBy(bad) == QLatin1String("error.decorate.empty"));
    bad = spec;
    bad.text = QString::fromUtf8("\xD0\x93\xD0\xA0");   // Cyrillic: named
    CHECK(refusedBy(bad) == QLatin1String("error.decorate.characterRefused"));
    CHECK(context(bad).contains(QLatin1String("U+0413")));
    bad = spec;
    bad.source = core::WatermarkSource::Image;   // no picture
    CHECK(context(bad) == QLatin1String("image"));
    bad.keepSourceOf = QStringLiteral("not a key");
    CHECK(context(bad) == QLatin1String("keepSourceOf"));
    bad.keepSourceOf = QString(32, QLatin1Char('b'));
    CHECK(context(bad) == QLatin1String("sourceSize"));
    bad.sourceSize = core::PdfSize{10, 10};
    CHECK(core::validate(bad));
}

TEST_CASE("unit.watermark: the spec as JSON, with its source and without", "[watermark][A-99]")
{
    core::WatermarkSpec spec = imageSpec();
    spec.opacity = 0.3;
    spec.rotation = -30;
    spec.anchor = core::WatermarkAnchor::BottomLeft;
    spec.offsetX = 12;
    spec.offsetY = 7;
    spec.tile = true;
    spec.tileGap = 20;
    spec.behind = false;
    spec.usage = core::WatermarkUsage::PrintOnly;
    spec.range = QStringLiteral("2-5");
    spec.parity = core::PageParity::Even;
    spec.sourceName = QStringLiteral("logo.png");
    auto back = core::watermarkFromJson(core::watermarkToJson(spec, true), true);
    REQUIRE(back);
    CHECK(back.value() == spec);
    // Without the bytes: the natural size stands for them -- refused where the source is required, kept where not.
    const QJsonObject record = core::watermarkToJson(spec, false);
    CHECK_FALSE(record.contains(QStringLiteral("image")));
    CHECK(record.value(QStringLiteral("sourceSize")).toArray().at(0).toDouble() == 300.0);
    auto required = core::watermarkFromJson(record, true);
    REQUIRE_FALSE(required);
    CHECK(required.error().context == QLatin1String("image"));
    auto listed = core::watermarkFromJson(record, false);
    REQUIRE(listed);
    CHECK(listed.value().sourceSize.width == 300.0);
    CHECK(listed.value().image.isEmpty());
    // Text, whole.
    core::WatermarkSpec text;
    text.text = QStringLiteral("第一章 CONFIDENTIAL");
    text.han = core::TypedFont::Japanese;
    text.colour = {0.1, 0.2, 0.3};
    text.usage = core::WatermarkUsage::ScreenOnly;
    back = core::watermarkFromJson(core::watermarkToJson(text, false), true);
    REQUIRE(back);
    CHECK(back.value() == text);
    // A hostile field refused, naming it.
    QJsonObject hostile = core::watermarkToJson(text, true);
    hostile.insert(QStringLiteral("anchor"), QStringLiteral("nowhere"));
    CHECK(core::watermarkFromJson(hostile, true).error().context == QLatin1String("anchor"));
}
