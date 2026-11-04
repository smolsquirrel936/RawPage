// pdf/cos/FullWriter -- a whole file written again as ONE revision of what its trailer reaches, every object
// keeping its number and generation. Stage 7 T6, decisions D7-25 and D7-29 (`Stage7-Assembly-Plan.md` §3).
//
// WHAT IT IS FOR. An incremental save (`IncrementalWriter`) keeps every byte of the file and appends, so what a
// page command deletes stays in the file for ever: delete 800 of 900 pages and save, and the file grows. Save As
// that REWRITES is the compaction path (`Tech-Stack-Plan.md`'s mutation section): this writer takes a whole file
// -- in the product, the newest revision on disk followed by the update the sink composed, put together in memory
// exactly as an incremental save would write it -- and writes only what that file's trailer reaches.
//
// -------------------------------------------------------------------------------------------
// WHAT IT WRITES
// -------------------------------------------------------------------------------------------
//   * **The header line the file had** (`%PDF-1.x`; `%PDF-1.7` when there is none), then a binary comment.
//   * **Every object reachable from the trailer's `/Root` and `/Info`**, in number order, each at the number and
//     generation it has -- one that lived in an object stream at generation 0, as ISO 32000-1 7.5.7 defines it --
//     written from its VALUES (`IncrementalWriter`'s serializer, spans ignored), a stream with its encoded data
//     copied byte for byte and a direct `/Length`. A stream's `/Length` is therefore not a reference the walk
//     follows: an indirect length object nothing else names is dropped, because the rewrite no longer needs it.
//   * **One classic cross-reference section and no `/Prev`.** No object streams and no cross-reference stream:
//     packing is Stage 8's (D7-25), and a classic table can list every object this writes.
//   * **A trailer** with `/Size`, `/Root`, `/Info` when the file has one, and an `/ID` whose first element is kept
//     and whose second is a hash of what was written. `/Encrypt` never: an encrypted file is refused (D2-6).
//
// WHAT IT DROPS -- everything else: superseded revisions, every cross-reference section and stream, every object
// stream (its objects are written loose), a linearization dictionary, and every object nothing reaches -- a
// deleted page, its content, an orphaned appearance.
//
// -------------------------------------------------------------------------------------------
// NUMBERS KEPT, AND WHAT A DROPPED NUMBER BECOMES (D7-25, D7-29)
// -------------------------------------------------------------------------------------------
// No object is renumbered, so an annotation identity (D6-4), a journal line (D7-24) and the undo history, which
// all name objects by number, still name the same objects after a rewrite. A dropped number is a FREE row, and
// its generation is THE ONE THE OBJECT HAD -- not one more, as a writer that deleted it for good would write --
// because D7-29 lets an undo bring it back: `ObjectDelta::revive` writes it again at that number and generation,
// so a reference the undo history holds (`N G R`) names it again, and the free row says in advance which
// generation that will be. `/Size` is one past the largest number the file had IN USE, dropped ones included, so
// a number a later edit allocates is never one an undo may revive.
//
// The rows listed: object 0, every number written or dropped, and every number the file's own table listed as
// free -- the source's own coverage, so a hostile `/Size` of eight million writes no eight million rows. A
// number nothing listed has no row, which every reader resolves as it resolves a free one: null.
//
// PORTABLE (`pdf/cos/` by rule): std C++ and QtCore's hash, built by `portable-asan` and `portable-fuzz`.

#ifndef RAWPAGE_PDF_COS_FULLWRITER_H
#define RAWPAGE_PDF_COS_FULLWRITER_H

#include "core/Result.h"

#include <string>
#include <string_view>
#include <vector>

namespace rawpage::pdf::cos {

class XrefTable;

struct FullRewrite {
    std::string bytes;                // the whole new file
    long long size = 0;               // the `/Size` its trailer declares
    std::vector<long long> kept;      // ascending: every object written, at its own number
    std::vector<long long> dropped;   // ascending: every number in use in the source and not written
};

class FullWriter {
public:
    // `table` must have been loaded over exactly `source`. Refused, with nothing produced: an encrypted file
    // (`error.pdf.encrypted`); one with no `/Root` reference (`error.pdf.writeNoRoot`); one whose `/Root` does
    // not fetch (`error.pdf.objectMissing`). Not const, because `XrefTable::fetch` caches.
    static core::Result<FullRewrite> rewrite(std::string_view source, XrefTable& table);

    // Every number in use in `table` that its trailer's `/Root` and `/Info` reach, ascending -- the walk the
    // rewrite makes, for a verify that counts what a written file holds against what it reaches. A stream's
    // `/Length` is not followed (see the header); an object that does not fetch is not reached.
    static std::vector<long long> reachable(XrefTable& table);
};

} // namespace rawpage::pdf::cos

#endif // RAWPAGE_PDF_COS_FULLWRITER_H
