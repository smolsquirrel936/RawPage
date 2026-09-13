// The settings vocabulary: every key this product has, declared once, with its type.
//
// ---------------------------------------------------------------------------------------------
// D1-10, AND WHY THERE IS NO STRING OVERLOAD ANYWHERE IN THIS FILE.
//
// "An undeclared key is a compile-time error" is achievable only if there is no way to NAME a key
// that does not exist. A `get(QString)` overload, however well-intentioned and however clearly
// documented as a last resort, makes the guarantee decorative -- and settings sprawl is not
// something a later stage cleans up, it is something the stage that introduces the first key either
// prevents or lives with forever. So `get` and `set` take a `Key<T>` object, `Key<T>` objects exist
// only as the constexpr variables generated below from `SettingsKeys.def`, and a misspelled key is a
// name that does not exist. Guide case A-41 asserts exactly that, by compiling a fixture that
// misspells one and requiring the compiler to reject it.
//
// The one genuinely dynamic family -- `shortcuts/<context>/<action>` -- is declared once as a family
// with a validator over KeyContext x ActionId (see ShortcutKey), not as an open string API. It is
// generated, so a shortcut for an action that does not exist cannot be spelled either.
//
// ---------------------------------------------------------------------------------------------
// WHY ENUMS ARE PERSISTED BY NAME AND NOT BY NUMBER.
//
// `general/theme=1` is a file whose meaning changes the day someone inserts a value into the enum.
// `general/theme=dark` is a file that still means dark. The cost is a name table per enum; the
// benefit is that D1-16's migration chain never has to renumber anything, and a user can read and
// hand-edit the file -- which they will, and which A-43 and A-44 require us to survive.

#ifndef RAWPAGE_CORE_SETTINGS_SETTINGSSCHEMA_H
#define RAWPAGE_CORE_SETTINGS_SETTINGSSCHEMA_H

#include "core/Log.h"
#include "core/cmd/ActionId.h"
#include "core/cmd/KeyContext.h"

