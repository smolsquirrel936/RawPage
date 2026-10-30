#include "core/doc/PageTree.h"

#include "core/doc/PageMap.h"

#include <utility>

namespace rawpage::core {

PageTree::PageTree(std::vector<Page> pages) : pages_(std::move(pages))
{
    // The index a page carries must agree with its position, because both are used: `TileGrid`
    // takes an index and a `Page`, and a page whose own `index()` disagreed with where it sits
    // would produce tiles attributed to the wrong page. Renumbered here rather than trusted, so a
    // caller building a tree from a filtered list cannot get it wrong.
    for (std::size_t at = 0; at < pages_.size(); ++at)
        pages_[at].setIndex(static_cast<int>(at));
}

const Page* PageTree::at(int index) const noexcept
{
    if (index < 0 || static_cast<std::size_t>(index) >= pages_.size())
        return nullptr;
    return &pages_[static_cast<std::size_t>(index)];
}

int PageTree::indexOfLabel(const QString& label) const
{
    if (label.isEmpty())
        return -1;
    for (std::size_t at = 0; at < pages_.size(); ++at) {
        if (pages_[at].label() == label)
            return static_cast<int>(at);
    }
    return -1;
}

PdfSize PageTree::displaySize(int index) const noexcept
{
    const Page* page = at(index);
    return page ? page->displaySize() : PdfSize{};
}

PageRotation PageTree::rotation(int index) const noexcept
{
    const Page* page = at(index);
    return page ? page->rotation() : PageRotation::None;
}

bool PageTree::setRotation(int index, int degrees) noexcept
{
    if (index < 0 || index >= count())
        return false;
    pages_[static_cast<std::size_t>(index)].setRotation(degrees);
    return true;
}

bool PageTree::apply(const PageMap& map, std::vector<Page> inserted)
{
    if (map.oldCount() != count() || map.newCount() == 0) return false;
    if (inserted.size() != map.inserted().size()) return false;

    std::vector<Page> rebuilt;
    rebuilt.reserve(static_cast<std::size_t>(map.newCount()));
    std::size_t next = 0;
    for (int now = 0; now < map.newCount(); ++now) {
        const int old = map.oldIndexOf(now);
        rebuilt.push_back(old == PageMap::kNone ? std::move(inserted[next++]) : pages_[static_cast<std::size_t>(old)]);
        rebuilt.back().setIndex(now);
    }
    pages_ = std::move(rebuilt);
    return true;
}

std::vector<Page> PageTree::removedBy(const PageMap& map) const
{
    std::vector<Page> out;
    if (map.oldCount() != count()) return out;
    for (const int old : map.removed()) out.push_back(pages_[static_cast<std::size_t>(old)]);
    return out;
}

PdfSize PageTree::largestDisplaySize() const noexcept
{
    // Widest and tallest INDEPENDENTLY, not the largest single page. A document with one very wide
    // page and one very tall page needs a viewport that fits both dimensions; returning whichever
    // page has the greatest area would clip the other one, and clipping is what a continuous
    // scroll view cannot recover from without relaying out mid-scroll.
    PdfSize largest{};
    for (const Page& page : pages_) {
        const PdfSize size = page.displaySize();
        if (size.width > largest.width)
            largest.width = size.width;
        if (size.height > largest.height)
            largest.height = size.height;
    }
    return largest;
}

} // namespace rawpage::core
