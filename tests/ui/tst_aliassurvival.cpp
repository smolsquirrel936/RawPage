// tst_aliassurvival -- what a USER keeps across an action rename. Stage 2 guide case A-73's second
// clause, and issue I-39.
//
// ===============================================================================================
// THE DEFECT THESE CASES WERE WRITTEN AGAINST
// ===============================================================================================
//
// D1-16 promises that a renamed action keeps a user's saved shortcuts and Quick Access Toolbar.
// Until I-39 the product kept only the READ half: a `shortcuts/Global/<retired>` key resolved at
// load and the remap worked. But every WRITE builds its key from the enum, so the Shortcuts page's
// reset cleared `shortcuts/Global/<current>` -- a key that did not exist -- and the retired key
// stayed in the file. **The user reset their shortcut, and it came back on the next launch.**
// `tst_actionalias.cpp` could not see it, because it drives the resolution algorithm and nothing
// that persists; no other case could, because the shipped alias table has no rows.
//
// The fix is in the loader (`Settings::migrateRetiredActionIds`): a retired spelling is rewritten
// under the current one at load, so nothing downstream ever meets it. These cases drive the two
// surfaces a user acts through -- the real `ShortcutsPage` and the real `MainWindow` toolbar
// restore -- over a file saved under a retired spelling, and then relaunch.
//
// ===============================================================================================
// WHAT THESE CASES DO NOT COVER (convention 19)
// ===============================================================================================
//
//   * **The composition root's import.** `app/CompositionRoot.cpp` hands `Settings::shortcutOverrides`
//     to `KeyBindingStore::importOverrides` at startup; a test has no composition root, so each case
//     below makes that one call itself. The line is the same call and is not what I-39 was about.
//   * **The shipped alias table.** Every case loads through `tests/unit/fixtures/ActionAliasFixture.def`.
//     The product composes `Settings` with the shipped table, which has zero rows, so the first real
//     rename is still the first time a real row meets a user's file.
//   * **A relaunch is a second `Settings` on the same file**, not a second process. Guide case A-55
//     (`tst_qat.cpp`) makes the same substitution for the same reason.

#include <catch2/catch_test_macros.hpp>

#include "tests/ui/ShellFixture.h"
#include "tests/unit/fixtures/ActionAliasFixture.h"
#include "core/cmd/Chord.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/Settings.h"
#include "ui/dialogs/ShortcutsPage.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/QuickAccessToolbar.h"
#include "ui/shell/UiCustomization.h"

#include <QFile>

#include <array>
#include <optional>
#include <vector>

using namespace rawpage;
using core::ActionId;
using core::KeyContext;

