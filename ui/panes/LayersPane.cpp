#include "ui/panes/LayersPane.h"

#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMenu>
#include <QMetaObject>
#include <QVariant>

namespace rawpage::ui {
namespace {

constexpr int kIdRole = Qt::UserRole + 1;

std::optional<core::LayerId> idOf(const QTreeWidgetItem* item)
{
    if (item == nullptr) return std::nullopt;
    const QVariant id = item->data(0, kIdRole);
    if (!id.isValid()) return std::nullopt;
    return core::LayerId::fromString(id.toString());
}

} // namespace

LayersPane::LayersPane(QWidget* parent) : QTreeWidget(parent)
{
    setObjectName(QStringLiteral("layersPane"));
    // NO `ShortcutBinder::declareContext` -- see the header.
    setColumnCount(2);
    setHeaderLabels({catalog::translate(catalog::key::LayersColumnName), catalog::translate(catalog::key::LayersColumnLocked)});
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    setUniformRowHeights(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::StrongFocus);
    setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::SelectedClicked);

    // THE EYE AND A RENAME arrive as one signal, `itemChanged`; which one is told apart by what changed.
    connect(this, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        if (filling_ || column != 0) return;
        const std::optional<core::LayerId> id = idOf(item);
        if (!id || tree_ == nullptr) return;
        const core::Layer* layer = tree_->find(*id);
        if (layer == nullptr) return;
        const bool shown = item->checkState(0) == Qt::Checked;
        const bool wasShown = hidden_.count(*id) == 0;
        if (shown != wasShown) {
            // Put back at once: the eye shows what the WINDOW decides -- `setDocument` ticks it again if it changed.
            filling_ = true;
            item->setCheckState(0, wasShown ? Qt::Checked : Qt::Unchecked);
            filling_ = false;
            // QUEUED: the window answers by handing this pane the tree again, which clears it -- and the row whose
            // `itemChanged` is still being emitted would be deleted under Qt's feet.
            const core::LayerId which = *id;
            QMetaObject::invokeMethod(this, [this, which, shown] { emit visibilityRequested(which, shown); }, Qt::QueuedConnection);
            return;
        }
        const QString typed = item->text(0);
        if (typed != layer->name) {
            filling_ = true;
            item->setText(0, layer->name);   // the command's result comes back through `setDocument`
            filling_ = false;
            if (!typed.trimmed().isEmpty()) {
                const core::LayerId which = *id;   // queued, for the eye's reason
                QMetaObject::invokeMethod(this, [this, which, typed] { emit renameRequested(which, typed); }, Qt::QueuedConnection);
            }
        }
    });
}

LayersPane::~LayersPane() = default;

void LayersPane::setDocument(core::DocumentHandle document, const core::LayerTree* tree, const std::set<core::LayerId>& hidden,
                             bool editable)
{
    const std::optional<core::LayerId> selected = document == document_ ? selectedLayer() : std::nullopt;
    filling_ = true;
    document_ = document;
    tree_ = tree;
    hidden_ = hidden;
    editable_ = editable;
    clear();
    if (tree != nullptr) addNodes(nullptr, tree->shownTree());
    expandAll();
    filling_ = false;
    if (selected) selectLayer(*selected);
}

void LayersPane::addNodes(QTreeWidgetItem* parent, const std::vector<core::LayerNode>& nodes)
{
    for (const core::LayerNode& node : nodes) {
        auto* row = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(this);
        if (node.isCollection()) {
            // A COLLECTION groups and is no group: no eye, no lock, no rename -- its label as the file writes it.
            row->setText(0, node.label);
            row->setFlags(Qt::ItemIsEnabled);
        } else {
            const core::Layer* layer = tree_->find(node.layer);
            if (layer == nullptr) {
                delete row;
                continue;
            }
            row->setText(0, layer->name);
            row->setData(0, kIdRole, layer->id.toString());
            Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
            if (!layer->locked) flags |= Qt::ItemIsUserCheckable;
            if (editable_) flags |= Qt::ItemIsEditable;
            row->setFlags(flags);
            row->setCheckState(0, hidden_.count(layer->id) != 0 ? Qt::Unchecked : Qt::Checked);
            if (layer->locked) {
                row->setText(1, catalog::translate(catalog::key::LayersLocked));
                row->setToolTip(0, catalog::translate(catalog::key::LayersLockedTip));
            }
        }
        addNodes(row, node.children);
    }
}

