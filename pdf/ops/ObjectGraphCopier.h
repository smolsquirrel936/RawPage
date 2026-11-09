// pdf/ops/ObjectGraphCopier -- pages out of one document, with everything they reach. Stage 7 T5, decision D7-4
// (`Stage7-Assembly-Plan.md` §3).
//
// -------------------------------------------------------------------------------------------
// A PAGE IS A GRAPH, AND THE COPY IS THE GRAPH ONCE
// -------------------------------------------------------------------------------------------
// A page reaches its content streams, fonts, images, colour spaces, patterns, annotations and their
// appearances. `copy` follows every reference from the pages it is given -- breadth first, a visited set, an
// object budget -- and numbers each object it reaches EXACTLY ONCE, so two pages sharing a font share it in the
// copy (guide A-39) and a cycle is a visited object, not a loop. One call over several pages of one source is
// what shares; a copy per page would write the font once per page.
//
// IT STOPS AT THE PAGE TREE. `/Parent` is never followed, and a reference to an object the source's page tree
// holds -- its catalog, an intermediate node, a page not being copied -- becomes `null`: a copy that followed it
// would bring the whole source along (guide A-41). A reference to a page that IS being copied is the copy.
//
// INHERITANCE IS MATERIALIZED, and it is the copier's whole difficulty. A page may take `/Resources`,
// `/MediaBox`, `/CropBox` and `/Rotate` from ancestors it leaves behind, and a copy whose `/Resources` stayed on
// the old parent draws with no fonts. Each of the four is resolved DOWN THE PATH THE WALK TOOK -- the walk's
// rule, never up `/Parent`, which is a claim a damaged file gets wrong -- and written on the copied page itself:
// `/Resources` always (an empty dictionary where nothing gave one), `/MediaBox` always (US Letter where nothing
// gave one, which is what every reader substitutes), `/Rotate` always (0 included), `/CropBox` where one was
// given. A copy so made draws the same under ANY parent, which is what lets an insert and a replace put it
// anywhere (guide A-40, whose control -- a copier that copies the page dictionary alone -- passes on
// `pages-900.pdf`, which is why that file is not the fixture).
//
// -------------------------------------------------------------------------------------------
// ANNOTATIONS COME WITH THEIR PAGE -- and only what can follow them
// -------------------------------------------------------------------------------------------
//   * Each annotation a copied page lists comes with it, `/P` naming the copy, a DIRECT dictionary in
//     `/Annots` made an object of its own (every copied annotation is then named alike, by number).
//   * `/Popup`, `/IRT` and `/Parent` are kept when their target is copied in the same call and DROPPED when it
//     is not; anywhere else, a reference to an annotation is `null`.
//   * A LINK whose destination page is copied in the same call points at the copy -- a destination named by
//     NAME written as the explicit array the name stood for, since the name means nothing in the document it
//     lands in -- and one whose page stayed behind keeps its rectangle and border with its action cleared, and
//     is REPORTED (D7-2's rule for a link to a deleted page). A link to another file, a URI, any other action, is
//     copied as written.
//   * **A WIDGET IS NOT COPIED** (the user's choice, 2026-10-31, amending D7-4 for T5): PDFium draws a widget
//     only through the form its field belongs to, so a widget copied without its field draws nothing, and the
//     fields are D7-16's -- merge's, from T10 (below). Each one left is reported (`WidgetNotCopied`), as
//     `DuplicatePagesCommand` reports a duplicate's.
//     STAGE 7 T10 -- `Options::fields`, which merge sets (D7-16): a widget IS copied, with the fields above it. Every
//     field node on a copied widget's `/Parent` chain is copied once, its `/Kids` narrowed to the widgets and nodes
//     the copy holds, and the payload's catalog gets an `/AcroForm` whose `/Fields` lists the copied fields' roots in
//     the source's order (a root `/Fields` never listed after them), with the source's `/DA`, `/Q`, `/DR`,
//     `/NeedAppearances` and `/SigFlags` and its `/CO` narrowed likewise; `/XFA` stays behind. Insert and replace
//     still copy no widget (the user's choice at T10: their fields are an open document's, `Deferred-Work.md`).
//   * STAGE 7 T10 -- `Options::outline`, which merge sets: the source's OUTLINE comes too, as the payload catalog's
//     `/Outlines`, every item kept with its title, colour, style, open state and any other key, its links rebuilt
//     and its `/Count` recounted. A destination is treated as a link's is: one whose page is copied names the copy (a
//     named one written as the array it stood for), one whose page stayed behind is CLEARED -- the item kept with its
//     title, never dropped (D7-2) -- and reported, and any other action is copied as written. `/SE` goes: the
//     structure tree stays behind.
//   * `/StructParent` and `/StructParents` are REMOVED -- a copied page arrives UNTAGGED, because a structure
//     tree cannot be grafted without the parent tree it indexes -- and each tagged page is reported.
//   * `/B` (a page's beads) is removed: its threads are not copied.
//
// -------------------------------------------------------------------------------------------
// WHAT IT PRODUCES: A PDF OF ITS OWN (D7-6's payload)
// -------------------------------------------------------------------------------------------
// The copy is written as a small, complete PDF: `%PDF-1.7`, a catalog (object 1), one page-tree node (2) whose
// kids are the copied pages (3, 4, ...) in the order given, then every other copied object, one classic
// cross-reference table and a trailer. Deterministic -- the same pages of the same bytes give the same payload.
// So every first-party reader reads it as it reads any file (`XrefTable::load`, `PageOps::walk`,
// `AnnotationReader`), the sink takes its objects from it at every compose (`MutationSinkImpl::insertPages`),
// and a case can open it in PDFium. Streams are copied ENCODED, byte for byte, their `/Length` the length
// written.
//
// REFUSED, by key, with nothing produced: an encrypted source (`error.pdf.encrypted` -- the first-party reader
// declines it, D2-6), a page set that is not one (`error.pdf.pageOutOfRange`), a source whose tree the walk
// noted anything about (`error.pdf.pageTreeDamaged` -- which page is which would be a guess), and a graph past
// the budget (`error.copy.tooLarge`).

