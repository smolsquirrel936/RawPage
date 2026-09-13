// The synthetic alias table from `ActionAliasFixture.def`, built ONCE for every suite that needs it.
//
// It was built inside `tests/unit/tst_actionalias.cpp` until Stage 2 I-39, when the settings loader
// (`tests/unit/tst_settings.cpp`) and the two consumers a user sees (`tests/ui/tst_aliassurvival.cpp`)
// needed the same rows. Three copies of a two-pass macro expansion agree until one of them is edited.
//
// The table is built by the SAME two-pass pattern `core/cmd/ActionId.h` uses for the shipped one --
// count by re-including with only the alias macro defined, then build -- so the macro is exercised
// as well as the algorithm. A hand-written array of `ActionAlias{...}` literals would test
// everything except the thing a real rename will use.

#ifndef RAWPAGE_TESTS_UNIT_FIXTURES_ACTIONALIASFIXTURE_H
#define RAWPAGE_TESTS_UNIT_FIXTURES_ACTIONALIASFIXTURE_H

#include "core/cmd/ActionId.h"

#include <array>
#include <cstddef>

namespace rawpage::testfixture {

inline constexpr std::size_t kFixtureAliasCount = 0
#define RAWPAGE_ACTION_ALIAS(OldStringId, Enumerator) +1
#include "tests/unit/fixtures/ActionAliasFixture.def"
    ;

inline constexpr std::array<core::ActionAlias, kFixtureAliasCount> kFixtureAliases{{
#define RAWPAGE_ACTION_ALIAS(OldStringId, Enumerator) \
    core::ActionAlias{OldStringId, core::ActionId::Enumerator},
#include "tests/unit/fixtures/ActionAliasFixture.def"
}};

} // namespace rawpage::testfixture

#endif // RAWPAGE_TESTS_UNIT_FIXTURES_ACTIONALIASFIXTURE_H