// THE SETTINGS TABLE NAMES `core/render/` VOCABULARY as of Stage 2 T15 -- `pageDisplay/layoutMode`
// is a `render::LayoutMode` and `pageDisplay/defaultZoom` is a `render::FitMode`. Both are `core/`,
// so this widens nothing; what it avoids is a parallel pair of enums in `core/settings/` listing
// the same values, which is `SettingsKeys.def` rule 2's argument applied to a type instead of to a
// default. `FitMode` MOVED into `core/render/` for exactly this reason.
#include "core/render/DpiPolicy.h"
#include "core/render/PagePresentation.h"

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawpage::core {

// ---------------------------------------------------------------------------------------
// Groups -- the Preferences pages of Features.md 9.1, plus one that is not a page.
// ---------------------------------------------------------------------------------------
// All 26 are declared now even though Stage 1 builds three pages, because the group is a property
// of the KEY and the key is declared once. A later stage adding `pageDisplay/layoutMode` should be
// choosing from a list, not inventing a group name that half-matches an existing page.
enum class SettingsGroup {
    General, Documents, PageDisplay, FullScreen, Commenting, Forms, Identity, Measuring,
    Reading, Search, Security, Signatures, TrustManager, UnitsAndGuides, Spelling,
    MultimediaAnd3D, ConvertToPdf, ConvertFromPdf, Batch, JavaScript, PerformanceAndCache,
    Language, Shortcuts, Updates, AiAssistant, Logging,

    // NOT a Preferences page (SettingsKeys.def rule 4): state that lives in the settings file and
    // is versioned with it, but that a user never sets. `meta/schemaVersion` is the archetype.
    Internal,
};

constexpr std::string_view settingsGroupName(SettingsGroup group)
{
    switch (group) {
    case SettingsGroup::General:             return "General";
    case SettingsGroup::Documents:           return "Documents";
    case SettingsGroup::PageDisplay:         return "Page Display";
    case SettingsGroup::FullScreen:          return "Full Screen";
    case SettingsGroup::Commenting:          return "Commenting";
    case SettingsGroup::Forms:               return "Forms";
    case SettingsGroup::Identity:            return "Identity";
    case SettingsGroup::Measuring:           return "Measuring";
    case SettingsGroup::Reading:             return "Reading";
    case SettingsGroup::Search:              return "Search";
    case SettingsGroup::Security:            return "Security";
    case SettingsGroup::Signatures:          return "Signatures";
    case SettingsGroup::TrustManager:        return "Trust Manager";
    case SettingsGroup::UnitsAndGuides:      return "Units & Guides";
    case SettingsGroup::Spelling:            return "Spelling";
    case SettingsGroup::MultimediaAnd3D:     return "Multimedia & 3D";
    case SettingsGroup::ConvertToPdf:        return "Convert to PDF";
    case SettingsGroup::ConvertFromPdf:      return "Convert from PDF";
    case SettingsGroup::Batch:               return "Batch";
    case SettingsGroup::JavaScript:          return "JavaScript";
    case SettingsGroup::PerformanceAndCache: return "Performance & Cache";
    case SettingsGroup::Language:            return "Language";
    case SettingsGroup::Shortcuts:           return "Shortcuts";
    case SettingsGroup::Updates:             return "Updates";
    case SettingsGroup::AiAssistant:         return "AI Assistant";
    case SettingsGroup::Logging:             return "Logging";
    case SettingsGroup::Internal:            return "(internal)";
    }
    return "?";
}

// ---------------------------------------------------------------------------------------
// Value types that are not already C++ or Qt types
// ---------------------------------------------------------------------------------------
// `ThemeManager` (T9) turns this into a palette. It lives here rather than in ui/ because the KEY's
// type has to be nameable from core/, and because a theme choice is state, not a widget.
//
// FollowSystem is a third value, not "no value": "follow the system" is a distinct, persistent user
// choice from "light, which currently happens to match the system".
enum class Theme { Light, Dark, FollowSystem };

constexpr std::string_view themeName(Theme theme)
{
    switch (theme) {
    case Theme::Light:        return "light";
    case Theme::Dark:         return "dark";
    case Theme::FollowSystem: return "followSystem";
    }
    return "?";
}

constexpr std::optional<Theme> themeFromName(std::string_view name)
{
    if (name == "light") return Theme::Light;
    if (name == "dark") return Theme::Dark;
    if (name == "followSystem") return Theme::FollowSystem;
    return std::nullopt;
}

constexpr std::optional<LogLevel> logLevelFromName(std::string_view name)
{
    if (name == "error") return LogLevel::Error;
    if (name == "warning") return LogLevel::Warning;
    if (name == "info") return LogLevel::Info;
    if (name == "debug") return LogLevel::Debug;
    if (name == "trace") return LogLevel::Trace;
    return std::nullopt;
}

inline constexpr std::size_t kLogSubsystemCount = 7;   // core/Log.h's LogSubsystem, all of it

constexpr std::optional<LogSubsystem> logSubsystemFromName(std::string_view name)
{
    if (name == "render") return LogSubsystem::Render;
    if (name == "parse")  return LogSubsystem::Parse;
    if (name == "save")   return LogSubsystem::Save;
    if (name == "sign")   return LogSubsystem::Sign;
    if (name == "ocr")    return LogSubsystem::Ocr;
    if (name == "batch")  return LogSubsystem::Batch;
    if (name == "ui")     return LogSubsystem::Ui;
    return std::nullopt;
}

// A set over those seven, persisted as a comma-separated list of their names. A bitmask rather than
// a QStringList so the filtering decision at every log site is one AND, and so an unknown name in a
// hand-edited file is something we can drop and report rather than a string that silently disables
// nothing.
class LogSubsystemMask {
public:
    constexpr LogSubsystemMask() = default;
    explicit constexpr LogSubsystemMask(std::uint32_t bits) : bits_(bits) {}

    static constexpr LogSubsystemMask all() { return LogSubsystemMask{(1u << kLogSubsystemCount) - 1u}; }
    static constexpr LogSubsystemMask none() { return LogSubsystemMask{0}; }

    constexpr bool contains(LogSubsystem subsystem) const { return (bits_ & bit(subsystem)) != 0; }
    constexpr void add(LogSubsystem subsystem) { bits_ |= bit(subsystem); }
    constexpr void remove(LogSubsystem subsystem) { bits_ &= ~bit(subsystem); }

    constexpr std::uint32_t bits() const { return bits_; }

    friend constexpr bool operator==(LogSubsystemMask a, LogSubsystemMask b) { return a.bits_ == b.bits_; }
    friend constexpr bool operator!=(LogSubsystemMask a, LogSubsystemMask b) { return a.bits_ != b.bits_; }

private:
    static constexpr std::uint32_t bit(LogSubsystem subsystem)
    {
        return 1u << static_cast<unsigned>(subsystem);
    }

    std::uint32_t bits_ = (1u << kLogSubsystemCount) - 1u;
};

// ---------------------------------------------------------------------------------------
// Key<T>
// ---------------------------------------------------------------------------------------
// The default is stored in whatever type can be a constant expression: a QString cannot, so a
// QString-valued key carries a `std::string_view` default and `SettingsCodec` widens it at the one
// point of use. Keeping the KEY constexpr is what makes it usable as a compile-time name.
template <class T> struct SettingsDefaultRepr { using type = T; };
template <> struct SettingsDefaultRepr<QString> { using type = std::string_view; };
// Semicolon-separated, and empty means an empty list. No list default has content today; when one
// does, this is the spelling.
template <> struct SettingsDefaultRepr<QStringList> { using type = std::string_view; };

template <class T>
struct Key {
    using Default = typename SettingsDefaultRepr<T>::type;

    std::string_view path;                     // the permanent public name, matching the INI
    Default          defaultValue{};
    SettingsGroup    group = SettingsGroup::General;
    bool             policyLockable = false;   // Stage 15's PolicyOverlay reads this; nothing does yet

    // Only meaningful for an arithmetic T. Out of range is CLAMPED and logged, never rejected
    // (SettingsKeys.def rule 3).
    bool    hasRange = false;
    Default minimum{};
    Default maximum{};
};

// ---------------------------------------------------------------------------------------
// Codecs -- how each value type becomes a line in an INI file, and back
// ---------------------------------------------------------------------------------------
// `fromVariant` returns nothing for a value it cannot make sense of, and the caller turns that into
// "fall back to the default and log it". Deliberate: a hand-edited `general/theme=purple` must not
// prevent startup (A-43, A-44), and a codec that guessed would be a codec that silently changed the
// user's setting to something they never chose.
template <class T> struct SettingsCodec;

// Every scalar codec reads through this rather than through `QVariant::toString()`, and the reason
// is measured rather than defensive: QSettings' INI reader turns ANY value containing a comma into
// a QStringList, and `QVariant::toString()` on a QStringList returns an EMPTY STRING. So
// `logging/subsystems=render,parse` read back as unset and silently reverted to the default -- a
// setting that ignores what the file says, with no error anywhere.
inline QString settingsVariantText(const QVariant& variant)
{
    if (variant.typeId() == QMetaType::QStringList) {
        return variant.toStringList().join(QLatin1Char(','));
    }
    return variant.toString();
}

template <> struct SettingsCodec<bool> {
    static QVariant toVariant(bool value) { return QVariant(value); }
    static std::optional<bool> fromVariant(const QVariant& variant)
    {
        // QSettings hands back a QString from an INI file, and QVariant::toBool() maps every
        // non-empty string that is not "0" or "false" to true -- so a typo would read as `true`,
        // which is the wrong direction for every one of these keys.
        const QString text = settingsVariantText(variant).trimmed().toLower();
        if (text == QLatin1String("true") || text == QLatin1String("1")) return true;
        if (text == QLatin1String("false") || text == QLatin1String("0")) return false;
        return std::nullopt;
    }
    static bool fromDefault(bool value) { return value; }
};

template <> struct SettingsCodec<int> {
    static QVariant toVariant(int value) { return QVariant(value); }
    static std::optional<int> fromVariant(const QVariant& variant)
    {
        bool ok = false;
        const int value = settingsVariantText(variant).trimmed().toInt(&ok);
        return ok ? std::optional<int>(value) : std::nullopt;
    }
    static int fromDefault(int value) { return value; }
};

template <> struct SettingsCodec<double> {
    static QVariant toVariant(double value) { return QVariant(value); }
    static std::optional<double> fromVariant(const QVariant& variant)
    {
        bool ok = false;
        // QString::toDouble is C-locale by definition, which is what we want: a settings file
        // written where the decimal separator is a comma must still read on a machine where it is
        // a point, and vice versa.
        const double value = settingsVariantText(variant).trimmed().toDouble(&ok);
        return ok ? std::optional<double>(value) : std::nullopt;
    }
    static double fromDefault(double value) { return value; }
};

template <> struct SettingsCodec<QString> {
    static QVariant toVariant(const QString& value) { return QVariant(value); }
    static std::optional<QString> fromVariant(const QVariant& variant)
    {
        if (!variant.isValid()) return std::nullopt;
        return settingsVariantText(variant);
    }
    static QString fromDefault(std::string_view value)
    {
        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    }
};

template <> struct SettingsCodec<QStringList> {
    static QVariant toVariant(const QStringList& value) { return QVariant(value); }
    static std::optional<QStringList> fromVariant(const QVariant& variant)
    {
        if (!variant.isValid()) return std::nullopt;
        // THE QSETTINGS INI GOTCHA, and it is not theoretical: a list written with ONE element
        // comes back as a bare QString rather than a one-element QStringList, so a plain
        // `variant.toStringList()` yields an EMPTY list for exactly the case of one pinned file.
        if (variant.typeId() == QMetaType::QStringList) return variant.toStringList();
        const QString text = variant.toString();
        if (text.isEmpty()) return QStringList{};  // and an absent value is an empty list, not one empty item
        return QStringList{text};
    }
    static QStringList fromDefault(std::string_view value)
    {
        if (value.empty()) return QStringList{};
        return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()))
            .split(QLatin1Char(';'), Qt::SkipEmptyParts);
    }
};

