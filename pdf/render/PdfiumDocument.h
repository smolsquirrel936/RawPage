// pdf/render/PdfiumDocument -- one open document, behind the engine boundary. Stage 2 T9.
//
// SCOPE. Open (including password-protected, D2-6), the page count, and a `core::PageTree` built
// from what the engine reports. **The `FPDF_DOCUMENT` never leaves this file's object**: the whole
// point of the boundary is that the engine's object graph stays inside `pdf/render/`, and
// `layer-lint` enforces the name rule in both its `FPDF_` and its handle-typedef form (D2-9).
//
// -------------------------------------------------------------------------------------------
// THE FILE IS READ BY US AND HANDED TO PDFium AS BYTES
// -------------------------------------------------------------------------------------------
// `FPDF_LoadDocument` takes a `const char*` path, and what encoding that is depends on the
// platform and the PDFium build. **This project already learned what that costs**: CLAUDE.md's
// convention 1 exists because the CRT turned an emoji path into `report-?????.pdf` on this
// machine's code page 950. Handing PDFium a path is the same trap one layer over.
//
// So the bytes are read through `QFile` -- which takes a `QString` and gets the encoding right on
// Windows by construction -- and passed to `FPDF_LoadMemDocument64`. Two further things fall out,
// and both are wanted: the buffer's lifetime is ours (PDFium does not copy it, so it must outlive
// the document, which is why it is a member), and the same bytes are available to `pdf/cos/` for
// the repair report without a second read.
//
// **THE COST IS REAL AND IS RECORDED RATHER THAN ABSORBED**: a 400 MB document occupies 400 MB.
// `FPDF_LoadCustomDocument` with a read callback is the upgrade path and is deliberately not taken
// here -- it trades a memory bound for a lifetime and threading problem that D2-9's measured
// finding (I-05) makes worse, and Stage 2 has no page-level streaming to benefit from it yet.
//
// -------------------------------------------------------------------------------------------
// ENCRYPTION: TWO READERS, TWO DIFFERENT CORRECT ANSWERS (D2-6)
// -------------------------------------------------------------------------------------------
// PDFium opens an encrypted document through the password callback; **`pdf/cos/` declines one by
// name** until Stage 10's `StandardSecurityHandler`. That is not an inconsistency to tidy up -- it
// is the arrangement D2-6 chose, because two parsers reaching different conclusions about one
// protected file is a class of bug that is very hard to see, and one of them refusing on purpose is
// the honest state until the handler exists.
//
// The callback is asked repeatedly with a 1-based attempt number, so a caller can say "that was
// wrong, try again" without keeping its own counter. Returning `std::nullopt` means the user gave
// up, which is `Cancelled` -- **not** a failure to open, because a cancelled prompt is not a
// damaged file and a message bar must not say it is.

#ifndef RAWPAGE_PDF_RENDER_PDFIUMDOCUMENT_H
#define RAWPAGE_PDF_RENDER_PDFIUMDOCUMENT_H

#include "core/Geometry.h"
#include "core/Result.h"
#include "core/backend.h"
#include "core/doc/Destinations.h"
#include "core/doc/Metadata.h"
#include "core/doc/Outline.h"
#include "core/doc/PageTree.h"
#include "core/doc/RepairReport.h"

#include <QByteArray>
#include <QString>

#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace rawpage::pdf::render {

class PdfiumDocument {
public:
    // Non-copyable and non-movable: it owns an engine handle and a buffer PDFium holds a pointer
    // into. A move would be implementable and is not implemented, because the only thing that owns
    // one of these is `DocumentBackendImpl`'s map, which holds them by `unique_ptr`.
    PdfiumDocument(const PdfiumDocument&) = delete;
    PdfiumDocument& operator=(const PdfiumDocument&) = delete;
    ~PdfiumDocument();

    // Reads the file, initializes the engine if it is not already, and opens. Fails with a NAMED
    // error: `error.open.notFound`, `error.open.notPdf` (no PDF header), `error.open.damaged` (a
    // header, and the engine still could not open it), `error.open.noPages` (it opened and has no
    // readable page -- refused rather than shown empty, I-31), `error.open.cancelled`,
    // `error.open.badPassword`, or `error.engine.initFailed`. **"Is this a PDF" is answered here
    // and nowhere else** -- Stage 1 decided it by suffix and said so at the time (case A-58), and
    // this is where that stops (guide case A-47).
    static core::Result<std::unique_ptr<PdfiumDocument>> open(const QString& path,
                                                              const core::PasswordCallback& password);

    // Opens bytes that are already in hand. The path form is built on this; it exists separately
    // so a test can open a corpus file it has already read, and so Stage 4's recovery journal can
    // reopen a document from a buffer without touching the filesystem again.
    static core::Result<std::unique_ptr<PdfiumDocument>> openBytes(
        QByteArray bytes, const QString& displayPath, const core::PasswordCallback& password);

