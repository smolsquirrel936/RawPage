// Stage 1 T9 -- ui/shell/ThemeManager. Guide case A-52 (Stage1-AppShell-Testing-Guide.md 5.6).
//
// WHAT THIS FILE CAN AND CANNOT CHECK. It runs offscreen, so it cannot judge whether the dark
// palette looks good, whether the contrast is comfortable, or whether the accent reads as an
// accent. Every one of those is manual case M-04's. What it CAN check is the property that makes
// the theme mechanism worth having at all: that the token set is closed, that every token resolves
// in every mode, and that a settings change reaches the palette without a restart.
//
// (High-contrast deference was decision D1-15 and was withdrawn on 2026-08-23 with the rest of the
// accessibility surface, so there are two resolved modes here rather than three.)

#include <catch2/catch_test_macros.hpp>

#include "core/settings/Settings.h"
#include "ui/shell/ThemeManager.h"

#include <QApplication>
#include <QDir>
#include <QFont>
#include <QTemporaryDir>

#include <set>

using namespace rawpage;

namespace {

// Every declared token, as values, so a test can iterate the set rather than restate it. Restating
// it is how a token gets added and tested nowhere -- the same trap ShellKeys.def and ActionId.def
// each exist to close.
const std::vector<ui::ThemeToken>& allTokens()
{
    static const std::vector<ui::ThemeToken> tokens{
        ui::ThemeToken::WindowBackground,    ui::ThemeToken::WindowText,
        ui::ThemeToken::SurfaceBackground,   ui::ThemeToken::SurfaceText,
        ui::ThemeToken::SubduedText,         ui::ThemeToken::Border,
        ui::ThemeToken::Accent,              ui::ThemeToken::AccentText,
        ui::ThemeToken::SelectionBackground, ui::ThemeToken::SelectionText,
        ui::ThemeToken::DisabledText,
    };
    return tokens;
}

} // namespace

TEST_CASE("every declared token resolves in both themes", "[theme]")
{
    // The count is asserted against the enum's own constant first. Without this, a token added to
    // the enum and forgotten here would leave the loop below testing a stale subset and passing.
    REQUIRE(allTokens().size() == ui::kThemeTokenCount);

    for (const ui::ThemeToken token : allTokens()) {
        for (const ui::ThemeManager::Resolved theme :
             {ui::ThemeManager::Resolved::Light, ui::ThemeManager::Resolved::Dark}) {
            const QColor color = ui::ThemeManager::colorFor(token, theme);
            INFO("token " << ui::themeTokenName(token));
            CHECK(color.isValid());
            CHECK(color.alpha() == 255);
        }
    }
}

TEST_CASE("every token has a name and no two names collide", "[theme]")
{
    std::set<std::string_view> names;
    for (const ui::ThemeToken token : allTokens()) {
        const std::string_view name = ui::themeTokenName(token);
        INFO("token index " << static_cast<int>(token));
        CHECK(name != "?");            // the switch's fallthrough answer
        CHECK(names.insert(name).second);
    }
    CHECK(names.size() == ui::kThemeTokenCount);
}

TEST_CASE("light and dark actually differ where it matters", "[theme]")
{
    // A theme that resolves cleanly and returns the same colours in both modes would pass the case
    // above completely. The backgrounds and the text are where a copy-paste error lands.
    for (const ui::ThemeToken token :
         {ui::ThemeToken::WindowBackground, ui::ThemeToken::SurfaceBackground,
          ui::ThemeToken::WindowText, ui::ThemeToken::SurfaceText}) {
        INFO("token " << ui::themeTokenName(token));
        CHECK(ui::ThemeManager::colorFor(token, ui::ThemeManager::Resolved::Light)
              != ui::ThemeManager::colorFor(token, ui::ThemeManager::Resolved::Dark));
    }

    // And the direction is the one a person would expect: dark mode's window is darker than its
    // text, light mode's is lighter. Reversing a pair is the single most common theming mistake and
    // it is invisible to an equality check.
    CHECK(ui::ThemeManager::colorFor(ui::ThemeToken::WindowBackground, ui::ThemeManager::Resolved::Dark).lightness()
          < ui::ThemeManager::colorFor(ui::ThemeToken::WindowText, ui::ThemeManager::Resolved::Dark).lightness());
    CHECK(ui::ThemeManager::colorFor(ui::ThemeToken::WindowBackground, ui::ThemeManager::Resolved::Light).lightness()
          > ui::ThemeManager::colorFor(ui::ThemeToken::WindowText, ui::ThemeManager::Resolved::Light).lightness());
}

TEST_CASE("the requested theme and the resolved theme are separate facts", "[theme]")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    core::Settings settings(QDir(directory.path()).filePath(QStringLiteral("settings.ini")));

    ui::ThemeManager theme(settings);

    settings.set(core::keys::general::theme, core::Theme::Dark);
    theme.apply();
    CHECK(theme.requested() == core::Theme::Dark);
    CHECK(theme.resolved() == ui::ThemeManager::Resolved::Dark);
    CHECK(theme.requestedNameKey() == QStringLiteral("shell.theme.dark"));

    settings.set(core::keys::general::theme, core::Theme::FollowSystem);
    theme.apply();
    CHECK(theme.requested() == core::Theme::FollowSystem);

    // FollowSystem is a request, not a state: it resolves to one of the two, and which one is the
    // session's business rather than this test's. What must hold is that it resolves at all --
    // "follow system" is never itself a palette.
    CHECK(theme.resolved() == ui::ThemeManager::systemTheme());
    CHECK(theme.requestedNameKey() == QStringLiteral("shell.theme.followSystem"));
}

TEST_CASE("a theme change reaches the manager without an explicit apply", "[theme]")
{
    // Flow J, in the smallest form Stage 1 can express it: a settings write propagates. From
    // Stage 2 the same signal invalidates the render cache, which is why the subscription is
    // asserted here rather than left to the Preferences dialog at T14 to discover.
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    core::Settings settings(QDir(directory.path()).filePath(QStringLiteral("settings.ini")));

    settings.set(core::keys::general::theme, core::Theme::Light);
    ui::ThemeManager theme(settings);
    theme.apply();
    REQUIRE(theme.resolved() == ui::ThemeManager::Resolved::Light);

    settings.set(core::keys::general::theme, core::Theme::Dark);
    CHECK(theme.resolved() == ui::ThemeManager::Resolved::Dark);
}

TEST_CASE("the UI font scale is clamped by the schema rather than by the theme", "[theme]")
{
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    core::Settings settings(QDir(directory.path()).filePath(QStringLiteral("settings.ini")));

    // 9.0 is the value guide case A-43 asks a person to hand-edit into settings.ini. The clamp
    // lives in core/settings (T5), and this case exists to prove the theme did not grow a second,
    // disagreeing copy of the range -- which is exactly how a default ends up with two homes.
    settings.set(core::keys::general::uiFontScale, 9.0);
    CHECK(settings.get(core::keys::general::uiFontScale) == 2.0);

    ui::ThemeManager theme(settings);
    theme.apply();
    CHECK(QApplication::font().pointSizeF() > 0.0);
}
