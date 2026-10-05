// Stage 6 T6 -- the three `identity/` keys D6-13 declares (`Stage6-Annotations-Plan.md` §3), the rule that
// reads them, and convention 11's control for it. Guide cases A-53's control and A-54's key half
// (`Stage6-Annotations-Testing-Guide.md` §5.8).
//
// WHY A FILE OF ITS OWN, for `tst_stage5settings.cpp`'s reason: `tst_settings.cpp` tests the MECHANISM and
// passes with an empty table; this file tests the ROWS -- that they exist, in the group that makes the
// Identity page ship, with the default their comment argues for.
//
// CONVENTION 11'S CONTROL. *"Empty means the login name"* is a DOMAIN rule (`core::identityAuthor`) over a
// name the HOST supplies (`app::Services::loginName`); so no file under `core/` may ask the environment or
// the operating system who the user is. Read as text, every source file of `core/`, here -- a grep that runs
// with the suite rather than one a person must remember.
//
// WHAT IT DOES NOT COVER (convention 19): that the product writes the name (A-53, through the note tool, is
// `tests/composition/tst_markupink.cpp`'s), or the Identity PAGE (`tests/ui/tst_viewsettings.cpp`).

#include <catch2/catch_test_macros.hpp>

#include "core/doc/Annotation.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"

#include <QTemporaryDir>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

using namespace rawpage;
using core::SettingsGroup;
namespace keys = core::keys;

TEST_CASE("the three identity keys are declared in the Identity group and empty by default", "[stage6settings][A-54][D6-13]")
{
    std::set<std::string> paths;
    for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys()) {
        if (info.group != SettingsGroup::Identity) continue;
        paths.insert(std::string(info.path));
    }
    CHECK(paths == std::set<std::string>{"identity/author", "identity/email", "identity/organization"});

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    core::Settings settings(scratch.filePath(QStringLiteral("settings.ini")));
    CHECK(settings.get(keys::identity::author).isEmpty());
    CHECK(settings.get(keys::identity::email).isEmpty());
    CHECK(settings.get(keys::identity::organization).isEmpty());

    // EMPTY MEANS THE LOGIN NAME, through the one rule; set, the setting wins.
    CHECK(core::identityAuthor(settings.get(keys::identity::author), QStringLiteral("login")) == QStringLiteral("login"));
    settings.set(keys::identity::author, QStringLiteral("Ada Reviewer"));
    CHECK(core::identityAuthor(settings.get(keys::identity::author), QStringLiteral("login")) == QStringLiteral("Ada Reviewer"));
}

TEST_CASE("no core file asks the environment or the operating system who the user is", "[stage6settings][A-53][convention-11]")
{
    const std::filesystem::path core = std::filesystem::path(RAWPAGE_CORPUS_DIR).parent_path() / "core";
    REQUIRE(std::filesystem::is_directory(core));
    const char* forbidden[] = {"getenv", "qgetenv", "qEnvironmentVariable", "QProcessEnvironment", "GetUserName",
                               "USERNAME", "getlogin", "getpwuid"};
    int files = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(core)) {
        const std::string extension = entry.path().extension().string();
        if (!entry.is_regular_file() || (extension != ".h" && extension != ".cpp" && extension != ".def")) continue;
        ++files;
        std::ifstream in(entry.path(), std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        const std::string source = text.str();
        for (const char* word : forbidden) {
            INFO(entry.path().string() << " names " << word);
            CHECK(source.find(word) == std::string::npos);
        }
    }
    CHECK(files > 100);   // the walk found core/ at all (convention 7)
}
