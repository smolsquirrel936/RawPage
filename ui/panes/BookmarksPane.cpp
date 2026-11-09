#include "ui/panes/BookmarksPane.h"

#include "core/doc/Outline.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/ShortcutBinder.h"

#include <QDropEvent>
#include <algorithm>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStyledItemDelegate>
#include <QTreeWidgetItemIterator>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QVariant>

namespace rawpage::ui {
namespace {

// The destination a row stands for, carried on the item rather than in a parallel structure: a
// second container keyed by row index is a container that disagrees with the tree the first time
// something is expanded.
constexpr int kDestinationRole = Qt::UserRole + 1;
// Stage 7 T11: the item's id (`OutlineItemId::toString`) and its title as the model holds it -- what a commit in place
// is compared with, so an editor closed unchanged requests nothing.
constexpr int kIdRole = Qt::UserRole + 2;
constexpr int kTitleRole = Qt::UserRole + 3;

core::OutlineItemId idOf(const QTreeWidgetItem* item)
{
    if (item == nullptr) return {};
    return core::OutlineItemId::fromString(item->data(0, kIdRole).toString()).value_or(core::OutlineItemId{});
}

QVariant packDestination(const core::Destination& destination)
{
    return QVariant::fromValue(destination);
}

core::Destination unpackDestination(const QTreeWidgetItem* item)
{
    if (item == nullptr) return {};
    const QVariant packed = item->data(0, kDestinationRole);
    return packed.canConvert<core::Destination>() ? packed.value<core::Destination>()
                                                  : core::Destination{};
}

int countVisibleRows(const QTreeWidgetItem* item)
{
    int rows = 1;
    if (!item->isExpanded()) return rows;
    for (int i = 0; i < item->childCount(); ++i) rows += countVisibleRows(item->child(i));
    return rows;
}

// THE RENAME EDITOR CLAIMS ITS OWN KEYS (Stage 7 T11). `ShortcutBinder` asks the focused widget first, through Qt's
// `ShortcutOverride` (its class comment), and a `QLineEdit` claims the editing keys but not `Enter`, `Esc` or `Tab` --
// so in the Bookmarks context `Enter` went to `bookmark.goTo` and `Tab` to `bookmark.demote`, and a title typed in place
// was neither committed nor dropped (found by guide A-73's first run). These four are the delegate's -- commit, cancel,
// commit -- and so the editor's.
class RenameEditor final : public QLineEdit {
public:
    using QLineEdit::QLineEdit;

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ShortcutOverride) {
            const int key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape || key == Qt::Key_Tab || key == Qt::Key_Backtab) {
                event->accept();
                return true;
            }
        }
        return QLineEdit::event(event);
    }
};

class RenameDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem&, const QModelIndex&) const override
    {
        auto* editor = new RenameEditor(parent);
        editor->setFrame(false);
        return editor;
    }
};

} // namespace

BookmarksPane::BookmarksPane(QWidget* parent) : QTreeWidget(parent)
{
    setObjectName(QStringLiteral("bookmarksPane"));

    // THE CONTEXT DECLARATION. Every §8 Bookmarks row resolves against this, which is how `Enter`
    // reaches `bookmark.goTo` and how the eight editing rows reach `NotImplemented` rather than
    // some other context's command.
    ShortcutBinder::declareContext(this, core::KeyContext::Bookmarks);

    setHeaderHidden(true);
    setColumnCount(1);
    setUniformRowHeights(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::StrongFocus);

    // NO EDIT TRIGGER, and it is switched off rather than left to chance: `QTreeWidget` will happily rename an item on
    // a slow double-click, and a rename that changed only the widget would be a bookmark the file does not have -- a
    // view pretending to be a mutation, which is the one thing D4-1 forbids outright. Since Stage 7 T11 the editor
    // opens only by `F2` (`beginRename`), and what it commits is a REQUEST the window turns into a command.
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setItemDelegate(new RenameDelegate(this));
    connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (populating_ || item == nullptr || column != 0) return;
        const QString typed = item->text(0);
        const QString held = item->data(0, kTitleRole).toString();
        if (typed == held) return;
        // PUT BACK AT ONCE: the row says what the model says until the model says otherwise.
        populating_ = true;
        item->setText(0, held);
        populating_ = false;
        emit renameRequested(idOf(item), typed);
    });

    // A CLICK IS THE GESTURE, and `itemActivated` rather than `itemClicked`: activation is a
    // double-click or `Enter` depending on the platform's own convention, and a single click is
    // how a person SELECTS a row to read it. Navigating on selection would make walking the tree
    // with the mouse a series of jumps nobody asked for -- the same rule the thumbnails pane
    // follows for its arrows.
    connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
        if (item != nullptr) emit activated(unpackDestination(item));
    });
}

BookmarksPane::~BookmarksPane() = default;

