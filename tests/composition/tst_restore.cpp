// tst_restore -- guide cases A-75 … A-79 (`Stage4-ViewingSurface-Testing-Guide.md` §5.15): quit, relaunch,
// and get the session back, THROUGH THE PRODUCT'S OWN COMPOSITION. Stage 4 T13, decision D4-9.
//
// A relaunch cannot be faked with a second `SessionState` over the same data, which is what a headless case
// would have to do: the thing under test is `app::SessionShell` reading a real `ui::MainWindow` into the model
// and building a real one back out of it, with the real engine opening the real files. So a "relaunch" here is
// a second `app::CompositionRoot` and a second `MainWindow` over the same scratch directory -- the first pair
// destroyed before the second is built, exactly as a process would.
//
// WHAT THESE CASES DO NOT COVER (convention 19), stated here and again at the assertions that need it:
//
//   * **`app/main.cpp` itself is not run.** That it calls `RecoveryPrompt::checkAndOffer`, then `openFiles`,
//     then `SessionShell::restore` -- D4-9's ordering -- is read in its source and walked in M-05. Each case
//     below makes those calls in that order itself, which proves the ORDER WORKS and not that main.cpp has it.
//   * **No process is killed and none quits.** `captureNow()` is what `aboutToQuit` calls; A-77's crashed
//     journal is planted with `rawpage_killprobe`, as Stage 3's A-74 does.
//   * **Geometry is offscreen geometry.** The clamp, the monitor fingerprint and a desktop that changed shape
//     overnight are `tst_fingerprint`'s arithmetic (headless) and manual case **M-09**'s hardware. What is
//     asserted here is that a remembered rectangle is APPLIED and that `rememberWindowLayout` off leaves it
//     alone -- which is the part the shell owns.
//   * **The dialogs are answered by their twins** (convention 16): under the offscreen platform a modal is
//     shown to nobody and never returns (Stage 1's I-23).

#include <catch2/catch_test_macros.hpp>

#include "app/Application.h"
#include "app/CompositionRoot.h"
#include "app/RecoveryPrompt.h"

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/doc/Document.h"
#include "core/doc/DocumentId.h"
#include "core/doc/PageTree.h"
#include "core/settings/SessionState.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"

#include "ui/dialogs/RecoveryDialog.h"
#include "ui/dialogs/UnsavedChangesDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/SidebarDock.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SplitViewContainer.h"
#include "ui/viewer/ViewportController.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

using namespace rawpage;

namespace {

QString corpusFile(const char* relative)
{
    return QString::fromStdString((std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative).string());
}

// A LAUNCH. One composition root, one shown window, and the session shell over the scratch file -- the four
// objects `app/main.cpp` has on its stack around `exec()`, in the same order and with the same arguments.
//
// **EVERY PATH IT USES IS THE SCRATCH DIRECTORY'S**, and that is guide case A-79 rather than tidiness: a suite
// that used `Settings::defaultFilePath()` or `SessionState::defaultFilePath()` would write, and on its next run
// REOPEN AND REPLACE, the documents and the preferences of whoever ran it. It is the trap Stage 3 T11 recorded
// for the recovery root, with a second subject, and `CLAUDE.md` → *Traps* carries it.
struct Launch {
    std::unique_ptr<app::CompositionRoot> root;
    std::unique_ptr<ui::MainWindow>       window;
    std::unique_ptr<app::SessionShell>    session;

    Launch(const QString& scratch, const QString& recoveryRoot = QString())
    {
        app::CompositionRoot::Options options;
        options.settingsPath = QDir(scratch).filePath(QStringLiteral("settings.ini"));
        options.sessionPath  = QDir(scratch).filePath(QStringLiteral("session.json"));
        options.recoveryRoot =
            recoveryRoot.isEmpty() ? QDir(scratch).filePath(QStringLiteral("recovery")) : recoveryRoot;
        root = std::make_unique<app::CompositionRoot>(std::move(options));

        window = std::make_unique<ui::MainWindow>(root->services());
        window->resize(1000, 800);
        window->show();
        QCoreApplication::processEvents();

        session = std::make_unique<app::SessionShell>(root->sessionState(), root->settings(),
                                                      root->platform().displayTopology.get());
        session->install(*QCoreApplication::instance());
    }

