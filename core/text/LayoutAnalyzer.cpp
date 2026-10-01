#include "core/text/LayoutAnalyzer.h"

#include <QChar>

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <tuple>

namespace rawpage::core::text {
namespace {

// --- Thresholds, in em of the glyphs involved. Each is argued in the header's step list. -----------
constexpr double kLineGapEm = 0.8;          // a horizontal gap wider than this ends a line
constexpr double kWordGapEm = 0.15;         // ... and one wider than this, between non-spaces, is a word gap
constexpr double kBandOverlap = 0.5;        // share of the smaller height two boxes must overlap
constexpr double kVerticalColumnEm = 0.2;   // centre-line tolerance for a vertical line
constexpr double kVerticalGapEm = 0.5;      // the most a vertical line's glyphs may be apart
constexpr double kSquareRatio = 0.6;        // a vertical glyph is at least this wide for its height
constexpr double kLeadingRatio = 1.4;       // spacing within this of a block's leading continues it
constexpr double kFirstPairEm = 2.2;        // a one-line block continues within this spacing
constexpr double kCutEm = 0.9;              // an XY-cut gap is at least this wide
constexpr double kTableAlign = 2.0;         // points: cell left edges within this are one column
constexpr double kTableRowEm = 2.5;         // table rows are at most this far apart
constexpr int kTableMinRows = 3;
constexpr double kTableMaxWords = 3.0;
constexpr double kRuleSlack = 2.0;          // points
constexpr std::size_t kRecentBlocks = 64;   // block joining looks back this far and no further
constexpr int kMaxCutDepth = 64;

struct Glyph {
    int index = -1;
    double x0 = 0, x1 = 0, y0 = 0, y1 = 0;   // the loose box
    double cx = 0, cy = 0;
    double ox = 0, oy = 0;                   // the origin
    double size = 1.0;
    char32_t cp = 0;
    bool space = false;
};

bool finite(double v) { return std::isfinite(v); }

bool isSpaceCodepoint(char32_t cp)
{
    return cp == U' ' || cp == 0x00A0 || cp == U'\t' || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x3000;
}

double glyphSize(const TextChar& c)
{
    double size = c.fontSize;
    if (!finite(size) || size <= 0.0) size = c.box.size.height;
    if (!finite(size) || size <= 0.0) size = 1.0;
    return size;
}

double median(std::vector<double> values)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

PdfRect unite(const PdfRect& a, const PdfRect& b)
{
    const double x0 = std::min(a.left(), b.left());
    const double y0 = std::min(a.bottom(), b.bottom());
    const double x1 = std::max(a.right(), b.right());
    const double y1 = std::max(a.top(), b.top());
    return PdfRect{PdfPoint{x0, y0}, PdfSize{x1 - x0, y1 - y0}};
}

PdfRect boxOf(const Glyph& g)
{
    return PdfRect{PdfPoint{g.x0, g.y0}, PdfSize{g.x1 - g.x0, g.y1 - g.y0}};
}

// A total order on glyphs that depends on geometry and content alone, never on the model index
// unless two glyphs are identical in every respect -- so a shuffled model lays out byte-identically
// (guide A-39). The ligature `f` and `i` PDFium reports with ONE box fall back to codepoint order.
bool geometryLess(const Glyph& a, const Glyph& b, double Glyph::*first, bool firstDescending)
{
    const double fa = a.*first;
    const double fb = b.*first;
    if (fa != fb) return firstDescending ? fa > fb : fa < fb;
    return std::tie(a.x0, a.cy, a.x1, a.cp, a.size) < std::tie(b.x0, b.cy, b.x1, b.cp, b.size);
}

// --- UAX #9, rules L1-L2 over resolved levels, for one right-to-left line --------------------------
// `visual` is the line in page order, left to right. Returns it in logical order.
std::vector<Glyph> toLogicalOrder(const std::vector<Glyph>& visual)
{
    const std::size_t n = visual.size();
    // 1 = right-to-left, 2 = left-to-right or a number inside an RTL line, 0 = neutral (unresolved).
    std::vector<int> level(n, 0);
    // What a neutral's neighbours count as for N1: a NUMBER counts as right-to-left there.
    std::vector<int> strongForNeutral(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        switch (QChar::direction(visual[i].cp)) {
        case QChar::DirR:
        case QChar::DirAL:
            level[i] = 1;
            strongForNeutral[i] = 1;
            break;
        case QChar::DirL:
            level[i] = 2;
            strongForNeutral[i] = 2;
            break;
        case QChar::DirEN:
        case QChar::DirAN:
            level[i] = 2;
            strongForNeutral[i] = 1;
            break;
        default:
            break;
        }
    }
    // N1/N2: a run of neutrals between two sides of one direction takes it; otherwise the line's.
    for (std::size_t i = 0; i < n;) {
        if (level[i] != 0) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < n && level[j] == 0) ++j;
        const int before = i > 0 ? strongForNeutral[i - 1] : 1;
        const int after = j < n ? strongForNeutral[j] : 1;
        const int resolved = (before == after) ? before : 1;
        for (std::size_t k = i; k < j; ++k) level[k] = resolved;
        i = j;
    }
    // L2: from the highest level down to the lowest odd one, reverse every run at that level or above.
    // Applied to PAGE order with the same levels it yields logical order, because each reversal is its
    // own inverse over the same runs.
    std::vector<std::size_t> position(n);
    for (std::size_t i = 0; i < n; ++i) position[i] = i;
    for (int k = 2; k >= 1; --k) {
        for (std::size_t i = 0; i < n;) {
            if (level[position[i]] < k) {
                ++i;
                continue;
            }
            std::size_t j = i;
            while (j < n && level[position[j]] >= k) ++j;
            std::reverse(position.begin() + static_cast<std::ptrdiff_t>(i),
                         position.begin() + static_cast<std::ptrdiff_t>(j));
            i = j;
        }
    }
    std::vector<Glyph> logical;
    logical.reserve(n);
    for (const std::size_t p : position) logical.push_back(visual[p]);
    return logical;
}

TextLine makeLine(const std::vector<Glyph>& ordered, WritingDirection direction)
{
    TextLine line;
    line.direction = direction;
    std::vector<double> sizes;
    std::vector<double> baselines;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const Glyph& g = ordered[i];
        line.chars.push_back(g.index);
        bool gap = false;
        if (i > 0) {
            const Glyph& p = ordered[i - 1];
            const double distance =
                direction == WritingDirection::TopToBottom
                    ? std::max(p.y0, g.y0) - std::min(p.y1, g.y1)
                    : std::max(p.x0, g.x0) - std::min(p.x1, g.x1);
            gap = !p.space && !g.space && distance > kWordGapEm * std::max(p.size, g.size);
        }
        line.gapBefore.push_back(gap);
        line.box = i == 0 ? boxOf(g) : unite(line.box, boxOf(g));
        sizes.push_back(g.size);
        baselines.push_back(direction == WritingDirection::TopToBottom ? g.cx : g.oy);
    }
    line.fontSize = median(sizes);
    line.baseline = median(baselines);
    return line;
}

