// Stage 1 T14 -- the Preferences dialog, its generated pages, and the Shortcuts page.
//
// Guide cases A-90 (the pages are generated from `SettingsSchema`), A-91 (a change propagates and
// persists -- flow J), and A-92 (the Shortcuts page edits `KeyBindingStore`, shows shadows, and a
// remap takes effect with nothing rebuilt).
//
// Offscreen, so nothing here judges where anything sits. What is asserted is that the rows come
// from the schema rather than from a hand-written form, that turning a control writes the settings
// file, and that a chord typed into the page is the chord the keyboard then answers to.

#include <catch2/catch_test_macros.hpp>

#include "app/Services.h"
#include "tests/ui/ShellFixture.h"
#include "core/LogBuffer.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"
#include "platform/platform.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/dialogs/SettingsPage.h"
#include "ui/dialogs/ShortcutsPage.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTemporaryDir>

#include <algorithm>
#include <set>
#include <string>

using namespace rawpage;

namespace {

using core::ActionId;
using core::SettingsGroup;

struct Fixture : uitest::ShellFixture {
    // Every member and `services()` come from `tests/ui/ShellFixture.h` (S2 T13). They were seven
    // copies of the same eight fields until `app::Services` grew a render pipeline and the copies
    // became a seven-way edit; the header states what a fixture deliberately does NOT provide.
};

} // namespace

// ===============================================================================================
// A-90 -- the pages are GENERATED
// ===============================================================================================

TEST_CASE("every declared key in a shipped group gets a row, and nothing else does", "[preferences]")
{
    // THE CASE THE WHOLE DESIGN TURNS ON. The plan requires each page to be built FROM
    // `SettingsSchema` rather than hand-laid-out, and the difference between the two is invisible
    // by inspection: a hand-written form for today's five General keys looks exactly like a
    // generated one. What tells them apart is asking the SCHEMA what should be there and comparing.
    // A key added to `SettingsKeys.def` in a later stage either appears here or fails this case.
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());

    for (const SettingsGroup group : ui::PreferencesDialog::shippedPages()) {
        if (group == SettingsGroup::Shortcuts) continue;   // not a list of keys -- see below

        ui::SettingsPage* page = dialog.settingsPage(group);
        INFO("group " << core::settingsGroupName(group));
        REQUIRE(page != nullptr);

        std::set<QString> expected;
        for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys()) {
            if (info.group != group) continue;
            expected.insert(QString::fromUtf8(info.path.data(),
                                              static_cast<qsizetype>(info.path.size())));
        }
        REQUIRE_FALSE(expected.empty());

        const QStringList rendered = page->rowPaths();
        std::set<QString> actual(rendered.begin(), rendered.end());

        // BOTH DIRECTIONS, and the second is the half that protects the design: "every declared key
        // has a row" is equally satisfied by a page that also hard-codes three rows of its own, and
        // the moment one exists, adding a preference stops being a `.def` row while this case stays
        // green. That is exactly the reasoning A-50 applies to the ribbon.
        CHECK(actual == expected);
    }
}

