#include "core/doc/PageReconnect.h"

#include "core/doc/Document.h"

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>
#include <map>
#include <set>

namespace rawpage::core {

namespace {

Error badReconnection(const char* field)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"),
                 QStringLiteral("page.insert.reconnect.") + QLatin1String(field)};
}

// A whole number in [0, high], or -1.
int wholeIn(const QJsonValue& value, int high)
{
    if (!value.isDouble()) return -1;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number || number < 0.0 || number > static_cast<double>(high))
        return -1;
    return static_cast<int>(number);
}

// The largest page index and link ordinal a line may name: ISO 32000-1 Annex C.1's object limit, as every page command.
constexpr int kMaxIndex = 8'388'606;

bool isCarriedLink(const AnnotationId& id) noexcept
{
    return id.kind == AnnotationId::Kind::Object || id.kind == AnnotationId::Kind::Created;
}

std::optional<ReconnectKind> kindFromName(const QString& name)
{
    for (const ReconnectKind kind : {ReconnectKind::Bookmark, ReconnectKind::BookmarkAction,
                                     ReconnectKind::NamedDestination, ReconnectKind::Link}) {
        if (name == QLatin1String(reconnectKindName(kind))) return kind;
    }
    return std::nullopt;
}

} // namespace

const char* reconnectKindName(ReconnectKind kind) noexcept
{
    switch (kind) {
    case ReconnectKind::Bookmark:         return "bookmark";
    case ReconnectKind::BookmarkAction:   return "bookmarkAction";
    case ReconnectKind::NamedDestination: return "destination";
    case ReconnectKind::Link:             return "link";
    }
    return "bookmark";
}

bool goesNowhere(const OutlineAction& action) noexcept
{
    return action.kind == OutlineAction::Kind::None
           || (action.kind == OutlineAction::Kind::GoTo && !action.destination.isResolved());
}

CopiedPageReferences referencesOfPages(const Document& document, const std::vector<int>& pages)
{
    CopiedPageReferences out;
    std::map<int, int> ordinalOf;   // page index -> its place in the copy
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (document.pages().at(pages[i]) != nullptr) ordinalOf.emplace(pages[i], static_cast<int>(i));
    }
    if (ordinalOf.empty()) return out;
    const auto copied = [&ordinalOf](int page) {
        const auto found = ordinalOf.find(page);
        return found == ordinalOf.end() ? -1 : found->second;
    };

    // What names a copied page: bookmarks ...
    for (const OutlineItem* item : document.outline().preorder()) {
        if (const int ordinal = copied(item->destination.pageIndex); ordinal >= 0)
            out.incoming.push_back({ReconnectKind::Bookmark, item->id, {}, {}, ordinal});
        if (item->action.kind == OutlineAction::Kind::GoTo) {
            if (const int ordinal = copied(item->action.destination.pageIndex); ordinal >= 0)
                out.incoming.push_back({ReconnectKind::BookmarkAction, item->id, {}, {}, ordinal});
        }
    }
    // ... named destinations ...
    for (const NamedDestination& entry : document.destinations().all()) {
        if (const int ordinal = copied(entry.destination.pageIndex); ordinal >= 0)
            out.incoming.push_back({ReconnectKind::NamedDestination, {}, entry.name, {}, ordinal});
    }
    // ... and links on the pages that were NOT copied. A link on a copied page to a copied page the copier keeps.
    const AnnotationStore& store = document.annotations();
    for (const Annotation& a : store.all()) {
        if (a.subtype != AnnotationSubtype::Link || a.action.kind != OutlineAction::Kind::GoTo || !isCarriedLink(a.id))
            continue;
        if (copied(a.page) >= 0) continue;
        if (const int ordinal = copied(a.action.destination.pageIndex); ordinal >= 0)
            out.incoming.push_back({ReconnectKind::Link, {}, {}, a.id, ordinal});
    }

    // What each copied page's own links name outside the copy -- by serial, which survives until the paste.
    for (const auto& [page, ordinal] : ordinalOf) {
        int link = 0;
        for (const AnnotationListingEntry& entry : store.listing(page)) {
            if (!entry.annotation) continue;
            const Annotation& a = store.at(*entry.annotation);
            if (a.subtype != AnnotationSubtype::Link) continue;
            const int number = link++;
            if (a.action.kind != OutlineAction::Kind::GoTo || !a.action.destination.isResolved()) continue;
            const int target = a.action.destination.pageIndex;
            if (copied(target) >= 0) continue;
            const Page* named = document.pages().at(target);
            if (named == nullptr || named->serial() == 0) continue;
            out.outgoing.push_back({ordinal, number, named->serial(), a.action});
        }
    }
    if (out.incoming.size() + out.outgoing.size() > static_cast<std::size_t>(kMaxReconnections)) return {};
    return out;
}

