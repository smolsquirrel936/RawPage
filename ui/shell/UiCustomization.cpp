#include "ui/shell/UiCustomization.h"

#include "core/Log.h"
#include "core/settings/Settings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;

// The member names are part of the format, exactly as a settings path is. They are spelled once,
// here, and read by both directions -- a serializer and a parser that name their fields separately
// agree until somebody fixes a typo in one of them.
constexpr QLatin1String kVersion{"version"};
constexpr QLatin1String kQatCommands{"qatCommands"};
constexpr QLatin1String kQatBelowRibbon{"qatBelowRibbon"};
constexpr QLatin1String kShortcuts{"shortcuts"};
constexpr QLatin1String kContext{"context"};
constexpr QLatin1String kAction{"action"};
constexpr QLatin1String kChord{"chord"};

constexpr QLatin1String kBadBlobKey{"error.customization.malformed"};

// Every member this version understands. Anything else in the object is a NEWER build's field and
// is carried through untouched -- see the header on why that is the load-bearing half of D1-16.
bool isKnownMember(const QString& name)
{
    return name == kVersion || name == kQatCommands || name == kQatBelowRibbon
           || name == kShortcuts;
}

QString toQString(const std::string& text) { return QString::fromStdString(text); }

} // namespace

UiCustomizationData UiCustomization::fromSettings(const core::Settings& settings,
                                                  const core::KeyBindingStore& bindings)
{
    UiCustomizationData data;
    data.version        = kUiCustomizationVersion;
    data.qatCommands    = settings.get(core::keys::ui::qatCommands);
    data.qatBelowRibbon = settings.get(core::keys::ui::qatBelowRibbon);
    data.shortcuts      = bindings.exportOverrides();
    return data;
}

UiCustomization::ApplyReport UiCustomization::applyToSettings(const UiCustomizationData& data,
                                                              core::Settings& settings,
                                                              core::KeyBindingStore& bindings)
{
    ApplyReport report;
    report.migrated = data.version < kUiCustomizationVersion;

    // Resolve BEFORE writing. A blob naming three commands of which one has been retired without an
    // alias should install the two that resolved and say so -- not refuse the import, and not write
    // the unresolvable name back out where the next load would drop it again in silence.
    QStringList dropped;
    const std::vector<core::ActionId> commands = resolveCommands(data.qatCommands, &dropped);
    report.droppedCommands = static_cast<int>(dropped.size());
    for (const QString& name : dropped)
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui) << "ui customization: dropping unknown command" << name;

    settings.set(core::keys::ui::qatCommands, toStringIds(commands));
    settings.set(core::keys::ui::qatBelowRibbon, data.qatBelowRibbon);

    // The shortcut half REPLACES rather than merges. An import is "make my layout look like this
    // one", and a merge would leave in place a remap the exporting machine had deliberately removed
    // -- which is the one outcome the person deploying a house layout cannot inspect.
    bindings.clearAllOverrides();
    const std::size_t accepted = bindings.importOverrides(data.shortcuts);
    report.droppedShortcuts = static_cast<int>(data.shortcuts.size() - accepted);

    for (const core::KeyBindingStore::OverrideEntry& entry : bindings.exportOverrides()) {
        const std::optional<core::ActionId> action = core::actionFromStringId(entry.actionStringId);
        if (!action) continue;
        settings.setShortcutOverride(core::ShortcutKey::forAction(entry.context, *action),
                                     toQString(entry.chord));
    }

    return report;
}

