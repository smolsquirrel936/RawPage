// tst_splitplan -- Stage 7 T7 (`Stage7-Assembly-Plan.md` §6, D7-30): the six split rules and the page-range syntax,
// pure -- guide case A-49's planning half (`Stage7-Assembly-Testing-Guide.md` §5.9).
//
// Every rule is graded twice: against hand values, and against `corpus/labels/assembly-split-blanks.json` -- the
// generator's own record of which pages are blank by the content box and which start a part -- read here as the
// facts the product would gather. WHAT IT DOES NOT COVER (convention 19): GATHERING those facts -- the engine's content
// box, the search engine's match, the assembler's weight -- and writing the files; that is
// `tests/render/tst_assemblerengine.cpp`'s, through the shipped backend and `core::SplitRunner`.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Outline.h"
#include "core/doc/PageRanges.h"
#include "core/doc/SplitPlan.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace rawpage;
using core::SplitPart;

namespace {

std::vector<std::vector<int>> pagesOf(const std::vector<SplitPart>& parts)
{
    std::vector<std::vector<int>> out;
    for (const SplitPart& part : parts) out.push_back(part.pages);
    return out;
}

QJsonObject label(const char* name)
{
    QFile file(QString::fromStdString((std::filesystem::path(RAWPAGE_CORPUS_DIR) / "labels" / name).string()));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
}

core::OutlineItem item(int page, std::vector<core::OutlineItem> children = {})
{
    core::OutlineItem out;
    out.title = QStringLiteral("item %1").arg(page);
    out.destination.pageIndex = page;
    out.children = std::move(children);
    return out;
}

// A refusal's key, or "(success)" -- so a rule that wrongly succeeds fails its CHECK instead of reading `error()` of a
// success, which is undefined.
template <typename T>
std::string keyOf(const core::Result<T>& result)
{
    return result ? std::string("(success)") : std::string(result.error().messageKey.data(), static_cast<std::size_t>(result.error().messageKey.size()));
}

} // namespace

TEST_CASE("pages a range as typed is the set it names", "[splitplan][pages]")
{
    const auto parsed = [](const char* text, int count) { return core::parsePageRanges(QString::fromUtf8(text), count); };
    CHECK(parsed("1-3, 5", 10).value() == std::vector<int>{0, 1, 2, 4});
    CHECK(parsed(" 5 ,1-2,2 ", 10).value() == std::vector<int>{0, 1, 4});   // each page once, ascending
    CHECK(parsed("8-", 10).value() == std::vector<int>{7, 8, 9});
    CHECK(parsed("-2", 10).value() == std::vector<int>{0, 1});
    CHECK(parsed("3\xE2\x80\x93" "4", 10).value() == std::vector<int>{2, 3});   // an en dash
    CHECK(parsed("10", 10).value() == std::vector<int>{9});
    for (const char* bad : {"", " ", "0", "11", "7-3", "1-2-3", "a", "1,,2", "-", "2.5"}) {
        INFO(bad);
        const auto refused = parsed(bad, 10);
        REQUIRE_FALSE(refused);
        CHECK(keyOf(refused) == std::string("error.pages.badRange"));
    }
    CHECK(core::formatPageRanges({0, 1, 2, 4, 7, 8}) == QStringLiteral("1-3, 5, 8-9"));
    CHECK(core::parsePageRanges(core::formatPageRanges({0, 2, 3, 9}), 10).value() == std::vector<int>{0, 2, 3, 9});
}

TEST_CASE("splitplan every N pages", "[splitplan][A-49]")
{
    CHECK(pagesOf(core::split::everyN(7, 3).value()) == std::vector<std::vector<int>>{{0, 1, 2}, {3, 4, 5}, {6}});
    CHECK(pagesOf(core::split::everyN(4, 1).value()) == std::vector<std::vector<int>>{{0}, {1}, {2}, {3}});
    CHECK(core::split::everyN(900, 100).value().size() == 9);
    CHECK(keyOf(core::split::everyN(5, 5)) == std::string("error.split.noBoundary"));   // one file: a copy
    CHECK(keyOf(core::split::everyN(5, 0)) == std::string("error.split.badCount"));
}

