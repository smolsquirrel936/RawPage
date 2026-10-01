// core/text/SearchUnit -- WHAT A SEARCH FINDS OUTSIDE THE PAGE TEXT: a comment, a form field, a bookmark,
// an attachment. Stage 5 T8, decision D5-12 (`Stage5-TextSearch-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// STRINGS WITH PLACES, NOT MODELS
// ---------------------------------------------------------------------------------------------
// `Features.md` §1.8 names five content classes. Page text is `DocumentIndex`'s; the other four are read
// by ONE read-only facade reader, `IDocumentBackend::searchUnits`, as plain SEARCH UNITS -- a class, the
// text, the page it is on (or none), a locator saying which object it came from, a rectangle -- and nothing
// more. **Stage 5 builds none of Stage 6's or Stage 9's models** (`AnnotationStore`, `FormModel`,
// `Attachments`): those are editing models with commands, and their read halves built here would be a
// second reader to keep in agreement once those stages arrive. When they exist they become the source, and
// folding this reader into them is their stages' decision.
//
// ---------------------------------------------------------------------------------------------
// READ IN PARTS, EACH NO LONGER THAN ONE PAGE (D5-12 clarified at T8)
// ---------------------------------------------------------------------------------------------
// A document's units are read on the engine thread (D5-3), and a job there cannot be preempted -- so one
// job that walked every page's annotations would hold a visible tile for the whole walk: ~145 ms on
// `text-900.pdf`, the cost T3 measured for an open's page walk, where one page's extraction is at most
// ~2.6 ms (`Tech-Stack-Plan.md` §3.3, *A2 at Stage 5*). That is the arrangement D5-13 was amended to
// forbid for a folder search's open, so the reader is never asked for a whole document at once. It is
// asked for one PART:
//
//   * part `p` in `[0, pageCount)` -- page `p`'s annotations: its comments and its form fields;
//   * `kOutlinePart` -- the bookmark titles, from Stage 4's `core/doc/Outline` (no engine call at all);
//   * `kAttachmentsPart` -- each embedded file's name and description, and the page text of an embedded
//     file that is itself a PDF, ONE LEVEL deep.
//
// Each part is read once per document and only when a search includes a class it holds (guide A-79):
// comments or fields need the pages' parts, bookmarks the outline's, attachments the attachments'.
// **The attachments part is the one that can be long**: an attached PDF's text is extracted in that one
// job, so a long attached PDF holds the engine for its whole extraction. That bound is recorded at the
// reader (`pdf/render/PdfiumDocument::searchUnits`) rather than split here.

#ifndef RAWPAGE_CORE_TEXT_SEARCHUNIT_H
#define RAWPAGE_CORE_TEXT_SEARCHUNIT_H

#include "core/Geometry.h"
#include "core/doc/Destinations.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace rawpage::core::text {

// The five classes of `Features.md` §1.8. Page text is always searched; the other four each have a
// `search/include*` switch (`SearchQuery::classes`).
enum class ContentClass : std::uint8_t {
    PageText,
    Comments,
    Bookmarks,
    Fields,
    Attachments,
};

// The two parts that are not a page (see the header). Negative, so a page's part is its index.
inline constexpr int kOutlinePart = -1;
inline constexpr int kAttachmentsPart = -2;

struct SearchUnit {
    // Which string of its object this is -- what a result list says the hit was in.
    enum class Field : std::uint8_t {
        CommentContents,        // an annotation's /Contents
        CommentSubject,         // ... and its /Subj
        FieldName,              // a form field's fully qualified name
        FieldValue,             // ... and its value
        BookmarkTitle,          // an outline item's /Title
        AttachmentName,         // an embedded file's name
        AttachmentDescription,  // ... and its /Desc
        AttachedText,           // one page of an embedded PDF's text
    };

    ContentClass contentClass = ContentClass::Comments;
    Field field = Field::CommentContents;
    QString text;

    // The page the object is ON, or -1: a bookmark and an attachment belong to the document, not a page.
    int page = -1;
    // Where on that page, in page space (PDF points, unrotated -- D2-12's convention, like every other
    // page-space fact): an annotation's /Rect, a widget's /Rect. Meaningful only when `hasRect`.
    PdfRect rect;
    bool hasRect = false;

    // WHICH OBJECT: an annotation's index on its page, a field's qualified name, an embedded file's
    // name, an outline item's path (`0/2/1`). Two units of one object share it -- a field's name and its
    // value, an attachment's name and its text -- and that is what lets a field shown by three widgets
    // be found once (`SearchEngine`).
    QString locator;

    // A bookmark's destination: where its hit navigates (D5-12).
    Destination destination;
    // `AttachedText`: which page of the attached PDF, 0-based.
    int attachedPage = -1;
};

// What one part delivered. A FAILED part is stored empty rather than asked again -- asked again it would
// fail again, and a search waiting on it would wait for ever (`IndexedPage`'s rule for a page).
struct SearchUnitPart {
    int part = 0;
    std::vector<SearchUnit> units;
    bool failed = false;
};

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_SEARCHUNIT_H
