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

// Resolves a string id, INCLUDING a retired one through its alias row. This is the function every
// persistence path must use -- `shortcuts/<context>/<action>` on load, and the QAT customization
// blob -- because it is the only one that honours a rename.
//
// A linear scan over 327 entries, deliberately. It runs at load time and on a `--list-actions`
// dump, not per keystroke: chord lookup goes through `KeyBindingStore`'s own index. A sorted table
// plus binary search would need the .def rows kept in string order, which would fight rule 4's
// grouping by Features.md section and make every future insertion a merge conflict.
constexpr std::optional<ActionId> actionFromStringId(std::string_view stringId)
{
    for (const ActionDescriptor& descriptor : kActions)
        if (descriptor.stringId == stringId) return descriptor.id;

    for (const ActionAlias& alias : kActionAliases)
        if (alias.oldStringId == stringId) return alias.id;

    return std::nullopt;
}

// True when the string resolves only through an alias row -- i.e. it is a retired spelling. Callers
// that rewrite a settings file use this to migrate the key to the current name while still honouring
// the old one on the next load.
constexpr bool actionStringIdIsRetired(std::string_view stringId)
{
    for (const ActionDescriptor& descriptor : kActions)
        if (descriptor.stringId == stringId) return false;

    for (const ActionAlias& alias : kActionAliases)
        if (alias.oldStringId == stringId) return true;

    return false;
}

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_ACTIONID_H
