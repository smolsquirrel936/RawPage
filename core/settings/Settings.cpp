#include "core/settings/Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include <set>
#include <utility>

namespace rawpage::core {
namespace {

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

const QString kSchemaVersionPath = QStringLiteral("meta/schemaVersion");

// **THE `general/` GROUP IS STORED AS `General/` -- Stage 4 guide I-15.** QSettings writes a group
// named `general` as the section `[%General]` (the escape for its own reserved `[General]`), and reads
// `[%General]` back as the group `General` -- CAPITAL G, measured 2026-09-26 against the user's own
// settings.ini. The schema declares `general/...`, so every key in that section arrived as
// `General/theme`, `General/restoreSession`, matched nothing declared, and was preserved as UNKNOWN:
// **no `general/` preference was ever read back from the file** after a real restart. The suite never
// saw it because QSettings caches a file per process, so a reload in the same process re-read the
// cache rather than the file. And writing one key produced a SECOND `[%General]` section, because the
// cache then held both `General/...` (read) and `general/...` (written) and QSettings emits each group.
//
// So the file speaks QSettings' spelling and nothing above this boundary ever sees it: a path is
// mapped on every read from and write to `backing_`, and nowhere else.
const QString kSchemaGeneral = QStringLiteral("general/");
const QString kFileGeneral = QStringLiteral("General/");

QString toFileKey(const QString& path)
{
    return path.startsWith(kSchemaGeneral) ? kFileGeneral + path.mid(kSchemaGeneral.size()) : path;
}

QString fromFileKey(const QString& key)
{
    return key.startsWith(kFileGeneral) ? kSchemaGeneral + key.mid(kFileGeneral.size()) : key;
}

} // namespace

QString Settings::defaultFilePath()
{
    // %APPDATA%\RawPage\settings.ini -- the path Implementation-Plan.md's Stage 1 section names.
    //
    // AppDataLocation is %APPDATA%\<org>\<app>, which is exactly what we want BECAUSE app/ leaves
    // the organization name unset: we are the organization, and setting it to "RawPage" as well
    // produced %APPDATA%\RawPage\RawPage\settings.ini on the first end-to-end run of the
    // composition root (issue I-13, fixed in app/main.cpp where the names are set, not here).
    // GenericConfigLocation is not the answer either -- on Windows it is %LOCALAPPDATA%, and a
    // preference that does not roam with the user is a preference that is missing on their second
    // machine.
    //
    // T5 could not have caught this: every one of its tests passes an explicit path, deliberately,
    // so that running the suite does not rewrite the settings of the person running it. T8 is the
    // first caller that ever reaches this function.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/settings.ini");
}

Settings::Settings(QString iniPath, std::span<const ActionAlias> aliases)
    : filePath_(std::move(iniPath))
{
    backing_ = std::make_unique<QSettings>(filePath_, QSettings::IniFormat);
    loadFromFile(aliases);
}

Settings::~Settings() = default;

void Settings::loadFromFile(std::span<const ActionAlias> aliases)
{
    // In the SCHEMA's spelling (I-15): `General/theme` in the file is `general/theme` from here on.
    // A file the defect damaged -- two `[%General]` sections -- needs nothing further: QSettings
    // merges them into one `General` group on read, and the next write emits that group once.
    QStringList fileKeys;
    for (const QString& key : backing_->allKeys()) fileKeys.push_back(fromFileKey(key));

    // --- D1-16: what version wrote this file ---------------------------------------------------
    // Absent-and-empty is a FRESH file, which is the current version by construction. Absent with
    // keys in it is a file from before versioning existed -- version 0 -- and that distinction
    // matters, because the second one runs the migration chain and the first must not be reported
    // as migrated.
    int storedVersion = SettingsSchema::kCurrentSchemaVersion;
    const bool hasVersionStamp = backing_->contains(kSchemaVersionPath);
    if (hasVersionStamp) {
        bool ok = false;
        storedVersion = backing_->value(kSchemaVersionPath).toString().trimmed().toInt(&ok);
        if (!ok) storedVersion = 0;
    } else if (!fileKeys.isEmpty()) {
        storedVersion = 0;
    }

    if (storedVersion > SettingsSchema::kCurrentSchemaVersion) {
        // The downgrade case. Read what we understand; write nothing, ever, in this session.
        writesSuppressed_ = true;
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "settings: file was written by a newer build (schema version" << storedVersion
            << "> this build's" << SettingsSchema::kCurrentSchemaVersion
            << ") -- reading what is understood and writing nothing, so a rollback does not"
            << "destroy settings this build cannot represent";
    }

    // --- Partition the file into declared, unknown, and stale ----------------------------------
    QVariantMap declared;
    for (const QString& key : fileKeys) {
        if (key == kSchemaVersionPath) continue;

        const QVariant value = backing_->value(toFileKey(key));

        if (SettingsSchema::isDeclaredPath(key, aliases)) {
            declared.insert(key, value);
            continue;
        }

        // THE `[General]` FOLD, measured rather than assumed. QSettings treats an INI section
        // named `General` -- case-insensitively -- as its own reserved fallback section: a
        // hand-written `[general]` yields keys with NO group at all, and QSettings writes our
        // `general/` group back out as `[%General]` to avoid the collision. The consequence, if
        // nothing did this, is that the exact edit the testing guide asks a human to make
        // (`general/uiFontScale=9.0` under `[general]`) would be read as an unknown top-level key
        // and silently ignored. Mapping is attempted ONLY when the result is a declared key, so a
        // genuinely top-level key a newer build wrote is still preserved as itself.
        if (!key.contains(QLatin1Char('/'))) {
            const QString folded = QStringLiteral("general/") + key;
            if (SettingsSchema::isDeclaredPath(folded, aliases) && !fileKeys.contains(folded)) {
                declared.insert(folded, value);
                // Normalized at the next write: the value is queued to be written under the
                // canonical path AND the top-level spelling is queued for removal. Doing only the
                // second half would delete the user's hand edit and write nothing in its place --
                // which is how a "harmless cleanup" becomes the bug report "it forgets my theme".
                dirty_.push_back(folded);
                removed_.push_back(key);
                continue;
            }
        }

        // A `shortcuts/` key whose context or action no longer resolves is STALE, not unknown: we
        // know the family, and we know this member of it does not exist. Dropped with a count, per
        // Stage1-AppShell-Plan.md 5.2 -- silently retaining it would leave a remap in the file that
        // nothing can ever apply.
        if (key.startsWith(toQString(ShortcutKey::pathPrefix()))) {
            ++droppedShortcutKeys_;
            // Queued for deletion, not merely skipped: Stage1-AppShell-Plan.md 5.2 says a stale
            // key is "detected and dropped at load", and a key that is only ignored stays in the
            // file forever, re-reported on every launch.
            removed_.push_back(key);
            RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
                << "settings: dropping shortcut override for an unknown context or action:" << key;
            continue;
        }

        // Everything else is preserved untouched. It may belong to a newer build (D1-16) or to a
        // feature this build was compiled without; either way, deleting it is the one thing we
        // must not do.
        unknown_.emplace(key, value);
    }

    // --- D1-16: a retired action spelling becomes the current one ------------------------------
    migrateRetiredActionIds(declared, aliases);

    // --- Migrate, if the file is older ---------------------------------------------------------
    if (storedVersion < SettingsSchema::kCurrentSchemaVersion) {
        const int reached = SettingsSchema::migrate(declared, storedVersion,
                                                    SettingsSchema::migrations(),
                                                    SettingsSchema::kCurrentSchemaVersion);
        wasMigrated_ = hasVersionStamp || !fileKeys.isEmpty();
        fileSchemaVersion_ = reached;

        if (wasMigrated_) {
            // Every key is rewritten, not just the ones a migration touched: the file is being
            // restamped at the new version, and a half-rewritten file whose stamp says otherwise
            // is worse than either state.
            for (auto it = declared.constBegin(); it != declared.constEnd(); ++it) {
                dirty_.push_back(it.key());
            }
            dirty_.push_back(kSchemaVersionPath);
        }
    } else {
        fileSchemaVersion_ = storedVersion;
    }

    for (auto it = declared.constBegin(); it != declared.constEnd(); ++it) {
        values_.emplace(it.key(), it.value());
    }

    // A fresh file gets its stamp on the first write. Writing it here would create a settings file
    // for a process that only ever READ its settings, which is how a portable install acquires an
    // %APPDATA% directory it never wanted.
    if (!hasVersionStamp && fileKeys.isEmpty()) {
        fileSchemaVersion_ = SettingsSchema::kCurrentSchemaVersion;
    }
}

void Settings::migrateRetiredActionIds(QVariantMap& declared, std::span<const ActionAlias> aliases)
{
    // STAGE 2 ISSUE I-39. D1-16 promises that a renamed action keeps a user's saved shortcuts and
    // Quick Access Toolbar. Resolving the old spelling is only the READ half of that promise, and
    // until I-39 it was the only half. Everything that WRITES builds the key from the enum -- a
    // Preferences reset clears `shortcuts/Viewer/<current>`, a rebind sets it -- so a key left under
    // `shortcuts/Viewer/<retired>` was one no write could ever reach: a reset came back on the next
    // launch, and a rebind raced the old remap with `values_`' alphabetical order as the referee.
    //
    // So the file is brought to the current spelling HERE, once, and nothing downstream ever meets
    // a retired one. The old key is queued for removal and the new one written at the next sync --
    // the same two-halves shape the `[General]` fold above uses, for the same reason. Nothing syncs
    // at load: a process that only read its settings still writes nothing (and a file from a newer
    // build is never written at all), so a load that is never followed by a change renames again
    // next time, which is harmless.
    //
    // Only two settings hold action ids, `shortcuts/<context>/<action>` and `ui/qatCommands`
    // (`SettingsKeys.def`). A third would need a branch here, and its tests would find it absent.
    if (aliases.empty()) return;

    const auto currentSpelling = [&](const std::string& stringId) -> std::optional<QString> {
        if (!actionStringIdIsRetiredIn(kActions, aliases, stringId)) return std::nullopt;
        const std::optional<ActionId> action = actionFromStringIdIn(kActions, aliases, stringId);
        if (!action) return std::nullopt;   // unreachable: a retired spelling resolves by definition
        return toQString(actionStringId(*action));
    };

    // --- The shortcuts family ------------------------------------------------------------------
    // TWO KEYS CAN LAND ON ONE CURRENT PATH, and which one wins is a rule rather than an accident:
    //
    //   * The CURRENT spelling, when the file already has it, beats every retired one. Only a build
    //     that knew the new name can have written it, so it is the newer of the two.
    //   * Between two RETIRED spellings, the one whose alias row comes LATER wins. `ActionId.def`'s
    //     rule 2 appends alias rows, so a later row is a more recent rename -- and a more recent
    //     spelling was written by a more recent build.
    const QString prefix = toQString(ShortcutKey::pathPrefix());

    struct Candidate {
        std::size_t row = 0;
        QString     retiredPath;
        QVariant    value;
    };
    std::map<QString, Candidate> chosen;              // current path -> the retired key written there
    std::vector<std::pair<QString, QString>> retired; // retired path -> current path

    for (auto it = declared.constBegin(); it != declared.constEnd(); ++it) {
        if (!it.key().startsWith(prefix)) continue;
        const QStringList parts = it.key().mid(prefix.size()).split(QLatin1Char('/'));
        if (parts.size() != 2) continue;

        const std::string stringId = parts.at(1).toStdString();
        const std::optional<QString> spelling = currentSpelling(stringId);
        if (!spelling) continue;

        std::size_t row = 0;
        while (row < aliases.size() && aliases[row].oldStringId != stringId) ++row;

        const QString current = prefix + parts.at(0) + QLatin1Char('/') + *spelling;
        retired.emplace_back(it.key(), current);

        const auto existing = chosen.find(current);
        if (existing == chosen.end() || row > existing->second.row)
            chosen[current] = Candidate{row, it.key(), it.value()};
    }

    // Removed first, then written: a current path is never itself retired, so nothing inserted
    // below can be one of the keys removed here.
    for (const auto& [retiredPath, current] : retired) {
        declared.remove(retiredPath);
        removed_.push_back(retiredPath);
    }

    // Taken BEFORE anything is inserted, so "the file already had the current spelling" is not
    // confused with "an earlier row of this loop just wrote it".
    std::set<QString> alreadySet;
    for (const auto& [current, candidate] : chosen)
        if (declared.contains(current)) alreadySet.insert(current);

    for (const auto& [retiredPath, current] : retired) {
        if (alreadySet.count(current) != 0) {
            RAWPAGE_LOG_INFO(LogSubsystem::Ui)
                << "settings: dropping" << retiredPath << "-- it names a retired action id and"
                << current << "is already set";
            continue;
        }
        const Candidate& winner = chosen.at(current);
        if (winner.retiredPath != retiredPath) {
            RAWPAGE_LOG_INFO(LogSubsystem::Ui)
                << "settings: dropping" << retiredPath << "-- a more recent spelling,"
                << winner.retiredPath << ", is also in the file";
            continue;
        }
        declared.insert(current, winner.value);
        dirty_.push_back(current);
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "settings: renamed" << retiredPath << "to" << current << "-- the action was renamed";
    }

    // --- The Quick Access Toolbar --------------------------------------------------------------
    // Renamed in place, order kept. A list holding both spellings of one command becomes a list
    // holding the current one twice, and `UiCustomization::resolveCommands` already drops a
    // duplicate -- a second rule for the same shape here would be a second opinion about it.
    const QString qatPath = toQString(keys::ui::qatCommands.path);
    if (const auto it = declared.find(qatPath); it != declared.end()) {
        if (std::optional<QStringList> commands = SettingsCodec<QStringList>::fromVariant(it.value())) {
            bool changed = false;
            for (QString& command : *commands) {
                if (const std::optional<QString> spelling = currentSpelling(command.toStdString())) {
                    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
                        << "settings: quick access toolbar command" << command << "renamed to"
                        << *spelling;
                    command = *spelling;
                    changed = true;
                }
            }
            if (changed) {
                it.value() = SettingsCodec<QStringList>::toVariant(*commands);
                dirty_.push_back(qatPath);
            }
        }
    }
}

void Settings::setVariant(const QString& path, SettingsGroup group, const QVariant& value)
{
    const auto it = values_.find(path);

    // A-42: an actual change only. Compared as the ENCODED variant, which is what would be written
    // -- comparing decoded values would make `1.0` and `1.00` a change, and comparing raw input
    // would make a clamped write look like a change every time.
    if (it != values_.end() && settingsVariantText(it->second) == settingsVariantText(value)) return;

    if (it == values_.end()) {
        values_.emplace(path, value);
    } else {
        it->second = value;
    }

    dirty_.push_back(path);
    autoFlush();
    notify(Change{path, group});
}

void Settings::autoFlush()
{
    // WRITTEN WHEN CHANGED, NOT AT EXIT (issue I-47). Until 2026-08-31 `sync()` was called from
    // exactly one place -- `app/CompositionRoot`'s teardown -- so every preference a person changed
    // and watched take effect was still only in this process's memory. A crash, a kill, or a power
    // cut lost the lot, and the Preferences dialog makes that worse rather than better: it applies
    // immediately and has no OK button, so there is no moment at which a user would think "now it is
    // saved". Guide case M-11 says settings are written when changed and had said so since it was
    // written; the code said otherwise, and the case had never been walked.
    //
    // The cost is one small INI write per changed key. Measured against the alternative: a debounce
    // timer would put a QTimer in `core/`, which may not have one, and would reintroduce exactly the
    // window this closes.
    if (const Result<void> written = sync(); !written) {
        // Logged, not returned: `set()` is a void, and a caller that could not have prevented the
        // failure cannot act on it either. `writesSuppressed()` is the question a UI asks instead.
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "settings: could not write" << filePath_ << "--" << written.error().messageKey;
    }
}

void Settings::notify(const Change& change)
{
    // Copied before iterating: a handler that unsubscribes itself -- which a Preferences page does
    // when it closes -- would otherwise invalidate the iterator mid-notification.
    const std::map<int, ChangeHandler> handlers = handlers_;
    for (const auto& [token, handler] : handlers) {
        if (handler) handler(change);
    }
}

int Settings::subscribe(ChangeHandler handler)
{
    const int token = nextToken_++;
    handlers_.emplace(token, std::move(handler));
    return token;
}

void Settings::unsubscribe(int token)
{
    handlers_.erase(token);
}

void Settings::logClamp(std::string_view path, bool hasRange) const
{
    if (!hasRange) return;
    RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
        << "settings:" << toQString(path)
        << "was outside its declared range and has been clamped -- the value in the file is"
        << "unchanged until the next write";
}

// ---------------------------------------------------------------------------------------
// The shortcuts family
// ---------------------------------------------------------------------------------------
std::optional<QString> Settings::shortcutOverride(const ShortcutKey& key) const
{
    const auto it = values_.find(key.path());
    if (it == values_.end()) return std::nullopt;
    const QString text = settingsVariantText(it->second);
    return text.isEmpty() ? std::nullopt : std::optional<QString>(text);
}

void Settings::setShortcutOverride(const ShortcutKey& key, const QString& chord)
{
    setVariant(key.path(), ShortcutKey::group(), QVariant(chord));
}

void Settings::clearShortcutOverride(const ShortcutKey& key)
{
    const QString path = key.path();
    const auto it = values_.find(path);
    if (it == values_.end()) return;

    values_.erase(it);
    removed_.push_back(path);
    autoFlush();
    notify(Change{path, ShortcutKey::group()});
}

std::vector<KeyBindingStore::OverrideEntry> Settings::shortcutOverrides() const
{
    const QString prefix = toQString(ShortcutKey::pathPrefix());

    std::vector<KeyBindingStore::OverrideEntry> entries;
    for (const auto& [path, value] : values_) {
        if (!path.startsWith(prefix)) continue;

        const QStringList parts = path.mid(prefix.size()).split(QLatin1Char('/'));
        if (parts.size() != 2) continue;

        // Resolvable by construction: loadFromFile() dropped everything that was not, and
        // setShortcutOverride only accepts a ShortcutKey. Re-derived rather than cached because
        // the context name is what the FILE holds, and this is the one place it turns back into
        // the enum.
        const std::string contextName = parts.at(0).toStdString();
        // Through core/cmd/KeyContext.h's own inverse rather than a loop written here. This file
        // carried that loop until T11, when `ui/shell/UiCustomization` needed the same conversion
        // and the choice was between two copies of a context-name table and none.
        const std::optional<KeyContext> context = keyContextFromName(contextName);
        if (!context) continue;

        KeyBindingStore::OverrideEntry entry;
        entry.context = *context;
        entry.actionStringId = parts.at(1).toStdString();
        entry.chord = settingsVariantText(value).toStdString();
        entries.push_back(std::move(entry));
    }
    return entries;
}

// ---------------------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------------------
Result<void> Settings::sync()
{
    if (writesSuppressed_) {
        // Not an error: this is the D1-16 downgrade rule working. A caller that needs to tell the
        // user asks `writesSuppressed()` once, rather than every `sync()` returning a failure that
        // every call site has to special-case into silence.
        RAWPAGE_LOG_DEBUG(LogSubsystem::Ui)
            << "settings: sync suppressed -- the file on disk was written by a newer build";
        return {};
    }

    if (dirty_.empty() && removed_.empty()) return {};

    // The directory may not exist on a first run. core/settings/ is the one part of core/ permitted
    // to write, which is why this is legal here and nowhere else in the layer.
    const QDir parent = QFileInfo(filePath_).dir();
    // No waiver marker here on purpose: `core/settings/` already carries the filesystem-write
    // privilege as its own layer entry in tools/layer-lint/rules.json, so a waiver would waive
    // nothing and would still inflate the --explain census that I-20 exists to keep honest.
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) {
        return Error{ErrorCode::IoFailure, QLatin1String("error.settings.directory"), parent.path()};
    }

    for (const QString& path : removed_) backing_->remove(toFileKey(path));
    removed_.clear();

    for (const QString& path : dirty_) {
        if (path == kSchemaVersionPath) continue;
        const auto it = values_.find(path);
        if (it == values_.end()) continue;
        backing_->setValue(toFileKey(path), it->second);
    }
    dirty_.clear();

    // Always stamped, and stamped LAST, so a write interrupted halfway leaves a file whose version
    // still describes the values that are actually in it.
    backing_->setValue(kSchemaVersionPath, SettingsSchema::kCurrentSchemaVersion);
    fileSchemaVersion_ = SettingsSchema::kCurrentSchemaVersion;

    backing_->sync();
    if (backing_->status() != QSettings::NoError) {
        return Error{ErrorCode::IoFailure, QLatin1String("error.settings.write"), filePath_};
    }
    return {};
}

QStringList Settings::unknownKeys() const
{
    QStringList keys;
    for (const auto& [path, value] : unknown_) {
        (void)value;
        keys << path;
    }
    return keys;
}

} // namespace rawpage::core
