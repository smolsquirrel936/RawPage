#include "platform/win/WinSingleInstanceBus.h"

#include "core/Log.h"

#include <QElapsedTimer>
#include <QMetaObject>

#include <windows.h>

#include <utility>
#include <vector>

namespace rawpage::platform::win {
namespace {

using core::LogSubsystem;

// The message on the wire: a 4-byte little-endian length, then that many bytes of UTF-8, then the
// file list as one string per line.
//
// LENGTH-PREFIXED, not "read until the pipe closes": a message-mode pipe can still return a partial
// read, and a path containing a newline is legal on Windows (rare, but this is exactly the code
// that must not corrupt a path). The prefix is what makes a short read detectable rather than
// silently truncating the last file name.
constexpr DWORD kMaxMessageBytes = 1u << 20;   // a megabyte of paths is already absurd
constexpr DWORD kPipeBufferBytes = 64 * 1024;
constexpr DWORD kConnectTimeoutMs = 3000;

std::wstring toWide(const QString& text)
{
    return std::wstring(reinterpret_cast<const wchar_t*>(text.utf16()), text.size());
}

QString lastErrorText(DWORD code)
{
    return QStringLiteral("win32 error %1").arg(code);
}

// A name component the kernel will accept. Backslash is the namespace separator, so leaving one in
// would put the object somewhere other than where the caller believes -- and every other character
// we reject here is one that makes a name unreadable in a debugger.
QString sanitizeScope(const QString& scopeId)
{
    QString clean;
    clean.reserve(scopeId.size());
    for (const QChar c : scopeId) {
        const bool ok = c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('-')
                        || c == QLatin1Char('_');
        clean.append(ok ? c : QLatin1Char('_'));
    }
    if (clean.isEmpty()) clean = QStringLiteral("default");
    // The kernel's limit is MAX_PATH for an object name; a scope longer than this is a caller bug
    // that would otherwise fail at CreateMutex with a useless error.
    return clean.left(64);
}

} // namespace

QString singleInstanceMutexName(const QString& scopeId)
{
    // `Local\` -- per session, deliberately. See the header.
    return QStringLiteral("Local\\RawPage.SingleInstance.") + sanitizeScope(scopeId);
}

QString singleInstancePipeName(const QString& scopeId, unsigned long sessionId)
{
    // THE SESSION ID IS IN THE NAME ON PURPOSE. Named pipes have no `Local\` namespace: every pipe
    // is machine-wide, so without this two logon sessions whose mutexes are correctly separated
    // would still share one pipe, and user B's launch would forward its file into user A's process
    // (or, more likely, fail with ACCESS_DENIED and look like a broken install).
    return QStringLiteral("\\\\.\\pipe\\RawPage.SingleInstance.%1.%2")
        .arg(sanitizeScope(scopeId))
        .arg(sessionId);
}

unsigned long currentSessionId()
{
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return 0;
    return session;
}

WinSingleInstanceBus::WinSingleInstanceBus() = default;

WinSingleInstanceBus::~WinSingleInstanceBus()
{
    release();
}

Result<bool> WinSingleInstanceBus::claim(const QString& scopeId)
{
    if (mutex_ != nullptr) {
        return core::Error{core::ErrorCode::InvalidArgument,
                           QLatin1String("error.singleInstance.alreadyClaimed")};
    }

    scopeId_ = scopeId;
    mutexName_ = singleInstanceMutexName(scopeId);
    pipeName_ = singleInstancePipeName(scopeId, currentSessionId());

    const std::wstring wideName = toWide(mutexName_);
    SetLastError(ERROR_SUCCESS);
    HANDLE handle = CreateMutexW(nullptr, TRUE, wideName.c_str());
    const DWORD error = GetLastError();

    if (handle == nullptr) {
        return core::Error{core::ErrorCode::IoFailure,
                           QLatin1String("error.singleInstance.claimFailed"), lastErrorText(error)};
    }

    mutex_ = handle;
    // ERROR_ALREADY_EXISTS means the name was there, i.e. somebody else is primary. The handle is
    // still valid and still ours to close -- releasing it here would be a leak of the OTHER
    // process's guarantee, not of ours, but keeping it costs nothing and keeps release() uniform.
    primary_ = (error != ERROR_ALREADY_EXISTS);

    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "single instance:" << (primary_ ? "primary" : "secondary") << "for"
        << mutexName_.toUtf8().constData();

    return primary_;
}

bool WinSingleInstanceBus::isPrimary() const
{
    return primary_;
}

Status WinSingleInstanceBus::listen(OpenFilesHandler handlerIn)
{
    if (!primary_) {
        return core::Error{core::ErrorCode::Unavailable,
                           QLatin1String("error.singleInstance.notPrimary")};
    }
    if (listening_.load()) return {};
    if (!handlerIn) {
        return core::Error{core::ErrorCode::InvalidArgument,
                           QLatin1String("error.singleInstance.noHandler")};
    }

    handler_ = std::move(handlerIn);

    // Created here, on the CALLING thread, so the queued invocation below lands back on it. This is
    // the whole thread-affinity contract of the class, in one line.
    deliveryContext_ = std::make_unique<QObject>();

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent_ == nullptr) {
        return core::Error{core::ErrorCode::IoFailure,
                           QLatin1String("error.singleInstance.listenFailed"),
                           lastErrorText(GetLastError())};
    }

