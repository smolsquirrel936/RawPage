// tst_compare -- core/text/DocumentComparer on synthetic pages, headless. Stage 5 T12, decision D5-17
// (`Stage5-TextSearch-Plan.md` §3): the arithmetic halves of guide cases A-97, A-98, A-100, A-101 and A-102
// (`Stage5-TextSearch-Testing-Guide.md` §5.14). The corpus halves -- the engine's text and rasters over
// `corpus/compare/`, graded against the generator's records -- are `tests/render/tst_comparecorpus`.
//
// What this does NOT cover (convention 19): no engine, so no raster -- the appearance layer is reached only
// by the corpus case -- and pages built here, glyph by glyph, rather than extracted.

#include <catch2/catch_test_macros.hpp>

#include "core/text/DocumentComparer.h"
#include "core/text/PageTextModel.h"

#include <QString>

#include <atomic>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

using rawpage::core::PdfPoint;
using rawpage::core::PdfRect;
using rawpage::core::PdfSize;
using namespace rawpage::core::text;

namespace {

using Edit = DocumentComparer::Edit;

constexpr double kSize = 10.0;
constexpr double kAdvance = 0.55 * kSize;

// ONE PAGE, drawn the way `corpus/make-compare.py` draws one: a header at the top, paragraphs wrapped at a
// measured width 13 pt a line and 8 pt apart, a footer at the bottom -- every space a drawn glyph.
struct Sheet {
    std::string header;
    std::vector<std::vector<std::string>> paragraphs;
    std::string footer;
};

void line(PageTextModel& model, const std::string& text, double x, double baseline)
{
    for (const char ch : text) {
        TextChar c;
        c.codepoint = static_cast<char32_t>(static_cast<unsigned char>(ch));
        c.fontSize = kSize;
        c.origin = PdfPoint{x, baseline};
        c.box = PdfRect{PdfPoint{x, baseline - 0.21 * kSize}, PdfSize{kAdvance, 1.11 * kSize}};
        model.append(c);
        x += kAdvance;
    }
}

ComparePage page(int index, const Sheet& sheet)
{
    PageTextModel model(index);
    if (!sheet.header.empty()) line(model, sheet.header, 72, 812);
    double y = 770;
    for (const auto& paragraph : sheet.paragraphs) {
        std::string current;
        for (const std::string& word : paragraph) {
            const std::string trial = current.empty() ? word : current + " " + word;
            if (!current.empty() && static_cast<double>(trial.size()) * kAdvance > 400.0) {
                line(model, current, 72, y);
                y -= 13;
                current = word;
            } else {
                current = trial;
            }
        }
        if (!current.empty()) {
            line(model, current, 72, y);
            y -= 13;
        }
        y -= 8;
    }
    if (!sheet.footer.empty()) line(model, sheet.footer, 72, 30);
    ComparePage out;
    out.reading = PageReading::build(std::make_shared<const PageTextModel>(std::move(model)));
    out.cropBox = PdfRect{PdfPoint{0, 0}, PdfSize{595, 842}};
    return out;
}

// Words no two paragraphs share, so nothing matches by accident.
std::vector<std::string> words(int paragraph, int count)
{
    std::vector<std::string> out;
    for (int i = 0; i < count; ++i) out.push_back("p" + std::to_string(paragraph) + "w" + std::to_string(i));
    return out;
}

// The classic distance, by dynamic programming: n + m - 2 LCS.
int classicDistance(const std::vector<int>& a, const std::vector<int>& b)
{
    std::vector<std::vector<int>> lcs(a.size() + 1, std::vector<int>(b.size() + 1, 0));
    for (std::size_t i = 1; i <= a.size(); ++i)
        for (std::size_t j = 1; j <= b.size(); ++j)
            lcs[i][j] = a[i - 1] == b[j - 1] ? lcs[i - 1][j - 1] + 1 : std::max(lcs[i - 1][j], lcs[i][j - 1]);
    return static_cast<int>(a.size() + b.size()) - 2 * lcs[a.size()][b.size()];
}

} // namespace

