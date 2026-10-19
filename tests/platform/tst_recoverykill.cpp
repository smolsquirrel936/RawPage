// tst_recoverykill -- TerminateProcess at random instants, then what survived. Stage 3 T9, guide cases A-65's
// child half, A-68 and A-69 (`Stage3-Mutation-Testing-Guide.md` §5.10), decisions D3-4 and D3-11.
//
// The child is `rawpage_killprobe` (tests/platform/killprobe/main.cpp). `QProcess::kill()` is
// `TerminateProcess` on Windows: no destructor, no crash handler, no flush on the way out -- the harsher
// condition, and the one D3-11's claim is written against.
//
// EACH CASE MUST PROVE IT REACHED THE INSTANTS IT CLAIMS (guide rule 4, Stage 2's I-41). The probe reports
// its phase before each step, the harness classifies where every kill landed, and a case whose twenty
// kills never landed in a required window FAILS AS UNMEASURED rather than passing. The windows are held
// open by the probe's test-only phase observers, which sleep; the header of the probe says why.
//
// A RANDOM DELAY IS NOT A RANDOM INSTANT UNLESS THE SLEEP IS OFF THE TICK (`Stage4-ViewingSurface-Testing-Guide.md`
// I-30, A-105). Until 2026-09-27 the harness slept with `QThread::msleep`, which wakes on Windows' 15.6 ms
// tick like the probe's own dwells, so every kill arrived as a dwell ended: 1 kill in 4 reached the
// window before the flush rather than 1 in 2, the floor below tripped by chance (about 0.4 % of runs), and
// a kill almost never reached the inside of a save's replace -- which is why A-69 had never seen a killed
// replace leave NO FILE at the target (I-31, A-106). `sleepOffTheTick` is the fix for both.
//
// WHAT THIS DOES NOT COVER (convention 19): a power loss -- the OS survives every kill here, so the file
// cache is flushed by Windows whether or not FlushFileBuffers ran; a kill in the OS's own write path; and
// the product's composition (T11's recovery prompt replays through `ui/`). The replay here runs through
// `tests/unit/CosBackend.h`, the first-party reader, as tst_recovery's does.

#include <catch2/catch_test_macros.hpp>

#include "tests/platform/killprobe/AnnotationSteps.h"
#include "tests/unit/CosBackend.h"

#include "core/cmd/CommandJournal.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/SaveCoordinator.h"
#include "platform/win/WinDurableFiles.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QThread>

#include <windows.h>

#include <iostream>
#include <map>
#include <set>
#include <string>

using namespace rawpage;

namespace {

constexpr int kRuns = 20;
constexpr int kDwellMs = 15;

std::string shaOf(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex().toStdString();
}

std::wstring wide(const QString& path)
{
    const QString native = QDir::toNativeSeparators(path);
    return std::wstring(reinterpret_cast<const wchar_t*>(native.utf16()), static_cast<std::size_t>(native.size()));
}

// Starts the probe and waits for its "ready" line. The seed of the run's delays is printed so a failure
// can be re-run with the same instants.
std::unique_ptr<QProcess> startProbe(const QStringList& arguments)
{
    auto probe = std::make_unique<QProcess>();
    probe->setProcessChannelMode(QProcess::SeparateChannels);
    probe->start(QStringLiteral(RAWPAGE_KILLPROBE_PATH), arguments);
    REQUIRE(probe->waitForStarted(10000));
    QByteArray seen;
    while (!seen.contains("ready ")) {
        REQUIRE(probe->waitForReadyRead(10000));
        seen += probe->peek(1 << 20);
        if (probe->state() != QProcess::Running)
            FAIL("the probe exited before it was ready: " << probe->readAllStandardError().toStdString());
    }
    return probe;
}

// Sleeps `us` microseconds OFF the system timer tick. `QThread::msleep` is `Sleep`, which wakes only on
// Windows' default 15.6 ms tick -- and so do the probe's dwells, so a kill after an msleep was PHASE-LOCKED
// to the probe: it arrived as a dwell ended, and a random delay chose which tick rather than which instant
// (S4 guide I-30). A high-resolution waitable timer (Windows 10 1803+) wakes within about a millisecond of
// the time asked for, whatever the tick.
void sleepOffTheTick(qint64 us)
{
    const HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    REQUIRE(timer != nullptr);
    LARGE_INTEGER due;
    due.QuadPart = -us * 10;                                 // relative, in 100 ns units
    REQUIRE(SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE));
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}

