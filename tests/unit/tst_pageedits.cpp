// tst_pageedits -- Stage 7 T9 (`Stage7-Assembly-Plan.md` §6, D7-15, D7-32): page boxes, crop, resize, renumber and
// embedded thumbnails, through the SHIPPED SINK and save -- what each writes, read back first-party from the file; an
// undo before a save writing nothing; an undo after a save writing back what the page had
// (`Stage7-Assembly-Testing-Guide.md` §5.11, A-58 and A-60 ... A-63's first-party halves).
//
// WHAT IT DOES NOT COVER (convention 19): what a READER makes of it. Every box, label and thumbnail here is read back by
// our own reader; PDFium's reading of the same files -- `FPDFPage_Get*Box`, `FPDF_GetPageLabel`,
// `FPDFPage_GetThumbnailAsBitmap` -- and the INK a crop or a resize leaves are `tests/render/tst_pageedits`'.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/PageTree.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/cos/filters/FlateFilter.h"
#include "pdf/ops/PageOps.h"

#include <QDir>
#include <QFile>

#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using Catch::Approx;
using pdf::cos::CosObject;

namespace {

struct Read {
    QByteArray bytes;
    pdf::cos::XrefTable table;
    pdf::ops::PageWalk walk;

    CosObject object(long long number)
    {
        auto fetched = table.fetch(number);
        REQUIRE(fetched);
        return fetched.value().object;
    }
    CosObject resolved(const CosObject& value) { return value.isReference() ? object(value.reference().number) : value; }
    CosObject page(int index) { return object(walk.pages[static_cast<std::size_t>(index)].number); }
    // A stream's data with its filters undone (FlateDecode, the one RawPage writes).
    QByteArray decoded(const CosObject& reference)
    {
        const CosObject stream = resolved(reference);
        REQUIRE(stream.isStream());
        const auto& data = stream.streamData();
        const std::string_view raw(bytes.constData() + data.dataOffset, data.dataLength);
        if (!stream.has("Filter")) return QByteArray(raw.data(), static_cast<qsizetype>(raw.size()));
        auto plain = pdf::cos::filters::flateDecode(raw);
        REQUIRE(plain);
        return QByteArray(reinterpret_cast<const char*>(plain.value().data()), static_cast<qsizetype>(plain.value().size()));
    }
    // What page `index` shows, as `PageOps` reads it with what the walk says it inherits.
    core::PageBoxes boxes(int index)
    {
        const pdf::ops::PageEntry& entry = walk.pages[static_cast<std::size_t>(index)];
        std::map<std::string, CosObject> inherited;
        const auto asObject = [](const std::array<double, 4>& b) {
            return CosObject::makeArray({CosObject::makeReal(b[0]), CosObject::makeReal(b[1]), CosObject::makeReal(b[2]), CosObject::makeReal(b[3])});
        };
        if (entry.mediaBox) inherited.emplace("MediaBox", asObject(*entry.mediaBox));
        if (entry.cropBox) inherited.emplace("CropBox", asObject(*entry.cropBox));
        return pdf::ops::PageOps::shownBoxes(page(index), inherited);
    }
};

std::unique_ptr<Read> readBytes(QByteArray bytes)
{
    auto out = std::make_unique<Read>();
    out->bytes = std::move(bytes);
    auto opened = pdf::cos::openWithRepair(std::string_view(out->bytes.constData(), static_cast<std::size_t>(out->bytes.size())));
    REQUIRE(opened);
    out->table = std::move(opened.value().table);
    auto walk = pdf::ops::PageOps::walk(out->table);
    REQUIRE(walk);
    out->walk = std::move(walk).value();
    return out;
}

std::unique_ptr<Read> readSaved(tests::SaveSession& session)
{
    return readBytes(tests::readAllBytes(session.path));
}

bool save(tests::SaveSession& session, const char* name)
{
    if (!session.coordinator->save(*session.journal, session.path)) return false;
    // Kept for `qpdf --check` in the portable-asan job (D7-18's oracle 2), as every suite that writes a file keeps it.
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    if (!QDir().mkpath(dir)) return false;
    const QString target = QDir(dir).filePath(QStringLiteral("pageedits-%1.pdf").arg(QLatin1String(name)));
    QFile::remove(target);
    return QFile::copy(session.path, target);
}

bool deltaIsEmpty(tests::SaveSession& session)
{
    auto delta = session.sink()->delta();
    REQUIRE(delta);
    return delta.value().isEmpty();
}

core::PdfRect rect(double left, double bottom, double right, double top)
{
    return core::PdfRect{core::PdfPoint{left, bottom}, core::PdfSize{right - left, top - bottom}};
}

} // namespace

