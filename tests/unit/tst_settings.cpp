// Stage 1 T5 -- core/settings/SettingsSchema and core/settings/Settings.
//
// Guide cases A-40 … A-45 and A-47 in Stage1-AppShell-Testing-Guide.md 5.5. A-41 is not here: an
// undeclared key must fail to COMPILE, so it is a compile fixture driven from tests/unit/CMakeLists
// rather than a case in a binary that by definition compiled.
//
// Every case that touches a file uses its own QTemporaryDir. QSettings keeps a process-wide cache
// keyed by path, so two cases sharing one settings file would pass or fail depending on the order
// ctest happened to run them in -- and a flake in the settings suite is the kind that gets rerun
// until it is green rather than investigated.

#include <catch2/catch_test_macros.hpp>

#include "core/Log.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"
#include "tests/unit/fixtures/ActionAliasFixture.h"

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <array>
#include <set>
#include <string>

using namespace rawpage::core;

namespace {

QString writeIni(const QTemporaryDir& dir, const QString& contents)
{
    const QString path = dir.path() + QStringLiteral("/settings.ini");
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&file);
    out << contents;
    out.flush();
    file.close();
    return path;
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

// Captures log output for the cases that assert something was REPORTED. A clamp that happens
// silently is a clamp nobody can diagnose, so "and logged" is part of the requirement, not
// decoration.
class LogCapture {
public:
    LogCapture()
    {
        previousThreshold_ = logThreshold();
        setLogThreshold(LogLevel::Trace);
        setLogSink([this](const LogRecordData& record) { lines_.push_back(record.message.toStdString()); });
    }
    ~LogCapture()
    {
        setLogSink({});
        setLogThreshold(previousThreshold_);
    }

    bool sawContaining(const std::string& fragment) const
    {
        for (const std::string& line : lines_) {
            if (line.find(fragment) != std::string::npos) return true;
        }
        return false;
    }
    std::size_t count() const { return lines_.size(); }

private:
    std::vector<std::string> lines_;
    LogLevel previousThreshold_ = LogLevel::Info;
};

} // namespace

// =======================================================================================
// The schema itself
// =======================================================================================

TEST_CASE("every declared key has a unique path and a real group", "[settings][schema]")
{
    std::set<std::string> paths;
    for (const SettingsKeyInfo& info : SettingsSchema::allKeys()) {
        const std::string path(info.path);
        INFO("key " << path);
        CHECK(!path.empty());
        CHECK(path.find('/') != std::string::npos);          // group/name, matching the INI shape
        CHECK(settingsGroupName(info.group) != "?");
        CHECK(paths.insert(path).second);                    // a duplicate path is two keys, one file line
    }
    CHECK(paths.size() == SettingsSchema::allKeys().size());
}

TEST_CASE("the twelve Stage 1 keys are all declared", "[settings][schema]")
{
    // The plan of record's list, spelled out. If a key is renamed without a migration this fails
    // here rather than as a silently reset preference on a user's machine.
    const std::vector<std::string> expected{
        "general/theme", "general/uiFontScale", "general/touchMode", "general/restoreSession",
        "general/singleInstance", "language/uiLocale", "logging/level", "logging/subsystems",
        "logging/retainFiles", "documents/recentCount", "documents/recentPinned",
    };
    for (const std::string& path : expected) {
        INFO("declared key " << path);
        CHECK(SettingsSchema::findKey(QString::fromStdString(path)) != nullptr);
    }

    // The twelfth is the shortcuts FAMILY, which is not a row and must still be a declared path.
    const ShortcutKey key = ShortcutKey::forAction(KeyContext::Viewer, ActionId::FileOpen);
    CHECK(key.path() == QStringLiteral("shortcuts/Viewer/file.open"));
    CHECK(SettingsSchema::isDeclaredPath(key.path()));
}

TEST_CASE("a shortcuts path is a family member only if both halves resolve", "[settings][schema]")
{
    CHECK(SettingsSchema::isDeclaredPath(QStringLiteral("shortcuts/Write/file.open")));
    CHECK_FALSE(SettingsSchema::isDeclaredPath(QStringLiteral("shortcuts/Nowhere/file.open")));
    CHECK_FALSE(SettingsSchema::isDeclaredPath(QStringLiteral("shortcuts/Viewer/file.notAnAction")));
    CHECK_FALSE(SettingsSchema::isDeclaredPath(QStringLiteral("shortcuts/Viewer")));
    CHECK_FALSE(SettingsSchema::isDeclaredPath(QStringLiteral("shortcuts/Viewer/file.open/extra")));
}

