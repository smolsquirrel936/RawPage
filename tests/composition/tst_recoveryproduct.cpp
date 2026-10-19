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

#include "tests/platform/killprobe/AnnotationSteps.h"

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
#include "ui/dialogs/UnsavedChangesDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/MessageBar.h"

#include <QCoreApplication>
#include <QCryptographicHash>
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

// =====================================================================================================
// Stage 6 guide A-107, its last clause: NO RECOVERY IS OFFERED FOR A SESSION THAT ENDED CLEANLY (Stage 6 T15)
// =====================================================================================================
// The kill half is `tests/platform/tst_recoverykill.cpp`'s, twenty kills over the annotation cycle. This is the
// other side, through the product's own close: two documents annotated by the same cycle -- a side file among
// the steps (D6-23) -- the window closed, one answered SAVE and one DON'T SAVE, and the root asked afterwards.
// The answers are the dialog's twin (convention 16), as `tst_restore`'s A-76 answers it.

TEST_CASE("a session whose annotations end in a save or a discard leaves nothing to offer", "[recoveryproduct][A-107]")
{
    struct CloseReset {
        ~CloseReset() { ui::UnsavedChangesDialog::setAnswerForTesting({}); }
    } closeReset;
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString saved = QDir(scratch.path()).filePath(QStringLiteral("saved.pdf"));
    const QString discarded = QDir(scratch.path()).filePath(QStringLiteral("discarded.pdf"));
    for (const QString& copy : {saved, discarded}) {
        REQUIRE(QFile::copy(corpusFile("ordinary/text-a4.pdf"), copy));
        QFile::setPermissions(copy, QFile::ReadOwner | QFile::WriteOwner);
    }
    const auto shaOf = [](const QString& path) {
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex().toStdString();
    };
    const std::string savedBefore = shaOf(saved);
    const std::string discardedBefore = shaOf(discarded);
    const QString recoveryRoot = QDir(scratch.path()).filePath(QStringLiteral("recovery"));

    app::CompositionRoot::Options options;
    options.settingsPath = QDir(scratch.path()).filePath(QStringLiteral("settings.ini"));
    options.sessionPath = QDir(scratch.path()).filePath(QStringLiteral("session.json"));
    options.recoveryRoot = recoveryRoot;
    auto root = std::make_unique<app::CompositionRoot>(std::move(options));
    auto window = std::make_unique<ui::MainWindow>(root->services());
    window->resize(1000, 800);
    window->show();
    QCoreApplication::processEvents();
    window->openFiles({saved, discarded});
    REQUIRE(window->documentCount() == 2);

    // FIVE STEPS EACH, THROUGH THE DOCUMENT'S OWN JOURNAL -- its show step and its recorder the product's.
    std::vector<QString> directories;
    for (int index = 0; index < 2; ++index) {
        ui::DocumentWindow* document = window->documentAt(index);
        REQUIRE(document != nullptr);
        REQUIRE(document->journal() != nullptr);
        REQUIRE(document->recovery() != nullptr);
        for (int k = 1; k <= tests::killsteps::kCycle; ++k)
            REQUIRE(document->journal()->run(tests::killsteps::step(k, document->journal()->document())));
        CHECK(document->isDirty());
        const QDir directory(document->recovery()->directory());
        REQUIRE(directory.exists());
        CHECK(directory.entryList(QStringList{QStringLiteral("*.side")}, QDir::Files).size() == 1);   // step 5's payload
        directories.push_back(directory.absolutePath());
    }
    // Live, so never offered -- the lock is held.
    CHECK(core::RecoveryJournal::discover(recoveryRoot).empty());

    // THE CLOSE: the window's, which asks once per unsaved document.
    std::vector<QString> asked;
    ui::UnsavedChangesDialog::setAnswerForTesting([&asked](const QString& name) {
        asked.push_back(name);
        return std::optional<ui::CloseDisposition>(name.startsWith(QStringLiteral("saved")) ? ui::CloseDisposition::Save
                                                                                            : ui::CloseDisposition::Discard);
    });
    REQUIRE(window->close());
    QCoreApplication::processEvents();
    CHECK(asked.size() == 2);
    window.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    root.reset();

    // NOTHING TO OFFER, and no directory left behind -- the saved session's and the discarded one's alike.
    for (const QString& directory : directories) {
        INFO(directory.toStdString());
        CHECK_FALSE(QDir(directory).exists());
    }
    CHECK(core::RecoveryJournal::discover(recoveryRoot).empty());
    // And the files say which answer each got: the saved one changed, the discarded one byte-identical.
    CHECK(shaOf(saved) != savedBefore);
    CHECK(shaOf(discarded) == discardedBefore);
    std::cout << "A-107: two annotated sessions closed -- one saved, one discarded -- left "
              << core::RecoveryJournal::discover(recoveryRoot).size() << " journal(s) to offer\n";
}
