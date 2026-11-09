// Stage 7 T10 (`Stage7-Assembly-Plan.md` §6, D7-16, D7-33) -- the merge dialog's widgets and `merge.documents` through the
// window: guide A-70's and A-72's widget halves, and the settings that start the dialog (A-66, A-68).
//
// WHAT THIS SUITE CANNOT SEE (convention 19): what a merge BUILDS -- `uitest::FakeBackend::mergeFiles` records what it is
// asked and answers fake bytes; the merged document is `unit.merge`'s and `render.merge`'s to judge, and the shipped
// backend through the window is `ui.composition`'s `tst_mergeproduct.cpp` (A-72's bound). The dialog is never shown
// modally: it is constructed and driven here, and `ask` answered through its twin. A drop is SENT to the dialog as
// the event a drag from Explorer delivers -- below the operating system's drag loop, which M-03 walks.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/doc/Merge.h"
#include "core/settings/Settings.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/MergeDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/StatusBar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMimeData>
#include <QRadioButton>
#include <QTableWidget>
#include <QThread>
#include <QUrl>

#include <map>
#include <memory>
#include <string>

using namespace rawpage;
using core::ActionId;

namespace {

struct Answers {
    ~Answers() { ui::MergeDialog::setAnswerForTesting({}); }
};

template <typename T>
std::string keyOf(const core::Result<T>& result)
{
    return result ? std::string("(success)") : std::string(result.error().messageKey.data(), static_cast<std::size_t>(result.error().messageKey.size()));
}

// A counter over named files: a count, or the refusal the backend would give.
ui::MergeDialog::Counter counterOf(std::map<QString, int> counts)
{
    return [counts](const QString& path) -> core::Result<int> {
        const auto found = counts.find(path);
        if (found == counts.end()) return core::Error{core::ErrorCode::PermissionDenied, QLatin1String("error.pdf.encrypted"), QString()};
        return found->second;
    };
}

// ONE row selected, through the selection model. NOT `QTableView::selectRow`: in extended selection it reads the
// application's keyboard modifiers, and a case earlier in a whole run can leave Ctrl+Shift held there -- under that
// state `selectRow` selected nothing (Stage 7 guide I-18, seed 2136814007).
void selectOnly(ui::MergeDialog& dialog, int row)
{
    QTableWidget* table = dialog.table();
    table->selectionModel()->select(table->model()->index(row, 0), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

QString pathAt(const ui::MergeDialog& dialog, int row)
{
    return dialog.choice().value().inputs[static_cast<std::size_t>(row)].path;
}

struct Shell {
    uitest::ShellFixture fixture;
    std::unique_ptr<ui::MainWindow> window;

    Shell() { window = std::make_unique<ui::MainWindow>(fixture.services()); }
    ~Shell()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    // The merge runs on its own thread and answers through the event loop: pumped until it has.
    bool waitForMerge(int milliseconds = 10000)
    {
        QElapsedTimer clock;
        clock.start();
        while (window->isMerging() && clock.elapsed() < milliseconds) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(2);
        }
        QCoreApplication::processEvents();
        return !window->isMerging();
    }
    QString said() const { return window->statusBar()->message()->text(); }
};

} // namespace

TEST_CASE("merge.dialog lists each file with what the merge will find, in an order a person sets", "[merge][merge.dialog][A-70]")
{
    const QString a = QStringLiteral("C:/in/a.pdf");
    const QString b = QStringLiteral("C:/in/b.pdf");
    const QString locked = QStringLiteral("C:/in/locked.pdf");
    ui::MergeDialog dialog(core::MergeOptions{}, counterOf({{a, 3}, {b, 12}}));
    CHECK(keyOf(dialog.choice()) == std::string("error.merge.noInputs"));   // nothing listed
    dialog.addFiles({a, b, locked, a});   // a file twice is a merge of it with itself
    REQUIRE(dialog.table()->rowCount() == 4);
    CHECK(dialog.table()->item(0, ui::MergeDialog::CountColumn)->text().contains(QStringLiteral("3")));
    CHECK(dialog.table()->item(1, ui::MergeDialog::CountColumn)->text().contains(QStringLiteral("12")));
    // ... and a file the merge will skip says so before the merge does.
    CHECK(dialog.table()->item(2, ui::MergeDialog::CountColumn)->text() != dialog.table()->item(0, ui::MergeDialog::CountColumn)->text());

    // A range, checked against that file's count before the dialog closes.
    dialog.table()->item(1, ui::MergeDialog::RangeColumn)->setText(QStringLiteral("2-4, 12"));
    // A range on a file that will be skipped is nobody's to check.
    dialog.table()->item(2, ui::MergeDialog::RangeColumn)->setText(QStringLiteral("99"));
    auto chosen = dialog.choice();
    REQUIRE(chosen);
    REQUIRE(chosen.value().inputs.size() == 4);
    CHECK(chosen.value().inputs[0] == core::MergeInput{a, {}});
    CHECK(chosen.value().inputs[1] == core::MergeInput{b, {1, 2, 3, 11}});
    CHECK(chosen.value().inputs[2].pages.empty());

    // Reordered: the third row moved to the top, then the last removed.
    selectOnly(dialog, 2);
    dialog.moveSelected(-1);
    dialog.moveSelected(-1);
    dialog.moveSelected(-1);   // already first: nothing moves
    CHECK(pathAt(dialog, 0) == locked);
    CHECK(pathAt(dialog, 1) == a);
    CHECK(pathAt(dialog, 2) == b);
    CHECK(dialog.choice().value().inputs[2].pages == std::vector<int>{1, 2, 3, 11});   // the range moved with its file
    selectOnly(dialog, 3);
    dialog.removeSelected();
    CHECK(dialog.table()->rowCount() == 3);

    // A bad range keeps the dialog open, the file named.
    dialog.table()->item(2, ui::MergeDialog::RangeColumn)->setText(QStringLiteral("13"));
    CHECK(keyOf(dialog.choice()) == std::string("error.merge.badRange"));
    dialog.accept();
    CHECK(dialog.result() != QDialog::Accepted);
    CHECK_FALSE(dialog.refusalLabel()->isHidden());
    CHECK(dialog.refusalLabel()->text().contains(QStringLiteral("b.pdf")));
}

TEST_CASE("merge.dialog takes the files a drag from Explorer drops on it", "[merge][merge.dialog]")
{
    uitest::ShellFixture fixture;
    const QString one = QDir(fixture.directory.path()).filePath(QStringLiteral("one.pdf"));
    const QString two = QDir(fixture.directory.path()).filePath(QStringLiteral("two.pdf"));
    for (const QString& path : {one, two}) {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("%PDF-1.7\n");
    }
    ui::MergeDialog dialog(core::MergeOptions{}, counterOf({{one, 1}, {two, 2}}));
    QMimeData data;
    data.setUrls({QUrl::fromLocalFile(one), QUrl::fromLocalFile(two), QUrl(QStringLiteral("https://example.com/remote.pdf"))});
    QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&dialog, &enter);
    CHECK(enter.isAccepted());
    QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &data, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&dialog, &drop);
    REQUIRE(dialog.table()->rowCount() == 2);   // the local files, in the order dragged; never a URL
    CHECK(pathAt(dialog, 0) == one);
    CHECK(pathAt(dialog, 1) == two);
}

