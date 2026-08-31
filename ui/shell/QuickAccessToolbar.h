// The Quick Access Toolbar -- a user-ordered row of commands that does not move when the ribbon
// tab does. `Features.md` §1.1 lists five operations for it: add from any ribbon control, remove,
// reorder, place above or below the ribbon, and reset to factory defaults. All five are here.
//
// "All five" was true of the METHODS and false of the product until 2026-08-31 (issue I-43):
// `moveCommand` was public, `UiCustomization` persisted the order, this comment and two other
// documents called reordering shipped -- and no menu item, no drag and no command could reach it,
// so the only caller in the tree was a test. It is now `ui.qatMoveLeft` / `ui.qatMoveRight`, in
// the context menu with the rest, each disabled at the end it cannot move towards.
//
// ---------------------------------------------------------------------------------------------
// IT HOLDS `ActionId`s, NOT WIDGETS, AND REBUILDS ITS BUTTONS FROM THAT LIST.
//
// The list is the state; the buttons are a rendering of it. Reordering by moving a QToolButton
// inside a layout would make the LAYOUT the source of truth, and the thing that has to be
// serialized, restored on the next launch and compared against a factory default is the list --
// so every mutation edits the list and rebuilds, and there is exactly one path from state to
// screen. At three to a dozen buttons the rebuild is free; at a hundred it would still be right.
//
// ---------------------------------------------------------------------------------------------
// THE PARAMETERIZED-COMMAND PROBLEM, STATED HERE BECAUSE THIS IS WHERE IT FIRST BITES.
//
// `ActionRegistry::dispatch(ActionId, KeyContext)` carries no arguments, and "add THIS command to
// the toolbar" needs one. Every other Stage 1 command is a verb with no object; `ui.qatAdd` and
// `ui.qatRemove` are the first two that are not.
//
// The Stage 1 answer is a `pendingTarget()` on this widget: the surface that raises the menu -- a
// right-click on a ribbon button, or on a QAT button -- records WHICH command was under the cursor,
// and the registered handler reads it. That keeps both commands reachable without widgets (a test,
// and Stage 14's batch runner, set the target and dispatch), which is the property the whole action
// layer exists to preserve. It is not a general answer: a batch script cannot name a target it did
// not click. Recorded as open decision **O11** in `Implementation-Plan.md` §8, owned by Stage 14,
// which is where flow K has to invoke a parameterized command for real.

#ifndef RAWPAGE_UI_SHELL_QUICKACCESSTOOLBAR_H
#define RAWPAGE_UI_SHELL_QUICKACCESSTOOLBAR_H

#include "core/cmd/ActionId.h"

#include <QList>
#include <QPointer>
#include <QPoint>
#include <QWidget>

#include <optional>
#include <vector>

class QHBoxLayout;
class QMenu;
class QToolButton;

namespace rawpage::ui {

class ActionFactory;

class QuickAccessToolbar : public QWidget {
    Q_OBJECT

public:
    // Everything it needs arrives through the factory: the labels, the enablement, and the one
    // connection each action carries. It never talks to the registry itself -- a widget that
    // dispatches on its own behalf is the shape D1-7 exists to keep out of `ui/`.
    explicit QuickAccessToolbar(ActionFactory& actions, QWidget* parent = nullptr);
    ~QuickAccessToolbar() override;

    // What a fresh profile gets, and what `ui.resetCustomization` returns to. Three commands, and
    // two of the three are not registered yet -- they render disabled and say why, exactly as the
    // ribbon's do. Shipping a default toolbar of only the commands that already work would make the
    // toolbar look complete and then grow silently over thirteen stages.
    static std::vector<core::ActionId> factoryDefaults();

    void setCommands(const std::vector<core::ActionId>& commands);
    const std::vector<core::ActionId>& commands() const { return commands_; }

    // Each returns false when the operation would not change anything -- a duplicate add, a remove
    // of something absent, a move to where it already is. A caller uses that to avoid persisting
    // and re-rendering a no-op, and a test uses it to assert the refusal.
    bool addCommand(core::ActionId id);
    bool removeCommand(core::ActionId id);

    // LOCKED means a drag cannot reorder the row. It is a guard against doing by accident what the
    // toolbar otherwise invites: dragging a button somewhere else. Persisted as `ui/qatLocked`.
    bool isLocked() const { return locked_; }
    void setLocked(bool locked);
    bool moveCommand(int from, int to);

    void resetToDefaults();

    bool isBelowRibbon() const { return belowRibbon_; }
    void setBelowRibbon(bool below);

    // The parameterized-command channel described in the header. `std::nullopt` means the menu was
    // raised over no particular command -- in which case `ui.qatAdd` has nothing to add and says so
    // rather than adding something arbitrary.
    void setPendingTarget(std::optional<core::ActionId> id) { pendingTarget_ = id; }
    std::optional<core::ActionId> pendingTarget() const { return pendingTarget_; }

    // Raised over a ribbon control: offers to add THAT command. Public because `MainWindow` owns
    // the connection from the ribbon -- the toolbar does not know the ribbon exists.
    void showAddMenu(const QPoint& globalPos, core::ActionId target);

    // Every command currently on the toolbar, in order, as the buttons see it. For the widget
    // suite: asserting against the button row rather than against `commands()` is what makes the
    // rebuild itself the thing under test.
    std::vector<core::ActionId> renderedCommands() const;

signals:
    void lockedChanged(bool locked);

    void commandsChanged();
    void placementChanged(bool belowRibbon);

protected:
    // DRAG-TO-REORDER lives here rather than in a QToolButton subclass: the order belongs to the
    // toolbar, and a button that knew how to reorder its siblings would be a button that has to
    // know it is on a toolbar. Filtered on each button because a QToolButton consumes the press.
    bool eventFilter(QObject* watched, QEvent* event) override;

    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void rebuild();
    QMenu* buildMenu();

    // Moves the DRAGGED WIDGET inside the layout, without touching `commands_` and without a
    // rebuild -- a rebuild would delete the very button the pointer is holding (see the note in
    // `rebuild()` about I-49 and about deleteLater). The list is committed once, on release.
    void moveDraggedButtonTo(int x);
    void commitDraggedOrder();

    ActionFactory& actions_;

    std::vector<core::ActionId>   commands_;
    bool                          locked_ = false;

    // The in-flight drag. `pressedButton_` is set on a press and cleared on release; `dragging_`
    // turns true only once the pointer has travelled the platform's start-drag distance, so a click
    // stays a click and a drag stops being one.
    QPointer<QToolButton>         pressedButton_;
    QPoint                        pressedAt_;
    bool                          dragging_ = false;
    std::optional<core::ActionId> pendingTarget_;
    bool                          belowRibbon_ = false;

    QHBoxLayout* row_ = nullptr;

    // The widgets THIS class put in the row, so that a rebuild takes down its own buttons and
    // nothing else. `findChildren` would also collect an open context menu -- which is a child of
    // this widget and is, at exactly the moment a rebuild happens, the thing that asked for it.
    QList<QPointer<QWidget>> items_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_QUICKACCESSTOOLBAR_H
