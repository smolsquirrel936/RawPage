// Stage 5 T13 -- the comparison's surface, decision D5-18 (`Stage5-TextSearch-Plan.md` §3). Guide case A-103
// and the halves of A-106 and A-107 a widget can reach (`Stage5-TextSearch-Testing-Guide.md` §5.15), over
// `FakeBackend`: an "older" document of three pages and a "newer" one of four, whose fourth page's text is new
// -- so the comparer finds exactly one difference, the page inserted.
//
// WHAT IT DOES NOT COVER (convention 19), and where that is:
//   * **The dialog's own modal run.** Offscreen a modal is shown to nobody (convention 16): `Ctrl+Alt+C` is
//     driven here as a KEY EVENT through `ShortcutBinder` with an ANSWER INSTALLED in place of the dialog --
//     a back door that does not cover the dialog's widgets, which the case "the dialog collects..." drives
//     directly, built and never executed.
//   * **Real pages.** The scroll by aligned page, the overlay's tints, the stepping over a real revision pair
//     and the files left byte-identical are `tests/composition/tst_comparisonink.cpp`'s, with the engine.
//   * **Whether what it reports reads as the change a person made** -- manual case M-08.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/settings/ComparePairs.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/CompareDialog.h"
#include "ui/dialogs/RotatePagesDialog.h"
#include "ui/panes/DifferencesPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/ComparisonView.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ViewportController.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>

#include <memory>
#include <optional>

using namespace rawpage;
using core::ActionId;
using core::text::CompareSensitivity;
using core::text::CompareSide;
namespace keys = core::keys::documents;

namespace {

struct Fixture : uitest::ShellFixture {
    QString writeFile(const QString& name)
    {
        const QString path = QDir(directory.path()).filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("the fake backend decides, not these bytes");
        file.close();
        return QFileInfo(path).absoluteFilePath();
    }
};

// Removes the installed answer however the case ends, so no later case is answered by this one's.
struct AnswerGuard {
    ~AnswerGuard()
    {
        ui::CompareDialog::setAnswerForTesting({});
        ui::RotatePagesDialog::setAnswerForTesting({});
    }
};

struct Comparer {
    mutable Fixture                 fixture;
    std::unique_ptr<ui::MainWindow> window;
    QString                         older;
    QString                         newer;
    AnswerGuard                     guard;

    Comparer()
    {
        // THREE PAGES OLDER, FOUR NEWER (the header) -- the fake decides the page count when it opens a path.
        fixture.backend.onOpen = [this](const QString& path) {
            fixture.backend.pagesPerDocument = path.endsWith(QStringLiteral("newer.pdf")) ? 4 : 3;
        };
        older = fixture.writeFile(QStringLiteral("older.pdf"));
        newer = fixture.writeFile(QStringLiteral("newer.pdf"));
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 850);
        window->openFiles({older});
        drive(window.get());
        window->raiseToForeground();
        window->currentView()->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        fixture.pumpTiles();
    }

    ~Comparer()
    {
        if (ui::ComparisonView* comparison = window->comparison()) delete comparison;
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    static void drive(QWidget* root)
    {
        root->show();
        for (QWidget* widget : root->findChildren<QWidget*>())
            if (QLayout* layout = widget->layout()) layout->activate();
        if (QLayout* layout = root->layout()) layout->activate();
        QCoreApplication::processEvents();
    }

    ui::CompareDialog::Choice choice(CompareSensitivity sensitivity = CompareSensitivity::TextOnly) const
    {
        ui::CompareDialog::Choice chosen;
        chosen.olderPath = older;
        chosen.newerPath = newer;
        chosen.options.sensitivity = sensitivity;
        return chosen;
    }

    // A key event, sent to `target` -- or to the focus -- as a person's key would arrive.
    static bool key(QWidget* target, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        REQUIRE(target != nullptr);
        QKeyEvent press(QEvent::KeyPress, key, modifiers);
        press.ignore();
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers);
        QCoreApplication::sendEvent(target, &release);
        QCoreApplication::processEvents();
        return press.isAccepted();
    }

