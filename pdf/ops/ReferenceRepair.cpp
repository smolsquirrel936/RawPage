// pdf/ops/ReferenceRepair -- implementation. Stage 7 T2. See the header for what is repaired and what never is.

#include "pdf/ops/ReferenceRepair.h"

#include "pdf/cos/XrefTable.h"
#include "pdf/ops/AnnotationOps.h"
#include "pdf/ops/EmbeddedFileReader.h"

#include <algorithm>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;
using Kind = core::PageReference::Kind;
using Treatment = core::PageReference::Treatment;

constexpr int kMaxDepth = 8;   // a destination behind references behind a /D: deeper is not a destination

struct Pointed {
    ReferenceRepair::Target target = ReferenceRepair::Target::None;
    long long page = -1;
    std::string name;
    int index = -1;
};

Pointed destinationOf(const ReferenceRepair::View& view, const CosObject& value, int depth = 0)
{
    Pointed out;
    if (depth > kMaxDepth) return out;
    if (value.isReference()) {
        const std::optional<CosObject> resolved = view.resolve(value);
        return resolved ? destinationOf(view, *resolved, depth + 1) : out;
    }
    if (value.isArray()) {
        if (value.items().empty()) return out;
        const CosObject& first = value.items().front();
        if (first.isReference()) {
            out.target = ReferenceRepair::Target::Page;
            out.page = first.reference().number;
        } else if (first.isInteger()) {
            out.target = ReferenceRepair::Target::Index;
            out.index = static_cast<int>(first.integerValue());
        }
        return out;
    }
    if (value.isDictionary()) return destinationOf(view, value.get("D"), depth + 1);
    if (value.isName() || value.isString()) {
        out.target = ReferenceRepair::Target::Named;
        out.name = value.text();
    }
    return out;
}

Pointed actionOf(const ReferenceRepair::View& view, const CosObject& value)
{
    Pointed out;
    const std::optional<CosObject> action = view.resolve(value);
    if (!action || !action->isDictionary()) return out;
    const CosObject& kind = action->get("S");
    if (!kind.isName()) return out;
    if (kind.text() == "GoTo") return destinationOf(view, action->get("D"));
    if (kind.text() == "GoToR") {
        out.target = ReferenceRepair::Target::Remote;
        const Pointed inner = destinationOf(view, action->get("D"));
        out.index = inner.target == ReferenceRepair::Target::Index ? inner.index : -1;
    }
    return out;
}

// An outline item's or a link's target: `/Dest` first, as ISO 32000-1 §12.3.3 has it, else `/A`.
Pointed holderTarget(const ReferenceRepair::View& view, const CosObject& holder)
{
    if (const CosObject* dest = holder.find("Dest")) return destinationOf(view, *dest);
    if (const CosObject* action = holder.find("A")) return actionOf(view, *action);
    return {};
}

bool hits(const Pointed& at, const std::set<long long>& gone, const std::map<std::string, long long>& named)
{
    if (at.target == ReferenceRepair::Target::Page) return gone.count(at.page) != 0;
    if (at.target == ReferenceRepair::Target::Named) {
        const auto found = named.find(at.name);
        return found != named.end() && gone.count(found->second) != 0;
    }
    return false;
}

QString words(const CosObject& value)
{
    if (value.isString()) return EmbeddedFileReader::decodeTextString(value.text());
    if (value.isName()) return QString::fromStdString(value.text());
    return {};
}

long long referenceNumber(const CosObject& value)
{
    return value.isReference() ? value.reference().number : -1;
}

ReferenceRepair::Record record(Kind kind, long long holder, const Pointed& at, QString text)
{
    ReferenceRepair::Record r;
    r.kind = kind;
    r.holder = holder;
    r.target = at.target;
    r.page = at.page;
    r.name = at.name;
    r.index = at.index;
    r.words = std::move(text);
    return r;
}

// ------------------------------------------------------------------------------------------
// The walks, shared by the scan and the compose. Each visits through the view and calls back.
// ------------------------------------------------------------------------------------------

