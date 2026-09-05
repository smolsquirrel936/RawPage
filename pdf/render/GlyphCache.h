// pdf/render/GlyphCache -- the font FACE DATA behind the substitution bridge, held once per face
// instead of once per ask. Stage 2 T16.
//
// ===============================================================================================
// READ THIS BEFORE THE NAME MISLEADS YOU: THIS IS NOT A CACHE OF RASTERIZED GLYPHS.
// ===============================================================================================
//
// The plan (`Stage2-Rendering-Plan.md` §4) names this file `GlyphCache`, and guide case **A-67**
// states its criterion as *"rendering two adjacent tiles of one text page rasterizes each distinct
// glyph once"*. Building it is what showed that the second sentence describes something on the far
// side of the engine boundary: **PDFium rasterizes glyphs, PDFium caches them, and it does so in
// process-global state this project deliberately touches in exactly one file** (`PdfiumInit`, and
// D2-9's reason). Nothing in `pdf/render/` ever sees a glyph bitmap, so nothing here can count
// them -- and a class that claimed to would be measuring a number it invented.
//
// What this layer genuinely owns is the step BEFORE that one. PDFium asks us for a substitute
// face, then asks us for its BYTES -- twice per request, since `GetFontData` is a size query
// followed by a fill -- and it asks again for every document that names the same missing font.
// MSJH.TTC is 18 MB. Reading it from disk per ask, on the render thread, in the middle of a page,
// is the cost this file removes, and it is a cost that grows with exactly the thing A-67 was
// worried about: more tiles, more pages, more documents.
//
// So the measurable claim is one step outward from the guide's wording and is stronger for being
// checkable: **a face is read from disk once, and every later ask for it -- across tiles, across
// pages, across documents -- is served from memory.** `FontResolverBridge::mapFontCalls()` carries
// the other half, since a second tile of the same page performs no font mapping at all. A-67's
// result records the correction the way T7 recorded A-18's and T15 recorded A-65's: the criterion
// was two claims wearing one sentence, and only one of them is ours to make.
//
// -----------------------------------------------------------------------------------------------
// WHY A BYTE CAP AND NOT A FACE COUNT.
//
// Faces differ by two orders of magnitude -- CONSOLA.TTF is 100 KB and a CJK collection is 18 MB --
// so "keep eight faces" is a memory bound between 800 KB and 144 MB depending on which language a
// person reads. The cap is bytes, evicting least-recently-used, and it is deliberately NOT a
// settings key: `performance/tileCacheMB` bounds tiles, which scale with zoom and page count, and
// this bounds a handful of system files that a document either needs or does not. A knob here
// would be a knob nobody can set correctly.
//
// -----------------------------------------------------------------------------------------------
// THREAD SAFETY: entered from the render worker, and it locks.
//
// The engine lock (`PdfiumInit::engineLock()`) is held whenever PDFium is inside a render call, so
// in the shipped arrangement -- one render thread, I-05 -- this is never contended. It locks
// anyway, because `RenderScheduler::Config::workerCount` exists so that T18 can walk the worker
// count without a rebuild, and a cache that is safe only at one setting is a crash waiting for that
// measurement.

#ifndef RAWPAGE_PDF_RENDER_GLYPHCACHE_H
#define RAWPAGE_PDF_RENDER_GLYPHCACHE_H

// <QHash> BEFORE <unordered_map>, and this is not a style preference. Qt's specialization of
// `std::hash<QString>` lives in `qhashfunctions.h`, which <QString> does NOT pull in -- so a
// translation unit that instantiates `std::unordered_map<QString, ...>` first fails with
// "specialization of std::hash<QString> after instantiation", a diagnostic that names neither this
// file nor the include that caused it. Measured at T16 by hitting it.
#include <QHash>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace rawpage::pdf::render {

class GlyphCache {
public:
    struct Config {
        // 64 MiB: three CJK collections and a dozen Latin faces, which is more than any single
        // document has ever needed and small enough that a machine never notices.
        std::size_t capacityBytes = 64u * 1024u * 1024u;
    };

    // TWO constructors rather than one with `= {}`, and the reason is a C++ rule rather than a
    // preference: a nested class's default member initializer is not usable inside the
    // ENCLOSING class's own member declarations, because the enclosing class is still
    // incomplete there. `Config config = Config{}` therefore does not compile, and the
    // diagnostic GCC gives for it names the initializer rather than the rule.
    GlyphCache();
    explicit GlyphCache(Config config);

    // The whole file backing a face, read once. Returns an empty vector when the path cannot be
    // read -- which is a real state rather than an assertion, because a font can be uninstalled
    // between the resolve and the fetch, and the caller's answer to that is to tell PDFium it has
    // no data rather than to fail the page.
    //
    // The reference stays valid until the next call that could evict, which is any other
    // `faceData()`. Callers copy out of it under the same lock the bridge already holds.
    const std::vector<std::uint8_t>& faceData(const QString& filePath);

    // Total bytes held, faces held, asks, and asks served without touching the disk. `hits + misses
    // == requests` is asserted by the suite rather than assumed, because a counter that is only
    // ever read one at a time is a counter nothing checks.
    std::size_t bytesHeld() const;
    std::size_t facesHeld() const;
    std::size_t requests() const;
    std::size_t hits() const;

    // Drops everything. For the suite, and for the case where a font was reinstalled -- there is no
    // product caller today and that is said out loud rather than left to be discovered
    // (convention 6's habit applied to a method): the resolver refreshes its collection per query,
    // so a reinstalled face is found again, and only its BYTES would be stale. Stage 14's font
    // management is what will call this.
    void clear();

private:
    struct Entry {
        QString                   path;
        std::vector<std::uint8_t> bytes;
    };

    void evictToFit();

    mutable std::mutex mutex_;
    Config             config_;

    // A list for recency and a map into it: the standard LRU, and the same shape
    // `core/render/MemoryTileStore` uses, so a reader who has seen one has seen both.
    std::list<Entry>                                           order_;
    std::unordered_map<QString, std::list<Entry>::iterator>     index_;
    std::size_t                                                bytesHeld_ = 0;
    std::size_t                                                requests_ = 0;
    std::size_t                                                hits_ = 0;

    // Returned for a path that cannot be read, so the accessor can hand back a reference without
    // inserting a failure into the cache -- a font that appears later must not be permanently
    // remembered as absent.
    const std::vector<std::uint8_t> empty_;
};

} // namespace rawpage::pdf::render

#endif // RAWPAGE_PDF_RENDER_GLYPHCACHE_H
