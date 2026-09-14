// The ENGINE boundary. Interfaces only -- no implementation, and no PDF engine.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS FILE IS.
//
// The second of the two interface headers this architecture rests on. `platform/platform.h` is the
// OS boundary; this is the boundary between the domain and whatever reads and writes PDF bytes.
// Together they are what makes `core/` testable with no display and no engine linked, and what
// makes the engine replaceable -- Tech-Stack-Plan.md 4.2 states the rule, and this file is the
// half of it that faces `pdf/`.
//
// ---------------------------------------------------------------------------------------------
// WHAT IS AND IS NOT ALLOWED TO BE HERE, AND WHY IT IS MACHINE-CHECKED.
//
// This file is included by `core/`, `pdf/`, `ui/` and `app/`. So it carries its OWN layer-lint
// entry, the same treatment platform/platform.h has and for the same reason: if an `FPDF_` symbol,
// a PDFium header or a QtGui type ever reaches it, every one of those layers acquires the
// dependency transitively, and the failure would not show up here -- it would show up as the Linux
// portable-asan job no longer being able to build `core/`, which is the mechanical proof that the
// domain layer is portable. Prose in a header comment is not a rule; the gate is.
//
// The concrete consequence, and it is the one thing to remember about this file: WHAT CROSSES THIS
// BOUNDARY IS BUFFERS AND CALLBACKS. No PDFium object, no PDFium handle, no exception, no heap
// ownership, and no Qt GUI type. `IPageRasterizer` therefore hands back a `RasterImage` -- width,
// height, stride, format, bytes -- and NOT a `QImage`, which is QtGui and which `core/` may not
// name (CLAUDE.md convention 3, the same rule that made `core/cmd/Chord` first-party rather than a
// `QKeySequence`). `ui/` wraps those bytes in a `QImage` at the far side, where QtGui is allowed.
//
// ---------------------------------------------------------------------------------------------
// WHY ALL SEVEN INTERFACES ARE DECLARED NOW, WHEN STAGE 2 IMPLEMENTS TWO OF THEM.
//
// Decision D2-7 (Stage2-Rendering-Plan.md 3), which is Implementation-Plan.md's own instruction --
// "declared now, even where unimplemented, so later stages add no boundary". A boundary added later
// is a boundary added under pressure, by whoever needed one method, in the shape that one call site
// wanted.
//
// AN UNIMPLEMENTED INTERFACE RETURNS AN EXPLICIT NOT-IMPLEMENTED `Result`, NEVER A SILENT SUCCESS
// AND NEVER AN EMPTY VALUE. That is CLAUDE.md convention 6 -- written about `platform/` and not in
// the least platform-specific: a stub that quietly succeeds lets a caller believe a page was
// extracted or a signature applied, and that surfaces stages later as data nobody produced.
// `notImplementedError()` below is the one way to say it, and `unimplementedBackendInterfaces()`
// makes "not built yet" a countable, printable fact rather than something a caller discovers by
// getting no result.
//
// ---------------------------------------------------------------------------------------------
// DECLARED SURFACE VERSUS FINISHED SIGNATURE -- read this before adding a method.
//
// The SEVEN INTERFACES are the boundary and they are all here. A METHOD whose parameter type is a
// later stage's vocabulary is added in the same change as that type, and every such type is
// forward-declared in one block below with the file and stage that brings it, so the coupling is
// visible rather than discovered. Adding a method to one of these seven is ordinary work; adding an
// EIGHTH interface is an architectural decision and belongs in a plan, not in a diff.

#ifndef RAWPAGE_CORE_BACKEND_H
#define RAWPAGE_CORE_BACKEND_H

#include "core/Result.h"

#include <QByteArray>
#include <QLatin1String>
#include <QString>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawpage::core {

// =======================================================================================
// Vocabulary that arrives later, forward-declared here
// =======================================================================================
// Each is a type this boundary genuinely names, owned by another file, listed with the stage that
// creates it. A forward declaration is how an interface is declared ahead of its vocabulary; the
// list is short on purpose, and it is the checkable record of what this header is waiting for.

