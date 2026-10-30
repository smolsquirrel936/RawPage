#include "core/render/RenderCache.h"

#include "core/Log.h"
#include "core/doc/PageTree.h"

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
} // namespace

bool tileTouchedBy(const RenderRequest& request, DocumentHandle document, const Scope& scope,
                   const PageTree* pages) noexcept
{
    if (request.document != document) return false;
    if (!scope.touchesPage(request.pageIndex)) return false;
    const std::vector<PdfRect>* rects = scope.regionsOf(request.pageIndex);
    if (rects == nullptr) return true;                       // the whole page
    const Page* page = pages != nullptr ? pages->at(request.pageIndex) : nullptr;
    if (page == nullptr || !(request.deviceScale > 0.0)) return true;   // cannot place it: the whole page

    // The rasterizer's placement: the crop box, turned by the page's own /Rotate and then by what the
    // request adds to it, at `deviceScale` device pixels a point (`pdf/render/PdfiumRasterizer`).
    const PageRotation turned = combine(page->rotation(), combine(request.viewRotation, request.pageRotationDelta));
    const PdfRect crop = page->cropBox();
    const PageTransform transform(crop.size, 1.0, request.deviceScale * 72.0, turned);
    constexpr double kPadPixels = 2.0;
    const double tileLeft = request.tile.x;
    const double tileTop = request.tile.y;
    const double tileRight = tileLeft + request.tile.width;
    const double tileBottom = tileTop + request.tile.height;
    for (const PdfRect& rect : *rects) {
        const PdfRect inBox{PdfPoint{rect.left() - crop.left(), rect.bottom() - crop.bottom()}, rect.size};
        const DeviceRect device = transform.toDevice(inBox);
        const double left = device.origin.x - kPadPixels;
        const double top = device.origin.y - kPadPixels;
        const double right = device.origin.x + device.size.width + kPadPixels;
        const double bottom = device.origin.y + device.size.height + kPadPixels;
        if (left < tileRight && tileLeft < right && top < tileBottom && tileTop < bottom) return true;
    }
    return false;
}

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
                                        Revision newRevision, const PageTree* pages)
{
    std::size_t dropped = 0;

    // A PAGE COMMAND (S7 T3, D7-3): a moved page's tiles are KEPT under its new index -- the page object is the
    // same, so its pixels are; only where it stands changed -- and a removed page's go. The re-key is the same
    // two passes as below, the index moving with the revision. With no valid revision to move to, the map is
    // not used and the scope is the `WholeDocument` it is: every tile of the document goes.
    if (const PageMap* map = scope.pageMap(); map != nullptr && newRevision.isValid()) {
        std::vector<EntryList::iterator> gone;
        std::vector<std::pair<EntryList::iterator, int>> moved;
        for (auto it = lru_.begin(); it != lru_.end(); ++it) {
            if (it->request.document != document) continue;
            const int to = map->newIndexOf(it->request.pageIndex);
            if (to == PageMap::kNone) gone.push_back(it);
            else moved.emplace_back(it, to);
        }
        for (const auto& it : gone) {
            eraseIterator(it);
            ++dropped;
        }
        for (const auto& [it, to] : moved) index_.erase(it->request);
        for (const auto& [it, to] : moved) {
            it->request.pageIndex = to;
            it->request.revision = newRevision;
            index_.emplace(it->request, it);
        }
        remapped_ += moved.size();
        return dropped;
    }

    // Two passes, because a re-key changes an entry's hash and therefore its position in the index:
    // mutating keys while iterating the map they live in is the shape of bug that survives every
    // test on a small map and corrupts a large one.
    std::vector<EntryList::iterator> stale;
    std::vector<EntryList::iterator> survivors;
    for (auto it = lru_.begin(); it != lru_.end(); ++it) {
        if (it->request.document != document) continue;
        if (tileTouchedBy(it->request, document, scope, pages))
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

void RenderCache::setMemoryCapacityBytes(std::size_t bytes)
{
    if (memory_) memory_->setCapacityBytes(bytes);
}

void RenderCache::setDiskCapacityBytes(std::size_t bytes)
{
    if (bytes == 0) {
        disk_.reset();
        return;
    }
    if (disk_) disk_->setCapacityBytes(bytes);
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

void RenderCache::invalidate(DocumentHandle document, const Scope& scope, Revision newRevision,
                             const PageTree* pages)
{
    ++stats_.invalidations;
    const std::size_t remappedBefore = memory_->remappedCount() + (disk_ != nullptr ? disk_->remappedCount() : 0u);
    std::size_t dropped = memory_->invalidate(document, scope, newRevision, pages);
    if (disk_ != nullptr) dropped += disk_->invalidate(document, scope, newRevision, pages);
    stats_.invalidatedEntries += static_cast<long long>(dropped);
    stats_.remappedEntries += static_cast<long long>(
        memory_->remappedCount() + (disk_ != nullptr ? disk_->remappedCount() : 0u) - remappedBefore);

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
