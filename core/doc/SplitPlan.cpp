// core/doc/SplitPlan -- implementation. Stage 7 T7. See the header for each rule.

#include "core/doc/SplitPlan.h"

#include "core/doc/Attachments.h"

#include <algorithm>
#include <optional>
#include <set>

namespace rawpage::core::split {
namespace {

Error noBoundary(const char* what)
{
    return Error{ErrorCode::NotFound, QLatin1String("error.split.noBoundary"), QLatin1String(what)};
}

Error badArgument(const char* key, qint64 value)
{
    return Error{ErrorCode::InvalidArgument, QLatin1String(key), QString::number(value)};
}

std::vector<int> run(int first, int last)
{
    std::vector<int> pages;
    pages.reserve(static_cast<std::size_t>(last - first + 1));
    for (int page = first; page <= last; ++page) pages.push_back(page);
    return pages;
}

void collect(const std::vector<OutlineItem>& items, int depth, int level, int pageCount, std::set<int>& into)
{
    if (depth > level) return;
    for (const OutlineItem& item : items) {
        const int page = item.destination.pageIndex;
        if (page >= 0 && page < pageCount) into.insert(page);
        collect(item.children, depth + 1, level, pageCount, into);
    }
}

} // namespace

Result<std::vector<SplitPart>> everyN(int pageCount, int n)
{
    if (pageCount <= 0) return badArgument("error.split.noPages", pageCount);
    if (n < 1) return badArgument("error.split.badCount", n);
    if (n >= pageCount) return noBoundary("everyN");
    std::vector<SplitPart> parts;
    for (int first = 0; first < pageCount; first += n) parts.push_back(SplitPart{run(first, std::min(pageCount, first + n) - 1), false});
    return parts;
}

std::vector<int> bookmarkStarts(const Outline& outline, int level, int pageCount)
{
    std::set<int> starts;
    if (level >= 1) collect(outline.roots(), 1, level, pageCount, starts);
    return std::vector<int>(starts.begin(), starts.end());
}

Result<std::vector<SplitPart>> atBookmarks(int pageCount, const Outline& outline, int level)
{
    if (level < 1) return badArgument("error.split.badLevel", level);
    const Result<std::vector<SplitPart>> parts = atStarts(pageCount, bookmarkStarts(outline, level, pageCount));
    if (!parts && parts.error().messageKey == QLatin1String("error.split.noBoundary")) return noBoundary("bookmarks");
    return parts;
}

Result<std::vector<SplitPart>> atStarts(int pageCount, const std::vector<int>& starts)
{
    if (pageCount <= 0) return badArgument("error.split.noPages", pageCount);
    std::set<int> cuts;
    for (const int start : starts) {
        if (start > 0 && start < pageCount) cuts.insert(start);
    }
    if (cuts.empty()) return noBoundary("starts");
    std::vector<SplitPart> parts;
    int first = 0;
    for (const int cut : cuts) {
        parts.push_back(SplitPart{run(first, cut - 1), false});
        first = cut;
    }
    parts.push_back(SplitPart{run(first, pageCount - 1), false});
    return parts;
}

Result<std::vector<SplitPart>> atBlankPages(const std::vector<bool>& blank)
{
    if (blank.empty()) return badArgument("error.split.noPages", 0);
    if (std::all_of(blank.begin(), blank.end(), [](bool b) { return b; }))
        return Error{ErrorCode::NotFound, QLatin1String("error.split.allBlank"), QString()};
    if (std::none_of(blank.begin(), blank.end(), [](bool b) { return b; })) return noBoundary("blank");
    std::vector<SplitPart> parts;
    SplitPart current;
    for (int page = 0; page < static_cast<int>(blank.size()); ++page) {
        if (blank[static_cast<std::size_t>(page)]) {
            if (!current.pages.empty()) parts.push_back(std::move(current));
            current = SplitPart{};
            continue;
        }
        current.pages.push_back(page);
    }
    if (!current.pages.empty()) parts.push_back(std::move(current));
    // Blank pages only at the ends: one file of what is between them -- there was nothing to split at.
    if (parts.size() < 2) return noBoundary("blank");
    return parts;
}

Result<std::vector<SplitPart>> bySize(int pageCount, qint64 maxBytes, const Weigher& weigh)
{
    if (pageCount <= 0) return badArgument("error.split.noPages", pageCount);
    if (maxBytes <= 0) return badArgument("error.split.badSize", maxBytes);
    if (!weigh) return Error{ErrorCode::InvalidArgument, QLatin1String("error.split.badSize"), QString()};
    // Whether pages first ... last fit, or why they could not be weighed.
    std::optional<Error> failure;
    const auto fits = [&](int first, int last) {
        const Result<qint64> weight = weigh(run(first, last));
        if (!weight) {
            failure = weight.error();
            return false;
        }
        return weight.value() <= maxBytes;
    };
    std::vector<SplitPart> parts;
    int first = 0;
    while (first < pageCount) {
        if (!fits(first, first)) {
            if (failure) return *failure;
            parts.push_back(SplitPart{{first}, true});
            ++first;
            continue;
        }
        // Doubling: `good` fits, `bad` (a page past the end, or a run that does not fit) does not.
        int good = first;
        int bad = pageCount;
        for (int length = 2; first + length - 1 < pageCount; length *= 2) {
            if (fits(first, first + length - 1)) {
                good = first + length - 1;
            } else {
                if (failure) return *failure;
                bad = first + length - 1;
                break;
            }
        }
        if (good == pageCount - 1) bad = pageCount;
        else if (bad == pageCount && fits(first, pageCount - 1)) good = pageCount - 1;
        if (failure) return *failure;
        // Halving between them.
        while (bad - good > 1 && good < pageCount - 1) {
            const int middle = good + (bad - good) / 2;
            if (fits(first, middle)) {
                good = middle;
            } else {
                if (failure) return *failure;
                bad = middle;
            }
        }
        parts.push_back(SplitPart{run(first, good), false});
        first = good + 1;
    }
    if (parts.size() < 2) return noBoundary("size");
    return parts;
}

std::vector<QString> numberedNames(const QString& pattern, const QString& stem, const std::vector<int>& numbers, int last)
{
    const int width = static_cast<int>(QString::number(std::max(1, last)).size());
    std::vector<QString> names;
    names.reserve(numbers.size());
    for (const int number : numbers)
        names.push_back(safeFileName(pattern.arg(stem, QStringLiteral("%1").arg(number, width, 10, QLatin1Char('0')))));
    return names;
}

} // namespace rawpage::core::split
