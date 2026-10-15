// tst_migrate -- comments carried from one revision to the next, over SYNTHETIC pages. Stage 6 T13, decision D6-21
// (`Stage6-Annotations-Plan.md` §3), guide cases A-98's headless half and A-99 (`Stage6-Annotations-Testing-Guide.md`
// §5.16).
//
// Each page is drawn here the way `tests/unit/tst_compare.cpp` draws one -- every character's box and ORIGIN set by
// the case, so where a phrase moved is known exactly -- and the alignment between the two documents is given, not
// computed: which page became which is `DocumentComparer`'s (Stage 5), and this suite is about what the migrator
// makes of it.
//
// WHAT THIS DOES NOT COVER (convention 19): text the ENGINE extracted, and the comparer's own alignment of real
// pages -- `tests/render/tst_migratecorpus` grades both over `annots-report-v1.pdf` and `compare/report-v2.pdf` on
// guide §3.5's bars; the product's flow is `tests/composition/tst_exchangeproduct`.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/CommentMigrator.h"
#include "core/text/PageTextModel.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;
using core::CommentMigrator;
using core::MigrationEntry;
using core::PdfPoint;
using core::PdfRect;
using core::PdfSize;
using core::text::PageMatch;
using core::text::PageReading;
using core::text::PageTextModel;
using core::text::TextChar;

namespace {

constexpr double kSize = 10.0;
constexpr double kAdvance = 0.55 * kSize;

struct Line {
    std::string text;
    double x = 72.0;
    double baseline = 700.0;
};

std::shared_ptr<const PageReading> page(int index, const std::vector<Line>& lines)
{
    PageTextModel model(index);
    for (const Line& l : lines) {
        double x = l.x;
        for (const char ch : l.text) {
            TextChar c;
            c.codepoint = static_cast<char32_t>(static_cast<unsigned char>(ch));
            c.fontSize = kSize;
            c.origin = PdfPoint{x, l.baseline};
            c.box = PdfRect{PdfPoint{x, l.baseline - 0.21 * kSize}, PdfSize{kAdvance, 1.11 * kSize}};
            model.append(c);
            x += kAdvance;
        }
    }
    return PageReading::build(std::make_shared<const PageTextModel>(std::move(model)));
}

// The quadrilateral over `phrase` in `l`, in Acrobat's corner order, as `corpus/make-annots.py` writes one.
std::vector<double> quadOver(const Line& l, const std::string& phrase)
{
    const std::size_t at = l.text.find(phrase);
    REQUIRE(at != std::string::npos);
    const double x0 = l.x + static_cast<double>(at) * kAdvance;
    const double x1 = x0 + static_cast<double>(phrase.size()) * kAdvance;
    const double bottom = l.baseline - 0.21 * kSize;
    const double top = l.baseline + 0.9 * kSize;
    return {x0, top, x1, top, x0, bottom, x1, bottom};
}

Annotation markup(const QString& name, int pageIndex, const std::vector<double>& quads)
{
    Annotation a = core::newAnnotation(AnnotationSubtype::Highlight, pageIndex, QStringLiteral("Kofi Mensah"), 1791590400);
    a.nm = name;
    a.id = AnnotationId::ofCreated(name);
    a.quadPoints = quads;
    a.rect = *core::boundsOfPoints(quads);
    return a;
}

Annotation note(const QString& name, int pageIndex, PdfRect rect)
{
    Annotation a = core::newAnnotation(AnnotationSubtype::Text, pageIndex, QStringLiteral("Kofi Mensah"), 1791590400);
    a.nm = name;
    a.id = AnnotationId::ofCreated(name);
    a.icon = QStringLiteral("Comment");
    a.rect = rect;
    return a;
}

std::vector<PageMatch> identity(int pages)
{
    std::vector<PageMatch> out;
    for (int i = 0; i < pages; ++i) out.push_back(PageMatch{PageMatch::Kind::Identical, i, i, 1.0});
    return out;
}

const Annotation* placed(const core::MigrationResult& result, const QString& name)
{
    for (const Annotation& a : result.placed.annotations) {
        if (a.id.nm == name) return &a;
    }
    return nullptr;
}

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

} // namespace