// ==========================================================================================
// A-60 -- the five boxes, written and read back (first-party half)
// ==========================================================================================

TEST_CASE("pageops.boxes: each box written and read back, undone before a save writing nothing, after one writing them back", "[pageedits][boxes][A-60]")
{
    auto session = tests::openCopy("assembly/boxes.pdf");
    REQUIRE(session);
    auto original = readSaved(*session);
    const core::PageBoxes page1 = original->boxes(0);
    REQUIRE(page1.crop == rect(18, 18, 594, 774));   // the label's, read from the bytes first
    REQUIRE(page1.art == rect(72, 72, 540, 720));

    SECTION("each box alone, and all five together")
    {
        const core::PageBoxes asked{rect(0, 0, 612, 792), rect(50, 60, 560, 700), rect(40, 50, 570, 710),
                                    rect(45, 55, 565, 705), rect(100, 100, 500, 600)};
        for (int which = 0; which < 5; ++which) {
            INFO("box " << which);
            core::PageBoxes one = page1;
            switch (which) {
            case 0: one.media = rect(-10, -10, 622, 802); break;
            case 1: one.crop = asked.crop; break;
            case 2: one.bleed = asked.bleed; break;
            case 3: one.trim = asked.trim; break;
            case 4: one.art = asked.art; break;
            }
            REQUIRE(session->journal->run(std::make_unique<core::CropPagesCommand>(std::vector<int>{0}, std::vector<core::PageBoxes>{one})));
            REQUIRE(save(*session, "boxes-one"));
            CHECK(readSaved(*session)->boxes(0) == one);
            CHECK(session->journal->document().pages().at(0)->cropBox() == (one.crop ? *one.crop : one.media));
            REQUIRE(session->journal->undo());
            REQUIRE(save(*session, "boxes-one-undone"));
            CHECK(readSaved(*session)->boxes(0) == page1);   // AFTER a save: the boxes the page had, written back
        }
        REQUIRE(session->journal->run(std::make_unique<core::CropPagesCommand>(std::vector<int>{0}, std::vector<core::PageBoxes>{asked})));
        REQUIRE(save(*session, "boxes-all"));
        CHECK(readSaved(*session)->boxes(0) == asked);
    }
    SECTION("an undo before a save writes nothing")
    {
        core::PageBoxes cropped = page1;
        cropped.crop = rect(100, 100, 300, 400);
        REQUIRE(session->journal->run(std::make_unique<core::CropPagesCommand>(std::vector<int>{0, 2}, std::vector<core::PageBoxes>{cropped})));
        CHECK_FALSE(deltaIsEmpty(*session));
        REQUIRE(session->journal->undo());
        CHECK(deltaIsEmpty(*session));
    }
    SECTION("UNDO A CROP: the CropBox removed, the MediaBox shown")
    {
        core::PageBoxes uncropped = page1;
        uncropped.crop.reset();
        REQUIRE(session->journal->run(std::make_unique<core::CropPagesCommand>(std::vector<int>{0}, std::vector<core::PageBoxes>{uncropped})));
        REQUIRE(save(*session, "boxes-uncropped"));
        auto saved = readSaved(*session);
        CHECK_FALSE(saved->page(0).has("CropBox"));
        CHECK(session->journal->document().pages().at(0)->cropBox() == page1.media);
    }
}

