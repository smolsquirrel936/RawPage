// The single dispatch path for every user action.
//
// Ribbon, QAT, context menus, chords, the tool finder, the CLI and the batch runner all resolve to
// one `ActionId` and call `dispatch()`. **Nothing is wired directly to a widget slot.** That rule is
// what makes the headless batch runner (flow K) and the CLI possible without a second
// implementation of every feature -- and it is why a feature reachable only from a widget is
// unfinished rather than done.
//
// THREE PROPERTIES THIS CLASS PINS DOWN, each a distinct way the layer quietly stops being real
// (Stage1-AppShell-Plan.md §5.1):
//
//   1. `dispatch` on an UNREGISTERED id returns an Error. It does not assert and does not no-op.
//      The batch runner will one day dispatch ids from a file a user wrote; a silent no-op there is
//      a batch run that reports success and did nothing, discovered three stages later.
//
//   2. `dispatch` on a registered-but-DISABLED id returns a DISTINCT error code. The CLI and the
//      batch report must be able to tell "that does not exist" from "that is not available right
//      now" -- the first is a typo in the user's file, the second is document state.
//
//   3. Registration is NOT idempotent. A second `register_` for the same id is an error, not an
//      overwrite. Two stages both claiming `edit.copy` should fail loudly at startup rather than
//      race, and whichever ran second silently winning is the worst of the three outcomes.
//
// THREADING: not thread-safe, and deliberately so. Registration happens once during composition on
// the main thread; dispatch happens on the main thread. The render pool never touches it. A mutex
// here would be a lie about where the boundary is.

#ifndef RAWPAGE_CORE_CMD_ACTIONREGISTRY_H
#define RAWPAGE_CORE_CMD_ACTIONREGISTRY_H

#include "core/Result.h"
#include "core/cmd/ActionId.h"
#include "core/cmd/KeyContext.h"

#include <functional>
#include <vector>

namespace rawpage::core {

class ActionRegistry {
public:
    // Runs the action. Takes the context it was dispatched from, because the same id can be
    // reached from several contexts and a handler occasionally needs to know which.
    using Handler = std::function<void(KeyContext)>;

    // Answers "is this available right now" from document and selection state. Absent means
    // always enabled -- the common case for a shell action such as Preferences.
    using Enable = std::function<bool()>;

    // Answers "is this currently on" for a toggle: Rulers, Snap to Grid, Change Tracking. Absent
    // means the action is not a toggle, which is different from being a toggle that is off.
    using Checked = std::function<bool()>;

    struct Registration {
        Handler handler;
        Enable  enable;      // optional
        Checked checked;     // optional
        std::string_view iconName;    // res/ icon stem; empty means no icon
        std::string_view tooltipKey;  // catalog key; empty falls back to the label key
    };

    ActionRegistry() = default;

    ActionRegistry(const ActionRegistry&) = delete;
    ActionRegistry& operator=(const ActionRegistry&) = delete;

    // Property 3: an error on the second call for the same id, and the FIRST registration survives.
    Result<void> register_(ActionId id, Registration registration);

    // Convenience for the overwhelmingly common shape: a handler, and optionally an enablement
    // predicate. The label key is not a parameter -- it comes from `ActionId.def`, which is the
    // whole point of D1-6. A `register_` taking a label would let a caller disagree with the table.
    Result<void> register_(ActionId id, Handler handler, Enable enable = {});

    // Properties 1 and 2. Returns `ErrorCode::NotFound` for an unregistered id and
    // `ErrorCode::Unavailable` for a registered-but-disabled one, each with its own message key.
    Result<void> dispatch(ActionId id, KeyContext context) const;

    bool isRegistered(ActionId id) const;

    // False for an unregistered id as well as for a disabled one. That conflation is correct HERE
    // and only here: a caller asking "should this button be greyed out" wants one answer, and
    // `RibbonTabBuilder` uses `isRegistered()` separately to choose which *tooltip* to show --
    // "arrives in Stage 7" versus "not available for this document".
    bool isEnabled(ActionId id) const;

    // False for anything that is not a toggle, so a caller can render a check state without
    // first asking whether one exists.
    bool isChecked(ActionId id) const;

    std::size_t registeredCount() const { return registeredCount_; }

    // Every declared id, with its registration state. Backs `--list-actions` (guide case A-25):
    // the ~260 declared-but-unregistered ids must be VISIBLE, because that list is what stops a
    // later stage silently forgetting to register something it declared.
    struct ActionState {
        ActionId id;
        bool     registered;
    };
    std::vector<ActionState> allActions() const;

private:
    struct Slot {
        Registration registration;
        bool         occupied = false;
    };

    // Indexed by ActionId, not a hash map: the id space is a dense enum known at compile time, so
    // this is one allocation and an O(1) lookup with no hashing and no collision story.
    std::array<Slot, kActionCount> slots_{};
    std::size_t registeredCount_ = 0;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_ACTIONREGISTRY_H
