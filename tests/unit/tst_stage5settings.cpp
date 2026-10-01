// Stage 5 T6 -- the ten `search/` keys D5-20 declares (`Stage5-TextSearch-Plan.md` §3), and their survival
// across a real restart. Guide case A-68's key half (`Stage5-TextSearch-Testing-Guide.md` §5.8).
//
// WHY A FILE OF ITS OWN, for `tst_stage3settings.cpp`'s reason: `tst_settings.cpp` tests the MECHANISM and
// passes with an empty table; this file tests the ROWS -- that they exist, in the group that makes the
// Search page ship, with the defaults their comments argue for -- and that a value written by one PROCESS
// is read back by another. Stage 4's I-15 was a preference that survived every in-process reload and no
// real restart, because QSettings answers a reload from the cache of what the same process wrote; so the
// restart here is two runs of this binary (`unit.search_settings.restart`, `RunSettingsRestart.cmake`), the
// first writing through the hidden case below and the second reading through the other.
//
// WHAT IT DOES NOT COVER (convention 19): that anything OBEYS the keys. `search/indexCacheMB` reaching the
// index is `app/CompositionRoot::applySearchSettings`, read by `tst_index`'s default case and not driven
// here; the four matching flags reach a search through `SearchQuery::fromSettings` (asserted below) and the
// find bar's toggles are T7's (A-73); the four content classes are read by nothing until T8 (A-76). The
// Search PAGE is `tests/ui/tst_viewsettings.cpp`'s.

#include <catch2/catch_test_macros.hpp>

#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"
#include "core/text/SearchQuery.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtGlobal>

#include <cstdio>
#include <string>
#include <vector>

using namespace rawpage;
using core::SettingsGroup;
namespace keys = core::keys;

namespace {

// The values the restart writes: every one of them OFF its default, so a reader that fell back to the
// default would be caught on every key.
void writeNonDefaults(core::Settings& settings)
{
    settings.set(keys::search::wholeWords, true);
    settings.set(keys::search::caseSensitive, true);
    settings.set(keys::search::ignoreDiacritics, true);
    settings.set(keys::search::useRegex, true);
    settings.set(keys::search::includeComments, true);
    settings.set(keys::search::includeBookmarks, true);
    settings.set(keys::search::includeFields, true);
    settings.set(keys::search::includeAttachments, true);
    settings.set(keys::search::maxResults, 10);
    settings.set(keys::search::indexCacheMB, 64);
}

// Every key, with a message naming the first that does not read back -- a restart probe that says only
// "failed" sends the next person to a debugger.
std::string checkNonDefaults(const core::Settings& settings)
{
    if (!settings.get(keys::search::wholeWords)) return "search/wholeWords";
    if (!settings.get(keys::search::caseSensitive)) return "search/caseSensitive";
    if (!settings.get(keys::search::ignoreDiacritics)) return "search/ignoreDiacritics";
    if (!settings.get(keys::search::useRegex)) return "search/useRegex";
    if (!settings.get(keys::search::includeComments)) return "search/includeComments";
    if (!settings.get(keys::search::includeBookmarks)) return "search/includeBookmarks";
    if (!settings.get(keys::search::includeFields)) return "search/includeFields";
    if (!settings.get(keys::search::includeAttachments)) return "search/includeAttachments";
    if (settings.get(keys::search::maxResults) != 10) return "search/maxResults";
    if (settings.get(keys::search::indexCacheMB) != 64) return "search/indexCacheMB";
    return {};
}

} // namespace

TEST_CASE("the ten Stage 5 search keys are the whole Search group", "[settings][stage5][A-68]")
{
    std::vector<std::string> search;
    for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys())
        if (info.group == SettingsGroup::Search) search.emplace_back(info.path);

    // THE WHOLE GROUP, in table order. A count alone would stay green if a key were renamed.
    CHECK(search == std::vector<std::string>{"search/wholeWords",       "search/caseSensitive",
                                             "search/ignoreDiacritics", "search/useRegex",
                                             "search/includeComments",  "search/includeBookmarks",
                                             "search/includeFields",    "search/includeAttachments",
                                             "search/maxResults",       "search/indexCacheMB"});
}

