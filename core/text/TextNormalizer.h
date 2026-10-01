// core/text/TextNormalizer -- ONE normalization pipeline for find, search, whole-word matching and the
// comparison. Stage 5 T4, decision D5-5 (`Stage5-TextSearch-Plan.md` §3).
//
// Three consumers with three normalizers would disagree in exactly the cases a person tests first:
// *café* found by find and reported as a change by compare. So there is one, and every consumer
// normalizes its QUERY through the same `normalizeQuery` it normalizes the page through.
//
// ---------------------------------------------------------------------------------------------
// THE STEPS, in order, per CLUSTER -- a base character and the combining marks after it
// ---------------------------------------------------------------------------------------------
//   1. NFKC: a `ﬁ` becomes `fi`, Arabic presentation forms their letters, full-width forms ASCII, and
//      a decomposed `e` + U+0301 the precomposed `é` -- which is why a cluster, not a character, is
//      the unit: the mark has to meet its base to compose.
//   2. Case folding (`QString::toCaseFolded`) when matching ignores case. It is SIMPLE folding: `ß`
//      stays `ß` and does not match `SS` (guide A-33 asserts the behaviour rather than leaving it to
//      be found).
//   3. Diacritics stripped when `search/ignoreDiacritics` asks: NFD, nonspacing marks dropped, NFC.
//
// THE OFFSET MAP. Every UTF-16 unit of the output records the FIRST and LAST model character of the
// cluster it came from, so a hit maps back to whole glyphs: a hit on the `f` of a `ﬁ` highlights the
// ligature, a hit on the `e` of a decomposed `é` highlights the base and its mark, and no highlight
// starts or ends inside a glyph (guide A-35). A space the normalizer SYNTHESIZED -- for a word gap, a
// line break, a block break -- maps to no character (-1).
//
// ---------------------------------------------------------------------------------------------
// ACROSS LINES
// ---------------------------------------------------------------------------------------------
// A line-final HYPHEN -- PDFium's hyphen flag, which T2 measured it sets on every line-end hyphen --
// followed by a LOWERCASE letter is JOINED: `exam-` / `ple` is `example`. Followed by anything else
// it stays and no space is added: `North-` / `West` is `North-West`, the compound the generator's
// label names (`corpus/labels/hyphenated.json`). *(D5-5 said "every other line break is a space";
// this is the one clarification T4 made to it, because a hard hyphen at a line end is the compound's
// own hyphen, and a space inside it is text that is not there.)* A hyphen in the MIDDLE of a line is
// never joined -- `well-known` is not `wellknown`. Every other line break, a word gap and a block
// break are one space; runs of whitespace are one space.
//
// **UNMAPPED characters produce nothing** but a word separator: they have no trustworthy Unicode
// (D5-2), so nothing may match them.
//
// **No ICU** (D5-5): QtCore's `normalized`, `toCaseFolded` and `QChar` categories, all present in the
// distro Qt 6.4 the portable presets build against (convention 24). The known cost is recorded in the
// plan: without ICU's dictionary break iterator, whole-word matching in CJK sees each ideograph as a
// word -- which is T6's to state at its case.

#ifndef RAWPAGE_CORE_TEXT_TEXTNORMALIZER_H
#define RAWPAGE_CORE_TEXT_TEXTNORMALIZER_H

#include "core/text/LayoutAnalyzer.h"
#include "core/text/PageTextModel.h"

#include <QString>

#include <utility>
#include <vector>

namespace rawpage::core::text {

struct NormalizeOptions {
    bool foldCase = true;           // `search/caseSensitive` off
    bool stripDiacritics = false;   // `search/ignoreDiacritics` on
};

struct NormalizedText {
    QString text;
    std::vector<int> first;   // per UTF-16 unit of `text`: the first model character, or -1
    std::vector<int> last;    // ... and the last, inclusive

    // The model characters a normalized range [begin, end) came from, as [first, last] -- or
    // {-1, -1} when the range holds only synthesized spaces.
    std::pair<int, int> sourceRange(qsizetype begin, qsizetype end) const;
};

class TextNormalizer {
public:
    explicit TextNormalizer(NormalizeOptions options = {}) : options_(options) {}

    const NormalizeOptions& options() const noexcept { return options_; }

    // A page, in the reading order `LayoutAnalyzer` decided.
    NormalizedText normalize(const PageTextModel& model, const std::vector<ReadingItem>& stream) const;

    // What a person typed: the same steps, whitespace collapsed and trimmed, no offsets.
    QString normalizeQuery(const QString& query) const;

    // A STRING THAT IS NOT A PAGE -- a comment, a field's value, a bookmark title (Stage 5 T8, D5-12) --
    // through the page's own steps, so the four other content classes match exactly as page text does.
    // The offsets are UTF-16 positions in `text`, not model indices. A line break is a page's line break
    // (D5-5's hyphen join included) and any other whitespace a word gap. U+FFFD is the model's own mark
    // for a glyph with no Unicode (`LayoutAnalyzer::readingText`), so it is UNMAPPED here and matches
    // nothing: an attached PDF's text arrives in that form.
    NormalizedText normalizeString(const QString& text) const;

private:
    QString normalizeCluster(const QString& cluster) const;

    NormalizeOptions options_;
};

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_TEXTNORMALIZER_H
