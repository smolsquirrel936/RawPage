// core/doc/SaveMode -- how Save (`Ctrl+S`) writes a document, and the token `documents/saveMode` stores. Stage 7 T6,
// decision D7-25 (`Stage7-Assembly-Plan.md` §3), which declared the key Stage 3's D3-9 had sent to Stage 8.
//
// A HEADER OF ITS OWN for `core/text/CompareSensitivity.h`'s reason: the settings table names this enum, and
// `SettingsSchema.h` is included by every settings reader -- `core/doc/SaveCoordinator.h` would bring the backend
// interface into all of them.
//
// `incremental` (the default): the changes are appended after the file, every signed byte range intact (D3-4).
// `full`: Save rewrites the file as Save As does -- one revision of what it reaches, what page commands left behind
// gone -- and a signed document is asked about first, as Save As asks (`SaveCoordinator`'s header).

#ifndef RAWPAGE_CORE_DOC_SAVEMODE_H
#define RAWPAGE_CORE_DOC_SAVEMODE_H

#include <optional>
#include <string_view>

namespace rawpage::core {

enum class SaveMode { Incremental, Full };

inline constexpr std::string_view saveModeName(SaveMode mode) noexcept
{
    return mode == SaveMode::Full ? "full" : "incremental";
}

inline std::optional<SaveMode> saveModeFromName(std::string_view name) noexcept
{
    if (name == "incremental") return SaveMode::Incremental;
    if (name == "full") return SaveMode::Full;
    return std::nullopt;
}

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_SAVEMODE_H
