// core/doc/PageReconnect -- pages pasted back into the document they were copied or cut from keep what pointed at them,
// and what they pointed at. Stage 7 guide I-23 (`Stage7-Assembly-Testing-Guide.md` §8), decision D7-37
// (`Stage7-Assembly-Plan.md` §3), the user's answers of 2026-11-17.
//
// A CUT IS A COPY AND A DELETE (D7-31(e)), and the delete does what every delete does (D7-2): each bookmark, named
// destination and link that named a cut page is KEPT, going nowhere -- its page -1, its view as it was. The paste is an
// insert of the copy: new pages nothing names, whose own links to pages that were not copied the copier cleared (D7-4).
// So until I-23 a cut and paste, which a person makes as a MOVE, lost every reference a drag or `Alt+Down` keeps.
//
// THE FIX IS TWO HALVES, and both are here as data and arithmetic:
//   * WHEN THE PAGES ARE COPIED (`referencesOfPages`), what named each copied page -- bookmarks by id, named destinations
//     by name, links on the pages NOT copied by id -- and each copied page's own links to a page not copied: which link
//     of that page, counted over its links in listing order (the copier keeps every link and their order), and the
//     page it named by its SERIAL (`Page::serial`), which survives every page command between the copy and the paste.
//   * WHEN THEY ARE PASTED into the same document (`reconnectionFor`), those references made concrete: each page an
//     INDEX of the document after the insert. `InsertPagesCommand` carries the result and, once its pages are in,
//     points again ONLY WHAT STILL GOES NOWHERE -- D7-37(c), the rule for a copy as for a cut: after a copy the
//     originals are still named, so nothing changes but the pasted pages' own links, which then name what their
//     sources name, as a duplicate's do (D7-3); a second paste of one cut finds the first paste's pages named.
//
// SAME DOCUMENT, SAME PROCESS, ANY TIME LATER (D7-37(b)). The record is the copying document's and a serial means
// nothing outside the process that gave it, so `ui/` keeps the record with the document it was copied from and offers
// it only to a paste into that document of the same payload. A paste into another document, or from another process,
// is the plain insert it was.
//
// A JOURNAL LINE NAMES PAGES BY INDEX (D7-24): `PageReconnection` holds indices, never serials, so a replay of the
// line against the document the journal recorded does what the paste did.
//
// NOT CARRIED: a link whose identity is DIRECT (`AnnotationId::Kind::Direct` names its page and position, which a page
// command changes); a structure element, a thread bead and `/OpenAction`, which the delete removed rather than kept
// going nowhere (D7-2) -- the user asked for bookmarks and links (2026-11-17), and named destinations go with them.
//
// Pure: QtCore and the standard library -- `rawpage_tests`, under `portable-asan` too.

#ifndef RAWPAGE_CORE_DOC_PAGERECONNECT_H
#define RAWPAGE_CORE_DOC_PAGERECONNECT_H

#include "core/Result.h"
#include "core/doc/Annotation.h"
#include "core/doc/Outline.h"

#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <vector>

namespace rawpage::core {

class Document;   // core/doc/Document.h

// What named a copied page.
enum class ReconnectKind : std::uint8_t {
    Bookmark,           // an outline item's own destination
    BookmarkAction,     // an outline item's GoTo action
    NamedDestination,   // an entry of the named destinations
    Link,               // a link on a page that was not copied
};

const char* reconnectKindName(ReconnectKind kind) noexcept;

// The largest number of references one record or one journal line carries -- a hostile line's bound.
inline constexpr int kMaxReconnections = 1'000'000;

// Taken when the pages are copied, held by `ui/` with the document they were copied from. Serials: this process only.
struct CopiedPageReferences {
    struct Incoming {
        ReconnectKind kind = ReconnectKind::Bookmark;
        OutlineItemId item;   // Bookmark, BookmarkAction
        QString name;         // NamedDestination
        AnnotationId link;    // Link
        int ordinal = 0;      // which copied page it named, counted in the copy's order
    };
    struct Outgoing {
        int ordinal = 0;           // the copied page the link is on
        int link = 0;              // which of that page's links, counted over its listing's links in order
        std::uint64_t target = 0;  // the page it named (`Page::serial`)
        OutlineAction action;      // as it was; its page is named again at the paste
    };
    std::vector<Incoming> incoming;
    std::vector<Outgoing> outgoing;

    bool isEmpty() const noexcept { return incoming.empty() && outgoing.empty(); }
};

// What names, and what is named by, the pages `pages` of `document` -- in that order, the order a copy of them holds
// them. A page outside the document is no page; a link to a page in `pages` is left to the copier, which keeps it.
CopiedPageReferences referencesOfPages(const Document& document, const std::vector<int>& pages);

// What `InsertPagesCommand` carries: the record made concrete for one insert, every page an index AFTER it.
struct PageReconnection {
    struct Incoming {
        ReconnectKind kind = ReconnectKind::Bookmark;
        OutlineItemId item;
        QString name;
        AnnotationId link;
        int page = 0;   // the pasted page it names now

        friend bool operator==(const Incoming&, const Incoming&) = default;
    };
    struct Outgoing {
        int ordinal = 0;        // the pasted page the link is on, counted from the insert's first
        int link = 0;           // which of its links
        OutlineAction action;   // a GoTo, its page an index after the insert

        friend bool operator==(const Outgoing&, const Outgoing&) = default;
    };
    std::vector<Incoming> incoming;
    std::vector<Outgoing> outgoing;

    bool isEmpty() const noexcept { return incoming.empty() && outgoing.empty(); }
    friend bool operator==(const PageReconnection&, const PageReconnection&) = default;

    // {"incoming": [{"kind": "bookmark", "id": "o:12", "page": 3}, {"kind": "destination", "name": "...", "page": 3},
    //  {"kind": "link", "id": "obj:19", "page": 3}], "outgoing": [{"page": 0, "link": 1, "action": {...}}]}
    // `fromJson` is a parser of hostile input: every field checked, every list and index bounded, an action that is
    // not a GoTo to a page refused.
    QJsonObject toJson() const;
    static Result<PageReconnection> fromJson(const QJsonObject& json);
};

// `references` for `count` pages inserted at `at` of `document` as it stands BEFORE the insert: each incoming reference
// that exists and goes nowhere now, at the pasted page it named; each outgoing link whose page `document` still holds,
// at that page's index after the insert. Empty when nothing would be named again.
PageReconnection reconnectionFor(const Document& document, const CopiedPageReferences& references, int at, int count);

// Whether a target goes nowhere: no action, or a GoTo whose page is gone.
bool goesNowhere(const OutlineAction& action) noexcept;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_PAGERECONNECT_H
