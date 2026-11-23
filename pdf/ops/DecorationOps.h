// pdf/ops/DecorationOps -- what a header, a footer or a Bates number is in COS. Stage 7 T14, decisions D7-7 and D7-44
// (`Stage7-Assembly-Plan.md` §3).
//
// ===========================================================================================
// THREE STREAMS AND TWO KEYS, EACH FOUND AGAIN BY ITS MARK (D7-7)
// ===========================================================================================
// A decoration on a page is:
//   * a FORM XObject that draws it -- each `core::DecorationLine` set at its matrix in D6-7's fonts (never embedded:
//     Helvetica in WinAnsiEncoding, the CJK collections through their UCS-2 CMaps), its `/BBox` what the page shows;
//   * a three-operator stream that paints the form -- `/Artifact << /Type /Pagination /Subtype /Header >> BDC q
//     /RawPageDec<id> Do Q EMC`, a pagination artifact (ISO 32000-1 §14.8.2.2), so a reader extracting the page's
//     text or its structure knows it is not the page's own -- listed LAST in `/Contents` (in front) or FIRST (behind);
//   * in front, a WRAPPER round the page's content: `q ... cm ... re W n` before it and `Q` after, so whatever state
//     the content leaves -- a `cm` it never undid -- is undone before the decoration draws, and with *shrink* the
//     content is scaled to clear the bands (`core/doc/HeaderFooter`);
// all of them carrying `/RawPageDecoration << /Kind /HeaderFooter|/Bates|/Watermark /Id <key> >>` (the wrapper's with
// `/Wrap true` too). The form is named in the page's `/Resources /XObject` as `RawPageDec<key>`.
//
// REMOVING ONE IS EXACT. `/Contents` is taken apart by mark (`PageOps::contentLayers`) and put back without the
// decoration's streams -- one stream reference again where there was one. `/Resources` cannot be taken apart that
// way: it may be inherited, or an object other pages share, which RawPage never edits. So the FIRST decoration a page
// gets records the page's `/Resources` as written in `/RawPageResources` (`/Absent` when it had none of its own) and
// writes a dictionary of the page's own -- the resources it drew with, and the forms -- and when the last one goes,
// the record is written back and taken away. A `/Contents` that was a reference to an ARRAY object is recorded the
// same way (`/RawPageContents`), since a list put back is a direct array. Both keys are RawPage's, private (ISO 32000-1
// §7.12, second-class names a reader ignores), and on the page only while it is decorated -- which is what the
// guide's §3.8 measures: apply, save, remove, save, and the page, its `/Contents` and its `/Resources` equal the
// original's, byte for byte in value.
//
// A WATERMARK -- Stage 7 T15 (D7-45) -- is the same three streams, its form painting a SOURCE once per tile:
//   `/RawPageGS gs  q <tile> cm <source> cm /RawPageSrc Do Q  q ... Q`
// the opacity an ExtGState (`/ca` and `/CA`), each tile a matrix from the source's natural box into the page
// (`core::layoutWatermark`), and the source an XObject written once a save however many pages draw it: the text as a
// form of its own, an image as an image stamp's is (`AppearanceStreamBuilder::imageObject`), a page as a page stamp's
// is (`PageStamp::form`) inside a TRANSPARENCY GROUP -- so the opacity applies to the page as one picture, whatever
// opacity its own content sets. The form's mark records the watermark's spec without its source's bytes (`/Spec`, the
// JSON as a hex string), which is what a list of watermarks shows and an update starts from. One for printing or for
// the screen only carries `/OC`: an optional-content group of its own, `/Usage` `/Print` and `/View` saying which, marked
// with the watermark's id -- listed in `/OCProperties` while a page draws it, and taken out with the last (the sink).
//
// A DECORATION RAWPAGE DID NOT WRITE IS NEVER ONE. Acrobat's header is `/Artifact` content inside the page's own
// stream, or a form named `/HeaderFooter` in its `/PieceInfo`; neither carries our mark, so neither is listed, taken
// or rewritten (guide A-94).
//
// Pure `pdf/`: no engine -- `tst_decoration` in `rawpage_tests`, and what a reader draws `tst_decorationengine`.

