// core/doc/CopiedPages -- pages copied out of another document, as a command carries them. Stage 7 T5
// (`Stage7-Assembly-Plan.md` D7-4, D7-6).
//
// A page from elsewhere is a GRAPH: its content, its fonts and images, its annotations and their appearances,
// and the attributes it inherited from a page tree it leaves behind. `pdf/ops/ObjectGraphCopier` copies that
// graph once, when a person picks the pages, and what it produces is this: the copied objects as one payload of
// PDF bytes -- `pdf/`'s format, which `core/` carries and never reads (`core/` holds no PDF syntax) -- the
// number of pages it holds, and what the copy met.
//
// THE COMMAND CARRIES THE COPY, NOT THE SOURCE FILE (D7-6). `InsertPagesCommand` and `ReplacePagesCommand`
// serialize `graph` into the recovery journal -- a payload over 64 KiB goes to a side file named by its
// SHA-256 (D6-23, `RecoveryJournal`) -- so a replay after a crash needs no source, and a source changed or
// deleted after the insert changes nothing. Copying at the command, not at every compose, is also what keeps a
// compose from re-reading a 200 MB source.
//
// `InsertedPages` is what `MutationSink::insertPages` and `replacePages` answer: each page as the model holds
// it -- boxes and rotation as the copy draws it, the label the sink gives it -- and each page's annotations, as
// COPIES with identities of their own (`AnnotationId::ofCreated`, named from the command's key, so a replay
// names the same ones; D6-4's rule, as `DuplicatePagesCommand` names its copies).
//
// Pure: QtCore and the standard library -- `rawpage_tests`, under `portable-asan` too.

#ifndef RAWPAGE_CORE_DOC_COPIEDPAGES_H
#define RAWPAGE_CORE_DOC_COPIEDPAGES_H

#include "core/doc/AnnotationStore.h"
#include "core/doc/Page.h"
#include "core/doc/PageMap.h"

#include <QByteArray>

#include <vector>

namespace rawpage::core {

struct CopiedPages {
    QByteArray graph;            // D7-6's payload: `pdf/ops`' to read, never `core/`'s
    int pageCount = 0;           // pages it holds, in the order they were picked
    PageCommandReport report;    // what the copy met -- a link whose page stayed behind, a widget, a tag tree

    bool isEmpty() const noexcept { return pageCount <= 0 || graph.isEmpty(); }
};

struct InsertedPages {
    std::vector<Page> pages;                              // one per page the payload holds, in order
    std::vector<AnnotationStore::PageListing> listings;   // parallel to `pages`
    PageCommandReport report;                             // what the sink met
};

// The largest payload a serialized insert or replace may carry, so that its base64 (341 MiB) fits the side file
// `RecoveryJournal` reads back (at most 360 MiB, `kMaxSideFileBytes`). A copy past it is refused when it is made.
inline constexpr qsizetype kMaxCopiedGraphBytes = 256LL * 1024 * 1024;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_COPIEDPAGES_H
