// Stage 3 T11 -- the recovery prompt at launch: `app/RecoveryPrompt`, `ui/dialogs/RecoveryDialog`, and
// `MainWindow::recoverDocument`. Guide cases A-67's untitled half and A-82's prompt half
// (`Stage3-Mutation-Testing-Guide.md` §5.10, §5.12); decision D3-11.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS SUITE CANNOT SEE, NAMED HERE (convention 19).
//
// * **No process dies.** A "crash" is a `MainWindow` destroyed without being closed: its dirty documents keep
//   their recovery directories and their locks are released by destructors, not by the OS reaping a process.
//   The real kill -- a journal written by `rawpage_killprobe`, killed with TerminateProcess, and replayed by the
//   product's own composition -- is `tests/composition/tst_recoveryproduct.cpp` (A-74).
// * **No real engine and no real replace**: `uitest::FakeBackend` in its editable mode and the headless
//   suite's `FakeDurableFiles`, as in `tst_mutationcommands.cpp`, whose header says what those do not cover.
// * **The dialog is answered through its twin** in every case that launches, because a modal dialog under the
//   offscreen platform is shown to nobody and never returns (Stage 1's I-23). The WIDGET is built and driven
//   by the first case; a person meets it in manual case M-04.
// * Some rotations are run through the journal directly rather than by `Ctrl+R`, to rotate a page that is not
//   the current one; each says so where it does. Pressing the chord is `tst_mutationcommands.cpp`'s (A-71).

#include <catch2/catch_test_macros.hpp>

#include "app/RecoveryPrompt.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/DocumentId.h"
#include "core/doc/PageTree.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/RecoveryDialog.h"
#include "ui/dialogs/RotatePagesDialog.h"
#include "ui/dialogs/UnsavedChangesDialog.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentTabBar.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/MessageBar.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLayout>
#include <QRadioButton>

#include <memory>

using namespace rawpage;
using core::ActionId;
using ui::MessageBar;
using ui::RecoveryDialog;
using ui::RecoveryDisposition;
using Offer = ui::RecoveryDialog::Offer;

namespace {

struct Fixture : uitest::ShellFixture {
    Fixture() { backend.editable = true; backend.pagesPerDocument = 3; }

    QString writePdf(const QString& name, const QByteArray& bytes = QByteArray("%PDF-1.7\n% a fake page\n"))
    {
        const QString path = QDir(directory.path()).filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(bytes) == bytes.size());
        file.close();
        return QFileInfo(path).absoluteFilePath();
    }

    QByteArray bytesOf(const QString& path)
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        return file.readAll();
    }
};

// Every twin is process-wide; a case that installed one must not leave it for the next case.
struct Answers {
    ~Answers()
    {
        RecoveryDialog::setAnswerForTesting({});
        ui::RotatePagesDialog::setAnswerForTesting({});
        ui::UnsavedChangesDialog::setAnswerForTesting({});
        ui::MainWindow::setSaveAsAnswerForTesting({});
    }
};