template <> struct SettingsCodec<Theme> {
    static QVariant toVariant(Theme value)
    {
        const std::string_view name = themeName(value);
        return QVariant(QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size())));
    }
    static std::optional<Theme> fromVariant(const QVariant& variant)
    {
        return themeFromName(settingsVariantText(variant).trimmed().toStdString());
    }
    static Theme fromDefault(Theme value) { return value; }
};

// The two Stage 2 enums. Both write the name their own `*Name()` function returns, so a combo box
// cannot offer a value the file cannot hold -- and both answer `nullopt` for a name they do not
// know, which makes `Settings` fall back to the declared default AND log it, rather than a typo in
// a hand-edited INI silently meaning something real.
template <> struct SettingsCodec<render::LayoutMode> {
    static QVariant toVariant(render::LayoutMode value)
    {
        return QVariant(QString::fromUtf8(render::layoutModeName(value)));
    }
    static std::optional<render::LayoutMode> fromVariant(const QVariant& variant)
    {
        return render::layoutModeFromName(settingsVariantText(variant).trimmed().toStdString());
    }
    static render::LayoutMode fromDefault(render::LayoutMode value) { return value; }
};

template <> struct SettingsCodec<render::FitMode> {
    static QVariant toVariant(render::FitMode value)
    {
        return QVariant(QString::fromUtf8(render::fitModeName(value)));
    }
    static std::optional<render::FitMode> fromVariant(const QVariant& variant)
    {
        return render::fitModeFromName(settingsVariantText(variant).trimmed().toStdString());
    }
    static render::FitMode fromDefault(render::FitMode value) { return value; }
};

