// Stage 1 T3 -- `core/cmd/ActionId` and `core/cmd/ActionRegistry`.
//
// Guide cases A-20 … A-26 in Stage1-AppShell-Testing-Guide.md §5.3.
//
// The three §5.1 properties get a section each, because each is a distinct way the action layer
// quietly stops being real and none of them is visible from a passing happy path.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionId.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/KeyContext.h"

#include <cctype>
#include <iterator>
#include <set>
#include <string>

using namespace rawpage::core;

// =======================================================================================
// A-20 -- register, dispatch, enablement
// =======================================================================================

TEST_CASE("a registered action dispatches and receives its context", "[actionregistry]")
{
    ActionRegistry registry;
    int calls = 0;
    KeyContext seen = KeyContext::Global;

    REQUIRE(registry.register_(ActionId::FileOpen, [&](KeyContext context) {
        ++calls;
        seen = context;
    }));

    REQUIRE(registry.isRegistered(ActionId::FileOpen));
    REQUIRE(registry.isEnabled(ActionId::FileOpen));

    const Result<void> dispatched = registry.dispatch(ActionId::FileOpen, KeyContext::Viewer);
    REQUIRE(dispatched);
    CHECK(calls == 1);
    CHECK(seen == KeyContext::Viewer);
}

TEST_CASE("an absent enable predicate means always enabled", "[actionregistry]")
{
    ActionRegistry registry;
    REQUIRE(registry.register_(ActionId::FilePreferences, [](KeyContext) {}));
    CHECK(registry.isEnabled(ActionId::FilePreferences));
}

TEST_CASE("an enable predicate is consulted on every dispatch, not cached", "[actionregistry]")
{
    // A predicate evaluated once at registration would make every action's enablement a snapshot of
    // startup state -- correct at launch and wrong from the first document opened.
    ActionRegistry registry;
    bool available = false;
    int calls = 0;

    REQUIRE(registry.register_(
        ActionId::FileSave, [&](KeyContext) { ++calls; }, [&] { return available; }));

    CHECK_FALSE(registry.isEnabled(ActionId::FileSave));
    CHECK_FALSE(registry.dispatch(ActionId::FileSave, KeyContext::Global));
    CHECK(calls == 0);

    available = true;
    CHECK(registry.isEnabled(ActionId::FileSave));
    CHECK(registry.dispatch(ActionId::FileSave, KeyContext::Global));
    CHECK(calls == 1);
}

TEST_CASE("a checked predicate reports toggle state, and absence is not 'off'", "[actionregistry]")
{
    ActionRegistry registry;
    bool rulersVisible = true;

    ActionRegistry::Registration toggle;
    toggle.handler = [&](KeyContext) { rulersVisible = !rulersVisible; };
    toggle.checked = [&] { return rulersVisible; };
    REQUIRE(registry.register_(ActionId::ViewToggleRulers, std::move(toggle)));

    CHECK(registry.isChecked(ActionId::ViewToggleRulers));
    REQUIRE(registry.dispatch(ActionId::ViewToggleRulers, KeyContext::Viewer));
    CHECK_FALSE(registry.isChecked(ActionId::ViewToggleRulers));

    // A non-toggle reports false, and so does an unregistered id. The caller distinguishes them
    // with isRegistered(); conflating them HERE keeps every render path from needing a tri-state.
    REQUIRE(registry.register_(ActionId::FileOpen, [](KeyContext) {}));
    CHECK_FALSE(registry.isChecked(ActionId::FileOpen));
    CHECK_FALSE(registry.isChecked(ActionId::FileQuit));
}

// =======================================================================================
// A-21 / A-22 -- the two error paths, and that they are DISTINCT
// =======================================================================================

TEST_CASE("dispatching an unregistered id returns an Error, not a no-op", "[actionregistry]")
{
    // The batch runner will dispatch ids read from a file a user wrote. A silent no-op there is a
    // batch run that reports success and did nothing.
    ActionRegistry registry;

    const Result<void> dispatched = registry.dispatch(ActionId::SignDocument, KeyContext::Global);
    REQUIRE_FALSE(dispatched);
    CHECK(dispatched.error().code == ErrorCode::NotFound);
    CHECK_FALSE(dispatched.error().messageKey.isEmpty());

    // The context names the action in the spelling the caller used, so a batch report can say which
    // row of the user's file failed.
    CHECK(dispatched.error().context == QStringLiteral("sign.document"));
}