TEST_CASE("each row's editor is the one its type asks for", "[preferences]")
{
    // The mapping lives in `SettingsEditorTraits` and the page dispatches on it with `if
    // constexpr`, so a type given a new editor kind and no branch fails to compile. What a test
    // adds is the other direction: that the branch actually built the widget the kind names.
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());

    ui::SettingsPage* general = dialog.settingsPage(SettingsGroup::General);
    REQUIRE(general != nullptr);

    // Choice -> a combo box carrying the codec's own tokens, so it cannot offer a value the file
    // cannot hold.
    auto* theme = qobject_cast<QComboBox*>(general->editorFor(QStringLiteral("general/theme")));
    REQUIRE(theme != nullptr);
    CHECK(theme->count() == 3);
    CHECK(theme->findData(QStringLiteral("followSystem")) >= 0);

    // Decimal -> a double spin box whose bounds are the SCHEMA's range rather than a number chosen
    // in the widget code. A range that lived in two places would be a clamp the user could not see.
    auto* scale =
        qobject_cast<QDoubleSpinBox*>(general->editorFor(QStringLiteral("general/uiFontScale")));
    REQUIRE(scale != nullptr);
    CHECK(scale->minimum() == 0.75);
    CHECK(scale->maximum() == 2.0);

    // Toggle -> a check box.
    CHECK(qobject_cast<QCheckBox*>(general->editorFor(QStringLiteral("general/touchMode"))) != nullptr);

    ui::SettingsPage* logging = dialog.settingsPage(SettingsGroup::Logging);
    REQUIRE(logging != nullptr);

    // Integer -> a spin box, bounded by the schema.
    auto* retain = qobject_cast<QSpinBox*>(logging->editorFor(QStringLiteral("logging/retainFiles")));
    REQUIRE(retain != nullptr);
    CHECK(retain->minimum() == 1);
    CHECK(retain->maximum() == 100);

    // Flags -> one check box per member of the closed set, which is seven for the log subsystems.
    QWidget* subsystems = logging->editorFor(QStringLiteral("logging/subsystems"));
    REQUIRE(subsystems != nullptr);
    CHECK(subsystems->findChildren<QCheckBox*>().size() == static_cast<qsizetype>(core::kLogSubsystemCount));
}

TEST_CASE("every key on a shipped page has a label the catalog can resolve", "[preferences]")
{
    // The label key is DERIVED from the path (`core::settingsLabelKey`), which makes "every
    // preference has a name" mechanically checkable rather than a thing somebody remembers. A key
    // with no catalog entry resolves to itself, which is what this looks for -- a row labelled
    // `prefs.setting.general.theme` is a defect nobody would notice in an `en` screenshot review
    // three stages later.
    for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys()) {
        if (info.group == SettingsGroup::Internal) continue;

        const QString key = QString::fromStdString(info.labelKey);
        INFO("settings label " << info.labelKey);
        CHECK(ui::catalog::translate(key) != key);

        for (const core::SettingsChoice& choice : info.choices) {
            const QString choiceKey = QString::fromStdString(choice.labelKey);
            INFO("choice label " << choice.labelKey);
            CHECK(ui::catalog::translate(choiceKey) != choiceKey);
        }
    }

    // And the page names and the context names, which are derived the same way from two other
    // enums. Every context, not only the ones a Stage 1 shell can reach: the Shortcuts page offers
    // all twelve, so a missing one is a combo box entry reading `prefs.context.FormsFill`.
    for (const SettingsGroup group : ui::PreferencesDialog::shippedPages()) {
        const QString key = ui::catalog::settingsGroupKey(group);
        INFO("group name " << key.toStdString());
        CHECK(ui::catalog::translate(key) != key);
    }
    for (std::size_t i = 0; i < core::kKeyContextCount; ++i) {
        const QString key = ui::catalog::keyContextKey(static_cast<core::KeyContext>(i));
        INFO("context name " << key.toStdString());
        CHECK(ui::catalog::translate(key) != key);
    }
}

TEST_CASE("the Internal group has no page and no row anywhere", "[preferences]")
{
    // `SettingsKeys.def` rule 4: internal state lives in the settings file and is versioned with
    // it, and a user never sets it. `meta/schemaVersion` on a Preferences page would be an
    // invitation to break the migration chain by hand.
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());

    CHECK(dialog.settingsPage(SettingsGroup::Internal) == nullptr);

    const auto& shipped = ui::PreferencesDialog::shippedPages();
    CHECK(std::find(shipped.begin(), shipped.end(), SettingsGroup::Internal) == shipped.end());

    for (const SettingsGroup group : shipped) {
        if (group == SettingsGroup::Shortcuts) continue;
        ui::SettingsPage* page = dialog.settingsPage(group);
        REQUIRE(page != nullptr);
        CHECK_FALSE(page->rowPaths().contains(QStringLiteral("meta/schemaVersion")));
        CHECK_FALSE(page->rowPaths().contains(QStringLiteral("ui/qatCommands")));
    }
}

