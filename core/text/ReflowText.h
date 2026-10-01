// core/text/ReflowText -- a page as Reflow view shows it: `LayoutAnalyzer`'s blocks, in reading order, as
// paragraphs to be re-wrapped. Stage 5 T11, decision D5-16 (`Stage5-TextSearch-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// ONE PARAGRAPH PER BLOCK, AND EACH ONE KNOWS WHERE IT CAME FROM
// ---------------------------------------------------------------------------------------------
// Reflow gives up the page's geometry and keeps its reading: each text block becomes one paragraph, its
// lines joined -- re-wrapping is the whole point -- and each paragraph carries its PAGE and BLOCK, because
// D5-16's "the reader's place survives a toggle both ways" is a statement about blocks: the block at the
// top of the page view is the block at the top of the reflow, and back. `blockAtTop` is the page-view half
// of that; `ui/viewer/ReflowView` is the other.
//
// A TABLE block is one paragraph per LINE, its cells separated by spaces: D5-16 is text only, and a grid
// rebuilt in a text surface would be a second table implementation for a view whose job is reading.
//
// ---------------------------------------------------------------------------------------------
// THE SAME RULES AS EVERY OTHER READER OF THE TEXT
// ---------------------------------------------------------------------------------------------
//   * A line break inside a block is a SPACE -- except after a line-final hyphen the engine flagged, which
//     D5-5 joins before a lowercase letter (`exam-` / `ple`) and keeps, with no space, before anything else.
//     `TextNormalizer` and `TextSelection::copy` read that rule the same way; this is the third reading, and
//     `tst_reflowtext` asserts it against the same cases.
//   * **An unmapped glyph produces nothing** (D5-2) and separates the words on either side as a space would.
//     Reflow drawing U+FFFD would be showing a character that is not in the document.
//   * Fonts are approximated by SIZE, WEIGHT and ITALIC, never by face (D5-16): the page's typography is
//     exactly what reflow gives up, and Qt's fonts draw it.
//
// Pure and headless, like the rest of `core/text/`: no widget, no engine, no catalog key.

#ifndef RAWPAGE_CORE_TEXT_REFLOWTEXT_H
#define RAWPAGE_CORE_TEXT_REFLOWTEXT_H

#include "core/Geometry.h"
#include "core/text/TextSelection.h"

#include <QString>

#include <vector>

namespace rawpage::core::text {

struct ReflowRun {
    QString text;
    double size = 0.0;    // points, the model's size -- the view scales it
    bool bold = false;
    bool italic = false;
};

struct ReflowParagraph {
    int page = -1;
    int block = -1;           // into the page's `PageLayout::blocks`
    bool rightToLeft = false;
    bool tableRow = false;    // one line of a table block
    std::vector<ReflowRun> runs;

    QString text() const;
};

struct ReflowPage {
    std::vector<ReflowParagraph> paragraphs;
    int unmappedSkipped = 0;
};

// The page's blocks in reading order, as paragraphs. A page with no placed text has none.
ReflowPage reflowParagraphs(const PageReading& page);

// The paragraphs' text, a blank line between them -- what a case compares against the analyzer's order.
QString reflowText(const std::vector<ReflowParagraph>& paragraphs);

// WHICH WAY IS DOWN ON THE PAGE VIEW'S SCREEN, said in page space (S5 guide I-06): a page point's distance
// BELOW the viewport's top edge is `dx * x + dy * y + offset`, in any unit, negative above the edge. It is
// linear because a page's presentation is a rotation, a scale and a translation -- and it has to be said
// this way, because "down" is page-space -y only on an UPRIGHT page: shown at 90 degrees it is an x
// direction, and at 180 it is +y. The caller reads the coefficients off its own layout (`DocumentWindow`);
// nothing here knows which way any rotation turns.
struct DepthBelowEdge {
    double dx = 0.0;
    double dy = -1.0;
    double offset = 0.0;

    double of(PdfPoint p) const noexcept { return dx * p.x + dy * p.y + offset; }
    // An upright page whose viewport top edge is at page-space height `topY`.
    static DepthBelowEdge upright(double topY) noexcept { return DepthBelowEdge{0.0, -1.0, topY}; }
};

// THE PLACE ON THE PAGE VIEW'S SIDE: the first block in reading order that is not wholly ABOVE the
// viewport's top edge -- some corner of its box at or below it. A page whose top is on screen answers its
// first block; a page scrolled past every block answers -1 -- the place is on a later page.
int blockAtTop(const PageReading& page, const DepthBelowEdge& depth);
// The same over an upright page whose edge is at page-space height `topY` (PDF points, y up).
int blockAtTop(const PageReading& page, double topY);

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_REFLOWTEXT_H
