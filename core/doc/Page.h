// core/doc/Page -- one page's geometry and identity. Stage 2 T9, brought forward from Stage 3 by
// decision D2-5: `core/render/TileGrid` is its first consumer and needs page geometry before any
// mutation exists.
//
// -------------------------------------------------------------------------------------------
// FIVE BOXES, AND THE ONE THAT MATTERS IS NOT `/MediaBox`
// -------------------------------------------------------------------------------------------
// ISO 32000-1 14.11.2 defines MediaBox, CropBox, BleedBox, TrimBox and ArtBox. **What a reader
// draws is the CropBox**, not the MediaBox -- a print-ready file routinely has a MediaBox with
// 9 mm of bleed and crop marks around a CropBox that is the actual page, and a viewer that laid
// out MediaBoxes would show every page surrounded by a grey margin nobody asked for and would
// report the wrong page size in Document Properties.
//
// The defaulting is a chain rather than a set of independent fields, and getting it wrong is
// silent: CropBox defaults to MediaBox; BleedBox and TrimBox and ArtBox each default to CropBox.
// A missing box is therefore never absent -- it is inherited -- which is why `cropBox()` and its
// siblings resolve rather than returning what the file happened to write down.
//
// **Every box is intersected with the MediaBox**, which the specification requires and which real
// files violate: a CropBox larger than its MediaBox is common enough that a viewer that trusted it
// would lay out pages of the wrong size against files that open correctly everywhere else.
//
// -------------------------------------------------------------------------------------------
// ROTATION IS A PROPERTY OF THE PAGE, NOT OF THE VIEW
// -------------------------------------------------------------------------------------------
// `/Rotate` is inheritable, a multiple of 90, and may be negative. `displaySize()` is the size
// AFTER rotation, which is what layout wants and what a caller that multiplied width by scale
// itself would get wrong for every landscape page in a portrait document.
//
// PURE, AND NO PDF SYNTAX. Nothing here knows what a dictionary is. `pdf/render/PdfiumDocument`
// builds these; `core/` holds the shape.

#ifndef RAWPAGE_CORE_DOC_PAGE_H
#define RAWPAGE_CORE_DOC_PAGE_H

#include "core/Geometry.h"

#include <QString>

#include <optional>

namespace rawpage::core {

class Page {
public:
    Page() = default;

    // `mediaBox` is the only required box; the other four default through the chain described
    // above. Rotation is normalized to 0/90/180/270 -- `/Rotate -90` is a legal file and means 270.
    Page(int index, PdfRect mediaBox);

    int index() const noexcept { return index_; }
    void setIndex(int index) noexcept { index_ = index; }

    PdfRect mediaBox() const noexcept { return mediaBox_; }

    // Resolved, never raw: an absent box is inherited rather than missing, and every box is
    // intersected with the MediaBox because real files write boxes that escape it.
    PdfRect cropBox() const noexcept;
    PdfRect bleedBox() const noexcept;
    PdfRect trimBox() const noexcept;
    PdfRect artBox() const noexcept;

    void setMediaBox(PdfRect box) noexcept { mediaBox_ = box; }
    void setCropBox(std::optional<PdfRect> box) noexcept { cropBox_ = box; }
    void setBleedBox(std::optional<PdfRect> box) noexcept { bleedBox_ = box; }
    void setTrimBox(std::optional<PdfRect> box) noexcept { trimBox_ = box; }
    void setArtBox(std::optional<PdfRect> box) noexcept { artBox_ = box; }

    // Whether the file actually carried the box, as opposed to inheriting it. Stage 3's writer
    // needs this: re-emitting an inherited CropBox as an explicit one changes the file for no
    // reason, and byte-identical incremental save is the property that would lose.
    bool hasExplicitCropBox() const noexcept { return cropBox_.has_value(); }

    PageRotation rotation() const noexcept { return rotation_; }
    void setRotation(int degrees) noexcept;

    // The CropBox's size AFTER rotation -- what layout and `TileGrid` want. A caller that scaled
    // `cropBox().size` itself gets every landscape page in a portrait document wrong.
    PdfSize displaySize() const noexcept;

    // The page LABEL (ISO 32000-1 12.4.2): "iv", "A-3", "12". Empty means the page has no label
    // and callers show the one-based index instead. It is NOT the index in disguise -- a document
    // with roman-numbered front matter has two page 1s, and the difference is what a Go To Page
    // box has to disambiguate.
    const QString& label() const noexcept { return label_; }
    void setLabel(QString label) { label_ = std::move(label); }

    friend bool operator==(const Page& a, const Page& b);
    friend bool operator!=(const Page& a, const Page& b) { return !(a == b); }

private:
    int index_ = 0;
    PdfRect mediaBox_;
    std::optional<PdfRect> cropBox_;
    std::optional<PdfRect> bleedBox_;
    std::optional<PdfRect> trimBox_;
    std::optional<PdfRect> artBox_;
    PageRotation rotation_ = PageRotation::None;
    QString label_;
};

// The intersection of two rectangles in PDF space, empty when they do not overlap. Exposed because
// the box-resolution rule above is "intersect with the MediaBox" and a second implementation of
// that is a second place to get an inverted rectangle.
PdfRect intersect(PdfRect a, PdfRect b) noexcept;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_PAGE_H
