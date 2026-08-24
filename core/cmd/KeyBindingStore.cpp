#include "core/cmd/KeyBindingStore.h"

#include "core/Log.h"

#include <algorithm>

namespace rawpage::core {
namespace {

constexpr QLatin1String kUnknownActionKey{"error.shortcut.unknownAction"};
constexpr QLatin1String kInvalidChordKey{"error.shortcut.invalidChord"};
constexpr QLatin1String kNativeChordKey{"error.shortcut.widgetNative"};

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

KeyBindingStore::KeyBindingStore()
{
    // The §8 seed. A chord that fails to parse here is a defect in the .def, not in user input, so
    // it is dropped with an error line rather than silently skipped -- `tst_keybindings` catches it
    // first by comparing against the document, but a broken row must be loud in production too.
#define RAWPAGE_BINDING(Context, ChordText, Enumerator)                                    \
    if (const std::optional<Chord> parsed = Chord::parse(ChordText)) {                      \
        defaults_.insert_or_assign(Key{KeyContext::Context, *parsed},                       \
            KeyBinding{KeyContext::Context, *parsed, ActionId::Enumerator,                  \
                       BindingKind::Dispatched});                                           \
    } else {                                                                                \
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)                                                 \
            << "KeyBindingStore: unparseable default chord" << ChordText;                   \
    }

#define RAWPAGE_BINDING_NATIVE(Context, ChordText, Reason)                                 \
    if (const std::optional<Chord> parsed = Chord::parse(ChordText)) {                      \
        defaults_.insert_or_assign(Key{KeyContext::Context, *parsed},                       \
            KeyBinding{KeyContext::Context, *parsed, std::nullopt,                          \
                       BindingKind::WidgetNative});                                         \
    } else {                                                                                \
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)                                                 \
            << "KeyBindingStore: unparseable native chord" << ChordText;                    \
    }

#include "core/cmd/KeyBindingDefaults.def"
}

std::optional<KeyBinding> KeyBindingStore::resolveExact(const Chord& chord, KeyContext context) const
{
    const Key key{context, chord};

    // A user override wins over the §8 default for the SAME context. It never leaks into another
    // context: remapping Ctrl+K in Write mode must not touch Preferences in the Viewer.
    if (const auto found = overrides_.find(key); found != overrides_.end())
        return KeyBinding{context, chord, found->second, BindingKind::Dispatched};

    if (const auto found = defaults_.find(key); found != defaults_.end()) {
        // SUPERSEDED? A default row whose action the user has bound to a DIFFERENT chord in this
        // context no longer resolves -- see the header. A widget-native row has no action and is
        // never superseded, which is right: those chords belong to the focused widget, not to a
        // command anybody can remap.
        if (found->second.action) {
            if (const std::optional<Chord> chosen = overrideChordFor(*found->second.action, context);
                chosen && !(*chosen == chord)) {
                return std::nullopt;
            }
        }
        return found->second;
    }

    return std::nullopt;
}

std::optional<KeyBinding> KeyBindingStore::resolve(const Chord& chord, KeyContext context) const
{
    // Innermost first. When the caller already asks about Global or Common, the loop below still
    // does the right thing -- it just finds the answer on the first or second step.
    if (!keyContextIsLayer(context)) {
        if (const std::optional<KeyBinding> inner = resolveExact(chord, context)) return inner;
    }

    if (const std::optional<KeyBinding> common = resolveExact(chord, KeyContext::Common))
        return common;

    return resolveExact(chord, KeyContext::Global);
}

std::optional<ActionId> KeyBindingStore::lookup(const Chord& chord, KeyContext context) const
{
    const std::optional<KeyBinding> binding = resolve(chord, context);
    if (!binding) return std::nullopt;
    return binding->action;   // empty for a widget-native row, which is the correct answer
}