// Every node of the `/Names /Dests` tree that holds pairs, as (holder object, the node's value). A DIRECT
// root (`/Names << /Dests << /Names [...] >> >>`) is reported with holder -1: the compose rewrites it in
// the object that contains it.
struct NameNode {
    long long holder = -1;
    CosObject node;
};

std::vector<NameNode> nameTreeLeaves(const ReferenceRepair::View& view, const CosObject& catalog,
                                     long long* rootHolder, CosObject* rootValue)
{
    std::vector<NameNode> out;
    const std::optional<CosObject> names = view.resolve(catalog.get("Names"));
    if (!names || !names->isDictionary()) return out;
    const CosObject& root = names->get("Dests");
    *rootHolder = referenceNumber(root);
    std::vector<std::pair<long long, CosObject>> stack;
    if (root.isReference()) {
        if (std::optional<CosObject> value = view.object(root.reference().number))
            stack.emplace_back(root.reference().number, std::move(*value));
    } else if (root.isDictionary()) {
        *rootValue = root;
        stack.emplace_back(-1, root);
    }
    std::set<long long> visited;
    while (!stack.empty() && out.size() < ReferenceRepair::kMaxReferences) {
        auto [holder, node] = std::move(stack.back());
        stack.pop_back();
        if (holder >= 0 && !visited.insert(holder).second) continue;
        if (!node.isDictionary()) continue;
        if (node.get("Names").isArray()) out.push_back(NameNode{holder, node});
        const CosObject& kids = node.get("Kids");
        if (!kids.isArray()) continue;
        for (auto kid = kids.items().rbegin(); kid != kids.items().rend(); ++kid) {
            if (!kid->isReference()) continue;
            if (std::optional<CosObject> value = view.object(kid->reference().number))
                stack.emplace_back(kid->reference().number, std::move(*value));
        }
    }
    return out;
}

// The outline's items, in document order, each (object, value). Iterative, visited, capped.
std::vector<std::pair<long long, CosObject>> outlineItems(const ReferenceRepair::View& view, const CosObject& catalog)
{
    std::vector<std::pair<long long, CosObject>> out;
    const std::optional<CosObject> outlines = view.resolve(catalog.get("Outlines"));
    if (!outlines || !outlines->isDictionary()) return out;
    std::vector<long long> stack;
    if (const long long first = referenceNumber(outlines->get("First")); first >= 0) stack.push_back(first);
    std::set<long long> visited;
    while (!stack.empty() && out.size() < ReferenceRepair::kMaxReferences) {
        const long long number = stack.back();
        stack.pop_back();
        if (!visited.insert(number).second) continue;
        std::optional<CosObject> item = view.object(number);
        if (!item || !item->isDictionary()) continue;
        // Its next sibling after all of its children: pushed first, so it pops last.
        if (const long long next = referenceNumber(item->get("Next")); next >= 0) stack.push_back(next);
        if (const long long child = referenceNumber(item->get("First")); child >= 0) stack.push_back(child);
        out.emplace_back(number, std::move(*item));
    }
    return out;
}

// A structure element's content item: what page it is on (`/Pg` of its own, else the element's), and
// whether it is a child ELEMENT (kept, and walked) rather than marked content or an object reference.
struct StructItem {
    bool element = false;
    long long child = -1;   // a child element's object
    long long page = -1;    // the page its content is on, or -1
};

StructItem structItem(const ReferenceRepair::View& view, const CosObject& item, long long elementPage)
{
    StructItem out;
    if (item.isInteger()) {
        out.page = elementPage;
        return out;
    }
    std::optional<CosObject> value = item;
    if (item.isReference()) value = view.object(item.reference().number);
    if (!value || !value->isDictionary()) {
        out.element = true;   // unreadable: kept as written, walked as nothing
        return out;
    }
    const CosObject& type = value->get("Type");
    const bool contentItem = type.isName() && (type.text() == "MCR" || type.text() == "OBJR");
    if (!contentItem) {
        out.element = true;
        out.child = referenceNumber(item);
        return out;
    }
    const long long own = referenceNumber(value->get("Pg"));
    out.page = own >= 0 ? own : elementPage;
    return out;
}