    // Until the comparison's run has finished -- its text read through the index, its diff delivered.
    void settle(ui::ComparisonView& comparison) const
    {
        QElapsedTimer timer;
        timer.start();
        while (!comparison.isFinished() && timer.elapsed() < 10000) {
            fixture.pumpTiles(200);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        drive(&comparison);
        fixture.pumpTiles(500);
        QCoreApplication::processEvents();
    }

    // The comparison's side view, focused in a window that is the active one -- where a person's key lands.
    ui::PageView* focusSide(ui::ComparisonView& comparison, CompareSide side) const
    {
        comparison.activateWindow();
        comparison.view(side)->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        return comparison.view(side);
    }
};

} // namespace

TEST_CASE("Ctrl+Alt+C from a key event dispatches compare.documents and opens the comparison", "[comparison][A-103]")
{
    Comparer comparer;
    REQUIRE(comparer.fixture.actions.isRegistered(ActionId::CompareDocuments));
    REQUIRE(comparer.window->shortcutBinder()->activeContext() == core::KeyContext::Viewer);

    // NOBODY TO ASK -- convention 16: offscreen with no answer the command is DISABLED and the chord does
    // nothing but be consumed; no comparison opens and nothing is asked of the backend.
    const int opensBefore = static_cast<int>(comparer.fixture.backend.asked.size());
    CHECK_FALSE(comparer.fixture.actions.isEnabled(ActionId::CompareDocuments));
    CHECK(Comparer::key(comparer.window->currentView(), Qt::Key_C, Qt::ControlModifier | Qt::AltModifier));
    CHECK(comparer.window->comparison() == nullptr);
    CHECK(static_cast<int>(comparer.fixture.backend.asked.size()) == opensBefore);

    // THE BACK DOOR (the header): an answer in place of the dialog. The chord is a KEY EVENT through the
    // binder all the same, which is what this case is about.
    int asked = 0;
    ui::CompareDialog::setAnswerForTesting([&] {
        ++asked;
        return std::optional<ui::CompareDialog::Choice>(comparer.choice(CompareSensitivity::TextAndLayout));
    });
    CHECK(comparer.fixture.actions.isEnabled(ActionId::CompareDocuments));
    CHECK(Comparer::key(comparer.window->currentView(), Qt::Key_C, Qt::ControlModifier | Qt::AltModifier));
    CHECK(asked == 1);
    ui::ComparisonView* comparison = comparer.window->comparison();
    REQUIRE(comparison != nullptr);

    // ITS OWN TWO HANDLES: both files opened again for it, the tab's untouched, and the choice's options
    // written to their settings -- the keys ARE the options.
    CHECK(static_cast<int>(comparer.fixture.backend.asked.size()) == opensBefore + 2);
    CHECK(comparison->handle(CompareSide::Base) != comparer.window->currentDocument()->handle());
    CHECK(comparison->path(CompareSide::Base) == comparer.older);
    CHECK(comparison->path(CompareSide::Revised) == comparer.newer);
    CHECK(comparer.fixture.settings.get(keys::compareSensitivity) == CompareSensitivity::TextAndLayout);
    CHECK(comparison->options().sensitivity == CompareSensitivity::TextAndLayout);
    CHECK(comparison->isWindow());
    CHECK(ui::ShortcutBinder::declaredContext(comparison) == core::KeyContext::Viewer);

    comparer.settle(*comparison);
    REQUIRE(comparison->isFinished());
    REQUIRE(comparison->differences().size() == 1);
    CHECK(comparison->differences().front().kind == core::text::DifferenceKind::PageInserted);
    CHECK(comparison->differences().front().revisedPage == 3);
    CHECK(comparison->differencesPane()->rowCount() == 1);
    CHECK(comparison->partnerOf(CompareSide::Base, 2) == 2);
    CHECK(comparison->partnerOf(CompareSide::Revised, 3) == -1);
}

