#include "core/settings/RecentFiles.h"

#include <QDir>

#include <algorithm>

namespace rawpage::core {
namespace {

// Compared -- and stored -- in one canonical spelling, because the same document reached through
// "C:/x/a.pdf", "C:\x\a.pdf" and a relative path is one entry in a recent list and three in a naive
// one. QDir::cleanPath normalizes the separators and collapses . and ..; case is deliberately NOT
// folded, since the path is also what gets DISPLAYED and a user's capitals are theirs.
QString canonical(const QString& path)
{
    return QDir::cleanPath(path);
}

void eraseValue(std::vector<QString>& list, const QString& value)
{
    list.erase(std::remove(list.begin(), list.end(), value), list.end());
}

bool contains(const std::vector<QString>& list, const QString& value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

} // namespace

RecentFiles::RecentFiles(Settings& settings) : settings_(settings)
{
    for (const QString& path : settings_.get(keys::documents::recentPinned)) {
        const QString clean = canonical(path);
        if (!clean.isEmpty() && !contains(pinned_, clean)) pinned_.push_back(clean);
    }

    for (const QString& path : settings_.get(keys::documents::recentList)) {
        const QString clean = canonical(path);
        if (clean.isEmpty()) continue;
        // A path that is both pinned and in the history is a file written by an older build, or by
        // a hand edit. The pin wins: it is the more deliberate of the two statements.
        if (contains(pinned_, clean) || contains(history_, clean)) continue;
        history_.push_back(clean);
    }

    evict();
}

void RecentFiles::note(const QString& path)
{
    const QString clean = canonical(path);
    if (clean.isEmpty()) return;

    if (contains(pinned_, clean)) {
        // Already pinned: it is already at the top and does not belong in the history. Noting it
        // is not an error and must not silently unpin it.
        return;
    }

    eraseValue(history_, clean);
    history_.insert(history_.begin(), clean);
    evict();
    persist();
}

Result<void> RecentFiles::pin(const QString& path)
{
    const QString clean = canonical(path);
    if (clean.isEmpty()) {
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.recent.emptyPath")};
    }
    if (contains(pinned_, clean)) return {};

    eraseValue(history_, clean);
    pinned_.push_back(clean);
    persist();
    return {};
}

void RecentFiles::unpin(const QString& path)
{
    const QString clean = canonical(path);
    if (!contains(pinned_, clean)) return;

    eraseValue(pinned_, clean);
    // Unpinning returns the entry to the history at the front rather than dropping it: the user
    // said "stop pinning this", not "forget I opened it".
    history_.insert(history_.begin(), clean);
    evict();
    persist();
}

bool RecentFiles::isPinned(const QString& path) const
{
    return contains(pinned_, canonical(path));
}

void RecentFiles::remove(const QString& path)
{
    const QString clean = canonical(path);
    eraseValue(pinned_, clean);
    eraseValue(history_, clean);
    persist();
}

void RecentFiles::clear()
{
    history_.clear();
    persist();
}

void RecentFiles::clearIncludingPins()
{
    history_.clear();
    pinned_.clear();
    persist();
}

std::vector<RecentFiles::Entry> RecentFiles::entries() const
{
    std::vector<Entry> all;
    all.reserve(pinned_.size() + history_.size());
    for (const QString& path : pinned_) all.push_back(Entry{path, true});
    for (const QString& path : history_) all.push_back(Entry{path, false});
    return all;
}

int RecentFiles::capacity() const
{
    return settings_.get(keys::documents::recentCount);
}

void RecentFiles::evict()
{
    const int limit = capacity();
    // 0 is a legitimate value -- "keep no history" is a privacy setting, not a degenerate one --
    // and pins are unaffected by it, which is the whole of the eviction rule in two lines.
    if (limit < 0) return;
    if (static_cast<int>(history_.size()) <= limit) return;
    history_.resize(static_cast<std::size_t>(limit));
}

void RecentFiles::persist()
{
    QStringList pinned;
    for (const QString& path : pinned_) pinned << path;
    QStringList history;
    for (const QString& path : history_) history << path;

    settings_.set(keys::documents::recentPinned, pinned);
    settings_.set(keys::documents::recentList, history);
}

} // namespace rawpage::core
