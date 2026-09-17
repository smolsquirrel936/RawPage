// Stage 3 T10 -- the mutation UI: five commands, two dialogs, the dirty marker, the save banners and every
// way a dirty document can close. Guide cases A-71 … A-73 and A-75 … A-81 (`Stage3-Mutation-Testing-Guide.md`
// §5.11); A-74 is T11's, because it needs the recovery prompt. A-77's setting half arrived at T12.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS SUITE CANNOT SEE, NAMED HERE (convention 19).
//
// * **No real engine and no real replace.** Documents come from `uitest::FakeBackend` in its editable
//   mode, which saves by writing the opened bytes plus a marker line; `platform::IDurableFiles` is the
//   headless suite's `FakeDurableFiles`, whose replace is neither atomic nor stream-preserving. What a
//   rotation writes into a PDF, and what the real replace keeps, are `tests/render/tst_saveengine.cpp`'s
//   and `tests/platform/tst_durablefiles.cpp`'s.
// * **No modal dialog is shown.** Rotate Pages, Unsaved Changes and Save As are answered through their
//   parameterized twins (`setAnswerForTesting`, `MainWindow::setSaveAsAnswerForTesting`) -- under the
//   offscreen platform a modal dialog is shown to nobody and never returns (Stage 1's I-23). The Rotate
//   Pages WIDGET is built and driven by A-73's case; the Unsaved Changes and Save As widgets are not,
//   and a person sees them in manual case M-07 (`Stage3-Mutation-Testing-Guide.md` §6).
// * **Tiles are pumped by hand**, as everywhere in `tests/ui` -- `ShellFixture.h` names that back door.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "core/doc/RepairReport.h"
#include "core/render/DpiPolicy.h"
#include "core/render/PagePresentation.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/RotatePagesDialog.h"
#include "ui/dialogs/UnsavedChangesDialog.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentTabBar.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/MessageBar.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/ToolFinder.h"
#include "ui/viewer/PageView.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QRadioButton>
#include <QScrollBar>
#include <QTranslator>

#include <memory>

using namespace rawpage;
using core::ActionId;
using ui::CloseDisposition;
using ui::MessageBar;
using ui::RotatePagesDialog;

namespace {

struct Fixture : uitest::ShellFixture {
    Fixture() { backend.editable = true; }

    QString writePdf(const QString& name, const QByteArray& bytes = QByteArray("%PDF-1.7\n% a fake page\n"))
    {
        const QString path = QDir(directory.path()).filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(bytes) == bytes.size());
        file.close();
        return QFileInfo(path).absoluteFilePath();
    }
};

// Every twin is process-wide; a case that installed one must not leave it for the next case.
struct Answers {
    ~Answers()
    {
        RotatePagesDialog::setAnswerForTesting({});
        ui::UnsavedChangesDialog::setAnswerForTesting({});
        ui::MainWindow::setSaveAsAnswerForTesting({});
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

bool press(QWidget* target, Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    QKeyEvent event(QEvent::KeyPress, key, modifiers);
    event.ignore();
    QCoreApplication::sendEvent(target, &event);
    return event.isAccepted();
}

int rotationOf(const ui::DocumentWindow* document, int page)
{
    return static_cast<int>(document->journal()->document().pages().pages().at(static_cast<std::size_t>(page)).rotation());
}

std::string text(const QString& value) { return value.toStdString(); }

// A shown window with one editable document, the viewer focused -- the state a Viewer chord resolves in.
struct Editor {
    Fixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow* document = nullptr;
    ui::PageView* view = nullptr;
    QString path;

    explicit Editor(const std::function<void(Fixture&)>& configure = {}, const QString& name = QStringLiteral("edit.pdf"))
    {
        fixture.backend.pagesPerDocument = 3;
        if (configure) configure(fixture);
        path = fixture.writePdf(name);
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1000, 800);
        window->openFiles({path});
        driveLayout(window.get());
        window->raiseToForeground();

        document = window->currentDocument();
        REQUIRE(document != nullptr);
        REQUIRE(document->journal() != nullptr);
        view = document->view();
        REQUIRE(view != nullptr);
        view->setFocus();
        REQUIRE(window->shortcutBinder()->activeContext() == core::KeyContext::Viewer);
    }

    // A CLOSED DOCUMENT IS DELETED LATER, and its destructor closes its handle on THIS fixture's backend. Left
    // pending past the fixture, the next case's first `processEvents` ran it against a destroyed backend --
    // which the first run of the headless-close section measured as its fresh document losing its sink.
    ~Editor()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    // `page.rotate` through the registry with a rotation answer installed -- the gesture most cases need
    // before the thing they are about.
    void rotateCurrentPage(int degrees = 90)
    {
        RotatePagesDialog::setAnswerForTesting([degrees] { return RotatePagesDialog::Choice{degrees, false}; });
        const core::Result<void> dispatched = fixture.actions.dispatch(ActionId::PageRotate, core::KeyContext::Viewer);
        INFO("refused: " << (dispatched ? std::string() : std::string(dispatched.error().messageKey.data(), dispatched.error().messageKey.size()))
             << " active shell is this window: " << (ui::MainWindow::activeShell() == window.get())
             << " shells: " << ui::MainWindow::shellWindows().size());
        REQUIRE(dispatched);
    }

    QString tooltip(ActionId id) const
    {
        return ui::ActionFactory::tooltipFor(fixture.actions, id, ui::catalog::translateAction(id));
    }
};

} // namespace

