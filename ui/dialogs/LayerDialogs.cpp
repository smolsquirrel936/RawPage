#include "ui/dialogs/LayerDialogs.h"

#include "ui/shell/Catalog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace rawpage::ui {
namespace {

bool aPersonCanAnswer()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

LayerNameDialog::Answer& installedName()
{
    static LayerNameDialog::Answer answer;
    return answer;
}

LayerMergeDialog::Answer& installedMerge()
{
    static LayerMergeDialog::Answer answer;
    return answer;
}

LayerPropertiesDialog::Answer& installedProperties()
{
    static LayerPropertiesDialog::Answer answer;
    return answer;
}

// The groups a person sees, in the pane's order -- `shownTree`, depth first.
void visibleIds(const std::vector<core::LayerNode>& nodes, std::vector<core::LayerId>& out)
{
    for (const core::LayerNode& node : nodes) {
        if (!node.isCollection()) out.push_back(node.layer);
        visibleIds(node.children, out);
    }
}

std::vector<core::LayerId> visibleIds(const core::LayerTree& tree)
{
    std::vector<core::LayerId> out;
    visibleIds(tree.shownTree(), out);
    return out;
}

// The three usage rows: none declared, shown, hidden.
QComboBox* usageBox(core::LayerUsage usage)
{
    auto* box = new QComboBox;
    box->addItem(catalog::translate(catalog::key::LayersUsageUnset));
    box->addItem(catalog::translate(catalog::key::LayersUsageOn));
    box->addItem(catalog::translate(catalog::key::LayersUsageOff));
    box->setCurrentIndex(usage == core::LayerUsage::On ? 1 : usage == core::LayerUsage::Off ? 2 : 0);
    return box;
}

core::LayerUsage usageOf(const QComboBox* box)
{
    switch (box->currentIndex()) {
    case 1: return core::LayerUsage::On;
    case 2: return core::LayerUsage::Off;
    default: return core::LayerUsage::Unset;
    }
}

} // namespace

// ---- A new group's name ----------------------------------------------------------------------------------------------

bool LayerNameDialog::canAsk()
{
    return static_cast<bool>(installedName()) || aPersonCanAnswer();
}

std::optional<QString> LayerNameDialog::ask(QWidget* parent, const QString& title, const QString& initial)
{
    if (installedName()) return installedName()(initial);
    if (!aPersonCanAnswer()) return std::nullopt;
    bool accepted = false;
    const QString name =
        QInputDialog::getText(parent, title, catalog::translate(catalog::key::LayersNameLabel), QLineEdit::Normal, initial, &accepted);
    if (!accepted || name.trimmed().isEmpty()) return std::nullopt;
    return name;
}

void LayerNameDialog::setAnswerForTesting(Answer answer)
{
    installedName() = std::move(answer);
}

// ---- The group to merge into -----------------------------------------------------------------------------------------

std::vector<core::LayerId> LayerMergeDialog::choices(const core::LayerTree& tree, const core::LayerId& from)
{
    std::vector<core::LayerId> out = visibleIds(tree);
    out.erase(std::remove(out.begin(), out.end(), from), out.end());
    return out;
}

bool LayerMergeDialog::canAsk()
{
    return static_cast<bool>(installedMerge()) || aPersonCanAnswer();
}

std::optional<core::LayerId> LayerMergeDialog::ask(QWidget* parent, const core::LayerTree& tree, const core::LayerId& from)
{
    const std::vector<core::LayerId> offered = choices(tree, from);
    if (installedMerge()) return installedMerge()(offered);
    if (!aPersonCanAnswer() || offered.empty()) return std::nullopt;
    QStringList names;
    for (const core::LayerId& id : offered) names << tree.find(id)->name;
    const core::Layer* source = tree.find(from);
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(parent, catalog::translate(catalog::key::LayersMergeTitle),
                                                 catalog::translate(catalog::key::LayersMergeInto).arg(source != nullptr ? source->name : QString()),
                                                 names, 0, false, &accepted);
    if (!accepted) return std::nullopt;
    const qsizetype at = names.indexOf(chosen);
    if (at < 0) return std::nullopt;
    return offered[static_cast<std::size_t>(at)];
}

void LayerMergeDialog::setAnswerForTesting(Answer answer)
{
    installedMerge() = std::move(answer);
}

