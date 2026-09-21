// The page-transition styles this product can DRAW -- Stage 4 T10, decision **D4-12**
// (`Stage4-ViewingSurface-Plan.md` §3).
//
// ===========================================================================================
// FOUR, BECAUSE `Features.md` §1.2 PROMISES FOUR -- AND ISO 32000 DEFINES TWELVE.
// ===========================================================================================
// *"Presentation transitions between pages (fade, wipe, dissolve, none) with a per-page override"*
// is the capability, and this enum is exactly that list. ISO 32000-1 12.4.4 defines twelve `/S`
// values (Split, Blinds, Box, Wipe, Dissolve, Glitter, R, Fly, Push, Cover, Uncover, Fade), so a
// real document can and does ask for one we do not draw -- `corpus/ordinary/transitions.pdf`
// carries a `/Split` for precisely that reason.
//
// **A style we cannot draw is reported as `None` and the file's own word is KEPT beside it**
// (`core::PageTransition::rawStyle`). Mapping `/Split` silently onto `Wipe` would be inventing a
// presentation the author did not ask for; mapping it to `None` and forgetting what it said would
// leave nobody able to answer *"why did that page not wipe"*. Stage 16 is where the other eight
// would be added, and it needs the raw name to know which ones matter.
//
// `Replace` is NOT a member and `/R` reads as `None`: they are the same thing to a viewer -- cut
// straight to the next page -- and two names for one behaviour is D1-6's complaint.
//
// ===========================================================================================
// THE TOKEN IS THE PDF NAME, LOWERCASED.
// ===========================================================================================
// `"fade"`, `"wipe"`, `"dissolve"`, `"none"` -- which is what `fullScreen/transition` stores in
// `settings.ini`, what `SettingsCodec` round-trips, and what the reader matches a `/S` against.
// One table of four words rather than a settings spelling and a PDF spelling that agree until
// somebody edits one (the argument `core/Units.h` makes for the unit suffix).

#ifndef RAWPAGE_CORE_TRANSITION_H
#define RAWPAGE_CORE_TRANSITION_H

#include <optional>
#include <string_view>

namespace rawpage::core {

enum class TransitionStyle {
    None,       // cut -- also what ISO's `/R` (Replace) and any style we cannot draw report as
    Fade,
    Wipe,
    Dissolve
};

inline constexpr std::string_view transitionStyleName(TransitionStyle style) noexcept
{
    switch (style) {
    case TransitionStyle::None:     return "none";
    case TransitionStyle::Fade:     return "fade";
    case TransitionStyle::Wipe:     return "wipe";
    case TransitionStyle::Dissolve: return "dissolve";
    }
    return "none";
}

// Matches the settings token AND the PDF `/S` name, which are the same four words in different
// cases -- so the caller lowercases once and asks here. Nothing else maps `/Split` or `/Blinds`:
// they are not members, so this answers `nullopt` and the reader records `None` with the raw name.
inline std::optional<TransitionStyle> transitionStyleFromName(std::string_view name) noexcept
{
    if (name == "none" || name == "r") return TransitionStyle::None;
    if (name == "fade") return TransitionStyle::Fade;
    if (name == "wipe") return TransitionStyle::Wipe;
    if (name == "dissolve") return TransitionStyle::Dissolve;
    return std::nullopt;
}

} // namespace rawpage::core

#endif // RAWPAGE_CORE_TRANSITION_H
