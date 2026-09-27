// platform/win/WinDurableFiles -- implementation. Stage 3 T7; the replace rebuilt at Stage 4 (guide
// I-31). The header says why each call is the one used.

#include "platform/win/WinDurableFiles.h"

#include "core/Log.h"

#include <QDir>

#include <windows.h>
#include <aclapi.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace rawpage::platform::win {
namespace {

using core::Error;
using core::ErrorCode;
using core::LogSubsystem;

// What a save carries from the document it replaces. READONLY is refused before anything is asked;
// COMPRESSED, ENCRYPTED and SPARSE cannot be set through FILE_BASIC_INFO (the header says so).
constexpr DWORD kCarriedAttributes =
    FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;

// FILE_RENAME_INFO with its first member spelled as the DWORD `Flags` that FileRenameInfoEx reads,
// whatever `_WIN32_WINNT` the headers were given. The classic FileRenameInfo reads the same bytes as
// the BOOLEAN `ReplaceIfExists`, which is why the fallback below sets the value 1.
struct RenameInfo {
    DWORD flags;
    HANDLE rootDirectory;
    DWORD fileNameLength;
    WCHAR fileName[1];
};
static_assert(offsetof(RenameInfo, fileName) == offsetof(FILE_RENAME_INFO, FileName));
constexpr DWORD kRenameReplaceIfExists = 0x1;    // FILE_RENAME_FLAG_REPLACE_IF_EXISTS
constexpr DWORD kRenamePosixSemantics = 0x2;     // FILE_RENAME_FLAG_POSIX_SEMANTICS

// The SDK's FileRenameInfoEx (22), which MinGW's minwinbase.h declares only for _WIN32_WINNT >=
// 0x0A000002. Pinned to the neighbour it does declare, so a reordered header fails to compile here.
static_assert(FileIdExtdDirectoryRestartInfo == 20);
constexpr auto kFileRenameInfoEx = static_cast<FILE_INFO_BY_HANDLE_CLASS>(22);

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
    case ERROR_SHARING_VIOLATION:              // measured at T7: a target open without FILE_SHARE_DELETE
    case ERROR_LOCK_VIOLATION:
    case ERROR_USER_MAPPED_FILE:
        return Error{ErrorCode::Unavailable, QLatin1String("error.file.inUse"), describe(path, code)};
    case ERROR_ACCESS_DENIED:
        return Error{ErrorCode::PermissionDenied, QLatin1String("error.file.accessDenied"), describe(path, code)};
    default:
        return Error{ErrorCode::IoFailure, QLatin1String(fallbackKey), describe(path, code)};
    }
}

// A volume without the thing asked for -- FAT has no named streams, no security descriptor and no
// POSIX rename, and Windows before 1809 has no FileRenameInfoEx -- answers one of these.
bool unsupportedByVolume(DWORD code)
{
    return code == ERROR_INVALID_FUNCTION || code == ERROR_NOT_SUPPORTED || code == ERROR_INVALID_PARAMETER;
}

class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
    ~ScopedHandle()
    {
        if (valid())
            CloseHandle(handle_);
    }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return handle_; }

private:
    HANDLE handle_;
};

// Every open this class makes, for reading and for writing, so the census stays one waiver.
HANDLE openFile(const std::wstring& path, DWORD access, DWORD shareMode, DWORD disposition)
{
    return CreateFileW(path.c_str(), access, shareMode, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);   // layer-lint: allow-filesystem-write(IDurableFiles -- the flush, the streams carried to a replacement, and the handle that renames it, D3-4)
}

constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

// Every deletion this class makes -- only `removeFile`'s now -- so the census stays one waiver.
bool deleteFile(const std::wstring& path)
{
    return DeleteFileW(path.c_str()) != 0;   // layer-lint: allow-filesystem-write(IDurableFiles -- a save's temporary file, removed by removeFile)
}

// The move to a target that does not exist: atomic, flushed, and never over an existing file.
bool moveWriteThrough(const std::wstring& from, const std::wstring& to)
{
    return MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != 0;   // layer-lint: allow-filesystem-write(IDurableFiles -- the move that makes a replacement a new target, D3-4)
}

// Both metadata writes on the replacement: its basic information, and the rename over the target.
bool setInformation(HANDLE file, FILE_INFO_BY_HANDLE_CLASS kind, void* information, DWORD size)
{
    return SetFileInformationByHandle(file, kind, information, size) != 0;   // layer-lint: allow-filesystem-write(IDurableFiles -- the replacement's creation time and attributes, and its one atomic rename over the target, S4 I-31)
}

