// tst_actionalias -- Stage 1 guide case A-26, finally run. Stage 2 T18, decision D2-10.
//
// ===============================================================================================
// THE CASE THIS FILE CLOSES, AND WHY IT WAS OPEN FOR THREE STAGES
// ===============================================================================================
//
// D1-16 says a renamed `ActionId` keeps resolving from its old string forever, so that a user's
// saved shortcuts and Quick Access Toolbar entries survive a rename. `RAWPAGE_ACTION_ALIAS` is the
// mechanism, it has been compiled since Stage 1 T3, and **it has zero rows** -- nothing has been
// renamed, so nothing has ever resolved through it. Stage 1's A-26 recorded that honestly as *not
// run: the mechanism exists and has never been exercised*, and **D2-10** carried it here.
//
// **The suite that was supposed to cover it made the position worse rather than better.**
// `tst_actionregistry.cpp`'s two alias cases put every assertion inside
// `for (const ActionAlias& alias : kActionAliases)` -- a loop over an empty array. They passed, they
// were counted, and they asserted nothing. That is convention 7's failure in its quietest form: not
// a control that fails for the wrong reason, but a case whose body never runs. Those two cases stay
// where they are and keep their subject, which is the SHIPPED table's well-formedness; this file
// takes the subject they could not have, which is the ALGORITHM.
//
// ===============================================================================================
// HOW A ROW IS SUPPLIED WITHOUT WRITING FICTION INTO THE SHIPPED TABLE
// ===============================================================================================
//
// An alias row asserts *this name used to mean that action*, and the strings are written to users'
// settings files. None of ours ever did. So `core/cmd/ActionId.h` splits the resolution algorithm
// out as `actionFromStringIdIn(descriptors, aliases, stringId)` and keeps the shipped functions as
// one-line wrappers, and this file builds a REAL alias table with the REAL macro over
// `tests/unit/fixtures/ActionAliasFixture.def`.
//
// ===============================================================================================
// WHAT THIS STILL DOES NOT COVER, NAMED AT THE ASSERTION (convention 19)
// ===============================================================================================
//
// **The consumers.** This file drives the algorithm and nothing that calls it. What a USER keeps
// across a rename -- a saved shortcut, a saved Quick Access Toolbar -- is guide case A-73's second
// clause, and since Stage 2 I-39 it has its own cases: `tst_settings.cpp` for the loader that
// rewrites a retired spelling, `tests/ui/tst_aliassurvival.cpp` for the Shortcuts page and the
// toolbar acting on what that loader produced. `ui/shell/RibbonModel` and the customization-blob
// import (`UiCustomization::applyToSettings`) still call the shipped wrapper over the shipped table,
// and with zero rows there is nothing there for them to resolve.
//
// ===============================================================================================
// THE ASSERTIONS ARE `STATIC_REQUIRE`, SO A DEFECT HERE STOPS THE BUILD
// ===============================================================================================
//
// Everything the fixture can answer is answerable at compile time, so it is asserted there. Two
// controls were run and both fired as **compile errors** rather than as failing cases:
//
//   * reversing the resolution order in `actionFromStringIdIn` -- alias table first -- produces
//     `error: static assertion failed: resolve("file.open") == ActionId::FileOpen`, which is the
//     assertion that could not have existed before this fixture, because there was no colliding
//     alias row for a live id to lose to;
//   * emptying the fixture produces the same on the row-count check, which is what stops this suite
//     from quietly becoming the vacuous loop it was written to replace.
//
// A build that stops is a better outcome than a case that fails: nobody can ship past it.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionId.h"
#include "tests/unit/fixtures/ActionAliasFixture.h"

#include <array>
#include <span>
#include <string>
#include <string_view>

using namespace rawpage::core;
using rawpage::testfixture::kFixtureAliasCount;
using rawpage::testfixture::kFixtureAliases;

namespace {

// The fixture table is built in `tests/unit/fixtures/ActionAliasFixture.h`, by the SAME two-pass
// macro pattern `ActionId.h` uses for the shipped one. It lived here until Stage 2 I-39, when the
// settings loader and the two consumers a user sees needed the same rows.

constexpr std::optional<ActionId> resolve(std::string_view stringId)
{
    return actionFromStringIdIn(kActions, kFixtureAliases, stringId);
}

constexpr bool retired(std::string_view stringId)
{
    return actionStringIdIsRetiredIn(kActions, kFixtureAliases, stringId);
}

} // namespace

// =======================================================================================
// The macro produced rows at all
// =======================================================================================

TEST_CASE("the alias macro builds a table whose size matches its rows", "[actionid][alias]")
{
    // THREE, and the number is spelled out rather than derived, because the whole failure this file
    // exists to correct is a loop over an empty array that reported success. If the fixture ever
    // empties, this line fails instead of every case below quietly passing.
    STATIC_REQUIRE(kFixtureAliasCount == 3);
    STATIC_REQUIRE(kFixtureAliases.size() == kFixtureAliasCount);

    // ...and the shipped table is still empty, which is the honest state and not a defect. Asserted
    // here so that the day a real rename lands, this line is what says the fixture is no longer the
    // only place an alias exists.
    CHECK(kActionAliasCount == 0);
}

// =======================================================================================
// An ordinary retirement resolves, and reports as retired
// =======================================================================================

