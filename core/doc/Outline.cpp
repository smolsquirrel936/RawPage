#include "core/doc/Outline.h"

#include <algorithm>

namespace rawpage::core {
namespace {

std::size_t countIn(const std::vector<OutlineItem>& items)
{
    std::size_t total = items.size();
    for (const OutlineItem& item : items) total += countIn(item.children);
    return total;
}

int depthOf(const std::vector<OutlineItem>& items)
{
    int deepest = 0;
    for (const OutlineItem& item : items) deepest = std::max(deepest, depthOf(item.children));
    return items.empty() ? 0 : deepest + 1;
}

} // namespace

std::size_t Outline::nodeCount() const { return countIn(roots_); }

int Outline::depth() const { return depthOf(roots_); }

} // namespace rawpage::core
