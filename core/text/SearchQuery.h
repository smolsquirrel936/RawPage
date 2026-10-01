// core/text/SearchQuery -- WHAT a search is for: the pattern, the four matching flags, and where to look.
// Stage 5 T6, decisions D5-4, D5-5 and D5-14 (`Stage5-TextSearch-Plan.md` §3).
//
// A value type and nothing else. Matching is `core/text/SearchEngine`'s, and the text it matches is
// `core/text/DocumentIndex`'s. It lives in a header of its own because the find bar (T7), the search pane
// (T9) and the comparison's ignore-lists (T12) all build one, and none of them should need the engine to
// say what they want.
//
// THE FLAGS ARE THE SETTINGS (D5-14). `fromSettings` is the one place a query's flags are read from
// `search/*`: the find bar's `Alt+R`, `Alt+W` and `Alt+C` WRITE those keys (T7), so the bar, Preferences
// and every search agree because there is one truth to agree with -- Stage 4's D4-5, *a second key for the
// same fact is two truths*.
//
// WHAT IS NOT HERE YET: the four content-class switches (`search/includeComments`, `…Bookmarks`,
// `…Fields`, `…Attachments`) are declared at T6 with the other six keys (D5-20) and read by nothing until
// T8 builds the reader they switch (D5-12). A search at T6 is over PAGE TEXT, which is always included.

#ifndef RAWPAGE_CORE_TEXT_SEARCHQUERY_H
#define RAWPAGE_CORE_TEXT_SEARCHQUERY_H

#include "core/settings/Settings.h"
#include "core/text/TextNormalizer.h"
#include "core/text/TextSelection.h"

#include <QString>

namespace rawpage::core::text {

struct SearchFlags {
    bool wholeWords = false;        // search/wholeWords
    bool caseSensitive = false;     // search/caseSensitive
    bool ignoreDiacritics = false;  // search/ignoreDiacritics
    bool useRegex = false;          // search/useRegex

    // D5-5's pipeline, configured by two of the four: the page and the query are normalized with the SAME
    // options, or a folded page would be searched for an unfolded word.
    NormalizeOptions normalizeOptions() const noexcept
    {
        NormalizeOptions options;
        options.foldCase = !caseSensitive;
        options.stripDiacritics = ignoreDiacritics;
        return options;
    }
};

// Where to look (D5-14): the whole document, a page range, or inside the current selection.
struct SearchScope {
    enum class Kind { WholeDocument, PageRange, Selection };

    Kind kind = Kind::WholeDocument;
    int firstPage = 0;          // PageRange: 0-based, inclusive, clamped to the document
    int lastPage = -1;
    TextSelection selection;    // Selection: a hit must lie wholly inside it

    static SearchScope wholeDocument() { return {}; }
    static SearchScope pages(int first, int last)
    {
        SearchScope scope;
        scope.kind = Kind::PageRange;
        scope.firstPage = first;
        scope.lastPage = last;
        return scope;
    }
    static SearchScope within(const TextSelection& selection)
    {
        SearchScope scope;
        scope.kind = Kind::Selection;
        scope.selection = selection;
        return scope;
    }
};

struct SearchQuery {
    QString pattern;
    SearchFlags flags;
    SearchScope scope;

    // How many hits the list may hold; 0 is no bound. The product passes `search/maxResults`, and a search
    // that reaches it SAYS so (`SearchEngine::Outcome::limitReached`, guide A-61) -- a list cut short in
    // silence reads as every hit there is.
    int maxResults = 0;

    // The flags and the bound as the settings hold them, over `pattern` and `scope`.
    static SearchQuery fromSettings(const Settings& settings, QString pattern, SearchScope scope = {})
    {
        SearchQuery query;
        query.pattern = std::move(pattern);
        query.scope = std::move(scope);
        query.flags.wholeWords = settings.get(keys::search::wholeWords);
        query.flags.caseSensitive = settings.get(keys::search::caseSensitive);
        query.flags.ignoreDiacritics = settings.get(keys::search::ignoreDiacritics);
        query.flags.useRegex = settings.get(keys::search::useRegex);
        query.maxResults = settings.get(keys::search::maxResults);
        return query;
    }
};

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_SEARCHQUERY_H
