// Stage 3 T12 -- the two `documents/` keys D3-9 kept (`Stage3-Mutation-Plan.md` §3), and the one function that
// turns the first of them into a directory. Guide case A-82's key half (`Stage3-Mutation-Testing-Guide.md`
// §5.12).
//
// WHY A FILE OF ITS OWN, for `tst_stage2settings.cpp`'s reason: `tst_settings.cpp` tests the MECHANISM, and
// every case there passes with an empty table. This file tests the ROWS -- that they exist, in the group that
// makes the Documents page ship, with the defaults D3-6 and D3-9 argue for, and that they survive a real INI.
//
// WHAT IT DOES NOT COVER (convention 19): that the product OBEYS either key. `documents/recoveryDir` reaching a
// new journal and the prompt is `tests/ui/tst_recoverydialog.cpp`'s; `documents/warnOnRepairedSave` off still
// forcing Save-As is `tests/ui/tst_mutationcommands.cpp`'s (A-77).

#include <catch2/catch_test_macros.hpp>

#include "core/Log.h"
#include "core/cmd/RecoveryJournal.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"

#include <QDir>
#include <QTemporaryDir>

#include <string>
#include <vector>

using namespace rawpage;
using core::SettingsGroup;
namespace keys = core::keys;

TEST_CASE("the two Stage 3 keys are declared in the Documents group and only Stage 5's four joined it", "[settings][stage3]")
{
    std::vector<std::string> documents;
    for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys())
        if (info.group == SettingsGroup::Documents) documents.emplace_back(info.path);

    // THE WHOLE GROUP, in table order -- Stage 1's two rows, then D3-9's two, then since Stage 5 T13 the four
    // comparison rows (D5-20). A count alone would stay green if `documents/saveMode` (Stage 8's) arrived early
    // in place of one of them.
    CHECK(documents == std::vector<std::string>{"documents/recentCount", "documents/recentPinned",
                                                "documents/recoveryDir", "documents/warnOnRepairedSave",
                                                "documents/compareSensitivity", "documents/compareIgnoreHeaderFooter",
                                                "documents/compareIgnorePageNumbers", "documents/compareIgnoreWhitespace"});

    // D3-9 MOVED TWO KEYS AWAY; they must not be declared anywhere.
    for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys()) {
        CHECK(info.path != "documents/saveMode");
        CHECK(info.path != "documents/autosaveIntervalSec");
    }
}

TEST_CASE("the Stage 3 keys default to the default root and to explaining", "[settings][stage3]")
{
    QTemporaryDir directory;
    core::Settings settings(QDir(directory.path()).filePath(QStringLiteral("settings.ini")));
    CHECK(settings.get(keys::documents::recoveryDir).isEmpty());
    CHECK(settings.get(keys::documents::warnOnRepairedSave) == true);
}

TEST_CASE("the Stage 3 keys round-trip through a real settings file", "[settings][stage3]")
{
    QTemporaryDir directory;
    const QString ini = QDir(directory.path()).filePath(QStringLiteral("settings.ini"));
    // A Windows path with a space, a non-ASCII name and backslashes: every character QSettings' INI format
    // escapes or might, in one value.
    const QString folder = QStringLiteral("C:\\Users\\someone\\R\u00e9cup\u00e9ration dir\\\u5fa9\u539f");
    {
        core::Settings settings(ini);
        settings.set(keys::documents::recoveryDir, folder);
        settings.set(keys::documents::warnOnRepairedSave, false);
        REQUIRE(settings.sync());
    }
    core::Settings reloaded(ini);
    CHECK(reloaded.get(keys::documents::recoveryDir) == folder);
    CHECK(reloaded.get(keys::documents::warnOnRepairedSave) == false);

    // And back: a value set to its default reads as the default after a reload.
    {
        core::Settings settings(ini);
        settings.set(keys::documents::recoveryDir, QString());
        settings.set(keys::documents::warnOnRepairedSave, true);
        REQUIRE(settings.sync());
    }
    core::Settings again(ini);
    CHECK(again.get(keys::documents::recoveryDir).isEmpty());
    CHECK(again.isAtDefault(keys::documents::warnOnRepairedSave));
}

TEST_CASE("the recovery root is the setting when it is absolute, then the composition's, then the default", "[settings][stage3][recovery]")
{
    QTemporaryDir directory;
    const QString configured = QDir(directory.path()).filePath(QStringLiteral("configured"));
    const QString fallback = QDir(directory.path()).filePath(QStringLiteral("fallback"));
    const QString defaultRoot = core::RecoveryJournal::defaultRoot();

    CHECK(core::RecoveryJournal::resolveRoot(configured, fallback) == QDir::cleanPath(configured));
    CHECK(core::RecoveryJournal::resolveRoot(QString(), fallback) == fallback);
    CHECK(core::RecoveryJournal::resolveRoot(QStringLiteral("   "), fallback) == fallback);
    CHECK(core::RecoveryJournal::resolveRoot(QString(), QString()) == defaultRoot);
    CHECK(core::RecoveryJournal::resolveRoot(configured + QStringLiteral("/./x/.."), QString()) == QDir::cleanPath(configured));

    // A RELATIVE PATH IS IGNORED, and said: it would resolve against whatever directory the process started in.
    std::vector<std::string> warnings;
    core::setLogSink([&warnings](const core::LogRecordData& record) {
        if (record.level == core::LogLevel::Warning) warnings.push_back(record.message.toStdString());
    });
    CHECK(core::RecoveryJournal::resolveRoot(QStringLiteral("recovery"), fallback) == fallback);
    CHECK(core::RecoveryJournal::resolveRoot(QStringLiteral("..\\elsewhere"), QString()) == defaultRoot);
    core::setLogSink({});
    REQUIRE(warnings.size() == 2);
    CHECK(warnings.front().find("documents/recoveryDir") != std::string::npos);
}