TEST_CASE("pageops.boxes: undo a crop on a page that INHERITS its CropBox shows the MediaBox", "[pageedits][boxes][A-60]")
{
    // `inherit-resources.pdf`'s pages take /MediaBox and /CropBox from their /Pages nodes (Stage 7 guide I-01). Removing
    // the page's own CropBox would leave it inheriting one: the page must say its MediaBox instead.
    auto session = tests::openCopy("assembly/inherit-resources.pdf");
    REQUIRE(session);
    auto original = readSaved(*session);
    REQUIRE_FALSE(original->page(0).has("CropBox"));
    const core::PageBoxes before = original->boxes(0);
    REQUIRE(before.crop);                              // inherited
    REQUIRE_FALSE(*before.crop == before.media);
    core::PageBoxes uncropped = before;
    uncropped.crop.reset();
    REQUIRE(session->journal->run(std::make_unique<core::CropPagesCommand>(std::vector<int>{0}, std::vector<core::PageBoxes>{uncropped})));
    REQUIRE(save(*session, "boxes-uncropped-inherited"));
    auto saved = readSaved(*session);
    const core::PageBoxes shown = saved->boxes(0);
    CHECK((shown.crop ? *shown.crop : shown.media) == before.media);   // what a reader shows is the sheet
    REQUIRE(session->journal->undo());
    REQUIRE(save(*session, "boxes-uncropped-inherited-undone"));
    CHECK(readSaved(*session)->boxes(0).crop == before.crop);
}

TEST_CASE("pageops.boxes: a box outside the MediaBox is clipped to it, and the report says so", "[pageedits][boxes][A-60]")
{
    auto session = tests::openCopy("assembly/boxes.pdf");
    REQUIRE(session);
    core::PageBoxes outside{rect(0, 0, 595.276, 841.89), rect(-40, 200, 400, 900), {}, rect(-5, -5, 100, 100), {}};
    auto command = std::make_unique<core::CropPagesCommand>(std::vector<int>{1}, std::vector<core::PageBoxes>{outside});
    const core::CropPagesCommand* raw = command.get();
    REQUIRE(session->journal->run(std::move(command)));
    REQUIRE(raw->report().references.size() == 2);
    for (const core::PageReference& reference : raw->report().references) {
        CHECK(reference.kind == core::PageReference::Kind::PageBox);
        CHECK(reference.treatment == core::PageReference::Treatment::Clipped);
        CHECK(reference.page == 1);
    }
    REQUIRE(save(*session, "boxes-clipped"));
    const core::PageBoxes saved = readSaved(*session)->boxes(1);
    CHECK(saved.crop == rect(0, 200, 400, 841.89));   // the label's `cropBoxClippedToMedia`
    CHECK(saved.trim == rect(0, 0, 100, 100));

    // REFUSED, nothing changed: a box that does not touch the MediaBox, and a CropBox under three points.
    const core::Status away = session->journal->run(std::make_unique<core::CropPagesCommand>(
        std::vector<int>{1}, std::vector<core::PageBoxes>{core::PageBoxes{rect(0, 0, 595.276, 841.89), rect(700, 900, 800, 1000), {}, {}, {}}}));
    CHECK_FALSE(away);
    const core::Status sliver = session->journal->run(std::make_unique<core::CropPagesCommand>(
        std::vector<int>{1}, std::vector<core::PageBoxes>{core::PageBoxes{rect(0, 0, 595.276, 841.89), rect(10, 10, 12, 500), {}, {}, {}}}));
    CHECK_FALSE(sliver);
    CHECK(deltaIsEmpty(*session));
}

// ==========================================================================================
// A-61 -- white margins (the arithmetic; the ink is the render suite's)
// ==========================================================================================

