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
// **Stage 6 T4 takes it for one purpose only** -- the RELOAD after an edit (`show()`, D6-2), which
// reads the original and the update as two buffers so the original is not copied per edit. The
// first load is still `FPDF_LoadMemDocument64` over `bytes_`.
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
#include "core/doc/Permissions.h"
#include "core/doc/RepairReport.h"
#include "core/text/SearchUnit.h"

#include <QByteArray>
#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace rawpage::pdf::render {

// THE VERDICT A QUEUED REVISION WAITS FOR -- Stage 7 T4 (`Stage7-Assembly-Plan.md` Q4). `IDocumentBackend::showEdits`
// verifies its composition on a thread of its own and hands every engine the update GATED on that verify: the
// engine swaps it in only once the gate has PASSED, and a job that needs the engine before then waits for it.
// Resolved once, by the verifying thread; read by any.
class ShowGate {
public:
    void resolve(bool passed);
    bool wait() const;                 // blocks until resolved; whether it passed
    std::optional<bool> verdict() const;

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable resolved_;
    std::optional<bool> passed_;
};

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

    // THE SAME OPEN, IN PIECES -- Stage 5 T9 (D5-13 as amended at T3). `openBytes` IS this, run to the
    // end in one call, so there is one open and not two to keep in agreement. Each `step()` is one piece:
    //
    //   1. LOAD -- `FPDF_LoadMemDocument64`, with the password loop (engine);
    //   2. the form-fill environment (engine);
    //   3. the page count (engine);
    //   4. the page walk, pages until `kPieceBudget` has passed (at least one), each page under its own
    //      lock (engine) -- the piece T3 measured at ~145 ms for 900 pages when it was one;
    //   5. the outline, the named destinations and the Info dictionary (engine);
    //   6. the first-party reading of the same bytes, for the repair report (NO engine).
    //
    // `nextNeedsEngine()` says which kind the next piece is, so a folder search can run 1-5 as tasks on
    // the render thread and 6 off it. Not thread-safe: one opening's pieces run one at a time.
    class Opening {
    public:
        // How long one piece of the page walk may run before it stops at the next page. A page's
        // extraction measured p50 1.43, p95 1.70-1.73, max 2.50-2.65 ms at T3; a piece that stops once
        // past a millisecond is under that by the cost of its last page (0.2-0.44 ms measured at T9).
        static constexpr std::chrono::microseconds kPieceBudget{1000};

        Opening(QByteArray bytes, QString displayPath, core::PasswordCallback password);
        ~Opening();
        Opening(const Opening&) = delete;
        Opening& operator=(const Opening&) = delete;

        bool done() const noexcept;
        bool nextNeedsEngine() const noexcept;
        // One piece. A refusal ends the opening: `step()` again returns it again.
        core::Status step();
        // After `done()`: the document. Null if called before, or twice.
        std::unique_ptr<PdfiumDocument> take();

    private:
        enum class Stage { Load, FormEnvironment, Count, Pages, Readers, FirstParty, Done, Failed };
        Stage stage_ = Stage::Load;
        std::unique_ptr<PdfiumDocument> document_;
        core::PasswordCallback password_;
        int count_ = 0;
        int nextPage_ = 0;
        std::vector<core::Page> pages_;
        std::vector<std::vector<core::AnnotationFact>> annotationFacts_;   // S6 T2, beside pages_
        std::optional<core::Error> failure_;
    };

    int pageCount() const noexcept;
    const core::PageTree& pages() const noexcept { return pages_; }

    // S6 T2 (D6-3): each page's `/Annots` as the ENGINE reads it -- per entry, whether it is a
    // dictionary, its subtype and its rectangle -- taken during the page walk at open, while the page
    // is loaded anyway. `core::Permissions::assessAnnotations` compares it with the first-party
    // reading. One vector per page of `pages()`.
    const std::vector<std::vector<core::AnnotationFact>>& annotationFacts() const noexcept
    {
        return annotationFacts_;
    }
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

    // ONE PART OF THE DOCUMENT'S SEARCH UNITS -- Stage 5 T8, decision **D5-12**; `core/text/SearchUnit.h`
    // says what a part is and why a document is never read whole.
    //
    //   * a PAGE: its comments -- every MARKUP annotation's `/Contents` and `/Subj` (ISO 32000-1 12.5.6.2's
    //     subtypes; a link, a popup and a widget are not comments) -- and its form fields' qualified names
    //     and values, from the widgets on it, each with the annotation's `/Rect`;
    //   * the OUTLINE: every bookmark title, from `outline()` -- read at open, so this calls no engine;
    //   * the ATTACHMENTS: each embedded file's name, its description from `descriptions` (the
    //     first-party reading, `pdf/ops/EmbeddedFileReader`, joined by position and confirmed by name),
    //     and -- for a file whose bytes are a PDF -- the text of each of its pages, opened from those
    //     bytes by `openBytes` with NO password callback (an encrypted attachment is skipped, never
    //     prompted for) and never written to disk. **One level deep**: the attached PDF's own attachments
    //     are not read. **The one long part**: an attached PDF's pages are extracted in this one call, so a
    //     long attached PDF holds the engine thread for its whole extraction -- not split, and not measured
    //     beyond `search-classes.pdf`'s one-page attachment (guide A-78).
    //
    // The engine lock is taken per page, never across a part; every engine call is inside it.
    // `outline` (S7 T3): the outline to read the bookmarks from, as it names pages NOW -- a page command moves
    // them -- or null for the one read at open.
    core::Status searchUnits(int part, const std::vector<std::pair<QString, QString>>& descriptions,
                             std::vector<core::text::SearchUnit>& into, const core::Outline* outline = nullptr) const;

    // ONE PAGE'S PATHS -- Stage 6 T12, decision **D6-14** (`core/backend.h` says what is read and why it is bounded).
    // Every path object the page draws -- stroked or filled; a path that does neither is clipping, and is not a
    // drawing anyone measures -- in page space: each object's matrix applied, and a form XObject's objects through
    // the form's, eight forms deep at most. A Bezier is flattened into eight pieces marked `curve`, its two ends
    // kept as anchors. Past `core::PageGeometry::kMaxSegments` the reading stops and the page is answered
    // `tooManySegments`. The whole page under the engine lock, as `searchUnits` reads a page.
    core::Status pageGeometry(int pageIndex, core::PageGeometry& into) const;

    // The bytes, for `pdf/cos/`. A view, not a copy: they live as long as this object, because
    // PDFium holds a pointer into them.
    QByteArray bytes() const { return bytes_; }

    // WHAT THE ENGINE DRAWS AFTER AN EDIT -- Stage 6 T4, decision **D6-2** (`Stage6-Annotations-Plan.md`
    // §3), which ends Stage 3's D3-15 for everything a render parameter cannot express.
    //
    // `show(base, update)` says: from the next engine call on, draw `base` followed by `update` -- the
    // newest revision on disk and the update the mutation sink composed over it, which are the bytes
    // Save would write. It does NOT touch the engine: it queues, under a lock of its own, and returns.
    // The swap happens on whichever thread next enters the engine for this document -- the render
    // thread, in the product -- at the top of that call and UNDER THE ENGINE LOCK, which every engine
    // call of this class, the rasterizer and the extractor holds for the whole of one job (load the
    // page, use it, close it). So a swap is BETWEEN JOBS by construction: no job can hold a page of
    // the document the swap closes. A newer `show` before the swap replaces the queued one, so a burst
    // of edits costs one reload, not one each.
    //
    // **NO COPY OF THE ORIGINAL.** The new engine document is read through `FPDF_LoadCustomDocument`,
    // whose callback reads across the two buffers; `base` is an implicitly shared `QByteArray`, so the
    // engine holds a reference to the bytes the backend already holds, plus the update.
    //
    // **WHAT IS NOT RE-READ:** the page tree, the labels, the outline and the other readers above.
    // `pages()` stays the tree read at open, the one a caller already holds a pointer to. A rotation the
    // update carries is drawn right anyway: the rasterizer takes each page's `/Rotate` from the loaded
    // page and corrects for its difference from the tree, which is what a request's unsaved-rotation
    // delta is relative to.
    //
    // **BUT A PAGE COMMAND REORDERS WHAT IS DRAWN** (Stage 7 T3, D7-3): `from[i]` is the index IN
    // `pages()` of the page the update draws at index `i` -- itself, moved, or the page a duplicate
    // copies -- and empty means the identity. The engine's own tree (`drawnPagesLocked`) is `pages()`
    // read through it, built here and swapped in WITH the engine document, under the engine lock, so a
    // job never reads one tree's page and the other document's index. Each page carries its boxes and
    // its rotation as read at open, which keeps D3-15's delta and the rasterizer's correction reading
    // the same rotation for the same page.
    //
    // **AND A PAGE THE ENGINE NEVER OPENED** (Stage 7 T5, D7-4): `drawn[i]`, where it holds one, is the page index
    // `i` draws -- one inserted from another document, or a page replaced by one -- taken instead of `pages()`'s. It is
    // part of the order: a show that changes only it is a new tree.
    //
    // **A RELOAD THAT FAILS changes nothing**: the engine keeps the document it had and keeps drawing
    // it, and the failure is counted and logged. `show(bytes(), {})` puts the document back on the
    // bytes it opened, and is no work at all when it never left them.
    //
    // **GATED -- Stage 7 T4.** With a `gate`, the revision is queued BEHIND it: swapped in once the gate passes, as a
    // revision queued without one is; DROPPED when it fails, the engine keeping what it drew -- and until the next
    // `show` or `clearRefusal()` every job of this document then finds no engine (`engineHandle()` null, so a tile
    // fails rather than draw the old pages at the indices the refused revision gave them). A job that would swap
    // waits for the gate first: `awaitShown()` before it takes the engine lock, and the swap itself under the lock if
    // a caller did not. A revision queued ungated before a gated one is kept until the gate decides.
    void show(QByteArray base, QByteArray update, std::vector<int> from = {}, std::shared_ptr<ShowGate> gate = {},
              std::vector<std::optional<core::Page>> drawn = {});
    // Waits, holding no lock, until no gated revision is waiting -- what a job calls before the engine lock.
    void awaitShown() const;
    // A gated revision whose gate failed is dropped now, and the refusal ends (S7 T4): the backend, once it has put
    // back what the refused show replaced.
    void clearRefusal();

    // What the ENGINE draws, page by page -- `pages()` through the last swapped `from`. **CALL IT WITH THE
    // ENGINE LOCK HELD and after `engineHandle()`**, which swaps a queued revision in first: then the tree and
    // the document it describes are the same revision's. Every index this class's engine readers are asked
    // about is an index of THIS tree.
    const core::PageTree& drawnPagesLocked() const noexcept { return drawn_ ? *drawn_ : pages_; }

    // What the reloads have done, for the cases and the bench that grade D6-2 (guide A-37, A-38).
    struct ReloadStats {
        long long reloads = 0;                  // swaps made
        long long failures = 0;                 // loads the engine refused; the previous document kept
        long long coalesced = 0;                // queued revisions a newer `show` replaced before a swap
        std::chrono::nanoseconds lastLoad{0};   // the last swap's load and form environment, under the lock
        std::thread::id lastThread;             // the thread the last swap ran on
        std::uint64_t pendingGeneration = 0;    // the newest `show`, 0 before any
        std::uint64_t shownGeneration = 0;      // the newest swapped in (or found to need none)
        const void* shownBase = nullptr;        // the data of the buffer the engine reads its original from
    };
    ReloadStats reloadStats() const;

    // The engine handle, as an opaque pointer, for `PdfiumRasterizer` alone. It is `void*` rather
    // than `FPDF_DOCUMENT` **so this header names no engine type**: `pdf/render/` may, but a header
    // that did would put `fpdf_*.h` into every translation unit that includes it, and the handle
    // rule `layer-lint` enforces is about names rather than about directories.
    //
    // **CALL IT WITH THE ENGINE LOCK HELD** (S6 T4): it swaps in a revision `show` queued before it
    // answers -- D6-2's between-jobs swap, at the top of a job.
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
    // `mutable` since S6 T4: a const reader swaps a queued revision in before it reads (D6-2). It is
    // the same document, at a newer revision, so the method stays logically const.
    mutable std::unique_ptr<Engine> engine_;

    // D6-2's queue and its record. `pending_` under `pendingMutex_` (written from the UI thread);
    // `reloadStats_` and the swap itself under the engine lock.
    struct Pending {
        QByteArray base;
        QByteArray update;
        std::vector<int> from;                           // S7 T3: empty is the identity
        std::vector<std::optional<core::Page>> drawn;    // S7 T5: pages `pages_` never held; empty for none
        std::shared_ptr<const core::PageTree> pages;     // ... and `pages_` read through it; null for the identity
        std::uint64_t generation = 0;
        std::shared_ptr<ShowGate> gate;                  // S7 T4: null for a revision nothing gates
    };
    void adoptPendingLocked() const;
    // S7 T4: under `pendingMutex_` -- a gated revision whose gate has decided becomes the pending one, or is dropped.
    void decideGatedLocked() const;
    mutable std::optional<Pending> gated_;               // S7 T4: behind its gate, under `pendingMutex_`
    mutable std::atomic<bool> refused_{false};           // S7 T4: the last gate failed (see `show`)
    mutable std::mutex pendingMutex_;
    mutable std::optional<Pending> pending_;
    std::uint64_t nextGeneration_ = 0;           // under `pendingMutex_`
    // What the engine draws now, under `pendingMutex_` (written at a swap, which holds the engine lock
    // and takes this one inside it -- never the other order): a `show` of exactly this is no work.
    mutable QByteArray shownBase_;
    mutable QByteArray shownUpdate_;
    mutable std::vector<int> shownFrom_;   // S7 T3, beside the two above
    mutable std::vector<std::optional<core::Page>> shownDrawn_;   // S7 T5, beside it
    mutable ReloadStats reloadStats_;
    // S7 T3: the engine's tree when it is not `pages_`; written at a swap, under the engine lock.
    mutable std::shared_ptr<const core::PageTree> drawn_;

    QByteArray bytes_;      // PDFium points INTO this; it must outlive `engine_`
    QString path_;
    core::PageTree pages_;
    std::vector<std::vector<core::AnnotationFact>> annotationFacts_;
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
