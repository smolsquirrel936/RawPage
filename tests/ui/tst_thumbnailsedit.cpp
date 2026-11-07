// tst_thumbnailsedit -- Stage 7 T8, the thumbnails pane's edits (`Stage7-Assembly-Plan.md` D7-13, D7-20;
// `Stage7-Assembly-Testing-Guide.md` §5.10): the whole Page Thumbnails table from key events (A-52), the selection as a
// set that follows its pages (A-53), a drag within a pane and into another window's (A-54), every text field keeping
// its own Ctrl+X/C/V (A-56), the badges painted (A-57's paint), the rotate/insert/delete/replace dialogs, and D7-20's
// warning (A-27's warning half).
//
// WHAT IT DOES NOT COVER (convention 19):
// * **The fake backend draws no page.** That a moved, pasted or dropped page SHOWS its identity is
//   `tests/composition/tst_pagesproduct.cpp`'s, through the engine; here a page command is counted by the fake sink and
//   the model's page count.
// * **`QDrag::exec`**, the SOURCE half of a drag, is a nested event loop a headless case cannot drive. The source's data
//   is taken from `ThumbnailsPane::dragData()` and the DROP half is driven with real drag events sent to the target's
//   viewport; the gesture as a person makes it is M-01's.
// * **The clipboard is offscreen's, in this process.** Pages pasted into a SECOND process are `[D7-13]`'s, on the
//   `windows` platform (`tst_pagesproduct.cpp`).
// * **A bookmark's rename editor** is named by A-56 and does not exist until T11; the three text fields that exist are
//   here.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/CopiedPages.h"
#include "core/doc/Document.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/PageOpsDialogs.h"
#include "ui/dialogs/RotatePagesDialog.h"
#include "ui/panes/ThumbnailsPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/MessageBar.h"
#include "ui/shell/StatusBar.h"
#include "ui/shell/ToolFinder.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ViewportController.h"
#include "ui/write/FindReplaceBar.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QRadioButton>
#include <QScrollBar>
#include <QSpinBox>

#include <memory>

using namespace rawpage;
using core::ActionId;

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

void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

// Every installed answer cleared however a case ends.
struct Answers {
    ~Answers()
    {
        ui::RotatePagesDialog::setAnswerForTesting({});
        ui::ExtractPagesDialog::setAnswerForTesting({});
        ui::InsertPagesDialog::setAnswerForTesting({});
        ui::DeletePagesDialog::setAnswerForTesting({});
        ui::ReplacePagesDialog::setAnswerForTesting({});
        if (QClipboard* clipboard = QGuiApplication::clipboard()) clipboard->clear();
    }
};

// One window, one editable document of `pages` pages, the Thumbnails pane laid out and focused.
struct Window {
    std::unique_ptr<ui::MainWindow> window;
    ui::ThumbnailsPane* pane = nullptr;

    Window(Fixture& fixture, const QString& name)
    {
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1200, 900);
        window->openFiles({fixture.writeFile(name)});
        driveLayout(window.get());
        pane = window->thumbnailsPane();
        REQUIRE(pane != nullptr);
        REQUIRE(fixture.actions.dispatch(ActionId::PaneThumbnails, core::KeyContext::Global));
        driveLayout(window.get());
        window->raiseToForeground();
        pane->setFocus(Qt::OtherFocusReason);
        pane->refresh();
        QCoreApplication::processEvents();
        REQUIRE(pane->height() > 200);
        REQUIRE_FALSE(pane->visiblePages().empty());
    }

    ui::DocumentWindow* document() const { return window->currentDocument(); }
    core::CommandJournal& journal() const { return *document()->journal(); }

    // A real key to the focused widget, through the window's own binder. Returns whether it was consumed.
    bool press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString& text = QString()) const
    {
        QWidget* focus = window->focusWidget();
        REQUIRE(focus != nullptr);
        QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
        event.ignore();
        QCoreApplication::sendEvent(focus, &event);
        QCoreApplication::processEvents();
        return event.isAccepted();
    }

    // A click on page `page`'s slot, in the viewport -- scrolled into view first by the scroll bar alone, which moves
    // no selection (`setCurrentPage` would make a one-page selection follow it).
    void click(int page, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        const QPoint at = slotCentre(page);
        QMouseEvent pressed(QEvent::MouseButtonPress, at, pane->viewport()->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(pane->viewport(), &pressed);
        QMouseEvent released(QEvent::MouseButtonRelease, at, pane->viewport()->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, modifiers);
        QCoreApplication::sendEvent(pane->viewport(), &released);
        QCoreApplication::processEvents();
    }

    // The centre of page `page`'s sheet in viewport coordinates, found by asking where a drop would land on each side.
    QPoint slotCentre(int page) const
    {
        QScrollBar* bar = pane->verticalScrollBar();
        for (int value = 0; value <= bar->maximum() + bar->pageStep(); value += std::max(1, bar->pageStep() / 2)) {
            bar->setValue(std::min(value, bar->maximum()));
            QCoreApplication::processEvents();
            for (int y = 2; y < pane->viewport()->height(); ++y)
                if (pane->insertionIndexAt(QPoint(60, y)) == page + 1 && pane->insertionIndexAt(QPoint(60, y - 1)) == page)
                    return QPoint(60, y - 2);
        }
        FAIL("page " << page << " is never on screen");
        return {};
    }
};

