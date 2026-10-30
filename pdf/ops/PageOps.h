// pdf/ops/PageOps -- pages as COS objects: which object IS page n, and what writing a rotation onto
// it means. Stage 3 T3 -- rotation only; insert, move, remove and duplicate are Stage 7's (D3-8).
//
// -------------------------------------------------------------------------------------------
// THE WALK: which object is page n
// -------------------------------------------------------------------------------------------
// A rotation of "page 3" has to land on the object the ENGINE drew as page 3, or the product saves
// a rotation onto a page nobody saw. So the walk is written to agree with PDFium rather than with
// the specification's happy path, and `tests/render/tst_pageopsengine` holds it to that over every
// corpus file both readers open (guide case A-29). Four rules, each a corpus file:
//
//   * **`/Count` is never read.** It is a claim; `malformed/pages-count-wrong.pdf` says 97 over three
//     real kids. Pages are what the walk reaches.
//   * **A node with a `/Kids` array is an intermediate node; any other reachable dictionary is a
//     page.** The same test PDFium applies -- a missing `/Type` does not hide a leaf.
//   * **A node already visited is skipped, and noted.** `malformed/pages-loop.pdf` lists an
//     ancestor as a kid; without a visited set the walk does not terminate.
//   * **The walk is iterative** with an explicit stack, and stops descending past `kMaxDepth` --
//     the tree is input, and `pages-900.pdf` is only two levels deep because a generator chose it.
//
// INHERITANCE is resolved on the way DOWN, not by following `/Parent` back up: the parent chain is
// another claim a damaged file can get wrong, while the path the walk took to a page is a fact. Only
// the attributes a reader of the walk needs are carried -- `/Rotate` and `/MediaBox` (Stage 3), and
// `/CropBox` since Stage 7 T1, so the backend can compare the box each reader says is DRAWN (Stage 7
// guide I-01: `corpus/assembly/inherit-resources.pdf` inherits both boxes).
//
// -------------------------------------------------------------------------------------------
// WRITING A ROTATION: D3-5, and one refinement of it
// -------------------------------------------------------------------------------------------
// `Stage3-Mutation-Plan.md` D3-5: a rotation is written on the PAGE DICTIONARY, never on an ancestor
// (which would rotate its siblings). `rotatedPage()` returns the page's dictionary with its own
// `/Rotate` set -- or with it REMOVED when the desired value equals what the page would inherit
// anyway. The plan removes it only when the on-disk dictionary had none; T3 drops that condition,
// because after a save and an undo the literal rule would leave an explicit `/Rotate 90` on a page
// whose original dictionary had no key, while this rule writes back the dictionary the page started
// with. Both are correct renderings; this one is the smaller and the reversible one, and the
// deviation is recorded in `Implementation-Plan.md` § Stage 3's T3 row.
//
// ROTATION VALUES follow PDFium, not a cleaner rule: `/Rotate` is truncated to quarter turns and
// normalized into 0 / 90 / 180 / 270, so a damaged `/Rotate 45` reads as 0 here exactly as it draws.
//
// -------------------------------------------------------------------------------------------
// THE PAGE ORDER -- Stage 7 T2 (D7-2, D7-3)
// -------------------------------------------------------------------------------------------
// `shape()` reads the tree's NODES as well as its leaves, and `composeTree()` lists a new order in it with
// the least it can change: the tree's shape is kept; the longest run of pages already in document order
// STAYS where it is (a delete moves nothing, and moving one page re-lists that page alone); every other
// page -- moved, new, or back after a save removed it -- is listed right after the page before it in the
// order; and a node left with no page leaves its parent. Only a node whose `/Kids` changed is rewritten, its `/Count` only where its number of
// pages changed, and a page only where its `/Parent` did -- guide A-24's *"only the page-tree nodes whose
// /Kids changed"*. A page that moves under another parent would INHERIT from that parent (PDFium resolves
// `/Resources`, `/MediaBox`, `/CropBox` and `/Rotate` up `/Parent`), so `inheritedValues()` says what each
// node passes down and the sink materializes, on such a page, each of the four that would change.
// Page structure is changed only in a tree the walk read without a note: a loop, a kid that is no
// reference or does not fetch, or one past the depth bound is a tree whose shape is a guess.
//
// PAGE LABELS (ISO 32000-1 §12.4.2) are a number tree keyed by page INDEX, so a move that kept them would
// re-label every page it shifted. `readLabels()` states each page's label as the range that numbers it
// would -- style, prefix, number -- and `labelTree()` writes the ranges back for any order, so each page
// keeps the label it had (D7-2) and a copy takes the next number of its range (guide A-21). Both follow
// PDFium, which is what the model's labels came from: a page no range covers is labelled with its own
// 1-based number, and `formatLabel()` spells roman numerals and letters as PDFium does.

#ifndef RAWPAGE_PDF_OPS_PAGEOPS_H
#define RAWPAGE_PDF_OPS_PAGEOPS_H

#include "core/Result.h"
#include "pdf/cos/CosObject.h"

#include <QString>

#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace rawpage::pdf::cos {
class XrefTable;
}

