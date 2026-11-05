// core/doc/SplitPlan -- which pages each file of a split holds. Stage 7 T7 (`Stage7-Assembly-Plan.md` §6, D7-30;
// `Features.md` §5's six splits).
//
// PURE: each rule is a function of FACTS about the document -- its page count, its outline, which pages are blank,
// which pages a pattern matched, what a set of pages weighs as a file -- and gathering those facts is someone else's
// (`core/doc/SplitRunner`: the engine for blank pages, the search engine for a pattern, the assembler for a size). So
// every rule is graded here against hand values and against the corpus labels with no engine and no file.
//
// -------------------------------------------------------------------------------------------
// THE RULES
// -------------------------------------------------------------------------------------------
//   * EVERY N PAGES: runs of `n`, the last one shorter when the count does not divide.
//   * AT BOOKMARKS: every page an outline item at depth 1 ... `level` lands on STARTS a file (level 1 is "every
//     top-level bookmark"). A part of the outline below `level` is not read -- a section inside a chapter does not
//     split it -- and a deeper level splits at its own items AND at every shallower one, which a chapter boundary
//     is. An item that resolves to no page (`Destination::pageIndex` -1) splits nothing. The pages before the first
//     start are a file of their own, as the front matter they are.
//   * AT PAGES MATCHING A PATTERN: every matched page starts a file -- `atStarts`, the bookmark rule's second half.
//   * AT BLANK PAGES: a blank page is a SEPARATOR, and is not written (D7-30): each maximal run of pages that are
//     not blank is a file, and a run of several blank pages separates once. "Blank" is the caller's fact -- the
//     product's is the content box (Stage 4, D4-14): a page with no object at all, so a page drawn white on white
//     is not blank to the split, though it is to the eye (guide A-49).
//   * BY SIZE: the fewest files, in order, each at most `maxBytes` as the ASSEMBLER writes it -- weighed, not
//     estimated, because pages sharing a font weigh less together than apart. Greedy from the first page: each file
//     takes as many pages as fit, found by doubling then halving the run, which costs about 2 log2(run) weighings
//     per file. A single page heavier than the bound is a file of its own, and flagged `overBound` -- written alone
//     and reported, rather than refused or dropped (guide A-49).
//
// A rule that finds nothing to split at -- no bookmark at the level, no matched page after the first, no blank
// page -- is REFUSED by `error.split.noBoundary`: one file holding the whole document is a copy, not a split, and
// writing it would hide that the document had nothing the person was splitting at. A document whose every page is
// blank is `error.split.allBlank`.
//
// Pure: QtCore and the standard library -- `rawpage_tests`.

#ifndef RAWPAGE_CORE_DOC_SPLITPLAN_H
#define RAWPAGE_CORE_DOC_SPLITPLAN_H

#include "core/Result.h"
#include "core/doc/Outline.h"

#include <QString>

#include <functional>
#include <vector>

namespace rawpage::core {

enum class SplitMethod {
    EveryN,
    TopLevelBookmarks,
    BookmarkLevel,
    MaxSize,
    BlankPages,
    TextPattern,
};

struct SplitPart {
    std::vector<int> pages;   // zero-based, strictly ascending
    bool overBound = false;   // BY SIZE: one page heavier than the bound, written alone
    bool operator==(const SplitPart&) const = default;
};

namespace split {

Result<std::vector<SplitPart>> everyN(int pageCount, int n);

// Every page an item at depth 1 ... `level` names, ascending and unique -- what `atBookmarks` splits at.
std::vector<int> bookmarkStarts(const Outline& outline, int level, int pageCount);
Result<std::vector<SplitPart>> atBookmarks(int pageCount, const Outline& outline, int level);

// Each page of `starts` begins a file (a start outside the document, or at page 0, splits nothing).
Result<std::vector<SplitPart>> atStarts(int pageCount, const std::vector<int>& starts);

Result<std::vector<SplitPart>> atBlankPages(const std::vector<bool>& blank);

// What `pages` weighs as the file the assembler would write, or why it could not be weighed.
using Weigher = std::function<Result<qint64>(const std::vector<int>& pages)>;
Result<std::vector<SplitPart>> bySize(int pageCount, qint64 maxBytes, const Weigher& weigh);

// `count` file names from `pattern` -- `%1` the stem, `%2` the number, zero-padded to the width of `last` so the
// files sort in order -- each made safe to be a file name (`core::safeFileName`).
std::vector<QString> numberedNames(const QString& pattern, const QString& stem, const std::vector<int>& numbers, int last);

} // namespace split

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_SPLITPLAN_H
