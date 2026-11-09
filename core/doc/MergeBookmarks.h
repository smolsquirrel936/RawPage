// core/doc/MergeBookmarks -- what bookmark a merge gives each file it merges, and the token
// `documents/mergeBookmarkPerFile` stores. Stage 7 T10, decision D7-16 (`Stage7-Assembly-Plan.md` §3).
//
// A HEADER OF ITS OWN for `core/doc/SaveMode.h`'s reason: the settings table names this enum, and `SettingsSchema.h`
// is included by every settings reader -- `core/doc/Merge.h` would bring the annotation model into all of them.
//
// `fileName` (the default): one top-level bookmark per merged file, titled with its file name without the `.pdf`;
// `title`: titled with the file's own `/Title` (its Info dictionary), its file name where it states none; `none`: no
// bookmark per file -- each file's own outline items are the merged document's top level, in the order merged.
// Either way a file's own outline comes with it (D7-16).

#ifndef RAWPAGE_CORE_DOC_MERGEBOOKMARKS_H
#define RAWPAGE_CORE_DOC_MERGEBOOKMARKS_H

#include <optional>
#include <string_view>

namespace rawpage::core {

enum class MergeBookmarks { None, FileName, Title };

inline constexpr MergeBookmarks kMergeBookmarks[] = {MergeBookmarks::None, MergeBookmarks::FileName, MergeBookmarks::Title};

inline constexpr std::string_view mergeBookmarksName(MergeBookmarks value) noexcept
{
    switch (value) {
    case MergeBookmarks::None: return "none";
    case MergeBookmarks::Title: return "title";
    case MergeBookmarks::FileName: break;
    }
    return "fileName";
}

inline std::optional<MergeBookmarks> mergeBookmarksFromName(std::string_view name) noexcept
{
    for (const MergeBookmarks value : kMergeBookmarks) {
        if (mergeBookmarksName(value) == name) return value;
    }
    return std::nullopt;
}

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_MERGEBOOKMARKS_H
