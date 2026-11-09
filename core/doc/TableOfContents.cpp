#include "core/doc/TableOfContents.h"

#include "core/doc/PageTree.h"

namespace rawpage::core {
namespace {

void collect(const std::vector<OutlineItem>& items, int depth, const PageTree& pages, std::vector<TableOfContentsEntry>& into)
{
    for (const OutlineItem& item : items) {
        const bool here = item.action.kind == OutlineAction::Kind::GoTo || item.action.kind == OutlineAction::Kind::GoToNamed;
        if (here && item.destination.isResolved() && item.destination.pageIndex < pages.count()) {
            TableOfContentsEntry entry;
            entry.title = item.title;
            entry.depth = depth;
            entry.destination = item.destination;
            const Page* page = pages.at(item.destination.pageIndex);
            entry.label = page != nullptr && !page->label().isEmpty() ? page->label()
                                                                       : QString::number(item.destination.pageIndex + 1);
            into.push_back(std::move(entry));
        }
        collect(item.children, depth + 1, pages, into);
    }
}

} // namespace

TableOfContents tableOfContentsFrom(const Outline& outline, const PageTree& pages, const QString& heading)
{
    TableOfContents out;
    out.heading = heading;
    collect(outline.roots(), 0, pages, out.entries);
    if (const Page* first = pages.at(0)) out.pageSize = first->displaySize();
    return out;
}

} // namespace rawpage::core