// Drag events, the drop half -- see the header.
bool dropOn(ui::ThumbnailsPane* pane, QMimeData* data, QPoint at)
{
    QDragEnterEvent enter(at, Qt::MoveAction | Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(pane->viewport(), &enter);
    if (!enter.isAccepted()) return false;
    QDragMoveEvent move(at, Qt::MoveAction | Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(pane->viewport(), &move);
    QDropEvent drop(at, Qt::MoveAction | Qt::CopyAction, data, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(pane->viewport(), &drop);
    QCoreApplication::processEvents();
    return drop.isAccepted();
}

// The point between page `before - 1` and page `before` -- a drop there lands before page `before`.
QPoint gapBefore(ui::ThumbnailsPane& pane, int before)
{
    QScrollBar* bar = pane.verticalScrollBar();
    for (int value = 0; value <= bar->maximum() + bar->pageStep(); value += std::max(1, bar->pageStep() / 2)) {
        bar->setValue(std::min(value, bar->maximum()));
        QCoreApplication::processEvents();
        // Any point that lands before `before` -- away from the column's ends, so a drag held there does not scroll the
        // column under it.
        for (int y = pane.viewport()->height() / 6; y < pane.viewport()->height() * 5 / 6; ++y)
            if (pane.insertionIndexAt(QPoint(20, y)) == before) return QPoint(20, y);
    }
    FAIL("no gap before page " << before << " on screen");
    return {};
}

} // namespace

// =============================================================================================
// A-52 -- the whole Page Thumbnails table, from key events
// =============================================================================================

TEST_CASE("ui.thumbnails_edit every row of the Page Thumbnails table runs from its key on the set", "[thumbnailsedit][A-52]")
{
    Answers answers;
    Fixture fixture;
    fixture.backend.pagesPerDocument = 12;
    fixture.backend.editable = true;
    Window shell(fixture, QStringLiteral("twelve.pdf"));
    core::CommandJournal& journal = shell.journal();

    // Ctrl+A: every page.
    REQUIRE(shell.press(Qt::Key_A, Qt::ControlModifier));
    CHECK(shell.pane->selectedPages().size() == 12);

    // The arrows (the widget's own, `BindingKind::WidgetNative`): one page, then Shift extends from the anchor.
    shell.click(2);
    CHECK(shell.pane->selectedPages() == std::vector<int>{2});
    REQUIRE(shell.press(Qt::Key_Down));
    CHECK(shell.pane->selectedPages() == std::vector<int>{3});
    REQUIRE(shell.press(Qt::Key_Down, Qt::ShiftModifier));
    REQUIRE(shell.press(Qt::Key_Down, Qt::ShiftModifier));
    CHECK(shell.pane->selectedPages() == std::vector<int>{3, 4, 5});

    // Alt+Down / Alt+Up: the set one place later / earlier, ONE undo step each, and the set FOLLOWS its pages.
    REQUIRE(shell.press(Qt::Key_Down, Qt::AltModifier));
    CHECK(journal.historySize() == 1);
    CHECK(shell.pane->selectedPages() == std::vector<int>{4, 5, 6});
    REQUIRE(shell.press(Qt::Key_Up, Qt::AltModifier));
    CHECK(journal.historySize() == 2);
    CHECK(shell.pane->selectedPages() == std::vector<int>{3, 4, 5});

    // Ctrl+Shift+D: one duplicate of the set.
    REQUIRE(shell.press(Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier));
    CHECK(journal.historySize() == 3);
    CHECK(shell.document()->pageCount() == 15);

    // Ctrl+R: the dialog, offered the SET, one command over it (D7-13: Stage 4's single selection ended).
    ui::RotatePagesDialog::setAnswerForTesting([] { return ui::RotatePagesDialog::Choice{90, ui::RotatePagesDialog::Pages::Selected}; });
    const std::vector<int> set = shell.pane->selectedPages();
    REQUIRE(shell.press(Qt::Key_R, Qt::ControlModifier));
    CHECK(journal.historySize() == 4);
    CHECK(journal.undoStep().size() == 1);
    for (int page = 0; page < 15; ++page) {
        INFO("page " << page);
        const bool turned = std::find(set.begin(), set.end(), page) != set.end();
        CHECK(static_cast<int>(journal.document().pages().rotation(page)) == (turned ? 90 : 0));
    }

    // Ctrl+Shift+X: extract, the dialog's range filled in with the set (cancelled: nothing made).
    std::vector<int> offered;
    ui::ExtractPagesDialog::setAnswerForTesting([&offered](int, const std::vector<int>& initial) -> std::optional<ui::ExtractPagesDialog::Choice> {
        offered = initial;
        return std::nullopt;
    });
    REQUIRE(shell.press(Qt::Key_X, Qt::ControlModifier | Qt::ShiftModifier));
    CHECK(offered == set);

    // Ctrl+C / Ctrl+V: the pages on the clipboard, then pasted AFTER the set -- one insert.
    REQUIRE(shell.press(Qt::Key_C, Qt::ControlModifier));
    CHECK(fixture.backend.copied.back() == set);
    CHECK(QGuiApplication::clipboard()->mimeData()->hasFormat(QLatin1String(core::kPagesMimeType)));
    REQUIRE(shell.press(Qt::Key_V, Qt::ControlModifier));
    CHECK(journal.historySize() == 5);
    // AFTER THE SET'S LAST PAGE (D7-31(c)) -- where the sink was told the pages land.
    const auto* fakeSink = fixture.backend.sinkFor(shell.document()->handle());
    REQUIRE(fakeSink != nullptr);
    REQUIRE_FALSE(fakeSink->insertedAt.empty());
    CHECK(fakeSink->insertedAt.back() == set.back() + 1);
    CHECK(shell.document()->pageCount() == 15 + static_cast<int>(set.size()));

    // Ctrl+X: copy, then ONE delete.
    shell.click(0);
    const int before = shell.document()->pageCount();
    REQUIRE(shell.press(Qt::Key_X, Qt::ControlModifier));
    CHECK(journal.historySize() == 6);
    CHECK(shell.document()->pageCount() == before - 1);
    CHECK(fixture.backend.copied.back() == std::vector<int>{0});

    // Delete: the set, no question, one step.
    shell.click(1);
    REQUIRE(shell.press(Qt::Key_Down, Qt::ShiftModifier));
    REQUIRE(shell.press(Qt::Key_Delete));
    CHECK(journal.historySize() == 7);
    CHECK(shell.document()->pageCount() == before - 3);

    // Enter: go to the selected page; + and -: the thumbnail size.
    shell.click(4);
    REQUIRE(shell.press(Qt::Key_Return));
    CHECK(shell.document()->view()->controller().currentPage() == 4);
    const int width = shell.pane->thumbnailWidth();
    REQUIRE(shell.press(Qt::Key_Plus, Qt::NoModifier, QStringLiteral("+")));
    CHECK(shell.pane->thumbnailWidth() > width);
    REQUIRE(shell.press(Qt::Key_Minus, Qt::NoModifier, QStringLiteral("-")));
    CHECK(shell.pane->thumbnailWidth() == width);

    // EVERY STEP UNDOES ONE GESTURE: seven undos, and the document is twelve unturned pages again.
    for (int step = 0; step < 7; ++step) REQUIRE(journal.undo());
    CHECK(shell.document()->pageCount() == 12);
    for (int page = 0; page < 12; ++page) CHECK(static_cast<int>(journal.document().pages().rotation(page)) == 0);
}

TEST_CASE("ui.thumbnails_edit a set at the end does not move further and every page is never deleted", "[thumbnailsedit][A-52]")
{
    Answers answers;
    Fixture fixture;
    fixture.backend.pagesPerDocument = 4;
    fixture.backend.editable = true;
    Window shell(fixture, QStringLiteral("four.pdf"));

    // THE CONTROL FOR THE ENABLEMENT: the last page cannot go later, the first earlier -- disabled, so the key is left
    // alone and nothing is run (a move to where the pages already are would be an undo step that changed nothing).
    shell.click(3);
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::PageMoveLater));
    (void)shell.press(Qt::Key_Down, Qt::AltModifier);
    CHECK(shell.journal().historySize() == 0);
    shell.click(0);
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::PageMoveEarlier));
    CHECK(fixture.actions.isEnabled(ActionId::PageMoveLater));

    // Every page: the delete is refused by its key and nothing changes; the cut is disabled.
    REQUIRE(shell.press(Qt::Key_A, Qt::ControlModifier));
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::PageCut));
    (void)shell.press(Qt::Key_Delete);
    CHECK(shell.journal().historySize() == 0);
    CHECK(shell.document()->pageCount() == 4);
}

