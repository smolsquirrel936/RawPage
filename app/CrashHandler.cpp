#include "app/CrashHandler.h"

#include "core/Log.h"

#include <windows.h>
#include <dbghelp.h>
#include <shlobj.h>

#include <filesystem>

namespace rawpage::app {
namespace {

// --- Install-time state ------------------------------------------------------
// Everything the filter needs is computed HERE, while the process is still healthy. A crash
// filter that calls into the allocator or the shell to work out where to put its output is a
// filter that fails on precisely the crashes worth capturing.
wchar_t   g_crashDir[MAX_PATH] = L"";
LastRites g_lastRites;
bool      g_lastRitesIsNoOp = true;
LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;

using MiniDumpWriteDumpFn = BOOL (WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                           PMINIDUMP_EXCEPTION_INFORMATION,
                                           PMINIDUMP_USER_STREAM_INFORMATION,
                                           PMINIDUMP_CALLBACK_INFORMATION);

// dbghelp is loaded lazily rather than at link time: it is ~1 MB of DLL that a process which
// never crashes never needs. Resolved at install(), not in the filter, because LoadLibrary
// takes the loader lock -- a lock the crashing thread may already hold.
MiniDumpWriteDumpFn g_miniDumpWriteDump = nullptr;

// --- Allocation-free formatting ----------------------------------------------
// std::to_string, QString and snprintf all touch the heap or the locale. Neither is safe on a
// path that may have been entered *because* the heap is corrupt.
void appendDecimal(wchar_t* out, size_t& at, unsigned value, int digits)
{
    for (int place = digits - 1; place >= 0; --place) {
        unsigned divisor = 1;
        for (int i = 0; i < place; ++i) divisor *= 10;
        out[at++] = static_cast<wchar_t>(L'0' + ((value / divisor) % 10));
    }
}

void appendWide(wchar_t* out, size_t& at, const wchar_t* text)
{
    while (*text) out[at++] = *text++;
}

void appendUtf8(char* out, size_t& at, const char* text)
{
    while (*text) out[at++] = *text++;
}

void appendHexAscii(char* out, size_t& at, unsigned long long value, int digits)
{
    static const char kDigits[] = "0123456789ABCDEF";
    for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
        out[at++] = kDigits[(value >> shift) & 0xF];
}

// --- The build id ------------------------------------------------------------
// Walks the running image's own PE debug directory for the CodeView RSDS record that
// -Wl,--build-id emits (the header explains why it is not .note.gnu.build-id here).
// Rendered as GUID+age, 33 hex characters -- exactly what a minidump's module list carries,
// so a dump and a binary match without a side table.
bool readBuildId(char* out33)
{
    auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    if (!base) return false;

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const IMAGE_DATA_DIRECTORY& dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (dir.VirtualAddress == 0 || dir.Size < sizeof(IMAGE_DEBUG_DIRECTORY)) return false;

    const auto* entries =
        reinterpret_cast<const IMAGE_DEBUG_DIRECTORY*>(base + dir.VirtualAddress);
    const size_t count = dir.Size / sizeof(IMAGE_DEBUG_DIRECTORY);

    for (size_t i = 0; i < count; ++i) {
        if (entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW) continue;
        if (entries[i].SizeOfData < 24) continue;

        const unsigned char* cv = base + entries[i].AddressOfRawData;
        if (cv[0] != 'R' || cv[1] != 'S' || cv[2] != 'D' || cv[3] != 'S') continue;

        GUID guid{};
        DWORD age = 0;
        __builtin_memcpy(&guid, cv + 4, sizeof(guid));
        __builtin_memcpy(&age, cv + 20, sizeof(age));

        size_t at = 0;
        appendHexAscii(out33, at, guid.Data1, 8);
        appendHexAscii(out33, at, guid.Data2, 4);
        appendHexAscii(out33, at, guid.Data3, 4);
        for (unsigned char byte : guid.Data4) appendHexAscii(out33, at, byte, 2);
        appendHexAscii(out33, at, age, 1);
        out33[at] = '\0';
        return true;
    }
    return false;
}

// --- The sidecar -------------------------------------------------------------
// A minidump is opaque without tooling, and the two facts a human needs first -- which build
// this was, and whether the last-rites hook ran -- are the two a dump viewer makes hardest to
// see. Written with raw Win32 so it survives a broken CRT.
void writeSidecar(const wchar_t* path, const EXCEPTION_RECORD* record, bool dumpWritten)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,  // layer-lint: allow-filesystem-write(the crash sidecar; app/ owns the crash path per D1-13, and this write is what makes a dump identifiable)
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    char text[1024];
    size_t at = 0;

