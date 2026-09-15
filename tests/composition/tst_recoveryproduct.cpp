// tst_recoveryproduct -- guide case A-74 (`Stage3-Mutation-Testing-Guide.md` §5.11): a journal left by a process
// killed with TerminateProcess, offered and replayed BY THE PRODUCT'S OWN COMPOSITION. Stage 3 T11, D3-11.
//
// The writer is `rawpage_killprobe` (tests/platform/killprobe/main.cpp) rotating pages of a corpus copy in a
// loop, one flushed journal line per rotation, until it is killed -- no destructor, no crash handler, no
// flush on the way out. The reader is what `app/main.cpp` assembles: `app::CompositionRoot` with the real
// PDFium backend and the real Windows bundle (`WinDurableFiles`), a `ui::MainWindow` on its services, and
// `app::RecoveryPrompt::checkAndOffer` -- the call main.cpp makes after showing the window.
//
// WHAT THIS DOES NOT COVER (convention 19), at the assertions as well as here:
//   * **The dialog is answered by its twin** (convention 16): under the offscreen platform a modal is shown
//     to nobody. The widget is `tests/ui/tst_recoverydialog.cpp`'s; a person meets it in manual case M-04.
//   * **`app/main.cpp` itself is not run**: this binary composes as it does and calls the prompt as it does.
//     That main.cpp makes the call at all is read in the source, and M-04 is where it is seen.
//   * **The real product is not the process killed**: the probe is. M-04 kills rawpage.exe.
//   * Pixels. The replayed rotation's geometry and pixels through the viewer are A-75's (tests/ui), and a
//     rotation drawn by the engine is A-58's (tests/render).

#include <catch2/catch_test_macros.hpp>

#include "app/CompositionRoot.h"
#include "app/RecoveryPrompt.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/doc/Document.h"
#include "core/doc/DocumentId.h"
#include "core/doc/PageTree.h"
#include "platform/platform.h"
#include "ui/dialogs/RecoveryDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/MessageBar.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

using namespace rawpage;

namespace {

QString corpusFile(const char* relative)
{
    return QString::fromStdString((std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative).string());
}

int rotationOf(const ui::DocumentWindow* document, int page)
{
    return static_cast<int>(document->journal()->document().pages().pages().at(static_cast<std::size_t>(page)).rotation());
}

// What `rawpage_killprobe commands` has done after `steps` rotations: rotation k turns page `k % pages` by 90.
int expectedRotation(int page, int steps, int pages)
{
    int turns = 0;
    for (int k = 1; k <= steps; ++k)
        if (k % pages == page) ++turns;
    return (turns * 90) % 360;
}

struct TwinReset {
    ~TwinReset() { ui::RecoveryDialog::setAnswerForTesting({}); }
};

} // namespace

