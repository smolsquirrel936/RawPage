// pdf/ops/ObjectGraphCopier -- implementation. Stage 7 T5. See the header for what is copied and what stays behind.

#include "pdf/ops/ObjectGraphCopier.h"

#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/EmbeddedFileReader.h"
#include "pdf/ops/PageOps.h"
#include "pdf/ops/ReferenceRepair.h"

#include <QString>

#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;
using Kind = core::PageReference::Kind;
using Treatment = core::PageReference::Treatment;

constexpr long long kCatalog = 1;   // the payload's catalog
constexpr long long kNode = 2;      // ... and its one page-tree node; the pages follow it
constexpr int kMaxPages = 1'000'000;

core::Error copyError(core::ErrorCode code, const char* key, long long detail = -1)
{
    return core::Error{code, QLatin1String(key), detail < 0 ? QString() : QString::number(static_cast<qlonglong>(detail))};
}

// What each page's path down the tree passes it -- the four inheritable keys, as the nearest ancestor states them --
// by page object; and every intermediate node met. The walk's own rules (a node is what has a `/Kids` array, a node
// met twice is skipped, a depth bound), so a page here is a page there.
struct TreeReading {
    std::map<long long, std::map<std::string, CosObject>> passed;
    std::set<long long> nodes;
};

TreeReading readTree(cos::XrefTable& table, long long catalog)
{
    TreeReading out;
    auto root = table.fetch(catalog);
    if (!root || !root.value().object.isDictionary()) return out;
    const CosObject& pages = root.value().object.get("Pages");
    if (!pages.isReference()) return out;
    struct Item {
        long long number;
        std::map<std::string, CosObject> inherited;
        int depth;
    };
    std::vector<Item> stack{{pages.reference().number, {}, 0}};
    std::set<long long> visited;
    while (!stack.empty()) {
        Item item = std::move(stack.back());
        stack.pop_back();
        if (!visited.insert(item.number).second || item.depth > PageOps::kMaxDepth) continue;
        auto fetched = table.fetch(item.number);
        if (!fetched || !fetched.value().object.isDictionary()) continue;
        const CosObject& node = fetched.value().object;
        const CosObject& kids = node.get("Kids");
        if (!kids.isArray()) {
            out.passed.emplace(item.number, std::move(item.inherited));
            continue;
        }
        out.nodes.insert(item.number);
        std::map<std::string, CosObject> passing = item.inherited;
        for (const char* key : PageOps::kInheritable) {
            if (const CosObject* own = node.find(key)) {
                CosObject value = *own;
                value.clearSpansRecursively();
                passing[key] = std::move(value);
            }
        }
        for (auto kid = kids.items().rbegin(); kid != kids.items().rend(); ++kid) {
            if (kid->isReference()) stack.push_back(Item{kid->reference().number, passing, item.depth + 1});
        }
    }
    return out;
}

// One copied object: its value with every reference renumbered, and a stream's ENCODED data.
struct Made {
    CosObject value;
    std::optional<std::string> data;
};

// The payload: a PDF holding `made`, its catalog and node first (see the header). Refused past `maxBytes`.
core::Result<std::string> writePayload(const std::map<long long, Made>& made, int pageCount, std::size_t maxBytes)
{
    std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;
    offsets.reserve(made.size());
    long long expected = 1;
    for (const auto& [number, object] : made) {
        if (number != expected++) return copyError(core::ErrorCode::Corrupted, "error.copy.tooLarge", number);
        offsets.push_back(out.size());
        out += std::to_string(number) + " 0 obj\n";
        if (object.value.isStream()) {
            const std::string& data = object.data ? *object.data : std::string();
            std::vector<CosObject::Entry> entries;
            for (const CosObject::Entry& entry : object.value.entries()) {
                if (entry.key != "Length") entries.push_back(entry);
            }
            entries.push_back({"Length", CosObject::makeInteger(static_cast<long long>(data.size()))});
            out += cos::serializeObject(CosObject::makeDictionary(std::move(entries)));
            out += "\nstream\n";
            out += data;
            out += "\nendstream";
        } else {
            out += cos::serializeObject(object.value);
        }
        out += "\nendobj\n";
        if (out.size() > maxBytes) return copyError(core::ErrorCode::Unsupported, "error.copy.tooLarge", pageCount);
    }
    const std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(made.size() + 1) + "\n0000000000 65535 f\r\n";
    char row[24];
    for (const std::size_t offset : offsets) {
        std::snprintf(row, sizeof row, "%010llu 00000 n\r\n", static_cast<unsigned long long>(offset));
        out += row;
    }
    out += "trailer\n<< /Size " + std::to_string(made.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref)
           + "\n%%EOF\n";
    if (out.size() > maxBytes) return copyError(core::ErrorCode::Unsupported, "error.copy.tooLarge", pageCount);
    return out;
}

