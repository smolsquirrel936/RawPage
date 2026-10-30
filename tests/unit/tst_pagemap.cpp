// tst_pagemap -- `core/doc/PageMap` and the map beside a `Scope`. Stage 7 T2, decision D7-3, guide case A-18
// (`Stage7-Assembly-Testing-Guide.md` §5.4).
//
// THE ORACLE IS A HAND-WRITTEN TABLE. Each command's map is compared with the order a person writes down
// for it -- "move pages 3 to 5 after 6" is `0 1 2 6 3 4 5 7` -- never with another computation of the same
// map; and the two algebraic properties every consumer leans on (a map then its inverse is the identity;
// two maps compose to the map of both commands) are checked over EVERY map of small documents, not a few.
//
// WHAT IT DOES NOT COVER (convention 19): any consumer of a map. Which ones remap and which drop is Stage 7
// T3's; at T2 every page command's scope is `WholeDocument`, which every consumer already honours.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/PageMap.h"
#include "core/doc/Scope.h"

#include <algorithm>
#include <numeric>
#include <vector>

using rawpage::core::PageMap;
using rawpage::core::Scope;

namespace {

constexpr int kNone = PageMap::kNone;

// Every page set of a document of `count` pages.
std::vector<std::vector<int>> everySet(int count)
{
    std::vector<std::vector<int>> out;
    for (int bits = 1; bits < (1 << count); ++bits) {
        std::vector<int> set;
        for (int page = 0; page < count; ++page) {
            if ((bits & (1 << page)) != 0) set.push_back(page);
        }
        out.push_back(std::move(set));
    }
    return out;
}

// Every map the three page commands make over a document of `count` pages.
std::vector<PageMap> everyMap(int count)
{
    std::vector<PageMap> out;
    for (const std::vector<int>& set : everySet(count)) {
        for (int to = 0; to <= count - static_cast<int>(set.size()); ++to) out.push_back(*PageMap::move(count, set, to));
        if (static_cast<int>(set.size()) < count) out.push_back(*PageMap::removal(count, set));
        out.push_back(*PageMap::duplication(count, set));
    }
    return out;
}

} // namespace

TEST_CASE("pagemap each command's map against a hand-written table", "[core][doc][pagemap][A-18]")
{
    SECTION("move pages 3 to 5 of eight past page 6 -- the first of them lands at index 4")
    {
        const auto map = PageMap::move(8, {3, 4, 5}, 4);
        REQUIRE(map);
        CHECK(map->order() == std::vector<int>{0, 1, 2, 6, 3, 4, 5, 7});
        CHECK(map->newIndexOf(3) == 4);
        CHECK(map->newIndexOf(6) == 3);
        CHECK(map->isPermutation());
        CHECK_FALSE(map->isIdentity());
    }
    SECTION("move the last page to the front, and the first to the end")
    {
        CHECK(PageMap::move(5, {4}, 0)->order() == std::vector<int>{4, 0, 1, 2, 3});
        CHECK(PageMap::move(5, {0}, 4)->order() == std::vector<int>{1, 2, 3, 4, 0});
    }
    SECTION("a set that is not contiguous moves as one block, keeping its order")
    {
        CHECK(PageMap::move(6, {0, 2, 5}, 1)->order() == std::vector<int>{1, 0, 2, 5, 3, 4});
    }
    SECTION("a move to where the pages already are is the identity -- no command is made for it (A-17)")
    {
        CHECK(PageMap::move(6, {2, 3}, 2)->isIdentity());
        CHECK(PageMap::move(1, {0}, 0)->isIdentity());
    }
    SECTION("removal closes up, and says what went")
    {
        const auto map = PageMap::removal(6, {1, 4});
        REQUIRE(map);
        CHECK(map->order() == std::vector<int>{0, 2, 3, 5});
        CHECK(map->removed() == std::vector<int>{1, 4});
        CHECK(map->inserted().empty());
        CHECK(map->newIndexOf(1) == kNone);
        CHECK(map->newIndexOf(5) == 3);
        CHECK_FALSE(map->isPermutation());
    }
    SECTION("duplication puts each copy right after its original")
    {
        const auto map = PageMap::duplication(4, {0, 3});
        REQUIRE(map);
        CHECK(map->order() == std::vector<int>{0, kNone, 1, 2, 3, kNone});
        CHECK(map->inserted() == std::vector<int>{1, 5});
        CHECK(map->removed().empty());
        CHECK(map->newIndexOf(3) == 4);
        CHECK(map->oldIndexOf(1) == kNone);
    }
    SECTION("removing every page is still a map -- the refusal is the command's and the tree's (D7-21)")
    {
        const auto map = PageMap::removal(3, {0, 1, 2});
        REQUIRE(map);
        CHECK(map->newCount() == 0);
    }
}

