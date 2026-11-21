// The Bookmarks pane -- `Features.md` §5.4 and §8's Bookmarks table, Stage 4 T6, decisions D4-2
// and D4-3.
//
// ===========================================================================================
// IT NAVIGATES, AND SINCE STAGE 7 T11 IT EDITS -- THROUGH COMMANDS ONLY (D7-10)
// ===========================================================================================
// `core/doc/Outline` arrived at Stage 4 T4 as a read-only half (D4-3). Since T11 the pane is handed the MODEL's
// outline -- `Document::outline()`, the first-party reading where the two agree -- and every edit is a command
// the window runs (`ui/shell/MainWindowBookmarks.cpp`): the pane never changes a title or moves a row itself. A
// rename in place, a drop and the keys of §8's table become a REQUEST (`renameRequested`, `moveRequested`, or
// an action the window registered), and the window hands the pane the model again once the journal has it --
// keeping each row's expansion and the selection by the item's id, so an edit does not collapse the tree.
//
// AN OUTLINE THAT CANNOT BE EDITED SAYS WHY: a document this build does not edit, or one whose two readings of
// the outline disagree (D7-10, D6-3's rule) -- the header shows the reason, rows cannot be renamed or dragged,
// and the window's editing actions are disabled for it.
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
//
// ===========================================================================================
// A RIGHT-CLICK MENU, AND A DOUBLE-CLICK THAT GOES AND THEN RENAMES (`Stage7-Assembly-Plan.md` D7-40)
// ===========================================================================================
// Asked for by the user after M-05's sign-off, 2026-11-22, and built as the Comments pane's menu was (Stage 6 guide
// I-32): a right click on a bookmark -- or the Menu key on the current one -- selects it WITHOUT going there and offers
// Rename, Set Destination, Delete and Bookmark Properties, each the window's registered command by its label and
// chord. A DOUBLE-CLICK goes to the bookmark; the window renames on the next one instead while the view is still where
// that left it. `Enter` always goes.

#ifndef RAWPAGE_UI_PANES_BOOKMARKSPANE_H
#define RAWPAGE_UI_PANES_BOOKMARKSPANE_H

#include "core/backend.h"
#include "core/cmd/ActionId.h"
#include "core/doc/Destinations.h"
#include "core/doc/Outline.h"

#include <QTreeWidget>

#include <cstddef>
#include <functional>
#include <map>

class QMenu;

namespace rawpage::ui {

class BookmarksPane : public QTreeWidget {
    Q_OBJECT

public:
    explicit BookmarksPane(QWidget* parent = nullptr);
    ~BookmarksPane() override;

    // `outline` is the document's -- the model's where the tab has a journal, else the backend's; null empties the
    // pane. Passing an EMPTY outline is not the same thing and must not read as an error -- `core/backend.h` draws
    // that distinction deliberately, and this pane shows its empty state for it. `editable`: whether rows may be
    // renamed and dragged; `readOnlyReason`, shown in the header when the outline itself refuses editing.
    // HANDED AGAIN after every edit: the same document keeps each row's expansion and the selection, by id.
    void setDocument(core::DocumentHandle document, const core::Outline* outline, bool editable = false,
                     const QString& readOnlyReason = QString());

    core::DocumentHandle document() const noexcept { return document_; }
    bool isEditable() const noexcept { return editable_; }
    QString readOnlyReason() const { return readOnlyReason_; }

    // ---- The editing half (Stage 7 T11) ------------------------------------------------------------
    // The selected item's id; invalid when nothing is selected.
    core::OutlineItemId selectedId() const;
    // Selects the row of `id`, its ancestors expanded; false when no row has it.
    bool selectId(const core::OutlineItemId& id);
    // Opens the in-place editor on the selected row -- `F2`. Its commit is `renameRequested`, never a change here.
    bool beginRename();
    bool isRenaming() const;
    // Whether the row of `id` is expanded now.
    bool isExpanded(const core::OutlineItemId& id) const;
    // Where the row of `id` is drawn, in the viewport's coordinates -- empty when no row has it or it is collapsed out
    // of sight. Selects nothing: what a case clicks at.
    QRect rowRect(const core::OutlineItemId& id) const;
    // THE DROP, as `dropEvent` makes it: the selected row dropped at viewport point `at` -- onto the row there (its last
    // child), above or below it (its top or bottom quarter), or past every row (last at the top level) -- requested as
    // `moveRequested`. False when the outline is not editable or nothing is selected.
    bool requestDropAt(QPoint at);