void addFrame(std::map<long long, Made>& made, int pageCount, std::optional<CosObject> layers = std::nullopt)
{
    std::vector<CosObject> kids;
    kids.reserve(static_cast<std::size_t>(pageCount));
    for (int i = 0; i < pageCount; ++i) kids.push_back(CosObject::makeReference(kNode + 1 + i, 0));
    std::vector<CosObject::Entry> catalog{{"Type", CosObject::makeName("Catalog")}, {"Pages", CosObject::makeReference(kNode, 0)}};
    if (layers) catalog.push_back({"OCProperties", std::move(*layers)});
    made[kCatalog] = Made{CosObject::makeDictionary(std::move(catalog)), std::nullopt};
    made[kNode] = Made{CosObject::makeDictionary({{"Type", CosObject::makeName("Pages")},
                                                  {"Kids", CosObject::makeArray(std::move(kids))},
                                                  {"Count", CosObject::makeInteger(pageCount)}}),
                       std::nullopt};
}

class Copier {
public:
    Copier(cos::XrefTable& table, const PageWalk& walk, const ObjectGraphCopier::Options& options)
        : table_(table), walk_(walk), options_(options)
    {
    }

    core::Result<ObjectGraphCopier::Copied> run(const std::vector<int>& pages);

private:
    // One annotation to copy: where it is listed, and its dictionary as read.
    struct Listed {
        long long source = -1;    // its object number; -1 for a direct dictionary
        long long local = 0;
        CosObject dictionary;
    };

    std::optional<long long> localFor(long long number);
    CosObject transform(const CosObject& value);
    void listAnnotations(int index, long long pageLocal);
    void copyPage(int index, long long local);
    void copyAnnotation(const Listed& annotation, long long pageLocal, int pageIndex);
    std::optional<CosObject> linkTarget(const CosObject& annotation, bool& fromAction);
    std::optional<CosObject> layers();
    bool spend()
    {
        if (next_ - 2 > options_.maxObjects) {
            overBudget_ = true;
            return false;
        }
        return true;
    }

    cos::XrefTable& table_;
    const PageWalk& walk_;
    const ObjectGraphCopier::Options& options_;
    long long catalog_ = -1;
    TreeReading tree_;
    std::set<long long> sourcePages_;
    std::map<long long, long long> copiedPages_;        // source page -> local
    std::map<long long, long long> annotations_;        // source annotation -> local
    std::map<long long, long long> local_;              // every other source object -> local
    std::map<long long, std::vector<Listed>> listed_;   // page local -> the annotations it copies, in order
    std::map<long long, std::vector<long long>> annots_; // page local -> every annotation it LISTS, in order
    std::deque<std::pair<long long, long long>> queue_; // (source, local) still to copy
    std::map<long long, Made> made_;                    // by local number
    std::map<std::string, CosObject> named_;
    long long next_ = kNode + 1;
    bool overBudget_ = false;
    core::PageCommandReport report_;
};

std::optional<long long> Copier::localFor(long long number)
{
    if (const auto page = copiedPages_.find(number); page != copiedPages_.end()) return page->second;
    // The page tree and everything it holds that is not copied: never followed (guide A-41).
    if (number == catalog_ || tree_.nodes.count(number) != 0 || sourcePages_.count(number) != 0) return std::nullopt;
    // An annotation is named only through the keys `copyAnnotation` writes itself.
    if (annotations_.count(number) != 0) return std::nullopt;
    if (const auto found = local_.find(number); found != local_.end()) return found->second;
    if (!table_.fetch(number)) return std::nullopt;   // a reference that names nothing means null, here as in any reader
    if (!spend()) return std::nullopt;
    const long long local = next_++;
    local_.emplace(number, local);
    queue_.emplace_back(number, local);
    return local;
}

