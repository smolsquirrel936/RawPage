#include "core/text/TextSelection.h"

#include <QChar>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace rawpage::core::text {
namespace {

bool isWordChar(char32_t cp)
{
    // An unmapped glyph is a word character here so that a double-click on a run of them selects the
    // run -- and the copy then says it cannot be extracted, rather than selecting one glyph at a time.
    if (cp == kReplacementCharacter) return true;
    return QChar::isLetterOrNumber(cp) || QChar::isMark(cp) || cp == U'_';
}

PdfRect unite(const PdfRect& a, const PdfRect& b)
{
    const double x0 = std::min(a.left(), b.left());
    const double y0 = std::min(a.bottom(), b.bottom());
    const double x1 = std::max(a.right(), b.right());
    const double y1 = std::max(a.top(), b.top());
    return PdfRect{PdfPoint{x0, y0}, PdfSize{x1 - x0, y1 - y0}};
}

// The box normalized, so a producer's negative width cannot turn an interval inside out.
struct Interval {
    double lo = 0.0;
    double hi = 0.0;
};

Interval xOf(const PdfRect& r) { return {std::min(r.left(), r.right()), std::max(r.left(), r.right())}; }
Interval yOf(const PdfRect& r) { return {std::min(r.bottom(), r.top()), std::max(r.bottom(), r.top())}; }

double distanceTo(double v, Interval i)
{
    if (v < i.lo) return i.lo - v;
    if (v > i.hi) return v - i.hi;
    return 0.0;
}

double distanceTo(PdfPoint p, const PdfRect& r)
{
    const double dx = distanceTo(p.x, xOf(r));
    const double dy = distanceTo(p.y, yOf(r));
    return std::hypot(dx, dy);
}

bool isVertical(const TextLine& line) { return line.direction == WritingDirection::TopToBottom; }

// How strong a break is, so the strongest one between two copied characters is the one written.
int strength(ReadingItem::Kind kind)
{
    switch (kind) {
    case ReadingItem::Kind::Char: return 0;
    case ReadingItem::Kind::WordGap: return 1;
    case ReadingItem::Kind::LineBreak: return 2;
    case ReadingItem::Kind::BlockBreak: return 3;
    }
    return 0;
}

QString scalar(char32_t cp)
{
    return QString::fromUcs4(&cp, 1);
}

// ---- The HTML side ---------------------------------------------------------------------------------

struct RunStyle {
    QString family;
    double size = 0.0;
    bool bold = false;
    bool italic = false;
    bool serif = false;

    friend bool operator==(const RunStyle& a, const RunStyle& b)
    {
        return a.family == b.family && a.size == b.size && a.bold == b.bold && a.italic == b.italic
               && a.serif == b.serif;
    }
};

// The family a paste target can look up: the engine's name without its subset tag (`ABCDEF+`) and
// without a style suffix the size and weight already say (`Helvetica-Bold` is `Helvetica`, bold).
QString familyOf(const QString& engineName)
{
    QString name = engineName;
    const qsizetype plus = name.indexOf(QLatin1Char('+'));
    if (plus == 6) name = name.mid(plus + 1);
    for (const QChar separator : {QLatin1Char('-'), QLatin1Char(',')}) {
        const qsizetype at = name.indexOf(separator);
        if (at > 0) {
            const QString style = name.mid(at + 1).toLower();
            if (style.contains(QLatin1String("bold")) || style.contains(QLatin1String("italic"))
                || style.contains(QLatin1String("oblique")) || style == QLatin1String("regular")
                || style == QLatin1String("roman") || style.contains(QLatin1String("light"))
                || style.contains(QLatin1String("medium")))
                name = name.left(at);
        }
    }
    return name;
}

RunStyle styleOf(const PageTextModel& model, const TextChar& c)
{
    RunStyle style;
    style.size = std::isfinite(c.fontSize) && c.fontSize > 0.0 ? std::round(c.fontSize * 2.0) / 2.0 : 0.0;
    if (const FontIdentity* font = model.fontOf(c)) {
        style.family = familyOf(font->name);
        style.bold = font->isBold();
        style.italic = font->isItalic();
        style.serif = (font->flags & 2) != 0;
    }
    return style;
}

QString cssOf(const RunStyle& style)
{
    QString css;
    if (!style.family.isEmpty()) {
        QString family = style.family;
        family.remove(QLatin1Char('\''));
        family.remove(QLatin1Char('"'));
        css += QStringLiteral("font-family:'%1',%2;")
                   .arg(family.toHtmlEscaped(),
                        style.serif ? QStringLiteral("serif") : QStringLiteral("sans-serif"));
    }
    if (style.size > 0.0) css += QStringLiteral("font-size:%1pt;").arg(style.size);
    if (style.bold) css += QStringLiteral("font-weight:bold;");
    if (style.italic) css += QStringLiteral("font-style:italic;");
    return css;
}

// One paragraph or one table cell: styled runs, written when the container closes.
struct RunWriter {
    struct Run {
        RunStyle style;
        QString text;
    };
    std::vector<Run> runs;

