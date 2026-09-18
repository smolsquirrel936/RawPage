#include "core/doc/Metadata.h"

#include <QRegularExpression>
#include <QTimeZone>

namespace rawpage::core {

bool Metadata::isEmpty() const noexcept
{
    return title.isEmpty() && author.isEmpty() && subject.isEmpty() && keywords.isEmpty()
           && creator.isEmpty() && producer.isEmpty() && creationDate.isEmpty()
           && modificationDate.isEmpty();
}

std::optional<QDateTime> Metadata::parseDate(const QString& stored)
{
    // ISO 32000-1 §7.9.4: D:YYYYMMDDHHmmSSOHH'mm', where everything after the year may be omitted
    // and O is +, - or Z. Written as one expression rather than as a chain of `mid()` calls because
    // the whole point is which parts are OPTIONAL, and a regular expression says that in one line
    // where index arithmetic says it in twenty.
    //
    // NOT `QDateTime::fromString`: its format strings have no notion of an optional tail, so a
    // producer that wrote only `D:2026` would parse as invalid and a caller would show nothing
    // where the file plainly says something.
    static const QRegularExpression pattern(
        QStringLiteral("^(?:D:)?(\\d{4})(\\d{2})?(\\d{2})?(\\d{2})?(\\d{2})?(\\d{2})?"
                       "(?:([Zz+\\-])(\\d{2})?'?(\\d{2})?'?)?$"));

    const QString trimmed = stored.trimmed();
    const QRegularExpressionMatch match = pattern.match(trimmed);
    if (!match.hasMatch()) return std::nullopt;

    const auto part = [&match](int index, int fallback) {
        const QString captured = match.captured(index);
        return captured.isEmpty() ? fallback : captured.toInt();
    };

    const QDate date(part(1, 0), part(2, 1), part(3, 1));
    if (!date.isValid()) return std::nullopt;
    const QTime time(part(4, 0), part(5, 0), part(6, 0));
    if (!time.isValid()) return std::nullopt;

    const QString sign = match.captured(7);
    if (sign.isEmpty()) {
        // No offset is not the same as UTC: the file did not say. Local time is what every reader
        // shows for such a date, and saying so here keeps the guess in one place.
        return QDateTime(date, time);
    }
    if (sign.compare(QStringLiteral("Z"), Qt::CaseInsensitive) == 0)
        return QDateTime(date, time, QTimeZone::utc());

    const int offsetSeconds = (part(8, 0) * 3600 + part(9, 0) * 60) * (sign == QLatin1String("-") ? -1 : 1);
    return QDateTime(date, time, QTimeZone::fromSecondsAheadOfUtc(offsetSeconds));
}

} // namespace rawpage::core
