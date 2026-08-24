// Chord → action, per context, with per-user overrides.
//
// Seeded from `Features.md` §8 via `core/cmd/KeyBindingDefaults.def`. §8 owns the bindings; this
// class owns *resolution*, *overriding* and the *collision census*.
//
// ---------------------------------------------------------------------------------------------
// RESOLUTION ORDER IS THE MODEL: innermost context, then `Common`, then `Global`.
//
// That order is what lets `Ctrl+K` mean Preferences in the Viewer and Insert-hyperlink in Write
// mode. It is not a lookup optimisation and must not be reordered for one.
//
// ---------------------------------------------------------------------------------------------
// THE CENSUS, AND WHY IT HAS TWO CLASSES THAT MUST NEVER BE CONFLATED.
//
//   Class 1 -- a context redefines a chord `Global` also claims. **6** of these in §8 today:
//              Ctrl+D, Ctrl+K, F11, Ctrl+Shift+L, Ctrl+Shift+A, Ctrl+Shift+G. These are what
//              `shadows()` reports, because a user remap here can make a Global chord unreachable
//              inside one context.
//
//   Class 2 -- a context overrides a `Common` chord. **9** of these: Ctrl+X, Ctrl+C, Ctrl+V,
//              Ctrl+A, Delete, Backspace, Esc, F2, Ctrl+F. §8's Common heading says in its own
//              text that contexts may override it, so by construction these are NOT shadows.
//              Reporting them would make `shadows()` noisy, and a noisy report gets ignored.
//
// ---------------------------------------------------------------------------------------------
// AN OVERRIDE REPLACES AN ACTION'S CHORDS IN ITS CONTEXT -- IT DOES NOT ADD ONE. (T14.)
//
// D1-10's persisted family is `shortcuts/<context>/<action>` = one chord. That shape is the whole
// statement: an action has ONE chord per context, and a user override says what it is. So binding
// `file.open` to `Ctrl+J` SUPERSEDES its §8 default `Ctrl+O` in that context -- the default row is
// still in the table, and it no longer resolves. Two consequences worth stating because neither is
// obvious:
//
//   * A chord freed this way falls through the resolution order like any unbound chord. `Ctrl+O`
//     released inside `Viewer` reaches Common, then Global, and means whatever they say.
//   * An action with TWO §8 defaults loses both. `Ctrl+Y` and `Ctrl+Shift+Z` are both redo; a user
//     who moves redo to `Ctrl+R` has said what redo's chord is, and leaving two other keys firing
//     it would make the setting a lie.
//
// The rule is DERIVED from the overrides rather than stored, so nothing new is persisted and a
// settings file written before T14 means exactly what it meant. It was found by building the
// Shortcuts page: until something remapped a chord for a user rather than for a test, "rebind"
// adding a second working key looked indistinguishable from replacing the first.
//
// ---------------------------------------------------------------------------------------------
// Neither number is the **12** the document set quotes. Twelve counts *Write mode's* collisions
// specifically -- 8 overriding Viewer and 4 overriding Global, 0 overriding Common -- and Stage 13's
// exit criteria depend on that figure. Conflating the three is exactly what made the documented
// number look wrong before issue I-01 was resolved, so `tst_keybindings` asserts all three
// separately and this class exposes all three separately.

#ifndef RAWPAGE_CORE_CMD_KEYBINDINGSTORE_H
#define RAWPAGE_CORE_CMD_KEYBINDINGSTORE_H

#include "core/Result.h"
#include "core/cmd/ActionId.h"
#include "core/cmd/Chord.h"
#include "core/cmd/KeyContext.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rawpage::core {

// Why a §8 row exists in the store without an action. See KeyBindingDefaults.def for the rule that
// decides which rows get this: it is only for chords that move focus, selection or scroll inside
// the focused widget, which the widget already implements and a batch run would never invoke.
enum class BindingKind {
    Dispatched,    // resolves to an ActionId and goes through ActionRegistry
    WidgetNative,  // documented in §8, handled by the focused widget, deliberately not an action
};

struct KeyBinding {
    KeyContext              context = KeyContext::Global;
    Chord                   chord;
    std::optional<ActionId> action;   // empty exactly when kind == WidgetNative
    BindingKind             kind = BindingKind::Dispatched;
};

// One entry of the class-1 census: a context binding a chord that `Global` also claims.
struct Shadow {
    Chord      chord;
    KeyContext context;         // the context that redefines it
    ActionId   contextAction;   // what it means there
    ActionId   globalAction;    // what it would otherwise have meant
};

