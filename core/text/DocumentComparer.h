// core/text/DocumentComparer -- what changed between two documents, in three layers, over inputs the engine
// thread produced. Stage 5 T12, decision D5-17 (`Stage5-TextSearch-Plan.md` §3), guide cases A-95 … A-102.
//
// ---------------------------------------------------------------------------------------------
// THE THREE LAYERS, ALL PURE `core/`
// ---------------------------------------------------------------------------------------------
//   1. **PAGES ARE ALIGNED FIRST.** Each page's BODY words (the header/footer band left out, below) are
//      hashed, and the two page sequences are diffed: an equal page is an ANCHOR. Between two anchors the
//      pages left over are paired, in order, by similarity -- the Dice coefficient of their body words as
//      multisets -- where it reaches `CompareOptions::pairSimilarity`; a page left over anywhere that
//      pairs with one left over anywhere else is MOVED; pages still left over in the same gap are paired
//      by position and diffed under the cap, and whatever has no partner is INSERTED or DELETED. Aligning
//      first is what keeps a 900-page comparison from being one 300,000-token diff, and what makes an
//      inserted page leave every later page matched to its counterpart (A-101).
//   2. **EACH ALIGNED PAIR'S WORDS ARE DIFFED** with Myers' O(ND) algorithm in its LINEAR-SPACE form (the
//      middle snake, as diff-match-patch's `bisect` writes it) over `TextNormalizer`'s words -- the same
//      normalizer find and search read (D5-5), case kept. A hunk is an insertion, a deletion or a
//      replacement; a deleted run and an inserted run of the SAME words, at least `minMoveTokens` long,
//      anywhere in the document, are one MOVE (A-100) -- a pure run tried at every position it could equally
//      have taken (its SLIDES: a paragraph moved after one that ends with the same words is an insertion a
//      diff may place those words early), and the matching slide applied to the script before any word is
//      paired for the layout layer. **THE BOUND**: a pair whose edit distance passes
//      `replaceShare` of its words, or `maxEdits`, stops there and is reported REPLACED rather than
//      diffed -- the forward search is abandoned at half that distance, so two unrelated documents cost
//      O(N × cap) and never O(N²) (A-97). At *text and layout*, matched words whose position moved more
//      than `layoutShiftPoints` are LAYOUT differences, one per run that moved together.
//   3. **APPEARANCE**, at that sensitivity only: the pair's rasters at 72 dpi, from `RasterProvider`, are
//      compared OUTSIDE every rectangle layer 2 already reported on that pair -- on both pages, both
//      sides' rectangles -- and outside every word an ignore-list dropped, and the differing pixels are
//      clustered into rectangles. So an image move is
//      found (text-only diffing misses it), and a text change is not reported a second time as pixels
//      (A-99).
//
// ---------------------------------------------------------------------------------------------
// THE IGNORE-LISTS (D5-17, Q7)
// ---------------------------------------------------------------------------------------------
//   * **The header/footer band**: a line whose whole BLOCK is in the top or bottom tenth of its page, as
//     drawn, and whose text -- digits normalized -- recurs on at least half the pages and at least three,
//     counted over BOTH documents' pages together. Together, because a revision that deleted a page leaves
//     a two-page document whose running header would otherwise be a header on one side and body text on
//     the other; the whole block, because a paragraph's first lines can reach into the tenth and look
//     alike from page to page. Its words are still compared unless `ignoreHeaderFooter`, and a difference
//     in it says which band (`Region`).
//   * **Page numbers**: a token of digits only that is in the band, or that equals its page's label --
//     dropped from both sides. A number in the body that is not the page's label is text, which is the
//     control that separates the band from the digits (A-98).
//   * **Whitespace**: words are compared without it; with `ignoreWhitespace` off, two matched words whose
//     SEPARATOR differs -- the number of spaces between them on one line, MEASURED as the gap over the space
//     glyph's width, because PDFium extracts a run of drawn spaces as one space character -- are a
//     WHITESPACE difference. A line break is a separator of its own and never one: re-wrapping is not a whitespace
//     change, and an insertion re-wraps the rest of its paragraph.
//
// ---------------------------------------------------------------------------------------------
// WHERE IT RUNS (A-102)
// ---------------------------------------------------------------------------------------------
// `DocumentComparer::compare` is the whole comparison as one pure, blocking call -- what `tst_compare`
// reaches with synthetic pages. `ComparisonRun` runs it on a THREAD OF ITS OWN: neither the UI thread nor
// the engine thread, whose part is the text and the rasters it was given. It reports progress -- a count
// that only grows, against a total fixed at the start -- and its outcome on the PUMPING thread from
// `deliverReady()`, as `SearchEngine` does, and `cancel()` is honoured between page pairs, so a cancelled
// run stops within one pair. The raster provider is called ON THAT THREAD and may block on the engine.
//
// **What this does not claim.** It is measured on `corpus/compare/`, pairs we generated in base-14
// Helvetica with one kind of change each (`corpus/make-compare.py`); a comparison right on them is right on
// them. The PDF outputs of `Features.md` §1.5 -- a summary page, an annotated report -- are Stage 8's (Q4).

