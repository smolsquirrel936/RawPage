// core/text/SearchResults -- what an Advanced Search found, and everything a person does with the list:
// sort it, filter it, refine it, export it. Stage 5 T9, decision D5-13 (`Stage5-TextSearch-Plan.md` §3).
//
// PURE: no thread, no engine, no widget. `core/text/FolderSearch` fills it a file at a time and
// `ui/panes/SearchPane` shows it; every rule a person can argue with is here, where `tst_searchresults`
// reaches it headless.
//
// ---------------------------------------------------------------------------------------------
// RELEVANCE IS DEFINED, NOT IMPLIED (D5-13)
// ---------------------------------------------------------------------------------------------
// A FILE's relevance is its number of hits; files sort by it, most first, ties by path. Within a file,
// hits stay in document order. "By page" is the page number first (a hit on no page -- a bookmark, an
// attachment -- before page 1), then the path; "by file" is the path, then document order. A sort nobody
// could check is a sort nobody can argue with, which is why it is defined here rather than left to a
// scoring formula.
//
// ---------------------------------------------------------------------------------------------
// THE FILTER (D5-13): Stage 4's Info metadata
// ---------------------------------------------------------------------------------------------
// Author and subject match when the file's Info contains the text, ignoring case; the date is `/ModDate`,
// else `/CreationDate`, and a range is inclusive at both ends. A filter that asks for something a file's
// Info does not have EXCLUDES that file -- a file with no author is not by "Levi" -- and with no filter
// every file is in.
//
// ---------------------------------------------------------------------------------------------
// THE SNIPPET, AND THE CSV
// ---------------------------------------------------------------------------------------------
// A snippet is the hit and `kSnippetWords` words either side, in reading order, cut at word boundaries,
// with the hit marked by being its own field. A glyph with no Unicode is never in it (D5-2): a snippet is
// text, and an unmapped glyph would be text that is not there. The export is UTF-8 with a byte-order mark
// -- so Excel reads it as UTF-8 -- one row per hit, RFC 4180 quoting, CRLF line ends. The column headings
// are the caller's: `core/` holds no user-facing English.

#ifndef RAWPAGE_CORE_TEXT_SEARCHRESULTS_H
#define RAWPAGE_CORE_TEXT_SEARCHRESULTS_H

#include "core/Result.h"
#include "core/doc/Metadata.h"
#include "core/text/SearchEngine.h"
#include "core/text/SearchUnit.h"
#include "core/text/TextSelection.h"

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <map>
#include <optional>
#include <set>
#include <vector>

namespace rawpage::core::text {

struct Snippet {
    QString before;   // ends where the hit begins: its last character is a space, unless the hit starts the text
    QString match;
    QString after;

    QString plain() const { return before + match + after; }
};

struct ResultHit {
    SearchHit hit;        // page, reading span or unit and `within` -- what activating it shows
    Snippet snippet;
    int order = 0;        // its place in the file's document order
};

struct FileResult {
    QString path;
    Metadata metadata;
    int pageCount = 0;
    std::vector<ResultHit> hits;
    // Why the file was not searched: an encrypted file (`PermissionDenied`, a folder search never asks for
    // a password), a damaged one, a file that is not a PDF. Unset for a file that was searched.
    std::optional<Error> refused;

    bool wasSearched() const noexcept { return !refused.has_value(); }
    bool isEncrypted() const noexcept { return refused && refused->code == ErrorCode::PermissionDenied; }
};

class SearchResults {
public:
    static constexpr int kSnippetWords = 5;

    enum class Sort { Relevance, Page, File };

    struct Filter {
        QString author;
        QString subject;
        std::optional<QDate> from;
        std::optional<QDate> to;
        bool isEmpty() const noexcept { return author.isEmpty() && subject.isEmpty() && !from && !to; }
    };

    // One line of the list: a hit and the file it is in. Pointers into this object, valid until it changes.
    struct Row {
        const FileResult* file = nullptr;
        const ResultHit* hit = nullptr;
    };

    void add(FileResult file);
    void clear();
    const std::vector<FileResult>& files() const noexcept { return files_; }
    int hitCount() const;
    int searchedCount() const;
    int refusedCount() const;

    // Every hit that passes `filter`, in `sort`'s order.
    std::vector<Row> rows(Sort sort, const Filter& filter = {}) const;
    static bool passes(const FileResult& file, const Filter& filter);
    // `/ModDate`, else `/CreationDate`, as a date; unset when neither parses.
    static std::optional<QDate> dateOf(const Metadata& metadata);

    // ---- Refine ---------------------------------------------------------------------------------
    // The pages each file has a hit on -- -1 standing for a hit on no page -- which is what a refining
    // search is limited to; and the second search's results cut to them: a page is kept only when BOTH
    // queries found it.
    std::map<QString, std::set<int>> hitPages() const;
    void keepOnly(const std::map<QString, std::set<int>>& pages);

    // ---- Snippets -------------------------------------------------------------------------------
    static Snippet snippetOf(const PageReading& reading, Span span, int words = kSnippetWords);
    static Snippet snippetOf(const QString& text, Span within, int words = kSnippetWords);

    // ---- Export ---------------------------------------------------------------------------------
    // `headings` are the three column titles -- file, page, snippet -- in the caller's language. A hit on
    // no page has an empty page cell.
    static QByteArray toCsv(const std::vector<Row>& rows, const QStringList& headings);

private:
    std::vector<FileResult> files_;
};

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_SEARCHRESULTS_H
