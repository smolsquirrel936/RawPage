// tst_recovery -- `core/cmd/RecoveryJournal`. Stage 3 T9, guide cases A-62 … A-67's in-process halves and
// A-70 (`Stage3-Mutation-Testing-Guide.md` §5.10), decision D3-11.
//
// WHAT THIS SUITE IS TRYING TO CATCH: a journal that loses an operation the edit returned from; a replay
// that goes past damage, or drops good steps before it; a group recovered in part; an undo after a save
// that recovery re-applies instead of reverting; a live session's journal offered as a crashed one; an
// unwritable directory that refuses the edit or complains ten times; and a hostile file that crashes.
//
// WHAT IT DOES NOT COVER (convention 19):
//   * a PROCESS dying. "Drop the journal object without discarding" stands in for a crash here, and a lock
//     is released by the destructor rather than by the OS reaping a process. The real kills --
//     `TerminateProcess` at random instants during commands and during saves -- are
//     `tests/platform/tst_recoverykill.cpp` (A-65's child half, A-68, A-69);
//   * durability: `tests/unit/FakeDurableFiles.h` flushes nothing;
//   * `ui/`: offering, replaying into an untitled document, and the report are T11's --
//     `tests/ui/tst_recoverydialog.cpp`, and the product's composition `tests/composition/`.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/Log.h"
#include "core/cmd/RecoveryJournal.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using core::CatalogText;
using core::RecoveryJournal;
using tests::SaveSession;

namespace {

std::string keyOf(QLatin1String key)
{
    return std::string(key.data(), static_cast<std::size_t>(key.size()));
}

QByteArray rotateLine(int page, int degrees = 90)
{
    return QJsonDocument(QJsonObject{{"op", "run"},
                                     {"cmd", core::RotatePageCommand(page, degrees).serialize()}})
        .toJson(QJsonDocument::Compact);
}

QByteArray groupBegin()
{
    return R"({"op":"groupBegin","name":{"key":"command.page.rotate","args":["all"]}})";
}

QByteArray groupEnd()
{
    return R"({"op":"groupEnd"})";
}

// A journal directory written by hand, for the damage and hostile cases.
QString plantJournal(const QTemporaryDir& root, const QString& name, const QByteArray& journalBytes)
{
    const QString dir = root.filePath(name);
    REQUIRE(QDir().mkpath(dir));
    QFile file(QDir(dir).filePath("journal.ndjson"));
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(journalBytes);
    return dir;
}

QStringList operationsIn(const QString& directory)
{
    QStringList ops;
    const QByteArray bytes = tests::readAllBytes(QDir(directory).filePath("journal.ndjson"));
    const QList<QByteArray> lines = bytes.split('\n');
    for (qsizetype i = 1; i < lines.size(); ++i) {
        if (lines[i].isEmpty())
            continue;
        ops << QJsonDocument::fromJson(lines[i]).object().value("op").toString();
    }
    return ops;
}

// Counts the recovery warnings the log receives while it lives. Convention 18: one sink, replaced and
// restored, never chained.
struct WarningCounter {
    std::vector<std::string> lines;
    WarningCounter()
    {
        core::setLogSink([this](const core::LogRecordData& record) {
            if (record.level == core::LogLevel::Warning && record.message.contains("recovery:"))
                lines.push_back(record.message.toStdString());
        });
    }
    ~WarningCounter() { core::setLogSink({}); }
};

// A command that rotates and cannot be serialized -- Tech-Stack-Plan.md §5.5's "non-recoverable".
class UnserializableRotate final : public core::Command {
public:
    explicit UnserializableRotate(int page) : real_(page, 90) {}
    QLatin1String typeId() const override { return QLatin1String("test.unserializable"); }
    CatalogText name() const override { return real_.name(); }
    core::Scope scope() const override { return real_.scope(); }
    core::Operation operation() const override { return real_.operation(); }
    bool apply(core::Document& d, core::MutationSink& s) override { return real_.apply(d, s); }
    bool revert(core::Document& d, core::MutationSink& s) override { return real_.revert(d, s); }
    QJsonObject serialize() const override { return {}; }

private:
    core::RotatePageCommand real_;
};


// Stage 7 guide I-11 -- RECOVERY AS `MainWindow::recoverDocument` DOES IT, over the first-party backend: the checkpoint
// when the candidate names one, given the source's identity, else the source; then the journal replayed.
struct Recovered {
    tests::CosBackend backend;
    core::DocumentHandle handle;
    std::unique_ptr<core::CommandJournal> journal;
    core::ReplayReport report;
    const core::Document& document() const { return journal->document(); }
};

std::unique_ptr<Recovered> recoverAsTheWindowDoes(const core::RecoveryCandidate& candidate)
{
    auto out = std::make_unique<Recovered>();
    const QString from = candidate.checkpointPath.isEmpty() ? candidate.meta.sourcePath : candidate.checkpointPath;
    auto opened = out->backend.open(from, nullptr);
    REQUIRE(opened);
    out->handle = opened.value();
    if (!candidate.checkpointPath.isEmpty()) {
        core::DocumentId identity;
        identity.path = core::DocumentId::normalizePath(candidate.meta.sourcePath);
        identity.size = candidate.meta.size;
        identity.modifiedMsecs = candidate.meta.modifiedMsecs;
        identity.contentHash = candidate.meta.contentHash;
        REQUIRE(out->backend.assumeIdentity(out->handle, identity));
    }
    core::Document document;
    REQUIRE(document.hydrate(out->backend, out->handle));
    out->journal = std::make_unique<core::CommandJournal>(std::move(document), out->backend.mutationSink(out->handle));
    auto report = RecoveryJournal::replay(candidate.directory, *out->journal);
    REQUIRE(report);
    out->report = report.value();
    return out;
}

// A recording journal over `session`, with the checkpoint writer the tab sets -- the backend's append form -- or none.
std::unique_ptr<RecoveryJournal> recording(SaveSession& session, const QString& root, bool checkpoints)
{
    auto recovery = RecoveryJournal::open(root, session.path, session.journal->document().id(), &session.durableFiles);
    REQUIRE(recovery->isRecoverable());
    if (checkpoints) {
        recovery->setCheckpointWriter([&session](const QString& path) {
            return session.backend.writeRevision(session.handle, path, core::SaveForm::Append);
        });
    }
    session.journal->setRecorder(recovery.get());
    return recovery;
}

QStringList checkpointFiles(const QString& directory)
{
    return QDir(directory).entryList(QStringList{QStringLiteral("checkpoint-*")}, QDir::Files);
}

} // namespace