TEST_CASE("an internal key is declared but is not a Preferences row", "[settings][schema]")
{
    const SettingsKeyInfo* version = SettingsSchema::findKey(QStringLiteral("meta/schemaVersion"));
    REQUIRE(version != nullptr);
    CHECK(version->group == SettingsGroup::Internal);

    const SettingsKeyInfo* recent = SettingsSchema::findKey(QStringLiteral("documents/recentList"));
    REQUIRE(recent != nullptr);
    CHECK(recent->group == SettingsGroup::Internal);
}

// =======================================================================================
// A-40 -- typed round-trip and defaults
// =======================================================================================

TEST_CASE("an absent file yields every declared default", "[settings]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));

    CHECK(settings.get(keys::general::theme) == Theme::FollowSystem);
    CHECK(settings.get(keys::general::uiFontScale) == 1.0);
    CHECK(settings.get(keys::general::touchMode) == false);
    CHECK(settings.get(keys::general::restoreSession) == true);
    CHECK(settings.get(keys::general::singleInstance) == true);
    CHECK(settings.get(keys::language::uiLocale).isEmpty());
    CHECK(settings.get(keys::logging::level) == LogLevel::Info);
    CHECK(settings.get(keys::logging::subsystems) == LogSubsystemMask::all());
    CHECK(settings.get(keys::logging::retainFiles) == 5);
    CHECK(settings.get(keys::documents::recentCount) == 10);
    CHECK(settings.get(keys::documents::recentPinned).isEmpty());
}

TEST_CASE("every type round-trips through a real file", "[settings]")
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/settings.ini");

    {
        Settings settings(path);
        settings.set(keys::general::theme, Theme::Dark);
        settings.set(keys::general::uiFontScale, 1.25);
        settings.set(keys::general::touchMode, true);
        settings.set(keys::language::uiLocale, QStringLiteral("zh_TW"));
        settings.set(keys::logging::level, LogLevel::Debug);
        settings.set(keys::logging::retainFiles, 7);
        settings.set(keys::documents::recentPinned,
                     QStringList{QStringLiteral("C:/a.pdf"), QStringLiteral("C:/b.pdf")});

        LogSubsystemMask mask = LogSubsystemMask::none();
        mask.add(LogSubsystem::Render);
        mask.add(LogSubsystem::Parse);
        settings.set(keys::logging::subsystems, mask);

        REQUIRE(settings.sync());
    }

    Settings reloaded(path);
    CHECK(reloaded.get(keys::general::theme) == Theme::Dark);
    CHECK(reloaded.get(keys::general::uiFontScale) == 1.25);
    CHECK(reloaded.get(keys::general::touchMode) == true);
    CHECK(reloaded.get(keys::language::uiLocale) == QStringLiteral("zh_TW"));
    CHECK(reloaded.get(keys::logging::level) == LogLevel::Debug);
    CHECK(reloaded.get(keys::logging::retainFiles) == 7);
    CHECK(reloaded.get(keys::documents::recentPinned).size() == 2);

    const LogSubsystemMask mask = reloaded.get(keys::logging::subsystems);
    CHECK(mask.contains(LogSubsystem::Render));
    CHECK(mask.contains(LogSubsystem::Parse));
    CHECK_FALSE(mask.contains(LogSubsystem::Ocr));
}

TEST_CASE("a one-element string list survives the QSettings INI round trip", "[settings]")
{
    // The gotcha this exists for: QSettings writes a one-element list as a bare value, and
    // QVariant::toStringList() on the way back yields an EMPTY list. The symptom would be "pinning
    // exactly one file loses it on restart" -- which is the first thing anyone tries.
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/settings.ini");

    {
        Settings settings(path);
        settings.set(keys::documents::recentPinned, QStringList{QStringLiteral("C:/only.pdf")});
        REQUIRE(settings.sync());
    }

    Settings reloaded(path);
    const QStringList pinned = reloaded.get(keys::documents::recentPinned);
    REQUIRE(pinned.size() == 1);
    CHECK(pinned.at(0) == QStringLiteral("C:/only.pdf"));
}

TEST_CASE("enums are persisted by name, not by number", "[settings]")
{
    // If this ever writes `general/theme=1`, inserting a value into the enum silently changes what
    // every existing settings file means.
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        settings.set(keys::general::theme, Theme::Dark);
        settings.set(keys::logging::level, LogLevel::Warning);
        REQUIRE(settings.sync());
    }

    const QByteArray text = readBytes(path);
    CHECK(text.contains("theme=dark"));
    CHECK(text.contains("level=warning"));
}