// Kills it and returns every line it managed to write.
QList<QByteArray> killProbe(QProcess& probe, qint64 afterUs)
{
    sleepOffTheTick(afterUs);
    probe.kill();                                            // TerminateProcess
    REQUIRE(probe.waitForFinished(10000));
    QByteArray all = probe.readAllStandardOutput();
    QList<QByteArray> lines = all.split('\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    for (QByteArray& line : lines)
        line = line.trimmed();
    return lines;
}

int numberAfter(const QByteArray& line, const QByteArray& word)
{
    if (!line.startsWith(word + ' '))
        return -1;
    return line.mid(word.size() + 1).split(' ').first().toInt();
}

} // namespace

// ==========================================================================================
// A-68 -- kills during commands: every acknowledged command is recovered
// ==========================================================================================

TEST_CASE("recovery.kill_commands: twenty kills during commands recover every acknowledged command", "[recovery][kill]")
{
    const quint32 seed = QRandomGenerator::global()->generate();
    QRandomGenerator random(seed);
    std::cout << "kill_commands: seed " << seed << std::endl;

    std::map<std::string, int> landed;
    for (int run = 1; run <= kRuns; ++run) {
        INFO("run " << run << " of " << kRuns << ", seed " << seed);
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString document = dir.filePath(QStringLiteral("doc.pdf"));
        REQUIRE(QFile::copy(QString::fromStdString(RAWPAGE_CORPUS_DIR "/ordinary/text-a4.pdf"), document));
        const QString root = dir.filePath(QStringLiteral("recovery"));

        auto probe = startProbe({QStringLiteral("commands"), document, root, QString::number(kDwellMs)});
        if (run == 1) {
            // A-65's child half, first part: the probe is alive, so its journal is NOT offered.
            CHECK(core::RecoveryJournal::discover(root).empty());
        }
        const QList<QByteArray> lines = killProbe(*probe, 20000 + static_cast<qint64>(random.bounded(380000)));
        REQUIRE_FALSE(lines.isEmpty());

        int acknowledged = 0;
        for (const QByteArray& line : lines)
            acknowledged = std::max(acknowledged, numberAfter(line, "ack"));
        const QByteArray last = lines.last();
        const std::string where = last.startsWith("writing") ? "between apply and the flush"
                                  : last.startsWith("flushing") ? "inside the flush"
                                  : last.startsWith("apply") ? "inside apply"
                                                             : "between commands";
        ++landed[where];

        // The process is gone, so its lock is stale and its journal IS offered (A-65, second part).
        const auto candidates = core::RecoveryJournal::discover(root);
        REQUIRE(candidates.size() == 1);
        CHECK(candidates.front().sourceMatches);

        tests::CosBackend backend;
        auto handle = backend.open(document, nullptr);
        REQUIRE(handle);
        core::Document fresh;
        REQUIRE(fresh.hydrate(backend, handle.value()));
        core::CommandJournal journal(std::move(fresh), backend.mutationSink(handle.value()));
        auto report = core::RecoveryJournal::replay(candidates.front().directory, journal);
        REQUIRE(report);
        INFO("killed " << where << " after ack " << acknowledged << "; replayed " << report.value().stepsApplied
             << (report.value().tornTrailingLine ? " (a torn line reported)" : ""));
        CHECK_FALSE(report.value().stoppedAtStep.has_value());
        CHECK(report.value().stepsApplied >= acknowledged);          // nothing acknowledged is lost
        CHECK(report.value().stepsApplied <= acknowledged + 1);      // and at most the one still inside run()
    }

    std::cout << "kill_commands: where the " << kRuns << " kills landed --";
    for (const auto& [where, count] : landed)
        std::cout << " " << where << ": " << count << ";";
    std::cout << std::endl;
    // UNMEASURED IS A FAILURE: twenty kills that never reached these windows prove nothing about them.
    CHECK(landed["inside the flush"] >= 1);
    CHECK(landed["between apply and the flush"] >= 1);
}