// ==========================================================================================
// A-62 -- append, discover, replay, truncate
// ==========================================================================================

TEST_CASE("recovery journal records every operation and replays the document and its undo history", "[recovery]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = RecoveryJournal::open(root.path(), session->path, session->journal->document().id(), &session->durableFiles);
    REQUIRE(recovery->isRecoverable());
    session->journal->setRecorder(recovery.get());

    REQUIRE(session->rotate(0));
    session->journal->beginGroup(CatalogText{QLatin1String("command.page.rotate"), {"all"}});
    REQUIRE(session->rotate(1));
    REQUIRE(session->rotate(2, 180));
    REQUIRE(session->journal->endGroup());
    REQUIRE(session->journal->undo());
    REQUIRE(session->journal->redo());
    REQUIRE(session->journal->undo());

    const QString directory = recovery->directory();
    const QByteArray bytes = tests::readAllBytes(QDir(directory).filePath("journal.ndjson"));
    CHECK(bytes.startsWith("{\"format\":1}\n"));
    CHECK(operationsIn(directory) == QStringList{"run", "groupBegin", "run", "run", "groupEnd", "undo", "redo", "undo"});

    // A LIVE journal is never offered (A-65's in-process half) ...
    CHECK(RecoveryJournal::discover(root.path()).empty());

    // ... and once its owner is gone -- dropped without discarding, which stands in for a crash -- it is.
    const QByteArray expectedModel = session->journal->document().modelHash();
    recovery.reset();
    session->journal->setRecorder(nullptr);
    const auto candidates = RecoveryJournal::discover(root.path());
    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front().directory == QDir(directory).absolutePath());
    CHECK(candidates.front().sourceMatches);
    CHECK(candidates.front().operationLines == 8);
    CHECK(keyOf(candidates.front().problemKey).empty());

    // Replay into a fresh document from the same source: the model AND the history.
    auto fresh = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(fresh);
    auto report = RecoveryJournal::replay(candidates.front().directory, *fresh->journal);
    REQUIRE(report);
    CHECK(report.value().stepsApplied == 5);                  // a run, a group, an undo, a redo, an undo
    CHECK_FALSE(report.value().stoppedAtStep.has_value());
    CHECK_FALSE(report.value().tornTrailingLine);
    CHECK(fresh->journal->document().modelHash() == expectedModel);
    CHECK(fresh->journal->historySize() == session->journal->historySize());
    CHECK(fresh->journal->position() == session->journal->position());
    CHECK(fresh->journal->canRedo() == session->journal->canRedo());
    CHECK(fresh->journal->redoName() == session->journal->redoName());
}

TEST_CASE("recovery journal truncates its lines on save and names the saved file", "[recovery]")
{
    // A-62's save half and A-56's meta.json clause.
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = RecoveryJournal::open(root.path(), session->path, session->journal->document().id(), &session->durableFiles);
    session->journal->setRecorder(recovery.get());
    const QString directory = recovery->directory();

    REQUIRE(session->rotate(0));
    REQUIRE(operationsIn(directory).size() == 1);
    const QString copy = session->directory.filePath("saved-as.pdf");
    auto saved = session->coordinator->saveAs(*session->journal, copy);
    REQUIRE(saved);
    REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);

    CHECK(tests::readAllBytes(QDir(directory).filePath("journal.ndjson")) == QByteArray("{\"format\":1}\n"));
    const QJsonObject meta = QJsonDocument::fromJson(tests::readAllBytes(QDir(directory).filePath("meta.json"))).object();
    CHECK(meta.value("source").toString() == copy);
    CHECK(meta.value("sha256").toString().toStdString() == tests::sha256(tests::readAllBytes(copy)));
    CHECK_FALSE(QFileInfo::exists(QDir(directory).filePath("meta.json.tmp")));

    // Lines after the save start again from the saved file.
    REQUIRE(session->rotate(1));
    CHECK(operationsIn(directory) == QStringList{"run"});

    SECTION("discard removes the directory")
    {
        recovery->discard();
        CHECK_FALSE(QFileInfo::exists(directory));
        CHECK_FALSE(recovery->isRecoverable());
        REQUIRE(session->rotate(2));                              // editing still works after a discard
    }
    SECTION("a discovered directory can be discarded once its owner is gone, and not before")
    {
        CHECK_FALSE(RecoveryJournal::discardDirectory(directory, &session->durableFiles));
        session->journal->setRecorder(nullptr);
        recovery.reset();
        CHECK(RecoveryJournal::discardDirectory(directory, &session->durableFiles));
        CHECK_FALSE(QFileInfo::exists(directory));
    }
}

