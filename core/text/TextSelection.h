// core/text/TextSelection -- what a person selected, in the document's READING order, and what a copy of
// it is. Stage 5 T5, decisions D5-8 and D5-9 (`Stage5-TextSearch-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// A RANGE OVER THE DOCUMENT'S READING ORDER, NOT OVER THE PAGE'S PIXELS
// ---------------------------------------------------------------------------------------------
// A drag from the foot of one column to the head of the next must take the end of the first and the
// start of the second -- not a band across both, which is what a selection by x and y would take
// (guide A-45). So a FLOW selection is two CARETS, each a (page, position between two characters of
// that page's reading sequence), and everything between them in reading order is selected -- across
// page boundaries too, because a paragraph that runs over a page break is the second thing anyone
// copies (D5-8). The reading sequence is `LayoutAnalyzer`'s (D5-6), built once per page by
// `PageReading` below.
//
// A COLUMN selection (`Alt`+drag, D5-8, `Features.md` §8's Viewer table) is the other shape: a
// rectangle on ONE page, selecting the spans of every line it crosses -- how a table's column, or one
// column of a two-column page, is taken on its own. It is copied as a TABLE (D5-9).
//
// ---------------------------------------------------------------------------------------------
// THE COPY: THREE FLAVOURS, AND ONE RULE THAT IS AN S1 WHEN BROKEN
// ---------------------------------------------------------------------------------------------
// `copy()` produces plain text and HTML together; the UI puts both in one `QMimeData` and the paste
// target chooses (D5-9). Plain text keeps the line breaks a person sees and a blank line between
// blocks. HTML keeps paragraphs -- lines joined within a block, a line-final hyphen before a lowercase
// letter joined as D5-5 joins it -- and each run's family, size, bold and italic. A column selection,
// and a table block met by a flow selection, is tab-separated plain text and an HTML `<table>`, which is
// what Excel takes.
//
// **AN UNMAPPED GLYPH PRODUCES NOTHING** (D5-2, guide A-21). Its codepoint is U+FFFD by the model's own
// rule, and copying it would put a character that is not in the document into somebody else's work --
// the S1 this stage is most likely to ship quietly. It is skipped, counted in `unmappedSkipped`, and it
// separates the words on either side as a space would, which is what `TextNormalizer` does with it too.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS FILE DOES NOT DO
// ---------------------------------------------------------------------------------------------
// It names no widget, no clipboard and no engine: it is pure and headless (`tst_selection`), and the
// fuzz target reaches it over hostile geometry (D5-21). Painting a selection is
// `ui/viewer/SelectionOverlay`'s, and a page's text reaches `DocumentText` from the engine thread
// through that class (D5-3) -- this file only reads what it is given, and a page it is not given is
// REPORTED (`CopiedText::missingPages`), never guessed.

#ifndef RAWPAGE_CORE_TEXT_TEXTSELECTION_H
#define RAWPAGE_CORE_TEXT_TEXTSELECTION_H

#include "core/Geometry.h"
#include "core/text/LayoutAnalyzer.h"
#include "core/text/PageTextModel.h"

#include <QString>

#include <climits>
#include <memory>
#include <vector>

namespace rawpage::core::text {

// Reading positions [begin, end) on one page.
struct Span {
    int begin = 0;
    int end = 0;

    bool isEmpty() const noexcept { return end <= begin; }
    friend bool operator==(const Span& a, const Span& b) noexcept { return a.begin == b.begin && a.end == b.end; }
};

// ONE PAGE IN READING ORDER. Built once per page from its model -- the analyzer runs here -- and
// immutable afterwards, so the two panes of a split and a copy can share one.
//
// A POSITION is an index into the page's reading sequence: the characters `LayoutAnalyzer` placed, in
// the order a person reads them. Characters with non-finite geometry are not placed and have no
// position (the analyzer's `unplaced`); the engine's GENERATED whitespace has none either, because the
// analyzer decides breaks itself -- `breakBefore()` is where a word gap, a line break or a block break
// is, standing between two positions rather than occupying one.
class PageReading {
public:
    static std::shared_ptr<const PageReading> build(std::shared_ptr<const PageTextModel> model);

    const PageTextModel& model() const noexcept { return *model_; }
    const PageLayout& layout() const noexcept { return layout_; }
    int pageIndex() const noexcept { return model_->pageIndex(); }

    int size() const noexcept { return static_cast<int>(chars_.size()); }
    bool isEmpty() const noexcept { return chars_.empty(); }

    int modelIndex(int position) const { return chars_[static_cast<std::size_t>(position)]; }
    const TextChar& charAt(int position) const { return model_->chars()[static_cast<std::size_t>(modelIndex(position))]; }

    // What stands BEFORE `position`: `Char` for nothing (the previous position is the same word),
    // `WordGap`, `LineBreak` or `BlockBreak`. Position 0 reports `BlockBreak` -- a page starts a block.
    ReadingItem::Kind breakBefore(int position) const { return before_[static_cast<std::size_t>(position)]; }

    int lineOf(int position) const { return line_[static_cast<std::size_t>(position)]; }
    int blockOf(int position) const { return block_[static_cast<std::size_t>(position)]; }
    // A line's and a block's positions are CONTIGUOUS in reading order -- the stream reads a block's
    // lines one after another and a line's characters one after another -- so each is one span.
    Span lineSpan(int line) const { return lineSpans_[static_cast<std::size_t>(line)]; }
    Span blockSpan(int block) const { return blockSpans_[static_cast<std::size_t>(block)]; }

