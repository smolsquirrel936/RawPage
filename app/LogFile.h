// The rolling log file -- Features.md §9.5's first bullet, and the reader `logging/retainFiles`
// has been waiting for since T5 declared it.
//
// ---------------------------------------------------------------------------------------------
// WHY IT IS IN `app/` AND NOT IN `core/`.
//
// `core/` may not write to disk and `tools/layer-lint` enforces it, which is exactly why
// `core/Log.h` defines no sink beyond an in-process callback and says so in its own header. So the
// sink lives here, `app/main.cpp` supplies the console half of it, and this supplies the file half.
// Both are fanned out by the one sink `app/CompositionRoot` installs.
//
// The three `allow-filesystem-write` waivers below take the standing census from four to seven.
// That is the point of the census: a rolling log file is a named product feature with a settings
// key, and it is visible in `layer-lint --explain` beside the crash handler's three rather than
// hidden behind a rule change.
//
// ---------------------------------------------------------------------------------------------
// WHAT ROTATION MEANS HERE, since "rolling" is used loosely elsewhere.
//
// One file per PROCESS, named for the moment it started, plus a size cap that starts a further
// file within the same process. Retention is a count of FILES, which is what `logging/retainFiles`
// says and what a user can reason about -- "keep my last five runs" -- rather than a byte budget
// they would have to divide by a line length to understand.
//
// Pruning happens when a file is opened rather than on a timer: a process that never logs enough
// to rotate still prunes once, at startup, which is the moment a user has just reproduced the
// problem and is about to go looking. A background sweep would be a thread this stage does not
// need.
//
// ---------------------------------------------------------------------------------------------
// WHAT IS DELIBERATELY NOT HERE.
//
// No formatting choice of its own: the line shape is the same one `main.cpp`'s console sink
// writes, because a support bundle and a developer's stderr showing different text for one record
// is a difference somebody will spend an afternoon on. And no redaction logic -- `core/Log.h`'s
// `Redacted<T>` has already decided what a record says by the time it reaches a sink, which is the
// whole reason that decision is made by TYPE at the call site rather than by policy at the sink.

#ifndef RAWPAGE_APP_LOGFILE_H
#define RAWPAGE_APP_LOGFILE_H

#include "core/Log.h"

#include <QString>

#include <cstddef>
#include <memory>
#include <mutex>

class QFile;
class QTextStream;

namespace rawpage::app {

class LogFile {
public:
    // %LOCALAPPDATA%\RawPage\logs -- beside `crashes\`, which `app/CrashHandler` owns. Local
    // rather than roaming, and that is the opposite call from `settings.ini` (issue I-13) on
    // purpose: a preference should follow the user to their second machine and a log describes
    // THIS machine's run, so roaming one would be noise in the profile and a lie in the file.
    static QString defaultDirectory();

    struct Options {
        QString directory;                       // empty means defaultDirectory()
        qint64  maxBytes = 4 * 1024 * 1024;      // per file; a further file starts past it
        int     retainFiles = 5;                 // `logging/retainFiles`, clamped by its schema range
    };

    explicit LogFile(Options options);
    ~LogFile();

    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;

    // Called from whatever thread emitted the record. Locks, writes, and flushes -- flushing every
    // line rather than buffering, because the log lines that matter most are the ones written
    // immediately before the process stopped being able to flush anything.
    void append(const core::LogRecordData& record);

    // The file currently being written, or empty if none could be opened. Empty is a legitimate
    // state: a read-only or full profile directory must not prevent the application from starting,
    // so a failure to open is reported once and then the file half of logging is simply absent.
    QString filePath() const;
    bool    isOpen() const;

    // The line as it is written. Exposed so that `main.cpp`'s console sink and this file cannot
    // drift apart, and so a test can assert the shape without parsing a file.
    static QString formatLine(const core::LogRecordData& record);

    // Deletes all but the newest `retainFiles` `rawpage-*.log` files in `directory`. Returns how
    // many were removed. Static and taking its directory so a test can drive it against a scratch
    // folder rather than against the user's own logs.
    static std::size_t prune(const QString& directory, int retainFiles);

private:
    void openNewFile();
    void closeFile();

    Options                options_;
    mutable std::mutex     mutex_;
    std::unique_ptr<QFile> file_;
    qint64                 written_ = 0;

    // True once a failure to open has been reported. A profile that cannot be written to would
    // otherwise emit one warning per log line, which is a log flood caused by logging.
    bool openFailureReported_ = false;
};

} // namespace rawpage::app

#endif // RAWPAGE_APP_LOGFILE_H