CosObject Copier::transform(const CosObject& value)
{
    switch (value.type()) {
    case cos::CosType::Reference: {
        const std::optional<long long> local = localFor(value.reference().number);
        return local ? CosObject::makeReference(*local, 0) : CosObject::makeNull();
    }
    case cos::CosType::Array: {
        std::vector<CosObject> items;
        items.reserve(value.items().size());
        for (const CosObject& item : value.items()) items.push_back(transform(item));
        return CosObject::makeArray(std::move(items));
    }
    case cos::CosType::Dictionary:
    case cos::CosType::Stream: {
        std::vector<CosObject::Entry> entries;
        entries.reserve(value.entries().size());
        for (const CosObject::Entry& entry : value.entries()) entries.push_back({entry.key, transform(entry.value)});
        return value.isStream() ? CosObject::makeStream(std::move(entries), cos::CosStreamData{})
                                : CosObject::makeDictionary(std::move(entries));
    }
    default: {
        CosObject copy = value;
        copy.clearSpan();
        return copy;
    }
    }
}

// A link's destination as the explicit array it stands for -- a name looked up, a reference followed -- or nullopt
// for a link that goes nowhere on a page of this file (a URI, another file, no action at all).
std::optional<CosObject> Copier::linkTarget(const CosObject& annotation, bool& fromAction)
{
    fromAction = false;
    CosObject destination;
    if (const CosObject* dest = annotation.find("Dest")) {
        destination = *dest;
    } else if (const CosObject* actionValue = annotation.find("A")) {
        CosObject action = *actionValue;
        if (action.isReference()) {
            auto fetched = table_.fetch(action.reference().number);
            if (!fetched) return std::nullopt;
            action = fetched.value().object;
        }
        if (!action.isDictionary() || !action.get("S").isName() || action.get("S").text() != "GoTo") return std::nullopt;
        fromAction = true;
        destination = action.get("D");
    } else {
        return std::nullopt;
    }
    for (int depth = 0; depth < 8; ++depth) {
        if (destination.isReference()) {
            auto fetched = table_.fetch(destination.reference().number);
            if (!fetched) return std::nullopt;
            destination = fetched.value().object;
        } else if (destination.isName() || destination.isString()) {
            const auto found = named_.find(destination.text());
            if (found == named_.end()) return std::nullopt;
            destination = found->second;
        } else if (destination.isDictionary()) {
            destination = destination.get("D");
        } else {
            break;
        }
    }
    if (!destination.isArray() || destination.items().empty()) return std::nullopt;
    destination.clearSpansRecursively();
    return destination;
}

