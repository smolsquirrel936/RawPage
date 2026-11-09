// core/doc/OutlineGenerator -- bookmarks made from what the pages say. Stage 7 T11, decision D7-10
// (`Stage7-Assembly-Plan.md` §3; `Features.md` §5.4's three generators).
//
// -------------------------------------------------------------------------------------------
// THREE WAYS TO FIND A HEADING, ONE WAY TO MAKE A TREE
// -------------------------------------------------------------------------------------------
//   * FROM HEADINGS: the structure tree's `H`, `H1` ... `H6` when the document is tagged -- read by the backend
//     (`IDocumentBackend::structureHeadings`), each with its level, its text and where it stands -- and otherwise
//     the LAYOUT's: a line set larger than the body text (`LayoutAnalyzer`'s lines, each with its median size), its
//     level the rank of its size among the heading sizes found, largest first.
//   * BY FONT SIZE AND STYLE: a line at least `minimumSize` points (or larger than the body, when 0), bold when asked,
//     its level by size likewise.
//   * BY A TEXT PATTERN: every line whose text matches a regular expression, at one level.
// Each heading becomes an item whose destination is its line -- the page, the line's left and top, the zoom kept --
// and each sits under the nearest heading before it of a smaller level (`tree`). Headless: QtCore and
// `PageTextModel`, no engine; the pages' text is read by whoever calls (the window, off the UI thread; a case, from
// the engine).
//
// THE BODY SIZE is the size MOST characters are set in, over every page read -- not the median line, which a page of
// short headings would pull up. A line at or below it is never a heading in the layout and size modes; a footer or a
// caption set smaller is never one either.

#ifndef RAWPAGE_CORE_DOC_OUTLINEGENERATOR_H
#define RAWPAGE_CORE_DOC_OUTLINEGENERATOR_H

#include "core/Result.h"
#include "core/doc/Outline.h"
#include "core/text/PageTextModel.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace rawpage::core {

struct OutlineGeneratorOptions {
    enum class Mode : std::uint8_t { Headings, Fonts, Pattern };
    Mode mode = Mode::Headings;
    double minimumSize = 0.0;   // Fonts: a heading is at least this size (points); 0: larger than the body
    bool boldOnly = false;      // Fonts: and bold
    QString pattern;            // Pattern: a regular expression the line's text matches
    int maxLevels = 6;
};

// One line of a page as the generators see it -- or one heading the structure tree names (`level` set).
struct HeadingLine {
    int page = -1;
    int level = 0;          // 1 ... 6 once a heading; 0 for a line not chosen yet
    QString text;
    double size = 0.0;
    bool bold = false;
    double left = 0.0;      // PDF points, the line's box
    double top = 0.0;
};

class OutlineGenerator {
public:
    // Page `page`'s lines, in reading order, from its text.
    static std::vector<HeadingLine> linesOf(int page, const text::PageTextModel& model);
    // The size most characters are set in, over `lines` (each weighted by its length).
    static double bodySize(const std::vector<HeadingLine>& lines);
    // Which lines are headings, each given its level -- refused by `error.outline.badPattern` for a pattern that is
    // no regular expression, and `error.outline.noHeadings` when none is found.
    static Result<std::vector<HeadingLine>> select(const std::vector<HeadingLine>& lines, const OutlineGeneratorOptions& options);
    // The tree: each heading under the nearest one before it of a smaller level, every item new -- ids `key`-0,
    // `key`-1 ... in order -- going to its line.
    static std::vector<OutlineItem> tree(const std::vector<HeadingLine>& headings, const QString& key);
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_OUTLINEGENERATOR_H