#ifndef RAWPAGE_CORE_TEXT_DOCUMENTCOMPARER_H
#define RAWPAGE_CORE_TEXT_DOCUMENTCOMPARER_H

#include "core/Geometry.h"
#include "core/backend.h"
#include "core/text/TextSelection.h"

#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace rawpage::core::text {

enum class CompareSensitivity { TextOnly, TextAndLayout, Appearance };

struct CompareOptions {
    CompareSensitivity sensitivity = CompareSensitivity::TextOnly;
    bool ignoreHeaderFooter = false;
    bool ignorePageNumbers = false;
    bool ignoreWhitespace = false;

    // A deleted run and an inserted run of the same words, at least this many, are one move.
    int minMoveTokens = 8;
    // Matched words whose position moved further than this, in points, are a layout difference.
    double layoutShiftPoints = 1.0;
    // THE BOUND: a pair whose edit distance passes this share of its words, or `maxEdits`, is replaced.
    double replaceShare = 0.5;
    int maxEdits = 20000;
    // Pages left over between two anchors are paired when their body words are at least this similar.
    double pairSimilarity = 0.35;
    // ... and a page left over anywhere is MOVED to one left over anywhere else at least this similar.
    double moveSimilarity = 0.6;
    // Appearance: a pixel differs when a channel differs by more than this; a cluster of fewer differing
    // pixels than `minClusterPixels` is noise; rectangles reported by layer 2 are grown by `maskMargin`
    // pixels before they are masked.
    int pixelTolerance = 24;
    int minClusterPixels = 12;
    int maskMargin = 2;
};

enum class CompareSide { Base, Revised };

// One page of one side, as the engine thread produced it. A null `reading` is a page whose text could not
// be extracted -- compared as a page with no words.
struct ComparePage {
    std::shared_ptr<const PageReading> reading;
    PdfRect cropBox;
    PageRotation rotation = PageRotation::None;
    QString label;     // the logical page label; empty means the physical number
};

using CompareDocument = std::vector<ComparePage>;

// Called on the comparer's thread for each aligned pair the APPEARANCE layer compares: the page drawn at
// 72 dpi as the rasterizer draws it (its own /Rotate applied). Empty when it cannot be drawn -- that pair is
// then compared by text alone and counted in `ComparisonResult::rastersMissing`.
using RasterProvider = std::function<std::optional<RasterImage>(CompareSide side, int page)>;

struct PageMatch {
    enum class Kind { Identical, Modified, Moved, Replaced, Inserted, Deleted };
    Kind kind = Kind::Identical;
    int base = -1;        // -1 for an inserted page
    int revised = -1;     // -1 for a deleted page
    double similarity = 0.0;
};

enum class DifferenceKind { Insert, Delete, Replace, Move, Whitespace, Layout, Appearance,
                            PageInserted, PageDeleted, PageMoved, PageReplaced };

enum class Region { Body, Header, Footer };

struct Difference {
    DifferenceKind kind = DifferenceKind::Insert;
    Region region = Region::Body;
    int basePage = -1;
    int revisedPage = -1;
    QString before;                     // the words, space-separated
    QString after;
    int tokensBefore = 0;
    int tokensAfter = 0;
    std::vector<PdfRect> baseRects;     // in `basePage`'s page space, one per line touched
    std::vector<PdfRect> revisedRects;  // in `revisedPage`'s
};

