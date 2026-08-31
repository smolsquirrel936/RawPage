#include "ui/shell/QuickAccessToolbar.h"

#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/RawPageAction.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>

#include <algorithm>

namespace rawpage::ui {
namespace {

using core::ActionId;

// Small enough not to compete with the ribbon, large enough to hit. The ribbon's own small buttons
// are a third of the body height; this matches them rather than inventing a third size.
constexpr int kButtonHeight = 22;

} // namespace

QuickAccessToolbar::QuickAccessToolbar(ActionFactory& actions, QWidget* parent)
    : QWidget(parent), actions_(actions)
{
    setObjectName(QStringLiteral("quickAccessToolbar"));
    setToolTip(catalog::translate(catalog::key::QatTitle));

    row_ = new QHBoxLayout(this);
    row_->setContentsMargins(6, 2, 6, 2);
    row_->setSpacing(2);
    row_->addStretch(1);

    commands_ = factoryDefaults();
    rebuild();
}

QuickAccessToolbar::~QuickAccessToolbar()
{
    // Nothing to disconnect: this widget connects no child signal to a lambda holding `this` --
    // every button carries a RawPageAction and the action dispatches through the registry, which is
    // the arrangement I-12 exists to make unnecessary here. Kept as an explicit destructor so that
    // the next person to add a `connect(child, ..., this, [this]{...})` sees where the disconnect
    // belongs rather than discovering it as a segfault at shutdown.
}

std::vector<ActionId> QuickAccessToolbar::factoryDefaults()
{
    return {ActionId::FileOpen, ActionId::FileSave, ActionId::FilePrint};
}

void QuickAccessToolbar::setCommands(const std::vector<ActionId>& commands)
{
    if (commands == commands_) return;
    commands_ = commands;
    rebuild();
    emit commandsChanged();
}

bool QuickAccessToolbar::addCommand(ActionId id)
{
    if (std::find(commands_.begin(), commands_.end(), id) != commands_.end()) return false;
    commands_.push_back(id);
    rebuild();
    emit commandsChanged();
    return true;
}

bool QuickAccessToolbar::removeCommand(ActionId id)
{
    const auto it = std::find(commands_.begin(), commands_.end(), id);
    if (it == commands_.end()) return false;
    commands_.erase(it);
    rebuild();
    emit commandsChanged();
    return true;
}

bool QuickAccessToolbar::moveCommand(int from, int to)
{
    const int size = static_cast<int>(commands_.size());
    if (from < 0 || from >= size || to < 0 || to >= size || from == to) return false;

    const ActionId moved = commands_.at(static_cast<std::size_t>(from));
    commands_.erase(commands_.begin() + from);
    commands_.insert(commands_.begin() + to, moved);
    rebuild();
    emit commandsChanged();
    return true;
}

void QuickAccessToolbar::resetToDefaults()
{
    // Placement is part of the customization and therefore part of the reset. `Features.md` §1.1
    // says "reset the Quick Access Toolbar and ribbon to factory defaults", and a reset that left
    // the toolbar under the ribbon would be a reset the user has to finish by hand.
    const bool placementChangedByReset = belowRibbon_;
    belowRibbon_ = false;
    setCommands(factoryDefaults());

    // Unconditionally, even when `setCommands` saw no change: a reset whose only effect was the
    // placement still has to reach the window that lays this widget out.
    if (placementChangedByReset) emit placementChanged(belowRibbon_);
    emit commandsChanged();
}

void QuickAccessToolbar::setBelowRibbon(bool below)
{
    if (belowRibbon_ == below) return;
    belowRibbon_ = below;
    emit placementChanged(belowRibbon_);
}

std::vector<ActionId> QuickAccessToolbar::renderedCommands() const
{
    // Read off the LAYOUT rather than off `commands_`, so that a case asserting an order is
    // asserting what the row shows. Reading the list back to itself would keep passing if `rebuild`
    // stopped being called at all.
    std::vector<ActionId> rendered;
    for (int i = 0; i < row_->count(); ++i) {
        auto* button = qobject_cast<QToolButton*>(row_->itemAt(i)->widget());
        if (!button) continue;
        if (auto* action = qobject_cast<RawPageAction*>(button->defaultAction()))
            rendered.push_back(action->id());
    }
    return rendered;
}

void QuickAccessToolbar::rebuild()
{
    // DELETE LATER, NEVER DELETE. The path that gets here most often is: a QAT button's action is
    // triggered -> ActionFactory's lambda dispatches -> the registry runs `ui.qatRemove` -> this
    // function. A plain `delete` would destroy the very action whose signal is still being emitted
    // one frame down the stack. Reparented and hidden immediately so that `renderedCommands()` and
    // the layout see the new row rather than a row plus its predecessor awaiting deletion.
    for (const QPointer<QWidget>& item : items_) {
        if (!item) continue;
        row_->removeWidget(item);
        item->hide();
        item->setParent(nullptr);
        item->deleteLater();
    }
    items_.clear();

    while (QLayoutItem* leftover = row_->takeAt(0)) delete leftover;

    for (const ActionId id : commands_) {
        RawPageAction* action = actions_.create(id, this);
        auto* button = new QToolButton(this);
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::TabFocus);   // keyboard operability, retained
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFixedHeight(kButtonHeight);
        row_->addWidget(button);
        items_.append(button);
    }

