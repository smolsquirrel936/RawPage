// tst_durablefiles -- `platform::IDurableFiles` on Windows, `platform/win/WinDurableFiles`. Stage 3 T7,
// guide cases A-47, A-48 and A-49 (`Stage3-Mutation-Testing-Guide.md` §5.8), decision D3-4.
//
// Every case runs against REAL files in a temporary directory on the test machine's disk, and plants
// what it checks for with Win32 directly -- the `Zone.Identifier` stream, the creation time, a handle
// held open -- so the thing being read back is the filesystem's answer and not this class's.
//
// WHAT THIS SUITE DOES NOT COVER (convention 19):
//   * a replace that DIES midway -- `tst_recoverykill`'s A-69 kills one at random instants, and that is
//     where the replace was found to leave no file at the target (S4 guide I-31).
//   * the DACL the replace carries: none is planted.
//   * the classic rename on an OS or volume that truly lacks the POSIX one: the classic cases below
//     FORCE it on this machine's NTFS, which proves the fallback's handling and not that Windows
//     before 1809 takes it.
//   * durability. `syncToDisk` succeeding proves the flush was asked for and accepted; no test can
//     observe a file surviving a power loss (A-49).
//   * a real application holding the file -- manual case M-08. A-48 plants the handle.
//   * a volume without alternate data streams (FAT, exFAT): the temporary directory is on NTFS.

#include <catch2/catch_test_macros.hpp>

#include "platform/platform.h"
#include "platform/win/WinDurableFiles.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <windows.h>

#include <optional>
#include <string>

using namespace rawpage::platform;
using rawpage::core::ErrorCode;