    // ---- Hit-testing, in PAGE space (PDF points, unrotated user space, as the model's boxes are) ----
    // The CARET nearest `point`: 0..size(). The line is the one whose box is nearest the point; inside
    // it, the character nearest along the line's direction, and the caret on whichever side of that
    // character's centre the point falls -- read in the character's own direction, so a caret in a
    // right-to-left word lands before a letter when the point is on the letter's RIGHT half.
    int caretAt(PdfPoint point) const;
    // The character nearest `point`, or -1 on a page with no placed characters.
    int positionAt(PdfPoint point) const;

    // The WORD, LINE or BLOCK holding `position`. A word is a run of letters, numbers and marks on one
    // line with no break inside it; a position that is none of those is a word of one character.
    Span wordAt(int position) const;
    Span lineAt(int position) const { return lineSpan(lineOf(position)); }
    Span blockAt(int position) const { return blockSpan(blockOf(position)); }

private:
    PageReading() = default;

    std::shared_ptr<const PageTextModel> model_;
    PageLayout layout_;
    std::vector<int> chars_;
    std::vector<ReadingItem::Kind> before_;
    std::vector<int> line_;
    std::vector<int> block_;
    std::vector<Span> lineSpans_;
    std::vector<Span> blockSpans_;
};

// A document's pages as selection reads them: each page's reading, or nothing yet. Filled by whoever
// asked the engine (`ui/viewer/SelectionOverlay`, D5-3); read by `TextSelection`.
class DocumentText {
public:
    explicit DocumentText(int pageCount = 0);

    int pageCount() const noexcept { return static_cast<int>(pages_.size()); }
    void reset(int pageCount);

    bool has(int page) const noexcept;
    const PageReading* page(int page) const noexcept;
    // Keyed by the reading's own page index; one outside the document is refused.
    bool insert(std::shared_ptr<const PageReading> reading);

    // Pages in [first, last] with no reading yet, in order.
    std::vector<int> missing(int first, int last) const;

private:
    std::vector<std::shared_ptr<const PageReading>> pages_;
};

// A caret: a page and a position between two of its characters. `kEndOfPage` stands for "after the
// last character", whatever the page turns out to hold -- what a whole-document selection ends at on a
// page not extracted yet (D5-9's lazy `Ctrl+A`).
struct TextPosition {
    static constexpr int kEndOfPage = INT_MAX;

    int page = -1;
    int caret = 0;

    bool isValid() const noexcept { return page >= 0; }
    friend bool operator<(const TextPosition& a, const TextPosition& b) noexcept
    {
        return a.page != b.page ? a.page < b.page : a.caret < b.caret;
    }
    friend bool operator==(const TextPosition& a, const TextPosition& b) noexcept
    {
        return a.page == b.page && a.caret == b.caret;
    }
};

// How far a press reaches: the caret itself, or the word, line or block under it -- one, two, three and
// four clicks (D5-8). A drag that started at a granularity extends by the same unit.
enum class Granularity { Character, Word, Line, Block };

// A unit under a point: two carets, equal for `Character`.
struct TextUnit {
    TextPosition begin;
    TextPosition end;
};

TextUnit unitAt(const PageReading& page, PdfPoint point, Granularity granularity);

struct CopiedText {
    QString plain;
    QString html;                  // a whole HTML document, the fragment between the standard markers
    int characters = 0;            // mapped characters copied
    int unmappedSkipped = 0;       // glyphs with no trustworthy Unicode, left out (D5-2)
    bool isTable = false;          // a column selection: the plain text is tab-separated
    std::vector<int> missingPages; // pages the selection covers that `DocumentText` did not hold

    // Nothing a paste could use. A selection over unmapped glyphs alone is this, with
    // `unmappedSkipped` saying why.
    bool isEmpty() const noexcept { return characters == 0; }
};

class TextSelection {
public:
    enum class Kind { None, Flow, Column };

    TextSelection() = default;

    // Two carets in either order. Equal carets are an EMPTY flow -- a click, which selects nothing.
    static TextSelection flow(TextPosition anchor, TextPosition focus);
    // The union of two units, which is what a drag that started on a word extends to.
    static TextSelection spanning(const TextUnit& anchor, const TextUnit& focus);
    static TextSelection column(int page, PdfRect rect);
    // Every page, first caret to last: `Ctrl+A` (D5-9 -- the whole DOCUMENT, Q6).
    static TextSelection wholeDocument(int pageCount);

    Kind kind() const noexcept { return kind_; }
    bool isEmpty() const noexcept;

    TextPosition start() const noexcept { return start_; }   // flow: the earlier caret
    TextPosition end() const noexcept { return end_; }       // ... and the later
    int columnPage() const noexcept { return columnPage_; }
    PdfRect columnRect() const noexcept { return columnRect_; }

    // The pages the selection touches, inclusive; {-1, -1} when empty.
    int firstPage() const noexcept;
    int lastPage() const noexcept;
    bool covers(int page) const noexcept { return !isEmpty() && page >= firstPage() && page <= lastPage(); }

    // What is selected on one page: one span for a flow, one per line crossed for a column.
    std::vector<Span> spansOn(const PageReading& page) const;

    // Where to paint it, in PAGE space: one rectangle per line per contiguous run -- the run's extent
    // along the line, the line's extent across it, so a highlight is the height of its line rather
    // than of each glyph's ascender.
    std::vector<PdfRect> highlightOn(const PageReading& page) const;

    std::vector<int> missingPages(const DocumentText& text) const;

    CopiedText copy(const DocumentText& text) const;

private:
    Kind kind_ = Kind::None;
    TextPosition start_;
    TextPosition end_;
    int columnPage_ = -1;
    PdfRect columnRect_;
};

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_TEXTSELECTION_H
