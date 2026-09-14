// core/doc/PageTree -- the ordered page collection. Stage 2 T9 builds the READ half; Stage 3 adds
// insert / move / remove / duplicate (decision D2-5, and the same split `core/doc/Outline` uses).
//
// WHY THE READ HALF IS STAGE 2's: `core/render/PagePresentation` (T10) lays pages out before
// anything can move them, and `PagePresentation::layout(const PageTree&, …)` is a Stage 2
// signature in `Implementation-Plan.md`'s own component list. Building the whole thing at Stage 3
// would force Stage 2 to invent a parallel collection and then delete it.
//
// -------------------------------------------------------------------------------------------
// PAGE LABELS ARE NOT PAGE NUMBERS, AND THE DIFFERENCE IS USER-VISIBLE
// -------------------------------------------------------------------------------------------
// ISO 32000-1 12.4.2: a document may number its front matter i, ii, iii and then restart at 1.
// **Such a document has two pages that both call themselves "1"**, and a Go To Page box has to
// decide what the user meant. This class answers both questions separately and never conflates
// them: `indexOfLabel()` finds a label, `at()` takes an index, and a caller that wants "whichever
// the user probably meant" composes the two rather than getting a guess from here.
//
// The index is 0-based throughout `core/`. The one-based number a person sees is a presentation
// concern and belongs in `ui/`; mixing the two inside the model is how an off-by-one reaches the
// scroll position.
//
// PURE, AND NO I/O. Nothing here opens a file or knows what a dictionary is. `pdf/render/
// PdfiumDocument` builds one; this holds the shape.

#ifndef RAWPAGE_CORE_DOC_PAGETREE_H
#define RAWPAGE_CORE_DOC_PAGETREE_H

#include "core/doc/Page.h"

#include <QString>

#include <cstddef>
#include <vector>

namespace rawpage::core {

class PageTree {
public:
    PageTree() = default;
    explicit PageTree(std::vector<Page> pages);

    int count() const noexcept { return static_cast<int>(pages_.size()); }
    bool isEmpty() const noexcept { return pages_.empty(); }

    // Range-checked, because every caller of this is arithmetic that can be off by one -- a scroll
    // position, a thumbnail index, a tile's page. Returns nullptr rather than throwing or
    // asserting: an out-of-range page is an ordinary outcome when a document has just been
    // replaced under a pane that has not repainted yet.
    const Page* at(int index) const noexcept;

    const std::vector<Page>& pages() const noexcept { return pages_; }

    // The page a LABEL names, or -1. Searches in document order and returns the FIRST match, which
    // is the right answer for the two-page-1 case above: a document's second "1" is reached by
    // index, not by asking for the label again.
    int indexOfLabel(const QString& label) const;

    // Convenience for the two things every layout consumer asks. Both answer for an out-of-range
    // index with an empty size / no rotation rather than reading past the end.
    PdfSize displaySize(int index) const noexcept;
    PageRotation rotation(int index) const noexcept;

    // The largest page in the document, by area of its display size. `PagePresentation` (T10) uses
    // it to size a continuous scroll view once rather than growing the viewport as it discovers
    // wider pages, which is what makes a mixed-page-size document scroll without the layout
    // shifting under the cursor -- `corpus/ordinary/mixed-page-sizes.pdf` is the fixture.
    PdfSize largestDisplaySize() const noexcept;

    // THE ONE MUTATION STAGE 3 HAS (T4, decision D3-8). Sets page `index`'s rotation in the MODEL --
    // what the page will display at once the change is saved -- and returns false, changing nothing,
    // for an index outside the tree. Normalized as `Page::setRotation` normalizes: -90 is 270.
    //
    // It changes the model and nothing else. Which object that means in the file is `pdf/ops/`'s
    // (flow D), and the engine keeps the rotation it loaded, so an unsaved rotation is drawn by
    // composing the difference into the render request (D3-15, T8). The only code that calls it
    // holds a mutable `Document`, which is `CommandJournal` (D3-10).
    //
    // Insert / move / remove / duplicate are Stage 7's, with the commands that call them. They are
    // deliberately absent rather than present-and-refusing: this class has no `Result`, an
    // unimplemented mutation would have nowhere to report to, and a stub which silently succeeds is
    // worse than an absent method (CLAUDE.md convention 6).
    bool setRotation(int index, int degrees) noexcept;

private:
    std::vector<Page> pages_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_PAGETREE_H
