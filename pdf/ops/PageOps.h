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

#ifndef RAWPAGE_PDF_OPS_PAGEOPS_H
#define RAWPAGE_PDF_OPS_PAGEOPS_H

#include "core/Result.h"
#include "pdf/cos/CosObject.h"

#include <array>
#include <cstddef>
#include <optional>
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
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_PAGEOPS_H
