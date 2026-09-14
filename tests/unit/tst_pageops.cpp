// tst_pageops -- the page-object walk and the rotation dictionary. Stage 3 T3, guide cases A-29's
// headless half and A-30's `PageOps` half (`Stage3-Mutation-Testing-Guide.md` §5.5).
//
// WHAT THIS SUITE IS TRYING TO CATCH: a walk that disagrees with the file about WHICH object is page
// n -- trusting `/Count`, looping forever, dropping an inherited `/Rotate`, taking two levels of
// inheritance for one -- and a rotation written as anything but the page's own dictionary.
//
// WHAT IT DOES NOT COVER, named at the assertion (convention 19):
//   * **Agreement with the ENGINE.** Every expected value here comes from `corpus/PROVENANCE.md` and
//     the generators, not from what PDFium draws. Whether this walk and PDFium pick the same page for
//     every index is `tests/render/tst_pageopsengine.cpp`, which can link the engine and this suite
//     may not.
//   * **Desired state and deltas** -- `tst_mutationsink`.

#include <catch2/catch_test_macros.hpp>

#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/PageOps.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using rawpage::pdf::cos::CosObject;
using rawpage::pdf::cos::openWithRepair;
using rawpage::pdf::cos::XrefTable;
using rawpage::pdf::ops::PageEntry;
using rawpage::pdf::ops::PageOps;
using rawpage::pdf::ops::PageWalk;
using rawpage::pdf::ops::PageWalkNote;

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::filesystem::path corpus(std::string_view relative)
{
    return std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative;
}

struct Opened {
    std::unique_ptr<std::string> bytes;
    XrefTable table;
};

Opened open(std::string_view relative)
{
    auto bytes = std::make_unique<std::string>(readFile(corpus(relative)));
    auto opened = openWithRepair(*bytes);
    REQUIRE(opened);
    return Opened{std::move(bytes), std::move(opened.value().table)};
}

std::size_t notesOf(const PageWalk& walk, PageWalkNote which)
{
    std::size_t count = 0;
    for (const auto& record : walk.notes)
        count += record.note == which;
    return count;
}

} // namespace

TEST_CASE("page walk finds every page in order and ignores Count", "[ops][pages]")
{
    SECTION("a flat three-page file")
    {
        Opened file = open("ordinary/text-a4.pdf");
        auto walk = PageOps::walk(file.table);
        REQUIRE(walk);
        CHECK(walk.value().pages.size() == 3);
        CHECK(walk.value().notes.empty());
    }
    SECTION("900 pages in a 30 by 30 tree")
    {
        Opened file = open("ordinary/pages-900.pdf");
        auto walk = PageOps::walk(file.table);
        REQUIRE(walk);
        CHECK(walk.value().pages.size() == 900);
        CHECK(walk.value().notes.empty());
    }
    SECTION("a Count of 97 over three real kids")
    {
        // `/Count` is a claim (PROVENANCE: "the answer has to be the tree").
        Opened file = open("malformed/pages-count-wrong.pdf");
        auto walk = PageOps::walk(file.table);
        REQUIRE(walk);
        CHECK(walk.value().pages.size() == 3);
    }
    SECTION("four media boxes in document order")
    {
        // Order is asserted by what distinguishes the pages -- their sizes, including the box with a
        // non-zero origin -- rather than by object numbers a generator happened to choose.
        Opened file = open("ordinary/mixed-page-sizes.pdf");
        auto walk = PageOps::walk(file.table);
        REQUIRE(walk);
        REQUIRE(walk.value().pages.size() == 4);
        for (const PageEntry& page : walk.value().pages)
            REQUIRE(page.mediaBox.has_value());
        const auto width = [&](std::size_t at) {
            const auto& box = *walk.value().pages[at].mediaBox;
            return box[2] - box[0];
        };
        const auto height = [&](std::size_t at) {
            const auto& box = *walk.value().pages[at].mediaBox;
            return box[3] - box[1];
        };
        CHECK(width(0) < height(0));    // A4 portrait
        CHECK(width(1) > height(1));    // A4 landscape
        CHECK(width(2) < width(0));     // A5
        CHECK((*walk.value().pages[3].mediaBox)[0] != 0.0);   // the non-zero origin
    }
}

TEST_CASE("page walk terminates on a page tree that loops", "[ops][pages]")
{
    // `malformed/pages-loop.pdf`: an intermediate node lists its own ancestor as a kid. The walk has
    // to END, and it has to say it skipped something -- a silent skip is indistinguishable from a
    // tree that was fine.
    Opened file = open("malformed/pages-loop.pdf");
    auto walk = PageOps::walk(file.table);
    REQUIRE(walk);
    CHECK_FALSE(walk.value().pages.empty());
    CHECK(notesOf(walk.value(), PageWalkNote::NodeVisitedTwice) > 0);
}

