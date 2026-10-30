// The Page Thumbnails pane -- `Features.md` §5.2, Stage 4 T5, decisions D4-2 and D4-10.
//
// ===========================================================================================
// IT IS A VIEWPORT OVER THE DOCUMENT, NOT A VIEW OF THE DOCUMENT. THIS IS THE WHOLE DESIGN.
// ===========================================================================================
// A 900-page document has 900 thumbnails and this pane asks for **the slots it can show plus one
// screen of margin**, never the document. D4-10 is explicit about why, and the reason is not
// memory: `RenderScheduler` runs ONE worker behind PDFium's process-wide engine lock (I-05, Stage 2
// T3/T11), so 900 speculative rasterizations queued at once are 900 jobs in front of the next tile
// the person reading the page actually needs. Strict priority keeps the viewport first -- every
// request this pane makes is `RenderPriority::Speculative`, which `RenderScheduler.h` declared at
// Stage 2 with the comment *"a page the user has not reached; Stage 4's thumbnails"* and which had
// no product caller until this file -- but priority orders a queue, it does not shorten it, and a
// queue that long spends the shared 256 MiB cache (open decision **O7**) on pages nobody is
// looking at. The pane's own scroll position is the natural bound, so it is the bound used.
//
// Stage 4 T2 measured the cost before this file existed, which is what D4-16 and the task ordering
// were for: with this stream running, the viewport's p95 is 10.59-10.74 ms against a 16.667 ms
// budget, and cross-document evictions stay at 0 at the shipped cap. The verdict is
// `go, with named custom work`, and the named work is **T11's**, not this pane's -- so nothing here
// is shaped by a no-go that did not happen.
//
// ===========================================================================================
// NO SECOND RASTERIZER, NO SECOND CACHE, NO SECOND THREAD.
// ===========================================================================================
// The pane holds none of the three. It builds a `RenderRequest` for a whole page at thumbnail
// scale and hands it to the same `RenderScheduler` the viewer uses, so a thumbnail and a tile
// compete in one queue against one cap and the figures T2 measured are the figures that apply. A
// pane that rasterized its own pages would be a second `FPDF_` caller, which `layer-lint` refuses
// by construction, and a pane with its own cache would fragment the budget **O7** resolved to keep
// whole.
//
// ===========================================================================================
// IT MUTATES NOTHING -- D4-1, AND THE PROOF IS MECHANICAL.
// ===========================================================================================
// `Ctrl+R` in this pane rotates pages, and this file constructs nothing to do it: the pane declares
// `KeyContext::PageThumbnails`, `ShortcutBinder` resolves the chord against §8's Page Thumbnails
// table, and `page.rotate` -- **Stage 3's** registration, unchanged -- does the work through
// `CommandJournal`. `mutation-lint` reports one command on the last day of Stage 4 as it did on the
// first, and that number is this stage's evidence that a viewing surface stayed one.
//
// The page operations in §8's table that WOULD need a command -- delete, cut, copy, paste, move,
// extract, duplicate -- are Stage 7's and are deliberately unregistered, so their keys reach this
// widget and are left alone rather than swallowed.
//
// ===========================================================================================
// SELECTION FOLLOWS THE VIEW; THE VIEW DOES NOT FOLLOW IT BACK. ONE DIRECTION PER GESTURE.
// ===========================================================================================
// Plan §5.3's last line, and the one thing about this pane that is a bug rather than a rough edge
// if it is got wrong: a pane whose selection scrolled the view, whose scroll then moved the
// selection, is a scroll that fights the user's hand. The rule is structural rather than guarded by
// a flag -- **`activated()` is emitted only by a GESTURE** (a click, or `page.goToSelected` from a
// key), and `setCurrentPage()` is the programmatic path and emits nothing at all. There is
// therefore no state in which the two can chase each other, and no flag whose window can be missed.
//
// ===========================================================================================
// SINGLE SELECTION, SAID OUT LOUD (convention 19).
// ===========================================================================================
// `Features.md` §5.2 asks for multi-select with click, shift-click and ctrl-click. This pane
// selects ONE page, because at Stage 4 a selection has nothing to act on: every page operation is
// Stage 7's, `Ctrl+A` (`page.selectAll`) is unregistered, and `Ctrl+R` rotates the current page or
// all pages rather than a set -- which the guide's own out-of-scope table states. A multi-selection
// with no operation that reads it would be a control that reads back as set and does nothing,
// which is Stage 2's **I-11** built by choice rather than met as a constraint (D4-2's argument).
// Stage 7 adds the set with the operations that need it.

#ifndef RAWPAGE_UI_PANES_THUMBNAILSPANE_H
#define RAWPAGE_UI_PANES_THUMBNAILSPANE_H

#include "core/backend.h"
#include "core/doc/Revision.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "ui/viewer/HeldRaster.h"

#include <QAbstractScrollArea>
#include <QRect>

#include <unordered_map>
#include <vector>

namespace rawpage::core {
class PageTree;
}

namespace rawpage::ui {

class ThumbnailsPane : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit ThumbnailsPane(core::render::RenderScheduler& scheduler, QWidget* parent = nullptr);
    ~ThumbnailsPane() override;

