#include "core/doc/Outline.h"

#include "core/doc/PageMap.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace rawpage::core {
namespace {

std::size_t countIn(const std::vector<OutlineItem>& items)
{
    std::size_t total = items.size();
    for (const OutlineItem& item : items) total += countIn(item.children);
    return total;
}

int treeDepth(const std::vector<OutlineItem>& items)
{
    int deepest = 0;
    for (const OutlineItem& item : items) deepest = std::max(deepest, treeDepth(item.children));
    return items.empty() ? 0 : deepest + 1;
}

Error badOutline(const char* what)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.outline.malformed"), QString::fromLatin1(what)};
}

bool keyCharacter(QChar c)
{
    return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
           || c == QLatin1Char('-');
}

// A finite number from JSON, or nullopt.
std::optional<double> finite(const QJsonValue& value)
{
    if (!value.isDouble()) return std::nullopt;
    const double number = value.toDouble();
    if (!std::isfinite(number)) return std::nullopt;
    return number;
}

std::optional<QString> boundedString(const QJsonObject& json, const char* key, qsizetype limit)
{
    const QJsonValue value = json.value(QLatin1String(key));
    if (value.isUndefined()) return QString();
    if (!value.isString()) return std::nullopt;
    QString text = value.toString();
    if (text.size() > limit) return std::nullopt;
    return text;
}

template <typename Item, typename Visit>
void walk(Item& items, Visit&& visit)
{
    for (auto& item : items) {
        visit(item);
        walk(item.children, visit);
    }
}

void remapDestination(Destination& destination, const PageMap& map)
{
    if (destination.pageIndex < 0) return;
    destination.pageIndex = destination.pageIndex < map.oldCount() ? map.newIndexOf(destination.pageIndex) : -1;
    if (destination.pageIndex < 0) destination.pageIndex = -1;
}

// The hash input, built by hand as `Document::modelHash` builds its own.
void addInt(QByteArray& out, qint64 value)
{
    const quint64 bits = static_cast<quint64>(value);
    for (int shift = 56; shift >= 0; shift -= 8) out.append(static_cast<char>((bits >> shift) & 0xFF));
}

void addReal(QByteArray& out, double value)
{
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    addInt(out, static_cast<qint64>(bits));
}

void addText(QByteArray& out, const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    addInt(out, bytes.size());
    out.append(bytes);
}

void addDestination(QByteArray& out, const Destination& d)
{
    addInt(out, d.pageIndex);
    addInt(out, static_cast<qint64>(d.fit));
    addInt(out, (d.hasX ? 1 : 0) | (d.hasY ? 2 : 0) | (d.hasZoom ? 4 : 0));
    addReal(out, d.x);
    addReal(out, d.y);
    addReal(out, d.zoom);
}

void addItems(QByteArray& out, const std::vector<OutlineItem>& items)
{
    addInt(out, static_cast<qint64>(items.size()));
    for (const OutlineItem& item : items) {
        addText(out, item.id.toString());
        addText(out, item.title);
        addDestination(out, item.destination);
        addInt(out, static_cast<qint64>(item.action.kind));
        addDestination(out, item.action.destination);
        addText(out, item.action.name);
        addText(out, item.action.file);
        addText(out, item.action.uri);
        addText(out, item.action.script);
        addInt(out, item.colour ? 1 : 0);
        if (item.colour) {
            for (const double channel : *item.colour) addReal(out, channel);
        }
        addInt(out, (item.italic ? 1 : 0) | (item.bold ? 2 : 0) | (item.openByDefault ? 4 : 0));
        addItems(out, item.children);
    }
}

} // namespace

// ---- OutlineItemId ---------------------------------------------------------------------------------

OutlineItemId OutlineItemId::ofObject(long long number)
{
    OutlineItemId id;
    id.object = number;
    return id;
}

OutlineItemId OutlineItemId::ofCreated(QString key)
{
    OutlineItemId id;
    id.created = std::move(key);
    return id;
}