// ===============================================================================================
// A-91 -- a change propagates (flow J) and persists
// ===============================================================================================

TEST_CASE("turning a control writes the setting, and the setting reaches the file", "[preferences]")
{
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());

    ui::SettingsPage* general = dialog.settingsPage(SettingsGroup::General);
    REQUIRE(general != nullptr);

    auto* theme = qobject_cast<QComboBox*>(general->editorFor(QStringLiteral("general/theme")));
    REQUIRE(theme != nullptr);

    REQUIRE(fixture.settings.get(core::keys::general::theme) == core::Theme::FollowSystem);
    theme->setCurrentIndex(theme->findData(QStringLiteral("dark")));

    // THE WIDGET IS DRIVEN, not `Settings::set`. A test that called the setter would assert that
    // the settings store works, which T5 already proved; what is unproven is that the control is
    // wired to it at all.
    CHECK(fixture.settings.get(core::keys::general::theme) == core::Theme::Dark);

    // And it reaches disk. `Settings` accumulates dirty keys and commits on `sync()`, which
    // `CompositionRoot` calls at teardown (issue I-14) -- so a preference that is correct in memory
    // and absent from the file is exactly the failure this stage has already had once.
    REQUIRE(fixture.settings.sync());

    core::Settings reloaded(fixture.iniPath);
    CHECK(reloaded.get(core::keys::general::theme) == core::Theme::Dark);
}

TEST_CASE("a change made elsewhere refreshes the page", "[preferences]")
{
    // Flow J in the direction nobody remembers to build. The second window's page, a
    // `restoreDefaults()`, and from Stage 15 a policy refresh all change a value this page is
    // showing; without the subscription the control would keep displaying the old one until the
    // dialog was closed and reopened.
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());

    ui::SettingsPage* general = dialog.settingsPage(SettingsGroup::General);
    REQUIRE(general != nullptr);
    auto* scale =
        qobject_cast<QDoubleSpinBox*>(general->editorFor(QStringLiteral("general/uiFontScale")));
    REQUIRE(scale != nullptr);

    fixture.settings.set(core::keys::general::uiFontScale, 1.5);
    CHECK(scale->value() == 1.5);

    // A CLAMP IS VISIBLE TOO. The schema caps this key at 2.0 and `Settings` clamps rather than
    // refuses (A-43); a page that showed 9.0 while the product used 2.0 would make the clamp
    // undiagnosable from the one screen built to explain it.
    fixture.settings.set(core::keys::general::uiFontScale, 9.0);
    CHECK(scale->value() == 2.0);
}

TEST_CASE("restoring a page's defaults resets its keys and leaves other pages alone", "[preferences]")
{
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());

    fixture.settings.set(core::keys::general::theme, core::Theme::Dark);
    fixture.settings.set(core::keys::general::touchMode, true);
    fixture.settings.set(core::keys::logging::retainFiles, 42);

    ui::SettingsPage* general = dialog.settingsPage(SettingsGroup::General);
    REQUIRE(general != nullptr);
    general->restoreDefaults();

    CHECK(fixture.settings.get(core::keys::general::theme) == core::Theme::FollowSystem);
    CHECK(fixture.settings.get(core::keys::general::touchMode) == false);

    // The other page is UNTOUCHED. "Restore defaults" on the General page resetting the log level
    // as well would be a button that quietly does more than it says.
    CHECK(fixture.settings.get(core::keys::logging::retainFiles) == 42);

    // And the widgets followed. `changed` fires on an ACTUAL change only (A-42), so a page whose
    // values were all already default gets no notification at all -- which is why the page
    // refreshes itself once at the end rather than relying on the subscription.
    auto* theme = qobject_cast<QComboBox*>(general->editorFor(QStringLiteral("general/theme")));
    REQUIRE(theme != nullptr);
    CHECK(theme->currentData().toString() == QStringLiteral("followSystem"));
}

