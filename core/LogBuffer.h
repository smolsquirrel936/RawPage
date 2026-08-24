// The last N log records, in memory, so something can show them -- Stage 1 T14.
//
// ---------------------------------------------------------------------------------------------
// WHY THIS EXISTS RATHER THAN `ui/panes/LogPane` CHAINING THE SINK.
//
// `core/Log.h` has exactly one sink and `setLogSink` REPLACES it. The obvious way to feed a log
// pane is therefore for the pane to capture the installed sink at construction and install a
// lambda that calls it and then appends -- and that is wrong three times over:
//
//   * `app/CompositionRoot::applyLoggingSettings()` re-installs the sink whenever a `logging/` key
//     changes, which is precisely what the Preferences page T14 also ships does. The pane's chain
//     is dropped, silently, the first time the user changes the log level -- the one moment they
//     are certainly looking at the log.
//   * There can be more than one window (T12 tears a tab out into one), so there can be more than
//     one pane, and chained lambdas would nest in construction order and unwind in whatever order
//     the windows happen to close in.
//   * A lambda installed by a widget outlives the widget unless something removes it. That is
//     issue I-12 exactly -- an object reached after its destructor ran -- arriving through a third
//     door.
//
// So the buffer is a plain object with a lifetime somebody owns: `CompositionRoot` builds it,
// feeds it from inside the one sink it already installs, and hands it down through `Services`. A
// pane SUBSCRIBES and unsubscribes in its destructor, with a token, exactly as `core::Settings`
// does -- and for the same reason that class has tokens rather than a bare handler list.
//
// ---------------------------------------------------------------------------------------------
// WHAT IT DOES NOT DO.
//
// It does not filter. It is fed from inside the sink that has ALREADY applied `logging/level` and
// `logging/subsystems`, so what reaches it is what the user asked to record; a pane narrowing that
// further is a view, not a second policy. The consequence is worth stating because it will
// otherwise be reported as a bug: raising the pane's own level filter above the global threshold
// shows nothing new, because the records were never recorded. The pane says so on screen.
//
// It does not write a file either -- `core/` may not, and `tools/layer-lint` enforces it. The
// rolling file is `app/LogFile`.
//
// ---------------------------------------------------------------------------------------------
// THREADING.
//
// `append` is called from whatever thread emitted the record, and from Stage 2 that is routinely a
// render worker. So the buffer locks, and it notifies subscribers ON THE APPENDING THREAD. A
// subscriber that touches widgets must marshal to the UI thread itself -- `LogPane` does, with a
// queued signal -- and that responsibility is stated here rather than assumed, because the failure
// mode is a crash that only happens once there are worker threads, which is a stage away.

#ifndef RAWPAGE_CORE_LOGBUFFER_H
#define RAWPAGE_CORE_LOGBUFFER_H

#include "core/Log.h"

#include <QDateTime>
#include <QString>

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace rawpage::core {

// A record plus the moment it happened. `LogRecordData` deliberately carries no timestamp -- a log
// SINK may not want one, and stamping inside the macro would cost a clock read on a filtered-out
// line. The buffer stamps on append, which is as close to the event as anything that keeps records.
struct LogEntry {
    LogLevel     level = LogLevel::Info;
    LogSubsystem subsystem = LogSubsystem::Ui;
    QString      message;
    QDateTime    when;
};

class LogBuffer {
public:
    // 2000 records is roughly a day of ordinary use at `info` and a few minutes at `trace`. It is a
    // constructor parameter rather than a settings key because it bounds MEMORY rather than
    // behaviour: `logging/retainFiles` is the user-facing knob and it bounds the files on disk.
    static constexpr std::size_t kDefaultCapacity = 2000;

    explicit LogBuffer(std::size_t capacity = kDefaultCapacity);
    ~LogBuffer();

    LogBuffer(const LogBuffer&) = delete;
    LogBuffer& operator=(const LogBuffer&) = delete;

    void append(const LogRecordData& record);

    // Oldest first, which is the order a log is read in. A copy rather than a reference: the deque
    // is mutated from other threads, and handing out a reference to it would be handing out a race.
    std::vector<LogEntry> entries() const;

    std::size_t size() const;
    std::size_t capacity() const { return capacity_; }

    // How many records fell off the front. Reported rather than silent: a pane showing 2000 lines
    // that begins mid-sentence is confusing, and "12,431 earlier records were discarded" is not.
    std::size_t discardedCount() const;

    void clear();

    // --- Notification -------------------------------------------------------------------------
    // The same token shape as `core::Settings::subscribe`. A subscriber that outlives this object
    // is a bug; a subscriber that forgets to unsubscribe before it dies is a crash, which is why
    // the token exists rather than a bare function list.
    //
    // CALLED ON THE APPENDING THREAD. See the header comment.
    using Handler = std::function<void(const LogEntry&)>;
    int  subscribe(Handler handler);
    void unsubscribe(int token);

private:
    mutable std::mutex   mutex_;
    std::deque<LogEntry> entries_;
    std::size_t          capacity_;
    std::size_t          discarded_ = 0;

    std::map<int, Handler> handlers_;
    int                    nextToken_ = 1;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_LOGBUFFER_H