// THE LAYERS THE COPY USES (Stage 7 guide I-07): the source's `/OCProperties`, narrowed to the optional-content groups
// the copied objects reached, each renumbered -- without it a page whose content a hidden group marks draws that
// content wherever it lands, since a reader shows a group no `/OCProperties` lists. Each list keeps the source's
// order; a nested `/Order` array or a radio group left with no group goes; `/AS` keeps its entries that still name
// one. A default configuration with `/BaseState /OFF` or `/Unchanged` is stated as the `/OFF` list it means, so the
// sink that unions it into another document's needs no base state of its own. Nullopt when no group was reached.
std::optional<CosObject> Copier::layers()
{
    auto catalog = table_.fetch(catalog_);
    if (!catalog || !catalog.value().object.isDictionary()) return std::nullopt;
    const auto resolve = [this](const CosObject& value) -> CosObject {
        if (!value.isReference()) return value;
        auto fetched = table_.fetch(value.reference().number);
        return fetched ? fetched.value().object : CosObject::makeNull();
    };
    const CosObject properties = resolve(catalog.value().object.get("OCProperties"));
    if (!properties.isDictionary()) return std::nullopt;
    const auto copied = [this](const CosObject& item) -> std::optional<long long> {
        if (!item.isReference()) return std::nullopt;
        const auto found = local_.find(item.reference().number);
        return found != local_.end() ? std::optional<long long>(found->second) : std::nullopt;
    };
    std::vector<CosObject> groups;
    std::set<long long> reached;
    const CosObject listedGroups = resolve(properties.get("OCGs"));
    for (const CosObject& item : listedGroups.items()) {
        if (const auto local = copied(item); local && reached.insert(*local).second) groups.push_back(CosObject::makeReference(*local, 0));
    }
    if (groups.empty()) return std::nullopt;
    const CosObject defaults = resolve(properties.get("D"));
    // The flat lists: each group the copy holds, in the order the source names it.
    const auto flat = [&](const CosObject& list) {
        std::vector<CosObject> out;
        const CosObject items = resolve(list);
        for (const CosObject& item : items.items()) {
            if (const auto local = copied(item); local && reached.count(*local) != 0) out.push_back(CosObject::makeReference(*local, 0));
        }
        return out;
    };
    std::vector<CosObject> off;
    const CosObject& base = defaults.get("BaseState");
    if (base.isName() && base.text() != "ON") {
        std::set<long long> on;
        for (const CosObject& item : flat(defaults.get("ON"))) on.insert(item.reference().number);
        for (const CosObject& group : groups) {
            if (on.count(group.reference().number) == 0) off.push_back(group);
        }
    } else {
        off = flat(defaults.get("OFF"));
    }
    // `/Order`, nested, its label strings kept where a group stays beneath them.
    std::function<std::optional<CosObject>(const CosObject&, int)> order = [&](const CosObject& value, int depth) -> std::optional<CosObject> {
        if (depth > 32) return std::nullopt;
        const CosObject list = resolve(value);
        if (!list.isArray()) return std::nullopt;
        std::vector<CosObject> items;
        bool any = false;
        for (const CosObject& item : list.items()) {
            if (item.isString()) {
                items.push_back(item);
            } else if (const auto local = copied(item); local && reached.count(*local) != 0) {
                items.push_back(CosObject::makeReference(*local, 0));
                any = true;
            } else if (std::optional<CosObject> inner = order(item, depth + 1)) {
                items.push_back(std::move(*inner));
                any = true;
            }
        }
        if (!any) return std::nullopt;
        CosObject out = CosObject::makeArray(std::move(items));
        out.clearSpansRecursively();
        return out;
    };
    std::vector<CosObject::Entry> d;
    d.push_back({"Order", order(defaults.get("Order"), 0).value_or(CosObject::makeArray(groups))});
    if (!off.empty()) d.push_back({"OFF", CosObject::makeArray(std::move(off))});
    if (std::vector<CosObject> locked = flat(defaults.get("Locked")); !locked.empty()) d.push_back({"Locked", CosObject::makeArray(std::move(locked))});
    std::vector<CosObject> radios;
    const CosObject radioGroups = resolve(defaults.get("RBGroups"));
    for (const CosObject& group : radioGroups.items()) {
        std::vector<CosObject> members = flat(group);
        if (!members.empty()) radios.push_back(CosObject::makeArray(std::move(members)));
    }
    if (!radios.empty()) d.push_back({"RBGroups", CosObject::makeArray(std::move(radios))});
    std::vector<CosObject> usage;
    const CosObject applications = resolve(defaults.get("AS"));
    for (const CosObject& entry : applications.items()) {
        const CosObject application = resolve(entry);
        std::vector<CosObject> members = flat(application.get("OCGs"));
        if (!application.isDictionary() || members.empty()) continue;
        std::vector<CosObject::Entry> kept;
        for (const CosObject::Entry& key : application.entries()) {
            if (key.key == "OCGs") kept.push_back({"OCGs", CosObject::makeArray(members)});
            else kept.push_back({key.key, transform(key.value)});
        }
        usage.push_back(CosObject::makeDictionary(std::move(kept)));
    }
    if (!usage.empty()) d.push_back({"AS", CosObject::makeArray(std::move(usage))});
    return CosObject::makeDictionary({{"OCGs", CosObject::makeArray(std::move(groups))}, {"D", CosObject::makeDictionary(std::move(d))}});
}

