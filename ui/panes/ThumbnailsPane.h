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
// STAGE 7 T8 KEPT IT THAT WAY (`Stage7-Assembly-Plan.md` D7-13). Every §8 row that changes the document --
// delete, cut, copy, paste, move earlier and later, duplicate, extract, rotate -- is an action `MainWindow`
// registers, which reads this pane's SELECTION and runs the command; and a DRAG, which is a gesture no chord
// names, ends here as a REQUEST -- `movePagesRequested` within the document, `pagesDropped` from another
// window's pane -- that the window turns into one `MovePagesCommand` or one `InsertPagesCommand`. This file
// still constructs nothing that changes a document.
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
// THE SELECTION IS A SET -- Stage 7 T8 (D7-13; guide A-53). Stage 4 selected ONE page (D4-2:
// a set with nothing to act on would be Stage 2's I-11 built by choice), and this stage brought the
// operations that read one.
// ===========================================================================================
// A FILE MANAGER'S RULES, because that is what a person's hands already know (M-01): a click
// selects one page and moves the ANCHOR there; `Ctrl`+click adds or removes one page and moves the
// anchor; `Shift`+click selects anchor ... page, replacing the set (`Ctrl+Shift` adds the run);
// an arrow moves the CURRENT page and selects it alone, `Shift`+arrow extends from the anchor.
// The current page is the keyboard's place, drawn with a focus frame; the selected pages are
// filled.
//
// ONLY A PLAIN CLICK NAVIGATES. `Ctrl`+click and `Shift`+click build a set and leave the page view
// where it is -- a set built across a 900-page document would otherwise drag the view to every
// page it touched. A plain press on a page ALREADY IN a set of several keeps the set until the
// release, so the set can be dragged; a release with no drag makes it that page alone.
//
// THE VIEW STILL LEADS A SINGLE SELECTION: `setCurrentPage` (the view's page, silently) makes a
// selection of one page follow the view as Stage 4's did, and leaves a set of several alone --
// scrolling to look at the pages a person picked must not unpick them. A page command REMAPS the
// set through D7-3's map (`remapPages`): the same pages, wherever they went; a deleted page leaves
// it, and a set left empty becomes the page that took the current page's place.
//
// ===========================================================================================
// A DRAG CARRIES PAGE NUMBERS, NEVER PAGES (D7-13).
// ===========================================================================================
// The drag's data is `kDragMimeType`: this process's id, the source document's handle and the
// page set. A drop on the SAME document's pane is a move; a drop on another window's pane of the
// same process is an insert, and the window copies the pages out of the source document AS IT
// STANDS at the drop (`AssemblySource::copy`) -- never at the press, which would pay a copy for
// every drag that ends as a move. A drag from another process carries a handle that means nothing
// here and is refused; between processes, pages travel on the clipboard (`page.copy`).
//
// WHERE IT LANDS IS WHERE THE MARKER SAID (M-01). The insertion point is the gap nearest the
// pointer, drawn as a bar across the column while the drag is over the pane, and it is the index
// in the document BEFORE the drop: `n` means "before page n", the page count means "at the end".
//
// ===========================================================================================
// BADGES (`Features.md` §5.2: *show which pages carry comments, form fields, or links*).
// ===========================================================================================
// Three marks along the bottom of each sheet, handed in by the window from the annotation store
// (`AnnotationStore::carries`) whenever the model changes; the pane reads nothing itself.

#ifndef RAWPAGE_UI_PANES_THUMBNAILSPANE_H
#define RAWPAGE_UI_PANES_THUMBNAILSPANE_H

#include "core/backend.h"
#include "core/doc/AnnotationStore.h"
#include "core/doc/Revision.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "ui/viewer/HeldRaster.h"

#include <QAbstractScrollArea>
#include <QRect>

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

