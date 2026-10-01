#include "core/text/ReflowText.h"

#include <algorithm>

namespace rawpage::core::text {
namespace {

QString scalarOf(char32_t cp)
{
    return QString::fromUcs4(&cp, 1);
}

// Appends to the paragraph's last run when the style is the same, so a paragraph of one face is one run.
void append(ReflowParagraph& paragraph, const ReflowRun& style, const QString& text)
{
    // A DRAWN SPACE is one space, wherever the separators already put one -- and never the first thing in
    // a paragraph, which re-wrapping would show as an indent nobody drew.
    if (text == QStringLiteral(" ")) {
        if (paragraph.runs.empty() || paragraph.runs.back().text.endsWith(QLatin1Char(' '))) return;
    }
    if (!paragraph.runs.empty()) {
        ReflowRun& last = paragraph.runs.back();
        if (last.size == style.size && last.bold == style.bold && last.italic == style.italic) {
            last.text += text;
            return;
        }
    }
    ReflowRun run = style;
    run.text = text;
    paragraph.runs.push_back(std::move(run));
}

// A separator goes on the run already there: a space carries no style of its own.
void appendSeparator(ReflowParagraph& paragraph)
{
    if (paragraph.runs.empty()) return;
    QString& text = paragraph.runs.back().text;
    if (!text.endsWith(QLatin1Char(' '))) text += QLatin1Char(' ');
}

void dropTrailingHyphen(ReflowParagraph& paragraph)
{
    if (paragraph.runs.empty()) return;
    QString& text = paragraph.runs.back().text;
    if (text.endsWith(QLatin1Char('-'))) text.chop(1);
}

ReflowRun styleOf(const PageTextModel& model, const TextChar& c, const TextLine& line)
{
    ReflowRun style;
    style.size = c.fontSize > 0.0 ? c.fontSize : line.fontSize;
    if (const FontIdentity* font = model.fontOf(c)) {
        style.bold = font->isBold();
        style.italic = font->isItalic();
    }
    return style;
}

} // namespace

QString ReflowParagraph::text() const
{
    QString out;
    for (const ReflowRun& run : runs) out += run.text;
    return out.trimmed();
}

ReflowPage reflowParagraphs(const PageReading& page)
{
    ReflowPage out;
    const PageLayout& layout = page.layout();

    ReflowParagraph current;
    bool open = false;
    bool pendingSpace = false;       // a word gap or a joined line since the last character
    bool lastWasLineHyphen = false;  // the last written character is a line-final hyphen (D5-5)
    int lastLine = -1;

    const auto close = [&] {
        if (open && !current.text().isEmpty()) {
            // A trailing separator is nobody's: trim it off the last run.
            if (!current.runs.empty()) {
                QString& tail = current.runs.back().text;
                while (tail.endsWith(QLatin1Char(' '))) tail.chop(1);
            }
            out.paragraphs.push_back(std::move(current));
        }
        current = ReflowParagraph{};
        open = false;
        pendingSpace = false;
        lastWasLineHyphen = false;
    };

    for (int p = 0; p < page.size(); ++p) {
        const int block = page.blockOf(p);
        const int line = page.lineOf(p);
        const TextBlock& textBlock = layout.blocks[static_cast<std::size_t>(block)];
        const TextLine& textLine = layout.lines[static_cast<std::size_t>(line)];
        const bool table = textBlock.kind == BlockKind::Table;

        // A NEW PARAGRAPH: a new block -- or, in a table, a new line.
        const bool newParagraph = !open || current.block != block || (table && line != lastLine);
        const ReadingItem::Kind before = page.breakBefore(p);
        if (newParagraph) {
            close();
            current.page = page.pageIndex();
            current.block = block;
            current.tableRow = table;
            current.rightToLeft = textLine.direction == WritingDirection::RightToLeft;
            open = true;
        } else if (before == ReadingItem::Kind::LineBreak || before == ReadingItem::Kind::BlockBreak) {
            // A LINE BREAK INSIDE A BLOCK -- D5-5's hyphen rule, decided once the next character is known.
            if (!lastWasLineHyphen) pendingSpace = true;
        } else if (before == ReadingItem::Kind::WordGap) {
            pendingSpace = true;
        }
        lastLine = line;

        const TextChar& c = page.charAt(p);
        if (c.isUnmapped()) {
            ++out.unmappedSkipped;
            pendingSpace = true;
            lastWasLineHyphen = false;
            continue;
        }

        if (lastWasLineHyphen && (before == ReadingItem::Kind::LineBreak || before == ReadingItem::Kind::BlockBreak)) {
            // `exam-` / `ple` joins; `North-` / `West` keeps its hyphen and takes no space.
            if (QChar::isLower(c.codepoint)) dropTrailingHyphen(current);
        } else if (pendingSpace) {
            appendSeparator(current);
        }
        pendingSpace = false;

        append(current, styleOf(page.model(), c, textLine), scalarOf(c.codepoint));
        // THE ENGINE'S FLAG IS THE EVIDENCE (D5-5): an unflagged hyphen at a line end is text.
        lastWasLineHyphen = c.isHyphen();
    }
    close();
    return out;
}

QString reflowText(const std::vector<ReflowParagraph>& paragraphs)
{
    QString out;
    for (const ReflowParagraph& paragraph : paragraphs) {
        if (!out.isEmpty()) out += QStringLiteral("\n\n");
        out += paragraph.text();
    }
    return out;
}

int blockAtTop(const PageReading& page, const DepthBelowEdge& depth)
{
    const PageLayout& layout = page.layout();
    for (const int block : layout.order) {
        if (block < 0 || static_cast<std::size_t>(block) >= layout.blocks.size()) continue;
        const PdfRect& box = layout.blocks[static_cast<std::size_t>(block)].box;
        // NOT WHOLLY ABOVE the top edge: some corner of it is below where the reader's eye starts. The
        // corner that is lowest on SCREEN, which is the page's bottom-most only on an upright page (I-06).
        const double lowest = std::max({depth.of(PdfPoint{box.left(), box.bottom()}),
                                        depth.of(PdfPoint{box.right(), box.bottom()}),
                                        depth.of(PdfPoint{box.left(), box.top()}),
                                        depth.of(PdfPoint{box.right(), box.top()})});
        if (lowest > 0.0) return block;
    }
    return -1;
}

int blockAtTop(const PageReading& page, double topY)
{
    return blockAtTop(page, DepthBelowEdge::upright(topY));
}

} // namespace rawpage::core::text
