// Stage 6 guide I-35 (`Stage6-Annotations-Testing-Guide.md` section 8): a console Ctrl+C asks every window to close
// rather than ending the process, a refused close leaves it running for the next one, and a second Ctrl+C while the
// first still waits ends it. `app/ConsoleInterrupt.h` is the design; `rawpage_ctrlcprobe` sends the probe its own,
// REAL, Ctrl+C -- Windows' delivery on Windows' thread, not a call into the handler -- and these cases read the result
// from the probe's exit code and its lines.
//
// WHAT THIS DOES NOT COVER (convention 19): the probe has a console of its own, where the product borrows its parent's
// through AttachConsole, and nobody types the Ctrl+C into a terminal. `ctrlcprobe/main.cpp` says the same at the source.

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <iostream>

namespace {

constexpr unsigned kControlCExit = 0xC000013AU;   // STATUS_CONTROL_C_EXIT: Windows' default handler, and the forced end
constexpr unsigned kAccessViolation = 0xC0000005U; // the default handler's ExitProcess crashing in its own teardown

struct ProbeRun {
    unsigned   exitCode = 0;
    QStringList lines;
};

ProbeRun runProbe(const QString& mode)
{
    QProcess probe;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    probe.setProcessEnvironment(environment);
    // A CONSOLE OF ITS OWN: the probe's Ctrl+C reaches every process on its console, and this one's holds ctest.
    probe.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
    probe.start(QStringLiteral(RAWPAGE_CTRLCPROBE_PATH), {mode});
    REQUIRE(probe.waitForStarted(10000));
    REQUIRE(probe.waitForFinished(20000));

    ProbeRun run;
    run.exitCode = static_cast<unsigned>(probe.exitCode());
    for (const QByteArray& line : probe.readAllStandardOutput().split('\n'))
        if (!line.trimmed().isEmpty()) run.lines << QString::fromUtf8(line.trimmed());
    std::cout << "I-35 probe " << mode.toStdString() << ": exit 0x" << std::hex << run.exitCode << std::dec << ", lines "
              << run.lines.join(QStringLiteral(" | ")).toStdString() << "\n";
    return run;
}

} // namespace

TEST_CASE("I-35 control: a console Ctrl+C with no handler ends the probe", "[I-35]")
{
    // THE CONTROL (convention 7): without it, a probe whose Ctrl+C never arrived would pass every case below that
    // expects the process to survive. It must be ended by Windows' own default, and closed by nothing.
    //
    // EITHER OF THE DEFAULT'S TWO ENDS. The default is ExitProcess on the handler's thread, and its teardown races the
    // GUI thread: an access violation in 3 of 40 measured runs, the rest STATUS_CONTROL_C_EXIT -- the race that made
    // the product's forced end a TerminateProcess of its own. Neither is the watchdog's 3 nor a clean 0, which is what
    // a Ctrl+C that never arrived would give.
    const ProbeRun run = runProbe(QStringLiteral("none"));
    CHECK((run.exitCode == kControlCExit || run.exitCode == kAccessViolation));
    CHECK(run.lines.contains(QStringLiteral("ready")));
    CHECK_FALSE(run.lines.contains(QStringLiteral("quit")));
}

TEST_CASE("I-35: a console Ctrl+C closes every window and the event loop ends", "[I-35]")
{
    const ProbeRun run = runProbe(QStringLiteral("close"));
    CHECK(run.exitCode == 0);
    CHECK(run.lines == QStringList({QStringLiteral("ready"), QStringLiteral("close 1 accepted"), QStringLiteral("quit")}));
}

TEST_CASE("I-35: a refused close keeps the process and the next Ctrl+C asks again", "[I-35]")
{
    // Had the refusal left the request PENDING, the second Ctrl+C would have ended the probe with kControlCExit.
    const ProbeRun run = runProbe(QStringLiteral("refuse"));
    CHECK(run.exitCode == 0);
    CHECK(run.lines == QStringList({QStringLiteral("ready"), QStringLiteral("close 1 refused"),
                                    QStringLiteral("close 2 accepted"), QStringLiteral("quit")}));
}

TEST_CASE("I-35: a second Ctrl+C while the first close still waits ends the process", "[I-35]")
{
    // The GUI thread is busy when both arrive, so the first close is still posted when the second comes: the handler
    // ends the process itself, and the posted close never runs.
    //
    // NOT COVERED: that the end is a TerminateProcess rather than the default's ExitProcess. The default crashed in 3 of
    // 40 runs of the control's `none` mode and in none of 40 here or of 80 in `asking`, so no case below tells the two
    // apart; planted control P5 (`Stage6-Annotations-Testing-Guide.md` I-35) stayed green, and the choice rests on the
    // control's measurement and on a kill running no teardown at all.
    const ProbeRun run = runProbe(QStringLiteral("hung"));
    CHECK(run.exitCode == kControlCExit);
    CHECK(run.lines == QStringList({QStringLiteral("ready")}));
}

TEST_CASE("I-35: a second Ctrl+C while the first close is asking a question ends the process", "[I-35]")
{
    // The close is IN PROGRESS -- the window's close runs a nested event loop, as a dirty document's question does --
    // when the second Ctrl+C comes. The handler must still be able to look at the request, so the close may not run
    // under the handler's lock: had it, the second Ctrl+C would wait on that lock for ever and the watchdog end it (3).
    const ProbeRun run = runProbe(QStringLiteral("asking"));
    CHECK(run.exitCode == kControlCExit);
    CHECK(run.lines == QStringList({QStringLiteral("ready"), QStringLiteral("close 1 asking")}));
}