bool isHorizontal(const TextLine& line) { return line.direction != WritingDirection::TopToBottom; }

// A list marker: `1.`, `12)`, `a.`, or a bullet -- the line's non-space text alone.
bool isListMarker(const PageTextModel& model, const TextLine& line)
{
    std::u32string text;
    for (const int index : line.chars) {
        const char32_t cp = model.chars()[static_cast<std::size_t>(index)].codepoint;
        if (!isSpaceCodepoint(cp)) text.push_back(cp);
    }
    if (text.empty() || text.size() > 4) return false;
    if (text.size() == 1) {
        const char32_t c = text[0];
        return c == 0x2022 || c == 0x00B7 || c == 0x2023 || c == 0x25AA || c == 0x2013 || c == U'*';
    }
    const char32_t last = text.back();
    if (last != U'.' && last != U')') return false;
    const std::u32string head = text.substr(0, text.size() - 1);
    const bool digits = std::all_of(head.begin(), head.end(), [](char32_t c) { return c >= U'0' && c <= U'9'; });
    const bool letter = head.size() == 1 && ((head[0] >= U'a' && head[0] <= U'z') || (head[0] >= U'A' && head[0] <= U'Z'));
    return digits || letter;
}

int wordsIn(const PageTextModel& model, const TextLine& line)
{
    int words = 1;
    for (std::size_t i = 0; i < line.chars.size(); ++i) {
        const char32_t cp = model.chars()[static_cast<std::size_t>(line.chars[i])].codepoint;
        if (isSpaceCodepoint(cp) || line.gapBefore[i]) ++words;
    }
    return words;
}

double horizontalOverlap(const PdfRect& a, const PdfRect& b)
{
    return std::min(a.right(), b.right()) - std::max(a.left(), b.left());
}

// --- XY-cut -----------------------------------------------------------------------------------------
// Groups `items` (block indices) by gaps in their projection on one axis, in reading order along it.
std::vector<std::vector<int>> cutGroups(const std::vector<int>& items, const std::vector<TextBlock>& blocks,
                                        bool horizontalCut, double threshold)
{
    struct Span {
        double from, to;   // along the axis, `from` < `to`
        int item;
    };
    std::vector<Span> spans;
    for (const int item : items) {
        const PdfRect& box = blocks[static_cast<std::size_t>(item)].box;
        // A horizontal cut reads TOP DOWN, so it orders by descending y: the span is negated.
        spans.push_back(horizontalCut ? Span{-box.top(), -box.bottom(), item}
                                      : Span{box.left(), box.right(), item});
    }
    std::sort(spans.begin(), spans.end(), [](const Span& a, const Span& b) {
        return std::tie(a.from, a.to, a.item) < std::tie(b.from, b.to, b.item);
    });
    std::vector<std::vector<int>> groups;
    double reach = 0.0;
    for (const Span& span : spans) {
        if (groups.empty() || span.from - reach >= threshold) {
            groups.push_back({span.item});
            reach = span.to;
        } else {
            groups.back().push_back(span.item);
            reach = std::max(reach, span.to);
        }
    }
    return groups;
}