Result<void> KeyBindingStore::rebind(const Chord& chord, KeyContext context, ActionId action)
{
    if (!chord.isValid())
        return Error{ErrorCode::InvalidArgument, kInvalidChordKey};

    if (static_cast<std::size_t>(action) >= kActionCount)
        return Error{ErrorCode::NotFound, kUnknownActionKey};

    // Refusing to remap ONTO a widget-native chord is the one restriction here. Binding an action
    // to Tab inside Forms — Fill would take the key away from the focus chain and leave the user
    // unable to move between fields, which is not a preference so much as a trap.
    const Key key{context, chord};
    if (const auto found = defaults_.find(key);
        found != defaults_.end() && found->second.kind == BindingKind::WidgetNative) {
        return Error{ErrorCode::InvalidArgument, kNativeChordKey, toQString(chord.toString())};
    }

    // REPLACE, NOT ADD. The persisted family holds one chord per (context, action), so an action
    // carrying two override chords is a state that cannot survive a restart -- the second write to
    // `shortcuts/<context>/<action>` would simply win, and which one it was would depend on map
    // order. Dropping the previous one here is what makes the in-memory store and the file agree.
    for (auto it = overrides_.begin(); it != overrides_.end();) {
        if (it->first.context == context && it->second == action && !(it->first.chord == chord))
            it = overrides_.erase(it);
        else
            ++it;
    }

    overrides_.insert_or_assign(key, action);

    // Reported, never refused: §8 ships six class-1 collisions by design, so forbidding a shadowing
    // remap would make the specified defaults themselves illegal.
    if (!keyContextIsLayer(context)) {
        if (const auto global = defaults_.find(Key{KeyContext::Global, chord});
            global != defaults_.end() && global->second.action) {
            RAWPAGE_LOG_INFO(LogSubsystem::Ui)
                << "KeyBindingStore:" << toQString(chord.toString()) << "in context"
                << toQString(keyContextName(context)) << "now shadows a Global binding";
        }
    }
    return {};
}

std::optional<Chord> KeyBindingStore::overrideChordFor(ActionId action, KeyContext context) const
{
    // A linear scan over the USER's overrides, which is a handful of entries rather than the 248
    // defaults. A second index would have to be kept in step with every rebind and every clear for
    // a lookup nobody performs in a loop.
    for (const auto& [key, bound] : overrides_)
        if (key.context == context && bound == action) return key.chord;
    return std::nullopt;
}

void KeyBindingStore::clearOverride(const Chord& chord, KeyContext context)
{
    overrides_.erase(Key{context, chord});
}

void KeyBindingStore::clearAllOverrides() { overrides_.clear(); }

std::optional<Chord> KeyBindingStore::primaryChordFor(ActionId action, KeyContext context) const
{
    // "Primary" means the first in the store's own chord order, which is stable across runs. Several
    // §8 rows bind two chords to one action (`Ctrl+Y` and `Ctrl+Shift+Z` both redo); a ribbon
    // tooltip shows one, and it must be the same one every time.
    for (const KeyBinding& binding : bindingsFor(context))
        if (binding.action == action) return binding.chord;
    return std::nullopt;
}

std::vector<KeyBinding> KeyBindingStore::bindingsFor(KeyContext context) const
{
    std::vector<KeyBinding> out;
    for (const auto& [key, binding] : defaults_) {
        if (key.context != context) continue;
        KeyBinding merged = binding;
        if (const auto over = overrides_.find(key); over != overrides_.end()) {
            merged.action = over->second;
            merged.kind = BindingKind::Dispatched;
            out.push_back(merged);
            continue;
        }

        // The same supersession `resolveExact` applies, applied to the LIST -- otherwise a
        // Preferences page and the keyboard would disagree about which chords exist, and the page
        // is where a user goes to find out.
        if (binding.action) {
            if (const std::optional<Chord> chosen = overrideChordFor(*binding.action, context);
                chosen && !(*chosen == key.chord)) {
                continue;
            }
        }
        out.push_back(merged);
    }

    // An override may bind a chord the defaults never mentioned.
    for (const auto& [key, action] : overrides_) {
        if (key.context != context) continue;
        if (defaults_.count(key) != 0) continue;
        out.push_back(KeyBinding{context, key.chord, action, BindingKind::Dispatched});
    }

    std::sort(out.begin(), out.end(),
              [](const KeyBinding& a, const KeyBinding& b) { return a.chord < b.chord; });
    return out;
}

std::vector<KeyBinding> KeyBindingStore::allBindings() const
{
    std::vector<KeyBinding> out;
    for (const auto& [key, binding] : defaults_) {
        KeyBinding merged = binding;
        if (const auto over = overrides_.find(key); over != overrides_.end()) {
            merged.action = over->second;
            merged.kind = BindingKind::Dispatched;
        }
        out.push_back(merged);
    }
    for (const auto& [key, action] : overrides_) {
        if (defaults_.count(key) != 0) continue;
        out.push_back(KeyBinding{key.context, key.chord, action, BindingKind::Dispatched});
    }
    return out;
}

std::size_t KeyBindingStore::defaultCount() const { return defaults_.size(); }

std::size_t KeyBindingStore::defaultCountFor(KeyContext context) const
{
    std::size_t count = 0;
    for (const auto& [key, binding] : defaults_) {
        (void)binding;
        if (key.context == context) ++count;
    }
    return count;
}

