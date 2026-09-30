// core/text/LayoutAnalyzer -- a page's characters grouped into lines, lines into blocks, blocks into
// the order a person reads them. Stage 5 T4, decisions D5-6 and D5-7 (`Stage5-TextSearch-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// WHY THIS FILE MATTERS MORE THAN ITS STAGE
// ---------------------------------------------------------------------------------------------
// Selection, copy, find across lines, Reflow view and the comparison all read text in the order this
// file decides, and Write mode's *Convert to Writable* (Stage 13) will rest on the same grouping
// (`Architecture-FileMap.md` §3.5). So its accuracy is MEASURED rather than asserted, against the
// generator's labels, on targets fixed before the measurement (`Stage5-TextSearch-Testing-Guide.md`
// §3.2), and the figures are R1's evidence -- a baseline Stage 13 inherits, bounded to our own
// generated corpus (D5-7, D5-23).
//
// ---------------------------------------------------------------------------------------------
// GEOMETRY ONLY, AND WHAT THAT MEANS HERE
// ---------------------------------------------------------------------------------------------
// The input is a `PageTextModel` -- characters with boxes, and the page's thin ruling lines -- and
// nothing else. `const TagTree*` is part of the signature because a tagged document's structure tree
// is the better source when it exists; it is **null for every Stage 5 caller**, `core/doc/TagTree`
// being Stage 7's, so a tagged document is ordered by geometry like any other (D5-6, guide A-39).
// The model's own ORDER is not used either: PDFium returns right-to-left words in page order and a
// rotated page's characters in another order again (measured at T2), so position is the only
// evidence that means the same thing on every page.
//
// THE STEPS, each with the observation it rests on:
//   1. VERTICAL LINES first: three or more square glyphs stacked with no gap, sharing a centre line,
//      are a top-to-bottom line (Identity-V CJK). Latin letters are not square, so a column of `l`s
//      down a code listing is not mistaken for one.
//   2. BANDS: the rest, swept top to bottom, join a band when their boxes overlap its vertical extent
//      by half the smaller height -- so a superscript stays in its line, and two lines of ordinary
//      leading do not touch.
//   3. LINES: a band split wherever the horizontal gap exceeds 0.8 em -- a column gutter, a table's
//      cell spacing, a list marker's hang -- while a word space, which the content draws as a space
//      glyph, is no gap at all. A smaller gap between two non-space glyphs is a WORD GAP the reading
//      stream reports, standing in for the spaces PDFium generates and this file does not use.
//   4. DIRECTION: a line whose strong characters are mostly right-to-left is reordered from page
//      order to logical order by UAX #9's rules L1-L2 over resolved levels -- strong-RTL runs,
//      numbers inside them and the neutrals between, **no explicit embeddings** (D5-6's bound).
//   5. TABLES: ruled -- two or more horizontal rules sharing an extent, crossed by two or more vertical
//      ones -- and unruled -- three or more consecutive bands with the same number of cells at the
//      same left edges, whose cells are short (a median of three words or fewer, which is what tells a
//      table from two columns of prose that also align). A table is ONE block, cells row by row.
//   6. BLOCKS: a line joins the block above it when they overlap horizontally and its spacing
//      matches the block's established leading (within 1.4x -- a paragraph gap is 1.5x), or, for a
//      block of one line, when the sizes agree and the spacing is under 2.2 em. A list marker
//      (`1.`, `a)`, a bullet) starts a block and takes the line beside it.
//   7. ORDER: recursive XY-cut -- horizontal whitespace bands of at least 0.9 em top to bottom, then
//      vertical gutters of at least 0.9 em left to right (right to left for vertical writing).
//   8. COLUMNS: the most side-by-side groups in any band holding a quarter of the page's lines; a page
//      whose main text is vertical has as many columns as its largest vertical block has lines.
//
// ---------------------------------------------------------------------------------------------
// HOSTILE INPUT (D5-21, guide A-41)
// ---------------------------------------------------------------------------------------------
// Every coordinate here came from an untrusted PDF. A character whose geometry is not finite is
// UNPLACED -- reported, never compared, because a NaN in a sort comparator is undefined behaviour.
// Sizes that are zero or negative fall back to the box, then to 1. Every loop is bounded: more than
// `kMaxCharsLaidOut` placed characters and the page is laid out in the model's order, flagged
// `degraded`; block joining considers only the most recent blocks; the XY-cut recursion is capped.

#ifndef RAWPAGE_CORE_TEXT_LAYOUTANALYZER_H
#define RAWPAGE_CORE_TEXT_LAYOUTANALYZER_H

#include "core/Geometry.h"
#include "core/text/PageTextModel.h"

#include <QString>

#include <cstddef>
#include <vector>

namespace rawpage::core {
class TagTree;   // core/doc/TagTree.h -- Stage 7. Null for every Stage 5 caller (D5-6).
}

namespace rawpage::core::text {

enum class WritingDirection { LeftToRight, RightToLeft, TopToBottom };

struct TextLine {
    std::vector<int> chars;        // model indices, in READING order within the line
    std::vector<bool> gapBefore;   // parallel to `chars`: a word gap the content drew no space for
    PdfRect box;
    double baseline = 0.0;         // the median origin y (x for a vertical line)
    double fontSize = 0.0;         // the median size
    WritingDirection direction = WritingDirection::LeftToRight;
};

enum class BlockKind { Text, Table };

struct TextBlock {
    std::vector<int> lines;        // into `PageLayout::lines`, in reading order
    PdfRect box;
    BlockKind kind = BlockKind::Text;
};

struct TableGrid {
    int block = -1;                // the block holding its cells
    PdfRect box;
    int rows = 0;
    int columns = 0;
    bool ruled = false;
};

struct PageLayout {
    std::vector<TextLine> lines;
    std::vector<TextBlock> blocks;
    std::vector<int> order;        // block indices, in reading order
    std::vector<TableGrid> tables;
    int columns = 0;
    std::vector<int> unplaced;     // characters with non-finite geometry, never laid out
    bool degraded = false;         // too many characters: laid out in model order (see the header)
};

// The page as a reader meets it: characters in reading order, and the breaks between them. What
// `TextNormalizer`, selection and Reflow view consume.
struct ReadingItem {
    enum class Kind { Char, WordGap, LineBreak, BlockBreak };
    Kind kind = Kind::Char;
    int charIndex = -1;            // for `Char`
};

class LayoutAnalyzer {
public:
    static constexpr std::size_t kMaxCharsLaidOut = 200000;

    static PageLayout analyze(const PageTextModel& model, const TagTree* tags);

    static std::vector<ReadingItem> readingStream(const PageLayout& layout);

    // The reading stream as a string: a word gap is a space, a line break `\n`, a block break a blank
    // line. Unmapped characters appear as U+FFFD (the model's rule). For tests and diagnostics; a
    // consumer that matches text uses `TextNormalizer`.
    static QString readingText(const PageTextModel& model, const PageLayout& layout);
};

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_LAYOUTANALYZER_H
