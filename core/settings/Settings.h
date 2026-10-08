// The one typed settings store.
//
// ---------------------------------------------------------------------------------------------
// WHY THE FILE IS READ ONCE INTO MEMORY AND WRITTEN DELIBERATELY, RATHER THAN USED AS A LIVE
// QSETTINGS.
//
// D1-16's downgrade rule is the reason. A settings file written by a NEWER build must be read for
// what we understand and otherwise left BYTE-IDENTICAL -- an older build that rewrites a newer
// build's file destroys those settings during a rollback, which is precisely the moment a user can
// least afford it. `QSettings` cannot express that: it syncs on destruction, so every unrelated
// `setValue` on the way past would rewrite the file, and "we did not mean to" is not a property.
//
// So: `QSettings` loads, an in-memory map serves every read, and writing is an explicit `sync()`
// that is SUPPRESSED entirely in the newer-file case. The user can still change settings in that
// session -- they simply do not persist, and the log says why.
//
// ---------------------------------------------------------------------------------------------
// THE OTHER THREE PROPERTIES THIS CLASS OWES, each with a case in the guide:
//
//   * A value outside its declared range is CLAMPED AND LOGGED, never rejected (A-43). A
//     hand-edited INI that prevents startup is a support call with no diagnosis path.
//   * A malformed file does not prevent startup (A-44) -- unreadable values fall back to their
//     defaults, one log line each.
//   * `changed` fires on an ACTUAL change only (A-42). From Stage 2 this signal invalidates the
//     render cache through flow J, and a `set` to the same value that invalidated it would be a
//     stutter blamed on the renderer for a week.

#ifndef RAWPAGE_CORE_SETTINGS_SETTINGS_H
#define RAWPAGE_CORE_SETTINGS_SETTINGS_H

#include "core/Log.h"
#include "core/Result.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/SettingsSchema.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

class QSettings;

namespace rawpage::core {

class Settings {
public:
    // %APPDATA%\RawPage\settings.ini. Resolved through QStandardPaths, so the portable-mode
    // variant of Features.md 9.2 becomes a different path passed in here rather than a second
    // code path through everything that reads a setting.
    static QString defaultFilePath();

    // `aliases` is the table a retired action spelling in the FILE is resolved through at load --
    // see `migrateRetiredActionIds`. A parameter only so that a test can supply rows the shipped
    // table does not have (Stage 2 I-39); every product caller takes the default.
    explicit Settings(QString iniPath, std::span<const ActionAlias> aliases = kActionAliases);
    ~Settings();

    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    // --- Reading and writing ------------------------------------------------------------------
    template <class T>
    T get(const Key<T>& key) const
    {
        const auto it = values_.find(QString::fromUtf8(key.path.data(),
                                                       static_cast<qsizetype>(key.path.size())));
        if (it == values_.end()) return SettingsCodec<T>::fromDefault(key.defaultValue);

        const std::optional<T> decoded = SettingsCodec<T>::fromVariant(it->second);
        if (!decoded) return SettingsCodec<T>::fromDefault(key.defaultValue);
        return clamp(key, *decoded);
    }

    // No Result: there is no way for this to fail that a caller could act on. An out-of-range value
    // is clamped (and logged), an unchanged value is a no-op, and a suppressed write is a property
    // of the session rather than of this call. `writesSuppressed()` answers that question once.
    template <class T>
    void set(const Key<T>& key, const T& value)
    {
        const QString path = QString::fromUtf8(key.path.data(), static_cast<qsizetype>(key.path.size()));
        const T clamped = clamp(key, value);
        setVariant(path, key.group, SettingsCodec<T>::toVariant(clamped));
    }

    // Whether the FILE carries this key at all -- which is a different question from whether its
    // value happens to equal the default, and the two answers differ exactly where it matters. The
    // Quick Access Toolbar ships three commands by default and the user may remove all three; a
    // stored empty list and a key that was never written are both "empty", and only one of them
    // means "give them the factory toolbar". (T11. T14's Preferences pages want the same question
    // for their reset-to-default affordance.)
    template <class T>
    bool isSet(const Key<T>& key) const
    {
        return values_.find(QString::fromUtf8(key.path.data(),
                                              static_cast<qsizetype>(key.path.size())))
               != values_.end();
    }

    template <class T>
    bool isAtDefault(const Key<T>& key) const
    {
        return get(key) == SettingsCodec<T>::fromDefault(key.defaultValue);
    }

    template <class T>
    void reset(const Key<T>& key)
    {
        set(key, SettingsCodec<T>::fromDefault(key.defaultValue));
    }

    // --- The shortcuts key family (D1-10) -----------------------------------------------------
    // Typed at the boundary in both directions, so `shortcuts/` is a family rather than an escape
    // hatch back into string keys.
    std::optional<QString> shortcutOverride(const ShortcutKey& key) const;
    void setShortcutOverride(const ShortcutKey& key, const QString& chord);
    void clearShortcutOverride(const ShortcutKey& key);

