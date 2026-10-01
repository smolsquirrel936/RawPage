// tst_searchresults -- `core/text/SearchResults`, headless: the snippets, the three sorts with relevance
// DEFINED, the metadata filter, refine and the CSV's bytes. Stage 5 T9, decision D5-13; the headless halves
// of guide cases A-82 ... A-86 (`Stage5-TextSearch-Testing-Guide.md` §5.11).
//
// WHAT IT CANNOT CLAIM (convention 19): every result here is built by hand, so nothing says a folder search
// FINDS these hits or that a snippet matches the corpus's text -- `tests/render/tst_foldersearch.cpp` grades
// that against the generator -- and nothing about the pane that shows them (`tests/ui/tst_searchpane.cpp`).

#include <catch2/catch_test_macros.hpp>

#include "core/text/PageTextModel.h"
#include "core/text/SearchResults.h"
#include "core/text/TextSelection.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>
#include <string>
#include <vector>

using rawpage::core::ErrorCode;
using rawpage::core::Metadata;
using rawpage::core::PdfPoint;
using rawpage::core::PdfRect;
using rawpage::core::PdfSize;
using namespace rawpage::core::text;

namespace {

ResultHit hitOn(int page, int order, const QString& match = QStringLiteral("lorem"))
{
    ResultHit hit;
    hit.hit.page = page;
    hit.order = order;
    hit.snippet.match = match;
    return hit;
}

FileResult fileWith(const QString& path, std::vector<int> pages)
{
    FileResult file;
    file.path = path;
    file.pageCount = 10;
    int order = 0;
    for (const int page : pages) file.hits.push_back(hitOn(page, order++));
    return file;
}

std::vector<QString> where(const std::vector<SearchResults::Row>& rows)
{
    std::vector<QString> out;
    for (const auto& row : rows) out.push_back(row.file->path + QLatin1Char(':') + QString::number(row.hit->hit.page));
    return out;
}

} // namespace

// =============================================================================================
// A-82 (headless) -- a snippet is the hit and the words either side, cut at word boundaries
// =============================================================================================

TEST_CASE("a snippet is five whole words either side of the hit with the hit its own field", "[searchresults][A-82]")
{
    const QString text = QStringLiteral("one two three four five six seven EIGHT nine ten eleven twelve thirteen fourteen");
    const qsizetype at = text.indexOf(QStringLiteral("EIGHT"));
    const Snippet snippet = SearchResults::snippetOf(text, Span{static_cast<int>(at), static_cast<int>(at + 5)});
    CHECK(snippet.before == QStringLiteral("three four five six seven "));
    CHECK(snippet.match == QStringLiteral("EIGHT"));
    CHECK(snippet.after == QStringLiteral(" nine ten eleven twelve thirteen"));
    CHECK(snippet.plain() == QStringLiteral("three four five six seven EIGHT nine ten eleven twelve thirteen"));

    // AT THE START, fewer words before -- and never a word cut in half at either end (the control: a cut at a
    // character count would leave "tee" of "thirteen").
    const Snippet first = SearchResults::snippetOf(text, Span{0, 3});
    CHECK(first.before.isEmpty());
    CHECK(first.match == QStringLiteral("one"));
    CHECK(first.after == QStringLiteral(" two three four five six"));

    // A LINE BREAK IS A SPACE, and U+FFFD -- an unmapped glyph's mark -- is never shown (D5-2).
    const QString broken = QStringLiteral("alpha\nbeta \uFFFD gamma delta");
    const qsizetype beta = broken.indexOf(QStringLiteral("beta"));
    const Snippet clean = SearchResults::snippetOf(broken, Span{static_cast<int>(beta), static_cast<int>(beta + 4)});
    CHECK_FALSE(clean.plain().contains(QChar(0xFFFD)));
    CHECK_FALSE(clean.plain().contains(QLatin1Char('\n')));
    CHECK(clean.before == QStringLiteral("alpha "));
}

