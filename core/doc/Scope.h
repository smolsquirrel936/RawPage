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
// -------------------------------------------------------------------------------------------
// THE ASPECT -- Stage 6 T4, decision D6-10 (`Stage6-Annotations-Plan.md` §3)
// -------------------------------------------------------------------------------------------
// A page scope says WHAT of those pages changed: their CONTENT (a rotation, and every Stage 3 scope)
// or only their ANNOTATIONS. Both drop the pages' tiles -- an annotation is drawn into the tile -- but
// only content drops their TEXT: `DocumentIndex` keeps an annotated page's extracted text, which no
// annotation can change, and drops its annotation search units alone. Without it, every highlight on
// page 450 of `text-900.pdf` would re-extract the page. The widening rule holds: two scopes of
// different aspects merge to CONTENT, which claims more, never less; every kind but `Pages` is content.
//
// AND WHERE ON THE PAGE -- the named work of A2 at Stage 6 (Stage 6 T4, `Tech-Stack-Plan.md` §3.3). An
// annotation scope may say WHICH RECTANGLES of a page changed, in page space: each added or removed
// annotation's `/Rect`. A tile none of them reaches keeps its pixels -- an annotation's appearance is
// drawn inside its rectangle -- so `core/render/` re-keys it rather than re-rendering it
// (`render::tileTouchedBy`). Measured at T4: without it, every edit re-rendered the edited page's 40
// visible tiles at 288 dpi, 23 of 30 s of the one engine thread under the guide's workload. A page with
// no rectangles is the WHOLE page, which is what every scope said before; any merge that cannot keep
// the rectangles exactly drops them, and a page whole on either side is whole.
//
// NO QT GUI TYPES, and no user-facing English: this is `core/`.

#ifndef RAWPAGE_CORE_DOC_SCOPE_H
#define RAWPAGE_CORE_DOC_SCOPE_H

#include "core/Geometry.h"

#include <cstdint>
#include <utility>
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

    // S6 T4 (D6-10). `Content` unless a page scope says otherwise.
    enum class Aspect : std::uint8_t { Content, Annotations };
    static Scope annotationsOf(std::vector<int> indices);
    Aspect aspect() const noexcept { return aspect_; }

    // An annotation scope over RECTANGLES of pages (page index, page-space rectangle). A page named
    // more than once keeps every rectangle. An empty list is `Nothing`.
    static Scope annotationRegions(std::vector<std::pair<int, PdfRect>> regions);
    // The rectangles of `page` that changed, or null when the WHOLE page did -- which is the answer for
    // every page of a scope without rectangles, and for a page this scope does not touch (ask
    // `touchesPage` first).
    const std::vector<PdfRect>* regionsOf(int page) const noexcept;

    // "Must I throw away the TEXT of page `index`?" -- `touchesPage` for every scope but one that
    // changed only annotations, which keeps it.
    bool touchesText(int index) const noexcept { return aspect_ == Aspect::Content && touchesPage(index); }

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
    Aspect aspect_ = Aspect::Content;
    std::vector<int> pages_;
    // Sorted by page; a page of `pages_` absent here is whole. Only for the `Annotations` aspect.
    std::vector<std::pair<int, std::vector<PdfRect>>> regions_;
    std::vector<long long> objects_;
};

// Stable, ASCII, non-localized. Diagnostics and test failure messages -- never a user-facing
// string; `core/Result.h` carries a catalog key for that.
const char* scopeKindName(Scope::Kind kind) noexcept;
const char* scopeAspectName(Scope::Aspect aspect) noexcept;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_SCOPE_H