TEST_CASE("crop.white_margins: the content box padded inside what the page shows, and pages of the same size", "[pageedits][crop][A-61]")
{
    CHECK(core::whiteMarginBox(rect(100, 100, 200, 300), 6, rect(0, 0, 612, 792)) == rect(94, 94, 206, 306));
    CHECK(core::whiteMarginBox(rect(2, 2, 200, 300), 6, rect(0, 0, 612, 792)) == rect(0, 0, 206, 306));   // never past the crop
    CHECK(core::whiteMarginBox(rect(100, 100, 200, 300), -4, rect(0, 0, 612, 792)) == rect(100, 100, 200, 300));
    CHECK(core::whiteMarginBox(rect(700, 700, 800, 800), 0, rect(0, 0, 612, 792)) == rect(0, 0, 612, 792));   // outside: unchanged

    auto session = tests::openCopy("ordinary/mixed-page-sizes.pdf");
    REQUIRE(session);
    const core::PageTree& pages = session->journal->document().pages();
    REQUIRE(pages.count() >= 4);
    for (int page = 0; page < pages.count(); ++page) {
        INFO("page " << page);
        const std::vector<int> same = core::pagesOfSameSize(pages, page);
        REQUIRE_FALSE(same.empty());
        CHECK(std::find(same.begin(), same.end(), page) != same.end());
        for (const int other : same) CHECK(pages.displaySize(other).width == Approx(pages.displaySize(page).width).margin(0.01));
    }
    // A4 portrait and A4 landscape are not the same size as SHOWN.
    CHECK(core::pagesOfSameSize(pages, 0) != core::pagesOfSameSize(pages, 1));
}

// ==========================================================================================
// A-62 -- a resize: the plan's arithmetic, and the wrapper written and taken out again
// ==========================================================================================