// ==========================================================================================
// A-63 -- damage stops replay where it is, and only there
// ==========================================================================================

TEST_CASE("recovery replay ignores and reports a torn trailing line", "[recovery][damage]")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString dir = plantJournal(root, "torn",
        "{\"format\":1}\n" + rotateLine(0) + "\n" + rotateLine(1) + "\n" + rotateLine(2) + "\n" + "{\"op\":\"ru");
    auto fresh = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(fresh);
    auto report = RecoveryJournal::replay(dir, *fresh->journal);
    REQUIRE(report);
    CHECK(report.value().tornTrailingLine);
    CHECK(report.value().stepsApplied == 3);
    CHECK_FALSE(report.value().stoppedAtStep.has_value());
    CHECK(fresh->journal->historySize() == 3);
}

TEST_CASE("recovery replay stops at a malformed step and keeps the steps before it", "[recovery][damage]")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString dir = plantJournal(root, "malformed",
        "{\"format\":1}\n" + rotateLine(0) + "\n" + "not json at all\n" + rotateLine(1) + "\n" + rotateLine(2) + "\n" + rotateLine(0) + "\n");
    auto fresh = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(fresh);
    auto report = RecoveryJournal::replay(dir, *fresh->journal);
    REQUIRE(report);
    CHECK(report.value().stepsApplied == 1);
    REQUIRE(report.value().stoppedAtStep.has_value());
    CHECK(*report.value().stoppedAtStep == 2);
    CHECK(keyOf(report.value().stopKey) == "error.recovery.malformedLine");
    CHECK(static_cast<int>(fresh->journal->document().pages().rotation(0)) == 90);
    CHECK(static_cast<int>(fresh->journal->document().pages().rotation(1)) == 0);
    CHECK(report.value().appliedLines == rotateLine(0) + "\n");
}

TEST_CASE("recovery replay drops an unterminated trailing group and reports it", "[recovery][damage]")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString dir = plantJournal(root, "unterminated",
        "{\"format\":1}\n" + rotateLine(0) + "\n" + groupBegin() + "\n" + rotateLine(1) + "\n" + rotateLine(2) + "\n");
    auto fresh = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(fresh);
    auto report = RecoveryJournal::replay(dir, *fresh->journal);
    REQUIRE(report);
    CHECK(report.value().droppedUnterminatedGroup);
    CHECK(report.value().stepsApplied == 1);
    CHECK_FALSE(report.value().stoppedAtStep.has_value());
    CHECK(fresh->journal->historySize() == 1);
    CHECK(static_cast<int>(fresh->journal->document().pages().rotation(1)) == 0);   // the group left no trace
    CHECK(static_cast<int>(fresh->journal->document().pages().rotation(2)) == 0);
    CHECK_FALSE(fresh->journal->isGroupOpen());
}

TEST_CASE("recovery replay abandons a group that fails part-way and keeps the group before it", "[recovery][damage]")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString dir = plantJournal(root, "group-fails",
        "{\"format\":1}\n"
        + groupBegin() + "\n" + rotateLine(0) + "\n" + groupEnd() + "\n"
        + groupBegin() + "\n" + rotateLine(1) + "\n" + rotateLine(99) + "\n" + groupEnd() + "\n"
        + groupBegin() + "\n" + rotateLine(2) + "\n" + groupEnd() + "\n");
    auto fresh = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(fresh);
    auto report = RecoveryJournal::replay(dir, *fresh->journal);
    REQUIRE(report);
    CHECK(report.value().stepsApplied == 1);
    REQUIRE(report.value().stoppedAtStep.has_value());
    CHECK(*report.value().stoppedAtStep == 2);
    CHECK_FALSE(keyOf(report.value().stopKey).empty());
    CHECK(fresh->journal->historySize() == 1);
    CHECK(static_cast<int>(fresh->journal->document().pages().rotation(0)) == 90);
    CHECK(static_cast<int>(fresh->journal->document().pages().rotation(1)) == 0);   // group 2 wholly absent
    CHECK(static_cast<int>(fresh->journal->document().pages().rotation(2)) == 0);
    CHECK_FALSE(fresh->journal->canRedo());
}

// ==========================================================================================
// A-64 -- an undo after a save is recovered
// ==========================================================================================

