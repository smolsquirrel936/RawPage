// pdf/ops/AppearanceStreamBuilder -- the `/AP /N` normal appearance of an annotation RawPage writes.
// Stage 6 T3 builds HIGHLIGHT; every other type Stage 6 creates is added by the task that builds its
// tool (T6 … T14), and until then `supports()` says no and the sink refuses to write one (D6-6).
//
// ===========================================================================================
// WE DRAW EVERY APPEARANCE OURSELVES (D6-6)
// ===========================================================================================
// A reader that finds no `/AP` invents one, and every reader invents a different one -- PDFium draws a
// `Line` with no `/AP` as nothing at all (Stage 6 guide I-02). So RawPage writes the appearance of
// everything it creates, and the four oracles that judge it are never this file: golden rasters LOOKED
// AT before they are kept, PDFium's own generation as a tolerance control, `qpdf --check`, and Acrobat
// and Firefox by a person. What this file promises is narrower and checked here: the stream is
// well-formed (`ContentStreamBuilder::check`, before it is returned) and its form is self-contained --
// every resource it names is in its own `/Resources`.
//
// ===========================================================================================
// A HIGHLIGHT
// ===========================================================================================
// A form XObject whose `/BBox` IS the annotation's `/Rect`, so ISO 32000-1 §12.5.5's mapping of the
// form onto the rectangle is the identity and the stream draws in page space. Each quadrilateral is
// filled in `/C` (yellow when the annotation has none, Acrobat's default) through an ExtGState with
// `/BM /Multiply` and the opacity as `/ca` and `/CA` -- multiply is what keeps the text under a
// highlight black, and it is how Acrobat draws its own. The four corners are drawn AROUND their
// centre, not in the order written: Acrobat writes top-left, top-right, bottom-left, bottom-right and
// ISO's figure shows another order (guide A-20), and drawing either as written would be a bow tie.
//
// ===========================================================================================
// UNDERLINE, STRIKEOUT, SQUIGGLY, CARET, NOTE -- Stage 6 T6
// ===========================================================================================
// The three text lines are drawn in each quadrilateral's OWN frame -- along its bottom edge and across
// towards its top -- so a line of vertical text is underlined along its side, not across it. Which edge
// is the bottom is read from the corner order itself: Acrobat's two horizontal edges run the same way,
// ISO's opposite ways. An underline is PDFium's own generation's (a 1-point line one point above the bottom
// edge, up to text 14 points tall, growing with the text past it). A strikeout and a squiggle are NOT where
// PDFium puts them, because guide case A-50 measured PDFium's against the glyphs and found them wrong: a
// strikeout through the box's middle sits above the x-height's middle third, and a 2-point squiggle spills out
// of the descender band -- so the strikeout is 42 % of the way up the box and the squiggle between 7 % and
// 16 % of it (D6-32). No blend: only a highlight multiplies. A caret is Acrobat's notched wedge; a note is the `Comment` speech bubble (or the
// `Note` page, the default `/Name`) in `/C`, outlined in black. A `Popup` has no appearance at all.
//
// ===========================================================================================
// FREE TEXT -- Stage 6 T7 (D6-7, D6-8)
// ===========================================================================================
// A box, a callout and a typewriter are one subtype, `FreeText`, told apart by `/IT`. The text is set by
// `core::layoutTypedText` -- the same lines the editor and the box's fitting see -- in the faces it chose
// (Helvetica in WinAnsi, the four CJK collections through their UCS-2 CMaps), each line placed with its own
// `Tm` so no rounding accumulates down a box, in the frame `core::freeTextFrame` gives: the rectangle less
// `/RD`, turned by `/Rotate` so the text reads upright on a turned page, and clipped to it. Behind the text:
// `/IC` filled, an outline `/BS /W` wide in `/C` (none for a typewriter); a callout's leader `/CL` in `/C`,
// with `/LE`'s ending at its first point -- `OpenArrow`, `ClosedArrow` or none, and any other ending refused
// by key until T8 draws line endings. Text in `/DA`'s colour and size. A character no face can show is
// refused BY NAME (`error.edit.typedCharacterRefused`), and so is a `/DA` font this build does not draw
// (`Courier`, …) -- never drawn in another face. A font is not embedded (D6-7): Helvetica is written with its
// `/Widths`, so every reader sets the text on the same advances, and a CJK font as a `Type0` over a
// `CIDFontType0` with `/DW 1000` and a `/FontDescriptor`, which ISO 32000-1 Table 117 requires indirect --
// so free text is the first appearance with objects BESIDE its form, asked for through `AppearanceObjects`.
//
// ===========================================================================================
// LINE, SQUARE, CIRCLE, POLYGON, POLYLINE, INK -- Stage 6 T8 (D6-15, D6-35)
// ===========================================================================================
// Each in `/C` at `/BS /W` (ISO's 1 when absent; no `/C` is no stroke, ISO's transparent), dashed when `/BS /S`
// is `D`, filled with `/IC` where the kind has an inside, and through the blend `/BM` names (a highlight's own is
// `Multiply`, everything else's `Normal`). A Square's or a Circle's outline is inset by half its width inside the
// rectangle less `/RD`, so the whole line shows; a Polygon whose `/BE` is cloudy is drawn as bumps round its
// vertices, and so is a cloudy Square round its box. A Line's and a PolyLine's `/LE` endings -- all ten of ISO
// 32000-1 Table 176, and a callout's too since T8 -- are drawn solid, a closed one filled with `/IC`. INK draws
// each stroke as the centripetal Catmull-Rom curve through its samples, round-capped. The geometry of a cloud's
// bumps, an ending and an ink curve is `core/doc/DrawnShapes`', which the hit test and the rectangle a tool writes
// read too. A measurement (`/IT /LineDimension` …) is refused by key until T12 draws its readout.
//
// ===========================================================================================
// STAMPS -- Stage 6 T10 (D6-16, D6-37)
// ===========================================================================================
// The one kind NOT drawn into its `/Rect`: the form's `/BBox` is the stamp's natural box and its `/Matrix` its
// turn (`core/doc/Stamps` says why), so a reader's Algorithm 8.1 scales it onto the rectangle. A STANDARD or
// DYNAMIC stamp is a rounded border in `/C` round a fill of its tint, its words in base-14 Helvetica-Bold (written
// with its `/Widths`), centred -- and a dynamic stamp's baked lines under them, set as typed text is (Helvetica
// and the CJK collections). An IMAGE stamp paints one image XObject over its box: a JPEG's bytes as they are,
// `/DCTDecode`; anything else `/FlateDecode`, its alpha an `/SMask`. The image and its mask are asked for
// through `AppearanceObjects` by the image's SHA-256 (`core::stampImageKey`), so one image placed many times is
// ONE object in a save (guide A-78). A stamp RawPage cannot redraw -- a foreign one, or an image stamp read from
// a file, whose pixels the model does not hold -- is refused by key (`error.edit.stampNotRedrawable`).
//
// ===========================================================================================
// A REDACTION MARK AND A SOUND -- Stage 6 T14 (D6-25, D6-18)
// ===========================================================================================
// A MARK has TWO appearances, and neither touches the page: `/AP /N`, what a reader shows while the mark is only a
// mark -- each quadrilateral (or, for an area, the rectangle) outlined in `/C`, red when it has none, as Acrobat
// outlines one -- and `/RO`, ISO 32000-1 Table 190's form of what the area WILL look like once the mark is applied:
// each region filled with `/IC` (nothing when it has none) and the overlay text set in `/DA`'s face, size and colour,
// once -- aligned by `/Q` and centred on the region's height -- or, with `/Repeat`, tiled from the region's top-left
// corner to fill it, every copy clipped to the region. Both are forms over the rectangle, drawn in page space. The
// overlay text is typed text (`core::layoutTypedText`), so a character no face can show is refused by name.
//
// A SOUND's icon is ISO 32000-1 Table 185's `Speaker` (the default) or `Mic`, drawn on a 20-unit grid in `/C` and
// outlined in black, as a note's is; any other `/Name` is refused by name.