TEST_CASE("the compare dialog collects two paths and the four options", "[comparison][A-103]")
{
    // BUILT AND DRIVEN, NEVER EXECUTED (the header): the widget the back door stands in for.
    core::text::CompareOptions options;
    options.sensitivity = CompareSensitivity::Appearance;
    options.ignorePageNumbers = true;
    ui::CompareDialog dialog({QStringLiteral("C:/a/older.pdf"), QStringLiteral("C:/a/newer.pdf")}, options);
    dialog.show();
    QCoreApplication::processEvents();

    // THE OLDER IS THE TAB IN FRONT, THE NEWER THE NEXT; the options are what the keys hold.
    ui::CompareDialog::Choice chosen = dialog.choice();
    CHECK(chosen.olderPath == QStringLiteral("C:/a/older.pdf"));
    CHECK(chosen.newerPath == QStringLiteral("C:/a/newer.pdf"));
    CHECK(chosen.options.sensitivity == CompareSensitivity::Appearance);
    CHECK_FALSE(chosen.options.ignoreHeaderFooter);
    CHECK(chosen.options.ignorePageNumbers);
    CHECK_FALSE(chosen.options.ignoreWhitespace);
    CHECK(dialog.okButton()->isEnabled());

    dialog.sensitivityField()->setCurrentIndex(0);
    dialog.ignoreHeaderFooterBox()->setChecked(true);
    dialog.ignoreWhitespaceBox()->setChecked(true);
    dialog.newerField()->setEditText(QStringLiteral("C:/b/typed.pdf"));
    chosen = dialog.choice();
    CHECK(chosen.options.sensitivity == CompareSensitivity::TextOnly);
    CHECK(chosen.options.ignoreHeaderFooter);
    CHECK(chosen.options.ignoreWhitespace);
    CHECK(chosen.newerPath == QStringLiteral("C:/b/typed.pdf"));

    // TWO FILES OR NOTHING: an empty field disables OK -- and the control, a name typed back, enables it.
    dialog.newerField()->setEditText(QString());
    CHECK_FALSE(dialog.okButton()->isEnabled());
    dialog.newerField()->setEditText(QStringLiteral("C:/b/typed.pdf"));
    CHECK(dialog.okButton()->isEnabled());

    // ONE OPEN TAB: the newer starts empty, and so does OK.
    ui::CompareDialog single({QStringLiteral("C:/a/only.pdf")}, core::text::CompareOptions{});
    CHECK(single.choice().olderPath == QStringLiteral("C:/a/only.pdf"));
    CHECK(single.choice().newerPath.isEmpty());
    CHECK_FALSE(single.okButton()->isEnabled());
}

