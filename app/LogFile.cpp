#include "app/LogFile.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace rawpage::app {
namespace {

using core::LogSubsystem;

constexpr auto kFilePrefix = "rawpage-";
constexpr auto kFileSuffix = ".log";

// Sorts lexicographically in time order, which is what lets `prune` keep the newest N by name
// alone -- no stat call per file, and no dependence on a modification time that a backup tool or a
// file copy is free to rewrite.
QString timestampForName()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
}

} // namespace

QString LogFile::defaultDirectory()
{
    // AppLocalDataLocation is %LOCALAPPDATA%\RawPage on Windows -- and it is exactly one level
    // deep because `main.cpp` deliberately does not set an organization name (issue I-13).
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) return QString();
    return QDir(base).filePath(QStringLiteral("logs"));
}

QString LogFile::formatLine(const core::LogRecordData& record)
{
    // The same shape `main.cpp`'s console sink writes, with a timestamp in front: a support bundle
    // and a developer's stderr showing different text for one record is a difference somebody will
    // spend an afternoon on.
    return QStringLiteral("%1 [%2][%3] %4")
        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
             QLatin1String(core::logLevelName(record.level)),
             QLatin1String(core::logSubsystemName(record.subsystem)),
             record.message);
}

LogFile::LogFile(Options options) : options_(std::move(options))
{
    if (options_.directory.isEmpty()) options_.directory = defaultDirectory();
    if (options_.maxBytes < 1024) options_.maxBytes = 1024;
    if (options_.retainFiles < 1) options_.retainFiles = 1;

    openNewFile();
}

LogFile::~LogFile()
{
    closeFile();
}

void LogFile::openNewFile()
{
    closeFile();
    if (options_.directory.isEmpty()) return;

    QDir directory(options_.directory);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        if (!openFailureReported_) {
            openFailureReported_ = true;
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
                << "log file: cannot create" << options_.directory << "-- file logging is off";
        }
        return;
    }

    // PRUNE BEFORE OPENING, not after: pruning after would have to know to exclude the file it had
    // just created, and "keep the newest five" applied to a set that includes an empty new file is
    // a rule that quietly keeps four.
    prune(options_.directory, options_.retainFiles - 1);

    const QString name = QLatin1String(kFilePrefix) + timestampForName() + QLatin1String(kFileSuffix);
    auto file = std::make_unique<QFile>(directory.filePath(name));

    // Append rather than Truncate. Two processes started in the same millisecond would otherwise
    // have the second one erase the first one's lines -- rare, and the kind of rare that eats a
    // support case rather than announcing itself.
    if (!file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {  // layer-lint: allow-filesystem-write(the rolling log file, Features.md 9.5; app/ owns the process's output channels and core/ may not write)
        if (!openFailureReported_) {
            openFailureReported_ = true;
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
                << "log file: cannot open" << file->fileName() << "-- file logging is off";
        }
        return;
    }

    written_ = file->size();
    file_ = std::move(file);
}

void LogFile::closeFile()
{
    if (!file_) return;
    file_->flush();
    file_->close();
    file_.reset();
    written_ = 0;
}

void LogFile::append(const core::LogRecordData& record)
{
    const QByteArray line = (formatLine(record) + QLatin1Char('\n')).toUtf8();

    const std::lock_guard<std::mutex> guard(mutex_);
    if (!file_) return;

    // Rotate BEFORE the write that would exceed the cap rather than after it, so the cap is a
    // bound rather than a suggestion. `openNewFile` is called with the lock held and does not log
    // on the success path; the failure path logs, which re-enters the sink -- and the sink calls
    // `append`, which would deadlock on this non-recursive mutex. So the rotation failure is
    // recorded and reported after the lock is released.
    bool rotationFailed = false;
    if (written_ + line.size() > options_.maxBytes) {
        openNewFile();
        rotationFailed = (file_ == nullptr);
    }

    if (file_) {
        written_ += file_->write(line);
        file_->flush();   // every line: the ones that matter are written just before a process stops
    }

    if (rotationFailed) {
        // Deliberately not logged. There is no sink that could carry it -- the file half is what
        // just failed, and the console half already received whatever `openNewFile` reported.
    }
}

QString LogFile::filePath() const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    return file_ ? file_->fileName() : QString();
}

bool LogFile::isOpen() const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    return file_ != nullptr;
}

std::size_t LogFile::prune(const QString& directory, int retainFiles)
{
    if (directory.isEmpty()) return 0;
    const int keep = std::max(retainFiles, 0);

    QDir dir(directory);
    if (!dir.exists()) return 0;

    // By name, which is by time -- see `timestampForName`. Newest first, so the survivors are the
    // head of the list and everything past `keep` goes.
    const QStringList names = dir.entryList(
        QStringList{QLatin1String(kFilePrefix) + QStringLiteral("*") + QLatin1String(kFileSuffix)},
        QDir::Files, QDir::Name | QDir::Reversed);

    std::size_t removed = 0;
    for (int i = keep; i < names.size(); ++i) {
        if (QFile::remove(dir.filePath(names.at(i)))) ++removed;  // layer-lint: allow-filesystem-write(log retention, the reader of logging/retainFiles; it deletes only rawpage-*.log files it wrote, in its own directory)
    }
    return removed;
}

} // namespace rawpage::app
