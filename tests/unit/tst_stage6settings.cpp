// Stage 6 T6 -- the three `identity/` keys D6-13 declares (`Stage6-Annotations-Plan.md` §3), the rule that
// reads them, and convention 11's control for it. Guide cases A-53's control and A-54's key half
// (`Stage6-Annotations-Testing-Guide.md` §5.8). Stage 6 T8 -- the two `commenting/` rows and the tool-defaults
// FAMILY `commenting/default<Property>.<tool>`: its table, its validators, its paths both ways, its rows written
// and read back across two processes (A-61's headless half; A-62's and A-47's keys).
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
#include "core/settings/CommentingTypes.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"

#include <QFile>
#include <QSettings>
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

// ==========================================================================================
// Stage 6 T8 (D6-13): the two `commenting/` rows and the tool-defaults FAMILY -- guide A-61's headless half,
// A-62's key and A-47's key
// ==========================================================================================

TEST_CASE("the two commenting keys are declared in the Commenting group with their defaults", "[stage6settings][A-62][A-47][D6-13]")
{
    std::set<std::string> paths;
    for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys()) {
        if (info.group != SettingsGroup::Commenting) continue;
        paths.insert(std::string(info.path));
    }
    // TWO ROWS: the per-tool defaults are a FAMILY, not rows (D6-13), and `showPopupsOnHover`,
    // `printComments`, `summaryLayout` and `spellCheckComments` are not declared (one fact one key; Q4; Q6).
    CHECK(paths == std::set<std::string>{"commenting/popupBehaviour", "commenting/stickyTool"});

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString path = scratch.filePath(QStringLiteral("settings.ini"));
    {
        core::Settings settings(path);
        CHECK_FALSE(settings.get(keys::commenting::stickyTool));   // Acrobat's: back to the Selection tool
        CHECK(settings.get(keys::commenting::popupBehaviour) == core::PopupBehaviour::OnHover);
        settings.set(keys::commenting::stickyTool, true);
        settings.set(keys::commenting::popupBehaviour, core::PopupBehaviour::Never);
    }
    core::Settings again(path);   // written when changed, read by the next process
    CHECK(again.get(keys::commenting::stickyTool));
    CHECK(again.get(keys::commenting::popupBehaviour) == core::PopupBehaviour::Never);
    for (const char* name : {"always", "onHover", "never"}) CHECK(core::popupBehaviourFromName(name));
    CHECK_FALSE(core::popupBehaviourFromName("sometimes"));
}

TEST_CASE("every tool-default row is a value its validator accepts and every key resolves both ways", "[stage6settings][A-61][D6-13]")
{
    // The TABLE: every row's default parses, so no tool starts from a value the file could not hold.
    int rows = 0;
    std::set<std::string> seen;
    for (const core::ToolDefaultKey& key : core::allToolDefaultKeys()) {
        ++rows;
        INFO(key.path().toStdString());
        core::ToolDefaults values;
        CHECK(core::parseToolProperty(key.property(), key.defaultText(), values));
        CHECK(core::toolHasProperty(key.tool(), key.property()));
        // The path names the key, and nothing else does.
        const std::optional<core::ToolDefaultKey> back = core::ToolDefaultKey::fromPath(key.path());
        REQUIRE(back);
        CHECK(back->tool() == key.tool());
        CHECK(back->property() == key.property());
        CHECK(core::SettingsSchema::isDeclaredPath(key.path()));
        CHECK(seen.insert(key.path().toStdString()).second);   // no row twice
    }
    CHECK(rows > 60);
    // Paths in the family's shape that name nothing: not keys.
    for (const char* path : {"commenting/defaultFontSize.pencil", "commenting/defaultColour.eraser",
                             "commenting/defaultColour.crayon", "commenting/defaultGlow.highlight",
                             "commenting/defaultColour", "commenting/defaultColour.highlight.extra",
                             "commenting/defaultcolour.highlight"}) {
        INFO(path);
        CHECK_FALSE(core::ToolDefaultKey::fromPath(QString::fromLatin1(path)));
        CHECK_FALSE(core::SettingsSchema::isDeclaredPath(QString::fromLatin1(path)));
    }
    CHECK(core::ToolDefaultKey::of<core::CommentingTool::Highlight, core::ToolProperty::Colour>().path()
          == QStringLiteral("commenting/defaultColour.highlight"));
    CHECK(core::ToolDefaultKey::of<core::CommentingTool::TextBox, core::ToolProperty::FontSize>().path()
          == QStringLiteral("commenting/defaultFontSize.textBox"));
    // The eraser makes nothing and has no property; the pencil no font size.
    CHECK_FALSE(core::ToolDefaultKey::find(core::CommentingTool::Eraser, core::ToolProperty::Colour));
    CHECK_FALSE(core::ToolDefaultKey::find(core::CommentingTool::Pencil, core::ToolProperty::FontSize));

    // The validators: in range or refused.
    core::ToolDefaults v;
    CHECK(core::parseToolProperty(core::ToolProperty::Colour, QStringLiteral("0.5"), v));
    CHECK(core::parseToolProperty(core::ToolProperty::Colour, QStringLiteral("0 0.25 1"), v));
    CHECK(core::parseToolProperty(core::ToolProperty::Colour, QStringLiteral("0 0 0 1"), v));
    for (const char* bad : {"", "1 1", "1 1 1 1 1", "2 0 0", "-0.1 0 0", "red", "nan 0 0"})
        CHECK_FALSE(core::parseToolProperty(core::ToolProperty::Colour, QString::fromLatin1(bad), v));
    CHECK(core::parseToolProperty(core::ToolProperty::FillColour, QString(), v));
    CHECK(v.fillColour.empty());
    CHECK_FALSE(core::parseToolProperty(core::ToolProperty::Opacity, QStringLiteral("1.5"), v));
    CHECK_FALSE(core::parseToolProperty(core::ToolProperty::Thickness, QStringLiteral("25"), v));
    CHECK_FALSE(core::parseToolProperty(core::ToolProperty::FontSize, QStringLiteral("3"), v));
    CHECK_FALSE(core::parseToolProperty(core::ToolProperty::LineStyle, QStringLiteral("wavy"), v));
    CHECK_FALSE(core::parseToolProperty(core::ToolProperty::BlendMode, QStringLiteral("Multiply"), v));   // the token is lower case
}

