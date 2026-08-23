// A key chord, as `Features.md` §8 spells one: `Ctrl+Shift+P`.
//
// WHY THIS TYPE EXISTS AT ALL, given that Qt has `QKeySequence`:
// `QKeySequence` lives in **QtGui**, and `core/` may not include QtGui — `tools/layer-lint` denies it
// by name and `core/` links only `Qt6::Core`, so the include would fail the gate and then fail the
// link. That is not an obstacle to route around; it is the layer contract doing its job. The binding
// model is domain logic: which chord means which action in which context is a fact about the product,
// not about the widget toolkit, and it has to be testable in the headless suite with no display.
//
// [`Implementation-Plan.md`'s Stage 1 component list](../../claude-plan/Implementation-Plan.md)
// sketches the signature as `lookup(QKeySequence, KeyContext)`. That was written before the layer
// rule was applied to it; `ui/shell/ShortcutBinder` converts a `QKeySequence` to a `Chord` at the
// boundary, which is exactly where a Qt type should stop.
//
// SCOPE: a single chord, not a sequence. §8 contains no multi-stroke bindings, and a two-stroke
// sequence (`Ctrl+K, Ctrl+C`) would need a pending-state machine in `ShortcutBinder` rather than a
// richer value type here. If one is ever specified, this is the file that grows a `next` field.

#ifndef RAWPAGE_CORE_CMD_CHORD_H
#define RAWPAGE_CORE_CMD_CHORD_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rawpage::core {

enum class ChordModifier : std::uint8_t {
    None  = 0,
    Ctrl  = 1 << 0,
    Alt   = 1 << 1,
    Shift = 1 << 2,
};

constexpr std::uint8_t operator|(ChordModifier a, ChordModifier b)
{
    return static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

class Chord {
public:
    Chord() = default;
    Chord(std::uint8_t modifiers, std::string key)
        : modifiers_(modifiers), key_(std::move(key)) {}

    // Parses §8's spelling. Returns nullopt for anything that is not a single literal chord --
    // `arrows`, `Ctrl+`arrows`, `Shift`+drag` -- so a caller must decide what to do about those
    // rather than silently receiving a wrong chord. `tst_keybindings` treats an unparseable cell it
    // has not explicitly classified as a FAILURE naming the row, which is D1-9's strictness.
    static std::optional<Chord> parse(std::string_view text);

    // Canonical spelling, modifiers in the fixed order Ctrl, Alt, Shift. §8 contains no chord with
    // both Alt and Shift, so this round-trips every row in the document today; the order is fixed
    // anyway, because a chord is a settings-file key (`shortcuts/<context>/<action>`) and two
    // spellings of one chord would be two keys.
    std::string toString() const;

    std::uint8_t     modifiers() const { return modifiers_; }
    std::string_view key() const { return key_; }
    bool             isValid() const { return !key_.empty(); }

    bool hasCtrl() const  { return (modifiers_ & static_cast<std::uint8_t>(ChordModifier::Ctrl)) != 0; }
    bool hasAlt() const   { return (modifiers_ & static_cast<std::uint8_t>(ChordModifier::Alt)) != 0; }
    bool hasShift() const { return (modifiers_ & static_cast<std::uint8_t>(ChordModifier::Shift)) != 0; }

    friend bool operator==(const Chord& a, const Chord& b)
    {
        return a.modifiers_ == b.modifiers_ && a.key_ == b.key_;
    }
    friend bool operator!=(const Chord& a, const Chord& b) { return !(a == b); }

    // Ordering is for map keys only and has no meaning to a user. Modifiers first so the comparison
    // is cheap in the common case.
    friend bool operator<(const Chord& a, const Chord& b)
    {
        if (a.modifiers_ != b.modifiers_) return a.modifiers_ < b.modifiers_;
        return a.key_ < b.key_;
    }

private:
    std::uint8_t modifiers_ = 0;
    std::string  key_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_CHORD_H
