// Stage 1 T9 -- ui/shell/{MainWindow,SidebarDock,StatusBar,Catalog}.
//
// Offscreen, so none of this judges appearance: whether the sidebar looks right docked at 280
// pixels, whether the placeholder surface reads well, and whether the auto-hide slide feels like
// anything are manual cases M-02 and M-09. What is asserted here is the structure underneath --
// that commands go through the registry, that the pane remembers its width, that the status bar
// tells the truth about having no document, and that D1-2's "no content sniffing" holds.

#include <catch2/catch_test_macros.hpp>

#include "app/Services.h"
#include "tests/ui/ShellFixture.h"
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
#include "ui/shell/Ribbon.h"
#include "ui/shell/RibbonModel.h"
#include "ui/shell/SidebarDock.h"
#include "ui/shell/StatusBar.h"

#include <QCoreApplication>
#include <QDir>
#include <QEnterEvent>
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
struct Fixture : uitest::ShellFixture {
    // Every member and `services()` come from `tests/ui/ShellFixture.h` (S2 T13). They were seven
    // copies of the same eight fields until `app::Services` grew a render pipeline and the copies
    // became a seven-way edit; the header states what a fixture deliberately does NOT provide.
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

    // FIFTY-SEVEN, and named. 361 ids are declared; an id that is declared and UNREGISTERED
    // renders as a visible, disabled control saying it is not built yet, which is the honest state.
    // Registering a stub handler for the other 304 would make every one of them look available.
    //
    // Nineteen at Stage 1 T14. Three joined on 2026-08-31, each because a manual walk reached for
    // something the shell could do and could not be asked to do: `app.ribbonKeyTips` (I-42),
    // `ui.qatMoveLeft` / `ui.qatMoveRight` (I-43), and `ui.qatLock`, which arrived with
    // drag-to-reorder. Those three are the only ids Stage 1 ADDED to the 358 the plan declared.
    // **Twenty-five more at Stage 2 T14**: the §8 Viewer subset `Implementation-Plan.md` § Stage 2
    // lists, and not one id beyond it -- the rest of that table belongs to Stages 3 to 13 and stays
    // unregistered, which is asserted below rather than assumed. **Five more at Stage 3 T10**: `edit.undo`,
    // `edit.redo`, `page.rotate`, `file.save` and `file.saveAs` (`Stage3-Mutation-Plan.md` §6).
    // **Four more at Stage 4 T5**: `pane.thumbnails`, `page.goToSelected`, `page.thumbnailsLarger`
    // and `page.thumbnailsSmaller` -- the Thumbnails pane and the three §8 Page Thumbnails rows it
    // services. `page.rotate` is in that table too and is NOT a fifth: the pane reaches Stage 3's
    // registration by declaring its context, which is D4-1's claim made structural.
    //
    // The count is asserted as well as the names on purpose: a name-only check stays green when a
    // forty-ninth registration is added by accident, and "the window registers exactly what it can
    // service" is a claim about the whole set rather than about four members of it.
    CHECK(fixture.actions.registeredCount() == 57);
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

    // S2 T14's twenty-five: the §8 Viewer subset. Spot-checked across all four groups it covers --
    // zoom, navigation, the view's own rotation, the layout modes and the three tools -- rather
    // than listed in full, because the COUNT above is what makes the set exact and a second copy of
    // the list here would be a copy of `registerViewerActions()` rather than a check on it.
    CHECK(fixture.actions.isRegistered(core::ActionId::ViewZoomIn));
    CHECK(fixture.actions.isRegistered(core::ActionId::ViewFitWidth));
    CHECK(fixture.actions.isRegistered(core::ActionId::ViewActualSize));
    CHECK(fixture.actions.isRegistered(core::ActionId::NavNextPage));
    CHECK(fixture.actions.isRegistered(core::ActionId::NavGoToPage));
    CHECK(fixture.actions.isRegistered(core::ActionId::ViewRotateViewCw));
    CHECK(fixture.actions.isRegistered(core::ActionId::ViewTwoUp));
    CHECK(fixture.actions.isRegistered(core::ActionId::ToolHand));