class QMimeData;

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

    // THE PROGRAMMATIC PATH. Moves the current page, scrolls it into view, and emits NOTHING -- see
    // the header on why that is the whole of the loop prevention. A selection of one page follows
    // it; a set of several stays (S7 T8).
    void setCurrentPage(int pageIndex);

    // ---- The set -- Stage 7 T8 (D7-13) --------------------------------------------------------
    // Zero-based, strictly ascending; empty only for an empty document or after a `Ctrl`+click
    // took the last page out.
    const std::vector<int>& selectedPages() const noexcept { return selected_; }
    // The pages a command means: the set, or the current page alone when the set is empty.
    std::vector<int> commandPages() const;
    // Programmatic, as `setCurrentPage` is: emits `selectionChanged` and nothing else. Pages
    // outside the document are dropped; the current page moves to the set's first.
    void setSelectedPages(std::vector<int> pages);
    // `Ctrl+A`, through `page.selectAll`.
    void selectAll();
    int anchorPage() const noexcept { return anchor_; }

    // ---- Badges ---------------------------------------------------------------------------------
    void setBadges(std::vector<core::AnnotationStore::PageCarries> badges);
    core::AnnotationStore::PageCarries badgesOf(int pageIndex) const;
    // Where page `pageIndex`'s badges are painted, in VIEWPORT coordinates -- empty when it has none
    // or is not laid out. For a case that grades the paint, not only the data.
    QRect badgeRect(int pageIndex) const;

    // ---- Dragging -------------------------------------------------------------------------------
    // The drag's data: this process, the document's handle, the pages. Exposed because a drag's
    // SOURCE half is `QDrag::exec`, a nested event loop a headless case cannot drive; the drop half
    // is driven through real drag events (convention 19 -- what that leaves to M-01).
    static constexpr const char* kDragMimeType = "application/x-rawpage-page-drag";
    QMimeData* dragData() const;
    // The insertion index a drop at `viewportPos` lands at: before page n, or the page count for the
    // end. -1 with no document.
    int insertionIndexAt(QPoint viewportPos) const;
    // While a drag is over the pane, the marker's index; -1 otherwise.
    int dropMarker() const noexcept { return dropIndex_; }

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

    // S7 T8: the set changed -- by a gesture, `selectAll`, `setSelectedPages` or a remap. The
    // window re-asks its actions, whose enablement reads the set.
    void selectionChanged();

    // S7 T8: a drag ended on THIS document's pane -- `pages` to land before page `insertionIndex`
    // (indices before the move). The window makes it one `MovePagesCommand`, or nothing for a drop
    // where the pages already are.
    void movePagesRequested(const std::vector<int>& pages, int insertionIndex);
    // ... or on this pane from ANOTHER document of this process: `source`'s `pages`, to be copied in
    // before page `insertionIndex` -- one `InsertPagesCommand`, the source unchanged.
    void pagesDropped(core::DocumentHandle source, const std::vector<int>& pages, int insertionIndex);

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
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

    // S7 T8: the drop half of a drag (D7-13), delivered by `QAbstractScrollArea` from the viewport.
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    bool viewportEvent(QEvent* event) override;   // the badges' tooltip

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

    // S7 T8 -- the set, its anchor, and a press that may become a drag.
    struct Drag {
        QPoint origin;           // viewport coordinates of the press
        int page = -1;           // the page pressed
        bool collapse = false;   // a plain press inside a set of several: the set becomes `page` on release
    };
    void select(std::vector<int> pages);           // replaces the set, emits when it changed
    void pressPage(int page, Qt::KeyboardModifiers modifiers);
    int slotAt(QPoint viewportPos) const;
    std::optional<std::pair<core::DocumentHandle, std::vector<int>>> readDrag(const QMimeData* mime) const;
    void paintBadges(QPainter& painter, int pageIndex, const QRect& imageRect) const;

    std::vector<int> selected_;
    int anchor_ = 0;
    std::optional<Drag> press_;
    int dropIndex_ = -1;
    std::vector<core::AnnotationStore::PageCarries> badges_;

    core::render::RenderScheduler::SubscriptionToken subscription_ = 0;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_THUMBNAILSPANE_H
