// core/doc/Revision -- the document's monotonic version number. Stage 2 T8, brought forward from
// Stage 3 by decision D2-5.
//
// WHY A TYPE AND NOT A `std::uint64_t`. Two reasons, and the second is the load-bearing one.
//
//   1. A bare integer is assignable from a page number, an object number, a tile index or a
//      revision, and every one of those is an integer in this codebase. `RenderRequest` carries a
//      `Revision` alongside a page index and a device resolution (D2-12), and a transposed
//      argument between two `int`s compiles.
//   2. **`RenderRequest`'s equality IS the render cache's key**, and D2-12 says the `Revision` is
//      MANDATORY rather than defaulted, because a defaulted one makes *"a stale tile after a
//      Stage 3 mutation"* the default behaviour. A type with no implicit conversion from an
//      integer is what makes "you must supply one" a compile error rather than a convention.
//
// MONOTONIC, NEVER REUSED. `next()` only goes up. Undo does NOT return to the previous revision --
// it produces a NEW one -- because a cache keyed on a revision that came back would serve tiles
// rendered before the redo, and "the same number means the same pixels" is the whole contract.
//
// HEADER-ONLY AND `constexpr`, so it costs nothing and can be a non-type template argument or a
// static assertion if a later stage wants one. `Architecture-FileMap.md` §3.2 lists this file with
// no `.cpp`, and that is the reason.

#ifndef RAWPAGE_CORE_DOC_REVISION_H
#define RAWPAGE_CORE_DOC_REVISION_H

#include <cstddef>
#include <cstdint>
#include <functional>

namespace rawpage::core {

class Revision {
public:
    // The revision a freshly opened document has. Deliberately 1 rather than 0, so that a
    // default-constructed `Revision` -- which a struct member without an initializer would get --
    // is DISTINGUISHABLE from a real one. `isValid()` is what a cache checks before trusting a key.
    constexpr Revision() = default;

    static constexpr Revision initial() noexcept { return Revision(1); }
    static constexpr Revision invalid() noexcept { return Revision(0); }

    constexpr std::uint64_t value() const noexcept { return value_; }
    constexpr bool isValid() const noexcept { return value_ != 0; }

    // The next revision. Free function style deliberately avoided: `revision.next()` reads as a
    // question about `revision` and returns a new value, which is what it does -- there is no
    // in-place increment, because a `Revision` that changed under a cache key would be the bug
    // this type exists to prevent.
    constexpr Revision next() const noexcept { return Revision(value_ + 1); }

    friend constexpr bool operator==(Revision a, Revision b) noexcept
    {
        return a.value_ == b.value_;
    }
    friend constexpr bool operator!=(Revision a, Revision b) noexcept { return !(a == b); }
    friend constexpr bool operator<(Revision a, Revision b) noexcept { return a.value_ < b.value_; }
    friend constexpr bool operator>(Revision a, Revision b) noexcept { return b < a; }
    friend constexpr bool operator<=(Revision a, Revision b) noexcept { return !(b < a); }
    friend constexpr bool operator>=(Revision a, Revision b) noexcept { return !(a < b); }

private:
    // EXPLICIT, and that is the whole point of the type: `Revision r = 7;` must not compile, or
    // every integer in scope becomes a candidate revision.
    constexpr explicit Revision(std::uint64_t value) noexcept : value_(value) {}

    std::uint64_t value_ = 0;
};

} // namespace rawpage::core

// `RenderCache` (T11) keys an unordered map on a `RenderRequest`, which contains a `Revision`.
// Supplied here so that the cache does not have to write its own and get it subtly different from
// `operator==` -- a hash that disagreed with equality would produce a cache that misses silently,
// which D2-12 already names as the failure with no symptom but slowness.
template <>
struct std::hash<rawpage::core::Revision> {
    std::size_t operator()(rawpage::core::Revision revision) const noexcept
    {
        return std::hash<std::uint64_t>{}(revision.value());
    }
};

#endif // RAWPAGE_CORE_DOC_REVISION_H
