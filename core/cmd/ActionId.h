// The `ActionId` enum and its lookup tables, all generated from `core/cmd/ActionId.def` (D1-6).
//
// Nothing here is hand-maintained. If you are about to add an id, add a row to the .def; if you are
// about to add a *column*, change the macro signature there and every expansion site below fails to
// compile, which is the intended way to find them.
//
// WHY string_view AND NOT QString: these tables are `constexpr`, live in the binary's read-only
// data, and cost nothing at startup. A QString table would allocate 327 strings before `main()` in
// every process, including the headless test binary and the batch runner. `ui/` converts at the
// point of display, which is once per visible button rather than once per declared action.

#ifndef RAWPAGE_CORE_CMD_ACTIONID_H
#define RAWPAGE_CORE_CMD_ACTIONID_H

#include "core/cmd/KeyContext.h"

#include <array>
#include <span>
#include <cstddef>
#include <optional>
#include <string_view>

namespace rawpage::core {

// ---------------------------------------------------------------------------------------
// The enum
// ---------------------------------------------------------------------------------------
// Values are NOT stable across builds and must never be persisted -- the string id is the
// permanent public name (rule 1 in the .def). Inserting a row in the middle renumbers
// everything after it, which is fine precisely because nothing on disk refers to the number.
enum class ActionId : int {
#define RAWPAGE_ACTION(Enumerator, StringId, DefaultContext, LabelKey) Enumerator,
#include "core/cmd/ActionId.def"

