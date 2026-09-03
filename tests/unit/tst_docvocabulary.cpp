// tst_docvocabulary -- `core/doc/Revision` and `core/doc/Scope`. Stage 2 T8.
//
// These are the two vocabulary types decision D2-5 brings forward from Stage 3, and they have no
// guide case of their own -- their consumers are T11's `RenderCache` and Stage 3's
// `CommandJournal`, neither of which exists yet. That is exactly why they are tested now: a
// vocabulary type with no test is one whose semantics get decided by its first accidental caller.
//
// WHAT THIS SUITE IS TRYING TO CATCH:
//
//   1. That `Revision` cannot be confused with the other integers around it. D2-12 makes the
//      `Revision` inside a `RenderRequest` MANDATORY rather than defaulted, and the reason is that
//      a defaulted one makes "a stale tile after a Stage 3 mutation" the default behaviour.
//   2. That `Scope` rounds OUTWARDS. Claiming more than was touched wastes a repaint; claiming
//      less shows a stale tile, which is a correctness bug that gets diagnosed as a driver
//      problem. Every case below is about which direction an unknown answer falls in.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Revision.h"
#include "core/doc/Scope.h"

#include <type_traits>
#include <unordered_set>

using rawpage::core::Revision;
using rawpage::core::Scope;
using rawpage::core::scopeKindName;

// ==========================================================================================
// Revision
// ==========================================================================================

TEST_CASE("doc revision is monotonic and never returns to a previous value", "[core][doc]")
{
    const Revision first = Revision::initial();
    const Revision second = first.next();
    const Revision third = second.next();

    CHECK(first < second);
    CHECK(second < third);
    CHECK(first != second);
    CHECK(third > first);

    // Undo does NOT return to the previous revision -- it produces a new one. A cache keyed on a
    // revision that came back would serve tiles rendered before the redo, and "the same number
    // means the same pixels" is the whole contract.
    CHECK(third.next() != second);
    CHECK(third.next() > third);
}

TEST_CASE("doc revision distinguishes a default-constructed one from a real one", "[core][doc]")
{
    // A struct member without an initializer gets the default. If that were a VALID revision, a
    // `RenderRequest` somebody forgot to fill in would look like a legitimate cache key and serve
    // the wrong tile -- silently, which is D2-12's named failure with no symptom but wrongness.
    CHECK_FALSE(Revision{}.isValid());
    CHECK_FALSE(Revision::invalid().isValid());
    CHECK(Revision::initial().isValid());
    CHECK(Revision{} == Revision::invalid());
    CHECK(Revision{} != Revision::initial());
}

TEST_CASE("doc revision cannot be built from a bare integer", "[core][doc]")
{
    // The reason this is a type rather than a `std::uint64_t`: page indices, object numbers, tile
    // indices and revisions are all integers in this codebase, and a transposed argument between
    // two of them compiles. Asserted structurally rather than trusted to review.
    STATIC_REQUIRE_FALSE(std::is_convertible_v<std::uint64_t, Revision>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<int, Revision>);
    STATIC_REQUIRE(std::is_default_constructible_v<Revision>);

    // And it is usable as a cache key, which is what T11 needs it for. The hash is supplied
    // alongside `operator==` so the two cannot disagree -- a hash that disagreed with equality
    // produces a cache that misses silently, whose only symptom is that scrolling is slow.
    std::unordered_set<Revision> keys;
    keys.insert(Revision::initial());
    keys.insert(Revision::initial().next());
    keys.insert(Revision::initial());
    CHECK(keys.size() == 2);
}

// ==========================================================================================
// Scope
// ==========================================================================================

TEST_CASE("doc scope defaults to nothing rather than to everything", "[core][doc]")
{
    // Deliberate, and the opposite of the safe-looking choice. A `Scope` somebody forgot to fill
    // in should show up as "the screen did not update" -- visible, reproducible, and fixed --
    // rather than as a permanent full-document repaint, which is invisible and merely slow.
    CHECK(Scope{}.isNothing());
    CHECK(Scope{}.kind() == Scope::Kind::Nothing);
    CHECK_FALSE(Scope{}.touchesPage(0));
    CHECK_FALSE(Scope{}.touchesEverything());
}

TEST_CASE("doc scope answers page questions and rounds unknown answers outwards", "[core][doc]")
{
    const Scope pages = Scope::pages({4, 1, 4});
    CHECK(pages.kind() == Scope::Kind::Pages);
    CHECK(pages.pageIndices() == std::vector<int>{1, 4});   // sorted and de-duplicated
    CHECK(pages.touchesPage(1));
    CHECK(pages.touchesPage(4));
    CHECK_FALSE(pages.touchesPage(2));

    CHECK(Scope::wholeDocument().touchesPage(999));

    // THE WIDENING RULE. An object scope does not know which pages its objects appear on -- that
    // is `core/doc/PageTree`'s knowledge and it arrives at T9 -- so the honest answer to "must I
    // repaint page 4?" is "it might", and a wasted repaint is the correct failure direction.
    const Scope objects = Scope::objects({12, 13});
    CHECK(objects.kind() == Scope::Kind::Objects);
    CHECK(objects.touchesPage(4));
    CHECK(objects.touchesPage(0));
}

TEST_CASE("doc scope treats an empty set as nothing rather than as an empty something",
          "[core][doc]")
{
    // A caller building a scope from a filtered list gets an empty vector. If that became an empty
    // `Pages` scope, `touchesPage()` would answer false for every page -- which is the NARROWING
    // this class forbids, arriving by accident through a perfectly ordinary code path.
    CHECK(Scope::pages({}).isNothing());
    CHECK(Scope::objects({}).isNothing());
}

TEST_CASE("doc scope merges outwards and never narrows", "[core][doc]")
{
    const Scope one = Scope::page(1);
    const Scope two = Scope::page(2);
    const Scope objects = Scope::objects({7});

    // Two page scopes merge precisely.
    const Scope both = one.merged(two);
    CHECK(both.kind() == Scope::Kind::Pages);
    CHECK(both.pageIndices() == std::vector<int>{1, 2});

    // Nothing is the identity in both directions -- a command that changed no rendered state must
    // not widen the scope of one that did.
    CHECK(one.merged(Scope::nothing()) == one);
    CHECK(Scope::nothing().merged(one) == one);

    // Everything absorbs.
    CHECK(one.merged(Scope::wholeDocument()).touchesEverything());
    CHECK(Scope::wholeDocument().merged(one).touchesEverything());

    // Pages merged with Objects rounds OUTWARDS: a precise union would need to know which pages
    // those objects appear on, and guessing in the other direction is a stale tile.
    CHECK(one.merged(objects).touchesEverything());
    CHECK(objects.merged(one).touchesEverything());

    // Two object scopes merge precisely, because no page knowledge is needed.
    CHECK(objects.merged(Scope::objects({8})).kind() == Scope::Kind::Objects);
    CHECK(objects.merged(Scope::objects({8})).objectNumbers() == std::vector<long long>{7, 8});

    // Merging is commutative, which a caller accumulating scopes in a loop depends on.
    CHECK(one.merged(two) == two.merged(one));
}

TEST_CASE("doc scope kind names are stable and complete", "[core][doc]")
{
    for (int value = 0; value <= static_cast<int>(Scope::Kind::WholeDocument); ++value)
        CHECK(std::string(scopeKindName(static_cast<Scope::Kind>(value))) != "?");
    CHECK(std::string(scopeKindName(Scope::Kind::WholeDocument)) == "WholeDocument");
}