// =============================================================================================
// A-53 -- selection is a set, and follows its pages
// =============================================================================================

TEST_CASE("thumbnails.selection click shift-click and ctrl-click give a file manager's sets", "[thumbnailsedit][A-53]")
{
    Answers answers;
    Fixture fixture;
    fixture.backend.pagesPerDocument = 12;
    fixture.backend.editable = true;
    Window shell(fixture, QStringLiteral("twelve.pdf"));
    ui::ThumbnailsPane* pane = shell.pane;

    shell.click(2);
    CHECK(pane->selectedPages() == std::vector<int>{2});
    CHECK(pane->anchorPage() == 2);
    shell.click(5, Qt::ShiftModifier);
    CHECK(pane->selectedPages() == std::vector<int>{2, 3, 4, 5});
    shell.click(0, Qt::ShiftModifier);   // the anchor stays: the run is redrawn from it
    CHECK(pane->selectedPages() == std::vector<int>{0, 1, 2});
    shell.click(7, Qt::ControlModifier);
    CHECK(pane->selectedPages() == std::vector<int>{0, 1, 2, 7});
    shell.click(1, Qt::ControlModifier);
    CHECK(pane->selectedPages() == std::vector<int>{0, 2, 7});
    CHECK(pane->anchorPage() == 1);
    shell.click(3, Qt::ControlModifier | Qt::ShiftModifier);   // a run ADDED from the anchor
    CHECK(pane->selectedPages() == std::vector<int>{0, 1, 2, 3, 7});

    // A set is not undone by the view moving: the view going to page 9 leaves it (see the header).
    shell.document()->view()->goToPage(9);
    QCoreApplication::processEvents();
    CHECK(pane->selectedPages() == std::vector<int>{0, 1, 2, 3, 7});

    // ONLY A PLAIN CLICK NAVIGATES: the Ctrl+click above left the view where it was.
    shell.click(10, Qt::ControlModifier);
    CHECK(shell.document()->view()->controller().currentPage() == 9);
    shell.click(10);
    CHECK(pane->selectedPages() == std::vector<int>{10});
    CHECK(shell.document()->view()->controller().currentPage() == 10);

    // AFTER A MOVE THE SET IS THE SAME PAGES (D7-3): {1, 4} moved to the end are {10, 11}; a delete drops a page from it.
    pane->setSelectedPages({1, 4});
    REQUIRE(shell.window->movePagesTo(shell.document(), {1, 4}, 12));
    CHECK(pane->selectedPages() == std::vector<int>{10, 11});
    pane->setSelectedPages({0, 10, 11});
    REQUIRE(shell.window->deletePages(shell.document(), {10}));
    CHECK(pane->selectedPages() == std::vector<int>{0, 10});
}

