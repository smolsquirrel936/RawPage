#include "ui/shell/QuickAccessToolbar.h"

#include "ui/shell/ActionFactory.h"
#include "ui/shell/ActionToolButton.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/RawPageAction.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QApplication>
#include <QMouseEvent>
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
        auto* button = new ActionToolButton(this);   // not QToolButton -- S2 issue I-35
        button->setDefaultAction(action);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::TabFocus);   // keyboard operability, retained
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setFixedHeight(kButtonHeight);
        button->installEventFilter(this);   // drag-to-reorder; see eventFilter()
        row_->addWidget(button);
        button->show();
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
        empty->show();
        items_.append(empty);
    }

    row_->addStretch(1);

    // SHOWN EXPLICITLY, AND THE PARENT TOLD (issue I-49). A widget constructed with a parent that is
    // ALREADY on screen starts hidden, and a hidden widget is an empty layout item -- so every
    // rebuild after the window was shown produced a row of invisible buttons and a toolbar that
    // collapsed to its 4 pixels of margin. The constructor's rebuild was the only one that worked,
    // because it runs before the window is shown and `show()` then reaches the whole tree.
    //
    // That is every customization: add, remove, reorder, reset, and the empty-state label that was
    // supposed to say the toolbar is empty rather than leave a bare strip. `updateGeometry()`
    // because the row's size hint has just changed and the header's layout is the one that has to
    // hear about it.
    updateGeometry();
}

void QuickAccessToolbar::setLocked(bool locked)
{
    if (locked_ == locked) return;
    locked_ = locked;

    // A lock taken mid-drag ends the drag rather than leaving a button following the pointer with
    // nothing able to drop it.
    if (locked_ && pressedButton_) {
        pressedButton_ = nullptr;
        dragging_ = false;
        unsetCursor();
    }
    emit lockedChanged(locked_);
}

bool QuickAccessToolbar::eventFilter(QObject* watched, QEvent* event)
{
    auto* button = qobject_cast<QToolButton*>(watched);
    if (!button) return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && !locked_) {
            pressedButton_ = button;
            pressedAt_     = mouse->globalPosition().toPoint();
            dragging_      = false;
        }
        break;   // NOT consumed: a press that never becomes a drag has to reach the button
    }

    case QEvent::MouseMove: {
        if (pressedButton_ != button) break;
        auto* mouse = static_cast<QMouseEvent*>(event);
        const QPoint travelled = mouse->globalPosition().toPoint() - pressedAt_;
        if (!dragging_ && travelled.manhattanLength() < QApplication::startDragDistance()) break;

        dragging_ = true;
        setCursor(Qt::ClosedHandCursor);
        moveDraggedButtonTo(mapFromGlobal(mouse->globalPosition().toPoint()).x());
        return true;
    }

    case QEvent::MouseButtonRelease: {
        if (pressedButton_ != button) break;
        const bool wasDragging = dragging_;
        pressedButton_ = nullptr;
        dragging_      = false;
        unsetCursor();

        if (!wasDragging) break;

        // CONSUMED, so the drag does not also RUN the command. Dropping a button on its new place
        // and having the file dialog open is the single most annoying way this feature can fail.
        commitDraggedOrder();
        return true;
    }

    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void QuickAccessToolbar::moveDraggedButtonTo(int x)
{
    if (!pressedButton_) return;

    // TWO INDEX SPACES, and confusing them is issue I-52. A slot is a position among the BUTTONS;
    // `QHBoxLayout::insertWidget` takes a position in the LAYOUT, which also holds the trailing
    // stretch that keeps the row left-aligned. The first version of this counted buttons and then
    // inserted by that number -- so a drop past the last button asked for a layout index one beyond
    // the last button, which after the removal is the STRETCH's index: the button landed on the far
    // right edge of the toolbar with a gap in front of it. It only happened at the right-hand end,
    // which is why it looked intermittent.
    QList<QToolButton*> others;
    int currentSlot = 0;
    bool seenDragged = false;
    for (int i = 0; i < row_->count(); ++i) {
        auto* sibling = qobject_cast<QToolButton*>(row_->itemAt(i)->widget());
        if (!sibling) continue;
        if (sibling == pressedButton_) { seenDragged = true; continue; }
        if (!seenDragged) ++currentSlot;
        others.append(sibling);
    }
    if (!seenDragged) return;

    // A neighbour yields once the pointer is past its CENTRE, which is what makes the row feel like
    // it follows the hand rather than snapping late.
    int slot = 0;
    for (QToolButton* other : others) {
        if (x <= other->geometry().center().x()) break;
        ++slot;
    }
    if (slot == currentSlot) return;

    // Only the WIDGET moves. `commands_` is left alone until the release, so nothing here triggers
    // `rebuild()` -- which would delete the button the pointer is still holding.
    row_->removeWidget(pressedButton_);

    const int layoutIndex = slot < others.size()
                                ? row_->indexOf(others.at(slot))          // before that button
                                : row_->indexOf(others.last()) + 1;       // after the last one,
                                                                         // and still before the stretch
    row_->insertWidget(layoutIndex, pressedButton_);
}

