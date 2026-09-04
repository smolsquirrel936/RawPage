// core/render/RenderCache -- rendered tiles, kept until they are stale or until the cap says no.
// Stage 2 T11, decision D2-14.
//
// -------------------------------------------------------------------------------------------
// TWO TIERS BEHIND ONE INTERFACE, AND THE SECOND ONE MAY SHIP OFF
// -------------------------------------------------------------------------------------------
// D2-14: the disk tier is built behind the SAME interface as the memory tier, and open decision
// **O7** -- one cache shared between documents, or one per document -- is answered by MEASUREMENT
// over the corpus rather than by preference. That is only possible if the instrumentation exists
// alongside the cache instead of being bolted on at T18, so `CacheStats` is part of this file and
// not an afterthought. The number that actually answers O7 is `crossDocumentEvictions`: if opening
// a second document evicts the first document's tiles, a shared cache is costing something real.
//
// -------------------------------------------------------------------------------------------
// HOW `Scope` AND `Revision` WORK TOGETHER, WHICH IS NOT OBVIOUS AND IS THE WHOLE POINT
// -------------------------------------------------------------------------------------------
// A `RenderRequest` carries a `Revision` and its equality IS the key (D2-12). So a naive cache
// would lose EVERY tile the moment a document's revision moved -- and then `Scope` would buy
// nothing at all, because rotating page 3 of a 900-page document would re-render all 900.
//
// So `invalidate()` does two things rather than one:
//
//   * entries the scope TOUCHES are dropped;
//   * entries it does not touch are **re-keyed to the new revision**, so a tile rendered before a
//     mutation that did not affect it is still found afterwards.
//
// The clause "a lookup for an older `Revision` misses" then holds structurally rather than by a
// second check: after a re-key there is no entry at the old revision to find. That is the property
// that makes Stage 3's mutations safe, and Stage 2 is where it must already be right (A-41).
//
// -------------------------------------------------------------------------------------------
// NOT THREAD-SAFE, ON PURPOSE
// -------------------------------------------------------------------------------------------
// `RenderScheduler` touches this ONLY from the thread that pumps `deliverReady()` -- the UI thread
// -- so the cache is single-threaded by construction rather than by a lock nobody can see the
// extent of. A worker never reaches it: a finished tile travels back as a buffer and is inserted on
// the delivering thread. That is `D2-9`'s *buffers and callbacks only* applied inside `core/`.
//
// `lookup()` is `const` and its internals are `mutable`: an LRU touch and a disk-to-memory
// promotion are physical changes and not logical ones, and the plan's signature is `const`.

#ifndef RAWPAGE_CORE_RENDER_RENDERCACHE_H
#define RAWPAGE_CORE_RENDER_RENDERCACHE_H

#include "core/backend.h"
#include "core/doc/Revision.h"
#include "core/doc/Scope.h"
#include "core/render/RenderRequest.h"