TEST_CASE("Myers' diff in linear space is a minimal edit script", "[core][text][compare][A-95]")
{
    std::mt19937 random(20261002);
    for (int round = 0; round < 400; ++round) {
        std::vector<int> a(random() % 30), b(random() % 30);
        const int alphabet = 1 + static_cast<int>(random() % 5);
        for (int& x : a) x = static_cast<int>(random() % alphabet);
        for (int& x : b) x = static_cast<int>(random() % alphabet);
        const std::optional<std::vector<Edit>> edits = DocumentComparer::diff(a, b, -1);
        REQUIRE(edits.has_value());
        // IT IS AN EDIT SCRIPT: applied to `a` it gives `b`.
        std::vector<int> rebuilt;
        std::size_t ia = 0, ib = 0;
        int distance = 0;
        for (const Edit& e : *edits) {
            if (e.op == Edit::Op::Equal) {
                for (int k = 0; k < e.count; ++k) {
                    REQUIRE(a[ia + k] == b[ib + k]);
                    rebuilt.push_back(a[ia + k]);
                }
                ia += e.count;
                ib += e.count;
            } else if (e.op == Edit::Op::Delete) {
                ia += e.count;
                distance += e.count;
            } else {
                for (int k = 0; k < e.count; ++k) rebuilt.push_back(b[ib + k]);
                ib += e.count;
                distance += e.count;
            }
        }
        REQUIRE(ia == a.size());
        CHECK(rebuilt == b);
        // ... AND A MINIMAL ONE.
        CHECK(distance == classicDistance(a, b));
    }
}

TEST_CASE("the diff stops at its cap and not before", "[core][text][compare][A-97]")
{
    std::mt19937 random(97);
    for (int round = 0; round < 200; ++round) {
        std::vector<int> a(1 + random() % 40), b(1 + random() % 40);
        for (int& x : a) x = static_cast<int>(random() % 6);
        for (int& x : b) x = static_cast<int>(random() % 6);
        const int distance = classicDistance(a, b);
        INFO("distance " << distance);
        // AT the distance it answers; one below, it refuses -- the control that the refusal is the cap's.
        CHECK(DocumentComparer::diff(a, b, distance).has_value());
        if (distance > 0) CHECK_FALSE(DocumentComparer::diff(a, b, distance - 1).has_value());
    }
    // THE WORK IS BOUNDED BY THE CAP: two sequences with nothing in common, explored under a small cap and a
    // lifted one.
    std::vector<int> a(2000), b(2000);
    for (int i = 0; i < 2000; ++i) {
        a[static_cast<std::size_t>(i)] = i;
        b[static_cast<std::size_t>(i)] = 10000 + i;
    }
    long long capped = 0, lifted = 0;
    CHECK_FALSE(DocumentComparer::diff(a, b, 100, &capped).has_value());
    CHECK(DocumentComparer::diff(a, b, -1, &lifted).has_value());
    INFO("explored " << capped << " capped, " << lifted << " lifted");
    CHECK(capped * 20 < lifted);
}

TEST_CASE("a moved paragraph is one move and below the move length a deletion and an insertion",
          "[core][text][compare][A-100]")
{
    const std::vector<std::vector<std::string>> paragraphs = {words(0, 30), words(1, 30), words(2, 30), words(3, 12)};
    Sheet before{"Running head", paragraphs, "footer"};
    Sheet after = before;
    after.paragraphs = {paragraphs[3], paragraphs[0], paragraphs[1], paragraphs[2]};
    const CompareDocument base{page(0, before)};
    const CompareDocument revised{page(0, after)};

    const ComparisonResult moved = DocumentComparer::compare(base, revised, CompareOptions{});
    REQUIRE(moved.differences.size() == 1);
    CHECK(moved.differences.front().kind == DifferenceKind::Move);
    CHECK(moved.differences.front().tokensBefore == 12);
    CHECK(moved.pages.front().kind == PageMatch::Kind::Modified);

    // THE CONTROL: twelve words under a move length of thirteen are a deletion and an insertion.
    CompareOptions longer;
    longer.minMoveTokens = 13;
    const ComparisonResult split = DocumentComparer::compare(base, revised, longer);
    CHECK(split.count(DifferenceKind::Move) == 0);
    CHECK(split.count(DifferenceKind::Delete) == 1);
    CHECK(split.count(DifferenceKind::Insert) == 1);
}