void orderBlocks(const std::vector<int>& items, const PageLayout& layout, double threshold, int depth,
                 std::vector<int>& out)
{
    if (items.size() <= 1 || depth >= kMaxCutDepth) {
        std::vector<int> sorted = items;
        std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
            const PdfRect& ba = layout.blocks[static_cast<std::size_t>(a)].box;
            const PdfRect& bb = layout.blocks[static_cast<std::size_t>(b)].box;
            return std::make_tuple(-ba.top(), ba.left(), a) < std::make_tuple(-bb.top(), bb.left(), b);
        });
        out.insert(out.end(), sorted.begin(), sorted.end());
        return;
    }
    const std::vector<std::vector<int>> bands = cutGroups(items, layout.blocks, true, threshold);
    if (bands.size() > 1) {
        // A COLUMN LONGER THAN ITS NEIGHBOUR IS STILL ONE COLUMN (S5 guide I-07). Horizontal-first XY-cut
        // finds a full-width gap at every paragraph break of the longer column BELOW the end of the shorter
        // one, and read band by band the page became: the left column's top, the right column, then the
        // rest of the left -- so a revision that re-flowed text across the column break moved words in the
        // reading order that never moved on the page (found at T12 by the comparison of `report-v1.pdf`
        // and `report-v2.pdf`). So a band that splits into columns takes the bands directly below it
        // whose blocks lie wholly inside ONE of its columns, while the gap stays a paragraph's -- at most
        // two cut thresholds -- and each column is then read whole. A spanning block, or a line set apart
        // by more than a paragraph gap (a footer), still ends the columns.
        for (std::size_t b = 0; b < bands.size();) {
            std::vector<std::vector<int>> columns = cutGroups(bands[b], layout.blocks, false, threshold);
            const bool vertical = std::all_of(bands[b].begin(), bands[b].end(), [&](int item) {
                const TextBlock& block = layout.blocks[static_cast<std::size_t>(item)];
                return !block.lines.empty()
                       && !isHorizontal(layout.lines[static_cast<std::size_t>(block.lines.front())]);
            });
            if (columns.size() < 2 || vertical) {
                orderBlocks(bands[b], layout, threshold, depth + 1, out);
                ++b;
                continue;
            }
            const auto extentOf = [&](const std::vector<int>& group) {
                PdfRect box = layout.blocks[static_cast<std::size_t>(group.front())].box;
                for (const int item : group) box = unite(box, layout.blocks[static_cast<std::size_t>(item)].box);
                return box;
            };
            std::vector<PdfRect> extents;
            for (const auto& column : columns) extents.push_back(extentOf(column));
            double bottom = extentOf(bands[b]).bottom();
            std::size_t next = b + 1;
            for (; next < bands.size(); ++next) {
                const PdfRect below = extentOf(bands[next]);
                if (bottom - below.top() > 2.0 * threshold) break;
                // UNDER ONE COLUMN: overlapping it and no other. Not "inside its extent": a ragged-right
                // paragraph below can be a point wider than every line the column held above it.
                int inside = -1;
                for (std::size_t c = 0; c < extents.size(); ++c) {
                    if (horizontalOverlap(below, extents[c]) > 0.0) inside = inside == -1 ? static_cast<int>(c) : -2;
                }
                if (inside < 0) break;
                columns[static_cast<std::size_t>(inside)].insert(columns[static_cast<std::size_t>(inside)].end(),
                                                                 bands[next].begin(), bands[next].end());
                bottom = below.bottom();
            }
            for (const auto& column : columns) orderBlocks(column, layout, threshold, depth + 1, out);
            b = next;
        }
        return;
    }
    std::vector<std::vector<int>> columns = cutGroups(items, layout.blocks, false, threshold);
    if (columns.size() > 1) {
        // Vertical writing reads its columns right to left.
        const bool vertical = std::all_of(items.begin(), items.end(), [&](int item) {
            const TextBlock& block = layout.blocks[static_cast<std::size_t>(item)];
            return !block.lines.empty()
                   && !isHorizontal(layout.lines[static_cast<std::size_t>(block.lines.front())]);
        });
        if (vertical) std::reverse(columns.begin(), columns.end());
        for (const auto& column : columns) orderBlocks(column, layout, threshold, depth + 1, out);
        return;
    }
    orderBlocks(items, layout, threshold, kMaxCutDepth, out);   // no cut: top to bottom
}

} // namespace

