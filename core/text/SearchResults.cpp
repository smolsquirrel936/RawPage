#include "core/text/SearchResults.h"

#include <QChar>

#include <algorithm>
#include <tuple>

namespace rawpage::core::text {
namespace {

bool isSpace(QChar c)
{
    return c.isSpace();
}

// A character a snippet may show: a line break and a tab are a space there, and U+FFFD -- an unmapped
// glyph's mark in an attached PDF's text -- is a space too, never a character (the header).
QChar shown(QChar c)
{
    if (c == QChar(0xFFFD) || c.isSpace()) return QLatin1Char(' ');
    return c;
}

QString shownText(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) out.append(shown(c));
    return out;
}

int caseInsensitiveCompare(const QString& a, const QString& b)
{
    const int folded = QString::compare(a, b, Qt::CaseInsensitive);
    return folded != 0 ? folded : QString::compare(a, b, Qt::CaseSensitive);
}

// The CSV's quoting, RFC 4180: a field holding a comma, a quote, or a line break is quoted, and a quote
// inside it doubled.
QByteArray csvField(const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    const bool quote = utf8.contains(',') || utf8.contains('"') || utf8.contains('\n') || utf8.contains('\r');
    if (!quote) return utf8;
    QByteArray out = "\"";
    for (const char c : utf8) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

} // namespace

void SearchResults::add(FileResult file)
{
    files_.push_back(std::move(file));
}

void SearchResults::clear()
{
    files_.clear();
}

int SearchResults::hitCount() const
{
    int total = 0;
    for (const FileResult& file : files_) total += static_cast<int>(file.hits.size());
    return total;
}

int SearchResults::searchedCount() const
{
    return static_cast<int>(std::count_if(files_.begin(), files_.end(), [](const FileResult& f) { return f.wasSearched(); }));
}

int SearchResults::refusedCount() const
{
    return static_cast<int>(files_.size()) - searchedCount();
}

std::optional<QDate> SearchResults::dateOf(const Metadata& metadata)
{
    if (const std::optional<QDateTime> modified = Metadata::parseDate(metadata.modificationDate))
        return modified->date();
    if (const std::optional<QDateTime> created = Metadata::parseDate(metadata.creationDate))
        return created->date();
    return std::nullopt;
}

bool SearchResults::passes(const FileResult& file, const Filter& filter)
{
    if (!filter.author.isEmpty() && !file.metadata.author.contains(filter.author, Qt::CaseInsensitive)) return false;
    if (!filter.subject.isEmpty() && !file.metadata.subject.contains(filter.subject, Qt::CaseInsensitive)) return false;
    if (filter.from || filter.to) {
        const std::optional<QDate> date = dateOf(file.metadata);
        if (!date) return false;
        if (filter.from && *date < *filter.from) return false;
        if (filter.to && *date > *filter.to) return false;
    }
    return true;
}

std::vector<SearchResults::Row> SearchResults::rows(Sort sort, const Filter& filter) const
{
    std::vector<Row> out;
    for (const FileResult& file : files_) {
        if (!file.wasSearched() || !passes(file, filter)) continue;
        for (const ResultHit& hit : file.hits) out.push_back(Row{&file, &hit});
    }

    const auto byFileThenOrder = [](const Row& a, const Row& b) {
        if (a.file != b.file) return caseInsensitiveCompare(a.file->path, b.file->path) < 0;
        return a.hit->order < b.hit->order;
    };
    switch (sort) {
    case Sort::Relevance:
        // THE DEFINITION (the header): a file's hits, most first; ties by path; document order within.
        std::stable_sort(out.begin(), out.end(), [&](const Row& a, const Row& b) {
            const std::size_t countA = a.file->hits.size();
            const std::size_t countB = b.file->hits.size();
            if (countA != countB) return countA > countB;
            return byFileThenOrder(a, b);
        });
        break;
    case Sort::Page:
        std::stable_sort(out.begin(), out.end(), [&](const Row& a, const Row& b) {
            if (a.hit->hit.page != b.hit->hit.page) return a.hit->hit.page < b.hit->hit.page;
            return byFileThenOrder(a, b);
        });
        break;
    case Sort::File:
        std::stable_sort(out.begin(), out.end(), byFileThenOrder);
        break;
    }
    return out;
}

std::map<QString, std::set<int>> SearchResults::hitPages() const
{
    std::map<QString, std::set<int>> pages;
    for (const FileResult& file : files_)
        for (const ResultHit& hit : file.hits) pages[file.path].insert(hit.hit.page);
    return pages;
}

void SearchResults::keepOnly(const std::map<QString, std::set<int>>& pages)
{
    for (FileResult& file : files_) {
        const auto allowed = pages.find(file.path);
        file.hits.erase(std::remove_if(file.hits.begin(), file.hits.end(),
                                       [&](const ResultHit& hit) {
                                           return allowed == pages.end() || allowed->second.count(hit.hit.page) == 0;
                                       }),
                        file.hits.end());
    }
}

Snippet SearchResults::snippetOf(const PageReading& reading, Span span, int words)
{
    // THE PAGE AS TEXT, in reading order: a break between two positions is a space, an unmapped glyph is a
    // space (never text, D5-2), and each position's first UTF-16 unit is recorded.
    QString text;
    std::vector<int> startOf(static_cast<std::size_t>(reading.size()) + 1, 0);
    for (int p = 0; p < reading.size(); ++p) {
        if (p > 0 && reading.breakBefore(p) != ReadingItem::Kind::Char) text.append(QLatin1Char(' '));
        startOf[static_cast<std::size_t>(p)] = static_cast<int>(text.size());
        const TextChar& c = reading.charAt(p);
        if (c.isUnmapped()) {
            text.append(QLatin1Char(' '));
        } else {
            const char32_t cp = c.codepoint;
            text.append(QString::fromUcs4(&cp, 1));
        }
    }
    startOf[static_cast<std::size_t>(reading.size())] = static_cast<int>(text.size());
    const int begin = std::clamp(span.begin, 0, reading.size());
    const int end = std::clamp(span.end, begin, reading.size());
    // The hit ends where its last position's text ends, not where the next one's begins -- the break
    // between them is not part of it.
    int endOffset = startOf[static_cast<std::size_t>(begin)];
    if (end > begin) {
        const int last = end - 1;
        const char32_t cp = reading.charAt(last).isUnmapped() ? U' ' : reading.charAt(last).codepoint;
        endOffset = startOf[static_cast<std::size_t>(last)] + (cp > 0xFFFF ? 2 : 1);
    }
    return snippetOf(text, Span{startOf[static_cast<std::size_t>(begin)], endOffset}, words);
}

Snippet SearchResults::snippetOf(const QString& text, Span within, int words)
{
    const int size = static_cast<int>(text.size());
    const int begin = std::clamp(within.begin, 0, size);
    const int end = std::clamp(within.end, begin, size);

    // `words` WHOLE words before: over the space before each, then the word itself, so the cut is always
    // at a word's start.
    int from = begin;
    for (int counted = 0; counted < words && from > 0; ++counted) {
        while (from > 0 && isSpace(text.at(from - 1))) --from;
        while (from > 0 && !isSpace(text.at(from - 1))) --from;
    }
    // ... and after, cut at a word's end.
    int to = end;
    for (int counted = 0; counted < words && to < size; ++counted) {
        while (to < size && isSpace(text.at(to))) ++to;
        while (to < size && !isSpace(text.at(to))) ++to;
    }

    Snippet snippet;
    snippet.before = shownText(text.mid(from, begin - from));
    snippet.match = shownText(text.mid(begin, end - begin));
    snippet.after = shownText(text.mid(end, to - end));
    return snippet;
}

QByteArray SearchResults::toCsv(const std::vector<Row>& rows, const QStringList& headings)
{
    // THE BYTE-ORDER MARK FIRST: without it Excel reads UTF-8 as the machine's code page, and a CJK or a
    // Hebrew snippet arrives as mojibake (guide A-86).
    QByteArray out("\xEF\xBB\xBF");
    const auto line = [&out](const QStringList& fields) {
        for (qsizetype i = 0; i < fields.size(); ++i) {
            if (i > 0) out += ',';
            out += csvField(fields.at(i));
        }
        out += "\r\n";
    };
    line(headings);
    for (const Row& row : rows) {
        const int page = row.hit->hit.page;
        line({row.file->path, page >= 0 ? QString::number(page + 1) : QString(), row.hit->snippet.plain()});
    }
    return out;
}

} // namespace rawpage::core::text