TEST_CASE("a moved paragraph of common words is one move", "[core][text][compare][A-100]")
{
    // PROSE FROM A SMALL VOCABULARY, as technical text is: a minimal edit script pairs the moved paragraph's
    // words with the words of the paragraphs it passed, and only anchoring the diff first keeps it one run.
    static const std::vector<std::string> kVocabulary = {"the", "page", "of", "a", "render", "cache", "tile",
                                                          "to", "and", "index", "text", "is"};
    std::mt19937 random(100);
    std::vector<std::vector<std::string>> paragraphs;
    for (int p = 0; p < 6; ++p) {
        std::vector<std::string> paragraph;
        for (int w = 0; w < 45; ++w) paragraph.push_back(kVocabulary[random() % kVocabulary.size()]);
        paragraphs.push_back(paragraph);
    }
    Sheet before{"", paragraphs, ""};
    Sheet after = before;
    after.paragraphs = {paragraphs[0], paragraphs[5], paragraphs[1], paragraphs[2], paragraphs[3], paragraphs[4]};
    const ComparisonResult result =
        DocumentComparer::compare(CompareDocument{page(0, before)}, CompareDocument{page(0, after)}, CompareOptions{});
    std::string seen;
    for (const Difference& d : result.differences) seen += std::to_string(static_cast<int>(d.kind)) + " ";
    INFO("difference kinds: " << seen);
    REQUIRE(result.differences.size() == 1);
    CHECK(result.differences.front().kind == DifferenceKind::Move);
    CHECK(result.differences.front().tokensBefore == 45);
}

TEST_CASE("a move whose run could slide is matched at the place it took", "[core][text][compare][A-100]")
{
    // THE MOVED PARAGRAPH ENDS WITH THE SAME THREE WORDS AS THE ONE IT NOW FOLLOWS, so its insertion is
    // equally well placed three words early -- the same words as the deletion, rotated.
    std::vector<std::vector<std::string>> paragraphs = {words(0, 30), words(1, 30), words(2, 30), words(3, 20)};
    for (const int p : {1, 3}) {
        auto& paragraph = paragraphs[static_cast<std::size_t>(p)];
        paragraph[paragraph.size() - 3] = "shared";
        paragraph[paragraph.size() - 2] = "closing";
        paragraph[paragraph.size() - 1] = "words.";
    }
    Sheet before{"", paragraphs, ""};
    Sheet after = before;
    after.paragraphs = {paragraphs[0], paragraphs[1], paragraphs[3], paragraphs[2]};
    const ComparisonResult result =
        DocumentComparer::compare(CompareDocument{page(0, before)}, CompareDocument{page(0, after)}, CompareOptions{});
    std::string seen;
    for (const Difference& d : result.differences)
        seen += std::to_string(static_cast<int>(d.kind)) + " '" + d.after.toStdString() + "' ";
    INFO("differences: " << seen);
    REQUIRE(result.differences.size() == 1);
    CHECK(result.differences.front().kind == DifferenceKind::Move);
    CHECK(result.differences.front().tokensBefore == 20);

    SECTION("and mirrored: the moved paragraph BEGINS like the one it now precedes")
    {
        std::vector<std::vector<std::string>> opening = {words(0, 30), words(1, 30), words(2, 30), words(3, 20)};
        for (const int p : {2, 3}) {
            auto& paragraph = opening[static_cast<std::size_t>(p)];
            paragraph[0] = "Shared";
            paragraph[1] = "opening";
            paragraph[2] = "words";
        }
        Sheet first{"", opening, ""};
        Sheet second = first;
        second.paragraphs = {opening[0], opening[1], opening[3], opening[2]};
        const ComparisonResult mirrored = DocumentComparer::compare(CompareDocument{page(0, first)},
                                                                    CompareDocument{page(0, second)}, CompareOptions{});
        std::string kinds;
        for (const Difference& d : mirrored.differences)
            kinds += std::to_string(static_cast<int>(d.kind)) + " '" + d.after.toStdString() + "' ";
        INFO("differences: " << kinds);
        REQUIRE(mirrored.differences.size() == 1);
        CHECK(mirrored.differences.front().kind == DifferenceKind::Move);
        CHECK(mirrored.differences.front().tokensBefore == 20);
    }
}