template <> struct SettingsCodec<LogLevel> {
    static QVariant toVariant(LogLevel value) { return QVariant(QString::fromUtf8(logLevelName(value))); }
    static std::optional<LogLevel> fromVariant(const QVariant& variant)
    {
        return logLevelFromName(settingsVariantText(variant).trimmed().toLower().toStdString());
    }
    static LogLevel fromDefault(LogLevel value) { return value; }
};

template <> struct SettingsCodec<LogSubsystemMask> {
    static QVariant toVariant(LogSubsystemMask value)
    {
        QStringList names;
        for (std::size_t i = 0; i < kLogSubsystemCount; ++i) {
            const auto subsystem = static_cast<LogSubsystem>(i);
            if (value.contains(subsystem)) names << QString::fromUtf8(logSubsystemName(subsystem));
        }
        // The empty set is a real, choosable value -- "log nothing" -- so it is written as an
        // explicit token rather than as an empty string, which an INI cannot tell from absent.
        return names.isEmpty() ? QVariant(QStringLiteral("none"))
                               : QVariant(names.join(QLatin1Char(',')));
    }
    static std::optional<LogSubsystemMask> fromVariant(const QVariant& variant)
    {
        const QString text = settingsVariantText(variant).trimmed();
        if (text.isEmpty()) return std::nullopt;
        if (text.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0) return LogSubsystemMask::none();
        if (text.compare(QLatin1String("all"), Qt::CaseInsensitive) == 0) return LogSubsystemMask::all();

        LogSubsystemMask mask = LogSubsystemMask::none();
        bool sawOne = false;
        for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const std::optional<LogSubsystem> subsystem =
                logSubsystemFromName(part.trimmed().toLower().toStdString());
            // An unknown name is dropped rather than fatal: a file written by a build with an
            // eighth subsystem must still configure the seven this build has.
            if (!subsystem) continue;
            mask.add(*subsystem);
            sawOne = true;
        }
        return sawOne ? std::optional<LogSubsystemMask>(mask) : std::nullopt;
    }
    static LogSubsystemMask fromDefault(LogSubsystemMask value) { return value; }
};

