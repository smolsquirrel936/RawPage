// core/doc/AnnotationStore -- every annotation of an open document, its threads, its states and the
// queries the Comments pane is a view of. Stage 6 T2 builds the READ side (decisions D6-3, D6-12,
// `Stage6-Annotations-Plan.md` §3); the commands that change it arrive from T3, and T9 the pane's
// grouping, filters, thread sets and sequence numbers.
//
// ===========================================================================================
// TWO VIEWS OF ONE READING, BECAUSE THE FILE HAS TWO
// ===========================================================================================
// A page's `/Annots` is a LISTING: entries in an order, some of them not dictionaries at all, one
// dictionary possibly listed by several pages. The annotations are the DICTIONARIES behind it, each
// once. D6-3's per-page agreement with the engine is a claim about the listing -- PDFium counts the
// entries of the array, whatever they are -- while the Comments pane, the threads and every command
// are about the dictionaries. So the store keeps both: `listing(page)` entry by entry, each naming the
// annotation it resolved to or none, and `all()` once per dictionary. An annotation listed by several
// pages is OWNED by the page its `/P` names when that page lists it, else by the first page that does,
// and the other listings point at the same annotation (`annots-direct.pdf`).
//
// ===========================================================================================
// THREADS ARE ISO 32000-1 §12.5.6.3's, AND A BROKEN THREAD IS SHOWN, NOT HIDDEN (D6-12)
// ===========================================================================================
// A reply is an annotation with `/IRT` (and `/RT /R`, the default); a GROUP member has `/RT /Group`
// and is shown as one item with its parent; a STATE reply carries `/StateModel` and is a status, not
// a comment. Three defects are read, reported and shown as TOP-LEVEL rather than dropped: a reply whose
// parent does not exist, a reply on another page than its parent, and a cycle of `/IRT` (every member
// of it). Threading is iterative -- a 10,000-deep chain is a loop, not a recursion (guide case A-22).
//
// ===========================================================================================
// HEADLESS
// ===========================================================================================
// Pure: `pdf/ops/AnnotationReader` produces a `Reading`; this class never sees a byte. Which is what
// lets `tst_annotstore` grade threads and queries in `rawpage_tests` with no engine linked, and what
// lets the batch runner (flow K) and Stage 8's summary reach the same query the pane shows.

#ifndef RAWPAGE_CORE_DOC_ANNOTATIONSTORE_H
#define RAWPAGE_CORE_DOC_ANNOTATIONSTORE_H

#include "core/Result.h"
#include "core/doc/Annotation.h"
#include "core/doc/Fdf.h"
#include "core/doc/Permissions.h"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <map>
#include <optional>
#include <vector>

namespace rawpage::core {

// What the reading met that a reader of the file should know. Reported -- to the log at open, and to
// whoever asks -- and never a reason to refuse the document.
enum class AnnotationNoteKind : std::uint8_t {
    AnnotsNotArray,       // a page's /Annots is not an array: the page lists nothing
    EntryNotDictionary,   // an /Annots entry that is not, or does not resolve to, a dictionary
    NonFiniteRect,        // a /Rect coordinate that is NaN or infinite
    GeometryTruncated,    // more coordinates than the reader keeps -- the rest are dropped
    ListedByManyPages,    // one dictionary in several pages' /Annots
    Unlisted,             // reached through /IRT, /Popup or /Parent and listed by no page: ignored
    MissingParent,        // /IRT names no annotation of this document: shown top-level
    ReplyCycle,           // an /IRT chain that returns to itself: every member shown top-level
    ReplyOnOtherPage,     // /IRT names an annotation on another page: shown top-level
};

const char* annotationNoteName(AnnotationNoteKind kind) noexcept;

struct AnnotationNote {
    AnnotationNoteKind kind = AnnotationNoteKind::EntryNotDictionary;
    int page = -1;              // 0-based, -1 when no page applies
    long long object = -1;      // the object it concerns, -1 for a direct dictionary or none
};

// One entry of a page's /Annots.
struct AnnotationListingEntry {
    AnnotationFact fact;                       // what D6-3 compares with the engine
    std::optional<std::size_t> annotation;     // the annotation it resolved to; none if not a dictionary
};

// One state reply in a history: who set which state, when.
struct AnnotationStateChange {
    std::size_t reply = 0;     // the state reply's index in `all()`
    QString author;
    QString state;
    PdfDate date;              // its /M, else its /CreationDate
};

class AnnotationStore {
public:
    struct Reading {
        std::vector<Annotation> annotations;                     // each dictionary once
        std::vector<std::vector<AnnotationListingEntry>> pages;  // one listing per page
        std::vector<AnnotationNote> notes;
    };

    // Unread: refused as not assessed, like a default `Permissions`.
    AnnotationStore();
    static AnnotationStore fromReading(Reading reading);
    // Nothing could be read -- an encrypted document (D2-6: the first-party reader declines it) or one
    // whose page tree it could not walk. `status()` returns `why`; every list is empty.
    static AnnotationStore refused(Error why);

    Status status() const;