TEST_CASE("merge.dialog starts from documents/mergeBookmarkPerFile and documents/renameCollidingFields", "[merge][merge.dialog][A-66][A-68]")
{
    Answers answers;
    Shell shell;
    CHECK_FALSE(shell.fixture.actions.isEnabled(ActionId::MergeDocuments));   // nobody to ask (convention 16)
    std::optional<core::MergeOptions> seen;
    ui::MergeDialog::setAnswerForTesting([&seen](const core::MergeOptions& defaults) -> std::optional<ui::MergeDialog::Choice> {
        seen = defaults;
        return std::nullopt;   // the person cancels
    });
    REQUIRE(shell.fixture.actions.isEnabled(ActionId::MergeDocuments));   // Global: no document needed
    REQUIRE(shell.fixture.actions.dispatch(ActionId::MergeDocuments, core::KeyContext::Global));
    REQUIRE(seen);
    CHECK(seen->bookmarks == core::MergeBookmarks::FileName);
    CHECK(seen->renameCollidingFields);
    CHECK(shell.fixture.backend.merges.empty());   // cancelled: nothing merged

    shell.fixture.settings.set(core::keys::documents::mergeBookmarkPerFile, core::MergeBookmarks::Title);
    shell.fixture.settings.set(core::keys::documents::renameCollidingFields, false);
    REQUIRE(shell.fixture.actions.dispatch(ActionId::MergeDocuments, core::KeyContext::Global));
    CHECK(seen->bookmarks == core::MergeBookmarks::Title);
    CHECK_FALSE(seen->renameCollidingFields);

    // ... and the dialog shows them.
    ui::MergeDialog dialog(*seen, {});
    CHECK(static_cast<core::MergeBookmarks>(dialog.bookmarksBox()->currentData().toInt()) == core::MergeBookmarks::Title);
    CHECK(dialog.mergeButton()->isChecked());
    CHECK_FALSE(dialog.separatorBox()->isChecked());
}