// The catalog's `/Threads`, read through the view: (holder of the array -- the catalog when it is direct --
// and the array).
std::pair<long long, CosObject> threadList(const ReferenceRepair::View& view, const CosObject& catalog,
                                           long long catalogNumber)
{
    const CosObject& threads = catalog.get("Threads");
    if (threads.isReference()) {
        if (std::optional<CosObject> array = view.object(threads.reference().number); array && array->isArray())
            return {threads.reference().number, *array};
        return {-1, CosObject::makeNull()};
    }
    if (threads.isArray()) return {catalogNumber, threads};
    return {-1, CosObject::makeNull()};
}

// One thread's beads in ring order from `/F`, following `/N`; visited, capped.
std::vector<std::pair<long long, CosObject>> beadsOf(const ReferenceRepair::View& view, const CosObject& thread)
{
    std::vector<std::pair<long long, CosObject>> beads;
    long long at = referenceNumber(thread.get("F"));
    std::set<long long> visited;
    while (at >= 0 && visited.insert(at).second && beads.size() < ReferenceRepair::kMaxReferences) {
        std::optional<CosObject> bead = view.object(at);
        if (!bead || !bead->isDictionary()) break;
        const long long next = referenceNumber(bead->get("N"));
        beads.emplace_back(at, std::move(*bead));
        at = next;
    }
    return beads;
}

QString threadTitle(const ReferenceRepair::View& view, const CosObject& thread)
{
    const std::optional<CosObject> info = view.resolve(thread.get("I"));
    return info && info->isDictionary() ? words(info->get("Title")) : QString();
}

} // namespace

// ==========================================================================================
// View
// ==========================================================================================

std::optional<CosObject> ReferenceRepair::View::object(long long number) const
{
    auto fetched = table_.fetch(number);
    if (!fetched) return std::nullopt;
    CosObject out = fetched.value().object;
    out.clearSpansRecursively();
    const auto held = ledger_.find(number);
    if (held == ledger_.end()) return out;
    for (const auto& [key, value] : held->second) {
        if (key.empty()) {
            if (value) out = *value;
            continue;
        }
        if (!out.isDictionary()) continue;
        out = value ? AnnotationOps::withEntry(out, key, *value) : AnnotationOps::withoutEntry(out, key);
    }
    return out;
}

std::optional<CosObject> ReferenceRepair::View::resolve(const CosObject& value) const
{
    if (value.isReference()) return object(value.reference().number);
    return value;
}

long long ReferenceRepair::View::catalogNumber() const
{
    return referenceNumber(table_.trailer().get("Root"));
}

// Stage 7 T5: a destination as the explicit array it is -- through references and a dictionary's `/D` -- or nullopt.
static std::optional<CosObject> explicitArray(const ReferenceRepair::View& view, const CosObject& value, int depth = 0)
{
    if (depth > kMaxDepth) return std::nullopt;
    if (value.isReference()) {
        const std::optional<CosObject> resolved = view.resolve(value);
        return resolved ? explicitArray(view, *resolved, depth + 1) : std::nullopt;
    }
    if (value.isArray()) return value;
    if (value.isDictionary()) return explicitArray(view, value.get("D"), depth + 1);
    return std::nullopt;
}

std::map<std::string, CosObject> ReferenceRepair::namedArrays(const View& view)
{
    std::map<std::string, CosObject> out;
    const long long catalogNumber = view.catalogNumber();
    const std::optional<CosObject> catalog = catalogNumber >= 0 ? view.object(catalogNumber) : std::nullopt;
    if (!catalog || !catalog->isDictionary()) return out;
    long long rootHolder = -1;
    CosObject rootValue;
    // The same two places, in the same order, as `scan` -- so a name both define means what it means there.
    for (const NameNode& leaf : nameTreeLeaves(view, *catalog, &rootHolder, &rootValue)) {
        const std::vector<CosObject>& pairs = leaf.node.get("Names").items();
        for (std::size_t at = 0; at + 1 < pairs.size() && out.size() < kMaxReferences; at += 2) {
            if (!pairs[at].isString()) continue;
            if (std::optional<CosObject> array = explicitArray(view, pairs[at + 1])) out.emplace(pairs[at].text(), std::move(*array));
        }
    }
    if (const std::optional<CosObject> dests = view.resolve(catalog->get("Dests")); dests && dests->isDictionary()) {
        for (const CosObject::Entry& entry : dests->entries()) {
            if (out.size() >= kMaxReferences) break;
            if (std::optional<CosObject> array = explicitArray(view, entry.value)) out.emplace(entry.key, std::move(*array));
        }
    }
    return out;
}