QString OutlineItemId::toString() const
{
    if (isCreated()) return QStringLiteral("c:") + created;
    if (object > 0) return QStringLiteral("o:") + QString::number(object);
    return QString();
}

std::optional<OutlineItemId> OutlineItemId::fromString(const QString& text)
{
    if (text.startsWith(QLatin1String("c:"))) {
        const QString key = text.mid(2);
        if (key.isEmpty() || key.size() > 64) return std::nullopt;
        if (!std::all_of(key.begin(), key.end(), keyCharacter)) return std::nullopt;
        return ofCreated(key);
    }
    if (text.startsWith(QLatin1String("o:"))) {
        const QString digits = text.mid(2);
        if (digits.isEmpty() || digits.size() > 10) return std::nullopt;
        if (!std::all_of(digits.begin(), digits.end(), [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); }))
            return std::nullopt;
        const long long number = digits.toLongLong();
        if (number <= 0 || number > 2147483647LL) return std::nullopt;
        return ofObject(number);
    }
    return std::nullopt;
}

// ---- Names -----------------------------------------------------------------------------------------

const char* outlineActionKindName(OutlineAction::Kind kind) noexcept
{
    switch (kind) {
    case OutlineAction::Kind::None:       return "none";
    case OutlineAction::Kind::GoTo:       return "goTo";
    case OutlineAction::Kind::GoToNamed:  return "goToNamed";
    case OutlineAction::Kind::GoToRemote: return "goToRemote";
    case OutlineAction::Kind::Launch:     return "launch";
    case OutlineAction::Kind::Uri:        return "uri";
    case OutlineAction::Kind::JavaScript: return "javaScript";
    case OutlineAction::Kind::Named:      return "named";
    case OutlineAction::Kind::SubmitForm: return "submitForm";
    case OutlineAction::Kind::Other:      return "other";
    }
    return "none";
}

std::optional<OutlineAction::Kind> outlineActionKindFromName(const QString& name) noexcept
{
    for (int k = 0; k <= static_cast<int>(OutlineAction::Kind::Other); ++k) {
        const auto kind = static_cast<OutlineAction::Kind>(k);
        if (name == QLatin1String(outlineActionKindName(kind))) return kind;
    }
    return std::nullopt;
}

std::optional<Destination::Fit> destinationFitFromName(const QString& name) noexcept
{
    for (int f = 0; f <= static_cast<int>(Destination::Fit::FitBV); ++f) {
        const auto fit = static_cast<Destination::Fit>(f);
        if (name == QLatin1String(destinationFitName(fit))) return fit;
    }
    return std::nullopt;
}

QJsonObject destinationToJson(const Destination& d)
{
    QJsonObject json{{QStringLiteral("page"), d.pageIndex},
                     {QStringLiteral("fit"), QLatin1String(destinationFitName(d.fit))}};
    if (d.hasX) json.insert(QStringLiteral("x"), d.x);
    if (d.hasY) json.insert(QStringLiteral("y"), d.y);
    if (d.hasZoom) json.insert(QStringLiteral("zoom"), d.zoom);
    return json;
}

Result<Destination> destinationFromJson(const QJsonObject& json)
{
    Destination d;
    const std::optional<double> page = finite(json.value(QStringLiteral("page")));
    if (!page || std::trunc(*page) != *page || *page < -1.0 || *page > 2147483647.0) return badOutline("page");
    d.pageIndex = static_cast<int>(*page);
    const QJsonValue fit = json.value(QStringLiteral("fit"));
    const std::optional<Destination::Fit> parsed = fit.isString() ? destinationFitFromName(fit.toString()) : std::nullopt;
    if (!parsed) return badOutline("fit");
    d.fit = *parsed;
    const auto coordinate = [&json](const char* key, bool& has, double& value) {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        const std::optional<double> number = finite(v);
        if (!number || std::fabs(*number) > 1.0e7) return false;
        has = true;
        value = *number;
        return true;
    };
    if (!coordinate("x", d.hasX, d.x) || !coordinate("y", d.hasY, d.y) || !coordinate("zoom", d.hasZoom, d.zoom))
        return badOutline("coordinate");
    return d;
}