#ifndef RAWPAGE_PDF_OPS_OBJECTGRAPHCOPIER_H
#define RAWPAGE_PDF_OPS_OBJECTGRAPHCOPIER_H

#include "core/Result.h"
#include "core/Geometry.h"
#include "core/doc/CopiedPages.h"
#include "core/doc/TableOfContents.h"
#include "core/doc/PageMap.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rawpage::pdf::cos {
class XrefTable;
}

namespace rawpage::pdf::ops {

struct PageWalk;

class ObjectGraphCopier {
public:
    struct Options {
        // Objects one copy may hold. A million is far past any real page set -- `pages-900.pdf` whole is a few
        // thousand -- and bounds what a hostile graph can make a copy hold.
        long long maxObjects = 1'000'000;
        // Bytes the payload may reach: `core::kMaxCopiedGraphBytes`, what the recovery journal can carry.
        std::size_t maxBytes = 256u * 1024u * 1024u;
        // S7 T10 (D7-16), MERGE'S TWO, off for every other copy: the copied pages' WIDGETS come with their fields, and
        // the source's OUTLINE comes with them -- each written into the payload's catalog (see the header).
        bool fields = false;
        bool outline = false;
        // ... and every optional-content group the source lists, not only those the copied pages reach: a merge takes a
        // document's layers whole, a label-only parent group included (D7-16).
        bool allLayers = false;
    };

    struct Copied {
        std::string pdf;                  // the payload: a PDF holding exactly the copied pages
        int pageCount = 0;
        long long objects = 0;            // objects the copy holds, catalog and node excluded
        core::PageCommandReport report;   // what the copy met; each reference's `page` the SOURCE index
    };

    // `pages` -- indices into `walk`, which must be `source`'s -- as a strictly ascending set.
    static core::Result<Copied> copy(cos::XrefTable& source, const PageWalk& walk, const std::vector<int>& pages,
                                     const Options& options);
    static core::Result<Copied> copy(cos::XrefTable& source, const PageWalk& walk, const std::vector<int>& pages)
    {
        return copy(source, walk, pages, Options{});
    }

    // `count` blank pages of `width` by `height` points: no content stream at all, an empty `/Resources` -- the
    // payload of an insert of blank pages (guide A-45). Refused for a size outside ISO 32000-1 Annex C's
    // 3 ... 14,400 units, or a count outside 1 ... `core::kMaxPagesPerCommand`.
    static core::Result<Copied> blank(double width, double height, int count);

    // What `IDocumentBackend::copyPages` and `blankPages` answer, for every backend that implements them: `bytes`, a
    // whole file, opened through `cos::openWithRepair` (an encrypted one refused, `error.pdf.encrypted`), walked and
    // copied; and the blank payload -- each as the command carries it.
    static core::Result<core::CopiedPages> copyFile(std::string_view bytes, const std::vector<int>& pages);
    // The pages `copyFile` would find in `bytes` -- refused as it refuses (S7 T8: `IDocumentBackend::filePageCount`).
    static core::Result<int> countFile(std::string_view bytes);
    static core::Result<core::CopiedPages> blankPages(core::PdfSize size, int count);

    // STAGE 7 T11 (D7-11): a CONTENTS PAGE -- or as many as its entries need -- as the payload one `InsertPagesCommand`
    // carries: the heading and each entry drawn in D6-7's faces (a title none can show refused by its character,
    // `error.edit.typedCharacterRefused`), indented by depth, dot leaders to the target's label, and a `Link` per entry
    // naming its target by INDEX in the document before the insert, the payload marked so the sink makes each a
    // reference (`core/doc/TableOfContents.h`). Refused for no entries (`error.outline.nothingToList`).
    static core::Result<core::CopiedPages> tableOfContents(const core::TableOfContents& toc);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_OBJECTGRAPHCOPIER_H