TEST_CASE("page walk resolves own and inherited rotation", "[ops][pages]")
{
    SECTION("rotated-pages.pdf: own values 0 90 180 270")
    {
        Opened file = open("ordinary/rotated-pages.pdf");
        auto walk = PageOps::walk(file.table);
        REQUIRE(walk);
        REQUIRE(walk.value().pages.size() == 4);
        const int expected[] = {0, 90, 180, 270};
        for (std::size_t at = 0; at < 4; ++at) {
            INFO("page index " << at);
            CHECK(walk.value().pages[at].ownRotate == expected[at]);
            CHECK(walk.value().pages[at].effectiveRotate() == expected[at]);
        }
    }
    SECTION("rotate-inherited.pdf: inherited, explicit zero, inherited through two levels")
    {
        Opened file = open("ordinary/rotate-inherited.pdf");
        auto walk = PageOps::walk(file.table);
        REQUIRE(walk);
        REQUIRE(walk.value().pages.size() == 3);
        const auto& pages = walk.value().pages;
        CHECK_FALSE(pages[0].ownRotate.has_value());
        CHECK(pages[0].inheritedRotate == 90);
        CHECK(pages[0].effectiveRotate() == 90);

        // THE CONTROL for the two above: an explicit 0 wins over the inherited 90. A walk that
        // treated "present and 0" like "absent" would read 90 here and pass everything else.
        CHECK(pages[1].ownRotate == 0);
        CHECK(pages[1].inheritedRotate == 90);
        CHECK(pages[1].effectiveRotate() == 0);

        CHECK_FALSE(pages[2].ownRotate.has_value());
        CHECK(pages[2].inheritedRotate == 90);
    }
}

TEST_CASE("page walk reads every ordinary file cleanly and ends on every malformed one", "[ops][pages][corpus]")
{
    std::size_t ordinary = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus("ordinary"))) {
        if (entry.path().extension() != ".pdf")
            continue;
        INFO("file: " << entry.path().filename().string());
        Opened file = open("ordinary/" + entry.path().filename().string());
        auto walk = PageOps::walk(file.table);
        REQUIRE(walk);
        CHECK_FALSE(walk.value().pages.empty());
        CHECK(walk.value().notes.empty());
        ++ordinary;
    }
    CHECK(ordinary >= 32);

    // Malformed files: terminating is the whole requirement (A-21's rule, one layer up). Some refuse
    // to open at all, and some open with no page tree; neither may hang or crash.
    std::size_t malformed = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus("malformed"))) {
        if (entry.path().extension() != ".pdf")
            continue;
        INFO("file: " << entry.path().filename().string());
        auto bytes = std::make_unique<std::string>(readFile(entry.path()));
        auto opened = openWithRepair(*bytes);
        if (opened)
            (void)PageOps::walk(opened.value().table);
        ++malformed;
    }
    CHECK(malformed >= 20);
}

TEST_CASE("page rotation normalizes as the engine does", "[ops][pages]")
{
    CHECK(PageOps::normalizeRotate(CosObject::makeInteger(0)) == 0);
    CHECK(PageOps::normalizeRotate(CosObject::makeInteger(-90)) == 270);
    CHECK(PageOps::normalizeRotate(CosObject::makeInteger(450)) == 90);
    CHECK(PageOps::normalizeRotate(CosObject::makeInteger(-450)) == 270);
    CHECK(PageOps::normalizeRotate(CosObject::makeInteger(45)) == 0);     // truncated, as PDFium does
    CHECK(PageOps::normalizeRotate(CosObject::makeReal(180.0)) == 180);
    CHECK(PageOps::normalizeRotate(CosObject::makeName("Rotate")) == 0);
}

TEST_CASE("page rotation writes the page's own dictionary and drops a Rotate it would inherit", "[ops][pages]")
{
    Opened file = open("ordinary/rotate-inherited.pdf");
    auto walk = PageOps::walk(file.table);
    REQUIRE(walk);
    const auto& pages = walk.value().pages;

    SECTION("an inheriting page rotated gets its own Rotate")
    {
        auto written = PageOps::rotatedPage(file.table, pages[0], 180);
        REQUIRE(written);
        REQUIRE(written.value().has_value());
        CHECK(written.value()->get("Rotate").integerValue(-1) == 180);
        CHECK(written.value()->get("Type").text() == "Page");
    }
    SECTION("a page already at the value writes nothing")
    {
        auto written = PageOps::rotatedPage(file.table, pages[0], 90);
        REQUIRE(written);
        CHECK_FALSE(written.value().has_value());
    }
    SECTION("an explicit 0 set to the inherited value loses the key")
    {
        // D3-5 as refined at T3 (PageOps.h): the inherited 90 supplies the value, so the smallest
        // correct dictionary has no /Rotate at all.
        auto written = PageOps::rotatedPage(file.table, pages[1], 90);
        REQUIRE(written);
        REQUIRE(written.value().has_value());
        CHECK_FALSE(written.value()->has("Rotate"));
        CHECK(written.value()->get("Type").text() == "Page");
    }
    SECTION("every other key survives, in order")
    {
        auto original = file.table.fetch(pages[0].number);
        REQUIRE(original);
        auto written = PageOps::rotatedPage(file.table, pages[0], 270);
        REQUIRE(written);
        REQUIRE(written.value().has_value());
        std::vector<std::string> before;
        for (const auto& entry : original.value().object.entries())
            before.push_back(entry.key);
        std::vector<std::string> after;
        for (const auto& entry : written.value()->entries()) {
            if (entry.key != "Rotate")
                after.push_back(entry.key);
        }
        CHECK(after == before);
    }
    SECTION("a value that is not a quarter turn is refused")
    {
        auto written = PageOps::rotatedPage(file.table, pages[0], 45);
        REQUIRE_FALSE(written);
        CHECK(std::string(written.error().messageKey.data()) == "error.pdf.rotationNotRightAngle");
    }
}