void BookmarksPane::setDocument(core::DocumentHandle document, const core::Outline* outline, bool editable,
                                const QString& readOnlyReason)
{
    // THE SAME DOCUMENT KEEPS WHAT A PERSON DID TO THE TREE (Stage 7 T11): each row's expansion and the selection, by
    // id, across an edit. Another document starts from what its file asks for.
    std::map<QString, bool> expanded;
    core::OutlineItemId selected;
    const bool same = document == document_ && document.isValid();
    if (same) {
        for (QTreeWidgetItemIterator it(this); *it; ++it) expanded.emplace((*it)->data(0, kIdRole).toString(), (*it)->isExpanded());
        selected = selectedId();
    }
    document_ = document;
    editable_ = editable && outline != nullptr;
    readOnlyReason_ = outline != nullptr && !editable ? readOnlyReason : QString();
    populating_ = true;
    clear();
    setDragDropMode(editable_ ? QAbstractItemView::InternalMove : QAbstractItemView::NoDragDrop);
    setDragEnabled(editable_);
    setAcceptDrops(editable_);
    setDropIndicatorShown(editable_);
    // A READ-ONLY OUTLINE SAYS WHY, in the header, where the rows cannot hide it.
    setHeaderHidden(readOnlyReason_.isEmpty());
    setHeaderLabel(readOnlyReason_);
    setToolTip(readOnlyReason_);

    if (outline == nullptr) {
        populating_ = false;
        return;
    }
    appendItems(nullptr, outline->roots(), expanded);
    populating_ = false;
    if (same && selected.isValid() && selectId(selected)) return;

    // THE FILE'S OWN OPEN/CLOSED STATE, applied by `appendItems` from `OutlineItem::openByDefault`
    // -- ISO 32000-1's `/Count` sign, which T4 read. A pane that expanded everything would throw
    // away what the document's author said about its own structure; one that collapsed everything
    // would hide a one-level outline entirely.

    if (topLevelItemCount() > 0) setCurrentItem(topLevelItem(0));
}

