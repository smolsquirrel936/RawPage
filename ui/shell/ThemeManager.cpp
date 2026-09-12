#include "ui/shell/ThemeManager.h"

#include "core/Log.h"
#include "core/settings/Settings.h"
#include "ui/shell/Catalog.h"

#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;

} // namespace

ThemeManager::ThemeManager(core::Settings& settings, QObject* parent)
    : QObject(parent), settings_(settings)
{
    // The BASE point size is captured once, before anything is scaled. Scaling the current font
    // instead would compound: two changes of 1.5 would give 2.25 and there would be no way back to
    // 1.0 except restarting.
    baseFontPointSize_ = QApplication::font().pointSizeF();

    settingsSubscription_ = settings_.subscribe([this](const core::Settings::Change& change) {
        if (change.group == core::SettingsGroup::General) apply();
    });

    // Live, not at the next launch: a person who flips Windows into dark mode while RawPage is
    // open and sees nothing happen concludes the setting does not work.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
        if (requested_ == core::Theme::FollowSystem) apply();
    });
}

ThemeManager::~ThemeManager()
{
    if (settingsSubscription_ != 0) settings_.unsubscribe(settingsSubscription_);
}

ThemeManager::Resolved ThemeManager::systemTheme()
{
    switch (QGuiApplication::styleHints()->colorScheme()) {
    case Qt::ColorScheme::Dark:  return Resolved::Dark;
    case Qt::ColorScheme::Light: return Resolved::Light;
    case Qt::ColorScheme::Unknown:
        break;
    }
    return Resolved::Light;
}

QColor ThemeManager::colorFor(ThemeToken token, Resolved theme)
{
    const bool dark = theme == Resolved::Dark;

    // One switch, both columns, no default case: -Werror is on for ui/, so -Wswitch turns a new
    // token with no value into a compile error at this line rather than an invalid QColor at
    // paint time.
    switch (token) {
    case ThemeToken::WindowBackground:    return dark ? QColor(0x1f, 0x1f, 0x1f) : QColor(0xf3, 0xf3, 0xf3);
    case ThemeToken::WindowText:          return dark ? QColor(0xe8, 0xe8, 0xe8) : QColor(0x1a, 0x1a, 0x1a);
    case ThemeToken::SurfaceBackground:   return dark ? QColor(0x2b, 0x2b, 0x2b) : QColor(0xff, 0xff, 0xff);
    case ThemeToken::SurfaceText:         return dark ? QColor(0xe8, 0xe8, 0xe8) : QColor(0x1a, 0x1a, 0x1a);
    case ThemeToken::SubduedText:         return dark ? QColor(0x9a, 0x9a, 0x9a) : QColor(0x5e, 0x5e, 0x5e);
    case ThemeToken::Border:              return dark ? QColor(0x3d, 0x3d, 0x3d) : QColor(0xd0, 0xd0, 0xd0);
    case ThemeToken::Accent:              return dark ? QColor(0x4c, 0x9b, 0xe8) : QColor(0x1a, 0x5f, 0xb4);
    case ThemeToken::AccentText:          return dark ? QColor(0x10, 0x10, 0x10) : QColor(0xff, 0xff, 0xff);
    case ThemeToken::SelectionBackground: return dark ? QColor(0x2d, 0x53, 0x7a) : QColor(0xcc, 0xe0, 0xf5);
    case ThemeToken::SelectionText:       return dark ? QColor(0xff, 0xff, 0xff) : QColor(0x10, 0x10, 0x10);
    // MEASURED, NOT PICKED (issue I-40). Disabled text is the majority of this shell's text --
    // 358 commands are declared and 19 registered -- so the disabled colour is not an edge case
    // here the way it is in a finished product; it is most of the ribbon. The old pair read 3.05
    // against the dark window and 2.54 against the light one, and the dark half was reported
    // unreadable by a person looking at it. Both now clear 3.0 with room: 4.77 dark, 3.87 light,
    // and each stays far below the enabled text's 13.45 / 15.68, so "disabled" still reads as
    // disabled at a glance. `tst_theme` asserts the floor rather than the hex values.
    case ThemeToken::DisabledText:        return dark ? QColor(0x8a, 0x8a, 0x8a) : QColor(0x7a, 0x7a, 0x7a);
    // THE BANNER IS A TINT, SEPARATED FROM THE DESK BY HUE -- IN LIGHT, ALMOST ONLY BY HUE
    // (Stage 2 guide issue I-29). It used to borrow `AlternateBase`, which `apply()` below sets to
    // the WINDOW colour -- so the one place the product says "this document is not what it seems"
    // was painted exactly the grey `PageView` paints around the page, and a person walking M-07
    // could not find it. A warm amber in both themes: a light tint keeps dark text legible, a deep
    // one keeps light text legible, and measured: 24.6 CIE76 units from the light window grey and 30.0 from the dark one, with the text
    // at 15.4 : 1 and 9.9 : 1 on it.
    // `tst_theme` asserts that distance and the text's contrast, never these hex values.
    case ThemeToken::MessageBackground:   return dark ? QColor(0x4a, 0x3a, 0x12) : QColor(0xff, 0xf1, 0xc2);
    case ThemeToken::MessageText:         return dark ? QColor(0xf2, 0xf2, 0xf2) : QColor(0x1a, 0x1a, 0x1a);
    }

    // Unreachable for any declared token; returning an INVALID colour rather than a plausible one
    // means a value that somehow got here shows up as a paint artefact instead of hiding.
    return QColor();
}