// ---------------------------------------------------------------------------------------
// What kind of editor a key wants, and what to call it -- T14
// ---------------------------------------------------------------------------------------
// A Preferences page is required to be built FROM this schema rather than hand-laid-out
// (Stage1-AppShell-Plan.md 6, T14), and a page cannot do that from a path, a group and a default
// alone: it has to know which widget a value wants and what word to put beside it. Both answers
// are derived from the key's TYPE and its PATH -- neither is a new column in SettingsKeys.def, and
// that is deliberate. A fifth column saying "checkbox" would be a second opinion about what `bool`
// means, and it would be wrong the first time somebody filled it in by hand.
//
// The value of putting this in core/ rather than in ui/: it is what makes the mapping testable
// with no display, and it is what Stage 15's PolicyOverlay reads when it has to say "this control
// is locked" for a control it does not own.

enum class SettingsEditor {
    Toggle,    // bool
    Integer,   // int, with the declared range as the spin box's bounds
    Decimal,   // double, likewise
    Text,      // QString
    List,      // QStringList
    Choice,    // one of a closed, named set -- Theme, LogLevel
    Flags,     // a subset of a closed, named set -- LogSubsystemMask
};

// One member of a Choice or Flags set. The token is EXACTLY what the codec writes into the INI, so
// a page can round-trip a value through `SettingsCodec<T>` rather than carrying its own idea of
// what "dark" means; the label key is what the user reads.
struct SettingsChoice {
    std::string token;
    std::string labelKey;
};

// The catalog key naming a setting: "general/uiFontScale" -> "prefs.setting.general.uiFontScale".
//
// DERIVED, exactly as `ActionId.def`'s rule 3 derives an action's catalog key from its string id,
// and for the same reason: a hand-written key column is a second list, and the day it disagrees
// with the path the symptom is a Preferences row labelled `prefs.setting.general.uiFontSacle`. The
// catalogs are hand-maintained data, so a key missing from `en` is a defect this convention makes
// mechanically checkable -- guide case A-91 walks every declared key and asserts each resolves.
//
// `prefs.setting.` rather than a bare `prefs.` and not for tidiness: `tools/i18n-lint` treats a
// quoted key reference ending in a dot as a NAMESPACE reference covering every key beneath it,
// which is how these generated keys avoid being reported as orphans. A prefix of `prefs.` would
// have covered the whole Preferences namespace and switched the orphan check off for all of it.
inline std::string settingsLabelKey(std::string_view path)
{
    std::string key = "prefs.setting.";
    for (const char c : path) key.push_back(c == '/' ? '.' : c);
    return key;
}

// The per-type answer. Specialized rather than switched, so that a key whose type has no editor is
// a COMPILE error at the point the key is declared rather than a blank row on a page.
template <class T> struct SettingsEditorTraits;

template <> struct SettingsEditorTraits<bool> {
    static constexpr SettingsEditor editor = SettingsEditor::Toggle;
    static std::vector<SettingsChoice> choices() { return {}; }
};
template <> struct SettingsEditorTraits<int> {
    static constexpr SettingsEditor editor = SettingsEditor::Integer;
    static std::vector<SettingsChoice> choices() { return {}; }
};
template <> struct SettingsEditorTraits<double> {
    static constexpr SettingsEditor editor = SettingsEditor::Decimal;
    static std::vector<SettingsChoice> choices() { return {}; }
};
template <> struct SettingsEditorTraits<QString> {
    static constexpr SettingsEditor editor = SettingsEditor::Text;
    static std::vector<SettingsChoice> choices() { return {}; }
};
template <> struct SettingsEditorTraits<QStringList> {
    static constexpr SettingsEditor editor = SettingsEditor::List;
    static std::vector<SettingsChoice> choices() { return {}; }
};