TEST_CASE("merge.documents merges off the UI thread and opens the result untitled and read-only", "[merge][merge.dialog][A-72]")
{
    Answers answers;
    Shell shell;
    const ui::MergeDialog::Choice choice{{core::MergeInput{QStringLiteral("C:/in/a.pdf"), {}}, core::MergeInput{QStringLiteral("C:/in/b.pdf"), {0, 1}}},
                                         core::MergeOptions{core::MergeBookmarks::None, false, true}};
    ui::MergeDialog::setAnswerForTesting([&choice](const core::MergeOptions&) { return std::optional<ui::MergeDialog::Choice>(choice); });
    shell.fixture.backend.mergeReport.skipped.push_back(core::MergeSkip{1, QStringLiteral("C:/in/b.pdf"), QStringLiteral("error.pdf.encrypted"), {}});
    shell.fixture.backend.mergeReport.fields.push_back(core::FieldCollision{QStringLiteral("name"), QString(), 1});
    shell.fixture.backend.mergeStepMs = 30;
    REQUIRE(shell.fixture.actions.dispatch(ActionId::MergeDocuments, core::KeyContext::Global));
    // RUNNING, and the dispatch returned: the work is on the runner's thread. A second is refused while it runs.
    CHECK(shell.window->isMerging());
    CHECK_FALSE(shell.fixture.actions.isEnabled(ActionId::MergeDocuments));
    REQUIRE(shell.waitForMerge());
    REQUIRE(shell.fixture.backend.merges.size() == 1);
    CHECK(shell.fixture.backend.merges.front().first == choice.inputs);
    CHECK(shell.fixture.backend.merges.front().second == choice.options);
    // The new document: one tab, untitled, opened from the merged bytes as an ASSEMBLY -- read-only until Saved As.
    REQUIRE(shell.window->documentCount() == 1);
    ui::DocumentWindow* merged = shell.window->currentDocument();
    REQUIRE(merged != nullptr);
    CHECK(merged->isAssembly());
    CHECK_FALSE(merged->isEditable());
    CHECK(merged->isDirty());
    REQUIRE_FALSE(shell.fixture.backend.openedOrigins.isEmpty());
    CHECK(shell.fixture.backend.openedOrigins.back() == core::BytesOrigin::Assembly);
    // What happened, on the status bar: the files merged, the one skipped and the field merged.
    CHECK(shell.said().contains(QStringLiteral("2")));
    CHECK(shell.fixture.actions.isEnabled(ActionId::MergeDocuments));
}

TEST_CASE("merge.documents cancelled opens nothing and can be started again", "[merge][merge.dialog][A-72]")
{
    Answers answers;
    Shell shell;
    // Seven steps of 200 ms: run to its end the merge takes 1.4 s, past §3.5's second; stopped at its next step, one.
    std::vector<core::MergeInput> five;
    for (int i = 0; i < 5; ++i) five.push_back(core::MergeInput{QStringLiteral("C:/in/%1.pdf").arg(i), {}});
    const ui::MergeDialog::Choice choice{five, core::MergeOptions{}};
    ui::MergeDialog::setAnswerForTesting([&choice](const core::MergeOptions&) { return std::optional<ui::MergeDialog::Choice>(choice); });
    shell.fixture.backend.mergeStepMs = 200;
    REQUIRE(shell.fixture.actions.dispatch(ActionId::MergeDocuments, core::KeyContext::Global));
    REQUIRE(shell.window->isMerging());
    QElapsedTimer clock;
    clock.start();
    shell.window->cancelMerge();
    REQUIRE(shell.waitForMerge());
    CHECK(clock.elapsed() < 1000);   // §3.5: cancel to stopped inside a second -- one step of the fake here
    CHECK(shell.fixture.backend.mergeStepsRun < 7);   // stopped at a step, not run to the end and then thrown away
    CHECK(shell.window->documentCount() == 0);
    CHECK(shell.said().contains(QStringLiteral("cancel"), Qt::CaseInsensitive));

    // ... and a merge started again runs to its end.
    shell.fixture.backend.mergeStepMs = 0;
    REQUIRE(shell.fixture.actions.dispatch(ActionId::MergeDocuments, core::KeyContext::Global));
    REQUIRE(shell.waitForMerge());
    CHECK(shell.window->documentCount() == 1);
    CHECK(shell.fixture.backend.merges.size() == 2);
}

TEST_CASE("merge.documents refused by the backend says why and opens nothing", "[merge][merge.dialog][A-70]")
{
    Answers answers;
    Shell shell;
    const ui::MergeDialog::Choice choice{{core::MergeInput{QStringLiteral("C:/in/a.pdf"), {}}}, core::MergeOptions{}};
    ui::MergeDialog::setAnswerForTesting([&choice](const core::MergeOptions&) { return std::optional<ui::MergeDialog::Choice>(choice); });
    shell.fixture.backend.mergeFails = "error.merge.nothingMerged";
    REQUIRE(shell.fixture.actions.dispatch(ActionId::MergeDocuments, core::KeyContext::Global));
    REQUIRE(shell.waitForMerge());
    CHECK(shell.window->documentCount() == 0);
    CHECK_FALSE(shell.said().isEmpty());
}