    void append(const RunStyle& style, const QString& text)
    {
        if (runs.empty() || !(runs.back().style == style)) runs.push_back(Run{style, QString()});
        runs.back().text += text;
    }
    // A separator takes the style of what it follows, so it never opens a run of its own.
    void appendSeparator(const QString& text)
    {
        if (runs.empty()) return;
        runs.back().text += text;
    }
    bool dropTrailingHyphen()
    {
        if (runs.empty() || runs.back().text.isEmpty()) return false;
        if (runs.back().text.back() != QLatin1Char('-')) return false;
        runs.back().text.chop(1);
        return true;
    }
    QString html() const
    {
        QString out;
        for (const Run& run : runs) {
            if (run.text.isEmpty()) continue;
            const QString css = cssOf(run.style);
            if (css.isEmpty()) out += run.text.toHtmlEscaped();
            else out += QStringLiteral("<span style=\"%1\">%2</span>").arg(css, run.text.toHtmlEscaped());
        }
        return out;
    }
    bool isEmpty() const
    {
        for (const Run& run : runs)
            if (!run.text.isEmpty()) return false;
        return true;
    }
};

QString htmlDocument(const QString& fragment)
{
    // THE FRAGMENT MARKERS ARE WRITTEN HERE rather than left to the platform: Qt's Windows clipboard
    // adds them only when they are missing, and a document that carries its own says exactly where the
    // copied part starts, which is what a paste target reads.
    return QStringLiteral("<html><head><meta charset=\"utf-8\"></head><body>"
                          "<!--StartFragment-->%1<!--EndFragment--></body></html>")
        .arg(fragment);
}

// ---- Tables -----------------------------------------------------------------------------------------

// One piece of a table: a run of one line, which in a table is one cell (the analyzer splits a band at
// every gap wider than 0.8 em, so cells are lines of their own).
struct Piece {
    const PageReading* page = nullptr;
    Span span;
    PdfRect box;
    int line = -1;
    bool vertical = false;
};

PdfRect boxOfSpan(const PageReading& page, Span span)
{
    PdfRect box = page.charAt(span.begin).box;
    for (int p = span.begin + 1; p < span.end; ++p) box = unite(box, page.charAt(p).box);
    const PdfRect& line = page.layout().lines[static_cast<std::size_t>(page.lineOf(span.begin))].box;
    // Across the line, the line's extent: a cell's row is decided by where its LINE is, not by the
    // height of whichever glyphs happen to be in it.
    if (isVertical(page.layout().lines[static_cast<std::size_t>(page.lineOf(span.begin))]))
        return PdfRect{PdfPoint{line.left(), yOf(box).lo}, PdfSize{line.size.width, yOf(box).hi - yOf(box).lo}};
    return PdfRect{PdfPoint{xOf(box).lo, line.bottom()}, PdfSize{xOf(box).hi - xOf(box).lo, line.size.height}};
}

struct Table {
    std::vector<std::vector<std::vector<const Piece*>>> cells;   // [row][column] -> pieces
    int columns = 0;
};

Table buildTable(const std::vector<Piece>& pieces)
{
    Table table;
    if (pieces.empty()) return table;

    // VERTICAL WRITING: each line is a row of one cell, in reading order. A column selection over
    // vertical CJK takes whole columns of text, and a grid of them has no rows to find.
    const bool vertical = std::all_of(pieces.begin(), pieces.end(), [](const Piece& p) { return p.vertical; });
    if (vertical) {
        table.columns = 1;
        for (const Piece& piece : pieces) table.cells.push_back({{&piece}});
        return table;
    }

    // ROWS: top to bottom, a piece joining the row whose vertical extent holds its centre.
    std::vector<const Piece*> order;
    for (const Piece& piece : pieces) order.push_back(&piece);
    std::stable_sort(order.begin(), order.end(), [](const Piece* a, const Piece* b) {
        return yOf(a->box).hi > yOf(b->box).hi;
    });
    std::vector<std::vector<const Piece*>> rows;
    std::vector<Interval> rowBands;
    for (const Piece* piece : order) {
        const Interval band = yOf(piece->box);
        const double centre = (band.lo + band.hi) / 2.0;
        if (!rows.empty() && centre >= rowBands.back().lo && centre <= rowBands.back().hi) {
            rows.back().push_back(piece);
            rowBands.back().lo = std::min(rowBands.back().lo, band.lo);
            rowBands.back().hi = std::max(rowBands.back().hi, band.hi);
        } else {
            rows.push_back({piece});
            rowBands.push_back(band);
        }
    }
    for (auto& row : rows)
        std::stable_sort(row.begin(), row.end(), [](const Piece* a, const Piece* b) {
            return xOf(a->box).lo < xOf(b->box).lo;
        });

    // COLUMNS: the pieces' left edges clustered, a new column wherever consecutive edges are further
    // apart than half the median size -- cells of one column start at one edge, give or take the
    // producer's rounding.
    std::vector<double> sizes;
    std::vector<double> edges;
    for (const Piece& piece : pieces) {
        edges.push_back(xOf(piece.box).lo);
        sizes.push_back(yOf(piece.box).hi - yOf(piece.box).lo);
    }
    std::sort(sizes.begin(), sizes.end());
    const double tolerance = std::max(2.0, 0.5 * sizes[sizes.size() / 2]);
    std::sort(edges.begin(), edges.end());
    std::vector<double> columnEdges;
    for (const double edge : edges)
        if (columnEdges.empty() || edge - columnEdges.back() > tolerance) columnEdges.push_back(edge);
    const auto columnOf = [&](const Piece* piece) {
        const double edge = xOf(piece->box).lo;
        int best = 0;
        for (std::size_t c = 0; c < columnEdges.size(); ++c)
            if (columnEdges[c] <= edge + tolerance) best = static_cast<int>(c);
        return best;
    };

    table.columns = static_cast<int>(columnEdges.size());
    for (const auto& row : rows) {
        std::vector<std::vector<const Piece*>> cells(columnEdges.size());
        for (const Piece* piece : row) cells[static_cast<std::size_t>(columnOf(piece))].push_back(piece);
        table.cells.push_back(std::move(cells));
    }
    return table;
}

} // namespace

