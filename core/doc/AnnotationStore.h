// core/doc/AnnotationStore -- every annotation of an open document, its threads, its states and the
// queries the Comments pane is a view of. Stage 6 T2 builds the READ side (decisions D6-3, D6-12,
// `Stage6-Annotations-Plan.md` §3); the commands that change it arrive from T3.
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
#include "core/doc/Permissions.h"

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
    struct Filter {
        std::vector<int> pages;                      // empty: every page
        std::vector<QString> authors;                // empty: every author
        std::vector<AnnotationSubtype> subtypes;     // empty: every subtype
        QString text;                                // in /Contents, /Subj or /T, case-insensitive
        bool includeHidden = true;                   // Hidden and NoView (D6-27)
    };
    enum class SortKey { Page, Date, Author, Subtype };

    // Top-level items only, matching `filter`, sorted by `key` and then by document order.
    std::vector<std::size_t> query(const Filter& filter, SortKey key) const;
    // Every author of a top-level item, sorted.
    std::vector<QString> authors() const;

private:
    void buildThreads();
    std::vector<AnnotationStateChange> stateHistory(std::size_t index, const QString& model) const;

    std::optional<Error> refusal_;
    std::vector<Annotation> annotations_;
    std::vector<std::vector<AnnotationListingEntry>> pages_;
    std::vector<AnnotationNote> notes_;
    std::map<long long, std::size_t> byObject_;
    std::vector<std::optional<std::size_t>> parent_;
    std::vector<std::vector<std::size_t>> children_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_ANNOTATIONSTORE_H