#ifndef RAWPAGE_PDF_OPS_APPEARANCESTREAMBUILDER_H
#define RAWPAGE_PDF_OPS_APPEARANCESTREAMBUILDER_H

#include "core/Result.h"
#include "core/doc/Annotation.h"
#include "core/doc/TypedText.h"
#include "pdf/cos/CosObject.h"

#include <QStringView>

#include <optional>
#include <string>
#include <vector>

namespace rawpage::pdf::ops {

struct Appearance {
    cos::CosObject dictionary;   // the form XObject: a Stream with an empty data descriptor; the
                                 //   writer adds `/Length` and writes `data` after it
    std::string data;            // the content stream, unencoded
};

// THE OBJECTS AN APPEARANCE'S FORM REFERS TO -- a font, and what a font refers to (Stage 6 T7). Each is asked
// for by a KEY, and the same key twice is one object: a compose that draws ten comments in Helvetica writes
// Helvetica once. `pdf/ops/MutationSinkImpl` writes them into the update.
class AppearanceObjects {
public:
    virtual ~AppearanceObjects() = default;
    // The reference `object` is written at. A key already asked for returns its object's reference, and
    // `object` is not written again.
    virtual cos::CosRef shared(const std::string& key, cos::CosObject object) = 0;
    // Stage 6 T10: the same for a STREAM -- `dictionary` with an empty data descriptor, `data` its bytes as they
    // are to be written (already encoded: the writer adds `/Length` and nothing else). An image stamp's picture.
    virtual cos::CosRef sharedStream(const std::string& key, cos::CosObject dictionary, std::string data) = 0;
};

// Keeps what it is handed, numbered from `first` up -- for a dry run, and for a case that reads the objects.
class CollectedAppearanceObjects final : public AppearanceObjects {
public:
    explicit CollectedAppearanceObjects(long long first = 1) : first_(first) {}
    cos::CosRef shared(const std::string& key, cos::CosObject object) override;
    cos::CosRef sharedStream(const std::string& key, cos::CosObject dictionary, std::string data) override;