TEST_CASE("a page reads flat in reading order and the words under a quadrilateral are its words", "[migrate][D6-21]")
{
    const Line first{"alpha beta gamma delta", 72, 700};
    const Line second{"epsilon zeta", 72, 687};
    const auto reading = page(0, {first, second});
    const CommentMigrator::FlatText flat = CommentMigrator::flatten(*reading);
    CHECK(flat.text.toStdString() == "alpha beta gamma delta epsilon zeta");
    CHECK(flat.position.size() == static_cast<std::size_t>(flat.text.size()));
    CHECK(CommentMigrator::textUnder(*reading, quadOver(first, "beta gamma")).toStdString() == "beta gamma");
    // Two quadrilaterals across the line break read as one phrase.
    std::vector<double> both = quadOver(first, "delta");
    const std::vector<double> next = quadOver(second, "epsilon");
    both.insert(both.end(), next.begin(), next.end());
    CHECK(CommentMigrator::textUnder(*reading, both).toStdString() == "delta epsilon");
    // THE CONTROL: a quadrilateral over nothing reads nothing.
    CHECK(CommentMigrator::textUnder(*reading, {300, 300, 310, 300, 300, 290, 310, 290}).isEmpty());
}

TEST_CASE("markup moves with the words it covers to the page and the place they now have", "[migrate][A-98][D6-21]")
{
    const Line was{"the quick brown fox jumps", 72, 700};
    const Line now{"the quick brown fox jumps", 90, 640};
    core::Fdf original;
    original.annotations = {markup(QStringLiteral("m1"), 0, quadOver(was, "brown fox"))};
    // The original's page 0 became the revision's page 1: a page was inserted before it.
    const std::vector<PageMatch> alignment{PageMatch{PageMatch::Kind::Inserted, -1, 0, 0.0},
                                           PageMatch{PageMatch::Kind::Identical, 0, 1, 1.0}};
    const core::MigrationResult result = CommentMigrator::migrate(
        original, {page(0, {was})}, {page(0, {Line{"an inserted page", 72, 700}}), page(1, {now})}, alignment);
    REQUIRE(result.entries.size() == 1);
    CHECK(result.entries.front().outcome == MigrationEntry::Outcome::Placed);
    CHECK(result.entries.front().placedPage == 1);
    CHECK(result.entries.front().text.toStdString() == "brown fox");
    const Annotation* moved = placed(result, QStringLiteral("m1"));
    REQUIRE(moved != nullptr);
    CHECK(moved->page == 1);
    const std::vector<double> expected = quadOver(now, "brown fox");
    REQUIRE(moved->quadPoints.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) CHECK(std::fabs(moved->quadPoints[i] - expected[i]) < 1e-9);
    CHECK(std::fabs(moved->rect.left() - (original.annotations.front().rect.left() + 18)) < 1e-9);
    CHECK(std::fabs(moved->rect.bottom() - (original.annotations.front().rect.bottom() - 60)) < 1e-9);
}

TEST_CASE("each quadrilateral of a phrase across two lines moves with its own first glyph", "[migrate][A-98][D6-21]")
{
    // The phrase "beta gamma" ends one line and starts the next; the revision moved the two lines by DIFFERENT amounts
    // (a line inserted between them), so one move for the whole phrase would put half of it beside its words.
    const Line first{"alpha beta", 72, 700};
    const Line second{"gamma delta", 72, 687};
    const Line movedFirst{"alpha beta", 72, 600};
    const Line inserted{"an inserted line", 72, 587};
    const Line movedSecond{"gamma delta", 72, 574};
    std::vector<double> quads = quadOver(first, "beta");
    const std::vector<double> more = quadOver(second, "gamma");
    quads.insert(quads.end(), more.begin(), more.end());
    core::Fdf original;
    original.annotations = {markup(QStringLiteral("across"), 0, quads)};
    const core::MigrationResult result =
        CommentMigrator::migrate(original, {page(0, {first, second})}, {page(0, {movedFirst, inserted, movedSecond})}, identity(1));
    REQUIRE(result.entries.size() == 1);
    // "beta gamma" is not on one line of the revision -- the inserted line stands between -- so it is not found.
    CHECK(result.entries.front().outcome == MigrationEntry::Outcome::Reported);

    // With no line between them -- the second now INDENTED, so the two lines moved differently -- each quadrilateral
    // lands on its own glyphs.
    const Line nearSecond{"gamma delta", 90, 587};
    const core::MigrationResult kept =
        CommentMigrator::migrate(original, {page(0, {first, second})}, {page(0, {movedFirst, nearSecond})}, identity(1));
    const Annotation* moved = placed(kept, QStringLiteral("across"));
    REQUIRE(moved != nullptr);
    std::vector<double> expected = quadOver(movedFirst, "beta");
    const std::vector<double> expectedMore = quadOver(nearSecond, "gamma");
    expected.insert(expected.end(), expectedMore.begin(), expectedMore.end());
    REQUIRE(moved->quadPoints.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) CHECK(std::fabs(moved->quadPoints[i] - expected[i]) < 1e-9);
}

