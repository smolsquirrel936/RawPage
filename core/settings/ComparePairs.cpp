#include "core/settings/ComparePairs.h"

#include <QDir>
#include <QStringList>

namespace rawpage::core {
namespace {

// The header's match: separators by hand, then `cleanPath`, then case folded.
QString matchKey(const QString& path)
{
    QString normalized = path.trimmed();
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(normalized).toCaseFolded();
}

} // namespace

QString ComparePairs::newerFor(const Settings& settings, const QString& older)
{
    const QString key = matchKey(older);
    if (key.isEmpty()) return QString();
    const QStringList pairs = settings.get(keys::documents::recentComparisons);
    // An odd last entry -- a hand edit -- has no partner and is not read.
    for (qsizetype i = 0; i + 1 < pairs.size(); i += 2)
        if (matchKey(pairs.at(i)) == key) return pairs.at(i + 1);
    return QString();
}

void ComparePairs::note(Settings& settings, const QString& older, const QString& newer)
{
    const QString olderKey = matchKey(older);
    const QString newerKey = matchKey(newer);
    if (olderKey.isEmpty() || newerKey.isEmpty() || olderKey == newerKey) return;

    QStringList kept{older.trimmed(), newer.trimmed()};
    const QStringList pairs = settings.get(keys::documents::recentComparisons);
    for (qsizetype i = 0; i + 1 < pairs.size() && kept.size() < 2 * kCapacity; i += 2) {
        if (matchKey(pairs.at(i)) == olderKey) continue;
        kept << pairs.at(i) << pairs.at(i + 1);
    }
    settings.set(keys::documents::recentComparisons, kept);
}

} // namespace rawpage::core