QTreeWidgetItem* LayersPane::rowOf(const core::LayerId& id) const
{
    const QString key = id.toString();
    QTreeWidgetItemIterator it(const_cast<LayersPane*>(this));
    for (; *it != nullptr; ++it) {
        if ((*it)->data(0, kIdRole).toString() == key) return *it;
    }
    return nullptr;
}

std::optional<core::LayerId> LayersPane::selectedLayer() const
{
    return idOf(currentItem());
}

bool LayersPane::selectLayer(const core::LayerId& id)
{
    QTreeWidgetItem* row = rowOf(id);
    if (row == nullptr) return false;
    setCurrentItem(row);
    return true;
}

void LayersPane::renameSelected()
{
    QTreeWidgetItem* row = currentItem();
    if (row == nullptr || !idOf(row) || !(row->flags() & Qt::ItemIsEditable)) return;
    editItem(row, 0);
}

std::vector<core::ActionId> LayersPane::menuActions()
{
    using core::ActionId;
    return {ActionId::LayerShow, ActionId::LayerHide,  ActionId::LayerCreate, ActionId::LayerRename,
            ActionId::LayerLock, ActionId::LayerMerge, ActionId::LayerDelete, ActionId::LayerProperties};
}

void LayersPane::contextMenuEvent(QContextMenuEvent* event)
{
    if (QTreeWidgetItem* row = itemAt(viewport()->mapFrom(this, event->pos())); row != nullptr && idOf(row)) setCurrentItem(row);
    QMenu menu(this);
    for (const core::ActionId id : menuActions()) {
        // Each row through `ActionFactory` (D1-7): it names the command and dispatches it, as the Comments pane's rows do.
        QAction* item = ActionFactory::addNonCommandMenuItem(&menu, catalog::translateAction(id));
        if (chordText_) {
            const QString chord = chordText_(id);
            if (!chord.isEmpty()) item->setText(item->text() + QLatin1Char('\t') + chord);
        }
        item->setEnabled(commandEnabled_ ? commandEnabled_(id) : true);
        connect(item, &QAction::triggered, this, [this, id] { emit actionRequested(id); });
        if (id == core::ActionId::LayerHide || id == core::ActionId::LayerRename) ActionFactory::addMenuSeparator(&menu);
    }
    menu.exec(event->globalPos());
}

int LayersPane::groupRowCount() const
{
    int count = 0;
    QTreeWidgetItemIterator it(const_cast<LayersPane*>(this));
    for (; *it != nullptr; ++it) {
        if (idOf(*it)) ++count;
    }
    return count;
}

QString LayersPane::nameOf(const core::LayerId& id) const
{
    const QTreeWidgetItem* row = rowOf(id);
    return row != nullptr ? row->text(0) : QString();
}

std::optional<bool> LayersPane::eyeOf(const core::LayerId& id) const
{
    const QTreeWidgetItem* row = rowOf(id);
    if (row == nullptr) return std::nullopt;
    return row->checkState(0) == Qt::Checked;
}

bool LayersPane::lockedRow(const core::LayerId& id) const
{
    const QTreeWidgetItem* row = rowOf(id);
    return row != nullptr && !row->text(1).isEmpty() && !(row->flags() & Qt::ItemIsUserCheckable);
}

int LayersPane::depthOf(const core::LayerId& id) const
{
    const QTreeWidgetItem* row = rowOf(id);
    if (row == nullptr) return 0;
    int depth = 1;
    for (const QTreeWidgetItem* up = row->parent(); up != nullptr; up = up->parent()) ++depth;
    return depth;
}

QStringList LayersPane::collectionLabels() const
{
    QStringList out;
    QTreeWidgetItemIterator it(const_cast<LayersPane*>(this));
    for (; *it != nullptr; ++it) {
        if (!idOf(*it)) out << (*it)->text(0);
    }
    return out;
}

bool LayersPane::clickEye(const core::LayerId& id)
{
    QTreeWidgetItem* row = rowOf(id);
    if (row == nullptr || !(row->flags() & Qt::ItemIsUserCheckable)) return false;
    row->setCheckState(0, row->checkState(0) == Qt::Checked ? Qt::Unchecked : Qt::Checked);
    return true;
}

} // namespace rawpage::ui
