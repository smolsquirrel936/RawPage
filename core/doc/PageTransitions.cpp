#include "core/doc/PageTransitions.h"

#include <algorithm>

namespace rawpage::core {

void PageTransitions::setPageCount(int count)
{
    const std::size_t size = count > 0 ? static_cast<std::size_t>(count) : 0;
    transitions_.assign(size, std::nullopt);
    timings_.assign(size, std::nullopt);
}

void PageTransitions::setTransition(int pageIndex, const PageTransition& transition)
{
    if (pageIndex < 0 || static_cast<std::size_t>(pageIndex) >= transitions_.size()) return;
    transitions_[static_cast<std::size_t>(pageIndex)] = transition;
}

void PageTransitions::setTiming(int pageIndex, const PageTiming& timing)
{
    if (pageIndex < 0 || static_cast<std::size_t>(pageIndex) >= timings_.size()) return;
    timings_[static_cast<std::size_t>(pageIndex)] = timing;
}

const PageTransition* PageTransitions::at(int pageIndex) const noexcept
{
    if (pageIndex < 0 || static_cast<std::size_t>(pageIndex) >= transitions_.size()) return nullptr;
    const auto& entry = transitions_[static_cast<std::size_t>(pageIndex)];
    return entry ? &*entry : nullptr;
}

const PageTiming* PageTransitions::timingAt(int pageIndex) const noexcept
{
    if (pageIndex < 0 || static_cast<std::size_t>(pageIndex) >= timings_.size()) return nullptr;
    const auto& entry = timings_[static_cast<std::size_t>(pageIndex)];
    return entry ? &*entry : nullptr;
}

int PageTransitions::unsupportedCount() const noexcept
{
    return static_cast<int>(std::count_if(
        transitions_.begin(), transitions_.end(),
        [](const std::optional<PageTransition>& entry) { return entry && !entry->supported; }));
}

bool PageTransitions::isEmpty() const noexcept
{
    // EMPTY MEANS THE DOCUMENT ASKED FOR NOTHING, not that it has no pages. A file with 900 pages
    // and no `/Trans`, no `/Dur` and no `/PageMode` is empty here, and that is what lets the
    // controller skip the model entirely for the overwhelming majority of documents.
    if (opensFullScreen_) return false;
    const auto present = [](const auto& entry) { return entry.has_value(); };
    return std::none_of(transitions_.begin(), transitions_.end(), present)
           && std::none_of(timings_.begin(), timings_.end(), present);
}

} // namespace rawpage::core