// ==========================================================================================
// Scan
// ==========================================================================================

ReferenceRepair::Scan ReferenceRepair::scan(const View& view, const std::vector<long long>& pages)
{
    Scan out;
    const long long catalogNumber = view.catalogNumber();
    const std::optional<CosObject> catalog = catalogNumber >= 0 ? view.object(catalogNumber) : std::nullopt;
    if (!catalog || !catalog->isDictionary()) return out;

    // Named destinations first: every other record that names one is resolved through them.
    long long rootHolder = -1;
    CosObject rootValue;
    for (const NameNode& leaf : nameTreeLeaves(view, *catalog, &rootHolder, &rootValue)) {
        const std::vector<CosObject>& pairs = leaf.node.get("Names").items();
        for (std::size_t at = 0; at + 1 < pairs.size(); at += 2) {
            if (!pairs[at].isString()) continue;
            const Pointed to = destinationOf(view, pairs[at + 1]);
            out.named.emplace(pairs[at].text(), to.target == Target::Page ? to.page : -1);
            Record r = record(Kind::NamedDestination, leaf.holder, to, words(pairs[at]));
            r.name = pairs[at].text();
            out.records.push_back(std::move(r));
        }
    }
    if (const std::optional<CosObject> dests = view.resolve(catalog->get("Dests")); dests && dests->isDictionary()) {
        const long long holder = catalog->get("Dests").isReference() ? referenceNumber(catalog->get("Dests")) : catalogNumber;
        for (const CosObject::Entry& entry : dests->entries()) {
            const Pointed to = destinationOf(view, entry.value);
            out.named.emplace(entry.key, to.target == Target::Page ? to.page : -1);
            Record r = record(Kind::NamedDestination, holder, to, QString::fromStdString(entry.key));
            r.name = entry.key;
            out.records.push_back(std::move(r));
        }
    }

    for (const auto& [number, item] : outlineItems(view, *catalog))
        out.records.push_back(record(Kind::OutlineItem, number, holderTarget(view, item), words(item.get("Title"))));

    for (const long long page : pages) {
        const std::optional<CosObject> dictionary = view.object(page);
        if (!dictionary || !dictionary->isDictionary()) continue;
        const std::optional<CosObject> annots = view.resolve(dictionary->get("Annots"));
        if (!annots || !annots->isArray()) continue;
        for (const CosObject& entry : annots->items()) {
            if (!entry.isReference()) continue;
            const std::optional<CosObject> annotation = view.object(entry.reference().number);
            if (!annotation || !annotation->isDictionary()) continue;
            const CosObject& subtype = annotation->get("Subtype");
            if (!subtype.isName() || subtype.text() != "Link") continue;
            Record r = record(Kind::Link, entry.reference().number, holderTarget(view, *annotation), QString());
            r.onPage = page;
            out.records.push_back(std::move(r));
            if (out.records.size() >= kMaxReferences) return out;
        }
    }

    // The structure tree: an element per /Pg, and one more per content item on a page of its own.
    if (const std::optional<CosObject> root = view.resolve(catalog->get("StructTreeRoot")); root && root->isDictionary()) {
        std::vector<long long> stack;
        const auto pushChildren = [&](const CosObject& kids, long long elementPage) {
            std::optional<CosObject> resolved = kids;
            if (kids.isReference()) resolved = view.object(kids.reference().number);
            if (!resolved) return;
            const std::vector<CosObject> items = resolved->isArray() ? resolved->items() : std::vector<CosObject>{kids};
            for (auto item = items.rbegin(); item != items.rend(); ++item) {
                const StructItem si = structItem(view, *item, elementPage);
                if (si.element && si.child >= 0) stack.push_back(si.child);
            }
        };
        pushChildren(root->get("K"), -1);
        std::set<long long> visited;
        while (!stack.empty() && out.records.size() < kMaxReferences) {
            const long long number = stack.back();
            stack.pop_back();
            if (!visited.insert(number).second) continue;
            const std::optional<CosObject> element = view.object(number);
            if (!element || !element->isDictionary()) continue;
            const long long page = referenceNumber(element->get("Pg"));
            const QString type = words(element->get("S"));
            if (page >= 0) out.records.push_back(record(Kind::StructureElement, number, Pointed{Target::Page, page, {}, -1}, type));
            std::optional<CosObject> kids = view.resolve(element->get("K"));
            if (kids) {
                const std::vector<CosObject> items = kids->isArray() ? kids->items() : std::vector<CosObject>{element->get("K")};
                for (const CosObject& item : items) {
                    const StructItem si = structItem(view, item, page);
                    if (!si.element && si.page >= 0 && si.page != page)
                        out.records.push_back(record(Kind::StructureElement, number, Pointed{Target::Page, si.page, {}, -1}, type));
                }
            }
            pushChildren(element->get("K"), page);
        }
    }

    const auto [threadsHolder, threads] = threadList(view, *catalog, catalogNumber);
    (void)threadsHolder;
    if (threads.isArray()) {
        for (const CosObject& entry : threads.items()) {
            const std::optional<CosObject> thread = view.resolve(entry);
            if (!thread || !thread->isDictionary()) continue;
            const QString title = threadTitle(view, *thread);
            for (const auto& [bead, value] : beadsOf(view, *thread))
                out.records.push_back(record(Kind::ThreadBead, bead, Pointed{Target::Page, referenceNumber(value.get("P")), {}, -1}, title));
        }
    }

    if (const CosObject* open = catalog->find("OpenAction")) {
        const std::optional<CosObject> value = view.resolve(*open);
        if (value) {
            const Pointed to = value->isDictionary() && value->has("S") ? actionOf(view, *value) : destinationOf(view, *value);
            out.records.push_back(record(Kind::OpenAction, catalogNumber, to, QString()));
        }
    }
    return out;
}

