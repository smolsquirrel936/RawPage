// pdf/ops/RevisionWriter -- the backend's half of Save: write the next revision to a file, prove it,
// and move the sink onto it. Stage 3 T8, decisions D3-4, D3-5 and D3-6 (`Stage3-Mutation-Plan.md` §3,
// §5.3's steps 2, 3 and "adopt").
//
// PORTABLE, in `rawpage_ops`, so that BOTH backends share it -- the shipped `pdf/DocumentBackendImpl`
// and the headless suite's `tests/unit/CosBackend.h` -- and so the round trip runs under
// `portable-asan` (D3-18). Nothing here knows about PDFium.
//
// -------------------------------------------------------------------------------------------
// WHAT `write()` DOES, IN ORDER
// -------------------------------------------------------------------------------------------
//   1. **Compose** the whole file in memory: the sink's newest revision on disk, COPIED VERBATIM,
//      followed by the update its desired state needs. The update is an ordinary `/Prev`-chained
//      one, or -- for a document whose table had to be repaired -- D3-6's COMPLETE section with no
//      `/Prev`, so the new file opens without repair. A non-repaired document with nothing to write
//      composes to the original bytes alone: a Save-As of an unchanged file is a copy.
//   2. **Write** it to `temporaryPath`.
//   3. **Read it back FROM THAT PATH** -- not from the buffer just written, because the claim is
//      about the file on disk -- and verify: the original is a byte-identical prefix; the file
//      opens through its own cross-reference structures with nothing that forces a Save-As; it has
//      the same number of pages; every page's effective rotation is the one the sink says it should
//      have; from Stage 7 T2, every page is the object the desired order puts there, a new page a new
//      object; and -- from Stage 6 T3 -- every page's `/Annots` is the listing the sink composed, each
//      created annotation a dictionary with its `/NM`, its `/Subtype`, its page and a well-formed `/AP`
//      (`MutationSinkImpl::reconcile`). A file failing any check is REMOVED and the save fails by key before anything
//      replaces anything (A-54).
//
// The temporary file is this class's to remove on its own failure. After `write()` returns success
// it belongs to the caller -- `core/doc/SaveCoordinator` flushes it, replaces the target with it,
// and removes it if either of those fails.
//
// `adopt()` is the step after the replace: read the file now at the target, and move the sink's
// comparison onto it (D3-5's "newest revision on disk"). It returns the bytes and the repair report
// of that file, which the backend keeps -- the sink holds views into those bytes.
//
// THE TEST SEAM. `setComposeFaultForTesting()` lets a test corrupt the composed bytes before they are
// written, which is the only way to obtain an update its own reader cannot parse from a writer that
// is correct (guide case A-54). It is process-wide, empty in the product, and every test that sets it
// clears it.

#ifndef RAWPAGE_PDF_OPS_REVISIONWRITER_H
#define RAWPAGE_PDF_OPS_REVISIONWRITER_H

#include "core/Result.h"
#include "core/doc/RepairReport.h"
#include "pdf/ops/MutationSinkImpl.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <vector>

namespace rawpage::pdf::ops {

struct ComposedRevision {
    QByteArray bytes;                       // the whole file
    bool complete = false;                  // D3-6's complete section, no /Prev
    std::vector<long long> objectsWritten;  // ascending; empty when nothing was appended
};

// S7 T4: the update alone -- what follows the newest revision on disk -- for a caller that hands the engine the
// two buffers apart and verifies elsewhere (`IDocumentBackend::showEdits`). Empty when nothing is appended.
struct ComposedUpdate {
    QByteArray update;
    bool complete = false;
    std::vector<long long> objectsWritten;
    std::size_t objectsReused = 0;   // of them, written again as an earlier compose wrote them
};

struct AdoptedRevision {
    QByteArray bytes;             // the file now at the target; the sink holds views into it
    core::RepairReport report;    // of that file -- empty after a correct write
};

class RevisionWriter {
public:
    // `complete`: write D3-6's complete section rather than a /Prev-chained update. The backend
    // passes `true` for a document whose opening needed a repair that `requiresSaveAs()`.
    static core::Result<ComposedRevision> compose(MutationSinkImpl& sink, bool complete);
    // S7 T4: step 1 without the copy of the original -- `compose` is this followed by the copy and the seam.
    static core::Result<ComposedUpdate> composeUpdate(MutationSinkImpl& sink, bool complete);

    // Checks `written` against `original` and the sink, as step 3 above. Pure: it reads no file.
    static core::Status verify(std::string_view original, std::string_view written,
                               const MutationSinkImpl& sink);
    // S7 T4: `verify` of `original` followed by `update`, the whole put together HERE with `fault` applied to it
    // -- the seam as it stood when the update was composed (`composeFault()`), so a verify that runs on another
    // thread, after a test has cleared the seam, still checks what that test planted. It reads `sink` and
    // changes nothing in it: the caller keeps the sink unchanged until it returns (`showEdits`).
    static core::Status verifyUpdate(std::string_view original, const QByteArray& update, const MutationSinkImpl& sink,
                                     const std::function<void(QByteArray&)>& fault);

    // Steps 1-3. On failure the temporary file does not exist.
    static core::Status write(MutationSinkImpl& sink, bool complete, const QString& temporaryPath);

    // Reads `path` and moves `sink` onto it. On failure the sink is unchanged.
    static core::Result<AdoptedRevision> adopt(MutationSinkImpl& sink, const QString& path);

    // The test seam; see the header. An empty function restores the product's behaviour.
    using ComposeFault = std::function<void(QByteArray& bytes)>;
    static void setComposeFaultForTesting(ComposeFault fault);
    static ComposeFault composeFault();   // S7 T4: the seam as it stands, for `verifyUpdate`
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_REVISIONWRITER_H