// ---- A group's properties --------------------------------------------------------------------------------------------

LayerPropertiesDialog::LayerPropertiesDialog(const core::LayerTree& tree, const core::LayerId& id, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("layerPropertiesDialog"));
    setWindowTitle(catalog::translate(catalog::key::LayersPropertiesTitle));
    const core::LayerProperties now = core::LayerProperties::of(tree, id);

    name_ = new QLineEdit(now.name, this);
    shown_ = new QCheckBox(catalog::translate(catalog::key::LayersShownAtOpen), this);
    shown_->setChecked(now.defaultOn);
    locked_ = new QCheckBox(catalog::translate(catalog::key::LayersLockedBox), this);
    locked_->setChecked(now.locked);
    print_ = usageBox(now.print);
    view_ = usageBox(now.view);
    export_ = usageBox(now.exportUsage);

    // GROUPING: the top level, then every group a person sees but this one and those under it.
    parent_ = new QComboBox(this);
    parent_->addItem(catalog::translate(catalog::key::LayersTopLevel));
    exclusive_ = new QListWidget(this);
    for (const core::LayerId& other : visibleIds(tree)) {
        if (other == id) continue;
        const core::Layer* layer = tree.find(other);
        if (layer == nullptr) continue;
        if (!tree.isWithin(other, id)) {
            parents_.push_back(other);
            parent_->addItem(layer->name);
            if (other == now.parent) parent_->setCurrentIndex(parent_->count() - 1);
        }
        exclusives_.push_back(other);
        auto* row = new QListWidgetItem(layer->name, exclusive_);
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        row->setCheckState(std::find(now.exclusiveWith.begin(), now.exclusiveWith.end(), other) != now.exclusiveWith.end() ? Qt::Checked
                                                                                                                          : Qt::Unchecked);
    }

    auto* form = new QFormLayout;
    form->addRow(catalog::translate(catalog::key::LayersNameLabel), name_);
    form->addRow(QString(), shown_);
    form->addRow(QString(), locked_);
    form->addRow(catalog::translate(catalog::key::LayersPrint), print_);
    form->addRow(catalog::translate(catalog::key::LayersView), view_);
    form->addRow(catalog::translate(catalog::key::LayersExport), export_);
    form->addRow(catalog::translate(catalog::key::LayersParent), parent_);
    form->addRow(catalog::translate(catalog::key::LayersExclusive), exclusive_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // A NAME A COMMAND WOULD REFUSE is refused here first: OK waits for one.
    const auto check = [this, buttons] { buttons->button(QDialogButtonBox::Ok)->setEnabled(core::isWritableLayerName(name_->text())); };
    connect(name_, &QLineEdit::textChanged, this, check);
    check();
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
}

core::LayerProperties LayerPropertiesDialog::properties() const
{
    core::LayerProperties out;
    out.name = name_->text();
    out.defaultOn = shown_->isChecked();
    out.locked = locked_->isChecked();
    out.print = usageOf(print_);
    out.view = usageOf(view_);
    out.exportUsage = usageOf(export_);
    const int parent = parent_->currentIndex();
    if (parent > 0 && static_cast<std::size_t>(parent) <= parents_.size()) out.parent = parents_[static_cast<std::size_t>(parent - 1)];
    for (int i = 0; i < exclusive_->count(); ++i) {
        if (exclusive_->item(i)->checkState() == Qt::Checked) out.exclusiveWith.push_back(exclusives_[static_cast<std::size_t>(i)]);
    }
    return out;
}

bool LayerPropertiesDialog::canAsk()
{
    return static_cast<bool>(installedProperties()) || aPersonCanAnswer();
}

std::optional<core::LayerProperties> LayerPropertiesDialog::ask(QWidget* parent, const core::LayerTree& tree, const core::LayerId& id)
{
    if (installedProperties()) return installedProperties()(core::LayerProperties::of(tree, id));
    if (!aPersonCanAnswer()) return std::nullopt;
    LayerPropertiesDialog dialog(tree, id, parent);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return dialog.properties();
}

void LayerPropertiesDialog::setAnswerForTesting(Answer answer)
{
    installedProperties() = std::move(answer);
}

} // namespace rawpage::ui
