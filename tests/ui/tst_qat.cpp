// Stage 1 T11 -- the Quick Access Toolbar and the customization it serializes.
//
// Guide case A-55 (a QAT customization round-trips) and the mechanical half of M-11 (it survives a
// real restart, which needs a real process and stays manual). Offscreen, so nothing here judges
// where the toolbar sits on screen; what is asserted is that the list is the state, that every
// operation goes through a command, and that what is written can be read back.

#include <catch2/catch_test_macros.hpp>

#include "app/Services.h"
#include "core/cmd/ActionRegistry.h"
#include "core/LogBuffer.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/QuickAccessToolbar.h"
#include "ui/shell/UiCustomization.h"

#include <QDir>
#include <QTemporaryDir>

#include <algorithm>

using namespace rawpage;

namespace {

using core::ActionId;

struct Fixture {
    QTemporaryDir         directory;
    core::Settings        settings{QDir(directory.path()).filePath(QStringLiteral("settings.ini"))};
    core::RecentFiles     recentFiles{settings};
    core::ActionRegistry  actions;
    core::KeyBindingStore keyBindings;
    core::LogBuffer       logBuffer;
    platform::Platform    platform;

    app::Services services()
    {
        return app::Services{settings, recentFiles, actions, keyBindings, logBuffer, platform};
    }
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

    // Two of the three defaults are declared and not yet registered -- they are on the toolbar,
    // disabled, exactly as they are on the ribbon. A default toolbar of only the commands that
    // already work would look complete and then grow silently over thirteen stages.
    CHECK_FALSE(fixture.actions.isRegistered(ActionId::FileSave));
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
