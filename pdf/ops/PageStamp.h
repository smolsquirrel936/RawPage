// pdf/ops/PageStamp -- a PDF page as a stamp's picture: read once when the stamp is made, and drawn as one form XObject
// wherever it is placed. Stage 7 T13 (`Stage7-Assembly-Plan.md` D7-36; Stage 6's Q7, `Stage6-Annotations-Plan.md` D6-16).
//
// ===========================================================================================
// THE PAGE COMES AS THE COPIER COPIED IT
// ===========================================================================================
// A page stamp's page is `core::StampPage::graph`: the payload `ObjectGraphCopier` writes for an insert (D7-6) --
// one page, its content, everything its resources reach, the inherited boxes, `/Resources` and `/Rotate` made the
// page's own. `fromPayload` reads it once, when a person makes the stamp, and answers the size the page SHOWS (its
// CropBox within its MediaBox, turned by `/Rotate`), refusing by `error.edit.stampPageUnsupported` -- named by what it
// met -- a payload that is not ONE page, a page whose content cannot be read, and a page with nothing to show.
//
// ===========================================================================================
// IT IS DRAWN AS A VECTOR: ONE FORM, ITS GRAPH WRITTEN ONCE PER SAVE
// ===========================================================================================
// `form` writes the page as a form XObject (ISO 32000-1 §8.10): `/BBox` the CropBox -- what the page shows, so
// content past it is clipped as the page clips it -- `/Resources` and `/Group` the page's, and the content stream
// the page's own bytes, its `/Filter` kept. A page whose `/Contents` is an ARRAY is decoded, each stream after the
// one before with a line break between (§7.8.2), and written again `/FlateDecode`d as one. Every object the form
// reaches is asked for through `AppearanceObjects` under a key made of the graph's SHA-256 and the payload's own
// number (`stamppage:<sha>:<n>`), RESERVED before any is written, so objects that name each other -- a pattern and
// its resources, a cycle a hostile file holds -- are written once each and point at each other. So one page placed
// many times in one save is ONE graph (guide A-90) and each placement's own appearance a three-operator stream. A
// reference to the page itself, its page-tree node or the payload's catalog -- which no resources should name -- is
// written as `null`: the copy stops at the page tree, as the copier's does.
//
// The turn is NOT the form's: `form` answers the CropBox and `/Rotate`, and the stamp's own appearance turns the page
// upright (`showMatrix`, at a scale of 1) inside a `/BBox` of the size it shows, so the form is the same object whatever
// the stamp's turn or size -- and the scale onto the stamp's rectangle is the reader's, never a written real.
//
// WHAT IS NOT DRAWN: the page's annotations -- a form draws content, and a stamp is one picture, not a page of
// comments (D7-36(c)); and its optional-content groups are not added to the document's `/OCProperties`, so content
// the source hid in a layer is drawn as each reader draws a group no `/OCGs` lists (T17 measures what that is).
//
// Portable: `rawpage_ops`, under `portable-asan` too.

#ifndef RAWPAGE_PDF_OPS_PAGESTAMP_H
#define RAWPAGE_PDF_OPS_PAGESTAMP_H

#include "core/Geometry.h"
#include "core/Result.h"
#include "core/doc/Annotation.h"
#include "pdf/cos/CosObject.h"
#include "pdf/ops/AppearanceStreamBuilder.h"

#include <QByteArray>

#include <array>

namespace rawpage::pdf::ops {

class PageStamp {
public:
    // What `form` wrote: the form's reference, and the box and turn the stamp's appearance maps from.
    struct Form {
        cos::CosRef at;
        core::PdfRect box;   // the CropBox within the MediaBox, in the page's own space -- the form's `/BBox`
        int rotate = 0;      // the page's `/Rotate`, 0, 90, 180 or 270
    };

    // A one-page payload as a stamp's page: its graph, and the size the page shows. Refused by key (see the header).
    static core::Result<core::StampPage> fromPayload(const QByteArray& graph);

    // `page` written as a form XObject through `objects` -- see the header.
    static core::Result<Form> form(const core::StampPage& page, AppearanceObjects& objects);

    // The matrix that maps the page as SHOWN -- `box` turned clockwise by `rotate` -- onto `0 0 width height`: what the
    // stamp's appearance concatenates before it paints the form.
    static std::array<double, 6> showMatrix(const core::PdfRect& box, int rotate, double width, double height) noexcept;
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_PAGESTAMP_H
