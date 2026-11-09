#include "core/doc/OutlineGenerator.h"

#include "core/cmd/commands/BookmarkCommands.h"
#include "core/text/LayoutAnalyzer.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <map>

namespace rawpage::core {
namespace {

// Sizes a producer writes as 13 and 13.0001 are one size; half a point apart are two.
double rounded(double size)
{
    return std::round(size * 2.0) / 2.0;
}

// Levels by size, largest first: the distinct sizes of the chosen lines, at most `maxLevels` of them.
void levelBySize(std::vector<HeadingLine>& chosen, int maxLevels)
{
    std::vector<double> sizes;
    for (const HeadingLine& line : chosen) sizes.push_back(rounded(line.size));
    std::sort(sizes.begin(), sizes.end(), std::greater<>());
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    for (HeadingLine& line : chosen) {
        const auto at = std::find(sizes.begin(), sizes.end(), rounded(line.size));
        line.level = std::min(static_cast<int>(at - sizes.begin()) + 1, std::max(1, maxLevels));
    }
}

} // namespace

std::vector<HeadingLine> OutlineGenerator::linesOf(int page, const text::PageTextModel& model)
{
    std::vector<HeadingLine> out;
    const text::PageLayout layout = text::LayoutAnalyzer::analyze(model, nullptr);
    const std::vector<text::TextChar>& chars = model.chars();
    for (const int blockIndex : layout.order) {
        const text::TextBlock& block = layout.blocks[static_cast<std::size_t>(blockIndex)];
        for (const int lineIndex : block.lines) {
            const text::TextLine& line = layout.lines[static_cast<std::size_t>(lineIndex)];
            HeadingLine heading;
            heading.page = page;
            heading.size = line.fontSize;
            heading.left = line.box.left();
            heading.top = line.box.top();
            int bold = 0;
            int counted = 0;
            for (std::size_t i = 0; i < line.chars.size(); ++i) {
                const text::TextChar& c = chars[static_cast<std::size_t>(line.chars[i])];
                if (i < line.gapBefore.size() && line.gapBefore[i] && !heading.text.isEmpty()) heading.text += QLatin1Char(' ');
                if (c.isUnmapped()) continue;
                heading.text += QString::fromUcs4(&c.codepoint, 1);
                if (c.isGenerated()) continue;
                ++counted;
                if (const text::FontIdentity* font = model.fontOf(c); font != nullptr && font->isBold()) ++bold;
            }
            heading.text = heading.text.simplified();
            heading.bold = counted > 0 && bold * 2 > counted;
            if (!heading.text.isEmpty()) out.push_back(std::move(heading));
        }
    }
    return out;
}

double OutlineGenerator::bodySize(const std::vector<HeadingLine>& lines)
{
    std::map<double, qsizetype> weight;
    for (const HeadingLine& line : lines) weight[rounded(line.size)] += line.text.size();
    double body = 0.0;
    qsizetype most = -1;
    for (const auto& [size, count] : weight) {
        if (count > most) {
            most = count;
            body = size;
        }
    }
    return body;
}

Result<std::vector<HeadingLine>> OutlineGenerator::select(const std::vector<HeadingLine>& lines, const OutlineGeneratorOptions& options)
{
    std::vector<HeadingLine> chosen;
    const double body = bodySize(lines);
    switch (options.mode) {
    case OutlineGeneratorOptions::Mode::Headings:
        for (const HeadingLine& line : lines) {
            if (rounded(line.size) > body) chosen.push_back(line);
        }
        levelBySize(chosen, options.maxLevels);
        break;
    case OutlineGeneratorOptions::Mode::Fonts:
        for (const HeadingLine& line : lines) {
            const bool large = options.minimumSize > 0.0 ? rounded(line.size) >= rounded(options.minimumSize) : rounded(line.size) > body;
            if (large && (!options.boldOnly || line.bold)) chosen.push_back(line);
        }
        levelBySize(chosen, options.maxLevels);
        break;
    case OutlineGeneratorOptions::Mode::Pattern: {
        const QRegularExpression pattern(options.pattern);
        if (options.pattern.isEmpty() || !pattern.isValid())
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.badPattern"), options.pattern};
        for (const HeadingLine& line : lines) {
            if (pattern.match(line.text).hasMatch()) {
                chosen.push_back(line);
                chosen.back().level = 1;
            }
        }
        break;
    }
    }
    if (chosen.empty()) return Error{ErrorCode::NotFound, QLatin1String("error.outline.noHeadings"), QString()};
    if (chosen.size() > kMaxOutlineNodes)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.tooLarge"), QString::number(chosen.size())};
    return chosen;
}

std::vector<OutlineItem> OutlineGenerator::tree(const std::vector<HeadingLine>& headings, const QString& key)
{
    std::vector<OutlineItem> roots;
    // The open chain: each level's latest item, as a path from the top -- a heading goes under the deepest one
    // whose level is smaller than its own.
    std::vector<std::pair<int, std::vector<OutlineItem>*>> chain;
    int ordinal = 0;
    for (const HeadingLine& heading : headings) {
        OutlineItem item;
        item.id = newBookmarkId(key, ordinal++);
        item.title = heading.text.left(kMaxOutlineTitleLength);
        Destination at;
        at.pageIndex = heading.page;
        at.fit = Destination::Fit::XYZ;
        at.hasX = true;
        at.x = heading.left;
        at.hasY = true;
        at.y = heading.top;
        item.action = OutlineAction::goTo(at);
        item.destination = at;
        const int level = std::clamp(heading.level, 1, kMaxOutlineDepth);
        while (!chain.empty() && chain.back().first >= level) chain.pop_back();
        std::vector<OutlineItem>* into = chain.empty() ? &roots : chain.back().second;
        if (!chain.empty()) {
            // The parent shows its new child: the file asks for a heading with children to open.
            into->back().openByDefault = true;
        }
        into = chain.empty() ? &roots : &chain.back().second->back().children;
        into->push_back(std::move(item));
        chain.emplace_back(level, into);
    }
    return roots;
}

} // namespace rawpage::core