TEST_CASE("resize: the plan scales what is shown to fit, places it at its anchor, and turns with the page", "[pageedits][resize][A-62]")
{
    const core::PdfSize a4{595.276, 841.89};
    core::Page letter(0, rect(0, 0, 612, 792));
    auto centred = core::planResize(letter, a4, true, core::ResizeAnchor::Centre);
    REQUIRE(centred);
    REQUIRE(centred.value().wrap);
    const double s = std::min(595.276 / 612.0, 841.89 / 792.0);
    const std::array<double, 6>& m = centred.value().wrap->matrix;
    CHECK(m[0] == Approx(s));
    CHECK(m[3] == Approx(s));
    CHECK(m[4] == Approx(0.0).margin(1e-9));                     // as wide as the target: no room across
    CHECK(m[5] == Approx((841.89 - 792.0 * s) / 2.0));           // centred down the page
    CHECK(centred.value().boxes.media == rect(0, 0, 595.276, 841.89));
    CHECK_FALSE(centred.value().boxes.crop);
    CHECK(centred.value().wrap->clip == rect(0, 0, 612, 792));

    // A CROPPED page scales what it SHOWS, from its own corner.
    core::Page cropped(0, rect(0, 0, 612, 792));
    cropped.setCropBox(rect(100, 200, 400, 600));
    auto top = core::planResize(cropped, core::PdfSize{600, 600}, true, core::ResizeAnchor::TopLeft);
    REQUIRE(top);
    const std::array<double, 6>& t = top.value().wrap->matrix;
    CHECK(t[0] == Approx(1.5));   // 400 tall into 600
    // The crop's top-left (100, 600) lands on the new page's top-left (0, 600).
    CHECK(t[0] * 100 + t[4] == Approx(0.0).margin(1e-9));
    CHECK(t[3] * 600 + t[5] == Approx(600.0));

    // TURNED A QUARTER: the target is as shown, so its sides swap in the page's own space, and the shown top-left is
    // the page's own bottom-left.
    core::Page turned(0, rect(0, 0, 612, 792));
    turned.setRotation(90);
    auto landscape = core::planResize(turned, core::PdfSize{841.89, 595.276}, true, core::ResizeAnchor::TopLeft);
    REQUIRE(landscape);
    CHECK(landscape.value().boxes.media == rect(0, 0, 595.276, 841.89));
    CHECK(landscape.value().wrap->matrix[4] == Approx(0.0).margin(1e-9));
    CHECK(landscape.value().wrap->matrix[5] == Approx(0.0).margin(1e-9));
    // An anchor that is NOT symmetric under the turn: the shown TOP CENTRE of a page turned 90 is its own LEFT edge,
    // halfway up -- the content against x = 0 and centred in y (top-left above cannot tell the two axes apart).
    auto topCentre = core::planResize(turned, core::PdfSize{841.89, 595.276}, true, core::ResizeAnchor::Top);
    REQUIRE(topCentre);
    const double t90 = std::min(595.276 / 612.0, 841.89 / 792.0);
    CHECK(topCentre.value().wrap->matrix[4] == Approx(0.0).margin(1e-9));
    CHECK(topCentre.value().wrap->matrix[5] == Approx((841.89 - 792.0 * t90) / 2.0));

    // WITHOUT SCALING the content stays where it is and the MediaBox is placed around it.
    auto unscaled = core::planResize(letter, core::PdfSize{700, 900}, false, core::ResizeAnchor::Centre);
    REQUIRE(unscaled);
    CHECK_FALSE(unscaled.value().wrap);
    CHECK(unscaled.value().boxes.media == rect(-44, -54, 656, 846));
    auto corner = core::planResize(letter, core::PdfSize{300, 400}, false, core::ResizeAnchor::BottomRight);
    REQUIRE(corner);
    CHECK(corner.value().boxes.media == rect(312, 0, 612, 400));

    // Refused: a target under three points, and one past Annex C's 14,400.
    CHECK_FALSE(core::planResize(letter, core::PdfSize{2, 400}, true, core::ResizeAnchor::Centre));
    CHECK_FALSE(core::planResize(letter, core::PdfSize{400, 14401}, false, core::ResizeAnchor::Centre));
}

