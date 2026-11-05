// core/doc/PageRanges -- implementation. Stage 7 T7. See the header for the syntax.

#include "core/doc/PageRanges.h"

#include <QStringList>

#include <optional>
#include <set>

namespace rawpage::core {
namespace {

Error badRange(const QString& piece)
{
    return Error{ErrorCode::InvalidArgument, QLatin1String("error.pages.badRange"), piece};
}

// A page number, 1 ... `pageCount`, or nothing.
std::optional<int> pageNumber(const QString& text, int pageCount)
{
    bool ok = false;
    const int value = text.toInt(&ok);
    if (!ok || value < 1 || value > pageCount) return std::nullopt;
    return value;
}

} // namespace

Result<std::vector<int>> parsePageRanges(const QString& text, int pageCount)
{
    if (pageCount <= 0) return badRange(text.trimmed());
    QString normalized = text;
    normalized.replace(QChar(0x2013), QLatin1Char('-'));   // an en dash, as a word processor types a range
    const QStringList pieces = normalized.split(QLatin1Char(','));
    std::set<int> pages;
    for (const QString& raw : pieces) {
        const QString piece = raw.simplified().remove(QLatin1Char(' '));
        if (piece.isEmpty()) return badRange(raw.trimmed());
        const qsizetype dash = piece.indexOf(QLatin1Char('-'));
        if (dash < 0) {
            const std::optional<int> page = pageNumber(piece, pageCount);
            if (!page) return badRange(piece);
            pages.insert(*page - 1);
            continue;
        }
        if (piece.indexOf(QLatin1Char('-'), dash + 1) >= 0) return badRange(piece);
        const QString left = piece.left(dash);
        const QString right = piece.mid(dash + 1);
        if (left.isEmpty() && right.isEmpty()) return badRange(piece);
        const std::optional<int> first = left.isEmpty() ? std::optional<int>(1) : pageNumber(left, pageCount);
        const std::optional<int> last = right.isEmpty() ? std::optional<int>(pageCount) : pageNumber(right, pageCount);
        if (!first || !last || *first > *last) return badRange(piece);
        for (int page = *first; page <= *last; ++page) pages.insert(page - 1);
    }
    return std::vector<int>(pages.begin(), pages.end());
}

QString formatPageRanges(const std::vector<int>& pages)
{
    QStringList runs;
    std::size_t at = 0;
    while (at < pages.size()) {
        std::size_t end = at;
        while (end + 1 < pages.size() && pages[end + 1] == pages[end] + 1) ++end;
        runs << (end == at ? QString::number(pages[at] + 1)
                           : QStringLiteral("%1-%2").arg(pages[at] + 1).arg(pages[end] + 1));
        at = end + 1;
    }
    return runs.join(QStringLiteral(", "));
}

} // namespace rawpage::core