class PageTree;              // core/doc/PageTree.h        -- Stage 2 T9 (read half, D2-5)
class RepairReport;          // core/doc/RepairReport.h    -- Stage 2 T8
class Permissions;           // core/doc/Permissions.h     -- Stage 3 T4 (pre-flight half, D3-7)
struct DocumentId;           // core/doc/DocumentId.h      -- Stage 3 T4

namespace render {
struct RenderRequest;        // core/render/RenderRequest.h -- Stage 2 T10
}
namespace text {
class PageTextModel;         // core/text/PageTextModel.h   -- Stage 5
}
namespace security {
struct SignatureRequest;     // core/security/SignatureRequest.h -- Stage 11
}

// =======================================================================================
// Value types crossing the boundary
// =======================================================================================

// An open document, as far as anything outside `pdf/` is concerned: an opaque token. It is
// deliberately NOT a pointer -- a pointer invites a caller to guess at what it points to, and the
// whole point of this boundary is that the engine's own object graph never leaves `pdf/`.
struct DocumentHandle {
    std::uint64_t id = 0;

    bool isValid() const noexcept { return id != 0; }

    friend bool operator==(DocumentHandle a, DocumentHandle b) noexcept { return a.id == b.id; }
    friend bool operator!=(DocumentHandle a, DocumentHandle b) noexcept { return a.id != b.id; }
};

// How a raster buffer's bytes are laid out. Both are 32 bits per pixel in BGRA byte order on a
// little-endian machine, which is what PDFium produces and what QImage::Format_ARGB32 and
// Format_RGB32 consume without a conversion -- so `ui/` wraps the bytes rather than copying them.
// The alpha byte is the only difference: `Bgrx8888` carries no transparency and its fourth byte is
// padding, which is the ordinary case for a page rendered onto white.
enum class PixelFormat {
    Bgra8888,   // ← QImage::Format_ARGB32 (not premultiplied)
    Bgrx8888,   // ← QImage::Format_RGB32
};

// The rasterizer's output. Bytes and geometry -- no image class, no engine bitmap, no ownership
// crossing the boundary. `bytesPerLine` is carried rather than computed because a rasterizer is
// entitled to align its rows, and a caller that assumes `width * 4` on a padded buffer draws
// diagonal garbage -- a bug that looks like a rendering defect rather than an arithmetic one.
struct RasterImage {
    int         width = 0;
    int         height = 0;
    int         bytesPerLine = 0;
    PixelFormat format = PixelFormat::Bgra8888;
    QByteArray  pixels;

    bool isValid() const noexcept
    {
        return width > 0 && height > 0 && bytesPerLine >= width * 4
               && pixels.size() >= static_cast<qsizetype>(bytesPerLine) * height;
    }
};

// Asked for a password when a document is encrypted (D2-6). `attempt` is 1-based, so a caller can
// say "that was wrong, try again" on the second ask without keeping its own counter, and returning
// `std::nullopt` means the user gave up -- which is `ErrorCode::Cancelled`, not a failure to open.
//
// It is a CALLBACK rather than a password parameter because the engine is what discovers the file
// is encrypted, and it may discover it after reading a good deal of the file. A caller with no user
// to ask (the batch runner, a test) passes a callback that returns `std::nullopt` immediately.
using PasswordCallback = std::function<std::optional<QString>(int attempt)>;

// What OCR is asked to produce. Both modes recognize the same pixels; they differ in what is
// written back -- an invisible text layer behind the original image, or replacement text.
enum class OcrOutputMode {
    SearchableImage,
    EditableText,
};

struct OcrPageResult {
    QString text;
    double  confidence = 0.0;   // 0..1 over the page as a whole
};

// One hit from the cross-file index (Stage 16; risk R2 -- Xapian is GPL, so the index is ours).
struct IndexHit {
    QString path;
    int     pageIndex = 0;
    double  score = 0.0;
};

