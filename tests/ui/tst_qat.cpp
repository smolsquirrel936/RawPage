// Stage 1 T11 -- the Quick Access Toolbar and the customization it serializes.
//
// Guide case A-55 (a QAT customization round-trips) and the mechanical half of M-11 (it survives a
// real restart, which needs a real process and stays manual). Offscreen, so nothing here judges
// where the toolbar sits on screen; what is asserted is that the list is the state, that every
// operation goes through a command, and that what is written can be read back.

#include <catch2/catch_test_macros.hpp>

#include "app/Services.h"
#include "tests/ui/ShellFixture.h"
#include "core/cmd/ActionRegistry.h"
#include "core/LogBuffer.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/QuickAccessToolbar.h"
#include "ui/shell/Ribbon.h"
#include "ui/shell/RawPageAction.h"
#include "ui/shell/UiCustomization.h"

#include <QApplication>
#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QSet>
#include <QToolButton>
#include <QDir>
#include <QTemporaryDir>

#include <algorithm>

using namespace rawpage;

namespace {

using core::ActionId;

struct Fixture : uitest::ShellFixture {
    // Every member and `services()` come from `tests/ui/ShellFixture.h` (S2 T13). They were seven
    // copies of the same eight fields until `app::Services` grew a render pipeline and the copies
    // became a seven-way edit; the header states what a fixture deliberately does NOT provide.
};

} // namespace

TEST_CASE("the toolbar starts at the factory defaults and renders every one of them", "[qat]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
    REQUIRE(qat != nullptr);
    CHECK(qat->commands() == ui::QuickAccessToolbar::factoryDefaults());

    // The BUTTONS, not the list read back to itself. `renderedCommands()` walks the layout, so this
    // is what fails if a mutation ever stops rebuilding the row.
    CHECK(qat->renderedCommands() == ui::QuickAccessToolbar::factoryDefaults());

    // One of the three defaults is declared and not yet registered -- it is on the toolbar, disabled,
    // exactly as it is on the ribbon. A default toolbar of only the commands that already work would
    // look complete and then grow silently over thirteen stages. `file.save` was the second until
    // Stage 3 T10 registered it, and is now on the toolbar and disabled for a different reason: there is
    // nothing to save.
    CHECK(fixture.actions.isRegistered(ActionId::FileSave));
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::FileSave));
    CHECK_FALSE(fixture.actions.isRegistered(ActionId::FilePrint));
}

TEST_CASE("add remove and reorder each change the row and refuse the no-op", "[qat]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
    REQUIRE(qat != nullptr);

    REQUIRE(qat->addCommand(ActionId::ViewToggleSidebar));
    CHECK(qat->renderedCommands().back() == ActionId::ViewToggleSidebar);

    // The refusals are the half that keeps the list a set: a duplicate add would put two buttons
    // for one command on a row the user cannot tell apart.
    CHECK_FALSE(qat->addCommand(ActionId::ViewToggleSidebar));
    CHECK_FALSE(qat->removeCommand(ActionId::EditCopy));
    CHECK_FALSE(qat->moveCommand(0, 0));
    CHECK_FALSE(qat->moveCommand(0, 99));

    const std::vector<ActionId> before = qat->commands();
    REQUIRE(qat->moveCommand(0, 2));
    CHECK(qat->commands().at(2) == before.at(0));
    CHECK(qat->renderedCommands() == qat->commands());

    REQUIRE(qat->removeCommand(ActionId::ViewToggleSidebar));
    CHECK(qat->renderedCommands().size() == before.size() - 1);
}

