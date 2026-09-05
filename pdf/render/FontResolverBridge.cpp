#include "pdf/render/FontResolverBridge.h"

#include "core/Log.h"
#include "core/render/FontSubstitution.h"
#include "pdf/render/GlyphCache.h"
#include "pdf/render/PdfiumInit.h"

#include <fpdf_sysfontinfo.h>

#include <cstring>
#include <mutex>
#include <new>

namespace rawpage::pdf::render {
namespace {

using core::LogSubsystem;

// The whole of this file's state, and there is one of it because PDFium's hook is process-global.
// Guarded by its own mutex rather than by `PdfiumInit::engineLock()`: the callbacks below run
// WHILE that lock is held by the rendering thread, so taking it again here would deadlock the
// moment a page needed a substitute.
std::mutex                              g_mutex;
const platform::IFontResolver*          g_resolver = nullptr;
std::vector<FontResolverBridge::Substitution> g_log;
std::size_t                             g_mapFontCalls = 0;
bool                                    g_installed = false;

GlyphCache& cache()
{
    // Function-local static: constructed on first use, and never destroyed before the process ends
    // -- which is deliberate and is `PdfiumInit`'s own reasoning about teardown. PDFium may call
    // back during its own shutdown, and a cache destroyed by a static destructor in an order
    // nothing controls is a crash report from inside the engine.
    static GlyphCache* instance = new GlyphCache();   // NOLINT(cppcoreguidelines-owning-memory)
    return *instance;
}

// -------------------------------------------------------------------------------------------
// The handle PDFium holds between `MapFont` and `DeleteFont`.
//
// It is OUR type and it never leaves this file: `void*` on the C boundary is the whole of what
// crosses, which is the same rule the rest of `pdf/render/` follows -- buffers and callbacks, no
// object graph.
struct FaceHandle {
    QString family;
    QString filePath;
    int     faceIndex = 0;
    int     charset = 0;
};

// -------------------------------------------------------------------------------------------
// PDFium's charset constants -> a BCP-47 hint, which is what decides Han disambiguation.
//
// Getting this wrong is not cosmetic: the same code point is drawn with different strokes in
// Simplified Chinese, Traditional Chinese and Japanese, and a native reader calls the wrong one
// broken rather than "slightly off". The charset is the only language signal the hook carries.
QString languageHintForCharset(int charset)
{
    switch (charset) {
    case FXFONT_SHIFTJIS_CHARSET:    return QStringLiteral("ja");
    case FXFONT_HANGEUL_CHARSET:     return QStringLiteral("ko");
    case FXFONT_GB2312_CHARSET:      return QStringLiteral("zh-Hans");
    case FXFONT_CHINESEBIG5_CHARSET: return QStringLiteral("zh-Hant");
    default:                         return {};
    }
}

// -------------------------------------------------------------------------------------------
// An SFNT table, found by tag, honouring the face index inside a TrueType Collection.
//
// PDFium asks with a tag when it wants one table and with 0 when it wants the file. The tag path
// is the one that is exact for a `.ttc`; the header says what the 0 path costs.
struct ByteRange {
    const std::uint8_t* data = nullptr;
    std::size_t         size = 0;
};

std::uint32_t readBE32(const std::uint8_t* p)
{
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint16_t readBE16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

// Where the face's table directory starts. For a plain font that is offset 0; for a collection it
// comes out of the `ttcf` header's offset array. Returns SIZE_MAX when the file is not one we can
// walk -- every read below is bounds-checked against the buffer, because this parses bytes that
// came off a user's disk and `platform/` handed us a path rather than a promise.
std::size_t faceDirectoryOffset(const std::vector<std::uint8_t>& bytes, int faceIndex)
{
    if (bytes.size() < 12) return static_cast<std::size_t>(-1);

    if (std::memcmp(bytes.data(), "ttcf", 4) != 0) return 0;

    const std::uint32_t faceCount = readBE32(bytes.data() + 8);
    const auto          wanted = static_cast<std::uint32_t>(faceIndex < 0 ? 0 : faceIndex);
    if (wanted >= faceCount) return static_cast<std::size_t>(-1);

    const std::size_t entry = 12u + static_cast<std::size_t>(wanted) * 4u;
    if (entry + 4 > bytes.size()) return static_cast<std::size_t>(-1);

    const std::size_t offset = readBE32(bytes.data() + entry);
    return offset + 12 <= bytes.size() ? offset : static_cast<std::size_t>(-1);
}

ByteRange findTable(const std::vector<std::uint8_t>& bytes, int faceIndex, std::uint32_t tag)
{
    const std::size_t base = faceDirectoryOffset(bytes, faceIndex);
    if (base == static_cast<std::size_t>(-1)) return {};

    const std::uint16_t tableCount = readBE16(bytes.data() + base + 4);
    for (std::uint16_t i = 0; i < tableCount; ++i) {
        const std::size_t record = base + 12u + static_cast<std::size_t>(i) * 16u;
        if (record + 16 > bytes.size()) return {};

        if (readBE32(bytes.data() + record) != tag) continue;

        const std::size_t offset = readBE32(bytes.data() + record + 8);
        const std::size_t length = readBE32(bytes.data() + record + 12);
        if (offset > bytes.size() || length > bytes.size() - offset) return {};

        return ByteRange{bytes.data() + offset, length};
    }
    return {};
}

// -------------------------------------------------------------------------------------------
// The FPDF_SYSFONTINFO callbacks. Plain C linkage rules apply: no exception may escape one of
// these into the engine, which is why every body is a lookup rather than a computation.

void bridgeRelease(FPDF_SYSFONTINFO*) {}

void* bridgeMapFont(FPDF_SYSFONTINFO*, int weight, FPDF_BOOL italic, int charset,
                    int pitchFamily, const char* face, FPDF_BOOL* /*exact*/)
{
    const std::lock_guard<std::mutex> guard(g_mutex);
    ++g_mapFontCalls;

    if (g_resolver == nullptr) return nullptr;

    // `face` is documented as "system local encoding", so it is decoded the way it was encoded.
    // In practice a /BaseFont is ASCII, and `fromLocal8Bit` is right for both.
    const QString requested = QString::fromLocal8Bit(face == nullptr ? "" : face);

    platform::IFontResolver::Request request;
    request.family = core::render::normalizeBaseFontName(requested);
    request.weight = weight;
    request.italic = italic != 0;
    request.serif = (pitchFamily & FXFONT_FF_ROMAN) != 0;
    request.fixedPitch = (pitchFamily & FXFONT_FF_FIXEDPITCH) != 0;
    request.symbolic = charset == FXFONT_SYMBOL_CHARSET;
    request.languageHint = languageHintForCharset(charset);

    FontResolverBridge::Substitution row;
    row.requested = requested;
    row.normalized = request.family;

    // THE BASE-14 DECLINE, AND IT IS THE FIRST THING THIS FUNCTION DOES WITH ITS ANSWER.
    //
    // PDFium asks the installed hook for EVERY unembedded font -- standard or not -- and only uses
    // its own bundled Foxit faces when the hook says no. Without this branch the product
    // substituted Segoe UI for Helvetica on every page of every corpus file, which is issue
    // **I-13** and was found by the control case in `tests/render/tst_fontbridge.cpp` rather than by
    // reading anything. The header of `core/render/FontSubstitution` records the assumption it
    // replaced.
    //
    // Returning null here is not a failure and is deliberately NOT logged as one: it is this
    // bridge's considered answer that the engine's own face is the better one. It is still recorded
    // in the log, because "we were asked about Helvetica and declined" is exactly what a support
    // reader needs to see next to "we were asked about Garamond and chose Times New Roman".
    if (core::render::isStandardFontName(request.family)) {
        row.outcome = FontResolverBridge::Outcome::StandardDeclined;
        g_log.push_back(row);
        return nullptr;
    }

    const core::Result<platform::FontMatch> match = g_resolver->resolve(request);
    if (!match) {
        row.outcome = FontResolverBridge::Outcome::NoneAvailable;
        // Null tells PDFium to fall back to its own internal face. That is the correct answer -- a
        // page with the engine's default font beats a page with no text -- and it is LOGGED,
        // because the whole reason this file exists is that the silent version of this decision was
        // unobservable.
        g_log.push_back(row);
        RAWPAGE_LOG_WARNING(LogSubsystem::Render)
            << "font: no substitute for" << requested << "-- falling back to the engine's default";
        return nullptr;
    }

    row.chosen = match.value().family;
    row.filePath = match.value().filePath;
    row.faceIndex = match.value().faceIndex;
    row.outcome = match.value().exact ? FontResolverBridge::Outcome::Exact
                                      : FontResolverBridge::Outcome::Substituted;
    g_log.push_back(row);

    if (row.filePath.isEmpty()) {
        // A face with no readable file cannot be handed to the engine as bytes. Refusing here is
        // better than returning a handle whose `GetFontData` answers zero, which PDFium reads as a
        // corrupt font rather than as an absent one.
        RAWPAGE_LOG_WARNING(LogSubsystem::Render)
            << "font:" << row.chosen << "has no readable file -- falling back to the engine";
        return nullptr;
    }

    RAWPAGE_LOG_INFO(LogSubsystem::Render)
        << "font: mapped" << requested << "->" << row.chosen
        << (row.outcome == FontResolverBridge::Outcome::Exact ? "(exact)" : "(substitute)");

    return new (std::nothrow) FaceHandle{row.chosen, row.filePath, row.faceIndex, charset};
}

unsigned long bridgeGetFontData(FPDF_SYSFONTINFO*, void* font, unsigned int table,
                                unsigned char* buffer, unsigned long bufferSize)
{
    const auto* handle = static_cast<const FaceHandle*>(font);
    if (handle == nullptr) return 0;

    // The cache has its own lock; this callback does not take `g_mutex`, because it touches no
    // shared state of ours beyond the cache and PDFium calls it twice per face (a size query, then
    // a fill).
    //
    // THE REFERENCE IS SAFE FOR A REASON THE CACHE'S OWN LOCK DOES NOT GIVE, and it is worth saying
    // here rather than in `GlyphCache.h`: `faceData()` returns a reference that a LATER call could
    // evict, and this is the only caller. Every call reaches it from inside PDFium, which is
    // entered under `PdfiumInit::engineLock()` -- so two of these can never overlap, whatever
    // `RenderScheduler::Config::workerCount` is set to. A second caller anywhere else, or a call
    // outside the engine lock, breaks that and must copy instead.
    const std::vector<std::uint8_t>& bytes = cache().faceData(handle->filePath);
    if (bytes.empty()) return 0;

    ByteRange range{bytes.data(), bytes.size()};
    if (table != 0) {
        range = findTable(bytes, handle->faceIndex, static_cast<std::uint32_t>(table));
        if (range.data == nullptr) return 0;
    }

    // The size query: PDFium passes a null buffer, or one too small, and expects the REQUIRED size
    // back rather than an error. Answering 0 there reads as "this font has no such table".
    if (buffer == nullptr || bufferSize < range.size) return static_cast<unsigned long>(range.size);

    std::memcpy(buffer, range.data, range.size);
    return static_cast<unsigned long>(range.size);
}

unsigned long bridgeGetFaceName(FPDF_SYSFONTINFO*, void* font, char* buffer,
                                unsigned long bufferSize)
{
    const auto* handle = static_cast<const FaceHandle*>(font);
    if (handle == nullptr) return 0;

    const QByteArray name = handle->family.toLocal8Bit();
    const auto       needed = static_cast<unsigned long>(name.size()) + 1;
    if (buffer == nullptr || bufferSize < needed) return needed;

    std::memcpy(buffer, name.constData(), static_cast<std::size_t>(name.size()));
    buffer[name.size()] = '\0';
    return needed;
}

int bridgeGetFontCharset(FPDF_SYSFONTINFO*, void* font)
{
    const auto* handle = static_cast<const FaceHandle*>(font);
    return handle == nullptr ? FXFONT_DEFAULT_CHARSET : handle->charset;
}

void bridgeDeleteFont(FPDF_SYSFONTINFO*, void* font)
{
    delete static_cast<FaceHandle*>(font);
}

// VERSION 2, and the version number is the load-bearing field.
//
// Version 1 makes PDFium call `EnumFonts` once during installation and build its own list of every
// installed family -- 118 of them on this machine, each requiring a face to be opened. Version 2
// skips that entirely and asks `MapFont` per request, which is the shape this bridge actually has:
// the substitution policy walks a short candidate list, so an enumeration would be work done at
// start-up for an answer nobody reads. `EnumFonts` is therefore left null, which version 2 permits
// and version 1 does not.
FPDF_SYSFONTINFO g_sysFontInfo{
    /* version        */ 2,
    /* Release        */ bridgeRelease,
    /* EnumFonts      */ nullptr,
    /* MapFont        */ bridgeMapFont,
    /* GetFont        */ nullptr,
    /* GetFontData    */ bridgeGetFontData,
    /* GetFaceName    */ bridgeGetFaceName,
    /* GetFontCharset */ bridgeGetFontCharset,
    /* DeleteFont     */ bridgeDeleteFont,
};

} // namespace

// ===============================================================================================

core::Status FontResolverBridge::install(const platform::IFontResolver& resolver)
{
    // The engine has to be up first: `FPDF_SetSystemFontInfo` writes into state
    // `FPDF_InitLibraryWithConfig` creates, and a hook installed before it is a hook the library
    // clears on start-up -- accepted, and never called.
    if (const core::Status ready = ensure(); !ready) return ready;

    {
        const std::lock_guard<std::mutex> guard(g_mutex);
        if (g_installed) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Render)
                << "font: the PDFium font hook was already installed -- replacing it";
        }
        g_resolver = &resolver;
        g_installed = true;
    }

