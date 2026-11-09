// core/doc/TableOfContents -- a contents PAGE made from the bookmark tree. Stage 7 T11, decision D7-11
// (`Stage7-Assembly-Plan.md` §3; `Features.md` §5.4: *with working links*).
//
// D7-11: the page is a GENERATED page inserted by ONE `InsertPagesCommand` -- one undo step -- its entries drawn in
// D6-7's fonts (Q5(e): base-14 Helvetica for WinAnsi, the four predefined CJK fonts, anything else refused by name)
// with dot leaders and the target pages' labels, and each line a `Link` annotation to its bookmark's destination.
// The payload's builder is `pdf/ops/ObjectGraphCopier::tableOfContents`; this is what it is asked for, and how an
// outline becomes that request -- headless.
//
// A LINK IN A PAYLOAD CANNOT NAME A PAGE OF THE DOCUMENT IT LANDS IN: the payload is a separate small PDF. So each
// entry names its target by the page's INDEX in the document BEFORE the insert, and the payload says so (its catalog's
// `/RawPageLocalTargets`): the sink resolves each to that page's OBJECT when the insert runs -- before the order
// changes -- and writes the link by reference from then on (`pdf/ops/MutationSinkImpl::insertPages`), so a later move
// needs no repair (D7-2) and a deletion clears it as it clears any link.

#ifndef RAWPAGE_CORE_DOC_TABLEOFCONTENTS_H
#define RAWPAGE_CORE_DOC_TABLEOFCONTENTS_H

#include "core/Geometry.h"
#include "core/doc/Destinations.h"
#include "core/doc/Outline.h"
#include "core/doc/TypedText.h"

#include <QString>

#include <vector>

namespace rawpage::core {

class PageTree;

struct TableOfContentsEntry {
    QString title;
    int depth = 0;              // 0 at the top level
    QString label;              // what the target page is called: its label, or its number
    Destination destination;    // `pageIndex` the target's index in the document BEFORE the insert
};

struct TableOfContents {
    QString heading;            // the page's title -- the catalog's `bookmarks.toc.heading`
    std::vector<TableOfContentsEntry> entries;
    PdfSize pageSize{612.0, 792.0};
    TypedFont hanPreference = TypedFont::Helvetica;   // which CJK face a Han title is set in (D6-7)
};

// One entry per bookmark that goes to a page of this document, depth first; a bookmark that goes nowhere -- an action,
// a destination cleared -- is left out, its children kept. Each labelled as `pages` labels its target, else by number.
TableOfContents tableOfContentsFrom(const Outline& outline, const PageTree& pages, const QString& heading);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_TABLEOFCONTENTS_H