TEST_CASE("a tool's defaults are written as its declared rows only and read back across two processes", "[stage6settings][A-61][D6-13]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString path = scratch.filePath(QStringLiteral("settings.ini"));
    core::ToolDefaults pencil;
    {
        core::Settings settings(path);
        // Nothing written: the table's defaults.
        CHECK(settings.toolDefaults(core::CommentingTool::Pencil) == core::builtInToolDefaults(core::CommentingTool::Pencil));
        CHECK(settings.toolDefaults(core::CommentingTool::Highlight).blendMode == core::ToolBlend::Multiply);
        CHECK(settings.toolDefaults(core::CommentingTool::TextBox).fontSize == 12.0);

        pencil = settings.toolDefaults(core::CommentingTool::Pencil);
        pencil.colour = {0.0, 0.5, 1.0};
        pencil.thickness = 4.0;
        pencil.blendMode = core::ToolBlend::Multiply;
        pencil.fontSize = 30.0;   // the pencil has no font size: not written
        settings.setToolDefaults(core::CommentingTool::Pencil, pencil);
        CHECK(settings.toolDefaultText(core::ToolDefaultKey::of<core::CommentingTool::Pencil, core::ToolProperty::Colour>())
              == QStringLiteral("0 0.5 1"));
    }
    core::Settings again(path);
    const core::ToolDefaults read = again.toolDefaults(core::CommentingTool::Pencil);
    CHECK(read.colour == std::vector<double>{0.0, 0.5, 1.0});
    CHECK(read.thickness == 4.0);
    CHECK(read.blendMode == core::ToolBlend::Multiply);
    CHECK(read.fontSize == core::builtInToolDefaults(core::CommentingTool::Pencil).fontSize);
    // The file holds the pencil's rows and no font size.
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        CHECK(text.contains(QStringLiteral("defaultThickness.pencil")));
        CHECK_FALSE(text.contains(QStringLiteral("defaultFontSize.pencil")));
    }

    // A hand-edited value the validator refuses reads as the row's default, never as a guess.
    again.setToolDefaults(core::CommentingTool::Line, again.toolDefaults(core::CommentingTool::Line));
    {
        QSettings raw(path, QSettings::IniFormat);
        raw.setValue(QStringLiteral("commenting/defaultOpacity.line"), QStringLiteral("7"));
    }
    core::Settings edited(path);
    CHECK(edited.toolDefaults(core::CommentingTool::Line).opacity == 1.0);

    // A reset removes the rows: the table's defaults again.
    edited.resetToolDefaults(core::CommentingTool::Pencil);
    CHECK(edited.toolDefaults(core::CommentingTool::Pencil) == core::builtInToolDefaults(core::CommentingTool::Pencil));
    CHECK_FALSE(edited.toolDefaultText(core::ToolDefaultKey::of<core::CommentingTool::Pencil, core::ToolProperty::Colour>()));
}