// =============================================================================================
// A-71 -- five registrations, dispatched from key events.
// =============================================================================================

TEST_CASE("the mutation chords dispatch their commands from key events in the viewer", "[mutationcommands]")
{
    Answers answers;
    Editor editor;
    ui::DocumentWindow* document = editor.document;

    int rotateAsked = 0;
    RotatePagesDialog::setAnswerForTesting([&rotateAsked] {
        ++rotateAsked;
        return RotatePagesDialog::Choice{90, false};
    });
    QStringList saveAsAsked;
    const QString saveAsTarget = QDir(editor.fixture.directory.path()).filePath(QStringLiteral("saved-as.pdf"));
    ui::MainWindow::setSaveAsAnswerForTesting([&](const QString& current) -> std::optional<QString> {
        saveAsAsked << current;
        return saveAsTarget;
    });

    // Ctrl+R -- page.rotate, through the dialog's twin, on the current page.
    CHECK(press(editor.view, Qt::Key_R, Qt::ControlModifier));
    CHECK(rotateAsked == 1);
    CHECK(rotationOf(document, 0) == 90);
    CHECK(document->isDirty());

    // Ctrl+Z -- edit.undo, from the Common table.
    CHECK(press(editor.view, Qt::Key_Z, Qt::ControlModifier));
    CHECK(rotationOf(document, 0) == 0);
    CHECK_FALSE(document->isDirty());

    // Ctrl+Y and Ctrl+Shift+Z -- edit.redo, both chords.
    CHECK(press(editor.view, Qt::Key_Y, Qt::ControlModifier));
    CHECK(rotationOf(document, 0) == 90);
    CHECK(press(editor.view, Qt::Key_Z, Qt::ControlModifier));
    CHECK(rotationOf(document, 0) == 0);
    CHECK(press(editor.view, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier));
    CHECK(rotationOf(document, 0) == 90);

    // Ctrl+S -- file.save, in place: the backend wrote once and the document is clean at its own path.
    CHECK(press(editor.view, Qt::Key_S, Qt::ControlModifier));
    CHECK(editor.fixture.backend.writes == 1);
    CHECK_FALSE(document->isDirty());
    CHECK(document->path() == editor.path);
    CHECK(saveAsAsked.isEmpty());

    // Ctrl+Shift+S -- file.saveAs, through its twin, which is handed the current path.
    CHECK(press(editor.view, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier));
    REQUIRE(saveAsAsked.size() == 1);
    CHECK(text(saveAsAsked.front()) == text(editor.path));
    CHECK(text(document->path()) == text(QFileInfo(saveAsTarget).absoluteFilePath()));
    CHECK(QFileInfo::exists(saveAsTarget));

    // All five registered -- the `--list-actions` half of A-71 is run against the product binary.
    for (const ActionId id : {ActionId::FileSave, ActionId::FileSaveAs, ActionId::EditUndo, ActionId::EditRedo,
                              ActionId::PageRotate}) {
        CHECK(editor.fixture.actions.isRegistered(id));
    }
}

// =============================================================================================
// A-72 -- Ctrl+R in the Page Thumbnails context, through a back door.
// =============================================================================================

TEST_CASE("Ctrl+R in a widget declaring Page Thumbnails dispatches page.rotate there", "[mutationcommands]")
{
    Answers answers;
    Editor editor;

    // THROUGH A BACK DOOR, NAMED (rule 1; D3-13). No `ThumbnailsPane` exists at Stage 3 -- it is Stage 4's --
    // so a bare widget declares the context. What this does NOT cover is any pane gesture: a thumbnail
    // selection, a right-click on a thumbnail, which page a thumbnail pane would call current.
    auto* thumbnails = new QWidget(editor.window.get());
    thumbnails->setFocusPolicy(Qt::StrongFocus);
    ui::ShortcutBinder::declareContext(thumbnails, core::KeyContext::PageThumbnails);
    thumbnails->show();
    thumbnails->setFocus();
    REQUIRE(editor.window->shortcutBinder()->activeContext() == core::KeyContext::PageThumbnails);

    int asked = 0;
    core::KeyContext askedIn = core::KeyContext::Global;
    RotatePagesDialog::setAnswerForTesting([&] {
        ++asked;
        askedIn = editor.window->shortcutBinder()->activeContext();
        return RotatePagesDialog::Choice{90, false};
    });

    CHECK(press(thumbnails, Qt::Key_R, Qt::ControlModifier));
    CHECK(asked == 1);
    CHECK(askedIn == core::KeyContext::PageThumbnails);
    CHECK(rotationOf(editor.document, 0) == 90);

    // NEGATIVE: the tool finder's search box. Its context binds no Ctrl+R, so nothing dispatches.
    ui::ToolFinder* finder = editor.window->toolFinder();
    REQUIRE(finder != nullptr);
    finder->focusSearchBox();
    REQUIRE(editor.window->focusWidget() == finder->searchBox());
    press(finder->searchBox(), Qt::Key_R, Qt::ControlModifier);
    CHECK(asked == 1);
    CHECK(rotationOf(editor.document, 0) == 90);
}