// Pass one, for a page: the annotations it lists, each numbered -- before ANY object is written, so `/Popup`, `/IRT`
// and `/Parent` resolve across every page copied in the same call. A widget stays with its field (see the header).
void Copier::listAnnotations(int index, long long pageLocal)
{
    const PageEntry& entry = walk_.pages[static_cast<std::size_t>(index)];
    auto fetched = table_.fetch(entry.number);
    if (!fetched || !fetched.value().object.isDictionary()) return;
    CosObject annots = fetched.value().object.get("Annots");
    if (annots.isReference()) {
        auto array = table_.fetch(annots.reference().number);
        annots = array ? array.value().object : CosObject::makeNull();
    }
    if (!annots.isArray()) return;
    std::vector<Listed>& listed = listed_[pageLocal];
    for (const CosObject& item : annots.items()) {
        CosObject dictionary = item;
        long long number = -1;
        if (item.isReference()) {
            number = item.reference().number;
            auto value = table_.fetch(number);
            if (!value) continue;
            dictionary = value.value().object;
        }
        dictionary.clearSpansRecursively();
        if (!dictionary.isDictionary()) continue;   // an entry that is no annotation: nothing to copy
        // Listed by two copied pages (or twice by one): ONE copy, listed wherever the source listed it.
        if (number >= 0) {
            if (const auto copied = annotations_.find(number); copied != annotations_.end()) {
                annots_[pageLocal].push_back(copied->second);
                continue;
            }
        }
        const CosObject& subtype = dictionary.get("Subtype");
        if (subtype.isName() && subtype.text() == "Widget") {
            const CosObject& field = dictionary.get("T");
            report_.references.push_back(core::PageReference{
                Kind::WidgetNotCopied, Treatment::NotCopied,
                field.isString() ? EmbeddedFileReader::decodeTextString(field.text()) : QString(), number, index});
            continue;
        }
        if (!spend()) return;
        const long long local = next_++;
        if (number >= 0) annotations_.emplace(number, local);
        annots_[pageLocal].push_back(local);
        listed.push_back(Listed{number, local, std::move(dictionary)});
    }
}

void Copier::copyAnnotation(const Listed& annotation, long long pageLocal, int pageIndex)
{
    const CosObject& source = annotation.dictionary;
    std::vector<CosObject::Entry> entries;
    const bool link = source.get("Subtype").isName() && source.get("Subtype").text() == "Link";
    bool fromAction = false;
    std::optional<CosObject> target = link ? linkTarget(source, fromAction) : std::nullopt;
    bool cleared = false;
    if (target && target->items().front().isReference()) {
        cleared = copiedPages_.count(target->items().front().reference().number) == 0;   // its page stays behind
    } else {
        target.reset();   // a page named by INTEGER -- a producer's error -- or none: copied as written
    }
    for (const CosObject::Entry& entry : source.entries()) {
        const std::string& key = entry.key;
        if (key == "P" || key == "StructParent") continue;
        if (key == "Popup" || key == "IRT" || key == "Parent") {
            if (entry.value.isReference()) {
                const auto found = annotations_.find(entry.value.reference().number);
                if (found != annotations_.end()) entries.push_back({key, CosObject::makeReference(found->second, 0)});
            }
            continue;   // a target not copied: the key goes (D7-4)
        }
        if (target && (key == "Dest" || (key == "A" && fromAction))) {
            if (cleared) continue;   // kept with its rectangle and border, its action gone (D7-2)
            CosObject array = transform(*target);
            if (key == "Dest") {
                entries.push_back({key, std::move(array)});
            } else {
                entries.push_back({key, CosObject::makeDictionary({{"S", CosObject::makeName("GoTo")}, {"D", std::move(array)}})});
            }
            continue;
        }
        entries.push_back({key, transform(entry.value)});
    }
    entries.push_back({"P", CosObject::makeReference(pageLocal, 0)});
    if (cleared) {
        const CosObject& contents = source.get("Contents");
        report_.references.push_back(core::PageReference{
            Kind::Link, Treatment::Cleared,
            contents.isString() ? EmbeddedFileReader::decodeTextString(contents.text()) : QString(), annotation.source,
            pageIndex});
    }
    made_[annotation.local] = Made{CosObject::makeDictionary(std::move(entries)), std::nullopt};
}

