// core/doc/Scope -- what a mutation touched. Stage 2 T8, brought forward from Stage 3 by D2-5.
//
// WHY IT EXISTS. `RenderCache::invalidate(Scope, Revision)` is a **Stage 2 signature**
// (`Implementation-Plan.md` § Stage 2), and its whole purpose is that changing one page's rotation
// repaints one page rather than a 900-page document. Without a scope the only honest answer to
// *"what is stale?"* is "everything", and a 900-page catalogue re-rendering on every keystroke in
// Write mode is not a performance problem to be optimized later -- it is the reason the cache
// exists at all.
//
// -------------------------------------------------------------------------------------------
// THE RULE THAT MAKES IT SAFE: WIDENING IS ALWAYS ALLOWED, NARROWING NEVER IS
// -------------------------------------------------------------------------------------------
// A `Scope` is a claim about what a caller must throw away. Claiming MORE than was really touched
// wastes work; claiming LESS shows a stale tile, which is a correctness bug that looks like a
// rendering glitch and gets diagnosed as a driver problem. So every operation here rounds
// OUTWARDS: merging a page scope with an object scope gives the whole document rather than a
// clever union, because `Scope` does not know which pages an object appears on -- `core/doc/` does,
// and it is Stage 3's.
//
// That asymmetry is why `touchesPage()` answers **true** for `WholeDocument` and for any scope
// this class cannot reason about precisely. A caller asking "must I repaint page 4?" gets "yes"
// when the answer is unknown, and a wasted repaint is the correct failure direction.
//
// NO QT GUI TYPES, and no user-facing English: this is `core/`.

#ifndef RAWPAGE_CORE_DOC_SCOPE_H
#define RAWPAGE_CORE_DOC_SCOPE_H

#include <cstdint>
#include <vector>

namespace rawpage::core {

class Scope {
public:
    enum class Kind : std::uint8_t {
        Nothing,        // a command that changed no rendered state (a selection, a bookmark rename)
        Pages,          // a specific set of page indices
        Objects,        // a specific set of COS object numbers, pages unknown
        WholeDocument,  // everything -- a page insert, a security change, a repair
    };

    // Default is `Nothing`, NOT `WholeDocument`. A default-constructed scope reaching a cache
    // should invalidate nothing, so that a `Scope` somebody forgot to fill in shows up as "the
    // screen did not update" -- a visible, reproducible bug -- rather than as a permanent
    // full-document repaint, which is invisible and merely slow. The former gets fixed.
    Scope() = default;

    static Scope nothing();
    static Scope wholeDocument();
    static Scope page(int index);
    static Scope pages(std::vector<int> indices);
    static Scope objects(std::vector<long long> numbers);

    Kind kind() const noexcept { return kind_; }
    bool isNothing() const noexcept { return kind_ == Kind::Nothing; }
    bool touchesEverything() const noexcept { return kind_ == Kind::WholeDocument; }

    // Sorted and de-duplicated; empty unless `kind()` matches.
    const std::vector<int>& pageIndices() const noexcept { return pages_; }
    const std::vector<long long>& objectNumbers() const noexcept { return objects_; }

    // "Must I repaint page `index`?" Answers TRUE whenever the answer is not knowably no --
    // `WholeDocument` always, and `Objects` always, because this class does not know which pages
    // an object appears on. See the widening rule above: a wasted repaint is the correct failure.
    bool touchesPage(int index) const noexcept;

    // The union, rounded OUTWARDS. Two page scopes merge to a page scope; anything else that
    // disagrees merges to `WholeDocument`, because a union this class cannot compute exactly must
    // not be guessed at.
    Scope merged(const Scope& other) const;

    friend bool operator==(const Scope& a, const Scope& b);
    friend bool operator!=(const Scope& a, const Scope& b) { return !(a == b); }

private:
    Kind kind_ = Kind::Nothing;
    std::vector<int> pages_;
    std::vector<long long> objects_;
};

// Stable, ASCII, non-localized. Diagnostics and test failure messages -- never a user-facing
// string; `core/Result.h` carries a catalog key for that.
const char* scopeKindName(Scope::Kind kind) noexcept;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_SCOPE_H
