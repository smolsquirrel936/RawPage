#include "core/cmd/commands/BookmarkCommands.h"

#include "core/doc/Annotation.h"

#include <QJsonArray>
#include <QJsonValue>

#include <cmath>
#include <set>

namespace rawpage::core {
namespace {

Error badBookmarkCommand(QLatin1String type, const char* field)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"),
                 QString(type) + QLatin1Char('.') + QLatin1String(field)};
}

bool typeIs(const QJsonObject& json, QLatin1String type)
{
    const QJsonValue value = json.value(QStringLiteral("type"));
    return value.isString() && value.toString() == type;
}

Error noSuchItem(const OutlineItemId& id)
{
    return Error{ErrorCode::NotFound, QLatin1String("error.outline.noSuchItem"), id.toString()};
}

std::optional<OutlineItemId> idFrom(const QJsonObject& json, const char* key)
{
    const QJsonValue value = json.value(QLatin1String(key));
    if (!value.isString()) return std::nullopt;
    return OutlineItemId::fromString(value.toString());
}

// `{"parent": "<id>" or "", "index": n}` -- a place.
std::optional<OutlinePosition> positionFrom(const QJsonObject& json)
{
    const QJsonValue parent = json.value(QStringLiteral("parent"));
    const QJsonValue index = json.value(QStringLiteral("index"));
    if (!parent.isString() || !index.isDouble()) return std::nullopt;
    OutlinePosition at;
    if (!parent.toString().isEmpty()) {
        const std::optional<OutlineItemId> id = OutlineItemId::fromString(parent.toString());
        if (!id) return std::nullopt;
        at.parent = *id;
    }
    const double value = index.toDouble();
    if (!std::isfinite(value) || std::trunc(value) != value || value < 0.0
        || value > static_cast<double>(kMaxOutlineNodes))
        return std::nullopt;
    at.index = static_cast<int>(value);
    return at;
}

void addPosition(QJsonObject& json, const OutlinePosition& at)
{
    json.insert(QStringLiteral("parent"), at.parent.toString());
    json.insert(QStringLiteral("index"), at.index);
}

std::optional<std::vector<OutlineItem>> itemsFrom(const QJsonObject& json)
{
    const QJsonValue value = json.value(QStringLiteral("items"));
    if (!value.isArray()) return std::nullopt;
    const QJsonArray array = value.toArray();
    if (array.isEmpty() || static_cast<std::size_t>(array.size()) > kMaxOutlineNodes) return std::nullopt;
    std::vector<OutlineItem> items;
    for (const QJsonValue& item : array) {
        if (!item.isObject()) return std::nullopt;
        auto parsed = OutlineItem::fromJson(item.toObject());
        if (!parsed) return std::nullopt;
        items.push_back(std::move(parsed).value());
    }
    return items;
}

QJsonArray itemsJson(const std::vector<OutlineItem>& items)
{
    QJsonArray array;
    for (const OutlineItem& item : items) array.append(item.toJson());
    return array;
}

// Items a command makes must be NEW: an id the outline already holds would make one item two.
Result<Outline> withInserted(const Outline& before, const OutlinePosition& at, const std::vector<OutlineItem>& items)
{
    if (items.empty()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.nothingToAdd"), QString()};
    Outline out = before;
    if (Status inserted = out.insert(at, items); !inserted) return inserted.error();
    return out;
}

CatalogText countName(const char* key, std::size_t count)
{
    return CatalogText{QLatin1String(key), {QString::number(static_cast<qulonglong>(count))}};
}

std::size_t countOf(const std::vector<OutlineItem>& items)
{
    std::size_t total = items.size();
    for (const OutlineItem& item : items) total += countOf(item.children);
    return total;
}

} // namespace

QString newBookmarkKey()
{
    return newAnnotationName();   // the same 128 random bits, the same spelling
}

OutlineItemId newBookmarkId(const QString& key, int ordinal)
{
    return OutlineItemId::ofCreated(key + QLatin1Char('-') + QString::number(ordinal));
}

Status outlineCommandPermitted(const Document& document)
{
    if (Status status = document.permissions().may(Operation::EditOutline); !status) return status;
    return document.outline().status();
}

bool OutlineEditState::apply(Document& document, MutationSink& sink, Result<Outline> after)
{
    refusal.reset();
    if (!after) {
        refusal = after.error();
        return false;
    }
    Outline next = std::move(after).value();
    if (Status valid = next.validate(); !valid) {
        refusal = valid.error();
        return false;
    }
    if (Status shown = sink.setOutline(next); !shown) {
        refusal = shown.error();
        return false;
    }
    before = document.outline();
    next.setStatus(document.outline().status());
    document.outline() = std::move(next);
    return true;
}