// Pass two, for a page: its dictionary, the four inheritable keys materialized (see the header).
void Copier::copyPage(int index, long long local)
{
    const PageEntry& entry = walk_.pages[static_cast<std::size_t>(index)];
    auto fetched = table_.fetch(entry.number);
    CosObject page = fetched && fetched.value().object.isDictionary() ? fetched.value().object : CosObject::makeDictionary({});
    page.clearSpansRecursively();
    const std::map<std::string, CosObject>& passed = tree_.passed[entry.number];

    std::vector<CosObject::Entry> entries;
    for (const CosObject::Entry& item : page.entries()) {
        const std::string& key = item.key;
        if (key == "Parent" || key == "Annots" || key == "StructParents" || key == "B") continue;
        bool inheritable = false;
        for (const char* name : PageOps::kInheritable) inheritable = inheritable || key == name;
        if (!inheritable) entries.push_back({key, transform(item.value)});
    }
    if (page.has("StructParents")) {
        report_.references.push_back(
            core::PageReference{Kind::StructureElement, Treatment::NotCopied, QString(), entry.number, index});
    }
    const auto resolved = [&](const char* key) -> std::optional<CosObject> {
        if (const CosObject* own = page.find(key)) return *own;
        if (const auto found = passed.find(key); found != passed.end()) return found->second;
        return std::nullopt;
    };
    const std::optional<CosObject> resources = resolved("Resources");
    entries.push_back({"Resources", resources ? transform(*resources) : CosObject::makeDictionary({})});
    const std::optional<CosObject> media = resolved("MediaBox");
    entries.push_back({"MediaBox", media ? transform(*media)
                                         : CosObject::makeArray({CosObject::makeInteger(0), CosObject::makeInteger(0),
                                                                 CosObject::makeInteger(612), CosObject::makeInteger(792)})});
    if (const std::optional<CosObject> crop = resolved("CropBox")) entries.push_back({"CropBox", transform(*crop)});
    entries.push_back({"Rotate", CosObject::makeInteger(entry.effectiveRotate())});
    if (!page.has("Type")) entries.push_back({"Type", CosObject::makeName("Page")});
    std::vector<CosObject> annots;
    for (const long long annotation : annots_[local]) annots.push_back(CosObject::makeReference(annotation, 0));
    if (!annots.empty()) entries.push_back({"Annots", CosObject::makeArray(std::move(annots))});
    entries.push_back({"Parent", CosObject::makeReference(kNode, 0)});
    made_[local] = Made{CosObject::makeDictionary(std::move(entries)), std::nullopt};
}

core::Result<ObjectGraphCopier::Copied> Copier::run(const std::vector<int>& pages)
{
    if (table_.isEncrypted()) return copyError(core::ErrorCode::PermissionDenied, "error.pdf.encrypted");
    if (!walk_.notes.empty())
        return copyError(core::ErrorCode::Unsupported, "error.pdf.pageTreeDamaged", walk_.notes.front().objectNumber);
    const int count = static_cast<int>(walk_.pages.size());
    if (pages.empty() || pages.size() > static_cast<std::size_t>(kMaxPages))
        return copyError(core::ErrorCode::InvalidArgument, "error.pdf.pageOutOfRange", static_cast<long long>(pages.size()));
    for (std::size_t at = 0; at < pages.size(); ++at) {
        if (pages[at] < 0 || pages[at] >= count || (at > 0 && pages[at] <= pages[at - 1]))
            return copyError(core::ErrorCode::InvalidArgument, "error.pdf.pageOutOfRange", pages[at]);
    }
    const CosObject& root = table_.trailer().get("Root");
    catalog_ = root.isReference() ? root.reference().number : -1;
    tree_ = readTree(table_, catalog_);
    for (const PageEntry& page : walk_.pages) sourcePages_.insert(page.number);
    const ReferenceRepair::Ledger none;
    named_ = ReferenceRepair::namedArrays(ReferenceRepair::View(table_, none));

    // The pages first, numbered in the order given: 3, 4, ... -- the payload's node lists exactly them.
    for (const int index : pages) copiedPages_.emplace(walk_.pages[static_cast<std::size_t>(index)].number, next_++);
    for (const int index : pages) listAnnotations(index, copiedPages_.at(walk_.pages[static_cast<std::size_t>(index)].number));
    for (const int index : pages) {
        const long long local = copiedPages_.at(walk_.pages[static_cast<std::size_t>(index)].number);
        copyPage(index, local);
        for (const Listed& annotation : listed_[local]) copyAnnotation(annotation, local, index);
    }
    // Everything they reach, breadth first, each object once.
    while (!queue_.empty() && !overBudget_) {
        const auto [number, local] = queue_.front();
        queue_.pop_front();
        auto fetched = table_.fetch(number);
        if (!fetched) {
            made_[local] = Made{CosObject::makeNull(), std::nullopt};
            continue;
        }
        const CosObject& object = fetched.value().object;
        Made made{transform(object), std::nullopt};
        if (object.isStream()) made.data = std::string(object.rawStreamData(table_.source()));
        made_[local] = std::move(made);
    }
    if (overBudget_)
        return copyError(core::ErrorCode::Unsupported, "error.copy.tooLarge", options_.maxObjects);

    std::optional<CosObject> usedLayers = layers();
    // `/AS`'s other keys may have reached objects of their own; they are copied like any other.
    while (!queue_.empty() && !overBudget_) {
        const auto [number, local] = queue_.front();
        queue_.pop_front();
        auto fetched = table_.fetch(number);
        Made made{fetched ? transform(fetched.value().object) : CosObject::makeNull(), std::nullopt};
        if (fetched && fetched.value().object.isStream()) made.data = std::string(fetched.value().object.rawStreamData(table_.source()));
        made_[local] = std::move(made);
    }
    if (overBudget_)
        return copyError(core::ErrorCode::Unsupported, "error.copy.tooLarge", options_.maxObjects);
    addFrame(made_, static_cast<int>(pages.size()), std::move(usedLayers));
    auto payload = writePayload(made_, static_cast<int>(pages.size()), options_.maxBytes);
    if (!payload) return payload.error();
    ObjectGraphCopier::Copied out;
    out.pdf = std::move(payload).value();
    out.pageCount = static_cast<int>(pages.size());
    out.objects = static_cast<long long>(made_.size()) - 2;
    out.report = std::move(report_);
    return out;
}

} // namespace