TEST_CASE("reset and isAtDefault agree with the declared default", "[settings]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));

    CHECK(settings.isAtDefault(keys::general::theme));
    settings.set(keys::general::theme, Theme::Light);
    CHECK_FALSE(settings.isAtDefault(keys::general::theme));
    settings.reset(keys::general::theme);
    CHECK(settings.isAtDefault(keys::general::theme));
    CHECK(settings.get(keys::general::theme) == Theme::FollowSystem);
}

// =======================================================================================
// A-42 -- changed(Key) fires only on an actual change
// =======================================================================================

TEST_CASE("a set to the current value emits nothing", "[settings]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));

    int notifications = 0;
    QString lastPath;
    const int token = settings.subscribe([&](const Settings::Change& change) {
        ++notifications;
        lastPath = change.path;
    });

    settings.set(keys::general::uiFontScale, 1.5);
    CHECK(notifications == 1);
    CHECK(lastPath == QStringLiteral("general/uiFontScale"));

    settings.set(keys::general::uiFontScale, 1.5);
    CHECK(notifications == 1);            // the same value is not a change

    settings.set(keys::general::uiFontScale, 1.0);
    CHECK(notifications == 2);

    settings.unsubscribe(token);
    settings.set(keys::general::uiFontScale, 1.75);
    CHECK(notifications == 2);            // and an unsubscribed handler stops hearing about it
}

TEST_CASE("a clamped set that changes nothing emits nothing", "[settings]")
{
    // From Stage 2 this signal invalidates the render cache (flow J). A `set` of 9.0 that clamps to
    // the 2.0 already stored must not look like a change, or every out-of-range write costs a full
    // re-render that gets blamed on the renderer.
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));

    int notifications = 0;
    settings.subscribe([&](const Settings::Change&) { ++notifications; });

    settings.set(keys::general::uiFontScale, 2.0);
    CHECK(notifications == 1);
    settings.set(keys::general::uiFontScale, 9.0);     // clamps to 2.0, which is what is stored
    CHECK(notifications == 1);
}

TEST_CASE("the change carries the key's group", "[settings]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));

    SettingsGroup seen = SettingsGroup::Internal;
    settings.subscribe([&](const Settings::Change& change) { seen = change.group; });

    settings.set(keys::logging::retainFiles, 9);
    CHECK(seen == SettingsGroup::Logging);
}

// =======================================================================================
// A-43 -- out-of-range values are clamped and logged, not rejected
// =======================================================================================

TEST_CASE("a hand-edited out-of-range value is clamped at load and reported", "[settings]")
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[general]\nuiFontScale=9.0\n\n[logging]\nretainFiles=9999\n"));

    LogCapture log;
    Settings settings(path);

    CHECK(settings.get(keys::general::uiFontScale) == 2.0);
    CHECK(settings.get(keys::logging::retainFiles) == 100);
    CHECK(log.sawContaining("clamped"));
}

TEST_CASE("a value below the minimum clamps upward", "[settings]")
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral("[general]\nuiFontScale=0.1\n"));
    Settings settings(path);
    CHECK(settings.get(keys::general::uiFontScale) == 0.75);
}

TEST_CASE("an out-of-range value set from code is clamped too", "[settings]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    settings.set(keys::documents::recentCount, 5000);
    CHECK(settings.get(keys::documents::recentCount) == 50);
    settings.set(keys::documents::recentCount, -3);
    CHECK(settings.get(keys::documents::recentCount) == 0);
}

// =======================================================================================
// A-44 -- a malformed settings file does not prevent startup
// =======================================================================================

TEST_CASE("a truncated file starts on defaults", "[settings]")
{
    QTemporaryDir dir;
    // Truncated mid-line, exactly as an interrupted write would leave it.
    const QString path = writeIni(dir, QStringLiteral("[general]\ntheme=dark\nuiFontSc"));

    Settings settings(path);
    CHECK(settings.get(keys::general::uiFontScale) == 1.0);
    // What survived is still honoured: falling back to defaults for the WHOLE file would throw away
    // readable settings because one line was cut.
    CHECK(settings.get(keys::general::theme) == Theme::Dark);
}

TEST_CASE("an unparseable value falls back to its default rather than guessing", "[settings]")
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[general]\ntheme=purple\ntouchMode=perhaps\nuiFontScale=large\n"));

    Settings settings(path);
    CHECK(settings.get(keys::general::theme) == Theme::FollowSystem);
    // The bool case is the sharp one: QVariant::toBool() maps "perhaps" to TRUE, so a codec that
    // deferred to Qt would silently switch touch mode on.
    CHECK(settings.get(keys::general::touchMode) == false);
    CHECK(settings.get(keys::general::uiFontScale) == 1.0);
}