    // THE FIRST PIPE INSTANCE IS CREATED HERE, SYNCHRONOUSLY, NOT ON THE WORKER THREAD.
    //
    // Found by the T7 tests: with creation on the thread, `listen()` returned before the pipe
    // existed, and a secondary that forwarded in that window got ERROR_FILE_NOT_FOUND -- a launch
    // that silently did nothing. The window is small and entirely real: it is exactly the case of a
    // user double-clicking a second PDF while the first instance is still starting.
    pendingPipe_ = createPipeInstance();
    if (pendingPipe_ == nullptr) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
        return core::Error{core::ErrorCode::IoFailure,
                           QLatin1String("error.singleInstance.listenFailed"),
                           lastErrorText(GetLastError())};
    }

    listening_.store(true);
    serverThread_ = std::thread([this] { serverLoop(); });
    return {};
}

// One listening instance of the handoff pipe. Returns nullptr on failure; the caller reports.
void* WinSingleInstanceBus::createPipeInstance()
{
    const std::wstring widePipe = toWide(pipeName_);
    HANDLE pipe = CreateNamedPipeW(
        widePipe.c_str(),
        PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES, 0, kPipeBufferBytes, 0,
        nullptr);   // default DACL: this session's user only, which is the scope we want
    return pipe == INVALID_HANDLE_VALUE ? nullptr : pipe;
}

void WinSingleInstanceBus::serverLoop()
{
    while (listening_.load()) {
        // The instance `listen()` -- or the previous iteration -- already created. A fresh instance
        // per connection, rather than one reused with DisconnectNamedPipe, whose race gives a
        // client ERROR_PIPE_NOT_CONNECTED and the user a launch that did nothing.
        HANDLE pipe = static_cast<HANDLE>(pendingPipe_);
        pendingPipe_ = nullptr;
        if (pipe == nullptr) {
            pipe = static_cast<HANDLE>(createPipeInstance());
            if (pipe == nullptr) {
                RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
                    << "single instance: cannot create the handoff pipe --"
                    << static_cast<int>(GetLastError());
                return;
            }
        }

        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        BOOL connected = ConnectNamedPipe(pipe, &overlapped);
        DWORD error = GetLastError();
        if (!connected && error == ERROR_PIPE_CONNECTED) {
            // The client beat us to it between CreateNamedPipe and ConnectNamedPipe. Not an error,
            // and the one case where forgetting to handle it produces a hang rather than a failure.
            SetEvent(overlapped.hEvent);
        } else if (!connected && error != ERROR_IO_PENDING) {
            CloseHandle(overlapped.hEvent);
            CloseHandle(pipe);
            return;
        }

        // Overlapped, with the stop event in the same wait, so release() does not need the
        // connect-to-your-own-pipe trick to unblock a thread that is inside a blocking call.
        HANDLE waits[2] = {overlapped.hEvent, stopEvent_};
        const DWORD which = WaitForMultipleObjects(2, waits, FALSE, INFINITE);

        if (which != WAIT_OBJECT_0) {
            CancelIo(pipe);
            CloseHandle(overlapped.hEvent);
            CloseHandle(pipe);
            return;
        }

        // The NEXT instance goes up before this one is drained, so there is never a moment with no
        // listening pipe. Without it a second forward arriving while the first is being read gets
        // ERROR_FILE_NOT_FOUND -- which is the multi-file "open with" case, not an exotic one.
        pendingPipe_ = createPipeInstance();

        std::vector<char> buffer(kPipeBufferBytes);
        DWORD read = 0;
        QByteArray message;
        while (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)
               && read > 0) {
            message.append(buffer.data(), static_cast<int>(read));
            if (static_cast<DWORD>(message.size()) > kMaxMessageBytes) break;
            if (read < buffer.size()) break;
        }

        CloseHandle(overlapped.hEvent);
        CloseHandle(pipe);

        if (message.size() < 4) continue;

        const auto* length = reinterpret_cast<const unsigned char*>(message.constData());
        const quint32 declared = static_cast<quint32>(length[0]) | (static_cast<quint32>(length[1]) << 8)
                                 | (static_cast<quint32>(length[2]) << 16)
                                 | (static_cast<quint32>(length[3]) << 24);
        // A short read is DROPPED, never truncated into a shorter path list. Half a file name is
        // worse than no file name: it names a different file, or none, and the user sees the wrong
        // document open.
        if (declared > kMaxMessageBytes || message.size() - 4 < static_cast<int>(declared)) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
                << "single instance: dropping a truncated handoff message";
            continue;
        }

        const QString payload = QString::fromUtf8(message.constData() + 4, static_cast<int>(declared));
        QStringList files = payload.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        if (files.isEmpty()) continue;

        // Back onto the thread that called listen(). Queued, always -- a direct call here would run
        // window code on this worker thread, and the crash would be reported three stages later as
        // a rendering bug.
        QMetaObject::invokeMethod(
            deliveryContext_.get(), [this, files = std::move(files)] { if (handler_) handler_(files); },
            Qt::QueuedConnection);
    }
}

