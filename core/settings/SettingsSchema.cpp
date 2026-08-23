#include "core/settings/SettingsSchema.h"

#include <QLatin1Char>

#include <algorithm>

namespace rawpage::core {

QString ShortcutKey::path() const
{
    const std::string_view context = keyContextName(context_);
    const std::string_view action = actionStringId(action_);
    return QString::fromUtf8(pathPrefix().data(), static_cast<qsizetype>(pathPrefix().size()))
        + QString::fromUtf8(context.data(), static_cast<qsizetype>(context.size()))
        + QLatin1Char('/')
        + QString::fromUtf8(action.data(), static_cast<qsizetype>(action.size()));
}

namespace SettingsSchema {
namespace {

// One row of the .def becomes one descriptor. Templated so that the QVariant for the default and
// the range comes from the SAME codec the reader and writer use -- a separate conversion here
// would be a second opinion about what `1.0` means in a file, and the two would eventually differ.
template <class T>
SettingsKeyInfo describe(const Key<T>& key)
{
    SettingsKeyInfo info;
    info.path = key.path;
    info.group = key.group;
    info.policyLockable = key.policyLockable;
    info.hasRange = key.hasRange;
    info.defaultValue = SettingsCodec<T>::toVariant(SettingsCodec<T>::fromDefault(key.defaultValue));
    if (key.hasRange) {
        info.minimum = SettingsCodec<T>::toVariant(SettingsCodec<T>::fromDefault(key.minimum));
        info.maximum = SettingsCodec<T>::toVariant(SettingsCodec<T>::fromDefault(key.maximum));
    }
    return info;
}

std::vector<SettingsKeyInfo> buildKeyTable()
{
    std::vector<SettingsKeyInfo> table;

#define RAWPAGE_SETTING(Namespace, Name, Type, Path, Default, Group, PolicyLockable) \
    table.push_back(describe(keys::Namespace::Name));
#define RAWPAGE_SETTING_RANGED(Namespace, Name, Type, Path, Default, Group, PolicyLockable, Min, Max) \
    table.push_back(describe(keys::Namespace::Name));
#include "core/settings/SettingsKeys.def"

    return table;
}

} // namespace

const std::vector<SettingsKeyInfo>& allKeys()
{
    static const std::vector<SettingsKeyInfo> table = buildKeyTable();
    return table;
}

const SettingsKeyInfo* findKey(const QString& path)
{
    const std::string utf8 = path.toStdString();
    for (const SettingsKeyInfo& info : allKeys()) {
        if (info.path == utf8) return &info;
    }
    return nullptr;
}

bool isDeclaredPath(const QString& path)
{
    if (findKey(path) != nullptr) return true;

    // The shortcuts family. Membership is decided by RESOLVING the two components, not by matching
    // the prefix: `shortcuts/Nowhere/file.open` and `shortcuts/Viewer/file.notAnAction` both look
    // like family members and are not, and the difference is what lets a stale key be dropped with
    // a named reason instead of being carried forward forever.
    const std::string_view prefix = ShortcutKey::pathPrefix();
    const QString qtPrefix = QString::fromUtf8(prefix.data(), static_cast<qsizetype>(prefix.size()));
    if (!path.startsWith(qtPrefix)) return false;

    const QStringList parts = path.mid(qtPrefix.size()).split(QLatin1Char('/'));
    if (parts.size() != 2) return false;

    const std::string contextName = parts.at(0).toStdString();
    bool contextKnown = false;
    for (std::size_t i = 0; i < kKeyContextCount; ++i) {
        if (keyContextName(static_cast<KeyContext>(i)) == contextName) { contextKnown = true; break; }
    }
    if (!contextKnown) return false;

    // Through actionFromStringId, so a RETIRED id still resolves via its alias row (D1-16). A user
    // who remapped a shortcut in Stage 7 keeps that remap after the id is renamed in Stage 9.
    return actionFromStringId(parts.at(1).toStdString()).has_value();
}

const std::vector<Migration>& migrations()
{
    // Empty at version 1: there is no version 0 to migrate from. The first key whose type or
    // meaning changes appends a row here IN THE SAME CHANGE as the change to the key, and bumps
    // kCurrentSchemaVersion. `tst_settings` exercises the chain against a synthetic migration, so
    // the machinery is tested before the first real one needs it.
    static const std::vector<Migration> chain;
    return chain;
}

int migrate(QVariantMap& values, int fromVersion, const std::vector<Migration>& chain, int targetVersion)
{
    if (fromVersion >= targetVersion) return fromVersion;

    // Sorted by fromVersion rather than trusted to be declared in order. The chain is appended to
    // by whoever bumps the version, and a row landing in the wrong place would apply a version-3
    // rewrite to a version-1 file -- a failure that reads as data corruption rather than as a
    // misplaced line.
    std::vector<const Migration*> ordered;
    ordered.reserve(chain.size());
    for (const Migration& step : chain) ordered.push_back(&step);
    std::sort(ordered.begin(), ordered.end(),
              [](const Migration* a, const Migration* b) { return a->fromVersion < b->fromVersion; });

    for (const Migration* step : ordered) {
        if (step->fromVersion < fromVersion || step->fromVersion >= targetVersion) continue;
        if (step->apply == nullptr) continue;
        step->apply(values);
        RAWPAGE_LOG_INFO(LogSubsystem::Ui)
            << "settings: migrated schema version" << step->fromVersion << "->"
            << (step->fromVersion + 1) << "--" << step->description;
    }

    return targetVersion;
}

} // namespace SettingsSchema
} // namespace rawpage::core
