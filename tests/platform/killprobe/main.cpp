// rawpage_killprobe -- a process to be KILLED: it edits or saves in a loop and says where it is, so a
// harness can call TerminateProcess at a random instant and then check what survived. Stage 3 T9, guide
// cases A-65's child half, A-68 and A-69 (`Stage3-Mutation-Testing-Guide.md` §5.10), decision D3-11.
//
// A HELPER EXECUTABLE, NOT A TEST TARGET (`Stage3-Mutation-Plan.md` §4): `tests/platform/tst_recoverykill.cpp`
// runs it. It installs no crash handler and flushes nothing on the way out -- a TerminateProcess runs no
// handler at all, which is the harsher condition the recovery claim rests on (D3-11).
//
//   rawpage_killprobe commands <document> <recovery root> <dwell ms>
//       Opens <document> and a RecoveryJournal under <recovery root>, then rotates pages forever.
//   rawpage_killprobe saves <document> <dwell ms>
//       Opens <document> and saves a rotation of page 1 through SaveCoordinator and the REAL
//       WinDurableFiles, forever.
//   rawpage_killprobe annotations <document> <recovery root> <dwell ms>
//       `commands`, but each step is the annotation cycle of `AnnotationSteps.h` -- an ink, a highlight, a
//       move, a delete and a 200 KiB attachment, the last a SIDE FILE (D6-23). Stage 6 T15, guide A-107.
//   rawpage_killprobe annotsaves <document> <dwell ms>
//       `saves`, but each save follows one step of that cycle. Stage 6 T15, guide A-107.
//
// THE PROTOCOL, one line per event on stdout, each flushed before the step it announces:
//   ready <pid>
//   commands:  apply <k> | writing <k> | flushing <k> | flushed <k> | ack <k>
//   saves:     writing <k> | written <k> | replacing <k> sha=<hex of the temporary file> | replaced <k> | ack <k> sha=<hex>
//
// THE DWELL. The windows the harness must prove it reached -- between a command's apply and its flush,
// inside the flush, between writing a save's temporary file and the replace -- are microseconds wide on
// an SSD, and twenty kills at random would almost never land in them. So the TEST-ONLY phase observers
// sleep <dwell ms> at those phases. The product code is unchanged; the sleep holds a real phase open.

#include "tests/platform/killprobe/AnnotationSteps.h"
#include "tests/unit/CosBackend.h"

#include "core/cmd/CommandJournal.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/SaveCoordinator.h"
#include "platform/win/WinDurableFiles.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>

#include <cstdio>
#include <memory>
#include <string>
#include <thread>

using namespace rawpage;

namespace {

int current = 0;   // the operation in progress, 1-based

void say(const std::string& line)
{
    std::fputs((line + "\n").c_str(), stdout);
    std::fflush(stdout);
}

std::string shaOf(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return "unreadable";
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex().toStdString();
}

void dwell(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 4)
        return 2;
    const QString mode = args.at(1);
    const QString documentPath = args.at(2);

    tests::CosBackend backend;
    platform::win::WinDurableFiles durableFiles;
    auto handle = backend.open(documentPath, nullptr);
    if (!handle || backend.mutationSink(handle.value()) == nullptr)
        return 3;
    core::Document document;
    if (!document.hydrate(backend, handle.value()))
        return 3;
    core::CommandJournal journal(std::move(document), backend.mutationSink(handle.value()));
    const int pages = journal.document().pages().count();

    const bool annotating = mode == QLatin1String("annotations") || mode == QLatin1String("annotsaves");
    // Step `k` of what this mode edits: a rotation, or the annotation cycle (A-107).
    const auto stepFor = [&](int k, int rotatePage) -> std::unique_ptr<core::Command> {
        if (annotating) return tests::killsteps::step(k, journal.document());
        return std::make_unique<core::RotatePageCommand>(rotatePage, 90);
    };

    if ((mode == QLatin1String("commands") || mode == QLatin1String("annotations")) && args.size() >= 5) {
        const int dwellMs = args.at(4).toInt();
        auto recovery = core::RecoveryJournal::open(args.at(3), documentPath, journal.document().id(), &durableFiles);
        if (!recovery->isRecoverable())
            return 4;
        journal.setRecorder(recovery.get());
        core::RecoveryJournal::setPhaseObserverForTesting([dwellMs](core::RecoveryJournal::Phase phase) {
            switch (phase) {
            case core::RecoveryJournal::Phase::Writing:
                say("writing " + std::to_string(current));
                dwell(dwellMs);               // applied, the line not yet written: between apply and the flush
                break;
            case core::RecoveryJournal::Phase::Flushing:
                say("flushing " + std::to_string(current));
                dwell(dwellMs);               // written, the flush not yet complete: inside the flush
                break;
            case core::RecoveryJournal::Phase::Flushed:
                say("flushed " + std::to_string(current));
                break;
            }
        });
        say("ready " + std::to_string(QCoreApplication::applicationPid()));
        for (current = 1; current < 1000000; ++current) {
            say("apply " + std::to_string(current));
            if (!journal.run(stepFor(current, current % pages)))
                return 5;
            say("ack " + std::to_string(current));
        }
        return 0;
    }

    if (mode == QLatin1String("saves") || mode == QLatin1String("annotsaves")) {
        const int dwellMs = args.at(3).toInt();
        core::SaveCoordinator coordinator(backend, &durableFiles);
        core::SaveCoordinator::setPhaseObserverForTesting([dwellMs](core::SaveCoordinator::Phase phase, const QString& temporary) {
            switch (phase) {
            case core::SaveCoordinator::Phase::Writing:
                say("writing " + std::to_string(current));
                break;
            case core::SaveCoordinator::Phase::Written:
                say("written " + std::to_string(current));
                dwell(dwellMs);               // the temporary file is on disk and the replace has not begun
                break;
            case core::SaveCoordinator::Phase::Replacing:
                say("replacing " + std::to_string(current) + " sha=" + shaOf(temporary));
                break;
            case core::SaveCoordinator::Phase::Replaced:
                say("replaced " + std::to_string(current));
                break;
            }
        });
        say("ready " + std::to_string(QCoreApplication::applicationPid()));
        for (current = 1; current < 1000000; ++current) {
            if (!journal.run(stepFor(current, 0)))
                return 5;
            auto saved = coordinator.save(journal, documentPath);
            if (!saved || saved.value().kind != core::SaveOutcome::Kind::Saved)
                return 6;
            say("ack " + std::to_string(current) + " sha=" + shaOf(documentPath));
        }
        return 0;
    }
    return 2;
}
