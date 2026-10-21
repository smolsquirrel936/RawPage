// ui/panes/CommentsPane -- every comment of the document in front, as a list a person works through: grouped,
// sorted, filtered and searched, threads expanded and collapsed, replied to, given a status and a personal
// checkmark, deleted one at a time, all at once or as filtered, and numbered for reference. `Features.md` §4.2
// and §8's Comments pane table; Stage 6 T9, decisions D6-1, D6-11, D6-12 and D6-36
// (`Stage6-Annotations-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// A VIEW OF THE STORE'S QUERY, AND NOTHING ELSE (D6-12)
// ---------------------------------------------------------------------------------------------
// What is listed, in which groups and in which order is `core::AnnotationStore::grouped` -- headless and graded
// in `rawpage_tests` -- and this pane shows what it returns. It never reads a byte and never writes the store:
// its whole mutation vocabulary is a `Command` handed to the runner the window gives it, which is the
// document's `CommandJournal::run` (D6-1). A reply is `ReplyToAnnotationCommand`, a status or a checkmark
// `SetAnnotationStatusCommand`, an edit `EditAnnotationCommand`, a delete -- of one comment with its thread, of
// everything, of everything the filter shows -- ONE `DeleteAnnotationCommand`. Grouping, sorting, filtering,
// searching, expanding and numbering are VIEW state: they construct nothing and change no byte (guide A-72).
//
// ---------------------------------------------------------------------------------------------
// AN ITEM MODEL OF ITS OWN, BECAUSE OF THE 9,000 (guide §3.6)
// ---------------------------------------------------------------------------------------------
// `annots-900.pdf` holds 9,000 comments, and a group, sort or filter change must be on screen in 100 ms at p95.
// A `QTreeWidget` builds an item object per row, per column, on every change; a model over the query's indices
// builds nothing a row is not asked for, and the view asks only for the rows it shows (`uniformRowHeights`).
// A change resets the model -- the query is the whole answer -- and puts back the current comment, by identity.
//
// ---------------------------------------------------------------------------------------------
// ITS KEYS ARE §8's, THROUGH THE ACTION LAYER
// ---------------------------------------------------------------------------------------------
// The pane declares `KeyContext::Comments`, so `ShortcutBinder` resolves the table's chords here and the
// commands `ui/shell/MainWindow` registers call the methods below. `Up`/`Down` are the item view's own (§8 binds
// them native). The editor below the list -- a reply's, or an edit in place -- is an `AnnotationTextEditor`,
// which claims its keys (`Enter` commits, `Esc` abandons), so `Space` and `Delete` type there and do not act on
// the list. The page FOLLOWS the list: a comment made current by a person's gesture is `activated`, and the
// window reveals and selects it; a selection made on the page comes back here SILENTLY (`selectComment`), so the
// two never chase each other.
//
// ---------------------------------------------------------------------------------------------
// THE COMMENT COLUMN HAS A FLOOR, AND A NARROW PANE SCROLLS (guide I-30)
// ---------------------------------------------------------------------------------------------
// The comment column takes what the other four leave, but never less than `commentColumnMinimum`: the deepest
// listed row's indentation, its check box and a dozen characters. Below that the list scrolls sideways. Until
// I-30 it was a `Stretch` section, which a narrow pane squeezed to a few pixels while each reply's indentation
// and check box kept their size -- so the box was painted over the author and page columns, further right the
// deeper the reply. The column's cells are also CLIPPED to themselves, for a thread deeper than the floor allows
// for (`kDeepestIndented`): such a row loses its box rather than painting it into the next column.
//
// ---------------------------------------------------------------------------------------------
// A RIGHT-CLICK MENU, A PICTURE PER KIND, AND A CLICK THAT OPENS (guide I-32, I-33, I-34)
// ---------------------------------------------------------------------------------------------
// Asked for by the user after M-05's sign-off, 2026-10-22. A right click on a comment -- or the Menu key on the
// current one -- makes it current and offers §8's rows by their labels and chords: Reply, Edit in Place, Open
// Editor, Set Status with its five states (the one it has ticked), and Delete. Each but a state is the WINDOW's
// registered command (`actionRequested`), so Delete asks the same question there as the key does (I-32); a state is
// `setStatus`, as `Ctrl+Shift+K`'s menu sets it. The checkmark is not on it: its box is a click away. Each row
// starts, after its check box, with a picture of its kind (`ui/panes/CommentIcon`, I-33); the kind's word, which
// used to lead the text, is the tooltip, and a comment with no text says `(Empty)`. And a CLICK on a row -- not an
// arrow -- asks the window to open the comment's pop-up on the page (`popupRequested`, I-34).
//
// It holds no `Command` beyond the ones it constructs, and words nothing in English: every string is a
// `comments.` or `panes.` catalog key.