    // The way a launch ends when the person is done: capture (what `aboutToQuit` does), then let go of the
    // window and the services, in that order -- the window's tabs close handles the root's backend owns.
    void quit()
    {
        session->captureNow();
        shutDown();
    }

    // The way a launch ends when nothing should be written.
    void shutDown()
    {
        window.reset();

        // A tab's destructor posts DeferredDelete, and the tab's destructor closes its handle on whatever
        // backend is at that address (`CLAUDE.md` → *Traps*, S3 T10). Flushing them here is what stops the
        // NEXT launch's backend from being the one they find.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        session.reset();
        root.reset();
    }

    ~Launch() { if (window || root) shutDown(); }
};

// Three corpus copies in the scratch directory, so nothing here ever edits the generated corpus -- which is
// checked byte-for-byte and is data, not a fixture (`CLAUDE.md` → *Commands*).
std::vector<QString> copyCorpus(const QString& scratch, std::initializer_list<const char*> relatives)
{
    std::vector<QString> copies;
    for (const char* relative : relatives) {
        const QString name = QFileInfo(QString::fromLatin1(relative)).fileName();
        const QString target = QDir(scratch).filePath(name);
        REQUIRE(QFile::copy(corpusFile(relative), target));
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        copies.push_back(QFileInfo(target).absoluteFilePath());
    }
    return copies;
}

std::vector<QString> openPaths(const ui::MainWindow& window)
{
    std::vector<QString> paths;
    for (int at = 0; at < window.documentCount(); ++at) {
        const ui::DocumentWindow* document = window.documentAt(at);
        paths.push_back(document != nullptr ? QFileInfo(document->path()).absoluteFilePath() : QString());
    }
    return paths;
}

int rotationOf(const ui::DocumentWindow& document, int page)
{
    return static_cast<int>(
        document.journal()->document().pages().pages().at(static_cast<std::size_t>(page)).rotation());
}

struct TwinReset {
    ~TwinReset()
    {
        ui::RecoveryDialog::setAnswerForTesting({});
        ui::UnsavedChangesDialog::setAnswerForTesting({});
    }
};

} // namespace

// =====================================================================================================
// A-75 -- the documents, the order, the active tab, the split, the pane and the view state
// =====================================================================================================

