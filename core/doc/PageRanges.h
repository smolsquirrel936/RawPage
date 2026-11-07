// core/doc/PageRanges -- a page set as a person types it, and back. Stage 7 T7 (`Stage7-Assembly-Plan.md` §6): the
// first range a dialog takes (extract), and the one T8 and T9's page dialogs take after it.
//
// THE SYNTAX, and nothing more: page NUMBERS counted from 1, as the status bar shows them -- never the labels, which
// a document may repeat -- separated by commas, each a number (`5`) or an inclusive range (`3-7`; an en dash too),
// either end of a range left open meaning the first or the last page (`-3`, `8-`). Whitespace anywhere. The result
// is the set the text names, zero-based, strictly ascending, each page once however many times it was named.
//
// REFUSED, by `error.pages.badRange` with the offending piece as context: an empty text, a piece that is not a number
// or a range, a page 0 or past the last, and a range written backwards (`7-3`) -- a reversed range is more often a
// typing slip than a wish for the same pages, and silently reading it forwards would hide the slip.
//
// Pure: QtCore and the standard library -- `rawpage_tests`.

#ifndef RAWPAGE_CORE_DOC_PAGERANGES_H
#define RAWPAGE_CORE_DOC_PAGERANGES_H

#include "core/Result.h"

#include <QString>

#include <vector>

namespace rawpage::core {

class PageTree;

Result<std::vector<int>> parsePageRanges(const QString& text, int pageCount);

// The shortest text `parsePageRanges` reads back as `pages` (zero-based, ascending): runs as `a-b`, single pages
// alone -- `{0, 1, 2, 4}` is `1-3, 5`.
QString formatPageRanges(const std::vector<int>& pages);

// ROTATION'S FILTERS -- Stage 7 T8 (`Features.md` §5.1: *restrict a rotation to a page range, to odd or even pages, or to
// pages of one orientation*; guide A-59). ODD and EVEN are page NUMBERS as a person counts them -- page 1 is odd, so
// index 0 -- never labels. ORIENTATION is the page AS SHOWN, its `/Rotate` and any unsaved turn included
// (`PageTree::displaySize`): landscape is wider than tall, portrait everything else, so a square page is portrait. The
// result keeps `pages`' order and drops what does not pass; a page outside `tree` is dropped.
enum class PageParity { Any, Odd, Even };
enum class PageOrientation { Any, Portrait, Landscape };
std::vector<int> filterPages(const std::vector<int>& pages, PageParity parity, PageOrientation orientation, const PageTree& tree);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_PAGERANGES_H