// =============================================================================================
// A-54 -- a drag reorders in one step, and copies across windows
// =============================================================================================

TEST_CASE("thumbnails.drag a set dropped in its own pane is one move, landing where the marker says", "[thumbnailsedit][A-54]")
{
    Answers answers;
    Fixture fixture;
    fixture.backend.pagesPerDocument = 12;
    fixture.backend.editable = true;
    Window shell(fixture, QStringLiteral("twelve.pdf"));
    shell.pane->setSelectedPages({1, 2});

    std::vector<int> requested;
    int at = -1;
    QObject::connect(shell.pane, &ui::ThumbnailsPane::movePagesRequested, shell.pane,
                     [&](const std::vector<int>& pages, int insertionIndex) {
                         requested = pages;
                         at = insertionIndex;
                     });
    const QPoint gap = gapBefore(*shell.pane, 5);
    // THE MARKER while the drag is over the pane: the index the drop will use.
    {
        std::unique_ptr<QMimeData> data(shell.pane->dragData());
        QDragEnterEvent enter(gap, Qt::MoveAction, data.get(), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(shell.pane->viewport(), &enter);
        CHECK(enter.isAccepted());
        CHECK(shell.pane->dropMarker() == 5);
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(shell.pane->viewport(), &leave);
        CHECK(shell.pane->dropMarker() == -1);
    }
    std::unique_ptr<QMimeData> data(shell.pane->dragData());
    REQUIRE(dropOn(shell.pane, data.get(), gap));
    CHECK(requested == std::vector<int>{1, 2});
    CHECK(at == 5);
    // One MovePages, one undo step: pages 1, 2 now stand at 3, 4 -- before what was page 5.
    CHECK(shell.journal().historySize() == 1);
    CHECK(shell.journal().undoStep().size() == 1);
    CHECK(shell.pane->selectedPages() == std::vector<int>{3, 4});
    // Dropped where they already are: no command.
    std::unique_ptr<QMimeData> again(shell.pane->dragData());
    REQUIRE(dropOn(shell.pane, again.get(), gapBefore(*shell.pane, 4)));
    CHECK(shell.journal().historySize() == 1);
}

TEST_CASE("thumbnails.drag a set dropped into another window's pane is one insert there and the source unchanged", "[thumbnailsedit][A-54]")
{
    Answers answers;
    Fixture fixture;
    fixture.backend.pagesPerDocument = 6;
    fixture.backend.editable = true;
    Window source(fixture, QStringLiteral("source.pdf"));
    Window target(fixture, QStringLiteral("target.pdf"));
    source.pane->setSelectedPages({0, 3});

    std::unique_ptr<QMimeData> data(source.pane->dragData());
    REQUIRE(dropOn(target.pane, data.get(), gapBefore(*target.pane, 2)));
    CHECK(fixture.backend.copied.back() == std::vector<int>{0, 3});   // copied out of the SOURCE as it stands
    CHECK(target.document()->pageCount() == 8);
    CHECK(target.journal().historySize() == 1);
    CHECK(source.document()->pageCount() == 6);
    CHECK(source.journal().historySize() == 0);
    CHECK_FALSE(source.document()->isDirty());

    // A DRAG FROM ANOTHER PROCESS carries a handle that means nothing here: refused, nothing run.
    QMimeData foreign;
    foreign.setData(QLatin1String(ui::ThumbnailsPane::kDragMimeType),
                    QByteArray(R"({"pid":1,"document":")") + QByteArray::number(static_cast<qulonglong>(source.document()->handle().id))
                        + QByteArray(R"(","pages":[0]})"));
    CHECK_FALSE(dropOn(target.pane, &foreign, gapBefore(*target.pane, 1)));
    CHECK(target.journal().historySize() == 1);
}

// =============================================================================================
// A-56 -- every text field keeps its own Ctrl+X, Ctrl+C and Ctrl+V
// =============================================================================================

TEST_CASE("thumbnails.clipboard_controls the page box the find bar and the tool finder keep their clipboard keys", "[thumbnailsedit][A-56]")
{
    Answers answers;
    Fixture fixture;
    fixture.backend.pagesPerDocument = 12;
    fixture.backend.editable = true;
    Window shell(fixture, QStringLiteral("twelve.pdf"));
    // Pages on the clipboard first, so a Ctrl+V that wrongly reached `page.paste` would insert them.
    REQUIRE(shell.window->copyPages(shell.document(), {0}));
    std::size_t copies = fixture.backend.copied.size();

    std::vector<QLineEdit*> fields;
    fields.push_back(shell.window->statusBar()->pageBox());
    shell.document()->findBar()->open();
    fields.push_back(shell.document()->findBar()->field());
    shell.window->toolFinder()->focusSearchBox();
    fields.push_back(shell.window->toolFinder()->searchBox());
    for (QLineEdit* field : fields) {
        INFO(field->objectName().toStdString());
        REQUIRE(field != nullptr);
        field->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        REQUIRE(shell.window->focusWidget() == field);
        field->setText(QStringLiteral("12"));
        field->selectAll();
        CHECK(shell.press(Qt::Key_X, Qt::ControlModifier));    // the FIELD cut its text
        CHECK(field->text().isEmpty());
        CHECK(shell.press(Qt::Key_V, Qt::ControlModifier));    // and pasted it back
        CHECK(field->text() == QStringLiteral("12"));
        field->selectAll();
        CHECK(shell.press(Qt::Key_C, Qt::ControlModifier));
        CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("12"));
        // No page was cut, copied or pasted.
        CHECK(shell.journal().historySize() == 0);
        CHECK(fixture.backend.copied.size() == copies);
        // Put pages back for the next field, outside the field's focus.
        REQUIRE(shell.window->copyPages(shell.document(), {0}));
        ++copies;
    }
    CHECK(shell.document()->pageCount() == 12);
}