TEST_CASE("a page's snippet is read in reading order with a break as a space", "[searchresults][A-82]")
{
    PageTextModel model(0);
    double baseline = 700.0;
    for (const std::u32string& line : {std::u32string(U"the quick brown"), std::u32string(U"fox jumps over")}) {
        double x = 72.0;
        for (const char32_t cp : line) {
            TextChar c;
            c.codepoint = cp;
            c.fontSize = 10.0;
            c.origin = PdfPoint{x, baseline};
            c.box = PdfRect{PdfPoint{x, baseline - 2.1}, PdfSize{5.5, 11.1}};
            model.append(c);
            x += 5.5;
        }
        baseline -= 12.0;
    }
    const auto reading = PageReading::build(std::make_shared<const PageTextModel>(std::move(model)));
    // `fox` -- the first three positions of the second line -- found by its reading positions.
    int fox = -1;
    for (int p = 0; p < reading->size() && fox < 0; ++p)
        if (reading->charAt(p).codepoint == U'f') fox = p;
    REQUIRE(fox >= 0);
    const Snippet snippet = SearchResults::snippetOf(*reading, Span{fox, fox + 3});
    CHECK(snippet.match == QStringLiteral("fox"));
    CHECK(snippet.before == QStringLiteral("the quick brown "));
    CHECK(snippet.after == QStringLiteral(" jumps over"));
}

// =============================================================================================
// A-83 -- sorting, with relevance defined
// =============================================================================================

TEST_CASE("results sort by relevance by page and by file each differently", "[searchresults][A-83]")
{
    SearchResults results;
    results.add(fileWith(QStringLiteral("b.pdf"), {5}));
    results.add(fileWith(QStringLiteral("a.pdf"), {2, 3, 9}));
    results.add(fileWith(QStringLiteral("c.pdf"), {0}));

    // RELEVANCE: the file's hits, most first -- a.pdf's three -- ties by path (b before c); document order within.
    const auto relevance = where(results.rows(SearchResults::Sort::Relevance));
    CHECK(relevance == std::vector<QString>{"a.pdf:2", "a.pdf:3", "a.pdf:9", "b.pdf:5", "c.pdf:0"});
    // BY PAGE: the page first, then the path.
    const auto page = where(results.rows(SearchResults::Sort::Page));
    CHECK(page == std::vector<QString>{"c.pdf:0", "a.pdf:2", "a.pdf:3", "b.pdf:5", "a.pdf:9"});
    // BY FILE: the path, then document order.
    const auto file = where(results.rows(SearchResults::Sort::File));
    CHECK(file == std::vector<QString>{"a.pdf:2", "a.pdf:3", "a.pdf:9", "b.pdf:5", "c.pdf:0"});
    // THE LIST WAS CHOSEN SO THE ORDERS DIFFER: relevance against page differs, and relevance against file
    // differs once a less relevant file sorts first by name.
    CHECK(relevance != page);
    results.add(fileWith(QStringLiteral("0-first.pdf"), {7}));
    CHECK(where(results.rows(SearchResults::Sort::Relevance)).front() == QStringLiteral("a.pdf:2"));
    CHECK(where(results.rows(SearchResults::Sort::File)).front() == QStringLiteral("0-first.pdf:7"));

    // A FILE NOT SEARCHED holds no row, and the counts say which was which.
    FileResult locked;
    locked.path = QStringLiteral("locked.pdf");
    locked.refused = rawpage::core::Error{ErrorCode::PermissionDenied, QLatin1String("error.open.badPassword")};
    results.add(locked);
    CHECK(results.rows(SearchResults::Sort::File).size() == 6);
    CHECK(results.refusedCount() == 1);
    CHECK(results.files().back().isEncrypted());
}

// =============================================================================================
// A-84 -- filtering by Info metadata
// =============================================================================================