void BookmarksPane::appendItems(QTreeWidgetItem* parent, const std::vector<core::OutlineItem>& items,
                                const std::map<QString, bool>& expanded)
{
    for (const core::OutlineItem& item : items) {
        auto* row = parent == nullptr ? new QTreeWidgetItem(this) : new QTreeWidgetItem(parent);

        // THE TITLE IS DOCUMENT TEXT, not a catalog key: it is the author's words and is shown
        // as-is. A bookmark with an empty title is drawn as an empty row rather than given one of
        // ours, because inventing *"(untitled)"* would put a string in the tree that is not in the
        // file and that a Stage 7 rename would then appear to have deleted.
        row->setText(0, item.title);
        row->setData(0, kDestinationRole, packDestination(item.destination));
        row->setData(0, kIdRole, item.id.toString());
        row->setData(0, kTitleRole, item.title);
        if (editable_) row->setFlags(row->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        // The file's style, as a reader draws it: `/F`'s bold and italic, `/C`'s colour.
        if (item.bold || item.italic) {
            QFont font = row->font(0);
            font.setBold(item.bold);
            font.setItalic(item.italic);
            row->setFont(0, font);
        }
        if (item.colour)
            row->setForeground(0, QColor::fromRgbF(static_cast<float>((*item.colour)[0]), static_cast<float>((*item.colour)[1]),
                                                   static_cast<float>((*item.colour)[2])));

        // A destination naming a page that is not there is shown, greyed, rather than dropped:
        // `Destinations::unresolvedCount()` exists for the same reason, and a bookmark silently
        // missing from the tree is worse than one that says it goes nowhere. STAGE 7 T11: still SELECTABLE where the
        // outline may be edited -- a bookmark with no destination is one a person renames, moves or points again.
        if (!item.destination.isResolved()) {
            if (!editable_) row->setFlags(row->flags() & ~Qt::ItemIsSelectable);
            row->setForeground(0, palette().color(QPalette::Disabled, QPalette::Text));
            row->setToolTip(0, catalog::translate(catalog::key::PanesDestinationsUnresolved));
        }

        appendItems(row, item.children, expanded);
        const auto kept = expanded.find(item.id.toString());
        row->setExpanded(kept != expanded.end() && item.id.isValid() ? kept->second : item.openByDefault);
    }
}

void BookmarksPane::goToSelected()
{
    if (const QTreeWidgetItem* item = currentItem())
        emit activated(unpackDestination(item));
}

void BookmarksPane::expandSelected()
{
    if (QTreeWidgetItem* item = currentItem()) item->setExpanded(true);
}

void BookmarksPane::collapseSelected()
{
    if (QTreeWidgetItem* item = currentItem()) item->setExpanded(false);
}

void BookmarksPane::expandAllItems()
{
    expandAll();
}

void BookmarksPane::collapseAllItems()
{
    collapseAll();
}

core::OutlineItemId BookmarksPane::selectedId() const
{
    return idOf(currentItem());
}

QTreeWidgetItem* BookmarksPane::rowOf(const core::OutlineItemId& id) const
{
    if (!id.isValid()) return nullptr;
    const QString key = id.toString();
    for (QTreeWidgetItemIterator it(const_cast<BookmarksPane*>(this)); *it; ++it) {
        if ((*it)->data(0, kIdRole).toString() == key) return *it;
    }
    return nullptr;
}

bool BookmarksPane::selectId(const core::OutlineItemId& id)
{
    QTreeWidgetItem* row = rowOf(id);
    if (row == nullptr) return false;
    for (QTreeWidgetItem* up = row->parent(); up != nullptr; up = up->parent()) up->setExpanded(true);
    setCurrentItem(row);
    scrollToItem(row);
    return true;
}

bool BookmarksPane::isExpanded(const core::OutlineItemId& id) const
{
    const QTreeWidgetItem* row = rowOf(id);
    return row != nullptr && row->isExpanded();
}

bool BookmarksPane::beginRename()
{
    QTreeWidgetItem* row = currentItem();
    if (!editable_ || row == nullptr || isRenaming()) return false;
    editItem(row, 0);
    return isRenaming();
}

bool BookmarksPane::isRenaming() const
{
    return state() == QAbstractItemView::EditingState;
}

void BookmarksPane::dropEvent(QDropEvent* event)
{
    // NOTHING MOVES HERE: the place the drop means is requested, and the tree is handed the model again.
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    (void)requestDropAt(event->position().toPoint());
}

bool BookmarksPane::requestDropAt(QPoint at)
{
    if (!editable_) return false;
    const QTreeWidgetItem* dragged = currentItem();
    if (dragged == nullptr) return false;
    const core::OutlineItemId id = idOf(dragged);
    QTreeWidgetItem* target = itemAt(at);
    // WHERE ON THE ROW, from the point itself: its top quarter above it, its bottom quarter below it, the rest onto it --
    // `QAbstractItemView`'s rule, read here rather than from the drag-move that last set it.
    QAbstractItemView::DropIndicatorPosition where = QAbstractItemView::OnViewport;
    if (target != nullptr) {
        const QRect row = visualItemRect(target);
        const int margin = std::max(2, row.height() / 4);
        where = at.y() < row.top() + margin ? QAbstractItemView::AboveItem
                : at.y() > row.bottom() - margin ? QAbstractItemView::BelowItem
                                                 : QAbstractItemView::OnItem;
    }
    core::OutlinePosition to;
    const auto indexIn = [](const QTreeWidgetItem* row) {
        return row->parent() != nullptr ? row->parent()->indexOfChild(const_cast<QTreeWidgetItem*>(row))
                                        : row->treeWidget()->indexOfTopLevelItem(const_cast<QTreeWidgetItem*>(row));
    };
    switch (where) {
    case QAbstractItemView::OnItem:
        to.parent = idOf(target);
        to.index = target->childCount() - (dragged->parent() == target ? 1 : 0);
        break;
    case QAbstractItemView::AboveItem:
    case QAbstractItemView::BelowItem: {
        to.parent = idOf(target->parent());
        int index = indexIn(target) + (where == QAbstractItemView::BelowItem ? 1 : 0);
        // Counted with the dragged row taken out of its list.
        if (dragged->parent() == target->parent() && indexIn(dragged) < index) --index;
        to.index = index;
        break;
    }
    case QAbstractItemView::OnViewport:
        to.index = topLevelItemCount() - (dragged->parent() == nullptr ? 1 : 0);
        break;
    }
    emit moveRequested(id, to);
    return true;
}

void BookmarksPane::keyPressEvent(QKeyEvent* event)
{
    const bool isArrow = event->key() == Qt::Key_Up || event->key() == Qt::Key_Down
                         || event->key() == Qt::Key_Left || event->key() == Qt::Key_Right;

    // See the header. `ignore()` and return rather than calling the base: the base is the thing
    // that would take it. Note that `Left` and `Right` never reach here unmodified -- the binder
    // dispatches `bookmark.collapse` and `bookmark.expand` and consumes them -- so in practice
    // this guards `Alt+Up` and `Alt+Down`, and guards the rest against the day §8 binds them.
    if (isArrow && event->modifiers() != Qt::NoModifier) {
        event->ignore();
        return;
    }
    QTreeWidget::keyPressEvent(event);
}

std::size_t BookmarksPane::nodeCount() const
{
    std::size_t nodes = 0;
    for (QTreeWidgetItemIterator it(const_cast<BookmarksPane*>(this)); *it; ++it) ++nodes;
    return nodes;
}

int BookmarksPane::visibleRowCount() const
{
    int rows = 0;
    for (int i = 0; i < topLevelItemCount(); ++i) rows += countVisibleRows(topLevelItem(i));
    return rows;
}

QString BookmarksPane::selectedTitle() const
{
    const QTreeWidgetItem* item = currentItem();
    return item == nullptr ? QString() : item->text(0);
}

core::Destination BookmarksPane::selectedDestination() const
{
    return unpackDestination(currentItem());
}

} // namespace rawpage::ui
