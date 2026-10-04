// core/settings/ComparePairs -- the Compare dialog's memory: for each older file, the newer file it was last
// compared against. The user's request, 2026-10-04 (`Stage5-TextSearch-Testing-Guide.md` A-126).
//
// TWO FREE FUNCTIONS OVER `Settings`, NOT A CLASS WITH STATE. The list lives in `documents/recentComparisons`
// and is read and written whole on each call: a comparison is opened a few times a session, and a cached copy
// would be a second store for two windows to disagree over. (Not `documents/compare*`: that prefix is the four
// comparison OPTIONS, a Preferences page's rows, and this is internal state.)
//
// KEYED ON THE OLDER FILE. Comparing `report-v1.pdf` against `report-v2.pdf` and then opening the dialog with
// `report-v1.pdf` in front offers `report-v2.pdf` again; another older file is unaffected. Noting a pair
// REPLACES that older file's previous one and moves it to the front, and the list keeps the `kCapacity` most
// recent, so it cannot grow with every file a person ever compared.
//
// THE MATCH IS ON A CANONICAL SPELLING AND IGNORES CASE. One file reached through `/` and `\`, or through a tab
// and through Browse with different capitals, is one key: every path RawPage sees is a Windows path, whose
// file system ignores case -- the same reasoning `RecentFiles` gives for normalizing separators by hand rather
// than by platform, so the portable build runs the shipping build's rule. What is STORED and returned is the
// spelling that was noted, because it is what the dialog shows.
//
// IT DOES NOT ASK WHETHER THE NEWER FILE STILL EXISTS -- `core/` touches no file system for this. The dialog
// asks, and falls back to its own default when the remembered file is gone.

#ifndef RAWPAGE_CORE_SETTINGS_COMPAREPAIRS_H
#define RAWPAGE_CORE_SETTINGS_COMPAREPAIRS_H

#include "core/settings/Settings.h"

#include <QString>

namespace rawpage::core {

struct ComparePairs {
    static constexpr int kCapacity = 20;

    // The newer file last compared against `older`, or an empty string when there is none.
    static QString newerFor(const Settings& settings, const QString& older);

    // Records that `older` was compared against `newer`. An empty path, or a file compared with itself, is
    // not recorded.
    static void note(Settings& settings, const QString& older, const QString& newer);
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_SETTINGS_COMPAREPAIRS_H