#ifndef RAWPAGE_UI_PANES_COMMENTSPANE_H
#define RAWPAGE_UI_PANES_COMMENTSPANE_H

#include "core/Geometry.h"
#include "core/Result.h"
#include "core/cmd/ActionId.h"
#include "core/doc/Annotation.h"
#include "core/doc/AnnotationStore.h"
#include "ui/viewer/AnnotationTextEditor.h"

#include <QPointer>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QMenu;
class QModelIndex;
class QPoint;
class QToolButton;
class QTreeView;
class QVBoxLayout;

namespace rawpage::core {
class Command;
}

namespace rawpage::ui {

class CommentsModel;

class CommentsPane : public QWidget {
    Q_OBJECT

public:
    using Runner = std::function<core::Status(std::unique_ptr<core::Command>)>;
    using Clock = std::function<std::int64_t()>;
    using Identity = std::function<QString()>;

    // What `step` did: nothing to step to, moved, or moved past the last (first) to the first (last).
    enum class Step { Nothing, Moved, Wrapped };

    explicit CommentsPane(QWidget* parent = nullptr);
    ~CommentsPane() override;

    // ---- The document in front ---------------------------------------------------------------------
    // Its store -- read, never written -- and what runs a command; null and none for no document, or one nothing
    // may change (a companion window's, a refused store's), when the list is shown and nothing is constructed.
    // `pageBoxes` and `rotations` are its pages' crop boxes (where a reply's icon goes, D6-36) and the
    // rotations they are shown at (the reading order numbers follow).
    void setDocument(const core::AnnotationStore* store, Runner runner, std::vector<core::PdfRect> pageBoxes,
                     std::vector<int> rotations);
    // The same pages after a change -- an unsaved rotation turns the reading order -- with no rebuild of its own.
    void setPages(std::vector<core::PdfRect> pageBoxes, std::vector<int> rotations);
    const core::AnnotationStore* store() const noexcept { return store_; }
    bool canChange() const noexcept { return store_ != nullptr && store_->status() && static_cast<bool>(runner_); }
    // WHO a reply, a status and a checkmark are by -- `identity/author`, else the login name -- asked each time,
    // so the checkmark shown is the current identity's (D6-12).
    void setIdentity(Identity identity);
    QString identity() const;
    void setClock(Clock clock);
    // The chord a command has now, as the right-click menu shows it beside its label -- the window's, from
    // `KeyBindingStore` (I-32). None: labels alone.
    void setChordText(std::function<QString(core::ActionId)> chordText);
    // The host's offset from UTC, in seconds -- which calendar day a date is (convention 11).
    void setUtcOffset(int seconds);
    // The document changed: the list re-asked, the current comment kept by identity. Deferred while the pane is
    // hidden and done when it shows or is asked.
    void refresh();