TEST_CASE("the metadata filter keeps exactly the files whose Info says so", "[searchresults][A-84]")
{
    SearchResults results;
    FileResult levi = fileWith(QStringLiteral("levi.pdf"), {0});
    levi.metadata.author = QStringLiteral("Noa Levi");
    levi.metadata.subject = QStringLiteral("Five content classes");
    levi.metadata.creationDate = QStringLiteral("D:20200101000000Z");
    levi.metadata.modificationDate = QStringLiteral("D:20260918120000Z");   // wins over the creation date
    FileResult chen = fileWith(QStringLiteral("chen.pdf"), {0});
    chen.metadata.author = QStringLiteral("Chen Wei");
    chen.metadata.subject = QStringLiteral("A non-embedded CJK font");
    chen.metadata.creationDate = QStringLiteral("D:20260917150000Z");      // no /ModDate: the creation date
    FileResult bare = fileWith(QStringLiteral("bare.pdf"), {0});              // no Info at all
    results.add(levi);
    results.add(chen);
    results.add(bare);

    const auto files = [&](const SearchResults::Filter& filter) {
        std::vector<QString> out;
        for (const auto& row : results.rows(SearchResults::Sort::File, filter)) out.push_back(row.file->path);
        return out;
    };
    // THE CONTROL: no filter, every file -- the one with no Info too.
    CHECK(files({}) == std::vector<QString>{"bare.pdf", "chen.pdf", "levi.pdf"});

    SearchResults::Filter author;
    author.author = QStringLiteral("levi");   // a part, any case
    CHECK(files(author) == std::vector<QString>{"levi.pdf"});   // bare.pdf has no author, and is out

    SearchResults::Filter subject;
    subject.subject = QStringLiteral("CJK");
    CHECK(files(subject) == std::vector<QString>{"chen.pdf"});

    // A DATE RANGE, inclusive, over /ModDate else /CreationDate.
    SearchResults::Filter range;
    range.from = QDate(2026, 9, 17);
    range.to = QDate(2026, 9, 17);
    CHECK(files(range) == std::vector<QString>{"chen.pdf"});
    range.to = QDate(2026, 9, 18);
    CHECK(files(range) == std::vector<QString>{"chen.pdf", "levi.pdf"});
    // levi.pdf's CREATION date is 2020: were it read, this range would hold it.
    SearchResults::Filter old;
    old.to = QDate(2021, 1, 1);
    CHECK(files(old).empty());
}

// =============================================================================================
// A-85 -- refine
// =============================================================================================

TEST_CASE("a refined search keeps only the pages both searches found", "[searchresults][A-85]")
{
    SearchResults first;
    first.add(fileWith(QStringLiteral("a.pdf"), {2, 3}));
    first.add(fileWith(QStringLiteral("b.pdf"), {5}));
    const auto pages = first.hitPages();
    CHECK(pages.at(QStringLiteral("a.pdf")) == std::set<int>{2, 3});

    SearchResults second;
    second.add(fileWith(QStringLiteral("a.pdf"), {3, 3, 4}));   // page 3 twice, page 4 the first never found
    second.add(fileWith(QStringLiteral("b.pdf"), {6}));
    second.add(fileWith(QStringLiteral("c.pdf"), {1}));         // a file the first search found nothing in
    second.keepOnly(pages);
    CHECK(where(second.rows(SearchResults::Sort::File)) == std::vector<QString>{"a.pdf:3", "a.pdf:3"});
    CHECK(second.hitCount() == 2);
}

// =============================================================================================
// A-86 -- the CSV's bytes
// =============================================================================================

TEST_CASE("the export is UTF-8 CSV with a byte order mark and RFC 4180 quoting", "[searchresults][A-86]")
{
    SearchResults results;
    FileResult file = fileWith(QStringLiteral("C:/scratch/a, b.pdf"), {0, -1, 1});
    file.hits[0].snippet = Snippet{QStringLiteral("say "), QStringLiteral("\"hi\""), QStringLiteral(" there")};
    file.hits[1].snippet = Snippet{QStringLiteral("line one\nline "), QStringLiteral("two"), QString()};
    file.hits[2].snippet = Snippet{QStringLiteral("中文 "), QStringLiteral("שלום"), QStringLiteral(" end")};
    results.add(file);
    const QByteArray csv =
        SearchResults::toCsv(results.rows(SearchResults::Sort::File), {QStringLiteral("File"), QStringLiteral("Page"), QStringLiteral("Text")});

    REQUIRE(csv.startsWith("\xEF\xBB\xBF"));   // the byte-order mark Excel reads UTF-8 by
    const QString text = QString::fromUtf8(csv.mid(3));
    const QStringList lines = text.split(QStringLiteral("\r\n"));
    CHECK(lines.at(0) == QStringLiteral("File,Page,Text"));
    // A comma in the path, quotes in the snippet: quoted, the quotes doubled.
    CHECK(lines.at(1) == QStringLiteral("\"C:/scratch/a, b.pdf\",1,\"say \"\"hi\"\" there\""));
    // A hit on no page: an empty page cell; a line break inside a field: quoted, kept.
    CHECK(text.contains(QStringLiteral("\"C:/scratch/a, b.pdf\",,\"line one\nline two\"")));
    // CJK and Hebrew survive a byte round trip.
    CHECK(text.contains(QStringLiteral("2,中文 שלום end")));
    // One row per hit, after the heading: three hits, and every line CRLF-terminated.
    CHECK(csv.endsWith("\r\n"));
    CHECK(csv.count("\r\n") == 4);
}