    // AND THE REST OF THE §8 VIEWER TABLE IS NOT REGISTERED, which is the half that keeps the
    // subset honest. `Stage2-Rendering-Plan.md` §10 names these as Stages 3 to 13's, and a stage
    // that implements MOST of a context's table invites someone to complete it -- so the absence is
    // an assertion rather than a note.
    CHECK_FALSE(fixture.actions.isRegistered(core::ActionId::ViewReflow));        // S13
    CHECK(fixture.actions.isRegistered(core::ActionId::PageRotate));              // S3 T10 -- see below
    CHECK(fixture.actions.isRegistered(core::ActionId::EditUndo));
    CHECK(fixture.actions.isRegistered(core::ActionId::EditRedo));
    CHECK(fixture.actions.isRegistered(core::ActionId::FileSave));
    CHECK(fixture.actions.isRegistered(core::ActionId::FileSaveAs));
    CHECK_FALSE(fixture.actions.isRegistered(core::ActionId::ToolSnapshot));      // S5 (D4-15)
    CHECK_FALSE(fixture.actions.isRegistered(core::ActionId::NavNextComment));    // S6

    // S4 T5's FOUR: the Thumbnails pane and the three §8 Page Thumbnails rows it services. The
    // other nine rows in that table need a `Command` and are Stage 7's, so they stay unregistered
    // and leave their keys unconsumed -- D4-1, and `tst_panes` presses each of them to prove it.
    CHECK(fixture.actions.isRegistered(core::ActionId::PaneThumbnails));
    CHECK(fixture.actions.isRegistered(core::ActionId::PageGoToSelected));
    CHECK(fixture.actions.isRegistered(core::ActionId::PageThumbnailsLarger));
    CHECK(fixture.actions.isRegistered(core::ActionId::PageThumbnailsSmaller));
    CHECK_FALSE(fixture.actions.isRegistered(core::ActionId::PageSelectAll));     // S7
    CHECK_FALSE(fixture.actions.isRegistered(core::ActionId::PageDelete));        // S7

    // And the seven panes whose MODELS do not exist are not registered either (D4-2): a pane
    // cannot precede its model, and a chord that opened an empty dock would be indistinguishable
    // from a pane that is broken. `pane.bookmarks` and `pane.destinations` arrive at T6.
    CHECK_FALSE(fixture.actions.isRegistered(core::ActionId::PaneBookmarks));     // S4 T6
    CHECK_FALSE(fixture.actions.isRegistered(core::ActionId::PaneDestinations));  // S4 T6
    CHECK_FALSE(fixture.actions.isRegistered(core::ActionId::PaneComments));      // S6
    CHECK_FALSE(fixture.actions.isRegistered(core::ActionId::PaneLayers));        // S7