TEST_CASE("a retired spelling resolves to its current action", "[actionid][alias]")
{
    STATIC_REQUIRE(resolve("view.sidebar.toggle") == ActionId::ViewToggleSidebar);
    STATIC_REQUIRE(retired("view.sidebar.toggle"));

    // The current spelling of the same action resolves too, and is NOT retired -- which is what a
    // settings rewrite reads to decide whether to migrate a key.
    STATIC_REQUIRE(resolve("view.toggleSidebar") == ActionId::ViewToggleSidebar);
    STATIC_REQUIRE_FALSE(retired("view.toggleSidebar"));
}

TEST_CASE("two renames deep, both old spellings still resolve", "[actionid][alias]")
{
    // A long-lived product renames things more than once, and a mechanism that tolerates one hop is
    // one nobody discovers is insufficient until the second rename -- by which time the settings
    // files it was meant to protect are already in the field.
    STATIC_REQUIRE(resolve("shell.toggleSidebar") == ActionId::ViewToggleSidebar);
    STATIC_REQUIRE(resolve("view.sidebar.toggle") == ActionId::ViewToggleSidebar);
    STATIC_REQUIRE(retired("shell.toggleSidebar"));
}

// =======================================================================================
// THE ORDER -- and this is the case that could not exist before
// =======================================================================================

TEST_CASE("a live id outranks an alias of the same spelling", "[actionid][alias]")
{
    // The fixture deliberately claims `file.open` -- a LIVE id -- as a retired spelling of
    // `ViewFullScreen`. Resolution must ignore it.
    //
    // `tst_actionregistry.cpp` has a case with this name over the shipped table, and over an empty
    // alias array it cannot fail: it walks all 363 live ids and finds each of them, which is true
    // whatever the alias path does. Here there is an alias that would give the wrong answer if the
    // order were reversed, so the assertion has a way of being false.
    STATIC_REQUIRE(resolve("file.open") == ActionId::FileOpen);
    STATIC_REQUIRE(resolve("file.open") != ActionId::ViewFullScreen);

    // ...and it is not reported as retired either, which is the half that matters to a settings
    // rewrite: migrating `file.open` away would delete a live user binding.
    STATIC_REQUIRE_FALSE(retired("file.open"));
}

// =======================================================================================
// The negatives
// =======================================================================================

TEST_CASE("a string in neither table resolves to nothing", "[actionid][alias]")
{
    STATIC_REQUIRE_FALSE(resolve("no.such.action").has_value());
    STATIC_REQUIRE_FALSE(resolve("").has_value());
    STATIC_REQUIRE_FALSE(retired("no.such.action"));
    STATIC_REQUIRE_FALSE(retired(""));
}

TEST_CASE("with an EMPTY alias table a retired spelling resolves to nothing",
          "[actionid][alias]")
{
    // THE CONTROL, and it is the one that says the three cases above are measuring the alias table
    // rather than something the live table would have answered anyway. Same algorithm, same
    // strings, no aliases: every retired spelling must now fail to resolve.
    //
    // It is also, precisely, what the shipped configuration does today -- so this case doubles as
    // the statement of what a user with a renamed binding would experience if the alias row were
    // ever dropped: the binding silently disappears.
    constexpr std::array<ActionAlias, 0> none{};

    STATIC_REQUIRE_FALSE(actionFromStringIdIn(kActions, none, "view.sidebar.toggle").has_value());
    STATIC_REQUIRE_FALSE(actionFromStringIdIn(kActions, none, "shell.toggleSidebar").has_value());
    STATIC_REQUIRE_FALSE(actionStringIdIsRetiredIn(kActions, none, "view.sidebar.toggle"));

    // ...while a live id is unaffected, which is what makes the control specific to the alias path
    // rather than a claim that the whole lookup stopped working.
    STATIC_REQUIRE(actionFromStringIdIn(kActions, none, "file.open") == ActionId::FileOpen);
}

TEST_CASE("the shipped wrappers agree with the algorithm over the shipped tables",
          "[actionid][alias]")
{
    // The split introduced at T18 has one new way of going wrong: a wrapper that passed the wrong
    // arrays.
    //
    // **THIS CASE IS LATENT TODAY, AND THAT WAS MEASURED RATHER THAN INFERRED.** A control run
    // rewrote `actionStringIdIsRetired` to read an empty alias table instead of `kActionAliases`,
    // and **nothing in the suite went red** -- correctly, because `kActionAliases` IS empty, so the
    // two spellings are the same function for every input that exists. It becomes a live check the
    // moment a real alias row lands, which is exactly when a mis-wired wrapper would start losing
    // users' bindings. Recorded here rather than left for the next reader to re-derive: a case that
    // cannot fail today is worth keeping only if its own file says when it starts being able to
    // (conventions 7 and 19).
    for (const ActionDescriptor& descriptor : kActions) {
        INFO(std::string(descriptor.stringId));
        CHECK(actionFromStringId(descriptor.stringId)
              == actionFromStringIdIn(kActions, kActionAliases, descriptor.stringId));
        CHECK(actionStringIdIsRetired(descriptor.stringId)
              == actionStringIdIsRetiredIn(kActions, kActionAliases, descriptor.stringId));
    }
    CHECK_FALSE(actionFromStringId("no.such.action").has_value());
}