// ---- OutlineAction ---------------------------------------------------------------------------------

OutlineAction OutlineAction::goTo(const Destination& destination)
{
    OutlineAction action;
    action.kind = Kind::GoTo;
    action.destination = destination;
    return action;
}

QJsonObject OutlineAction::toJson() const
{
    QJsonObject json{{QStringLiteral("kind"), QLatin1String(outlineActionKindName(kind))}};
    if (kind == Kind::GoTo || kind == Kind::GoToRemote || kind == Kind::GoToNamed)
        json.insert(QStringLiteral("dest"), destinationToJson(destination));
    if (!name.isEmpty()) json.insert(QStringLiteral("name"), name);
    if (!file.isEmpty()) json.insert(QStringLiteral("file"), file);
    if (!uri.isEmpty()) json.insert(QStringLiteral("uri"), uri);
    if (!script.isEmpty()) json.insert(QStringLiteral("script"), script);
    return json;
}

Result<OutlineAction> OutlineAction::fromJson(const QJsonObject& json)
{
    OutlineAction action;
    const QJsonValue kind = json.value(QStringLiteral("kind"));
    const std::optional<Kind> parsed = kind.isString() ? outlineActionKindFromName(kind.toString()) : std::nullopt;
    // `Other` is a kind the FILE has and nothing writes: a command never carries one.
    if (!parsed || *parsed == Kind::Other) return badOutline("action kind");
    action.kind = *parsed;
    const QJsonValue dest = json.value(QStringLiteral("dest"));
    if (action.kind == Kind::GoTo || action.kind == Kind::GoToRemote || action.kind == Kind::GoToNamed) {
        if (!dest.isObject()) return badOutline("action dest");
        auto destination = destinationFromJson(dest.toObject());
        if (!destination) return destination.error();
        action.destination = destination.value();
    } else if (!dest.isUndefined()) {
        return badOutline("action dest");
    }
    const auto name = boundedString(json, "name", kMaxOutlineTitleLength);
    const auto file = boundedString(json, "file", kMaxOutlineTextLength);
    const auto uri = boundedString(json, "uri", kMaxOutlineTextLength);
    const auto script = boundedString(json, "script", kMaxOutlineTextLength);
    if (!name || !file || !uri || !script) return badOutline("action text");
    action.name = *name;
    action.file = *file;
    action.uri = *uri;
    action.script = *script;
    return action;
}

// ---- OutlineItem -----------------------------------------------------------------------------------

bool OutlineItem::sameProperties(const OutlineItem& other) const
{
    return id == other.id && title == other.title && destination == other.destination && action == other.action
           && colour == other.colour && italic == other.italic && bold == other.bold
           && openByDefault == other.openByDefault;
}

QJsonObject OutlineItem::toJson() const
{
    QJsonObject json{{QStringLiteral("id"), id.toString()},
                     {QStringLiteral("title"), title},
                     {QStringLiteral("action"), action.toJson()}};
    if (colour) json.insert(QStringLiteral("colour"), QJsonArray{(*colour)[0], (*colour)[1], (*colour)[2]});
    if (italic) json.insert(QStringLiteral("italic"), true);
    if (bold) json.insert(QStringLiteral("bold"), true);
    if (openByDefault) json.insert(QStringLiteral("open"), true);
    if (!children.empty()) {
        QJsonArray array;
        for (const OutlineItem& child : children) array.append(child.toJson());
        json.insert(QStringLiteral("children"), array);
    }
    return json;
}