// ===============================================================================================
// A-92 -- the Shortcuts page
// ===============================================================================================

TEST_CASE("the shortcuts page shows one row per binding in the chosen context", "[preferences]")
{
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());

    ui::ShortcutsPage* page = dialog.shortcutsPage();
    REQUIRE(page != nullptr);
    CHECK(page->context() == core::KeyContext::Global);

    // Against the STORE, not against a number written here: §8 grows, and a case that hard-coded
    // the Global row count would fail for a documentation edit rather than for a defect.
    CHECK(static_cast<std::size_t>(page->rowCount())
          == fixture.keyBindings.bindingsFor(core::KeyContext::Global).size());

    page->setContext(core::KeyContext::Viewer);
    CHECK(page->context() == core::KeyContext::Viewer);
    CHECK(static_cast<std::size_t>(page->rowCount())
          == fixture.keyBindings.bindingsFor(core::KeyContext::Viewer).size());
}

TEST_CASE("a remap through the page reaches the store and the settings file", "[preferences]")
{
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());
    ui::ShortcutsPage* page = dialog.shortcutsPage();
    REQUIRE(page != nullptr);

    // Find `file.preferences`, whose §8 Global default is Ctrl+K.
    int row = -1;
    for (int i = 0; i < page->rowCount(); ++i) {
        if (page->actionAt(i) == ActionId::FilePreferences) { row = i; break; }
    }
    REQUIRE(row >= 0);
    CHECK(page->chordAt(row) == core::Chord::parse("Ctrl+K"));
    CHECK_FALSE(page->isOverriddenAt(row));

    const std::optional<core::Chord> chord = core::Chord::parse("Ctrl+Alt+P");
    REQUIRE(chord);
    REQUIRE(page->rebindRow(row, *chord));

    // THE STORE, so the keyboard answers to it immediately -- nothing was rebuilt.
    CHECK(fixture.keyBindings.lookup(*chord, core::KeyContext::Global) == ActionId::FilePreferences);

    // AND THE SETTINGS, so it is still there tomorrow. A remap that reached one and not the other
    // is a feature the user calls broken either way -- see ShortcutsPage.h.
    REQUIRE(fixture.settings.sync());
    core::Settings reloaded(fixture.iniPath);
    core::KeyBindingStore restored;
    restored.importOverrides(reloaded.shortcutOverrides());
    CHECK(restored.lookup(*chord, core::KeyContext::Global) == ActionId::FilePreferences);

    // AND THE OLD CHORD IS GONE. This is the T14 rule in `KeyBindingStore.h`: the persisted family
    // holds one chord per action per context, so an override REPLACES rather than adds. Before
    // T14, `Ctrl+K` would still have opened Preferences and the setting would have been a
    // half-truth that a restart resolved at random.
    const std::optional<core::Chord> ctrlK = core::Chord::parse("Ctrl+K");
    REQUIRE(ctrlK);
    CHECK_FALSE(fixture.keyBindings.lookup(*ctrlK, core::KeyContext::Global).has_value());
    CHECK_FALSE(restored.lookup(*ctrlK, core::KeyContext::Global).has_value());
}

TEST_CASE("resetting a row gives the key back", "[preferences]")
{
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());
    ui::ShortcutsPage* page = dialog.shortcutsPage();
    REQUIRE(page != nullptr);

    int row = -1;
    for (int i = 0; i < page->rowCount(); ++i) {
        if (page->actionAt(i) == ActionId::FilePreferences) { row = i; break; }
    }
    REQUIRE(row >= 0);

    const std::optional<core::Chord> chord = core::Chord::parse("Ctrl+Alt+P");
    REQUIRE(chord);
    REQUIRE(page->rebindRow(row, *chord));

    // The row on screen is now the NEW chord, so a reset that cleared "whatever this row shows"
    // would work here and fail on the freed default row. `ShortcutsPage::resetRow` clears by the
    // action's override chord instead, which is why this case then looks for the row again.
    int after = -1;
    for (int i = 0; i < page->rowCount(); ++i) {
        if (page->actionAt(i) == ActionId::FilePreferences) { after = i; break; }
    }
    REQUIRE(after >= 0);
    CHECK(page->isOverriddenAt(after));

    page->resetRow(after);

    const std::optional<core::Chord> ctrlK = core::Chord::parse("Ctrl+K");
    REQUIRE(ctrlK);
    CHECK(fixture.keyBindings.lookup(*ctrlK, core::KeyContext::Global) == ActionId::FilePreferences);
    CHECK_FALSE(fixture.keyBindings.lookup(*chord, core::KeyContext::Global).has_value());
}