TEST_CASE("pagemap refuses a set that is empty unsorted repeated or out of range", "[core][doc][pagemap][A-18]")
{
    for (const std::vector<int>& bad : std::vector<std::vector<int>>{{}, {2, 1}, {1, 1}, {-1}, {5}, {0, 7}}) {
        INFO("set of " << bad.size());
        CHECK_FALSE(PageMap::move(5, bad, 0));
        CHECK_FALSE(PageMap::removal(5, bad));
        CHECK_FALSE(PageMap::duplication(5, bad));
    }
    CHECK_FALSE(PageMap::move(5, {1, 2}, 4));    // the first would land past the end
    CHECK_FALSE(PageMap::move(5, {1}, -1));
    CHECK_FALSE(PageMap::fromOrder(3, {0, 0, 1}));
    CHECK_FALSE(PageMap::fromOrder(3, {0, 3}));
    CHECK(PageMap::fromOrder(3, {2, kNone, 0}));
}

TEST_CASE("pagemap a map then its inverse puts every page back for every map of small documents", "[core][doc][pagemap][A-18]")
{
    // A map names pages by PLACE, so a page a removal took has no name the inverse could give back: it returns
    // as an insertion at the index it had. Every page both maps keep is where it started, and a move -- which
    // keeps every page -- is exactly the identity.
    std::size_t checked = 0;
    std::size_t expected = 0;
    for (int count = 1; count <= 5; ++count) {
        const std::vector<PageMap> maps = everyMap(count);
        expected += maps.size();
        for (const PageMap& map : maps) {
            const auto there = map.then(map.inverse());
            REQUIRE(there);
            REQUIRE(there->newCount() == count);
            const std::vector<int> removed = map.removed();
            for (int page = 0; page < count; ++page) {
                const bool went = std::find(removed.begin(), removed.end(), page) != removed.end();
                CHECK(there->oldIndexOf(page) == (went ? kNone : page));
            }
            if (map.isPermutation()) CHECK(there->isIdentity());
            const auto back = map.inverse().then(map);
            REQUIRE(back);
            REQUIRE(back->newCount() == map.newCount());
            const std::vector<int> inserted = map.inserted();
            for (int page = 0; page < map.newCount(); ++page) {
                const bool made = std::find(inserted.begin(), inserted.end(), page) != inserted.end();
                CHECK(back->oldIndexOf(page) == (made ? kNone : page));
            }
            ++checked;
        }
    }
    CHECK(checked == expected);
    CHECK(checked >= 280);
}

TEST_CASE("pagemap two maps compose to the map of running both", "[core][doc][pagemap][A-18]")
{
    // Running the maps over a list of names is the oracle: apply the first to the list, then the second, and
    // compare with the composed map applied once. An inserted page is a name nobody had.
    const auto run = [](const PageMap& map, const std::vector<int>& names, int& fresh) {
        std::vector<int> out;
        for (int now = 0; now < map.newCount(); ++now) {
            const int old = map.oldIndexOf(now);
            out.push_back(old == kNone ? fresh-- : names[static_cast<std::size_t>(old)]);
        }
        return out;
    };
    std::size_t checked = 0;
    for (int count = 1; count <= 4; ++count) {
        std::vector<int> names(static_cast<std::size_t>(count));
        std::iota(names.begin(), names.end(), 0);
        for (const PageMap& first : everyMap(count)) {
            for (const PageMap& second : everyMap(first.newCount())) {
                if (second.newCount() == 0) continue;
                const auto both = first.then(second);
                REQUIRE(both);
                int freshA = -1;
                int freshB = -1;
                const std::vector<int> stepwise = run(second, run(first, names, freshA), freshA);
                std::vector<int> once = run(*both, names, freshB);
                // An inserted page is new in both runs; its NAME differs, so compare which are original.
                REQUIRE(stepwise.size() == once.size());
                for (std::size_t at = 0; at < once.size(); ++at) {
                    if (stepwise[at] >= 0 || once[at] >= 0) CHECK(stepwise[at] == once[at]);
                }
                ++checked;
            }
        }
    }
    CHECK(checked > 1000);
    // And counts that do not chain do not compose.
    CHECK_FALSE(PageMap::identity(3).then(PageMap::identity(4)));
}

TEST_CASE("pagemap a page command's scope carries its map and an undo carries the inverse", "[core][doc][pagemap][scope]")
{
    const PageMap map = *PageMap::move(5, {0}, 4);
    const Scope scope = Scope::pageCommand(map);
    CHECK(scope.touchesEverything());          // a consumer that cannot remap drops everything (D7-3)
    REQUIRE(scope.pageMap() != nullptr);
    CHECK(*scope.pageMap() == map);
    REQUIRE(scope.reversed().pageMap() != nullptr);
    CHECK(*scope.reversed().pageMap() == map.inverse());

    SECTION("two page commands in one step compose; anything else drops the map and stays WholeDocument")
    {
        const PageMap second = *PageMap::removal(5, {2});
        const Scope both = scope.merged(Scope::pageCommand(second));
        REQUIRE(both.pageMap() != nullptr);
        CHECK(*both.pageMap() == *map.then(second));
        const Scope mixed = scope.merged(Scope::page(1));
        CHECK(mixed.touchesEverything());
        CHECK(mixed.pageMap() == nullptr);
        CHECK(Scope::nothing().merged(scope) == scope);   // nothing merged keeps the map
    }
    SECTION("the controls: every other scope has no map, and its reverse is itself")
    {
        for (const Scope& other : {Scope::nothing(), Scope::page(2), Scope::wholeDocument(), Scope::objects({4})}) {
            CHECK(other.pageMap() == nullptr);
            CHECK(other.reversed() == other);
        }
    }
}