// =====================================================================================================
// PageReading
// =====================================================================================================

std::shared_ptr<const PageReading> PageReading::build(std::shared_ptr<const PageTextModel> model)
{
    if (!model) return nullptr;
    auto reading = std::shared_ptr<PageReading>(new PageReading);
    reading->model_ = std::move(model);
    // `TagTree` null: every Stage 5 caller passes none (D5-6, guide A-39).
    reading->layout_ = LayoutAnalyzer::analyze(*reading->model_, nullptr);

    const PageLayout& layout = reading->layout_;
    reading->lineSpans_.assign(layout.lines.size(), Span{});
    reading->blockSpans_.assign(layout.blocks.size(), Span{});

    // THE SAME WALK `LayoutAnalyzer::readingStream` MAKES -- blocks in order, lines in a block, characters
    // in a line -- with the line and block of every position kept beside it, which the stream drops.
    for (const int b : layout.order) {
        const TextBlock& block = layout.blocks[static_cast<std::size_t>(b)];
        const int blockStart = reading->size();
        bool firstLine = true;
        for (const int l : block.lines) {
            const TextLine& line = layout.lines[static_cast<std::size_t>(l)];
            const int lineStart = reading->size();
            for (std::size_t i = 0; i < line.chars.size(); ++i) {
                ReadingItem::Kind before = ReadingItem::Kind::Char;
                if (i == 0) {
                    before = firstLine ? ReadingItem::Kind::BlockBreak : ReadingItem::Kind::LineBreak;
                } else if (line.gapBefore[i]) {
                    before = ReadingItem::Kind::WordGap;
                }
                reading->chars_.push_back(line.chars[i]);
                reading->before_.push_back(before);
                reading->line_.push_back(l);
                reading->block_.push_back(b);
            }
            if (!line.chars.empty()) firstLine = false;
            reading->lineSpans_[static_cast<std::size_t>(l)] = Span{lineStart, reading->size()};
        }
        reading->blockSpans_[static_cast<std::size_t>(b)] = Span{blockStart, reading->size()};
    }
    return reading;
}

