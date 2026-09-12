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
#include "tests/ui/ColorDistance.h"
#include "ui/shell/ThemeManager.h"

#include <QApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <QDir>
#include <QFont>
#include <QTemporaryDir>

#include <cmath>
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
        ui::ThemeToken::MessageBackground,   ui::ThemeToken::MessageText,
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

namespace {

// Relative luminance and contrast ratio, the standard formulation. Two notes on why it is here.
//
// It is NOT an accessibility check -- accessibility left this product on 2026-08-23 (Features.md ->
// Removed from scope) and nothing below cites a conformance level. It is a legibility floor, and the
// reason this shell needs one where a finished product might not is issue I-40: **358 commands are
// declared and 19 registered**, so disabled text is not a rare state here, it is most of what is on
// screen. A disabled colour chosen by eye against a white mock is a colour nobody has judged against
// the other theme, which is exactly what happened.
//
// And it is a RATIO rather than a set of expected hex values, because pinning the hex would make
// this case a copy of the table it is supposed to be checking -- green for any pair of numbers,
// including an unreadable one.
double relativeLuminance(const QColor& color)
{
    const auto channel = [](double value) {
        return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF())
         + 0.7152 * channel(color.greenF())
         + 0.0722 * channel(color.blueF());
}

double contrastRatio(const QColor& a, const QColor& b)
{
    const double first = relativeLuminance(a);
    const double second = relativeLuminance(b);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

} // namespace

TEST_CASE("every text token is legible against the surface it is drawn on", "[theme]")
{
    // ISSUE I-40, found by the user walking M-05 into Windows dark mode. Every foreground token is
    // measured against the background it is actually painted over, in BOTH themes, because the pair
    // is what a person reads -- a token is not light or dark on its own.
    struct Pair {
        ui::ThemeToken foreground;
        ui::ThemeToken background;
    };
    static constexpr Pair pairs[] = {
        {ui::ThemeToken::WindowText,     ui::ThemeToken::WindowBackground},
        {ui::ThemeToken::SurfaceText,    ui::ThemeToken::SurfaceBackground},
        {ui::ThemeToken::SubduedText,    ui::ThemeToken::WindowBackground},
        {ui::ThemeToken::DisabledText,   ui::ThemeToken::WindowBackground},
        {ui::ThemeToken::DisabledText,   ui::ThemeToken::SurfaceBackground},
        {ui::ThemeToken::AccentText,     ui::ThemeToken::Accent},
        {ui::ThemeToken::SelectionText,  ui::ThemeToken::SelectionBackground},
        {ui::ThemeToken::MessageText,    ui::ThemeToken::MessageBackground},
    };

    // 3.0 is a floor, not a target: it is the level at which the reported defect stops being one,
    // and every pair here clears it with room. It is deliberately low enough that a DELIBERATELY
    // quiet colour -- subdued captions, disabled commands -- can still be quiet.
    constexpr double floorRatio = 3.0;

    for (const ui::ThemeManager::Resolved theme :
         {ui::ThemeManager::Resolved::Light, ui::ThemeManager::Resolved::Dark}) {
        for (const Pair& pair : pairs) {
            const QColor foreground = ui::ThemeManager::colorFor(pair.foreground, theme);
            const QColor background = ui::ThemeManager::colorFor(pair.background, theme);
            const double ratio = contrastRatio(foreground, background);

            UNSCOPED_INFO((theme == ui::ThemeManager::Resolved::Dark ? "dark  " : "light ")
                          << ui::themeTokenName(pair.foreground) << " on "
                          << ui::themeTokenName(pair.background) << " = " << ratio);
            CHECK(ratio >= floorRatio);
        }
    }

    // AND DISABLED STILL READS AS DISABLED. Raising the floor until the two are indistinguishable
    // would trade one defect for a worse one -- a ribbon where nothing tells you which of its 358
    // commands you can actually use.
    for (const ui::ThemeManager::Resolved theme :
         {ui::ThemeManager::Resolved::Light, ui::ThemeManager::Resolved::Dark}) {
        const QColor background = ui::ThemeManager::colorFor(ui::ThemeToken::WindowBackground, theme);
        const double enabled = contrastRatio(
            ui::ThemeManager::colorFor(ui::ThemeToken::WindowText, theme), background);
        const double disabled = contrastRatio(
            ui::ThemeManager::colorFor(ui::ThemeToken::DisabledText, theme), background);
        UNSCOPED_INFO("enabled " << enabled << " vs disabled " << disabled);
        CHECK(disabled * 2.0 < enabled);
    }
}

TEST_CASE("the message banner is a colour of its own and not the desk grey", "[theme]")
{
    // STAGE 2 ISSUE I-29, found by the user preparing the M-07 walk: the document-condition banner
    // borrowed `AlternateBase`, which `ThemeManager::apply()` sets to `WindowBackground` -- the
    // colour `PageView` paints around every page -- so the one strip that says a document was
    // repaired was indistinguishable from the desk it sits on.
    //
    // Measured as a colour DISTANCE, not a contrast ratio -- `tests/ui/ColorDistance.h` says why --
    // against two surfaces: the window grey (the desk and the tab strip both paint it) and the
    // page-white surface colour.
    const double floorDeltaE = uitest::kDistinctSurfaceDeltaE;

    // THE CONTROL, and it fails the floor for the right reason (convention 7): the colour the OLD
    // banner actually painted in the light theme, measured by grabbing it -- #f7f7f7, not quite the
    // #f3f3f3 window, because the style nudges `AlternateBase` -- is unequal to the desk and still
    // far under the floor. A hex literal on purpose: it is a measurement of the defect, not a token.
    CHECK(uitest::deltaE76(QColor(0xf7, 0xf7, 0xf7),
                           ui::ThemeManager::colorFor(ui::ThemeToken::WindowBackground,
                                                      ui::ThemeManager::Resolved::Light))
          < floorDeltaE);

    for (const ui::ThemeManager::Resolved theme :
         {ui::ThemeManager::Resolved::Light, ui::ThemeManager::Resolved::Dark}) {
        const QColor banner = ui::ThemeManager::colorFor(ui::ThemeToken::MessageBackground, theme);
        for (const ui::ThemeToken surface :
             {ui::ThemeToken::WindowBackground, ui::ThemeToken::SurfaceBackground}) {
            const double distance = uitest::deltaE76(banner, ui::ThemeManager::colorFor(surface, theme));
            UNSCOPED_INFO((theme == ui::ThemeManager::Resolved::Dark ? "dark  " : "light ")
                          << "messageBackground vs " << ui::themeTokenName(surface) << " = "
                          << distance);
            CHECK(distance >= floorDeltaE);
        }
    }
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

TEST_CASE("the resolved theme is handed to the style and not only to the palette", "[theme]")
{
    // ISSUE I-41, and the half of a theme this suite did not know existed. Qt's native `windows11`
    // style paints control text from the colour scheme it derives from Windows, NOT from the
    // application palette -- so with a dark palette installed on a light-mode OS an enabled
    // QToolButton's glyph came out #1f1f1f on a #1f1f1f background while every QLabel in the same
    // window obeyed the palette perfectly. Half a themed shell, and the unthemed half was the
    // ribbon.
    //
    // WHAT THIS CASE DOES NOT COVER, named here because it cannot be fixed by trying harder
    // (convention 19). Under the offscreen platform `setColorScheme()` is INERT -- measured:
    // `colorScheme()` still answers `Unknown` afterwards and `colorSchemeChanged` never fires,
    // for Dark, Light and Unknown alike. So nothing here can observe that Qt was told; this
    // asserts the MAPPING from theme to scheme, and the call sits on the next line beside the
    // value it asserts. It also cannot assert the PAINTING, since the style in force offscreen is
    // not the one in force on a desktop -- a pixel assertion here would describe the test
    // platform. The evidence that the call changes what the `windows11` style paints is measured
    // in I-41's log entry (an enabled button's glyph went from #1f1f1f on a #1f1f1f background to
    // #99deff), and confirming it on screen is M-05, which is where this was found.
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    core::Settings settings(QDir(directory.path()).filePath(QStringLiteral("settings.ini")));

    ui::ThemeManager theme(settings);

    settings.set(core::keys::general::theme, core::Theme::Dark);
    theme.apply();
    CHECK(theme.requestedColorScheme() == Qt::ColorScheme::Dark);

    settings.set(core::keys::general::theme, core::Theme::Light);
    theme.apply();
    CHECK(theme.requestedColorScheme() == Qt::ColorScheme::Light);

    // AND FOLLOW-SYSTEM HANDS IT BACK. Without this the override outlives the choice that made it:
    // `systemTheme()` reads `styleHints()->colorScheme()`, so a manager that had just forced Dark
    // would read its own answer and report it as the system's -- "follow system" would mean "follow
    // whatever I last chose", and it would look correct in every test that only asked for a colour.
    settings.set(core::keys::general::theme, core::Theme::FollowSystem);
    theme.apply();
    CHECK(theme.requestedColorScheme() == Qt::ColorScheme::Unknown);
    CHECK(theme.resolved() == ui::ThemeManager::systemTheme());

    // The reentrancy the fix introduces, asserted rather than assumed: `setColorScheme` emits
    // `colorSchemeChanged`, which is connected back to `apply()`. If that recursed, this line would
    // not be reached at all.
    settings.set(core::keys::general::theme, core::Theme::Dark);
    theme.apply();
    theme.apply();
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
