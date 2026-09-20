#include "ui/panes/DestinationsPane.h"

#include "ui/shell/Catalog.h"

#include <QHeaderView>
#include <QStringList>
#include <QVariant>

#include <limits>

namespace rawpage::ui {
namespace {

constexpr int kDestinationRole = Qt::UserRole + 1;
constexpr int kPageSortRole = Qt::UserRole + 2;

core::Destination unpackDestination(const QTreeWidgetItem* item)
{
    if (item == nullptr) return {};
    const QVariant packed = item->data(0, kDestinationRole);
    return packed.canConvert<core::Destination>() ? packed.value<core::Destination>()
                                                  : core::Destination{};
}

// A page column that sorts as a NUMBER while reading as text. `QTreeWidgetItem`'s default
// comparison is lexicographic, which puts page 10 before page 2 -- a sort that looks like it works
// on a nine-page document and is wrong on every larger one.
class Row final : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem& other) const override
    {
        const int column = treeWidget() != nullptr ? treeWidget()->sortColumn() : 0;
        if (column != 1) return QTreeWidgetItem::operator<(other);

        // An unresolved row sorts last whichever way the column is pointing, because it has no page
        // to be between two others -- and sorting it as page 0 would put "goes nowhere" at the top
        // of a document's destination list.
        const int mine = data(1, kPageSortRole).toInt();
        const int theirs = other.data(1, kPageSortRole).toInt();
        return mine < theirs;
    }
};

} // namespace

DestinationsPane::DestinationsPane(QWidget* parent) : QTreeWidget(parent)
{
    setObjectName(QStringLiteral("destinationsPane"));

    // NO `ShortcutBinder::declareContext` -- see the header. §8 has no Destinations table, so there
    // is no context to declare and `Enter` is the widget's.

    setColumnCount(2);
    setHeaderLabels({catalog::translate(catalog::key::PanesDestinationsColumnName),
                     catalog::translate(catalog::key::PanesDestinationsColumnPage)});
    setRootIsDecorated(false);
    setUniformRowHeights(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::StrongFocus);
    setEditTriggers(QAbstractItemView::NoEditTriggers);

    // SORTABLE BY CLICKING A HEADER -- `Features.md` §5.5, and the reason this is a two-column tree
    // rather than a list.
    //
    // **THE DEFAULT ORDER IS BY NAME, ASCENDING, AND IT IS SET EXPLICITLY.** `setSortingEnabled`
    // alone leaves the initial order to whatever sort indicator the header happens to carry, which
    // is a default nobody chose and which a case reading row 0 would pin down by accident. By name
    // is also the right answer on its merits: ISO 32000-1's `/Names /Dests` IS a name tree and is
    // already lexically ordered, so for most documents this is the file's own order rather than a
    // re-arrangement of it -- and the old-style `/Dests` dictionary, which T4 merges into the same
    // list, has no defined order to preserve.
    setSortingEnabled(true);
    sortByColumn(0, Qt::AscendingOrder);
    header()->setSectionsClickable(true);

    connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item, int) {
        if (item == nullptr) return;
        const core::Destination destination = unpackDestination(item);
        // A DESTINATION THAT GOES NOWHERE DOES NOTHING, rather than scrolling to page 1. The row
        // stays in the list and says why (see the header); activating it is refused here so the
        // refusal is one behaviour rather than something each caller re-derives.
        if (destination.isResolved()) emit activated(destination);
    });
}

DestinationsPane::~DestinationsPane() = default;

void DestinationsPane::setDocument(core::DocumentHandle document,
                                   const core::Destinations* destinations)
{
    document_ = document;

    // SORTING OFF WHILE FILLING, then on again. A tree that re-sorts on every insertion is
    // quadratic and, worse, reorders rows under the loop that is adding them.
    const bool wasSorting = isSortingEnabled();
    setSortingEnabled(false);
    clear();

    if (destinations != nullptr) {
        for (const core::NamedDestination& entry : destinations->all()) {
            auto* row = new Row(this);
            row->setText(0, entry.name);
            row->setData(0, kDestinationRole, QVariant::fromValue(entry.destination));

            if (entry.destination.isResolved()) {
                // ONE-BASED, because it is a page number a person reads, and the physical index is
                // what `core/` counts in. The logical page LABEL is a different thing again and is
                // T12's, where the status bar and *Go to Page* learn it.
                row->setText(1, QString::number(entry.destination.pageIndex + 1));
                row->setData(1, kPageSortRole, entry.destination.pageIndex);
            } else {
                row->setText(1, catalog::translate(catalog::key::PanesDestinationsUnresolved));
                row->setData(1, kPageSortRole, std::numeric_limits<int>::max());
                row->setFlags(row->flags() & ~Qt::ItemIsSelectable);
                row->setToolTip(0, catalog::translate(catalog::key::PanesDestinationsUnresolved));
            }
        }
    }

    setSortingEnabled(wasSorting);
    if (topLevelItemCount() > 0) setCurrentItem(topLevelItem(0));
}

void DestinationsPane::goToSelected()
{
    const core::Destination destination = unpackDestination(currentItem());
    if (destination.isResolved()) emit activated(destination);
}

int DestinationsPane::rowCount() const
{
    return topLevelItemCount();
}

int DestinationsPane::unresolvedRowCount() const
{
    int unresolved = 0;
    for (int i = 0; i < topLevelItemCount(); ++i)
        if (!unpackDestination(topLevelItem(i)).isResolved()) ++unresolved;
    return unresolved;
}

QString DestinationsPane::nameAt(int index) const
{
    if (index < 0 || index >= topLevelItemCount()) return {};
    return topLevelItem(index)->text(0);
}

QString DestinationsPane::pageTextAt(int index) const
{
    if (index < 0 || index >= topLevelItemCount()) return {};
    return topLevelItem(index)->text(1);
}

QString DestinationsPane::selectedName() const
{
    const QTreeWidgetItem* item = currentItem();
    return item == nullptr ? QString() : item->text(0);
}

core::Destination DestinationsPane::selectedDestination() const
{
    return unpackDestination(currentItem());
}

} // namespace rawpage::ui