TEST_CASE("an unknown logging subsystem name is dropped, not fatal", "[settings]")
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral("[logging]\nsubsystems=render,teleport,ocr\n"));

    Settings settings(path);
    const LogSubsystemMask mask = settings.get(keys::logging::subsystems);
    CHECK(mask.contains(LogSubsystem::Render));
    CHECK(mask.contains(LogSubsystem::Ocr));
    CHECK_FALSE(mask.contains(LogSubsystem::Ui));
}

TEST_CASE("the empty subsystem set is a value, not an absence", "[settings]")
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        settings.set(keys::logging::subsystems, LogSubsystemMask::none());
        REQUIRE(settings.sync());
    }
    Settings reloaded(path);
    CHECK(reloaded.get(keys::logging::subsystems) == LogSubsystemMask::none());
}

// =======================================================================================
// A-45 -- the logging keys take effect, and document text never appears above debug
// =======================================================================================

TEST_CASE("the logging keys decode into the values core/Log.h consumes", "[settings]")
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral("[logging]\nlevel=debug\nsubsystems=parse\n"));
    Settings settings(path);

    CHECK(settings.get(keys::logging::level) == LogLevel::Debug);
    const LogSubsystemMask mask = settings.get(keys::logging::subsystems);
    CHECK(mask.contains(LogSubsystem::Parse));
    CHECK_FALSE(mask.contains(LogSubsystem::Render));
}

TEST_CASE("a path logged at the level a settings file selects is redacted above debug", "[settings]")
{
    // The rule is core/Log.h's, but this is where it becomes reachable from a user-editable file:
    // `logging/level=info` must not be a way to get document paths into a support bundle.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral("[logging]\nlevel=info\n"));
    Settings settings(path);

    LogCapture capture;
    setLogThreshold(settings.get(keys::logging::level));
    RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "opened" << redact(QStringLiteral("C:/secret/merger.pdf"));

    CHECK(capture.sawContaining("<redacted>"));
    CHECK_FALSE(capture.sawContaining("merger.pdf"));
}

// =======================================================================================
// The shortcuts key family
// =======================================================================================

TEST_CASE("a shortcut override round-trips through the family", "[settings]")
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/settings.ini");
    const ShortcutKey key = ShortcutKey::forAction(KeyContext::Viewer, ActionId::FileOpen);

    {
        Settings settings(path);
        CHECK_FALSE(settings.shortcutOverride(key).has_value());
        settings.setShortcutOverride(key, QStringLiteral("Ctrl+Shift+O"));
        REQUIRE(settings.sync());
    }

    Settings reloaded(path);
    const std::optional<QString> chord = reloaded.shortcutOverride(key);
    REQUIRE(chord.has_value());
    CHECK(*chord == QStringLiteral("Ctrl+Shift+O"));

    const std::vector<KeyBindingStore::OverrideEntry> entries = reloaded.shortcutOverrides();
    REQUIRE(entries.size() == 1);
    CHECK(entries.front().context == KeyContext::Viewer);
    CHECK(entries.front().actionStringId == "file.open");
    CHECK(entries.front().chord == "Ctrl+Shift+O");
}

TEST_CASE("clearing an override removes it from the file, not just from memory", "[settings]")
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/settings.ini");
    const ShortcutKey key = ShortcutKey::forAction(KeyContext::Viewer, ActionId::FileOpen);

    {
        Settings settings(path);
        settings.setShortcutOverride(key, QStringLiteral("Ctrl+Shift+O"));
        REQUIRE(settings.sync());
    }
    {
        Settings settings(path);
        settings.clearShortcutOverride(key);
        REQUIRE(settings.sync());
    }

    Settings reloaded(path);
    CHECK_FALSE(reloaded.shortcutOverride(key).has_value());
    CHECK(reloaded.shortcutOverrides().empty());
    CHECK_FALSE(readBytes(path).contains("file.open"));
}

TEST_CASE("a saved shortcut for an id that no longer exists is dropped and counted", "[settings]")
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[shortcuts]\n"
        "Viewer/file.open=Ctrl+Shift+O\n"
        "Viewer/file.thisActionWasRetired=Ctrl+Shift+R\n"
        "Nowhere/file.open=Ctrl+Shift+N\n"));

    LogCapture log;
    Settings settings(path);

    CHECK(settings.droppedShortcutKeyCount() == 2);
    CHECK(settings.shortcutOverrides().size() == 1);
    CHECK(log.sawContaining("unknown context or action"));

    // And they are gone from the file once anything else is written -- retaining a remap nothing
    // can apply is what Stage1-AppShell-Plan.md 5.2 rules out.
    settings.set(keys::general::theme, Theme::Dark);
    REQUIRE(settings.sync());
    CHECK_FALSE(readBytes(path).contains("thisActionWasRetired"));
}