    FPDF_SetSystemFontInfo(&g_sysFontInfo);

    RAWPAGE_LOG_INFO(LogSubsystem::Render)
        << "font: PDFium font resolution routed through platform::IFontResolver";
    return core::Status{};
}

void FontResolverBridge::uninstall()
{
    bool wasInstalled = false;
    {
        const std::lock_guard<std::mutex> guard(g_mutex);
        wasInstalled = g_installed;
        g_installed = false;
        g_resolver = nullptr;
    }

    // Told to the engine unconditionally: `g_installed` is our bookkeeping, and the pointer inside
    // PDFium is what would dangle. Clearing it when it was already clear is defined and free.
    if (isInitialized()) FPDF_SetSystemFontInfo(nullptr);

    if (wasInstalled) {
        RAWPAGE_LOG_INFO(LogSubsystem::Render) << "font: PDFium font hook removed";
    }
}

bool FontResolverBridge::isInstalled()
{
    const std::lock_guard<std::mutex> guard(g_mutex);
    return g_installed;
}

std::vector<FontResolverBridge::Substitution> FontResolverBridge::substitutions()
{
    const std::lock_guard<std::mutex> guard(g_mutex);
    return g_log;
}

std::optional<FontResolverBridge::Substitution> FontResolverBridge::lastSubstitution()
{
    const std::lock_guard<std::mutex> guard(g_mutex);
    if (g_log.empty()) return std::nullopt;
    return g_log.back();
}

void FontResolverBridge::resetLog()
{
    const std::lock_guard<std::mutex> guard(g_mutex);
    g_log.clear();
    g_mapFontCalls = 0;
}

std::size_t FontResolverBridge::mapFontCalls()
{
    const std::lock_guard<std::mutex> guard(g_mutex);
    return g_mapFontCalls;
}

GlyphCache& FontResolverBridge::glyphCache()
{
    return cache();
}

} // namespace rawpage::pdf::render