TEST_CASE("every toolbar operation is reachable as a command through the registry", "[qat]")
{
    // THE PROPERTY THE WHOLE ACTION LAYER EXISTS FOR, on a surface where it is easy to lose: a
    // context menu is the classic place a lambda gets wired straight to a behaviour, and the result
    // is a feature the Stage 14 batch runner cannot reach. Nothing below touches the widget's own
    // methods -- each operation is a dispatch.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
    REQUIRE(qat != nullptr);

    // Add. The target is the parameter the registry has no channel for, so the surface that raised
    // the menu records it -- see QuickAccessToolbar.h and open decision O11.
    qat->setPendingTarget(ActionId::ViewFullScreen);
    REQUIRE(fixture.actions.dispatch(ActionId::UiQatAdd, core::KeyContext::Global));
    CHECK(qat->renderedCommands().back() == ActionId::ViewFullScreen);

    // Remove, the same way.
    REQUIRE(fixture.actions.dispatch(ActionId::UiQatRemove, core::KeyContext::Global));
    const std::vector<ActionId> after = qat->commands();
    CHECK(std::find(after.begin(), after.end(), ActionId::ViewFullScreen) == after.end());

    // With NO target, add is not merely a failed dispatch -- it is reported as unavailable, which
    // is the distinct code T3 introduced so that "that does not exist" and "that is not applicable
    // right now" can be told apart by a CLI or a batch report.
    qat->setPendingTarget(std::nullopt);
    const core::Result<void> noTarget =
        fixture.actions.dispatch(ActionId::UiQatAdd, core::KeyContext::Global);
    REQUIRE_FALSE(noTarget);
    CHECK(noTarget.error().code == core::ErrorCode::Unavailable);

    // Placement, and reset.
    CHECK_FALSE(qat->isBelowRibbon());
    REQUIRE(fixture.actions.dispatch(ActionId::UiQatPlacement, core::KeyContext::Global));
    CHECK(qat->isBelowRibbon());
    CHECK(fixture.actions.isChecked(ActionId::UiQatPlacement));

    REQUIRE(qat->addCommand(ActionId::ViewToggleRibbon));
    REQUIRE(fixture.actions.dispatch(ActionId::UiResetCustomization, core::KeyContext::Global));
    CHECK(qat->commands() == ui::QuickAccessToolbar::factoryDefaults());
    CHECK_FALSE(qat->isBelowRibbon());
}

TEST_CASE("reordering the toolbar is reachable by a person and stops at both ends", "[qat]")
{
    // ISSUE I-43. `moveCommand` was public from T11, `UiCustomization` persisted the order from T11,
    // three documents described reordering as shipped -- and the ONLY caller was the case above,
    // which calls the widget method directly. Nothing on screen could move a button: no menu item,
    // no drag, no command. Convention 19's shape, and the same one as I-34: the destination was
    // proven and the route was never built.
    //
    // So this case dispatches, and never calls `moveCommand`.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
    REQUIRE(qat != nullptr);

    const std::vector<ActionId> defaults = qat->commands();
    REQUIRE(defaults.size() >= 3);

    // Right-clicking the middle button is what sets the target (O11); the commands read it at
    // dispatch time rather than capturing an index that the next add would invalidate.
    qat->setPendingTarget(defaults.at(1));
    REQUIRE(fixture.actions.dispatch(ActionId::UiQatMoveLeft, core::KeyContext::Global));
    CHECK(qat->commands().at(0) == defaults.at(1));
    CHECK(qat->commands().at(1) == defaults.at(0));
    CHECK(qat->renderedCommands() == qat->commands());   // the BUTTONS moved, not just the list

    REQUIRE(fixture.actions.dispatch(ActionId::UiQatMoveRight, core::KeyContext::Global));
    CHECK(qat->commands() == defaults);

    // AT THE ENDS THE COMMAND IS DISABLED, not offered and silently refused. A menu item that does
    // nothing when clicked is how a person learns to stop trusting the menu.
    qat->setPendingTarget(defaults.front());
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::UiQatMoveLeft));
    CHECK(fixture.actions.isEnabled(ActionId::UiQatMoveRight));

    qat->setPendingTarget(defaults.back());
    CHECK(fixture.actions.isEnabled(ActionId::UiQatMoveLeft));
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::UiQatMoveRight));

    // And with no button under the cursor there is nothing to move in either direction.
    qat->setPendingTarget(std::nullopt);
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::UiQatMoveLeft));
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::UiQatMoveRight));

    // THE ROUTE MOVED, and this is where that is recorded. I-43 put the two move commands in the
    // context menu because reordering had no route at all; the toolbar now reorders by DRAGGING a
    // button, so the menu items were removed as redundant and the commands stayed registered for
    // flow K. What follows therefore asserts the menu still raises and still offers Remove -- the
    // gesture is covered by `a button can be dragged to a new place unless the toolbar is locked`,
    // and the menu's current shape by `the move commands stay reachable although the menu no longer
    // offers them`.
    window.show();
    auto* button = qat->findChild<QToolButton*>();
    REQUIRE(button != nullptr);

    const QPoint inButton = button->geometry().center();
    QContextMenuEvent rightClick(QContextMenuEvent::Mouse, inButton, qat->mapToGlobal(inButton));
    QApplication::sendEvent(qat, &rightClick);

    QMenu* menu = qat->findChild<QMenu*>();
    REQUIRE(menu != nullptr);

    QSet<int> offered;
    for (QAction* action : menu->actions())
        if (auto* command = qobject_cast<ui::RawPageAction*>(action))
            offered.insert(static_cast<int>(command->id()));

    CHECK(offered.contains(static_cast<int>(ActionId::UiQatRemove)));
    CHECK(offered.contains(static_cast<int>(ActionId::UiQatLock)));

    menu->close();
}