TEST_CASE("a line style is a dash and a dash reads back as its style", "[stage6settings][D6-13]")
{
    CHECK(core::dashFor(core::LineStyle::Solid, 2.0).empty());
    CHECK(core::dashFor(core::LineStyle::Dashed, 1.0) == std::vector<double>{3.0, 3.0});
    CHECK(core::dashFor(core::LineStyle::Dashed, 2.0) == std::vector<double>{6.0, 6.0});
    CHECK(core::dashFor(core::LineStyle::Dotted, 0.5) == std::vector<double>{1.0, 2.0});
    for (const core::LineStyle style : {core::LineStyle::Solid, core::LineStyle::Dashed, core::LineStyle::Dotted}) {
        for (const double width : {0.5, 1.0, 3.0}) {
            const QString bs = style == core::LineStyle::Solid ? QStringLiteral("S") : QStringLiteral("D");
            CHECK(core::lineStyleOf(bs, core::dashFor(style, width)) == style);
        }
    }
    CHECK(core::blendNameFor(true, core::ToolBlend::Multiply).isEmpty());    // a highlight's own
    CHECK(core::blendNameFor(true, core::ToolBlend::Normal) == QStringLiteral("Normal"));
    CHECK(core::blendNameFor(false, core::ToolBlend::Normal).isEmpty());     // everything else's own
    CHECK(core::blendNameFor(false, core::ToolBlend::Screen) == QStringLiteral("Screen"));
}

// =============================================================================================
// Stage 6 T12 (D6-13, D6-14) -- the five `measuring/` rows and `units/snapToObjects` (guide A-92's keys)
// =============================================================================================

TEST_CASE("the five measuring keys and snapToObjects are declared with their defaults and their units read back",
          "[stage6settings][A-92][D6-13][D6-14]")
{
    std::set<std::string> paths;
    for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys()) {
        if (info.group != SettingsGroup::Measuring) continue;
        paths.insert(std::string(info.path));
    }
    CHECK(paths == std::set<std::string>{"measuring/scaleRatio", "measuring/displayUnit", "measuring/precision",
                                         "measuring/snapToPaths", "measuring/writeToComment"});
    bool objects = false;
    for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys()) {
        if (std::string(info.path) == "units/snapToObjects") objects = info.group == SettingsGroup::UnitsAndGuides;
    }
    CHECK(objects);   // on Units & Guides, beside the other snap switches (D4-5)

    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const QString path = scratch.filePath(QStringLiteral("settings.ini"));
    {
        core::Settings settings(path);
        // ACTUAL SIZE in inches, two decimals, both snaps and the comment on (each row argues why).
        CHECK(settings.get(keys::measuring::scaleRatio) == 1.0);
        CHECK(settings.get(keys::measuring::displayUnit) == core::MeasureUnit::Inches);
        CHECK(settings.get(keys::measuring::precision) == 2);
        CHECK(settings.get(keys::measuring::snapToPaths));
        CHECK(settings.get(keys::measuring::writeToComment));
        CHECK(settings.get(keys::units::snapToObjects));
        // Every unit written and read back by its symbol; the ratio and the decimals clamped into their ranges.
        for (int i = 0; i < core::kMeasureUnitCount; ++i) {
            const auto unit = static_cast<core::MeasureUnit>(i);
            settings.set(keys::measuring::displayUnit, unit);
            CHECK(settings.get(keys::measuring::displayUnit) == unit);
        }
        settings.set(keys::measuring::displayUnit, core::MeasureUnit::Feet);
        settings.set(keys::measuring::scaleRatio, 48.0);
        settings.set(keys::measuring::precision, 9);   // past six: clamped
        CHECK(settings.get(keys::measuring::precision) == 6);
    }
    // THE FILE says the symbol, so a person reading settings.ini reads "ft" -- and a hand-edited unknown unit
    // falls back to the declared default rather than meaning something real.
    {
        QSettings ini(path, QSettings::IniFormat);
        CHECK(ini.value(QStringLiteral("measuring/displayUnit")).toString() == QStringLiteral("ft"));
        CHECK(ini.value(QStringLiteral("measuring/scaleRatio")).toDouble() == 48.0);
        ini.setValue(QStringLiteral("measuring/displayUnit"), QStringLiteral("furlong"));
    }
    core::Settings reread(path);
    CHECK(reread.get(keys::measuring::scaleRatio) == 48.0);
    CHECK(reread.get(keys::measuring::displayUnit) == core::MeasureUnit::Inches);
}
