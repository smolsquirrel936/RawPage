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
#include <QLayout>
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