PageLayout LayoutAnalyzer::analyze(const PageTextModel& model, const TagTree* tags)
{
    // `tags` is null for every Stage 5 caller (D5-6); a Stage 7 caller that passes one will find it
    // ignored here until that stage teaches this file to prefer it.
    (void)tags;

    PageLayout layout;
    const auto& chars = model.chars();

    // --- The glyphs, and the ones that cannot be placed ---------------------------------------------
    std::vector<Glyph> glyphs;
    glyphs.reserve(chars.size());
    for (std::size_t i = 0; i < chars.size(); ++i) {
        const TextChar& c = chars[i];
        if (c.isGenerated()) continue;   // the engine's inferred whitespace: the analyzer decides breaks
        const PdfRect& b = c.box;
        if (!finite(b.origin.x) || !finite(b.origin.y) || !finite(b.size.width) || !finite(b.size.height)
            || !finite(c.origin.x) || !finite(c.origin.y)) {
            layout.unplaced.push_back(static_cast<int>(i));
            continue;
        }
        Glyph g;
        g.index = static_cast<int>(i);
        g.x0 = std::min(b.left(), b.right());
        g.x1 = std::max(b.left(), b.right());
        g.y0 = std::min(b.bottom(), b.top());
        g.y1 = std::max(b.bottom(), b.top());
        g.cx = (g.x0 + g.x1) / 2.0;
        g.cy = (g.y0 + g.y1) / 2.0;
        g.ox = c.origin.x;
        g.oy = c.origin.y;
        g.size = glyphSize(c);
        g.cp = c.codepoint;
        g.space = isSpaceCodepoint(c.codepoint);
        // A DEGENERATE BOX is given a height from the origin and the size. PDFium reports a space
        // drawn on its own -- `rtl-text.pdf`'s logical-order page positions every glyph separately --
        // as a zero-size box ON THE BASELINE, which no line's band overlaps: each such space became a
        // line of its own (found at T4 by the layout measurement).
        if (g.y1 - g.y0 <= 0.0) {
            g.y0 = g.oy - 0.2 * g.size;
            g.y1 = g.oy + 0.8 * g.size;
        }
        if (g.x1 < g.x0) std::swap(g.x0, g.x1);
        if (!finite(g.cx) || !finite(g.cy)) {   // two finite edges can still overflow their sum
            layout.unplaced.push_back(g.index);
            continue;
        }
        glyphs.push_back(g);
    }

    // --- The bound (see the header): too many characters, and the model's order is used -------------
    if (glyphs.size() > kMaxCharsLaidOut) {
        layout.degraded = true;
        TextBlock block;
        for (std::size_t start = 0; start < glyphs.size(); start += 1000) {
            const std::size_t end = std::min(glyphs.size(), start + 1000);
            layout.lines.push_back(makeLine(std::vector<Glyph>(glyphs.begin() + static_cast<std::ptrdiff_t>(start),
                                                               glyphs.begin() + static_cast<std::ptrdiff_t>(end)),
                                            WritingDirection::LeftToRight));
            block.lines.push_back(static_cast<int>(layout.lines.size()) - 1);
            block.box = block.lines.size() == 1 ? layout.lines.back().box : unite(block.box, layout.lines.back().box);
        }
        layout.blocks.push_back(block);
        layout.order.push_back(0);
        layout.columns = 1;
        return layout;
    }

    // --- 1. Vertical lines --------------------------------------------------------------------------
    std::vector<bool> taken(glyphs.size(), false);
    {
        // A glyph with a neighbour beside it on its own baseline is part of a horizontal line,
        // whatever is stacked above and below it: three lines that each begin with an `M` share a
        // centre line and are square enough, and are not a vertical line. Found by sorting on the
        // baseline and looking at the next glyph along it.
        std::vector<bool> besideAnother(glyphs.size(), false);
        {
            std::vector<std::size_t> alongLine(glyphs.size());
            for (std::size_t i = 0; i < glyphs.size(); ++i) alongLine[i] = i;
            std::sort(alongLine.begin(), alongLine.end(), [&](std::size_t a, std::size_t b) {
                const Glyph& ga = glyphs[a];
                const Glyph& gb = glyphs[b];
                return std::make_tuple(ga.cy, ga.x0, ga.cp, a) < std::make_tuple(gb.cy, gb.x0, gb.cp, b);
            });
            for (std::size_t k = 1; k < alongLine.size(); ++k) {
                const Glyph& a = glyphs[alongLine[k - 1]];
                const Glyph& b = glyphs[alongLine[k]];
                const double size = std::max(a.size, b.size);
                const double gap = std::max(a.x0, b.x0) - std::min(a.x1, b.x1);
                if (std::abs(a.cy - b.cy) < 0.3 * size && gap < 0.3 * size) {
                    besideAnother[alongLine[k - 1]] = true;
                    besideAnother[alongLine[k]] = true;
                }
            }
        }
        // Down each centre line, top to bottom.
        std::vector<std::size_t> byColumn(glyphs.size());
        for (std::size_t i = 0; i < glyphs.size(); ++i) byColumn[i] = i;
        std::sort(byColumn.begin(), byColumn.end(), [&](std::size_t a, std::size_t b) {
            const Glyph& ga = glyphs[a];
            const Glyph& gb = glyphs[b];
            return std::make_tuple(ga.cx, -ga.cy, ga.x0, ga.cp, a) < std::make_tuple(gb.cx, -gb.cy, gb.x0, gb.cp, b);
        });
        std::vector<std::size_t> chain;
        const auto closeChain = [&] {
            if (chain.size() >= 3) {
                std::vector<Glyph> ordered;
                for (const std::size_t i : chain) {
                    ordered.push_back(glyphs[i]);
                    taken[i] = true;
                }
                layout.lines.push_back(makeLine(ordered, WritingDirection::TopToBottom));
            }
            chain.clear();
        };
        for (const std::size_t i : byColumn) {
            const Glyph& g = glyphs[i];
            const double width = g.x1 - g.x0;
            const double height = g.y1 - g.y0;
            const bool square = height > 0.0 && width >= kSquareRatio * height && !g.space && !besideAnother[i];
            if (!square) {
                closeChain();
                continue;
            }
            if (!chain.empty()) {
                const Glyph& last = glyphs[chain.back()];
                const double size = std::max(g.size, last.size);
                const double gap = last.y0 - g.y1;
                const bool below = std::abs(g.cx - last.cx) <= kVerticalColumnEm * size
                                   && gap >= -0.3 * size && gap <= kVerticalGapEm * size;
                if (!below) closeChain();
            }
            chain.push_back(i);
        }
        closeChain();
    }

    // --- 2 and 3. Bands, then lines ------------------------------------------------------------------
    std::vector<int> lineBand;   // parallel to layout.lines: the band a horizontal line came from
    lineBand.resize(layout.lines.size(), -1);
    int bandCount = 0;
    {
        std::vector<Glyph> rest;
        for (std::size_t i = 0; i < glyphs.size(); ++i)
            if (!taken[i]) rest.push_back(glyphs[i]);
        std::sort(rest.begin(), rest.end(), [](const Glyph& a, const Glyph& b) {
            return geometryLess(a, b, &Glyph::cy, true);
        });

        std::vector<std::vector<Glyph>> bands;
        double bandLow = 0.0, bandHigh = 0.0;
        for (const Glyph& g : rest) {
            bool joins = false;
            if (!bands.empty()) {
                const double overlap = std::min(g.y1, bandHigh) - std::max(g.y0, bandLow);
                const double smaller = std::min(g.y1 - g.y0, bandHigh - bandLow);
                joins = smaller > 0.0 ? overlap >= kBandOverlap * smaller
                                      : std::abs(g.cy - (bandLow + bandHigh) / 2.0) <= 0.5;
            }
            if (!joins) {
                bands.emplace_back();
                bandLow = g.y0;
                bandHigh = g.y1;
            } else {
                bandLow = std::min(bandLow, g.y0);
                bandHigh = std::max(bandHigh, g.y1);
            }
            bands.back().push_back(g);
        }

        for (auto& band : bands) {
            std::sort(band.begin(), band.end(), [](const Glyph& a, const Glyph& b) {
                return geometryLess(a, b, &Glyph::x0, false);
            });
            std::vector<Glyph> current;
            const auto closeLine = [&] {
                if (current.empty()) return;
                int rtl = 0, ltr = 0;
                for (const Glyph& g : current) {
                    const QChar::Direction d = QChar::direction(g.cp);
                    if (d == QChar::DirR || d == QChar::DirAL) ++rtl;
                    else if (d == QChar::DirL) ++ltr;
                }
                if (rtl > ltr) {
                    layout.lines.push_back(makeLine(toLogicalOrder(current), WritingDirection::RightToLeft));
                } else {
                    layout.lines.push_back(makeLine(current, WritingDirection::LeftToRight));
                }
                lineBand.push_back(bandCount);
                current.clear();
            };
            double reach = 0.0;
            for (const Glyph& g : band) {
                if (!current.empty()) {
                    const double gap = g.x0 - reach;
                    if (gap > kLineGapEm * std::max(g.size, current.back().size)) closeLine();
                }
                reach = current.empty() ? g.x1 : std::max(reach, g.x1);
                current.push_back(g);
            }
            closeLine();
            ++bandCount;
        }
    }
    const std::size_t lineCount = layout.lines.size();

    // --- 5. Tables ----------------------------------------------------------------------------------
    std::vector<int> lineTable(lineCount, -1);
    struct Found {
        PdfRect box;
        int rows, columns;
        bool ruled;
        std::vector<int> lines;
    };
    std::vector<Found> found;
    {
        // Ruled: horizontal rules grouped by their extent, crossed by vertical ones.
        std::vector<PdfRect> across, down;
        for (const PdfRect& rule : model.rules()) {
            if (!finite(rule.origin.x) || !finite(rule.origin.y) || !finite(rule.size.width)
                || !finite(rule.size.height))
                continue;
            if (rule.size.height <= kRuleMaxThickness) across.push_back(rule);
            else down.push_back(rule);
        }
        std::sort(across.begin(), across.end(), [](const PdfRect& a, const PdfRect& b) {
            return std::make_tuple(a.left(), a.right(), -a.top()) < std::make_tuple(b.left(), b.right(), -b.top());
        });
        std::vector<bool> used(across.size(), false);
        for (std::size_t i = 0; i < across.size(); ++i) {
            if (used[i]) continue;
            std::vector<double> ys;
            for (std::size_t j = i; j < across.size(); ++j) {
                if (std::abs(across[j].left() - across[i].left()) > kRuleSlack
                    || std::abs(across[j].right() - across[i].right()) > kRuleSlack)
                    continue;
                used[j] = true;
                const double y = (across[j].bottom() + across[j].top()) / 2.0;
                if (std::none_of(ys.begin(), ys.end(), [y](double seen) { return std::abs(seen - y) <= kRuleSlack; }))
                    ys.push_back(y);
            }
            if (ys.size() < 2) continue;
            const double low = *std::min_element(ys.begin(), ys.end());
            const double high = *std::max_element(ys.begin(), ys.end());
            const double left = across[i].left();
            const double right = across[i].right();
            std::vector<double> xs;
            for (const PdfRect& rule : down) {
                const double x = (rule.left() + rule.right()) / 2.0;
                if (x < left - kRuleSlack || x > right + kRuleSlack) continue;
                if (rule.bottom() > low + kRuleSlack || rule.top() < high - kRuleSlack) continue;
                if (std::none_of(xs.begin(), xs.end(), [x](double seen) { return std::abs(seen - x) <= kRuleSlack; }))
                    xs.push_back(x);
            }
            if (xs.size() < 2) continue;
            Found table{PdfRect{PdfPoint{left, low}, PdfSize{right - left, high - low}},
                        static_cast<int>(ys.size()) - 1, static_cast<int>(xs.size()) - 1, true, {}};
            for (std::size_t l = 0; l < lineCount; ++l) {
                const PdfRect& box = layout.lines[l].box;
                const double cx = (box.left() + box.right()) / 2.0;
                const double cy = (box.bottom() + box.top()) / 2.0;
                if (isHorizontal(layout.lines[l]) && lineTable[l] < 0 && table.box.contains(PdfPoint{cx, cy}))
                    table.lines.push_back(static_cast<int>(l));
            }
            if (table.lines.empty()) continue;
            for (const int l : table.lines) lineTable[static_cast<std::size_t>(l)] = static_cast<int>(found.size());
            found.push_back(table);
        }

        // Unruled: consecutive bands with the same number of short cells at the same left edges.
        std::map<int, std::vector<int>> bandLines;   // band -> its horizontal lines not in a table, left to right
        for (std::size_t l = 0; l < lineCount; ++l)
            if (lineBand[l] >= 0 && lineTable[l] < 0) bandLines[lineBand[l]].push_back(static_cast<int>(l));
        std::vector<std::vector<int>> run;
        std::vector<double> runEdges;
        const auto closeRun = [&] {
            if (static_cast<int>(run.size()) >= kTableMinRows) {
                std::vector<double> words;
                for (const auto& row : run)
                    for (const int l : row) words.push_back(wordsIn(model, layout.lines[static_cast<std::size_t>(l)]));
                if (median(words) <= kTableMaxWords) {
                    Found table{layout.lines[static_cast<std::size_t>(run.front().front())].box,
                                static_cast<int>(run.size()), static_cast<int>(runEdges.size()), false, {}};
                    for (const auto& row : run) {
                        for (const int l : row) {
                            table.box = unite(table.box, layout.lines[static_cast<std::size_t>(l)].box);
                            table.lines.push_back(l);
                            lineTable[static_cast<std::size_t>(l)] = static_cast<int>(found.size());
                        }
                    }
                    found.push_back(table);
                }
            }
            run.clear();
            runEdges.clear();
        };
        for (const auto& [band, members] : bandLines) {
            (void)band;
            if (members.size() < 2) {
                closeRun();
                continue;
            }
            std::vector<double> edges;
            for (const int l : members) edges.push_back(layout.lines[static_cast<std::size_t>(l)].box.left());
            bool continues = !run.empty() && edges.size() == runEdges.size();
            if (continues) {
                for (std::size_t k = 0; k < edges.size(); ++k)
                    if (std::abs(edges[k] - runEdges[k]) > kTableAlign) continues = false;
                const TextLine& above = layout.lines[static_cast<std::size_t>(run.back().front())];
                const TextLine& here = layout.lines[static_cast<std::size_t>(members.front())];
                if (above.baseline - here.baseline > kTableRowEm * std::max(above.fontSize, here.fontSize))
                    continues = false;
            }
            if (!continues) {
                closeRun();
                runEdges = edges;
            }
            run.push_back(members);
        }
        closeRun();
    }

    // --- 6. Blocks -----------------------------------------------------------------------------------
    std::vector<int> lineBlock(lineCount, -1);
    // Tables first: one block each, cells row by row.
    for (const Found& table : found) {
        TextBlock block;
        block.kind = BlockKind::Table;
        std::vector<int> cells = table.lines;
        std::sort(cells.begin(), cells.end(), [&](int a, int b) {
            const TextLine& la = layout.lines[static_cast<std::size_t>(a)];
            const TextLine& lb = layout.lines[static_cast<std::size_t>(b)];
            return std::make_tuple(lineBand[static_cast<std::size_t>(a)], la.box.left(), a)
                   < std::make_tuple(lineBand[static_cast<std::size_t>(b)], lb.box.left(), b);
        });
        block.lines = cells;
        block.box = table.box;
        for (const int l : cells) lineBlock[static_cast<std::size_t>(l)] = static_cast<int>(layout.blocks.size());
        layout.tables.push_back(TableGrid{static_cast<int>(layout.blocks.size()), table.box, table.rows,
                                          table.columns, table.ruled});
        layout.blocks.push_back(block);
    }

    // List markers and the line beside each -- looked for in the marker's own band only.
    std::vector<int> markerPartner(lineCount, -1);
    std::vector<bool> isPartner(lineCount, false);
    std::map<int, std::vector<int>> linesOfBand;
    for (std::size_t l = 0; l < lineCount; ++l)
        if (lineBand[l] >= 0) linesOfBand[lineBand[l]].push_back(static_cast<int>(l));
    for (std::size_t l = 0; l < lineCount; ++l) {
        if (lineBand[l] < 0 || lineTable[l] >= 0 || !isListMarker(model, layout.lines[l])) continue;
        int best = -1;
        for (const int other : linesOfBand[lineBand[l]]) {
            const std::size_t m = static_cast<std::size_t>(other);
            if (m == l || lineTable[m] >= 0) continue;
            const double gap = layout.lines[m].box.left() - layout.lines[l].box.right();
            if (gap < 0.0 || gap > 3.0 * layout.lines[l].fontSize) continue;
            if (best < 0 || layout.lines[m].box.left() < layout.lines[static_cast<std::size_t>(best)].box.left())
                best = static_cast<int>(m);
        }
        if (best >= 0 && !isPartner[static_cast<std::size_t>(best)]) {
            markerPartner[l] = best;
            isPartner[static_cast<std::size_t>(best)] = true;
        }
    }

    {
        // Horizontal text lines, top to bottom.
        std::vector<int> sequence;
        for (std::size_t l = 0; l < lineCount; ++l)
            if (isHorizontal(layout.lines[l]) && lineBlock[l] < 0) sequence.push_back(static_cast<int>(l));
        std::sort(sequence.begin(), sequence.end(), [&](int a, int b) {
            const TextLine& la = layout.lines[static_cast<std::size_t>(a)];
            const TextLine& lb = layout.lines[static_cast<std::size_t>(b)];
            return std::make_tuple(-la.baseline, la.box.left(), a) < std::make_tuple(-lb.baseline, lb.box.left(), b);
        });

        struct Open {
            int block;
            int last;              // the lowest line so far
            double leading = 0.0;  // 0 until the block has two rows
            int rows = 1;
        };
        std::deque<Open> recent;
        for (const int l : sequence) {
            const std::size_t li = static_cast<std::size_t>(l);
            const TextLine& line = layout.lines[li];
            if (isPartner[li]) continue;   // placed with its marker, below

            Open* best = nullptr;
            double bestSpacing = 0.0;
            if (markerPartner[li] < 0) {
                for (Open& open : recent) {
                    const TextLine& last = layout.lines[static_cast<std::size_t>(open.last)];
                    const double spacing = last.baseline - line.baseline;
                    if (spacing <= 0.0) continue;
                    const double overlap = horizontalOverlap(last.box, line.box);
                    const double narrower = std::min(last.box.size.width, line.box.size.width);
                    if (!(overlap > 0.0 && overlap >= 0.5 * narrower)) continue;
                    bool joins = false;
                    if (open.rows >= 2) {
                        joins = spacing <= kLeadingRatio * open.leading && spacing >= open.leading / kLeadingRatio;
                    } else {
                        const double larger = std::max(last.fontSize, line.fontSize);
                        const double smaller = std::min(last.fontSize, line.fontSize);
                        joins = spacing <= kFirstPairEm * larger && smaller >= 0.9 * larger;
                    }
                    if (joins && (best == nullptr || spacing < bestSpacing)) {
                        best = &open;
                        bestSpacing = spacing;
                    }
                }
            }
            if (best != nullptr) {
                TextBlock& block = layout.blocks[static_cast<std::size_t>(best->block)];
                block.lines.push_back(l);
                block.box = unite(block.box, line.box);
                lineBlock[li] = best->block;
                if (best->rows == 1) best->leading = bestSpacing;
                ++best->rows;
                best->last = l;
                continue;
            }
            TextBlock block;
            block.lines.push_back(l);
            block.box = line.box;
            lineBlock[li] = static_cast<int>(layout.blocks.size());
            Open open{static_cast<int>(layout.blocks.size()), l};
            if (markerPartner[li] >= 0) {
                const int partner = markerPartner[li];
                block.lines.push_back(partner);
                block.box = unite(block.box, layout.lines[static_cast<std::size_t>(partner)].box);
                lineBlock[static_cast<std::size_t>(partner)] = open.block;
                open.last = partner;
            }
            layout.blocks.push_back(block);
            recent.push_back(open);
            if (recent.size() > kRecentBlocks) recent.pop_front();
        }
        // A partner whose marker was a table cell or otherwise unplaced gets a block of its own.
        for (std::size_t l = 0; l < lineCount; ++l) {
            if (lineBlock[l] >= 0) continue;
            if (!isHorizontal(layout.lines[l])) continue;
            TextBlock block;
            block.lines.push_back(static_cast<int>(l));
            block.box = layout.lines[l].box;
            lineBlock[l] = static_cast<int>(layout.blocks.size());
            layout.blocks.push_back(block);
        }

        // Vertical lines: side by side, right to left.
        std::vector<int> verticals;
        for (std::size_t l = 0; l < lineCount; ++l)
            if (!isHorizontal(layout.lines[l])) verticals.push_back(static_cast<int>(l));
        std::sort(verticals.begin(), verticals.end(), [&](int a, int b) {
            const PdfRect& ba = layout.lines[static_cast<std::size_t>(a)].box;
            const PdfRect& bb = layout.lines[static_cast<std::size_t>(b)].box;
            return std::make_tuple(-ba.right(), -ba.top(), a) < std::make_tuple(-bb.right(), -bb.top(), b);
        });
        int openVertical = -1;
        for (const int l : verticals) {
            const TextLine& line = layout.lines[static_cast<std::size_t>(l)];
            if (openVertical >= 0) {
                TextBlock& block = layout.blocks[static_cast<std::size_t>(openVertical)];
                const TextLine& last = layout.lines[static_cast<std::size_t>(block.lines.back())];
                const double gap = last.box.left() - line.box.right();
                const double size = std::max(last.fontSize, line.fontSize);
                if (gap >= -0.2 * size && gap <= 1.5 * size && std::abs(last.box.top() - line.box.top()) <= size) {
                    block.lines.push_back(l);
                    block.box = unite(block.box, line.box);
                    lineBlock[static_cast<std::size_t>(l)] = openVertical;
                    continue;
                }
            }
            TextBlock block;
            block.lines.push_back(l);
            block.box = line.box;
            openVertical = static_cast<int>(layout.blocks.size());
            lineBlock[static_cast<std::size_t>(l)] = openVertical;
            layout.blocks.push_back(block);
        }
    }

    // --- 7. Reading order ----------------------------------------------------------------------------
    std::vector<double> sizes;
    for (const TextLine& line : layout.lines) sizes.push_back(line.fontSize);
    const double threshold = kCutEm * std::max(median(sizes), 1e-6);
    std::vector<int> all(layout.blocks.size());
    for (std::size_t b = 0; b < all.size(); ++b) all[b] = static_cast<int>(b);
    orderBlocks(all, layout, threshold, 0, layout.order);

    // --- 8. Columns ----------------------------------------------------------------------------------
    if (!layout.lines.empty()) {
        double top = layout.lines.front().box.top();
        double bottom = layout.lines.front().box.bottom();
        for (const TextLine& line : layout.lines) {
            top = std::max(top, line.box.top());
            bottom = std::min(bottom, line.box.bottom());
        }
        const double band = 0.1 * (top - bottom);
        const auto furniture = [&](const TextLine& line) {
            const double cy = (line.box.bottom() + line.box.top()) / 2.0;
            return cy > top - band || cy < bottom + band;
        };
        std::size_t verticalChars = 0, horizontalChars = 0, horizontalLines = 0;
        for (const TextLine& line : layout.lines) {
            if (furniture(line)) continue;
            if (isHorizontal(line)) {
                horizontalChars += line.chars.size();
                ++horizontalLines;
            } else {
                verticalChars += line.chars.size();
            }
        }
        if (verticalChars > horizontalChars) {
            std::size_t most = 0;
            for (const TextBlock& block : layout.blocks)
                if (!block.lines.empty() && !isHorizontal(layout.lines[static_cast<std::size_t>(block.lines.front())]))
                    most = std::max(most, block.lines.size());
            layout.columns = static_cast<int>(most);
        } else {
            layout.columns = 1;
            const std::vector<std::vector<int>> bands = cutGroups(all, layout.blocks, true, threshold);
            for (const auto& group : bands) {
                std::size_t groupLines = 0;
                for (const int b : group)
                    for (const int l : layout.blocks[static_cast<std::size_t>(b)].lines)
                        if (!furniture(layout.lines[static_cast<std::size_t>(l)])) ++groupLines;
                if (horizontalLines == 0 || groupLines * 4 < horizontalLines) continue;
                const int side = static_cast<int>(cutGroups(group, layout.blocks, false, threshold).size());
                layout.columns = std::max(layout.columns, side);
            }
        }
    }
    return layout;
}

