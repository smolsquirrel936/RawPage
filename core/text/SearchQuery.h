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
// THE FOUR CONTENT-CLASS SWITCHES (T8, D5-12): `search/includeComments`, `…Bookmarks`, `…Fields` and
// `…Attachments`, read here like the flags. PAGE TEXT IS ALWAYS INCLUDED. **Where a class is searched
// depends on the scope**: a comment and a field are ON a page, so a page range searches the ones on its
// pages; a bookmark and an attachment belong to the DOCUMENT, so only a whole-document search includes
// them; and a selection is a range of page text, so it includes none of the four.

#ifndef RAWPAGE_CORE_TEXT_SEARCHQUERY_H
#define RAWPAGE_CORE_TEXT_SEARCHQUERY_H

#include "core/settings/Settings.h"
#include "core/text/SearchUnit.h"
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

// Which of the four other content classes a search includes (the header). Page text is not a switch.
struct SearchClasses {
    bool comments = false;      // search/includeComments
    bool bookmarks = false;     // search/includeBookmarks
    bool fields = false;        // search/includeFields
    bool attachments = false;   // search/includeAttachments

    bool includes(ContentClass contentClass) const noexcept
    {
        switch (contentClass) {
        case ContentClass::PageText: return true;
        case ContentClass::Comments: return comments;
        case ContentClass::Bookmarks: return bookmarks;
        case ContentClass::Fields: return fields;
        case ContentClass::Attachments: return attachments;
        }
        return false;
    }
    bool any() const noexcept { return comments || bookmarks || fields || attachments; }
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
    SearchClasses classes;

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
        query.classes.comments = settings.get(keys::search::includeComments);
        query.classes.bookmarks = settings.get(keys::search::includeBookmarks);
        query.classes.fields = settings.get(keys::search::includeFields);
        query.classes.attachments = settings.get(keys::search::includeAttachments);
        query.maxResults = settings.get(keys::search::maxResults);
        return query;
    }
};

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_SEARCHQUERY_H