TEST_CASE("unregistered and disabled are distinct error codes", "[actionregistry]")
{
    // Without this, the CLI cannot tell a user "that does not exist" from "that is not available
    // right now" -- and those call for opposite responses.
    ActionRegistry registry;
    REQUIRE(registry.register_(
        ActionId::FileSave, [](KeyContext) {}, [] { return false; }));

    const Result<void> disabled = registry.dispatch(ActionId::FileSave, KeyContext::Global);
    const Result<void> unknown = registry.dispatch(ActionId::FileSaveAs, KeyContext::Global);

    REQUIRE_FALSE(disabled);
    REQUIRE_FALSE(unknown);

    CHECK(disabled.error().code == ErrorCode::Unavailable);
    CHECK(unknown.error().code == ErrorCode::NotFound);
    CHECK(disabled.error().code != unknown.error().code);

    // Both carry a catalog key, so both are translatable rather than one being a bare code.
    CHECK_FALSE(disabled.error().messageKey.isEmpty());
    CHECK_FALSE(unknown.error().messageKey.isEmpty());
    CHECK(disabled.error().messageKey != unknown.error().messageKey);
}

TEST_CASE("a disabled action's handler is never entered", "[actionregistry]")
{
    ActionRegistry registry;
    bool ran = false;
    REQUIRE(registry.register_(
        ActionId::RedactApply, [&](KeyContext) { ran = true; }, [] { return false; }));

    CHECK_FALSE(registry.dispatch(ActionId::RedactApply, KeyContext::Global));
    CHECK_FALSE(ran);
}

// =======================================================================================
// A-23 -- registration is not idempotent
// =======================================================================================

TEST_CASE("a second registration is an error and the first handler survives", "[actionregistry]")
{
    // Two stages both claiming edit.copy should fail loudly at startup, not race. Whichever ran
    // last silently winning is the worst of the three possible outcomes.
    ActionRegistry registry;
    int first = 0;
    int second = 0;

    REQUIRE(registry.register_(ActionId::EditCopy, [&](KeyContext) { ++first; }));

    const Result<void> again = registry.register_(ActionId::EditCopy, [&](KeyContext) { ++second; });
    REQUIRE_FALSE(again);
    CHECK(again.error().code == ErrorCode::InvalidArgument);
    CHECK(again.error().context == QStringLiteral("edit.copy"));

    REQUIRE(registry.dispatch(ActionId::EditCopy, KeyContext::Viewer));
    CHECK(first == 1);
    CHECK(second == 0);
    CHECK(registry.registeredCount() == 1);
}

TEST_CASE("a registration without a handler is rejected", "[actionregistry]")
{
    // It would otherwise dispatch successfully and do nothing -- A-21's failure mode arriving
    // through the front door.
    ActionRegistry registry;
    const Result<void> registered = registry.register_(ActionId::FileOpen, ActionRegistry::Handler{});
    REQUIRE_FALSE(registered);
    CHECK(registered.error().code == ErrorCode::InvalidArgument);
    CHECK_FALSE(registry.isRegistered(ActionId::FileOpen));
}

// =======================================================================================
// A-24 -- the generated table and the .def agree
// =======================================================================================

TEST_CASE("every declared action has a string id, a label key and a default context", "[actionid]")
{
    REQUIRE(kActionCount > 0);
    REQUIRE(kActions.size() == kActionCount);

    for (const ActionDescriptor& descriptor : kActions) {
        INFO("action index " << static_cast<int>(descriptor.id));
        CHECK_FALSE(descriptor.stringId.empty());
        CHECK_FALSE(descriptor.labelKey.empty());
        CHECK(keyContextName(descriptor.defaultContext) != std::string_view{"?"});
    }
}

