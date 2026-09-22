// The Bookmarks pane -- `Features.md` §5.4 and §8's Bookmarks table, Stage 4 T6, decisions D4-2
// and D4-3.
//
// ===========================================================================================
// IT NAVIGATES. IT DOES NOT EDIT, AND THE ABSENCE IS THE DESIGN RATHER THAN A GAP.
// ===========================================================================================
// `core/doc/Outline` arrived at T4 as a **read-only half** (D4-3): the tree is hydrated once, at
// open, through the PDFium facade, because *we read through PDFium and write through our own code*
// and a second first-party walk would be a second reader to keep in agreement. The editing half --
// rename, delete, re-parent, new-from-view, set-destination, and the generators -- is **Stage 7's**,
// and arrives with the `BookmarkCommands` that make each of them undoable.
//
// So of §8's thirteen Bookmarks rows this pane services **five**: `Enter`, `→`, `←`, `*` and `/`.
// The other eight stay unregistered, which means `ShortcutBinder` answers `NotImplemented` and
// **leaves the key unconsumed** -- the state D4-1 requires and the one a case can assert. A handler
// that did nothing would be indistinguishable from one that is broken (Stage 2's **I-11**), and a
// pane that could rename a bookmark would be a `Commands.def` row this stage has decided not to
// spend.
//
// ===========================================================================================
// A `QTreeWidget`, AND WHY THE ARROWS ARE NOT ITS BUSINESS HERE.
// ===========================================================================================
// `ui/panes/ThumbnailsPane` owns its own geometry because it is a viewport over a render stream;
// a bookmark tree is an item view and `QTreeWidget` is what an item view is for -- expansion state,
// keyboard walking, elision, and the accessible focus ring, none of which is worth reimplementing.
//
// But the four arrows are NOT handled the way the thumbnails pane handles them, and the difference
// is §8's rather than ours. `KeyBindingDefaults.def` records `Up`/`Down`/`Left`/`Right` in
// `PageThumbnails` as `BindingKind::WidgetNative` -- *the item view owns it* -- while in
// `Bookmarks` it binds `Right` and `Left` to real commands, `bookmark.expand` and
// `bookmark.collapse`. So here the binder CONSUMES them and dispatches, and the tree never sees
// them; `Up` and `Down` appear in no Bookmarks row at all, resolve to nothing, and reach the tree,
// which walks the selection natively. Three behaviours, one table, and none of them decided in this
// file.
//
// ===========================================================================================
// WHAT NAVIGATING A DESTINATION DOES, AND WHAT IT STILL DOES NOT (convention 19).
// ===========================================================================================
// It hands a `core::Destination` up, and `ui/viewer/PageView::goToDestination` applies the fit --
// the page, and then `/XYZ`'s point and zoom, `/FitH`'s top, `/FitV`'s left, `/Fit`'s whole page and
// `/FitB`'s content box, with a null in any of them meaning *keep what you have*.
//
// **T6 SHIPPED THIS PANE GOING TO THE PAGE ALONE, AND SAID SO HERE.** That was **I-05**
// (`Stage4-ViewingSurface-Testing-Guide.md` §8), open for two days and **closed at T12**: turning a
// destination's PDF-space point into a scroll offset needs the page's slot rectangle and the
// resolution that produced it, which is the arithmetic T12 built for Fit Visible (D4-14), and
// guessing it here would have put a second coordinate conversion in the product -- a wrong one
// scrolls the reader silently to the wrong place. The conversion is `PageLayout::toContent`, and
// there is still exactly one of it.
//
// **What is still not applied: `/FitR`'s rectangle.** `core::Destination` has no fields for the
// other two of its four numbers, so a `/FitR` destination lands on its page at the current zoom.
// The vocabulary grows in Stage 7, which owns the editing half of both destination tables.

#ifndef RAWPAGE_UI_PANES_BOOKMARKSPANE_H
#define RAWPAGE_UI_PANES_BOOKMARKSPANE_H

#include "core/backend.h"
#include "core/doc/Destinations.h"

#include <QTreeWidget>

#include <cstddef>

namespace rawpage::core {
class Outline;
struct OutlineItem;
}

namespace rawpage::ui {

class BookmarksPane : public QTreeWidget {
    Q_OBJECT

public:
    explicit BookmarksPane(QWidget* parent = nullptr);
    ~BookmarksPane() override;

    // `outline` is the backend's, hydrated at open and owned by it; null empties the pane. Passing
    // an EMPTY outline is not the same thing and must not read as an error -- `core/backend.h` draws
    // that distinction deliberately, and this pane shows its empty state for it.
    void setDocument(core::DocumentHandle document, const core::Outline* outline);

    core::DocumentHandle document() const noexcept { return document_; }

    // ---- The five §8 rows this stage services ------------------------------------------------
    void goToSelected();
    void expandSelected();
    void collapseSelected();
    // `QTreeWidget::expandAll`/`collapseAll` exist; these exist so the registered command has one
    // name to call and so a case reads the same entry point the chord does.
    void expandAllItems();
    void collapseAllItems();

    // ---- What a case can assert, rather than a screenshot ------------------------------------
    std::size_t nodeCount() const;

    // The rows the tree would DRAW -- expanded children included, collapsed ones not. This is the
    // number convention 12 requires a case to find non-zero before asserting anything about what is
    // visible, and it is what distinguishes *expand all worked* from *the tree has children*.
    int visibleRowCount() const;

    // The selected bookmark's title and destination, or empty / unresolved when nothing is selected.
    QString selectedTitle() const;
    core::Destination selectedDestination() const;

signals:
    // A GESTURE asked to go to this destination -- `Enter`, or a click. Never emitted by a
    // programmatic selection, for the reason `ThumbnailsPane` states at length: a pane whose
    // selection navigated, and whose navigation moved the selection, is a scroll that fights the
    // user's hand.
    void activated(core::Destination destination);

protected:
    // THE MODIFIED ARROWS, AND ONLY TO LET THEM GO. `Alt+Up` and `Alt+Down` are §8's
    // *move up / down among siblings* -- Stage 7's `bookmark.moveUp` and `bookmark.moveDown`,
    // unregistered here, so `ShortcutBinder` answers `NotImplemented` and leaves the key for
    // whatever else would have had it. `QAbstractItemView::keyPressEvent` is that "whatever else"
    // and it walks the selection on an arrow **without looking at the modifiers**, so an
    // unregistered reorder chord would silently move the selection instead.
    //
    // This is **I-04 a second time, in a second pane** (`Stage4-ViewingSurface-Testing-Guide.md`
    // §8): the thumbnails pane met the identical trap at T5, one layer lower. Both were found by
    // the control written from what must NOT happen, and neither by a positive case -- the bare
    // arrows walk the selection correctly in both panes, which is all a positive case can see.
    void keyPressEvent(QKeyEvent* event) override;

private:
    void appendItems(QTreeWidgetItem* parent, const std::vector<core::OutlineItem>& items);

    core::DocumentHandle document_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_BOOKMARKSPANE_H
