// core/doc/Destinations -- where a link, a bookmark or a named destination POINTS, and the
// document's table of named ones. Stage 4 T4; decision D4-3 (`Stage4-ViewingSurface-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// READ-ONLY, AND THE READER IS THE ENGINE'S (D4-3)
// ---------------------------------------------------------------------------------------------
// *"We read through PDFium and write through our own code"* (`CLAUDE.md`, D3). Both forms a PDF may
// carry -- the PDF 1.2 `/Names /Dests` name TREE and the PDF 1.1 flat `/Dests` DICTIONARY -- are
// resolved by `FPDFDest_*` behind the facade, including the inherited and indirect cases, and a
// second first-party walk would be a second reader to keep in agreement. The EDITING half is Stage
// 7's, and it is where a first-party writer's view of both tables arrives.
//
// This file is the vocabulary only: no `FPDF_` name appears here, and `layer-lint` would say so.
//
// ---------------------------------------------------------------------------------------------
// AN UNRESOLVED DESTINATION IS A VALUE, NOT AN ERROR
// ---------------------------------------------------------------------------------------------
// A named destination whose page is gone is what deleting a page leaves behind, and
// `corpus/ordinary/dests-named.pdf` carries one on purpose (`ghost-page`, pointing at an object
// that does not exist). It must be **reported** rather than silently dropped -- a pane that omits
// it tells the user their document has four destinations when it has five, and the one that is
// broken is the one they were looking for. So `pageIndex` is `-1` and `isResolved()` is false, and
// the entry stays in the table.

#ifndef RAWPAGE_CORE_DOC_DESTINATIONS_H
#define RAWPAGE_CORE_DOC_DESTINATIONS_H

#include <QString>

#include <cstddef>
#include <vector>

namespace rawpage::core {

// Where to go, and how to frame it once there. The fit modes are ISO 32000-1 §12.3.2.2's, named
// rather than numbered, because a `/FitH` with no top is a different thing from a `/FitH` with one
// and a bare integer could not say so.
struct Destination {
    enum class Fit {
        Unknown,   // the destination named no view, or the engine could not say
        XYZ,       // a point and a zoom, any of which may be null -- see the `has*` flags
        Fit,       // fit the whole page
        FitH,      // fit the width, at a given top
        FitV,      // fit the height, at a given left
        FitR,      // fit a rectangle
        FitB,      // fit the bounding box of the page's content
        FitBH,
        FitBV,
    };

    int  pageIndex = -1;         // -1 means UNRESOLVED, which is a value (see the header)
    Fit  fit = Fit::Unknown;

    // `/XYZ null null null` is legal and common -- it means *keep the current value* for each null.
    // A reader that treated a missing coordinate as 0 would scroll every such link to the page's
    // bottom-left corner, so the three are carried separately from their values.
    bool   hasX = false;
    bool   hasY = false;
    bool   hasZoom = false;
    double x = 0.0;
    double y = 0.0;
    double zoom = 0.0;

    bool isResolved() const noexcept { return pageIndex >= 0; }

    friend bool operator==(const Destination& a, const Destination& b)
    {
        return a.pageIndex == b.pageIndex && a.fit == b.fit && a.hasX == b.hasX && a.hasY == b.hasY
               && a.hasZoom == b.hasZoom && a.x == b.x && a.y == b.y && a.zoom == b.zoom;
    }
};

// ASCII, stable, and the token any file or log holds -- the same arrangement `layoutModeName` and
// `fitModeName` use, so a surface cannot display a value the enum lacks.
const char* destinationFitName(Destination::Fit fit) noexcept;

struct NamedDestination {
    QString     name;
    Destination destination;
};

// The document's named destinations, in the order the engine enumerates them. **Both PDF forms land
// here as one list**: a caller asking for `chapter-one` does not care which table it came from, and
// keeping two lists would make *"which one wins"* a question every caller had to answer.
class Destinations {
public:
    void add(NamedDestination entry) { entries_.push_back(std::move(entry)); }

    const std::vector<NamedDestination>& all() const noexcept { return entries_; }
    bool isEmpty() const noexcept { return entries_.empty(); }
    std::size_t size() const noexcept { return entries_.size(); }

    // Null when no destination carries that name. Exact match: PDF destination names are byte
    // strings and are case-SENSITIVE, so folding them here would make two distinct destinations
    // one.
    const NamedDestination* find(const QString& name) const;

    // How many entries name a page that is not there. Zero on every well-formed document, which is
    // why a pane can show it without shouting.
    std::size_t unresolvedCount() const;

private:
    std::vector<NamedDestination> entries_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_DESTINATIONS_H