    Count_
};

inline constexpr std::size_t kActionCount = static_cast<std::size_t>(ActionId::Count_);

// ---------------------------------------------------------------------------------------
// The descriptor table
// ---------------------------------------------------------------------------------------
struct ActionDescriptor {
    ActionId         id;
    std::string_view stringId;        // permanent public name; written to disk
    KeyContext       defaultContext;  // where the chord resolves unless a user remaps it
    std::string_view labelKey;        // "action." + stringId; resolved by ui/ through tr()
};

inline constexpr std::array<ActionDescriptor, kActionCount> kActions{{
#define RAWPAGE_ACTION(Enumerator, StringId, DefaultContext, LabelKey) \
    ActionDescriptor{ActionId::Enumerator, StringId, KeyContext::DefaultContext, LabelKey},
#include "core/cmd/ActionId.def"
}};

// ---------------------------------------------------------------------------------------
// Aliases -- D1-16
// ---------------------------------------------------------------------------------------
// A renamed id keeps resolving from its old string forever, so a user's saved shortcuts and QAT
// entries survive the rename. Counted by re-including the table with only the alias macro defined,
// so the array size cannot drift from the rows.
struct ActionAlias {
    std::string_view oldStringId;
    ActionId         id;
};

inline constexpr std::size_t kActionAliasCount = 0
#define RAWPAGE_ACTION_ALIAS(OldStringId, Enumerator) +1
#include "core/cmd/ActionId.def"
    ;

inline constexpr std::array<ActionAlias, kActionAliasCount> kActionAliases{{
#define RAWPAGE_ACTION_ALIAS(OldStringId, Enumerator) ActionAlias{OldStringId, ActionId::Enumerator},
#include "core/cmd/ActionId.def"
}};

// ---------------------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------------------
constexpr const ActionDescriptor& actionDescriptor(ActionId id)
{
    return kActions[static_cast<std::size_t>(id)];
}

constexpr std::string_view actionStringId(ActionId id) { return actionDescriptor(id).stringId; }
constexpr KeyContext actionDefaultContext(ActionId id) { return actionDescriptor(id).defaultContext; }
constexpr std::string_view actionLabelKey(ActionId id) { return actionDescriptor(id).labelKey; }

// ---------------------------------------------------------------------------------------
// THE RESOLUTION ALGORITHM, SEPARATED FROM THE SHIPPED TABLE -- Stage 2 T18, decision D2-10
// ---------------------------------------------------------------------------------------
// The two functions below take their tables as spans, and the two beneath them are the shipped
// table's one-line wrappers. That split exists for exactly one reason and it is a testing one.
//
// **`kActionAliases` HAS ZERO ROWS AND HAS HAD SINCE THE MACRO WAS WRITTEN.** Nothing has been
// renamed yet, so the alias path is live code that has never executed -- which is Stage 1's guide
// case **A-26** (`Stage1-AppShell-Testing-Guide.md` §5), recorded there as *not run: the mechanism
// exists and has never been exercised*, and carried to this stage by **D2-10**. The case's own
// suite made the position worse rather than better: its assertions are inside
// `for (const ActionAlias& alias : kActionAliases)`, a loop over an empty array, so it passed
// while asserting nothing at all -- convention 7's failure in its quietest form.
//
// A row cannot simply be added to `ActionId.def` to fix that: an alias for a rename that never
// happened is fiction in a table that is written to users' settings files. So the ALGORITHM is
// made drivable with a table a test supplies, and `tests/unit/tst_actionalias.cpp` drives it with
// real rows built by the real `RAWPAGE_ACTION_ALIAS` macro over
// `tests/unit/fixtures/ActionAliasFixture.def`.
//
// **What that still does not cover (convention 19):** most *consumers*. `KeyBindingStore`,
// `UiCustomization` and `RibbonModel` call the shipped wrappers over the shipped table, and with
// zero rows there is nothing for them to resolve. The one consumer that takes a table is the
// settings LOADER (`Settings`, `SettingsSchema::isDeclaredPath`), since Stage 2 I-39: it rewrites a
// retired spelling in the file under the current one, so the consumers that act on a user's saved
// shortcuts and toolbar never meet an alias at all, and `tests/ui/tst_aliassurvival.cpp` drives
// them over a file the fixture's rows produced. The first real rename is still the first time a
// real row meets a user's file.

constexpr std::optional<ActionId> actionFromStringIdIn(
    std::span<const ActionDescriptor> descriptors, std::span<const ActionAlias> aliases,
    std::string_view stringId)
{
    // LIVE TABLE FIRST, ALIAS TABLE SECOND, and the order is the contract rather than an
    // implementation detail: if an alias row is ever written for a name that later comes back as a
    // real action, the real one must win or the resurrected id is unreachable.
    for (const ActionDescriptor& descriptor : descriptors)
        if (descriptor.stringId == stringId) return descriptor.id;

    for (const ActionAlias& alias : aliases)
        if (alias.oldStringId == stringId) return alias.id;

    return std::nullopt;
}

constexpr bool actionStringIdIsRetiredIn(std::span<const ActionDescriptor> descriptors,
                                         std::span<const ActionAlias> aliases,
                                         std::string_view stringId)
{
    for (const ActionDescriptor& descriptor : descriptors)
        if (descriptor.stringId == stringId) return false;

    for (const ActionAlias& alias : aliases)
        if (alias.oldStringId == stringId) return true;

    return false;
}

// Resolves a string id, INCLUDING a retired one through its alias row. Anything that reads an id a
// user or a resource wrote must use it -- the QAT customization blob, `res/ribbon.json` -- because it
// is the only lookup that honours a rename. The settings file is the exception that goes further:
// resolving a retired key is not enough there, because every write builds the key from the enum
// and could never reach the retired one again, so `Settings` rewrites it at load (Stage 2 I-39).
//
// A linear scan over the whole table, deliberately. It runs at load time and on a `--list-actions`
// dump, not per keystroke: chord lookup goes through `KeyBindingStore`'s own index. A sorted table
// plus binary search would need the .def rows kept in string order, which would fight rule 4's
// grouping by Features.md section and make every future insertion a merge conflict.
// *(This comment said "over 327 entries" until T18 re-measured it. `kActionCount` is the figure and
// is always right; a number spelled out in prose beside it can only ever go stale.)*
constexpr std::optional<ActionId> actionFromStringId(std::string_view stringId)
{
    return actionFromStringIdIn(kActions, kActionAliases, stringId);
}

// True when the string resolves only through an alias row -- i.e. it is a retired spelling.
// `Settings::migrateRetiredActionIds` asks this (as `actionStringIdIsRetiredIn`) to decide which keys
// in the file to rewrite under the current name. *(Until Stage 2 I-39 this comment said callers that
// rewrite a settings file used it, and nothing did: the loader kept the retired key, and a
// Preferences reset that cleared the current one came back on the next launch.)*
constexpr bool actionStringIdIsRetired(std::string_view stringId)
{
    return actionStringIdIsRetiredIn(kActions, kActionAliases, stringId);
}

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_ACTIONID_H