// =============================================================================================
// A-73 -- the Rotate Pages dialog constructs commands and nothing else.
// =============================================================================================

TEST_CASE("the Rotate Pages dialog answers a choice and the plan is one command or one group", "[mutationcommands]")
{
    Answers answers;
    Fixture fixture;
    fixture.backend.pagesPerDocument = 4;
    const QString path = fixture.writePdf(QStringLiteral("plan.pdf"));
    const core::Result<core::DocumentHandle> opened = fixture.backend.open(path, {});
    REQUIRE(opened);
    core::Document model;
    REQUIRE(model.hydrate(fixture.backend, opened.value()));
    core::CommandJournal journal(std::move(model), fixture.backend.mutationSink(opened.value()));

    // THE WIDGET, driven. It holds radio buttons and nothing else: its constructor takes no document and no
    // journal -- only the zero-based page it names -- which is what "constructs commands and nothing else"
    // means in a type.
    RotatePagesDialog dialog(2);

    // "Current page" says WHICH page, one-based (M-01's walk: a person could not tell which page was current).
    // NOT COVERED here (convention 19): that the product passes the page the viewer counts as current --
    // `MainWindow` reads it through the same `rotationPageOf` it rotates, and an installed answer never
    // builds this widget, so only a person meeting the real dialog sees the two agree.
    CHECK(dialog.currentPageButton()->text()
          == ui::catalog::translate(ui::catalog::key::RotateCurrentPage).arg(3));
    CHECK(dialog.currentPageButton()->text().contains(QLatin1Char('3')));
    CHECK_FALSE(dialog.currentPageButton()->text().contains(QLatin1String("%1")));

    dialog.currentPageButton()->setChecked(true);
    dialog.clockwiseButton()->setChecked(true);
    CHECK(dialog.choice() == RotatePagesDialog::Choice{90, false});

    // Current page, 90 degrees clockwise: ONE command, ungrouped, ONE step.
    ui::RotationPlan current = ui::planRotation(dialog.choice(), 2, 4);
    CHECK_FALSE(current.group.has_value());
    REQUIRE(current.commands.size() == 1);
    REQUIRE(journal.run(std::move(current.commands.front())));
    CHECK(journal.historySize() == 1);
    CHECK(journal.undoStep().size() == 1);
    CHECK(static_cast<int>(journal.document().pages().pages().at(2).rotation()) == 90);

    // All pages, 180 degrees: ONE group of n commands, ONE undo step.
    dialog.allPagesButton()->setChecked(true);
    dialog.halfTurnButton()->setChecked(true);
    CHECK(dialog.choice() == RotatePagesDialog::Choice{180, true});
    ui::RotationPlan all = ui::planRotation(dialog.choice(), 2, 4);
    REQUIRE(all.group.has_value());
    CHECK(all.group->key == QLatin1String("command.page.rotateAll"));
    REQUIRE(all.commands.size() == 4);
    journal.beginGroup(*all.group);
    for (auto& command : all.commands) REQUIRE(journal.run(std::move(command)));
    REQUIRE(journal.endGroup());
    CHECK(journal.historySize() == 2);
    CHECK(journal.undoStep().size() == 4);
    REQUIRE(journal.undo());
    CHECK(static_cast<int>(journal.document().pages().pages().at(0).rotation()) == 0);
    CHECK(static_cast<int>(journal.document().pages().pages().at(2).rotation()) == 90);

    // Counter-clockwise is 270 clockwise.
    dialog.counterClockwiseButton()->setChecked(true);
    CHECK(dialog.choice().degrees == 270);

    // Cancel, through the product's command: nothing is run.
    Editor editor;
    RotatePagesDialog::setAnswerForTesting([] { return std::optional<RotatePagesDialog::Choice>{}; });
    REQUIRE(editor.fixture.actions.dispatch(ActionId::PageRotate, core::KeyContext::Viewer));
    CHECK(editor.document->journal()->historySize() == 0);
    CHECK_FALSE(editor.document->isDirty());
}

// =============================================================================================
// A-75 -- geometry first, then pixels.
// =============================================================================================