bool OutlineEditState::revert(Document& document, MutationSink& sink)
{
    // The journal undoes in order, so the model is the outline this command made, and the one before is exact.
    if (!before || !sink.setOutline(*before)) return false;
    document.outline() = *before;
    return true;
}

// ---- CreateBookmarkCommand ---------------------------------------------------------------------------

CreateBookmarkCommand::CreateBookmarkCommand(OutlinePosition at, std::vector<OutlineItem> items)
    : at_(std::move(at)), items_(std::move(items))
{
}

CatalogText CreateBookmarkCommand::name() const
{
    if (items_.size() == 1) return CatalogText{QLatin1String("command.bookmark.create"), {items_.front().title}};
    return countName("command.bookmark.createMany", countOf(items_));
}

Result<Outline> CreateBookmarkCommand::after(const Outline& before) const
{
    for (const OutlineItem& item : items_) {
        if (!item.id.isCreated()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.badId"), item.id.toString()};
    }
    return withInserted(before, at_, items_);
}

QJsonObject CreateBookmarkCommand::serialize() const
{
    QJsonObject json{{QStringLiteral("type"), QLatin1String(kTypeId)}, {QStringLiteral("items"), itemsJson(items_)}};
    addPosition(json, at_);
    return json;
}

Result<std::unique_ptr<Command>> CreateBookmarkCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badBookmarkCommand(kTypeId, "type");
    const std::optional<OutlinePosition> at = positionFrom(json);
    if (!at) return badBookmarkCommand(kTypeId, "position");
    std::optional<std::vector<OutlineItem>> items = itemsFrom(json);
    if (!items) return badBookmarkCommand(kTypeId, "items");
    return std::unique_ptr<Command>(new CreateBookmarkCommand(*at, std::move(*items)));
}

// ---- EditBookmarkCommand -----------------------------------------------------------------------------

EditBookmarkCommand::Properties EditBookmarkCommand::Properties::of(const OutlineItem& item)
{
    return Properties{item.title, item.colour, item.italic, item.bold, item.openByDefault};
}

EditBookmarkCommand::EditBookmarkCommand(OutlineItemId id, Properties properties)
    : id_(std::move(id)), properties_(std::move(properties))
{
}

CatalogText EditBookmarkCommand::name() const
{
    return CatalogText{QLatin1String("command.bookmark.edit"), {properties_.title}};
}

Result<Outline> EditBookmarkCommand::after(const Outline& before) const
{
    Outline out = before;
    OutlineItem* item = out.find(id_);
    if (item == nullptr) return noSuchItem(id_);
    // Nothing to change is no command, as a move to where the pages are is none (guide A-17's rule).
    if (Properties::of(*item) == properties_)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.nothingChanged"), id_.toString()};
    item->title = properties_.title;
    item->colour = properties_.colour;
    item->italic = properties_.italic;
    item->bold = properties_.bold;
    item->openByDefault = properties_.open;
    return out;
}

QJsonObject EditBookmarkCommand::serialize() const
{
    QJsonObject json{{QStringLiteral("type"), QLatin1String(kTypeId)},
                     {QStringLiteral("id"), id_.toString()},
                     {QStringLiteral("title"), properties_.title},
                     {QStringLiteral("italic"), properties_.italic},
                     {QStringLiteral("bold"), properties_.bold},
                     {QStringLiteral("open"), properties_.open}};
    if (properties_.colour)
        json.insert(QStringLiteral("colour"), QJsonArray{(*properties_.colour)[0], (*properties_.colour)[1], (*properties_.colour)[2]});
    return json;
}

Result<std::unique_ptr<Command>> EditBookmarkCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badBookmarkCommand(kTypeId, "type");
    const std::optional<OutlineItemId> id = idFrom(json, "id");
    if (!id) return badBookmarkCommand(kTypeId, "id");
    // The properties are an item's, read by the item's own parser -- the same bounds, the same refusals.
    QJsonObject item{{QStringLiteral("id"), id->toString()},
                     {QStringLiteral("title"), json.value(QStringLiteral("title"))},
                     {QStringLiteral("action"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("none")}}}};
    for (const char* key : {"colour", "italic", "bold", "open"}) {
        const QJsonValue value = json.value(QLatin1String(key));
        if (!value.isUndefined()) item.insert(QLatin1String(key), value);
    }
    if (!json.value(QStringLiteral("italic")).isBool() || !json.value(QStringLiteral("bold")).isBool()
        || !json.value(QStringLiteral("open")).isBool())
        return badBookmarkCommand(kTypeId, "flags");
    auto parsed = OutlineItem::fromJson(item);
    if (!parsed) return badBookmarkCommand(kTypeId, "properties");
    return std::unique_ptr<Command>(new EditBookmarkCommand(*id, Properties::of(parsed.value())));
}