// =======================================================================================
// D1-16 -- a RETIRED action spelling in the file (Stage 2 I-39, guide case A-73)
// =======================================================================================
//
// The shipped alias table has zero rows, so every case here loads through the synthetic table in
// `tests/unit/fixtures/ActionAliasFixture.def`: `shell.toggleSidebar` (oldest) and
// `view.sidebar.toggle` (newer) both retired onto `view.toggleSidebar`, and `file.open` claimed by
// an alias row although it is live. What this does NOT cover (convention 19): the product composes
// `Settings` with the SHIPPED table, so the day a real alias row lands this is still the first time
// that row meets a user's file -- but no longer the first time the loader meets any row.

TEST_CASE("a shortcut saved under a retired spelling loads under the current one", "[settings][alias]")
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[shortcuts]\n"
        "Global/view.sidebar.toggle=Ctrl+Alt+B\n"));
    const QByteArray before = readBytes(path);

    {
        Settings settings(path, rawpage::testfixture::kFixtureAliases);

        const ShortcutKey current = ShortcutKey::forAction(KeyContext::Global, ActionId::ViewToggleSidebar);
        CHECK(settings.shortcutOverride(current) == QStringLiteral("Ctrl+Alt+B"));
        CHECK(settings.droppedShortcutKeyCount() == 0);

        const std::vector<KeyBindingStore::OverrideEntry> entries = settings.shortcutOverrides();
        REQUIRE(entries.size() == 1);
        CHECK(entries.front().actionStringId == "view.toggleSidebar");

        // NOTHING WRITTEN AT LOAD. A process that only reads its settings must not rewrite them.
        CHECK(readBytes(path) == before);

        // ...and the rename reaches the file with the next write of anything at all.
        settings.set(keys::general::theme, Theme::Dark);
        REQUIRE(settings.sync());
    }

    const QByteArray after = readBytes(path);
    CHECK_FALSE(after.contains("view.sidebar.toggle"));
    CHECK(after.contains("view.toggleSidebar=Ctrl+Alt+B"));

    // Read back through the SHIPPED table, which knows nothing of the old spelling: the remap is
    // there because the file now says the current name, not because an alias resolved it again.
    Settings shipped(path);
    CHECK(shipped.shortcutOverride(ShortcutKey::forAction(KeyContext::Global, ActionId::ViewToggleSidebar))
          == QStringLiteral("Ctrl+Alt+B"));
}

TEST_CASE("without its alias row a retired shortcut is dropped as stale", "[settings][alias]")
{
    // THE CONTROL for the case above. The same file through a table with no rows -- which is what
    // the shipped table is today -- loses the remap, so the case above is measuring the alias row
    // and not something the loader would have done anyway.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[shortcuts]\n"
        "Global/view.sidebar.toggle=Ctrl+Alt+B\n"));

    constexpr std::array<ActionAlias, 0> none{};
    Settings settings(path, none);
    CHECK(settings.droppedShortcutKeyCount() == 1);
    CHECK(settings.shortcutOverrides().empty());
}

TEST_CASE("when two keys land on one current path the newer spelling wins", "[settings][alias]")
{
    // Two rules, from `Settings::migrateRetiredActionIds`: the CURRENT spelling in the file beats
    // any retired one, and between retired spellings the LATER alias row wins.
    QTemporaryDir dir;
    const QString contents = QStringLiteral(
        "[shortcuts]\n"
        "Global/view.toggleSidebar=Ctrl+Alt+1\n"
        "Global/view.sidebar.toggle=Ctrl+Alt+2\n"
        "Viewer/shell.toggleSidebar=Ctrl+Alt+3\n"
        "Viewer/view.sidebar.toggle=Ctrl+Alt+4\n");
    const QString path = writeIni(dir, contents);

    const ShortcutKey global = ShortcutKey::forAction(KeyContext::Global, ActionId::ViewToggleSidebar);
    const ShortcutKey viewer = ShortcutKey::forAction(KeyContext::Viewer, ActionId::ViewToggleSidebar);

    {
        Settings settings(path, rawpage::testfixture::kFixtureAliases);
        CHECK(settings.shortcutOverride(global) == QStringLiteral("Ctrl+Alt+1"));
        CHECK(settings.shortcutOverride(viewer) == QStringLiteral("Ctrl+Alt+4"));
        CHECK(settings.shortcutOverrides().size() == 2);
    }

    // THE ROW ORDER, NOT THE ALPHABET. In the fixture the later row (`view.sidebar.toggle`) also
    // sorts later, so "whichever key the map visits last" would pass the check above. The same file
    // through the same rows REVERSED must now pick `shell.toggleSidebar` -- which the alphabet
    // would not.
    QTemporaryDir reversedDir;
    const QString reversedPath = writeIni(reversedDir, contents);
    constexpr std::array<ActionAlias, 2> reversed{{
        ActionAlias{"view.sidebar.toggle", ActionId::ViewToggleSidebar},
        ActionAlias{"shell.toggleSidebar", ActionId::ViewToggleSidebar},
    }};
    Settings settings(reversedPath, reversed);
    CHECK(settings.shortcutOverride(viewer) == QStringLiteral("Ctrl+Alt+3"));
    CHECK(settings.shortcutOverride(global) == QStringLiteral("Ctrl+Alt+1"));
}