    // The distinction A-22 pinned down at T3, seen from the shell: a command that has not arrived
    // yet is NotFound, not a silent no-op. `file.print` is one of the ~310 that are declared and
    // unregistered; it used to be `file.open`, which T12 registered, then `file.save`, which Stage 3 T10 did.
    const core::Result<void> notYet =
        fixture.actions.dispatch(core::ActionId::FilePrint, core::KeyContext::Global);
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

TEST_CASE("hovering the edge strip peeks the auto-hidden pane", "[shell]")
{
    // M-04, decided 2026-08-30: reaching for the edge is the gesture that reveals an auto-hidden
    // pane, and until that day the strip answered only to a click -- so the case's own Expect line
    // described a product we did not have. Offscreen there is no pointer to move, so the Enter the
    // window manager would send is sent by hand; what that DOES cover is the wiring from the strip
    // to `peek()`, which is the half that was missing. Whether a real pointer crossing a 24-pixel
    // strip feels right is M-04's to say and always was.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.show();

    auto* left = window.findChild<QToolButton*>(QStringLiteral("sidebarStripLeft"));
    REQUIRE(left != nullptr);

    // Pinned: the strip is not even up, and a stray Enter must not open anything.
    QEnterEvent stray(QPointF(1, 1), QPointF(1, 1), QPointF(1, 1));
    QCoreApplication::sendEvent(left, &stray);
    CHECK_FALSE(window.sidebar()->isPeeking());

    window.sidebar()->setAutoHidden(true);
    REQUIRE(left->isVisible());
    REQUIRE_FALSE(window.sidebar()->isPeeking());

    QEnterEvent arrive(QPointF(1, 1), QPointF(1, 1), QPointF(1, 1));
    QCoreApplication::sendEvent(left, &arrive);

    // Revealed, and still auto-hidden: a peek is a look, not a pin.
    CHECK(window.sidebar()->isPeeking());
    CHECK(window.sidebar()->isAutoHidden());

    // AND THE STRIP IS GONE WHILE THE PANE IS OUT (issue I-39). The strip stands in for the pane;
    // the two of them on screen together is a bar sitting beside the pane it exists to summon.
    CHECK_FALSE(left->isVisible());

    // And leaving the pane puts it away again -- the half that already worked, asserted here so the
    // round trip is one case rather than two halves nobody joins up.
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(window.sidebar(), &leave);
    CHECK_FALSE(window.sidebar()->isPeeking());

    // The pane is away again, so the strip is back -- otherwise there would be nothing left to
    // reach for and the pane would be unreachable by pointer for the rest of the session.
    CHECK(left->isVisible());
}

TEST_CASE("Alt key tips are reachable through the registry", "[shell]")
{
    // ISSUE I-42. The ribbon has drawn key tips since T10 and NOTHING EVER ASKED IT TO: `Alt` is
    // bound to `AppRibbonKeyTips` in `KeyBindingDefaults.def`, `ShortcutBinder` forms that chord and
    // `tst_context` asserts that it does -- and the id was never registered, so `dispatch` answered
    // `NotFound`, left the key unconsumed, and pressing Alt did nothing in any locale. Every link
    // was tested and the CHAIN was not, which is why this dispatches through the registry rather
    // than calling `showKeyTips()`. R-5 had been recorded as met against the T2 spike, which had a
    // handler of its own that the shipped ribbon never inherited.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.show();

    ui::Ribbon* ribbon = window.ribbon();
    REQUIRE(ribbon != nullptr);
    REQUIRE_FALSE(ribbon->keyTipsVisible());

    REQUIRE(fixture.actions.dispatch(core::ActionId::AppRibbonKeyTips, core::KeyContext::Global));
    CHECK(ribbon->keyTipsVisible());

    // A letter chooses its tab and opens level two; Esc walks back out, one level at a time.
    const QString letter = ribbon->accessKeyOfPage(1);
    REQUIRE(letter.size() == 1);
    CHECK(ribbon->handleKeyTip(letter.at(0).unicode()));
    CHECK(ribbon->currentPage() == 1);
    CHECK(ribbon->keyTipsVisible());

    CHECK(ribbon->handleKeyTip(Qt::Key_Escape));
    CHECK(ribbon->keyTipsVisible());              // back to level one, not gone
    CHECK(ribbon->handleKeyTip(Qt::Key_Escape));
    CHECK_FALSE(ribbon->keyTipsVisible());

    // Alt again is a toggle, which is what Alt does in every ribbon.
    REQUIRE(fixture.actions.dispatch(core::ActionId::AppRibbonKeyTips, core::KeyContext::Global));
    CHECK(ribbon->keyTipsVisible());
    REQUIRE(fixture.actions.dispatch(core::ActionId::AppRibbonKeyTips, core::KeyContext::Global));
    CHECK_FALSE(ribbon->keyTipsVisible());
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

TEST_CASE("a path that does not exist and one the backend refuses are both refused", "[shell]")
{
    // **REWRITTEN AT T9.** It used to assert that a file not named `.pdf` was refused, which was
    // Stage 1's D1-2 and is no longer true: guide case A-47 moved that decision to the backend, and
    // the suffix is not consulted. The two refusals that REMAIN are the ones worth keeping, and
    // they are refused by two different parties for two different reasons.
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    // The shell's own: existence. "There is no such file" is a different message from "that is not
    // a PDF", and asking the engine about a path that is not there would turn the first into the
    // second.
    const QString missing = QDir(fixture.directory.path()).filePath(QStringLiteral("absent.pdf"));

    // The backend's: these bytes are not a document.
    const QString refused = writeFile(fixture.directory, QStringLiteral("notes.txt"),
                                      QByteArrayLiteral("notes\n"));
    fixture.backend.accepts = [&refused](const QString& path) { return path != refused; };

    window.openFiles({missing, refused});

    // Neither reached the recent list. A recent list that accumulates files nobody could open is
    // one people clear and stop using.
    CHECK(fixture.recentFiles.entries().empty());

    // The missing one was never handed to the backend -- the shell answered it -- and the other
    // one was. Without this the case cannot tell which party refused what.
    REQUIRE(fixture.backend.asked.size() == 1);
    CHECK(fixture.backend.asked.front() == refused);
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