namespace {

// `view.sidebar.toggle` is the fixture's more recent retired spelling of `view.toggleSidebar`, whose
// §8 Global default is F4.
const QString kSavedUnderRetiredSpelling = QStringLiteral(
    "[shortcuts]\n"
    "Global/view.sidebar.toggle=Ctrl+Alt+B\n");

QByteArray fileBytes(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

int rowFor(const ui::ShortcutsPage& page, ActionId action)
{
    for (int i = 0; i < page.rowCount(); ++i)
        if (page.actionAt(i) == action) return i;
    return -1;
}

// The relaunch: a fresh `Settings` on the same file, and a fresh store fed from it the way the
// composition root feeds one.
core::KeyBindingStore relaunchBindings(const QString& iniPath)
{
    core::Settings reloaded(iniPath, testfixture::kFixtureAliases);
    core::KeyBindingStore restored;
    CHECK(restored.importOverrides(reloaded.shortcutOverrides()) == 0);
    return restored;
}

} // namespace

TEST_CASE("a shortcut saved under a retired spelling survives the rename", "[alias][preferences]")
{
    uitest::ShellFixture fixture(kSavedUnderRetiredSpelling, testfixture::kFixtureAliases);
    CHECK(fixture.keyBindings.importOverrides(fixture.settings.shortcutOverrides()) == 0);

    const core::Chord saved = *core::Chord::parse("Ctrl+Alt+B");
    const core::Chord f4    = *core::Chord::parse("F4");
    CHECK(fixture.keyBindings.lookup(saved, KeyContext::Global) == ActionId::ViewToggleSidebar);
    CHECK_FALSE(fixture.keyBindings.lookup(f4, KeyContext::Global).has_value());

    // And the page SHOWS it as the user's choice, which is what they would look for.
    ui::ShortcutsPage page(fixture.settings, fixture.keyBindings);
    const int row = rowFor(page, ActionId::ViewToggleSidebar);
    REQUIRE(row >= 0);
    CHECK(page.chordAt(row) == saved);
    CHECK(page.isOverriddenAt(row));
}

TEST_CASE("resetting a shortcut saved under a retired spelling stays reset", "[alias][preferences]")
{
    // THE I-39 CASE. Before the loader renamed the key, `resetRow` cleared a key that did not exist
    // and this relaunch found Ctrl+Alt+B bound again.
    uitest::ShellFixture fixture(kSavedUnderRetiredSpelling, testfixture::kFixtureAliases);
    fixture.keyBindings.importOverrides(fixture.settings.shortcutOverrides());

    {
        ui::ShortcutsPage page(fixture.settings, fixture.keyBindings);
        const int row = rowFor(page, ActionId::ViewToggleSidebar);
        REQUIRE(row >= 0);
        REQUIRE(page.isOverriddenAt(row));
        page.resetRow(rowFor(page, ActionId::ViewToggleSidebar));
    }

    const core::KeyBindingStore restored = relaunchBindings(fixture.iniPath);
    CHECK(restored.lookup(*core::Chord::parse("F4"), KeyContext::Global) == ActionId::ViewToggleSidebar);
    CHECK_FALSE(restored.lookup(*core::Chord::parse("Ctrl+Alt+B"), KeyContext::Global).has_value());
    CHECK_FALSE(fileBytes(fixture.iniPath).contains("view.sidebar.toggle"));
}

TEST_CASE("rebinding a shortcut saved under a retired spelling keeps the new chord", "[alias][preferences]")
{
    // Before I-39 a rebind wrote the current key BESIDE the retired one, and a relaunch imported
    // both in the settings map's alphabetical order -- the later key won. With the fixture's
    // spellings the current key happens to sort later, so the chord check alone would have passed
    // then too; the FILE check is what could not have.
    uitest::ShellFixture fixture(kSavedUnderRetiredSpelling, testfixture::kFixtureAliases);
    fixture.keyBindings.importOverrides(fixture.settings.shortcutOverrides());

    const core::Chord chosen = *core::Chord::parse("Ctrl+Alt+N");
    {
        ui::ShortcutsPage page(fixture.settings, fixture.keyBindings);
        const int row = rowFor(page, ActionId::ViewToggleSidebar);
        REQUIRE(row >= 0);
        REQUIRE(page.rebindRow(row, chosen));
    }

    const core::KeyBindingStore restored = relaunchBindings(fixture.iniPath);
    CHECK(restored.lookup(chosen, KeyContext::Global) == ActionId::ViewToggleSidebar);
    CHECK_FALSE(restored.lookup(*core::Chord::parse("Ctrl+Alt+B"), KeyContext::Global).has_value());

    const QByteArray bytes = fileBytes(fixture.iniPath);
    CHECK_FALSE(bytes.contains("view.sidebar.toggle"));
    CHECK(bytes.contains("view.toggleSidebar=Ctrl+Alt+N"));
}

TEST_CASE("a toolbar saved with a retired command restores it and saves the current spelling", "[alias][qat]")
{
    // `shell.toggleSidebar` is the fixture's OLDEST spelling -- two renames back.
    const QString saved = QStringLiteral("[ui]\nqatCommands=file.open, shell.toggleSidebar\n");
    uitest::ShellFixture fixture(saved, testfixture::kFixtureAliases);

    {
        ui::MainWindow window(fixture.services());
        ui::QuickAccessToolbar* qat = window.quickAccessToolbar();
        REQUIRE(qat != nullptr);
        CHECK(qat->commands() == std::vector<ActionId>{ActionId::FileOpen, ActionId::ViewToggleSidebar});
        CHECK(qat->renderedCommands() == qat->commands());

        // Any change writes the toolbar back.
        REQUIRE(qat->addCommand(ActionId::FileSave));
    }

    CHECK_FALSE(fileBytes(fixture.iniPath).contains("shell.toggleSidebar"));

    // Read back through the SHIPPED table: it resolves because the file now says the current name.
    core::Settings shipped(fixture.iniPath);
    CHECK(ui::UiCustomization::resolveCommands(shipped.get(core::keys::ui::qatCommands), nullptr)
          == std::vector<ActionId>{ActionId::FileOpen, ActionId::ViewToggleSidebar, ActionId::FileSave});
}

TEST_CASE("without its alias row a retired toolbar command is dropped", "[alias][qat]")
{
    // THE CONTROL for the case above: the same file with no alias rows loses the command, so the
    // case above measures the alias row and not a restore that would have kept it anyway.
    const QString saved = QStringLiteral("[ui]\nqatCommands=file.open, shell.toggleSidebar\n");
    constexpr std::array<core::ActionAlias, 0> none{};
    uitest::ShellFixture fixture(saved, none);

    ui::MainWindow window(fixture.services());
    REQUIRE(window.quickAccessToolbar() != nullptr);
    CHECK(window.quickAccessToolbar()->commands() == std::vector<ActionId>{ActionId::FileOpen});
}