void QuickAccessToolbar::commitDraggedOrder()
{
    // Read back off the layout, which is where the drag has been working all along.
    const std::vector<ActionId> dropped = renderedCommands();
    if (dropped == commands_) return;
    commands_ = dropped;
    emit commandsChanged();
}

QMenu* QuickAccessToolbar::buildMenu()
{
    // Parented to this widget and deleted when it closes: the menu's items are RawPageActions, and
    // a menu kept alive between right-clicks would accumulate one set of them per click.
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    // EVERY ITEM IS A COMMAND, through ActionFactory, with no exception -- including `ui.customize`,
    // which the shell has not registered and which therefore appears disabled and says why. A
    // context menu built from lambdas would be four features the Stage 14 batch runner cannot
    // reach, on the toolbar whose entire purpose is reaching commands quickly.
    // WHAT IS UNDER THE CURSOR DECIDES, not which call site raised the menu (issue I-50). The old
    // split was "raised from the toolbar" versus "raised from the ribbon", which offered `Add` over
    // the toolbar's own empty space -- where there is no command to add, so the item could never be
    // enabled and sat there greyed, telling a person that adding is impossible at the exact moment
    // they went looking for it. It also offered `Add` for a ribbon command already ON the toolbar,
    // which would have been refused.
    const bool hasTarget = pendingTarget_.has_value();
    const bool alreadyOnToolbar =
        hasTarget && std::find(commands_.begin(), commands_.end(), *pendingTarget_) != commands_.end();

    if (alreadyOnToolbar) {
        actions_.addToMenu(menu, ActionId::UiQatRemove);

        // REORDERING IS DELIBERATELY NOT IN THIS MENU. Until 2026-08-31 it was reachable from
        // nowhere at all (issue I-43): `moveCommand` was public, `UiCustomization` persisted the
        // order, and the only caller was a test. I-43 put `ui.qatMoveLeft` / `ui.qatMoveRight` here
        // beside Remove; the user then asked for a DRAG instead, so both items came back out the
        // same day. They stay REGISTERED and unlisted -- `eventFilter()` holds the gesture, and a
        // gesture reaches nothing but a pointer, so flow K and the tool finder still need the two
        // commands. `tst_qat` asserts BOTH halves: registered, and absent from this menu.
        actions_.addMenuSeparator(menu);
    } else if (hasTarget) {
        actions_.addToMenu(menu, ActionId::UiQatAdd);
        actions_.addMenuSeparator(menu);
    }

    actions_.addToMenu(menu, ActionId::UiQatLock);
    actions_.addToMenu(menu, ActionId::UiQatPlacement);
    actions_.addToMenu(menu, ActionId::UiResetCustomization);
    actions_.addMenuSeparator(menu);
    actions_.addToMenu(menu, ActionId::UiCustomize);
    return menu;
}

void QuickAccessToolbar::showAddMenu(const QPoint& globalPos, ActionId target)
{
    setPendingTarget(target);
    buildMenu()->popup(globalPos);
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

    buildMenu()->popup(event->globalPos());
    event->accept();
}

} // namespace rawpage::ui