TEST_CASE("recovery replays an undo after a save as the command's revert", "[recovery]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = RecoveryJournal::open(root.path(), session->path, session->journal->document().id(), &session->durableFiles);
    session->journal->setRecorder(recovery.get());

    REQUIRE(session->rotate(0));
    auto saved = session->coordinator->save(*session->journal, session->path);
    REQUIRE(saved);
    REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);
    REQUIRE(session->journal->undo());                          // page 1 back to 0 -- in the saved file it is 90
    const QByteArray expected = session->journal->document().modelHash();

    session->journal->setRecorder(nullptr);
    const QString directory = recovery->directory();
    recovery.reset();                                           // the owner is gone
    const auto candidates = RecoveryJournal::discover(root.path());
    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front().sourceMatches);                    // the source is the SAVED file

    // Open the SAVED file, as recovery would: the journal's lines start from it.
    tests::CosBackend backend;
    auto handle = backend.open(session->path, nullptr);
    REQUIRE(handle);
    core::Document document;
    REQUIRE(document.hydrate(backend, handle.value()));
    REQUIRE(static_cast<int>(document.pages().rotation(0)) == 90);
    core::CommandJournal replayed(std::move(document), backend.mutationSink(handle.value()));

    auto report = RecoveryJournal::replay(directory, replayed);
    REQUIRE(report);
    CHECK(report.value().stepsApplied == 1);
    CHECK_FALSE(report.value().stoppedAtStep.has_value());
    CHECK(static_cast<int>(replayed.document().pages().rotation(0)) == 0);   // UN-rotated relative to the saved file
    CHECK(replayed.document().modelHash() == expected);
}

// ==========================================================================================
// A-65 -- a live journal is never offered (the in-process half; a killed child is platform's)
// ==========================================================================================

TEST_CASE("recovery gives two journals of one file their own directories and offers only the one whose owner is gone", "[recovery]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto first = RecoveryJournal::open(root.path(), session->path, session->journal->document().id(), &session->durableFiles);
    auto second = RecoveryJournal::open(root.path(), session->path, session->journal->document().id(), &session->durableFiles);
    REQUIRE(first->isRecoverable());
    REQUIRE(second->isRecoverable());
    CHECK(first->directory() != second->directory());
    CHECK(RecoveryJournal::discover(root.path()).empty());

    const QString gone = QDir(second->directory()).absolutePath();
    second.reset();
    const auto candidates = RecoveryJournal::discover(root.path());
    REQUIRE(candidates.size() == 1);
    CHECK(candidates.front().directory == gone);
}

// ==========================================================================================
// A-66 -- an unwritable recovery directory degrades to memory, once
// ==========================================================================================

TEST_CASE("recovery degrades to memory once when its directory cannot be written and editing goes on", "[recovery][degrade]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    WarningCounter warnings;

    SECTION("a root that is a FILE, so no directory can be made beneath it")
    {
        const QString file = root.filePath("not-a-directory");
        { QFile f(file); REQUIRE(f.open(QIODevice::WriteOnly)); f.write("x"); }
        auto recovery = RecoveryJournal::open(file, session->path, session->journal->document().id(), &session->durableFiles);
        session->journal->setRecorder(recovery.get());
        for (int i = 0; i < 10; ++i)
            REQUIRE(session->rotate(i % 3));
        CHECK_FALSE(recovery->isRecoverable());
        CHECK(keyOf(recovery->unrecoverableKey()) == "error.recovery.unavailable");
        CHECK(warnings.lines.size() == 1);
        CHECK(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot) == QStringList{"not-a-directory"});
        session->journal->setRecorder(nullptr);
    }
    SECTION("no IDurableFiles, and the log says why")
    {
        auto recovery = RecoveryJournal::open(root.path(), session->path, session->journal->document().id(), nullptr);
        session->journal->setRecorder(recovery.get());
        for (int i = 0; i < 10; ++i)
            REQUIRE(session->rotate(i % 3));
        CHECK(keyOf(recovery->unrecoverableKey()) == "error.recovery.noDurableFiles");
        REQUIRE(warnings.lines.size() == 1);
        CHECK(warnings.lines.front().find("error.recovery.noDurableFiles") != std::string::npos);
        CHECK(QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
        session->journal->setRecorder(nullptr);
    }
    SECTION("a command that cannot be serialized ends recording, once, and editing goes on")
    {
        auto recovery = RecoveryJournal::open(root.path(), session->path, session->journal->document().id(), &session->durableFiles);
        session->journal->setRecorder(recovery.get());
        REQUIRE(session->rotate(0));
        REQUIRE(session->journal->run(std::make_unique<UnserializableRotate>(1)));
        for (int i = 0; i < 8; ++i)
            REQUIRE(session->rotate(2));
        CHECK(keyOf(recovery->unrecoverableKey()) == "error.recovery.notRecoverable");
        CHECK(warnings.lines.size() == 1);
        CHECK(operationsIn(recovery->directory()) == QStringList{"run"});   // nothing after the unserializable one
        session->journal->setRecorder(nullptr);
    }
}

// ==========================================================================================
// A-67 -- recover to a new file when the source has changed
// ==========================================================================================

TEST_CASE("recovery offers a changed source as a new file and a rotation of a missing page stops there", "[recovery]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = RecoveryJournal::open(root.path(), session->path, session->journal->document().id(), &session->durableFiles);
    session->journal->setRecorder(recovery.get());
    REQUIRE(session->rotate(0));
    REQUIRE(session->rotate(1));
    REQUIRE(session->rotate(2));
    session->journal->setRecorder(nullptr);
    recovery.reset();

    // The source is replaced by a TWO-page file.
    QFile::remove(session->path);
    REQUIRE(QFile::copy(tests::corpusFile("ordinary/text-letter.pdf"), session->path));
    const auto candidates = RecoveryJournal::discover(root.path());
    REQUIRE(candidates.size() == 1);
    CHECK_FALSE(candidates.front().sourceMatches);             // -> "recover to a new file", never a replay under its path

    auto current = tests::openCopy("ordinary/text-letter.pdf");
    REQUIRE(current);
    auto report = RecoveryJournal::replay(candidates.front().directory, *current->journal);
    REQUIRE(report);
    CHECK(report.value().stepsApplied == 2);
    REQUIRE(report.value().stoppedAtStep.has_value());
    CHECK(*report.value().stoppedAtStep == 3);
    CHECK(keyOf(report.value().stopKey) == "error.command.failed");
}

