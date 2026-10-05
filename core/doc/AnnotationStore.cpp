// core/doc/AnnotationStore -- implementation. Stage 6 T2. See the header for the two views and why
// a broken thread is shown rather than hidden.

#include "core/doc/AnnotationStore.h"

#include <QCryptographicHash>
#include <QJsonDocument>

#include <algorithm>
#include <limits>
#include <utility>

namespace rawpage::core {
namespace {

const std::vector<AnnotationListingEntry> kNoListing;

// The instant a state reply or a comment is ordered by: its /M, else its /CreationDate. An undated
// one sorts first, then document order decides.
std::int64_t when(const Annotation& annotation)
{
    if (annotation.modified.utcSeconds) return *annotation.modified.utcSeconds;
    if (annotation.created.utcSeconds) return *annotation.created.utcSeconds;
    return std::numeric_limits<std::int64_t>::min();
}

bool containsCaseless(const QString& haystack, const QString& needle)
{
    return haystack.contains(needle, Qt::CaseInsensitive);
}

} // namespace

const char* annotationNoteName(AnnotationNoteKind kind) noexcept
{
    switch (kind) {
    case AnnotationNoteKind::AnnotsNotArray: return "AnnotsNotArray";
    case AnnotationNoteKind::EntryNotDictionary: return "EntryNotDictionary";
    case AnnotationNoteKind::NonFiniteRect: return "NonFiniteRect";
    case AnnotationNoteKind::GeometryTruncated: return "GeometryTruncated";
    case AnnotationNoteKind::ListedByManyPages: return "ListedByManyPages";
    case AnnotationNoteKind::Unlisted: return "Unlisted";
    case AnnotationNoteKind::MissingParent: return "MissingParent";
    case AnnotationNoteKind::ReplyCycle: return "ReplyCycle";
    case AnnotationNoteKind::ReplyOnOtherPage: return "ReplyOnOtherPage";
    }
    return "?";
}

AnnotationStore::AnnotationStore()
    : refusal_(Error{ErrorCode::PermissionDenied, QLatin1String("error.edit.notAssessed"), QString()})
{
}

AnnotationStore AnnotationStore::fromReading(Reading reading)
{
    AnnotationStore store;
    store.refusal_.reset();
    store.annotations_ = std::move(reading.annotations);
    store.pages_ = std::move(reading.pages);
    store.readingNotes_ = std::move(reading.notes);
    store.reindex();
    return store;
}

void AnnotationStore::reindex()
{
    byObject_.clear();
    for (std::size_t index = 0; index < annotations_.size(); ++index) {
        const AnnotationId& id = annotations_[index].id;
        if (id.kind == AnnotationId::Kind::Object) byObject_.emplace(id.object, index);
    }
    // The thread notes are recomputed with the threads; the reading's are what the file held at open.
    notes_ = readingNotes_;
    buildThreads();
}

AnnotationStore AnnotationStore::refused(Error why)
{
    AnnotationStore store;
    store.refusal_ = std::move(why);
    return store;
}

Status AnnotationStore::status() const
{
    if (refusal_) return *refusal_;
    return Status{};
}

std::optional<std::size_t> AnnotationStore::find(const AnnotationId& id) const
{
    if (id.kind == AnnotationId::Kind::Object) return findObject(id.object);
    for (std::size_t index = 0; index < annotations_.size(); ++index) {
        if (annotations_[index].id == id) return index;
    }
    return std::nullopt;
}

std::optional<std::size_t> AnnotationStore::findObject(long long number) const
{
    const auto found = byObject_.find(number);
    if (found == byObject_.end()) return std::nullopt;
    return found->second;
}

const std::vector<AnnotationListingEntry>& AnnotationStore::listing(int page) const
{
    if (page < 0 || page >= pageCount()) return kNoListing;
    return pages_[static_cast<std::size_t>(page)];
}

std::vector<std::size_t> AnnotationStore::onPage(int page) const
{
    std::vector<std::size_t> out;
    for (const AnnotationListingEntry& entry : listing(page)) {
        if (entry.annotation && annotations_[*entry.annotation].page == page
            && std::find(out.begin(), out.end(), *entry.annotation) == out.end()) {
            out.push_back(*entry.annotation);
        }
    }
    return out;
}

std::vector<std::vector<AnnotationFact>> AnnotationStore::listingFacts() const
{
    std::vector<std::vector<AnnotationFact>> out(pages_.size());
    for (std::size_t page = 0; page < pages_.size(); ++page) {
        for (const AnnotationListingEntry& entry : pages_[page]) out[page].push_back(entry.fact);
    }
    return out;
}

// --- Threads ----------------------------------------------------------------------------------

void AnnotationStore::buildThreads()
{
    const std::size_t count = annotations_.size();
    parent_.assign(count, std::nullopt);
    children_.assign(count, {});

    for (std::size_t index = 0; index < count; ++index) {
        const Annotation& reply = annotations_[index];
        if (!reply.inReplyTo) continue;
        const long long object =
            reply.id.kind == AnnotationId::Kind::Object ? reply.id.object : -1;
        const std::optional<std::size_t> target = find(*reply.inReplyTo);
        if (!target || *target == index) {
            notes_.push_back({AnnotationNoteKind::MissingParent, reply.page, object});
            continue;
        }
        if (annotations_[*target].page != reply.page) {
            notes_.push_back({AnnotationNoteKind::ReplyOnOtherPage, reply.page, object});
            continue;
        }
        parent_[index] = *target;
    }

    // Cycles, iteratively: walk each unvisited node up its parent chain, marking the path. Meeting a
    // node still on the CURRENT path is a cycle; every member of it loses its parent and is reported.
    enum : unsigned char { Unseen, OnPath, Done };
    std::vector<unsigned char> state(count, Unseen);
    std::vector<std::size_t> path;
    for (std::size_t start = 0; start < count; ++start) {
        if (state[start] != Unseen) continue;
        path.clear();
        std::optional<std::size_t> at = start;
        while (at && state[*at] == Unseen) {
            state[*at] = OnPath;
            path.push_back(*at);
            at = parent_[*at];
        }
        if (at && state[*at] == OnPath) {
            const auto first = std::find(path.begin(), path.end(), *at);
            for (auto member = first; member != path.end(); ++member) {
                parent_[*member].reset();
                const Annotation& a = annotations_[*member];
                notes_.push_back({AnnotationNoteKind::ReplyCycle, a.page,
                                  a.id.kind == AnnotationId::Kind::Object ? a.id.object : -1});
            }
        }
        for (const std::size_t node : path) state[node] = Done;
    }

    for (std::size_t index = 0; index < count; ++index) {
        if (parent_[index]) children_[*parent_[index]].push_back(index);
    }
    for (std::vector<std::size_t>& list : children_) {
        std::stable_sort(list.begin(), list.end(), [this](std::size_t a, std::size_t b) {
            return when(annotations_[a]) < when(annotations_[b]);
        });
    }
}

std::optional<std::size_t> AnnotationStore::parentOf(std::size_t index) const
{
    if (index >= parent_.size()) return std::nullopt;
    return parent_[index];
}

std::vector<std::size_t> AnnotationStore::replies(std::size_t index) const
{
    std::vector<std::size_t> out;
    if (index >= children_.size()) return out;
    for (const std::size_t child : children_[index]) {
        const Annotation& a = annotations_[child];
        if (a.replyType == ReplyType::Reply && !a.isStateReply()) out.push_back(child);
    }
    return out;
}

std::vector<std::size_t> AnnotationStore::groupMembers(std::size_t index) const
{
    std::vector<std::size_t> out;
    if (index >= children_.size()) return out;
    for (const std::size_t child : children_[index]) {
        if (annotations_[child].replyType == ReplyType::Group) out.push_back(child);
    }
    return out;
}

bool AnnotationStore::isTopLevel(std::size_t index) const
{
    if (index >= annotations_.size()) return false;
    const Annotation& a = annotations_[index];
    if (a.subtype == AnnotationSubtype::Popup || a.isStateReply()) return false;
    return !parent_[index].has_value();
}

std::vector<std::size_t> AnnotationStore::topLevel() const
{
    std::vector<std::size_t> out;
    for (std::size_t index = 0; index < annotations_.size(); ++index) {
        if (isTopLevel(index)) out.push_back(index);
    }
    return out;
}

std::vector<AnnotationStateChange> AnnotationStore::stateHistory(std::size_t index,
                                                                 const QString& model) const
{
    std::vector<AnnotationStateChange> out;
    if (index >= children_.size()) return out;
    for (const std::size_t child : children_[index]) {
        const Annotation& a = annotations_[child];
        if (a.stateModel != model) continue;
        out.push_back({child, a.author, a.state, a.modified.utcSeconds ? a.modified : a.created});
    }
    return out;   // children_ is already oldest first
}

std::vector<AnnotationStateChange> AnnotationStore::reviewHistory(std::size_t index) const
{
    return stateHistory(index, QStringLiteral("Review"));
}

std::optional<AnnotationStateChange> AnnotationStore::currentReview(std::size_t index) const
{
    const std::vector<AnnotationStateChange> history = reviewHistory(index);
    if (history.empty()) return std::nullopt;
    return history.back();
}

std::vector<AnnotationStateChange> AnnotationStore::markedHistory(std::size_t index) const
{
    return stateHistory(index, QStringLiteral("Marked"));
}

bool AnnotationStore::isMarkedBy(std::size_t index, const QString& author) const
{
    std::optional<QString> latest;
    for (const AnnotationStateChange& change : markedHistory(index)) {
        if (change.author == author) latest = change.state;
    }
    return latest && *latest == QLatin1String("Marked");
}

// --- Queries ----------------------------------------------------------------------------------

std::vector<std::size_t> AnnotationStore::query(const Filter& filter, SortKey key) const
{
    std::vector<std::size_t> out;
    for (const std::size_t index : topLevel()) {
        const Annotation& a = annotations_[index];
        if (!filter.pages.empty()
            && std::find(filter.pages.begin(), filter.pages.end(), a.page) == filter.pages.end())
            continue;
        if (!filter.authors.empty()
            && std::find(filter.authors.begin(), filter.authors.end(), a.author) == filter.authors.end())
            continue;
        if (!filter.subtypes.empty()
            && std::find(filter.subtypes.begin(), filter.subtypes.end(), a.subtype) == filter.subtypes.end())
            continue;
        if (!filter.text.isEmpty() && !containsCaseless(a.contents, filter.text)
            && !containsCaseless(a.subject, filter.text) && !containsCaseless(a.author, filter.text))
            continue;
        if (!filter.includeHidden
            && (a.hasFlag(AnnotationFlag::Hidden) || a.hasFlag(AnnotationFlag::NoView)))
            continue;
        out.push_back(index);
    }

    // `out` is in document order already (the reading appends page by page, entry by entry), so a
    // STABLE sort by the key leaves document order as the tie-break.
    auto by = [this](auto project) {
        return [this, project](std::size_t a, std::size_t b) {
            return project(annotations_[a]) < project(annotations_[b]);
        };
    };
    switch (key) {
    case SortKey::Page:
        std::stable_sort(out.begin(), out.end(), by([](const Annotation& a) { return a.page; }));
        break;
    case SortKey::Date:
        std::stable_sort(out.begin(), out.end(), by([](const Annotation& a) { return when(a); }));
        break;
    case SortKey::Author:
        std::stable_sort(out.begin(), out.end(), [this](std::size_t a, std::size_t b) {
            return QString::compare(annotations_[a].author, annotations_[b].author,
                                    Qt::CaseInsensitive) < 0;
        });
        break;
    case SortKey::Subtype:
        std::stable_sort(out.begin(), out.end(), by([](const Annotation& a) {
            return static_cast<int>(a.subtype);
        }));
        break;
    }
    return out;
}

std::vector<QString> AnnotationStore::authors() const
{
    std::vector<QString> out;
    for (const std::size_t index : topLevel()) {
        const QString& author = annotations_[index].author;
        if (!author.isEmpty() && std::find(out.begin(), out.end(), author) == out.end())
            out.push_back(author);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// --- Changes (Stage 6 T3) ---------------------------------------------------------------------

namespace {

AnnotationFact factOf(const Annotation& annotation)
{
    AnnotationFact fact;
    fact.dictionary = true;
    fact.subtype = annotation.subtype;
    if (annotation.rectFinite) fact.rect = annotation.rect;
    return fact;
}

} // namespace

std::vector<AnnotationPlacement> AnnotationStore::placementsOf(const AnnotationId& id) const
{
    std::vector<AnnotationPlacement> out;
    const std::optional<std::size_t> index = find(id);
    if (!index) return out;
    for (std::size_t page = 0; page < pages_.size(); ++page) {
        const std::vector<AnnotationListingEntry>& listing = pages_[page];
        for (std::size_t position = 0; position < listing.size(); ++position) {
            if (listing[position].annotation == index)
                out.push_back(AnnotationPlacement{static_cast<int>(page), static_cast<int>(position)});
        }
    }
    return out;
}

Status AnnotationStore::insert(const std::vector<std::pair<AnnotationPlacement, Annotation>>& entries)
{
    if (refusal_) return *refusal_;
    // Validated in full against a simulation of the listing sizes before anything changes.
    std::map<int, std::size_t> sizes;
    for (const auto& [at, annotation] : entries) {
        if (at.page < 0 || at.page >= pageCount() || at.position < 0 || !annotation.id.isValid())
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.command.malformed"),
                         QStringLiteral("placement %1:%2").arg(at.page).arg(at.position)};
        auto size = sizes.find(at.page);
        if (size == sizes.end()) size = sizes.emplace(at.page, pages_[static_cast<std::size_t>(at.page)].size()).first;
        if (static_cast<std::size_t>(at.position) > size->second)
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.command.malformed"),
                         QStringLiteral("placement %1:%2").arg(at.page).arg(at.position)};
        ++size->second;
    }

    for (const auto& [at, annotation] : entries) {
        std::optional<std::size_t> index = find(annotation.id);
        if (!index) {
            index = annotations_.size();
            digests_.resize(annotations_.size());   // a digest moves with its annotation; a new one has none
            annotations_.push_back(annotation);
            digests_.emplace_back();
            if (annotation.id.kind == AnnotationId::Kind::Object) byObject_.emplace(annotation.id.object, *index);
        }
        AnnotationListingEntry entry;
        entry.fact = factOf(annotations_[*index]);
        entry.annotation = *index;
        std::vector<AnnotationListingEntry>& listing = pages_[static_cast<std::size_t>(at.page)];
        listing.insert(listing.begin() + at.position, entry);
    }
    reindex();
    return Status{};
}

Status AnnotationStore::remove(const std::vector<AnnotationPlacement>& placements)
{
    if (refusal_) return *refusal_;
    // On copies of the touched listings, so a refusal part-way leaves the store as it was.
    std::map<int, std::vector<AnnotationListingEntry>> changed;
    for (const AnnotationPlacement& at : placements) {
        if (at.page < 0 || at.page >= pageCount())
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.command.malformed"),
                         QStringLiteral("placement %1:%2").arg(at.page).arg(at.position)};
        auto listing = changed.find(at.page);
        if (listing == changed.end()) listing = changed.emplace(at.page, pages_[static_cast<std::size_t>(at.page)]).first;
        if (at.position < 0 || static_cast<std::size_t>(at.position) >= listing->second.size()
            || !listing->second[static_cast<std::size_t>(at.position)].annotation) {
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.command.malformed"),
                         QStringLiteral("placement %1:%2").arg(at.page).arg(at.position)};
        }
        listing->second.erase(listing->second.begin() + at.position);
    }
    for (auto& [page, listing] : changed) pages_[static_cast<std::size_t>(page)] = std::move(listing);

    // An annotation no page lists any more leaves the store, and every index above it moves down.
    std::vector<bool> listed(annotations_.size(), false);
    for (const std::vector<AnnotationListingEntry>& listing : pages_) {
        for (const AnnotationListingEntry& entry : listing) {
            if (entry.annotation) listed[*entry.annotation] = true;
        }
    }
    std::vector<std::size_t> renumbered(annotations_.size(), 0);
    std::vector<Annotation> kept;
    std::vector<QByteArray> keptDigests;
    kept.reserve(annotations_.size());
    digests_.resize(annotations_.size());
    for (std::size_t index = 0; index < annotations_.size(); ++index) {
        renumbered[index] = kept.size();
        if (listed[index]) {
            kept.push_back(std::move(annotations_[index]));
            keptDigests.push_back(std::move(digests_[index]));
        }
    }
    annotations_ = std::move(kept);
    digests_ = std::move(keptDigests);
    for (std::vector<AnnotationListingEntry>& listing : pages_) {
        for (AnnotationListingEntry& entry : listing) {
            if (entry.annotation) entry.annotation = renumbered[*entry.annotation];
        }
    }
    reindex();
    return Status{};
}