Status copyStream(const std::wstring& from, const std::wstring& to, const QString& reportAs)
{
    const ScopedHandle source(openFile(from, GENERIC_READ, kShareAll, OPEN_EXISTING));
    if (!source.valid())
        return fileError(GetLastError(), "error.file.replaceFailed", reportAs);
    const ScopedHandle destination(openFile(to, GENERIC_WRITE, 0, CREATE_ALWAYS));
    if (!destination.valid())
        return fileError(GetLastError(), "error.file.replaceFailed", reportAs);
    std::vector<char> buffer(64 * 1024);
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(source.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr))
            return fileError(GetLastError(), "error.file.replaceFailed", reportAs);
        if (got == 0)
            break;
        DWORD written = 0;
        if (!WriteFile(destination.get(), buffer.data(), got, &written, nullptr) || written != got)
            return fileError(GetLastError(), "error.file.replaceFailed", reportAs);
    }
    if (!FlushFileBuffers(destination.get()))
        return fileError(GetLastError(), "error.file.replaceFailed", reportAs);
    return Status{};
}

// The target's named streams, onto the replacement -- `Zone.Identifier` among them. The replacement
// is a file this process has just written, so it has none of its own to keep.
Status carryNamedStreams(const QString& target, const QString& replacement)
{
    std::vector<std::wstring> names;
    WIN32_FIND_STREAM_DATA stream;
    const HANDLE find = FindFirstStreamW(toWide(target).c_str(), FindStreamInfoStandard, &stream, 0);
    if (find == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        if (code == ERROR_HANDLE_EOF || unsupportedByVolume(code))
            return Status{};                        // no named stream, or a volume that cannot hold one
        return fileError(code, "error.file.replaceFailed", target);
    }
    do {
        const std::wstring name(stream.cStreamName);   // ":Zone.Identifier:$DATA"; "::$DATA" is the content
        if (name != L"::$DATA")
            names.push_back(name);
    } while (FindNextStreamW(find, &stream));
    const DWORD ended = GetLastError();
    FindClose(find);
    if (ended != ERROR_HANDLE_EOF)
        return fileError(ended, "error.file.replaceFailed", target);

    const std::wstring from = toWide(target);
    const std::wstring to = toWide(replacement);
    for (const std::wstring& name : names) {
        if (const Status copied = copyStream(from + name, to + name, target); !copied)
            return copied;
    }
    return Status{};
}

// The target's DACL -- and whether it is protected from inheritance -- onto the replacement. A DACL
// that is NULL (every access) is not copied: the replacement keeps what its directory gave it.
Status carrySecurity(HANDLE source, HANDLE destination, const QString& target)
{
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const DWORD read = GetSecurityInfo(source, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
                                       nullptr, &descriptor);
    if (read != ERROR_SUCCESS)
        return unsupportedByVolume(read) ? Status{} : fileError(read, "error.file.replaceFailed", target);

    DWORD written = ERROR_SUCCESS;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (dacl != nullptr && GetSecurityDescriptorControl(descriptor, &control, &revision)) {
        const SECURITY_INFORMATION what = DACL_SECURITY_INFORMATION
            | ((control & SE_DACL_PROTECTED) ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION);
        written = SetSecurityInfo(destination, SE_FILE_OBJECT, what, nullptr, nullptr, dacl, nullptr);
    }
    LocalFree(descriptor);
    if (written != ERROR_SUCCESS && !unsupportedByVolume(written))
        return fileError(written, "error.file.replaceFailed", target);
    return Status{};
}

// The target's creation time and the attributes a document carries, onto the replacement.
Status carryBasicInformation(HANDLE source, HANDLE destination, const QString& target)
{
    FILE_BASIC_INFO from{};
    FILE_BASIC_INFO to{};
    if (!GetFileInformationByHandleEx(source, FileBasicInfo, &from, sizeof from)
        || !GetFileInformationByHandleEx(destination, FileBasicInfo, &to, sizeof to))
        return fileError(GetLastError(), "error.file.replaceFailed", target);
    FILE_BASIC_INFO carried{};                       // a zero time means "leave it"
    carried.CreationTime = from.CreationTime;
    carried.FileAttributes = (to.FileAttributes & ~kCarriedAttributes) | (from.FileAttributes & kCarriedAttributes);
    if (carried.FileAttributes == 0)
        carried.FileAttributes = FILE_ATTRIBUTE_NORMAL;
    if (!setInformation(destination, FileBasicInfo, &carried, sizeof carried))
        return fileError(GetLastError(), "error.file.replaceFailed", target);
    return Status{};
}