// =============================================================================================
// A-57's paint -- the badges are drawn where the data says
// =============================================================================================

TEST_CASE("thumbnails.badges are painted on the pages that carry something and nowhere else", "[thumbnailsedit][A-57]")
{
    Answers answers;
    Fixture fixture;
    fixture.backend.pagesPerDocument = 4;
    Window shell(fixture, QStringLiteral("four.pdf"));
    using Carries = core::AnnotationStore::PageCarries;
    shell.pane->setBadges({Carries{true, false, false}, Carries{}, Carries{true, true, true}, Carries{false, false, true}});
    shell.pane->viewport()->repaint();
    const QImage grabbed = shell.pane->viewport()->grab().toImage();
    REQUIRE(shell.pane->badgeRect(0).isValid());
    CHECK(shell.pane->badgeRect(1).isEmpty());
    CHECK(shell.pane->badgeRect(2).width() > shell.pane->badgeRect(0).width());   // three marks, not one
    // The badge's square differs from the blank sheet behind it; page 1's same square is the sheet (white).
    const auto inkIn = [&](const QRect& rect) {
        int ink = 0;
        const qreal ratio = grabbed.devicePixelRatio();
        for (int y = rect.top(); y <= rect.bottom(); ++y)
            for (int x = rect.left(); x <= rect.right(); ++x)
                if (grabbed.pixel(QPoint(static_cast<int>(x * ratio), static_cast<int>(y * ratio))) != qRgb(255, 255, 255)) ++ink;
        return ink;
    };
    const QRect onZero = shell.pane->badgeRect(0);
    CHECK(inkIn(onZero) > onZero.width() * onZero.height() / 4);
    // THE CONTROL: the same square a slot further down, on page 1, which carries nothing -- the white sheet.
    const int slotStep = shell.slotCentre(1).y() - shell.slotCentre(0).y();
    shell.pane->verticalScrollBar()->setValue(0);
    CHECK(inkIn(onZero.translated(0, slotStep)) == 0);
    CHECK(shell.pane->badgesOf(3) == Carries{false, false, true});
    CHECK(shell.pane->badgesOf(1) == Carries{});
}

