// Stage 1 T9 -- ui/shell/{MainWindow,SidebarDock,StatusBar,Catalog}.
//
// Offscreen, so none of this judges appearance: whether the sidebar looks right docked at 280
// pixels, whether the placeholder surface reads well, and whether the auto-hide slide feels like
// anything are manual cases M-02 and M-09. What is asserted here is the structure underneath --
// that commands go through the registry, that the pane remembers its width, that the status bar
// tells the truth about having no document, and that D1-2's "no content sniffing" holds.

#include <catch2/catch_test_macros.hpp>

#include "app/Services.h"
#include "core/cmd/ActionRegistry.h"
#include "core/LogBuffer.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "core/settings/SettingsSchema.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/QuickAccessToolbar.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/RawPageAction.h"
#include "ui/shell/RibbonModel.h"
#include "ui/shell/SidebarDock.h"
#include "ui/shell/StatusBar.h"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QTemporaryDir>
#include <QToolButton>

#include <set>

using namespace rawpage;

namespace {

// Everything a window needs, owned together so a case is four lines rather than fourteen. Note
// what is NOT here: no PDF engine and no OS bundle beyond an empty `Platform`, because that is the
// property flow A exists to produce -- MainWindow receives interfaces and could be built against
// mocks with no PDFium and no Win32.
struct Fixture {
    QTemporaryDir           directory;
    core::Settings          settings{QDir(directory.path()).filePath(QStringLiteral("settings.ini"))};
    core::RecentFiles       recentFiles{settings};
    core::ActionRegistry    actions;
    core::KeyBindingStore   keyBindings;
    core::LogBuffer         logBuffer;
    platform::Platform      platform;   // every interface null: the honest state of a test bundle

    app::Services services()
    {
        return app::Services{settings, recentFiles, actions, keyBindings, logBuffer, platform};
    }
};

QString writeFile(const QTemporaryDir& directory, const QString& name, const QByteArray& contents)
{
    const QString path = QDir(directory.path()).filePath(name);
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(contents);
    file.close();
    return path;
}

} // namespace

TEST_CASE("the window registers exactly the actions it can service", "[shell]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    // Nineteen, and named. Stage 1 declares 358 ids; an id that is declared and UNREGISTERED renders as
    // a visible, disabled control saying it is not built yet, which is the honest state. Registering
    // a stub handler for the other 354 would make every one of them look available.
    //
    // The count is asserted as well as the names on purpose: a name-only check stays green when a
    // fifth registration is added by accident, and "the window registers exactly what it can
    // service" is a claim about the whole set rather than about four members of it.
    CHECK(fixture.actions.registeredCount() == 19);
    CHECK(fixture.actions.isRegistered(core::ActionId::ViewToggleSidebar));
    CHECK(fixture.actions.isRegistered(core::ActionId::ViewFullScreen));
    CHECK(fixture.actions.isRegistered(core::ActionId::FileQuit));
    CHECK(fixture.actions.isRegistered(core::ActionId::ViewToggleRibbon));   // T10

    // T11's five. `ui.customize` is NOT among them and that is the point of checking the count as
    // well as the names: the customization dialog is T14, and its menu item is offered disabled
    // rather than wired to a handler that opens nothing.
    CHECK(fixture.actions.isRegistered(core::ActionId::AppToolFinder));
    CHECK(fixture.actions.isRegistered(core::ActionId::UiQatAdd));
    CHECK(fixture.actions.isRegistered(core::ActionId::UiQatRemove));
    CHECK(fixture.actions.isRegistered(core::ActionId::UiQatPlacement));
    CHECK(fixture.actions.isRegistered(core::ActionId::UiResetCustomization));
    CHECK_FALSE(fixture.actions.isRegistered(core::ActionId::UiCustomize));

    // T12's eight: the documents and the windows.
    CHECK(fixture.actions.isRegistered(core::ActionId::FileOpen));
    CHECK(fixture.actions.isRegistered(core::ActionId::FileClose));
    CHECK(fixture.actions.isRegistered(core::ActionId::FileCloseAll));
    CHECK(fixture.actions.isRegistered(core::ActionId::WindowNextTab));
    CHECK(fixture.actions.isRegistered(core::ActionId::WindowPrevTab));
    CHECK(fixture.actions.isRegistered(core::ActionId::WindowNext));
    CHECK(fixture.actions.isRegistered(core::ActionId::WindowPrev));
    CHECK(fixture.actions.isRegistered(core::ActionId::WindowNew));

    // T14's two: the Preferences dialog and the log pane. `log.openFolder` is deliberately NOT
    // among them -- the folder belongs to `app/LogFile` and `ui/` looks outward to exactly two
    // files, so opening it would have widened that list for a convenience. Checking the count as
    // well as the names is what makes that absence an assertion rather than a comment.
    CHECK(fixture.actions.isRegistered(core::ActionId::FilePreferences));
    CHECK(fixture.actions.isRegistered(core::ActionId::PaneLog));
    CHECK_FALSE(fixture.actions.isRegistered(core::ActionId::LogOpenFolder));

    // The distinction A-22 pinned down at T3, seen from the shell: a command that has not arrived
    // yet is NotFound, not a silent no-op. `file.save` is one of the ~340 that are declared and
    // unregistered; it used to be `file.open`, which T12 registered.
    const core::Result<void> notYet =
        fixture.actions.dispatch(core::ActionId::FileSave, core::KeyContext::Global);
    REQUIRE_FALSE(notYet);
    CHECK(notYet.error().code == core::ErrorCode::NotFound);

    // AND THE OTHER HALF OF THAT DISTINCTION, which T12 made load-bearing rather than academic.
    // `file.open` exists and raises a modal file picker, and there is nobody to look at one under
    // `offscreen` -- so it is registered, disabled, and refused with `Unavailable`. Without that
    // predicate this very dispatch hung the suite for ten minutes (issue I-23); with it, a headless
    // caller gets an answer it can tell apart from a typo.
    const core::Result<void> noUser =
        fixture.actions.dispatch(core::ActionId::FileOpen, core::KeyContext::Global);
    REQUIRE_FALSE(noUser);
    CHECK(noUser.error().code == core::ErrorCode::Unavailable);
}

