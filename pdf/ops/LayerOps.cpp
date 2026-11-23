#include "pdf/ops/LayerOps.h"

#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/AnnotationOps.h"
#include "pdf/ops/DecorationOps.h"
#include "pdf/ops/EmbeddedFileReader.h"

#include <algorithm>

namespace rawpage::pdf::ops {

using cos::CosObject;
using core::Layer;
using core::LayerId;
using core::LayerNode;
using core::LayerTree;
using core::LayerUsage;

namespace {

CosObject resolved(cos::XrefTable& table, const CosObject& value)
{
    if (!value.isReference()) return value;
    auto fetched = table.fetch(value.reference().number);
    if (!fetched) return CosObject::makeNull();
    CosObject out = fetched.value().object;
    out.clearSpansRecursively();
    return out;
}

// A list's items BY VALUE: a range-for over `resolved(...).items()` would iterate a destroyed temporary.
std::vector<CosObject> itemsOf(cos::XrefTable& table, const CosObject& value)
{
    return resolved(table, value).items();
}

core::Error layerReadError(const char* key, long long detail = -1)
{
    return core::Error{core::ErrorCode::Corrupted, QLatin1String(key), detail >= 0 ? QString::number(detail) : QString()};
}

std::vector<std::string> namesIn(const CosObject& value)
{
    std::vector<std::string> out;
    if (value.isName()) out.push_back(value.text());
    for (const CosObject& item : value.items()) {
        if (item.isName()) out.push_back(item.text());
    }
    return out;
}

bool hasName(const std::vector<std::string>& names, const std::string& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

LayerUsage usageIn(cos::XrefTable& table, const CosObject& usage, const char* category, const char* state)
{
    const CosObject entry = resolved(table, resolved(table, usage).get(category));
    const CosObject& value = entry.get(state);
    if (!value.isName()) return LayerUsage::Unset;
    if (value.text() == "ON") return LayerUsage::On;
    if (value.text() == "OFF") return LayerUsage::Off;
    return LayerUsage::Unset;
}

// THE `/Order` WALK -- bounded and cycle-safe (see the header).
struct OrderWalk {
    cos::XrefTable& table;
    const std::map<long long, LayerId>& groups;
    std::set<long long> path;    // indirect arrays on the way down: one seen again is a cycle
    std::set<long long> seen;    // groups already placed: shown once
    std::size_t nodes = 0;
    std::optional<core::Error> refusal;

    std::vector<LayerNode> items(const std::vector<CosObject>& list, std::size_t first, int depth)
    {
        std::vector<LayerNode> out;
        if (depth > core::kMaxLayerDepth) {
            if (!refusal) refusal = layerReadError("error.layer.tooDeep", core::kMaxLayerDepth);
            return out;
        }
        bool lastWasGroup = false;
        for (std::size_t i = first; i < list.size() && !refusal; ++i) {
            const CosObject& item = list[i];
            if (item.isReference()) {
                if (const auto group = groups.find(item.reference().number); group != groups.end()) {
                    lastWasGroup = false;
                    if (!seen.insert(group->first).second) continue;
                    if (++nodes > 2 * core::kMaxLayers) {
                        refusal = layerReadError("error.layer.tooMany", static_cast<long long>(nodes));
                        return out;
                    }
                    LayerNode node;
                    node.layer = group->second;
                    out.push_back(std::move(node));
                    lastWasGroup = true;
                    continue;
                }
            }
            std::optional<long long> number;
            if (item.isReference()) {
                number = item.reference().number;
                if (path.count(*number) != 0) {
                    refusal = layerReadError("error.layer.orderCycle", *number);
                    return out;
                }
            }
            const CosObject array = resolved(table, item);
            if (!array.isArray()) {
                lastWasGroup = false;
                continue;   // a group no `/OCGs` lists, or something that is no entry at all
            }
            if (number) path.insert(*number);
            const std::vector<CosObject>& inner = array.items();
            const CosObject opening = inner.empty() ? CosObject::makeNull() : resolved(table, inner.front());
            if (!opening.isString() && lastWasGroup) {
                // A group's children: the array right after it, not opening with a label.
                std::vector<LayerNode> children = items(inner, 0, depth + 1);
                for (LayerNode& child : children) out.back().children.push_back(std::move(child));
            } else {
                LayerNode collection;
                if (opening.isString()) collection.label = EmbeddedFileReader::decodeTextString(opening.text());
                collection.children = items(inner, opening.isString() ? 1 : 0, depth + 1);
                if (++nodes > 2 * core::kMaxLayers && !refusal) refusal = layerReadError("error.layer.tooMany", static_cast<long long>(nodes));
                out.push_back(std::move(collection));
            }
            if (number) path.erase(*number);
            lastWasGroup = false;
        }
        return out;
    }
};

CosObject withList(const CosObject& dictionary, const char* key, std::vector<CosObject> items, bool keepEmpty = false)
{
    if (items.empty() && !keepEmpty) return AnnotationOps::withoutEntry(dictionary, key);
    return AnnotationOps::withEntry(dictionary, key, CosObject::makeArray(std::move(items)));
}

// Every reference `value` makes, at any depth -- a stream's dictionary only.
void referencesIn(const CosObject& value, std::set<long long>& out)
{
    if (value.isReference()) {
        out.insert(value.reference().number);
        return;
    }
    for (const CosObject& item : value.items()) referencesIn(item, out);
    for (const auto& entry : value.entries()) referencesIn(entry.value, out);
}

bool namesAny(const CosObject& value, const std::set<long long>& numbers)
{
    std::set<long long> found;
    referencesIn(value, found);
    return std::any_of(found.begin(), found.end(), [&numbers](long long n) { return numbers.count(n) != 0; });
}

// The model's `/Order`, as PDF writes one: a group, then the array of its children; a collection an array of its own,
// opening with its label when it has one.
void writeOrder(const std::vector<LayerNode>& nodes, const std::map<LayerId, CosObject>& refs, std::vector<CosObject>& out)
{
    for (const LayerNode& node : nodes) {
        std::vector<CosObject> children;
        writeOrder(node.children, refs, children);
        if (node.isCollection()) {
            std::vector<CosObject> array;
            if (!node.label.isEmpty()) array.push_back(AnnotationOps::textString(node.label));
            for (CosObject& child : children) array.push_back(std::move(child));
            out.push_back(CosObject::makeArray(std::move(array)));
            continue;
        }
        const auto ref = refs.find(node.layer);
        if (ref == refs.end()) continue;
        out.push_back(ref->second);
        if (!children.empty()) out.push_back(CosObject::makeArray(std::move(children)));
    }
}

const char* stateKey(const char* category)
{
    if (std::string_view(category) == "Print") return "PrintState";
    if (std::string_view(category) == "View") return "ViewState";
    return "ExportState";
}

} // namespace

// =====================================================================================================================
// Read
// =====================================================================================================================

Layer LayerOps::layerOf(cos::XrefTable& table, const CosObject& dictionary)
{
    Layer layer;
    const CosObject name = resolved(table, dictionary.get("Name"));
    if (name.isString()) layer.name = EmbeddedFileReader::decodeTextString(name.text());
    const CosObject& usage = dictionary.get("Usage");
    layer.print = usageIn(table, usage, "Print", "PrintState");
    layer.view = usageIn(table, usage, "View", "ViewState");
    layer.exportUsage = usageIn(table, usage, "Export", "ExportState");
    layer.decoration = DecorationOps::markedId(dictionary).has_value();
    return layer;
}

LayerTree LayerOps::read(cos::XrefTable& table)
{
    LayerTree tree;
    const CosObject catalog = resolved(table, table.trailer().get("Root"));
    if (!catalog.isDictionary()) return tree;
    const CosObject properties = resolved(table, catalog.get("OCProperties"));
    if (!properties.isDictionary()) return tree;

    std::optional<core::Error> refusal;
    std::map<long long, LayerId> groups;
    std::map<long long, std::size_t> indexOf;
    const CosObject listed = resolved(table, properties.get("OCGs"));
    // A LIST PAST THE BOUND is read up to it and refused -- whatever its entries name, so a million references cost a
    // bounded read and say so (Stage 7 guide A-105).
    if (listed.items().size() > core::kMaxLayers)
        refusal = layerReadError("error.layer.tooMany", static_cast<long long>(listed.items().size()));
    const std::size_t readable = std::min(listed.items().size(), core::kMaxLayers);
    for (std::size_t at = 0; at < readable; ++at) {
        const CosObject& item = listed.items()[at];
        if (!item.isReference()) continue;   // a direct dictionary: no list could name it, so no group of ours
        const long long number = item.reference().number;
        if (indexOf.count(number) != 0) continue;
        const CosObject dictionary = resolved(table, item);
        if (!dictionary.isDictionary()) continue;
        Layer layer = layerOf(table, dictionary);
        layer.id = LayerId::ofObject(number);
        indexOf.emplace(number, tree.layers().size());
        groups.emplace(number, layer.id);
        tree.layers().push_back(std::move(layer));
    }

    const CosObject defaults = resolved(table, properties.get("D"));
    if (defaults.isDictionary()) {
        const CosObject& base = defaults.get("BaseState");
        const bool baseOff = base.isName() && base.text() == "OFF";
        tree.setBaseOff(baseOff);
        for (Layer& layer : tree.layers()) layer.defaultOn = !baseOff;
        const auto each = [&](const char* key, const std::function<void(Layer&)>& apply) {
            for (const CosObject& item : itemsOf(table, defaults.get(key))) {
                if (!item.isReference()) continue;
                if (const auto at = indexOf.find(item.reference().number); at != indexOf.end()) apply(tree.layers()[at->second]);
            }
        };
        each("ON", [](Layer& l) { l.defaultOn = true; });
        each("OFF", [](Layer& l) { l.defaultOn = false; });
        each("Locked", [](Layer& l) { l.locked = true; });
        for (const CosObject& group : itemsOf(table, defaults.get("RBGroups"))) {
            std::vector<LayerId> members;
            for (const CosObject& item : itemsOf(table, group)) {
                if (!item.isReference()) continue;
                const auto found = groups.find(item.reference().number);
                if (found != groups.end() && std::find(members.begin(), members.end(), found->second) == members.end())
                    members.push_back(found->second);
            }
            if (members.size() >= 2) tree.radioGroups().push_back(std::move(members));
        }
        for (const CosObject& entry : itemsOf(table, defaults.get("AS"))) {
            const CosObject application = resolved(table, entry);
            const CosObject& event = application.get("Event");
            if (!event.isName()) continue;
            const std::vector<std::string> categories = namesIn(resolved(table, application.get("Category")));
            // A group's declaration applies at an event when the event's own category is among those applied.
            if (!hasName(categories, event.text())) continue;
            for (const CosObject& item : itemsOf(table, application.get("OCGs"))) {
                if (!item.isReference()) continue;
                const auto at = indexOf.find(item.reference().number);
                if (at == indexOf.end()) continue;
                Layer& layer = tree.layers()[at->second];
                if (event.text() == "View") layer.autoView = true;
                else if (event.text() == "Print") layer.autoPrint = true;
                else if (event.text() == "Export") layer.autoExport = true;
            }
        }
        OrderWalk walk{table, groups, {}, {}, 0, std::nullopt};
        CosObject order = defaults.get("Order");
        std::vector<CosObject> top;
        if (order.isReference()) {
            walk.path.insert(order.reference().number);
            order = resolved(table, order);
        }
        if (order.isArray()) top = order.items();
        tree.order() = walk.items(top, 0, 1);
        if (walk.refusal && !refusal) refusal = walk.refusal;
    }
    if (refusal) tree.setStatus(*refusal);
    return tree;
}

// =====================================================================================================================
// Compose
// =====================================================================================================================

CosObject LayerOps::groupDictionary(const Layer& layer, const CosObject& base)
{
    CosObject out = base.isDictionary() ? base : CosObject::makeDictionary({});
    out = AnnotationOps::withEntry(out, "Type", CosObject::makeName("OCG"));
    out = AnnotationOps::withEntry(out, "Name", AnnotationOps::textString(layer.name));
    CosObject usage = out.get("Usage").isDictionary() ? out.get("Usage") : CosObject::makeDictionary({});
    const bool hadUsage = out.find("Usage") != nullptr;
    const auto set = [&usage](const char* category, LayerUsage state) {
        CosObject entry = usage.get(category).isDictionary() ? usage.get(category) : CosObject::makeDictionary({});
        if (state == LayerUsage::Unset) entry = AnnotationOps::withoutEntry(entry, stateKey(category));
        else entry = AnnotationOps::withEntry(entry, stateKey(category), CosObject::makeName(state == LayerUsage::On ? "ON" : "OFF"));
        usage = entry.entries().empty() ? AnnotationOps::withoutEntry(usage, category) : AnnotationOps::withEntry(usage, category, entry);
    };
    set("Print", layer.print);
    set("View", layer.view);
    set("Export", layer.exportUsage);
    if (!usage.entries().empty()) out = AnnotationOps::withEntry(out, "Usage", usage);
    else if (hadUsage) out = AnnotationOps::withoutEntry(out, "Usage");
    return out;
}

core::Result<LayerOps::Composed> LayerOps::compose(cos::XrefTable& table, const CosObject& properties, const CosObject& defaults,
                                                   const LayerTree& model, const std::set<long long>& removed, const RefOf& refOf)
{
    // The model's references, a created group's new number among them.
    std::map<LayerId, CosObject> refs;
    std::map<long long, const Layer*> byNumber;
    std::vector<CosObject> groups;
    for (const Layer& layer : model.layers()) {
        std::optional<CosObject> ref = refOf(layer.id);
        if (!ref || !ref->isReference())
            return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.layer.notWritten"), layer.id.toString()};
        refs.emplace(layer.id, *ref);
        byNumber.emplace(ref->reference().number, &layer);
        groups.push_back(*ref);
    }
    // WHAT THE MODEL DOES NOT KNOW, kept (see the header): listed now, neither the model's nor removed.
    std::set<long long> unknown;
    for (const CosObject& item : itemsOf(table, properties.get("OCGs"))) {
        if (!item.isReference()) continue;
        const long long number = item.reference().number;
        if (byNumber.count(number) != 0 || removed.count(number) != 0 || !unknown.insert(number).second) continue;
        groups.push_back(item);
    }
    const auto isUnknown = [&unknown](const CosObject& item) { return item.isReference() && unknown.count(item.reference().number) != 0; };
    const auto unknownOf = [&](const char* key) {
        std::vector<CosObject> out;
        for (const CosObject& item : itemsOf(table, defaults.get(key))) {
            if (isUnknown(item)) out.push_back(item);
        }
        return out;
    };
    const auto refsWhere = [&](const std::function<bool(const Layer&)>& want) {
        std::vector<CosObject> out;
        for (const Layer& layer : model.layers()) {
            if (want(layer)) out.push_back(refs.at(layer.id));
        }
        return out;
    };
    const auto append = [](std::vector<CosObject> head, std::vector<CosObject> tail) {
        for (CosObject& item : tail) head.push_back(std::move(item));
        return head;
    };

    CosObject d = defaults.isDictionary() ? defaults : CosObject::makeDictionary({});

    // `/Order`: the model's, then the top-level entries of groups it does not know, each with its children's array.
    std::vector<CosObject> order;
    writeOrder(model.order(), refs, order);
    std::set<long long> known;
    for (const auto& [number, layer] : byNumber) known.insert(number);
    known.insert(removed.begin(), removed.end());
    const std::vector<CosObject> was = resolved(table, defaults.get("Order")).items();
    for (std::size_t i = 0; i < was.size(); ++i) {
        const CosObject& item = was[i];
        if (isUnknown(item)) {
            order.push_back(item);
            if (i + 1 < was.size()) {
                const CosObject next = resolved(table, was[i + 1]);
                const CosObject first = next.items().empty() ? CosObject::makeNull() : resolved(table, next.items().front());
                if (next.isArray() && !first.isString() && !namesAny(next, known)) {
                    order.push_back(was[i + 1]);
                    ++i;
                }
            }
            continue;
        }
        const CosObject array = resolved(table, item);
        if (array.isArray() && namesAny(array, unknown) && !namesAny(array, known)) order.push_back(item);
    }
    d = withList(d, "Order", std::move(order), defaults.find("Order") != nullptr);

    // The default state, as `/BaseState` reads it.
    if (model.baseOff()) {
        d = withList(d, "ON", append(refsWhere([](const Layer& l) { return l.defaultOn; }), unknownOf("ON")));
        d = withList(d, "OFF", unknownOf("OFF"));
    } else {
        d = withList(d, "OFF", append(refsWhere([](const Layer& l) { return !l.defaultOn; }), unknownOf("OFF")));
        d = withList(d, "ON", unknownOf("ON"));
    }
    d = withList(d, "Locked", append(refsWhere([](const Layer& l) { return l.locked; }), unknownOf("Locked")));

    std::vector<CosObject> radios;
    for (const std::vector<LayerId>& group : model.radioGroups()) {
        std::vector<CosObject> members;
        for (const LayerId& id : group) {
            if (const auto ref = refs.find(id); ref != refs.end()) members.push_back(ref->second);
        }
        if (members.size() >= 2) radios.push_back(CosObject::makeArray(std::move(members)));
    }
    for (const CosObject& group : itemsOf(table, defaults.get("RBGroups"))) {
        const CosObject members = resolved(table, group);
        if (!members.items().empty() && std::all_of(members.items().begin(), members.items().end(), isUnknown)) radios.push_back(group);
    }
    d = withList(d, "RBGroups", std::move(radios));

    // `/AS`: each application as it was, a removed group out of it, a model group in it exactly when the model applies
    // its usage at that event (where the application applies the event's own category); then each group applied and in
    // none, added to the event's first such application, or to a new one.
    struct Event {
        const char* name;
        bool Layer::*applied;
    };
    const Event events[] = {{"View", &Layer::autoView}, {"Print", &Layer::autoPrint}, {"Export", &Layer::autoExport}};
    std::vector<CosObject> applications;
    std::map<std::string, std::set<long long>> covered;
    std::map<std::string, std::size_t> firstOwn;
    for (const CosObject& entry : itemsOf(table, defaults.get("AS"))) {
        CosObject application = resolved(table, entry);
        if (!application.isDictionary()) continue;
        const CosObject& event = application.get("Event");
        const std::string name = event.isName() ? event.text() : std::string();
        const bool own = !name.empty() && hasName(namesIn(resolved(table, application.get("Category"))), name);
        const Event* matched = nullptr;
        for (const Event& e : events) {
            if (name == e.name) matched = &e;
        }
        std::vector<CosObject> members;
        for (const CosObject& item : itemsOf(table, application.get("OCGs"))) {
            if (!item.isReference()) {
                members.push_back(item);
                continue;
            }
            const long long number = item.reference().number;
            if (removed.count(number) != 0) continue;
            if (const auto layer = byNumber.find(number); layer != byNumber.end() && own && matched != nullptr) {
                if (!(layer->second->*(matched->applied))) continue;
                covered[name].insert(number);
            }
            members.push_back(item);
        }
        if (members.empty()) continue;
        if (own && matched != nullptr && firstOwn.count(name) == 0) firstOwn.emplace(name, applications.size());
        applications.push_back(AnnotationOps::withEntry(application, "OCGs", CosObject::makeArray(std::move(members))));
    }
    for (const Event& e : events) {
        std::vector<CosObject> missing;
        for (const Layer& layer : model.layers()) {
            const long long number = refs.at(layer.id).reference().number;
            if (layer.*(e.applied) && covered[e.name].count(number) == 0) missing.push_back(refs.at(layer.id));
        }
        if (missing.empty()) continue;
        if (const auto at = firstOwn.find(e.name); at != firstOwn.end()) {
            CosObject& application = applications[at->second];
            std::vector<CosObject> members = application.get("OCGs").items();
            for (CosObject& item : missing) members.push_back(std::move(item));
            application = AnnotationOps::withEntry(application, "OCGs", CosObject::makeArray(std::move(members)));
        } else {
            applications.push_back(CosObject::makeDictionary({{"Event", CosObject::makeName(e.name)},
                                                              {"OCGs", CosObject::makeArray(std::move(missing))},
                                                              {"Category", CosObject::makeArray({CosObject::makeName(e.name)})}}));
        }
    }
    d = withList(d, "AS", std::move(applications));

    return Composed{CosObject::makeArray(std::move(groups)), std::move(d)};
}

// =====================================================================================================================
// Merge
// =====================================================================================================================

std::optional<CosObject> LayerOps::repointed(const CosObject& value, const std::map<long long, CosObject>& into)
{
    if (value.isReference()) {
        const auto found = into.find(value.reference().number);
        if (found == into.end()) return std::nullopt;
        return found->second;
    }
    if (value.isArray()) {
        std::vector<CosObject> items = value.items();
        bool changed = false;
        for (CosObject& item : items) {
            if (std::optional<CosObject> next = repointed(item, into)) {
                item = std::move(*next);
                changed = true;
            }
        }
        if (!changed) return std::nullopt;
        return CosObject::makeArray(std::move(items));
    }
    if (value.isDictionary()) {
        std::vector<CosObject::Entry> entries = value.entries();
        bool changed = false;
        for (CosObject::Entry& entry : entries) {
            if (std::optional<CosObject> next = repointed(entry.value, into)) {
                entry.value = std::move(*next);
                changed = true;
            }
        }
        if (!changed) return std::nullopt;
        if (value.isStream()) return CosObject::makeStream(std::move(entries), value.streamData());
        return CosObject::makeDictionary(std::move(entries));
    }
    return std::nullopt;
}

std::set<long long> LayerOps::referencing(cos::XrefTable& table, const std::set<long long>& groups)
{
    std::set<long long> out;
    if (groups.empty()) return out;
    std::vector<long long> numbers;
    numbers.reserve(table.entries().size());
    for (const auto& [number, entry] : table.entries()) {
        if (entry.type != cos::XrefEntryType::Free && groups.count(number) == 0) numbers.push_back(number);
    }
    for (const long long number : numbers) {
        auto fetched = table.fetch(number);
        if (!fetched) continue;
        if (namesAny(fetched.value().object, groups)) out.insert(number);
    }
    return out;
}

// =====================================================================================================================
// View
// =====================================================================================================================

core::Result<QByteArray> LayerOps::viewSection(std::string_view composed, const std::vector<long long>& hidden, bool protect)
{
    auto opened = cos::openWithRepair(composed);
    if (!opened) return opened.error();
    cos::XrefTable& table = opened.value().table;
    const CosObject& root = table.trailer().get("Root");
    if (!root.isReference()) return QByteArray();
    const CosObject catalog = resolved(table, root);
    if (!catalog.isDictionary()) return QByteArray();
    const CosObject* held = catalog.find("OCProperties");
    if (held == nullptr) return QByteArray();
    const CosObject properties = resolved(table, *held);
    if (!properties.isDictionary()) return QByteArray();

    // Only groups the file lists, each by the reference it is listed with.
    std::map<long long, CosObject> listed;
    for (const CosObject& item : itemsOf(table, properties.get("OCGs"))) {
        if (item.isReference()) listed.emplace(item.reference().number, item);
    }
    std::vector<CosObject> off;
    for (const long long number : hidden) {
        if (const auto ref = listed.find(number); ref != listed.end()) off.push_back(ref->second);
    }
    CosObject d = resolved(table, properties.get("D"));
    if (!d.isDictionary()) d = CosObject::makeDictionary({});
    d = AnnotationOps::withEntry(d, "BaseState", CosObject::makeName("ON"));
    d = AnnotationOps::withoutEntry(d, "ON");
    d = withList(d, "OFF", std::move(off));
    std::vector<CosObject> applications;
    for (const CosObject& entry : itemsOf(table, d.get("AS"))) {
        const CosObject application = resolved(table, entry);
        const CosObject& event = application.get("Event");
        if (event.isName() && event.text() == "View") continue;
        applications.push_back(entry);
    }
    d = withList(d, "AS", std::move(applications));

    cos::ObjectDelta delta(table);
    if (protect) cos::IncrementalWriter::protectOriginal(table, delta);
    // A GROUP'S OWN SCREEN STATE GOES TOO. PDFium reads a group's `/Usage /View /ViewState` BEFORE the configuration --
    // whether or not `/AS` applies it (measured at T17: a print-only group shown through `/D` alone stayed undrawn) -- so
    // a group that declares one is written again in this section without it, and the configuration above decides.
    for (const auto& [number, ref] : listed) {
        const CosObject group = resolved(table, ref);
        const CosObject usage = resolved(table, group.get("Usage"));
        if (!usage.isDictionary() || usage.find("View") == nullptr) continue;
        if (delta.touches(number)) continue;
        const CosObject rest = AnnotationOps::withoutEntry(usage, "View");
        delta.replace(number, rest.entries().empty() ? AnnotationOps::withoutEntry(group, "Usage") : AnnotationOps::withEntry(group, "Usage", rest));
    }
    const CosObject& dHeld = properties.get("D");
    if (dHeld.isReference()) {
        if (delta.touches(dHeld.reference().number)) return QByteArray();
        delta.replace(dHeld.reference().number, std::move(d));
    } else if (held->isReference()) {
        if (delta.touches(held->reference().number)) return QByteArray();
        delta.replace(held->reference().number, AnnotationOps::withEntry(properties, "D", std::move(d)));
    } else {
        CosObject next = AnnotationOps::withEntry(catalog, "OCProperties", AnnotationOps::withEntry(properties, "D", std::move(d)));
        if (delta.touches(root.reference().number)) return QByteArray();
        delta.replace(root.reference().number, std::move(next));
    }
    auto written = opened.value().usedOriginalXref ? cos::IncrementalWriter::append(composed, table, delta)
                                                     : cos::IncrementalWriter::appendComplete(composed, table, delta);
    if (!written) return written.error();
    return QByteArray(written.value().bytes.data(), static_cast<qsizetype>(written.value().bytes.size()));
}

} // namespace rawpage::pdf::ops