// =============================================================================================
// The dialogs -- rotate's ranges (A-59's widget half), insert (A-45's), delete, replace
// =============================================================================================

TEST_CASE("pageops the rotate dialog offers the set, a range and the filters, and refuses an empty result", "[thumbnailsedit][pageops][A-59]")
{
    ui::RotatePagesDialog dialog(2, {1, 3, 4}, 9);
    CHECK(dialog.selectedPagesButton()->isChecked());   // a set of several: offered and chosen
    CHECK(dialog.selectedPagesButton()->text().contains(QStringLiteral("2, 4-5")));
    CHECK(dialog.rangeField()->text() == QStringLiteral("2, 4-5"));
    dialog.rangeButton()->setChecked(true);
    dialog.rangeField()->setText(QStringLiteral("1-6"));
    dialog.parityBox()->setCurrentIndex(dialog.parityBox()->findData(static_cast<int>(core::PageParity::Even)));
    auto chosen = dialog.choice();
    REQUIRE(chosen);
    CHECK(chosen.value().pages == ui::RotatePagesDialog::Pages::Range);
    CHECK(chosen.value().range == QStringLiteral("1-6"));
    CHECK(chosen.value().parity == core::PageParity::Even);
    dialog.rangeField()->setText(QStringLiteral("6-1"));
    dialog.accept();
    CHECK(dialog.result() != QDialog::Accepted);
    CHECK_FALSE(dialog.refusalLabel()->isHidden());

    // The plan over a real tree: even pages of 1-6 are 2, 4, 6.
    Fixture fixture;
    fixture.backend.pagesPerDocument = 9;
    const QString path = fixture.writeFile(QStringLiteral("nine.pdf"));
    auto opened = fixture.backend.open(path, {});
    REQUIRE(opened);
    core::Document model;
    REQUIRE(model.hydrate(fixture.backend, opened.value()));
    ui::RotatePagesDialog::Choice even(90, ui::RotatePagesDialog::Pages::Range);
    even.range = QStringLiteral("1-6");
    even.parity = core::PageParity::Even;
    auto plan = ui::planRotation(even, 0, {}, model.pages());
    REQUIRE(plan);
    CHECK(dynamic_cast<const core::RotatePageCommand&>(*plan.value()).pages() == std::vector<int>{1, 3, 5});
    // A filter that leaves nothing: refused by key, no command (all the fake's pages are portrait).
    ui::RotatePagesDialog::Choice none(90, ui::RotatePagesDialog::Pages::All);
    none.orientation = core::PageOrientation::Landscape;
    auto refused = ui::planRotation(none, 0, {}, model.pages());
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.pages.noneMatch"));
}