    // Every stored override, in the shape `KeyBindingStore` consumes. A key naming an action or a
    // context that no longer resolves was already dropped at load -- see droppedShortcutKeys().
    std::vector<KeyBindingStore::OverrideEntry> shortcutOverrides() const;

    // How many `shortcuts/` keys were dropped at load because they named something that no longer
    // exists. Reported rather than silent: a user whose remaps vanished deserves a line in the log
    // that says so, and a stage that retires an id without an alias row deserves to see the count
    // move.
    std::size_t droppedShortcutKeyCount() const { return droppedShortcutKeys_; }

    // --- The tool-defaults key family (S6 T8, D6-13) -------------------------------------------
    // `commenting/default<Property>.<tool>`, typed at the boundary as the shortcuts family is: a tool's every
    // declared property as the FILE holds it where its validator accepts it, else its row's default (a refused
    // value is logged); a property the tool has no row for carries `builtInToolDefaults`' value.
    ToolDefaults toolDefaults(CommentingTool tool) const;
    // Writes every property `ToolDefaults.def` declares for `tool` -- and nothing else.
    void setToolDefaults(CommentingTool tool, const ToolDefaults& values);
    // Removes every one of `tool`'s rows from the file: it reads as the table's defaults again.
    void resetToolDefaults(CommentingTool tool);
    // One key's text as the file holds it, or nullopt when it is not set.
    std::optional<QString> toolDefaultText(const ToolDefaultKey& key) const;

    // --- Change notification -------------------------------------------------------------------
    // The plan writes this as `Signal<Key> changed`. There is no Signal type in core/ yet and
    // inventing a core-wide one for a single user would be speculative infrastructure, so it is a
    // subscription list here. `Change` carries the path rather than the `Key<T>` object because a
    // handler is not a template and cannot name a type per key.
    struct Change {
        QString       path;
        SettingsGroup group = SettingsGroup::General;
    };
    using ChangeHandler = std::function<void(const Change&)>;

    // The token is what unsubscribes. A subscriber that outlives this object is a bug; a
    // subscriber that forgets to unsubscribe before it dies is a crash, which is why the token
    // exists rather than a bare function list.
    int subscribe(ChangeHandler handler);
    void unsubscribe(int token);

    // --- Persistence ---------------------------------------------------------------------------
    // Writes every changed key. A no-op when writes are suppressed, and when nothing changed.
    Result<void> sync();

    // D1-16. The version stamped in the file we loaded; kCurrentSchemaVersion for a fresh file.
    int fileSchemaVersion() const { return fileSchemaVersion_; }

    // True when the file was written by a NEWER build: this session reads what it understands,
    // keeps user changes in memory, and never writes.
    bool writesSuppressed() const { return writesSuppressed_; }

    // True when the file we loaded was older and has been (or will be, at the next sync) rewritten
    // at the current version.
    bool wasMigrated() const { return wasMigrated_; }

    const QString& filePath() const { return filePath_; }

    // Keys present in the file that this build does not declare. Preserved, never removed: they may
    // belong to a newer build. Exposed for the guide's A-47 and for --explain-style diagnostics.
    QStringList unknownKeys() const;

private:
    void loadFromFile(std::span<const ActionAlias> aliases);
    void migrateRetiredActionIds(QVariantMap& declared, std::span<const ActionAlias> aliases);
    void setVariant(const QString& path, SettingsGroup group, const QVariant& value);
    void notify(const Change& change);

    template <class T>
    T clamp(const Key<T>& key, const T& value) const
    {
        if constexpr (std::is_arithmetic_v<T>) {
            if (!key.hasRange) return value;
            if (value < key.minimum) {
                logClamp(key.path, key.hasRange);
                return key.minimum;
            }
            if (value > key.maximum) {
                logClamp(key.path, key.hasRange);
                return key.maximum;
            }
        }
        return value;
    }

    // Out of line so the template above does not carry the logging machinery into every
    // instantiation, and so the message is written once.
    void logClamp(std::string_view path, bool hasRange) const;

    // Flushes after every change, so a killed process keeps what the user had already seen take
    // effect (I-47). `sync()` stays public because teardown still calls it and because a caller
    // that wants the Result rather than a log line should have one.
    void autoFlush();

    QString filePath_;
    std::unique_ptr<QSettings> backing_;

    std::map<QString, QVariant> values_;        // declared keys, decoded lazily by get()
    std::map<QString, QVariant> unknown_;       // keys we do not declare; preserved untouched
    std::vector<QString> dirty_;
    std::vector<QString> removed_;   // keys to delete from the file at the next sync

    std::map<int, ChangeHandler> handlers_;
    int nextToken_ = 1;

    int         fileSchemaVersion_ = SettingsSchema::kCurrentSchemaVersion;
    bool        writesSuppressed_ = false;
    bool        wasMigrated_ = false;
    std::size_t droppedShortcutKeys_ = 0;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_SETTINGS_SETTINGS_H