TEST_CASE("the newer field offers the file the older one was last compared against", "[comparison][A-126]")
{
    // BUILT AND DRIVEN, NEVER EXECUTED, as the case above. Real files, because a remembered file that is gone is
    // not offered.
    Fixture fixture;
    const QString older = QDir::toNativeSeparators(fixture.writeFile(QStringLiteral("report-v1.pdf")));
    const QString remembered = QDir::toNativeSeparators(fixture.writeFile(QStringLiteral("report-v2.pdf")));
    const QString otherOlder = QDir::toNativeSeparators(fixture.writeFile(QStringLiteral("draft.pdf")));
    const QString nextTab = QDir::toNativeSeparators(fixture.writeFile(QStringLiteral("next-tab.pdf")));
    const QString gone = QDir(fixture.directory.path()).filePath(QStringLiteral("deleted.pdf"));
    const auto memory = [&](const QString& asked) {
        if (asked == older) return remembered;
        if (asked == otherOlder) return gone;
        return QString();
    };

    // THE REMEMBERED FILE, OVER THE NEXT TAB: the older in front was compared against `report-v2.pdf` before.
    ui::CompareDialog dialog({older, nextTab}, core::text::CompareOptions{}, nullptr, memory);
    CHECK(dialog.choice().olderPath == older);
    CHECK(dialog.choice().newerPath == remembered);
    CHECK(dialog.okButton()->isEnabled());

    // IT FOLLOWS THE OLDER: the next tab picked as the older has no memory, so the newer is the default again --
    // and an older whose remembered file is GONE offers the default, not the missing file.
    dialog.olderField()->setCurrentIndex(1);
    CHECK(dialog.choice().newerPath == nextTab);
    dialog.olderField()->setEditText(otherOlder);
    CHECK(dialog.choice().newerPath == nextTab);
    dialog.olderField()->setEditText(older);
    CHECK(dialog.choice().newerPath == remembered);

    // A NEWER CHOSEN BY HAND IS KEPT: picked from the list as a person picks, then the older changed.
    dialog.newerField()->setCurrentIndex(0);
    emit dialog.newerField()->textActivated(dialog.newerField()->currentText());
    dialog.olderField()->setEditText(otherOlder);
    CHECK(dialog.choice().newerPath == older);

    // And typed: the control, a dialog nobody touched, still follows.
    ui::CompareDialog typed({otherOlder, nextTab}, core::text::CompareOptions{}, nullptr, memory);
    typed.newerField()->lineEdit()->setText(QStringLiteral("C:/b/typed.pdf"));
    emit typed.newerField()->lineEdit()->textEdited(QStringLiteral("C:/b/typed.pdf"));
    typed.olderField()->setEditText(older);
    CHECK(typed.choice().newerPath == QStringLiteral("C:/b/typed.pdf"));
    ui::CompareDialog untouched({otherOlder, nextTab}, core::text::CompareOptions{}, nullptr, memory);
    untouched.olderField()->setEditText(older);
    CHECK(untouched.choice().newerPath == remembered);

    // NO MEMORY GIVEN: exactly the dialog of the case above.
    ui::CompareDialog plain({older, nextTab}, core::text::CompareOptions{});
    CHECK(plain.choice().newerPath == nextTab);
}

TEST_CASE("an opened comparison is remembered for its older file and a refused one is not", "[comparison][A-126]")
{
    Comparer comparer;
    REQUIRE(core::ComparePairs::newerFor(comparer.fixture.settings, comparer.older).isEmpty());

    // REFUSED -- the newer does not exist -- so nothing is remembered.
    ui::CompareDialog::Choice missing = comparer.choice();
    missing.newerPath = QDir(comparer.fixture.directory.path()).filePath(QStringLiteral("absent.pdf"));
    CHECK_FALSE(static_cast<bool>(comparer.window->compareDocuments(missing)));
    CHECK(core::ComparePairs::newerFor(comparer.fixture.settings, comparer.older).isEmpty());

    // OPENED: the pair is remembered, in the separators a tab shows, under the older file however it is spelt.
    const core::Result<ui::ComparisonView*> opened = comparer.window->compareDocuments(comparer.choice());
    REQUIRE(opened);
    CHECK(core::ComparePairs::newerFor(comparer.fixture.settings, comparer.older)
          == QDir::toNativeSeparators(comparer.newer));
    CHECK(core::ComparePairs::newerFor(comparer.fixture.settings, QDir::toNativeSeparators(comparer.older))
          == QDir::toNativeSeparators(comparer.newer));
}