TEST_CASE("splitplan at bookmarks: each level splits at its own items and every shallower one", "[splitplan][A-49]")
{
    core::Outline outline;
    outline.roots() = {item(2, {item(4), item(6, {item(7)})}), item(9), item(1)};
    core::OutlineItem unresolved = item(-1);
    outline.roots().push_back(unresolved);
    // Top level: 1, 2, 9 -- the front matter (page 0) a file of its own; an unresolved item splits nothing.
    CHECK(pagesOf(core::split::atBookmarks(12, outline, 1).value())
          == std::vector<std::vector<int>>{{0}, {1}, {2, 3, 4, 5, 6, 7, 8}, {9, 10, 11}});
    CHECK(core::split::bookmarkStarts(outline, 2, 12) == std::vector<int>{1, 2, 4, 6, 9});
    CHECK(core::split::bookmarkStarts(outline, 3, 12) == std::vector<int>{1, 2, 4, 6, 7, 9});
    core::Outline empty;
    CHECK(keyOf(core::split::atBookmarks(12, empty, 1)) == std::string("error.split.noBoundary"));
    core::Outline onlyFirst;
    onlyFirst.roots() = {item(0)};
    CHECK(keyOf(core::split::atBookmarks(12, onlyFirst, 1)) == std::string("error.split.noBoundary"));
}

TEST_CASE("splitplan at blank pages: a blank page separates and is not written", "[splitplan][A-49]")
{
    CHECK(pagesOf(core::split::atBlankPages({false, true, false, false, true, true, false}).value())
          == std::vector<std::vector<int>>{{0}, {2, 3}, {6}});
    CHECK(keyOf(core::split::atBlankPages({true, true})) == std::string("error.split.allBlank"));
    CHECK(keyOf(core::split::atBlankPages({false, false})) == std::string("error.split.noBoundary"));
    // Blank pages only at the ends separate nothing.
    CHECK(keyOf(core::split::atBlankPages({true, false, false, true})) == std::string("error.split.noBoundary"));
}

TEST_CASE("splitplan the labelled split-blanks file: by blank pages, by its headings, by its bookmarks", "[splitplan][A-49]")
{
    const QJsonObject file = label("assembly-split-blanks.json");
    const QJsonArray pages = file.value(QStringLiteral("pages")).toArray();
    REQUIRE(pages.size() == 9);
    // What each page's identity is, as DRAWN -- the white page draws one, invisible to the eye.
    std::vector<QString> identity;
    std::vector<bool> blankByContentBox;
    std::map<int, int> pageOfObject;
    for (const QJsonValue& value : pages) {
        const QJsonObject page = value.toObject();
        identity.push_back(page.value(QStringLiteral("drawnIdentity")).toString());
        blankByContentBox.push_back(page.value(QStringLiteral("contentBoxEmpty")).toBool());
        pageOfObject[page.value(QStringLiteral("object")).toInt()] = page.value(QStringLiteral("index")).toInt();
    }
    const auto ends = [&](const std::vector<SplitPart>& parts) {
        std::vector<std::pair<QString, QString>> out;
        for (const SplitPart& part : parts) out.emplace_back(identity[part.pages.front()], identity[part.pages.back()]);
        return out;
    };

    // BLANK: the two pages with no object separate; the white-ink page is NOT blank to the split and opens part three.
    const auto blank = core::split::atBlankPages(blankByContentBox);
    REQUIRE(blank);
    CHECK(ends(blank.value()) == std::vector<std::pair<QString, QString>>{
                                     {QStringLiteral("SB-0001"), QStringLiteral("SB-0002")},
                                     {QStringLiteral("SB-0004"), QStringLiteral("SB-0005")},
                                     {QStringLiteral("SB-0007"), QStringLiteral("SB-0009")}});

    // HEADINGS: the label's own part starts, as a matched page each.
    std::vector<int> starts;
    for (const QJsonValue& value : file.value(QStringLiteral("partStarts")).toArray()) starts.push_back(value.toInt());
    const auto headed = core::split::atStarts(9, starts);
    REQUIRE(headed);
    CHECK(pagesOf(headed.value()) == std::vector<std::vector<int>>{{0, 1, 2}, {3, 4, 5, 6}, {7, 8}});

    // BOOKMARKS: the label's outline, each item's target page by its object.
    core::Outline outline;
    for (const QJsonValue& value : file.value(QStringLiteral("outline")).toArray())
        outline.roots().push_back(item(pageOfObject.at(value.toObject().value(QStringLiteral("targetPage")).toInt())));
    CHECK(pagesOf(core::split::atBookmarks(9, outline, 1).value()) == pagesOf(headed.value()));
}