// ---- DeleteBookmarkCommand ---------------------------------------------------------------------------

DeleteBookmarkCommand::DeleteBookmarkCommand(std::vector<OutlineItemId> ids, bool keepChildren)
    : ids_(std::move(ids)), keepChildren_(keepChildren)
{
}

CatalogText DeleteBookmarkCommand::name() const
{
    if (ids_.size() == 1) return CatalogText{QLatin1String("command.bookmark.delete"), {}};
    return countName("command.bookmark.deleteMany", ids_.size());
}

Result<Outline> DeleteBookmarkCommand::after(const Outline& before) const
{
    if (ids_.empty()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.nothingToDelete"), QString()};
    Outline out = before;
    for (const OutlineItemId& id : ids_) {
        const std::optional<OutlinePosition> at = out.positionOf(id);
        if (!at) return noSuchItem(id);
        auto removed = out.remove(id);
        if (!removed) return removed.error();
        if (keepChildren_ && !removed.value().children.empty()) {
            // Its children PROMOTED into its place, in order (`Features.md` §5.4: *a choice about its children*).
            if (Status kept = out.insert(*at, std::move(removed.value().children)); !kept) return kept.error();
        }
    }
    return out;
}

QJsonObject DeleteBookmarkCommand::serialize() const
{
    QJsonArray ids;
    for (const OutlineItemId& id : ids_) ids.append(id.toString());
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("ids"), ids},
                       {QStringLiteral("keepChildren"), keepChildren_}};
}

Result<std::unique_ptr<Command>> DeleteBookmarkCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badBookmarkCommand(kTypeId, "type");
    const QJsonValue ids = json.value(QStringLiteral("ids"));
    if (!ids.isArray() || ids.toArray().isEmpty() || static_cast<std::size_t>(ids.toArray().size()) > kMaxOutlineNodes)
        return badBookmarkCommand(kTypeId, "ids");
    std::vector<OutlineItemId> parsed;
    std::set<OutlineItemId> seen;
    for (const QJsonValue& value : ids.toArray()) {
        const std::optional<OutlineItemId> id = value.isString() ? OutlineItemId::fromString(value.toString()) : std::nullopt;
        if (!id || !seen.insert(*id).second) return badBookmarkCommand(kTypeId, "ids");
        parsed.push_back(*id);
    }
    const QJsonValue keep = json.value(QStringLiteral("keepChildren"));
    if (!keep.isBool()) return badBookmarkCommand(kTypeId, "keepChildren");
    return std::unique_ptr<Command>(new DeleteBookmarkCommand(std::move(parsed), keep.toBool()));
}

// ---- ReparentBookmarkCommand -------------------------------------------------------------------------

ReparentBookmarkCommand::ReparentBookmarkCommand(OutlineItemId id, OutlinePosition to) : id_(std::move(id)), to_(std::move(to)) {}

CatalogText ReparentBookmarkCommand::name() const
{
    return CatalogText{QLatin1String("command.bookmark.move"), {}};
}

Result<Outline> ReparentBookmarkCommand::after(const Outline& before) const
{
    const std::optional<OutlinePosition> from = before.positionOf(id_);
    if (!from) return noSuchItem(id_);
    // A TREE HAS NO PLACE UNDER ITSELF: the item, or anything below it, as its own new parent.
    if (to_.parent.isValid() && before.isWithin(to_.parent, id_))
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.cycle"), id_.toString()};
    if (to_.parent == from->parent && to_.index == from->index)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.nothingChanged"), id_.toString()};
    Outline out = before;
    auto taken = out.remove(id_);
    if (!taken) return taken.error();
    std::vector<OutlineItem> moved;
    moved.push_back(std::move(taken).value());
    if (Status placed = out.insert(to_, std::move(moved)); !placed) return placed.error();
    return out;
}

QJsonObject ReparentBookmarkCommand::serialize() const
{
    QJsonObject json{{QStringLiteral("type"), QLatin1String(kTypeId)}, {QStringLiteral("id"), id_.toString()}};
    addPosition(json, to_);
    return json;
}

Result<std::unique_ptr<Command>> ReparentBookmarkCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badBookmarkCommand(kTypeId, "type");
    const std::optional<OutlineItemId> id = idFrom(json, "id");
    if (!id) return badBookmarkCommand(kTypeId, "id");
    const std::optional<OutlinePosition> to = positionFrom(json);
    if (!to) return badBookmarkCommand(kTypeId, "position");
    return std::unique_ptr<Command>(new ReparentBookmarkCommand(*id, *to));
}