// =======================================================================================
// The census
// =======================================================================================
// Declared before the interfaces because `IDocumentBackend` reports its own membership of it.

enum class BackendInterface : std::size_t {
    DocumentBackend = 0,
    PageRasterizer,
    TextExtractor,
    Mutation,
    Signer,
    OcrEngine,
    Indexer,
};

inline constexpr std::size_t kBackendInterfaceCount = 7;

inline const std::array<std::string_view, kBackendInterfaceCount>& backendInterfaceNames()
{
    static constexpr std::array<std::string_view, kBackendInterfaceCount> kNames{
        "IDocumentBackend", "IPageRasterizer", "ITextExtractor", "MutationSink",
        "ISigner", "IOcrEngine", "IIndexer",
    };
    return kNames;
}

inline std::string_view backendInterfaceName(BackendInterface which)
{
    return backendInterfaceNames()[static_cast<std::size_t>(which)];
}

// THE ONE WAY TO SAY "NOT BUILT YET" (D2-7). The message is a catalog key, never a sentence, for
// the reason core/Result.h gives: `core/` holds no user-facing English. The context names the
// interface and the method, untranslated, because that is the part a support log needs and the
// part no translator can supply.
inline constexpr QLatin1String kBackendNotImplementedKey{"error.backend.notImplemented"};

inline Error notImplementedError(BackendInterface which, std::string_view method)
{
    QString where = QString::fromUtf8(backendInterfaceName(which).data(),
                                      static_cast<qsizetype>(backendInterfaceName(which).size()));
    if (!method.empty()) {
        where += QLatin1String("::");
        where += QString::fromUtf8(method.data(), static_cast<qsizetype>(method.size()));
    }
    // `Unsupported` rather than `NotFound`: the operation is well-formed and named, and this build
    // does not implement it. A CLI or batch caller acts differently on the two (core/Result.h).
    return Error{ErrorCode::Unsupported, kBackendNotImplementedKey, std::move(where)};
}

// =======================================================================================
// 1. IPageRasterizer -- one request in, one buffer out
// =======================================================================================
// Stage 2 T9 implements it (`pdf/render/PdfiumRasterizer`). THREAD-CONFINED PER WORKER (D2-9): the
// scheduler owns one instance per worker thread and never shares one between them, so nothing here
// needs a lock and PDFium never sees two threads at once.
//
// `RenderRequest` carries the page, the tile rect, the device resolution, the `Revision` and the
// render flags -- and its equality IS the cache key, which is why the rasterizer takes the whole
// request rather than a parameter list nobody can hash.
class IPageRasterizer {
public:
    virtual ~IPageRasterizer() = default;

    // Blocking, and expected to be called on a worker thread. The UI thread never rasterizes.
    virtual Result<RasterImage> raster(const render::RenderRequest& request) = 0;
};

// =======================================================================================
// 2. ITextExtractor -- glyphs with geometry, for selection, find and copy
// =======================================================================================
// Stage 5 (`pdf/render/PdfiumTextExtractor`). It fills a `core/text/PageTextModel` -- characters
// with rects, writing direction and font identity -- rather than returning a string, because every
// consumer downstream of it (selection, find, the reflow converter) needs the geometry and not
// only the text.
class ITextExtractor {
public:
    virtual ~ITextExtractor() = default;

    virtual Status extractPage(DocumentHandle document, int pageIndex, text::PageTextModel& into) = 0;
};

// =======================================================================================
// 3. MutationSink -- the engine side of every document change
// =======================================================================================
// Stage 3 (`pdf/ops/MutationSinkImpl`). READ FLOW D IN Architecture-FileMap.md 5 BEFORE ADDING TO
// THIS INTERFACE. A `Command::apply()` expresses INTENT here -- "page 3 is now rotated 90 degrees"
// -- and never touches bytes; `pdf/ops/` decides how intent becomes COS objects. That split is what
// gives undo, crash recovery, precise invalidation and incremental save their shape, and it is the
// single most load-bearing design in the product.
//
// One method per mutation kind, each arriving with the `Command` subclass that emits it.
// `setPageRotation` is here now because it is the first-milestone mutation and therefore the shape
// every later one copies.
class MutationSink {
public:
    virtual ~MutationSink() = default;