QColor ThemeManager::color(ThemeToken token) const
{
    return colorFor(token, resolved_);
}

QString ThemeManager::requestedNameKey() const
{
    switch (requested_) {
    case core::Theme::Light:        return catalog::key::ThemeLight;
    case core::Theme::Dark:         return catalog::key::ThemeDark;
    case core::Theme::FollowSystem: return catalog::key::ThemeFollowSystem;
    }
    return catalog::key::ThemeFollowSystem;
}

void ThemeManager::apply()
{
    if (applying_) return;
    applying_ = true;

    requested_ = settings_.get(core::keys::general::theme);
    fontScale_ = settings_.get(core::keys::general::uiFontScale);

    const Resolved previous = resolved_;

    // ---------------------------------------------------------------------------------------
    // TELL QT THE SCHEME, NOT ONLY THE PALETTE. Issue I-41, and the reason the palette below is
    // necessary but was never sufficient.
    //
    // The native `windows11` style paints control text from ITS OWN colour scheme -- the one Qt
    // derives from Windows -- and ignores the application palette while doing it. Measured, with the
    // OS in light mode and this palette installed: an ENABLED QToolButton's glyph came out #1f1f1f
    // on a #1f1f1f background, and a disabled one #181b1f, DARKER than the background it sat on. A
    // `QLabel` in the same window honoured the palette exactly. So half the shell was themed and the
    // half made of buttons -- which is the ribbon, the toolbar and the tool finder -- was not.
    //
    // `setColorScheme` is how an application says what it is painting, rather than inferring it. The
    // order below is load-bearing: `FollowSystem` hands the scheme BACK to the OS first, because
    // `systemTheme()` reads `styleHints()->colorScheme()` and would otherwise read our own last
    // answer and call it the system's.
    // ---------------------------------------------------------------------------------------
    QStyleHints* const hints = QGuiApplication::styleHints();
    switch (requested_) {
    case core::Theme::Light:
        requestedScheme_ = Qt::ColorScheme::Light;
        hints->setColorScheme(requestedScheme_);
        resolved_ = Resolved::Light;
        break;
    case core::Theme::Dark:
        requestedScheme_ = Qt::ColorScheme::Dark;
        hints->setColorScheme(requestedScheme_);
        resolved_ = Resolved::Dark;
        break;
    case core::Theme::FollowSystem:
        requestedScheme_ = Qt::ColorScheme::Unknown;
        hints->setColorScheme(requestedScheme_);
        resolved_ = systemTheme();
        break;
    }

    QPalette palette;
    palette.setColor(QPalette::Window,          color(ThemeToken::WindowBackground));
    palette.setColor(QPalette::WindowText,      color(ThemeToken::WindowText));
    palette.setColor(QPalette::Base,            color(ThemeToken::SurfaceBackground));
    palette.setColor(QPalette::AlternateBase,   color(ThemeToken::WindowBackground));
    palette.setColor(QPalette::Text,            color(ThemeToken::SurfaceText));
    palette.setColor(QPalette::Button,          color(ThemeToken::WindowBackground));
    palette.setColor(QPalette::ButtonText,      color(ThemeToken::WindowText));
    palette.setColor(QPalette::ToolTipBase,     color(ThemeToken::SurfaceBackground));
    palette.setColor(QPalette::ToolTipText,     color(ThemeToken::SurfaceText));
    palette.setColor(QPalette::PlaceholderText, color(ThemeToken::SubduedText));
    palette.setColor(QPalette::Highlight,       color(ThemeToken::SelectionBackground));
    palette.setColor(QPalette::HighlightedText, color(ThemeToken::SelectionText));
    palette.setColor(QPalette::Link,            color(ThemeToken::Accent));
    palette.setColor(QPalette::Mid,             color(ThemeToken::Border));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, color(ThemeToken::DisabledText));
    palette.setColor(QPalette::Disabled, QPalette::Text,       color(ThemeToken::DisabledText));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, color(ThemeToken::DisabledText));
    QApplication::setPalette(palette);

    if (baseFontPointSize_ > 0.0) {
        QFont font = QApplication::font();
        font.setPointSizeF(baseFontPointSize_ * fontScale_);
        QApplication::setFont(font);
    }

    RAWPAGE_LOG_DEBUG(LogSubsystem::Ui)
        << "theme:" << QString::fromUtf8(core::themeName(requested_).data(),
                                         static_cast<qsizetype>(core::themeName(requested_).size()))
        << "resolved to" << (resolved_ == Resolved::Dark ? "dark" : "light")
        << "at font scale" << fontScale_;

    applying_ = false;

    if (resolved_ != previous) emit themeChanged(resolved_);
}

} // namespace rawpage::ui