Result<OutlineItem> OutlineItem::fromJson(const QJsonObject& json, int depth)
{
    if (depth >= kMaxOutlineDepth) return badOutline("depth");
    OutlineItem item;
    const QJsonValue id = json.value(QStringLiteral("id"));
    const std::optional<OutlineItemId> parsedId = id.isString() ? OutlineItemId::fromString(id.toString()) : std::nullopt;
    if (!parsedId) return badOutline("id");
    item.id = *parsedId;
    const QJsonValue title = json.value(QStringLiteral("title"));
    if (!title.isString() || title.toString().size() > kMaxOutlineTitleLength) return badOutline("title");
    item.title = title.toString();
    const QJsonValue action = json.value(QStringLiteral("action"));
    if (!action.isObject()) return badOutline("action");
    auto parsedAction = OutlineAction::fromJson(action.toObject());
    if (!parsedAction) return parsedAction.error();
    item.action = parsedAction.value();
    if (item.action.kind == OutlineAction::Kind::GoTo) item.destination = item.action.destination;
    const QJsonValue colour = json.value(QStringLiteral("colour"));
    if (!colour.isUndefined()) {
        const QJsonArray array = colour.toArray();
        if (!colour.isArray() || array.size() != 3) return badOutline("colour");
        std::array<double, 3> rgb{};
        for (int i = 0; i < 3; ++i) {
            const std::optional<double> channel = finite(array.at(i));
            if (!channel || *channel < 0.0 || *channel > 1.0) return badOutline("colour");
            rgb[static_cast<std::size_t>(i)] = *channel;
        }
        item.colour = rgb;
    }
    const auto flag = [&json](const char* key, bool& out) {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        if (!v.isBool()) return false;
        out = v.toBool();
        return true;
    };
    if (!flag("italic", item.italic) || !flag("bold", item.bold) || !flag("open", item.openByDefault))
        return badOutline("flag");
    const QJsonValue children = json.value(QStringLiteral("children"));
    if (!children.isUndefined()) {
        if (!children.isArray()) return badOutline("children");
        const QJsonArray array = children.toArray();
        if (static_cast<std::size_t>(array.size()) > kMaxOutlineNodes) return badOutline("children");
        for (const QJsonValue& child : array) {
            if (!child.isObject()) return badOutline("child");
            auto parsed = fromJson(child.toObject(), depth + 1);
            if (!parsed) return parsed.error();
            item.children.push_back(std::move(parsed).value());
        }
    }
    return item;
}

// ---- Outline ---------------------------------------------------------------------------------------

std::size_t Outline::nodeCount() const { return countIn(roots_); }

int Outline::depth() const { return treeDepth(roots_); }

Outline Outline::refused(Error why, std::vector<OutlineItem> roots)
{
    Outline outline;
    outline.roots_ = std::move(roots);
    outline.status_ = std::move(why);
    return outline;
}

const OutlineItem* Outline::find(const OutlineItemId& id) const
{
    return const_cast<Outline*>(this)->find(id);
}

OutlineItem* Outline::find(const OutlineItemId& id)
{
    if (!id.isValid()) return nullptr;
    std::vector<std::vector<OutlineItem>*> stack{&roots_};
    while (!stack.empty()) {
        std::vector<OutlineItem>* level = stack.back();
        stack.pop_back();
        for (OutlineItem& item : *level) {
            if (item.id == id) return &item;
            stack.push_back(&item.children);
        }
    }
    return nullptr;
}

std::optional<OutlinePosition> Outline::positionOf(const OutlineItemId& id) const
{
    if (!id.isValid()) return std::nullopt;
    struct Frame {
        const std::vector<OutlineItem>* level;
        OutlineItemId parent;
    };
    std::vector<Frame> stack{{&roots_, OutlineItemId{}}};
    while (!stack.empty()) {
        const Frame frame = stack.back();
        stack.pop_back();
        for (std::size_t i = 0; i < frame.level->size(); ++i) {
            const OutlineItem& item = (*frame.level)[i];
            if (item.id == id) return OutlinePosition{frame.parent, static_cast<int>(i)};
            stack.push_back({&item.children, item.id});
        }
    }
    return std::nullopt;
}