TEST_CASE("Ctrl+] and Ctrl+[ are disabled without a comparison and step one when there is", "[comparison][A-106]")
{
    Comparer comparer;
    REQUIRE(comparer.fixture.actions.isRegistered(ActionId::NavNextDifference));
    REQUIRE(comparer.fixture.actions.isRegistered(ActionId::NavPrevDifference));
    CHECK_FALSE(comparer.fixture.actions.isEnabled(ActionId::NavNextDifference));
    CHECK_FALSE(comparer.fixture.actions.isEnabled(ActionId::NavPrevDifference));

    // §8 BINDS THEM IN THE VIEWER, so a disabled step still CONSUMES its key -- and moves nothing.
    ui::PageView* tab = comparer.window->currentView();
    const int pageBefore = tab->controller().currentPage();
    CHECK(Comparer::key(tab, Qt::Key_BracketRight, Qt::ControlModifier));
    CHECK(tab->controller().currentPage() == pageBefore);

    const core::Result<ui::ComparisonView*> opened = comparer.window->compareDocuments(comparer.choice());
    REQUIRE(opened);
    ui::ComparisonView& comparison = *opened.value();
    comparer.settle(comparison);
    REQUIRE(comparison.differences().size() == 1);
    CHECK(comparer.fixture.actions.isEnabled(ActionId::NavNextDifference));
    CHECK(comparison.currentDifference() == -1);

    // FROM THE COMPARISON'S OWN VIEW: the binder still owns these two chords there (they pass its claim).
    ui::PageView* side = comparer.focusSide(comparison, CompareSide::Revised);
    CHECK(Comparer::key(side, Qt::Key_BracketRight, Qt::ControlModifier));
    CHECK(comparison.currentDifference() == 0);
    CHECK(comparison.differencesPane()->current() == 0);

    // MARKED ON THE PAGE IT IS ON: the inserted page, whole, outlined as the current one, on the newer side
    // only -- the older has no page for it.
    const std::vector<ui::PageView::PageMark>& marks = comparison.view(CompareSide::Revised)->pageMarks();
    REQUIRE(marks.size() == 1);
    CHECK(marks.front().page == 3);
    CHECK(marks.front().outlined);
    CHECK(comparison.view(CompareSide::Base)->pageMarks().empty());

    // ONE DIFFERENCE: the next step WRAPS, and says so; so does the previous one.
    CHECK(Comparer::key(side, Qt::Key_BracketRight, Qt::ControlModifier));
    CHECK(comparison.currentDifference() == 0);
    CHECK(comparison.statusText() == ui::catalog::translate(ui::catalog::key::CompareStatusWrappedToFirst));
    CHECK(Comparer::key(side, Qt::Key_BracketLeft, Qt::ControlModifier));
    CHECK(comparison.currentDifference() == 0);
    CHECK(comparison.statusText() == ui::catalog::translate(ui::catalog::key::CompareStatusWrappedToLast));
}

TEST_CASE("a key pressed in the comparison never reaches the tab behind it", "[comparison][A-107]")
{
    Comparer comparer;
    ui::DocumentWindow* tab = comparer.window->currentDocument();
    REQUIRE(tab != nullptr);
    ui::PageView* tabView = tab->view();
    const double tabZoom = tabView->controller().zoom().factor;

    int rotateAsked = 0;
    ui::RotatePagesDialog::setAnswerForTesting([&] {
        ++rotateAsked;
        return std::optional<ui::RotatePagesDialog::Choice>(ui::RotatePagesDialog::Choice{});
    });

    const core::Result<ui::ComparisonView*> opened = comparer.window->compareDocuments(comparer.choice());
    REQUIRE(opened);
    ui::ComparisonView& comparison = *opened.value();
    comparer.settle(comparison);
    ui::PageView* side = comparer.focusSide(comparison, CompareSide::Base);
    REQUIRE(comparer.window->shortcutBinder()->activeContext() == core::KeyContext::Viewer);
    const double sideZoom = side->controller().zoom().factor;

    // `Ctrl+R` (rotate pages), `Ctrl+Z` (undo), `Ctrl+=` (zoom) and `Ctrl+4` (reflow) -- each would act on
    // the TAB in front if it were dispatched from here. The comparison claims them first (D5-18 clarified).
    CHECK(Comparer::key(side, Qt::Key_R, Qt::ControlModifier) == false);
    CHECK(rotateAsked == 0);
    Comparer::key(side, Qt::Key_Z, Qt::ControlModifier);
    Comparer::key(side, Qt::Key_4, Qt::ControlModifier);
    CHECK_FALSE(tab->isReflowing());
    CHECK(Comparer::key(side, Qt::Key_Equal, Qt::ControlModifier));
    comparer.fixture.pumpTiles();
    CHECK(tabView->controller().zoom().factor == tabZoom);
    // ... and the zoom chord is the COMPARISON's: both sides at the new factor.
    CHECK(side->controller().zoom().factor > sideZoom);
    CHECK(comparison.view(CompareSide::Revised)->controller().zoom().factor == side->controller().zoom().factor);

    CHECK(tab->journal() != nullptr);
    CHECK_FALSE(tab->isDirty());
    CHECK_FALSE(tab->journal()->canUndo());
}