int PageReading::positionAt(PdfPoint point) const
{
    if (chars_.empty() || !std::isfinite(point.x) || !std::isfinite(point.y)) return -1;

    // THE LINE FIRST, by the distance from the point to its box -- so a point in a column gutter or
    // between two lines belongs to the nearest line rather than to whichever character is nearest in
    // a straight line, which on a page of small type is often the line above.
    int bestLine = -1;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t l = 0; l < layout_.lines.size(); ++l) {
        const Span span = lineSpans_[l];
        if (span.isEmpty()) continue;
        const double d = distanceTo(point, layout_.lines[l].box);
        if (d < bestDistance || (d == bestDistance && bestLine >= 0 && span.begin < lineSpans_[static_cast<std::size_t>(bestLine)].begin)) {
            bestDistance = d;
            bestLine = static_cast<int>(l);
        }
    }
    if (bestLine < 0) return -1;

    const TextLine& line = layout_.lines[static_cast<std::size_t>(bestLine)];
    const Span span = lineSpans_[static_cast<std::size_t>(bestLine)];
    const bool vertical = isVertical(line);
    int best = span.begin;
    double bestAlong = std::numeric_limits<double>::infinity();
    for (int p = span.begin; p < span.end; ++p) {
        const PdfRect& box = charAt(p).box;
        const double d = vertical ? distanceTo(point.y, yOf(box)) : distanceTo(point.x, xOf(box));
        if (d < bestAlong) {
            bestAlong = d;
            best = p;
        }
    }
    return best;
}

int PageReading::caretAt(PdfPoint point) const
{
    const int position = positionAt(point);
    if (position < 0) return 0;

    const TextLine& line = layout_.lines[static_cast<std::size_t>(lineOf(position))];
    const Span span = lineSpan(lineOf(position));
    const PdfRect& box = charAt(position).box;

    // WHICH WAY THIS CHARACTER READS. A vertical line reads downwards. A horizontal one reads the way
    // its neighbours say: a right-to-left word's next letter is to the LEFT, a number inside it reads
    // left to right -- so the line's majority direction alone would put the caret on the wrong side of
    // every digit in `rtl-text.pdf`'s mixed line.
    double forwardX = 1.0;
    double forwardY = 0.0;
    if (isVertical(line)) {
        forwardX = 0.0;
        forwardY = -1.0;
    } else {
        const double centre = (xOf(box).lo + xOf(box).hi) / 2.0;
        if (position + 1 < span.end) {
            const PdfRect& next = charAt(position + 1).box;
            forwardX = (xOf(next).lo + xOf(next).hi) / 2.0 >= centre ? 1.0 : -1.0;
        } else if (position > span.begin) {
            const PdfRect& previous = charAt(position - 1).box;
            forwardX = (xOf(previous).lo + xOf(previous).hi) / 2.0 <= centre ? 1.0 : -1.0;
        } else if (line.direction == WritingDirection::RightToLeft) {
            forwardX = -1.0;
        }
    }

    const double cx = (xOf(box).lo + xOf(box).hi) / 2.0;
    const double cy = (yOf(box).lo + yOf(box).hi) / 2.0;
    const double along = (point.x - cx) * forwardX + (point.y - cy) * forwardY;
    return along <= 0.0 ? position : position + 1;
}

Span PageReading::wordAt(int position) const
{
    if (position < 0 || position >= size()) return {};
    const Span line = lineAt(position);
    if (!isWordChar(charAt(position).codepoint)) return Span{position, position + 1};

    int begin = position;
    while (begin > line.begin && breakBefore(begin) == ReadingItem::Kind::Char
           && isWordChar(charAt(begin - 1).codepoint))
        --begin;
    int end = position + 1;
    while (end < line.end && breakBefore(end) == ReadingItem::Kind::Char && isWordChar(charAt(end).codepoint))
        ++end;
    return Span{begin, end};
}

// =====================================================================================================
// DocumentText
// =====================================================================================================

DocumentText::DocumentText(int pageCount)
{
    reset(pageCount);
}

void DocumentText::reset(int pageCount)
{
    pages_.assign(static_cast<std::size_t>(std::max(0, pageCount)), nullptr);
}

bool DocumentText::has(int page) const noexcept
{
    return page >= 0 && page < pageCount() && pages_[static_cast<std::size_t>(page)] != nullptr;
}

const PageReading* DocumentText::page(int page) const noexcept
{
    return has(page) ? pages_[static_cast<std::size_t>(page)].get() : nullptr;
}