    // ---- The five §8 rows this stage services ------------------------------------------------
    void goToSelected();
    void expandSelected();
    void collapseSelected();
    // `QTreeWidget::expandAll`/`collapseAll` exist; these exist so the registered command has one
    // name to call and so a case reads the same entry point the chord does.
    void expandAllItems();
    void collapseAllItems();

    // ---- The right-click menu (D7-40) ------------------------------------------------------------------
    // Rename, Set Destination, Delete, then Bookmark Properties -- each by its label and the chord `chordText` gives it
    // NOW (a remap included; nothing becomes a shortcut, convention 15). Opened on the row at viewport point `at`, which
    // it SELECTS without going there: Set Destination reads the view, and a menu that moved the view would set a
    // bookmark to its own place. False -- no menu -- on no row, or an outline that cannot be edited.
    void setChordText(std::function<QString(core::ActionId)> chordText);
    bool showItemMenu(QPoint at);
    QMenu* itemMenu() const noexcept { return itemMenu_; }

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
    // THE EDITING REQUESTS (Stage 7 T11): a title typed in place, and a row dropped at a place -- `to` counted in its
    // new parent's children with the dragged row already taken out (`ReparentBookmarkCommand`'s rule).
    void renameRequested(core::OutlineItemId id, QString title);
    void moveRequested(core::OutlineItemId id, core::OutlinePosition to);

    // A GESTURE asked to go to this destination -- `Enter` (`goToSelected`). Never emitted by a
    // programmatic selection, for the reason `ThumbnailsPane` states at length: a pane whose
    // selection navigated, and whose navigation moved the selection, is a scroll that fights the
    // user's hand.
    void activated(core::Destination destination);

    // A DOUBLE-CLICK on the row of `id` (`Stage7-Assembly-Plan.md` D7-40). Not `activated`: the window decides whether
    // it goes to `destination` or renames -- a second double-click on the bookmark the first one went to, while the
    // view is still where that left it, renames (`MainWindow::bookmarkDoubleClicked`).
    void doubleClicked(core::OutlineItemId id, core::Destination destination);
    // The current row became `id` (invalid: none) -- by a person or a selection the window made, never by the pane
    // rebuilding itself from the model (`setDocument`), which puts the same row back. What forgets D7-40's last jump.
    void currentBookmarkChanged(core::OutlineItemId id);

    // A ROW OF THE RIGHT-CLICK MENU (D7-40): the window's registered command, dispatched in the Bookmarks context -- so
    // the menu's Delete asks what the key's does.
    void actionRequested(core::ActionId id);

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

    // A DROP IS A REQUEST (Stage 7 T11): the place it means, as `moveRequested`; the tree moves nothing itself -- the
    // model, handed back, does.
    void dropEvent(QDropEvent* event) override;

    // THE RIGHT-CLICK MENU (D7-40), from the mouse on a row or the Menu key on the current one.
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void appendItems(QTreeWidgetItem* parent, const std::vector<core::OutlineItem>& items,
                     const std::map<QString, bool>& expanded);
    QTreeWidgetItem* rowOf(const core::OutlineItemId& id) const;

    std::function<QString(core::ActionId)> chordText_;
    QMenu* itemMenu_ = nullptr;   // made afresh each time it shows
    core::DocumentHandle document_;
    bool editable_ = false;
    bool populating_ = false;
    QString readOnlyReason_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_BOOKMARKSPANE_H