std::vector<OutlineItem>* Outline::childrenOf(const OutlineItemId& parent)
{
    if (!parent.isValid()) return &roots_;
    OutlineItem* item = find(parent);
    return item ? &item->children : nullptr;
}

const std::vector<OutlineItem>* Outline::childrenOf(const OutlineItemId& parent) const
{
    return const_cast<Outline*>(this)->childrenOf(parent);
}

bool Outline::isWithin(const OutlineItemId& id, const OutlineItemId& ancestor) const
{
    const OutlineItem* top = find(ancestor);
    if (top == nullptr) return false;
    if (top->id == id) return true;
    bool found = false;
    walk(top->children, [&](const OutlineItem& item) { found = found || item.id == id; });
    return found;
}

std::vector<const OutlineItem*> Outline::preorder() const
{
    std::vector<const OutlineItem*> out;
    walk(roots_, [&out](const OutlineItem& item) { out.push_back(&item); });
    return out;
}

int Outline::depthOf(const OutlineItemId& id) const
{
    int depth = 0;
    std::optional<OutlinePosition> at = positionOf(id);
    while (at) {
        ++depth;
        if (!at->parent.isValid()) return depth;
        at = positionOf(at->parent);
    }
    return 0;
}

Status Outline::insert(const OutlinePosition& at, std::vector<OutlineItem> items)
{
    std::vector<OutlineItem>* level = childrenOf(at.parent);
    if (level == nullptr) return Error{ErrorCode::NotFound, QLatin1String("error.outline.noSuchItem"), at.parent.toString()};
    if (at.index < 0 || at.index > static_cast<int>(level->size()))
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.badPosition"), QString::number(at.index)};
    // Checked on a copy, so a refusal changes nothing.
    Outline trial = *this;
    std::vector<OutlineItem>* trialLevel = trial.childrenOf(at.parent);
    trialLevel->insert(trialLevel->begin() + at.index, items.begin(), items.end());
    if (Status valid = trial.validate(); !valid) return valid;
    level->insert(level->begin() + at.index, std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
    return Status{};
}

Result<OutlineItem> Outline::remove(const OutlineItemId& id)
{
    const std::optional<OutlinePosition> at = positionOf(id);
    if (!at) return Error{ErrorCode::NotFound, QLatin1String("error.outline.noSuchItem"), id.toString()};
    std::vector<OutlineItem>* level = childrenOf(at->parent);
    OutlineItem taken = std::move((*level)[static_cast<std::size_t>(at->index)]);
    level->erase(level->begin() + at->index);
    return taken;
}

void Outline::applyPageMap(const PageMap& map)
{
    walk(roots_, [&map](OutlineItem& item) {
        remapDestination(item.destination, map);
        if (item.action.kind == OutlineAction::Kind::GoTo) remapDestination(item.action.destination, map);
    });
}

Status Outline::validate() const
{
    if (nodeCount() > kMaxOutlineNodes)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.tooLarge"), QString::number(nodeCount())};
    if (depth() > kMaxOutlineDepth)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.tooDeep"), QString::number(depth())};
    std::set<OutlineItemId> seen;
    Status status;
    walk(roots_, [&](const OutlineItem& item) {
        if (!status) return;
        if (!item.id.isValid() || !seen.insert(item.id).second)
            status = Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.badId"), item.id.toString()};
        else if (item.title.size() > kMaxOutlineTitleLength)
            status = Error{ErrorCode::InvalidArgument, QLatin1String("error.outline.titleTooLong"), item.id.toString()};
    });
    return status;
}

QByteArray Outline::digest() const
{
    QByteArray buffer;
    addInt(buffer, status_ ? 1 : 0);
    addItems(buffer, roots_);
    return QCryptographicHash::hash(buffer, QCryptographicHash::Sha256);
}

} // namespace rawpage::core