// ==========================================================================================
// A-70 -- hostile journals (and under portable-asan)
// ==========================================================================================

TEST_CASE("recovery survives hostile journals and meta files", "[recovery][hostile]")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto fresh = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(fresh);

    SECTION("binary junk in place of the journal")
    {
        QByteArray junk;
        for (int i = 0; i < 4096; ++i)
            junk.append(static_cast<char>((i * 131) % 256));
        auto report = RecoveryJournal::replay(plantJournal(root, "junk", junk), *fresh->journal);
        REQUIRE_FALSE(report);
        CHECK(keyOf(report.error().messageKey) == "error.recovery.badJournal");
    }
    SECTION("a line of ten megabytes")
    {
        QByteArray huge = "{\"op\":\"run\",\"cmd\":{\"type\":\"page.rotate\",\"page\":\"";
        huge.append(QByteArray(10 * 1024 * 1024, '9'));
        huge.append("\",\"degrees\":90}}");
        auto report = RecoveryJournal::replay(plantJournal(root, "huge", "{\"format\":1}\n" + rotateLine(0) + "\n" + huge + "\n"), *fresh->journal);
        REQUIRE(report);
        CHECK(report.value().stepsApplied == 1);
        CHECK(report.value().stoppedAtStep == 2);
    }
    SECTION("JSON nested a hundred thousand deep")
    {
        QByteArray deep = "{\"op\":\"run\",\"cmd\":";
        deep.append(QByteArray(100000, '['));
        auto report = RecoveryJournal::replay(plantJournal(root, "deep", "{\"format\":1}\n" + deep + "\n"), *fresh->journal);
        REQUIRE(report);
        CHECK(report.value().stoppedAtStep == 1);
        CHECK(fresh->journal->historySize() == 0);
    }
    SECTION("an undo naming ten thousand commands of an unknown type")
    {
        QByteArray line = "{\"op\":\"undo\",\"name\":{\"key\":\"x\",\"args\":[]},\"cmds\":[";
        for (int i = 0; i < 10000; ++i)
            line.append(i ? ",{\"type\":\"nope\"}" : "{\"type\":\"nope\"}");
        line.append("]}");
        auto report = RecoveryJournal::replay(plantJournal(root, "many", "{\"format\":1}\n" + line + "\n"), *fresh->journal);
        REQUIRE(report);
        CHECK(report.value().stoppedAtStep == 1);
        CHECK(keyOf(report.value().stopKey) == "error.command.unknownType");
    }
    SECTION("meta files that are junk, and one naming a missing source")
    {
        const QString junkDir = plantJournal(root, "badmeta", "{\"format\":1}\n");
        { QFile f(QDir(junkDir).filePath("meta.json")); REQUIRE(f.open(QIODevice::WriteOnly)); f.write("\x01\x02{{{"); }
        const QString missingDir = plantJournal(root, "missing", "{\"format\":1}\n" + rotateLine(0) + "\n");
        {
            QFile f(QDir(missingDir).filePath("meta.json"));
            REQUIRE(f.open(QIODevice::WriteOnly));
            f.write(R"({"format":1,"source":"/nowhere/at/all.pdf","size":10,"modified":0,"sha256":"00"})");
        }
        const auto candidates = RecoveryJournal::discover(root.path());
        REQUIRE(candidates.size() == 2);
        for (const auto& candidate : candidates) {
            if (candidate.directory.endsWith("badmeta"))
                CHECK(keyOf(candidate.problemKey) == "error.recovery.badMeta");
            else
                CHECK((keyOf(candidate.problemKey).empty() && !candidate.sourceMatches && candidate.operationLines == 1));
        }
    }
}

TEST_CASE("recovery refuses to replay into a journal that is itself recording", "[recovery]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = RecoveryJournal::open(root.path(), session->path, session->journal->document().id(), &session->durableFiles);
    session->journal->setRecorder(recovery.get());
    auto report = RecoveryJournal::replay(recovery->directory(), *session->journal);
    REQUIRE_FALSE(report);
    CHECK(keyOf(report.error().messageKey) == "error.recovery.replayWhileRecording");
    session->journal->setRecorder(nullptr);
}

// ==========================================================================================
// Stage 3 T11 -- an untitled document's meta, and a directory name never reused
// ==========================================================================================