    appendUtf8(text, at, "RawPage crash report\r\nbuildId=");
    char buildIdText[34] = "";
    if (readBuildId(buildIdText)) appendUtf8(text, at, buildIdText);
    else appendUtf8(text, at, "(none -- -Wl,--build-id was lost; this dump cannot be symbolized)");

    appendUtf8(text, at, "\r\nexceptionCode=0x");
    appendHexAscii(text, at, record ? record->ExceptionCode : 0u, 8);
    appendUtf8(text, at, "\r\nexceptionAddress=0x");
    appendHexAscii(text, at,
        reinterpret_cast<unsigned long long>(record ? record->ExceptionAddress : nullptr), 16);
    appendUtf8(text, at, "\r\nprocessId=0x");
    appendHexAscii(text, at, GetCurrentProcessId(), 8);

    // A-66: the no-op state of the hook is RECORDED, never implied by silence. An unwired
    // hook and a hook that ran and did nothing look identical otherwise.
    appendUtf8(text, at, "\r\nlastRites=");
    appendUtf8(text, at, g_lastRitesIsNoOp
        ? "no-op (RecoveryJournal arrives at Stage 3; the hook is installed and was invoked)"
        : "invoked");

    appendUtf8(text, at, "\r\nminidump=");
    appendUtf8(text, at, dumpWritten ? "written" : "FAILED");
    appendUtf8(text, at, "\r\n");

    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(at), &written, nullptr);
    CloseHandle(file);
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* exception)
{
    // Re-entrancy guard: a fault raised INSIDE this filter must not recurse.
    static LONG entered = 0;
    if (InterlockedExchange(&entered, 1) != 0) return EXCEPTION_EXECUTE_HANDLER;

    // Last rites first, while the most state is still intact -- see LastRites in the header.
    // Deliberately NOT wrapped in __try/__except: GCC on Windows x64 does not implement SEH
    // in C++, so a wrapper here would be a comment pretending to be a guard. The hook's
    // contract is instead that it must be small and must not allocate (Stage 3 owns that).
    if (g_lastRites) g_lastRites();

    // <dir>\rawpage-YYYYMMDD-HHMMSS-<pid>.dmp, built into a stack buffer.
    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t stem[MAX_PATH * 2];
    size_t at = 0;
    appendWide(stem, at, g_crashDir);
    appendWide(stem, at, L"\\rawpage-");
    appendDecimal(stem, at, now.wYear, 4);
    appendDecimal(stem, at, now.wMonth, 2);
    appendDecimal(stem, at, now.wDay, 2);
    stem[at++] = L'-';
    appendDecimal(stem, at, now.wHour, 2);
    appendDecimal(stem, at, now.wMinute, 2);
    appendDecimal(stem, at, now.wSecond, 2);
    stem[at++] = L'-';
    appendDecimal(stem, at, GetCurrentProcessId(), 6);
    const size_t stemLength = at;

    wchar_t dumpPath[MAX_PATH * 2];
    __builtin_memcpy(dumpPath, stem, stemLength * sizeof(wchar_t));
    size_t dumpAt = stemLength;
    appendWide(dumpPath, dumpAt, L".dmp");
    dumpPath[dumpAt] = L'\0';

    bool dumpWritten = false;
    if (g_miniDumpWriteDump) {
        HANDLE file = CreateFileW(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,  // layer-lint: allow-filesystem-write(the minidump itself; app/ owns the crash path per D1-13)
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION info{};
            info.ThreadId = GetCurrentThreadId();
            info.ExceptionPointers = exception;
            info.ClientPointers = FALSE;

            // Thread stacks plus the memory they point at, and the module list: enough to
            // walk the faulting stack and identify the binary. Deliberately NOT
            // MiniDumpWithFullMemory -- a full dump of a PDF editor contains the user's
            // document, and a crash report is routinely mailed around. That is the same rule
            // Features.md 9.5 states for logs, applied to the other artefact that leaves the
            // machine.
            const auto type = static_cast<MINIDUMP_TYPE>(
                MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory |
                MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);

            dumpWritten = g_miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                                              file, type, &info, nullptr, nullptr) != FALSE;
            CloseHandle(file);
        }
    }

    wchar_t sidecarPath[MAX_PATH * 2];
    __builtin_memcpy(sidecarPath, stem, stemLength * sizeof(wchar_t));
    size_t sidecarAt = stemLength;
    appendWide(sidecarPath, sidecarAt, L".txt");
    sidecarPath[sidecarAt] = L'\0';
    writeSidecar(sidecarPath, exception ? exception->ExceptionRecord : nullptr, dumpWritten);

    if (g_previousFilter) g_previousFilter(exception);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void CrashHandler::install(LastRites lastRites)
{
    wchar_t localAppData[MAX_PATH] = L"";
    // Not QStandardPaths: install() runs before any Qt object exists, on purpose, because an
    // exception thrown while Qt is initialising is one of the ones worth catching.
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData)))
        return;

    size_t at = 0;
    appendWide(g_crashDir, at, localAppData);
    appendWide(g_crashDir, at, L"\\RawPage\\crashes");
    g_crashDir[at] = L'\0';

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(g_crashDir), ec);  // layer-lint: allow-filesystem-write(creates %LOCALAPPDATA%\RawPage\crashes at install, so the crash path itself never has to)
    if (ec) {
        RAWPAGE_LOG_WARNING(core::LogSubsystem::Ui)
            << "CrashHandler: could not create the crash directory; dumps will not be written";
        return;
    }

    g_lastRitesIsNoOp = !static_cast<bool>(lastRites);
    g_lastRites = g_lastRitesIsNoOp ? LastRites([] {}) : std::move(lastRites);

    if (HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll"))
        g_miniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(
            reinterpret_cast<void*>(GetProcAddress(dbghelp, "MiniDumpWriteDump")));

    if (!g_miniDumpWriteDump) {
        RAWPAGE_LOG_WARNING(core::LogSubsystem::Ui)
            << "CrashHandler: dbghelp!MiniDumpWriteDump unavailable; only the sidecar is written";
    }

    g_previousFilter = SetUnhandledExceptionFilter(unhandledExceptionFilter);

    // A-66 at the other end: the hook's no-op state is said out loud at install, so it is
    // visible in an ordinary session log rather than only in a crash nobody has had.
    RAWPAGE_LOG_INFO(core::LogSubsystem::Ui)
        << "CrashHandler installed; buildId" << buildId() << "; lastRites"
        << (g_lastRitesIsNoOp ? "no-op (RecoveryJournal arrives at Stage 3)" : "wired");
}

QString CrashHandler::crashDirectory()
{
    return QString::fromWCharArray(g_crashDir);
}

QString CrashHandler::buildId()
{
    char text[34] = "";
    if (!readBuildId(text)) return QString();
    return QString::fromLatin1(text);
}

void CrashHandler::forceCrashForTesting()
{
    // A null write rather than std::abort(): abort() raises SIGABRT, which the CRT handles
    // without ever reaching SetUnhandledExceptionFilter -- so it would test nothing.
    volatile int* nowhere = nullptr;
    *nowhere = 1;
    __builtin_unreachable();
}

} // namespace rawpage::app
