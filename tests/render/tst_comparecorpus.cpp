// tst_comparecorpus -- core/text/DocumentComparer over corpus/compare/, graded against each pair's
// expected.json. Stage 5 T12, decision D5-17 (`Stage5-TextSearch-Plan.md` §3), guide cases A-95 … A-102
// (`Stage5-TextSearch-Testing-Guide.md` §5.14) and the verdict of its §3.4.
//
// THE INPUTS ARE THE ENGINE'S: every page extracted by the shipped backend's `ITextExtractor` and drawn at
// 72 dpi by its `IPageRasterizer` -- the same two things the engine thread hands the comparer in the
// product. THE ORACLE IS THE GENERATOR'S: `corpus/make-compare.py` writes each pair's changes as records it
// re-derives the revised document from (`--self-test`, A-15), and every assertion here is against those
// records -- and against nothing the comparer said that the records do not name: at *text only*, a
// difference no record accounts for fails the pair (§3.4's "no difference outside the labelled ones").
//
// WHAT THIS DOES NOT COVER (convention 19): the inputs are gathered here, on the test's thread, before the
// comparison -- the product's gathering through `DocumentIndex` and the scheduler is T13's surface; and
// every pair is ours, in base-14 Helvetica, one kind of change each (`corpus/make-compare.py`'s header).

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Page.h"
#include "core/doc/Revision.h"
#include "core/render/RenderRequest.h"
#include "core/text/DocumentComparer.h"
#include "tests/bench/HeapCensus.h"
#include "tests/render/TextOracle.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace rawpage;
using namespace rawpage::test_oracle;
using core::text::CompareDocument;
using core::text::CompareOptions;
using core::text::ComparePage;
using core::text::CompareSensitivity;
using core::text::CompareSide;
using core::text::ComparisonResult;
using core::text::ComparisonRun;
using core::text::Difference;
using core::text::DifferenceKind;
using core::text::DocumentComparer;
using core::text::PageMatch;
using core::text::PageReading;
using core::text::Region;

namespace {

// `corpus/make-compare.py`'s image: IMAGE_W x IMAGE_H points.
constexpr double kImageWidth = 160.0;
constexpr double kImageHeight = 90.0;

// ONE DOCUMENT AS THE ENGINE GIVES IT: its pages read, and a way to draw each at 72 dpi.
struct Side {
    std::unique_ptr<Opened> opened;
    CompareDocument pages;

    std::optional<core::RasterImage> raster(int index) const
    {
        const core::Page& page = opened->page(index);
        const core::PdfSize display = page.displaySize();
        core::render::RenderRequest request;
        request.document = opened->handle;
        request.pageIndex = index;
        request.deviceScale = 1.0;
        request.revision = core::Revision::initial();
        request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(display.width)),
                                              static_cast<int>(std::ceil(display.height))};
        core::IPageRasterizer* rasterizer = opened->backend.rasterizer(opened->handle);
        if (rasterizer == nullptr) return std::nullopt;
        auto image = rasterizer->raster(request);
        if (!image) return std::nullopt;
        return std::move(image).value();
    }
};

Side load(const std::string& relative)
{
    Side side;
    side.opened = openCorpus(relative);
    for (int i = 0; i < side.opened->pageCount(); ++i) {
        ComparePage page;
        page.reading = PageReading::build(std::make_shared<const core::text::PageTextModel>(extract(*side.opened, i)));
        page.cropBox = side.opened->page(i).cropBox();
        page.rotation = side.opened->page(i).rotation();
        side.pages.push_back(std::move(page));
    }
    return side;
}

struct Loaded {
    std::string name;
    QJsonObject record;
    Side base;
    Side revised;

    core::text::RasterProvider rasters() const
    {
        return [this](CompareSide side, int page) {
            return side == CompareSide::Base ? base.raster(page) : revised.raster(page);
        };
    }
};