TEST_CASE("the descriptor table is indexed by its own enum value", "[actionid]")
{
    // The lookup functions index the array directly, so a row out of order would silently return a
    // different action's string id -- and every consumer would agree with it.
    for (std::size_t i = 0; i < kActionCount; ++i)
        CHECK(static_cast<std::size_t>(kActions[i].id) == i);
}

TEST_CASE("the catalog key is exactly 'action.' + the string id", "[actionid]")
{
    // Rule 3 in ActionId.def. The column is written out rather than derived because i18n-lint reads
    // the file as text -- so this test is what stops the two halves drifting.
    for (const ActionDescriptor& descriptor : kActions) {
        INFO("string id " << std::string(descriptor.stringId));
        const std::string expected = "action." + std::string(descriptor.stringId);
        CHECK(std::string(descriptor.labelKey) == expected);
    }
}

TEST_CASE("string ids are unique", "[actionid]")
{
    // A duplicate would make actionFromStringId() resolve a persisted shortcut to whichever row
    // came first, which is a customization silently attached to the wrong command.
    std::set<std::string_view> seen;
    for (const ActionDescriptor& descriptor : kActions) {
        INFO("duplicate string id " << std::string(descriptor.stringId));
        CHECK(seen.insert(descriptor.stringId).second);
    }
    CHECK(seen.size() == kActionCount);
}

TEST_CASE("string ids look like a persisted public name", "[actionid]")
{
    // They are written into settings files and read back by later versions, so the shape is part of
    // the contract: lowerCamel segments separated by a single dot, no spaces, no uppercase start.
    for (const ActionDescriptor& descriptor : kActions) {
        INFO("string id " << std::string(descriptor.stringId));
        const std::string_view id = descriptor.stringId;

        CHECK(id.find(' ') == std::string_view::npos);
        CHECK(id.find("..") == std::string_view::npos);
        CHECK(id.front() != '.');
        CHECK(id.back() != '.');
        CHECK(id.find('.') != std::string_view::npos);   // every id is area.verb
        CHECK(std::islower(static_cast<unsigned char>(id.front())) != 0);
    }
}

TEST_CASE("actionFromStringId round-trips every declared action", "[actionid]")
{
    for (const ActionDescriptor& descriptor : kActions) {
        const std::optional<ActionId> resolved = actionFromStringId(descriptor.stringId);
        REQUIRE(resolved.has_value());
        CHECK(*resolved == descriptor.id);
        CHECK_FALSE(actionStringIdIsRetired(descriptor.stringId));
    }
}

TEST_CASE("an unknown string id resolves to nothing rather than to action zero", "[actionid]")
{
    CHECK_FALSE(actionFromStringId("no.such.action").has_value());
    CHECK_FALSE(actionFromStringId("").has_value());
    CHECK_FALSE(actionStringIdIsRetired("no.such.action"));
}

// =======================================================================================
// A-25 -- the declared-versus-registered split is reported, not hidden
// =======================================================================================

TEST_CASE("allActions reports every declared id with its registration state", "[actionregistry]")
{
    // ~327 declared, a fraction registered at this stage. The gap must be visible: this list is
    // what stops a later stage silently forgetting to register something it declared.
    ActionRegistry registry;
    REQUIRE(registry.register_(ActionId::FileOpen, [](KeyContext) {}));
    REQUIRE(registry.register_(ActionId::FileQuit, [](KeyContext) {}));

    const std::vector<ActionRegistry::ActionState> states = registry.allActions();
    REQUIRE(states.size() == kActionCount);

    std::size_t registered = 0;
    for (const ActionRegistry::ActionState& state : states)
        if (state.registered) ++registered;

    CHECK(registered == 2);
    CHECK(registry.registeredCount() == 2);
    CHECK(states.front().id == kActions.front().id);
}

// =======================================================================================
// A-26 -- the alias mechanism (D1-16)
// =======================================================================================

