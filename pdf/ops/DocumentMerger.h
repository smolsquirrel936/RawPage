// pdf/ops/DocumentMerger -- many PDFs built into one new document. Stage 7 T10, decision D7-16 (`Stage7-Assembly-
// Plan.md` §3) and D7-33; guide cases A-66 ... A-72 (`Stage7-Assembly-Testing-Guide.md` §5.12).
//
// -------------------------------------------------------------------------------------------
// EACH INPUT IS A PAYLOAD, AND THE MERGE IS THE PAYLOADS JOINED
// -------------------------------------------------------------------------------------------
// Each input is read, opened by the first-party reader and its pages copied by `ObjectGraphCopier` with MERGE'S TWO
// OPTIONS -- its widgets with their fields, its outline -- into a payload (D7-6's shape: a PDF of its own). An input
// that cannot be taken is SKIPPED and reported, by key: no file there (`error.open.notFound`), not a PDF -- no
// `%PDF-` in its first 1,024 bytes, as ISO 32000-1 7.5.2 allows (`error.open.notPdf`), encrypted
// (`error.pdf.encrypted`), opened only by a repair that lost or guessed something (`error.merge.repairedWithLoss`: a
// repair the report rates `Degraded` or worse -- one it rates `Recovered` is merged, as an insert copies it), a page
// tree the walk noted anything about (`error.pdf.pageTreeDamaged`), a page range outside it (`error.pdf.pageOutOfRange`)
// or a copy past its budget (`error.copy.tooLarge`).
//
// The payloads are then JOINED into one, numbered afresh -- the catalog 1, the one tree node 2, every page from 3 in
// the order merged (a separator after each input but the last, when asked), then each payload's other objects --
// and what a document needs beyond its pages is built from what each payload's catalog carried:
//   * THE OUTLINE. A bookmark per input (`MergeOptions::bookmarks`), titled with its file name or its `/Title`, landing
//     on its first page and CLOSED, with the input's own outline beneath it; with none, each input's items are the
//     top level, in order. Every destination is the copier's: repointed at the copy, or cleared and reported where a
//     range left its page out.
//   * THE FIELDS. Each input's `/Fields` roots added in order; a root whose name an earlier input's root already has
//     is RENAMED (`name` -> `name_2`, the first free number) -- the root's `/T` alone, so every field and widget below
//     keeps its `/Parent` chain -- or, with `renameCollidingFields` false, MERGED into the earlier field: two terminal
//     fields become one with both inputs' widgets, the earlier's value and appearance on all of them (a widget that is
//     its field is split into a field and a widget first), and two non-terminal ones become one whose kids are both
//     inputs', a kid colliding again merged again. Two fields that cannot be one -- different `/FT`, or one terminal
//     and one not -- are renamed instead, and reported so. Every collision is reported either way. `/DR`'s resource
//     names are unioned, the first input's winning a name two inputs use; `/NeedAppearances` is set when any input
//     set it or any field was merged, so a reader that draws from the value draws the value.
//   * THE LAYERS. Every input's groups in `/OCGs`, each input's `/Order` under a label of its own -- the name its
//     bookmark would have -- and `/OFF`, `/Locked`, `/RBGroups` joined, `/AS` joined per event and category. Groups
//     of one name in two inputs stay two groups: a merge does not decide that two inputs' `Draft`s are one layer.
// The joined payload is BUILT by `DocumentAssembler` -- a balanced tree, a fresh `/ID`, written by `FullWriter` -- and
// VERIFIED against it as an extract's is (guide A-47's checks).
//
// PROGRESS AND CANCEL. `progress` is asked before each input, before the build and before the verify; an answer of
// false refuses the merge by `error.merge.cancelled`. Nothing is written anywhere: the result is bytes in memory.
//
// PORTABLE, in `rawpage_ops`: no engine -- `portable-asan` builds it, and `fuzz_assembly` merges its input with itself.

#ifndef RAWPAGE_PDF_OPS_DOCUMENTMERGER_H
#define RAWPAGE_PDF_OPS_DOCUMENTMERGER_H

#include "core/Result.h"
#include "core/doc/Merge.h"

#include <QString>

#include <functional>
#include <string>
#include <vector>

namespace rawpage::pdf::ops {

class DocumentMerger {
public:
    struct Input {
        QString name;                    // the file's name without its suffix: its bookmark's title, its layers' label
        std::vector<int> pages;          // zero-based, strictly ascending; empty for every page
        std::function<core::Result<std::string>()> read;   // the file's bytes, read when its turn comes
    };

    static core::Result<core::MergedDocument> merge(const std::vector<Input>& inputs, const core::MergeOptions& options,
                                                    const core::MergeProgress& progress);

    // `IDocumentBackend::mergeFiles`: each input read from its path, at its turn.
    static core::Result<core::MergedDocument> mergeFiles(const std::vector<core::MergeInput>& inputs, const core::MergeOptions& options,
                                                         const core::MergeProgress& progress);

    // What a colliding name becomes: `name_n`.
    static QString renamed(const QString& name, int n);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_DOCUMENTMERGER_H