bool DocumentText::insert(std::shared_ptr<const PageReading> reading)
{
    if (!reading) return false;
    const int index = reading->pageIndex();
    if (index < 0 || index >= pageCount()) return false;
    pages_[static_cast<std::size_t>(index)] = std::move(reading);
    return true;
}

bool DocumentText::erase(int page)
{
    if (!has(page)) return false;
    pages_[static_cast<std::size_t>(page)].reset();
    return true;
}

std::vector<int> DocumentText::missing(int first, int last) const
{
    std::vector<int> out;
    first = std::max(first, 0);
    last = std::min(last, pageCount() - 1);
    for (int p = first; p <= last; ++p)
        if (!has(p)) out.push_back(p);
    return out;
}

// =====================================================================================================
// Units
// =====================================================================================================

TextUnit unitAt(const PageReading& page, PdfPoint point, Granularity granularity)
{
    const int pageIndex = page.pageIndex();
    if (granularity == Granularity::Character || page.isEmpty()) {
        const TextPosition caret{pageIndex, page.caretAt(point)};
        return TextUnit{caret, caret};
    }
    const int position = page.positionAt(point);
    if (position < 0) {
        const TextPosition caret{pageIndex, 0};
        return TextUnit{caret, caret};
    }
    Span span;
    switch (granularity) {
    case Granularity::Word: span = page.wordAt(position); break;
    case Granularity::Line: span = page.lineAt(position); break;
    case Granularity::Block: span = page.blockAt(position); break;
    case Granularity::Character: break;
    }
    return TextUnit{TextPosition{pageIndex, span.begin}, TextPosition{pageIndex, span.end}};
}

// =====================================================================================================
// TextSelection
// =====================================================================================================

TextSelection TextSelection::flow(TextPosition anchor, TextPosition focus)
{
    TextSelection selection;
    if (!anchor.isValid() || !focus.isValid()) return selection;
    selection.kind_ = Kind::Flow;
    selection.start_ = std::min(anchor, focus);
    selection.end_ = std::max(anchor, focus);
    return selection;
}

TextSelection TextSelection::spanning(const TextUnit& anchor, const TextUnit& focus)
{
    TextSelection selection;
    if (!anchor.begin.isValid() || !focus.begin.isValid()) return selection;
    selection.kind_ = Kind::Flow;
    selection.start_ = std::min(anchor.begin, focus.begin);
    selection.end_ = std::max(anchor.end, focus.end);
    return selection;
}

TextSelection TextSelection::column(int page, PdfRect rect)
{
    TextSelection selection;
    if (page < 0 || !std::isfinite(rect.origin.x) || !std::isfinite(rect.origin.y)
        || !std::isfinite(rect.size.width) || !std::isfinite(rect.size.height))
        return selection;
    selection.kind_ = Kind::Column;
    selection.columnPage_ = page;
    const Interval x = xOf(rect);
    const Interval y = yOf(rect);
    selection.columnRect_ = PdfRect{PdfPoint{x.lo, y.lo}, PdfSize{x.hi - x.lo, y.hi - y.lo}};
    return selection;
}

TextSelection TextSelection::wholeDocument(int pageCount)
{
    if (pageCount <= 0) return {};
    return flow(TextPosition{0, 0}, TextPosition{pageCount - 1, TextPosition::kEndOfPage});
}

bool TextSelection::isEmpty() const noexcept
{
    switch (kind_) {
    case Kind::None: return true;
    case Kind::Flow: return start_ == end_;
    case Kind::Column: return columnRect_.size.width <= 0.0 || columnRect_.size.height <= 0.0;
    }
    return true;
}

TextSelection TextSelection::remapped(const PageMap& map) const
{
    if (isEmpty()) return {};
    if (kind_ == Kind::Column) {
        const int to = map.newIndexOf(columnPage_);
        return to == PageMap::kNone ? TextSelection{} : column(to, columnRect_);
    }
    // `Ctrl+A`'s selection is the document's, whatever its pages now are.
    const bool whole = start_ == TextPosition{0, 0} && end_.page == map.oldCount() - 1
                       && end_.caret == TextPosition::kEndOfPage;
    if (whole) return wholeDocument(map.newCount());

    const int first = map.newIndexOf(start_.page);
    if (first == PageMap::kNone) return {};
    for (int page = start_.page + 1; page <= end_.page; ++page) {
        if (map.newIndexOf(page) != first + (page - start_.page)) return {};
    }
    TextSelection moved = *this;
    moved.start_.page = first;
    moved.end_.page = first + (end_.page - start_.page);
    return moved;
}