// ==========================================================================================
// Report
// ==========================================================================================

core::PageCommandReport ReferenceRepair::report(const Scan& scan, const std::set<long long>& gone,
                                                const std::map<long long, int>& indexOf, bool byIndex)
{
    core::PageCommandReport out;
    std::set<std::pair<int, long long>> seen;   // one line per (kind, holder) and target
    for (const Record& r : scan.records) {
        if (r.kind == Kind::Link && gone.count(r.onPage) != 0) continue;   // gone with its page
        if (r.target == Target::Remote || r.target == Target::Index) {
            if (!byIndex) continue;
            out.references.push_back(core::PageReference{
                r.target == Target::Remote ? Kind::RemoteGoTo : Kind::IndexDestination, Treatment::Reported, r.words,
                r.holder, -1});
            continue;
        }
        long long page = -1;
        if (r.target == Target::Page) page = r.page;
        if (r.target == Target::Named && r.kind != Kind::NamedDestination) {
            const auto found = scan.named.find(r.name);
            if (found != scan.named.end()) page = found->second;
        }
        if (page < 0 || gone.count(page) == 0) continue;
        if (!seen.insert({static_cast<int>(r.kind), r.holder * 1'000'003 + page}).second) continue;
        Treatment treatment = Treatment::Removed;
        if (r.kind == Kind::OutlineItem || r.kind == Kind::Link) treatment = Treatment::Cleared;
        if (r.kind == Kind::ThreadBead) treatment = Treatment::Unlinked;
        const auto index = indexOf.find(page);
        out.references.push_back(core::PageReference{r.kind, treatment, r.words, r.holder,
                                                     index == indexOf.end() ? -1 : index->second});
    }
    return out;
}

// ==========================================================================================
// Compose
// ==========================================================================================

