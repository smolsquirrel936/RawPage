// Subsystem-tagged, level-filtered logging.
//
// THE RULE THIS FILE ENFORCES BY TYPE (Features.md 9.5): *never write document text or file
// paths into a log above the debug level.* A support bundle is routinely mailed around; a
// log that quotes the user's document is a confidentiality incident that nobody intended.
//
// A code-review rule would be enforced by whoever happens to review. So paths and document
// text are passed as Redacted<T>, which renders as <redacted> at info and above and as the
// real value at debug and trace. The safe thing is the easy thing, and the unsafe thing
// requires deliberately unwrapping.
//
// No sink is defined here beyond an in-process callback: core/ must not write files
// (tools/layer-lint enforces that), so the rolling log file is wired by ui/ and app/ at
// Stage 1. Until then the default sink drops everything, which is correct for a headless
// unit test.

#ifndef RECTO_CORE_LOG_H
#define RECTO_CORE_LOG_H

#include <QString>
#include <QStringList>

#include <functional>

namespace recto::core {

enum class LogLevel { Error = 0, Warning = 1, Info = 2, Debug = 3, Trace = 4 };

// Exactly the seven of Features.md 9.5. Per-subsystem filtering is what makes a render
// investigation readable without turning off everything else.
enum class LogSubsystem { Render, Parse, Save, Sign, Ocr, Batch, Ui };

constexpr const char* logLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Error:   return "error";
    case LogLevel::Warning: return "warning";
    case LogLevel::Info:    return "info";
    case LogLevel::Debug:   return "debug";
    case LogLevel::Trace:   return "trace";
    }
    return "?";
}

constexpr const char* logSubsystemName(LogSubsystem subsystem) {
    switch (subsystem) {
    case LogSubsystem::Render: return "render";
    case LogSubsystem::Parse:  return "parse";
    case LogSubsystem::Save:   return "save";
    case LogSubsystem::Sign:   return "sign";
    case LogSubsystem::Ocr:    return "ocr";
    case LogSubsystem::Batch:  return "batch";
    case LogSubsystem::Ui:     return "ui";
    }
    return "?";
}

// ---------------------------------------------------------------------------------------
// Redaction
// ---------------------------------------------------------------------------------------
// Wrap anything that could identify or reveal the user's document: file paths, extracted
// text, form field values, bookmark titles, search terms.
template <class T>
class Redacted {
public:
    explicit Redacted(T value) : value_(std::move(value)) {}
    const T& unredacted() const { return value_; }

private:
    T value_;
};

template <class T>
Redacted<T> redact(T value) { return Redacted<T>(std::move(value)); }

// ---------------------------------------------------------------------------------------
// Sink
// ---------------------------------------------------------------------------------------
struct LogRecordData {
    LogLevel level = LogLevel::Info;
    LogSubsystem subsystem = LogSubsystem::Ui;
    QString message;
};

using LogSink = std::function<void(const LogRecordData&)>;

namespace detail {
inline LogSink& sinkRef() {
    static LogSink sink;                 // empty by default: a headless test logs nothing
    return sink;
}
inline LogLevel& thresholdRef() {
    static LogLevel threshold = LogLevel::Info;
    return threshold;
}
} // namespace detail

inline void setLogSink(LogSink sink) { detail::sinkRef() = std::move(sink); }
inline void setLogThreshold(LogLevel level) { detail::thresholdRef() = level; }
inline LogLevel logThreshold() { return detail::thresholdRef(); }

inline bool logEnabled(LogLevel level) {
    return static_cast<int>(level) <= static_cast<int>(detail::thresholdRef());
}

// ---------------------------------------------------------------------------------------
// The record
// ---------------------------------------------------------------------------------------
class LogRecord {
public:
    LogRecord(LogLevel level, LogSubsystem subsystem) : level_(level), subsystem_(subsystem) {}

    ~LogRecord() {
        if (!active_) return;
        const LogSink& sink = detail::sinkRef();
        if (sink) sink(LogRecordData{level_, subsystem_, parts_.join(QLatin1Char(' '))});
    }

    LogRecord(const LogRecord&) = delete;
    LogRecord& operator=(const LogRecord&) = delete;

    LogRecord& operator<<(const QString& text) { if (active_) parts_ << text; return *this; }
    LogRecord& operator<<(QLatin1String text) { if (active_) parts_ << QString(text); return *this; }
    LogRecord& operator<<(const char* text) { if (active_) parts_ << QString::fromUtf8(text); return *this; }
    LogRecord& operator<<(int value) { if (active_) parts_ << QString::number(value); return *this; }
    LogRecord& operator<<(qint64 value) { if (active_) parts_ << QString::number(value); return *this; }
    LogRecord& operator<<(double value) { if (active_) parts_ << QString::number(value); return *this; }

    // The whole point of the file. Debug and trace see the value; info, warning and error
    // -- the levels that reach a shipped log file -- see a placeholder.
    template <class T>
    LogRecord& operator<<(const Redacted<T>& redacted) {
        if (!active_) return *this;
        if (static_cast<int>(level_) >= static_cast<int>(LogLevel::Debug)) {
            *this << redacted.unredacted();
        } else {
            parts_ << QStringLiteral("<redacted>");
        }
        return *this;
    }

    void setInactive() { active_ = false; }

private:
    LogLevel level_;
    LogSubsystem subsystem_;
    QStringList parts_;
    bool active_ = true;
};

// ---------------------------------------------------------------------------------------
// Macros
// ---------------------------------------------------------------------------------------
// Guarded by logEnabled() so the streamed arguments are not evaluated when the level is
// filtered out -- formatting a page's text only to discard it is not free.
#define RECTO_LOG(subsystem, level)                                                        \
    if (!::recto::core::logEnabled(level)) {                                               \
    } else                                                                                 \
        ::recto::core::LogRecord(level, subsystem)

#define RECTO_LOG_ERROR(sub)   RECTO_LOG(sub, ::recto::core::LogLevel::Error)
#define RECTO_LOG_WARNING(sub) RECTO_LOG(sub, ::recto::core::LogLevel::Warning)
#define RECTO_LOG_INFO(sub)    RECTO_LOG(sub, ::recto::core::LogLevel::Info)
#define RECTO_LOG_DEBUG(sub)   RECTO_LOG(sub, ::recto::core::LogLevel::Debug)

// Trace compiles out entirely in release: the branch, the arguments, and the record. It is
// the level used inside per-tile and per-glyph loops, where even a predictable branch is
// worth removing.
#ifdef NDEBUG
#define RECTO_LOG_TRACE(sub)                                                               \
    if constexpr (true) {                                                                  \
    } else                                                                                 \
        ::recto::core::LogRecord(::recto::core::LogLevel::Trace, sub)
#else
#define RECTO_LOG_TRACE(sub) RECTO_LOG(sub, ::recto::core::LogLevel::Trace)
#endif

} // namespace recto::core

#endif // RECTO_CORE_LOG_H