TEST_CASE("resize: the content is wrapped in two marked streams and unwrapped exactly, before a save and after", "[pageedits][resize][A-62]")
{
    auto session = tests::openCopy("ordinary/text-letter.pdf");
    REQUIRE(session);
    auto original = readSaved(*session);
    const CosObject contentsBefore = original->page(0).get("Contents");
    const core::PageBoxes boxesBefore = original->boxes(0);

    REQUIRE(session->journal->run(std::make_unique<core::ResizePagesCommand>(std::vector<int>{0}, core::PdfSize{595.276, 841.89}, true,
                                                                             core::ResizeAnchor::Centre)));
    CHECK(session->journal->document().pages().at(0)->mediaBox() == rect(0, 0, 595.276, 841.89));
    REQUIRE(session->journal->undo());
    CHECK(deltaIsEmpty(*session));   // before a save: nothing to write
    REQUIRE(session->journal->redo());
    REQUIRE(save(*session, "resize-wrapped"));

    auto saved = readSaved(*session);
    const CosObject page = saved->page(0);
    CHECK(saved->boxes(0).media == rect(0, 0, 595.276, 841.89));
    const CosObject contents = saved->resolved(page.get("Contents"));
    REQUIRE(contents.isArray());
    REQUIRE(contents.items().size() == 3);
    const CosObject before = saved->resolved(contents.items().front());
    const CosObject after = saved->resolved(contents.items().back());
    CHECK(pdf::ops::PageOps::wrapId(before).size() == 32);
    CHECK(pdf::ops::PageOps::wrapId(before) == pdf::ops::PageOps::wrapId(after));
    CHECK(saved->decoded(contents.items().front()).startsWith("q 0.9727 0 0 0.9727 0 "));
    CHECK(saved->decoded(contents.items().front()).endsWith("re W n\n"));
    CHECK(saved->decoded(contents.items().back()) == "Q\n");
    CosObject middle = contents.items()[1];
    CosObject was = contentsBefore;
    middle.clearSpansRecursively();
    was.clearSpansRecursively();
    CHECK(middle == was);   // the page's own content, untouched, between them

    // AFTER THE SAVE: the undo takes the two streams out of /Contents again by their mark, and the MediaBox back.
    REQUIRE(session->journal->undo());
    REQUIRE(save(*session, "resize-unwrapped"));
    auto undone = readSaved(*session);
    CosObject back = undone->page(0).get("Contents");
    back.clearSpansRecursively();
    CHECK(back == was);
    CHECK(undone->boxes(0) == boxesBefore);

    // Two resizes nest, and the inner one comes out alone.
    REQUIRE(session->journal->run(std::make_unique<core::ResizePagesCommand>(std::vector<int>{0}, core::PdfSize{500, 700}, true,
                                                                             core::ResizeAnchor::Centre)));
    REQUIRE(save(*session, "resize-first"));
    REQUIRE(session->journal->run(std::make_unique<core::ResizePagesCommand>(std::vector<int>{0}, core::PdfSize{400, 400}, true,
                                                                             core::ResizeAnchor::TopLeft)));
    REQUIRE(save(*session, "resize-second"));
    auto twice = readSaved(*session);
    CHECK(twice->resolved(twice->page(0).get("Contents")).items().size() == 5);
    REQUIRE(session->journal->undo());
    REQUIRE(save(*session, "resize-second-undone"));
    auto once = readSaved(*session);
    CHECK(once->resolved(once->page(0).get("Contents")).items().size() == 3);
    CHECK(once->boxes(0).media == rect(0, 0, 500, 700));
}

// ==========================================================================================
// A-63 -- renumbering (first-party half)
// ==========================================================================================

