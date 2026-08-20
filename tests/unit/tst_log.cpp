#include "core/Log.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace rawpage::core;

namespace {

struct Captured {
    std::vector<LogRecordData> records;

    void install()
    {
        records.clear();
        setLogSink([this](const LogRecordData& record) { records.push_back(record); });
    }
    void reset()
    {
        setLogSink({});
        setLogThreshold(LogLevel::Info);
    }
};

} // namespace

TEST_CASE("records reach the sink with subsystem and level", "[tst_log]")
{
    Captured captured;
    captured.install();
    setLogThreshold(LogLevel::Info);

    RAWPAGE_LOG_INFO(LogSubsystem::Render) << "tile" << 4 << "ready";

    REQUIRE(captured.records.size() == 1);
    CHECK(captured.records[0].level == LogLevel::Info);
    CHECK(captured.records[0].subsystem == LogSubsystem::Render);
    CHECK(captured.records[0].message == QStringLiteral("tile 4 ready"));

    captured.reset();
}

TEST_CASE("the threshold filters lower levels out", "[tst_log]")
{
    Captured captured;
    captured.install();
    setLogThreshold(LogLevel::Warning);

    RAWPAGE_LOG_ERROR(LogSubsystem::Save) << "kept";
    RAWPAGE_LOG_WARNING(LogSubsystem::Save) << "kept";
    RAWPAGE_LOG_INFO(LogSubsystem::Save) << "dropped";
    RAWPAGE_LOG_DEBUG(LogSubsystem::Save) << "dropped";

    CHECK(captured.records.size() == 2);
    captured.reset();
}

// =======================================================================================
// The rule from Features.md 9.5, enforced by the type system rather than by review:
// never write document text or file paths into a log above the debug level. A support
// bundle gets mailed around; a log quoting the user document is a confidentiality
// incident nobody intended.
// =======================================================================================
TEST_CASE("a redacted value is hidden at info and above", "[tst_log]")
{
    Captured captured;
    captured.install();
    setLogThreshold(LogLevel::Trace);   // nothing filtered; only redaction applies

    const QString path = QStringLiteral("C:/Users/someone/Taxes 2025.pdf");

    RAWPAGE_LOG_ERROR(LogSubsystem::Parse) << "cannot open" << redact(path);
    RAWPAGE_LOG_WARNING(LogSubsystem::Parse) << "retrying" << redact(path);
    RAWPAGE_LOG_INFO(LogSubsystem::Parse) << "opened" << redact(path);

    REQUIRE(captured.records.size() == 3);
    for (const LogRecordData& record : captured.records) {
        CHECK(record.message.contains(QStringLiteral("<redacted>")));
        CHECK_FALSE(record.message.contains(QStringLiteral("Taxes")));
        CHECK_FALSE(record.message.contains(QStringLiteral("someone")));
    }

    captured.reset();
}

TEST_CASE("a redacted value is visible at debug and below", "[tst_log]")
{
    Captured captured;
    captured.install();
    setLogThreshold(LogLevel::Trace);

    const QString text = QStringLiteral("paragraph one");
    RAWPAGE_LOG_DEBUG(LogSubsystem::Parse) << "extracted" << redact(text);

    REQUIRE(captured.records.size() == 1);
    CHECK(captured.records[0].message.contains(text));

    captured.reset();
}

TEST_CASE("filtered records evaluate none of their arguments", "[tst_log]")
{
    Captured captured;
    captured.install();
    setLogThreshold(LogLevel::Error);

    int callCount = 0;
    auto expensive = [&callCount]() -> QString {
        ++callCount;
        return QStringLiteral("x");
    };

    RAWPAGE_LOG_DEBUG(LogSubsystem::Render) << expensive();

    // Formatting a page worth of text and then discarding it is not free; the macro guards
    // before the streamed arguments are touched at all.
    CHECK(callCount == 0);
    CHECK(captured.records.empty());

    captured.reset();
}

TEST_CASE("subsystem and level names are stable", "[tst_log]")
{
    // These strings land in log files that outlive any given build, so they are part of the
    // format rather than an implementation detail.
    CHECK(std::string(logLevelName(LogLevel::Error)) == "error");
    CHECK(std::string(logLevelName(LogLevel::Trace)) == "trace");
    CHECK(std::string(logSubsystemName(LogSubsystem::Render)) == "render");
    CHECK(std::string(logSubsystemName(LogSubsystem::Ocr)) == "ocr");
}