int TextSelection::firstPage() const noexcept
{
    if (isEmpty()) return -1;
    return kind_ == Kind::Column ? columnPage_ : start_.page;
}

int TextSelection::lastPage() const noexcept
{
    if (isEmpty()) return -1;
    return kind_ == Kind::Column ? columnPage_ : end_.page;
}

std::vector<Span> TextSelection::spansOn(const PageReading& page) const
{
    std::vector<Span> spans;
    const int index = page.pageIndex();
    if (!covers(index) || page.isEmpty()) return spans;

    if (kind_ == Kind::Flow) {
        const int begin = index == start_.page ? std::clamp(start_.caret, 0, page.size()) : 0;
        const int end = index == end_.page ? std::clamp(end_.caret, 0, page.size()) : page.size();
        if (end > begin) spans.push_back(Span{begin, end});
        return spans;
    }

    // A COLUMN: every line the rectangle crosses, and on it the characters whose centre is inside the
    // rectangle's extent ALONG the line. Crossing is judged across the line -- a rectangle that grazes a
    // line's box selects from it, which is what a person dragging down a column of cells means.
    const PdfRect& rect = columnRect_;
    const PageLayout& layout = page.layout();
    for (std::size_t l = 0; l < layout.lines.size(); ++l) {
        const Span lineSpan = page.lineSpan(static_cast<int>(l));
        if (lineSpan.isEmpty()) continue;
        const TextLine& line = layout.lines[l];
        const bool vertical = isVertical(line);
        const Interval across = vertical ? xOf(line.box) : yOf(line.box);
        const Interval rectAcross = vertical ? xOf(rect) : yOf(rect);
        if (across.hi < rectAcross.lo || across.lo > rectAcross.hi) continue;

        const Interval rectAlong = vertical ? yOf(rect) : xOf(rect);
        int runStart = -1;
        for (int p = lineSpan.begin; p <= lineSpan.end; ++p) {
            bool inside = false;
            if (p < lineSpan.end) {
                const PdfRect& box = page.charAt(p).box;
                const double centre = vertical ? (yOf(box).lo + yOf(box).hi) / 2.0 : (xOf(box).lo + xOf(box).hi) / 2.0;
                inside = centre >= rectAlong.lo && centre <= rectAlong.hi;
            }
            if (inside && runStart < 0) runStart = p;
            if (!inside && runStart >= 0) {
                spans.push_back(Span{runStart, p});
                runStart = -1;
            }
        }
    }
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.begin < b.begin; });
    return spans;
}

std::vector<PdfRect> TextSelection::highlightOn(const PageReading& page) const
{
    std::vector<PdfRect> rects;
    for (const Span& span : spansOn(page)) {
        int runStart = span.begin;
        for (int p = span.begin; p <= span.end; ++p) {
            const bool lineEnds = p == span.end || page.lineOf(p) != page.lineOf(runStart);
            if (!lineEnds) continue;
            if (p > runStart) rects.push_back(boxOfSpan(page, Span{runStart, p}));
            runStart = p;
        }
    }
    return rects;
}

std::vector<double> TextSelection::markupQuadsOn(const PageReading& page) const
{
    std::vector<double> quads;
    for (const Span& span : spansOn(page)) {
        int runStart = span.begin;
        for (int p = span.begin; p <= span.end; ++p) {
            const bool lineEnds = p == span.end || page.lineOf(p) != page.lineOf(runStart);
            if (!lineEnds) continue;
            if (p > runStart) {
                const PdfRect box = boxOfSpan(page, Span{runStart, p});
                const double l = box.left(), r = box.right(), b = box.bottom(), t = box.top();
                if (isVertical(page.layout().lines[static_cast<std::size_t>(page.lineOf(runStart))]))
                    quads.insert(quads.end(), {r, t, r, b, l, t, l, b});   // reading down: right is the top
                else
                    quads.insert(quads.end(), {l, t, r, t, l, b, r, b});
            }
            runStart = p;
        }
    }
    return quads;
}