TEST_CASE("the real window, shown, has no action that bypasses the registry", "[shell]")
{
    // D1-7(b) against the ACTUAL subject rather than a fixture. tst_actionwiring plants violations
    // to prove the check can fail; this proves the product passes it -- and the two together are
    // the pair, because either alone is satisfiable by a check that always answers the same way.
    //
    // show() matters. The self-check runs from MainWindow::showEvent, so a suite that never showed
    // the window would leave the assertion in the product untested no matter how many actions it
    // built. In a debug build the Q_ASSERT_X inside that handler fires here first; this CHECK is
    // what makes the release build report it too.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.show();

    const QList<QString> complaints = ui::ActionFactory::wiringComplaints(&window);
    for (const QString& complaint : complaints) UNSCOPED_INFO(complaint.toStdString());
    CHECK(complaints.isEmpty());

    // And the ribbon really is on the window rather than merely constructed -- 192 controls, every
    // one of them a RawPageAction, which is what makes the check above a meaningful sample. Since
    // T11 the Quick Access Toolbar's buttons carry actions too, so the figure is the ribbon's
    // controls PLUS the toolbar's commands: naming both is what keeps this a census rather than a
    // number somebody adjusted until it matched.
    REQUIRE(window.ribbon() != nullptr);
    REQUIRE(window.quickAccessToolbar() != nullptr);
    CHECK(window.findChildren<ui::RawPageAction*>().size()
          == ui::ribbon::shipped().spec.controlCount()
                 + static_cast<qsizetype>(window.quickAccessToolbar()->commands().size()));
}

TEST_CASE("toggling the sidebar goes through the registry and keeps its width", "[shell]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    ui::SidebarDock* sidebar = window.sidebar();
    REQUIRE(sidebar != nullptr);

    sidebar->setRememberedWidth(317);
    REQUIRE(sidebar->isVisible() == false);   // nothing is shown until show(); the state is what matters

    // Dispatched, not called. If this ever becomes `window.toggleSidebar()`, the ribbon (T10), the
    // chord (T13) and the batch runner (Stage 14) each need their own path to the same behaviour.
    REQUIRE(fixture.actions.dispatch(core::ActionId::ViewToggleSidebar, core::KeyContext::Global));
    REQUIRE(fixture.actions.dispatch(core::ActionId::ViewToggleSidebar, core::KeyContext::Global));

    // The width survived the round trip. Losing it is the defect people notice within a minute and
    // report as "the sidebar keeps resizing itself".
    CHECK(sidebar->rememberedWidth() == 317);
}

TEST_CASE("a width of zero is never remembered", "[shell]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    ui::SidebarDock* sidebar = window.sidebar();

    const int before = sidebar->rememberedWidth();
    sidebar->setRememberedWidth(0);
    sidebar->setRememberedWidth(-40);

    // Zero and negative are what a collapsing splitter reports on its way out. Recording either
    // means the pane comes back invisible, and an invisible pane reads as a broken build.
    CHECK(sidebar->rememberedWidth() == before);
}

