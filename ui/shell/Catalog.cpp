#include "ui/shell/Catalog.h"

#include "core/settings/SettingsSchema.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/shell/RibbonModel.h"

#include <QCoreApplication>

#include <algorithm>

namespace rawpage::ui::catalog {
namespace {

// The context is the key's own namespace -- everything before the first dot. See the header for
// why it is not the enclosing class name.
QByteArray contextOf(const QString& key)
{
    const qsizetype dot = key.indexOf(QLatin1Char('.'));
    if (dot <= 0) return {};
    return key.left(dot).toLatin1();
}

} // namespace

QString translate(const QString& key)
{
    const QByteArray context = contextOf(key);
    if (context.isEmpty()) {
        // A key with no namespace cannot be looked up, and returning it verbatim puts the mistake
        // on screen where it will be noticed on the first run rather than in a zh-TW screenshot
        // three stages later.
        return key;
    }
    return QCoreApplication::translate(context.constData(), key.toUtf8().constData());
}

QString translateAction(core::ActionId id)
{
    const std::string_view labelKey = core::actionLabelKey(id);
    return translate(QString::fromUtf8(labelKey.data(), static_cast<qsizetype>(labelKey.size())));
}

QString translateError(const QString& messageKey, const QString& context)
{
    const QString text = translate(messageKey);
    // `arg` on a string with no %1 returns it unchanged, which is exactly right: an error whose
    // catalog entry takes no argument is not a defect, and an error whose entry takes one and got
    // none renders the placeholder, which is visible.
    return context.isEmpty() ? text : text.arg(context);
}

QString settingsGroupKey(core::SettingsGroup group)
{
    // `PageDisplay` becomes `pageDisplay`; `Units & Guides` becomes `unitsGuides`. Lower-cased
    // first letter and no spaces or ampersands, which is the same camelCase every settings path
    // and every action string id already uses.
    const std::string_view name = core::settingsGroupName(group);
    QString text = QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
    text.remove(QLatin1Char(' '));
    text.remove(QLatin1Char('&'));
    if (!text.isEmpty()) text[0] = text.at(0).toLower();
    return QStringLiteral("prefs.group.") + text;
}

QString keyContextKey(core::KeyContext context)
{
    const std::string_view name = core::keyContextName(context);
    return QStringLiteral("prefs.context.")
           + QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

QStringList dumpStrings()
{
    QStringList lines;

    // Every shell key, from the one table the widgets also read.
#define RAWPAGE_SHELL_STRING(Enumerator, Key) \
    lines << QStringLiteral(Key "\t") + translate(QStringLiteral(Key));
#include "ui/shell/ShellKeys.def"

    // Every action label. All 358, not a sample: A-72's whole claim is that a catalog LOADED, and
    // a sample can be green while the half nobody sampled is missing.
    for (const core::ActionDescriptor& descriptor : core::kActions) {
        const QString key =
            QString::fromUtf8(descriptor.labelKey.data(), static_cast<qsizetype>(descriptor.labelKey.size()));
        lines << key + QLatin1Char('\t') + translate(key);
    }

    // Every ribbon tab and group title, read from the SHIPPED spec rather than from a second list.
    // A-72 compares an `en` run against a `--lang zh-TW` run, and the case is worth exactly as much
    // as the dump's coverage: 58 tab and group names that never appeared in it would keep the case
    // green while a fifth of the visible shell went unchecked. This is the same reasoning that put
    // the shell's own keys in a .def rather than at their call sites (T9), applied to keys whose
    // single source of truth happens to be a resource instead of a table.
    for (const QString& key : ribbon::shipped().spec.catalogKeys())
        lines << key + QLatin1Char('	') + translate(key);

    // T14's generated keys, from the same tables the pages read. Three families, none of which
    // appears whole in any source: one label per declared settings key, one label per member of a
    // closed choice set, one name per preference group and one per KeyContext. Without these the
    // dump would cover the shell and the ribbon and silently omit the entire Preferences surface,
    // and A-72 would keep passing over less.
    for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys()) {
        if (info.group == core::SettingsGroup::Internal) continue;   // no page, no label
        const QString key = QString::fromStdString(info.labelKey);
        lines << key + QLatin1Char('	') + translate(key);

        for (const core::SettingsChoice& choice : info.choices) {
            const QString choiceKey = QString::fromStdString(choice.labelKey);
            lines << choiceKey + QLatin1Char('	') + translate(choiceKey);
        }
    }

    // The groups that HAVE a page, from the dialog's own list. Dumping all 26 would emit 23 keys
    // no catalog has, and A-72's claim is that no key renders as itself.
    for (const core::SettingsGroup group : PreferencesDialog::shippedPages()) {
        const QString key = settingsGroupKey(group);
        lines << key + QLatin1Char('	') + translate(key);
    }

    for (std::size_t i = 0; i < core::kKeyContextCount; ++i) {
        const QString key = keyContextKey(static_cast<core::KeyContext>(i));
        lines << key + QLatin1Char('	') + translate(key);
    }

    // A choice label may be shared -- the theme names are the shell's own keys, so the same key
    // arrives from `ShellKeys.def` and from the schema. Sorted then deduplicated, because a
    // duplicate line would make A-72's counts disagree with the number of keys that exist.
    std::sort(lines.begin(), lines.end());
    lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
    return lines;
}

} // namespace rawpage::ui::catalog
