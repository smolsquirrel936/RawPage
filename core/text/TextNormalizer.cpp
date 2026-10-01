#include "core/text/TextNormalizer.h"

#include <QChar>
#include <QList>

#include <algorithm>

namespace rawpage::core::text {
namespace {

bool isMark(char32_t cp)
{
    const QChar::Category category = QChar::category(cp);
    return category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining
           || category == QChar::Mark_Enclosing;
}

bool isWhitespace(char32_t cp)
{
    return QChar::isSpace(cp);
}

} // namespace

std::pair<int, int> NormalizedText::sourceRange(qsizetype begin, qsizetype end) const
{
    int from = -1;
    int to = -1;
    begin = std::max<qsizetype>(0, begin);
    end = std::min<qsizetype>(end, static_cast<qsizetype>(first.size()));
    for (qsizetype i = begin; i < end; ++i) {
        const int f = first[static_cast<std::size_t>(i)];
        const int l = last[static_cast<std::size_t>(i)];
        if (f < 0) continue;
        from = from < 0 ? f : std::min(from, f);
        to = std::max(to, l);
    }
    return {from, to};
}

QString TextNormalizer::normalizeCluster(const QString& cluster) const
{
    QString out = cluster.normalized(QString::NormalizationForm_KC);
    if (options_.stripDiacritics) {
        const QString decomposed = out.normalized(QString::NormalizationForm_D);
        QString stripped;
        stripped.reserve(decomposed.size());
        for (const QChar c : decomposed)
            if (c.category() != QChar::Mark_NonSpacing) stripped.append(c);
        out = stripped.normalized(QString::NormalizationForm_C);
    }
    if (options_.foldCase) out = out.toCaseFolded();
    return out;
}

NormalizedText TextNormalizer::normalize(const PageTextModel& model,
                                         const std::vector<ReadingItem>& stream) const
{
    NormalizedText result;
    const auto& chars = model.chars();

    const auto emitSpace = [&] {
        if (result.text.isEmpty() || result.text.back() == QLatin1Char(' ')) return;
        result.text.append(QLatin1Char(' '));
        result.first.push_back(-1);
        result.last.push_back(-1);
    };

    // The cluster being collected, and where the last emitted cluster began in the output -- the
    // hyphen join needs to take it back out.
    QString cluster;
    int clusterFirst = -1;
    int clusterLast = -1;
    bool clusterIsHyphen = false;
    qsizetype lastClusterStart = -1;
    bool lastClusterWasHyphen = false;

    const auto flush = [&] {
        if (clusterFirst < 0) return;
        lastClusterStart = result.text.size();
        lastClusterWasHyphen = clusterIsHyphen;
        if (cluster.size() == 1 && cluster.at(0).unicode() < 0x80) {
            // ONE ASCII CHARACTER -- by far the commonest cluster -- needs none of the pipeline: NFKC
            // leaves ASCII alone, it has no mark to strip, and simple case folding of ASCII is lower-
            // casing. The same output as `normalizeCluster`, without its three strings per character,
            // which Stage 5 T6 measured as most of the first search of a 900-page file.
            QChar c = cluster.at(0);
            if (options_.foldCase && c.unicode() >= u'A' && c.unicode() <= u'Z')
                c = QChar(static_cast<char16_t>(c.unicode() + (u'a' - u'A')));
            if (c.isSpace()) {
                emitSpace();
            } else {
                result.text.append(c);
                result.first.push_back(clusterFirst);
                result.last.push_back(clusterLast);
            }
        } else {
            const QString normalized = normalizeCluster(cluster);
            for (const QChar c : normalized) {
                if (c.isSpace()) {
                    emitSpace();
                    continue;
                }
                result.text.append(c);
                result.first.push_back(clusterFirst);
                result.last.push_back(clusterLast);
            }
        }
        cluster.truncate(0);   // keeps its buffer for the next cluster
        clusterFirst = clusterLast = -1;
        clusterIsHyphen = false;
    };

    // What a LineBreak does depends on the character AFTER it, so its decision waits for that one.
    bool pendingLineBreak = false;

    for (const ReadingItem& item : stream) {
        if (item.kind != ReadingItem::Kind::Char) {
            flush();
            if (item.kind == ReadingItem::Kind::LineBreak) {
                if (pendingLineBreak) emitSpace();
                pendingLineBreak = true;
            } else {
                if (pendingLineBreak) pendingLineBreak = false;
                emitSpace();
            }
            continue;
        }
        if (item.charIndex < 0 || static_cast<std::size_t>(item.charIndex) >= chars.size()) continue;
        const TextChar& c = chars[static_cast<std::size_t>(item.charIndex)];

        if (pendingLineBreak) {
            pendingLineBreak = false;
            flush();
            const bool lower = !c.isUnmapped() && QChar::isLower(c.codepoint);
            if (lastClusterWasHyphen && lastClusterStart >= 0 && lower) {
                // JOINED: the hyphen goes, and so does the break.
                result.text.truncate(lastClusterStart);
                result.first.resize(static_cast<std::size_t>(lastClusterStart));
                result.last.resize(static_cast<std::size_t>(lastClusterStart));
            } else if (!lastClusterWasHyphen) {
                emitSpace();
            }
            // A hyphen before anything else stays, and no space is added: the compound's own hyphen.
            lastClusterWasHyphen = false;
        }

        if (c.isUnmapped()) {
            flush();
            emitSpace();   // a separator, and nothing a query could match
            lastClusterWasHyphen = false;
            continue;
        }
        if (isWhitespace(c.codepoint)) {
            flush();
            emitSpace();
            lastClusterWasHyphen = false;
            continue;
        }
        if (isMark(c.codepoint) && clusterFirst >= 0) {
            const char32_t scalar = c.codepoint;
            cluster.append(QString::fromUcs4(&scalar, 1));
            clusterLast = item.charIndex;
            continue;
        }
        flush();
        const char32_t scalar = c.codepoint;
        if (scalar < 0x80) {
            cluster.truncate(0);
            cluster.append(QChar(static_cast<char16_t>(scalar)));
        } else {
            cluster = QString::fromUcs4(&scalar, 1);
        }
        clusterFirst = clusterLast = item.charIndex;
        clusterIsHyphen = c.isHyphen();
    }
    flush();
    // A trailing synthesized space is dropped: it matches nothing and would widen a last hit.
    while (!result.text.isEmpty() && result.text.back() == QLatin1Char(' ') && result.first.back() < 0) {
        result.text.chop(1);
        result.first.pop_back();
        result.last.pop_back();
    }
    return result;
}

QString TextNormalizer::normalizeQuery(const QString& query) const
{
    QString out;
    QString cluster;
    const auto flush = [&] {
        if (cluster.isEmpty()) return;
        for (const QChar c : normalizeCluster(cluster)) {
            if (c.isSpace()) {
                if (!out.isEmpty() && out.back() != QLatin1Char(' ')) out.append(QLatin1Char(' '));
            } else {
                out.append(c);
            }
        }
        cluster.clear();
    };
    const QList<uint> scalars = query.toUcs4();
    for (const uint unit : scalars) {
        const char32_t cp = static_cast<char32_t>(unit);
        if (isWhitespace(cp)) {
            flush();
            if (!out.isEmpty() && out.back() != QLatin1Char(' ')) out.append(QLatin1Char(' '));
            continue;
        }
        if (isMark(cp) && !cluster.isEmpty()) {
            cluster.append(QString::fromUcs4(&cp, 1));
            continue;
        }
        flush();
        cluster = QString::fromUcs4(&cp, 1);
    }
    flush();
    while (out.endsWith(QLatin1Char(' '))) out.chop(1);
    return out;
}

} // namespace rawpage::core::text