TEST_CASE("markup on words the revision deleted is reported and nothing of it placed", "[migrate][A-99][D6-21]")
{
    const Line was{"schedule raster tile percentile table", 72, 700};
    const Line now{"schedule raster throughput percentile table", 72, 700};
    core::Fdf original;
    Annotation struck = markup(QStringLiteral("gone"), 0, quadOver(was, "raster tile percentile"));
    struck.subtype = AnnotationSubtype::StrikeOut;
    Annotation reply = core::newAnnotation(AnnotationSubtype::Text, 0, QStringLiteral("Ana"), 1791590400);
    reply.nm = QStringLiteral("gone-reply");
    reply.id = AnnotationId::ofCreated(reply.nm);
    reply.rect = PdfRect{PdfPoint{500, 690}, PdfSize{20, 20}};
    reply.inReplyTo = struck.id;
    original.annotations = {struck, reply};
    const core::MigrationResult result = CommentMigrator::migrate(original, {page(0, {was})}, {page(0, {now})}, identity(1));
    REQUIRE(result.entries.size() == 1);   // the reply follows its comment: one top-level entry
    CHECK(result.entries.front().outcome == MigrationEntry::Outcome::Reported);
    REQUIRE(result.entries.front().why);
    CHECK(keyOf(*result.entries.front().why) == "error.migrate.textGone");
    CHECK(result.entries.front().text.toStdString() == "raster tile percentile");
    CHECK(result.placed.annotations.empty());
}

TEST_CASE("a phrase the revision holds twice goes to the occurrence nearest where it was and never into a longer word",
          "[migrate][A-98][D6-21]")
{
    const Line was{"cat sat on the mat", 72, 600};
    const Line elsewhere{"a cat far away", 72, 760};
    const Line near{"cat sat on the mat", 72, 590};
    core::Fdf original;
    original.annotations = {markup(QStringLiteral("cat"), 0, quadOver(was, "cat"))};
    SECTION("the nearer of two whole words")
    {
        const core::MigrationResult result =
            CommentMigrator::migrate(original, {page(0, {was})}, {page(0, {elsewhere, near})}, identity(1));
        const Annotation* moved = placed(result, QStringLiteral("cat"));
        REQUIRE(moved != nullptr);
        CHECK(std::fabs(moved->quadPoints[1] - quadOver(near, "cat")[1]) < 1e-9);
    }
    SECTION("a whole word is not moved onto part of a longer one")
    {
        const Line longer{"concatenate sat on the mat", 72, 600};
        const core::MigrationResult result =
            CommentMigrator::migrate(original, {page(0, {was})}, {page(0, {longer, elsewhere})}, identity(1));
        const Annotation* moved = placed(result, QStringLiteral("cat"));
        REQUIRE(moved != nullptr);
        CHECK(std::fabs(moved->quadPoints[1] - quadOver(elsewhere, "cat")[1]) < 1e-9);
    }
    // Each edge on its own: in "concatenate" BOTH sides are letters, so either check alone refuses it -- a word that
    // ENDS with the phrase is refused only by the leading check, one that STARTS with it only by the trailing one.
    SECTION("nor onto the end of one")
    {
        const Line ending{"bobcat sat on the mat", 72, 600};
        const core::MigrationResult result =
            CommentMigrator::migrate(original, {page(0, {was})}, {page(0, {ending, elsewhere})}, identity(1));
        const Annotation* moved = placed(result, QStringLiteral("cat"));
        REQUIRE(moved != nullptr);
        CHECK(std::fabs(moved->quadPoints[1] - quadOver(elsewhere, "cat")[1]) < 1e-9);
    }
    SECTION("nor onto the start of one")
    {
        const Line starting{"catalog sat on the mat", 72, 600};
        const core::MigrationResult result =
            CommentMigrator::migrate(original, {page(0, {was})}, {page(0, {starting, elsewhere})}, identity(1));
        const Annotation* moved = placed(result, QStringLiteral("cat"));
        REQUIRE(moved != nullptr);
        CHECK(std::fabs(moved->quadPoints[1] - quadOver(elsewhere, "cat")[1]) < 1e-9);
    }
}