    if (commands_.empty()) {
        // An empty toolbar renders as a sentence rather than as nothing. A zero-height strip of
        // nothing reads as a layout bug; "the Quick Access Toolbar is empty" reads as a choice the
        // user made and can undo from the same context menu.
        auto* empty = new QLabel(catalog::translate(catalog::key::QatEmpty), this);
        empty->setObjectName(QStringLiteral("quickAccessEmpty"));
        empty->setEnabled(false);
        row_->addWidget(empty);
        items_.append(empty);
    }

    row_->addStretch(1);
}

QMenu* QuickAccessToolbar::buildMenu(bool overCommand)
{
    // Parented to this widget and deleted when it closes: the menu's items are RawPageActions, and
    // a menu kept alive between right-clicks would accumulate one set of them per click.
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // EVERY ITEM IS A COMMAND, through ActionFactory, with no exception -- including `ui.customize`,
    // which the shell has not registered and which therefore appears disabled and says why. A
    // context menu built from lambdas would be four features the Stage 14 batch runner cannot
    // reach, on the toolbar whose entire purpose is reaching commands quickly.
    if (overCommand) {
        actions_.addToMenu(menu, ActionId::UiQatRemove);

        // REORDERING IS TWO COMMANDS, and until 2026-08-31 it was none (issue I-43): `moveCommand`
        // was public, `UiCustomization` persisted the order, and the only caller was a test. Each is
        // disabled at the end it cannot move towards -- the enablement predicate reads the pending
        // target, which `contextMenuEvent` has already set by the time this menu is built.
        actions_.addToMenu(menu, ActionId::UiQatMoveLeft);
        actions_.addToMenu(menu, ActionId::UiQatMoveRight);
    } else {
        actions_.addToMenu(menu, ActionId::UiQatAdd);
    }
    actions_.addMenuSeparator(menu);
    actions_.addToMenu(menu, ActionId::UiQatPlacement);
    actions_.addToMenu(menu, ActionId::UiResetCustomization);
    actions_.addMenuSeparator(menu);
    actions_.addToMenu(menu, ActionId::UiCustomize);
    return menu;
}

void QuickAccessToolbar::showAddMenu(const QPoint& globalPos, ActionId target)
{
    setPendingTarget(target);
    buildMenu(/*overCommand=*/false)->popup(globalPos);
}

void QuickAccessToolbar::contextMenuEvent(QContextMenuEvent* event)
{
    // Which button was under the cursor IS the parameter -- see the header on O11. Right-clicking
    // the empty part of the toolbar leaves the target unset, and `ui.qatRemove` then has nothing to
    // remove: it is offered, it refuses, and it logs why, rather than removing whichever button
    // happened to be last.
    std::optional<ActionId> target;
    if (auto* button = qobject_cast<QToolButton*>(childAt(event->pos()))) {
        if (auto* action = qobject_cast<RawPageAction*>(button->defaultAction()))
            target = action->id();
    }
    setPendingTarget(target);

    buildMenu(/*overCommand=*/target.has_value())->popup(event->globalPos());
    event->accept();
}

} // namespace rawpage::ui