TEST_CASE("a quit and a relaunch bring back the documents the order and the layout", "[restore]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const std::vector<QString> files =
        copyCorpus(scratch.path(), {"ordinary/text-a4.pdf", "ordinary/outline-labels.pdf", "ordinary/mixed-page-sizes.pdf"});

    // -------------------------------------------------------------------------------------------------
    // 1. A SESSION: three documents, the second split, the third scrolled and zoomed, a pane open.
    // -------------------------------------------------------------------------------------------------
    {
        Launch first(scratch.path());
        for (const QString& file : files) first.window->openFiles({file});
        REQUIRE(first.window->documentCount() == 3);

        ui::DocumentWindow* second = first.window->documentAt(1);
        REQUIRE(second != nullptr);
        REQUIRE(second->split() != nullptr);
        second->split()->setMode(ui::SplitViewContainer::Mode::Vertical);
        REQUIRE(second->split()->views().size() == 2);

        ui::DocumentWindow* third = first.window->documentAt(2);
        REQUIRE(third != nullptr);
        REQUIRE(third->view() != nullptr);
        third->view()->setLayoutMode(core::render::LayoutMode::TwoUpContinuous);
        third->view()->setZoom(core::render::Zoom::percent(150));
        third->view()->goToPage(2);
        QCoreApplication::processEvents();

        REQUIRE(first.window->sidebar() != nullptr);
        first.window->sidebar()->show();
        first.window->sidebar()->showPane(core::ActionId::PaneThumbnails);
        first.window->activateDocument(1);
        QCoreApplication::processEvents();

        first.quit();
    }

    // The file exists and is this suite's, never the user's -- A-79's first half; its second is its own case.
    REQUIRE(QFileInfo::exists(QDir(scratch.path()).filePath(QStringLiteral("session.json"))));

    // -------------------------------------------------------------------------------------------------
    // 2. THE RELAUNCH, in main.cpp's order: recovery (nothing to offer), the command line (nothing), then
    //    the session.
    // -------------------------------------------------------------------------------------------------
    Launch again(scratch.path());
    const app::SessionShell::Restored restored = again.session->restore(*again.window, again.root->services());

    CHECK_FALSE(restored.disabled);
    CHECK(restored.windows == 1);
    CHECK(restored.documents == 3);
    CHECK(restored.skipped.empty());
    CHECK(restored.droppedPaths.empty());

    // THE ORDER, and it is asserted as the whole list rather than one document at a time: a restore that
    // brought back the right three in the wrong order would satisfy any per-document check.
    REQUIRE(again.window->documentCount() == 3);
    CHECK(openPaths(*again.window) == files);

    CHECK(again.window->currentDocument() == again.window->documentAt(1));

    const ui::DocumentWindow* second = again.window->documentAt(1);
    REQUIRE(second != nullptr);
    REQUIRE(second->split() != nullptr);
    CHECK(second->split()->mode() == ui::SplitViewContainer::Mode::Vertical);
    CHECK(second->split()->views().size() == 2);

    const ui::DocumentWindow* third = again.window->documentAt(2);
    REQUIRE(third != nullptr);
    REQUIRE(third->view() != nullptr);
    CHECK(third->view()->controller().layoutMode() == core::render::LayoutMode::TwoUpContinuous);
    CHECK(third->view()->controller().zoom().factor == core::render::Zoom::percent(150).factor);
    CHECK(third->view()->controller().currentPage() == 2);

    REQUIRE(again.window->sidebar() != nullptr);
    CHECK(again.window->sidebar()->isVisible());
    REQUIRE(again.window->sidebar()->currentPane().has_value());
    CHECK(*again.window->sidebar()->currentPane() == core::ActionId::PaneThumbnails);

    std::cout << "A-75: " << restored.documents << " document(s) in " << restored.windows
              << " window(s) restored; active tab " << 1 << ", split Vertical, Thumbnails pane open\n";
}

// =====================================================================================================
// A-76 -- a document closed with *Don't Save* comes back CLEAN. **S1 if it does not.**
// =====================================================================================================

