// tst_layoutcorpus -- `core/text/LayoutAnalyzer` against the GENERATOR's labels. Stage 5 T4, decision
// D5-7, guide case **A-38** (`Stage5-TextSearch-Testing-Guide.md` §5.6), graded on the targets the
// guide's §3.2 fixed BEFORE this ran. Plus the corpus halves of A-23 (right-to-left reading order),
// A-32, A-34 and A-36 (the normalizer over real extraction) and A-39 (a tagged file, by geometry).
//
// HOW A PREDICTION IS MATCHED TO A LABEL. The labels say what the generator drew; the model says what
// PDFium extracted. `TextOracle.h`'s claim -- the same one A-17 grades extraction with -- assigns each
// shown model character to the labelled line whose region holds its origin. A labelled line is then
// the SET of model characters it claimed, a predicted line the set the analyzer grouped, and they
// match when the sets are equal: §3.2's *"a predicted line matching a labelled one when their
// character sets are equal"*. Blocks the same way, over the union of their lines.
//
// WHAT THIS DOES NOT CLAIM (convention 19, and D5-7's bound): the figures are about documents WE
// generated -- regular layouts, Helvetica and rectangle glyphs -- and say nothing about a real
// document's reading order (D5-23). They are R1's baseline, not a verdict on it.

#include <catch2/catch_test_macros.hpp>

#include "core/text/LayoutAnalyzer.h"
#include "core/text/TextNormalizer.h"
#include "tests/render/TextOracle.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using namespace rawpage::test_oracle;
using core::text::LayoutAnalyzer;
using core::text::PageLayout;