// ==========================================================================================
// A-69 -- kills during saves: the target is never partial
// ==========================================================================================

TEST_CASE("recovery.kill_saves: twenty kills during saves leave the target at a whole revision with its stream", "[recovery][kill]")
{
    const quint32 seed = QRandomGenerator::global()->generate();
    QRandomGenerator random(seed);
    std::cout << "kill_saves: seed " << seed << std::endl;
    const QByteArray zone = "[ZoneTransfer]\r\nZoneId=3\r\n";

    std::map<std::string, int> landed;
    for (int run = 1; run <= kRuns; ++run) {
        INFO("run " << run << " of " << kRuns << ", seed " << seed);
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString target = dir.filePath(QStringLiteral("approval.pdf"));
        REQUIRE(QFile::copy(QString::fromStdString(RAWPAGE_CORPUS_DIR "/signed/approval.pdf"), target));
        QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        {
            const HANDLE stream = CreateFileW(wide(target + QStringLiteral(":Zone.Identifier")).c_str(), GENERIC_WRITE, 0,
                                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            REQUIRE(stream != INVALID_HANDLE_VALUE);
            DWORD written = 0;
            WriteFile(stream, zone.constData(), static_cast<DWORD>(zone.size()), &written, nullptr);
            CloseHandle(stream);
        }
        const std::string original = shaOf(target);

        auto probe = startProbe({QStringLiteral("saves"), target, QString::number(kDwellMs)});
        const qint64 probePid = probe->processId();
        const QList<QByteArray> lines = killProbe(*probe, 30000 + static_cast<qint64>(random.bounded(570000)));
        REQUIRE_FALSE(lines.isEmpty());

        // The hashes the target may legitimately have: the last acknowledged save's (or the original), and
        // the temporary file that was about to replace it if a replace had begun.
        std::set<std::string> allowed{original};
        int acknowledged = 0;
        for (const QByteArray& line : lines) {
            if (numberAfter(line, "ack") > 0) {
                acknowledged = numberAfter(line, "ack");
                allowed = {line.mid(line.indexOf("sha=") + 4).toStdString()};
            }
        }
        for (const QByteArray& line : lines)
            if (numberAfter(line, "replacing") == acknowledged + 1)
                allowed.insert(line.mid(line.indexOf("sha=") + 4).toStdString());

        const QByteArray last = lines.last();
        const std::string where = last.startsWith("written") ? "between writing the temporary file and the replace"
                                  : last.startsWith("replacing") ? "inside the replace"
                                  : last.startsWith("writing") ? "writing the temporary file"
                                                               : "between saves";
        ++landed[where];

        INFO("killed " << where << " after save " << acknowledged << "; the directory holds: "
             << QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).join(QStringLiteral(", ")).toStdString());
        REQUIRE(QFileInfo::exists(target));                           // a kill never leaves the path empty
        const std::string now = shaOf(target);
        CHECK(allowed.count(now) == 1);                              // a whole revision, never anything else
        std::string streamBytes;
        {
            const HANDLE read = CreateFileW(wide(target + QStringLiteral(":Zone.Identifier")).c_str(), GENERIC_READ,
                                            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            REQUIRE(read != INVALID_HANDLE_VALUE);
            char buffer[256];
            DWORD got = 0;
            ReadFile(read, buffer, sizeof buffer, &got, nullptr);
            CloseHandle(read);
            streamBytes.assign(buffer, got);
        }
        CHECK(streamBytes == zone.toStdString());                    // the stream survived every replace

        // A stray temporary file the killed probe left is removed by the next save (A-60).
        tests::CosBackend backend;
        platform::win::WinDurableFiles durableFiles;
        auto handle = backend.open(target, nullptr);
        REQUIRE(handle);
        core::Document document;
        REQUIRE(document.hydrate(backend, handle.value()));
        core::CommandJournal journal(std::move(document), backend.mutationSink(handle.value()));
        REQUIRE(journal.run(std::make_unique<core::RotatePageCommand>(0, 90)));
        core::SaveCoordinator coordinator(backend, &durableFiles);
        auto saved = coordinator.save(journal, target);
        REQUIRE(saved);
        CHECK(saved.value().kind == core::SaveOutcome::Kind::Saved);
        CHECK_FALSE(QFileInfo::exists(core::SaveCoordinator::temporaryPathFor(target, probePid)));
    }

    std::cout << "kill_saves: where the " << kRuns << " kills landed --";
    for (const auto& [where, count] : landed)
        std::cout << " " << where << ": " << count << ";";
    std::cout << std::endl;
    CHECK(landed["between writing the temporary file and the replace"] >= 1);
}

// ==========================================================================================
// Stage 6 guide A-107 -- a killed session recovers its annotations (Stage 6 T15, D6-23)
// ==========================================================================================
//
// The same twenty kills, over the annotation cycle of `killprobe/AnnotationSteps.h` -- an ink, a highlight, a
// move, a delete and a 200 KiB attachment that D6-23 puts in a SIDE FILE -- and the expectation is a SECOND
// RUN of the same steps in this process: the cycle is deterministic, so what step k left in the probe is what
// step k leaves here, compared by `Document::modelHash`, which covers every page's annotation listing and every
// document-level attachment (Stage 6 T3, T11).
//
// WHAT THIS DOES NOT COVER (convention 19): the product's composition -- the replay is `tests::CosBackend`'s, as
// A-68's is, and the product's own offer of a killed journal is `tst_recoveryproduct`'s (Stage 3 guide A-74);
// a session that ENDS CLEANLY offering nothing is the product's close path, and is that file's too.

namespace {

// The model after the first `steps` steps of the cycle, run here on a pristine copy of `corpusRelative`.
struct Expected {
    QByteArray modelHash;
    std::set<QString> names;          // the kill-* annotations listed
    std::set<QString> attachments;    // the document-level attachments' keys
};

Expected expectedAfter(const QString& corpusRelative, int steps, const QString& scratch)
{
    const QString copy = QDir(scratch).filePath(QStringLiteral("expected-%1.pdf").arg(steps));
    QFile::remove(copy);
    REQUIRE(QFile::copy(QString::fromStdString(RAWPAGE_CORPUS_DIR "/") + corpusRelative, copy));
    tests::CosBackend backend;
    auto handle = backend.open(copy, nullptr);
    REQUIRE(handle);
    core::Document document;
    REQUIRE(document.hydrate(backend, handle.value()));
    core::CommandJournal journal(std::move(document), backend.mutationSink(handle.value()));
    for (int k = 1; k <= steps; ++k)
        REQUIRE(journal.run(tests::killsteps::step(k, journal.document())));
    Expected expected;
    expected.modelHash = journal.document().modelHash();
    for (const core::Annotation& a : journal.document().annotations().all())
        if (a.nm.startsWith(QStringLiteral("kill-"))) expected.names.insert(a.nm);
    for (const core::DocumentAttachment& file : journal.document().attachments().files()) expected.attachments.insert(file.key);
    return expected;
}

int payloadSteps(int steps)
{
    int n = 0;
    for (int k = 1; k <= steps; ++k)
        if (tests::killsteps::isPayloadStep(k)) ++n;
    return n;
}

} // namespace

TEST_CASE("recovery.kill_annotations: twenty kills during annotation commands recover every acknowledged one and its payloads",
          "[recovery][kill][A-107]")
{
    const quint32 seed = QRandomGenerator::global()->generate();
    QRandomGenerator random(seed);
    std::cout << "kill_annotations: seed " << seed << std::endl;

    std::map<std::string, int> landed;
    int runsWithPayload = 0;
    int mostSteps = 0;
    for (int run = 1; run <= kRuns; ++run) {
        INFO("run " << run << " of " << kRuns << ", seed " << seed);
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString document = dir.filePath(QStringLiteral("doc.pdf"));
        REQUIRE(QFile::copy(QString::fromStdString(RAWPAGE_CORPUS_DIR "/ordinary/text-a4.pdf"), document));
        const QString root = dir.filePath(QStringLiteral("recovery"));

        auto probe = startProbe({QStringLiteral("annotations"), document, root, QString::number(kDwellMs)});
        const QList<QByteArray> lines = killProbe(*probe, 20000 + static_cast<qint64>(random.bounded(380000)));
        REQUIRE_FALSE(lines.isEmpty());

        int acknowledged = 0;
        for (const QByteArray& line : lines)
            acknowledged = std::max(acknowledged, numberAfter(line, "ack"));
        const QByteArray last = lines.last();
        const std::string where = last.startsWith("writing") ? "between apply and the flush"
                                  : last.startsWith("flushing") ? "inside the flush"
                                  : last.startsWith("apply") ? "inside apply"
                                                             : "between commands";
        ++landed[where];

        const auto candidates = core::RecoveryJournal::discover(root);
        REQUIRE(candidates.size() == 1);
        CHECK(candidates.front().sourceMatches);

        tests::CosBackend backend;
        auto handle = backend.open(document, nullptr);
        REQUIRE(handle);
        core::Document fresh;
        REQUIRE(fresh.hydrate(backend, handle.value()));
        core::CommandJournal journal(std::move(fresh), backend.mutationSink(handle.value()));
        auto report = core::RecoveryJournal::replay(candidates.front().directory, journal);
        REQUIRE(report);
        const int steps = report.value().stepsApplied;
        INFO("killed " << where << " after ack " << acknowledged << "; replayed " << steps
             << (report.value().tornTrailingLine ? " (a torn line reported)" : ""));
        CHECK_FALSE(report.value().stoppedAtStep.has_value());
        CHECK(steps >= acknowledged);          // nothing acknowledged is lost
        CHECK(steps <= acknowledged + 1);      // and at most the one still inside run()
        mostSteps = std::max(mostSteps, steps);

        // THE STORE EQUALS THE JOURNAL'S LAST LINE: the replayed model is the one the same steps make here.
        const Expected expected = expectedAfter(QStringLiteral("ordinary/text-a4.pdf"), steps, dir.path());
        CHECK(journal.document().modelHash().toHex().toStdString() == expected.modelHash.toHex().toStdString());

        // THE SIDE FILES: one per payload replayed -- and one more only when the kill fell between a side file's
        // write and the line that names it, which D6-23 writes in that order.
        const int payloads = payloadSteps(steps);
        const int sides = static_cast<int>(
            QDir(candidates.front().directory).entryList(QStringList{QStringLiteral("*.side")}, QDir::Files).size());
        CHECK(sides >= payloads);
        CHECK(sides <= payloads + 1);
        // And each payload replayed is its bytes, from the side file, byte for byte (compared as SHA-256 --
        // CLAUDE.md's reporter trap on two QByteArrays).
        for (int k = 1; k <= steps; ++k) {
            if (!tests::killsteps::isPayloadStep(k)) continue;
            const auto bytes = backend.attachmentData(handle.value(),
                                                      core::AttachmentRef::ofDocument(QStringLiteral("payload-%1.bin").arg(k)));
            REQUIRE(bytes);
            CHECK(QCryptographicHash::hash(bytes.value(), QCryptographicHash::Sha256).toHex().toStdString()
                  == QCryptographicHash::hash(tests::killsteps::payload(k), QCryptographicHash::Sha256).toHex().toStdString());
        }
        if (payloads > 0) ++runsWithPayload;
    }

    std::cout << "kill_annotations: where the " << kRuns << " kills landed --";
    for (const auto& [where, count] : landed)
        std::cout << " " << where << ": " << count << ";";
    std::cout << " runs replaying a side file: " << runsWithPayload << "; most steps replayed: " << mostSteps << std::endl;
    // UNMEASURED IS A FAILURE, as A-68's: the windows reached, and a large payload among what was recovered.
    CHECK(landed["inside the flush"] >= 1);
    CHECK(landed["between apply and the flush"] >= 1);
    CHECK(runsWithPayload >= 1);
}

TEST_CASE("recovery.kill_annotsaves: twenty kills during saves of annotation edits leave the target a whole revision",
          "[recovery][kill][A-107]")
{
    const quint32 seed = QRandomGenerator::global()->generate();
    QRandomGenerator random(seed);
    std::cout << "kill_annotsaves: seed " << seed << std::endl;

    std::map<std::string, int> landed;
    int mostSaves = 0;
    for (int run = 1; run <= kRuns; ++run) {
        INFO("run " << run << " of " << kRuns << ", seed " << seed);
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString target = dir.filePath(QStringLiteral("doc.pdf"));
        REQUIRE(QFile::copy(QString::fromStdString(RAWPAGE_CORPUS_DIR "/ordinary/text-a4.pdf"), target));
        QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        const std::string original = shaOf(target);

        auto probe = startProbe({QStringLiteral("annotsaves"), target, QString::number(kDwellMs)});
        const QList<QByteArray> lines = killProbe(*probe, 30000 + static_cast<qint64>(random.bounded(570000)));
        REQUIRE_FALSE(lines.isEmpty());

        // Each hash the target may have, with the number of steps that file holds: the last acknowledged save's
        // (or the original's, 0), and the temporary file about to replace it if a replace had begun.
        std::map<std::string, int> allowed{{original, 0}};
        int acknowledged = 0;
        for (const QByteArray& line : lines) {
            if (numberAfter(line, "ack") > 0) {
                acknowledged = numberAfter(line, "ack");
                allowed = {{line.mid(line.indexOf("sha=") + 4).toStdString(), acknowledged}};
            }
        }
        for (const QByteArray& line : lines)
            if (numberAfter(line, "replacing") == acknowledged + 1)
                allowed.emplace(line.mid(line.indexOf("sha=") + 4).toStdString(), acknowledged + 1);
        mostSaves = std::max(mostSaves, acknowledged);

        const QByteArray last = lines.last();
        const std::string where = last.startsWith("written") ? "between writing the temporary file and the replace"
                                  : last.startsWith("replacing") ? "inside the replace"
                                  : last.startsWith("writing") ? "writing the temporary file"
                                                               : "between saves";
        ++landed[where];

        INFO("killed " << where << " after save " << acknowledged);
        REQUIRE(QFileInfo::exists(target));                        // a kill never leaves the path empty
        const auto found = allowed.find(shaOf(target));
        REQUIRE(found != allowed.end());                           // a whole revision, never anything else

        // AND THAT REVISION READS BACK as the steps it holds: every kill-* annotation and attachment the same
        // steps leave here, by name, through the first-party reader.
        tests::CosBackend backend;
        auto handle = backend.open(target, nullptr);
        REQUIRE(handle);
        core::Document reopened;
        REQUIRE(reopened.hydrate(backend, handle.value()));
        const Expected expected = expectedAfter(QStringLiteral("ordinary/text-a4.pdf"), found->second, dir.path());
        std::set<QString> names;
        for (const core::Annotation& a : reopened.annotations().all())
            if (a.nm.startsWith(QStringLiteral("kill-"))) names.insert(a.nm);
        std::set<QString> attachments;
        for (const core::DocumentAttachment& file : reopened.attachments().files()) attachments.insert(file.key);
        CHECK(names == expected.names);
        CHECK(attachments == expected.attachments);
    }

    std::cout << "kill_annotsaves: where the " << kRuns << " kills landed --";
    for (const auto& [where, count] : landed)
        std::cout << " " << where << ": " << count << ";";
    std::cout << " most saves acknowledged: " << mostSaves << std::endl;
    CHECK(landed["between writing the temporary file and the replace"] >= 1);
}