#ifndef RAWPAGE_PDF_OPS_DECORATIONOPS_H
#define RAWPAGE_PDF_OPS_DECORATIONOPS_H

#include "core/doc/HeaderFooter.h"
#include "pdf/cos/CosObject.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/AppearanceStreamBuilder.h"

#include <QJsonObject>
#include <QString>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rawpage::pdf::ops {

class DecorationOps {
public:
    // The page keys the record lives in, and the value `/RawPageResources` takes for a page with no `/Resources` of its own.
    static constexpr const char* kResourcesRecord = "RawPageResources";
    static constexpr const char* kContentsRecord = "RawPageContents";
    static constexpr const char* kAbsent = "Absent";
    static constexpr const char* kResourcePrefix = "RawPageDec";

    // `RawPageDec<id>` -- the form's name in the page's `/Resources /XObject`.
    static std::string resourceName(const QString& id);
    static bool isDecorationResource(const std::string& name);

    // The form's content: every line at its matrix, in its colour, underlined where asked.
    static std::string formContent(const core::PageDecoration& decoration);
    // The form's dictionary -- an empty data descriptor; the writer adds `/Length` -- its fonts asked of `objects`.
    static cos::CosObject formDictionary(const core::PageDecoration& decoration, const QString& id, AppearanceObjects& objects);

    // The stream that paints the form, and its dictionary.
    static std::string drawContent(const core::PageDecoration& decoration, const QString& id);
    // S7 T15: `behind` marks the painting stream of one drawn behind the content `/Behind true` (see `PageOps::DecorationMark`).
    static cos::CosObject markedDictionary(const QString& id, core::DecorationKind kind, bool behind = false);

    // THE PAGE'S RESOURCES WHILE DECORATED (see the header): `effective` -- what the page draws with, its own or
    // inherited, resolved -- with every `RawPageDec` form dropped from its `/XObject` and `forms` added, in order. The
    // `/XObject` it had is read through `table` when it is a reference; nothing `effective` refers to is changed.
    static cos::CosObject decoratedResources(cos::XrefTable& table, const cos::CosObject& effective,
                                             const std::vector<std::pair<std::string, cos::CosObject>>& forms);
    // The form a page's `/Resources` names `RawPageDec<id>` -- a reference -- or none.
    static std::optional<cos::CosObject> formOf(cos::XrefTable& table, const cos::CosObject& resources, const QString& id);

    // ---- Watermarks -- Stage 7 T15 (D7-45); see "A WATERMARK" in the header -----------------------------------------
    static constexpr const char* kSourceResource = "RawPageSrc";
    static constexpr const char* kStateResource = "RawPageGS";

    // The XObject a watermark paints, written through `objects`: text as a form of its own (keyed by `id`), an image
    // by its SHA-256, a page as `PageStamp` writes it inside a transparency group (keyed by its graph's SHA-256).
    static core::Result<cos::CosRef> watermarkSource(const core::WatermarkDrawing& drawing, const QString& id,
                                                     AppearanceObjects& objects);
    // The form's content: the opacity set, then every tile -- `q <tile> cm <source> cm /RawPageSrc Do Q`.
    static std::string watermarkContent(const core::WatermarkDrawing& drawing);
    // The form's dictionary: `/BBox` what the page shows, the opacity and the source in its resources, `/OC` the
    // group for a print- or screen-only one, and the mark -- `/Spec` the drawing's record.
    static cos::CosObject watermarkFormDictionary(const core::PageDecoration& decoration, const QString& id,
                                                  const cos::CosObject& source, const std::optional<cos::CosObject>& group);
    // The optional-content group of a print- or screen-only watermark: `/Usage` saying which, marked with its id.
    static cos::CosObject groupDictionary(const QString& id, core::WatermarkUsage usage);
    // Read back from a form's or a group's dictionary (resolved): what its mark records -- the spec, and the id.
    static std::optional<QJsonObject> recordedSpec(const cos::CosObject& form);
    static std::optional<QString> markedId(const cos::CosObject& dictionary);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_DECORATIONOPS_H