#include <cstddef>
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace rawpage::core::render {

// One tier. D2-14's "behind the same interface as the memory tier", which is what lets the disk
// tier be absent, present, or replaced without `RenderCache` knowing which.
//
// `lookup` is non-const here and `const` on `RenderCache`: a tier genuinely mutates (LRU order, a
// file's access time), and hiding that behind `mutable` in every implementation would be the same
// lie told three times instead of once.
class ITileStore {
public:
    virtual ~ITileStore() = default;

    virtual std::optional<RasterImage> lookup(const RenderRequest& request) = 0;

    // False when the entry was REFUSED -- larger than the whole cap, or unwritable. A refusal is
    // not a failure: the tile is simply not cached, and a caller must not treat it as one, because
    // a 40-megapixel tile that evicted everything else to store itself is worse than a miss.
    virtual bool put(const RenderRequest& request, const RasterImage& image) = 0;

    // Drops what the scope touches for `document` and re-keys the rest to `newRevision`. Returns
    // how many entries it dropped, which is what `CacheStats` counts.
    virtual std::size_t invalidate(DocumentHandle document, const Scope& scope,
                                   Revision newRevision) = 0;

    virtual std::size_t dropDocument(DocumentHandle document) = 0;
    virtual void clear() = 0;

    virtual std::size_t bytesHeld() const = 0;
    virtual std::size_t entryCount() const = 0;
    virtual std::size_t capacityBytes() const = 0;
    virtual void setCapacityBytes(std::size_t bytes) = 0;

    // How many entries this tier has evicted, and how many of those evictions were caused by an
    // insertion belonging to a DIFFERENT document. The second number is O7's, and it is counted
    // here because only the tier knows what it threw away.
    virtual std::size_t evictionCount() const = 0;
    virtual std::size_t crossDocumentEvictionCount() const = 0;

    // ASCII, stable, non-localized: a log line and a benchmark column, never a user-facing string.
    virtual const char* tierName() const = 0;
};

// The memory tier: an LRU list under a byte cap.
class MemoryTileStore final : public ITileStore {
public:
    explicit MemoryTileStore(std::size_t capacityBytes);

    std::optional<RasterImage> lookup(const RenderRequest& request) override;
    bool put(const RenderRequest& request, const RasterImage& image) override;
    std::size_t invalidate(DocumentHandle document, const Scope& scope,
                           Revision newRevision) override;
    std::size_t dropDocument(DocumentHandle document) override;
    void clear() override;

    std::size_t bytesHeld() const override { return bytesHeld_; }
    std::size_t entryCount() const override { return index_.size(); }
    std::size_t capacityBytes() const override { return capacityBytes_; }
    void setCapacityBytes(std::size_t bytes) override;
    std::size_t evictionCount() const override { return evictions_; }
    std::size_t crossDocumentEvictionCount() const override { return crossDocumentEvictions_; }
    const char* tierName() const override { return "memory"; }

    // The documents this tier currently holds tiles for. O7 again: a shared cache whose entries
    // only ever name one document at a time is not being shared in any way that matters.
    std::vector<DocumentHandle> documents() const;

private:
    struct Entry {
        RenderRequest request;
        RasterImage image;
        std::size_t bytes = 0;
    };
    using EntryList = std::list<Entry>;

    void evictUntilFits(std::size_t incoming, DocumentHandle inserting);
    void eraseIterator(EntryList::iterator it);

    EntryList lru_;   // front = most recently used
    std::unordered_map<RenderRequest, EntryList::iterator> index_;
    std::size_t capacityBytes_ = 0;
    std::size_t bytesHeld_ = 0;
    std::size_t evictions_ = 0;
    std::size_t crossDocumentEvictions_ = 0;
};

// O7's instrumentation (D2-14), reported by `bench_render` at T18. Counters only -- a cache that
// computed its own verdict would be answering the open decision on its own.
struct CacheStats {
    long long lookups = 0;
    long long memoryHits = 0;
    long long diskHits = 0;
    long long misses = 0;

    long long insertions = 0;
    long long refusals = 0;       // too large for the cap, or the tier declined
    long long promotions = 0;     // found on disk and lifted back into memory

    long long invalidations = 0;      // calls
    long long invalidatedEntries = 0; // entries the scopes actually dropped

    long long hits() const noexcept { return memoryHits + diskHits; }

    // 0.0 when nothing has been looked up, rather than a division by zero or a NaN that poisons
    // every average downstream of it.
    double hitRate() const noexcept
    {
        return lookups > 0 ? static_cast<double>(hits()) / static_cast<double>(lookups) : 0.0;
    }
};

class RenderCache {
public:
    struct Config {
        // `performance/tileCacheMB` and `performance/diskCacheMB` (Stage 2 T15). Until that task
        // adds the rows, these constants are the defaults' only home -- `SettingsKeys.def` rule 2,
        // the same arrangement `DpiPolicy::Limits::maxRenderDpi` records.
        //
        // 256 MiB is roughly 1000 tiles of 256x256 BGRA: a 4K viewport's worth of visible tiles at
        // three zoom levels, which is what makes zooming out and back in free.
        std::size_t memoryCapBytes = 256u * 1024u * 1024u;

        // The disk tier is OFF by default, and D2-14 says why: whether it earns its place is a
        // measurement T18 makes, not a preference expressed here. `setDiskTier()` supplies one.
        std::size_t diskCapBytes = 0;
    };

    RenderCache() : RenderCache(Config{}) {}
    explicit RenderCache(Config config);

    // Installs (or removes, with nullptr) the second tier. Separate from the constructor because
    // the disk tier needs a directory, and choosing a directory is `app/`'s job -- `core/` does not
    // name a user's folders.
    void setDiskTier(std::unique_ptr<ITileStore> tier);

    // `performance/tileCacheMB` and `performance/diskCacheMB` (S2 T15), which are changed WHILE the
    // application is running -- a person who has just been told the cache is too large expects the
    // next scroll to obey, not the next launch. Shrinking evicts immediately; that is the tier's
    // own `setCapacityBytes`, which is why this is two lines and not a rebuild.
    //
    // A disk capacity of ZERO REMOVES the tier rather than keeping an empty one, because D2-14's
    // default is off and "installed but holding nothing" is a third state nobody asked for.
    void setMemoryCapacityBytes(std::size_t bytes);
    void setDiskCapacityBytes(std::size_t bytes);
    const ITileStore* diskTier() const noexcept { return disk_.get(); }
    const ITileStore& memoryTier() const noexcept { return *memory_; }

    // A hit in the disk tier is PROMOTED into memory, so the second lookup of a tile is fast.
    std::optional<RasterImage> lookup(const RenderRequest& request) const;

    void put(const RenderRequest& request, const RasterImage& image);

    // See the header: drops what the scope names and RE-KEYS the rest to `newRevision`.
    //
    // **`DocumentHandle` is a deviation from `Implementation-Plan.md`'s
    // `invalidate(Scope, Revision)` and it is not optional.** A `Scope` names pages and objects and
    // says nothing about which document they belong to, and O7 leaves open whether one cache serves
    // several -- `RenderRequest` carries a handle precisely so two documents can share a cache
    // without their tiles colliding. Without the handle, invalidating one document's page 3 would
    // drop page 3 of every open document.
    void invalidate(DocumentHandle document, const Scope& scope, Revision newRevision);

    // Everything for one document, when it is closed.
    void dropDocument(DocumentHandle document);
    void clear();

    const CacheStats& stats() const noexcept { return stats_; }
    void resetStats() noexcept { stats_ = CacheStats{}; }

    std::size_t bytesHeld() const noexcept;
    std::size_t entryCount() const noexcept;

    // O7's third number. Evictions caused by an insertion from a different document -- the direct
    // measure of whether one shared cache makes two documents fight.
    std::size_t crossDocumentEvictions() const noexcept;

    // The revision this cache believes each document is at, for a caller that needs to check its
    // own request against it. Invalid for a document that has never been invalidated.
    Revision currentRevision(DocumentHandle document) const;

private:
    std::unique_ptr<ITileStore> memory_;
    std::unique_ptr<ITileStore> disk_;
    std::unordered_map<std::uint64_t, Revision> revisions_;
    mutable CacheStats stats_;
};

} // namespace rawpage::core::render

#endif // RAWPAGE_CORE_RENDER_RENDERCACHE_H
