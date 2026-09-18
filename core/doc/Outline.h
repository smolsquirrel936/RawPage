// core/doc/Outline -- the document outline, what a reader calls bookmarks. Stage 4 T4; decision
// D4-3 (`Stage4-ViewingSurface-Plan.md` §3).
//
// READ-ONLY THIS STAGE. `ui/panes/BookmarksPane` navigates it (T6); renaming, re-parenting,
// deleting and new-from-view are Stage 7's, and they arrive with a first-party writer rather than
// with a second reader. Hydrated through the PDFium facade at open, once (guide case A-30).
//
// ---------------------------------------------------------------------------------------------
// THE OPEN FLAG IS A DOCUMENT'S REQUEST, AND IT IS NOT THE PANE'S STATE
// ---------------------------------------------------------------------------------------------
// `/Count` in an outline item says how the author wanted the item to appear when the document is
// first shown: positive means expanded, negative means collapsed (ISO 32000-1 Table 153). It is a
// STARTING state, not a live one -- once a person expands a node, that is the pane's business and
// belongs in the widget or in `SessionState`, never here. This model is what the file says.
//
// ---------------------------------------------------------------------------------------------
// A TREE FROM A FILE IS NOT NECESSARILY A TREE
// ---------------------------------------------------------------------------------------------
// `/First`, `/Next` and `/Parent` are just references, and a damaged or hostile file can point them
// in a circle. The reader is therefore bounded -- by depth and by total node count -- and says so
// where it reads, because a reader that walks a cycle does not fail: it hangs, with a progress bar,
// on a document that opens fine in every other reader.

#ifndef RAWPAGE_CORE_DOC_OUTLINE_H
#define RAWPAGE_CORE_DOC_OUTLINE_H

#include "core/doc/Destinations.h"

#include <QString>

#include <cstddef>
#include <vector>

namespace rawpage::core {

struct OutlineItem {
    QString     title;
    Destination destination;

    // What the FILE asks for, not what the pane is doing. See the header.
    bool openByDefault = false;

    std::vector<OutlineItem> children;
};

class Outline {
public:
    std::vector<OutlineItem>&       roots() noexcept { return roots_; }
    const std::vector<OutlineItem>& roots() const noexcept { return roots_; }

    bool isEmpty() const noexcept { return roots_.empty(); }

    // Every node, at every depth. A pane sizes itself from this; a case asserts the shape with it.
    std::size_t nodeCount() const;

    // 0 for an empty outline, 1 for a flat list. `outline-labels.pdf` is 2.
    int depth() const;

private:
    std::vector<OutlineItem> roots_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_OUTLINE_H
