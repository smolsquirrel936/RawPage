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
    case ThemeToken::DisabledText:        return dark ? QColor(0x6a, 0x6a, 0x6a) : QColor(0x9a, 0x9a, 0x9a);
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
    requested_ = settings_.get(core::keys::general::theme);
    fontScale_ = settings_.get(core::keys::general::uiFontScale);

    const Resolved previous = resolved_;
    switch (requested_) {
    case core::Theme::Light:        resolved_ = Resolved::Light; break;
    case core::Theme::Dark:         resolved_ = Resolved::Dark; break;
    case core::Theme::FollowSystem: resolved_ = systemTheme(); break;
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

    if (resolved_ != previous) emit themeChanged(resolved_);
}

} // namespace rawpage::ui