TEST_CASE("a note beside a table keeps its place by the nearest line of prose and not by a cell", "[migrate][A-98][D6-21]")
{
    // An unruled table (`tst_layout`'s, found by alignment) whose rows sit right beside the note, and one paragraph line
    // further away. The revision moved the paragraph and not the table: the note goes with the PARAGRAPH, because a
    // cell is not a line of prose a note is written beside.
    std::vector<Line> was;
    std::vector<Line> now;
    for (int r = 0; r < 4; ++r) {
        const double y = 600.0 - r * 15.0;
        const std::string key = std::string("key") + static_cast<char>('a' + r);
        for (std::vector<Line>* lines : {&was, &now}) {
            lines->push_back({key, 50, y});
            lines->push_back({"on", 150, y});
            lines->push_back({"Page", 220, y});
        }
    }
    was.push_back({"a paragraph of running prose", 50, 480});
    now.push_back({"a paragraph of running prose", 50, 460});
    const std::shared_ptr<const PageReading> base = page(0, was);
    REQUIRE(base->layout().tables.size() == 1);   // the precondition: the rows ARE a table
    core::Fdf original;
    original.annotations = {note(QStringLiteral("t1"), 0, PdfRect{PdfPoint{260, 590}, PdfSize{20, 20}})};
    const core::MigrationResult result = CommentMigrator::migrate(original, {base}, {page(0, now)}, identity(1));
    const Annotation* moved = placed(result, QStringLiteral("t1"));
    REQUIRE(moved != nullptr);
    CHECK(std::fabs(moved->rect.bottom() - 570.0) < 1e-9);
}

TEST_CASE("a note keeps its place beside the nearest line and its reply and pop-up go with it", "[migrate][A-98][D6-21]")
{
    const std::vector<Line> was{{"Report viewport render policy", 72, 567}, {"a line much further down", 72, 400}};
    const std::vector<Line> now{{"a new first line", 72, 600}, {"Report viewport render policy", 72, 554}, {"a line much further down", 72, 400}};
    core::Fdf original;
    Annotation n = note(QStringLiteral("n1"), 0, PdfRect{PdfPoint{220, 585}, PdfSize{20, 20}});
    Annotation popup;
    popup.subtype = AnnotationSubtype::Popup;
    popup.page = 0;
    popup.nm = core::popupNameFor(n.nm);
    popup.id = AnnotationId::ofCreated(popup.nm);
    popup.parent = n.id;
    popup.rect = PdfRect{PdfPoint{300, 500}, PdfSize{180, 100}};
    n.popup = popup.id;
    Annotation reply = note(QStringLiteral("n1-reply"), 0, n.rect);
    reply.inReplyTo = n.id;
    original.annotations = {n, popup, reply};
    const core::MigrationResult result = CommentMigrator::migrate(original, {page(0, was)}, {page(0, now)}, identity(1));
    REQUIRE(result.entries.size() == 1);
    CHECK(result.entries.front().outcome == MigrationEntry::Outcome::Placed);
    const Annotation* moved = placed(result, QStringLiteral("n1"));
    REQUIRE(moved != nullptr);
    // The label's arithmetic: the line moved from 567 to 554, so the note from 585 to 572.
    CHECK(std::fabs(moved->rect.bottom() - 572.0) < 1e-9);
    CHECK(std::fabs(moved->rect.left() - 220.0) < 1e-9);
    const Annotation* movedReply = placed(result, QStringLiteral("n1-reply"));
    REQUIRE(movedReply != nullptr);
    CHECK(std::fabs(movedReply->rect.bottom() - 572.0) < 1e-9);
    const Annotation* movedPopup = placed(result, popup.nm);
    REQUIRE(movedPopup != nullptr);
    CHECK(std::fabs(movedPopup->rect.bottom() - 487.0) < 1e-9);
}

TEST_CASE("a note whose line changed and a comment on a deleted page are reported and never placed", "[migrate][A-99][D6-21]")
{
    const std::vector<Line> was{{"Report viewport render policy", 72, 567}};
    core::Fdf original;
    original.annotations = {note(QStringLiteral("n1"), 0, PdfRect{PdfPoint{220, 585}, PdfSize{20, 20}}),
                            markup(QStringLiteral("m2"), 1, quadOver(Line{"on the second page", 72, 700}, "second page"))};
    const std::vector<std::shared_ptr<const PageReading>> base{page(0, was), page(1, {Line{"on the second page", 72, 700}})};
    const std::vector<std::shared_ptr<const PageReading>> revised{page(0, {Line{"Report viewport render throughput", 72, 567}})};
    const std::vector<PageMatch> alignment{PageMatch{PageMatch::Kind::Modified, 0, 0, 0.8}, PageMatch{PageMatch::Kind::Deleted, 1, -1, 0.0}};
    const core::MigrationResult result = CommentMigrator::migrate(original, base, revised, alignment);
    REQUIRE(result.entries.size() == 2);
    CHECK(keyOf(*result.entries[0].why) == "error.migrate.anchorGone");
    CHECK(keyOf(*result.entries[1].why) == "error.migrate.pageGone");
    CHECK(result.count(MigrationEntry::Outcome::Reported) == 2);
    CHECK(result.placed.annotations.empty());
}