template <> struct SettingsEditorTraits<Theme> {
    static constexpr SettingsEditor editor = SettingsEditor::Choice;

    // The tokens come from `themeName`, which is also what the codec writes -- so the combo box
    // cannot offer a value the file cannot hold. The LABELS are the shell's own theme keys rather
    // than new `prefs.*` ones: `ui/shell/ShellKeys.def` says in its own comment that a theme name
    // reading one way in Preferences and another in the status bar is the first localization bug
    // anyone files, and two keys for one word is how that happens.
    static std::vector<SettingsChoice> choices()
    {
        std::vector<SettingsChoice> out;
        for (const Theme theme : {Theme::Light, Theme::Dark, Theme::FollowSystem}) {
            const std::string_view token = themeName(theme);
            out.push_back(SettingsChoice{std::string(token),
                                         "shell.theme." + std::string(token)});
        }
        return out;
    }
};

template <> struct SettingsEditorTraits<render::LayoutMode> {
    static constexpr SettingsEditor editor = SettingsEditor::Choice;
    static std::vector<SettingsChoice> choices()
    {
        std::vector<SettingsChoice> out;
        for (const render::LayoutMode mode :
             {render::LayoutMode::Single, render::LayoutMode::SingleContinuous,
              render::LayoutMode::TwoUp, render::LayoutMode::TwoUpContinuous}) {
            const std::string token = render::layoutModeName(mode);
            out.push_back(SettingsChoice{token, "prefs.choice.layoutMode." + token});
        }
        return out;
    }
};

template <> struct SettingsEditorTraits<render::FitMode> {
    static constexpr SettingsEditor editor = SettingsEditor::Choice;
    static std::vector<SettingsChoice> choices()
    {
        std::vector<SettingsChoice> out;
        for (const render::FitMode mode : {render::FitMode::None, render::FitMode::Page,
                                           render::FitMode::Width, render::FitMode::Height,
                                           render::FitMode::Visible}) {
            const std::string token = render::fitModeName(mode);
            out.push_back(SettingsChoice{token, "prefs.choice.fitMode." + token});
        }
        return out;
    }
};

template <> struct SettingsEditorTraits<LogLevel> {
    static constexpr SettingsEditor editor = SettingsEditor::Choice;
    static std::vector<SettingsChoice> choices()
    {
        std::vector<SettingsChoice> out;
        for (const LogLevel level : {LogLevel::Error, LogLevel::Warning, LogLevel::Info,
                                     LogLevel::Debug, LogLevel::Trace}) {
            const std::string token = logLevelName(level);
            out.push_back(SettingsChoice{token, "prefs.choice.logLevel." + token});
        }
        return out;
    }
};

template <> struct SettingsEditorTraits<LogSubsystemMask> {
    static constexpr SettingsEditor editor = SettingsEditor::Flags;
    static std::vector<SettingsChoice> choices()
    {
        std::vector<SettingsChoice> out;
        for (std::size_t i = 0; i < kLogSubsystemCount; ++i) {
            const std::string token = logSubsystemName(static_cast<LogSubsystem>(i));
            out.push_back(SettingsChoice{token, "prefs.choice.logSubsystem." + token});
        }
        return out;
    }
};

// ---------------------------------------------------------------------------------------
// The keys themselves -- generated, never hand-written
// ---------------------------------------------------------------------------------------
// Each row reopens its namespace. That is legal, and it is what lets one flat table produce
// `keys::general::theme` and `keys::logging::level` without a second list saying which key belongs
// to which namespace.
namespace keys {

#define RAWPAGE_SETTING(Namespace, Name, Type, Path, Default, Group, PolicyLockable)           \
    namespace Namespace {                                                                     \
    inline constexpr Key<Type> Name{Path, Default, SettingsGroup::Group, PolicyLockable};      \
    }
#define RAWPAGE_SETTING_RANGED(Namespace, Name, Type, Path, Default, Group, PolicyLockable, Min, Max) \
    namespace Namespace {                                                                     \
    inline constexpr Key<Type> Name{Path, Default, SettingsGroup::Group, PolicyLockable, true, Min, Max}; \
    }
#include "core/settings/SettingsKeys.def"

} // namespace keys