TEST_CASE("the comparison's bar writes the four settings and re-runs over the text it read", "[comparison][A-103]")
{
    Comparer comparer;
    const core::Result<ui::ComparisonView*> opened = comparer.window->compareDocuments(comparer.choice());
    REQUIRE(opened);
    ui::ComparisonView& comparison = *opened.value();
    comparer.settle(comparison);
    const core::text::ComparisonResult* first = comparison.result();
    REQUIRE(first != nullptr);
    const long long extracted = comparer.fixture.documentIndex.stats().requested;

    // A SWITCH ON THE BAR IS THE KEY (D5-14's rule), and the change re-runs the comparison.
    auto* box = comparison.findChild<QCheckBox*>(QStringLiteral("comparisonIgnoreWhitespace"));
    REQUIRE(box != nullptr);
    box->click();
    CHECK(comparer.fixture.settings.get(keys::compareIgnoreWhitespace));
    CHECK(comparison.options().ignoreWhitespace);
    comparer.settle(comparison);
    CHECK(comparison.isFinished());
    CHECK(comparison.result() != first);
    // OVER THE TEXT ALREADY READ: the index was asked for nothing more.
    CHECK(comparer.fixture.documentIndex.stats().requested == extracted);

    // AND A CHANGE MADE ELSEWHERE -- Preferences -- reaches the bar.
    comparer.fixture.settings.set(keys::compareIgnorePageNumbers, true);
    auto* numbers = comparison.findChild<QCheckBox*>(QStringLiteral("comparisonIgnorePageNumbers"));
    REQUIRE(numbers != nullptr);
    CHECK(numbers->isChecked());
    comparer.settle(comparison);
    CHECK(comparison.options().ignorePageNumbers);
}

TEST_CASE("closing the comparison closes its two handles and nothing else", "[comparison][A-107]")
{
    Comparer comparer;
    const core::Result<ui::ComparisonView*> opened = comparer.window->compareDocuments(comparer.choice());
    REQUIRE(opened);
    ui::ComparisonView* comparison = opened.value();
    comparer.settle(*comparison);
    const int closesBefore = comparer.fixture.backend.closes;

    comparison->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    CHECK(comparer.window->comparison() == nullptr);
    CHECK(comparer.fixture.backend.closes == closesBefore + 2);
    // The tab is still open and still drawing its own handle.
    REQUIRE(comparer.window->documentCount() == 1);
    CHECK(comparer.fixture.backend.pages(comparer.window->currentDocument()->handle()) != nullptr);

    // A REFUSED FILE OPENS NOTHING, closes what it had opened -- and leaves the comparison that was open, and
    // the four keys, as they were: the options are written only once both files are open.
    const core::Result<ui::ComparisonView*> kept = comparer.window->compareDocuments(comparer.choice());
    REQUIRE(kept);
    comparer.settle(*kept.value());
    comparer.fixture.backend.accepts = [](const QString& path) { return !path.endsWith(QStringLiteral("newer.pdf")); };
    const int closes = comparer.fixture.backend.closes;
    const core::Result<ui::ComparisonView*> refused =
        comparer.window->compareDocuments(comparer.choice(CompareSensitivity::Appearance));
    CHECK_FALSE(refused);
    CHECK(comparer.window->comparison() == kept.value());
    CHECK(comparer.fixture.settings.get(keys::compareSensitivity) == CompareSensitivity::TextOnly);
    CHECK(comparer.fixture.backend.closes == closes + 1);
}

