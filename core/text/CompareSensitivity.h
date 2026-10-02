// core/text/CompareSensitivity -- which layers a comparison runs (`Features.md` §1.5's "text-only, text and
// layout, appearance"), and the token `documents/compareSensitivity` stores. Stage 5 T13, decisions D5-17 and
// D5-20 (`Stage5-TextSearch-Plan.md` §3).
//
// A HEADER OF ITS OWN, moved out of `core/text/DocumentComparer.h` at T13, for one reason: the settings table
// names this enum (`SettingsKeys.def`, rule 2's "one enum, not a parallel one"), and `SettingsSchema.h` is
// included by every settings reader in the product. Including the comparer there would put its threads,
// condition variables and raster types into all of them to read three words. `core/Transition.h` is the same
// shape for the same reason.
//
// THE TOKEN IS THE ENUMERATOR'S NAME in lower camel case -- "textOnly", "textAndLayout", "appearance" --
// because a settings file holds names and never numbers (`SettingsSchema.h`), and these are the names a
// person reading `settings.ini` would guess.

#ifndef RAWPAGE_CORE_TEXT_COMPARESENSITIVITY_H
#define RAWPAGE_CORE_TEXT_COMPARESENSITIVITY_H

#include <optional>
#include <string_view>

namespace rawpage::core::text {

// *Text only* aligns pages and diffs their words; *text and layout* adds the shifts of matched words; and
// *appearance* adds the 72 dpi raster comparison outside what the words claimed (D5-17).
enum class CompareSensitivity { TextOnly, TextAndLayout, Appearance };

inline constexpr std::string_view compareSensitivityName(CompareSensitivity sensitivity) noexcept
{
    switch (sensitivity) {
    case CompareSensitivity::TextOnly:      return "textOnly";
    case CompareSensitivity::TextAndLayout: return "textAndLayout";
    case CompareSensitivity::Appearance:    return "appearance";
    }
    return "textOnly";
}

inline std::optional<CompareSensitivity> compareSensitivityFromName(std::string_view name) noexcept
{
    if (name == "textOnly") return CompareSensitivity::TextOnly;
    if (name == "textAndLayout") return CompareSensitivity::TextAndLayout;
    if (name == "appearance") return CompareSensitivity::Appearance;
    return std::nullopt;
}

} // namespace rawpage::core::text

#endif // RAWPAGE_CORE_TEXT_COMPARESENSITIVITY_H