std::unique_ptr<ui::MainWindow> launch(Fixture& fixture)
{
    auto window = std::make_unique<ui::MainWindow>(fixture.services());
    window->resize(1000, 800);
    window->show();
    for (QWidget* widget : window->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    QCoreApplication::processEvents();
    window->raiseToForeground();
    return window;
}

// A "crash": the window destroyed WITHOUT closing it, so no document is asked about and a dirty one keeps its
// recovery directory. Not a process dying -- the header says where that is.
void crash(std::unique_ptr<ui::MainWindow>& window)
{
    window.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

ui::DocumentWindow* openIn(ui::MainWindow& window, const QString& path)
{
    window.openFiles({path});
    ui::DocumentWindow* document = window.currentDocument();
    REQUIRE(document != nullptr);
    REQUIRE(document->journal() != nullptr);
    REQUIRE(document->recovery() != nullptr);
    REQUIRE(document->recovery()->isRecoverable());
    return document;
}

void rotateCurrentPage(Fixture& fixture, int degrees, bool allPages = false)
{
    ui::RotatePagesDialog::setAnswerForTesting([degrees, allPages] { return ui::RotatePagesDialog::Choice{degrees, allPages}; });
    REQUIRE(fixture.actions.dispatch(ActionId::PageRotate, core::KeyContext::Viewer));
}

int rotationOf(const ui::DocumentWindow* document, int page)
{
    return static_cast<int>(document->journal()->document().pages().pages().at(static_cast<std::size_t>(page)).rotation());
}

int operationLinesIn(const QString& directory)
{
    QFile file(QDir(directory).filePath(QStringLiteral("journal.ndjson")));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    return static_cast<int>(bytes.count('\n')) - 1;
}

// An answer that records the offers it was shown and answers the same disposition for each.
struct Recorder {
    std::vector<std::vector<Offer>> shown;
    RecoveryDisposition answer;
    explicit Recorder(RecoveryDisposition disposition) : answer(disposition)
    {
        RecoveryDialog::setAnswerForTesting([this](const std::vector<Offer>& offers) {
            shown.push_back(offers);
            return std::optional<std::vector<RecoveryDisposition>>(std::vector<RecoveryDisposition>(offers.size(), answer));
        });
    }
};

std::string text(const QString& value) { return value.toStdString(); }

} // namespace

// =============================================================================================
// The dialog itself: it describes, it answers, and it holds nothing
// =============================================================================================

TEST_CASE("the recovery dialog describes each journal and answers one disposition per row", "[recoverydialog]")
{
    Answers answers;
    std::vector<Offer> offers(5);
    offers[0] = Offer{QStringLiteral("a.pdf"), QStringLiteral("C:/docs/a.pdf"), Offer::State::IntoFile, {}};
    offers[1] = Offer{QStringLiteral("b.pdf"), QStringLiteral("C:/docs/b.pdf"), Offer::State::SourceChanged, {}};
    offers[2] = Offer{QStringLiteral("c.pdf"), QStringLiteral("C:/docs/c.pdf"), Offer::State::Untitled, {}};
    offers[3] = Offer{QStringLiteral("d.pdf"), QStringLiteral("C:/docs/d.pdf"), Offer::State::SourceMissing, {}};
    offers[4] = Offer{QStringLiteral("e.pdf"), QStringLiteral("C:/docs/e.pdf"), Offer::State::Damaged, QStringLiteral("a planted reason")};

    // The constructor takes descriptions and a parent: no journal, no document, no window state.
    RecoveryDialog dialog(offers);
    REQUIRE(dialog.rowCount() == 5);

    // THE BUTTON NAMES WHAT RECOVER DOES: into the file for the first row only.
    CHECK(text(dialog.recoverButton(0)->text()) == text(ui::catalog::translate(ui::catalog::key::RecoveryRecover)));
    for (int row : {1, 2})
        CHECK(text(dialog.recoverButton(row)->text()) == text(ui::catalog::translate(ui::catalog::key::RecoveryRecoverToNewFile)));
    CHECK(text(dialog.stateLabel(1)->text()) == text(ui::catalog::translate(ui::catalog::key::RecoveryStateSourceChanged)));
    CHECK(dialog.stateLabel(4)->text().contains(QStringLiteral("a planted reason")));

    // Nothing is thrown away by default; a row that cannot be recovered waits, and its Recover is off.
    for (int row : {0, 1, 2}) {
        CHECK(dialog.recoverButton(row)->isEnabled());
        CHECK(dialog.recoverButton(row)->isChecked());
    }
    for (int row : {3, 4}) {
        CHECK_FALSE(dialog.recoverButton(row)->isEnabled());
        CHECK(dialog.laterButton(row)->isChecked());
    }
    CHECK(dialog.dispositions()
          == std::vector<RecoveryDisposition>{RecoveryDisposition::Recover, RecoveryDisposition::Recover,
                                              RecoveryDisposition::Recover, RecoveryDisposition::Later,
                                              RecoveryDisposition::Later});

    // Driven: discard the first, defer the second -- and a disabled Recover checked by force still answers Later.
    dialog.discardButton(0)->setChecked(true);
    dialog.laterButton(1)->setChecked(true);
    dialog.recoverButton(3)->setChecked(true);
    CHECK(dialog.dispositions()
          == std::vector<RecoveryDisposition>{RecoveryDisposition::Discard, RecoveryDisposition::Later,
                                              RecoveryDisposition::Recover, RecoveryDisposition::Later,
                                              RecoveryDisposition::Later});

    // Nobody to ask: offscreen with no answer installed, ask() returns at once rather than showing a modal.
    CHECK_FALSE(RecoveryDialog::canAsk());
    CHECK_FALSE(RecoveryDialog::ask(nullptr, offers).has_value());
}

// =============================================================================================
// M-04's steps 1-3, headless: Recover restores the document, its history and its marker
// =============================================================================================

TEST_CASE("a crashed session is offered at launch and Recover restores the document its history and the dirty marker", "[recoverydialog]")
{
    Answers answers;
    Fixture fixture;
    const QString path = fixture.writePdf(QStringLiteral("edit.pdf"));

    // SESSION ONE: rotate page 1; rotate all pages 180 as one group; undo it; rotate page 2. Not saved.
    auto first = launch(fixture);
    ui::DocumentWindow* before = openIn(*first, path);
    rotateCurrentPage(fixture, 90);
    rotateCurrentPage(fixture, 180, /*allPages=*/true);
    REQUIRE(fixture.actions.dispatch(ActionId::EditUndo, core::KeyContext::Viewer));
    // Page 2 is not the current page, so this rotation goes to the journal directly (the header says why).
    REQUIRE(before->journal()->run(std::make_unique<core::RotatePageCommand>(1, 90)));

    const QByteArray model = before->journal()->document().modelHash();
    const std::size_t historySize = before->journal()->historySize();
    const std::size_t position = before->journal()->position();
    const auto undoName = before->journal()->undoName();
    const QString crashedDirectory = before->recovery()->directory();
    crash(first);
    REQUIRE(QDir(crashedDirectory).exists());

    // SESSION TWO, launched: the prompt offers the document, and Recover is answered.
    auto second = launch(fixture);
    Recorder recorder(RecoveryDisposition::Recover);
    const app::RecoveryPrompt::Outcome outcome = app::RecoveryPrompt::checkAndOffer(*second, fixture.services());
    CHECK(outcome.found == 1);
    CHECK(outcome.offered == 1);
    CHECK(outcome.recovered == 1);
    REQUIRE(recorder.shown.size() == 1);
    REQUIRE(recorder.shown.front().size() == 1);
    CHECK(text(recorder.shown.front().front().documentName) == "edit.pdf");
    CHECK(recorder.shown.front().front().state == Offer::State::IntoFile);

    REQUIRE(second->documentCount() == 1);
    ui::DocumentWindow* recovered = second->currentDocument();
    REQUIRE(recovered != nullptr);
    CHECK(recovered->path() == path);
    CHECK_FALSE(recovered->isUntitled());
    CHECK(recovered->isDirty());
    CHECK(recovered->journal()->document().modelHash() == model);
    CHECK(recovered->journal()->historySize() == historySize);
    CHECK(recovered->journal()->position() == position);
    CHECK(recovered->journal()->undoName() == undoName);
    CHECK(rotationOf(recovered, 0) == 90);
    CHECK(rotationOf(recovered, 1) == 90);
    CHECK(rotationOf(recovered, 2) == 0);
    CHECK(text(second->documentTabs()->tabText(0))
          == text(ui::catalog::translate(ui::catalog::key::DocumentDirtyTitle).arg(QStringLiteral("edit.pdf"))));

    // The banner says it was recovered, and how many steps.
    REQUIRE(recovered->recoveryReport().has_value());
    CHECK(recovered->recoveryReport()->stepsApplied == 4);      // a run, a group, an undo, a run
    REQUIRE(second->messageBar()->isShowing(MessageBar::Kind::Recovered));
    CHECK(text(second->messageBar()->textFor(MessageBar::Kind::Recovered))
          == text(ui::catalog::translate(ui::catalog::key::MessageBarRecovered)
                      .arg(QStringLiteral("edit.pdf"), QStringLiteral("4"))));

    // The crashed session's directory is gone; the document's new journal holds every line it replayed.
    CHECK_FALSE(QDir(crashedDirectory).exists());
    REQUIRE(recovered->recovery() != nullptr);
    CHECK(QDir(recovered->recovery()->directory()).absolutePath() != QDir(crashedDirectory).absolutePath());
    CHECK(operationLinesIn(recovered->recovery()->directory()) == 8);

    // `Ctrl+Z` twice walks back through the same history: page 2, then page 1, and the document is clean.
    REQUIRE(fixture.actions.dispatch(ActionId::EditUndo, core::KeyContext::Viewer));
    CHECK(rotationOf(recovered, 1) == 0);
    REQUIRE(fixture.actions.dispatch(ActionId::EditUndo, core::KeyContext::Viewer));
    CHECK(rotationOf(recovered, 0) == 0);
    CHECK_FALSE(recovered->isDirty());

    // A LIVE document's journal is never offered: a third window, while the second lives, is offered nothing.
    Recorder another(RecoveryDisposition::Recover);
    auto third = launch(fixture);
    const app::RecoveryPrompt::Outcome none = app::RecoveryPrompt::checkAndOffer(*third, fixture.services());
    CHECK(none.found == 0);
    CHECK(another.shown.empty());
    CHECK(third->documentCount() == 0);

    third.reset();
    second.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

// =============================================================================================
// M-04's step 4 and A-67's second half: a changed source is recovered into an untitled document
// =============================================================================================

TEST_CASE("recover to a new file opens an untitled document that stays untitled and Save asks where", "[recoverydialog]")
{
    Answers answers;
    Fixture fixture;
    const QString path = fixture.writePdf(QStringLiteral("edit.pdf"));

    auto first = launch(fixture);
    openIn(*first, path);
    rotateCurrentPage(fixture, 90);
    crash(first);

    // ANOTHER FILE IS COPIED OVER THE SOURCE.
    const QByteArray replaced("%PDF-1.7\n% somebody else's file\n");
    fixture.writePdf(QStringLiteral("edit.pdf"), replaced);

    auto second = launch(fixture);
    {
        Recorder recorder(RecoveryDisposition::Recover);
        CHECK(app::RecoveryPrompt::checkAndOffer(*second, fixture.services()).recovered == 1);
        REQUIRE(recorder.shown.size() == 1);
        CHECK(recorder.shown.front().front().state == Offer::State::SourceChanged);
    }
    ui::DocumentWindow* untitled = second->currentDocument();
    REQUIRE(untitled != nullptr);
    CHECK(untitled->isUntitled());
    CHECK(untitled->path().isEmpty());
    CHECK(untitled->sourcePath() == path);
    CHECK(text(untitled->title()) == text(ui::catalog::translate(ui::catalog::key::DocumentUntitled)));
    CHECK(untitled->isDirty());
    CHECK(rotationOf(untitled, 0) == 90);
    CHECK(second->messageBar()->textFor(MessageBar::Kind::Recovered)
              .contains(ui::catalog::translate(ui::catalog::key::MessageBarRecoveredSourceChanged).arg(QStringLiteral("edit.pdf"))));
    CHECK(fixture.bytesOf(path) == replaced);
    CHECK(fixture.backend.writes == 0);

    // CRASHED AGAIN before saving: the source now matches the journal, and the document is STILL offered as a
    // new file -- the meta said untitled -- never as a replay under the source's path.
    crash(second);
    auto third = launch(fixture);
    {
        Recorder recorder(RecoveryDisposition::Recover);
        CHECK(app::RecoveryPrompt::checkAndOffer(*third, fixture.services()).recovered == 1);
        REQUIRE(recorder.shown.size() == 1);
        CHECK(recorder.shown.front().front().state == Offer::State::Untitled);
    }
    untitled = third->currentDocument();
    REQUIRE(untitled != nullptr);
    CHECK(untitled->isUntitled());
    CHECK(rotationOf(untitled, 0) == 90);
    CHECK(third->messageBar()->textFor(MessageBar::Kind::Recovered)
              .contains(ui::catalog::translate(ui::catalog::key::MessageBarRecoveredUntitled).arg(QStringLiteral("edit.pdf"))));

    // `Ctrl+S` ASKS FOR A PATH, after saying why, and proposes a new name beside the source -- never the source.
    const QString proposedName = ui::catalog::translate(ui::catalog::key::RecoverySuggestedName).arg(QStringLiteral("edit"))
                                 + QStringLiteral(".pdf");
    const QString chosen = QDir(fixture.directory.path()).filePath(QStringLiteral("kept.pdf"));
    QStringList proposals;
    QString explanation;
    ui::MainWindow::setSaveAsAnswerForTesting([&](const QString& proposed) -> std::optional<QString> {
        proposals << proposed;
        explanation = third->messageBar()->textFor(MessageBar::Kind::SaveAsRequired);
        return chosen;
    });
    REQUIRE(fixture.actions.dispatch(ActionId::FileSave, core::KeyContext::Viewer));
    REQUIRE(proposals.size() == 1);
    CHECK(QFileInfo(proposals.front()).fileName() == proposedName);
    CHECK(QFileInfo(proposals.front()).absolutePath() == QFileInfo(path).absolutePath());
    // THE REASON IS "NO FILE OF ITS OWN", not "changed on disk": `SaveCoordinator`'s identity check would also turn
    // a save to the empty path into a Save As, with the wrong explanation -- which is how the first control of
    // the untitled branch stayed green until this line existed (convention 7).
    INFO("explanation: " << text(explanation));
    CHECK(explanation.contains(ui::catalog::translate(QStringLiteral("error.save.untitled"))));
    CHECK(untitled->path() == QFileInfo(chosen).absoluteFilePath());
    CHECK_FALSE(untitled->isUntitled());
    CHECK_FALSE(untitled->isDirty());
    CHECK(fixture.backend.writes == 1);
    CHECK(fixture.bytesOf(path) == replaced);                      // the source was never written
    CHECK_FALSE(third->messageBar()->isShowing(MessageBar::Kind::Recovered));

    third.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

// =============================================================================================
// M-04's step 5: Discard, and the next launch offers nothing
// =============================================================================================

TEST_CASE("discard removes a crashed session's journal and the next launch offers nothing", "[recoverydialog]")
{
    Answers answers;
    Fixture fixture;
    const QString path = fixture.writePdf(QStringLiteral("edit.pdf"));

    auto first = launch(fixture);
    const QString directory = openIn(*first, path)->recovery()->directory();
    rotateCurrentPage(fixture, 90);
    crash(first);

    auto second = launch(fixture);
    {
        Recorder recorder(RecoveryDisposition::Discard);
        const app::RecoveryPrompt::Outcome outcome = app::RecoveryPrompt::checkAndOffer(*second, fixture.services());
        CHECK(outcome.offered == 1);
        CHECK(outcome.discarded == 1);
        CHECK(outcome.recovered == 0);
    }
    CHECK(second->documentCount() == 0);
    CHECK_FALSE(QDir(directory).exists());
    crash(second);

    auto third = launch(fixture);
    Recorder recorder(RecoveryDisposition::Recover);
    const app::RecoveryPrompt::Outcome outcome = app::RecoveryPrompt::checkAndOffer(*third, fixture.services());
    CHECK(outcome.found == 0);
    CHECK(recorder.shown.empty());
    CHECK(third->documentCount() == 0);
}

// =============================================================================================
// The report of step k (D3-11): what could not be replayed is named, and what came before it kept
// =============================================================================================

TEST_CASE("a replay that stops at step k keeps the steps before it and the banner names k", "[recoverydialog]")
{
    Answers answers;
    Fixture fixture;
    const QString path = fixture.writePdf(QStringLiteral("edit.pdf"));

    // Three rotations -- pages 1, 3 and 2 -- run through the journal directly, to rotate pages that are not
    // the current one (the header says so).
    auto first = launch(fixture);
    ui::DocumentWindow* before = openIn(*first, path);
    REQUIRE(before->journal()->run(std::make_unique<core::RotatePageCommand>(0, 90)));
    REQUIRE(before->journal()->run(std::make_unique<core::RotatePageCommand>(2, 90)));
    REQUIRE(before->journal()->run(std::make_unique<core::RotatePageCommand>(1, 90)));
    const QString crashedDirectory = before->recovery()->directory();
    crash(first);

    // The same bytes now open with TWO pages, so step 2's rotation of page 3 has no page to land on.
    fixture.backend.pagesPerDocument = 2;
    auto second = launch(fixture);
    Recorder recorder(RecoveryDisposition::Recover);
    CHECK(app::RecoveryPrompt::checkAndOffer(*second, fixture.services()).recovered == 1);

    ui::DocumentWindow* recovered = second->currentDocument();
    REQUIRE(recovered != nullptr);
    REQUIRE(recovered->recoveryReport().has_value());
    const core::ReplayReport& report = *recovered->recoveryReport();
    CHECK(report.stepsApplied == 1);
    REQUIRE(report.stoppedAtStep.has_value());
    CHECK(*report.stoppedAtStep == 2);
    CHECK(rotationOf(recovered, 0) == 90);
    CHECK(rotationOf(recovered, 1) == 0);                          // step 3 was after the stop
    CHECK(recovered->journal()->historySize() == 1);
    CHECK(recovered->isDirty());

    const QString stopped = ui::catalog::translate(ui::catalog::key::MessageBarRecoveredStopped)
                                .arg(QStringLiteral("2"),
                                     ui::catalog::translateError(QString(report.stopKey), report.stopContext));
    INFO("banner: " << text(second->messageBar()->textFor(MessageBar::Kind::Recovered)));
    CHECK(second->messageBar()->textFor(MessageBar::Kind::Recovered).contains(stopped));
    CHECK_FALSE(stopped.contains(QStringLiteral("shell.messageBar")));   // resolved text, not a key

    CHECK(operationLinesIn(recovered->recovery()->directory()) == 1);
    CHECK_FALSE(QDir(crashedDirectory).exists());

    second.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

// =============================================================================================
// Convention 16, the empty journal, and a damaged one
// =============================================================================================

TEST_CASE("with nobody to ask every journal is kept and an empty one is removed without asking", "[recoverydialog]")
{
    Answers answers;
    Fixture fixture;
    const QString path = fixture.writePdf(QStringLiteral("edit.pdf"));

    // One journal with a change in it ...
    auto first = launch(fixture);
    const QString changed = openIn(*first, path)->recovery()->directory();
    rotateCurrentPage(fixture, 90);
    crash(first);

    // ... one with none: a session that opened the document and ended with nothing unsaved ...
    QString empty;
    {
        const core::DocumentId id = core::DocumentId::of(path, QFileInfo(path).lastModified().toMSecsSinceEpoch(),
                                                         fixture.bytesOf(path));
        auto journal = core::RecoveryJournal::open(fixture.recoveryRoot, path, id, fixture.platform.durableFiles.get());
        REQUIRE(journal->isRecoverable());
        empty = journal->directory();
    }
    // ... and one too damaged to name its source.
    const QString damaged = QDir(fixture.recoveryRoot).filePath(QStringLiteral("damaged-1-0"));
    REQUIRE(QDir().mkpath(damaged));
    {
        QFile meta(QDir(damaged).filePath(QStringLiteral("meta.json")));
        REQUIRE(meta.open(QIODevice::WriteOnly));
        meta.write("\x01{{ not json");
    }

    // NOBODY TO ASK: nothing opens, nothing with a change in it is lost, and the empty one goes quietly.
    auto second = launch(fixture);
    REQUIRE_FALSE(RecoveryDialog::canAsk());
    const app::RecoveryPrompt::Outcome unasked = app::RecoveryPrompt::checkAndOffer(*second, fixture.services());
    CHECK(unasked.found == 3);
    CHECK(unasked.empty == 1);
    CHECK(unasked.offered == 0);
    CHECK(unasked.kept == 2);
    CHECK(second->documentCount() == 0);
    CHECK(QDir(changed).exists());
    CHECK(QDir(damaged).exists());
    CHECK_FALSE(QDir(empty).exists());

    // ASKED, with Recover answered for everything: the damaged journal is offered as damaged, is NOT recovered
    // though the answer says so, and is kept; the other is recovered.
    Recorder recorder(RecoveryDisposition::Recover);
    const app::RecoveryPrompt::Outcome asked = app::RecoveryPrompt::checkAndOffer(*second, fixture.services());
    CHECK(asked.offered == 2);
    CHECK(asked.recovered == 1);
    CHECK(asked.kept == 1);
    REQUIRE(recorder.shown.size() == 1);
    int damagedOffers = 0;
    for (const Offer& offer : recorder.shown.front())
        if (offer.state == Offer::State::Damaged) ++damagedOffers;
    CHECK(damagedOffers == 1);
    CHECK(second->documentCount() == 1);
    CHECK(QDir(damaged).exists());
    CHECK_FALSE(QDir(changed).exists());

    second.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

// =============================================================================================
// Two untitled documents in one window: closing one closes THAT one
// =============================================================================================

TEST_CASE("two untitled recovered documents close the one that was asked about", "[recoverydialog]")
{
    Answers answers;
    Fixture fixture;
    const QString one = fixture.writePdf(QStringLiteral("one.pdf"));
    const QString two = fixture.writePdf(QStringLiteral("two.pdf"), QByteArray("%PDF-1.7\n% the second\n"));

    auto first = launch(fixture);
    openIn(*first, one);
    rotateCurrentPage(fixture, 90);
    openIn(*first, two);
    rotateCurrentPage(fixture, 180);
    crash(first);

    // Both sources change, so both come back untitled -- two tabs whose path is empty.
    fixture.writePdf(QStringLiteral("one.pdf"), QByteArray("%PDF-1.7\n% replaced one\n"));
    fixture.writePdf(QStringLiteral("two.pdf"), QByteArray("%PDF-1.7\n% replaced two\n"));
    auto second = launch(fixture);
    Recorder recorder(RecoveryDisposition::Recover);
    REQUIRE(app::RecoveryPrompt::checkAndOffer(*second, fixture.services()).recovered == 2);
    REQUIRE(second->documentCount() == 2);
    const ui::DocumentWindow* keep = second->documentAt(0);
    const ui::DocumentWindow* close = second->documentAt(1);
    REQUIRE(keep->isUntitled());
    REQUIRE(close->isUntitled());

    // Closing the SECOND with Discard closes the second -- an index found by an empty path would be the first.
    REQUIRE(second->closeDocument(1, ui::CloseDisposition::Discard));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    REQUIRE(second->documentCount() == 1);
    CHECK(second->documentAt(0) == keep);

    second.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

// =============================================================================================
// A recovery that cannot happen says so, and loses nothing
// =============================================================================================

TEST_CASE("a recovery that cannot happen says so in a banner and keeps the journal", "[recoverydialog]")
{
    Answers answers;
    Fixture fixture;
    const QString path = fixture.writePdf(QStringLiteral("edit.pdf"));

    auto first = launch(fixture);
    const QString crashedDirectory = openIn(*first, path)->recovery()->directory();
    rotateCurrentPage(fixture, 90);
    crash(first);

    // The same file is already open -- clean -- in the window the prompt runs in, so recovering INTO it would
    // put two tabs on one file.
    auto second = launch(fixture);
    second->openFiles({path});
    REQUIRE(second->documentCount() == 1);
    REQUIRE_FALSE(second->currentDocument()->isDirty());

    Recorder recorder(RecoveryDisposition::Recover);
    const app::RecoveryPrompt::Outcome outcome = app::RecoveryPrompt::checkAndOffer(*second, fixture.services());
    CHECK(outcome.offered == 1);
    CHECK(outcome.recovered == 0);
    CHECK(outcome.kept == 1);
    CHECK(second->documentCount() == 1);
    CHECK_FALSE(second->currentDocument()->isDirty());
    CHECK(QDir(crashedDirectory).exists());

    REQUIRE(second->messageBar()->isShowing(MessageBar::Kind::OpenFailed));
    const QString expected = ui::catalog::translate(ui::catalog::key::MessageBarRecoveryFailed)
                                 .arg(QStringLiteral("edit.pdf"),
                                      ui::catalog::translateError(QStringLiteral("error.recovery.alreadyOpen"),
                                                                  QStringLiteral("edit.pdf")));
    CHECK(text(second->messageBar()->textFor(MessageBar::Kind::OpenFailed)) == text(expected));
    CHECK_FALSE(expected.contains(QStringLiteral("error.recovery")));   // resolved, not the key

    second.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