    // ---- View state (none of it a command) ---------------------------------------------------------
    void setGroupKey(core::AnnotationStore::GroupKey key);
    core::AnnotationStore::GroupKey groupKey() const noexcept { return group_; }
    void setSortKey(core::AnnotationStore::SortKey key);
    core::AnnotationStore::SortKey sortKey() const noexcept { return sort_; }
    void setSearchText(const QString& text);
    // The filter's four lists and its date range, as `core::AnnotationStore::Filter` holds them; the text and
    // the identity are the pane's own.
    void setAuthorFilter(std::vector<QString> authors);
    void setTypeFilter(std::vector<core::AnnotationSubtype> types);
    void setStatusFilter(std::vector<QString> statuses);
    void setCheckmarkFilter(std::optional<bool> checked);
    void setDateRange(std::optional<std::int64_t> from, std::optional<std::int64_t> to);
    void clearFilter();
    core::AnnotationStore::Filter filter() const;

    // SEQUENCE NUMBERS (guide A-72): shown in the list and, through the window, over the page -- never written.
    void setNumbering(bool on);
    bool numbering() const noexcept { return numbering_; }
    // Parallel to the store's `all()`, 0 for an item not numbered; empty while numbering is off.
    const std::vector<int>& numbers() const noexcept { return numbers_; }

    // ---- The current comment -------------------------------------------------------------------------
    std::optional<core::AnnotationId> currentComment() const;
    // Makes `id` current WITHOUT `activated` -- the page's selection coming back. False when it is not listed.
    bool selectComment(const core::AnnotationId& id);
    // The top-level comments in the order the list shows them.
    std::vector<core::AnnotationId> listedComments() const;

    // ---- §8's Comments pane rows, and the commands beside them --------------------------------------
    void openEditor();                       // Enter: `openEditorRequested`
    bool editInPlace();                      // F2: the current comment's text, in the editor below the list
    bool reply();                            // Ctrl+Enter: a reply to the current comment, typed below
    core::Status deleteCurrent();            // Delete: the comment and its thread, one command
    core::Status toggleCheckmark();          // Space: the current identity's checkmark, one command
    bool showStatusMenu();                   // Ctrl+Shift+K: the five review states
    core::Status setStatus(const QString& state);   // one of them, one command
    void focusSearch();                      // Ctrl+F: search within comments
    // Ctrl+. / Ctrl+, : the next (previous) listed comment after the current one -- or, with none current, the
    // first on or after (last on or before) `fromPage` -- made current and `activated`. Wraps.
    Step step(int direction, int fromPage = 0);
    core::Status deleteAll();                // comment.deleteAll: every comment, with its thread
    core::Status deleteMatching();           // comment.deleteMatching: every comment the list shows
    void showFilterMenu();                   // comment.filter
    void showSortMenu();                     // comment.sort
    // The right-click menu (I-32) for the comment at `pos` in the list's viewport, made current first. False -- and
    // no menu -- on a group, on no row, or for a document nothing may change.
    bool showItemMenu(const QPoint& pos);

    // ---- What a case can assert, rather than a screenshot ---------------------------------------
    QTreeView* view() const noexcept { return view_; }
    QLineEdit* searchField() const noexcept { return search_; }
    QComboBox* groupBox() const noexcept { return groupBox_; }
    QComboBox* sortBox() const noexcept { return sortBox_; }
    QMenu* statusMenu() const noexcept { return statusMenu_; }
    QMenu* filterMenu() const noexcept { return filterMenu_; }
    QMenu* itemMenu() const noexcept { return itemMenu_; }
    AnnotationTextEditor* editor() const noexcept { return editor_; }
    QLabel* countLabel() const noexcept { return count_; }
    // The rows the view would DRAW -- expanded children included (convention 12's count).
    int visibleRowCount() const;
    // The top-level comments listed, and how many groups.
    int listedCount() const;
    int groupCount() const;
    // The row text of a listed comment's columns, for a case: 0 the comment, 1 the author, 2 the page, 3 the
    // date, 4 the status.
    QString textOf(const core::AnnotationId& id, int column) const;
    // How long the last rebuild of the list took, in milliseconds (guide §3.6).
    double lastRebuildMs() const noexcept { return lastRebuildMs_; }
    // The narrowest the comment column is made (I-30): the deepest listed row's indentation, at most
    // `kDeepestIndented` levels, its check box, its picture (I-33) and a dozen characters.
    int commentColumnMinimum() const;
    static constexpr int kDeepestIndented = 6;

signals:
    // A person made this comment current -- a click, an arrow, `Ctrl+.` -- and the page follows it.
    void activated(const core::AnnotationId& id, int page, const core::PdfRect& rect);
    // `Enter`: open the comment's own editor on the page (the window's properties bar).
    void openEditorRequested(const core::AnnotationId& id);
    // A CLICK on a comment's row (I-34) -- after `activated` -- asks for its pop-up on the page.
    void popupRequested(const core::AnnotationId& id);
    // A command from the pane's own menu -- dispatched by the window, so it is the registered command.
    void actionRequested(core::ActionId id);
    // A command the pane constructed was refused -- by a lock, a page that cannot be edited. The window words it.
    void refused(const core::Error& error);
    // The numbers changed: numbering switched, or the document did while it is on.
    void numbersChanged();

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    friend class CommentsModel;