TEST_CASE("the search keys default to finding what was typed", "[settings][stage5][A-68]")
{
    QTemporaryDir directory;
    core::Settings settings(QDir(directory.path()).filePath(QStringLiteral("settings.ini")));
    CHECK_FALSE(settings.get(keys::search::wholeWords));
    CHECK_FALSE(settings.get(keys::search::caseSensitive));
    CHECK_FALSE(settings.get(keys::search::ignoreDiacritics));
    CHECK_FALSE(settings.get(keys::search::useRegex));
    CHECK_FALSE(settings.get(keys::search::includeComments));
    CHECK_FALSE(settings.get(keys::search::includeBookmarks));
    CHECK_FALSE(settings.get(keys::search::includeFields));
    CHECK_FALSE(settings.get(keys::search::includeAttachments));
    CHECK(settings.get(keys::search::maxResults) == 10000);
    CHECK(settings.get(keys::search::indexCacheMB) == 1024);

    // The ranges clamp rather than refuse (the table's rule 3): A-61 sets the bound to 10, which is legal.
    settings.set(keys::search::maxResults, 0);
    CHECK(settings.get(keys::search::maxResults) == 1);
    settings.set(keys::search::indexCacheMB, 1);
    CHECK(settings.get(keys::search::indexCacheMB) == 16);
}

TEST_CASE("a query takes its flags and its bound from the settings", "[settings][stage5][A-68]")
{
    QTemporaryDir directory;
    core::Settings settings(QDir(directory.path()).filePath(QStringLiteral("settings.ini")));
    const core::text::SearchQuery defaults = core::text::SearchQuery::fromSettings(settings, QStringLiteral("x"));
    CHECK_FALSE(defaults.flags.wholeWords);
    CHECK_FALSE(defaults.flags.caseSensitive);
    CHECK_FALSE(defaults.flags.ignoreDiacritics);
    CHECK_FALSE(defaults.flags.useRegex);
    CHECK(defaults.maxResults == 10000);

    writeNonDefaults(settings);
    const core::text::SearchQuery set = core::text::SearchQuery::fromSettings(settings, QStringLiteral("x"));
    CHECK(set.flags.wholeWords);
    CHECK(set.flags.caseSensitive);
    CHECK(set.flags.ignoreDiacritics);
    CHECK(set.flags.useRegex);
    CHECK(set.maxResults == 10);
    CHECK(set.pattern == QStringLiteral("x"));
}

TEST_CASE("the search keys read back from a copy of the file", "[settings][stage5][A-68]")
{
    // A-86's form (Stage 4 guide): read from a COPY, which QSettings has never seen this process write --
    // the in-process half of the restart, kept because it runs in every configuration.
    QTemporaryDir directory;
    const QString ini = QDir(directory.path()).filePath(QStringLiteral("settings.ini"));
    {
        core::Settings settings(ini);
        writeNonDefaults(settings);
        REQUIRE(settings.sync());
    }
    const QString copy = QDir(directory.path()).filePath(QStringLiteral("copy.ini"));
    REQUIRE(QFile::copy(ini, copy));
    const core::Settings reloaded(copy);
    CHECK(checkNonDefaults(reloaded).empty());
}

// ---- The restart, as two processes (`RunSettingsRestart.cmake`) -----------------------------------
// HIDDEN: each needs `RAWPAGE_SETTINGS_PROBE_INI`, and run on its own -- with no file written first -- the
// reader would fail for a reason that is not a defect.

TEST_CASE("search settings restart probe writes", "[.][restart]")
{
    const QString ini = qEnvironmentVariable("RAWPAGE_SETTINGS_PROBE_INI");
    REQUIRE_FALSE(ini.isEmpty());
    QFile::remove(ini);
    core::Settings settings(ini);
    writeNonDefaults(settings);
    REQUIRE(settings.sync());
}

TEST_CASE("search settings restart probe reads", "[.][restart]")
{
    const QString ini = qEnvironmentVariable("RAWPAGE_SETTINGS_PROBE_INI");
    REQUIRE_FALSE(ini.isEmpty());
    REQUIRE(QFile::exists(ini));
    const core::Settings settings(ini);
    const std::string missing = checkNonDefaults(settings);
    INFO("the key that did not survive the restart: " << missing);
    CHECK(missing.empty());
    std::printf("search settings restart: all ten keys read back by a second process\n");
}