TEST_CASE("auto-hide and pin round trip and peeking is not a state change", "[shell]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    ui::SidebarDock* sidebar = window.sidebar();
    window.show();

    REQUIRE_FALSE(sidebar->isAutoHidden());

    sidebar->setAutoHidden(true);
    CHECK(sidebar->isAutoHidden());
    CHECK_FALSE(sidebar->isVisible());

    // Peeking reveals it WITHOUT pinning it. Conflating the two is why a pane you glanced at stays
    // open for the rest of the session.
    sidebar->peek();
    CHECK(sidebar->isPeeking());
    CHECK(sidebar->isAutoHidden());

    sidebar->unpeek();
    CHECK_FALSE(sidebar->isPeeking());
    CHECK_FALSE(sidebar->isVisible());

    sidebar->setAutoHidden(false);
    CHECK_FALSE(sidebar->isAutoHidden());
    CHECK(sidebar->isVisible());

    // Idempotent both ways, because the strip, the title-bar button and (from T10) the ribbon all
    // set it and none of them knows what the others just did.
    sidebar->setAutoHidden(false);
    CHECK(sidebar->isVisible());
}

TEST_CASE("the edge strip appears only on the side the pane is docked to", "[shell]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.show();

    auto* left = window.findChild<QToolButton*>(QStringLiteral("sidebarStripLeft"));
    auto* right = window.findChild<QToolButton*>(QStringLiteral("sidebarStripRight"));
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);

    CHECK_FALSE(left->isVisible());
    CHECK_FALSE(right->isVisible());

    window.sidebar()->setAutoHidden(true);
    CHECK(left->isVisible());
    CHECK_FALSE(right->isVisible());

    window.addDockWidget(Qt::RightDockWidgetArea, window.sidebar());
    CHECK_FALSE(left->isVisible());
    CHECK(right->isVisible());
}

TEST_CASE("the status bar says there is no document rather than showing a made-up one", "[shell]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    ui::StatusBar* status = window.statusBar();
    REQUIRE(status != nullptr);

    // Present and disabled, not absent. "1 of 1 at 100 %" with nothing open would be a lie that is
    // very hard to notice once there IS a document to compare against.
    CHECK_FALSE(status->pageBox()->isEnabled());
    CHECK_FALSE(status->zoomLabel()->isEnabled());
    CHECK(status->pageBox()->text().isEmpty());

    // Idle means hidden. A progress bar that is always there is one people stop looking at.
    status->clearTaskProgress();
    CHECK(status->taskProgress()->isHidden());
}

TEST_CASE("opening a file checks the name and never reads the bytes", "[shell]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    // Guide case A-58: a .pdf whose contents are plain text. Deciding whether bytes are a PDF is
    // IDocumentBackend's job at Stage 2, and pre-empting that boundary inside ui/ is exactly the
    // violation the architecture exists to prevent -- so this is ACCEPTED here.
    const QString textInPdfClothing =
        writeFile(fixture.directory, QStringLiteral("not-really.pdf"),
                  QByteArrayLiteral("this is plain text, not a PDF at all\n"));

    window.openFiles({textInPdfClothing});
    REQUIRE(fixture.recentFiles.entries().size() == 1);
    CHECK(fixture.recentFiles.entries().front().path.endsWith(QStringLiteral("not-really.pdf")));
}

TEST_CASE("a path that does not exist, and one that is not a pdf, are both refused", "[shell]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    const QString missing = QDir(fixture.directory.path()).filePath(QStringLiteral("absent.pdf"));
    const QString notPdf = writeFile(fixture.directory, QStringLiteral("notes.txt"),
                                     QByteArrayLiteral("notes\n"));

    window.openFiles({missing, notPdf});

    // Neither reached the recent list. A recent list that accumulates files nobody could open is
    // one people clear and stop using.
    CHECK(fixture.recentFiles.entries().empty());
}