namespace {

std::wstring wide(const QString& path)
{
    const QString native = QDir::toNativeSeparators(path);
    return std::wstring(reinterpret_cast<const wchar_t*>(native.utf16()), static_cast<std::size_t>(native.size()));
}

// EVERY WIN32 CALL BELOW NAMES ITS PATH AND `GetLastError()` IF IT FAILS. The control case further down
// failed ONCE, in a full `ctest` run on 2026-09-27, and passed about 8,500 times afterwards -- alone,
// four loops at once, thirty passes of the platform suite, and under a Defender scanning load -- while
// the one failing run's output was lost. What is still unknown is which call failed and why; another
// process holding one of these files without `FILE_SHARE_DELETE` (an on-access scanner, the indexer)
// is a guess that fits and nothing has confirmed. So the next failure names the call and the error
// code rather than printing `REQUIRE( ok )` (`Stage4-ViewingSurface-Testing-Guide.md` §9 item 4).
std::string failed(const char* call, const QString& path, DWORD error)
{
    return std::string(call) + "(" + QDir::toNativeSeparators(path).toStdString() + ") failed, GetLastError "
           + std::to_string(error);
}

// The error of the last `readRaw` that could not open its file -- a read-back that returns nothing is
// otherwise indistinguishable from a file that is not there.
DWORD g_lastReadError = 0;

// Writes `bytes` to `path`, which may name a stream (`file.pdf:Zone.Identifier`) -- hence Win32 and not
// QFile, whose path handling is not documented to accept a stream name.
void writeRaw(const QString& path, const QByteArray& bytes)
{
    const HANDLE file = CreateFileW(wide(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD openError = GetLastError();
    {
        INFO(failed("CreateFileW", path, openError));   // scoped: an INFO describes every check after it
        REQUIRE(file != INVALID_HANDLE_VALUE);
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    const DWORD writeError = GetLastError();
    CloseHandle(file);
    {
        INFO(failed("WriteFile", path, writeError));
        REQUIRE(ok);
        REQUIRE(written == static_cast<DWORD>(bytes.size()));
    }
}

std::optional<QByteArray> readRaw(const QString& path)
{
    const HANDLE file = CreateFileW(wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        g_lastReadError = GetLastError();
        return std::nullopt;
    }
    QByteArray bytes;
    char buffer[4096];
    DWORD got = 0;
    while (ReadFile(file, buffer, sizeof buffer, &got, nullptr) && got > 0)
        bytes.append(buffer, static_cast<qsizetype>(got));
    CloseHandle(file);
    return bytes;
}

const QByteArray kZone = "[ZoneTransfer]\r\nZoneId=3\r\nHostUrl=https://example.invalid/report.pdf\r\n";

// 2001-01-01 00:00:00 UTC: nothing the test machine creates today can have it by accident.
FILETIME plantedCreationTime()
{
    SYSTEMTIME st{};
    st.wYear = 2001;
    st.wMonth = 1;
    st.wDay = 1;
    FILETIME ft{};
    REQUIRE(SystemTimeToFileTime(&st, &ft));
    return ft;
}

void setCreationTime(const QString& path, const FILETIME& time)
{
    const HANDLE file = CreateFileW(wide(path).c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD openError = GetLastError();
    {
        INFO(failed("CreateFileW", path, openError));   // scoped: an INFO describes every check after it
        REQUIRE(file != INVALID_HANDLE_VALUE);
    }
    const BOOL ok = SetFileTime(file, &time, nullptr, nullptr);
    const DWORD setError = GetLastError();
    CloseHandle(file);
    {
        INFO(failed("SetFileTime", path, setError));
        REQUIRE(ok);
    }
}

quint64 creationTime(const QString& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    REQUIRE(GetFileAttributesExW(wide(path).c_str(), GetFileExInfoStandard, &data));
    return (static_cast<quint64>(data.ftCreationTime.dwHighDateTime) << 32) | data.ftCreationTime.dwLowDateTime;
}

quint64 asNumber(const FILETIME& time)
{
    return (static_cast<quint64>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

QStringList entries(const QTemporaryDir& dir)
{
    return QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name);
}

// A target as a downloaded PDF looks: content, a Zone.Identifier stream, an old creation time.
QString plantDownloadedTarget(const QTemporaryDir& dir, const QString& name)
{
    const QString target = dir.filePath(name);
    writeRaw(target, "%PDF-1.7 original");
    writeRaw(target + QStringLiteral(":Zone.Identifier"), kZone);
    setCreationTime(target, plantedCreationTime());
    REQUIRE(readRaw(target + QStringLiteral(":Zone.Identifier")) == kZone);
    REQUIRE(creationTime(target) == asNumber(plantedCreationTime()));
    return target;
}

std::string keyOf(const rawpage::core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

} // namespace

// ==========================================================================================
// A-47 -- a replace keeps the target's stream and creation time, and the check can see a loss
// ==========================================================================================

TEST_CASE("durable replace keeps the target's Zone.Identifier stream and creation time", "[platform][durablefiles]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = plantDownloadedTarget(dir, QStringLiteral("report.pdf"));
    const QString replacement = dir.filePath(QStringLiteral("report.pdf.saving"));
    writeRaw(replacement, "%PDF-1.7 saved by RawPage");

    win::WinDurableFiles files;
    REQUIRE(files.replaceFile(target, replacement));

    CHECK(readRaw(target) == QByteArray("%PDF-1.7 saved by RawPage"));
    CHECK(readRaw(target + QStringLiteral(":Zone.Identifier")) == kZone);
    CHECK(creationTime(target) == asNumber(plantedCreationTime()));
    // The replacement is gone under its own name, and nothing else is left beside the target.
    CHECK(entries(dir) == QStringList{QStringLiteral("report.pdf")});
}

TEST_CASE("durable replace carries the hidden attribute and not the replacement's", "[platform][durablefiles]")
{
    // The attributes a document carries (`kCarriedAttributes`): a hidden target stays hidden. A rename
    // alone would give it the replacement's attributes, which is the control.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = plantDownloadedTarget(dir, QStringLiteral("hidden.pdf"));
    REQUIRE(SetFileAttributesW(wide(target).c_str(), FILE_ATTRIBUTE_HIDDEN));
    const QString replacement = dir.filePath(QStringLiteral("hidden.pdf.saving"));
    writeRaw(replacement, "new");
    REQUIRE_FALSE(GetFileAttributesW(wide(replacement).c_str()) & FILE_ATTRIBUTE_HIDDEN);   // the control

    win::WinDurableFiles files;
    REQUIRE(files.replaceFile(target, replacement));
    CHECK(readRaw(target) == QByteArray("new"));
    CHECK((GetFileAttributesW(wide(target).c_str()) & FILE_ATTRIBUTE_HIDDEN) != 0);
    CHECK(readRaw(target + QStringLiteral(":Zone.Identifier")) == kZone);
}

// ==========================================================================================
// S4 guide A-107 -- the CLASSIC rename, forced: what Windows before 1809 or a volume without POSIX
// renames would take. It is as atomic and must carry the same identity; it refuses ANY held target.
// ==========================================================================================

namespace {
struct ClassicRenameOnly {
    ClassicRenameOnly() { win::WinDurableFiles::forceClassicRenameForTesting(true); }
    ~ClassicRenameOnly() { win::WinDurableFiles::forceClassicRenameForTesting(false); }
};
} // namespace

TEST_CASE("durable replace through the classic rename keeps the stream and creation time", "[platform][durablefiles][classic]")
{
    const ClassicRenameOnly classic;
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = plantDownloadedTarget(dir, QStringLiteral("report.pdf"));
    const QString replacement = dir.filePath(QStringLiteral("report.pdf.saving"));
    writeRaw(replacement, "%PDF-1.7 saved by the classic rename");

    win::WinDurableFiles files;
    const Status result = files.replaceFile(target, replacement);
    INFO("context: " << (result ? std::string() : result.error().context.toStdString()));
    REQUIRE(result);
    CHECK(readRaw(target) == QByteArray("%PDF-1.7 saved by the classic rename"));
    CHECK(readRaw(target + QStringLiteral(":Zone.Identifier")) == kZone);
    CHECK(creationTime(target) == asNumber(plantedCreationTime()));
    CHECK(entries(dir) == QStringList{QStringLiteral("report.pdf")});
}

TEST_CASE("durable replace through the classic rename refuses a held target as in use whatever its sharing", "[platform][durablefiles][classic]")
{
    // The classic rename reports ACCESS DENIED over a held target (measured 2026-09-27), and refuses
    // even a reader that shares delete -- which the POSIX rename lets through. Either way the user must
    // be told the file is IN USE, not that they may not write it, and both files must be left.
    const ClassicRenameOnly classic;
    for (const DWORD share : {DWORD{FILE_SHARE_READ | FILE_SHARE_WRITE}, DWORD{FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE}}) {
        INFO("the holder's share mode: " << share);
        QTemporaryDir dir;
        REQUIRE(dir.isValid());
        const QString target = plantDownloadedTarget(dir, QStringLiteral("held.pdf"));
        const QString replacement = dir.filePath(QStringLiteral("held.pdf.saving"));
        writeRaw(replacement, "new");
        HANDLE holder = CreateFileW(wide(target).c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        REQUIRE(holder != INVALID_HANDLE_VALUE);

        win::WinDurableFiles files;
        const Status result = files.replaceFile(target, replacement);
        CloseHandle(holder);

        REQUIRE_FALSE(result);
        INFO("context: " << result.error().context.toStdString());
        CHECK(keyOf(result.error()) == "error.file.inUse");
        CHECK(readRaw(target) == QByteArray("%PDF-1.7 original"));
        CHECK(readRaw(target + QStringLiteral(":Zone.Identifier")) == kZone);
        CHECK(readRaw(replacement) == QByteArray("new"));

        // THE CONTROL: the handle was the reason. Closed, the same classic replace succeeds.
        CHECK(files.replaceFile(target, replacement));
        CHECK(readRaw(target) == QByteArray("new"));
    }
}

TEST_CASE("durable replace control: a plain rename over the target loses the stream", "[platform][durablefiles][control]")
{
    // THE CONTROL for the case above. The same preparation, replaced the way QSaveFile does it. If the
    // stream survived this, the read-back could not see the loss and the case above would prove
    // nothing.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = plantDownloadedTarget(dir, QStringLiteral("report.pdf"));
    const QString replacement = dir.filePath(QStringLiteral("report.pdf.saving"));
    writeRaw(replacement, "%PDF-1.7 saved by a rename");

    const BOOL moved = MoveFileExW(wide(replacement).c_str(), wide(target).c_str(), MOVEFILE_REPLACE_EXISTING);
    const DWORD moveError = GetLastError();
    {
        INFO(failed("MoveFileExW", replacement + QStringLiteral(" -> ") + target, moveError));
        REQUIRE(moved);
    }

    // Read FIRST, then describe: INFO formats its message when it runs, not when a check fails.
    g_lastReadError = 0;
    const std::optional<QByteArray> content = readRaw(target);
    {
        INFO("the read-back's open error, if it could not open: GetLastError " << g_lastReadError);
        CHECK(content == QByteArray("%PDF-1.7 saved by a rename"));
    }
    CHECK_FALSE(readRaw(target + QStringLiteral(":Zone.Identifier")).has_value());
    // NOT asserted: the creation time. A rename measured at T7 lost it too, but NTFS documents
    // "tunneling" -- a creation time restored to a name re-created within fifteen seconds of its
    // removal -- so a control that depended on it would be reading a filesystem cache, not this
    // class. Streams are not tunneled.
}

// ==========================================================================================
// A-48 -- a replace that cannot happen leaves both files
// ==========================================================================================

TEST_CASE("durable replace refuses a target held open without delete sharing and leaves both files", "[platform][durablefiles]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = plantDownloadedTarget(dir, QStringLiteral("held.pdf"));
    const QString replacement = dir.filePath(QStringLiteral("held.pdf.saving"));
    writeRaw(replacement, "new");

    // What a reader that shares read and write but not delete does -- the share mode matters, and a
    // real application's is manual case M-08's.
    HANDLE holder = CreateFileW(wide(target).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(holder != INVALID_HANDLE_VALUE);

    win::WinDurableFiles files;
    const Status result = files.replaceFile(target, replacement);
    CloseHandle(holder);

    REQUIRE_FALSE(result);
    INFO("context: " << result.error().context.toStdString());
    CHECK(keyOf(result.error()) == "error.file.inUse");
    CHECK(result.error().code == ErrorCode::Unavailable);
    CHECK(readRaw(target) == QByteArray("%PDF-1.7 original"));
    CHECK(readRaw(target + QStringLiteral(":Zone.Identifier")) == kZone);
    CHECK(readRaw(replacement) == QByteArray("new"));
    CHECK(entries(dir) == QStringList{QStringLiteral("held.pdf"), QStringLiteral("held.pdf.saving")});

    // THE CONTROL: the handle was the reason. Closed, the same replace succeeds.
    CHECK(files.replaceFile(target, replacement));
    CHECK(readRaw(target) == QByteArray("new"));
}

TEST_CASE("durable replace succeeds past a reader that shares delete", "[platform][durablefiles]")
{
    // The other share mode. A reader that opened with FILE_SHARE_DELETE does not block the replace, and
    // keeps reading the content it opened -- which is why a save while another program has the file
    // open can succeed without that program seeing a torn file.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = plantDownloadedTarget(dir, QStringLiteral("shared.pdf"));
    const QString replacement = dir.filePath(QStringLiteral("shared.pdf.saving"));
    writeRaw(replacement, "new content");

    HANDLE reader = CreateFileW(wide(target).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(reader != INVALID_HANDLE_VALUE);

    win::WinDurableFiles files;
    const Status result = files.replaceFile(target, replacement);
    INFO("context: " << (result ? std::string() : result.error().context.toStdString()));
    CHECK(result);

    char buffer[64] = {};
    DWORD got = 0;
    CHECK(ReadFile(reader, buffer, sizeof buffer, &got, nullptr));
    CloseHandle(reader);
    CHECK(QByteArray(buffer, static_cast<qsizetype>(got)) == QByteArray("%PDF-1.7 original"));
    CHECK(readRaw(target) == QByteArray("new content"));
}

TEST_CASE("durable replace refuses a read-only target and leaves both files", "[platform][durablefiles]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = plantDownloadedTarget(dir, QStringLiteral("locked.pdf"));
    const QString replacement = dir.filePath(QStringLiteral("locked.pdf.saving"));
    writeRaw(replacement, "new");
    REQUIRE(SetFileAttributesW(wide(target).c_str(), FILE_ATTRIBUTE_READONLY));

    win::WinDurableFiles files;
    const Status result = files.replaceFile(target, replacement);
    SetFileAttributesW(wide(target).c_str(), FILE_ATTRIBUTE_NORMAL);   // so the directory can be removed

    REQUIRE_FALSE(result);
    CHECK(keyOf(result.error()) == "error.file.readOnly");
    CHECK(result.error().code == ErrorCode::PermissionDenied);
    CHECK(readRaw(target) == QByteArray("%PDF-1.7 original"));
    CHECK(readRaw(replacement) == QByteArray("new"));
    CHECK(entries(dir) == QStringList{QStringLiteral("locked.pdf"), QStringLiteral("locked.pdf.saving")});
}

TEST_CASE("durable replace refuses a missing replacement and leaves the target", "[platform][durablefiles]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString target = plantDownloadedTarget(dir, QStringLiteral("kept.pdf"));

    win::WinDurableFiles files;
    const Status result = files.replaceFile(target, dir.filePath(QStringLiteral("never-written.saving")));

    REQUIRE_FALSE(result);
    CHECK(keyOf(result.error()) == "error.file.notFound");
    CHECK(result.error().code == ErrorCode::NotFound);
    CHECK(result.error().context.contains(QStringLiteral("never-written.saving")));
    CHECK(readRaw(target) == QByteArray("%PDF-1.7 original"));
    CHECK(readRaw(target + QStringLiteral(":Zone.Identifier")) == kZone);
    CHECK(entries(dir) == QStringList{QStringLiteral("kept.pdf")});
}

TEST_CASE("durable replace moves the replacement to a target that does not exist", "[platform][durablefiles]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString replacement = dir.filePath(QStringLiteral("new.pdf.saving"));
    writeRaw(replacement, "first save");

    win::WinDurableFiles files;
    REQUIRE(files.replaceFile(dir.filePath(QStringLiteral("new.pdf")), replacement));
    CHECK(readRaw(dir.filePath(QStringLiteral("new.pdf"))) == QByteArray("first save"));
    CHECK(entries(dir) == QStringList{QStringLiteral("new.pdf")});

    SECTION("but not into a directory that does not exist")
    {
        const QString again = dir.filePath(QStringLiteral("again.saving"));
        writeRaw(again, "x");
        const Status result = files.replaceFile(dir.filePath(QStringLiteral("missing-dir/new.pdf")), again);
        REQUIRE_FALSE(result);
        CHECK(keyOf(result.error()) == "error.file.notFound");
        CHECK(readRaw(again) == QByteArray("x"));
    }
}

// ==========================================================================================
// A-49 -- syncToDisk is called, and what that proves is stated
// ==========================================================================================

TEST_CASE("durable sync flushes a written file and refuses a missing one by key", "[platform][durablefiles]")
{
    // WHAT THIS PROVES, AND ALL IT PROVES: FlushFileBuffers was called on the file and Windows accepted
    // it. No test can observe durability across a power loss.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("written.pdf"));
    writeRaw(path, "%PDF-1.7 to be flushed");

    win::WinDurableFiles files;
    CHECK(files.syncToDisk(path));
    CHECK(readRaw(path) == QByteArray("%PDF-1.7 to be flushed"));   // opened for write, not truncated

    const QString missing = dir.filePath(QStringLiteral("not-there.pdf"));
    const Status result = files.syncToDisk(missing);
    REQUIRE_FALSE(result);
    CHECK(keyOf(result.error()) == "error.file.notFound");
    CHECK_FALSE(QFile::exists(missing));                               // OPEN_EXISTING: not created
}

// ==========================================================================================
// Stage 3 T8 -- removeFile and processExists, which the save path's temporary files need (A-60)
// ==========================================================================================

TEST_CASE("durable files remove a file and treat an absent one as already removed", "[platform][durablefiles]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral(".report.pdf.rawpage-1.tmp"));
    writeRaw(path, "temporary");

    win::WinDurableFiles files;
    CHECK(files.removeFile(path));
    CHECK_FALSE(QFile::exists(path));
    CHECK(files.removeFile(path));                     // already gone: success, by the interface's rule

    // A file held open without delete sharing cannot be removed, and says so by key.
    writeRaw(path, "held");
    HANDLE holder = CreateFileW(wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(holder != INVALID_HANDLE_VALUE);
    const Status held = files.removeFile(path);
    CloseHandle(holder);
    REQUIRE_FALSE(held);
    CHECK(keyOf(held.error()) == "error.file.inUse");
    CHECK(QFile::exists(path));
}

TEST_CASE("durable files know this process exists and a finished one does not", "[platform][durablefiles]")
{
    win::WinDurableFiles files;
    CHECK(files.processExists(static_cast<qint64>(GetCurrentProcessId())));
    CHECK_FALSE(files.processExists(0));
    CHECK_FALSE(files.processExists(-5));

    // A real child that has EXITED: its id must read as gone, even while a handle to it is still open --
    // which is the case OpenProcess alone gets wrong, since a handle keeps the process object alive.
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION child{};
    std::wstring command = L"cmd.exe /c exit 0";
    REQUIRE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &child));
    REQUIRE(WaitForSingleObject(child.hProcess, 10000) == WAIT_OBJECT_0);
    CHECK_FALSE(files.processExists(static_cast<qint64>(child.dwProcessId)));
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
}
