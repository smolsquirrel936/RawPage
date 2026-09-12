// Light, dark, follow-system -- and the palette tokens every shell widget draws from.
//
// ---------------------------------------------------------------------------------------------
// WHY TOKENS RATHER THAN COLOURS AT CALL SITES.
//
// A widget that names `#2b2b2b` is a widget that is wrong in one of the two themes, and the bug is
// invisible until somebody switches. Naming a ROLE -- "surface background", "border" -- means the
// switch is a table lookup and a new theme is a new column, not a sweep. Guide case A-52 asserts
// that every declared token resolves in every mode, which is only a meaningful assertion because
// the token list is closed and lives here.
//
// `colorFor()` is a switch over the whole enum with -Werror in effect for ui/, so ADDING A TOKEN
// WITHOUT GIVING IT A VALUE IN BOTH THEMES DOES NOT COMPILE. That is the same trick ActionId.def
// plays with its table: make the omission a build error at the point of the omission.
//
// *(OS high-contrast deference was decision D1-15 and was WITHDRAWN on 2026-08-23 with the rest of
// the accessibility surface -- see Features.md → Removed from scope. There are three modes here,
// not four, and the third is "follow system", not "high contrast".)*
// ---------------------------------------------------------------------------------------------

#ifndef RAWPAGE_UI_SHELL_THEMEMANAGER_H
#define RAWPAGE_UI_SHELL_THEMEMANAGER_H

#include "core/settings/SettingsSchema.h"

#include <QColor>
#include <QObject>
#include <QString>

#include <cstddef>
#include <string_view>

namespace rawpage::core {
class Settings;
}

namespace rawpage::ui {

// The closed set of colour roles the shell paints with. Every one of them has a value in both
// resolved themes, and nothing outside this file decides what that value is.
enum class ThemeToken {
    WindowBackground,
    WindowText,
    SurfaceBackground,     // panes, the central surface -- one step off the window
    SurfaceText,
    SubduedText,           // secondary explanatory text; still readable, deliberately quieter
    Border,
    Accent,
    AccentText,            // text drawn ON the accent
    SelectionBackground,
    SelectionText,
    DisabledText,
    MessageBackground,     // the document-condition banner -- a TINT, never the window's grey
    MessageText,           // text drawn ON the banner
};

inline constexpr std::size_t kThemeTokenCount = 13;

constexpr std::string_view themeTokenName(ThemeToken token)
{
    switch (token) {
    case ThemeToken::WindowBackground:    return "windowBackground";
    case ThemeToken::WindowText:          return "windowText";
    case ThemeToken::SurfaceBackground:   return "surfaceBackground";
    case ThemeToken::SurfaceText:         return "surfaceText";
    case ThemeToken::SubduedText:         return "subduedText";
    case ThemeToken::Border:              return "border";
    case ThemeToken::Accent:              return "accent";
    case ThemeToken::AccentText:          return "accentText";
    case ThemeToken::SelectionBackground: return "selectionBackground";
    case ThemeToken::SelectionText:       return "selectionText";
    case ThemeToken::DisabledText:        return "disabledText";
    case ThemeToken::MessageBackground:   return "messageBackground";
    case ThemeToken::MessageText:         return "messageText";
    }
    return "?";
}

class ThemeManager : public QObject {
    Q_OBJECT

public:
    // `FollowSystem` is a REQUEST, not a state: it resolves to Light or Dark at any moment. Keeping
    // the two apart is what lets the status bar report "Follow system" while the palette in force
    // is dark.
    enum class Resolved { Light, Dark };

    explicit ThemeManager(core::Settings& settings, QObject* parent = nullptr);
    ~ThemeManager() override;

    // Reads `general/theme` and `general/uiFontScale`, resolves, and applies to the application
    // palette and font. Called once at startup and again whenever either setting changes.
    void apply();

    core::Theme requested() const { return requested_; }
    Resolved    resolved() const { return resolved_; }

    // The catalog key naming the CURRENT REQUEST -- "Follow system" rather than "Dark".
    QString requestedNameKey() const;

    QColor color(ThemeToken token) const;

    // The value a token takes in a given resolved theme, independent of what is in force. A-52
    // walks this over both modes without switching the running application's palette.
    static QColor colorFor(ThemeToken token, Resolved theme);

    // What "follow system" currently means. Qt reports Unknown on a platform or session that does
    // not say; Light is the documented answer there, because guessing Dark on a system that never
    // asked for it is the more disruptive of the two mistakes.
    static Resolved systemTheme();

    // WHAT `apply()` LAST ASKED `QStyleHints` FOR -- `Unknown` meaning "the OS decides". Kept
    // because it cannot be read back everywhere: under the offscreen platform the widget suite runs
    // on, `QStyleHints::colorScheme()` answers `Unknown` no matter what was set, so a test that
    // interrogated Qt would be asserting a property of the test platform. This is the value handed
    // over; whether the STYLE then paints with it is manual case M-05. (Issue I-41.)
    Qt::ColorScheme requestedColorScheme() const { return requestedScheme_; }

signals:
    void themeChanged(Resolved resolved);

private:
    core::Settings& settings_;
    core::Theme requested_ = core::Theme::FollowSystem;
    Resolved    resolved_ = Resolved::Light;
    double      fontScale_ = 1.0;
    int         settingsSubscription_ = 0;
    double      baseFontPointSize_ = 0.0;

    // `apply()` asks Qt for a colour scheme, and Qt answers by emitting `colorSchemeChanged`,
    // which is connected back to `apply()`. One flag rather than a subtler comparison: the
    // reentrant call has nothing left to do, and "nothing" is the cheapest thing to prove.
    bool        applying_ = false;
    Qt::ColorScheme requestedScheme_ = Qt::ColorScheme::Unknown;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_THEMEMANAGER_H