TEST_CASE("an inserted page leaves every later page matched to its counterpart", "[core][text][compare][A-101]")
{
    CompareDocument base, revised;
    for (int i = 0; i < 5; ++i) base.push_back(page(i, Sheet{"", {words(10 * i, 40), words(10 * i + 1, 40)}, ""}));
    revised = base;
    revised.insert(revised.begin() + 2, page(2, Sheet{"", {words(900, 40)}, ""}));
    const ComparisonResult result = DocumentComparer::compare(base, revised, CompareOptions{});
    REQUIRE(result.pages.size() == 6);
    for (const PageMatch& m : result.pages) {
        if (m.revised == 2) {
            CHECK(m.kind == PageMatch::Kind::Inserted);
            continue;
        }
        CHECK(m.kind == PageMatch::Kind::Identical);
        CHECK(m.revised == (m.base < 2 ? m.base : m.base + 1));
    }
    CHECK(result.differences.size() == 1);
    CHECK(result.count(DifferenceKind::PageInserted) == 1);

    SECTION("THE CONTROL: a page changed in place is paired with its old self and diffed, not inserted")
    {
        CompareDocument edited = base;
        std::vector<std::string> changed = words(20, 40);
        changed[7] = "edited";
        edited[2] = page(2, Sheet{"", {changed, words(21, 40)}, ""});
        const ComparisonResult diffed = DocumentComparer::compare(base, edited, CompareOptions{});
        std::string seen;
        for (const Difference& d : diffed.differences)
            seen += std::to_string(static_cast<int>(d.kind)) + " p" + std::to_string(d.basePage) + "->p"
                    + std::to_string(d.revisedPage) + " '" + d.before.toStdString() + "' -> '" + d.after.toStdString()
                    + "'\n";
        INFO(seen);
        CHECK(diffed.count(DifferenceKind::PageInserted) == 0);
        CHECK(diffed.count(DifferenceKind::Replace) == 1);
    }
}

TEST_CASE("the running header is found over both documents and ignored only when asked",
          "[core][text][compare][A-98]")
{
    CompareDocument base;
    for (int i = 0; i < 3; ++i)
        base.push_back(page(i, Sheet{"Quarterly notes", {words(i, 40)}, "page " + std::to_string(i + 1)}));
    // A PAGE DELETED: the revision has two pages, too few for a band of its own -- counted together, the
    // header and the footer are a band on both sides.
    CompareDocument shorter{base[0], page(1, Sheet{"Quarterly notes", {words(2, 40)}, "page 2"})};
    CompareOptions options;
    options.ignoreHeaderFooter = true;
    const ComparisonResult deleted = DocumentComparer::compare(base, shorter, options);
    INFO("differences " << deleted.differences.size());
    CHECK(deleted.differences.size() == 1);
    CHECK(deleted.count(DifferenceKind::PageDeleted) == 1);

    // THE CONTROL: the header changed is a header difference on every page when the band is compared.
    CompareDocument revisedHeader;
    for (int i = 0; i < 3; ++i)
        revisedHeader.push_back(page(i, Sheet{"Quarterly notes revised", {words(i, 40)}, "page " + std::to_string(i + 1)}));
    const ComparisonResult compared = DocumentComparer::compare(base, revisedHeader, CompareOptions{});
    CHECK(compared.differences.size() == 3);
    for (const Difference& d : compared.differences) {
        CHECK(d.kind == DifferenceKind::Insert);
        CHECK(d.region == Region::Header);
        CHECK(d.after == QStringLiteral("revised"));
    }
    CHECK(DocumentComparer::compare(base, revisedHeader, options).differences.empty());
}

TEST_CASE("the run works on its own thread and delivers on the pump", "[core][text][compare][A-102]")
{
    CompareDocument base, revised;
    for (int i = 0; i < 6; ++i) {
        base.push_back(page(i, Sheet{"", {words(i, 50)}, ""}));
        std::vector<std::string> changed = words(i, 50);
        changed[3] = "changed";
        revised.push_back(page(i, Sheet{"", {changed}, ""}));
    }
    ComparisonRun run(base, revised, CompareOptions{});
    std::vector<std::pair<int, int>> progress;
    std::optional<ComparisonResult> outcome;
    std::set<std::thread::id> handlers;
    run.setProgressHandler([&](int done, int total) {
        progress.emplace_back(done, total);
        handlers.insert(std::this_thread::get_id());
    });
    run.setFinishedHandler([&](const ComparisonResult& result) {
        outcome = result;
        handlers.insert(std::this_thread::get_id());
    });
    std::atomic<int> wakes{0};
    run.setWakeHandler([&] { ++wakes; });
    run.start();
    REQUIRE(run.waitForFinished(10000));
    CHECK(run.workerThread() != std::this_thread::get_id());
    CHECK(wakes.load() > 0);
    CHECK_FALSE(outcome.has_value());   // nothing is delivered until the pump runs
    CHECK(run.deliverReady() > 0);
    REQUIRE(outcome.has_value());
    CHECK(handlers == std::set<std::thread::id>{std::this_thread::get_id()});
    CHECK(outcome->count(DifferenceKind::Replace) == 6);
    for (std::size_t i = 1; i < progress.size(); ++i) CHECK(progress[i].first >= progress[i - 1].first);
    CHECK(progress.back().first == progress.back().second);
}
