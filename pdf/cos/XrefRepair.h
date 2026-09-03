// pdf/cos/XrefRepair -- why damaged files open instead of failing. Stage 2 T8.
//
// THE STAGE 2 EXIT CRITERION LIVES HERE: *every* file in `corpus/malformed/` yields a REPORT and
// never a crash, a hang, an assertion or an unbounded allocation (guide case **A-21**). Twenty-four
// files, one named fault each, and this is the code that has to survive all of them.
//
// -------------------------------------------------------------------------------------------
// IT REPAIRS AS LITTLE AS POSSIBLE, AND THAT IS THE DESIGN
// -------------------------------------------------------------------------------------------
// The obvious implementation -- "if the xref is broken, scan the whole file and rebuild" -- throws
// away the good half of a file whose damage is confined to its tail, which is what most damaged
// files are. Worse, it makes the report useless: every repaired document would come out as
// *"reconstructed the entire xref"*, and `Stage2-Rendering-Plan.md` §5.4 exists to say that
// *"reconstructed 3 objects of 4,000"* and *"reconstructed the entire xref"* are **different
// risks** that Stage 3 acts on differently.
//
// So `openWithRepair()` works in this order:
//
//   1. **Try the file's own structures.** `XrefTable::load()`, then VERIFY -- the trailer must name
//      a `/Root` that fetches to a `/Catalog`, and no entry may have been caught pointing at the
//      wrong object. A file that passes gets an EMPTY report and is not a repaired document.
//   2. **Scan for object headers** only when that fails. Every `N G obj` in the file, taken
//      last-wins, because an incremental update appends and the later definition is the newer one.
//   3. **Keep every entry that still verifies** and replace only the ones that do not. The report
//      counts exactly what changed, so the ratio in A-22's summary means something.
//   4. **Rebuild the trailer only if there isn't one**, by looking for an object whose `/Type` is
//      `/Catalog`. A file can lose its trailer and still be entirely readable.
//
// -------------------------------------------------------------------------------------------
// THE THREE THINGS A-21 FORBIDS, and where each is prevented
// -------------------------------------------------------------------------------------------
//   * **A crash.** Everything below goes through `pdf/cos/Parser`, which never fails and bounds
//     its own recursion (`kMaxDepth`), and `pdf/cos/Lexer`, which never reads out of bounds.
//   * **A hang.** The scan is a single forward pass; the `/Prev` walk in `XrefTable` refuses a
//     repeated offset; object-stream expansion refuses a container already being expanded. Each
//     is a fixture: `circular-prev.pdf`, `pages-loop.pdf`.
//   * **An unbounded allocation.** A stream is an offset and a length into the caller's buffer and
//     is never copied, so `huge-length-claim.pdf`'s `/Length 4294967295` cannot reserve anything.
//     The scan itself allocates one entry per object header actually present in the bytes.
//
// WHAT IT DOES NOT DO. It does not decrypt (D2-6 -- an encrypted file is declined by name and the
// `StandardSecurityHandler` is Stage 10's), it does not rebuild a page tree (that is
// `core/doc/PageTree`, T9), and it does not write anything. Stage 3 forces a repaired document's
// first save to Save-As; this stage only has to hand it the evidence to make that decision.

#ifndef RAWPAGE_PDF_COS_XREFREPAIR_H
#define RAWPAGE_PDF_COS_XREFREPAIR_H

#include "core/Result.h"
#include "core/doc/RepairReport.h"
#include "pdf/cos/XrefTable.h"

#include <string_view>

namespace rawpage::pdf::cos {

struct OpenedFile {
    XrefTable table;
    core::RepairReport report;

    // True when the file's own structures were used unchanged. NOT the same as
    // `report.isEmpty()`: a file can be read entirely through its own table and still have an
    // `Info` finding, such as a `/Length` recovered exactly -- which is the commonest thing in
    // any real PDF and is not a repair.
    bool usedOriginalXref = true;
};

// THE ONE ENTRY POINT, and it is deliberately the only one. T9's backend calls this rather than
// `XrefTable::load()`, so that "open a document" is a single code path whether or not the file is
// damaged -- two paths is how the repaired case ends up untested.
//
// Fails only when there is nothing to work with at all: no object header anywhere in the buffer.
// Every other outcome is a table plus a report, which is what A-21 asks for in as many words.
core::Result<OpenedFile> openWithRepair(std::string_view source);

// The scan on its own, exposed for testing and for T9's diagnostics: every `N G obj` header the
// bytes actually contain, last definition winning. Does not parse the objects.
std::vector<std::pair<long long, std::size_t>> scanObjectHeaders(std::string_view source);

} // namespace rawpage::pdf::cos

#endif // RAWPAGE_PDF_COS_XREFREPAIR_H