    std::size_t size() const noexcept { return annotations_.size(); }
    const Annotation& at(std::size_t index) const { return annotations_.at(index); }
    const std::vector<Annotation>& all() const noexcept { return annotations_; }
    std::optional<std::size_t> find(const AnnotationId& id) const;
    std::optional<std::size_t> findObject(long long number) const;

    int pageCount() const noexcept { return static_cast<int>(pages_.size()); }
    const std::vector<AnnotationListingEntry>& listing(int page) const;
    // The annotations a page OWNS, in its listing order.
    std::vector<std::size_t> onPage(int page) const;
    // Each page's listing reduced to what D6-3 compares.
    std::vector<std::vector<AnnotationFact>> listingFacts() const;
    const std::vector<AnnotationNote>& notes() const noexcept { return notes_; }

    // --- Threads (D6-12) ---------------------------------------------------------------------
    // The parent a reply hangs under, or none: none for a top-level annotation AND for one whose
    // /IRT is missing, on another page, or in a cycle.
    std::optional<std::size_t> parentOf(std::size_t index) const;
    // /RT /R replies that are not state replies, oldest first.
    std::vector<std::size_t> replies(std::size_t index) const;
    // /RT /Group members, shown as one item with their parent.
    std::vector<std::size_t> groupMembers(std::size_t index) const;
    // An item the pane lists at the top: not a pop-up, not a state reply, not a reply or group member
    // under a parent that exists.
    bool isTopLevel(std::size_t index) const;
    std::vector<std::size_t> topLevel() const;

    // The Review model's history on an annotation, oldest first, and its latest state.
    std::vector<AnnotationStateChange> reviewHistory(std::size_t index) const;
    std::optional<AnnotationStateChange> currentReview(std::size_t index) const;
    // The Marked model's history -- a CHECKMARK is personal (`Features.md` §4.2), so the question is
    // asked per author: has `author`'s latest Marked-model reply the state `Marked`?
    std::vector<AnnotationStateChange> markedHistory(std::size_t index) const;
    bool isMarkedBy(std::size_t index, const QString& author) const;

    // --- Queries -------------------------------------------------------------------------------
    // Stage 6 T9 widened the filter to `Features.md` §4.2's five -- author, type, status, checkmark, date
    // range -- and the text to the replies under an item: a thread is found by what anyone in it wrote.
    struct Filter {
        std::vector<int> pages;                      // empty: every page
        std::vector<QString> authors;                // empty: every author
        std::vector<AnnotationSubtype> subtypes;     // empty: every subtype
        QString text;                                // in /Contents, /Subj or /T -- of the item or a reply
                                                     // under it, at any depth -- case-insensitive
        bool includeHidden = true;                   // Hidden and NoView (D6-27)
        // T9: the review status each item has NOW (`reviewStatus`): any of these, `None` for an item no
        // one has set one on. Empty: every status.
        std::vector<QString> statuses;
        // T9: checked (true) or unchecked (false) by `identity` -- a checkmark is personal (D6-12). None:
        // either.
        std::optional<bool> checked;
        QString identity;
        // T9: the instant an item is dated by (`/M`, else `/CreationDate`), in UTC seconds, both bounds
        // inclusive. An undated item is outside every range.
        std::optional<std::int64_t> from;
        std::optional<std::int64_t> to;
    };
    enum class SortKey { Page, Date, Author, Subtype };

    // Top-level items only, matching `filter`, sorted by `key` and then by document order.
    std::vector<std::size_t> query(const Filter& filter, SortKey key) const;
    // Every author of a top-level item, sorted.
    std::vector<QString> authors() const;

    // --- The Comments pane (Stage 6 T9, D6-12) -------------------------------------------------
    // GROUPED: `query`'s items, each in exactly one group, the groups in their key's natural order and the
    // items in each sorted by `sort`. A group's `key` is a stable value the pane words, never words:
    //   Page      -- the 0-based page, as a number (`3`), ascending;
    //   Author    -- `/T`, case-insensitively ascending, an item with none in a last group keyed empty;
    //   Date      -- the CALENDAR DAY of the item's instant at `utcOffsetSeconds` from UTC (`2026-09-01`),
    //                newest first, undated last under an empty key -- the offset is the host's to give
    //                (convention 11), so a case reads the same day on every machine;
    //   Type      -- the subtype's PDF name (`Highlight`), ascending;
    //   Status    -- `Accepted`, `Rejected`, `Cancelled`, `Completed`, then `None`;
    //   Checkmark -- `Marked` (by `filter.identity`) first, then `Unmarked`.
    // `None` is one group with an empty key holding every item.
    enum class GroupKey { None, Page, Author, Date, Type, ReviewStatus, Checkmark };
    struct Group {
        QString key;
        std::vector<std::size_t> items;
    };
    std::vector<Group> grouped(const Filter& filter, GroupKey group, SortKey sort, int utcOffsetSeconds = 0) const;

    // The review status an item has now: its latest Review-model state reply's `/State`, else `None`.
    QString reviewStatus(std::size_t index) const;

