// rawpage_ctrlcprobe -- a process that sends ITSELF a console Ctrl+C, so `tests/composition/tst_consoleinterrupt.cpp`
// can read what became of it. Stage 6 guide I-35 (`Stage6-Annotations-Testing-Guide.md` section 8).
//
// A HELPER EXECUTABLE, NOT A TEST TARGET, as `rawpage_killprobe` is. It MUST run in a console of its own -- the harness
// starts it with CREATE_NO_WINDOW -- because GenerateConsoleCtrlEvent with group 0 reaches every process attached to the
// caller's console, and in the harness's console that is ctest and whoever ran it.
//
//   rawpage_ctrlcprobe none     no handler: Windows' default ends the probe (the control -- the Ctrl+C does arrive)
//   rawpage_ctrlcprobe close    one window, which closes: the event loop ends and the probe returns 0
//   rawpage_ctrlcprobe refuse   the window refuses its first close and accepts its second; the second Ctrl+C is sent
//                               only once the first close has been refused
//   rawpage_ctrlcprobe hung     the GUI thread is busy for 3 s while two Ctrl+C arrive 200 ms apart: the second ends it
//   rawpage_ctrlcprobe asking   the window's close runs a NESTED EVENT LOOP that never ends, as a dirty document's
//                               question does, and Qt is kept busy inside it; a second Ctrl+C 50 ms in ends the probe.
//                               The mode where ending by ExitProcess races the GUI thread -- `hung`'s thread is asleep
//
// One line per event on stdout, each flushed: `ready`, `close <n> accepted|refused|asking`, `quit`. Exit 3 is the watchdog: no
// Ctrl+C ended or closed anything in 10 s.
//
// WHAT THIS DOES NOT COVER (convention 19). The product attaches to its PARENT's console through AttachConsole; the probe
// is a console program with its own. The handler list and the delivery are the same machinery, but the attach is not
// exercised, and neither is a Ctrl+C typed into a real terminal -- that is the by-hand half I-35 names.

#include "app/ConsoleInterrupt.h"

#include <windows.h>

#include <QApplication>
#include <QCloseEvent>
#include <QEventLoop>
#include <QTimer>
#include <QWidget>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

namespace {

void say(const std::string& line)
{
    std::fputs((line + "\n").c_str(), stdout);
    std::fflush(stdout);
}

void sendCtrlC()
{
    GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
}

class ProbeWindow final : public QWidget {
public:
    ProbeWindow(int refusals, bool asking) : refusals_(refusals), asking_(asking) {}

protected:
    void closeEvent(QCloseEvent* event) override
    {
        ++closes_;
        if (asking_) {
            say("close " + std::to_string(closes_) + " asking");
            event->ignore();
            QTimer::singleShot(50, &sendCtrlC);
            QTimer busy;              // keeps the nested loop working inside Qt, as a dialog's painting does
            QObject::connect(&busy, &QTimer::timeout, [this] { update(); });
            busy.start(0);
            QEventLoop question;
            question.exec();          // never quits: only the second Ctrl+C ends it
            return;
        }
        if (closes_ <= refusals_) {
            say("close " + std::to_string(closes_) + " refused");
            event->ignore();
            // After the request that sent this close has returned -- that is when the refusal is recorded.
            QTimer::singleShot(200, &sendCtrlC);
            return;
        }
        say("close " + std::to_string(closes_) + " accepted");
        QWidget::closeEvent(event);
    }

private:
    int refusals_ = 0;
    bool asking_ = false;
    int closes_ = 0;
};

} // namespace

int main(int argc, char** argv)
{
    // A process started in a new process group, or below one that ignores Ctrl+C, inherits the IGNORE flag, and then no
    // handler is called at all. Cleared, so the run depends on this probe and not on how ctest was started.
    SetConsoleCtrlHandler(nullptr, FALSE);

    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication application(argc, argv);

    const QString mode = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (mode != QLatin1String("none") && mode != QLatin1String("close") && mode != QLatin1String("refuse")
        && mode != QLatin1String("hung") && mode != QLatin1String("asking")) {
        say("usage: rawpage_ctrlcprobe none|close|refuse|hung|asking");
        return 2;
    }

    ProbeWindow window(mode == QLatin1String("refuse") ? 1 : 0, mode == QLatin1String("asking"));
    window.show();

    std::unique_ptr<rawpage::app::ConsoleInterrupt> interrupt;
    if (mode != QLatin1String("none")) {
        interrupt = std::make_unique<rawpage::app::ConsoleInterrupt>(
            application, &rawpage::app::ConsoleInterrupt::closeAllWindows);
        if (!interrupt->installed()) {
            say("not installed");
            return 4;
        }
    }

    QObject::connect(&application, &QCoreApplication::aboutToQuit, [] { say("quit"); });
    QTimer::singleShot(10000, [] { say("watchdog"); std::exit(3); });

    std::thread sender;
    if (mode == QLatin1String("hung")) {
        QTimer::singleShot(0, [] { Sleep(3000); });
        sender = std::thread([] {
            Sleep(100);
            sendCtrlC();
            Sleep(200);
            sendCtrlC();
        });
    } else {
        QTimer::singleShot(0, &sendCtrlC);
    }

    say("ready");
    const int result = application.exec();
    if (sender.joinable()) sender.join();
    return result;
}