    // The document to show. `pages` is the MODEL's tree -- the one whose rotations include unsaved
    // ones -- and `enginePages` is what the engine loaded; the difference travels on every request
    // as `RenderRequest::pageRotationDelta` (D3-15), exactly as `TilePresenter` carries it, so a
    // rotation the user has not saved is visible in the thumbnail without the engine being touched.
    //
    // Passing an invalid handle with null trees is how the pane is emptied, which is what a window
    // with no document in front does.
    void setDocument(core::DocumentHandle document, core::Revision revision,
                     const core::PageTree* pages, const core::PageTree* enginePages = nullptr);

    // The model changed in place -- a rotation -- and is now at `revision`. Unlike `setDocument`,
    // this keeps the selection and the scroll position: the document did not arrive, it moved.
    void modelChanged(core::Revision revision);

    // S7 T3 (D7-3): a page command moved pages -- called before `modelChanged`. The current page follows its
    // page; the pane is the second of `Stage7-Assembly-Testing-Guide.md` A-31's "both panes' selections".
    void remapPages(const core::PageMap& map);

    core::DocumentHandle document() const noexcept { return document_; }
    int pageCount() const;

    int currentPage() const noexcept { return currentPage_; }

    // THE PROGRAMMATIC PATH. Moves the selection, scrolls it into view, and emits NOTHING -- see
    // the header on why that is the whole of the loop prevention.
    void setCurrentPage(int pageIndex);

    // `+` / `-`, through `page.thumbnailsLarger` / `page.thumbnailsSmaller`. The width is in
    // LOGICAL pixels; the raster scale is derived per page from it and the device pixel ratio.
    static constexpr int kMinThumbnailWidth = 64;
    static constexpr int kMaxThumbnailWidth = 320;
    static constexpr int kThumbnailWidthStep = 32;

    int  thumbnailWidth() const noexcept { return thumbnailWidth_; }
    void setThumbnailWidth(int width);
    void larger();
    void smaller();

    // `Enter`, through `page.goToSelected`, and what a click does. Emits `activated()`.
    void activateCurrentPage();

    // ---- What a case can assert, rather than a screenshot ------------------------------------
    // THE SLOTS THE LAYOUT ACTUALLY SHOWS, which is the number convention 12 requires a case to
    // find non-zero before it asserts anything about what is visible. A pane that was never laid
    // out reports an empty range here and every "only the visible slots were enqueued" assertion
    // below it would pass vacuously.
    std::vector<int> visiblePages() const;

    // Every page this pane currently holds an interest in: a live ticket or a delivered thumbnail.
    // This is A-32's census -- it must not grow with the document's page count.
    std::vector<int> requestedPages() const;

    std::size_t pendingTicketCount() const;
    std::size_t heldThumbnailCount() const;

    // The request a given page is being rendered with, for a case that needs to read its priority
    // or its rotation delta rather than infer them.
    const core::render::RenderRequest* requestForPage(int pageIndex) const;

    // Forces the geometry and the request set to agree with the widget's current size. Public for
    // convention 12's reason: a case drives `show()` and `activate()` itself and then calls this,
    // because offscreen there is no event loop to deliver the resize that would.
    void refresh();

signals:
    // A GESTURE asked to go to this page. Never emitted by `setCurrentPage()`.
    void activated(int pageIndex);

    // At least one thumbnail arrived; the pane repainted itself already. A signal so a case can
    // wait on delivery without polling, as `TilePresenter::tilesArrived` does for the viewer.
    void thumbnailsArrived();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

    // THE ARROWS ONLY. `KeyBindingDefaults.def` records `Up`, `Down`, `Left` and `Right` in this
    // context as `BindingKind::WidgetNative` -- *"moves the selection; the item view owns it"* --
    // so `ShortcutBinder` deliberately does not dispatch them and this widget is the owner it
    // names. Every other row in §8's Page Thumbnails table goes through the registry, including
    // `Enter`, `+` and `-`: handling those here as well would give them a second implementation
    // that the ribbon, the tool finder and the Stage 14 batch runner could never reach.
    void keyPressEvent(QKeyEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;

    // A PANE NOBODY CAN SEE ASKS FOR NOTHING. An auto-hidden or closed sidebar is the ordinary
    // case, not an edge one, and a pane that went on streaming from behind a collapsed strip would
    // spend the one worker and the shared cache on pixels that reach no screen.
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Slot {
        int    pageIndex = 0;
        QRect  rect;        // in CONTENT space: the full scrolled column, origin at its top
        QRect  imageRect;   // the sheet inside it, above the label
    };

    struct Entry {
        core::render::RenderRequest request;
        core::render::RenderTicket  ticket;
        HeldRaster                  thumbnail;
    };

    void rebuildSlots();
    void updateScrollBar();
    void requestVisible();
    void cancelAll();
    void dropEntry(int pageIndex);
    core::render::RenderRequest requestFor(int pageIndex) const;

    // The visible content range, and the same range grown by one screen on each side -- the margin
    // §5.4 names. Both in slot indices; `{-1, -1}` when there is nothing to show.
    std::pair<int, int> visibleRange() const;
    std::pair<int, int> requestRange() const;

    core::render::RenderScheduler& scheduler_;

    core::DocumentHandle  document_;
    core::Revision        revision_;
    const core::PageTree* pages_ = nullptr;
    const core::PageTree* enginePages_ = nullptr;

    std::vector<Slot> slots_;
    int               contentHeight_ = 0;

    std::unordered_map<int, Entry> entries_;   // by page index

    int thumbnailWidth_ = 128;
    int currentPage_ = 0;

    core::render::RenderScheduler::SubscriptionToken subscription_ = 0;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_THUMBNAILSPANE_H