TEST_CASE("the alias table is well formed and never shadows a live id", "[actionid]")
{
    // There are no alias rows yet -- nothing has been renamed, because nothing has shipped. What
    // this asserts is that the MECHANISM is wired: the count matches the array, and no alias
    // shadows a live string id. A shadowing alias would make a current name resolve to whatever the
    // rename pointed at, which is the failure the mechanism exists to prevent, inverted.
    CHECK(kActionAliases.size() == kActionAliasCount);

    for (const ActionAlias& alias : kActionAliases) {
        INFO("alias " << std::string(alias.oldStringId));
        CHECK_FALSE(alias.oldStringId.empty());

        bool shadowsLiveId = false;
        for (const ActionDescriptor& descriptor : kActions)
            if (descriptor.stringId == alias.oldStringId) shadowsLiveId = true;
        CHECK_FALSE(shadowsLiveId);

        // The alias must resolve, and must be reported as retired so a settings rewrite can migrate
        // the key while still honouring the old spelling on the next load.
        const std::optional<ActionId> resolved = actionFromStringId(alias.oldStringId);
        REQUIRE(resolved.has_value());
        CHECK(*resolved == alias.id);
        CHECK(actionStringIdIsRetired(alias.oldStringId));
    }
}

TEST_CASE("a live id always wins over an alias of the same spelling", "[actionid]")
{
    // Resolution order is live table first, alias table second. Asserted rather than assumed,
    // because if an alias row is ever added for a name that later comes back as a real action, the
    // real one must win or the resurrected id is unreachable.
    for (const ActionDescriptor& descriptor : kActions) {
        const std::optional<ActionId> resolved = actionFromStringId(descriptor.stringId);
        REQUIRE(resolved.has_value());
        CHECK(*resolved == descriptor.id);
    }
}

// =======================================================================================
// KeyContext
// =======================================================================================

// NAME DELIBERATELY ASCII-ONLY. This case was first written as "... matches Features.md §8" and
// failed -- not in its body, but because `catch_discover_tests` registers the Catch2 name with
// CTest and CTest passes it back as a command-line filter. The `§` does not survive that round
// trip (it came back as "Features.md ??8"), Catch2 matched no test, and CTest reported a failure
// that looked like a broken assertion. Same root cause as issue I-04: a narrow, code-page-bound
// process boundary. Test names are ASCII; section references go in comments.
TEST_CASE("every KeyContext value has a name, and the count matches the Features key-binding tables",
          "[keycontext]")
{
    const KeyContext all[] = {
        KeyContext::Global,      KeyContext::Common,         KeyContext::Viewer,
        KeyContext::Write,       KeyContext::AnnotationTools, KeyContext::PageThumbnails,
        KeyContext::Bookmarks,   KeyContext::Comments,       KeyContext::FormsDesign,
        KeyContext::FormsFill,   KeyContext::FindSearch,     KeyContext::FullScreen,
    };
    CHECK(std::size(all) == kKeyContextCount);

    std::set<std::string_view> names;
    for (KeyContext context : all) {
        INFO("context " << std::string(keyContextName(context)));
        CHECK(keyContextName(context) != std::string_view{"?"});
        CHECK(names.insert(keyContextName(context)).second);
    }
}

TEST_CASE("only Global and Common are layers", "[keycontext]")
{
    // The distinction drives KeyBindingStore's resolution order at T4, so it is worth pinning
    // before that code exists rather than after it depends on it.
    CHECK(keyContextIsLayer(KeyContext::Global));
    CHECK(keyContextIsLayer(KeyContext::Common));
    CHECK_FALSE(keyContextIsLayer(KeyContext::Viewer));
    CHECK_FALSE(keyContextIsLayer(KeyContext::Write));
    CHECK_FALSE(keyContextIsLayer(KeyContext::FullScreen));
}

TEST_CASE("every declared action's default context is a real context", "[actionid]")
{
    // A row whose fourth column names a context that does not exist would not compile, but one that
    // names `Common` for an action no context can override is a modelling error the compiler cannot
    // see. Global and Common are both legal defaults; this asserts nothing sneaks in outside the enum.
    for (const ActionDescriptor& descriptor : kActions) {
        INFO("string id " << std::string(descriptor.stringId));
        const auto value = static_cast<int>(descriptor.defaultContext);
        CHECK(value >= static_cast<int>(KeyContext::Global));
        CHECK(value <= static_cast<int>(KeyContext::FullScreen));
    }
}
