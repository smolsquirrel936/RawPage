#include "core/render/DiskTileStore.h"

#include "core/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace rawpage::core::render {
namespace {

std::size_t imageBytes(const RasterImage& image) noexcept
{
    return static_cast<std::size_t>(image.pixels.size());
}


// EVERY DELETION IN THIS FILE GOES THROUGH HERE, and that is a layering decision rather than a
// tidiness one: it keeps the file's `layer-lint` waiver census at TWO -- one write, one delete --
// where four call sites would have needed four. The rule's own note says that a handful of waivers
// means the rule is right and a hundred means it is wrong, so the census is worth spending a
// function on.
void removeTileFile(const QString& path)
{
    QFile::remove(path);   // layer-lint: allow-filesystem-write(the disk tile cache removes only files it wrote itself, inside the directory app/ gave it -- D2-14)
}

} // namespace

DiskTileStore::DiskTileStore(QString directory, std::size_t capacityBytes)
    : directory_(std::move(directory)), capacityBytes_(capacityBytes)
{
    QDir dir(directory_);
    // `mkpath` is not on `layer-lint`'s write list and is deliberately not waived here: creating a
    // directory is not writing a document, and a waiver that silences nothing is itself reported by
    // `--explain` (Stage 0's I-20). The two markers in this file are the write and the delete.
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Render)
            << "cache: disk tier unusable, cannot create" << directory_;
        return;
    }

    // Anything already here was written by a previous process whose index died with it -- see the
    // header. Removing it now is what keeps the directory bounded: a tier that left orphans behind
    // would grow without limit across runs while reporting `bytesHeld()` of zero.
    const QStringList orphans = dir.entryList({QStringLiteral("tile-*.bin")}, QDir::Files);
    for (const QString& name : orphans)
        removeTileFile(directory_ + QLatin1Char('/') + name);

    usable_ = true;
}

DiskTileStore::~DiskTileStore()
{
    clear();
}

QString DiskTileStore::pathFor(std::uint64_t serial) const
{
    // A SERIAL, not a hash of the key. A hash would collide, and a collision here serves one
    // document's pixels for another document's tile -- the one failure a cache must never have.
    // The key lives in the in-memory index; the file name only has to be unique.
    return directory_ + QStringLiteral("/tile-%1.bin").arg(serial, 16, 16, QLatin1Char('0'));
}

std::optional<RasterImage> DiskTileStore::lookup(const RenderRequest& request)
{
    if (!usable_) return std::nullopt;
    const auto found = index_.find(request);
    if (found == index_.end()) return std::nullopt;

    const Entry& entry = *found->second;
    QFile file(entry.path);
    if (!file.open(QIODevice::ReadOnly)) {
        ++ioFailures_;
        eraseIterator(found->second);
        return std::nullopt;
    }
    const QByteArray pixels = file.readAll();
    if (static_cast<std::size_t>(pixels.size()) != entry.bytes) {
        // A short read means the file was truncated or removed under us. Dropping the entry is the
        // only safe answer: decoding a partial buffer produces a half-drawn tile, which is a
        // rendering defect that would be blamed on the rasterizer.
        ++ioFailures_;
        eraseIterator(found->second);
        return std::nullopt;
    }

    RasterImage image;
    image.width = entry.width;
    image.height = entry.height;
    image.bytesPerLine = entry.bytesPerLine;
    image.format = entry.format;
    image.pixels = pixels;

    lru_.splice(lru_.begin(), lru_, found->second);
    return image;
}

bool DiskTileStore::put(const RenderRequest& request, const RasterImage& image)
{
    if (!usable_) return false;
    const std::size_t bytes = imageBytes(image);
    if (bytes == 0 || bytes > capacityBytes_) return false;

    const auto existing = index_.find(request);
    if (existing != index_.end()) eraseIterator(existing->second);

    evictUntilFits(bytes, request.document);

    Entry entry;
    entry.request = request;
    entry.path = pathFor(nextSerial_++);
    entry.bytes = bytes;
    entry.width = image.width;
    entry.height = image.height;
    entry.bytesPerLine = image.bytesPerLine;
    entry.format = image.format;

    QFile file(entry.path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {   // layer-lint: allow-filesystem-write(the disk tier of the tile cache -- D2-14; the directory is chosen by app/, never here)
        ++ioFailures_;
        return false;
    }
    // The PIXELS ONLY: the geometry lives in the in-memory index, so there is no header to keep in
    // step with the struct. That is only sound because the index dies with the process and the
    // directory is purged on construction -- if this tier is ever made to survive a restart, the
    // geometry has to go into the file and be validated on read.
    const qint64 written = file.write(image.pixels);
    file.close();
    if (written != static_cast<qint64>(bytes)) {
        ++ioFailures_;
        removeTileFile(entry.path);
        return false;
    }

    lru_.push_front(entry);
    index_.emplace(request, lru_.begin());
    bytesHeld_ += bytes;
    return true;
}

void DiskTileStore::evictUntilFits(std::size_t incoming, DocumentHandle inserting)
{
    while (!lru_.empty() && bytesHeld_ + incoming > capacityBytes_) {
        auto victim = std::prev(lru_.end());
        if (victim->request.document != inserting) ++crossDocumentEvictions_;
        ++evictions_;
        eraseIterator(victim);
    }
}

void DiskTileStore::eraseIterator(EntryList::iterator it)
{
    removeTileFile(it->path);
    bytesHeld_ -= it->bytes;
    index_.erase(it->request);
    lru_.erase(it);
}

std::size_t DiskTileStore::invalidate(DocumentHandle document, const Scope& scope,
                                      Revision newRevision, const PageTree* pages)
{
    std::size_t dropped = 0;
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
    // The re-key is a map operation only -- the FILE does not move, because its name is a serial
    // and carries no part of the key. See `pathFor()`.
    for (const auto& it : survivors) {
        index_.erase(it->request);
        it->request.revision = newRevision;
        index_.emplace(it->request, it);
    }
    return dropped;
}

std::size_t DiskTileStore::dropDocument(DocumentHandle document)
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

void DiskTileStore::clear()
{
    for (auto it = lru_.begin(); it != lru_.end(); ++it)
        removeTileFile(it->path);
    lru_.clear();
    index_.clear();
    bytesHeld_ = 0;
}

void DiskTileStore::setCapacityBytes(std::size_t bytes)
{
    capacityBytes_ = bytes;
    while (!lru_.empty() && bytesHeld_ > capacityBytes_) {
        auto victim = std::prev(lru_.end());
        ++evictions_;
        eraseIterator(victim);
    }
}

} // namespace rawpage::core::render