// =============================================================================================
// Guide I-11 -- the pages keep their width while the comparison reports its progress
// =============================================================================================
//
// Until Stage 5 T14 the bar's status label -- where the run writes its progress -- took its width from its
// text, so a longer progress line raised the comparison window's minimum and Qt WIDENED the window to it
// (the find bar's I-10, in this window): both pages, fitted to their width, re-fitted and re-rasterized every
// tile at `Visible`. T14's bench saw it five times in one comparison of `text-900.pdf`, the pages 874 to 895
// pixels wide, monotonically, as the line grew. The Differences pane's summary is written the same lines and
// is driven here too, as the CONTROL that the pane is not the cause: it wraps, and reverting the two changes
// first made to it for this issue left this case green, so they were taken out again.
//
// WHAT IT DOES NOT COVER (convention 19): the progress lines here are WRITTEN to the two labels through the
// pane's `setStatus` and the bar label's `setText` -- the calls the run's progress makes, but with lines of
// chosen lengths, because the fake's three and four pages produce progress lines whose width barely moves.
// That the run's OWN progress no longer re-fits a page is T14's bench, over `text-900.pdf`.

TEST_CASE("the pages keep their width and zoom while the comparison reports its progress", "[comparison][I-11]")
{
    Comparer comparer;
    const core::Result<ui::ComparisonView*> opened = comparer.window->compareDocuments(comparer.choice());
    REQUIRE(opened);
    ui::ComparisonView& comparison = *opened.value();
    comparer.settle(comparison);

    ui::PageView* older = comparison.view(CompareSide::Base);
    ui::PageView* newer = comparison.view(CompareSide::Revised);
    REQUIRE(older != nullptr);
    REQUIRE(newer != nullptr);
    REQUIRE(older->width() > 200);   // rule 4: the geometry first
    const int olderWidth = older->width();
    const int newerWidth = newer->width();
    const double olderZoom = older->controller().zoom().factor;
    const int windowMinimum = comparison.minimumSizeHint().width();
    const int windowWidth = comparison.width();

    // EVERY ZOOM CHANGE OF EITHER PAGE, from here on -- a re-fit is a re-rasterization of the whole page.
    int refits = 0;
    for (ui::PageView* view : {older, newer})
        QObject::connect(view, &ui::PageView::viewChanged, &comparison,
                         [&refits, view, zoom = view->controller().zoom().factor](int, int, double now) mutable {
                             (void)view;
                             if (now != zoom) ++refits;
                             zoom = now;
                         });

    // PROGRESS LINES OF VERY DIFFERENT WIDTHS, into the pane's summary and the bar's status.
    auto* status = comparison.findChild<QLabel*>(QStringLiteral("comparisonStatus"));
    REQUIRE(status != nullptr);
    const QString longLine = QStringLiteral("Comparing 1 of 900 pages -- reading the older and the newer document's text "
                                            "and their 72 dpi rasters, page after page after page");
    for (const QString& line : {QStringLiteral("x"), longLine, QStringLiteral("Reading 12 of 1800"), longLine + longLine,
                                QString()}) {
        INFO("the line: " << line.toStdString());
        comparison.differencesPane()->setStatus(line);
        status->setText(line);
        Comparer::drive(&comparison);
        CHECK(comparison.width() == windowWidth);
        CHECK(older->width() == olderWidth);
        CHECK(newer->width() == newerWidth);
        CHECK(older->controller().zoom().factor == olderZoom);
        CHECK(comparison.minimumSizeHint().width() == windowMinimum);
    }
    CHECK(refits == 0);
}