namespace {

struct LabelledPage {
    const char* name;
    int index;
};

const std::vector<LabelledPage>& labelledPages()
{
    static const std::vector<LabelledPage> kPages = {
        {"columns-two", 0},   {"columns-two", 1},    {"columns-two", 2}, {"columns-three", 0},
        {"columns-three", 1}, {"table-ruled", 0},    {"rtl-text", 0},    {"rtl-text", 1},
        {"cjk-vertical", 0},  {"cjk-readable", 0},   {"hyphenated", 0},  {"search-classes", 0},
        {"text-tounicode", 0}, {"text-tounicode", 1}, {"text-900", 0},   {"text-900", 1},
        {"text-900", 899},
    };
    return kPages;
}

using CharSet = std::vector<int>;   // sorted model indices

CharSet sorted(std::vector<int> v)
{
    std::sort(v.begin(), v.end());
    return v;
}

struct Tally {
    long long labelLines = 0, predictedLines = 0, matchedLines = 0;
    long long labelBlocks = 0, predictedBlocks = 0, matchedBlocks = 0;
    int pages = 0, columnsRight = 0;
    long long orderPairs = 0, orderKept = 0;
    int labelTables = 0, predictedTables = 0, matchedTables = 0, gridRight = 0;
    std::vector<std::string> misses;
};

double iou(const PdfRect& a, const PdfRect& b)
{
    const double w = std::min(a.right(), b.right()) - std::max(a.left(), b.left());
    const double h = std::min(a.top(), b.top()) - std::max(a.bottom(), b.bottom());
    if (w <= 0 || h <= 0) return 0.0;
    const double inter = w * h;
    return inter / (a.size.width * a.size.height + b.size.width * b.size.height - inter);
}

PdfRect rectOf(const QJsonArray& box)
{
    const double x0 = box.at(0).toDouble(), y0 = box.at(1).toDouble();
    const double x1 = box.at(2).toDouble(), y1 = box.at(3).toDouble();
    return PdfRect{PdfPoint{x0, y0}, PdfSize{x1 - x0, y1 - y0}};
}

QJsonObject labelPage(const QJsonObject& label, int index)
{
    for (const QJsonValue value : label.value(QStringLiteral("pages")).toArray())
        if (value.toObject().value(QStringLiteral("index")).toInt(-1) == index) return value.toObject();
    return {};
}

void measurePage(const LabelledPage& page, Tally& tally)
{
    const QJsonObject label = loadLabel(page.name);
    const QJsonObject pageLabel = labelPage(label, page.index);
    REQUIRE_FALSE(pageLabel.isEmpty());
    auto opened = openCorpus(std::string("ordinary/") + page.name + ".pdf");
    const PageTextModel model = extract(*opened, page.index);
    const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
    const std::string where = std::string(page.name) + " p" + std::to_string(page.index + 1);

    // --- lines ----------------------------------------------------------------------------------
    const std::vector<Line> lines = labelLines(label, page.index);
    const Claims claims = claim(model, lines);
    std::vector<CharSet> labelled;
    for (const auto& claimed : claims.perLine) {
        std::vector<int> v;
        for (const std::size_t i : claimed) v.push_back(static_cast<int>(i));
        labelled.push_back(sorted(v));
    }
    std::set<CharSet> labelledSet(labelled.begin(), labelled.end());
    std::set<CharSet> predictedLines;
    for (const auto& line : layout.lines) predictedLines.insert(sorted(line.chars));
    long long matched = 0;
    for (const CharSet& s : predictedLines)
        if (labelledSet.count(s) != 0) ++matched;
    tally.labelLines += static_cast<long long>(labelled.size());
    tally.predictedLines += static_cast<long long>(layout.lines.size());
    tally.matchedLines += matched;
    for (std::size_t l = 0; l < labelled.size(); ++l)
        if (predictedLines.count(labelled[l]) == 0)
            tally.misses.push_back(where + " line not found: '" + lines[l].text.left(40).toStdString() + "'");

    // --- blocks --------------------------------------------------------------------------------
    std::vector<CharSet> labelBlocks;
    std::vector<int> labelBlockIds;
    const QJsonArray blocks = pageLabel.value(QStringLiteral("blocks")).toArray();
    std::map<int, CharSet> blockById;
    std::map<int, std::string> roleById;
    for (const QJsonValue value : blocks) {
        const QJsonObject block = value.toObject();
        std::vector<int> v;
        for (const QJsonValue id : block.value(QStringLiteral("lines")).toArray()) {
            const std::size_t l = static_cast<std::size_t>(id.toInt());
            REQUIRE(l < labelled.size());
            v.insert(v.end(), labelled[l].begin(), labelled[l].end());
        }
        const int id = block.value(QStringLiteral("id")).toInt();
        blockById[id] = sorted(v);
        roleById[id] = block.value(QStringLiteral("role")).toString().toStdString();
        labelBlocks.push_back(blockById[id]);
    }
    std::set<CharSet> predictedBlocks;
    for (const auto& block : layout.blocks) {
        std::vector<int> v;
        for (const int l : block.lines)
            v.insert(v.end(), layout.lines[static_cast<std::size_t>(l)].chars.begin(),
                     layout.lines[static_cast<std::size_t>(l)].chars.end());
        predictedBlocks.insert(sorted(v));
    }
    long long matchedBlocks = 0;
    for (const CharSet& s : labelBlocks)
        if (predictedBlocks.count(s) != 0) ++matchedBlocks;
    for (const auto& [id, set] : blockById)
        if (predictedBlocks.count(set) == 0) tally.misses.push_back(where + " block not found: " + roleById[id]);
    tally.labelBlocks += static_cast<long long>(labelBlocks.size());
    tally.predictedBlocks += static_cast<long long>(layout.blocks.size());
    tally.matchedBlocks += matchedBlocks;

    // --- columns -------------------------------------------------------------------------------
    const int labelColumns = pageLabel.value(QStringLiteral("columns")).toInt();
    ++tally.pages;
    if (layout.columns == labelColumns) ++tally.columnsRight;
    else tally.misses.push_back(where + " columns " + std::to_string(layout.columns) + ", label " + std::to_string(labelColumns));

    // --- reading order: each labelled pair of consecutive blocks, by where their first character falls --
    std::map<int, long long> position;
    long long at = 0;
    for (const auto& item : LayoutAnalyzer::readingStream(layout))
        if (item.kind == core::text::ReadingItem::Kind::Char) position[item.charIndex] = at++;
    const QJsonArray order = pageLabel.value(QStringLiteral("order")).toArray();
    const auto firstPosition = [&](int id) {
        long long best = -1;
        for (const int c : blockById[id]) {
            const auto found = position.find(c);
            if (found != position.end() && (best < 0 || found->second < best)) best = found->second;
        }
        return best;
    };
    for (qsizetype i = 0; i + 1 < order.size(); ++i) {
        ++tally.orderPairs;
        const int a = order.at(i).toInt();
        const int b = order.at(i + 1).toInt();
        if (firstPosition(a) < firstPosition(b)) ++tally.orderKept;
        else tally.misses.push_back(where + " order: " + roleById[a] + " not before " + roleById[b]);
    }

    // --- tables ------------------------------------------------------------------------------------
    const QJsonArray tables = pageLabel.value(QStringLiteral("tables")).toArray();
    tally.labelTables += static_cast<int>(tables.size());
    tally.predictedTables += static_cast<int>(layout.tables.size());
    std::vector<bool> used(layout.tables.size(), false);
    for (const QJsonValue value : tables) {
        const QJsonObject table = value.toObject();
        const PdfRect box = rectOf(table.value(QStringLiteral("box")).toArray());
        bool hit = false;
        for (std::size_t t = 0; t < layout.tables.size(); ++t) {
            if (used[t] || iou(box, layout.tables[t].box) < 0.8) continue;
            used[t] = true;
            hit = true;
            ++tally.matchedTables;
            if (layout.tables[t].rows == table.value(QStringLiteral("rows")).toInt()
                && layout.tables[t].columns == table.value(QStringLiteral("cols")).toInt())
                ++tally.gridRight;
            else
                tally.misses.push_back(where + " table grid " + std::to_string(layout.tables[t].rows) + "x"
                                       + std::to_string(layout.tables[t].columns));
            break;
        }
        if (!hit) tally.misses.push_back(where + " table not found");
    }
    for (std::size_t t = 0; t < layout.tables.size(); ++t)
        if (!used[t]) tally.misses.push_back(where + " a table that is not one");
}

double f1(long long matched, long long predicted, long long labelled)
{
    if (predicted == 0 || labelled == 0) return 0.0;
    const double p = static_cast<double>(matched) / static_cast<double>(predicted);
    const double r = static_cast<double>(matched) / static_cast<double>(labelled);
    return p + r > 0 ? 2 * p * r / (p + r) : 0.0;
}

} // namespace

