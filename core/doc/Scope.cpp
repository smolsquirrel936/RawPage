#include "core/doc/Scope.h"

#include <algorithm>
#include <utility>

namespace rawpage::core {
namespace {

template <class T>
void sortUnique(std::vector<T>& values)
{
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

} // namespace

Scope Scope::nothing()
{
    return Scope{};
}

Scope Scope::wholeDocument()
{
    Scope scope;
    scope.kind_ = Kind::WholeDocument;
    return scope;
}

Scope Scope::pageCommand(PageMap map)
{
    Scope scope;
    scope.kind_ = Kind::WholeDocument;
    scope.map_ = std::move(map);
    return scope;
}

Scope Scope::reversed() const
{
    Scope scope = *this;
    if (scope.map_) scope.map_ = scope.map_->inverse();
    return scope;
}

Scope Scope::page(int index)
{
    return pages({index});
}

Scope Scope::pages(std::vector<int> indices)
{
    // An EMPTY page set is `Nothing`, not an empty `Pages`. Otherwise a caller building a scope
    // from a filtered list gets a scope that claims to be about pages and names none, and
    // `touchesPage()` answers false for every page -- which is the narrowing this class forbids
    // arriving by accident.
    if (indices.empty())
        return Scope{};

    Scope scope;
    scope.kind_ = Kind::Pages;
    scope.pages_ = std::move(indices);
    sortUnique(scope.pages_);
    return scope;
}

Scope Scope::annotationsOf(std::vector<int> indices)
{
    Scope scope = pages(std::move(indices));
    if (scope.kind_ == Kind::Pages) scope.aspect_ = Aspect::Annotations;
    return scope;
}

Scope Scope::annotationRegions(std::vector<std::pair<int, PdfRect>> regions)
{
    std::vector<int> indices;
    indices.reserve(regions.size());
    for (const auto& region : regions) indices.push_back(region.first);
    Scope scope = annotationsOf(std::move(indices));
    if (scope.kind_ != Kind::Pages) return scope;
    std::stable_sort(regions.begin(), regions.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [page, rect] : regions) {
        if (scope.regions_.empty() || scope.regions_.back().first != page) scope.regions_.push_back({page, {}});
        scope.regions_.back().second.push_back(rect);
    }
    return scope;
}

const std::vector<PdfRect>* Scope::regionsOf(int page) const noexcept
{
    const auto found = std::lower_bound(regions_.begin(), regions_.end(), page,
                                        [](const auto& entry, int value) { return entry.first < value; });
    return found != regions_.end() && found->first == page ? &found->second : nullptr;
}

Scope Scope::objects(std::vector<long long> numbers)
{
    if (numbers.empty())
        return Scope{};

    Scope scope;
    scope.kind_ = Kind::Objects;
    scope.objects_ = std::move(numbers);
    sortUnique(scope.objects_);
    return scope;
}

bool Scope::touchesPage(int index) const noexcept
{
    switch (kind_) {
    case Kind::Nothing:
        return false;
    case Kind::WholeDocument:
        return true;
    case Kind::Pages:
        return std::binary_search(pages_.begin(), pages_.end(), index);
    case Kind::Objects:
        // TRUE, deliberately. This class does not know which pages an object appears on -- that is
        // `core/doc/PageTree`'s knowledge and it arrives at T9 -- so the honest answer is "it
        // might", and the widening rule says an unknown answer rounds outwards.
        return true;
    }
    return true;
}

Scope Scope::merged(const Scope& other) const
{
    if (kind_ == Kind::Nothing)
        return other;
    if (other.kind_ == Kind::Nothing)
        return *this;
    // S7 T2: two page commands in one step are one map, the first then the second; a map whose counts do not
    // chain, or one merged with anything else, is dropped -- the header says why that is still correct.
    if (map_ && other.map_) {
        if (std::optional<PageMap> both = map_->then(*other.map_)) return pageCommand(std::move(*both));
        return wholeDocument();
    }
    if (kind_ == Kind::WholeDocument || other.kind_ == Kind::WholeDocument)
        return wholeDocument();

    if (kind_ == Kind::Pages && other.kind_ == Kind::Pages) {
        std::vector<int> combined = pages_;
        combined.insert(combined.end(), other.pages_.begin(), other.pages_.end());
        // Annotations only when BOTH say so (D6-10): a content change in either is a content change.
        if (aspect_ != Aspect::Annotations || other.aspect_ != Aspect::Annotations) return pages(std::move(combined));
        Scope merged = annotationsOf(std::move(combined));
        // THE RECTANGLES: a page keeps them only when BOTH sides have them for it, or only one side
        // names the page and has them; a page WHOLE on either side is whole.
        for (const int page : merged.pages_) {
            const bool inThis = std::binary_search(pages_.begin(), pages_.end(), page);
            const bool inOther = std::binary_search(other.pages_.begin(), other.pages_.end(), page);
            const std::vector<PdfRect>* mine = inThis ? regionsOf(page) : nullptr;
            const std::vector<PdfRect>* theirs = inOther ? other.regionsOf(page) : nullptr;
            if ((inThis && mine == nullptr) || (inOther && theirs == nullptr)) continue;   // whole
            std::vector<PdfRect> rects;
            if (mine != nullptr) rects.insert(rects.end(), mine->begin(), mine->end());
            if (theirs != nullptr) rects.insert(rects.end(), theirs->begin(), theirs->end());
            merged.regions_.push_back({page, std::move(rects)});
        }
        return merged;
    }
    if (kind_ == Kind::Objects && other.kind_ == Kind::Objects) {
        std::vector<long long> combined = objects_;
        combined.insert(combined.end(), other.objects_.begin(), other.objects_.end());
        return objects(std::move(combined));
    }

    // Pages merged with Objects. A precise union would need to know which pages those objects
    // appear on, which this class does not -- so it rounds outwards rather than guessing. Getting
    // this wrong in the other direction is a stale tile, and a stale tile is diagnosed as a
    // graphics driver problem months later.
    return wholeDocument();
}

bool operator==(const Scope& a, const Scope& b)
{
    return a.kind_ == b.kind_ && a.aspect_ == b.aspect_ && a.pages_ == b.pages_ && a.objects_ == b.objects_
           && a.regions_ == b.regions_ && a.map_ == b.map_;
}

const char* scopeKindName(Scope::Kind kind) noexcept
{
    switch (kind) {
    case Scope::Kind::Nothing:       return "Nothing";
    case Scope::Kind::Pages:         return "Pages";
    case Scope::Kind::Objects:       return "Objects";
    case Scope::Kind::WholeDocument: return "WholeDocument";
    }
    return "?";
}

const char* scopeAspectName(Scope::Aspect aspect) noexcept
{
    switch (aspect) {
    case Scope::Aspect::Content:     return "Content";
    case Scope::Aspect::Annotations: return "Annotations";
    }
    return "?";
}

} // namespace rawpage::core
