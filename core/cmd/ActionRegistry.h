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

#include <QString>

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

        // WHY THIS COMMAND WOULD BE REFUSED, as a catalog key -- shown INSTEAD of `tooltipKey`
        // while `enable` answers false. Added at Stage 2 T14 with Stage 1's I-53, and it is a
        // SECOND field rather than a reuse of the one above: they are shown in mutually exclusive
        // states and mean opposite things ("what this does" against "why it cannot"), so one field
        // serving both would put "needs a document to be open" on an enabled button -- which is
        // what the first attempt at I-53 actually did.
        //
        // Empty falls back to the generic *not available for this document*. A registration should
        // fill it in whenever it can be more specific: "this is already the first page" is useful
        // and the generic sentence is not, on a command that is plainly looking at a document.
        std::string_view unavailableKey;

        // TEXT THAT DEPENDS ON THE DOCUMENT IN FRONT, added at Stage 3 T10. A key cannot say *Undo rotate page 3
        // by 90 degrees* or *this document is encrypted*: the first needs the command's own name and the
        // second the document's refusal. Each function is written in `ui/`, where `tr()` lives, and returns
        // already-resolved text -- `core/` stores it and never produces a sentence (convention 5). An empty
        // function, or one returning an empty string, falls back to the key above it.
        std::function<QString()> tooltipText;        // while available
        std::function<QString()> unavailableText;    // while refused
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

    // Whether a `checked` predicate was supplied at all -- the question `isChecked` deliberately
    // cannot answer, because it conflates "not a toggle" with "a toggle that is off".
    //
    // ADDED AT T11, and by a defect rather than by a plan. `ui/shell/ActionFactory`'s header has
    // always promised that "a toggle is checkable, and only a toggle is", and the code called
    // `QAction::setChecked` without ever calling `setCheckable` -- which on a non-checkable action
    // does nothing at all. Nothing had a toggle until the QAT's placement item, so the promise had
    // never been tested against a subject that could fail it. (Issue I-21.)
    bool isToggle(ActionId id) const;

    // The registration's tooltip key, or an empty view when it named none -- in which case the
    // caller falls back to the label key, as the field's own comment says. Also added at T11: the
    // field has been in `Registration` since T3 and nothing could read it, so a registration that
    // supplied one was silently discarding it.
    std::string_view tooltipKey(ActionId id) const;

    // The registration's *why it is refused* key, or empty. Read by `ui/shell/ActionFactory` only
    // while the action is disabled -- see `Registration::unavailableKey`.
    std::string_view unavailableKey(ActionId id) const;

    // The registration's dynamic text (Stage 3 T10), or an empty string when it supplied none.
    QString tooltipText(ActionId id) const;
    QString unavailableText(ActionId id) const;

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