TEST_CASE("a rotated page is laid out landscape before its pixels change and undo restores both", "[mutationcommands]")
{
    Answers answers;
    Editor editor;
    ui::PageView* view = editor.view;

    // A FIXED ZOOM, so the page's size in pixels depends on its rotation alone -- a fit mode would rescale
    // every page when the widest one changed and blur what the rectangle comparison says.
    view->setZoom(core::render::Zoom::percent(50));
    view->goToPage(0);
    driveLayout(editor.window.get());
    editor.fixture.pumpTiles();

    const auto slot = [view](int page) {
        const core::render::PageSlot* found = view->layoutEngine().layout().slotForPage(page);
        REQUIRE(found != nullptr);
        return *found;
    };
    const auto grabbed = [&] {
        editor.fixture.pumpTiles();
        QCoreApplication::processEvents();
        return view->viewport()->grab().toImage();
    };
    // THE DELTA A PAGE-1 REQUEST CARRIED since the last reset, waited for rather than read after one pump.
    const auto requestedDelta = [&] {
        QElapsedTimer timer;
        timer.start();
        while (uitest::fakeFirstPageRotationDelta.load() == -1 && timer.elapsed() < 4000)
            editor.fixture.pumpTiles(200);
        return uitest::fakeFirstPageRotationDelta.load();
    };
    const auto isFakeTile = [](QRgb pixel) {
        return qRed(pixel) == uitest::kFakeTileRed && qGreen(pixel) == uitest::kFakeTileGreen
               && qBlue(pixel) == uitest::kFakeTileBlue;
    };

    const core::render::PageSlot before0 = slot(0);
    const core::render::PageSlot before1 = slot(1);
    REQUIRE(before0.rect.size.height > before0.rect.size.width);          // portrait
    const double gap = before1.rect.top() - before0.rect.bottom();

    // THE PROBE POINT: inside the landscape page that is coming and outside the portrait page that is there.
    // Same top; the landscape page is wider, so a point just inside its right edge is beyond the portrait's.
    const double landscapeWidth = before0.rect.size.height;
    const double centre = before0.rect.left() + before0.rect.size.width / 2.0;
    const int probeX = static_cast<int>(centre + landscapeWidth / 2.0 - 6.0) - view->horizontalScrollBar()->value();
    const int probeY = static_cast<int>(before0.rect.top() + 6.0) - view->verticalScrollBar()->value();
    REQUIRE(probeX > static_cast<int>(before0.rect.right()) - view->horizontalScrollBar()->value());

    const QImage portrait = grabbed();
    REQUIRE(probeX < portrait.width());
    CHECK_FALSE(isFakeTile(portrait.pixel(probeX, probeY)));

    // RESET BEFORE THE COMMAND, not after it: the command's own refresh requests the tiles, and a worker that
    // took one at once had already recorded its delta when a reset placed after the command wiped it (measured
    // under a loaded parallel run, 2026-09-15).
    uitest::fakeFirstPageRotationDelta = -1;
    editor.rotateCurrentPage(90);

    // FIRST the geometry -- read straight after the command, before a single tile is pumped.
    const core::render::PageSlot after0 = slot(0);
    const core::render::PageSlot after1 = slot(1);
    CHECK(after0.rect.size.width > after0.rect.size.height);               // landscape
    CHECK(after0.rect.size.width == before0.rect.size.height);
    CHECK(after1.rect.top() != before1.rect.top());                        // the next page moved…
    CHECK(after1.rect.top() - after0.rect.bottom() == gap);                // …by exactly the difference
    CHECK(view->revision() == editor.document->revision());

    // THEN the pixels -- and the page's tiles were asked for with the unsaved rotation as their delta, the
    // engine's own reading untouched (D3-15). The fake paints one blue whatever the delta, so the delta is read
    // from what it was asked, not from the image.
    const QImage landscape = grabbed();
    CHECK(isFakeTile(landscape.pixel(probeX, probeY)));
    CHECK(requestedDelta() == 90);
    CHECK(static_cast<int>(editor.fixture.backend.pages(editor.document->handle())->pages().front().rotation()) == 0);

    // Undo restores both.
    uitest::fakeFirstPageRotationDelta = -1;
    REQUIRE(editor.fixture.actions.dispatch(ActionId::EditUndo, core::KeyContext::Viewer));
    CHECK(slot(0).rect.size.height == before0.rect.size.height);
    CHECK(slot(1).rect.top() == before1.rect.top());
    const QImage restored = grabbed();
    CHECK_FALSE(isFakeTile(restored.pixel(probeX, probeY)));
    CHECK(requestedDelta() == 0);
}

// =============================================================================================
// A-76 -- dirty is visible, and so is why a command is off.
// =============================================================================================