core::Result<ObjectGraphCopier::Copied> ObjectGraphCopier::copy(cos::XrefTable& source, const PageWalk& walk,
                                                                const std::vector<int>& pages, const Options& options)
{
    Copier copier(source, walk, options);
    return copier.run(pages);
}

core::Result<ObjectGraphCopier::Copied> ObjectGraphCopier::blank(double width, double height, int count)
{
    // ISO 32000-1 Annex C: a page is 3 to 14,400 units on a side.
    if (!std::isfinite(width) || !std::isfinite(height) || width < 3.0 || height < 3.0 || width > 14400.0 || height > 14400.0)
        return copyError(core::ErrorCode::InvalidArgument, "error.pdf.pageSizeOutOfRange");
    if (count < 1 || count > kMaxPages)
        return copyError(core::ErrorCode::InvalidArgument, "error.pdf.pageOutOfRange", count);
    std::map<long long, Made> made;
    addFrame(made, count);
    for (int i = 0; i < count; ++i) {
        made[kNode + 1 + i] = Made{
            CosObject::makeDictionary({{"Type", CosObject::makeName("Page")},
                                       {"MediaBox", CosObject::makeArray({CosObject::makeInteger(0), CosObject::makeInteger(0),
                                                                          CosObject::makeReal(width), CosObject::makeReal(height)})},
                                       {"Resources", CosObject::makeDictionary({})},
                                       {"Rotate", CosObject::makeInteger(0)},
                                       {"Parent", CosObject::makeReference(kNode, 0)}}),
            std::nullopt};
    }
    auto payload = writePayload(made, count, Options{}.maxBytes);
    if (!payload) return payload.error();
    Copied out;
    out.pdf = std::move(payload).value();
    out.pageCount = count;
    return out;
}

namespace {

core::CopiedPages carried(ObjectGraphCopier::Copied copied)
{
    core::CopiedPages out;
    out.graph = QByteArray(copied.pdf.data(), static_cast<qsizetype>(copied.pdf.size()));
    out.pageCount = copied.pageCount;
    out.report = std::move(copied.report);
    return out;
}

} // namespace

core::Result<core::CopiedPages> ObjectGraphCopier::copyFile(std::string_view bytes, const std::vector<int>& pages)
{
    auto opened = cos::openWithRepair(bytes);
    if (!opened) return opened.error();
    if (opened.value().table.isEncrypted()) return copyError(core::ErrorCode::PermissionDenied, "error.pdf.encrypted");
    auto walk = PageOps::walk(opened.value().table);
    if (!walk) return walk.error();
    auto copied = copy(opened.value().table, walk.value(), pages);
    if (!copied) return copied.error();
    return carried(std::move(copied).value());
}

core::Result<core::CopiedPages> ObjectGraphCopier::blankPages(core::PdfSize size, int count)
{
    auto copied = blank(size.width, size.height, count);
    if (!copied) return copied.error();
    return carried(std::move(copied).value());
}

} // namespace rawpage::pdf::ops
