// See ConsoleInterrupt.h for why a console Ctrl+C is a close request, and why a second one is not.

#include "app/ConsoleInterrupt.h"

#include "core/Log.h"

#include <windows.h>

#include <QApplication>
#include <QMetaObject>
#include <QWidget>

#include <mutex>

namespace rawpage::app {

struct ConsoleInterrupt::State {
    QObject*              context = nullptr;
    std::function<bool()> requestClose;
    bool                  pending = false;   // a close was posted and has not finished on the context's thread
};

namespace {

using core::LogSubsystem;

// Both under `gate()`: the handler's thread reads them, the context's thread writes them.
std::mutex& gate()
{
    static std::mutex mutex;
    return mutex;
}
ConsoleInterrupt::State* current = nullptr;

const char* eventName(DWORD type)
{
    return type == CTRL_C_EVENT ? "Ctrl+C" : "Ctrl+Break";
}

// What the posted close does, on the context's thread. The mutex is NOT held while `requestClose` runs -- see the
// header -- so the state is looked up again on each side of it rather than captured: the instance may be gone by then.
void runRequestedClose()
{
    std::function<bool()> requestClose;
    {
        const std::lock_guard<std::mutex> lock(gate());
        if (current == nullptr) return;
        requestClose = current->requestClose;
    }
    const bool closed = requestClose();
    const std::lock_guard<std::mutex> lock(gate());
    if (current == nullptr) return;
    // A close that went ahead keeps `pending`: the process is on its way out, and a Ctrl+C during the way out is the
    // second press that forces it. A refused one clears it, so the next Ctrl+C asks again rather than ending the
    // process an hour later.
    if (!closed) {
        current->pending = false;
        RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "console: the close was refused; the next Ctrl+C asks again";
    }
}

BOOL WINAPI onConsoleControl(DWORD type)
{
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) return FALSE;

    const std::lock_guard<std::mutex> lock(gate());
    if (current == nullptr) return FALSE;
    if (current->pending) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "console: a second" << eventName(type)
            << "while the first close is still waiting -- ending the process; the recovery journal holds any edit";
        // TerminateProcess, NOT `return FALSE` into Windows' default. The default is ExitProcess on THIS thread, which
        // detaches every DLL and runs the static destructors while the GUI thread is still inside Qt -- and that crashed
        // with an access violation in 3 of 40 runs of the probe's `none` mode (2026-10-22), which here would be a
        // minidump and a "crashed" next launch for a deliberate stop. A kill runs no teardown at all, which is the
        // condition the recovery journal is proved against (D3-11's TerminateProcess harness).
        TerminateProcess(GetCurrentProcess(), static_cast<UINT>(STATUS_CONTROL_C_EXIT));
        return TRUE;
    }
    current->pending = true;
    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "console:" << eventName(type) << "-- closing every window; press it again to end the process at once";
    QMetaObject::invokeMethod(current->context, &runRequestedClose, Qt::QueuedConnection);
    return TRUE;
}

} // namespace

ConsoleInterrupt::ConsoleInterrupt(QObject& context, std::function<bool()> requestClose)
{
    const std::lock_guard<std::mutex> lock(gate());
    if (current != nullptr) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "console: a second ConsoleInterrupt installs nothing";
        return;
    }
    if (SetConsoleCtrlHandler(&onConsoleControl, TRUE) == FALSE) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "console: SetConsoleCtrlHandler failed, error" << static_cast<qint64>(GetLastError())
            << "-- a Ctrl+C in the console ends the process";
        return;
    }
    state_ = std::make_unique<State>();
    state_->context = &context;
    state_->requestClose = std::move(requestClose);
    current = state_.get();
}

ConsoleInterrupt::~ConsoleInterrupt()
{
    if (!state_) return;
    {
        const std::lock_guard<std::mutex> lock(gate());
        current = nullptr;
    }
    SetConsoleCtrlHandler(&onConsoleControl, FALSE);
}

bool ConsoleInterrupt::closeAllWindows()
{
    QApplication::closeAllWindows();
    for (const QWidget* widget : QApplication::topLevelWidgets()) {
        const Qt::WindowType kind = widget->windowType();
        if (widget->isVisible() && (kind == Qt::Window || kind == Qt::Dialog)) return false;
    }
    return true;
}

} // namespace rawpage::app
