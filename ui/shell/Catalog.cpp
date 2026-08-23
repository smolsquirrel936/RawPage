#include "ui/shell/Catalog.h"

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

    std::sort(lines.begin(), lines.end());
    return lines;
}

} // namespace rawpage::ui::catalog