TEST_CASE("a live id claimed by an alias row is not renamed", "[settings][alias]")
{
    // The fixture's third row claims `file.open` as a retired spelling of `view.fullScreen`. A live
    // id outranks an alias, so the loader must leave the key exactly where it is -- renaming it
    // would move a user's Open shortcut onto Full Screen.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[shortcuts]\n"
        "Viewer/file.open=Ctrl+Alt+O\n"));

    Settings settings(path, rawpage::testfixture::kFixtureAliases);
    CHECK(settings.shortcutOverride(ShortcutKey::forAction(KeyContext::Viewer, ActionId::FileOpen))
          == QStringLiteral("Ctrl+Alt+O"));
    CHECK_FALSE(settings.shortcutOverride(
        ShortcutKey::forAction(KeyContext::Viewer, ActionId::ViewFullScreen)).has_value());
}

TEST_CASE("a toolbar saved with a retired command loads with the current one", "[settings][alias]")
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/settings.ini");

    // Written through the shipped table, which stores whatever strings it is handed -- the one way
    // to get a list into the file exactly as QSettings would have written it.
    {
        Settings writer(path);
        writer.set(keys::ui::qatCommands,
                   QStringList{QStringLiteral("file.open"), QStringLiteral("shell.toggleSidebar"),
                               QStringLiteral("file.save")});
        REQUIRE(writer.sync());
    }

    {
        Settings settings(path, rawpage::testfixture::kFixtureAliases);
        CHECK(settings.get(keys::ui::qatCommands)
              == QStringList({QStringLiteral("file.open"), QStringLiteral("view.toggleSidebar"),
                              QStringLiteral("file.save")}));
        settings.set(keys::general::theme, Theme::Dark);
        REQUIRE(settings.sync());
    }
    CHECK_FALSE(readBytes(path).contains("shell.toggleSidebar"));

    // The control: no rows, no rename -- the string is still the retired one, and the toolbar's own
    // restore is what would then drop it (`UiCustomization::resolveCommands`).
    QTemporaryDir controlDir;
    const QString controlPath = controlDir.path() + QStringLiteral("/settings.ini");
    {
        Settings writer(controlPath);
        writer.set(keys::ui::qatCommands, QStringList{QStringLiteral("shell.toggleSidebar")});
        REQUIRE(writer.sync());
    }
    constexpr std::array<ActionAlias, 0> none{};
    Settings control(controlPath, none);
    CHECK(control.get(keys::ui::qatCommands) == QStringList{QStringLiteral("shell.toggleSidebar")});
}

TEST_CASE("a newer build's file is renamed in memory and never rewritten", "[settings][alias]")
{
    // D1-16's downgrade rule outranks the rename: a file from a newer build is read for what is
    // understood and left byte-identical, even though this build would rather spell a key its way.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[meta]\nschemaVersion=99\n\n"
        "[shortcuts]\n"
        "Global/view.sidebar.toggle=Ctrl+Alt+B\n"));
    const QByteArray before = readBytes(path);

    Settings settings(path, rawpage::testfixture::kFixtureAliases);
    CHECK(settings.writesSuppressed());
    CHECK(settings.shortcutOverride(ShortcutKey::forAction(KeyContext::Global, ActionId::ViewToggleSidebar))
          == QStringLiteral("Ctrl+Alt+B"));

    settings.set(keys::general::theme, Theme::Dark);
    REQUIRE(settings.sync());
    CHECK(readBytes(path) == before);
}

