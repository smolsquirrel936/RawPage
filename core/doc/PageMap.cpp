// core/doc/PageMap -- implementation. Stage 7 T2. See the header for what a map means.

#include "core/doc/PageMap.h"

#include <algorithm>

namespace rawpage::core {

PageMap PageMap::identity(int count)
{
    PageMap map;
    map.oldCount_ = std::max(0, count);
    map.order_.resize(static_cast<std::size_t>(map.oldCount_));
    for (int index = 0; index < map.oldCount_; ++index) map.order_[static_cast<std::size_t>(index)] = index;
    map.forward_ = map.order_;
    return map;
}

std::optional<PageMap> PageMap::fromOrder(int oldCount, std::vector<int> order)
{
    if (oldCount < 0) return std::nullopt;
    PageMap map;
    map.oldCount_ = oldCount;
    map.forward_.assign(static_cast<std::size_t>(oldCount), kNone);
    for (std::size_t at = 0; at < order.size(); ++at) {
        const int old = order[at];
        if (old == kNone) continue;
        if (old < 0 || old >= oldCount || map.forward_[static_cast<std::size_t>(old)] != kNone) return std::nullopt;
        map.forward_[static_cast<std::size_t>(old)] = static_cast<int>(at);
    }
    map.order_ = std::move(order);
    return map;
}

bool PageMap::isPageSet(int count, const std::vector<int>& pages)
{
    if (pages.empty()) return false;
    for (std::size_t at = 0; at < pages.size(); ++at) {
        if (pages[at] < 0 || pages[at] >= count) return false;
        if (at > 0 && pages[at] <= pages[at - 1]) return false;
    }
    return true;
}

std::optional<PageMap> PageMap::move(int count, const std::vector<int>& pages, int to)
{
    if (!isPageSet(count, pages)) return std::nullopt;
    const int moved = static_cast<int>(pages.size());
    if (to < 0 || to > count - moved) return std::nullopt;

    // The pages that stay, in order, with the moved block put in at `to`.
    std::vector<int> order;
    order.reserve(static_cast<std::size_t>(count));
    std::size_t next = 0;
    for (int old = 0; old < count; ++old) {
        if (next < pages.size() && pages[next] == old) {
            ++next;
            continue;
        }
        order.push_back(old);
    }
    order.insert(order.begin() + to, pages.begin(), pages.end());
    return fromOrder(count, std::move(order));
}

std::optional<PageMap> PageMap::removal(int count, const std::vector<int>& pages)
{
    if (!isPageSet(count, pages)) return std::nullopt;
    std::vector<int> order;
    order.reserve(static_cast<std::size_t>(count) - pages.size());
    std::size_t next = 0;
    for (int old = 0; old < count; ++old) {
        if (next < pages.size() && pages[next] == old) {
            ++next;
            continue;
        }
        order.push_back(old);
    }
    return fromOrder(count, std::move(order));
}

std::optional<PageMap> PageMap::duplication(int count, const std::vector<int>& pages)
{
    if (!isPageSet(count, pages)) return std::nullopt;
    std::vector<int> order;
    order.reserve(static_cast<std::size_t>(count) + pages.size());
    std::size_t next = 0;
    for (int old = 0; old < count; ++old) {
        order.push_back(old);
        if (next < pages.size() && pages[next] == old) {
            order.push_back(kNone);   // the copy, right after its original
            ++next;
        }
    }
    return fromOrder(count, std::move(order));
}

int PageMap::newIndexOf(int oldIndex) const noexcept
{
    if (oldIndex < 0 || oldIndex >= oldCount_) return kNone;
    return forward_[static_cast<std::size_t>(oldIndex)];
}

int PageMap::oldIndexOf(int newIndex) const noexcept
{
    if (newIndex < 0 || newIndex >= newCount()) return kNone;
    return order_[static_cast<std::size_t>(newIndex)];
}

std::vector<int> PageMap::removed() const
{
    std::vector<int> out;
    for (int old = 0; old < oldCount_; ++old) {
        if (forward_[static_cast<std::size_t>(old)] == kNone) out.push_back(old);
    }
    return out;
}

std::vector<int> PageMap::inserted() const
{
    std::vector<int> out;
    for (int now = 0; now < newCount(); ++now) {
        if (order_[static_cast<std::size_t>(now)] == kNone) out.push_back(now);
    }
    return out;
}

bool PageMap::isIdentity() const noexcept
{
    if (newCount() != oldCount_) return false;
    for (int index = 0; index < oldCount_; ++index) {
        if (order_[static_cast<std::size_t>(index)] != index) return false;
    }
    return true;
}

bool PageMap::isPermutation() const noexcept
{
    if (newCount() != oldCount_) return false;
    return std::none_of(order_.begin(), order_.end(), [](int old) { return old == kNone; });
}

PageMap PageMap::inverse() const
{
    PageMap map;
    map.oldCount_ = newCount();
    map.order_ = forward_;
    map.forward_ = order_;
    return map;
}

std::optional<PageMap> PageMap::then(const PageMap& next) const
{
    if (next.oldCount() != newCount()) return std::nullopt;
    std::vector<int> order(static_cast<std::size_t>(next.newCount()), kNone);
    for (int now = 0; now < next.newCount(); ++now) {
        const int middle = next.oldIndexOf(now);
        order[static_cast<std::size_t>(now)] = middle == kNone ? kNone : oldIndexOf(middle);
    }
    return fromOrder(oldCount_, std::move(order));
}

const char* pageReferenceKindName(PageReference::Kind kind) noexcept
{
    switch (kind) {
    case PageReference::Kind::OutlineItem: return "OutlineItem";
    case PageReference::Kind::Link: return "Link";
    case PageReference::Kind::NamedDestination: return "NamedDestination";
    case PageReference::Kind::StructureElement: return "StructureElement";
    case PageReference::Kind::ThreadBead: return "ThreadBead";
    case PageReference::Kind::OpenAction: return "OpenAction";
    case PageReference::Kind::RemoteGoTo: return "RemoteGoTo";
    case PageReference::Kind::IndexDestination: return "IndexDestination";
    case PageReference::Kind::WidgetNotCopied: return "WidgetNotCopied";
    }
    return "?";
}

const char* pageReferenceTreatmentName(PageReference::Treatment treatment) noexcept
{
    switch (treatment) {
    case PageReference::Treatment::Cleared: return "Cleared";
    case PageReference::Treatment::Removed: return "Removed";
    case PageReference::Treatment::Unlinked: return "Unlinked";
    case PageReference::Treatment::Reported: return "Reported";
    case PageReference::Treatment::NotCopied: return "NotCopied";
    }
    return "?";
}

} // namespace rawpage::core