    int pageCount() const noexcept;
    const core::PageTree& pages() const noexcept { return pages_; }
    const QString& path() const noexcept { return path_; }

    // What the first-party COS reader had to reconstruct, for the same bytes. **PDFium does its own
    // silent repair and reports nothing**, so this is the only source of a `RepairReport` -- and it
    // is why `pdf/cos/XrefRepair` is not redundant with an engine that already opens damaged files.
    // Empty when the file needed nothing.
    const core::RepairReport& repairReport() const noexcept { return repair_; }
    bool wasRepaired() const noexcept { return !repair_.isEmpty(); }

    // WHAT THE DOCUMENT SAYS ABOUT ITSELF -- read ONCE, here, at open (S4 T4, D4-3 and D4-4, guide
    // case A-30). Each is empty for a document that carries none, which is the ordinary case and
    // not a failure.
    //
    // They are read at open rather than on demand because the alternative is a pane that walks the
    // engine's outline every time it is shown, and because `PdfiumDocument` is the only place that
    // may hold an `FPDF_DOCUMENT` at all: a lazy reader would have to keep the handle reachable
    // from `core/`, which is the layering the facade exists to prevent.
    const core::Outline& outline() const noexcept { return outline_; }
    const core::Destinations& destinations() const noexcept { return destinations_; }
    const core::Metadata& metadata() const noexcept { return metadata_; }

    // THE PAGE'S CONTENT BOUNDING BOX -- S4 T12, decision **D4-14**, closing Stage 2's **I-28**.
    // The union of the page's object bounds, intersected with the CropBox, in PDF points.
    //
    // **ON DEMAND AND CACHED, unlike the three readers above.** Those are one fact per document and
    // are read at open; this is one per PAGE, and a 900-page document asked for every page's box at
    // open would walk every object in the file to answer a question about the one page being
    // fitted. So the page asked about is loaded, unioned, closed and the answer kept -- asking
    // twice calls the engine once (guide case A-67).
    //
    // `ErrorCode::NotFound` for a page with **no objects**: an empty page is a value, not a
    // failure, and `core/render/FitPolicy` falls back to the CropBox for it. `InvalidArgument` for
    // an index outside the tree.
    //
    // **`mutable` CACHE, `const` METHOD, AND A LOCK OF ITS OWN.** The method is logically a read;
    // the cache behind it is written. The engine lock serializes the PDFium half, but two threads
    // asking about two different pages would race on the map itself, which the engine lock says
    // nothing about -- and the whole reason this class is careful is that PDFium's own sharing is
    // process-global (I-05).
    core::Result<core::PdfRect> contentBox(int pageIndex) const;

    // The bytes, for `pdf/cos/`. A view, not a copy: they live as long as this object, because
    // PDFium holds a pointer into them.
    QByteArray bytes() const { return bytes_; }

    // The engine handle, as an opaque pointer, for `PdfiumRasterizer` alone. It is `void*` rather
    // than `FPDF_DOCUMENT` **so this header names no engine type**: `pdf/render/` may, but a header
    // that did would put `fpdf_*.h` into every translation unit that includes it, and the handle
    // rule `layer-lint` enforces is about names rather than about directories.
    void* engineHandle() const noexcept;

    // The form-fill environment, likewise opaque and likewise for `PdfiumRasterizer` alone. Null
    // when PDFium declined to make one, which is a degradation rather than a failure: the page
    // still renders and only its widget appearances are missing.
    //
    // ITS ONLY PURPOSE IS `FPDF_FFLDraw`. PDFium will not draw a `/Subtype /Widget` annotation
    // from `FPDF_RenderPageBitmap` however the flags are set, so a form page renders as its
    // labels and nothing else without this (**I-21**). Interaction -- focus, typing, validation --
    // is Stage 9's and is not reachable through this handle as it is used here.
    void* formHandle() const noexcept;

private:
    PdfiumDocument();

    struct Engine;
    std::unique_ptr<Engine> engine_;

    QByteArray bytes_;      // PDFium points INTO this; it must outlive `engine_`
    QString path_;
    core::PageTree pages_;
    core::RepairReport repair_;
    core::Outline outline_;
    core::Destinations destinations_;
    core::Metadata metadata_;

    // S4 T12. `std::nullopt` in the map means *asked, and the page has no objects* -- which is why
    // the map holds an optional rather than only the rectangles it found: a second ask for an empty
    // page must not walk the page again.
    mutable std::mutex contentBoxMutex_;
    mutable std::map<int, std::optional<core::PdfRect>> contentBoxes_;
};

} // namespace rawpage::pdf::render

#endif // RAWPAGE_PDF_RENDER_PDFIUMDOCUMENT_H
