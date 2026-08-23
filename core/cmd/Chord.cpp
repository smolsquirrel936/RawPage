#include "core/cmd/Chord.h"

#include <array>

namespace rawpage::core {
namespace {

// The arrow glyphs Features.md §8 uses in its tables, mapped to spelled names. The document is
// written for a human reader, so it says `→`; a settings file key should not depend on a UTF-8
// arrow surviving a round trip through a code page (issues I-04 and I-07 are both that same
// hazard), so the canonical form spells them out.
struct KeyAlias { std::string_view from; std::string_view to; };

constexpr std::array<KeyAlias, 8> kKeyAliases{{
    {"\xE2\x86\x91", "Up"},      // U+2191
    {"\xE2\x86\x93", "Down"},    // U+2193
    {"\xE2\x86\x90", "Left"},    // U+2190
    {"\xE2\x86\x92", "Right"},   // U+2192
    {"Return",       "Enter"},
    {"Escape",       "Esc"},
    {"PageDown",     "Page Down"},
    {"PageUp",       "Page Up"},
}};

std::string canonicalKey(std::string_view key)
{
    for (const KeyAlias& alias : kKeyAliases)
        if (key == alias.from) return std::string(alias.to);
    return std::string(key);
}

bool isModifierWord(std::string_view word, std::uint8_t& modifiers)
{
    if (word == "Ctrl" || word == "Control") {
        modifiers |= static_cast<std::uint8_t>(ChordModifier::Ctrl);
        return true;
    }
    if (word == "Alt") {
        modifiers |= static_cast<std::uint8_t>(ChordModifier::Alt);
        return true;
    }
    if (word == "Shift") {
        modifiers |= static_cast<std::uint8_t>(ChordModifier::Shift);
        return true;
    }
    return false;
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return text;
}

} // namespace

std::optional<Chord> Chord::parse(std::string_view text)
{
    text = trim(text);
    if (text.empty()) return std::nullopt;

    std::uint8_t modifiers = 0;
    std::string_view rest = text;

    // Split on '+', but only while the piece is a modifier NAME. The last piece is the key, and it
    // may itself be '+' -- `+` is a real binding in the Page Thumbnails table ("larger thumbnails").
    // Splitting greedily on every '+' would turn that row into an empty key and lose it silently.
    while (true) {
        const std::size_t plus = rest.find('+');
        if (plus == std::string_view::npos) break;

        const std::string_view word = trim(rest.substr(0, plus));
        std::uint8_t probe = modifiers;
        if (!isModifierWord(word, probe)) break;

        modifiers = probe;
        rest = rest.substr(plus + 1);
    }

    const std::string_view key = trim(rest);
    if (key.empty()) return std::nullopt;

    // A bare modifier is a legitimate binding: §8's Global table binds `Alt` alone to the ribbon
    // key tips. It parses as a chord whose KEY is the modifier name and whose modifier set is
    // empty, which keeps "Alt" and "Alt+Enter" distinct.
    std::uint8_t ignored = 0;
    if (modifiers == 0 && isModifierWord(key, ignored))
        return Chord{0, std::string(key)};

    // Anything still containing a modifier separator or a backtick is not a single literal chord:
    // it is one of §8's descriptive cells such as `Ctrl+`arrows`. Reject rather than guess.
    if (key.find('`') != std::string_view::npos) return std::nullopt;
    if (key == "arrows" || key == "drag") return std::nullopt;

    return Chord{modifiers, canonicalKey(key)};
}

std::string Chord::toString() const
{
    std::string out;
    if (hasCtrl()) out += "Ctrl+";
    if (hasAlt()) out += "Alt+";
    if (hasShift()) out += "Shift+";
    out += key_;
    return out;
}

} // namespace rawpage::core