PageReconnection reconnectionFor(const Document& document, const CopiedPageReferences& references, int at, int count)
{
    PageReconnection out;
    if (count <= 0 || at < 0 || at > document.pages().count()) return out;

    for (const CopiedPageReferences::Incoming& in : references.incoming) {
        if (in.ordinal < 0 || in.ordinal >= count) continue;
        bool nowhere = false;
        switch (in.kind) {
        case ReconnectKind::Bookmark:
            if (const OutlineItem* item = document.outline().find(in.item)) nowhere = !item->destination.isResolved();
            break;
        case ReconnectKind::BookmarkAction:
            if (const OutlineItem* item = document.outline().find(in.item))
                nowhere = item->action.kind == OutlineAction::Kind::GoTo && !item->action.destination.isResolved();
            break;
        case ReconnectKind::NamedDestination:
            if (const NamedDestination* entry = document.destinations().find(in.name))
                nowhere = !entry->destination.isResolved();
            break;
        case ReconnectKind::Link:
            if (const std::optional<std::size_t> index = document.annotations().find(in.link)) {
                const Annotation& a = document.annotations().at(*index);
                nowhere = a.subtype == AnnotationSubtype::Link && a.action.kind == OutlineAction::Kind::GoTo
                          && !a.action.destination.isResolved();
            }
            break;
        }
        if (nowhere) out.incoming.push_back({in.kind, in.item, in.name, in.link, at + in.ordinal});
    }

    for (const CopiedPageReferences::Outgoing& link : references.outgoing) {
        if (link.ordinal < 0 || link.ordinal >= count || link.link < 0) continue;
        const int target = document.pages().indexOfSerial(link.target);
        if (target < 0) continue;   // the page it named has gone since the copy
        OutlineAction action = link.action;
        action.kind = OutlineAction::Kind::GoTo;
        action.destination.pageIndex = target >= at ? target + count : target;
        out.outgoing.push_back({link.ordinal, link.link, std::move(action)});
    }
    return out;
}

// ---- the journal line ------------------------------------------------------------------------------------------

QJsonObject PageReconnection::toJson() const
{
    QJsonArray in;
    for (const Incoming& entry : incoming) {
        QJsonObject item{{QStringLiteral("kind"), QLatin1String(reconnectKindName(entry.kind))},
                         {QStringLiteral("page"), entry.page}};
        switch (entry.kind) {
        case ReconnectKind::Bookmark:
        case ReconnectKind::BookmarkAction:   item.insert(QStringLiteral("id"), entry.item.toString()); break;
        case ReconnectKind::NamedDestination: item.insert(QStringLiteral("name"), entry.name); break;
        case ReconnectKind::Link:             item.insert(QStringLiteral("id"), entry.link.toString()); break;
        }
        in.append(item);
    }
    QJsonArray outList;
    for (const Outgoing& entry : outgoing) {
        outList.append(QJsonObject{{QStringLiteral("page"), entry.ordinal},
                                   {QStringLiteral("link"), entry.link},
                                   {QStringLiteral("action"), entry.action.toJson()}});
    }
    QJsonObject json;
    if (!in.isEmpty()) json.insert(QStringLiteral("incoming"), in);
    if (!outList.isEmpty()) json.insert(QStringLiteral("outgoing"), outList);
    return json;
}