struct ComparisonResult {
    std::vector<PageMatch> pages;          // in reading order of the revised document, deletions in place
    std::vector<Difference> differences;   // page-level first, then per pair in the same order
    bool cancelled = false;
    int pairsCompared = 0;                 // aligned pairs whose words were diffed (or found over the cap)
    int pairsReplaced = 0;
    int rastersMissing = 0;
    long long editsExplored = 0;           // the diff's forward-search work, for the bound's measurement

    int count(DifferenceKind kind) const;
};

class DocumentComparer {
public:
    // The progress handler is called with `done` growing to `total`; `total` is fixed for the call.
    using Progress = std::function<void(int done, int total)>;

    // THE WHOLE COMPARISON, blocking, on the calling thread. `cancel`, when given, is read between pages and
    // between pairs; a cancelled call returns what it had with `cancelled` set. `rasters` is consulted only
    // at `CompareSensitivity::Appearance`.
    static ComparisonResult compare(const CompareDocument& base, const CompareDocument& revised,
                                    const CompareOptions& options, const RasterProvider& rasters = {},
                                    const std::atomic<bool>* cancel = nullptr, const Progress& progress = {});

    // ---- The pieces the cases reach -------------------------------------------------------------------------
    // One edit of a word-sequence diff: `Equal` keeps `count` words of both, `Delete` drops `count` of `a`,
    // `Insert` adds `count` of `b`.
    struct Edit {
        enum class Op { Equal, Delete, Insert };
        Op op = Op::Equal;
        int count = 0;
    };
    // Myers' diff of two sequences in linear space. Empty when the edit distance passes `maxDistance` (the
    // cap); `explored`, when given, accumulates the forward search's diagonal steps.
    static std::optional<std::vector<Edit>> diff(const std::vector<int>& a, const std::vector<int>& b,
                                                 int maxDistance, long long* explored = nullptr);
};

// THE COMPARISON ON A THREAD OF ITS OWN (see the header). Constructed with its inputs, started once; its
// handlers run on the thread that calls `deliverReady()`.
class ComparisonRun {
public:
    using ProgressHandler = std::function<void(int done, int total)>;
    using FinishedHandler = std::function<void(const ComparisonResult& result)>;
    // Called FROM THE WORKER, carrying nothing; must post and return, as `SearchEngine`'s does.
    using WakeHandler = std::function<void()>;

    ComparisonRun(CompareDocument base, CompareDocument revised, CompareOptions options,
                  RasterProvider rasters = {});
    // Cancels and joins: the worker never outlives the run.
    ~ComparisonRun();

    ComparisonRun(const ComparisonRun&) = delete;
    ComparisonRun& operator=(const ComparisonRun&) = delete;

    void setProgressHandler(ProgressHandler handler) { onProgress_ = std::move(handler); }
    void setFinishedHandler(FinishedHandler handler) { onFinished_ = std::move(handler); }
    void setWakeHandler(WakeHandler handler);

    void start();
    void cancel() noexcept { cancelled_.store(true); }

    // THE PUMP: delivers what the worker posted since the last call. Returns how many items it delivered.
    int deliverReady();
    // Until the worker has posted its outcome (not until it is delivered). For tests and shutdown.
    bool waitForFinished(int milliseconds);
    bool isFinished() const;

    std::thread::id workerThread() const;

private:
    struct Posted {
        bool finished = false;
        int done = 0;
        int total = 0;
        std::shared_ptr<const ComparisonResult> result;
    };

    void post(Posted item);

    CompareDocument base_;
    CompareDocument revised_;
    CompareOptions options_;
    RasterProvider rasters_;
    ProgressHandler onProgress_;
    FinishedHandler onFinished_;

    std::atomic<bool> cancelled_{false};
    mutable std::mutex mutex_;
    std::condition_variable finishedCondition_;
    std::deque<Posted> posted_;
    bool workerFinished_ = false;
    WakeHandler wake_;
    std::thread::id workerId_;
    std::thread worker_;
};

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_DOCUMENTCOMPARER_H
