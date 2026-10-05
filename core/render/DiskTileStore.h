// core/render/DiskTileStore -- the second cache tier, D2-14. Stage 2 T11.
//
// -------------------------------------------------------------------------------------------
// THIS IS THE ONLY FILE IN `core/render/` THAT WRITES TO DISK, AND IT CARRIES WAIVERS FOR IT
// -------------------------------------------------------------------------------------------
// `tools/layer-lint`'s `filesystem-write` rule confines writes to `pdf/`, `core/settings/`,
// `tools/` and `tests/`, so the two write calls in the implementation carry an inline
// `// layer-lint: allow-filesystem-write(...)` marker and appear in `layer-lint --explain`'s
// census. That is the mechanism working rather than being worked around: a tile cache that cannot
// write a file is not a disk tier, and the rule's own note says it is a heuristic and the only
// escapable one. **If this file ever needs a third and fourth waiver, that is the signal to move
// the I/O behind an interface `app/` implements** -- the census is what makes that visible.
//
// -------------------------------------------------------------------------------------------
// WHAT IT DOES AND DOES NOT CLAIM
// -------------------------------------------------------------------------------------------
// It spills tiles out of memory so a 900-page document scrolled twice does not re-rasterize
// everything the second time. **It does NOT claim to survive a restart**: the index is in memory,
// so a new process finds files it cannot key and clears the directory on construction. A
// cross-session cache needs an on-disk index and a validity check against the document's bytes --
// which is a decision with a security dimension (a cached render of a document the user has since
// deleted) and belongs with **O7** at T18, not here.
//
// The directory is a CONSTRUCTOR ARGUMENT and never chosen here. `core/` does not name a user's
// folders; `app/CompositionRoot` resolves `QStandardPaths::CacheLocation` and hands it down. That
// also makes the tier testable against a temporary directory, which is the only way to assert its
// cap without writing into whoever ran the suite.
//
// OFF BY DEFAULT. `RenderCache::Config::diskCapBytes` is 0 and no tier is installed unless someone
// asks -- D2-14 says whether the tier earns its place is a measurement T18 makes.

#ifndef RAWPAGE_CORE_RENDER_DISKTILESTORE_H
#define RAWPAGE_CORE_RENDER_DISKTILESTORE_H

#include "core/render/RenderCache.h"

#include <QString>

#include <cstdint>
#include <list>
#include <unordered_map>

namespace rawpage::core::render {

class DiskTileStore final : public ITileStore {
public:
    // Creates `directory` if it does not exist and empties it if it does -- see the note above
    // about not claiming to survive a restart. `isUsable()` is false when the directory could not
    // be created, and a tier that is not usable refuses every `put` and answers every `lookup`
    // with a miss rather than pretending.
    DiskTileStore(QString directory, std::size_t capacityBytes);
    ~DiskTileStore() override;

    bool isUsable() const noexcept { return usable_; }
    const QString& directory() const noexcept { return directory_; }

    std::optional<RasterImage> lookup(const RenderRequest& request) override;
    bool put(const RenderRequest& request, const RasterImage& image) override;
    std::size_t invalidate(DocumentHandle document, const Scope& scope, Revision newRevision,
                           const PageTree* pages) override;
    std::size_t dropDocument(DocumentHandle document) override;
    void clear() override;

    std::size_t bytesHeld() const override { return bytesHeld_; }
    std::size_t entryCount() const override { return index_.size(); }
    std::size_t capacityBytes() const override { return capacityBytes_; }
    void setCapacityBytes(std::size_t bytes) override;
    std::size_t evictionCount() const override { return evictions_; }
    std::size_t crossDocumentEvictionCount() const override { return crossDocumentEvictions_; }
    const char* tierName() const override { return "disk"; }

    // How many files this tier failed to write or read. A disk tier that silently stopped working
    // is indistinguishable from one with a 0 % hit rate, and the two want completely different
    // actions from whoever is looking at the number.
    std::size_t ioFailureCount() const noexcept { return ioFailures_; }

private:
    struct Entry {
        RenderRequest request;
        QString path;
        std::size_t bytes = 0;
        // The image's geometry, held in memory so a lookup can size its buffer before reading and
        // so a truncated file is detected rather than decoded into garbage.
        int width = 0;
        int height = 0;
        int bytesPerLine = 0;
        PixelFormat format = PixelFormat::Bgra8888;
    };
    using EntryList = std::list<Entry>;

    void evictUntilFits(std::size_t incoming, DocumentHandle inserting);
    void eraseIterator(EntryList::iterator it);
    QString pathFor(std::uint64_t serial) const;

    QString directory_;
    std::size_t capacityBytes_ = 0;
    std::size_t bytesHeld_ = 0;
    std::size_t evictions_ = 0;
    std::size_t crossDocumentEvictions_ = 0;
    std::size_t ioFailures_ = 0;
    std::uint64_t nextSerial_ = 1;
    bool usable_ = false;

    EntryList lru_;
    std::unordered_map<RenderRequest, EntryList::iterator> index_;
};

} // namespace rawpage::core::render

#endif // RAWPAGE_CORE_RENDER_DISKTILESTORE_H