// ONE rename of the replacement over the target: at every instant the target's name is the old file
// or the new one, never neither (I-31). POSIX semantics first, because only it succeeds past a reader
// holding the target with FILE_SHARE_DELETE; the classic rename -- as atomic, and refused by such a
// reader -- where the OS or the volume has no POSIX rename.
bool classicRenameOnly = false;   // WinDurableFiles::forceClassicRenameForTesting

bool renameOver(HANDLE moving, const std::wstring& target)
{
    const std::size_t nameBytes = target.size() * sizeof(wchar_t);
    std::vector<unsigned char> buffer(offsetof(RenameInfo, fileName) + nameBytes + sizeof(wchar_t), 0);
    auto* info = reinterpret_cast<RenameInfo*>(buffer.data());
    info->rootDirectory = nullptr;
    info->fileNameLength = static_cast<DWORD>(nameBytes);
    std::memcpy(info->fileName, target.c_str(), nameBytes);

    if (!classicRenameOnly) {
        info->flags = kRenameReplaceIfExists | kRenamePosixSemantics;
        if (setInformation(moving, kFileRenameInfoEx, info, static_cast<DWORD>(buffer.size())))
            return true;
        if (!unsupportedByVolume(GetLastError()))
            return false;
    }
    info->flags = 1;                                 // the BOOLEAN ReplaceIfExists, TRUE
    return setInformation(moving, FileRenameInfo, info, static_cast<DWORD>(buffer.size()));
}

} // namespace

Status WinDurableFiles::syncToDisk(const QString& path)
{
    // OPEN_EXISTING: a flush of a file that is not there must fail, not create an empty one.
    const ScopedHandle file(openFile(toWide(path), GENERIC_WRITE, kShareAll, OPEN_EXISTING));
    if (!file.valid())
        return fileError(GetLastError(), "error.file.syncFailed", path);
    if (!FlushFileBuffers(file.get()))
        return fileError(GetLastError(), "error.file.syncFailed", path);
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

    // 1. What a rename would throw away, carried onto the replacement while the target is untouched.
    //    A kill anywhere in here leaves the target exactly as it was.
    if (const Status streams = carryNamedStreams(target, replacement); !streams)
        return streams;
    const ScopedHandle moving(openFile(wideReplacement, GENERIC_WRITE | FILE_READ_ATTRIBUTES | WRITE_DAC | DELETE,
                                       kShareAll, OPEN_EXISTING));
    if (!moving.valid())
        return fileError(GetLastError(), "error.file.replaceFailed", replacement);
    {
        // Closed before the rename: the classic rename refuses a target anything holds open, this
        // process included.
        const ScopedHandle source(openFile(wideTarget, READ_CONTROL | FILE_READ_ATTRIBUTES, kShareAll, OPEN_EXISTING));
        if (!source.valid())
            return fileError(GetLastError(), "error.file.replaceFailed", target);
        if (const Status security = carrySecurity(source.get(), moving.get(), target); !security)
            return security;
        if (const Status basic = carryBasicInformation(source.get(), moving.get(), target); !basic)
            return basic;
    }

    // 2. The one step that changes the target.
    if (!renameOver(moving.get(), wideTarget)) {
        const DWORD code = GetLastError();
        if (code == ERROR_ACCESS_DENIED) {
            // Measured at I-31: over a target another handle holds without delete sharing, the POSIX
            // rename reports a sharing violation (32) itself, but the CLASSIC rename reports access
            // denied (5) -- and refuses a target held open at all. Ask the target, exclusively, whether
            // anything holds it, so the user is told the file is in use rather than that they may not
            // write it (Stage 3 T8, M-08).
            const ScopedHandle probe(openFile(wideTarget, DELETE, 0, OPEN_EXISTING));
            if (!probe.valid() && GetLastError() == ERROR_SHARING_VIOLATION)
                return fileError(ERROR_SHARING_VIOLATION, "error.file.replaceFailed", target);
        }
        return fileError(code, "error.file.replaceFailed", target);
    }

    // 3. The rename to stable storage. The target is already whole whichever way this goes, so a
    //    failure is logged rather than reported as a failed save.
    if (!FlushFileBuffers(moving.get())) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Save)
            << "durable files: replaced" << QDir::toNativeSeparators(target)
            << "but could not flush it -- win32 error" << static_cast<int>(GetLastError());
    }
    return Status{};
}

void WinDurableFiles::forceClassicRenameForTesting(bool classicOnly)
{
    classicRenameOnly = classicOnly;
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