TEST_CASE("a document closed with dont save comes back clean and unrotated", "[restore]")
{
    TwinReset twins;
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const std::vector<QString> files = copyCorpus(scratch.path(), {"ordinary/text-a4.pdf"});

    const QByteArray before = [&] {
        QFile file(files.front());
        REQUIRE(file.open(QIODevice::ReadOnly));
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
    }();

    QString recoveryDirectory;
    {
        Launch first(scratch.path());
        first.window->openFiles({files.front()});
        REQUIRE(first.window->documentCount() == 1);

        ui::DocumentWindow* document = first.window->currentDocument();
        REQUIRE(document != nullptr);
        REQUIRE(document->isEditable());
        REQUIRE(first.window->rotatePages(document, {90, false}));
        CHECK(document->isDirty());
        CHECK(rotationOf(*document, 0) == 90);
        REQUIRE(document->recovery() != nullptr);
        recoveryDirectory = document->recovery()->directory();
        REQUIRE(QDir(recoveryDirectory).exists());

        // THE CLOSE IS THE WINDOW'S, not the tab's, and that is the shape of this case: quitting is what
        // asks, and the close protocol REMOVES the tab before `aboutToQuit` would ever see it. A session
        // captured only at quit would forget a document the person had open and merely declined to save --
        // which is why `SessionShell` stashes on `QEvent::Close` instead (see its header).
        ui::UnsavedChangesDialog::setAnswerForTesting(
            [](const QString&) { return std::optional<ui::CloseDisposition>(ui::CloseDisposition::Discard); });
        REQUIRE(first.window->close());
        QCoreApplication::processEvents();

        first.quit();
    }

    // D3-14: the discard took the recovery directory with it, so nothing offers the edit back either.
    CHECK_FALSE(QDir(recoveryDirectory).exists());

    Launch again(scratch.path());
    const app::SessionShell::Restored restored = again.session->restore(*again.window, again.root->services());
    CHECK(restored.documents == 1);

    REQUIRE(again.window->documentCount() == 1);
    ui::DocumentWindow* back = again.window->currentDocument();
    REQUIRE(back != nullptr);
    CHECK(QFileInfo(back->path()).absoluteFilePath() == files.front());

    // THE THREE THINGS A RESURRECTED EDIT WOULD SHOW, asserted separately because each fails differently:
    // the model's rotation, the dirty flag, and the bytes on disk.
    CHECK(rotationOf(*back, 0) == 0);
    CHECK_FALSE(back->isDirty());
    REQUIRE(back->journal() != nullptr);
    CHECK(back->journal()->historySize() == 0);

    QFile file(files.front());
    REQUIRE(file.open(QIODevice::ReadOnly));
    CHECK(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex() == before);

    std::cout << "A-76: the discarded rotation did not come back; file hash unchanged\n";
}

// =====================================================================================================
// A-77 -- restore runs after recovery, and never opens the same document twice
// =====================================================================================================