std::optional<CosObject> ReferenceRepair::clearedTarget(const View& view, const CosObject& holder,
                                                        const std::set<long long>& gone,
                                                        const std::map<std::string, long long>& named)
{
    if (!holder.isDictionary() || !hits(holderTarget(view, holder), gone, named)) return std::nullopt;
    return AnnotationOps::withoutEntry(AnnotationOps::withoutEntry(holder, "Dest"), "A");
}

core::Result<ReferenceRepair::Ledger> ReferenceRepair::compose(const View& view, const Scan& scan,
                                                               const std::set<long long>& gone,
                                                               std::map<long long, CosObject>& pending)
{
    Ledger touched;
    const long long catalogNumber = view.catalogNumber();
    const std::optional<CosObject> catalogValue = catalogNumber >= 0 ? view.object(catalogNumber) : std::nullopt;
    if (!catalogValue || !catalogValue->isDictionary()) return touched;

    // What each object SHOULD be, built from the view (the ledger put back) and changed by each pass in turn;
    // written at the end, key by key, where it differs from the disk.
    std::map<long long, CosObject> desired;
    const auto want = [&](long long number) -> CosObject* {
        auto found = desired.find(number);
        if (found == desired.end()) {
            std::optional<CosObject> value = view.object(number);
            if (!value) return nullptr;
            found = desired.emplace(number, std::move(*value)).first;
        }
        return &found->second;
    };
    const std::map<std::string, long long>& named = scan.named;
    const auto targetGone = [&](const CosObject& value) { return hits(destinationOf(view, value), gone, named); };
    // EVERY object a pass visits is taken into `desired` from the view, changed or not: a value the ledger
    // put back -- a page restored after a save -- is a difference from the disk, and is written only so.
    want(catalogNumber);

    // Named destinations: a pair to a page gone is removed, and a leaf's /Limits redrawn.
    const auto filteredPairs = [&](const CosObject& node) {
        std::vector<CosObject> kept;
        const std::vector<CosObject>& pairs = node.get("Names").items();
        for (std::size_t at = 0; at + 1 < pairs.size(); at += 2) {
            if (targetGone(pairs[at + 1])) continue;
            kept.push_back(pairs[at]);
            kept.push_back(pairs[at + 1]);
        }
        if (pairs.size() % 2 != 0) kept.push_back(pairs.back());   // a stray key, kept as written
        CosObject out = AnnotationOps::withEntry(node, "Names", CosObject::makeArray(kept));
        if (node.has("Limits")) {
            if (kept.size() >= 2) {
                out = AnnotationOps::withEntry(out, "Limits", CosObject::makeArray({kept.front(), kept[kept.size() - (kept.size() % 2 == 0 ? 2 : 3)]}));
            } else {
                out = AnnotationOps::withoutEntry(out, "Limits");
            }
        }
        return out;
    };
    long long rootHolder = -1;
    CosObject rootValue;
    for (const NameNode& leaf : nameTreeLeaves(view, *catalogValue, &rootHolder, &rootValue)) {
        if (leaf.holder >= 0) {
            // /Limits redrawn only where the pairs changed: an untouched leaf keeps the limits it was written with.
            if (CosObject* node = want(leaf.holder)) {
                const CosObject filtered = filteredPairs(*node);
                if (!(filtered.get("Names") == node->get("Names"))) *node = filtered;
            }
            continue;
        }
        // A direct root: rewritten inside the names dictionary that holds it.
        const CosObject& names = catalogValue->get("Names");
        const CosObject filtered = filteredPairs(leaf.node);
        if (filtered.get("Names") == leaf.node.get("Names")) continue;
        if (names.isReference()) {
            if (CosObject* holder = want(names.reference().number)) *holder = AnnotationOps::withEntry(*holder, "Dests", filtered);
        } else if (names.isDictionary()) {
            CosObject* catalog = want(catalogNumber);
            if (catalog != nullptr)
                *catalog = AnnotationOps::withEntry(*catalog, "Names", AnnotationOps::withEntry(catalog->get("Names"), "Dests", filtered));
        }
    }
    {
        const CosObject& dests = catalogValue->get("Dests");
        CosObject* holder = dests.isReference() ? want(dests.reference().number) : nullptr;
        CosObject direct = dests;
        CosObject* value = holder != nullptr ? holder : (dests.isDictionary() ? &direct : nullptr);
        if (value != nullptr && value->isDictionary()) {
            CosObject kept = *value;
            for (const CosObject::Entry& entry : value->entries()) {
                if (targetGone(entry.value)) kept = AnnotationOps::withoutEntry(kept, entry.key);
            }
            if (holder != nullptr) *holder = kept;
            else if (!(kept == direct)) {
                if (CosObject* catalog = want(catalogNumber)) *catalog = AnnotationOps::withEntry(*catalog, "Dests", kept);
            }
        }
    }

    // Outline items, and links on the pages that stay: their destination cleared.
    for (const auto& [number, item] : outlineItems(view, *catalogValue)) {
        CosObject* value = want(number);
        if (value == nullptr) continue;
        if (std::optional<CosObject> cleared = clearedTarget(view, *value, gone, named)) *value = std::move(*cleared);
    }
    for (const Record& r : scan.records) {
        if (r.kind != Kind::Link || gone.count(r.onPage) != 0) continue;
        CosObject* value = want(r.holder);
        if (value == nullptr) continue;
        if (std::optional<CosObject> cleared = clearedTarget(view, *value, gone, named)) *value = std::move(*cleared);
    }

    // The structure tree: content items on a page gone, and the /Pg naming it.
    if (const std::optional<CosObject> root = view.resolve(catalogValue->get("StructTreeRoot")); root && root->isDictionary()) {
        std::vector<long long> stack;
        std::set<long long> visited;
        const auto children = [&](const CosObject& kids, long long elementPage) {
            std::optional<CosObject> resolved = view.resolve(kids);
            if (!resolved) return;
            const std::vector<CosObject> items = resolved->isArray() ? resolved->items() : std::vector<CosObject>{kids};
            for (auto item = items.rbegin(); item != items.rend(); ++item) {
                const StructItem si = structItem(view, *item, elementPage);
                if (si.element && si.child >= 0) stack.push_back(si.child);
            }
        };
        children(root->get("K"), -1);
        while (!stack.empty() && visited.size() < kMaxReferences) {
            const long long number = stack.back();
            stack.pop_back();
            if (!visited.insert(number).second) continue;
            CosObject* element = want(number);
            if (element == nullptr || !element->isDictionary()) continue;
            const long long page = referenceNumber(element->get("Pg"));
            const CosObject kidsAsWritten = element->get("K");
            children(kidsAsWritten, page);
            const auto keep = [&](const CosObject& item) {
                const StructItem si = structItem(view, item, page);
                return si.element || si.page < 0 || gone.count(si.page) == 0;
            };
            if (kidsAsWritten.isReference()) {
                CosObject* array = want(kidsAsWritten.reference().number);
                if (array != nullptr && array->isArray()) {
                    std::vector<CosObject> kept;
                    for (const CosObject& item : array->items()) if (keep(item)) kept.push_back(item);
                    *array = CosObject::makeArray(std::move(kept));
                } else if (array != nullptr && !keep(kidsAsWritten)) {
                    *element = AnnotationOps::withoutEntry(*element, "K");
                }
            } else if (kidsAsWritten.isArray()) {
                std::vector<CosObject> kept;
                for (const CosObject& item : kidsAsWritten.items()) if (keep(item)) kept.push_back(item);
                *element = kept.empty() ? AnnotationOps::withoutEntry(*element, "K")
                                        : AnnotationOps::withEntry(*element, "K", CosObject::makeArray(std::move(kept)));
            } else if (element->has("K") && !keep(kidsAsWritten)) {
                *element = AnnotationOps::withoutEntry(*element, "K");
            }
            if (page >= 0 && gone.count(page) != 0) *element = AnnotationOps::withoutEntry(*element, "Pg");
        }
    }

    // Threads: the beads on pages gone unlinked from each ring; a thread with none left leaves /Threads.
    {
        const auto [listHolder, threads] = threadList(view, *catalogValue, catalogNumber);
        if (threads.isArray()) {
            std::vector<CosObject> keptThreads;
            for (const CosObject& entry : threads.items()) {
                const long long threadNumber = referenceNumber(entry);
                const std::optional<CosObject> thread = view.resolve(entry);
                if (threadNumber < 0 || !thread || !thread->isDictionary()) {
                    keptThreads.push_back(entry);
                    continue;
                }
                const auto beads = beadsOf(view, *thread);
                std::vector<long long> surviving;
                for (const auto& [bead, value] : beads) {
                    if (gone.count(referenceNumber(value.get("P"))) == 0) surviving.push_back(bead);
                }
                want(threadNumber);
                for (const auto& [bead, value] : beads) want(bead);
                if (surviving.empty() && !beads.empty()) continue;   // the thread leaves the list
                keptThreads.push_back(entry);
                if (surviving.size() == beads.size()) continue;
                const std::size_t n = surviving.size();
                for (std::size_t at = 0; at < n; ++at) {
                    CosObject* bead = want(surviving[at]);
                    if (bead == nullptr) continue;
                    *bead = AnnotationOps::withEntry(*bead, "N", CosObject::makeReference(surviving[(at + 1) % n], 0));
                    *bead = AnnotationOps::withEntry(*bead, "V", CosObject::makeReference(surviving[(at + n - 1) % n], 0));
                    if (at == 0 && !bead->has("T")) *bead = AnnotationOps::withEntry(*bead, "T", entry);
                }
                if (CosObject* value = want(threadNumber))
                    *value = AnnotationOps::withEntry(*value, "F", CosObject::makeReference(surviving.front(), 0));
            }
            const CosObject list = CosObject::makeArray(std::move(keptThreads));
            if (listHolder == catalogNumber) {
                if (CosObject* catalog = want(catalogNumber)) *catalog = AnnotationOps::withEntry(*catalog, "Threads", list);
            } else if (listHolder >= 0) {
                if (CosObject* array = want(listHolder)) *array = list;
            }
        }
    }

    // /OpenAction to a page gone.
    if (const CosObject* open = catalogValue->find("OpenAction")) {
        const std::optional<CosObject> value = view.resolve(*open);
        if (value) {
            const Pointed to = value->isDictionary() && value->has("S") ? actionOf(view, *value) : destinationOf(view, *value);
            if (hits(to, gone, named)) {
                if (CosObject* catalog = want(catalogNumber)) *catalog = AnnotationOps::withoutEntry(*catalog, "OpenAction");
            }
        }
    }

    // Written key by key where the desired value differs from the DISK, over whatever another part of the
    // compose already made of the object -- and the value each changed key has ON DISK, which the sink keeps
    // in its ledger after a save unless the ledger already holds an older one.
    for (auto& [number, value] : desired) {
        auto fetched = view.table().fetch(number);
        if (!fetched) continue;
        CosObject disk = fetched.value().object;
        disk.clearSpansRecursively();
        if (!value.isDictionary() || !disk.isDictionary() || value.isStream() != disk.isStream()) {
            if (value == disk) continue;
            pending[number] = value;
            touched[number][""] = disk;
            continue;
        }
        auto base = pending.find(number);
        CosObject written = base != pending.end() ? base->second : disk;
        bool changed = false;
        std::set<std::string> keys;
        for (const CosObject::Entry& entry : value.entries()) keys.insert(entry.key);
        for (const CosObject::Entry& entry : disk.entries()) keys.insert(entry.key);
        for (const std::string& key : keys) {
            const CosObject* now = value.find(key);
            const CosObject* was = disk.find(key);
            if ((now == nullptr && was == nullptr) || (now != nullptr && was != nullptr && *now == *was)) continue;
            written = now != nullptr ? AnnotationOps::withEntry(written, key, *now) : AnnotationOps::withoutEntry(written, key);
            touched[number][key] = was != nullptr ? std::optional<CosObject>(*was) : std::nullopt;
            changed = true;
        }
        if (changed) pending[number] = std::move(written);
    }
    return touched;
}

} // namespace rawpage::pdf::ops