Result<PageReconnection> PageReconnection::fromJson(const QJsonObject& json)
{
    for (auto it = json.begin(); it != json.end(); ++it) {
        if (it.key() != QLatin1String("incoming") && it.key() != QLatin1String("outgoing")) return badReconnection("key");
    }
    PageReconnection out;
    const QJsonValue in = json.value(QStringLiteral("incoming"));
    const QJsonValue outgoingValue = json.value(QStringLiteral("outgoing"));
    if (!in.isUndefined() && !in.isArray()) return badReconnection("incoming");
    if (!outgoingValue.isUndefined() && !outgoingValue.isArray()) return badReconnection("outgoing");
    const QJsonArray inArray = in.toArray();
    const QJsonArray outArray = outgoingValue.toArray();
    if (inArray.isEmpty() && outArray.isEmpty()) return badReconnection("empty");
    if (inArray.size() + outArray.size() > kMaxReconnections) return badReconnection("size");

    for (const QJsonValue value : inArray) {
        if (!value.isObject()) return badReconnection("incoming");
        const QJsonObject item = value.toObject();
        const QJsonValue kindValue = item.value(QStringLiteral("kind"));
        const std::optional<ReconnectKind> kind = kindValue.isString() ? kindFromName(kindValue.toString()) : std::nullopt;
        if (!kind) return badReconnection("kind");
        Incoming entry;
        entry.kind = *kind;
        entry.page = wholeIn(item.value(QStringLiteral("page")), kMaxIndex);
        if (entry.page < 0) return badReconnection("page");
        const QJsonValue id = item.value(QStringLiteral("id"));
        const QJsonValue name = item.value(QStringLiteral("name"));
        switch (entry.kind) {
        case ReconnectKind::Bookmark:
        case ReconnectKind::BookmarkAction: {
            const std::optional<OutlineItemId> parsed = id.isString() ? OutlineItemId::fromString(id.toString()) : std::nullopt;
            if (!parsed || !name.isUndefined()) return badReconnection("id");
            entry.item = *parsed;
            break;
        }
        case ReconnectKind::NamedDestination:
            if (!name.isString() || name.toString().isEmpty() || name.toString().size() > kMaxDestinationNameLength
                || !id.isUndefined())
                return badReconnection("name");
            entry.name = name.toString();
            break;
        case ReconnectKind::Link: {
            const std::optional<AnnotationId> parsed = id.isString() ? AnnotationId::fromString(id.toString()) : std::nullopt;
            if (!parsed || !isCarriedLink(*parsed) || !name.isUndefined()) return badReconnection("id");
            entry.link = *parsed;
            break;
        }
        }
        if (item.size() != 3) return badReconnection("incoming");
        out.incoming.push_back(std::move(entry));
    }

    for (const QJsonValue value : outArray) {
        if (!value.isObject()) return badReconnection("outgoing");
        const QJsonObject item = value.toObject();
        if (item.size() != 3) return badReconnection("outgoing");
        Outgoing entry;
        entry.ordinal = wholeIn(item.value(QStringLiteral("page")), kMaxIndex);
        entry.link = wholeIn(item.value(QStringLiteral("link")), kMaxIndex);
        if (entry.ordinal < 0) return badReconnection("page");
        if (entry.link < 0) return badReconnection("link");
        const QJsonValue action = item.value(QStringLiteral("action"));
        if (!action.isObject()) return badReconnection("action");
        auto parsed = OutlineAction::fromJson(action.toObject());
        if (!parsed || parsed.value().kind != OutlineAction::Kind::GoTo || !parsed.value().destination.isResolved()
            || parsed.value().destination.pageIndex > kMaxIndex)
            return badReconnection("action");
        entry.action = std::move(parsed).value();
        out.outgoing.push_back(std::move(entry));
    }
    return out;
}

} // namespace rawpage::core