std::optional<InsertionPoint> insertionPointAt(const PageReading& page, int caret)
{
    if (page.isEmpty()) return std::nullopt;
    caret = std::clamp(caret, 0, page.size());
    // The characters either side, kept only when they share a line.
    const int before = caret - 1;
    const int after = caret < page.size() ? caret : -1;
    const int anchor = after >= 0 ? after : before;
    const TextLine& line = page.layout().lines[static_cast<std::size_t>(page.lineOf(anchor))];
    const bool vertical = isVertical(line);
    const bool hasBefore = before >= 0 && page.lineOf(before) == page.lineOf(anchor);
    const bool hasAfter = after >= 0 && page.lineOf(after) == page.lineOf(anchor);

    // Along the line: x for a horizontal line, y for a vertical one.
    const auto range = [vertical](const PdfRect& r) { return vertical ? yOf(r) : xOf(r); };
    double along = 0.0;
    if (hasBefore && hasAfter) {
        const Interval a = range(page.charAt(before).box);
        const Interval c = range(page.charAt(after).box);
        // The FACING edges, whichever way the line reads.
        along = (a.lo + a.hi) / 2.0 <= (c.lo + c.hi) / 2.0 ? (a.hi + c.lo) / 2.0 : (c.hi + a.lo) / 2.0;
    } else {
        const int only = hasAfter ? after : before;
        const Interval o = range(page.charAt(only).box);
        const bool forward = line.direction == WritingDirection::LeftToRight;
        // Before the first character: its leading edge; after the last: its trailing edge.
        if (vertical) along = hasAfter ? o.hi : o.lo;   // reading down: leading is the top
        else along = (hasAfter == forward) ? o.lo : o.hi;
    }
    InsertionPoint out;
    out.vertical = vertical;
    if (vertical) {
        out.at = PdfPoint{xOf(line.box).lo, along};
        out.across = xOf(line.box).hi - xOf(line.box).lo;
    } else {
        out.at = PdfPoint{along, yOf(line.box).lo};
        out.across = yOf(line.box).hi - yOf(line.box).lo;
    }
    return out;
}

std::vector<int> TextSelection::missingPages(const DocumentText& text) const
{
    if (isEmpty()) return {};
    return text.missing(firstPage(), lastPage());
}