    virtual Status setPageRotation(int pageIndex, int degrees) = 0;
};

// =======================================================================================
// 4. ISigner -- digital signatures
// =======================================================================================
// Stage 11 (`pdf/ops/SignatureOps`). The crypto itself is `platform::ICryptoProvider`'s; this
// interface owns the PDF half -- reserving the `/Contents` byte range, hashing the exact bytes,
// embedding the CMS blob, timestamp and revocation data.
class ISigner {
public:
    virtual ~ISigner() = default;

    virtual Status sign(DocumentHandle document, const security::SignatureRequest& request) = 0;
};

// =======================================================================================
// 5. IOcrEngine -- recognition over a rendered page
// =======================================================================================
// Stage 16 (`core/ocr/OcrPipeline`, over Tesseract). Note that this one is implemented in `core/`
// rather than in `pdf/`: it consumes a raster buffer and produces text, and neither end of that is
// PDF syntax. It is declared here because it is a replaceable ENGINE, which is what this header is
// the contract for.
class IOcrEngine {
public:
    virtual ~IOcrEngine() = default;

    virtual Result<OcrPageResult> recognize(const RasterImage& page, OcrOutputMode mode) = 0;
};

// =======================================================================================
// 6. IIndexer -- the cross-file full-text index
// =======================================================================================
// Stage 16 (`core/text/CatalogIndex`), and first-party by necessity: Xapian is GPL and D4 forbids
// it outright, which is risk R2 in Tech-Stack-Plan.md 10.
class IIndexer {
public:
    virtual ~IIndexer() = default;

    virtual Status addDocument(const QString& path) = 0;
    virtual Result<std::vector<IndexHit>> search(const QString& query, int maxHits) const = 0;
};

// =======================================================================================
// 7. IDocumentBackend -- the root, and the only one `app/` ever names
// =======================================================================================
// Stage 2 T9 implements it (`pdf/DocumentBackendImpl`). It opens and closes documents and hands out
// the other six for a document it opened. `app/CompositionRoot` constructs exactly one, through
// `pdf::createBackend()`, and everything downstream sees this interface and never a concrete type.
//
// The sub-interface accessors return a POINTER, and null means "this build does not implement that
// one" -- the same fact `implementedInterfaces()` reports, in the form a call site can act on.
// That is the direct analogue of `platform::Platform`'s null members, and it is deliberately not an
// empty stub: a caller must be able to tell "not built" from "built and it did nothing".
class IDocumentBackend {
public:
    virtual ~IDocumentBackend() = default;

    // Which of the seven this build actually implements, in the order of `BackendInterface`. A
    // backend that implements none of them is a legitimate state -- it is what Stage 2 T1 ships,
    // and it says so rather than pretending.
    virtual std::array<bool, kBackendInterfaceCount> implementedInterfaces() const = 0;

    // Opens a document, asking `password` if the file is encrypted (D2-6). WHETHER THE BYTES ARE A
    // PDF IS THIS METHOD'S QUESTION, not the caller's: Stage 1 decided a file by its suffix and
    // said so at the time (case A-58), and Stage 2 T9 is where that stops.
    virtual Result<DocumentHandle> open(const QString& path, const PasswordCallback& password) = 0;

    virtual Status close(DocumentHandle document) = 0;