QByteArray UiCustomization::serialize(const UiCustomizationData& data)
{
    // The unknown members go in FIRST, so that a known member always wins if a newer build ever
    // reuses one of our names for something else. Losing an extra we could not interpret is a much
    // smaller failure than writing a field whose meaning we got wrong.
    QJsonObject object = data.extras;

    object.insert(kVersion, kUiCustomizationVersion);
    object.insert(kQatCommands, QJsonArray::fromStringList(data.qatCommands));
    object.insert(kQatBelowRibbon, data.qatBelowRibbon);

    QJsonArray shortcuts;
    for (const core::KeyBindingStore::OverrideEntry& entry : data.shortcuts) {
        QJsonObject row;
        const std::string_view contextName = core::keyContextName(entry.context);
        row.insert(kContext,
                   QString::fromUtf8(contextName.data(), static_cast<qsizetype>(contextName.size())));
        row.insert(kAction, toQString(entry.actionStringId));
        row.insert(kChord, toQString(entry.chord));
        shortcuts.append(row);
    }
    object.insert(kShortcuts, shortcuts);

    return QJsonDocument(object).toJson(QJsonDocument::Indented);
}

core::Result<UiCustomizationData> UiCustomization::parse(const QByteArray& bytes)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return core::Error{core::ErrorCode::ParseFailure, kBadBlobKey, error.errorString()};

    const QJsonObject object = document.object();

    UiCustomizationData data;

    // A blob with no version is not a version-1 blob -- it is a file we have no reason to believe
    // is one of ours at all, and treating it as version 1 is how a JSON file of something else
    // entirely becomes "your customization, imported successfully, with everything reset".
    if (!object.value(kVersion).isDouble())
        return core::Error{core::ErrorCode::ParseFailure, kBadBlobKey,
                           QString::fromLatin1(kVersion.data(), kVersion.size())};
    data.version = object.value(kVersion).toInt();

    const QJsonArray commands = object.value(kQatCommands).toArray();
    for (const QJsonValue& value : commands)
        if (value.isString()) data.qatCommands.append(value.toString());

    data.qatBelowRibbon = object.value(kQatBelowRibbon).toBool(false);

    const QJsonArray shortcuts = object.value(kShortcuts).toArray();
    for (const QJsonValue& value : shortcuts) {
        const QJsonObject row = value.toObject();
        const std::optional<core::KeyContext> context =
            core::keyContextFromName(row.value(kContext).toString().toStdString());
        if (!context) continue;

        core::KeyBindingStore::OverrideEntry entry;
        entry.context        = *context;
        entry.actionStringId = row.value(kAction).toString().toStdString();
        entry.chord          = row.value(kChord).toString().toStdString();
        if (entry.actionStringId.empty() || entry.chord.empty()) continue;
        data.shortcuts.push_back(std::move(entry));
    }

    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        if (!isKnownMember(it.key())) data.extras.insert(it.key(), it.value());

    if (data.version > kUiCustomizationVersion) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "ui customization: file version" << data.version << "is newer than this build's"
            << kUiCustomizationVersion << "--" << data.extras.size()
            << "unknown member(s) preserved";
    }

    return data;
}

std::vector<core::ActionId> UiCustomization::resolveCommands(const QStringList& stringIds,
                                                             QStringList* dropped)
{
    std::vector<core::ActionId> commands;
    commands.reserve(static_cast<std::size_t>(stringIds.size()));

    for (const QString& stringId : stringIds) {
        const std::optional<core::ActionId> id = core::actionFromStringId(stringId.toStdString());
        if (!id) {
            if (dropped) dropped->append(stringId);
            continue;
        }

        // A duplicate is dropped rather than placed twice. Two buttons for one command that the
        // user cannot tell apart is a customization nobody asked for, and a hand-edited file is
        // exactly where one comes from.
        if (std::find(commands.begin(), commands.end(), *id) != commands.end()) continue;
        commands.push_back(*id);
    }
    return commands;
}

QStringList UiCustomization::toStringIds(const std::vector<core::ActionId>& commands)
{
    QStringList ids;
    ids.reserve(static_cast<qsizetype>(commands.size()));
    for (const core::ActionId id : commands) {
        const std::string_view stringId = core::actionStringId(id);
        ids.append(QString::fromUtf8(stringId.data(), static_cast<qsizetype>(stringId.size())));
    }
    return ids;
}

} // namespace rawpage::ui
