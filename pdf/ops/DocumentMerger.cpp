// pdf/ops/DocumentMerger -- implementation. Stage 7 T10. The header says what each input brings, how they are
// joined, and what is skipped.

#include "pdf/ops/DocumentMerger.h"

#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/AnnotationOps.h"
#include "pdf/ops/DocumentAssembler.h"
#include "pdf/ops/EmbeddedFileReader.h"
#include "pdf/ops/ObjectGraphCopier.h"
#include "pdf/ops/PageOps.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;

core::Error mergeError(core::ErrorCode code, const char* key, const QString& context = QString())
{
    return core::Error{code, QLatin1String(key), context};
}

// The objects of the joined payload, by number: a value, and a stream's data ENCODED.
struct Value {
    CosObject object;
    std::optional<std::string> data;
};
using Objects = std::map<long long, Value>;

// ISO 32000-1 7.5.2: the header may follow up to 1,024 bytes of anything else.
bool looksLikePdf(const std::string& bytes)
{
    const std::size_t at = bytes.find("%PDF-");
    return at != std::string::npos && at <= 1024;
}

// `value` with every reference through `map` -- one the map does not hold is null.
CosObject renumbered(const CosObject& value, const std::map<long long, long long>& map)
{
    switch (value.type()) {
    case cos::CosType::Reference: {
        const auto found = map.find(value.reference().number);
        return found != map.end() ? CosObject::makeReference(found->second, 0) : CosObject::makeNull();
    }
    case cos::CosType::Array: {
        std::vector<CosObject> items;
        items.reserve(value.items().size());
        for (const CosObject& item : value.items()) items.push_back(renumbered(item, map));
        return CosObject::makeArray(std::move(items));
    }
    case cos::CosType::Dictionary:
    case cos::CosType::Stream: {
        std::vector<CosObject::Entry> entries;
        entries.reserve(value.entries().size());
        for (const CosObject::Entry& entry : value.entries()) entries.push_back({entry.key, renumbered(entry.value, map)});
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

CosObject with(const CosObject& dictionary, const std::string& key, CosObject value)
{
    return AnnotationOps::withEntry(dictionary, key, std::move(value));
}

CosObject without(const CosObject& dictionary, const std::string& key)
{
    return AnnotationOps::withoutEntry(dictionary, key);
}

QString textOf(const CosObject& value)
{
    return value.isString() ? EmbeddedFileReader::decodeTextString(value.text()) : QString();
}

// One input taken: its payload, kept for as long as its table reads it.
struct Taken {
    int input = -1;
    QString label;
    std::string payload;
    int pageCount = 0;
};

// The payload of the join, with its numbers, its form and its outline built from `taken`.
class Joiner {
public:
    Joiner(std::vector<Taken>& taken, const core::MergeOptions& options, core::MergeReport& report)
        : taken_(taken), options_(options), report_(report)
    {
    }
    core::Result<std::string> run();

private:
    struct Part {
        std::optional<cos::OpenedFile> opened;
        std::map<long long, long long> map;   // payload number -> joined number
        std::vector<long long> pages;         // joined numbers, in order
        CosObject catalog;                    // the payload's, renumbered
        long long outlineRoot = -1;           // the payload's /Outlines object, or -1
        long long outlineCount = 0;
    };

    CosObject resolve(const CosObject& value)
    {
        if (!value.isReference()) return value;
        const auto found = objects_.find(value.reference().number);
        return found != objects_.end() ? found->second.object : CosObject::makeNull();
    }
    CosObject& at(long long number) { return objects_.at(number).object; }

    void outline();
    void form();
    void layers();

    // The fields.
    bool isWidget(long long number);
    bool isTerminal(long long number);
    std::vector<long long> kidsOf(long long number);
    QString nameOf(long long number) { return textOf(at(number).get("T")); }
    bool compatible(long long a, long long b, int depth);
    long long mergeField(long long into, long long from, int depth);
    long long asNode(long long widget);

    std::vector<Taken>& taken_;
    const core::MergeOptions& options_;
    core::MergeReport& report_;
    std::vector<Part> parts_;
    Objects objects_;
    long long next_ = 3;
    long long outlineRoot_ = -1;
    std::vector<long long> perFile_;   // the bookmark per input, or -1 each
    std::vector<CosObject::Entry> catalogExtra_;
    bool mergedAny_ = false;
};

core::Result<std::string> Joiner::run()
{
    parts_.resize(taken_.size());
    // The pages first, in the order merged, a separator after each input but the last when asked.
    std::vector<std::optional<long long>> separators(taken_.size());
    for (std::size_t k = 0; k < taken_.size(); ++k) {
        Part& part = parts_[k];
        auto opened = cos::openWithRepair(taken_[k].payload);
        if (!opened) return opened.error();
        part.opened.emplace(std::move(opened).value());
        cos::XrefTable& table = part.opened->table;
        auto catalog = table.fetch(1);
        auto node = table.fetch(2);
        if (!catalog || !node) return mergeError(core::ErrorCode::Corrupted, "error.assembly.verifyFailed", QStringLiteral("payload"));
        for (const CosObject& kid : node.value().object.get("Kids").items()) {
            if (!kid.isReference()) continue;
            part.map[kid.reference().number] = next_;
            part.pages.push_back(next_++);
        }
        if (part.pages.empty()) return mergeError(core::ErrorCode::Corrupted, "error.assembly.verifyFailed", QStringLiteral("payload"));
        if (options_.separatorPages && k + 1 < taken_.size()) separators[k] = next_++;
        const CosObject& outlines = catalog.value().object.get("Outlines");
        if (outlines.isReference()) {
            part.outlineRoot = outlines.reference().number;
            if (auto root = table.fetch(part.outlineRoot)) part.outlineCount = std::max(0LL, root.value().object.get("Count").integerValue());
        }
    }
    // A bookmark per input and the root above them: numbered now, so each input's own items name them as parent.
    const bool anyOutline = std::any_of(parts_.begin(), parts_.end(), [](const Part& p) { return p.outlineRoot >= 0; });
    if (options_.bookmarks != core::MergeBookmarks::None || anyOutline) outlineRoot_ = next_++;
    perFile_.assign(taken_.size(), -1);
    if (options_.bookmarks != core::MergeBookmarks::None) {
        for (long long& number : perFile_) number = next_++;
    }
    for (std::size_t k = 0; k < taken_.size(); ++k) {
        Part& part = parts_[k];
        if (part.outlineRoot >= 0) part.map[part.outlineRoot] = perFile_[k] >= 0 ? perFile_[k] : outlineRoot_;
        part.map[2] = 2;
        for (const auto& [number, entry] : part.opened->table.entries()) {
            if (number <= 2 || entry.type == cos::XrefEntryType::Free || number == part.outlineRoot) continue;
            if (part.map.count(number) == 0) part.map[number] = next_++;
        }
    }
    // Every object, renumbered -- each payload's catalog and node consumed, its outline root replaced.
    for (Part& part : parts_) {
        cos::XrefTable& table = part.opened->table;
        for (const auto& [number, entry] : table.entries()) {
            if (number <= 0 || entry.type == cos::XrefEntryType::Free || number == part.outlineRoot) continue;
            auto fetched = table.fetch(number);
            if (!fetched) return mergeError(core::ErrorCode::Corrupted, "error.assembly.verifyFailed", QStringLiteral("payload object"));
            if (number == 1) {
                part.catalog = renumbered(fetched.value().object, part.map);
                continue;
            }
            if (number == 2) continue;
            Value value{renumbered(fetched.value().object, part.map), std::nullopt};
            if (fetched.value().object.isStream()) value.data = std::string(fetched.value().object.rawStreamData(table.source()));
            objects_[part.map.at(number)] = std::move(value);
        }
    }
    // The separators: blank, the size the page before each SHOWS -- its CropBox within its MediaBox, and its turn.
    for (std::size_t k = 0; k < taken_.size(); ++k) {
        if (!separators[k]) continue;
        const CosObject& last = at(parts_[k].pages.back());
        const auto box = [&](const char* key) -> std::optional<std::vector<double>> {
            const CosObject value = resolve(last.get(key));
            if (!value.isArray() || value.items().size() != 4) return std::nullopt;
            std::vector<double> out;
            for (const CosObject& n : value.items()) out.push_back(resolve(n).numberValue());
            return std::vector<double>{std::min(out[0], out[2]), std::min(out[1], out[3]), std::max(out[0], out[2]), std::max(out[1], out[3])};
        };
        std::vector<double> shown = box("MediaBox").value_or(std::vector<double>{0, 0, 612, 792});
        if (const auto crop = box("CropBox")) {
            const std::vector<double> clipped{std::max(shown[0], (*crop)[0]), std::max(shown[1], (*crop)[1]), std::min(shown[2], (*crop)[2]),
                                              std::min(shown[3], (*crop)[3])};
            if (clipped[2] > clipped[0] && clipped[3] > clipped[1]) shown = clipped;
        }
        std::vector<CosObject> media;
        for (const double n : shown) media.push_back(CosObject::makeReal(n));
        objects_[*separators[k]] = Value{CosObject::makeDictionary({{"Type", CosObject::makeName("Page")},
                                                                    {"Parent", CosObject::makeReference(2, 0)},
                                                                    {"MediaBox", CosObject::makeArray(std::move(media))},
                                                                    {"Rotate", CosObject::makeInteger(resolve(last.get("Rotate")).integerValue())},
                                                                    {"Resources", CosObject::makeDictionary({})}}),
                                         std::nullopt};
        ++report_.separators;
    }

    outline();
    form();
    layers();

    // The frame: the catalog, and the one node listing every page.
    std::vector<CosObject> kids;
    for (std::size_t k = 0; k < taken_.size(); ++k) {
        for (const long long page : parts_[k].pages) kids.push_back(CosObject::makeReference(page, 0));
        if (separators[k]) kids.push_back(CosObject::makeReference(*separators[k], 0));
    }
    report_.pageCount = static_cast<int>(kids.size());
    objects_[2] = Value{CosObject::makeDictionary({{"Type", CosObject::makeName("Pages")},
                                                   {"Kids", CosObject::makeArray(std::move(kids))},
                                                   {"Count", CosObject::makeInteger(report_.pageCount)}}),
                        std::nullopt};
    std::vector<CosObject::Entry> catalog{{"Type", CosObject::makeName("Catalog")}, {"Pages", CosObject::makeReference(2, 0)}};
    for (CosObject::Entry& entry : catalogExtra_) catalog.push_back(std::move(entry));
    objects_[1] = Value{CosObject::makeDictionary(std::move(catalog)), std::nullopt};

    // Written as one classic section; the numbers a merged field left behind are free rows.
    std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
    std::map<long long, std::size_t> offsets;
    for (const auto& [number, value] : objects_) {
        offsets[number] = out.size();
        out += cos::serializeIndirect(number, 0, value.object, value.data);
    }
    const long long size = objects_.rbegin()->first + 1;
    const std::size_t xref = out.size();
    out += "xref\n0 " + std::to_string(size) + "\n0000000000 65535 f\r\n";
    char row[24];
    for (long long number = 1; number < size; ++number) {
        const auto found = offsets.find(number);
        if (found != offsets.end()) std::snprintf(row, sizeof row, "%010llu 00000 n\r\n", static_cast<unsigned long long>(found->second));
        else std::snprintf(row, sizeof row, "%010d 00000 f\r\n", 0);
        out += row;
    }
    out += "trailer\n<< /Size " + std::to_string(size) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return out;
}

// THE OUTLINE -- see the header.
void Joiner::outline()
{
    if (outlineRoot_ < 0) return;
    // Each input's own top level, as its first and last item and how many a reader shows below its root.
    std::vector<std::pair<long long, long long>> ends(taken_.size(), {-1, -1});
    for (std::size_t k = 0; k < taken_.size(); ++k) {
        Part& part = parts_[k];
        if (part.outlineRoot < 0) continue;
        auto root = part.opened->table.fetch(part.outlineRoot);
        if (!root) continue;
        const CosObject& first = root.value().object.get("First");
        const CosObject& last = root.value().object.get("Last");
        if (!first.isReference() || !last.isReference()) continue;
        ends[k] = {part.map.at(first.reference().number), part.map.at(last.reference().number)};
    }
    long long count = 0;
    long long first = -1;
    long long last = -1;
    if (options_.bookmarks != core::MergeBookmarks::None) {
        const std::vector<long long>& items = perFile_;   // numbered in `run`, so each input's own items name them
        for (std::size_t k = 0; k < taken_.size(); ++k) {
            std::vector<CosObject::Entry> entries{
                {"Title", AnnotationOps::textString(taken_[k].label)},
                {"Parent", CosObject::makeReference(outlineRoot_, 0)},
                {"Dest", CosObject::makeArray({CosObject::makeReference(parts_[k].pages.front(), 0), CosObject::makeName("Fit")})}};
            if (k > 0) entries.push_back({"Prev", CosObject::makeReference(items[k - 1], 0)});
            if (k + 1 < items.size()) entries.push_back({"Next", CosObject::makeReference(items[k + 1], 0)});
            if (ends[k].first >= 0) {
                entries.push_back({"First", CosObject::makeReference(ends[k].first, 0)});
                entries.push_back({"Last", CosObject::makeReference(ends[k].second, 0)});
                // CLOSED: a merged document opens on one bookmark per input.
                entries.push_back({"Count", CosObject::makeInteger(-std::max(1LL, parts_[k].outlineCount))});
            }
            objects_[items[k]] = Value{CosObject::makeDictionary(std::move(entries)), std::nullopt};
        }
        first = items.front();
        last = items.back();
        count = static_cast<long long>(items.size());
    } else {
        // Each input's items the top level: the last of one linked to the first of the next.
        long long previous = -1;
        for (std::size_t k = 0; k < taken_.size(); ++k) {
            if (ends[k].first < 0) continue;
            if (previous >= 0) {
                at(previous) = with(at(previous), "Next", CosObject::makeReference(ends[k].first, 0));
                at(ends[k].first) = with(at(ends[k].first), "Prev", CosObject::makeReference(previous, 0));
            } else {
                first = ends[k].first;
            }
            previous = ends[k].second;
            count += parts_[k].outlineCount;
        }
        last = previous;
        if (first < 0) return;
    }
    objects_[outlineRoot_] = Value{CosObject::makeDictionary({{"Type", CosObject::makeName("Outlines")},
                                                              {"First", CosObject::makeReference(first, 0)},
                                                              {"Last", CosObject::makeReference(last, 0)},
                                                              {"Count", CosObject::makeInteger(count)}}),
                                   std::nullopt};
    catalogExtra_.push_back({"Outlines", CosObject::makeReference(outlineRoot_, 0)});
}

// ---- The fields --------------------------------------------------------------------------------------

// The keys that belong to a FIELD rather than to its widget (ISO 32000-1 Tables 220, 222, 228, 231): what moves to the
// field node when a widget that is its own field is split, and what a widget merged into another field gives up.
const std::set<std::string>& fieldKeys()
{
    static const std::set<std::string> keys{"FT", "T", "TU", "TM", "Ff", "V", "DV", "Opt", "MaxLen", "TI", "I", "RV", "DS"};
    return keys;
}

bool Joiner::isWidget(long long number)
{
    const CosObject& subtype = at(number).get("Subtype");
    return subtype.isName() && subtype.text() == "Widget";
}

std::vector<long long> Joiner::kidsOf(long long number)
{
    std::vector<long long> out;
    for (const CosObject held366 = resolve(at(number).get("Kids")); const CosObject& kid : held366.items()) {
        if (kid.isReference() && objects_.count(kid.reference().number) != 0) out.push_back(kid.reference().number);
    }
    return out;
}

// A terminal field: its widgets are itself or its kids -- none of its kids is a field with a name.
bool Joiner::isTerminal(long long number)
{
    if (isWidget(number)) return true;
    for (const long long kid : kidsOf(number)) {
        if (at(kid).has("T")) return false;
    }
    return true;
}

bool Joiner::compatible(long long a, long long b, int depth)
{
    if (depth > 16) return false;
    const CosObject& ta = at(a).get("FT");
    const CosObject& tb = at(b).get("FT");
    if (ta.isName() && tb.isName() && ta.text() != tb.text()) return false;
    const bool terminal = isTerminal(a);
    if (terminal != isTerminal(b)) return false;
    if (terminal) return true;
    for (const long long kid : kidsOf(b)) {
        const QString name = nameOf(kid);
        for (const long long mine : kidsOf(a)) {
            if (!name.isEmpty() && nameOf(mine) == name && !compatible(mine, kid, depth + 1)) return false;
        }
    }
    return true;
}

// A widget that is its own field, split in two: a new field node holding the field's keys and its parent, and the
// widget below it with the rest. The node's number is answered.
long long Joiner::asNode(long long widget)
{
    const long long node = next_++;
    std::vector<CosObject::Entry> field;
    std::vector<CosObject::Entry> rest;
    for (const CosObject::Entry& entry : at(widget).entries()) {
        if (fieldKeys().count(entry.key) != 0 || entry.key == "Parent") field.push_back(entry);
        else rest.push_back(entry);
    }
    field.push_back({"Kids", CosObject::makeArray({CosObject::makeReference(widget, 0)})});
    rest.push_back({"Parent", CosObject::makeReference(node, 0)});
    at(widget) = CosObject::makeDictionary(std::move(rest));
    objects_[node] = Value{CosObject::makeDictionary(std::move(field)), std::nullopt};
    return node;
}

// `from` merged into `into`, which `compatible` accepted; the number standing for `into` afterwards is answered (a
// widget that was its own field is a node now). `from` is gone.
long long Joiner::mergeField(long long into, long long from, int depth)
{
    mergedAny_ = true;
    if (isTerminal(into)) {
        const long long node = isWidget(into) ? asNode(into) : into;
        const std::vector<long long> mine = kidsOf(node);
        // The appearance every widget now shows: the surviving field's first widget's.
        const CosObject source = mine.empty() ? CosObject::makeNull() : at(mine.front());
        std::vector<long long> theirs = isWidget(from) ? std::vector<long long>{from} : kidsOf(from);
        std::vector<CosObject> kids = resolve(at(node).get("Kids")).items();
        for (const long long widget : theirs) {
            CosObject value = at(widget);
            for (const std::string& key : fieldKeys()) value = without(value, key);
            value = with(value, "Parent", CosObject::makeReference(node, 0));
            if (const CosObject* appearance = source.find("AP")) value = with(value, "AP", *appearance);
            if (const CosObject* state = source.find("AS")) value = with(value, "AS", *state);
            at(widget) = std::move(value);
            kids.push_back(CosObject::makeReference(widget, 0));
        }
        at(node) = with(at(node), "Kids", CosObject::makeArray(std::move(kids)));
        if (!isWidget(from)) objects_.erase(from);
        return node;
    }
    std::vector<CosObject> kids = resolve(at(into).get("Kids")).items();
    for (const long long kid : kidsOf(from)) {
        const QString name = nameOf(kid);
        bool merged = false;
        for (CosObject& mine : kids) {
            if (!mine.isReference() || name.isEmpty() || objects_.count(mine.reference().number) == 0 || nameOf(mine.reference().number) != name)
                continue;
            const long long now = mergeField(mine.reference().number, kid, depth + 1);
            at(now) = with(at(now), "Parent", CosObject::makeReference(into, 0));
            mine = CosObject::makeReference(now, 0);
            merged = true;
            break;
        }
        if (merged) continue;
        at(kid) = with(at(kid), "Parent", CosObject::makeReference(into, 0));
        kids.push_back(CosObject::makeReference(kid, 0));
    }
    at(into) = with(at(into), "Kids", CosObject::makeArray(std::move(kids)));
    objects_.erase(from);
    return into;
}

// THE FIELDS -- see the header.
void Joiner::form()
{
    std::vector<long long> roots;
    std::map<QString, std::size_t> byName;   // a top-level name -> its place in `roots`
    std::set<QString> used;
    std::optional<CosObject> appearanceString;
    std::optional<CosObject> quadding;
    long long sigFlags = 0;
    bool needAppearances = false;
    std::map<std::string, std::vector<CosObject::Entry>> resources;   // /DR's kinds, first input winning a name
    std::vector<std::string> kinds;
    std::vector<CosObject> order;
    bool any = false;
    for (std::size_t k = 0; k < taken_.size(); ++k) {
        const CosObject form = resolve(parts_[k].catalog.get("AcroForm"));
        if (!form.isDictionary()) continue;
        any = true;
        if (!appearanceString && form.has("DA")) appearanceString = form.get("DA");
        if (!quadding && form.has("Q")) quadding = form.get("Q");
        sigFlags |= resolve(form.get("SigFlags")).integerValue();
        needAppearances = needAppearances || resolve(form.get("NeedAppearances")).booleanValue();
        for (const CosObject held487 = resolve(form.get("DR")); const CosObject::Entry& kind : held487.entries()) {
            const CosObject names = resolve(kind.value);
            if (!names.isDictionary()) continue;
            if (resources.count(kind.key) == 0) kinds.push_back(kind.key);
            std::vector<CosObject::Entry>& held = resources[kind.key];
            for (const CosObject::Entry& name : names.entries()) {
                if (std::none_of(held.begin(), held.end(), [&](const CosObject::Entry& e) { return e.key == name.key; })) held.push_back(name);
            }
        }
        for (const CosObject held496 = resolve(form.get("CO")); const CosObject& item : held496.items()) {
            if (item.isReference() && objects_.count(item.reference().number) != 0) order.push_back(item);
        }
        for (const CosObject held499 = resolve(form.get("Fields")); const CosObject& item : held499.items()) {
            if (!item.isReference() || objects_.count(item.reference().number) == 0) continue;
            const long long root = item.reference().number;
            const QString name = nameOf(root);
            const auto earlier = byName.find(name);
            if (name.isEmpty() || earlier == byName.end()) {
                if (!name.isEmpty()) byName.emplace(name, roots.size());
                used.insert(name);
                roots.push_back(root);
                continue;
            }
            if (!options_.renameCollidingFields && compatible(roots[earlier->second], root, 0)) {
                roots[earlier->second] = mergeField(roots[earlier->second], root, 0);
                report_.fields.push_back(core::FieldCollision{name, QString(), taken_[k].input});
                continue;
            }
            QString fresh;
            for (int n = 2; fresh.isEmpty() || used.count(fresh) != 0; ++n) fresh = DocumentMerger::renamed(name, n);
            at(root) = with(at(root), "T", AnnotationOps::textString(fresh));
            used.insert(fresh);
            byName.emplace(fresh, roots.size());
            roots.push_back(root);
            report_.fields.push_back(core::FieldCollision{name, fresh, taken_[k].input});
        }
    }
    if (!any || roots.empty()) return;
    std::vector<CosObject> fields;
    for (const long long root : roots) fields.push_back(CosObject::makeReference(root, 0));
    std::vector<CosObject::Entry> entries{{"Fields", CosObject::makeArray(std::move(fields))}};
    if (needAppearances || mergedAny_) entries.push_back({"NeedAppearances", CosObject::makeBoolean(true)});
    if (sigFlags != 0) entries.push_back({"SigFlags", CosObject::makeInteger(sigFlags)});
    if (appearanceString) entries.push_back({"DA", *appearanceString});
    if (quadding) entries.push_back({"Q", *quadding});
    if (!kinds.empty()) {
        std::vector<CosObject::Entry> dr;
        for (const std::string& kind : kinds) dr.push_back({kind, CosObject::makeDictionary(resources[kind])});
        entries.push_back({"DR", CosObject::makeDictionary(std::move(dr))});
    }
    // `/CO` without a field a merge took away.
    std::vector<CosObject> calculated;
    for (const CosObject& item : order) {
        if (objects_.count(item.reference().number) != 0) calculated.push_back(item);
    }
    if (!calculated.empty()) entries.push_back({"CO", CosObject::makeArray(std::move(calculated))});
    catalogExtra_.push_back({"AcroForm", CosObject::makeDictionary(std::move(entries))});
}

// THE LAYERS -- see the header.
void Joiner::layers()
{
    std::vector<CosObject> groups;
    std::vector<CosObject> order;
    std::vector<CosObject> off;
    std::vector<CosObject> locked;
    std::vector<CosObject> radios;
    std::vector<std::pair<std::string, CosObject>> usage;   // (event and category, as written) -> the application
    for (std::size_t k = 0; k < taken_.size(); ++k) {
        const CosObject properties = resolve(parts_[k].catalog.get("OCProperties"));
        if (!properties.isDictionary()) continue;
        const std::vector<CosObject> mine = resolve(properties.get("OCGs")).items();
        if (mine.empty()) continue;
        for (const CosObject& group : mine) groups.push_back(group);
        const CosObject d = resolve(properties.get("D"));
        std::vector<CosObject> labelled{AnnotationOps::textString(taken_[k].label)};
        for (const CosObject held563 = resolve(d.get("Order")); const CosObject& item : held563.items()) labelled.push_back(item);
        order.push_back(CosObject::makeArray(std::move(labelled)));
        for (const CosObject held565 = resolve(d.get("OFF")); const CosObject& item : held565.items()) off.push_back(item);
        for (const CosObject held566 = resolve(d.get("Locked")); const CosObject& item : held566.items()) locked.push_back(item);
        for (const CosObject held567 = resolve(d.get("RBGroups")); const CosObject& item : held567.items()) radios.push_back(item);
        for (const CosObject held568 = resolve(d.get("AS")); const CosObject& entry : held568.items()) {
            const CosObject application = resolve(entry);
            if (!application.isDictionary()) continue;
            const std::string key = cos::serializeObject(application.get("Event")) + ' ' + cos::serializeObject(application.get("Category"));
            const auto found = std::find_if(usage.begin(), usage.end(), [&key](const auto& u) { return u.first == key; });
            if (found == usage.end()) {
                usage.emplace_back(key, application);
                continue;
            }
            std::vector<CosObject> members = resolve(found->second.get("OCGs")).items();
            for (const CosObject held578 = resolve(application.get("OCGs")); const CosObject& group : held578.items()) members.push_back(group);
            found->second = with(found->second, "OCGs", CosObject::makeArray(std::move(members)));
        }
    }
    if (groups.empty()) return;
    std::vector<CosObject::Entry> d{{"Order", CosObject::makeArray(std::move(order))}};
    if (!off.empty()) d.push_back({"OFF", CosObject::makeArray(std::move(off))});
    if (!locked.empty()) d.push_back({"Locked", CosObject::makeArray(std::move(locked))});
    if (!radios.empty()) d.push_back({"RBGroups", CosObject::makeArray(std::move(radios))});
    if (!usage.empty()) {
        std::vector<CosObject> applications;
        for (auto& [key, application] : usage) applications.push_back(std::move(application));
        d.push_back({"AS", CosObject::makeArray(std::move(applications))});
    }
    catalogExtra_.push_back({"OCProperties", CosObject::makeDictionary({{"OCGs", CosObject::makeArray(std::move(groups))},
                                                                        {"D", CosObject::makeDictionary(std::move(d))}})});
}

// The `/Title` an input's Info dictionary states, or nothing.
QString titleOf(cos::XrefTable& table)
{
    CosObject info = table.trailer().get("Info");
    if (info.isReference()) {
        auto fetched = table.fetch(info.reference().number);
        info = fetched ? fetched.value().object : CosObject::makeNull();
    }
    CosObject title = info.get("Title");
    if (title.isReference()) {
        auto fetched = table.fetch(title.reference().number);
        title = fetched ? fetched.value().object : CosObject::makeNull();
    }
    return textOf(title).trimmed();
}

} // namespace

QString DocumentMerger::renamed(const QString& name, int n)
{
    return name + QLatin1Char('_') + QString::number(n);
}

core::Result<core::MergedDocument> DocumentMerger::merge(const std::vector<Input>& inputs, const core::MergeOptions& options,
                                                         const core::MergeProgress& progress)
{
    if (inputs.empty()) return mergeError(core::ErrorCode::InvalidArgument, "error.merge.noInputs");
    const int total = static_cast<int>(inputs.size()) + 2;
    int done = 0;
    const auto proceed = [&] { return !progress || progress(done, total); };
    const auto cancelled = [] { return mergeError(core::ErrorCode::Unavailable, "error.merge.cancelled"); };

    core::MergedDocument out;
    core::MergeReport& report = out.report;
    report.references.resize(inputs.size());
    std::vector<Taken> taken;
    for (std::size_t i = 0; i < inputs.size(); ++i, ++done) {
        if (!proceed()) return cancelled();
        const Input& input = inputs[i];
        const auto skip = [&](const core::Error& error) {
            report.skipped.push_back(core::MergeSkip{static_cast<int>(i), input.name, QString(error.messageKey), error.context});
        };
        core::Result<std::string> bytes = input.read ? input.read() : mergeError(core::ErrorCode::IoFailure, "error.open.notFound");
        if (!bytes) {
            skip(bytes.error());
            continue;
        }
        if (!looksLikePdf(bytes.value())) {
            skip(mergeError(core::ErrorCode::ParseFailure, "error.open.notPdf"));
            continue;
        }
        auto opened = cos::openWithRepair(bytes.value());
        if (!opened) {
            skip(opened.error());
            continue;
        }
        cos::XrefTable& table = opened.value().table;
        if (table.isEncrypted()) {
            skip(mergeError(core::ErrorCode::PermissionDenied, "error.pdf.encrypted"));
            continue;
        }
        if (opened.value().report.worstSeverity() >= core::RepairSeverity::Degraded) {
            skip(mergeError(core::ErrorCode::Corrupted, "error.merge.repairedWithLoss",
                            QString::number(static_cast<qulonglong>(opened.value().report.objectsReconstructed()))));
            continue;
        }
        auto walk = PageOps::walk(table);
        if (!walk) {
            skip(walk.error());
            continue;
        }
        if (!walk.value().notes.empty()) {
            skip(mergeError(core::ErrorCode::Unsupported, "error.pdf.pageTreeDamaged", QString::number(walk.value().notes.front().objectNumber)));
            continue;
        }
        const int count = static_cast<int>(walk.value().pages.size());
        std::vector<int> pages = input.pages;
        if (pages.empty()) {
            for (int p = 0; p < count; ++p) pages.push_back(p);
        }
        if (pages.empty()) {
            skip(mergeError(core::ErrorCode::InvalidArgument, "error.pdf.pageOutOfRange", QStringLiteral("0")));
            continue;
        }
        ObjectGraphCopier::Options copy;
        copy.fields = true;
        copy.outline = true;
        copy.allLayers = true;
        auto copied = ObjectGraphCopier::copy(table, walk.value(), pages, copy);
        if (!copied) {
            skip(copied.error());
            continue;
        }
        const QString title = options.bookmarks == core::MergeBookmarks::Title ? titleOf(table) : QString();
        report.references[i] = std::move(copied.value().report);
        taken.push_back(Taken{static_cast<int>(i), title.isEmpty() ? input.name : title, std::move(copied.value().pdf), copied.value().pageCount});
    }
    if (taken.empty())
        return mergeError(core::ErrorCode::InvalidArgument, "error.merge.nothingMerged", QString::number(static_cast<int>(report.skipped.size())));
    if (!proceed()) return cancelled();
    report.inputsMerged = static_cast<int>(taken.size());

    Joiner joiner(taken, options, report);
    core::Result<std::string> payload = joiner.run();
    if (!payload) return payload.error();
    // The seed: this process's clock and what was merged -- two merges of the same files are two documents.
    std::string seed = "merge:" + std::to_string(QDateTime::currentMSecsSinceEpoch());
    for (const Taken& t : taken) seed += ':' + t.label.toStdString() + '/' + std::to_string(t.pageCount);
    auto built = DocumentAssembler::build(payload.value(), seed);
    if (!built) return built.error();
    ++done;
    if (!proceed()) return cancelled();
    if (core::Status verified = DocumentAssembler::verify(built.value(), payload.value()); !verified) return verified.error();
    ++done;
    if (progress) (void)progress(done, total);
    out.bytes = QByteArray(built.value().data(), static_cast<qsizetype>(built.value().size()));
    return out;
}

core::Result<core::MergedDocument> DocumentMerger::mergeFiles(const std::vector<core::MergeInput>& inputs, const core::MergeOptions& options,
                                                              const core::MergeProgress& progress)
{
    std::vector<Input> read;
    read.reserve(inputs.size());
    for (const core::MergeInput& input : inputs) {
        const QString path = input.path;
        read.push_back(Input{QFileInfo(path).completeBaseName(), input.pages, [path]() -> core::Result<std::string> {
                                 QFile file(path);
                                 if (!file.open(QIODevice::ReadOnly))
                                     return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.open.notFound"), path};
                                 const QByteArray bytes = file.readAll();
                                 return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
                             }});
    }
    auto merged = merge(read, options, progress);
    if (merged) {
        // The skipped inputs named by PATH, which is what the person picked.
        for (core::MergeSkip& skipped : merged.value().report.skipped) skipped.path = inputs[static_cast<std::size_t>(skipped.input)].path;
    }
    return merged;
}

} // namespace rawpage::pdf::ops