TEST_CASE("adding a command starts with a right-click on the ribbon button", "[qat]")
{
    // THE ROUTE, which nothing had ever driven: `showAddMenu` and `Ribbon::controlContextMenuRequested`
    // appear in no test in this tree, and the case above reaches `ui.qatAdd` by setting the pending
    // target by hand -- which proves the command and says nothing about how a person gets to it.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.show();

    ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
    ui::Ribbon* ribbon = window.ribbon();
    REQUIRE(qat != nullptr);
    REQUIRE(ribbon != nullptr);

    // LAID OUT, not merely shown (convention 12). Offscreen there is no event loop, so the ribbon's
    // pages keep default geometry and every control reports itself invisible -- and `childAt()`, which
    // is how a position becomes a command for a DISABLED control, sees nothing at all.
    window.resize(1600, 900);
    window.layout()->activate();
    ribbon->layout()->activate();
    if (ui::RibbonPageLayout* page = ribbon->layoutOfPage(ribbon->currentPage())) page->activate();

    // A real ribbon button carrying a command NOT already on the toolbar. `wantEnabled` picks which
    // half of the ribbon to take it from, and BOTH halves matter: 22 of the 360 declared commands are
    // enabled and 338 are not, and the disabled ones are precisely what a person pins for later.
    // `mustBeVisible` only for the disabled half, and the asymmetry is measured rather than tidy.
    // Offscreen this ribbon reports 197 controls, 8 enabled, 20 visible and NONE both: `file.open` is
    // deliberately disabled where there is nobody to answer a file dialog (I-23), and the commands
    // that are enabled sit on pages the stacked widget is not showing. The enabled half does not need
    // visibility -- the event goes straight at the button, which is what an enabled widget receives.
    // The disabled half does, because `childAt()` skips hidden children and that is the whole
    // mechanism under test.
    const auto ribbonButton = [&](bool wantEnabled, bool mustBeVisible) -> QToolButton* {
        for (QToolButton* button : ribbon->findChildren<QToolButton*>()) {
            auto* action = qobject_cast<ui::RawPageAction*>(button->defaultAction());
            if (!action) continue;
            const std::vector<ActionId>& on = qat->commands();
            if (std::find(on.begin(), on.end(), action->id()) != on.end()) continue;
            if (button->isEnabled() != wantEnabled) continue;
            if (mustBeVisible && !button->isVisible()) continue;
            return button;
        }
        return nullptr;
    };

    QToolButton* targetWidget = ribbonButton(/*wantEnabled=*/true, /*mustBeVisible=*/false);
    REQUIRE(targetWidget != nullptr);
    auto* target = qobject_cast<ui::RawPageAction*>(targetWidget->defaultAction());
    REQUIRE(target != nullptr);

    // Right-click it, the way a person does. The ribbon owns the signal; the window connects it to
    // the toolbar, which records the command as the pending target (O11) and raises its menu.
    int emitted = 0;
    QObject::connect(ribbon, &ui::Ribbon::controlContextMenuRequested, ribbon,
                     [&emitted](core::ActionId, const QPoint&) { ++emitted; });

    QContextMenuEvent rightClick(QContextMenuEvent::Mouse, QPoint(2, 2),
                                 targetWidget->mapToGlobal(QPoint(2, 2)));
    QApplication::sendEvent(targetWidget, &rightClick);

    UNSCOPED_INFO("policy: " << static_cast<int>(targetWidget->contextMenuPolicy())
                  << "  (3 = CustomContextMenu)   signal emissions: " << emitted
                  << "   pending target set: " << qat->pendingTarget().has_value());

    QMenu* menu = qat->findChild<QMenu*>();
    REQUIRE(menu != nullptr);

    ui::RawPageAction* addItem = nullptr;
    for (QAction* action : menu->actions())
        if (auto* command = qobject_cast<ui::RawPageAction*>(action);
            command && command->id() == ActionId::UiQatAdd)
            addItem = command;

    REQUIRE(addItem != nullptr);
    INFO("the Add item is the one a person clicks; disabled here means the toolbar cannot be built");
    CHECK(addItem->isEnabled());

    // And it does what it says.
    const std::size_t before = qat->commands().size();
    addItem->trigger();
    CHECK(qat->commands().size() == before + 1);
    CHECK(qat->commands().back() == target->id());
    CHECK(qat->renderedCommands() == qat->commands());
    menu->close();

    // CLOSING A MENU ONLY SCHEDULES ITS DELETION -- `WA_DeleteOnClose` runs through `deleteLater`,
    // and offscreen nothing drains that queue on its own. Without this the next `findChild<QMenu*>`
    // returns the menu that was just dismissed, whose `Add` still points at the previous target, and
    // the assertions below pass or fail for reasons that have nothing to do with the product.
    const auto dropClosedMenus = [] {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    dropClosedMenus();

    // AND A DISABLED CONTROL, which is 338 of the 360 and was the whole defect (I-50). Qt delivers
    // no input event to a disabled widget, so its own `CustomContextMenu` signal never fires; the
    // event reaches the nearest enabled ancestor and propagates to `Ribbon`, which maps the position
    // back to a command. Before that, right-clicking a greyed button did nothing at all and a person
    // could pin only what already worked.
    QToolButton* greyed = ribbonButton(/*wantEnabled=*/false, /*mustBeVisible=*/true);
    REQUIRE(greyed != nullptr);
    auto* greyedAction = qobject_cast<ui::RawPageAction*>(greyed->defaultAction());
    REQUIRE(greyedAction != nullptr);
    REQUIRE_FALSE(greyed->isEnabled());

    // ON THE PAGE THAT IS ACTUALLY SHOWING: `childAt()` skips hidden children, and the ribbon keeps
    // its other eight pages in a QStackedWidget.
    REQUIRE(greyed->isVisible());

    // Delivered to the RIBBON, at the button's position. That is where Qt's own propagation ends up
    // -- a disabled widget takes no input event, the nearest enabled ancestor gets it, and an
    // ignored context-menu event walks up the parent chain from there. Sending it straight at the
    // button, as the enabled half of this case does, is precisely what a disabled widget never sees.
    const QPoint inRibbon = greyed->mapTo(ribbon, QPoint(2, 2));
    QContextMenuEvent onGreyed(QContextMenuEvent::Mouse, inRibbon, greyed->mapToGlobal(QPoint(2, 2)));
    QApplication::sendEvent(ribbon, &onGreyed);

    QMenu* greyedMenu = qat->findChild<QMenu*>();
    REQUIRE(greyedMenu != nullptr);

    ui::RawPageAction* addGreyed = nullptr;
    for (QAction* action : greyedMenu->actions())
        if (auto* command = qobject_cast<ui::RawPageAction*>(action);
            command && command->id() == ActionId::UiQatAdd)
            addGreyed = command;
    REQUIRE(addGreyed != nullptr);
    CHECK(addGreyed->isEnabled());

    addGreyed->trigger();
    CHECK(qat->commands().back() == greyedAction->id());
    greyedMenu->close();
    dropClosedMenus();

    // AND THE TOOLBAR'S OWN EMPTY SPACE OFFERS NO `Add` AT ALL. It cannot: there is no command under
    // the cursor there, so the item could only ever appear greyed -- which is what told the user
    // adding was impossible while they were looking for how to do it.
    qat->setPendingTarget(std::nullopt);
    QContextMenuEvent onEmpty(QContextMenuEvent::Mouse, QPoint(1, 1), qat->mapToGlobal(QPoint(1, 1)));
    QApplication::sendEvent(qat, &onEmpty);

    QMenu* emptyMenu = qat->findChild<QMenu*>();
    REQUIRE(emptyMenu != nullptr);
    for (QAction* action : emptyMenu->actions())
        if (auto* command = qobject_cast<ui::RawPageAction*>(action)) {
            INFO("offered over empty space: " << static_cast<int>(command->id()));
            CHECK(command->id() != ActionId::UiQatAdd);
            CHECK(command->id() != ActionId::UiQatRemove);
        }
    emptyMenu->close();
}

TEST_CASE("the toolbar stays on screen when emptied and when moved", "[qat]")
{
    // THE TWO WAYS A PERSON CAN LOSE IT, asked as questions about the WIDGET rather than about the
    // state -- which is the gap the cases above leave: they assert `isBelowRibbon()` and the command
    // list, both of which stay perfectly correct in a toolbar nobody can see.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.show();

    ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
    REQUIRE(qat != nullptr);
    REQUIRE(qat->isVisible());
    const int populatedHeight = qat->height();
    REQUIRE(populatedHeight > 0);

    // 1. EMPTIED. Removing the last command must not remove the toolbar: the context menu is the
    //    only way back, and a widget with no height has nothing to right-click.
    // COPIED FIRST: `commands()` returns a reference to the vector `removeCommand` mutates, and
    // iterating it while removing walks off the end of the list it is shortening.
    const std::vector<ActionId> toRemove = qat->commands();
    for (const ActionId id : toRemove) qat->removeCommand(id);
    REQUIRE(qat->commands().empty());
    // EVERY LEVEL, per convention 12: the toolbar's own row, the header that holds it, and the
    // window. Offscreen nothing re-runs a layout for you, and activating only the outermost one
    // reports the geometry the widget had before the change -- which is how a 4-pixel reading gets
    // mistaken for a defect, or a defect for a stale number.
    const auto relayout = [&] {
        qat->layout()->activate();
        if (QWidget* header = qat->parentWidget(); header && header->layout()) header->layout()->activate();
        window.layout()->activate();
    };
    relayout();

    CHECK(qat->isVisible());
    INFO("emptied height: " << qat->height());
    CHECK(qat->height() > 0);
    auto* emptyLabel = qat->findChild<QLabel*>(QStringLiteral("quickAccessEmpty"));
    CHECK(emptyLabel != nullptr);
    if (emptyLabel) {
        UNSCOPED_INFO("label text: '" << emptyLabel->text().toStdString() << "'"
                      << "  label sizeHint: " << emptyLabel->sizeHint().height()
                      << "  label visible: " << emptyLabel->isVisible());
    }
    UNSCOPED_INFO("qat sizeHint: " << qat->sizeHint().height()
                  << "  minimumSizeHint: " << qat->minimumSizeHint().height()
                  << "  layout count: " << qat->layout()->count());

    // 2. MOVED, in both directions, while empty -- the state a person reaches by accident.
    qat->setBelowRibbon(true);
    relayout();
    CHECK(qat->isVisible());
    CHECK(qat->height() > 0);
    CHECK(qat->parentWidget() != nullptr);

    qat->setBelowRibbon(false);
    relayout();
    CHECK(qat->isVisible());
    CHECK(qat->height() > 0);

    // 3. AND BACK. `ui.resetCustomization` is the documented way out, and it is reachable from the
    //    ribbon as well as from the toolbar's own menu -- which matters precisely when the toolbar
    //    is the thing that went missing.
    REQUIRE(fixture.actions.dispatch(ActionId::UiResetCustomization, core::KeyContext::Global));
    relayout();
    CHECK(qat->commands() == ui::QuickAccessToolbar::factoryDefaults());
    CHECK(qat->isVisible());
    CHECK(qat->height() == populatedHeight);

    // AND THE BUTTONS THEMSELVES ARE ON SCREEN, which is the general form of this defect: every
    // widget a rebuild creates after the window is shown starts hidden, and `commands()` and
    // `renderedCommands()` both go on answering correctly about a row nobody can see. Those two are
    // exactly what every other case in this file asks, which is why none of them noticed.
    const QList<QToolButton*> buttons = qat->findChildren<QToolButton*>();
    REQUIRE(buttons.size() == static_cast<int>(ui::QuickAccessToolbar::factoryDefaults().size()));
    for (QToolButton* button : buttons) {
        INFO("button: " << button->text().toStdString());
        CHECK(button->isVisible());
        CHECK(button->height() > 0);
    }
}

TEST_CASE("a button can be dragged to a new place unless the toolbar is locked", "[qat]")
{
    // The gesture that replaced the two move items in the context menu. Both halves matter: the drag
    // must reorder, and it must NOT also run the command it was holding -- dropping `Open` in its new
    // place and having a file dialog appear is the most annoying way this can fail.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.show();
    window.resize(1200, 800);
    window.layout()->activate();

    ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
    REQUIRE(qat != nullptr);
    qat->layout()->activate();

    const std::vector<ActionId> before = qat->renderedCommands();
    REQUIRE(before.size() >= 3);

    QList<QToolButton*> buttons;
    for (int i = 0; i < qat->layout()->count(); ++i)
        if (auto* button = qobject_cast<QToolButton*>(qat->layout()->itemAt(i)->widget()))
            buttons.append(button);
    REQUIRE(buttons.size() == static_cast<int>(before.size()));

    QToolButton* dragged = buttons.first();
    auto* draggedAction = qobject_cast<ui::RawPageAction*>(dragged->defaultAction());
    REQUIRE(draggedAction != nullptr);

    int fired = 0;
    QObject::connect(draggedAction, &QAction::triggered, draggedAction, [&fired]() { ++fired; });

    // WHAT THIS CANNOT SEE, said here rather than dressed up (convention 19). The release is
    // consumed so that a drop does not ALSO run the command it was holding -- dropping `Open` in
    // its new place and getting a file dialog is the worst way this feature can fail. That is not
    // observable here: a synthetic press never leaves the button in the pressed state a real click
    // needs, so `fired` stays 0 either way, and `isAccepted()` is true either way because
    // QToolButton accepts a release regardless of what a filter did. The ORDER is what this case
    // proves; the swallow is walked by hand in M-11.
    const auto dragTo = [&](QToolButton* button, const QPoint& destination) {
        const QPoint from = button->mapToGlobal(button->rect().center());
        QMouseEvent press(QEvent::MouseButtonPress, button->rect().center(), QPointF(from), QPointF(from),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(button, &press);

        const QPointF to(destination);
        QMouseEvent move(QEvent::MouseMove, button->mapFromGlobal(destination), to, to,
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(button, &move);

        QMouseEvent release(QEvent::MouseButtonRelease, button->mapFromGlobal(destination), to, to,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(button, &release);
    };

    // Past the LAST button's centre, which is what says "put it after that one".
    const QPoint pastTheEnd = buttons.last()->mapToGlobal(buttons.last()->rect().center()) + QPoint(4, 0);
    dragTo(dragged, pastTheEnd);

    const std::vector<ActionId> after = qat->renderedCommands();
    INFO("before: " << before.size() << " commands; after: " << after.size());
    CHECK(after.size() == before.size());
    CHECK(after.back() == before.front());        // the dragged one ended up last
    CHECK(qat->commands() == after);              // and the LIST followed the row, not the reverse
    CHECK(fired == 0);                            // the drag did not also run the command

    // AND IT LANDED IN THE ROW, not past the end of it (issue I-52). A slot among the buttons is not
    // an index into the layout -- the layout also holds the trailing stretch that keeps the toolbar
    // left-aligned, and inserting at the stretch's index parked the button on the far right edge
    // with a gap in front of it. `renderedCommands()` reads the same order either way, which is
    // exactly why the first version of this case passed while the row looked wrong: the invariant
    // that catches it is POSITIONAL.
    const auto buttonsOccupyTheFirstSlots = [&] {
        bool seenNonButton = false;
        for (int i = 0; i < qat->layout()->count(); ++i) {
            const bool isButton = qobject_cast<QToolButton*>(qat->layout()->itemAt(i)->widget()) != nullptr;
            if (!isButton) seenNonButton = true;
            else if (seenNonButton) return false;   // a button after the stretch
        }
        return true;
    };
    CHECK(buttonsOccupyTheFirstSlots());

    // LOCKED, and the same drag does nothing at all.
    qat->setLocked(true);
    CHECK(qat->isLocked());

    const std::vector<ActionId> locked = qat->renderedCommands();
    QToolButton* first = nullptr;
    for (int i = 0; i < qat->layout()->count() && !first; ++i)
        first = qobject_cast<QToolButton*>(qat->layout()->itemAt(i)->widget());
    REQUIRE(first != nullptr);
    dragTo(first, pastTheEnd);

    CHECK(qat->renderedCommands() == locked);
    CHECK(qat->commands() == locked);
}

TEST_CASE("locking is a command, is remembered, and a reset clears it", "[qat]")
{
    Fixture fixture;
    {
        ui::MainWindow window(fixture.services());
        ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
        REQUIRE(qat != nullptr);
        CHECK_FALSE(qat->isLocked());              // unlocked by default: dragging is the expectation

        REQUIRE(fixture.actions.dispatch(ActionId::UiQatLock, core::KeyContext::Global));
        CHECK(qat->isLocked());
        CHECK(fixture.actions.isChecked(ActionId::UiQatLock));   // the menu renders a check mark
    }

    // A SECOND WINDOW ON THE SAME SETTINGS reads it back -- the same path a relaunch takes.
    {
        ui::MainWindow window(fixture.services());
        ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
        REQUIRE(qat != nullptr);
        CHECK(qat->isLocked());

        // And a reset unlocks: a reset that left the toolbar locked would not be one.
        REQUIRE(fixture.actions.dispatch(ActionId::UiResetCustomization, core::KeyContext::Global));
        CHECK_FALSE(qat->isLocked());
        CHECK(qat->commands() == ui::QuickAccessToolbar::factoryDefaults());
    }
}

TEST_CASE("the move commands stay reachable although the menu no longer offers them", "[qat]")
{
    // THE RULE THIS FEATURE COLLIDED WITH, asserted rather than remembered: `CLAUDE.md` says no
    // feature may exist that the headless batch path cannot reach, and a drag reaches nothing but a
    // pointer. So the menu drops the two move items -- the gesture replaces them -- and the COMMANDS
    // stay registered, which is what keeps reordering available to flow K and to a chord.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
    REQUIRE(qat != nullptr);
    window.show();

    CHECK(fixture.actions.isRegistered(ActionId::UiQatMoveLeft));
    CHECK(fixture.actions.isRegistered(ActionId::UiQatMoveRight));

    const std::vector<ActionId> before = qat->commands();
    qat->setPendingTarget(before.at(1));
    REQUIRE(fixture.actions.dispatch(ActionId::UiQatMoveLeft, core::KeyContext::Global));
    CHECK(qat->commands().front() == before.at(1));

    // The menu over a toolbar button offers Remove and the toolbar-wide items, and no Move.
    auto* button = qat->findChild<QToolButton*>();
    REQUIRE(button != nullptr);
    const QPoint at = button->geometry().center();
    QContextMenuEvent rightClick(QContextMenuEvent::Mouse, at, qat->mapToGlobal(at));
    QApplication::sendEvent(qat, &rightClick);

    QMenu* menu = qat->findChild<QMenu*>();
    REQUIRE(menu != nullptr);
    bool sawRemove = false, sawLock = false;
    for (QAction* action : menu->actions())
        if (auto* command = qobject_cast<ui::RawPageAction*>(action)) {
            CHECK(command->id() != ActionId::UiQatMoveLeft);
            CHECK(command->id() != ActionId::UiQatMoveRight);
            if (command->id() == ActionId::UiQatRemove) sawRemove = true;
            if (command->id() == ActionId::UiQatLock)   sawLock = true;
        }
    CHECK(sawRemove);
    CHECK(sawLock);
    menu->close();
}

TEST_CASE("a customization survives being written and read back", "[qat]")
{
    // A-55. Not a restart -- that is M-11 and needs a real process -- but the whole path a restart
    // uses: the window writes to Settings on every change, and a second window built on the same
    // settings file restores from it.
    Fixture fixture;

    {
        ui::MainWindow window(fixture.services());
        ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
        REQUIRE(qat != nullptr);
        REQUIRE(qat->removeCommand(ActionId::FilePrint));
        REQUIRE(qat->addCommand(ActionId::ViewToggleSidebar));
        REQUIRE(qat->moveCommand(2, 0));
        qat->setBelowRibbon(true);
    }

    const std::vector<ActionId> expected{ActionId::ViewToggleSidebar, ActionId::FileOpen,
                                         ActionId::FileSave};
    CHECK(fixture.settings.get(core::keys::ui::qatCommands)
          == ui::UiCustomization::toStringIds(expected));

    // THE ACTION STRING IDS, not the enum values, are what is on disk. That is what makes a
    // customization survive an insertion into ActionId.def -- and, through D1-16's alias rows, a
    // rename in a later stage.
    CHECK(fixture.settings.get(core::keys::ui::qatCommands).first()
          == QStringLiteral("view.toggleSidebar"));

    ui::MainWindow restored(fixture.services());
    REQUIRE(restored.quickAccessToolbar() != nullptr);
    CHECK(restored.quickAccessToolbar()->commands() == expected);
    CHECK(restored.quickAccessToolbar()->renderedCommands() == expected);
    CHECK(restored.quickAccessToolbar()->isBelowRibbon());
}

TEST_CASE("a toolbar the user emptied stays empty and a fresh profile gets the defaults", "[qat]")
{
    // The distinction `Settings::isSet` exists for. Both states read back as an empty list, and
    // only one of them means "give them the factory toolbar" -- without the difference, a user who
    // cleared the toolbar would find it repopulated on every launch.
    Fixture fixture;
    CHECK_FALSE(fixture.settings.isSet(core::keys::ui::qatCommands));

    {
        ui::MainWindow window(fixture.services());
        ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
        REQUIRE(qat != nullptr);
        qat->setCommands({});
    }

    CHECK(fixture.settings.isSet(core::keys::ui::qatCommands));
    CHECK(fixture.settings.get(core::keys::ui::qatCommands).isEmpty());

    ui::MainWindow restored(fixture.services());
    CHECK(restored.quickAccessToolbar()->commands().empty());
    CHECK(restored.quickAccessToolbar()->renderedCommands().empty());
}

TEST_CASE("the customization blob round-trips and keeps a newer version's fields", "[qat]")
{
    // D1-16 in both directions, on the third of the three vocabularies Stage 1 writes to disk. The
    // downgrade half is the one people forget: an older build that rewrites a newer build's file
    // must not silently delete what it did not understand.
    Fixture fixture;

    ui::UiCustomizationData data;
    data.qatCommands    = QStringList{QStringLiteral("file.open"), QStringLiteral("view.fullScreen")};
    data.qatBelowRibbon = true;
    data.shortcuts.push_back(
        core::KeyBindingStore::OverrideEntry{core::KeyContext::Global, "view.toggleSidebar", "Ctrl+Alt+B"});

    const QByteArray bytes = ui::UiCustomization::serialize(data);
    const core::Result<ui::UiCustomizationData> parsed = ui::UiCustomization::parse(bytes);
    REQUIRE(parsed);
    CHECK(parsed.value().version == ui::kUiCustomizationVersion);
    CHECK(parsed.value().qatCommands == data.qatCommands);
    CHECK(parsed.value().qatBelowRibbon);
    REQUIRE(parsed.value().shortcuts.size() == 1);
    CHECK(parsed.value().shortcuts.front().actionStringId == "view.toggleSidebar");
    CHECK(parsed.value().shortcuts.front().chord == "Ctrl+Alt+B");

    // A file from a build that knows more than this one.
    const QByteArray newer =
        QByteArrayLiteral("{\n  \"version\": 99,\n  \"qatCommands\": [\"file.open\"],\n"
                          "  \"ribbonTabs\": [\"a tab kind we have never heard of\"]\n}\n");
    const core::Result<ui::UiCustomizationData> fromNewer = ui::UiCustomization::parse(newer);
    REQUIRE(fromNewer);
    CHECK(fromNewer.value().version == 99);
    CHECK(fromNewer.value().extras.contains(QStringLiteral("ribbonTabs")));

    // Re-serialized by THIS build, the unknown member is still there. That is the whole claim.
    const core::Result<ui::UiCustomizationData> reparsed =
        ui::UiCustomization::parse(ui::UiCustomization::serialize(fromNewer.value()));
    REQUIRE(reparsed);
    CHECK(reparsed.value().extras.contains(QStringLiteral("ribbonTabs")));

    // And a blob that is not one of ours is an error rather than an empty customization, because
    // "imported successfully, everything reset" is indistinguishable from a reset the user asked
    // for.
    CHECK_FALSE(ui::UiCustomization::parse(QByteArrayLiteral("{\"qatCommands\": []}")));
    CHECK_FALSE(ui::UiCustomization::parse(QByteArrayLiteral("not json at all")));
}

TEST_CASE("an applied customization reports the shortcuts it DROPPED and not the ones it kept", "[qat]")
{
    // STAGE 2 ISSUE I-40. `applyToSettings` read `KeyBindingStore::importOverrides`' return -- the
    // number DROPPED -- as the number accepted and subtracted it from the total, so this report was
    // the exact opposite of the truth in both directions. Two blobs, because either alone can be
    // satisfied by the wrong arithmetic: all-good must report 0 (the defect said 2), and one bad in
    // three must report 1 (the defect said 2 as well). The second is the control on the first.
    Fixture fixture;

    ui::UiCustomizationData clean;
    clean.version = ui::kUiCustomizationVersion;
    clean.shortcuts.push_back(
        core::KeyBindingStore::OverrideEntry{core::KeyContext::Global, "view.toggleSidebar", "Ctrl+Alt+B"});
    clean.shortcuts.push_back(
        core::KeyBindingStore::OverrideEntry{core::KeyContext::Global, "file.open", "Ctrl+Alt+O"});

    const ui::UiCustomization::ApplyReport cleanReport =
        ui::UiCustomization::applyToSettings(clean, fixture.settings, fixture.keyBindings);
    CHECK(cleanReport.droppedShortcuts == 0);
    CHECK(fixture.keyBindings.exportOverrides().size() == 2);

    ui::UiCustomizationData partial = clean;
    partial.shortcuts.push_back(core::KeyBindingStore::OverrideEntry{
        core::KeyContext::Global, "file.thisWasRemovedInStage7", "Ctrl+Alt+R"});

    const ui::UiCustomization::ApplyReport partialReport =
        ui::UiCustomization::applyToSettings(partial, fixture.settings, fixture.keyBindings);
    CHECK(partialReport.droppedShortcuts == 1);
    CHECK(fixture.keyBindings.exportOverrides().size() == 2);
}

TEST_CASE("a saved command whose id no longer exists is dropped and named", "[qat]")
{
    // What D1-16's alias rows exist to prevent, seen from the other side: an id that has no alias
    // is dropped rather than crashing the restore or leaving a nameless button.
    QStringList dropped;
    const std::vector<ActionId> resolved = ui::UiCustomization::resolveCommands(
        QStringList{QStringLiteral("file.open"), QStringLiteral("file.thisWasRemovedInStage7"),
                    QStringLiteral("file.open")},
        &dropped);

    REQUIRE(resolved.size() == 1);
    CHECK(resolved.front() == ActionId::FileOpen);
    REQUIRE(dropped.size() == 1);
    CHECK(dropped.first() == QStringLiteral("file.thisWasRemovedInStage7"));
}