TEST_CASE("renumber: labels per range in every style, the rest kept, and undone exactly", "[pageedits][renumber][A-63]")
{
    SECTION("a document with no labels: numbered as a reader numbers it, then the range")
    {
        auto session = tests::openCopy("assembly/odd-count-9.pdf");
        REQUIRE(session);
        REQUIRE(pdf::ops::PageOps::readLabels(readSaved(*session)->table, 9).empty());
        REQUIRE(session->journal->run(std::make_unique<core::RenumberPagesCommand>(std::vector<int>{0, 1, 2}, core::PageLabelStyle::RomanLower,
                                                                                   QString(), 1)));
        std::vector<QString> shown;
        for (const core::Page& page : session->journal->document().pages().pages()) shown.push_back(page.label());
        CHECK(shown == std::vector<QString>{"i", "ii", "iii", "4", "5", "6", "7", "8", "9"});
        REQUIRE(save(*session, "renumber-roman"));
        const std::vector<pdf::ops::PageLabelRange> read = pdf::ops::PageOps::readLabels(readSaved(*session)->table, 9);
        REQUIRE(read.size() == 9);
        CHECK(pdf::ops::PageOps::formatLabel(read[2]) == "iii");
        CHECK(pdf::ops::PageOps::formatLabel(read[3]) == "4");
        // Undone after the save: the document has no /PageLabels again, and shows no label.
        REQUIRE(session->journal->undo());
        REQUIRE(save(*session, "renumber-roman-undone"));
        auto undone = readSaved(*session);
        CHECK_FALSE(undone->resolved(undone->table.trailer().get("Root")).has("PageLabels"));
        CHECK(session->journal->document().pages().at(0)->label().isEmpty());
    }
    SECTION("each style, a prefix and a start, against hand-written tables")
    {
        auto session = tests::openCopy("assembly/odd-count-9.pdf");
        REQUIRE(session);
        const std::vector<std::pair<core::PageLabelStyle, std::vector<QString>>> styles{
            {core::PageLabelStyle::Decimal, {"P-26", "P-27", "P-28"}},
            {core::PageLabelStyle::RomanUpper, {"P-XXVI", "P-XXVII", "P-XXVIII"}},
            {core::PageLabelStyle::RomanLower, {"P-xxvi", "P-xxvii", "P-xxviii"}},
            {core::PageLabelStyle::LettersUpper, {"P-Z", "P-AA", "P-BB"}},
            {core::PageLabelStyle::LettersLower, {"P-z", "P-aa", "P-bb"}},
            {core::PageLabelStyle::None, {"P-", "P-", "P-"}},
        };
        for (const auto& [style, want] : styles) {
            INFO("style " << static_cast<int>(style));
            REQUIRE(session->journal->run(std::make_unique<core::RenumberPagesCommand>(std::vector<int>{4, 5, 6}, style, QStringLiteral("P-"), 26)));
            REQUIRE(save(*session, "renumber-style"));
            const std::vector<pdf::ops::PageLabelRange> read = pdf::ops::PageOps::readLabels(readSaved(*session)->table, 9);
            REQUIRE(read.size() == 9);
            for (int i = 0; i < 3; ++i) CHECK(pdf::ops::PageOps::formatLabel(read[static_cast<std::size_t>(4 + i)]) == want[static_cast<std::size_t>(i)]);
            CHECK(pdf::ops::PageOps::formatLabel(read[3]) == "4");   // outside the range: as it was
            CHECK(pdf::ops::PageOps::formatLabel(read[7]) == "8");
            REQUIRE(session->journal->undo());
        }
    }
    SECTION("a document with labels keeps every other page's, and an undo restores its own tree")
    {
        auto session = tests::openCopy("ordinary/outline-labels.pdf");
        REQUIRE(session);
        const int count = session->journal->document().pages().count();
        const std::vector<pdf::ops::PageLabelRange> before = pdf::ops::PageOps::readLabels(readSaved(*session)->table, count);
        REQUIRE_FALSE(before.empty());
        REQUIRE(session->journal->run(std::make_unique<core::RenumberPagesCommand>(std::vector<int>{count - 1}, core::PageLabelStyle::Decimal,
                                                                                   QStringLiteral("End "), 1)));
        REQUIRE(save(*session, "renumber-kept"));
        const std::vector<pdf::ops::PageLabelRange> after = pdf::ops::PageOps::readLabels(readSaved(*session)->table, count);
        for (int page = 0; page + 1 < count; ++page) CHECK(after[static_cast<std::size_t>(page)] == before[static_cast<std::size_t>(page)]);
        CHECK(pdf::ops::PageOps::formatLabel(after.back()) == "End 1");
        REQUIRE(session->journal->undo());
        CHECK(deltaIsEmpty(*session) == false);   // after the save: the old numbering is written back
        REQUIRE(save(*session, "renumber-kept-undone"));
        CHECK(pdf::ops::PageOps::readLabels(readSaved(*session)->table, count) == before);
    }
}

// ==========================================================================================
// A-58 -- embedded thumbnails (first-party half)
// ==========================================================================================