TEST_CASE("the layout analyzer meets the section 3.2 targets on the labelled corpus", "[render][layout][A-38]")
{
    Tally tally;
    for (const LabelledPage& page : labelledPages()) measurePage(page, tally);

    const double lineF1 = f1(tally.matchedLines, tally.predictedLines, tally.labelLines);
    const double blockF1 = f1(tally.matchedBlocks, tally.predictedBlocks, tally.labelBlocks);
    const double columns = static_cast<double>(tally.columnsRight) / tally.pages;
    const double order = static_cast<double>(tally.orderKept) / static_cast<double>(tally.orderPairs);
    const double tablePrecision = tally.predictedTables > 0
                                      ? static_cast<double>(tally.matchedTables) / tally.predictedTables : 0.0;
    const double tableRecall = tally.labelTables > 0 ? static_cast<double>(tally.matchedTables) / tally.labelTables : 0.0;
    const double grid = tally.matchedTables > 0 ? static_cast<double>(tally.gridRight) / tally.matchedTables : 0.0;

    for (const std::string& miss : tally.misses) std::printf("  miss: %s\n", miss.c_str());
    std::printf("A-38 over %d labelled pages (our own generated corpus):\n", tally.pages);
    std::printf("  line grouping F1 ....... %.3f  (%lld of %lld labelled, %lld predicted)   target >= 0.98\n",
                lineF1, tally.matchedLines, tally.labelLines, tally.predictedLines);
    std::printf("  block grouping F1 ...... %.3f  (%lld of %lld labelled, %lld predicted)   target >= 0.95\n",
                blockF1, tally.matchedBlocks, tally.labelBlocks, tally.predictedBlocks);
    std::printf("  columns ................ %.1f %% (%d of %d pages)   target >= 95 %%\n", 100.0 * columns,
                tally.columnsRight, tally.pages);
    std::printf("  reading order .......... %.3f  (%lld of %lld consecutive pairs)   target >= 0.95\n", order,
                tally.orderKept, tally.orderPairs);
    std::printf("  tables, detection ...... precision %.2f  recall %.2f  (%d found, %d labelled, %d predicted)"
                "   target >= 0.90 each\n",
                tablePrecision, tableRecall, tally.matchedTables, tally.labelTables, tally.predictedTables);
    std::printf("  tables, grid ........... %.2f  (%d of %d)   target >= 0.80\n", grid, tally.gridRight,
                tally.matchedTables);

    CHECK(lineF1 >= 0.98);
    CHECK(blockF1 >= 0.95);
    CHECK(columns >= 0.95);
    CHECK(order >= 0.95);
    CHECK(tablePrecision >= 0.90);
    CHECK(tableRecall >= 0.90);
    CHECK(grid >= 0.80);
}


// =============================================================================================
// The corpus halves of A-23, A-32, A-34, A-36 and A-39
// =============================================================================================

namespace {

core::text::NormalizedText normalizedPage(Opened& opened, int page, core::text::NormalizeOptions options = {})
{
    const PageTextModel model = extract(opened, page);
    const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
    return core::text::TextNormalizer(options).normalize(model, LayoutAnalyzer::readingStream(layout));
}

bool pageFinds(const core::text::NormalizedText& page, const QString& query,
               core::text::NormalizeOptions options = {})
{
    return page.text.contains(core::text::TextNormalizer(options).normalizeQuery(query));
}

} // namespace

