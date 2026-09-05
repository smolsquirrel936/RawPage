#include "pdf/render/GlyphCache.h"

#include "core/Log.h"

#include <QFile>

#include <utility>

namespace rawpage::pdf::render {
namespace {

using core::LogSubsystem;

} // namespace

GlyphCache::GlyphCache() : GlyphCache(Config{}) {}

GlyphCache::GlyphCache(Config config) : config_(config) {}

const std::vector<std::uint8_t>& GlyphCache::faceData(const QString& filePath)
{
    const std::lock_guard<std::mutex> guard(mutex_);
    ++requests_;

    if (filePath.isEmpty()) return empty_;

    if (const auto found = index_.find(filePath); found != index_.end()) {
        ++hits_;
        // Recency, by moving the node rather than by copying it: `Entry` holds the whole font file,
        // and an LRU that copied its payload on every hit would cost more than the disk read it
        // exists to avoid.
        order_.splice(order_.begin(), order_, found->second);
        return found->second->bytes;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        // Not cached as a failure. A font can be installed while the application runs, and a
        // remembered "absent" would keep substituting past the moment the user fixed it -- the same
        // argument that makes `WinFontResolver` refresh its collection on every query.
        RAWPAGE_LOG_WARNING(LogSubsystem::Render)
            << "font: cannot read face file" << filePath << "--" << file.errorString();
        return empty_;
    }

    const QByteArray raw = file.readAll();
    if (raw.isEmpty()) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Render) << "font: face file is empty" << filePath;
        return empty_;
    }

    // A single face larger than the whole cache is served and NOT retained: retaining it would
    // evict everything and then evict itself, so the next ask reads the disk again anyway while the
    // cache reports a face it does not have. There is no such font today -- the cap is 64 MiB and
    // the largest face on a Windows machine is under 20 -- and the branch exists because "no font is
    // that big" is a fact about today's fonts rather than about this code.
    if (static_cast<std::size_t>(raw.size()) > config_.capacityBytes) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Render)
            << "font: face larger than the cache, not retained --" << filePath
            << static_cast<int>(raw.size() / 1024) << "KiB";
        return empty_;
    }

    Entry entry;
    entry.path = filePath;
    entry.bytes.assign(reinterpret_cast<const std::uint8_t*>(raw.constData()),
                       reinterpret_cast<const std::uint8_t*>(raw.constData()) + raw.size());

    bytesHeld_ += entry.bytes.size();
    order_.push_front(std::move(entry));
    index_[filePath] = order_.begin();

    RAWPAGE_LOG_DEBUG(LogSubsystem::Render)
        << "font: face data read --" << filePath << static_cast<int>(order_.front().bytes.size() / 1024)
        << "KiB;" << static_cast<int>(index_.size()) << "faces held";

    // Eviction runs AFTER the insert and never touches the head, so the reference returned below
    // survives it. Evicting first would be the obvious order and would be wrong the moment a single
    // face is a large fraction of the cap: it would free space for a face whose size is not yet
    // known here.
    evictToFit();
    return order_.front().bytes;
}

void GlyphCache::evictToFit()
{
    while (bytesHeld_ > config_.capacityBytes && order_.size() > 1) {
        const auto victim = std::prev(order_.end());
        bytesHeld_ -= victim->bytes.size();
        index_.erase(victim->path);
        order_.erase(victim);
    }
}

std::size_t GlyphCache::bytesHeld() const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    return bytesHeld_;
}

std::size_t GlyphCache::facesHeld() const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    return index_.size();
}

std::size_t GlyphCache::requests() const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    return requests_;
}

std::size_t GlyphCache::hits() const
{
    const std::lock_guard<std::mutex> guard(mutex_);
    return hits_;
}

void GlyphCache::clear()
{
    const std::lock_guard<std::mutex> guard(mutex_);
    order_.clear();
    index_.clear();
    bytesHeld_ = 0;
    // The counters deliberately survive: they measure what this process ASKED FOR, and a suite that
    // clears between cases still wants the cumulative figure to make sense.
}

} // namespace rawpage::pdf::render
