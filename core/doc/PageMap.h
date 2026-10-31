// core/doc/PageMap -- where every page went. Stage 7 T2, decision D7-3 (`Stage7-Assembly-Plan.md` §3).
//
// A page command does not only change pages: it MOVES them, and six stages of the product hold page
// INDICES -- the tile cache, the thumbnails, the text index, the annotation store, the find bar's hits,
// the view's current page, every selection. A consumer that is told only *"pages changed"* either drops
// everything (a 900-page re-render for a one-page move) or keeps an index that now names another page,
// which shows the right content on the wrong page and looks like nothing is broken at all (guide §1,
// the S1 that is new at this stage). So a page command returns, beside its `Scope`, THIS: for every
// page before the command, its index after it or *removed*; for every page after it, where it came from
// or *inserted*.
//
// IT IS ARITHMETIC AND NOTHING ELSE: no file, no engine, no page content -- an index in, an index out.
// `tst_pagemap` grades it against hand-written tables (guide A-18), and the two properties every
// consumer leans on are stated as operations here rather than left to each consumer: a map composed with
// its `inverse()` takes every page back to where it was -- the IDENTITY for a move; for a removal or a
// duplication, every page both keep is where it was, and a page one removed is the other's insertion at
// its old index, since a map names pages by place and a page that went has none -- and `then()` composes
// two commands' maps into the map of running both.
//
// THE THREE PAGE COMMANDS' MAPS ARE BUILT HERE, so a command and every consumer agree on what *"move
// pages 3 to 5 to index 6"* means -- one definition, not one per caller:
//   * `move(count, pages, to)` -- the moved pages keep their order among themselves and the first of them
//     lands at new index `to`; every other page keeps its order. `to` counts in the document AFTER the
//     move, so it runs from 0 to `count - pages.size()`.
//   * `removal(count, pages)` -- the pages go, the rest close up.
//   * `duplication(count, pages)` -- each page's copy lands immediately after it.
//   * `insertion(count, at, n)` -- Stage 7 T5: n pages from elsewhere land at `at`, the rest after them.
// Each refuses (nullopt) a page set that is empty, unsorted, repeated or out of range. A move that
// changes nothing returns the IDENTITY, which is how a caller knows to make no command (guide A-17).
//
// AND WHAT A PAGE COMMAND FOUND: `PageCommandReport`, the D7-2 list of every reference a command
// cleared, removed or unlinked, and every one it reported and left as written -- what the message bar
// says after a delete, and what guide A-20 grades. Built by `pdf/ops/` (only it can read an outline),
// carried in `core/` vocabulary: a kind, a treatment, and the words a person recognizes it by.
//
// Pure: QtCore and the standard library -- `rawpage_tests`, under `portable-asan` too.

#ifndef RAWPAGE_CORE_DOC_PAGEMAP_H
#define RAWPAGE_CORE_DOC_PAGEMAP_H

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace rawpage::core {

class PageMap {
public:
    // `newIndexOf` for a page the command removed; `oldIndexOf` for a page it inserted.
    static constexpr int kNone = -1;

    PageMap() = default;   // the identity over no pages

    static PageMap identity(int count);

    // `order[newIndex]` is the old index the page at `newIndex` came from, or `kNone` for a page the
    // command inserted. Nullopt when an old index is named twice or lies outside [0, oldCount).
    static std::optional<PageMap> fromOrder(int oldCount, std::vector<int> order);

    // The three page commands' maps -- see the header for what each means.
    static std::optional<PageMap> move(int count, const std::vector<int>& pages, int to);
    static std::optional<PageMap> removal(int count, const std::vector<int>& pages);
    static std::optional<PageMap> duplication(int count, const std::vector<int>& pages);
    // Stage 7 T5: `inserted` new pages landing at index `at` (0 ... `count`), every page from `at` on after them.
    // Refused for an `at` outside that range, or a count outside 1 ... `kMaxInserted`.
    static std::optional<PageMap> insertion(int count, int at, int inserted);
    static constexpr int kMaxInserted = 1'000'000;

    // A page SET as every page command takes one: non-empty, strictly ascending, inside [0, count).
    static bool isPageSet(int count, const std::vector<int>& pages);

    int oldCount() const noexcept { return oldCount_; }
    int newCount() const noexcept { return static_cast<int>(order_.size()); }

    // `kNone` for an index outside the map as well as for a removed or inserted page.
    int newIndexOf(int oldIndex) const noexcept;
    int oldIndexOf(int newIndex) const noexcept;
    const std::vector<int>& order() const noexcept { return order_; }

    std::vector<int> removed() const;    // OLD indices, ascending
    std::vector<int> inserted() const;   // NEW indices, ascending

    bool isIdentity() const noexcept;
    // Nothing removed, nothing inserted: only an order changed.
    bool isPermutation() const noexcept;

    // The map back: a removed page is inserted, an inserted one removed. `m.then(m.inverse())` keeps every
    // page at its own index -- the identity when `m` is a permutation -- with each page `m` removed an
    // insertion where it was.
    PageMap inverse() const;
    // This command, then `next`, as one map. `next.oldCount()` must equal `newCount()`; nullopt otherwise.
    std::optional<PageMap> then(const PageMap& next) const;

    friend bool operator==(const PageMap& a, const PageMap& b) noexcept
    {
        return a.oldCount_ == b.oldCount_ && a.order_ == b.order_;
    }

private:
    int oldCount_ = 0;
    std::vector<int> order_;     // new -> old, or kNone
    std::vector<int> forward_;   // old -> new, or kNone
};

// One reference a page command met -- D7-2. Stable, ASCII, non-localized names; `ui/` words them.
struct PageReference {
    enum class Kind : std::uint8_t {
        OutlineItem,        // a bookmark's /Dest or /A
        Link,               // a Link annotation's /Dest or /A on a page that stays
        NamedDestination,   // an entry of /Names /Dests or of the catalog's /Dests
        StructureElement,   // a structure element's content items on the page
        ThreadBead,         // an article thread's bead on the page
        OpenAction,         // the catalog's /OpenAction
        RemoteGoTo,         // a GoToR: a page NUMBER in another file -- never repaired from here
        IndexDestination,   // a local destination naming its page by integer -- a producer's error
        WidgetNotCopied,    // a duplicated page's form field widget: fields are D7-16's
    };
    enum class Treatment : std::uint8_t {
        Cleared,    // kept, its destination or action removed (an outline item, a link)
        Removed,    // gone (a named destination, /OpenAction, a structure content item)
        Unlinked,   // taken out of its chain (a bead)
        Reported,   // left exactly as written, and said so
        NotCopied,  // left behind on purpose
    };

    Kind kind = Kind::OutlineItem;
    Treatment treatment = Treatment::Reported;
    QString name;            // what a person knows it by: a bookmark's title, a destination's name, a type
    long long object = -1;   // the object that holds it, for a log; -1 when it is direct in another
    int page = -1;           // the page index it named, BEFORE the command; -1 when it names none here

    bool operator==(const PageReference&) const = default;
};

struct PageCommandReport {
    std::vector<PageReference> references;

    bool isEmpty() const noexcept { return references.empty(); }
    bool operator==(const PageCommandReport&) const = default;
};

// What `MutationSink::duplicatePages` answers: each copy's page label, in order, and what it met.
struct DuplicatedPages {
    std::vector<QString> labels;
    PageCommandReport report;
};

const char* pageReferenceKindName(PageReference::Kind kind) noexcept;
const char* pageReferenceTreatmentName(PageReference::Treatment treatment) noexcept;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_PAGEMAP_H
