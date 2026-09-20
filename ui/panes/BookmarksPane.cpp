#include "ui/panes/BookmarksPane.h"

#include "core/doc/Outline.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/ShortcutBinder.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QTreeWidgetItemIterator>
#include <QVariant>

namespace rawpage::ui {
namespace {

// The destination a row stands for, carried on the item rather than in a parallel structure: a
// second container keyed by row index is a container that disagrees with the tree the first time
// something is expanded.
constexpr int kDestinationRole = Qt::UserRole + 1;

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

    // NO INLINE EDITING, and it is switched off rather than left to chance: `QTreeWidget` will
    // happily rename an item on a slow double-click, and a rename that changed only the widget
    // would be a bookmark the file does not have -- a view pretending to be a mutation, which is
    // the one thing D4-1 forbids outright.
    setEditTriggers(QAbstractItemView::NoEditTriggers);

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

void BookmarksPane::setDocument(core::DocumentHandle document, const core::Outline* outline)
{
    document_ = document;
    clear();

    if (outline == nullptr) return;
    appendItems(nullptr, outline->roots());

    // THE FILE'S OWN OPEN/CLOSED STATE, applied by `appendItems` from `OutlineItem::openByDefault`
    // -- ISO 32000-1's `/Count` sign, which T4 read. A pane that expanded everything would throw
    // away what the document's author said about its own structure; one that collapsed everything
    // would hide a one-level outline entirely.

    if (topLevelItemCount() > 0) setCurrentItem(topLevelItem(0));
}

void BookmarksPane::appendItems(QTreeWidgetItem* parent,
                                const std::vector<core::OutlineItem>& items)
{
    for (const core::OutlineItem& item : items) {
        auto* row = parent == nullptr ? new QTreeWidgetItem(this) : new QTreeWidgetItem(parent);

        // THE TITLE IS DOCUMENT TEXT, not a catalog key: it is the author's words and is shown
        // as-is. A bookmark with an empty title is drawn as an empty row rather than given one of
        // ours, because inventing *"(untitled)"* would put a string in the tree that is not in the
        // file and that a Stage 7 rename would then appear to have deleted.
        row->setText(0, item.title);
        row->setData(0, kDestinationRole, packDestination(item.destination));

        // A destination naming a page that is not there is shown, greyed, rather than dropped:
        // `Destinations::unresolvedCount()` exists for the same reason, and a bookmark silently
        // missing from the tree is worse than one that says it goes nowhere.
        if (!item.destination.isResolved()) {
            row->setFlags(row->flags() & ~Qt::ItemIsSelectable);
            row->setToolTip(0, catalog::translate(catalog::key::PanesDestinationsUnresolved));
        }

        appendItems(row, item.children);
        row->setExpanded(item.openByDefault);
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