TEST_CASE("a change is on disk before anything shuts down", "[settings]")
{
    // ISSUE I-47, and the state a KILLED process leaves behind. `sync()` used to be called from one
    // place -- `app/CompositionRoot`'s teardown -- so a preference the user changed and watched take
    // effect lived only in this process's memory until a clean quit. The Preferences dialog makes
    // that worse rather than better: it applies immediately and has no OK button, so there is no
    // moment at which anyone would think "now it is saved".
    //
    // Nothing here calls `sync()`. That omission IS the case: a second `Settings` opened on the same
    // file is what a relaunch after a kill sees.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = QDir(dir.path()).filePath(QStringLiteral("settings.ini"));

    {
        Settings settings(path);
        settings.set(keys::general::theme, Theme::Dark);
        settings.set(keys::general::uiFontScale, 1.5);
        settings.setShortcutOverride(ShortcutKey::forAction(KeyContext::Global, ActionId::FileOpen),
                                     QStringLiteral("Ctrl+Shift+O"));
        // No sync, and no destructor-driven flush either -- the object is still alive here.
        CHECK(readBytes(path).contains("dark"));
    }

    Settings reopened(path);
    CHECK(reopened.get(keys::general::theme) == Theme::Dark);
    CHECK(reopened.get(keys::general::uiFontScale) == 1.5);
    REQUIRE(reopened.shortcutOverrides().size() == 1);
    CHECK(reopened.shortcutOverrides().front().chord == "Ctrl+Shift+O");

    // A REMOVAL travels the same way. Clearing an override erases a key rather than writing one, and
    // that path had its own `removed_` list and its own way of never reaching the disk.
    {
        Settings settings(path);
        settings.clearShortcutOverride(ShortcutKey::forAction(KeyContext::Global, ActionId::FileOpen));
    }
    Settings afterClear(path);
    CHECK(afterClear.shortcutOverrides().empty());
}

// =======================================================================================
// A-47 -- D1-16: migration up, and NO REWRITE on downgrade
// =======================================================================================

TEST_CASE("a file at the current version loads unchanged", "[settings][schema-version]")
{
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[meta]\nschemaVersion=1\n\n[logging]\nlevel=warning\nretainFiles=9\n"));
    const QByteArray before = readBytes(path);

    Settings settings(path);
    CHECK(settings.fileSchemaVersion() == 1);
    CHECK_FALSE(settings.wasMigrated());
    CHECK_FALSE(settings.writesSuppressed());
    CHECK(settings.get(keys::logging::level) == LogLevel::Warning);
    CHECK(settings.get(keys::logging::retainFiles) == 9);

    REQUIRE(settings.sync());                 // nothing changed, so nothing is written
    CHECK(readBytes(path) == before);
}

TEST_CASE("a hand-written [general] section is honoured, and normalized on the next write",
          "[settings]")
{
    // QSettings treats an INI section named `General` -- case-insensitively -- as its own reserved
    // fallback section, so a hand-written `[general]` yields keys with no group at all, and
    // QSettings writes our `general/` group back out as `[%General]`. Measured at T5, and this case
    // is what keeps it measured: the exact hand edit A-43 asks a human to make must take effect,
    // and the file must converge on ONE spelling rather than growing two sections that both claim
    // `theme`.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[meta]\nschemaVersion=1\n\n[general]\ntheme=dark\nuiFontScale=1.5\n"));

    Settings settings(path);
    CHECK(settings.get(keys::general::theme) == Theme::Dark);
    CHECK(settings.get(keys::general::uiFontScale) == 1.5);

    REQUIRE(settings.sync());

    // The hand edit SURVIVES the normalization. Removing the old spelling without writing the new
    // one would read to a user as "it forgets my theme".
    Settings reloaded(path);
    CHECK(reloaded.get(keys::general::theme) == Theme::Dark);
    CHECK(reloaded.get(keys::general::uiFontScale) == 1.5);
    CHECK(reloaded.unknownKeys().isEmpty());
}

TEST_CASE("a file older than the schema is migrated and restamped", "[settings][schema-version]")
{
    QTemporaryDir dir;
    // No stamp, but keys present: a file from before versioning existed. Version 0.
    const QString path = writeIni(dir, QStringLiteral("[general]\ntheme=dark\nuiFontScale=1.25\n"));

    Settings settings(path);
    CHECK(settings.wasMigrated());
    CHECK(settings.fileSchemaVersion() == SettingsSchema::kCurrentSchemaVersion);
    CHECK(settings.get(keys::general::theme) == Theme::Dark);
    CHECK(settings.get(keys::general::uiFontScale) == 1.25);

    REQUIRE(settings.sync());
    const QByteArray after = readBytes(path);
    CHECK(after.contains("schemaVersion=1"));
    CHECK(after.contains("theme=dark"));      // the migration restamps; it does not discard
}