Status AnnotationStore::replace(const std::vector<Annotation>& changed)
{
    if (refusal_) return *refusal_;
    const auto malformed = [](const Annotation& a) {
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.command.malformed"), a.id.toString()};
    };
    // Validated in full before anything changes.
    std::vector<std::size_t> indices;
    indices.reserve(changed.size());
    std::vector<bool> replaced(annotations_.size(), false);
    for (const Annotation& a : changed) {
        const std::optional<std::size_t> index = find(a.id);
        if (!index || replaced[*index]) return malformed(a);
        replaced[*index] = true;
        const Annotation& now = annotations_[*index];
        if (now.subtype != a.subtype || now.page != a.page || now.nm != a.nm) return malformed(a);
        indices.push_back(*index);
    }

    if (digests_.size() < annotations_.size()) digests_.resize(annotations_.size());
    for (std::size_t i = 0; i < changed.size(); ++i) {
        annotations_[indices[i]] = changed[i];
        digests_[indices[i]].clear();
    }
    for (std::vector<AnnotationListingEntry>& listing : pages_) {
        for (AnnotationListingEntry& entry : listing) {
            if (entry.annotation && replaced[*entry.annotation])
                entry.fact = factOf(annotations_[*entry.annotation]);
        }
    }
    reindex();
    return Status{};
}

const QByteArray& AnnotationStore::annotationDigest(std::size_t index) const
{
    if (digests_.size() < annotations_.size()) digests_.resize(annotations_.size());
    QByteArray& digest = digests_.at(index);
    if (digest.isEmpty()) {
        const QByteArray json = QJsonDocument(annotations_.at(index).toJson()).toJson(QJsonDocument::Compact);
        digest = QCryptographicHash::hash(json, QCryptographicHash::Sha256);
    }
    return digest;
}

} // namespace rawpage::core
