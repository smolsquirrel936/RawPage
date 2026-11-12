#include "pdf/ops/DestinationReader.h"

#include "pdf/ops/EmbeddedFileReader.h"
#include "pdf/ops/OutlineReader.h"

#include <set>

namespace rawpage::pdf::ops {

using cos::CosObject;
using core::NamedDestination;

namespace {

constexpr int kMaxResolve = 8;   // references and `/D`s followed to reach one array

// A value as the explicit array it stands for -- through references and a dictionary's `/D` -- or none.
std::optional<CosObject> explicitArray(const ReferenceRepair::View& view, const CosObject& value, int depth = 0)
{
    if (depth > kMaxResolve) return std::nullopt;
    if (value.isReference()) {
        const std::optional<CosObject> resolved = view.resolve(value);
        return resolved ? explicitArray(view, *resolved, depth + 1) : std::nullopt;
    }
    if (value.isArray()) return value;
    if (value.isDictionary()) return explicitArray(view, value.get("D"), depth + 1);
    return std::nullopt;
}

DestinationReader::Entry entryOf(const ReferenceRepair::View& view, NamedDestination::Form form, std::string key,
                                 CosObject keyObject, CosObject value)
{
    DestinationReader::Entry entry;
    entry.form = form;
    entry.key = std::move(key);
    entry.keyObject = std::move(keyObject);
    entry.value = std::move(value);
    if (const std::optional<CosObject> array = explicitArray(view, entry.value); array && !array->items().empty()) {
        entry.view = OutlineReader::viewOf(*array);
        entry.view.pageIndex = -1;
        const CosObject& first = array->items().front();
        if (first.isReference()) entry.page = first.reference().number;
        else if (first.isInteger()) entry.index = static_cast<int>(first.integerValue());
    }
    return entry;
}

} // namespace

DestinationReader::Reading DestinationReader::read(const ReferenceRepair::View& view)
{
    Reading reading;
    const long long catalogNumber = view.catalogNumber();
    if (catalogNumber < 0) return reading;
    const std::optional<CosObject> catalog = view.object(catalogNumber);
    if (!catalog || !catalog->isDictionary()) return reading;
    const auto full = [&reading] {
        if (reading.entries.size() < core::kMaxNamedDestinations) return false;
        reading.truncated = true;
        return true;
    };

    // The name tree: every node depth first, a leaf's pairs in order -- the order PDFium counts them in.
    if (const std::optional<CosObject> names = view.resolve(catalog->get("Names")); names && names->isDictionary()) {
        const CosObject& root = names->get("Dests");
        std::vector<std::pair<long long, CosObject>> stack;
        if (root.isReference()) {
            if (std::optional<CosObject> value = view.object(root.reference().number))
                stack.emplace_back(root.reference().number, std::move(*value));
            else
                reading.truncated = true;
        } else if (root.isDictionary()) {
            stack.emplace_back(-1, root);
        }
        std::set<long long> visited;
        while (!stack.empty() && !full()) {
            auto [holder, node] = std::move(stack.back());
            stack.pop_back();
            if (holder >= 0 && !visited.insert(holder).second) {
                reading.truncated = true;   // a node reached twice: a cycle, or a shared kid -- not a tree
                continue;
            }
            if (!node.isDictionary()) continue;
            if (const std::optional<CosObject> pairs = view.resolve(node.get("Names")); pairs && pairs->isArray()) {
                const std::vector<CosObject>& items = pairs->items();
                for (std::size_t at = 0; at + 1 < items.size() && !full(); at += 2) {
                    if (!items[at].isString()) continue;   // not a key PDFium reads
                    reading.entries.push_back(entryOf(view, NamedDestination::Form::NameTree, items[at].text(), items[at],
                                                      items[at + 1]));
                }
            }
            const std::optional<CosObject> kids = view.resolve(node.get("Kids"));
            if (!kids || !kids->isArray()) continue;
            for (auto kid = kids->items().rbegin(); kid != kids->items().rend(); ++kid) {
                if (!kid->isReference()) continue;
                if (std::optional<CosObject> value = view.object(kid->reference().number))
                    stack.emplace_back(kid->reference().number, std::move(*value));
                else
                    reading.truncated = true;
            }
        }
    }

    // The catalog's `/Dests` dictionary, in its own order.
    if (const std::optional<CosObject> dests = view.resolve(catalog->get("Dests")); dests && dests->isDictionary()) {
        for (const CosObject::Entry& pair : dests->entries()) {
            if (full()) break;
            reading.entries.push_back(entryOf(view, NamedDestination::Form::Dictionary, pair.key,
                                              CosObject::makeName(pair.key), pair.value));
        }
    }
    return reading;
}

QString DestinationReader::nameOf(const Entry& entry)
{
    if (entry.form == NamedDestination::Form::NameTree) return EmbeddedFileReader::decodeTextString(entry.key);
    return QString::fromLatin1(entry.key.data(), static_cast<qsizetype>(entry.key.size()));
}

core::Destinations DestinationReader::model(const Reading& reading, const std::map<long long, int>& indexOf)
{
    core::Destinations table;
    for (const Entry& entry : reading.entries) {
        NamedDestination named;
        named.name = nameOf(entry);
        named.form = entry.form;
        named.destination = entry.view;
        if (entry.page >= 0) {
            const auto placed = indexOf.find(entry.page);
            named.destination.pageIndex = placed == indexOf.end() ? -1 : placed->second;
        } else {
            named.destination.pageIndex = entry.index;
        }
        table.add(std::move(named));
    }
    return table;
}

core::Status DestinationReader::agreesWith(const core::Destinations& engine, const core::Destinations& resolved)
{
    const auto disagree = [](const QString& name, const char* how) {
        return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.destination.readingsDisagree"),
                           name + QStringLiteral(": ") + QLatin1String(how)};
    };
    if (engine.size() != resolved.size())
        return disagree(QString::number(static_cast<qulonglong>(resolved.size())), "count");
    // Each name ONCE -- a name in both tables, or twice in one, is a table the commands cannot name an entry of.
    std::map<QString, int> pages;
    for (const NamedDestination& entry : resolved.all()) {
        if (!pages.emplace(entry.name, entry.destination.pageIndex).second) return disagree(entry.name, "duplicate");
    }
    for (const NamedDestination& entry : engine.all()) {
        const auto found = pages.find(entry.name);
        if (found == pages.end()) return disagree(entry.name, "name");
        if (found->second != entry.destination.pageIndex) return disagree(entry.name, "page");
    }
    return core::Status{};
}

} // namespace rawpage::pdf::ops