TEST_CASE("the page refuses a remap onto a chord the focused widget owns", "[preferences]")
{
    // `KeyBindingStore`'s one refusal, seen from the surface that would otherwise offer it: binding
    // a command to `Tab` inside Forms — Fill takes the key away from the focus chain and leaves the
    // user unable to move between fields. The page must report that rather than appear to succeed.
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());
    ui::ShortcutsPage* page = dialog.shortcutsPage();
    REQUIRE(page != nullptr);

    page->setContext(core::KeyContext::FormsFill);

    int native = -1;
    int dispatched = -1;
    for (int i = 0; i < page->rowCount(); ++i) {
        if (!page->actionAt(i)) {
            if (native < 0) native = i;
        } else if (dispatched < 0) {
            dispatched = i;
        }
    }
    REQUIRE(native >= 0);
    REQUIRE(dispatched >= 0);

    // A widget-native row is SHOWN -- §8 records those chords and hiding them would make the page
    // disagree with the document it is a view of -- and it cannot be the target of a rebind,
    // because it names no action to rebind.
    CHECK_FALSE(page->rebindRow(native, *core::Chord::parse("Ctrl+Alt+Y")));

    // And remapping a real command ONTO the native chord is the refusal the store makes.
    const std::optional<core::Chord> nativeChord = page->chordAt(native);
    REQUIRE(nativeChord);
    CHECK_FALSE(page->rebindRow(dispatched, *nativeChord));
}

TEST_CASE("the shadow report names the six shadows the document ships and no more", "[preferences]")
{
    // Class 1 only -- a context redefining a chord `Global` also claims. §8 ships six of those BY
    // DESIGN, so a page that refused such a remap would refuse the product's own defaults; it
    // reports instead. Asserted against the store's own census rather than against a list here,
    // which is what keeps this case about the PAGE.
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());
    ui::ShortcutsPage* page = dialog.shortcutsPage();
    REQUIRE(page != nullptr);

    std::size_t reported = 0;
    for (std::size_t i = 0; i < core::kKeyContextCount; ++i) {
        page->setContext(static_cast<core::KeyContext>(i));
        reported += static_cast<std::size_t>(page->shadowSummary().size());
    }

    // Every entry the store reports reaches a page, and no page invents one.
    CHECK(reported == fixture.keyBindings.shadows().size());

    // AND THE SIX ARE SIX CHORDS, NOT SIX ENTRIES -- which is the distinction this case learned by
    // getting it wrong. `Features.md` §8 and `KeyBindingStore`'s own header both state the class-1
    // census as six CHORDS (Ctrl+D, Ctrl+K, F11, Ctrl+Shift+L, Ctrl+Shift+A, Ctrl+Shift+G), and one
    // of those is redefined in two different contexts -- so the store returns seven ENTRIES over
    // six chords, and both numbers are correct about different things. Conflating two counts of the
    // same census is exactly what made the documented figure look wrong at I-01, so this asserts
    // both rather than picking whichever one happened to match.
    std::set<std::string> chords;
    for (const core::Shadow& shadow : fixture.keyBindings.shadows())
        chords.insert(shadow.chord.toString());
    CHECK(chords.size() == 6);
    CHECK(reported >= chords.size());

    // Global itself shadows nothing -- it is the layer being shadowed.
    page->setContext(core::KeyContext::Global);
    CHECK(page->shadowSummary().isEmpty());
}