TEST_CASE("the string dump covers every shell key and every action label", "[shell]")
{
    // A-72 compares an `en` run against a `--lang zh-TW` run, and that comparison is worth
    // something only if the dump covers everything the shell can put on screen. Counting the rows
    // here is what stops a key being added to the UI and not to the dump.
    const QStringList lines = ui::catalog::dumpStrings();

    std::set<QString> keys;
    for (const QString& line : lines) keys.insert(line.section(QLatin1Char('\t'), 0, 0));

    std::size_t shellKeyCount = 0;
#define RAWPAGE_SHELL_STRING(Enumerator, Key)                     \
    ++shellKeyCount;                                              \
    INFO("shell key " Key);                                       \
    CHECK(keys.count(QStringLiteral(Key)) == 1);
#include "ui/shell/ShellKeys.def"

    for (const core::ActionDescriptor& descriptor : core::kActions) {
        const QString key = QString::fromUtf8(descriptor.labelKey.data(),
                                              static_cast<qsizetype>(descriptor.labelKey.size()));
        INFO("action label " << key.toStdString());
        CHECK(keys.count(key) == 1);
    }

    // And every ribbon tab and group title, which live in res/ribbon.json rather than in a .def --
    // 58 names, a fifth of the shell's visible text. Read from the shipped spec here for the same
    // reason dumpStrings() reads it there: a second hand-maintained list of the same names is the
    // drift D1-6 exists to prevent.
    const ui::ribbon::Spec& spec = ui::ribbon::shipped().spec;
    const QStringList ribbonKeys = spec.catalogKeys();
    REQUIRE_FALSE(ribbonKeys.isEmpty());
    for (const QString& key : ribbonKeys) {
        INFO("ribbon key " << key.toStdString());
        CHECK(keys.count(key) == 1);
    }

    // T14's three generated families, which appear whole in no source at all: one label per
    // declared settings key plus one per member of its choice set, one name per shipped preference
    // group, one per KeyContext. They are checked against the SCHEMA rather than against a list
    // here, for the same reason the ribbon keys are read from the shipped spec -- a second copy of
    // `SettingsKeys.def` in a test is the drift D1-6 exists to prevent.
    std::set<QString> generated;
    for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys()) {
        if (info.group == core::SettingsGroup::Internal) continue;
        generated.insert(QString::fromStdString(info.labelKey));
        for (const core::SettingsChoice& choice : info.choices)
            generated.insert(QString::fromStdString(choice.labelKey));
    }
    for (const core::SettingsGroup group : ui::PreferencesDialog::shippedPages())
        generated.insert(ui::catalog::settingsGroupKey(group));
    for (std::size_t i = 0; i < core::kKeyContextCount; ++i)
        generated.insert(ui::catalog::keyContextKey(static_cast<core::KeyContext>(i)));

    for (const QString& key : generated) {
        INFO("generated key " << key.toStdString());
        CHECK(keys.count(key) == 1);
    }

    // A SET rather than a sum, because the two families OVERLAP: `general/theme`'s choice labels
    // are the shell's own `shell.theme.*` keys, which `ShellKeys.def` already counted. Adding the
    // sizes would have produced a number three too large and sent the next reader looking for three
    // missing keys.
    std::set<QString> expected;
    for (const QString& key : generated) expected.insert(key);
#define RAWPAGE_SHELL_STRING(Enumerator, Key) expected.insert(QStringLiteral(Key));
#include "ui/shell/ShellKeys.def"
    for (const core::ActionDescriptor& descriptor : core::kActions) {
        expected.insert(QString::fromUtf8(descriptor.labelKey.data(),
                                          static_cast<qsizetype>(descriptor.labelKey.size())));
    }
    for (const QString& key : ribbonKeys) expected.insert(key);

    CHECK(shellKeyCount > 0);
    CHECK(keys.size() == expected.size());
}

TEST_CASE("a well-formed key resolves through the catalog and a bare word does not", "[shell]")
{
    // The context is the part of the key before the first dot -- `shell`, `action`, `error` -- and
    // NOT the enclosing class name, which is what lets the same label resolve identically from the
    // ribbon, the tool finder and a test. Resolving to something other than the key is the whole
    // of the mechanism working: the .ts context name, the lrelease step, and the split all agree.
    const QString ready = ui::catalog::translate(QStringLiteral("shell.status.ready"));
    INFO("shell.status.ready resolved to: " << ready.toStdString());
    CHECK(ready != QStringLiteral("shell.status.ready"));
    CHECK_FALSE(ready.isEmpty());

    // Action labels come through the same door, from ActionId.def's fourth column.
    const QString open = ui::catalog::translateAction(core::ActionId::FileOpen);
    CHECK(open != QStringLiteral("action.file.open"));

    // A key with no namespace cannot be looked up at all. Returning it verbatim puts the mistake on
    // screen, where it is noticed on the first run rather than in a zh-TW screenshot three stages
    // later.
    CHECK(ui::catalog::translate(QStringLiteral("nodots")) == QStringLiteral("nodots"));
}