// ---- SetBookmarkTargetCommand ------------------------------------------------------------------------

SetBookmarkTargetCommand::SetBookmarkTargetCommand(OutlineItemId id, OutlineAction action)
    : id_(std::move(id)), action_(std::move(action))
{
}

CatalogText SetBookmarkTargetCommand::name() const
{
    return CatalogText{QLatin1String("command.bookmark.setTarget"), {}};
}

Result<Outline> SetBookmarkTargetCommand::after(const Outline& before) const
{
    if (action_.kind == OutlineAction::Kind::Other)
        return Error{ErrorCode::Unsupported, QLatin1String("error.outline.actionUnwritable"), QStringLiteral("other")};
    Outline out = before;
    OutlineItem* item = out.find(id_);
    if (item == nullptr) return noSuchItem(id_);
    if (item->action == action_)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.nothingChanged"), id_.toString()};
    item->action = action_;
    // Where `Enter` goes: a destination in this document is the target itself; every other kind goes nowhere here.
    item->destination = action_.kind == OutlineAction::Kind::GoTo ? action_.destination : Destination{};
    return out;
}

QJsonObject SetBookmarkTargetCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("id"), id_.toString()},
                       {QStringLiteral("action"), action_.toJson()}};
}

Result<std::unique_ptr<Command>> SetBookmarkTargetCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badBookmarkCommand(kTypeId, "type");
    const std::optional<OutlineItemId> id = idFrom(json, "id");
    if (!id) return badBookmarkCommand(kTypeId, "id");
    const QJsonValue action = json.value(QStringLiteral("action"));
    if (!action.isObject()) return badBookmarkCommand(kTypeId, "action");
    auto parsed = OutlineAction::fromJson(action.toObject());
    if (!parsed) return badBookmarkCommand(kTypeId, "action");
    return std::unique_ptr<Command>(new SetBookmarkTargetCommand(*id, std::move(parsed).value()));
}

// ---- GenerateBookmarksCommand ------------------------------------------------------------------------

namespace {

const char* sourceName(GenerateBookmarksCommand::Source source)
{
    switch (source) {
    case GenerateBookmarksCommand::Source::Headings: return "headings";
    case GenerateBookmarksCommand::Source::Fonts:    return "fonts";
    case GenerateBookmarksCommand::Source::Pattern:  return "pattern";
    case GenerateBookmarksCommand::Source::Import:   return "import";
    }
    return "headings";
}

} // namespace

GenerateBookmarksCommand::GenerateBookmarksCommand(OutlinePosition at, std::vector<OutlineItem> items, Source source)
    : at_(std::move(at)), items_(std::move(items)), source_(source)
{
}

CatalogText GenerateBookmarksCommand::name() const
{
    return countName(source_ == Source::Import ? "command.bookmark.import" : "command.bookmark.generate", countOf(items_));
}

Result<Outline> GenerateBookmarksCommand::after(const Outline& before) const
{
    Outline made;
    made.roots() = items_;
    for (const OutlineItem* item : made.preorder()) {
        if (!item->id.isCreated()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.badId"), item->id.toString()};
    }
    return withInserted(before, at_, items_);
}

QJsonObject GenerateBookmarksCommand::serialize() const
{
    QJsonObject json{{QStringLiteral("type"), QLatin1String(kTypeId)},
                     {QStringLiteral("source"), QLatin1String(sourceName(source_))},
                     {QStringLiteral("items"), itemsJson(items_)}};
    addPosition(json, at_);
    return json;
}

Result<std::unique_ptr<Command>> GenerateBookmarksCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badBookmarkCommand(kTypeId, "type");
    const std::optional<OutlinePosition> at = positionFrom(json);
    if (!at) return badBookmarkCommand(kTypeId, "position");
    const QJsonValue sourceValue = json.value(QStringLiteral("source"));
    std::optional<Source> source;
    for (const Source candidate : {Source::Headings, Source::Fonts, Source::Pattern, Source::Import}) {
        if (sourceValue.isString() && sourceValue.toString() == QLatin1String(sourceName(candidate))) source = candidate;
    }
    if (!source) return badBookmarkCommand(kTypeId, "source");
    std::optional<std::vector<OutlineItem>> items = itemsFrom(json);
    if (!items) return badBookmarkCommand(kTypeId, "items");
    return std::unique_ptr<Command>(new GenerateBookmarksCommand(*at, std::move(*items), *source));
}

} // namespace rawpage::core