TEST_CASE("right-to-left lines read in logical order on both emissions", "[render][layout][A-23]")
{
    // A-23's second half. T2 measured PDFium returning each Hebrew word in logical order and the
    // words in PAGE order; the analyzer reorders by geometry (UAX #9 L1-L2 over resolved levels --
    // strong-RTL runs, numbers inside them and the neutrals between, NO explicit embeddings: the
    // bound D5-6 names). Both pages -- the visual-order and the logical-order content stream -- must
    // read as the generator's logical strings.
    const QJsonObject label = loadLabel("rtl-text");
    auto opened = openCorpus("ordinary/rtl-text.pdf");
    for (const int page : {0, 1}) {
        const PageTextModel model = extract(*opened, page);
        const PageLayout layout = LayoutAnalyzer::analyze(model, nullptr);
        const QString reading = LayoutAnalyzer::readingText(model, layout);
        int hebrew = 0;
        for (const Line& line : labelLines(label, page)) {
            if (line.direction != Direction::Rtl) continue;
            ++hebrew;
            INFO("page " << page + 1 << ": " << line.text.toStdString());
            CHECK(reading.contains(line.text));
        }
        CHECK(hebrew == 4);
    }
}

TEST_CASE("the ligatures of text-tounicode are found both ways", "[render][layout][A-32]")
{
    auto opened = openCorpus("ordinary/text-tounicode.pdf");
    const core::text::NormalizedText page = normalizedPage(*opened, 0);
    CHECK(pageFinds(page, QStringLiteral("fish and chips")));          // the glyph mapped to U+FB01
    CHECK(pageFinds(page, QStringLiteral("a file of figures")));       // the one mapped to `fi`
    CHECK(pageFinds(page, QString::fromUtf8("\xEF\xAC\x81") + QStringLiteral("sh")));   // `ﬁsh` typed
    CHECK(pageFinds(page, QString::fromUtf8("\xEF\xAC\x81") + QStringLiteral("gures")));
    CHECK_FALSE(pageFinds(page, QStringLiteral("fjord")));   // the control
}

TEST_CASE("the two cafes of text-tounicode are one word to the normalizer", "[render][layout][A-34]")
{
    auto opened = openCorpus("ordinary/text-tounicode.pdf");
    const core::text::NormalizedText plain = normalizedPage(*opened, 0);
    const QString cafe = core::text::TextNormalizer().normalizeQuery(QString::fromUtf8("caf\xC3\xA9"));
    CHECK(plain.text.count(cafe) == 2);                       // precomposed, and e + U+0301
    CHECK_FALSE(pageFinds(plain, QStringLiteral("cafe au")));   // the control: accents matter by default
    core::text::NormalizeOptions folding;
    folding.stripDiacritics = true;
    const core::text::NormalizedText stripped = normalizedPage(*opened, 0, folding);
    CHECK(stripped.text.count(core::text::TextNormalizer(folding).normalizeQuery(QStringLiteral("cafe"))) == 2);
}

TEST_CASE("hyphenated joins exam-ple and keeps well-known", "[render][layout][A-36]")
{
    auto opened = openCorpus("ordinary/hyphenated.pdf");
    const core::text::NormalizedText page = normalizedPage(*opened, 0);
    CHECK(pageFinds(page, QStringLiteral("example")));
    CHECK(pageFinds(page, QStringLiteral("incremental")));
    CHECK(pageFinds(page, QStringLiteral("well-known")));
    CHECK(pageFinds(page, QStringLiteral("North-West")));      // the label's "joins": false
    CHECK_FALSE(pageFinds(page, QStringLiteral("wellknown")));   // the control: a real compound
}

TEST_CASE("a tagged document is ordered by geometry because no tag tree is passed", "[render][layout][A-39]")
{
    // D5-6, convention 19: EVERY Stage 5 caller passes a null `TagTree` -- `core/doc/TagTree` is
    // Stage 7's -- so `tagged.pdf`'s structure tree is not read and its order is the geometry's. What
    // is asserted is that it lays out, and the same way twice; that a TREE would order it better is
    // Stage 7's to show.
    auto opened = openCorpus("ordinary/tagged.pdf");
    for (int page = 0; page < opened->pageCount(); ++page) {
        const PageTextModel model = extract(*opened, page);
        const PageLayout first = LayoutAnalyzer::analyze(model, nullptr);
        const PageLayout second = LayoutAnalyzer::analyze(model, nullptr);
        CHECK(LayoutAnalyzer::readingText(model, first) == LayoutAnalyzer::readingText(model, second));
        std::size_t placed = 0;
        for (const auto& line : first.lines) placed += line.chars.size();
        CHECK(placed == model.shownCount());
    }
}