Status WinSingleInstanceBus::forward(const QStringList& files)
{
    if (primary_) {
        return core::Error{core::ErrorCode::InvalidArgument,
                           QLatin1String("error.singleInstance.primaryCannotForward")};
    }
    if (pipeName_.isEmpty()) {
        return core::Error{core::ErrorCode::InvalidArgument,
                           QLatin1String("error.singleInstance.notClaimed")};
    }

    const std::wstring widePipe = toWide(pipeName_);

    // TWO transient failures are retried, and neither is exotic:
    //
    //   ERROR_FILE_NOT_FOUND  the primary has claimed the mutex but has not called listen() yet.
    //                         This is the ordinary race of launching a second instance while the
    //                         first is still starting, and it is what the T7 tests caught.
    //   ERROR_PIPE_BUSY       every instance is in use. The primary puts the next one up before
    //                         draining the current one, so the window is microseconds wide.
    //
    // Anything else is reported immediately: retrying an access-denied or a bad name just delays
    // the same failure by three seconds.
    HANDLE pipe = INVALID_HANDLE_VALUE;
    QElapsedTimer elapsed;
    elapsed.start();
    while (true) {
        // The gate's filesystem-write rule matches `CreateFileW(` by name, and this opens a NAMED
        // PIPE rather than a file -- the exact false positive the heuristic was documented to
        // produce. Waived here rather than by narrowing the pattern: `CreateFileW` really is how a
        // file gets written on Windows, and a rule that stopped matching it to accommodate one pipe
        // would stop catching the thing it exists for.
        pipe = CreateFileW(widePipe.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);   // layer-lint: allow-filesystem-write(a named pipe client, not a file -- the single-instance handoff channel)
        if (pipe != INVALID_HANDLE_VALUE) break;

        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY) break;
        if (elapsed.elapsed() >= kConnectTimeoutMs) break;

        if (error == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(widePipe.c_str(), 100);
        } else {
            Sleep(20);
        }
    }

    if (pipe == INVALID_HANDLE_VALUE) {
        return core::Error{core::ErrorCode::IoFailure,
                           QLatin1String("error.singleInstance.forwardFailed"),
                           lastErrorText(GetLastError())};
    }

    // THE FOREGROUND HANDSHAKE, and it happens BEFORE the write for a reason: once the primary has
    // the file list it may raise its window immediately, and a permission granted after that is a
    // permission granted too late. The primary's pid comes from the pipe itself rather than from
    // the message, so a malformed or spoofed payload cannot redirect the grant.
    ULONG serverPid = 0;
    if (GetNamedPipeServerProcessId(pipe, &serverPid) && serverPid != 0) {
        AllowSetForegroundWindow(serverPid);
    } else {
        // ASFW_ANY is the documented fallback. Broader than we would like, and still far better
        // than a handoff that lands with no window coming forward.
        AllowSetForegroundWindow(ASFW_ANY);
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "single instance: could not read the primary's process id; granting foreground to any";
    }

    const QByteArray payload = files.join(QLatin1Char('\n')).toUtf8();
    QByteArray framed;
    framed.reserve(payload.size() + 4);
    const quint32 length = static_cast<quint32>(payload.size());
    framed.append(static_cast<char>(length & 0xFF));
    framed.append(static_cast<char>((length >> 8) & 0xFF));
    framed.append(static_cast<char>((length >> 16) & 0xFF));
    framed.append(static_cast<char>((length >> 24) & 0xFF));
    framed.append(payload);

    DWORD written = 0;
    const BOOL ok = WriteFile(pipe, framed.constData(), static_cast<DWORD>(framed.size()), &written,
                              nullptr);
    const DWORD writeError = GetLastError();
    FlushFileBuffers(pipe);
    CloseHandle(pipe);

    if (!ok || written != static_cast<DWORD>(framed.size())) {
        return core::Error{core::ErrorCode::IoFailure,
                           QLatin1String("error.singleInstance.forwardFailed"),
                           lastErrorText(writeError)};
    }
    return {};
}

void WinSingleInstanceBus::release()
{
    if (listening_.exchange(false)) {
        if (stopEvent_ != nullptr) SetEvent(stopEvent_);
        if (serverThread_.joinable()) serverThread_.join();
    }

    if (pendingPipe_ != nullptr) {
        // The instance the loop had queued and never accepted. Closed here rather than left to
        // process exit, so a release() followed by a fresh claim() in the same process -- which is
        // exactly what the tests do -- does not leave a stale listener holding the name.
        CloseHandle(static_cast<HANDLE>(pendingPipe_));
        pendingPipe_ = nullptr;
    }
    if (stopEvent_ != nullptr) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    if (mutex_ != nullptr) {
        if (primary_) ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }

    deliveryContext_.reset();
    handler_ = {};
    primary_ = false;
}

} // namespace rawpage::platform::win