TEST_CASE("recovery keeps an untitled document untitled in its meta until it is saved", "[recovery]")
{
    // A document recovered INTO A NEW FILE has its source's bytes and no file of its own. Crashing again must
    // offer it as a new file again -- never as a replay under the source's path, where a Save would write
    // into a file the user chose not to touch.
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = RecoveryJournal::open(root.path(), session->path, session->journal->document().id(), &session->durableFiles,
                                          {}, /*untitled=*/true);
    session->journal->setRecorder(recovery.get());
    const QString directory = recovery->directory();
    REQUIRE(session->rotate(0));

    SECTION("dropped unsaved, it is discovered untitled with its source still matching")
    {
        session->journal->setRecorder(nullptr);
        recovery.reset();
        const auto candidates = RecoveryJournal::discover(root.path());
        REQUIRE(candidates.size() == 1);
        CHECK(candidates.front().sourceMatches);        // the source is untouched ...
        CHECK(candidates.front().meta.untitled);        // ... and the document is still not that file
    }
    SECTION("saved as a file of its own, it is no longer untitled")
    {
        const QString copy = session->directory.filePath("named.pdf");
        auto saved = session->coordinator->saveAs(*session->journal, copy);
        REQUIRE(saved);
        REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);
        REQUIRE(session->rotate(1));
        session->journal->setRecorder(nullptr);
        recovery.reset();
        const auto candidates = RecoveryJournal::discover(root.path());
        REQUIRE(candidates.size() == 1);
        CHECK(candidates.front().directory == QDir(directory).absolutePath());
        CHECK(candidates.front().meta.sourcePath == copy);
        CHECK_FALSE(candidates.front().meta.untitled);
        CHECK(candidates.front().sourceMatches);
    }
    SECTION("a titled journal's meta carries no untitled key at all")
    {
        auto titled = RecoveryJournal::open(root.path(), session->path, session->journal->document().id(), &session->durableFiles);
        const QJsonObject meta = QJsonDocument::fromJson(tests::readAllBytes(QDir(titled->directory()).filePath("meta.json"))).object();
        CHECK_FALSE(meta.contains("untitled"));
        CHECK(meta.keys().size() == 5);
    }
}

TEST_CASE("recovery never opens a journal in a directory that is already on disk", "[recovery]")
{
    // Windows reuses process ids. A session whose pid and counter matched a crashed session's would have
    // written over the journal the recovery prompt had not been answered about yet. The next name this
    // process would choose is planted first -- as a crashed session's directory, with its meta and a line.
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const core::DocumentId id = session->journal->document().id();
    auto first = RecoveryJournal::open(root.path(), session->path, id, &session->durableFiles);
    const QString firstName = QFileInfo(first->directory()).fileName();
    const int counter = firstName.section('-', -1).toInt();
    const QString planted = plantJournal(root, firstName.section('-', 0, -2) + "-" + QString::number(counter + 1),
                                         "{\"format\":1}\n" + rotateLine(0) + "\n");
    {
        QFile meta(QDir(planted).filePath("meta.json"));
        REQUIRE(meta.open(QIODevice::WriteOnly));
        meta.write(R"({"format":1,"source":"elsewhere.pdf","size":1,"modified":0,"sha256":"00"})");
    }
    const QByteArray plantedBytes = tests::readAllBytes(QDir(planted).filePath("journal.ndjson"));

    auto second = RecoveryJournal::open(root.path(), session->path, id, &session->durableFiles);
    REQUIRE(second->isRecoverable());
    CHECK(QDir(second->directory()).absolutePath() != QDir(planted).absolutePath());
    CHECK(tests::readAllBytes(QDir(planted).filePath("journal.ndjson")) == plantedBytes);
    CHECK(tests::readAllBytes(QDir(planted).filePath("meta.json")).contains("elsewhere.pdf"));
}

// ==========================================================================================
// Stage 7 guide I-11 -- an undo, after a save, of a page command from before it
// ==========================================================================================
// THE ISSUE'S OWN SCENARIO: `assembly/refs-every-kind.pdf` -- whose pages every kind of reference names -- page 2
// deleted, saved, the delete UNDONE, and a later edit; the session then gone. A page delete's undo puts back the pages
// the sink kept under the command's key, which a replaying process never held, so the replay used to stop at the undo
// and lose it and every step after (the CONTROL below, still so without a checkpoint writer). With one, the undo is a
// checkpoint, and the recovery is the live document exactly -- model hash, outline and links included -- and saves back
// over the user's file as that file.