TEST_CASE("pageops the insert dialog starts at the default page size and says where the pages go", "[thumbnailsedit][pageops][A-45]")
{
    ui::InsertPagesDialog dialog(10, 3, core::PaperSize::Letter, 0);
    CHECK(dialog.blankSizeBox()->currentData().toInt() == static_cast<int>(core::PaperSize::Letter));
    CHECK_FALSE(dialog.sourceButton(ui::InsertPagesDialog::Source::Clipboard)->isEnabled());   // nothing on it
    CHECK(dialog.pageField()->value() == 4);
    dialog.sourceButton(ui::InsertPagesDialog::Source::Blank)->setChecked(true);
    dialog.blankCountField()->setValue(2);
    dialog.landscapeBox()->setChecked(true);
    auto blank = dialog.choice();
    REQUIRE(blank);
    CHECK(blank.value().source == ui::InsertPagesDialog::Source::Blank);
    CHECK(blank.value().blankCount == 2);
    CHECK(blank.value().blankSize == core::PdfSize{792.0, 612.0});
    CHECK(blank.value().at == 4);   // after page 4
    dialog.whereBox()->setCurrentIndex(dialog.whereBox()->findData(static_cast<int>(ui::InsertPagesDialog::Where::Before)));
    CHECK(dialog.choice().value().at == 3);
    dialog.whereBox()->setCurrentIndex(dialog.whereBox()->findData(static_cast<int>(ui::InsertPagesDialog::Where::Last)));
    CHECK(dialog.choice().value().at == 10);
    // From a file, with no file named: refused in the dialog.
    dialog.sourceButton(ui::InsertPagesDialog::Source::File)->setChecked(true);
    CHECK_FALSE(dialog.choice());
    CHECK(ui::InsertPagesDialog(10, 0, core::PaperSize::A4, 3, nullptr).sourceButton(ui::InsertPagesDialog::Source::Clipboard)->isEnabled());
}

TEST_CASE("pageops.blank the insert through the product uses documents/defaultPageSize and the file's range", "[thumbnailsedit][pageops][A-45]")
{
    Answers answers;
    Fixture fixture;
    fixture.backend.pagesPerDocument = 5;
    fixture.backend.editable = true;
    fixture.settings.set(core::keys::documents::defaultPageSize, core::PaperSize::A5);
    Window shell(fixture, QStringLiteral("five.pdf"));
    core::PaperSize offeredSize = core::PaperSize::A4;
    ui::InsertPagesDialog::setAnswerForTesting([&](int, int, core::PaperSize size, int) -> std::optional<ui::InsertPagesDialog::Choice> {
        offeredSize = size;
        ui::InsertPagesDialog::Choice choice;
        choice.source = ui::InsertPagesDialog::Source::Blank;
        choice.blankCount = 2;
        choice.blankSize = core::paperSizePoints(size);
        choice.at = 1;
        return choice;
    });
    REQUIRE(fixture.actions.dispatch(ActionId::PageInsert, core::KeyContext::Global));
    CHECK(offeredSize == core::PaperSize::A5);
    REQUIRE_FALSE(fixture.backend.blankSizes.empty());
    CHECK(fixture.backend.blankSizes.back() == core::PdfSize{420.0, 595.0});
    CHECK(shell.document()->pageCount() == 7);

    // From a FILE: counted first, then its range read against that count -- `3-` of a four-page file is 3 and 4.
    const QString file = fixture.writeFile(QStringLiteral("source.pdf"));
    ui::InsertPagesDialog::setAnswerForTesting([&](int, int, core::PaperSize, int) -> std::optional<ui::InsertPagesDialog::Choice> {
        ui::InsertPagesDialog::Choice choice;
        choice.path = file;
        choice.range = QStringLiteral("3-");
        choice.at = 0;
        return choice;
    });
    REQUIRE(fixture.actions.dispatch(ActionId::PageInsert, core::KeyContext::Global));
    REQUIRE_FALSE(fixture.backend.fileCopies.empty());
    CHECK(fixture.backend.fileCopies.back().second == std::vector<int>{2, 3});
    CHECK(shell.document()->pageCount() == 9);
    CHECK(shell.journal().historySize() == 2);
}