TEST_CASE("restore skips a path recovery already opened", "[restore]")
{
    TwinReset twins;
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const std::vector<QString> files =
        copyCorpus(scratch.path(), {"ordinary/text-a4.pdf", "ordinary/outline-labels.pdf"});
    const QString recoveryRoot = QDir(scratch.path()).filePath(QStringLiteral("recovery"));

    // -------------------------------------------------------------------------------------------------
    // 1. A SESSION OF TWO WINDOWS, one document each, captured and finished cleanly.
    //
    //    TWO WINDOWS, AND THAT IS THE WHOLE DESIGN OF THIS CASE rather than thoroughness. In ONE window
    //    the skip is guarded twice: `MainWindow::openFiles` already refuses a path that window holds and
    //    focuses the existing tab instead (Stage 2 T13), so a restore that had forgotten to skip would
    //    still not produce two tabs -- and the case would go green for the wrong reason. That guard is
    //    PER WINDOW. A session whose second window names a document recovery has opened in the FIRST is
    //    the arrangement where nothing below `SessionShell` is watching, and it is where the S1 lives:
    //    a second `DocumentHandle` and a second `CommandJournal` over one file are two Saves racing for
    //    the same bytes (D4-13's reasoning, arriving through the session instead of through a split).
    // -------------------------------------------------------------------------------------------------
    {
        Launch first(scratch.path(), recoveryRoot);
        first.window->openFiles({files.front()});
        REQUIRE(first.window->documentCount() == 1);

        auto* second = new ui::MainWindow(first.root->services());
        second->setAttribute(Qt::WA_DeleteOnClose);
        second->resize(1000, 800);
        second->show();
        second->openFiles({files.back()});
        REQUIRE(second->documentCount() == 1);
        QCoreApplication::processEvents();

        first.session->captureNow();
        REQUIRE(first.root->sessionState().session().windows.size() == 2);

        second->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        first.shutDown();
    }
    REQUIRE(core::RecoveryJournal::discover(recoveryRoot).empty());

    // -------------------------------------------------------------------------------------------------
    // 2. A CRASHED SESSION'S JOURNAL for the SECOND WINDOW'S document, written by the kill probe and
    //    killed with TerminateProcess -- Stage 3 A-74's instrument, used here for its leavings rather
    //    than for its replay.
    // -------------------------------------------------------------------------------------------------
    {
        QProcess probe;
        probe.start(QStringLiteral(RAWPAGE_KILLPROBE_PATH),
                    {QStringLiteral("commands"), files.back(), recoveryRoot, QStringLiteral("2")});
        REQUIRE(probe.waitForStarted(10000));
        int acknowledged = 0;
        QByteArray seen;
        QElapsedTimer timer;
        timer.start();
        while (acknowledged < 2 && timer.elapsed() < 20000) {
            if (!probe.waitForReadyRead(10000)) break;
            seen += probe.readAllStandardOutput();
            for (const QByteArray& line : seen.split('\n'))
                if (line.trimmed().startsWith("ack "))
                    acknowledged = std::max(acknowledged, line.trimmed().mid(4).toInt());
        }
        REQUIRE(acknowledged >= 2);
        probe.kill();
        REQUIRE(probe.waitForFinished(10000));
    }
    REQUIRE(core::RecoveryJournal::discover(recoveryRoot).size() == 1);

    // -------------------------------------------------------------------------------------------------
    // 3. THE RELAUNCH, IN MAIN.CPP'S ORDER (D4-9). Recovery first -- it opens the second window's
    //    document into the FIRST window, which is the only window there is at that point -- and then
    //    restore, which must find that path open in a window that is not the one the session put it in,
    //    and leave it alone.
    // -------------------------------------------------------------------------------------------------
    Launch again(scratch.path(), recoveryRoot);
    ui::RecoveryDialog::setAnswerForTesting([](const std::vector<ui::RecoveryDialog::Offer>& offers) {
        return std::optional<std::vector<ui::RecoveryDisposition>>(
            std::vector<ui::RecoveryDisposition>(offers.size(), ui::RecoveryDisposition::Recover));
    });
    const app::RecoveryPrompt::Outcome recovery =
        app::RecoveryPrompt::checkAndOffer(*again.window, again.root->services());
    REQUIRE(recovery.recovered == 1);
    REQUIRE(again.window->documentCount() == 1);
    CHECK(QFileInfo(again.window->documentAt(0)->path()).absoluteFilePath() == files.back());

    const app::SessionShell::Restored restored = again.session->restore(*again.window, again.root->services());

    // CHECK rather than REQUIRE, deliberately: the skipped list is the MECHANISM and the tab count below
    // is the HARM, and a case that stopped at the mechanism would tell whoever plants a bug in it that
    // something is wrong without telling them what it costs.
    CHECK(restored.skipped.size() == 1);
    if (!restored.skipped.empty())
        CHECK(QFileInfo(restored.skipped.front()).absoluteFilePath() == files.back());
    CHECK(restored.documents == 1);       // only the FIRST window's document was opened by restore

    // EXACTLY ONE TAB IN THE WHOLE PROCESS HOLDS THAT FILE, counted across every window rather than in
    // one of them: the defect this case exists for is a SECOND WINDOW opening a second handle, which any
    // per-window count would miss entirely.
    int holding = 0;
    int windows = 0;
    for (const ui::MainWindow* window : ui::MainWindow::shellWindows()) {
        if (window == nullptr || !window->isVisible()) continue;
        ++windows;
        for (int at = 0; at < window->documentCount(); ++at)
            if (QFileInfo(window->documentAt(at)->path()).absoluteFilePath() == files.back()) ++holding;
    }
    CHECK(holding == 1);

    // AND NO EMPTY SECOND WINDOW. The session's second window had exactly one document, restore skipped
    // it, and a bare shell shown beside the real one would be a window the person has to close to find
    // out it was never theirs.
    CHECK(windows == 1);
    CHECK(restored.windows == 1);

    // The recovered tab is still the recovered one: dirty, with the probe's rotations in its journal. A
    // restore that had re-opened the file over it would have produced a clean tab at the bytes on disk.
    const ui::DocumentWindow* recovered = again.window->documentAt(0);
    REQUIRE(recovered != nullptr);
    CHECK(recovered->isDirty());
    REQUIRE(recovered->recoveryReport().has_value());

    std::cout << "A-77: recovery opened 1, restore opened " << restored.documents << " and skipped "
              << restored.skipped.size() << "; " << holding << " tab(s) in " << windows
              << " window(s) hold the recovered file\n";
}