CopiedText TextSelection::copy(const DocumentText& text) const
{
    CopiedText out;
    if (isEmpty()) return out;
    out.missingPages = missingPages(text);

    // ---- A COLUMN, OR A TABLE BLOCK: pieces into a grid -------------------------------------------
    const auto writeTable = [&out](const std::vector<Piece>& pieces, QString& plain, QString& html) {
        const Table table = buildTable(pieces);
        html += QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"2\">");
        for (std::size_t r = 0; r < table.cells.size(); ++r) {
            if (r > 0) plain += QLatin1Char('\n');
            html += QStringLiteral("<tr>");
            for (std::size_t c = 0; c < table.cells[r].size(); ++c) {
                if (c > 0) plain += QLatin1Char('\t');
                RunWriter cell;
                bool firstPiece = true;
                for (const Piece* piece : table.cells[r][c]) {
                    if (!firstPiece) {
                        plain += QLatin1Char(' ');
                        cell.appendSeparator(QStringLiteral(" "));
                    }
                    firstPiece = false;
                    bool pendingGap = false;
                    bool wrote = false;
                    for (int p = piece->span.begin; p < piece->span.end; ++p) {
                        const TextChar& c2 = piece->page->charAt(p);
                        if (p > piece->span.begin && piece->page->breakBefore(p) != ReadingItem::Kind::Char)
                            pendingGap = true;
                        if (c2.isUnmapped()) {
                            ++out.unmappedSkipped;
                            pendingGap = true;
                            continue;
                        }
                        if (pendingGap && wrote) {
                            plain += QLatin1Char(' ');
                            cell.appendSeparator(QStringLiteral(" "));
                        }
                        pendingGap = false;
                        const QString s = scalar(c2.codepoint);
                        plain += s;
                        cell.append(styleOf(piece->page->model(), c2), s);
                        ++out.characters;
                        wrote = true;
                    }
                }
                html += QStringLiteral("<td>") + cell.html() + QStringLiteral("</td>");
            }
            html += QStringLiteral("</tr>");
        }
        html += QStringLiteral("</table>");
    };

    const auto piecesOf = [](const PageReading& page, const std::vector<Span>& spans) {
        std::vector<Piece> pieces;
        for (const Span& span : spans) {
            int runStart = span.begin;
            for (int p = span.begin; p <= span.end; ++p) {
                if (p < span.end && page.lineOf(p) == page.lineOf(runStart)) continue;
                if (p > runStart) {
                    const int line = page.lineOf(runStart);
                    pieces.push_back(Piece{&page, Span{runStart, p}, boxOfSpan(page, Span{runStart, p}), line,
                                           isVertical(page.layout().lines[static_cast<std::size_t>(line)])});
                }
                runStart = p;
            }
        }
        return pieces;
    };

    if (kind_ == Kind::Column) {
        out.isTable = true;
        const PageReading* page = text.page(columnPage_);
        if (page == nullptr) return out;
        QString fragment;
        writeTable(piecesOf(*page, spansOn(*page)), out.plain, fragment);
        if (out.characters > 0) out.html = htmlDocument(fragment);
        return out;
    }

    // ---- A FLOW: paragraphs, and a table block as a table ------------------------------------------
    QString fragment;
    RunWriter paragraph;
    bool paragraphOpen = false;
    bool paragraphRtl = false;
    bool wroteAny = false;          // a character has been written to the plain text
    int pending = 0;                // the strongest break since the last written character
    bool lastWasLineHyphen = false; // the last written character is a line-final hyphen (D5-5)

    const auto closeParagraph = [&] {
        if (paragraphOpen && !paragraph.isEmpty()) {
            fragment += paragraphRtl ? QStringLiteral("<p dir=\"rtl\">") : QStringLiteral("<p>");
            fragment += paragraph.html() + QStringLiteral("</p>");
        }
        paragraph = RunWriter{};
        paragraphOpen = false;
    };

    for (int pageIndex = start_.page; pageIndex <= end_.page; ++pageIndex) {
        const PageReading* page = text.page(pageIndex);
        if (page == nullptr) continue;   // reported in `missingPages`; nothing is invented for it
        const std::vector<Span> spans = spansOn(*page);
        if (spans.empty()) continue;
        const Span span = spans.front();
        pending = std::max(pending, wroteAny ? 3 : 0);   // a new page is a new block

        int p = span.begin;
        while (p < span.end) {
            const int block = page->blockOf(p);
            const TextBlock& textBlock = page->layout().blocks[static_cast<std::size_t>(block)];

            if (textBlock.kind == BlockKind::Table) {
                // THE PART OF A TABLE BLOCK THE FLOW COVERS, as a table of its own.
                const Span blockSpan = page->blockSpan(block);
                const Span covered{p, std::min(span.end, blockSpan.end)};
                closeParagraph();
                if (wroteAny) out.plain += QStringLiteral("\n\n");
                const int before = out.characters;
                writeTable(piecesOf(*page, {covered}), out.plain, fragment);
                if (out.characters > before) wroteAny = true;
                pending = 3;
                lastWasLineHyphen = false;
                p = covered.end;
                continue;
            }

            if (p > span.begin) pending = std::max(pending, strength(page->breakBefore(p)));
            const TextChar& c = page->charAt(p);
            if (c.isUnmapped()) {
                ++out.unmappedSkipped;
                pending = std::max(pending, 1);
                ++p;
                continue;
            }

            // THE SEPARATOR THE BREAKS SINCE THE LAST CHARACTER AMOUNT TO.
            if (wroteAny && pending > 0) {
                out.plain += pending == 3 ? QStringLiteral("\n\n") : pending == 2 ? QStringLiteral("\n") : QStringLiteral(" ");
            }
            if (pending == 3 || !paragraphOpen) {
                closeParagraph();
                paragraphOpen = true;
                const TextLine& line = page->layout().lines[static_cast<std::size_t>(page->lineOf(p))];
                paragraphRtl = line.direction == WritingDirection::RightToLeft;
            } else if (pending == 2) {
                // A LINE BREAK INSIDE A PARAGRAPH IS A SPACE in the HTML -- except after a line-final
                // hyphen, which is D5-5's rule read the same way `TextNormalizer` reads it: before a
                // lowercase letter the hyphen goes and the halves join (`exam-` / `ple`); before anything
                // else it stays and no space is added (`North-` / `West` is `North-West`).
                if (lastWasLineHyphen) {
                    if (QChar::isLower(c.codepoint)) paragraph.dropTrailingHyphen();
                } else {
                    paragraph.appendSeparator(QStringLiteral(" "));
                }
            } else if (pending == 1) {
                paragraph.appendSeparator(QStringLiteral(" "));
            }
            pending = 0;

            const QString s = scalar(c.codepoint);
            out.plain += s;
            paragraph.append(styleOf(page->model(), c), s);
            ++out.characters;
            wroteAny = true;
            // THE ENGINE'S FLAG IS THE EVIDENCE (D5-5, guide A-36): an unflagged hyphen at a line end
            // is text, and is never joined.
            lastWasLineHyphen = c.isHyphen();
            ++p;
        }
    }
    closeParagraph();
    if (out.characters > 0) out.html = htmlDocument(fragment);
    return out;
}

} // namespace rawpage::core::text