TEST_CASE("recovery checkpoints an undo past the last save of a page delete and recovers it exactly",
          "[recovery][S7-I-11]")
{
    for (const bool checkpoints : {true, false}) {
        INFO((checkpoints ? "with checkpoints" : "the control: no checkpoint writer, I-11 as it was"));
        auto session = tests::openCopy("assembly/refs-every-kind.pdf");
        REQUIRE(session);
        const int pages = session->journal->document().pages().count();
        REQUIRE(pages == 12);
        QTemporaryDir root;
        REQUIRE(root.isValid());
        auto recovery = recording(*session, root.path(), checkpoints);
        const QString directory = recovery->directory();

        REQUIRE(session->journal->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{1})));
        auto saved = session->coordinator->save(*session->journal, session->path);
        REQUIRE(saved);
        REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);
        REQUIRE(session->journal->undo());                         // the deleted page back, after the save
        REQUIRE(session->journal->document().pages().count() == pages);
        REQUIRE(session->rotate(0));                               // and an edit after it, which a stop would lose
        const QByteArray expected = session->journal->document().modelHash();

        if (checkpoints) {
            CHECK(operationsIn(directory) == QStringList{"base", "run"});
            CHECK(checkpointFiles(directory) == QStringList{"checkpoint-1.pdf"});
        } else {
            CHECK(operationsIn(directory) == QStringList{"undo", "run"});
            CHECK(checkpointFiles(directory).isEmpty());
        }

        session->journal->setRecorder(nullptr);
        recovery.reset();                                          // the owner is gone
        const auto candidates = RecoveryJournal::discover(root.path());
        REQUIRE(candidates.size() == 1);
        const core::RecoveryCandidate& candidate = candidates.front();
        CHECK(candidate.sourceMatches);
        CHECK(keyOf(candidate.problemKey).empty());
        CHECK(candidate.operationLines == 2);
        auto recovered = recoverAsTheWindowDoes(candidate);

        if (!checkpoints) {
            // I-11, REPRODUCED: the undo refused -- the key unknown to this process -- and the edit after it lost.
            REQUIRE(recovered->report.stoppedAtStep.has_value());
            CHECK(*recovered->report.stoppedAtStep == 1);
            CHECK(recovered->report.stepsApplied == 0);
            CHECK(recovered->document().pages().count() == pages - 1);
            CHECK(recovered->document().modelHash() != expected);
            continue;
        }

        CHECK(QFileInfo(candidate.checkpointPath).fileName() == QStringLiteral("checkpoint-1.pdf"));
        CHECK(recovered->report.fromCheckpoint);
        CHECK_FALSE(recovered->report.stoppedAtStep.has_value());
        CHECK(recovered->report.stepsApplied == 1);               // the rotation; the checkpoint is not a step
        CHECK(recovered->document().pages().count() == pages);
        CHECK(static_cast<int>(recovered->document().pages().rotation(0)) == 90);
        CHECK(recovered->document().modelHash() == expected);     // pages, outline, annotations and links
        CHECK(recovered->document().outline().digest() == session->journal->document().outline().digest());
        // AS THE USER'S FILE: its identity, so Save checks that file and writes over it (D3-16).
        CHECK(recovered->document().id().path == core::DocumentId::normalizePath(session->path));
        CHECK(recovered->document().id().contentHash == candidate.meta.contentHash);
        tests::FakeDurableFiles durable;
        core::SaveCoordinator coordinator(recovered->backend, &durable);
        auto resaved = coordinator.save(*recovered->journal, session->path);
        REQUIRE(resaved);
        CHECK(resaved.value().kind == core::SaveOutcome::Kind::Saved);
        tests::CosBackend reread;
        auto reopened = reread.open(session->path, nullptr);
        REQUIRE(reopened);
        core::Document again;
        REQUIRE(again.hydrate(reread, reopened.value()));
        CHECK(again.pages().count() == pages);
        CHECK(static_cast<int>(again.pages().rotation(0)) == 90);
        CHECK(again.outline().digest() == recovered->document().outline().digest());
    }
}

TEST_CASE("recovery checkpoints an undo past a save that rewrote and one past a renumber", "[recovery][S7-I-11]")
{
    SECTION("a page delete, saved as a rewrite (D7-29: its objects then only in the session's memory)")
    {
        auto session = tests::openCopy("assembly/refs-every-kind.pdf");
        REQUIRE(session);
        QTemporaryDir root;
        REQUIRE(root.isValid());
        auto recovery = recording(*session, root.path(), true);
        REQUIRE(session->journal->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{1, 4})));
        const QString rewritten = session->directory.filePath("rewritten.pdf");
        auto saved = session->coordinator->saveAs(*session->journal, rewritten);
        REQUIRE(saved);
        REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);
        REQUIRE(session->backend.lastSaveForm() == core::SaveForm::Rewrite);
        REQUIRE(session->journal->undo());
        const QByteArray expected = session->journal->document().modelHash();
        session->journal->setRecorder(nullptr);
        recovery.reset();
        const auto candidates = RecoveryJournal::discover(root.path());
        REQUIRE(candidates.size() == 1);
        CHECK(candidates.front().meta.sourcePath == rewritten);
        auto recovered = recoverAsTheWindowDoes(candidates.front());
        CHECK(recovered->report.fromCheckpoint);
        CHECK_FALSE(recovered->report.stoppedAtStep.has_value());
        CHECK(recovered->document().pages().count() == 12);
        CHECK(recovered->document().modelHash() == expected);
    }
    SECTION("a renumber, whose undo puts back the numbering the sink kept")
    {
        auto session = tests::openCopy("assembly/refs-every-kind.pdf");
        REQUIRE(session);
        QTemporaryDir root;
        REQUIRE(root.isValid());
        auto recovery = recording(*session, root.path(), true);
        const QByteArray opened = session->journal->document().modelHash();
        REQUIRE(session->journal->run(std::make_unique<core::RenumberPagesCommand>(
            std::vector<int>{0, 1, 2}, core::PageLabelStyle::LettersUpper, QStringLiteral("X-"), 1)));
        auto saved = session->coordinator->save(*session->journal, session->path);
        REQUIRE(saved);
        REQUIRE(session->journal->undo());
        CHECK(session->journal->document().modelHash() == opened);
        session->journal->setRecorder(nullptr);
        recovery.reset();
        const auto candidates = RecoveryJournal::discover(root.path());
        REQUIRE(candidates.size() == 1);
        auto recovered = recoverAsTheWindowDoes(candidates.front());
        CHECK(recovered->report.fromCheckpoint);
        CHECK_FALSE(recovered->report.stoppedAtStep.has_value());
        CHECK(recovered->document().modelHash() == opened);
    }
}