TEST_CASE("the dirty marker follows the journal and disabled commands say why in both locales", "[mutationcommands]")
{
    Answers answers;
    Editor editor;
    ui::DocumentTabBar* tabs = editor.window->documentTabs();
    const QString name = QStringLiteral("edit.pdf");
    const QString marked = ui::catalog::translate(QStringLiteral("shell.document.dirtyTitle")).arg(name);
    REQUIRE(marked != name);

    CHECK(text(tabs->tabText(0)) == text(name));

    // Nothing to undo and nothing to save: disabled, each with its own reason.
    CHECK_FALSE(editor.fixture.actions.isEnabled(ActionId::EditUndo));
    CHECK_FALSE(editor.fixture.actions.isEnabled(ActionId::FileSave));
    CHECK(text(editor.tooltip(ActionId::EditUndo))
          == text(ui::catalog::translate(QStringLiteral("shell.tooltip.nothingToUndo"))
                      .arg(ui::catalog::translateAction(ActionId::EditUndo))));
    CHECK(text(editor.tooltip(ActionId::FileSave))
          == text(ui::catalog::translate(QStringLiteral("shell.tooltip.nothingToSave"))
                      .arg(ui::catalog::translateAction(ActionId::FileSave))));

    editor.rotateCurrentPage(90);
    CHECK(text(tabs->tabText(0)) == text(marked));
    CHECK(editor.window->windowTitle().contains(marked));
    CHECK(editor.fixture.actions.isEnabled(ActionId::FileSave));

    // Undo's tooltip names the step, from its `command.*` entry.
    CHECK(text(editor.tooltip(ActionId::EditUndo)) == "Undo: Rotate page 1 by 90 degrees");

    // Saved: unmarked.
    REQUIRE(editor.fixture.actions.dispatch(ActionId::FileSave, core::KeyContext::Global));
    CHECK(text(tabs->tabText(0)) == text(name));
    CHECK_FALSE(editor.window->windowTitle().contains(marked));

    // Dirty again, then undone TO the saved position: unmarked again.
    editor.rotateCurrentPage(90);
    CHECK(text(tabs->tabText(0)) == text(marked));
    REQUIRE(editor.fixture.actions.dispatch(ActionId::EditUndo, core::KeyContext::Viewer));
    CHECK(text(tabs->tabText(0)) == text(name));
    CHECK_FALSE(editor.document->isDirty());
    CHECK(text(editor.tooltip(ActionId::FileSave))
          == text(ui::catalog::translate(QStringLiteral("shell.tooltip.nothingToSave"))
                      .arg(ui::catalog::translateAction(ActionId::FileSave))));

    // zh-TW: the same step's name, in Chinese, from the same key.
    QTranslator chinese;
    REQUIRE(chinese.load(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("i18n/rawpage_zh_TW.qm"))));
    REQUIRE(QCoreApplication::installTranslator(&chinese));
    const QString undoTip = editor.tooltip(ActionId::EditUndo);
    const QString disabledSave = editor.tooltip(ActionId::FileSave);
    QCoreApplication::removeTranslator(&chinese);

    CHECK(undoTip.contains(QStringLiteral("將第 1 頁旋轉 90 度")));
    CHECK(undoTip.startsWith(QStringLiteral("復原")));
    CHECK(disabledSave.contains(QStringLiteral("沒有未儲存的變更")));
}

// =============================================================================================
// A-77 -- a repaired document's Save goes to Save-As.
// =============================================================================================

TEST_CASE("saving a repaired document explains first and then asks for a new file", "[mutationcommands]")
{
    Answers answers;
    Editor editor([](Fixture& fixture) {
        fixture.backend.repairs.add(core::RepairEntry{core::RepairKind::ObjectOffsetWrong,
                                                      core::RepairSeverity::Recovered, 12, 4096, 0});
    });
    REQUIRE(editor.document->journal()->document().repairReport().requiresSaveAs());
    QFile original(editor.path);
    REQUIRE(original.open(QIODevice::ReadOnly));
    const QByteArray originalBytes = original.readAll();
    original.close();

    editor.rotateCurrentPage(90);

    const QString target = QDir(editor.fixture.directory.path()).filePath(QStringLiteral("repaired-copy.pdf"));
    bool explainedFirst = false;
    int writesWhenAsked = -1;
    ui::MainWindow::setSaveAsAnswerForTesting([&](const QString&) -> std::optional<QString> {
        explainedFirst = editor.window->messageBar()->isShowing(MessageBar::Kind::SaveAsRequired);
        writesWhenAsked = editor.fixture.backend.writes;
        return target;
    });

    CHECK(press(editor.view, Qt::Key_S, Qt::ControlModifier));

    CHECK(explainedFirst);                     // the explanation, THEN the Save-As twin
    CHECK(writesWhenAsked == 0);               // no in-place save was attempted before it
    CHECK(editor.window->messageBar()->textFor(MessageBar::Kind::SaveAsRequired).isEmpty());   // cleared by the save
    CHECK(text(editor.document->path()) == text(QFileInfo(target).absoluteFilePath()));
    CHECK_FALSE(editor.document->isDirty());

    QFile after(editor.path);
    REQUIRE(after.open(QIODevice::ReadOnly));
    CHECK(after.readAll() == originalBytes);   // the repaired original is untouched

    // THE SETTING HALF is the next case (Stage 3 T12): `documents/warnOnRepairedSave` off.
}

