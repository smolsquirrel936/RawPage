#include "core/render/RenderCache.h"

#include "core/Log.h"

#include <algorithm>
#include <utility>

namespace rawpage::core::render {
namespace {

std::size_t imageBytes(const RasterImage& image) noexcept
{
    return static_cast<std::size_t>(image.pixels.size());
}

// Whether an entry belonging to `document` is stale after a mutation of `scope`.
//
// `Scope::touchesPage()` already rounds OUTWARDS -- it answers true for `WholeDocument` and for
// `Objects`, because it cannot know which pages an object appears on. That asymmetry is the whole
// design (`core/doc/Scope.h`), and a cache is exactly the caller it was written for: a wasted
// repaint is the correct failure direction, a stale tile is not.
bool isStale(const RenderRequest& request, DocumentHandle document, const Scope& scope) noexcept
{
    if (request.document != document) return false;
    return scope.touchesPage(request.pageIndex);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// MemoryTileStore
// ---------------------------------------------------------------------------------------------

MemoryTileStore::MemoryTileStore(std::size_t capacityBytes) : capacityBytes_(capacityBytes) {}

std::optional<RasterImage> MemoryTileStore::lookup(const RenderRequest& request)
{
    const auto found = index_.find(request);
    if (found == index_.end()) return std::nullopt;
    // The LRU touch. `splice` moves the node without copying the image or invalidating the
    // iterator the index holds -- which is why the entries live in a `std::list` rather than in a
    // vector that would have to be re-indexed on every hit.
    lru_.splice(lru_.begin(), lru_, found->second);
    return found->second->image;
}

bool MemoryTileStore::put(const RenderRequest& request, const RasterImage& image)
{
    const std::size_t bytes = imageBytes(image);
    if (bytes == 0) return false;

    // REFUSED RATHER THAN ACCEPTED AT ANY COST. A tile larger than the whole cap would evict every
    // other entry to store itself and then be evicted by the next insertion -- a cache that holds
    // nothing and reports a 0 % hit rate, which looks like a cache that is simply cold.
    if (bytes > capacityBytes_) return false;

    const auto existing = index_.find(request);
    if (existing != index_.end()) {
        bytesHeld_ -= existing->second->bytes;
        lru_.erase(existing->second);
        index_.erase(existing);
    }

    evictUntilFits(bytes, request.document);

    lru_.push_front(Entry{request, image, bytes});
    index_.emplace(request, lru_.begin());
    bytesHeld_ += bytes;
    return true;
}

void MemoryTileStore::evictUntilFits(std::size_t incoming, DocumentHandle inserting)
{
    while (!lru_.empty() && bytesHeld_ + incoming > capacityBytes_) {
        auto victim = std::prev(lru_.end());   // least recently used
        if (victim->request.document != inserting) ++crossDocumentEvictions_;
        ++evictions_;
        eraseIterator(victim);
    }
}

void MemoryTileStore::eraseIterator(EntryList::iterator it)
{
    bytesHeld_ -= it->bytes;
    index_.erase(it->request);
    lru_.erase(it);
}

std::size_t MemoryTileStore::invalidate(DocumentHandle document, const Scope& scope,
                                        Revision newRevision)
{
    std::size_t dropped = 0;
    // Two passes, because a re-key changes an entry's hash and therefore its position in the index:
    // mutating keys while iterating the map they live in is the shape of bug that survives every
    // test on a small map and corrupts a large one.
    std::vector<EntryList::iterator> stale;
    std::vector<EntryList::iterator> survivors;
    for (auto it = lru_.begin(); it != lru_.end(); ++it) {
        if (it->request.document != document) continue;
        if (isStale(it->request, document, scope))
            stale.push_back(it);
        else if (newRevision.isValid() && it->request.revision != newRevision)
            survivors.push_back(it);
    }

    for (const auto& it : stale) {
        eraseIterator(it);
        ++dropped;
    }

    // THE RE-KEY. Without it, `Scope` buys nothing: every tile of a 900-page document would miss
    // after a one-page rotation, because the revision inside the key had moved. See the header.
    for (const auto& it : survivors) {
        index_.erase(it->request);
        it->request.revision = newRevision;
        index_.emplace(it->request, it);
    }
    return dropped;
}

std::size_t MemoryTileStore::dropDocument(DocumentHandle document)
{
    std::size_t dropped = 0;
    for (auto it = lru_.begin(); it != lru_.end();) {
        if (it->request.document == document) {
            auto victim = it++;
            eraseIterator(victim);
            ++dropped;
        } else {
            ++it;
        }
    }
    return dropped;
}

void MemoryTileStore::clear()
{
    lru_.clear();
    index_.clear();
    bytesHeld_ = 0;
}

void MemoryTileStore::setCapacityBytes(std::size_t bytes)
{
    capacityBytes_ = bytes;
    // Shrinking the cap takes effect at once rather than at the next insertion: a user who has just
    // lowered the limit because the machine is short of memory is owed the memory now.
    while (!lru_.empty() && bytesHeld_ > capacityBytes_) {
        auto victim = std::prev(lru_.end());
        ++evictions_;
        eraseIterator(victim);
    }
}

std::vector<DocumentHandle> MemoryTileStore::documents() const
{
    std::vector<DocumentHandle> out;
    for (const Entry& entry : lru_) {
        if (std::find(out.begin(), out.end(), entry.request.document) == out.end())
            out.push_back(entry.request.document);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// RenderCache
// ---------------------------------------------------------------------------------------------

RenderCache::RenderCache(Config config)
    : memory_(std::make_unique<MemoryTileStore>(config.memoryCapBytes))
{
    // `diskCapBytes` is carried by the config but a tier is not built here: the disk tier needs a
    // directory, and `core/` does not choose one. `app/CompositionRoot` builds a `DiskTileStore`
    // and calls `setDiskTier()` when `performance/diskCacheMB` is non-zero (T15).
    (void)config.diskCapBytes;
}

void RenderCache::setDiskTier(std::unique_ptr<ITileStore> tier)
{
    disk_ = std::move(tier);
}

std::optional<RasterImage> RenderCache::lookup(const RenderRequest& request) const
{
    ++stats_.lookups;
    if (!request.isValid()) {
        // An invalid request has no valid key -- D2-12's `Revision` clause -- so it cannot hit
        // anything, and letting it reach the map would make a defaulted revision look like an
        // ordinary cold miss.
        ++stats_.misses;
        return std::nullopt;
    }

    if (auto hit = memory_->lookup(request)) {
        ++stats_.memoryHits;
        return hit;
    }
    if (disk_ != nullptr) {
        if (auto hit = disk_->lookup(request)) {
            ++stats_.diskHits;
            // PROMOTED, so the second look at a tile does not pay for the disk again. The promotion
            // can be refused (a tile larger than the memory cap), and that is not an error.
            if (memory_->put(request, *hit)) ++stats_.promotions;
            return hit;
        }
    }
    ++stats_.misses;
    return std::nullopt;
}

void RenderCache::put(const RenderRequest& request, const RasterImage& image)
{
    if (!request.isValid() || !image.isValid()) {
        ++stats_.refusals;
        return;
    }
    const bool stored = memory_->put(request, image);
    if (stored) {
        ++stats_.insertions;
    } else {
        ++stats_.refusals;
    }
    // The disk tier is written THROUGH rather than only on eviction. Writing on eviction would mean
    // the tiles that reach disk are exactly the ones nobody wanted, and the tier would measure as
    // useless for a reason that is about the policy rather than about the disk (D2-14).
    if (disk_ != nullptr) disk_->put(request, image);
}

void RenderCache::invalidate(DocumentHandle document, const Scope& scope, Revision newRevision)
{
    ++stats_.invalidations;
    std::size_t dropped = memory_->invalidate(document, scope, newRevision);
    if (disk_ != nullptr) dropped += disk_->invalidate(document, scope, newRevision);
    stats_.invalidatedEntries += static_cast<long long>(dropped);

    if (newRevision.isValid()) revisions_[document.id] = newRevision;

    RAWPAGE_LOG_DEBUG(LogSubsystem::Render)
        << "cache: invalidated" << static_cast<int>(dropped) << "tiles, scope"
        << scopeKindName(scope.kind()) << "revision" << static_cast<int>(newRevision.value());
}

void RenderCache::dropDocument(DocumentHandle document)
{
    memory_->dropDocument(document);
    if (disk_ != nullptr) disk_->dropDocument(document);
    revisions_.erase(document.id);
}

void RenderCache::clear()
{
    memory_->clear();
    if (disk_ != nullptr) disk_->clear();
    revisions_.clear();
}

std::size_t RenderCache::bytesHeld() const noexcept
{
    return memory_->bytesHeld() + (disk_ != nullptr ? disk_->bytesHeld() : 0u);
}

std::size_t RenderCache::entryCount() const noexcept
{
    return memory_->entryCount() + (disk_ != nullptr ? disk_->entryCount() : 0u);
}

std::size_t RenderCache::crossDocumentEvictions() const noexcept
{
    return memory_->crossDocumentEvictionCount()
           + (disk_ != nullptr ? disk_->crossDocumentEvictionCount() : 0u);
}

Revision RenderCache::currentRevision(DocumentHandle document) const
{
    const auto found = revisions_.find(document.id);
    return found == revisions_.end() ? Revision::invalid() : found->second;
}

} // namespace rawpage::core::render
