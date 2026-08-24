#include "core/LogBuffer.h"

#include <algorithm>
#include <utility>

namespace rawpage::core {

LogBuffer::LogBuffer(std::size_t capacity)
    // A capacity of zero would make `append` a no-op that looks like a working buffer, and the
    // symptom would be an empty pane on a machine where logging is plainly working. One is the
    // smallest honest answer.
    : capacity_(std::max<std::size_t>(capacity, 1))
{
}

LogBuffer::~LogBuffer() = default;

void LogBuffer::append(const LogRecordData& record)
{
    LogEntry entry;
    entry.level     = record.level;
    entry.subsystem = record.subsystem;
    entry.message   = record.message;
    entry.when      = QDateTime::currentDateTime();

    // The handlers are COPIED under the lock and called outside it. A handler that logs -- and one
    // will, eventually, because a log pane failing to load an icon is exactly the sort of thing
    // that logs -- would otherwise re-enter `append` on a locked non-recursive mutex and deadlock.
    std::vector<Handler> handlers;
    {
        const std::lock_guard<std::mutex> guard(mutex_);
        entries_.push_back(entry);
        while (entries_.size() > capacity_) {
            entries_.pop_front();
            ++discarded_;
        }

        handlers.reserve(handlers_.size());
        for (const auto& [token, handler] : handlers_) handlers.push_back(handler);
    }

    for (const Handler& handler : handlers)
        if (handler) handler(entry);
}

std::vector<LogEntry> LogBuffer::entries() const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    return std::vector<LogEntry>(entries_.begin(), entries_.end());
}

std::size_t LogBuffer::size() const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    return entries_.size();
}

std::size_t LogBuffer::discardedCount() const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    return discarded_;
}

void LogBuffer::clear()
{
    const std::lock_guard<std::mutex> guard(mutex_);
    entries_.clear();

    // The discard count is NOT reset. It answers "how much of this session did I never see", and a
    // user who cleared the pane has not un-discarded anything. Resetting it here would make the
    // count mean "since the last clear", which is a different and less useful question.
}

int LogBuffer::subscribe(Handler handler)
{
    const std::lock_guard<std::mutex> guard(mutex_);
    const int token = nextToken_++;
    handlers_.emplace(token, std::move(handler));
    return token;
}

void LogBuffer::unsubscribe(int token)
{
    const std::lock_guard<std::mutex> guard(mutex_);
    handlers_.erase(token);
}

} // namespace rawpage::core