    // WHAT A PERSON DELETES WITH A COMMENT (A-71): the item, every reply under it at any depth, the group
    // members and state replies of each, and the pop-up of every one of them -- once each, in document order.
    // Walked iteratively, so a 10,000-deep thread is a loop.
    std::vector<std::size_t> threadOf(std::size_t index) const;

    // SEQUENCE NUMBERS FOR REFERENCE IN DISCUSSION (`Features.md` §4.2), shown and NEVER written (guide A-72):
    // one per top-level item, from 1, in READING order -- by page, then on each page top to bottom and left to
    // right AS THE PAGE IS SHOWN, `rotations[page]` (0, 90, 180, 270; missing means 0) turning its rectangle.
    // Parallel to `all()`; 0 for an item that is not numbered (a reply, a pop-up, a state reply).
    std::vector<int> sequenceNumbers(const std::vector<int>& rotations) const;

    // --- Changes (Stage 6 T3) ------------------------------------------------------------------
    // Called by a command's `apply()` and `revert()` and by nothing else (D6-1): the store is the
    // MODEL half of an annotation command, and `MutationSink`'s annotation intents are the other half,
    // named in the same placements so the two stay entry-for-entry the same listing.

    // Every listing of `id`, ascending by page and position. Empty for an id the store does not hold.
    std::vector<AnnotationPlacement> placementsOf(const AnnotationId& id) const;

    // Lists each annotation at its placement, IN ORDER -- each position counted in the listing as the
    // inserts before it left it. An annotation whose id the store already holds (or that an earlier
    // entry of the same call added) is listed AGAIN, and its value here is ignored: the store keeps one
    // annotation per dictionary. ALL OR NOTHING: refused, unchanged, for a refused store, a page or
    // position out of range, or an invalid id.
    Status insert(const std::vector<std::pair<AnnotationPlacement, Annotation>>& entries);

    // Removes the listing entry at each placement, IN ORDER; an annotation listed nowhere afterwards
    // leaves the store. All or nothing: refused, unchanged, for a placement out of range or one naming
    // an entry that is not an annotation -- an entry the model cannot name is not the model's to drop.
    Status remove(const std::vector<AnnotationPlacement>& placements);

    // REPLACES annotations IN PLACE, each by its id -- an edit (Stage 6 T5): every listing is left as it
    // is, and each entry naming a replaced annotation takes its new fact. ALL OR NOTHING: refused,
    // unchanged, for a refused store, an id it does not hold, one id twice, or a change to what an edit
    // cannot change -- the identity, the subtype, the owning page.
    Status replace(const std::vector<Annotation>& changed);

    // A SHA-256 of annotation `index`'s whole JSON -- every modelled field, the form A-20 proves complete --
    // for `Document::modelHash()`. CACHED per annotation: a digest is computed once and moves with its
    // annotation through `insert` and `remove`, and `replace` -- the one way an annotation changes in
    // place -- drops the digest of each annotation it replaces. Measured at Stage 6 T3: hashing `annots-900.pdf`'s 9,000 annotations' JSON took 78 ms,
    // and `CommandJournal::run` hashes the model before EVERY command -- one highlight took 80 ms.
    const QByteArray& annotationDigest(std::size_t index) const;

    // --- Exchange (Stage 6 T13, D6-20) -- defined in `core/doc/Fdf.cpp`, beside the exchange model -------------
    // WHAT AN IMPORT OF `exchange` INTO THIS DOCUMENT WILL DO -- added, replaced, kept, refused, and why (the merge
    // `core/doc/Fdf.h` states). Changes nothing: the caller hands `added` and `replaced` to ONE
    // `ImportAnnotationsCommand`. `check` is asked of every annotation the import would write; a refusal is that
    // annotation's, never the import's.
    FdfImport importFdf(const Fdf& exchange, const ImportCheck& check = {}) const;
    // THE ANNOTATIONS `items` NAME (indices into `all()`), EACH WITH ITS THREAD (`threadOf`), as an exchange carries
    // them -- by name, references by name, pages as indices -- in document order, once each. Only comments
    // (`isCommentSubtype`); a sound is left out and noted (its payload is not the model's). `sources` says where each
    // came from.
    Fdf exportFdf(const std::vector<std::size_t>& items) const;

private:
    void reindex();
    void buildThreads();
    std::vector<AnnotationStateChange> stateHistory(std::size_t index, const QString& model) const;

    std::optional<Error> refusal_;
    std::vector<Annotation> annotations_;
    mutable std::vector<QByteArray> digests_;    // parallel to annotations_; empty until asked for
    std::vector<std::vector<AnnotationListingEntry>> pages_;
    std::vector<AnnotationNote> notes_;          // the reading's notes, then the threads' (rebuilt)
    std::vector<AnnotationNote> readingNotes_;   // the reading's alone: what a change never recomputes
    std::map<long long, std::size_t> byObject_;
    // T9: a created identity found as fast as an object's -- `find` is asked for every reply's parent at every
    // rebuild of the threads, and a comment RawPage made is named by its `/NM`.
    std::map<QString, std::size_t> byCreated_;
    std::vector<std::optional<std::size_t>> parent_;
    std::vector<std::vector<std::size_t>> children_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_ANNOTATIONSTORE_H