TEST_CASE("recovery keeps one checkpoint, drops it at a save, and carries it into the next journal", "[recovery][S7-I-11]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = recording(*session, root.path(), true);
    const QString directory = recovery->directory();

    REQUIRE(session->rotate(0));
    REQUIRE(session->rotate(1));
    auto saved = session->coordinator->save(*session->journal, session->path);
    REQUIRE(saved);
    REQUIRE(session->journal->undo());                            // past the save: a checkpoint
    CHECK(checkpointFiles(directory) == QStringList{"checkpoint-1.pdf"});
    REQUIRE(session->journal->undo());                            // past the new base: another, the first gone
    CHECK(checkpointFiles(directory) == QStringList{"checkpoint-2.pdf"});
    CHECK(operationsIn(directory) == QStringList{"base"});
    REQUIRE(session->journal->redo());                            // an ordinary line after it
    CHECK(operationsIn(directory) == QStringList{"base", "redo"});
    const QByteArray expected = session->journal->document().modelHash();

    SECTION("a save removes it: the saved file is the base")
    {
        REQUIRE(session->coordinator->save(*session->journal, session->path));
        CHECK(checkpointFiles(directory).isEmpty());
        CHECK(tests::readAllBytes(QDir(directory).filePath("journal.ndjson")) == QByteArray("{\"format\":1}\n"));
    }
    SECTION("a recovery's next journal starts from a copy of it, and a second crash recovers the same")
    {
        session->journal->setRecorder(nullptr);
        recovery.reset();
        auto candidates = RecoveryJournal::discover(root.path());
        REQUIRE(candidates.size() == 1);
        auto first = recoverAsTheWindowDoes(candidates.front());
        REQUIRE(first->document().modelHash() == expected);
        // As `DocumentWindow` seeds it: the applied lines -- the base line first -- and the old directory's files.
        auto next = RecoveryJournal::open(root.path(), session->path, first->document().id(), &session->durableFiles,
                                          first->report.appliedLines, false, candidates.front().directory);
        REQUIRE(next->isRecoverable());
        const QString nextDirectory = next->directory();
        CHECK(checkpointFiles(nextDirectory) == QStringList{"checkpoint-2.pdf"});
        REQUIRE(RecoveryJournal::discardDirectory(candidates.front().directory, &session->durableFiles));
        next.reset();
        candidates = RecoveryJournal::discover(root.path());
        REQUIRE(candidates.size() == 1);
        CHECK(candidates.front().directory == QDir(nextDirectory).absolutePath());
        CHECK(QFileInfo(candidates.front().checkpointPath).fileName() == QStringLiteral("checkpoint-2.pdf"));
        auto second = recoverAsTheWindowDoes(candidates.front());
        CHECK_FALSE(second->report.stoppedAtStep.has_value());
        CHECK(second->document().modelHash() == expected);
    }
    SECTION("discarding removes it with the directory")
    {
        session->journal->setRecorder(nullptr);
        recovery.reset();
        REQUIRE(RecoveryJournal::discardDirectory(directory, &session->durableFiles));
        CHECK_FALSE(QFileInfo::exists(directory));
    }
}

TEST_CASE("recovery refuses a checkpoint that is damaged, misnamed or not first", "[recovery][S7-I-11][hostile]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    auto recovery = recording(*session, root.path(), true);
    const QString directory = recovery->directory();
    REQUIRE(session->rotate(0));
    REQUIRE(session->coordinator->save(*session->journal, session->path));
    REQUIRE(session->journal->undo());
    session->journal->setRecorder(nullptr);
    recovery.reset();
    const QString checkpoint = QDir(directory).filePath("checkpoint-1.pdf");
    const QByteArray good = tests::readAllBytes(checkpoint);
    REQUIRE_FALSE(good.isEmpty());
    REQUIRE(RecoveryJournal::checkpointOf(directory));
    const QString journalPath = QDir(directory).filePath("journal.ndjson");
    const QByteArray journal = tests::readAllBytes(journalPath);
    const auto write = [](const QString& path, const QByteArray& bytes) {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        REQUIRE(file.write(bytes) == bytes.size());
    };
    const auto problem = [&root]() {
        const auto candidates = RecoveryJournal::discover(root.path());
        REQUIRE(candidates.size() == 1);
        return keyOf(candidates.front().problemKey);
    };

    SECTION("one byte of it changed")
    {
        QByteArray bad = good;
        bad[bad.size() / 2] = static_cast<char>(bad[bad.size() / 2] ^ 0x20);
        write(checkpoint, bad);
        CHECK(problem() == "error.recovery.checkpoint");
    }
    SECTION("missing")
    {
        REQUIRE(QFile::remove(checkpoint));
        CHECK(problem() == "error.recovery.checkpoint");
    }
    SECTION("a base line naming a file outside the directory")
    {
        write(journalPath, QByteArray(journal).replace("checkpoint-1.pdf", "../checkpoint-1.pdf"));
        CHECK(problem() == "error.recovery.checkpoint");
    }
    SECTION("a base line after an operation: malformed where it stands")
    {
        write(journalPath, "{\"format\":1}\n" + rotateLine(1) + "\n" + journal.split('\n').at(1) + "\n");
        CHECK(problem().empty());                                 // the first line is not a base: none is named
        auto fresh = tests::openCopy("ordinary/text-a4.pdf");
        REQUIRE(fresh);
        auto report = RecoveryJournal::replay(directory, *fresh->journal);
        REQUIRE(report);
        CHECK(report.value().stepsApplied == 1);
        REQUIRE(report.value().stoppedAtStep.has_value());
        CHECK(keyOf(report.value().stopKey) == "error.recovery.malformedLine");
    }
}
