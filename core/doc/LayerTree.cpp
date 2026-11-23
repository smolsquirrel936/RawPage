#include "core/doc/LayerTree.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <functional>
#include <map>

namespace rawpage::core {
namespace {

bool keyCharacter(QChar c)
{
    return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('z')) || c == QLatin1Char('-');
}

Error badLayer(const char* field)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"), QStringLiteral("layer.") + QLatin1String(field)};
}

Error layerError(const char* key, QString context = {})
{
    return Error{ErrorCode::InvalidArgument, QLatin1String(key), std::move(context)};
}

void addInt(QByteArray& out, qint64 value)
{
    const quint64 bits = static_cast<quint64>(value);
    for (int shift = 56; shift >= 0; shift -= 8) out.append(static_cast<char>((bits >> shift) & 0xFF));
}

void addText(QByteArray& out, const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    addInt(out, bytes.size());
    out.append(bytes);
}

void addId(QByteArray& out, const LayerId& id)
{
    addInt(out, id.object);
    addText(out, id.created);
}

void addNodes(QByteArray& out, const std::vector<LayerNode>& nodes)
{
    addInt(out, static_cast<qint64>(nodes.size()));
    for (const LayerNode& node : nodes) {
        addId(out, node.layer);
        addText(out, node.label);
        addNodes(out, node.children);
    }
}

// Depth first, each node with the list it is in and its place there; the visitor returns false to stop.
bool walk(std::vector<LayerNode>& nodes, const std::function<bool(std::vector<LayerNode>&, std::size_t)>& visit)
{
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (!visit(nodes, i)) return false;
        if (!walk(nodes[i].children, visit)) return false;
    }
    return true;
}

bool walkConst(const std::vector<LayerNode>& nodes, const std::function<bool(const LayerNode&, const LayerNode* group, int depth)>& visit,
               const LayerNode* group = nullptr, int depth = 1)
{
    for (const LayerNode& node : nodes) {
        if (!visit(node, group, depth)) return false;
        if (!walkConst(node.children, visit, node.isCollection() ? group : &node, depth + 1)) return false;
    }
    return true;
}

int depthOf(const std::vector<LayerNode>& nodes)
{
    int deepest = 0;
    for (const LayerNode& node : nodes) deepest = std::max(deepest, 1 + depthOf(node.children));
    return deepest;
}

// `id`'s node taken out of the order, its children put where it was; true when it was there.
bool unlink(std::vector<LayerNode>& order, const LayerId& id)
{
    bool found = false;
    walk(order, [&](std::vector<LayerNode>& list, std::size_t at) {
        if (list[at].layer != id) return true;
        std::vector<LayerNode> children = std::move(list[at].children);
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(at));
        list.insert(list.begin() + static_cast<std::ptrdiff_t>(at), std::make_move_iterator(children.begin()),
                    std::make_move_iterator(children.end()));
        found = true;
        return false;
    });
    return found;
}

// `id`'s node and its subtree taken out of the order whole.
std::optional<LayerNode> detach(std::vector<LayerNode>& order, const LayerId& id)
{
    std::optional<LayerNode> out;
    walk(order, [&](std::vector<LayerNode>& list, std::size_t at) {
        if (list[at].layer != id) return true;
        out = std::move(list[at]);
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(at));
        return false;
    });
    return out;
}

LayerNode* nodeIn(std::vector<LayerNode>& order, const LayerId& id)
{
    LayerNode* out = nullptr;
    walk(order, [&](std::vector<LayerNode>& list, std::size_t at) {
        if (list[at].layer != id) return true;
        out = &list[at];
        return false;
    });
    return out;
}

// Every radio group without `id`; a group left with fewer than two members is no group.
void leaveRadioGroups(std::vector<std::vector<LayerId>>& groups, const LayerId& id)
{
    for (std::vector<LayerId>& group : groups) group.erase(std::remove(group.begin(), group.end(), id), group.end());
    groups.erase(std::remove_if(groups.begin(), groups.end(), [](const std::vector<LayerId>& g) { return g.size() < 2; }),
                 groups.end());
}