std::vector<ReadingItem> LayoutAnalyzer::readingStream(const PageLayout& layout)
{
    std::vector<ReadingItem> stream;
    bool firstBlock = true;
    for (const int b : layout.order) {
        const TextBlock& block = layout.blocks[static_cast<std::size_t>(b)];
        if (!firstBlock) stream.push_back({ReadingItem::Kind::BlockBreak, -1});
        firstBlock = false;
        bool firstLine = true;
        for (const int l : block.lines) {
            const TextLine& line = layout.lines[static_cast<std::size_t>(l)];
            if (!firstLine) stream.push_back({ReadingItem::Kind::LineBreak, -1});
            firstLine = false;
            for (std::size_t i = 0; i < line.chars.size(); ++i) {
                if (line.gapBefore[i]) stream.push_back({ReadingItem::Kind::WordGap, -1});
                stream.push_back({ReadingItem::Kind::Char, line.chars[i]});
            }
        }
    }
    return stream;
}

QString LayoutAnalyzer::readingText(const PageTextModel& model, const PageLayout& layout)
{
    QString out;
    for (const ReadingItem& item : readingStream(layout)) {
        switch (item.kind) {
        case ReadingItem::Kind::Char: {
            const char32_t scalar = model.chars()[static_cast<std::size_t>(item.charIndex)].codepoint;
            out += QString::fromUcs4(&scalar, 1);
            break;
        }
        case ReadingItem::Kind::WordGap: out += QLatin1Char(' '); break;
        case ReadingItem::Kind::LineBreak: out += QLatin1Char('\n'); break;
        case ReadingItem::Kind::BlockBreak: out += QLatin1String("\n\n"); break;
        }
    }
    return out;
}

} // namespace rawpage::core::text
