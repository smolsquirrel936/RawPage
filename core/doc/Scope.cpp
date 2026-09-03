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
    if (kind_ == Kind::WholeDocument || other.kind_ == Kind::WholeDocument)
        return wholeDocument();

    if (kind_ == Kind::Pages && other.kind_ == Kind::Pages) {
        std::vector<int> combined = pages_;
        combined.insert(combined.end(), other.pages_.begin(), other.pages_.end());
        return pages(std::move(combined));
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
    return a.kind_ == b.kind_ && a.pages_ == b.pages_ && a.objects_ == b.objects_;
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

} // namespace rawpage::core