Result<LayerUsage> usageFrom(const QJsonObject& json, const char* key)
{
    const QJsonValue value = json.value(QLatin1String(key));
    if (value.isUndefined()) return LayerUsage::Unset;
    const std::optional<LayerUsage> usage = value.isString() ? layerUsageFromName(value.toString()) : std::nullopt;
    if (!usage) return badLayer(key);
    return *usage;
}

} // namespace

// ---- LayerId ---------------------------------------------------------------------------------------------------------

LayerId LayerId::ofObject(long long number)
{
    LayerId id;
    id.object = number;
    return id;
}

LayerId LayerId::ofCreated(QString key)
{
    LayerId id;
    id.created = std::move(key);
    return id;
}

QString LayerId::toString() const
{
    if (isCreated()) return QStringLiteral("c:") + created;
    if (object > 0) return QStringLiteral("o:") + QString::number(object);
    return QString();
}

std::optional<LayerId> LayerId::fromString(const QString& text)
{
    if (text.startsWith(QLatin1String("c:"))) {
        const QString key = text.mid(2);
        if (key.isEmpty() || key.size() > 64 || !std::all_of(key.begin(), key.end(), keyCharacter)) return std::nullopt;
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

const char* layerUsageName(LayerUsage usage) noexcept
{
    switch (usage) {
    case LayerUsage::Unset: return "unset";
    case LayerUsage::On: return "on";
    case LayerUsage::Off: return "off";
    }
    return "unset";
}

std::optional<LayerUsage> layerUsageFromName(const QString& name) noexcept
{
    for (const LayerUsage usage : {LayerUsage::Unset, LayerUsage::On, LayerUsage::Off}) {
        if (name == QLatin1String(layerUsageName(usage))) return usage;
    }
    return std::nullopt;
}

bool isWritableLayerName(const QString& name)
{
    if (name.isEmpty() || name.size() > kMaxLayerNameLength || name.trimmed().isEmpty()) return false;
    return std::none_of(name.begin(), name.end(), [](QChar c) { return c.category() == QChar::Other_Control; });
}

// ---- Layer -----------------------------------------------------------------------------------------------------------

QJsonObject Layer::toJson() const
{
    return QJsonObject{{QStringLiteral("id"), id.toString()},
                       {QStringLiteral("name"), name},
                       {QStringLiteral("on"), defaultOn},
                       {QStringLiteral("locked"), locked},
                       {QStringLiteral("print"), QLatin1String(layerUsageName(print))},
                       {QStringLiteral("view"), QLatin1String(layerUsageName(view))},
                       {QStringLiteral("export"), QLatin1String(layerUsageName(exportUsage))}};
}

Result<Layer> Layer::fromJson(const QJsonObject& json)
{
    Layer layer;
    const QJsonValue id = json.value(QStringLiteral("id"));
    const std::optional<LayerId> parsed = id.isString() ? LayerId::fromString(id.toString()) : std::nullopt;
    if (!parsed) return badLayer("id");
    layer.id = *parsed;
    const QJsonValue name = json.value(QStringLiteral("name"));
    if (!name.isString() || name.toString().size() > kMaxLayerNameLength) return badLayer("name");
    layer.name = name.toString();
    const QJsonValue on = json.value(QStringLiteral("on"));
    const QJsonValue locked = json.value(QStringLiteral("locked"));
    if (!on.isBool() || !locked.isBool()) return badLayer("state");
    layer.defaultOn = on.toBool();
    layer.locked = locked.toBool();
    auto print = usageFrom(json, "print");
    auto view = usageFrom(json, "view");
    auto exportUsage = usageFrom(json, "export");
    if (!print) return print.error();
    if (!view) return view.error();
    if (!exportUsage) return exportUsage.error();
    layer.print = print.value();
    layer.view = view.value();
    layer.exportUsage = exportUsage.value();
    layer.autoPrint = layer.print != LayerUsage::Unset;
    layer.autoView = layer.view != LayerUsage::Unset;
    layer.autoExport = layer.exportUsage != LayerUsage::Unset;
    return layer;
}

// ---- LayerProperties -------------------------------------------------------------------------------------------------

LayerProperties LayerProperties::of(const LayerTree& tree, const LayerId& id)
{
    LayerProperties out;
    const Layer* layer = tree.find(id);
    if (layer == nullptr) return out;
    out.name = layer->name;
    out.defaultOn = layer->defaultOn;
    out.locked = layer->locked;
    // A declaration no `/AS` applies reads as none: it changes nothing drawn, and setting the field applies it.
    out.print = layer->autoPrint ? layer->print : LayerUsage::Unset;
    out.view = layer->autoView ? layer->view : LayerUsage::Unset;
    out.exportUsage = layer->autoExport ? layer->exportUsage : LayerUsage::Unset;
    out.parent = tree.parentOf(id);
    out.exclusiveWith = tree.exclusiveWith(id);
    return out;
}

QJsonObject LayerProperties::toJson() const
{
    QJsonArray exclusive;
    for (const LayerId& id : exclusiveWith) exclusive.append(id.toString());
    QJsonObject out{{QStringLiteral("name"), name},
                    {QStringLiteral("on"), defaultOn},
                    {QStringLiteral("locked"), locked},
                    {QStringLiteral("print"), QLatin1String(layerUsageName(print))},
                    {QStringLiteral("view"), QLatin1String(layerUsageName(view))},
                    {QStringLiteral("export"), QLatin1String(layerUsageName(exportUsage))},
                    {QStringLiteral("exclusiveWith"), exclusive}};
    if (parent.isValid()) out.insert(QStringLiteral("parent"), parent.toString());
    return out;
}

Result<LayerProperties> LayerProperties::fromJson(const QJsonObject& json)
{
    LayerProperties out;
    const QJsonValue name = json.value(QStringLiteral("name"));
    if (!name.isString() || name.toString().size() > kMaxLayerNameLength) return badLayer("name");
    out.name = name.toString();
    const QJsonValue on = json.value(QStringLiteral("on"));
    const QJsonValue locked = json.value(QStringLiteral("locked"));
    if (!on.isBool() || !locked.isBool()) return badLayer("state");
    out.defaultOn = on.toBool();
    out.locked = locked.toBool();
    auto print = usageFrom(json, "print");
    auto view = usageFrom(json, "view");
    auto exportUsage = usageFrom(json, "export");
    if (!print) return print.error();
    if (!view) return view.error();
    if (!exportUsage) return exportUsage.error();
    out.print = print.value();
    out.view = view.value();
    out.exportUsage = exportUsage.value();
    const QJsonValue parent = json.value(QStringLiteral("parent"));
    if (!parent.isUndefined()) {
        const std::optional<LayerId> id = parent.isString() ? LayerId::fromString(parent.toString()) : std::nullopt;
        if (!id) return badLayer("parent");
        out.parent = *id;
    }
    const QJsonValue exclusive = json.value(QStringLiteral("exclusiveWith"));
    if (!exclusive.isArray() || static_cast<std::size_t>(exclusive.toArray().size()) > kMaxLayers) return badLayer("exclusiveWith");
    for (const QJsonValue& value : exclusive.toArray()) {
        const std::optional<LayerId> id = value.isString() ? LayerId::fromString(value.toString()) : std::nullopt;
        if (!id) return badLayer("exclusiveWith");
        out.exclusiveWith.push_back(*id);
    }
    return out;
}

// ---- LayerTree -------------------------------------------------------------------------------------------------------

LayerTree LayerTree::refused(Error why)
{
    LayerTree tree;
    tree.status_ = std::move(why);
    return tree;
}

std::size_t LayerTree::visibleCount() const
{
    return static_cast<std::size_t>(std::count_if(layers_.begin(), layers_.end(), [](const Layer& l) { return !l.decoration; }));
}

const Layer* LayerTree::find(const LayerId& id) const
{
    const auto found = std::find_if(layers_.begin(), layers_.end(), [&id](const Layer& l) { return l.id == id; });
    return found == layers_.end() ? nullptr : &*found;
}

Layer* LayerTree::find(const LayerId& id)
{
    const auto found = std::find_if(layers_.begin(), layers_.end(), [&id](const Layer& l) { return l.id == id; });
    return found == layers_.end() ? nullptr : &*found;
}

const LayerNode* LayerTree::nodeOf(const LayerId& id) const
{
    const LayerNode* out = nullptr;
    walkConst(order_, [&](const LayerNode& node, const LayerNode*, int) {
        if (node.layer != id) return true;
        out = &node;
        return false;
    });
    return out;
}

LayerId LayerTree::parentOf(const LayerId& id) const
{
    LayerId out;
    walkConst(order_, [&](const LayerNode& node, const LayerNode* group, int) {
        if (node.layer != id) return true;
        if (group != nullptr) out = group->layer;
        return false;
    });
    return out;
}

bool LayerTree::isWithin(const LayerId& id, const LayerId& ancestor) const
{
    if (id == ancestor) return true;
    const LayerNode* top = nodeOf(ancestor);
    if (top == nullptr) return false;
    bool found = false;
    walkConst(top->children, [&](const LayerNode& node, const LayerNode*, int) {
        found = node.layer == id;
        return !found;
    });
    return found;
}

std::vector<LayerNode> LayerTree::shownTree() const
{
    std::vector<LayerNode> out = order_;
    std::set<LayerId> listed;
    walkConst(order_, [&](const LayerNode& node, const LayerNode*, int) {
        if (!node.isCollection()) listed.insert(node.layer);
        return true;
    });
    for (const Layer& layer : layers_) {
        if (layer.decoration || listed.count(layer.id) != 0) continue;
        LayerNode node;
        node.layer = layer.id;
        out.push_back(std::move(node));
    }
    return out;
}

std::vector<LayerId> LayerTree::exclusiveWith(const LayerId& id) const
{
    std::vector<LayerId> out;
    for (const std::vector<LayerId>& group : radioGroups_) {
        if (std::find(group.begin(), group.end(), id) == group.end()) continue;
        for (const LayerId& other : group) {
            if (other != id && std::find(out.begin(), out.end(), other) == out.end()) out.push_back(other);
        }
    }
    return out;
}

Status LayerTree::insert(Layer layer, const LayerId& parent)
{
    if (!layer.id.isValid() || find(layer.id) != nullptr) return layerError("error.layer.badId", layer.id.toString());
    if (!isWritableLayerName(layer.name)) return layerError("error.layer.badName", layer.name.left(64));
    if (layers_.size() >= kMaxLayers) return layerError("error.layer.tooMany", QString::number(static_cast<qulonglong>(kMaxLayers)));
    LayerTree next = *this;
    LayerNode node;
    node.layer = layer.id;
    if (parent.isValid()) {
        const Layer* above = find(parent);
        if (above == nullptr || above->decoration) return layerError("error.layer.notFound", parent.toString());
        LayerNode* under = nodeIn(next.order_, parent);
        if (under == nullptr) {
            // A parent no order lists is listed now, at the top level, with the new group under it.
            LayerNode top;
            top.layer = parent;
            next.order_.push_back(std::move(top));
            under = &next.order_.back();
        }
        under->children.push_back(std::move(node));
    } else {
        next.order_.push_back(std::move(node));
    }
    next.layers_.push_back(std::move(layer));
    if (Status valid = next.validate(); !valid) return valid;
    *this = std::move(next);
    return Status{};
}

Status LayerTree::remove(const LayerId& id)
{
    const Layer* layer = find(id);
    if (layer == nullptr) return layerError("error.layer.notFound", id.toString());
    if (layer->decoration) return layerError("error.layer.notEditable", layer->name.left(64));
    layers_.erase(std::find_if(layers_.begin(), layers_.end(), [&id](const Layer& l) { return l.id == id; }));
    unlink(order_, id);
    leaveRadioGroups(radioGroups_, id);
    return Status{};
}

Status LayerTree::merge(const LayerId& from, const LayerId& into)
{
    const Layer* source = find(from);
    const Layer* target = find(into);
    if (source == nullptr) return layerError("error.layer.notFound", from.toString());
    if (target == nullptr) return layerError("error.layer.notFound", into.toString());
    if (from == into) return layerError("error.layer.mergeSelf", source->name.left(64));
    if (source->decoration || target->decoration) return layerError("error.layer.notEditable", source->name.left(64));
    LayerTree next = *this;
    if (Status removed = next.remove(from); !removed) return removed;
    next.merges_.emplace_back(from, into);
    *this = std::move(next);
    return Status{};
}

Status LayerTree::setProperties(const LayerId& id, const LayerProperties& properties)
{
    const Layer* found = find(id);
    if (found == nullptr) return layerError("error.layer.notFound", id.toString());
    if (found->decoration) return layerError("error.layer.notEditable", found->name.left(64));
    if (!isWritableLayerName(properties.name)) return layerError("error.layer.badName", properties.name.left(64));
    LayerTree next = *this;
    Layer& layer = *next.find(id);
    layer.name = properties.name;
    layer.defaultOn = properties.defaultOn;
    layer.locked = properties.locked;
    // A usage set is applied (`/AS`); one cleared is applied no more -- the declaration itself kept as read.
    const auto usage = [](LayerUsage set, LayerUsage& field, bool& applied) {
        if (set != LayerUsage::Unset) {
            field = set;
            applied = true;
        } else {
            applied = false;
        }
    };
    usage(properties.print, layer.print, layer.autoPrint);
    usage(properties.view, layer.view, layer.autoView);
    usage(properties.exportUsage, layer.exportUsage, layer.autoExport);

    // GROUPING: moved only when it names another parent.
    if (properties.parent != next.parentOf(id)) {
        if (properties.parent.isValid()) {
            const Layer* above = next.find(properties.parent);
            if (above == nullptr || above->decoration) return layerError("error.layer.notFound", properties.parent.toString());
            if (next.isWithin(properties.parent, id)) return layerError("error.layer.badParent", properties.name.left(64));
        }
        std::optional<LayerNode> node = detach(next.order_, id);
        if (!node) {
            node = LayerNode{};
            node->layer = id;
        }
        if (properties.parent.isValid()) {
            LayerNode* under = nodeIn(next.order_, properties.parent);
            if (under == nullptr) {
                LayerNode top;
                top.layer = properties.parent;
                next.order_.push_back(std::move(top));
                under = &next.order_.back();
            }
            under->children.push_back(std::move(*node));
        } else {
            next.order_.push_back(std::move(*node));
        }
    }

    // ITS RADIO GROUP: unchanged unless it names other members.
    std::vector<LayerId> wanted = properties.exclusiveWith;
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
    std::vector<LayerId> now = next.exclusiveWith(id);
    std::sort(now.begin(), now.end());
    if (wanted != now) {
        leaveRadioGroups(next.radioGroups_, id);
        for (const LayerId& other : wanted) {
            const Layer* member = next.find(other);
            if (member == nullptr || member->decoration || other == id) return layerError("error.layer.notFound", other.toString());
            leaveRadioGroups(next.radioGroups_, other);
        }
        if (!wanted.empty()) {
            std::vector<LayerId> group{id};
            group.insert(group.end(), wanted.begin(), wanted.end());
            next.radioGroups_.push_back(std::move(group));
        }
    }
    if (Status valid = next.validate(); !valid) return valid;
    *this = std::move(next);
    return Status{};
}

Status LayerTree::validate() const
{
    if (layers_.size() > kMaxLayers) return layerError("error.layer.tooMany", QString::number(static_cast<qulonglong>(layers_.size())));
    std::set<LayerId> ids;
    for (const Layer& layer : layers_) {
        if (!layer.id.isValid() || !ids.insert(layer.id).second) return layerError("error.layer.badId", layer.id.toString());
        if (layer.name.size() > kMaxLayerNameLength) return layerError("error.layer.badName", layer.name.left(64));
    }
    if (depthOf(order_) > kMaxLayerDepth) return layerError("error.layer.tooDeep", QString::number(kMaxLayerDepth));
    std::set<LayerId> ordered;
    bool unique = true;
    std::size_t nodes = 0;
    walkConst(order_, [&](const LayerNode& node, const LayerNode*, int) {
        ++nodes;
        if (node.isCollection()) return true;
        if (ids.count(node.layer) == 0 || !ordered.insert(node.layer).second) {
            unique = false;
            return false;
        }
        return true;
    });
    if (!unique) return layerError("error.layer.badOrder");
    if (nodes > 2 * kMaxLayers) return layerError("error.layer.tooMany", QString::number(static_cast<qulonglong>(nodes)));
    for (const std::vector<LayerId>& group : radioGroups_) {
        for (const LayerId& id : group) {
            if (ids.count(id) == 0) return layerError("error.layer.badOrder", id.toString());
        }
    }
    // A merge's source is gone, once. Its target may be gone too -- deleted after the merge, the content of both then
    // orphaned together -- so a merge is only ever followed, never required to land on a group still listed.
    std::set<LayerId> merged;
    for (const auto& [from, to] : merges_) {
        if (ids.count(from) != 0 || !from.isValid() || !to.isValid() || from == to || !merged.insert(from).second)
            return layerError("error.layer.badId", from.toString());
    }
    return Status{};
}

QByteArray LayerTree::digest() const
{
    QByteArray buffer;
    addInt(buffer, status_ ? 1 : 0);
    addInt(buffer, baseOff_ ? 1 : 0);
    addInt(buffer, static_cast<qint64>(layers_.size()));
    for (const Layer& layer : layers_) {
        addId(buffer, layer.id);
        addText(buffer, layer.name);
        addInt(buffer, (layer.defaultOn ? 1 : 0) | (layer.locked ? 2 : 0) | (layer.autoPrint ? 4 : 0) | (layer.autoView ? 8 : 0)
                           | (layer.autoExport ? 16 : 0) | (layer.decoration ? 32 : 0));
        addInt(buffer, static_cast<qint64>(layer.print) | (static_cast<qint64>(layer.view) << 4)
                           | (static_cast<qint64>(layer.exportUsage) << 8));
    }
    addNodes(buffer, order_);
    addInt(buffer, static_cast<qint64>(radioGroups_.size()));
    for (const std::vector<LayerId>& group : radioGroups_) {
        addInt(buffer, static_cast<qint64>(group.size()));
        for (const LayerId& id : group) addId(buffer, id);
    }
    addInt(buffer, static_cast<qint64>(merges_.size()));
    for (const auto& [from, to] : merges_) {
        addId(buffer, from);
        addId(buffer, to);
    }
    return QCryptographicHash::hash(buffer, QCryptographicHash::Sha256);
}

// ---- LayerView -------------------------------------------------------------------------------------------------------

namespace LayerView {

std::set<LayerId> atOpen(const LayerTree& tree)
{
    std::set<LayerId> hidden;
    for (const Layer& layer : tree.layers()) {
        if (!layer.shownAtOpen()) hidden.insert(layer.id);
    }
    return hidden;
}

Result<std::set<LayerId>> setShown(const LayerTree& tree, std::set<LayerId> hidden, const LayerId& id, bool shown)
{
    const Layer* layer = tree.find(id);
    if (layer == nullptr) return layerError("error.layer.notFound", id.toString());
    if (layer->decoration) return layerError("error.layer.notEditable", layer->name.left(64));
    if (layer->locked) return Error{ErrorCode::PermissionDenied, QLatin1String("error.layer.locked"), layer->name.left(64)};
    if (!shown) {
        hidden.insert(id);
        return hidden;
    }
    hidden.erase(id);
    for (const LayerId& other : tree.exclusiveWith(id)) hidden.insert(other);
    return hidden;
}

std::vector<long long> key(const std::set<LayerId>& hidden)
{
    std::vector<long long> out;
    out.reserve(hidden.size());
    for (const LayerId& id : hidden) {
        if (!id.isCreated()) {
            out.push_back(id.object);
            continue;
        }
        // A created group's key as a negative number: a hash of its key, which never meets an object number.
        const QByteArray digest = QCryptographicHash::hash(id.created.toUtf8(), QCryptographicHash::Sha256);
        quint64 bits = 0;
        for (int i = 0; i < 7; ++i) bits = (bits << 8) | static_cast<unsigned char>(digest.at(i));
        out.push_back(-static_cast<long long>(bits) - 1);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace LayerView

} // namespace rawpage::core
