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

#include "core/Result.h"
#include "core/backend.h"
#include "core/doc/PageTree.h"
#include "core/doc/RepairReport.h"

#include <QByteArray>
#include <QString>

#include <memory>

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
    // error: `error.open.notFound`, `error.open.notPdf`, `error.open.cancelled`,
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

    // The bytes, for `pdf/cos/`. A view, not a copy: they live as long as this object, because
    // PDFium holds a pointer into them.
    QByteArray bytes() const { return bytes_; }

    // The engine handle, as an opaque pointer, for `PdfiumRasterizer` alone. It is `void*` rather
    // than `FPDF_DOCUMENT` **so this header names no engine type**: `pdf/render/` may, but a header
    // that did would put `fpdf_*.h` into every translation unit that includes it, and the handle
    // rule `layer-lint` enforces is about names rather than about directories.
    void* engineHandle() const noexcept;

private:
    PdfiumDocument();

    struct Engine;
    std::unique_ptr<Engine> engine_;

    QByteArray bytes_;      // PDFium points INTO this; it must outlive `engine_`
    QString path_;
    core::PageTree pages_;
    core::RepairReport repair_;
};

} // namespace rawpage::pdf::render

#endif // RAWPAGE_PDF_RENDER_PDFIUMDOCUMENT_H
