// Stage 1 T14 -- `core::LogBuffer` and `app::LogFile`, the two things a log has to reach before
// anybody can read it.
//
// Guide case A-93. Both are exercised headlessly: the buffer names no Qt GUI type at all, and the
// file uses QtCore only, which is what lets it be compiled into this suite the way
// `app/CommandLine.cpp` already is rather than needing the executable.

#include <catch2/catch_test_macros.hpp>

#include "app/LogFile.h"
#include "core/Log.h"
#include "core/LogBuffer.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <vector>

using namespace rawpage;

namespace {

core::LogRecordData record(core::LogLevel level, core::LogSubsystem subsystem, const char* message)
{
    return core::LogRecordData{level, subsystem, QString::fromUtf8(message)};
}

} // namespace

TEST_CASE("the buffer keeps the last N records and counts what fell off", "[logbuffer]")
{
    core::LogBuffer buffer(3);

    for (int i = 0; i < 5; ++i)
        buffer.append(record(core::LogLevel::Info, core::LogSubsystem::Ui,
                             QByteArray::number(i).constData()));

    const std::vector<core::LogEntry> entries = buffer.entries();
    REQUIRE(entries.size() == 3);

    // OLDEST FIRST, which is the order a log is read in. Asserting the contents rather than only
    // the size is what catches a ring that drops from the wrong end -- a bug whose only symptom is
    // a pane that always shows the beginning of the session.
    CHECK(entries.front().message == QStringLiteral("2"));
    CHECK(entries.back().message == QStringLiteral("4"));

    // The two that fell off are COUNTED. A pane showing three lines that begins mid-sentence is
    // confusing; one that says two earlier records were discarded is not.
    CHECK(buffer.discardedCount() == 2);
}

TEST_CASE("clearing empties the records and deliberately keeps the discard count", "[logbuffer]")
{
    core::LogBuffer buffer(2);
    for (int i = 0; i < 4; ++i)
        buffer.append(record(core::LogLevel::Info, core::LogSubsystem::Ui, "x"));

    REQUIRE(buffer.discardedCount() == 2);
    buffer.clear();

    CHECK(buffer.size() == 0);

    // NOT reset, and this is the assertion that pins the decision rather than the code. The count
    // answers "how much of this session did I never see"; a user who cleared the pane has not
    // un-discarded anything, and resetting would silently change the question to "since the last
    // clear".
    CHECK(buffer.discardedCount() == 2);
}

TEST_CASE("a subscriber hears every record until it unsubscribes", "[logbuffer]")
{
    core::LogBuffer buffer;

    std::vector<QString> heard;
    const int token = buffer.subscribe([&heard](const core::LogEntry& entry) {
        heard.push_back(entry.message);
    });

    buffer.append(record(core::LogLevel::Warning, core::LogSubsystem::Parse, "one"));
    buffer.append(record(core::LogLevel::Error, core::LogSubsystem::Save, "two"));
    REQUIRE(heard.size() == 2);
    CHECK(heard.front() == QStringLiteral("one"));

    buffer.unsubscribe(token);
    buffer.append(record(core::LogLevel::Info, core::LogSubsystem::Ui, "three"));

    // The record still reached the BUFFER -- unsubscribing stops the notification, not the
    // recording. A pane that closed must not take the log with it.
    CHECK(heard.size() == 2);
    CHECK(buffer.size() == 3);
}

TEST_CASE("a handler that logs does not deadlock the buffer it is logging into", "[logbuffer]")
{
    // The hazard is real rather than theoretical: a log pane failing to do something is exactly the
    // sort of thing that logs, and `append` holds a non-recursive mutex. The handlers are copied
    // under the lock and called outside it, and this is the case that fails if that is ever
    // simplified back.
    core::LogBuffer buffer;

    int depth = 0;
    buffer.subscribe([&buffer, &depth](const core::LogEntry&) {
        if (depth++ > 0) return;   // one level, or the recursion is the test's fault rather than the code's
        buffer.append(core::LogRecordData{core::LogLevel::Error, core::LogSubsystem::Ui,
                                          QStringLiteral("logged from a handler")});
    });

    buffer.append(core::LogRecordData{core::LogLevel::Info, core::LogSubsystem::Ui,
                                      QStringLiteral("the first one")});

    CHECK(buffer.size() == 2);
}

// ===============================================================================================
// app/LogFile
// ===============================================================================================

TEST_CASE("the log file writes the same line shape the console sink does", "[logfile]")
{
    const QString line = app::LogFile::formatLine(
        core::LogRecordData{core::LogLevel::Warning, core::LogSubsystem::Render,
                            QStringLiteral("tile 4 was slow")});

    // The level and the subsystem in the brackets `main.cpp`'s console sink uses, so that a support
    // bundle and a developer's stderr are the same text for one record.
    CHECK(line.contains(QStringLiteral("[warning][render]")));
    CHECK(line.endsWith(QStringLiteral("tile 4 was slow")));
}

TEST_CASE("the log file opens, writes, and retains only the newest files", "[logfile]")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());

    app::LogFile::Options options;
    options.directory = directory.path();
    options.retainFiles = 3;

    {
        app::LogFile file(options);
        REQUIRE(file.isOpen());
        file.append(core::LogRecordData{core::LogLevel::Info, core::LogSubsystem::Ui,
                                        QStringLiteral("hello")});

        QFile written(file.filePath());
        REQUIRE(written.open(QIODevice::ReadOnly | QIODevice::Text));
        CHECK(QTextStream(&written).readAll().contains(QStringLiteral("hello")));
    }

    // Retention: five files on disk, a new run opens a sixth, and three survive -- the two it kept
    // plus its own. Asserted through the real `prune`, over files this test wrote itself rather
    // than over the user's logs.
    for (int i = 0; i < 5; ++i) {
        QFile stale(QDir(directory.path())
                        .filePath(QStringLiteral("rawpage-2020010%1-000000-000.log").arg(i)));
        REQUIRE(stale.open(QIODevice::WriteOnly));
        stale.close();
    }

    const std::size_t removed = app::LogFile::prune(directory.path(), 2);
    CHECK(removed >= 3);

    const QStringList left = QDir(directory.path())
                                 .entryList(QStringList{QStringLiteral("rawpage-*.log")}, QDir::Files);
    CHECK(left.size() == 2);
}

TEST_CASE("a log directory that cannot be created is not fatal", "[logfile]")
{
    // A read-only or full profile must not prevent the application from starting: the file half of
    // logging is simply absent and one warning says so. The directory is pointed at an existing
    // FILE, which `mkpath` cannot turn into a directory on any platform -- a genuine failure rather
    // than a path nobody happens to have permission for, which would pass on the wrong machine.
    QTemporaryDir directory;
    REQUIRE(directory.isValid());

    const QString blocker = QDir(directory.path()).filePath(QStringLiteral("not-a-directory"));
    QFile file(blocker);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.close();

    app::LogFile::Options options;
    options.directory = blocker;
    app::LogFile logFile(options);

    CHECK_FALSE(logFile.isOpen());
    CHECK(logFile.filePath().isEmpty());

    // And appending to a closed file is a no-op rather than a crash, which is what the application
    // relies on: the sink calls this for every record whether or not a file was ever opened.
    logFile.append(core::LogRecordData{core::LogLevel::Info, core::LogSubsystem::Ui,
                                       QStringLiteral("nowhere to go")});
    CHECK_FALSE(logFile.isOpen());
}