namespace rawpage::pdf::ops {

struct PageEntry {
    long long number = 0;                    // the page dictionary's object number
    std::optional<int> ownRotate;            // this dictionary's /Rotate, normalized; absent if none
    int inheritedRotate = 0;                 // what an ancestor supplies, normalized; 0 if none
    int effectiveRotate() const noexcept { return ownRotate.value_or(inheritedRotate); }
    std::optional<std::array<double, 4>> mediaBox;   // own or inherited /MediaBox, as written
    std::optional<std::array<double, 4>> cropBox;    // own or inherited /CropBox, as written
};

enum class PageWalkNote {
    NodeVisitedTwice,     // a loop, or one node listed under two parents -- skipped
    KidNotAReference,     // a /Kids item that is not an indirect reference -- skipped
    KidUnreadable,        // a /Kids reference that does not fetch to a dictionary -- skipped
    DepthLimitExceeded,   // the tree is deeper than kMaxDepth -- the subtree is skipped
};

struct PageWalkNoteRecord {
    PageWalkNote note = PageWalkNote::NodeVisitedTwice;
    long long objectNumber = -1;
};

struct PageWalk {
    std::vector<PageEntry> pages;
    std::vector<PageWalkNoteRecord> notes;
};

// The page tree's nodes (Stage 7 T2). `nodes[0]` is the root; a kid is a page (`leaf`) or a node.
struct PageTreeNode {
    struct Kid {
        bool leaf = true;
        long long number = 0;
        int node = -1;            // a node kid: its index in `nodes`
    };
    long long number = 0;
    int parent = -1;              // index in `nodes`; -1 for the root
    std::vector<Kid> kids;        // as written
    int leaves = 0;               // pages beneath it
};

struct PageTreeShape {
    std::vector<PageTreeNode> nodes;
    std::map<long long, int> parentOf;   // page object -> its node's index
};

// What a new order means for the tree's nodes. Only what changed.
struct ComposedTree {
    std::map<long long, std::vector<cos::CosObject>> kids;   // node object -> its new /Kids
    std::map<long long, long long> counts;                    // node object -> its new /Count
    std::map<long long, long long> parents;                   // every listed page -> its node's object
};

// One page's label as the range that numbers it states it (Stage 7 T2). `style` empty: no numeric part.
struct PageLabelRange {
    std::string style;                        // "D", "R", "r", "A", "a" or ""
    std::optional<cos::CosObject> prefix;     // /P as written
    long long value = 1;                      // this page's number in its range
    bool operator==(const PageLabelRange& other) const
    {
        return style == other.style && prefix == other.prefix && value == other.value;
    }
};

class PageOps {
public:
    static constexpr int kMaxDepth = 256;

    // Every page, in document order. Fails only when there is no page tree to walk at all -- no
    // `/Root`, no `/Pages`, or a `/Pages` that is not a dictionary. An empty tree is not a failure.
    static core::Result<PageWalk> walk(cos::XrefTable& table);

    // A `/Rotate` value as PDFium reads it: quarter turns, truncated, normalized to 0..270.
    static int normalizeRotate(const cos::CosObject& value) noexcept;

    // The dictionary to write so that `page` displays at `degrees` (0, 90, 180 or 270), built from
    // the page's CURRENT dictionary in `table`. Nullopt when the page already displays at `degrees`
    // -- nothing to write. `degrees` must already be normalized; anything else is InvalidArgument.
    static core::Result<std::optional<cos::CosObject>> rotatedPage(cos::XrefTable& table,
                                                                  const PageEntry& page,
                                                                  int degrees);

    // --- The page order (Stage 7 T2) -- see the header -----------------------------------------
    // The tree's nodes and where each page hangs. Fails as `walk` fails, and for a tree the walk noted
    // anything about (`error.pdf.pageTreeDamaged`): its shape would be a guess.
    static core::Result<PageTreeShape> shape(cos::XrefTable& table);
    // The four inheritable keys each node passes down -- its own, else its parent's -- parallel to `nodes`.
    static std::vector<std::map<std::string, cos::CosObject>> inheritedValues(cos::XrefTable& table,
                                                                             const PageTreeShape& shape);
    static constexpr std::array<const char*, 4> kInheritable{"Resources", "MediaBox", "CropBox", "Rotate"};
    // The tree listing `order` -- page objects; those in `fresh` are new pages, listed after the page before
    // them. Every page of `order` not in `fresh` must be a page of `shape`.
    static ComposedTree composeTree(const PageTreeShape& shape, const std::vector<long long>& order,
                                    const std::set<long long>& fresh);

    // Each of `pageCount` pages' labels as the catalog's /PageLabels states them; EMPTY when there is no
    // /PageLabels at all -- then no label is written, and none is shown.
    static std::vector<PageLabelRange> readLabels(cos::XrefTable& table, int pageCount);
    // The label PDFium shows for a range: the prefix, then the number in its style.
    static QString formatLabel(const PageLabelRange& label);
    // The /PageLabels number tree that gives each page `labels[i]` -- one range per run that counts on.
    static cos::CosObject labelTree(const std::vector<PageLabelRange>& labels);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_PAGEOPS_H