TEST_CASE("splitplan by size: the fewest files under the bound, a heavy page alone", "[splitplan][A-49]")
{
    // A file weighs 1,000 bytes of frame and shared font, and each page its own weight; page 6 is heavy.
    const std::vector<qint64> own{100, 100, 100, 100, 100, 100, 5000, 100, 100, 100};
    int weighings = 0;
    const core::split::Weigher weigh = [&](const std::vector<int>& pages) -> core::Result<qint64> {
        ++weighings;
        qint64 total = 1000;
        for (const int page : pages) total += own[static_cast<std::size_t>(page)];
        return total;
    };
    const auto parts = core::split::bySize(10, 1350, weigh);
    REQUIRE(parts);
    CHECK(pagesOf(parts.value()) == std::vector<std::vector<int>>{{0, 1, 2}, {3, 4, 5}, {6}, {7, 8, 9}});
    for (const SplitPart& part : parts.value()) {
        qint64 total = 1000;
        for (const int page : part.pages) total += own[static_cast<std::size_t>(page)];
        CHECK((total <= 1350) == !part.overBound);   // every file at or under the bound but the one heavy page
    }
    CHECK(parts.value()[2].overBound);
    CHECK(weighings < 40);   // doubling then halving, never one weighing per run length

    CHECK(keyOf(core::split::bySize(10, 1'000'000, weigh)) == std::string("error.split.noBoundary"));
    CHECK(keyOf(core::split::bySize(10, 0, weigh)) == std::string("error.split.badSize"));
    // A weighing that fails is the split's failure, by its own key.
    const core::split::Weigher refusing = [](const std::vector<int>& pages) -> core::Result<qint64> {
        if (pages.size() > 2) return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.assembly.verifyFailed"), QString()};
        return 10;
    };
    CHECK(keyOf(core::split::bySize(10, 100, refusing)) == std::string("error.assembly.verifyFailed"));
    // Every page alone, when even two do not fit.
    CHECK(core::split::bySize(4, 1150, weigh).value().size() == 4);
}

TEST_CASE("splitplan file names are numbered to sort", "[splitplan]")
{
    const std::vector<QString> names = core::split::numberedNames(QStringLiteral("%1 part %2.pdf"), QStringLiteral("report"), {1, 2, 10}, 12);
    CHECK(names == std::vector<QString>{QStringLiteral("report part 01.pdf"), QStringLiteral("report part 02.pdf"),
                                        QStringLiteral("report part 10.pdf")});
    // A stem a file name cannot hold is made one.
    const std::vector<QString> safe = core::split::numberedNames(QStringLiteral("%1 part %2.pdf"), QStringLiteral("a/b:c"), {3}, 3);
    REQUIRE(safe.size() == 1);
    CHECK_FALSE(safe.front().contains(QLatin1Char('/')));
    CHECK_FALSE(safe.front().contains(QLatin1Char(':')));
}