    // THE DOCUMENT'S PAGE STRUCTURE, added at Stage 2 T13 -- and the reason it is on the interface
    // rather than on the concrete backend is worth stating, because it was on the concrete backend
    // first (Stage 2 guide issue I-08).
    //
    // T9 put `pages()` on `pdf::DocumentBackendImpl` with a comment saying `ui/` would reach it
    // "through the backend it was handed, which is still a `core::` type". That was not achievable:
    // `ui/` is handed a `core::IDocumentBackend&`, `ui/` may never name `pdf/`, and a
    // `dynamic_cast` to the concrete type would be the layering violation the whole boundary
    // exists to prevent. So T13 -- the first task with a viewer that needs to know how many pages
    // there are and how big they are -- moves the declaration here.
    //
    // A page tree is not something a signer or an OCR engine has, which was the original argument
    // for keeping it off this header. But this is `IDocumentBackend`, not one of the six
    // sub-interfaces: opening a document and saying what is in it are the same responsibility, and
    // `open()` already returns a handle that means nothing without it.
    //
    // Null for a handle this backend did not open, or has closed. The pointee is owned by the
    // backend and is valid until `close()`.
    virtual const PageTree* pages(DocumentHandle document) const = 0;

    // WHAT HAD TO BE RECONSTRUCTED TO OPEN THIS FILE, or null for a handle this backend did not
    // open. Empty for a file that needed nothing, which is not the same as null.
    //
    // MOVED HERE AT T14, from `pdf::DocumentBackendImpl`, for exactly the reason `pages()` moved at
    // T13 -- and the repetition is the point. T13 left this one on the concrete type deliberately,
    // saying its first consumer would find out; `ui/shell/MessageBar` is that consumer, `ui/` is
    // handed this interface and may never name `pdf/`, and the two-line move is made at the call
    // site that needs it rather than in advance. (Stage 2 guide issue I-08.)
    //
    // Stage 3 reads it for a harder reason than a banner: a document whose cross-reference table
    // was reconstructed must have its first save forced to Save-As, because appending to a file
    // whose structure was guessed is how an original gets destroyed. `RepairReport::requiresSaveAs()`
    // is that question and it is deliberately not `!isEmpty()`.
    virtual const RepairReport* repairReport(DocumentHandle document) const = 0;

    // WHETHER THIS DOCUMENT MAY BE EDITED IN THIS BUILD, and if not the refusal's catalog key -- or
    // null for a handle this backend did not open. Added at Stage 3 T4 (decision D3-7). The
    // assessment itself is `Permissions::assess()` in `core/`; the backend is what holds BOTH readings
    // the rule compares, the engine's page tree and the first-party reader's, so it is the one that
    // calls it, once, at open.
    virtual const Permissions* permissions(DocumentHandle document) const = 0;

    // WHICH FILE THIS IS -- path, size, modification time and a hash of the bytes that were opened --
    // or null for a handle this backend did not open. Added at Stage 3 T4: "taken at open" (D3-16) is
    // only possible where the bytes are read, and a second read of the file by a caller could hash a
    // file that had already changed.
    virtual const DocumentId* documentId(DocumentHandle document) const = 0;

    // The six, for a document this backend opened. Null when unimplemented; never a silent stub.
    virtual IPageRasterizer* rasterizer(DocumentHandle document) = 0;
    virtual ITextExtractor*  textExtractor(DocumentHandle document) = 0;
    virtual MutationSink*    mutationSink(DocumentHandle document) = 0;
    virtual ISigner*         signer(DocumentHandle document) = 0;
    virtual IOcrEngine*      ocrEngine() = 0;
    virtual IIndexer*        indexer() = 0;
};

// The names of the interfaces `backend` does not implement. Empty means all seven, which will not
// be true before Stage 16. Written as one function rather than seven `if`s at each call site, and
// kept beside the names so that adding an interface without extending both is a size mismatch the
// compiler reports.
inline std::vector<std::string_view> unimplementedBackendInterfaces(const IDocumentBackend& backend)
{
    const std::array<bool, kBackendInterfaceCount> present = backend.implementedInterfaces();
    std::vector<std::string_view> missing;
    for (std::size_t i = 0; i < kBackendInterfaceCount; ++i) {
        if (!present[i]) missing.push_back(backendInterfaceNames()[i]);
    }
    return missing;
}

} // namespace rawpage::core

#endif // RAWPAGE_CORE_BACKEND_H