TEST_CASE("the product's composition offers and replays a journal the killed probe left behind", "[recoveryproduct]")
{
    TwinReset twinReset;
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString document = QDir(scratch.path()).filePath(QStringLiteral("text-a4.pdf"));
    REQUIRE(QFile::copy(corpusFile("ordinary/text-a4.pdf"), document));
    const QString recoveryRoot = QDir(scratch.path()).filePath(QStringLiteral("recovery"));

    // -----------------------------------------------------------------------------------------------------
    // 1. THE PROBE ROTATES UNTIL IT IS KILLED. It is killed as soon as it has acknowledged three rotations, so
    //    the journal has work in it; where inside the next rotation the kill lands is whatever it is -- A-68
    //    measures that, twenty times -- and the replay must hold every acknowledged rotation and at most one more.
    // -----------------------------------------------------------------------------------------------------
    QProcess probe;
    probe.setProcessChannelMode(QProcess::SeparateChannels);
    probe.start(QStringLiteral(RAWPAGE_KILLPROBE_PATH),
                {QStringLiteral("commands"), document, recoveryRoot, QStringLiteral("2")});
    REQUIRE(probe.waitForStarted(10000));
    int lastAck = 0;
    QByteArray seen;
    QElapsedTimer timer;
    timer.start();
    while (lastAck < 3 && timer.elapsed() < 20000) {
        if (!probe.waitForReadyRead(10000)) break;
        seen += probe.readAllStandardOutput();
        for (const QByteArray& line : seen.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.startsWith("ack "))
                lastAck = std::max(lastAck, trimmed.mid(4).toInt());
        }
    }
    INFO("probe stderr: " << probe.readAllStandardError().toStdString());
    REQUIRE(lastAck >= 3);
    probe.kill();                                          // TerminateProcess
    REQUIRE(probe.waitForFinished(10000));
    seen += probe.readAllStandardOutput();
    for (const QByteArray& line : seen.split('\n'))
        if (line.trimmed().startsWith("ack ")) lastAck = std::max(lastAck, line.trimmed().mid(4).toInt());
    CHECK(probe.exitStatus() == QProcess::CrashExit);     // killed, not finished
    std::cout << "A-74: the probe acknowledged " << lastAck << " rotation(s) before TerminateProcess\n";

    const auto leftBehind = core::RecoveryJournal::discover(recoveryRoot);
    REQUIRE(leftBehind.size() == 1);
    const QString crashedDirectory = leftBehind.front().directory;
    REQUIRE(leftBehind.front().sourceMatches);

    // -----------------------------------------------------------------------------------------------------
    // 2. THE PRODUCT'S COMPOSITION, as app/main.cpp assembles it: a scratch settings file, no log file, the
    //    recovery root pointed at the probe's -- and the REAL engine and the REAL Windows bundle.
    // -----------------------------------------------------------------------------------------------------
    app::CompositionRoot::Options options;
    options.settingsPath = QDir(scratch.path()).filePath(QStringLiteral("settings.ini"));
    options.recoveryRoot = recoveryRoot;
    auto root = std::make_unique<app::CompositionRoot>(std::move(options));
    REQUIRE(root->platform().durableFiles != nullptr);

    auto window = std::make_unique<ui::MainWindow>(root->services());
    window->resize(1000, 800);
    window->show();
    QCoreApplication::processEvents();
    window->raiseToForeground();

    // THE TWIN ANSWERS THE DIALOG (convention 16) -- this case does not show the widget, and says so here.
    std::vector<ui::RecoveryDialog::Offer> shown;
    ui::RecoveryDialog::setAnswerForTesting([&shown](const std::vector<ui::RecoveryDialog::Offer>& offers) {
        shown = offers;
        return std::optional<std::vector<ui::RecoveryDisposition>>(
            std::vector<ui::RecoveryDisposition>(offers.size(), ui::RecoveryDisposition::Recover));
    });
    const app::RecoveryPrompt::Outcome outcome = app::RecoveryPrompt::checkAndOffer(*window, root->services());
    CHECK(outcome.offered == 1);
    REQUIRE(outcome.recovered == 1);
    REQUIRE(shown.size() == 1);
    CHECK(shown.front().state == ui::RecoveryDialog::Offer::State::IntoFile);
    CHECK(shown.front().documentName.toStdString() == "text-a4.pdf");

    // -----------------------------------------------------------------------------------------------------
    // 3. THE DOCUMENT: open under its own path, with the replayed rotations, dirty, and its undo history.
    // -----------------------------------------------------------------------------------------------------
    REQUIRE(window->documentCount() == 1);
    ui::DocumentWindow* recovered = window->currentDocument();
    REQUIRE(recovered != nullptr);
    CHECK(core::DocumentId::normalizePath(recovered->path()) == core::DocumentId::normalizePath(document));
    CHECK_FALSE(recovered->isUntitled());
    REQUIRE(recovered->recoveryReport().has_value());
    const core::ReplayReport& report = *recovered->recoveryReport();
    const int steps = report.stepsApplied;
    std::cout << "A-74: replayed " << steps << " step(s); torn trailing line " << report.tornTrailingLine << "\n";
    CHECK_FALSE(report.stoppedAtStep.has_value());
    CHECK(steps >= lastAck);                               // every acknowledged rotation ...
    CHECK(steps <= lastAck + 1);                           // ... and at most the one in progress

    REQUIRE(recovered->journal() != nullptr);
    CHECK(recovered->isEditable());                        // the real backend's sink and permissions
    CHECK(recovered->isDirty());
    CHECK(recovered->journal()->historySize() == static_cast<std::size_t>(steps));
    CHECK(recovered->journal()->position() == static_cast<std::size_t>(steps));
    const int pages = recovered->journal()->document().pages().count();
    REQUIRE(pages == 3);
    for (int page = 0; page < pages; ++page) {
        INFO("page " << page);
        CHECK(rotationOf(recovered, page) == expectedRotation(page, steps, pages));
    }
    CHECK(window->messageBar()->isShowing(ui::MessageBar::Kind::Recovered));

    // The history is the probe's: one undo takes back its last rotation, one redo puts it back.
    const int lastPage = steps % pages;
    REQUIRE(root->actions().dispatch(core::ActionId::EditUndo, core::KeyContext::Viewer));
    CHECK(rotationOf(recovered, lastPage) == expectedRotation(lastPage, steps - 1, pages));
    REQUIRE(root->actions().dispatch(core::ActionId::EditRedo, core::KeyContext::Viewer));
    CHECK(rotationOf(recovered, lastPage) == expectedRotation(lastPage, steps, pages));

    // -----------------------------------------------------------------------------------------------------
    // 4. THE DIRECTORIES: the killed session's is gone, and the document's own -- live, so never offered --
    //    holds every step it replayed, through the real WinDurableFiles.
    // -----------------------------------------------------------------------------------------------------
    CHECK_FALSE(QDir(crashedDirectory).exists());
    REQUIRE(recovered->recovery() != nullptr);
    CHECK(recovered->recovery()->isRecoverable());
    CHECK(core::RecoveryJournal::discover(recoveryRoot).empty());
    {
        QFile journal(QDir(recovered->recovery()->directory()).filePath(QStringLiteral("journal.ndjson")));
        REQUIRE(journal.open(QIODevice::ReadOnly));
        const QByteArray bytes = journal.readAll();
        // The replayed steps, plus the undo and the redo just recorded on top of them.
        CHECK(static_cast<int>(bytes.count('\n')) - 1 == steps + 2);
    }

    window.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    root.reset();
}