TEST_CASE("the control -- with no crashed journal restore opens the document itself", "[restore]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const std::vector<QString> files = copyCorpus(scratch.path(), {"ordinary/text-a4.pdf"});

    {
        Launch first(scratch.path());
        first.window->openFiles({files.front()});
        first.quit();
    }

    Launch again(scratch.path());
    const app::RecoveryPrompt::Outcome recovery =
        app::RecoveryPrompt::checkAndOffer(*again.window, again.root->services());
    REQUIRE(recovery.offered == 0);
    const app::SessionShell::Restored restored = again.session->restore(*again.window, again.root->services());

    // The same document, reached by the other path. Without this the case above would pass just as well if
    // restore had stopped opening anything at all (convention 7).
    CHECK(restored.skipped.empty());
    CHECK(restored.documents == 1);
    REQUIRE(again.window->documentCount() == 1);
    CHECK(openPaths(*again.window) == files);
}

// =====================================================================================================
// A-78 -- the three memory switches, each asserted on WHAT CAME BACK rather than on the setting
// =====================================================================================================

TEST_CASE("restoreSession off restores nothing and captures nothing", "[restore]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const std::vector<QString> files = copyCorpus(scratch.path(), {"ordinary/text-a4.pdf"});
    const QString sessionFile = QDir(scratch.path()).filePath(QStringLiteral("session.json"));

    // A session worth restoring, written while the switch is ON.
    {
        Launch first(scratch.path());
        first.window->openFiles({files.front()});
        first.quit();
    }
    REQUIRE(QFileInfo::exists(sessionFile));
    const QByteArray captured = [&] {
        QFile file(sessionFile);
        REQUIRE(file.open(QIODevice::ReadOnly));
        return file.readAll();
    }();

    // Now off. NOTHING COMES BACK, and -- the half that is easy to forget -- NOTHING IS WRITTEN either: a
    // product that went on recording what was read while the person had asked it to stop would be answering
    // a different question from the one on the checkbox.
    {
        Launch second(scratch.path());
        second.root->settings().set(core::keys::general::restoreSession, false);
        const app::SessionShell::Restored restored =
            second.session->restore(*second.window, second.root->services());
        CHECK(restored.disabled);
        CHECK(restored.documents == 0);
        CHECK(second.window->documentCount() == 0);

        second.window->openFiles({files.front()});
        REQUIRE(second.window->documentCount() == 1);
        second.quit();
    }

    QFile file(sessionFile);
    REQUIRE(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll() == captured);   // byte-identical: the second quit wrote nothing
}

TEST_CASE("rememberWindowLayout off restores the documents at default geometry", "[restore]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const std::vector<QString> files = copyCorpus(scratch.path(), {"ordinary/text-a4.pdf"});

    // WIDER THAN THE SHELL'S MINIMUM, and that is the first thing this case had to learn: a `MainWindow`
    // carries a ribbon, and `setGeometry` on a rectangle narrower than its minimum size hint gives back the
    // minimum. A "remembered" 820-wide window would have been 1222 wide on both sides of the relaunch, and
    // the case would have passed while proving nothing about the session (convention 7).
    const QRect chosen(60, 70, 1400, 900);
    QRect wanted;
    {
        Launch first(scratch.path());
        first.window->openFiles({files.front()});
        first.window->setGeometry(chosen);
        QCoreApplication::processEvents();

        // What the window ACTUALLY took, which is what a capture can possibly hold. Asserting against
        // `chosen` rather than this would be asserting that Qt honoured a request, which is not the subject.
        wanted = first.window->geometry();
        first.quit();
    }
    REQUIRE(wanted.width() == chosen.width());
    REQUIRE(wanted.height() == chosen.height());

    // ON first, so the OFF assertion below is a difference rather than a coincidence -- the geometry has to be
    // demonstrably restorable before "it was not restored" means anything (convention 7).
    {
        Launch on(scratch.path());
        on.session->restore(*on.window, on.root->services());

        // Component by component, because Catch cannot print a `QRect` and `{?} == {?}` says nothing about
        // WHICH of the four numbers moved.
        const QRect got = on.window->geometry();
        CHECK(got.x() == wanted.x());
        CHECK(got.y() == wanted.y());
        CHECK(got.width() == wanted.width());
        CHECK(got.height() == wanted.height());
        on.shutDown();
    }

    Launch off(scratch.path());
    const QRect asBuilt = off.window->geometry();
    off.root->settings().set(core::keys::general::rememberWindowLayout, false);
    const app::SessionShell::Restored restored = off.session->restore(*off.window, off.root->services());

    CHECK(restored.documents == 1);            // the DOCUMENTS still come back; only the rectangle does not
    CHECK(off.window->geometry() == asBuilt);
    CHECK(off.window->width() != wanted.width());
}