    struct Entry {
        std::string key;
        cos::CosRef at;
        cos::CosObject object;
        std::optional<std::string> data;   // a stream's bytes (T10)
    };
    const std::vector<Entry>& entries() const noexcept { return entries_; }
    const cos::CosObject* find(cos::CosRef at) const;

private:
    long long first_;
    std::vector<Entry> entries_;
};

class AppearanceStreamBuilder {
public:
    // Whether this build can draw `subtype`. Stage 6 T3: Highlight; T6: Underline, StrikeOut, Squiggly,
    // Caret, and Text (a note, its `Comment` and `Note` icons -- another `/Name` is refused by `normal`); T7:
    // FreeText -- a box, a callout, a typewriter; T8: Line, Square, Circle, Polygon, PolyLine and Ink; T10: Stamp
    // (a foreign one refused by `normal`, `error.edit.stampNotRedrawable`); T11: FileAttachment; T14: Redact (with
    // its `/RO`) and Sound.
    static bool supports(core::AnnotationSubtype subtype) noexcept;

    // A kind that has NO appearance of its own: a `Popup` (ISO 32000-1 §12.5.6.14), which a reader draws as
    // its own window -- RawPage's overlay draws it, and PDFium draws none (Stage 6 T6). One is written with no
    // `/AP`, and verify asks it for none.
    static bool drawsNothing(core::AnnotationSubtype subtype) noexcept;
    // Stage 6 T9: and an annotation that has none -- a pop-up, or a STATE REPLY (D6-12): a hidden `Text`
    // carrying `/StateModel`, which Acrobat writes with no `/AP` and no reader draws.
    static bool drawsNothing(const core::Annotation& annotation) noexcept;

    // The normal appearance of `annotation`, the objects its form refers to handed to `objects`.
    // `error.pdf.appearanceUnsupported` for a subtype this build cannot draw; `error.pdf.appearanceGeometry`
    // for geometry it cannot draw from (a highlight with no whole quadrilateral, a non-finite coordinate, a
    // colour of 2 components); `error.edit.typedCharacterRefused` for free text holding a character no face
    // can show.
    static core::Result<Appearance> normal(const core::Annotation& annotation, AppearanceObjects& objects);
    // The same, its objects kept nowhere: whether `annotation` can be drawn, and its stream.
    static core::Result<Appearance> normal(const core::Annotation& annotation);

    // Stage 6 T14 (D6-25): a redaction mark's `/RO` -- see the header. `error.pdf.appearanceUnsupported` for anything
    // that is not a `Redact`.
    static core::Result<Appearance> redactionOverlay(const core::Annotation& annotation, AppearanceObjects& objects);
    static core::Result<Appearance> redactionOverlay(const core::Annotation& annotation);

    // The font object for `font` -- and, for a CJK one, its CIDFont and descriptor -- through `objects`, keyed
    // so each is written once. What free text's `/Font` resources name; public so a case can set text in
    // exactly the objects an appearance writes (`tests/render/tst_typedtext.cpp`).
    static cos::CosRef fontObject(core::TypedFont font, AppearanceObjects& objects);
    // `text` (in `font`'s faces only) as the codes `font`'s encoding gives it: WinAnsi bytes, or UCS-2,
    // big-endian. Empty for a character the font has no code for.
    static std::string encodeText(core::TypedFont font, QStringView text);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_APPEARANCESTREAMBUILDER_H