TEST_CASE("pageops the delete and replace dialogs start at the set and refuse what cannot be done", "[thumbnailsedit][pageops]")
{
    ui::DeletePagesDialog remove(6, {1, 2});
    CHECK(remove.rangeField()->text() == QStringLiteral("2-3"));
    CHECK(remove.choice().value().pages == std::vector<int>{1, 2});
    remove.rangeField()->setText(QStringLiteral("1-6"));
    auto everything = remove.choice();
    REQUIRE_FALSE(everything);
    CHECK(everything.error().messageKey == QLatin1String("error.edit.noPagesLeft"));

    ui::ReplacePagesDialog replace(6, {4});
    CHECK(replace.rangeField()->text() == QStringLiteral("5"));
    CHECK_FALSE(replace.choice());   // no file named
    replace.fileField()->setText(QStringLiteral("C:/x.pdf"));
    replace.sourceFirstField()->setValue(3);
    auto chosen = replace.choice();
    REQUIRE(chosen);
    CHECK(chosen.value() == ui::ReplacePagesDialog::Choice{{4}, QStringLiteral("C:/x.pdf"), 2});

    // Through the product: the ribbon's Delete asks, the pane's does not (§8: the key deletes the set).
    Answers answers;
    Fixture fixture;
    fixture.backend.pagesPerDocument = 6;
    fixture.backend.editable = true;
    Window shell(fixture, QStringLiteral("six.pdf"));
    int asked = 0;
    ui::DeletePagesDialog::setAnswerForTesting([&asked](int, const std::vector<int>&) -> std::optional<ui::DeletePagesDialog::Choice> {
        ++asked;
        return ui::DeletePagesDialog::Choice{{4, 5}};
    });
    REQUIRE(fixture.actions.dispatch(ActionId::PageDelete, core::KeyContext::Global));
    CHECK(asked == 1);
    CHECK(shell.document()->pageCount() == 4);
    shell.click(0);
    REQUIRE(fixture.actions.dispatch(ActionId::PageDelete, core::KeyContext::PageThumbnails));
    CHECK(asked == 1);
    CHECK(shell.document()->pageCount() == 3);

    // Replace through the product: a file too short for the pages replaced is refused by the command's key.
    const QString file = fixture.writeFile(QStringLiteral("four.pdf"));
    ui::ReplacePagesDialog::setAnswerForTesting([&file](int, const std::vector<int>&) -> std::optional<ui::ReplacePagesDialog::Choice> {
        return ui::ReplacePagesDialog::Choice{{0, 1}, file, 3};   // pages 4 and 5 of a four-page file
    });
    REQUIRE(fixture.actions.dispatch(ActionId::PageReplace, core::KeyContext::Global));
    CHECK(shell.journal().historySize() == 2);
    CHECK(shell.window->statusBar()->message()->text()
          == ui::catalog::translateError(QStringLiteral("error.edit.replaceCount"), QStringLiteral("4")));
}

// =============================================================================================
// A-27's warning half -- D7-20
// =============================================================================================

TEST_CASE("pages a signed document is warned once before its first page command and an unsigned one never", "[thumbnailsedit][A-27][D7-20]")
{
    Answers answers;
    Fixture fixture;
    fixture.backend.pagesPerDocument = 6;
    fixture.backend.editable = true;
    Window plain(fixture, QStringLiteral("plain.pdf"));
    const core::Status duplicated = plain.window->duplicatePages(plain.document(), {0});
    INFO((duplicated ? std::string() : std::string(duplicated.error().messageKey.data()) + " " + duplicated.error().context.toStdString()));
    REQUIRE(duplicated);
    CHECK_FALSE(plain.window->messageBar()->isShowing(ui::MessageBar::Kind::Signed));
    CHECK(plain.document()->conditions().count(static_cast<int>(ui::MessageBar::Kind::Signed)) == 0);

    Window signedOne(fixture, QStringLiteral("signed.pdf"));
    fixture.backend.signatures[signedOne.document()->handle().id] = 2;
    CHECK_FALSE(signedOne.window->messageBar()->isShowing(ui::MessageBar::Kind::Signed));   // not before a change is asked
    REQUIRE(signedOne.window->stepPages(signedOne.document(), {0}, 1));
    REQUIRE(signedOne.window->messageBar()->isShowing(ui::MessageBar::Kind::Signed));
    const QString said = signedOne.window->messageBar()->textFor(ui::MessageBar::Kind::Signed);
    CHECK(said.contains(QLatin1Char('2')));
    CHECK(signedOne.journal().historySize() == 1);   // and the command RAN
    // ONCE: a second command does not raise it again (the banner is the document's condition, kept).
    signedOne.window->messageBar()->dismiss(ui::MessageBar::Kind::Signed);
    REQUIRE(signedOne.window->duplicatePages(signedOne.document(), {1}));
    CHECK_FALSE(signedOne.window->messageBar()->isShowing(ui::MessageBar::Kind::Signed));
    CHECK(signedOne.journal().historySize() == 2);
}