// ---------------------------------------------------------------------------------------
// The `shortcuts/<context>/<action>` key family -- the twelfth Stage 1 key (D1-10)
// ---------------------------------------------------------------------------------------
// A family, not a row: 12 contexts x 358 actions is not a table anyone maintains. What matters is
// that it is still impossible to NAME a key outside the family -- `ShortcutKey` has no constructor
// from a string, so the only way to obtain one is to hand it a real `KeyContext` and a real
// `ActionId`. A stale key found in a FILE is a different problem, and is handled at load: dropped
// and logged, never fatal (Settings::shortcutOverrides).
class ShortcutKey {
public:
    static ShortcutKey forAction(KeyContext context, ActionId action)
    {
        return ShortcutKey(context, action);
    }

    KeyContext context() const { return context_; }
    ActionId   action() const { return action_; }

    // "shortcuts/Viewer/view.zoomIn" -- the context by NAME and the action by its permanent string
    // id, so the file survives either enum being renumbered.
    QString path() const;

    static constexpr SettingsGroup group() { return SettingsGroup::Shortcuts; }
    static constexpr std::string_view pathPrefix() { return "shortcuts/"; }

private:
    ShortcutKey(KeyContext context, ActionId action) : context_(context), action_(action) {}

    KeyContext context_;
    ActionId   action_;
};

// ---------------------------------------------------------------------------------------
// The runtime view of the schema
// ---------------------------------------------------------------------------------------
// What a Preferences page (T14) and a policy overlay (Stage 15) iterate. Built from the same .def,
// so a key that exists cannot be missing from it.
struct SettingsKeyInfo {
    std::string_view path;
    SettingsGroup    group = SettingsGroup::General;
    bool             policyLockable = false;
    bool             hasRange = false;
    QVariant         defaultValue;
    QVariant         minimum;
    QVariant         maximum;

    // T14. What a generated Preferences row needs on top of the above: which widget, what to call
    // it, and -- for a closed set -- what the members are. All three are derived from the row's
    // TYPE and PATH by the traits above; none is a new column anybody maintains.
    SettingsEditor              editor = SettingsEditor::Text;
    std::string                 labelKey;
    std::vector<SettingsChoice> choices;
};

namespace SettingsSchema {

const std::vector<SettingsKeyInfo>& allKeys();

const SettingsKeyInfo* findKey(const QString& path);

// Nothing outside `allKeys()` and the shortcuts family is a declared key. Used at load to tell a
// key we never had from a key a NEWER build wrote -- D1-16 treats the two differently.
//
// The alias table is a parameter for one reason, and it is a testing one (Stage 2 I-39): the
// shipped table has zero rows, so a `shortcuts/` key under a RETIRED spelling can only be loaded by
// a test that supplies its own rows. Every product caller takes the default.
bool isDeclaredPath(const QString& path, std::span<const ActionAlias> aliases = kActionAliases);

// D1-16. Bumped when a key's TYPE or MEANING changes -- not when a key is added, since an added key
// simply reads as its default in an older file and needs no migration at all.
inline constexpr int kCurrentSchemaVersion = 1;

// An ordered chain: each entry migrates a file at `fromVersion` to `fromVersion + 1`, applied in
// order, so a version-1 file five versions later runs four steps rather than one special case per
// pair of versions. Empty today, and that emptiness is the point -- the mechanism exists before the
// first migration needs it, which is the only way anyone ever knows it works.
struct Migration {
    int         fromVersion = 0;
    const char* description = "";
    void      (*apply)(QVariantMap& values) = nullptr;   // rewrites the flat key/value map in place
};

const std::vector<Migration>& migrations();

// Runs `chain` over `values`, in ascending `fromVersion` order, from `fromVersion` up to
// `targetVersion`, and returns the version reached.
//
// A version with NO migration row is not an error and not a gap: it means that bump added keys and
// changed no existing one, which is the common case, and an added key already reads as its default
// in an older file. So the return value is `targetVersion` whenever `fromVersion` is below it,
// whether or not any row applied.
//
// Taking the chain as a parameter rather than reading `migrations()` is what lets `tst_settings`
// drive a SYNTHETIC chain and prove the ordering and the rewrite actually happen -- today's real
// chain is empty, and a mechanism whose only test is an empty list is not tested at all.
int migrate(QVariantMap& values, int fromVersion, const std::vector<Migration>& chain, int targetVersion);

} // namespace SettingsSchema

} // namespace rawpage::core

#endif // RAWPAGE_CORE_SETTINGS_SETTINGSSCHEMA_H