std::unique_ptr<Loaded> loadPair(const std::string& name)
{
    auto pair = std::make_unique<Loaded>();
    pair->name = name;
    QFile file(corpusPath("compare/" + name + ".expected.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    pair->record = QJsonDocument::fromJson(file.readAll()).object();
    pair->base = load(pair->record.value(QStringLiteral("base")).toString().toStdString());
    pair->revised = load(pair->record.value(QStringLiteral("revised")).toString().toStdString());
    return pair;
}

ComparisonResult run(const Loaded& pair, const CompareOptions& options)
{
    return DocumentComparer::compare(pair.base.pages, pair.revised.pages, options, pair.rasters());
}

const char* kindName(DifferenceKind kind)
{
    switch (kind) {
    case DifferenceKind::Insert: return "insert";
    case DifferenceKind::Delete: return "delete";
    case DifferenceKind::Replace: return "replace";
    case DifferenceKind::Move: return "move";
    case DifferenceKind::Whitespace: return "whitespace";
    case DifferenceKind::Layout: return "layout";
    case DifferenceKind::Appearance: return "appearance";
    case DifferenceKind::PageInserted: return "page-inserted";
    case DifferenceKind::PageDeleted: return "page-deleted";
    case DifferenceKind::PageMoved: return "page-moved";
    case DifferenceKind::PageReplaced: return "page-replaced";
    }
    return "?";
}

std::string describe(const Difference& d)
{
    const char* region = d.region == Region::Header ? "header" : d.region == Region::Footer ? "footer" : "body";
    return std::string(kindName(d.kind)) + " [" + region + "] p" + std::to_string(d.basePage) + "->p"
           + std::to_string(d.revisedPage) + " '" + d.before.left(50).toStdString() + "' -> '"
           + d.after.left(50).toStdString() + "'";
}

std::string describe(const ComparisonResult& result)
{
    std::string out;
    for (const Difference& d : result.differences) out += "  " + describe(d) + "\n";
    return out;
}

QString joined(const QJsonValue& words)
{
    if (words.isString()) return words.toString();
    QStringList out;
    for (const QJsonValue w : words.toArray()) out << w.toString();
    return out.join(QLatin1Char(' '));
}

// THE GENERATOR'S RECORDS, applied in order to a list of where each base page is: an edit's `page` is in the
// numbering of the moment it was made (`make-compare.py`'s `apply()` runs them in order).
struct Pages {
    std::vector<int> current;   // base page at each position; -1 for an inserted page

    int basePage(int position) const { return current[static_cast<std::size_t>(position)]; }
    int revisedOf(int base) const
    {
        const auto it = std::find(current.begin(), current.end(), base);
        return it == current.end() ? -1 : static_cast<int>(it - current.begin());
    }
};

// GRADES ONE RESULT AGAINST ITS RECORDS. Each record consumes the differences it accounts for; what is left
// is returned, and the caller says which kinds may be left (none at text only).
std::vector<const Difference*> grade(const Loaded& pair, const ComparisonResult& result, bool appearance)
{
    std::vector<bool> used(result.differences.size(), false);
    const auto take = [&](auto&& match) -> int {
        int taken = 0;
        for (std::size_t i = 0; i < result.differences.size(); ++i) {
            if (used[i] || !match(result.differences[i])) continue;
            used[i] = true;
            ++taken;
        }
        return taken;
    };
    Pages pages;
    for (int i = 0; i < static_cast<int>(pair.base.pages.size()); ++i) pages.current.push_back(i);
    const QJsonArray changes = pair.record.value(QStringLiteral("changes")).toArray();
    // Positions first: an edit's base page is fixed when it is read, its revised page only at the end.
    struct Edit {
        QJsonObject change;
        int base = -1;
    };
    std::vector<Edit> edits;
    for (const QJsonValue value : changes) {
        const QJsonObject change = value.toObject();
        const QString kind = change.value(QStringLiteral("kind")).toString();
        Edit edit{change, -1};
        if (change.contains(QStringLiteral("page"))) edit.base = pages.basePage(change.value(QStringLiteral("page")).toInt());
        if (kind == QStringLiteral("page-insert")) {
            pages.current.insert(pages.current.begin() + change.value(QStringLiteral("at")).toInt(), -1);
        } else if (kind == QStringLiteral("page-delete")) {
            edit.base = pages.basePage(change.value(QStringLiteral("at")).toInt());
            pages.current.erase(pages.current.begin() + change.value(QStringLiteral("at")).toInt());
        } else if (kind == QStringLiteral("page-order")) {
            std::vector<int> reordered;
            for (const QJsonValue at : change.value(QStringLiteral("order")).toArray())
                reordered.push_back(pages.current[static_cast<std::size_t>(at.toInt())]);
            pages.current = reordered;
        }
        edits.push_back(edit);
    }
    REQUIRE(static_cast<int>(pages.current.size()) == static_cast<int>(pair.revised.pages.size()));

    for (const Edit& edit : edits) {
        const QJsonObject& change = edit.change;
        const QString kind = change.value(QStringLiteral("kind")).toString();
        const int base = edit.base;
        const int revised = base >= 0 ? pages.revisedOf(base) : -1;
        INFO(pair.name << ": record " << kind.toStdString() << " on base page " << base << " (revised " << revised
                       << ")\n" << describe(result));
        const QString before = joined(change.value(QStringLiteral("before")));
        const QString after = joined(change.value(QStringLiteral("after")));
        const auto onPair = [&](const Difference& d) { return d.basePage == base && d.revisedPage == revised; };
        if (kind == QStringLiteral("insert")) {
            CHECK(take([&](const Difference& d) {
                      return onPair(d) && d.kind == DifferenceKind::Insert && d.region == Region::Body && d.after == after;
                  }) == 1);
        } else if (kind == QStringLiteral("delete")) {
            CHECK(take([&](const Difference& d) {
                      return onPair(d) && d.kind == DifferenceKind::Delete && d.region == Region::Body && d.before == before;
                  }) == 1);
        } else if (kind == QStringLiteral("replace") || kind == QStringLiteral("table-cell")) {
            CHECK(take([&](const Difference& d) {
                      return onPair(d) && d.kind == DifferenceKind::Replace && d.region == Region::Body
                             && d.before == before && d.after == after;
                  }) == 1);
        } else if (kind == QStringLiteral("move")) {
            const QString words = joined(change.value(QStringLiteral("words")));
            CHECK(take([&](const Difference& d) {
                      return onPair(d) && d.kind == DifferenceKind::Move && d.before == words && d.after == words;
                  }) == 1);
        } else if (kind == QStringLiteral("whitespace")) {
            // THE WORD AFTER EACH DOUBLED SPACE, in the paragraph the record names.
            int expected = 0;
            for (const QJsonValue at : change.value(QStringLiteral("doubleAfter")).toArray()) {
                (void)at;
                ++expected;
            }
            CHECK(take([&](const Difference& d) { return onPair(d) && d.kind == DifferenceKind::Whitespace; }) == expected);
        } else if (kind == QStringLiteral("image-move")) {
            if (!appearance) continue;   // text only does not see it (A-99)
            // THE OLD AND THE NEW PLACE: every appearance difference on the page, whose union is the image's
            // width plus the distance it moved, and its height.
            double left = 1e9, right = -1e9, bottom = 1e9, top = -1e9;
            const int taken = take([&](const Difference& d) {
                if (!(onPair(d) && d.kind == DifferenceKind::Appearance)) return false;
                for (const core::PdfRect& r : d.revisedRects) {
                    left = std::min(left, r.left());
                    right = std::max(right, r.right());
                    bottom = std::min(bottom, r.bottom());
                    top = std::max(top, r.top());
                }
                return true;
            });
            const double moved = std::abs(change.value(QStringLiteral("after")).toDouble() - change.value(QStringLiteral("before")).toDouble());
            INFO("appearance union " << left << ".." << right << " x " << bottom << ".." << top);
            CHECK(taken >= 1);
            CHECK(std::abs((right - left) - (kImageWidth + moved)) < 8.0);
            CHECK(std::abs((top - bottom) - kImageHeight) < 8.0);
        } else if (kind == QStringLiteral("page-insert")) {
            const int at = static_cast<int>(std::find(pages.current.begin(), pages.current.end(), -1) - pages.current.begin());
            CHECK(take([&](const Difference& d) { return d.kind == DifferenceKind::PageInserted && d.revisedPage == at; }) == 1);
        } else if (kind == QStringLiteral("page-delete")) {
            CHECK(take([&](const Difference& d) { return d.kind == DifferenceKind::PageDeleted && d.basePage == base; }) == 1);
        } else if (kind == QStringLiteral("page-order")) {
            // EVERY PAGE MATCHED TO WHERE IT WENT, and the move said.
            for (const PageMatch& m : result.pages) {
                REQUIRE(m.base >= 0);
                REQUIRE(m.revised >= 0);
                CHECK(pages.current[static_cast<std::size_t>(m.revised)] == m.base);
            }
            CHECK(take([](const Difference& d) { return d.kind == DifferenceKind::PageMoved; }) >= 1);
        } else if (kind == QStringLiteral("header") || kind == QStringLiteral("footer-date")) {
            // ON EVERY PAGE THAT HAS A COUNTERPART, in its band.
            const Region region = kind == QStringLiteral("header") ? Region::Header : Region::Footer;
            QString added = after;
            if (added.startsWith(before)) added = added.mid(before.size()).trimmed();
            for (const PageMatch& m : result.pages) {
                if (m.base < 0 || m.revised < 0) continue;
                INFO("page " << m.base << " -> " << m.revised);
                CHECK(take([&](const Difference& d) {
                          if (d.basePage != m.base || d.revisedPage != m.revised || d.region != region) return false;
                          return kind == QStringLiteral("header") ? (d.kind == DifferenceKind::Insert && d.after == added)
                                                                  : (d.kind == DifferenceKind::Replace && d.before == before
                                                                     && d.after == after);
                      }) == 1);
            }
        } else if (kind == QStringLiteral("page-numbers")) {
            const int from = change.value(QStringLiteral("before")).toInt();
            const int to = change.value(QStringLiteral("after")).toInt();
            for (const PageMatch& m : result.pages) {
                INFO("page " << m.base << " -> " << m.revised);
                CHECK(take([&](const Difference& d) {
                          return d.basePage == m.base && d.revisedPage == m.revised && d.region == Region::Footer
                                 && d.kind == DifferenceKind::Replace && d.before == QString::number(m.base + 1 + from)
                                 && d.after == QString::number(m.revised + 1 + to);
                      }) == 1);
            }
        } else {
            FAIL("a record kind this case does not grade: " << kind.toStdString());
        }
    }
    std::vector<const Difference*> left;
    for (std::size_t i = 0; i < used.size(); ++i)
        if (!used[i]) left.push_back(&result.differences[i]);
    return left;
}

// The options a pair is graded under: its page numbers ignored -- an inserted, deleted or reordered page
// takes its footer number with it, which `make-compare.py` says in prose and does not record -- except on
// the pair whose change IS the page numbers.
CompareOptions gradingOptions(const Loaded& pair, CompareSensitivity sensitivity)
{
    CompareOptions options;
    options.sensitivity = sensitivity;
    options.ignorePageNumbers = true;
    for (const QJsonValue value : pair.record.value(QStringLiteral("changes")).toArray())
        if (value.toObject().value(QStringLiteral("kind")).toString() == QStringLiteral("page-numbers"))
            options.ignorePageNumbers = false;
    return options;
}

const std::vector<std::string>& editPairs()
{
    static const std::vector<std::string> kPairs = {
        "word-inserted",  "word-deleted",   "word-replaced", "paragraph-moved",      "image-moved",
        "page-inserted",  "page-deleted",   "pages-reordered", "header-changed",     "page-numbers-shifted",
        "whitespace-only", "report"};
    return kPairs;
}

} // namespace

TEST_CASE("every labelled change in every pair is found with its kind and nothing else at text only",
          "[comparecorpus][A-95]")
{
    for (const std::string& name : editPairs()) {
        const auto pair = loadPair(name);
        // TEXT ONLY: every record but an image's found, and NOTHING the records do not name (§3.4).
        {
            const ComparisonResult result = run(*pair, gradingOptions(*pair, CompareSensitivity::TextOnly));
            const std::vector<const Difference*> left = grade(*pair, result, false);
            std::string unexplained;
            for (const Difference* d : left) unexplained += "  " + describe(*d) + "\n";
            INFO(name << " at text only: differences no record names:\n" << unexplained);
            CHECK(left.empty());
            std::printf("A-95 %-22s text only: %d differences, all named\n", name.c_str(),
                        static_cast<int>(result.differences.size()));
        }
        // APPEARANCE: every record, the image's included; what is left may only be the layout layer's.
        {
            const ComparisonResult result = run(*pair, gradingOptions(*pair, CompareSensitivity::Appearance));
            const std::vector<const Difference*> left = grade(*pair, result, true);
            std::string unexplained;
            for (const Difference* d : left)
                if (d->kind != DifferenceKind::Layout) unexplained += "  " + describe(*d) + "\n";
            INFO(name << " at appearance: differences no record names, beyond layout:\n" << unexplained);
            CHECK(unexplained.empty());
            CHECK(result.rastersMissing == 0);
        }
    }
}

TEST_CASE("an identical pair reports nothing at every sensitivity and every ignore-list", "[comparecorpus][A-96]")
{
    const auto pair = loadPair("identical");
    for (const CompareSensitivity sensitivity :
         {CompareSensitivity::TextOnly, CompareSensitivity::TextAndLayout, CompareSensitivity::Appearance}) {
        for (int flags = 0; flags < 8; ++flags) {
            CompareOptions options;
            options.sensitivity = sensitivity;
            options.ignoreHeaderFooter = (flags & 1) != 0;
            options.ignorePageNumbers = (flags & 2) != 0;
            options.ignoreWhitespace = (flags & 4) != 0;
            const ComparisonResult result = run(*pair, options);
            INFO("sensitivity " << static_cast<int>(sensitivity) << ", ignore flags " << flags << "\n" << describe(result));
            CHECK(result.differences.empty());
            REQUIRE(result.pages.size() == pair->base.pages.size());
            for (const PageMatch& m : result.pages) {
                CHECK(m.kind == PageMatch::Kind::Identical);
                CHECK(m.base == m.revised);
            }
        }
    }
}

TEST_CASE("two unrelated documents finish inside the bound with every page replaced", "[comparecorpus][A-97][run-serial]")
{
    const auto pair = loadPair("unrelated");
    REQUIRE(pair->base.pages.size() == 50);
    REQUIRE(pair->revised.pages.size() == 50);

    // THE COMPARISON ON ITS OWN THREAD, the live heap sampled from this one while it runs (Stage 2 I-35:
    // live heap, never private bytes).
    CompareOptions options;
    options.sensitivity = CompareSensitivity::Appearance;
    ComparisonRun comparison(pair->base.pages, pair->revised.pages, options, pair->rasters());
    std::optional<ComparisonResult> outcome;
    comparison.setFinishedHandler([&](const ComparisonResult& result) { outcome = result; });
    const std::size_t heapBefore = bench::heapCensus().busyBytes;
    std::size_t peak = heapBefore;
    const auto started = std::chrono::steady_clock::now();
    comparison.start();
    while (!comparison.isFinished()) {
        peak = std::max(peak, bench::heapCensus().busyBytes);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    peak = std::max(peak, bench::heapCensus().busyBytes);
    comparison.deliverReady();
    REQUIRE(outcome.has_value());
    std::printf("A-97 unrelated: %.3f s, live heap %.1f MB before, peak %.1f MB, %d of %d pairs replaced, "
                "%lld diagonals explored\n",
                seconds, heapBefore / 1048576.0, peak / 1048576.0, outcome->pairsReplaced, outcome->pairsCompared,
                outcome->editsExplored);
    CHECK(seconds < 10.0);
    CHECK(peak < 256u * 1024u * 1024u);
    CHECK(outcome->pairsReplaced == 50);
    CHECK(outcome->count(DifferenceKind::PageReplaced) == 50);
    CHECK(outcome->count(DifferenceKind::Insert) + outcome->count(DifferenceKind::Delete)
          + outcome->count(DifferenceKind::Replace) == 0);

    // THE CONTROL: the same pair with the cap lifted is DIFFED, and explores several times the work -- so the
    // bound above is the cap's doing, not the pair's.
    CompareOptions uncapped = options;
    uncapped.sensitivity = CompareSensitivity::TextOnly;
    uncapped.replaceShare = 1.0;
    uncapped.maxEdits = 1 << 30;
    const ComparisonResult diffed = run(*pair, uncapped);
    std::printf("A-97 control, cap lifted: %d pairs replaced, %lld diagonals explored\n", diffed.pairsReplaced,
                diffed.editsExplored);
    CHECK(diffed.pairsReplaced == 0);
    CHECK(diffed.editsExplored > 3 * outcome->editsExplored);
}

TEST_CASE("each ignore-list removes its change and only its change", "[comparecorpus][A-98]")
{
    const auto count = [](const ComparisonResult& r) { return static_cast<int>(r.differences.size()); };
    SECTION("the header band")
    {
        const auto pair = loadPair("header-changed");
        CompareOptions options;
        CHECK(count(run(*pair, options)) == 3);
        options.ignoreHeaderFooter = true;
        CHECK(count(run(*pair, options)) == 0);
    }
    SECTION("page numbers")
    {
        const auto pair = loadPair("page-numbers-shifted");
        CompareOptions options;
        CHECK(count(run(*pair, options)) == 3);
        options.ignorePageNumbers = true;
        CHECK(count(run(*pair, options)) == 0);
    }
    SECTION("whitespace")
    {
        const auto pair = loadPair("whitespace-only");
        CompareOptions options;
        CHECK(run(*pair, options).count(DifferenceKind::Whitespace) == 3);
        options.ignoreWhitespace = true;
        CHECK(count(run(*pair, options)) == 0);
    }
    SECTION("the control: a number in the body is text with page numbers ignored")
    {
        // `report-v2.pdf`'s table cell 18 -> 21 is a number-only token outside the band, and not its page's
        // label: the ignore-list must leave it, or "page numbers" would mean "every number".
        const auto pair = loadPair("report");
        CompareOptions options;
        options.ignorePageNumbers = true;
        options.ignoreHeaderFooter = true;
        const ComparisonResult result = run(*pair, options);
        INFO(describe(result));
        CHECK(std::count_if(result.differences.begin(), result.differences.end(), [](const Difference& d) {
                  return d.kind == DifferenceKind::Replace && d.before == QStringLiteral("18") && d.after == QStringLiteral("21");
              }) == 1);
        // ... while every footer difference -- the dates and the numbers -- is gone with the band.
        CHECK(std::none_of(result.differences.begin(), result.differences.end(),
                           [](const Difference& d) { return d.region == Region::Footer; }));
    }
}

TEST_CASE("the sensitivities layer as D5-17 says", "[comparecorpus][A-99]")
{
    CompareOptions options;
    SECTION("an image move is the appearance layer's alone")
    {
        const auto pair = loadPair("image-moved");
        options.sensitivity = CompareSensitivity::TextOnly;
        CHECK(run(*pair, options).differences.empty());
        options.sensitivity = CompareSensitivity::TextAndLayout;
        CHECK(run(*pair, options).differences.empty());
        options.sensitivity = CompareSensitivity::Appearance;
        const ComparisonResult result = run(*pair, options);
        INFO(describe(result));
        CHECK(result.count(DifferenceKind::Appearance) >= 1);
        CHECK(static_cast<int>(result.differences.size()) == result.count(DifferenceKind::Appearance));
        for (const Difference& d : result.differences) CHECK(d.revisedPage == 1);
    }
    SECTION("a moved paragraph's shift is the layout layer's")
    {
        const auto pair = loadPair("paragraph-moved");
        options.sensitivity = CompareSensitivity::TextOnly;
        CHECK(run(*pair, options).count(DifferenceKind::Layout) == 0);
        options.sensitivity = CompareSensitivity::TextAndLayout;
        const ComparisonResult result = run(*pair, options);
        INFO(describe(result));
        CHECK(result.count(DifferenceKind::Layout) >= 1);
        CHECK(result.count(DifferenceKind::Appearance) == 0);
    }
    SECTION("a text change is not reported a second time as pixels")
    {
        for (const char* name : {"word-replaced", "word-inserted", "header-changed", "paragraph-moved"}) {
            const auto pair = loadPair(name);
            options.sensitivity = CompareSensitivity::Appearance;
            const ComparisonResult result = run(*pair, options);
            INFO(name << "\n" << describe(result));
            CHECK(result.count(DifferenceKind::Appearance) == 0);
        }
    }
}

TEST_CASE("a moved paragraph is one move and below the move length a deletion and an insertion",
          "[comparecorpus][A-100]")
{
    const auto pair = loadPair("paragraph-moved");
    const QJsonObject change = pair->record.value(QStringLiteral("changes")).toArray().at(0).toObject();
    const QString words = joined(change.value(QStringLiteral("words")));
    const int length = static_cast<int>(change.value(QStringLiteral("words")).toArray().size());

    CompareOptions options;
    const ComparisonResult moved = run(*pair, options);
    INFO(describe(moved));
    REQUIRE(moved.count(DifferenceKind::Move) == 1);
    CHECK(moved.differences.size() == 1);
    CHECK(moved.differences.front().tokensBefore == length);

    // THE CONTROL: a move length one word longer than the paragraph, and the same change is a deletion and an
    // insertion of those words.
    options.minMoveTokens = length + 1;
    const ComparisonResult split = run(*pair, options);
    INFO(describe(split));
    CHECK(split.count(DifferenceKind::Move) == 0);
    CHECK(split.count(DifferenceKind::Delete) == 1);
    CHECK(split.count(DifferenceKind::Insert) == 1);
    for (const Difference& d : split.differences) CHECK((d.kind == DifferenceKind::Delete ? d.before : d.after) == words);
}

TEST_CASE("pages are aligned before words are diffed", "[comparecorpus][A-101]")
{
    const auto pair = loadPair("page-inserted");
    CompareOptions options;
    options.ignorePageNumbers = true;
    const ComparisonResult result = run(*pair, options);
    INFO(describe(result));
    // AFTER THE INSERTED PAGE, every page is matched to its counterpart and is unchanged.
    REQUIRE(result.pages.size() == 4);
    CHECK((result.pages[0].base == 0 && result.pages[0].revised == 0 && result.pages[0].kind == PageMatch::Kind::Identical));
    CHECK((result.pages[1].base == -1 && result.pages[1].revised == 1 && result.pages[1].kind == PageMatch::Kind::Inserted));
    CHECK((result.pages[2].base == 1 && result.pages[2].revised == 2 && result.pages[2].kind == PageMatch::Kind::Identical));
    CHECK((result.pages[3].base == 2 && result.pages[3].revised == 3 && result.pages[3].kind == PageMatch::Kind::Identical));
    CHECK(result.differences.size() == 1);

    // THE CONTROL: with the page numbers counted, the same pages are still matched -- they differ ONLY by
    // the footer number they took with them, which a page diffed against its old neighbour would not.
    options.ignorePageNumbers = false;
    const ComparisonResult counted = run(*pair, options);
    INFO(describe(counted));
    CHECK(counted.count(DifferenceKind::Replace) == 2);
    for (const Difference& d : counted.differences)
        if (d.kind == DifferenceKind::Replace) CHECK((d.region == Region::Footer && d.revisedPage == d.basePage + 1));
}

TEST_CASE("the comparison runs on a thread of its own with progress that only grows and a cancel that stops it",
          "[comparecorpus][A-102]")
{
    const auto pair = loadPair("report");
    CompareOptions options;
    options.sensitivity = CompareSensitivity::Appearance;
    std::mutex mutex;
    std::set<std::thread::id> rasterThreads;
    const core::text::RasterProvider rasters = [&](CompareSide side, int page) {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            rasterThreads.insert(std::this_thread::get_id());
        }
        return side == CompareSide::Base ? pair->base.raster(page) : pair->revised.raster(page);
    };
    ComparisonRun comparison(pair->base.pages, pair->revised.pages, options, rasters);
    std::vector<std::pair<int, int>> progress;
    std::optional<ComparisonResult> outcome;
    std::set<std::thread::id> handlerThreads;
    comparison.setProgressHandler([&](int done, int total) {
        progress.emplace_back(done, total);
        handlerThreads.insert(std::this_thread::get_id());
    });
    comparison.setFinishedHandler([&](const ComparisonResult& result) {
        outcome = result;
        handlerThreads.insert(std::this_thread::get_id());
    });
    comparison.start();
    REQUIRE(comparison.waitForFinished(30000));
    comparison.deliverReady();
    REQUIRE(outcome.has_value());

    // THE THREADS: the work and every raster on the comparer's own, the handlers on this one (the pump).
    const std::thread::id here = std::this_thread::get_id();
    CHECK(comparison.workerThread() != here);
    CHECK(rasterThreads == std::set<std::thread::id>{comparison.workerThread()});
    CHECK(handlerThreads == std::set<std::thread::id>{here});
    // THE PROGRESS: a count that only grows, against one total, to that total.
    REQUIRE_FALSE(progress.empty());
    for (std::size_t i = 1; i < progress.size(); ++i) {
        CHECK(progress[i].first >= progress[i - 1].first);
        CHECK(progress[i].second == progress.front().second);
    }
    CHECK(progress.back().first == progress.back().second);

    // CANCEL, between pairs: a cancel raised once five pairs are done leaves at most one more compared.
    const auto unrelated = loadPair("unrelated");
    std::atomic<bool> cancel{false};
    const int words = static_cast<int>(unrelated->base.pages.size() + unrelated->revised.pages.size()) + 1;
    int pairsWhenCancelled = -1;
    const ComparisonResult cancelled = DocumentComparer::compare(
        unrelated->base.pages, unrelated->revised.pages, CompareOptions{}, {}, &cancel, [&](int done, int) {
            if (done >= words + 5 && !cancel.load()) {
                pairsWhenCancelled = done - words;
                cancel.store(true);
            }
        });
    std::printf("A-102 cancelled after %d pairs; %d compared\n", pairsWhenCancelled, cancelled.pairsCompared);
    CHECK(cancelled.cancelled);
    CHECK(cancelled.pairsCompared <= pairsWhenCancelled + 1);
    CHECK(cancelled.pairsCompared < 50);

    // ... and through the run: cancelled from this thread, it finishes, and says so.
    ComparisonRun stopped(unrelated->base.pages, unrelated->revised.pages, CompareOptions{});
    std::optional<ComparisonResult> stoppedOutcome;
    stopped.setFinishedHandler([&](const ComparisonResult& result) { stoppedOutcome = result; });
    stopped.start();
    stopped.cancel();
    REQUIRE(stopped.waitForFinished(10000));
    stopped.deliverReady();
    REQUIRE(stoppedOutcome.has_value());
    CHECK(stoppedOutcome->cancelled);
}