TEST_CASE("restoring shortcut defaults clears every context, not just the one on screen",
          "[preferences]")
{
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());
    ui::ShortcutsPage* page = dialog.shortcutsPage();
    REQUIRE(page != nullptr);

    const std::optional<core::Chord> chord = core::Chord::parse("Ctrl+Alt+J");
    REQUIRE(chord);
    REQUIRE(fixture.keyBindings.rebind(*chord, core::KeyContext::Viewer, ActionId::ViewZoomIn));
    fixture.settings.setShortcutOverride(
        core::ShortcutKey::forAction(core::KeyContext::Viewer, ActionId::ViewZoomIn),
        QStringLiteral("Ctrl+Alt+J"));

    page->setContext(core::KeyContext::Global);   // a DIFFERENT context is on screen
    page->restoreDefaults();

    CHECK_FALSE(fixture.keyBindings.lookup(*chord, core::KeyContext::Viewer).has_value());
    CHECK_FALSE(fixture.settings
                    .shortcutOverride(core::ShortcutKey::forAction(core::KeyContext::Viewer,
                                                                   ActionId::ViewZoomIn))
                    .has_value());
}

// ===============================================================================================
// The command, and the window
// ===============================================================================================

TEST_CASE("file.preferences dispatches headlessly and raises the dialog", "[preferences]")
{
    // The difference between this command and `file.open`'s, and the reason it needs no enablement
    // predicate: a modeless dialog blocks nothing. Issue I-23's `canPromptTheUser` exists for a
    // modal `exec()` that never returns when there is nobody to answer it, and that is precisely
    // what stopped a whole `ctest` run dead at T12. Dispatching this one under `offscreen` is the
    // assertion that the same trap was not dug twice.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.show();

    REQUIRE(window.preferences() == nullptr);
    REQUIRE(fixture.actions.dispatch(ActionId::FilePreferences, core::KeyContext::Global));

    ui::PreferencesDialog* dialog = window.preferences();
    REQUIRE(dialog != nullptr);
    CHECK(dialog->isVisible());

    // Dispatched twice, the SAME dialog comes back -- built on first use and kept, so reopening
    // returns to the page the user was last on.
    dialog->showPage(SettingsGroup::Shortcuts);
    REQUIRE(fixture.actions.dispatch(ActionId::FilePreferences, core::KeyContext::Global));
    CHECK(window.preferences() == dialog);
    CHECK(dialog->currentPage() == SettingsGroup::Shortcuts);
}

TEST_CASE("showing the window with the dialog open keeps both self-checks clean", "[preferences]")
{
    // THE CASE THAT WOULD OTHERWISE BE FOUND BY A USER. `MainWindow::showEvent` runs D1-7's two
    // self-checks over `findChildren`, and `findChildren` is recursive through the QObject parent
    // tree -- so a dialog parented to the window is INSIDE the walk even though it is its own
    // top-level window. Any Qt-manufactured action in there (`QLineEdit`'s clear button is the one
    // that caught T11 out, issue I-22) or any stray key sequence would assert in a debug build the
    // second time somebody showed the window, and never during a run that opened Preferences last.
    //
    // Showing AFTER the dialog exists is the whole point of the case; the case above shows first.
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    REQUIRE(fixture.actions.dispatch(ActionId::FilePreferences, core::KeyContext::Global));
    REQUIRE(window.preferences() != nullptr);
    window.preferences()->showPage(SettingsGroup::Shortcuts);   // build the table and the capture box

    window.show();

    const QList<QString> wiring = ui::ActionFactory::wiringComplaints(&window);
    INFO(wiring.join(QStringLiteral(" | ")).toStdString());
    CHECK(wiring.isEmpty());

    const QList<QString> keys = ui::ShortcutBinder::shortcutComplaints(&window);
    INFO(keys.join(QStringLiteral(" | ")).toStdString());
    CHECK(keys.isEmpty());
}