TEST_CASE("a fresh file is not reported as migrated", "[settings][schema-version]")
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/settings.ini"));
    CHECK_FALSE(settings.wasMigrated());
    CHECK(settings.fileSchemaVersion() == SettingsSchema::kCurrentSchemaVersion);
}

TEST_CASE("a file from a NEWER build is read but never rewritten", "[settings][schema-version]")
{
    // The half people forget. An older build that rewrites a newer build's settings destroys them
    // during a rollback -- exactly when a user can least afford it.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[meta]\nschemaVersion=99\n"
        "\n[general]\ntheme=dark\n"
        "\n[future]\nsomethingWeDoNotUnderstand=42\n"));
    const QByteArray before = readBytes(path);

    LogCapture log;
    Settings settings(path);

    CHECK(settings.writesSuppressed());
    CHECK(settings.fileSchemaVersion() == 99);
    CHECK(settings.get(keys::general::theme) == Theme::Dark);      // read for what we understand
    CHECK(settings.unknownKeys().contains(QStringLiteral("future/somethingWeDoNotUnderstand")));
    CHECK(log.sawContaining("newer build"));

    // Even a deliberate change is memory-only: the session works, the file does not move.
    settings.set(keys::general::theme, Theme::Light);
    CHECK(settings.get(keys::general::theme) == Theme::Light);
    REQUIRE(settings.sync());
    CHECK(readBytes(path) == before);
}

TEST_CASE("an unknown key from a newer build survives a normal rewrite", "[settings][schema-version]")
{
    // Same rule at the key level rather than the file level: a key we do not declare is preserved
    // even when the file IS being written, because it may be a newer build's and deleting it is the
    // one thing we must not do.
    QTemporaryDir dir;
    const QString path = writeIni(dir, QStringLiteral(
        "[meta]\nschemaVersion=1\n\n[future]\nsomethingWeDoNotUnderstand=42\n"));

    Settings settings(path);
    CHECK(settings.unknownKeys().size() == 1);
    settings.set(keys::general::theme, Theme::Dark);
    REQUIRE(settings.sync());

    const QByteArray after = readBytes(path);
    CHECK(after.contains("somethingWeDoNotUnderstand=42"));
    CHECK(after.contains("theme=dark"));
}

TEST_CASE("the migration chain runs in order and only over the versions it spans",
          "[settings][schema-version]")
{
    // The real chain is empty at version 1, and a mechanism whose only test is an empty list is not
    // tested at all -- so it is driven here with a synthetic chain. This is what makes the first
    // real migration, three stages from now, a row rather than an experiment.
    static std::vector<std::string> applied;
    applied.clear();

    const std::vector<SettingsSchema::Migration> chain{
        SettingsSchema::Migration{1, "one to two",
                                  [](QVariantMap& values) {
                                      applied.push_back("1->2");
                                      values.insert(QStringLiteral("added/byOne"), 1);
                                  }},
        SettingsSchema::Migration{3, "three to four",
                                  [](QVariantMap& values) {
                                      applied.push_back("3->4");
                                      values.insert(QStringLiteral("added/byThree"), 3);
                                  }},
        // Declared out of order on purpose: the chain is appended to by whoever bumps the version,
        // and applying a row out of order is a data-corruption bug that reads as anything but.
        SettingsSchema::Migration{2, "two to three",
                                  [](QVariantMap& values) {
                                      applied.push_back("2->3");
                                      values.insert(QStringLiteral("added/byTwo"), 2);
                                  }},
    };

    QVariantMap values;
    const int reached = SettingsSchema::migrate(values, 1, chain, 4);

    CHECK(reached == 4);
    REQUIRE(applied.size() == 3);
    CHECK(applied.at(0) == "1->2");
    CHECK(applied.at(1) == "2->3");
    CHECK(applied.at(2) == "3->4");
    CHECK(values.value(QStringLiteral("added/byTwo")).toInt() == 2);

    // A version at or above the target runs nothing, and reports the version it was already at.
    applied.clear();
    QVariantMap newer;
    CHECK(SettingsSchema::migrate(newer, 9, chain, 4) == 9);
    CHECK(applied.empty());

    // A span with no migration row is not a gap: that bump added keys and changed none, which is
    // the common case and needs no step.
    applied.clear();
    QVariantMap partial;
    CHECK(SettingsSchema::migrate(partial, 3, chain, 4) == 4);
    REQUIRE(applied.size() == 1);
    CHECK(applied.at(0) == "3->4");
}