struct CollisionCensus {
    std::vector<Shadow> classOne;              // a context redefines a Global chord
    std::vector<Shadow> classTwo;              // a context overrides a Common chord
    std::vector<Shadow> writeModeOverViewer;   // the 8
    std::vector<Shadow> writeModeOverGlobal;   // the 4
    std::vector<Shadow> writeModeOverCommon;   // 0, asserted so a future edit that adds one is seen

    // The figure the document set quotes, scoped to Write mode.
    std::size_t writeModeTotal() const
    {
        return writeModeOverViewer.size() + writeModeOverGlobal.size() + writeModeOverCommon.size();
    }
};

class KeyBindingStore {
public:
    // Seeded from the §8 defaults. Cheap: a few hundred small inserts, once.
    KeyBindingStore();

    // THE lookup. Innermost context, then Common, then Global -- see the header comment.
    // Returns nothing for a chord that resolves only to a widget-native row, because there is no
    // action to dispatch; the caller lets the event through to the focused widget.
    std::optional<ActionId> lookup(const Chord& chord, KeyContext context) const;

    // Distinguishes "no binding at all" from "a binding that the widget handles". `ShortcutBinder`
    // needs the difference: the first may fall through to a parent, the second must NOT be
    // swallowed by the shortcut layer.
    std::optional<KeyBinding> resolve(const Chord& chord, KeyContext context) const;

    // Per-user override. Deliberately permissive: §8 ships six class-1 collisions by design, so
    // forbidding a shadowing remap would make the specified defaults illegal. A shadow is
    // REPORTED, never refused.
    //
    // REPLACES rather than adds -- see the header. Any previous override chord for this action in
    // this context is dropped first, so an action never holds two, which is the only shape the
    // `shortcuts/<context>/<action>` family can persist.
    Result<void> rebind(const Chord& chord, KeyContext context, ActionId action);

    // The user's chord for an action in a context, or nothing when they have not set one. The
    // question `bindingsFor` cannot answer, and what makes a Preferences row able to say whether
    // it is showing a default or a choice.
    std::optional<Chord> overrideChordFor(ActionId action, KeyContext context) const;

    // Removes a user override, restoring whatever §8 specifies. Not an error if none was set.
    void clearOverride(const Chord& chord, KeyContext context);
    void clearAllOverrides();

    std::optional<Chord> primaryChordFor(ActionId action, KeyContext context) const;

    // Every binding in one context, defaults and overrides merged. Ordered by chord so a
    // Preferences page and a test both get a stable list.
    std::vector<KeyBinding> bindingsFor(KeyContext context) const;
    std::vector<KeyBinding> allBindings() const;

    std::size_t defaultCount() const;
    std::size_t defaultCountFor(KeyContext context) const;

    // Class 1 only. See the header: the class-2 overrides are permitted by §8's own text and
    // including them here would bury the six that matter.
    std::vector<Shadow> shadows() const;

    CollisionCensus census() const;

    // --- Persistence, D1-10's `shortcuts/<context>/<action>` key family -----------------------
    // The store does not touch the filesystem: `core/settings/` owns that privilege, and
    // `tools/layer-lint` enforces it. These two convert to and from the flat key/value shape the
    // settings file stores, so T5 can wire them without this class growing an I/O dependency.
    struct OverrideEntry {
        KeyContext  context;
        std::string actionStringId;   // the PERMANENT name, not the enum value
        std::string chord;
    };
    std::vector<OverrideEntry> exportOverrides() const;

    // Applies saved overrides. A key naming an action that no longer exists is DROPPED with a log
    // line rather than failing the load -- a settings file from a newer build, or one referring to
    // an id a later stage retired without an alias, must not prevent startup. Returns how many were
    // dropped so a caller can report it.
    std::size_t importOverrides(const std::vector<OverrideEntry>& entries);

private:
    struct Key {
        KeyContext context;
        Chord      chord;
        friend bool operator<(const Key& a, const Key& b)
        {
            if (a.context != b.context) return a.context < b.context;
            return a.chord < b.chord;
        }
    };

    std::map<Key, KeyBinding> defaults_;
    std::map<Key, ActionId>   overrides_;

    std::optional<KeyBinding> resolveExact(const Chord& chord, KeyContext context) const;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_KEYBINDINGSTORE_H