TEST_CASE("thumbnails.embed: each page's /Thumb written as the image given, removed, and undone", "[pageedits][thumbnails][A-58]")
{
    auto session = tests::openCopy("assembly/odd-count-3.pdf");
    REQUIRE(session);
    std::vector<core::PageThumbnail> images;
    for (int page = 0; page < 3; ++page) {
        core::PageThumbnail image{8 + page, 10, QByteArray()};
        for (int i = 0; i < image.width * image.height * 3; ++i) image.rgb.append(static_cast<char>((i * 7 + page * 50) % 256));
        images.push_back(image);
    }
    REQUIRE(session->journal->run(std::make_unique<core::SetEmbeddedThumbnailsCommand>(std::vector<int>{0, 1, 2}, images)));
    REQUIRE(session->journal->undo());
    CHECK(deltaIsEmpty(*session));
    REQUIRE(session->journal->redo());
    REQUIRE(save(*session, "thumbnails"));
    // SAVED, the images are the file's: nothing is left to write (a sink that kept them pending would write each one
    // again, as a new stream, at every later save).
    CHECK(deltaIsEmpty(*session));
    {
        auto saved = readSaved(*session);
        for (int page = 0; page < 3; ++page) {
            INFO("page " << page);
            const CosObject thumb = saved->resolved(saved->page(page).get("Thumb"));
            REQUIRE(thumb.isStream());
            CHECK(thumb.get("Width").numberValue() == images[static_cast<std::size_t>(page)].width);
            CHECK(thumb.get("Height").numberValue() == 10);
            CHECK(thumb.get("ColorSpace").text() == "DeviceRGB");
            CHECK(tests::sha256(saved->decoded(saved->page(page).get("Thumb"))) == tests::sha256(images[static_cast<std::size_t>(page)].rgb));
        }
    }
    // Removed from every page, saved; then the removal undone after the save brings the same images back.
    REQUIRE(session->journal->run(std::make_unique<core::SetEmbeddedThumbnailsCommand>(std::vector<int>{0, 1, 2}, std::vector<core::PageThumbnail>{})));
    REQUIRE(save(*session, "thumbnails-removed"));
    {
        auto saved = readSaved(*session);
        for (int page = 0; page < 3; ++page) CHECK_FALSE(saved->page(page).has("Thumb"));
    }
    REQUIRE(session->journal->undo());
    REQUIRE(save(*session, "thumbnails-back"));
    {
        auto saved = readSaved(*session);
        for (int page = 0; page < 3; ++page)
            CHECK(tests::sha256(saved->decoded(saved->page(page).get("Thumb"))) == tests::sha256(images[static_cast<std::size_t>(page)].rgb));
    }
    // And the embedding itself undone: no page carries one, as the file came.
    REQUIRE(session->journal->undo());
    REQUIRE(save(*session, "thumbnails-none"));
    auto none = readSaved(*session);
    for (int page = 0; page < 3; ++page) CHECK_FALSE(none->page(page).has("Thumb"));
}

// ==========================================================================================
// A page's boxes, wrapper and thumbnail travel with it
// ==========================================================================================

TEST_CASE("pageedits: a cropped page moved, duplicated and deleted keeps what the crop gave it", "[pageedits][boxes]")
{
    auto session = tests::openCopy("assembly/odd-count-5.pdf");
    REQUIRE(session);
    const core::PageBoxes base = boxesOf(*session->journal->document().pages().at(1));
    core::PageBoxes cropped = base;
    cropped.crop = rect(base.media.left() + 30, base.media.bottom() + 40, base.media.right() - 50, base.media.top() - 60);
    REQUIRE(session->journal->run(std::make_unique<core::CropPagesCommand>(std::vector<int>{1}, std::vector<core::PageBoxes>{cropped})));
    REQUIRE(session->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{1}, 4)));
    REQUIRE(session->journal->run(std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{4})));
    REQUIRE(save(*session, "boxes-carried"));
    auto saved = readSaved(*session);
    REQUIRE(saved->walk.pages.size() == 6);
    CHECK(saved->boxes(4).crop == cropped.crop);   // the page, moved
    CHECK(saved->boxes(5).crop == cropped.crop);   // its copy
    CHECK(saved->boxes(0).crop == base.crop);
    // Every step undone: the file's own boxes everywhere.
    for (int step = 0; step < 3; ++step) REQUIRE(session->journal->undo());
    REQUIRE(save(*session, "boxes-carried-undone"));
    auto undone = readSaved(*session);
    REQUIRE(undone->walk.pages.size() == 5);
    CHECK(undone->boxes(1) == base);
}
