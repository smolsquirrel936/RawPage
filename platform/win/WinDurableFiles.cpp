// platform/win/WinDurableFiles -- implementation. Stage 3 T7. The header says why each call is the one used.

#include "platform/win/WinDurableFiles.h"

#include "core/Log.h"

#include <QDir>
#include <QUuid>

#include <windows.h>

#include <string>

namespace rawpage::platform::win {
namespace {

using core::Error;
using core::ErrorCode;
using core::LogSubsystem;

std::wstring toWide(const QString& path)
{
    const QString native = QDir::toNativeSeparators(path);
    return std::wstring(reinterpret_cast<const wchar_t*>(native.utf16()), static_cast<std::size_t>(native.size()));
}

QString describe(const QString& path, DWORD code)
{
    return QStringLiteral("%1 (win32 error %2)").arg(QDir::toNativeSeparators(path)).arg(code);
}

// What the OS said, as a key a person can be shown. `fallbackKey` names the operation for any code
// not recognized here, so an unexpected failure is still reported as the operation that failed.
Error fileError(DWORD code, const char* fallbackKey, const QString& path)
{
    switch (code) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        return Error{ErrorCode::NotFound, QLatin1String("error.file.notFound"), describe(path, code)};
    case ERROR_SHARING_VIOLATION:              // measured at T7: ReplaceFileW on a target open without FILE_SHARE_DELETE
    case ERROR_LOCK_VIOLATION:
    case ERROR_USER_MAPPED_FILE:
        return Error{ErrorCode::Unavailable, QLatin1String("error.file.inUse"), describe(path, code)};
    case ERROR_ACCESS_DENIED:
        return Error{ErrorCode::PermissionDenied, QLatin1String("error.file.accessDenied"), describe(path, code)};
    default:
        return Error{ErrorCode::IoFailure, QLatin1String(fallbackKey), describe(path, code)};
    }
}

// Every deletion this class makes -- the backup, and `removeFile` -- so the census stays one waiver.
bool deleteFile(const std::wstring& path)
{
    return DeleteFileW(path.c_str()) != 0;   // layer-lint: allow-filesystem-write(IDurableFiles -- the backup ReplaceFileW leaves, and a save's temporary file)
}

// Both moves this class makes: atomic, flushed, and never over an existing file.
bool moveWriteThrough(const std::wstring& from, const std::wstring& to)
{
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != 0;   // layer-lint: allow-filesystem-write(IDurableFiles -- the move that makes a replacement the target, D3-4)
}

} // namespace

Status WinDurableFiles::syncToDisk(const QString& path)
{
    const std::wstring wide = toWide(path);
    // OPEN_EXISTING: a flush of a file that is not there must fail, not create an empty one.
    const HANDLE file = CreateFileW(wide.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);   // layer-lint: allow-filesystem-write(IDurableFiles -- FlushFileBuffers needs a handle opened for write, D3-4)
    if (file == INVALID_HANDLE_VALUE)
        return fileError(GetLastError(), "error.file.syncFailed", path);

    const BOOL flushed = FlushFileBuffers(file);
    const DWORD code = flushed ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!flushed)
        return fileError(code, "error.file.syncFailed", path);
    return Status{};
}

Status WinDurableFiles::replaceFile(const QString& target, const QString& replacement)
{
    const std::wstring wideTarget = toWide(target);
    const std::wstring wideReplacement = toWide(replacement);

    // The replacement first: a missing one must never get as far as touching the target.
    const DWORD replacementAttributes = GetFileAttributesW(wideReplacement.c_str());
    if (replacementAttributes == INVALID_FILE_ATTRIBUTES)
        return fileError(GetLastError(), "error.file.replaceFailed", replacement);
    if (replacementAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.file.replaceFailed"), describe(replacement, ERROR_DIRECTORY_NOT_SUPPORTED)};

    const DWORD targetAttributes = GetFileAttributesW(wideTarget.c_str());
    if (targetAttributes == INVALID_FILE_ATTRIBUTES) {
        const DWORD code = GetLastError();
        if (code != ERROR_FILE_NOT_FOUND)
            return fileError(code, "error.file.replaceFailed", target);   // a missing DIRECTORY is not a new file
        if (!moveWriteThrough(wideReplacement, wideTarget))
            return fileError(GetLastError(), "error.file.replaceFailed", target);
        return Status{};
    }
    if (targetAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.file.replaceFailed"), describe(target, ERROR_DIRECTORY_NOT_SUPPORTED)};
    if (targetAttributes & FILE_ATTRIBUTE_READONLY)
        return Error{ErrorCode::PermissionDenied, QLatin1String("error.file.readOnly"), QDir::toNativeSeparators(target)};

    // Beside the target, so it is on the same volume, and unique, so it can never be an existing file.
    const QString backup = target + QStringLiteral(".rawpage-replace-%1.tmp").arg(QUuid::createUuid().toString(QUuid::Id128));
    const std::wstring wideBackup = toWide(backup);

    if (ReplaceFileW(wideTarget.c_str(), wideReplacement.c_str(), wideBackup.c_str(), 0, nullptr, nullptr)) {   // layer-lint: allow-filesystem-write(IDurableFiles -- the replace that keeps the target's streams and creation time, D3-4)
        if (!deleteFile(wideBackup)) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Save)
                << "durable files: replaced" << QDir::toNativeSeparators(target)
                << "but could not delete the previous content at" << QDir::toNativeSeparators(backup)
                << "-- win32 error" << static_cast<int>(GetLastError());
        }
        return Status{};
    }

    const DWORD code = GetLastError();
    if (code == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
        // The target is at the backup name and the replacement is still under its own. Put the
        // target back; if that fails too, say where it is.
        if (!moveWriteThrough(wideBackup, wideTarget)) {
            RAWPAGE_LOG_ERROR(LogSubsystem::Save)
                << "durable files: replace failed after moving" << QDir::toNativeSeparators(target)
                << "to" << QDir::toNativeSeparators(backup) << "and it could not be moved back -- win32 error"
                << static_cast<int>(GetLastError());
            return Error{ErrorCode::IoFailure, QLatin1String("error.file.originalMoved"), QDir::toNativeSeparators(backup)};
        }
    }
    return fileError(code, "error.file.replaceFailed", target);
}

Status WinDurableFiles::removeFile(const QString& path)
{
    if (deleteFile(toWide(path)))
        return Status{};
    const DWORD code = GetLastError();
    if (code == ERROR_FILE_NOT_FOUND)
        return Status{};   // already gone: see the interface
    return fileError(code, "error.file.removeFailed", path);
}

bool WinDurableFiles::processExists(qint64 processId) const
{
    if (processId <= 0 || processId > static_cast<qint64>(MAXDWORD))
        return false;
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));
    if (process == nullptr)
        return GetLastError() == ERROR_ACCESS_DENIED;   // it exists; we may not look at it
    DWORD exitCode = 0;
    const BOOL queried = GetExitCodeProcess(process, &exitCode);
    CloseHandle(process);
    return !queried || exitCode == STILL_ACTIVE;   // a handle to a process that has exited stays valid
}

} // namespace rawpage::platform::win