CollisionCensus KeyBindingStore::census() const
{
    CollisionCensus out;

    for (const auto& [key, binding] : defaults_) {
        if (keyContextIsLayer(key.context)) continue;   // Global and Common are the baselines
        if (!binding.action) continue;                  // a widget-native row overrides nothing

        // Class 1 -- this context redefines a chord Global claims.
        if (const auto global = defaults_.find(Key{KeyContext::Global, key.chord});
            global != defaults_.end() && global->second.action) {
            out.classOne.push_back(
                Shadow{key.chord, key.context, *binding.action, *global->second.action});
        }

        // Class 2 -- this context overrides a Common chord. Permitted by §8's own heading, so these
        // are deliberately NOT added to classOne and are never reported by shadows().
        if (const auto common = defaults_.find(Key{KeyContext::Common, key.chord});
            common != defaults_.end() && common->second.action) {
            out.classTwo.push_back(
                Shadow{key.chord, key.context, *binding.action, *common->second.action});
        }

        if (key.context != KeyContext::Write) continue;

        // The twelve, scoped to Write mode. Split by what Write is overriding, because §8 states
        // the figure as 8 + 4 + 0 and a total that matched with a different split would be a
        // different fact wearing the right number.
        if (const auto viewer = defaults_.find(Key{KeyContext::Viewer, key.chord});
            viewer != defaults_.end() && viewer->second.action) {
            out.writeModeOverViewer.push_back(
                Shadow{key.chord, key.context, *binding.action, *viewer->second.action});
        }
        if (const auto global = defaults_.find(Key{KeyContext::Global, key.chord});
            global != defaults_.end() && global->second.action) {
            out.writeModeOverGlobal.push_back(
                Shadow{key.chord, key.context, *binding.action, *global->second.action});
        }
        if (const auto common = defaults_.find(Key{KeyContext::Common, key.chord});
            common != defaults_.end() && common->second.action) {
            out.writeModeOverCommon.push_back(
                Shadow{key.chord, key.context, *binding.action, *common->second.action});
        }
    }

    return out;
}

std::vector<Shadow> KeyBindingStore::shadows() const
{
    // Class 1 only, plus any user override that has newly created one. The user's own remaps are
    // the reason this is a runtime query rather than a constant: §8's six are known, but a seventh
    // can appear the moment somebody rebinds something in Preferences.
    std::vector<Shadow> out = census().classOne;

    for (const auto& [key, action] : overrides_) {
        if (keyContextIsLayer(key.context)) continue;
        const auto global = defaults_.find(Key{KeyContext::Global, key.chord});
        if (global == defaults_.end() || !global->second.action) continue;

        const bool alreadyListed = std::any_of(out.begin(), out.end(), [&](const Shadow& shadow) {
            return shadow.chord == key.chord && shadow.context == key.context;
        });
        if (!alreadyListed)
            out.push_back(Shadow{key.chord, key.context, action, *global->second.action});
    }
    return out;
}

std::vector<KeyBindingStore::OverrideEntry> KeyBindingStore::exportOverrides() const
{
    std::vector<OverrideEntry> out;
    out.reserve(overrides_.size());
    for (const auto& [key, action] : overrides_) {
        out.push_back(OverrideEntry{key.context, std::string(actionStringId(action)),
                                    key.chord.toString()});
    }
    return out;
}

std::size_t KeyBindingStore::importOverrides(const std::vector<OverrideEntry>& entries)
{
    std::size_t dropped = 0;
    for (const OverrideEntry& entry : entries) {
        // actionFromStringId honours alias rows, so an id renamed in a later stage still resolves
        // and the user keeps their customization (D1-16).
        const std::optional<ActionId> action = actionFromStringId(entry.actionStringId);
        if (!action) {
            RAWPAGE_LOG_INFO(LogSubsystem::Ui)
                << "KeyBindingStore: dropping shortcut for unknown action"
                << toQString(entry.actionStringId)
                << "-- it was removed or renamed without an alias row";
            ++dropped;
            continue;
        }

        const std::optional<Chord> chord = Chord::parse(entry.chord);
        if (!chord) {
            RAWPAGE_LOG_INFO(LogSubsystem::Ui)
                << "KeyBindingStore: dropping unparseable saved chord"
                << toQString(entry.chord) << "for" << toQString(entry.actionStringId);
            ++dropped;
            continue;
        }

        // A stale entry must never prevent startup, so a rejected rebind is counted and skipped
        // rather than propagated.
        if (!rebind(*chord, entry.context, *action)) ++dropped;
    }
    return dropped;
}

} // namespace rawpage::core