    void rebuild();
    // The comment column: what the other columns leave of the viewport, and never under its minimum (I-30).
    void fitCommentColumn();
    void ensureFresh();
    void updateCount();
    void populateFilterMenu();
    void populateItemMenu();
    core::Status run(std::unique_ptr<core::Command> command);
    std::int64_t now() const;
    std::optional<std::size_t> currentIndex() const;
    // The top-level comment the current row is, or is in (a reply's thread's head).
    std::optional<std::size_t> currentTopLevel() const;
    QModelIndex indexOf(const core::AnnotationId& id) const;
    void makeCurrent(const QModelIndex& index, bool activate);
    void openTextEditor(bool replying);
    core::Status deleteThreads(const std::vector<std::size_t>& items);
    void onCurrentChanged(const QModelIndex& current);
    // Checkmark toggled on `index` -- by `Space` or by the check box.
    core::Status toggleCheckmarkOf(std::size_t index);

    const core::AnnotationStore* store_ = nullptr;
    Runner runner_;
    Clock clock_;
    Identity identity_;
    std::function<QString(core::ActionId)> chordText_;
    int utcOffset_ = 0;
    std::vector<core::PdfRect> pageBoxes_;
    std::vector<int> rotations_;

    core::AnnotationStore::GroupKey group_ = core::AnnotationStore::GroupKey::None;
    core::AnnotationStore::SortKey sort_ = core::AnnotationStore::SortKey::Page;
    QString text_;
    std::vector<QString> authors_;
    std::vector<core::AnnotationSubtype> types_;
    std::vector<QString> statuses_;
    std::optional<bool> checked_;
    std::optional<std::int64_t> from_;
    std::optional<std::int64_t> to_;
    bool numbering_ = false;
    std::vector<int> numbers_;

    bool stale_ = true;
    bool silent_ = false;          // a current change that is not a person's gesture
    double lastRebuildMs_ = 0.0;

    CommentsModel* model_ = nullptr;
    QTreeView* view_ = nullptr;
    QLineEdit* search_ = nullptr;
    QComboBox* groupBox_ = nullptr;
    QComboBox* sortBox_ = nullptr;
    QToolButton* filterButton_ = nullptr;
    QToolButton* moreButton_ = nullptr;
    QMenu* filterMenu_ = nullptr;
    QMenu* moreMenu_ = nullptr;
    QMenu* statusMenu_ = nullptr;
    QMenu* itemMenu_ = nullptr;   // I-32: the right-click menu, made afresh each time it shows
    QWidget* dateRow_ = nullptr;
    QDateEdit* fromEdit_ = nullptr;
    QDateEdit* toEdit_ = nullptr;
    QLabel* editorTitle_ = nullptr;
    QVBoxLayout* editorSlot_ = nullptr;
    QPointer<AnnotationTextEditor> editor_;
    QLabel* count_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_PANES_COMMENTSPANE_H