TEST_CASE("with warnOnRepairedSave off a repaired document still goes to Save As with no explanation", "[mutationcommands]")
{
    // A-77's CONTROL (guide rule 5): a setting that suppresses a warning must not suppress what the warning is
    // about. D3-6 -- it skips the explanation and can never turn the forcing off.
    Answers answers;
    Editor editor([](Fixture& fixture) {
        fixture.backend.repairs.add(core::RepairEntry{core::RepairKind::ObjectOffsetWrong,
                                                      core::RepairSeverity::Recovered, 12, 4096, 0});
        fixture.settings.set(core::keys::documents::warnOnRepairedSave, false);
    });
    REQUIRE(editor.document->journal()->document().repairReport().requiresSaveAs());
    const QByteArray originalBytes = [&] {
        QFile file(editor.path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        return file.readAll();
    }();
    editor.rotateCurrentPage(90);

    const QString target = QDir(editor.fixture.directory.path()).filePath(QStringLiteral("repaired-quiet.pdf"));
    int asked = 0;
    bool explained = true;
    bool conditionKept = true;
    int writesWhenAsked = -1;
    ui::MainWindow::setSaveAsAnswerForTesting([&](const QString&) -> std::optional<QString> {
        ++asked;
        explained = editor.window->messageBar()->isShowing(MessageBar::Kind::SaveAsRequired);
        conditionKept = editor.document->conditions().count(static_cast<int>(MessageBar::Kind::SaveAsRequired)) != 0;
        writesWhenAsked = editor.fixture.backend.writes;
        return target;
    });

    CHECK(press(editor.view, Qt::Key_S, Qt::ControlModifier));

    CHECK(asked == 1);                         // STILL Save As ...
    CHECK_FALSE(explained);                    // ... with no explanation shown ...
    CHECK_FALSE(conditionKept);                // ... or kept for the next tab switch to show
    CHECK(writesWhenAsked == 0);               // no in-place save was attempted first
    CHECK(text(editor.document->path()) == text(QFileInfo(target).absoluteFilePath()));
    CHECK_FALSE(editor.document->isDirty());
    QFile after(editor.path);
    REQUIRE(after.open(QIODevice::ReadOnly));
    CHECK(after.readAll() == originalBytes);   // the repaired original is untouched

    SECTION("and it silences the repaired reason only: a file changed on disk still says why")
    {
        editor.rotateCurrentPage(90);
        editor.fixture.writePdf(QStringLiteral("repaired-quiet.pdf"), QByteArray("%PDF-1.7\n% somebody else saved this\n"));
        bool changedExplained = false;
        ui::MainWindow::setSaveAsAnswerForTesting([&](const QString&) -> std::optional<QString> {
            changedExplained = editor.window->messageBar()->isShowing(MessageBar::Kind::SaveAsRequired);
            return std::nullopt;
        });
        CHECK_FALSE(editor.window->saveDocument(editor.document));
        CHECK(changedExplained);
    }
}

// =============================================================================================
// I-15 -- a save the FILE SYSTEM refuses says why and offers Save As (D3-16, second half).
//
// The gap M-08 walked into: A-77 covers the three reasons that are decided BEFORE anything is written
// (repaired, changed on disk, untitled), and `tst_durablefiles` covers the refusals themselves below
// the UI. Neither drives a save that fails AT THE FILE SYSTEM through the shell, which is the only
// place that can say what the user is then offered.
// =============================================================================================

TEST_CASE("a save the file system refuses explains and then asks for another file", "[mutationcommands]")
{
    Answers answers;
    Editor editor;
    editor.rotateCurrentPage(90);

    // The fake replaces non-atomically and refuses on demand; what it does NOT cover is a real
    // `ReplaceFileW` (its own header). The key it returns is the one `WinDurableFiles` returns for a
    // target another process holds, measured at T7 as `ERROR_SHARING_VIOLATION`.
    editor.fixture.durableFiles().failReplaceTarget = editor.path;   // THIS file, the way read-only works

    const QString target = QDir(editor.fixture.directory.path()).filePath(QStringLiteral("elsewhere.pdf"));
    bool explainedFirst = false;
    QString explanation;
    QString proposed;
    ui::MainWindow::setSaveAsAnswerForTesting([&](const QString& path) -> std::optional<QString> {
        explainedFirst = editor.window->messageBar()->isShowing(MessageBar::Kind::SaveAsRequired);
        explanation = editor.window->messageBar()->textFor(MessageBar::Kind::SaveAsRequired);
        proposed = path;
        return target;
    });

    CHECK(press(editor.view, Qt::Key_S, Qt::ControlModifier));

    CHECK(explainedFirst);                                  // the reason, THEN the question
    CHECK(text(proposed) == text(editor.path));             // proposing the file it could not write
    CHECK(text(editor.document->path()) == text(QFileInfo(target).absoluteFilePath()));
    CHECK_FALSE(editor.document->isDirty());                // the rotation is saved, at the new path

    // THE REASON IS RENDERED, not left as a key or a `%1`: the banner carries the refusal's own words
    // and the path it was refused for. `translate` alone would have left the placeholder.
    CHECK_THAT(text(explanation), Catch::Matchers::ContainsSubstring("could not be replaced"));
    CHECK_THAT(text(explanation), Catch::Matchers::ContainsSubstring(text(QFileInfo(editor.path).fileName())));
    CHECK_THAT(text(explanation), !Catch::Matchers::ContainsSubstring("%1"));
    CHECK_THAT(text(explanation), !Catch::Matchers::ContainsSubstring("error.file."));

    SECTION("and the refusal is not silently a success: with nobody to ask, nothing is written")
    {
        // Convention 16 and flow K: the same refusal with no answer installed cancels rather than
        // blocking on a dialog -- and rather than reporting a save that did not happen.
        editor.rotateCurrentPage(90);
        editor.fixture.durableFiles().failReplaceTarget = editor.document->path();
        ui::MainWindow::setSaveAsAnswerForTesting({});
        const auto bytesOf = [](const QString& path) {
            QFile file(path);
            REQUIRE(file.open(QIODevice::ReadOnly));
            return file.readAll();
        };
        const QByteArray before = bytesOf(editor.document->path());

        CHECK_FALSE(editor.window->saveDocument(editor.document));
        CHECK(editor.document->isDirty());
        CHECK(bytesOf(editor.document->path()) == before);
    }
}

// =============================================================================================
// A-78 -- closing a dirty document asks, every way it can close.
// =============================================================================================

TEST_CASE("closing a dirty tab asks and each disposition does what it says", "[mutationcommands]")
{
    Answers answers;

    for (const CloseDisposition disposition : {CloseDisposition::Cancel, CloseDisposition::Discard, CloseDisposition::Save}) {
        INFO("disposition " << static_cast<int>(disposition));
        Editor editor;
        editor.rotateCurrentPage(90);
        REQUIRE(editor.document->recovery() != nullptr);
        const QString recoveryDirectory = editor.document->recovery()->directory();
        REQUIRE(QDir(recoveryDirectory).exists());

        QStringList asked;
        ui::UnsavedChangesDialog::setAnswerForTesting([&](const QString& name) -> std::optional<CloseDisposition> {
            asked << name;
            return disposition;
        });

        // The tab's own close button.
        emit editor.window->documentTabs()->closeRequested(0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(asked.size() == 1);

        switch (disposition) {
        case CloseDisposition::Cancel:
            CHECK(editor.window->documentCount() == 1);
            CHECK(editor.window->currentDocument()->isDirty());
            CHECK(editor.fixture.backend.writes == 0);
            break;
        case CloseDisposition::Discard:
            CHECK(editor.window->documentCount() == 0);
            CHECK_FALSE(QDir(recoveryDirectory).exists());
            CHECK(editor.fixture.backend.writes == 0);
            break;
        case CloseDisposition::Save:
            CHECK(editor.window->documentCount() == 0);
            CHECK(editor.fixture.backend.writes == 1);
            CHECK_FALSE(QDir(recoveryDirectory).exists());   // closed clean
            break;
        }
    }
}

TEST_CASE("closing a window or quitting with dirty documents asks once per document", "[mutationcommands]")
{
    Answers answers;

    SECTION("the window's close asks and Cancel keeps the window")
    {
        Editor editor;
        editor.rotateCurrentPage(90);
        int asked = 0;
        ui::UnsavedChangesDialog::setAnswerForTesting([&](const QString&) {
            ++asked;
            return std::optional<CloseDisposition>{CloseDisposition::Cancel};
        });
        CHECK_FALSE(editor.window->close());
        CHECK(asked == 1);
        CHECK(editor.window->isVisible());
        CHECK(editor.window->documentCount() == 1);

        ui::UnsavedChangesDialog::setAnswerForTesting([&](const QString&) {
            ++asked;
            return std::optional<CloseDisposition>{CloseDisposition::Discard};
        });
        CHECK(editor.window->close());
        CHECK(asked == 2);
    }

    SECTION("a torn-off window's close asks about the document it took")
    {
        Editor editor;
        editor.window->openFiles({editor.fixture.writePdf(QStringLiteral("second.pdf"))});
        REQUIRE(editor.window->documentCount() == 2);
        editor.rotateCurrentPage(90);                       // the second document, in front
        ui::MainWindow* torn = editor.window->detachDocument(1);
        REQUIRE(torn != nullptr);
        REQUIRE(torn->currentDocument()->isDirty());

        QStringList asked;
        ui::UnsavedChangesDialog::setAnswerForTesting([&](const QString& name) {
            asked << name;
            return std::optional<CloseDisposition>{CloseDisposition::Cancel};
        });
        CHECK_FALSE(torn->close());
        CHECK(asked == QStringList{QStringLiteral("second.pdf")});

        ui::UnsavedChangesDialog::setAnswerForTesting([&](const QString& name) {
            asked << name;
            return std::optional<CloseDisposition>{CloseDisposition::Save};
        });
        CHECK(torn->close());
        CHECK(asked.size() == 2);
        CHECK(editor.fixture.backend.writes == 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        CHECK(ui::MainWindow::shellWindows().size() == 1);
        CHECK(editor.window->documentCount() == 1);         // the clean first document never asked
    }

    SECTION("quit asks about each dirty document in every window")
    {
        Editor editor;
        editor.rotateCurrentPage(90);
        editor.window->openFiles({editor.fixture.writePdf(QStringLiteral("second.pdf"))});
        REQUIRE(editor.window->documentCount() == 2);
        editor.rotateCurrentPage(180);

        QStringList asked;
        ui::UnsavedChangesDialog::setAnswerForTesting([&](const QString& name) {
            asked << name;
            return std::optional<CloseDisposition>{CloseDisposition::Discard};
        });
        REQUIRE(editor.fixture.actions.dispatch(ActionId::FileQuit, core::KeyContext::Global));
        CHECK(asked.size() == 2);
        CHECK(asked.contains(QStringLiteral("edit.pdf")));
        CHECK(asked.contains(QStringLiteral("second.pdf")));
        CHECK(editor.window->documentCount() == 0);
    }

    SECTION("a headless close with no disposition is refused and the document stays")
    {
        Editor editor;
        editor.rotateCurrentPage(90);
        REQUIRE_FALSE(ui::UnsavedChangesDialog::canAsk());

        const core::Status closed = editor.window->closeDocument(0);
        REQUIRE_FALSE(closed);
        CHECK(closed.error().code == core::ErrorCode::Unavailable);
        CHECK(closed.error().messageKey == QLatin1String("error.close.needsDisposition"));
        CHECK(editor.window->documentCount() == 1);
        CHECK(editor.document->isDirty());

        // And an explicit disposition needs nobody.
        CHECK(editor.window->closeDocument(0, CloseDisposition::Discard));
        CHECK(editor.window->documentCount() == 0);
    }
}

// =============================================================================================
// A-79 -- a document this build cannot edit says so.
// =============================================================================================

TEST_CASE("an encrypted document disables rotate with the encryption reason", "[mutationcommands]")
{
    Answers answers;
    Editor editor([](Fixture& fixture) { fixture.backend.editableReading = core::CosReading::Encrypted; });

    int asked = 0;
    RotatePagesDialog::setAnswerForTesting([&asked] {
        ++asked;
        return RotatePagesDialog::Choice{90, false};
    });

    CHECK_FALSE(editor.document->isEditable());
    CHECK_FALSE(editor.fixture.actions.isEnabled(ActionId::PageRotate));
    CHECK(text(editor.tooltip(ActionId::PageRotate))
          == text(ui::catalog::translateError(QStringLiteral("error.edit.encrypted"), QString())));
    CHECK(editor.tooltip(ActionId::PageRotate) != QStringLiteral("error.edit.encrypted"));   // resolved, not the key

    press(editor.view, Qt::Key_R, Qt::ControlModifier);
    CHECK(asked == 0);
    CHECK(rotationOf(editor.document, 0) == 0);
    CHECK(editor.document->recovery() == nullptr);
}

// =============================================================================================
// A-80 -- the three new banners.
// =============================================================================================

TEST_CASE("a degraded journal a failed save and a changed file each raise their own banner", "[mutationcommands]")
{
    Answers answers;

    SECTION("recovery unavailable, once")
    {
        // A-66's first half, through the window: the recovery root sits under a FILE, so no directory can
        // be made and the journal degrades to memory.
        Editor editor([](Fixture& fixture) {
            const QString blocker = QDir(fixture.directory.path()).filePath(QStringLiteral("not-a-directory"));
            QFile file(blocker);
            REQUIRE(file.open(QIODevice::WriteOnly));
            file.close();
            fixture.recoveryRoot = QDir(blocker).filePath(QStringLiteral("recovery"));
        });
        REQUIRE(editor.document->recovery() != nullptr);
        REQUIRE_FALSE(editor.document->recovery()->isRecoverable());

        int reported = 0;
        QObject::connect(editor.document, &ui::DocumentWindow::recoveryUnavailable, [&reported] { ++reported; });

        editor.rotateCurrentPage(90);
        CHECK(reported == 1);
        CHECK(editor.window->messageBar()->isShowing(MessageBar::Kind::RecoveryUnavailable));

        // Dismissed, then another command: it does NOT come back -- once, not once per command.
        editor.window->messageBar()->dismiss(MessageBar::Kind::RecoveryUnavailable);
        editor.rotateCurrentPage(90);
        editor.rotateCurrentPage(90);
        CHECK(reported == 1);
        CHECK_FALSE(editor.window->messageBar()->isShowing(MessageBar::Kind::RecoveryUnavailable));
        CHECK(editor.window->messageBar()->messageCount() == 0);
    }

    SECTION("a failed save no other path could answer")
    {
        // `Corrupted` -- the bytes we composed are wrong, so Save-As is NOT offered and the banner is the
        // refusal itself (I-15 draws that line; `SaveCoordinator.h` argues it). A save refused by the FILE
        // SYSTEM takes the other branch, and its own case is above.
        Editor editor([](Fixture& fixture) { fixture.backend.failVerify = true; });
        editor.rotateCurrentPage(90);
        const core::Status saved = editor.window->saveDocument(editor.document);
        REQUIRE_FALSE(saved);
        CHECK(saved.error().code == core::ErrorCode::Corrupted);
        CHECK_FALSE(editor.window->messageBar()->isShowing(MessageBar::Kind::SaveAsRequired));
        CHECK(editor.window->messageBar()->isShowing(MessageBar::Kind::SaveFailed));
        CHECK(editor.window->messageBar()->messageCount() == 1);
        const QString shown = editor.window->messageBar()->textFor(MessageBar::Kind::SaveFailed);
        CHECK(shown.contains(QStringLiteral("edit.pdf")));
        CHECK_FALSE(shown.contains(QStringLiteral("error.save")));    // resolved, not a key
        CHECK(editor.document->isDirty());
        CHECK(editor.window->documentCount() == 1);
    }

    SECTION("a file changed on disk since it was opened")
    {
        Editor editor;
        editor.rotateCurrentPage(90);
        editor.fixture.writePdf(QStringLiteral("edit.pdf"), QByteArray("%PDF-1.7\n% somebody else saved this\n"));

        ui::MainWindow::setSaveAsAnswerForTesting([](const QString&) { return std::optional<QString>{}; });
        const core::Status saved = editor.window->saveDocument(editor.document);
        REQUIRE_FALSE(saved);
        CHECK(saved.error().code == core::ErrorCode::Cancelled);
        CHECK(editor.window->messageBar()->isShowing(MessageBar::Kind::SaveAsRequired));
        CHECK(editor.window->messageBar()->messageCount() == 1);
        CHECK(text(editor.window->messageBar()->textFor(MessageBar::Kind::SaveAsRequired))
              .find(text(ui::catalog::translate(QStringLiteral("error.save.fileChanged")))) != std::string::npos);
        CHECK(editor.fixture.backend.writes == 0);
        CHECK(editor.document->isDirty());
    }
}