TEST_CASE("rememberPaneLayout off restores the documents with no panes", "[restore]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const std::vector<QString> files = copyCorpus(scratch.path(), {"ordinary/text-a4.pdf"});

    {
        Launch first(scratch.path());
        first.window->openFiles({files.front()});
        REQUIRE(first.window->sidebar() != nullptr);
        first.window->sidebar()->show();
        first.window->sidebar()->showPane(core::ActionId::PaneThumbnails);
        QCoreApplication::processEvents();
        first.quit();
    }

    {
        Launch on(scratch.path());
        on.session->restore(*on.window, on.root->services());
        REQUIRE(on.window->sidebar() != nullptr);
        CHECK(on.window->sidebar()->isVisible());
        on.shutDown();
    }

    Launch off(scratch.path());
    off.root->settings().set(core::keys::general::rememberPaneLayout, false);
    const app::SessionShell::Restored restored = off.session->restore(*off.window, off.root->services());

    CHECK(restored.documents == 1);
    REQUIRE(off.window->sidebar() != nullptr);

    // NO PANES means the sidebar is closed, and not "the window as it was built" -- a window is built with the
    // sidebar showing, so leaving it alone would make the switch look like it had done nothing.
    CHECK_FALSE(off.window->sidebar()->isVisible());
}

// =====================================================================================================
// A-79 -- a test composition writes its own session file, never the user's
// =====================================================================================================

TEST_CASE("a composition case never touches the real session file", "[restore]")
{
    // The trap Stage 3 T11 recorded for the recovery root, with a second subject. Read BEFORE and AFTER a
    // full capture-and-restore round trip: if `Options::sessionPath` were ever dropped, this is what says so.
    const QString real = core::SessionState::defaultFilePath();
    const QFileInfo info(real);
    const bool existed = info.exists();
    const QDateTime modifiedBefore = existed ? info.lastModified() : QDateTime();
    const QByteArray hashBefore = [&] {
        QFile file(real);
        if (!file.open(QIODevice::ReadOnly)) return QByteArray();
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
    }();

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const std::vector<QString> files = copyCorpus(scratch.path(), {"ordinary/text-a4.pdf"});
    {
        Launch first(scratch.path());
        first.window->openFiles({files.front()});
        first.quit();
    }
    {
        Launch again(scratch.path());
        again.session->restore(*again.window, again.root->services());
        REQUIRE(again.window->documentCount() == 1);
        again.quit();
    }

    // The scratch file is the one that moved.
    CHECK(QFileInfo::exists(QDir(scratch.path()).filePath(QStringLiteral("session.json"))));

    const QFileInfo after(real);
    CHECK(after.exists() == existed);
    if (existed) {
        CHECK(after.lastModified() == modifiedBefore);
        QFile file(real);
        REQUIRE(file.open(QIODevice::ReadOnly));
        CHECK(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex() == hashBefore);
    }
    std::cout << "A-79: " << real.toStdString() << (existed ? " unchanged\n" : " still absent\n");
}
