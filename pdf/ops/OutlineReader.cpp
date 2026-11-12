#include "pdf/ops/OutlineReader.h"

#include "pdf/ops/EmbeddedFileReader.h"

#include <algorithm>
#include <array>
#include <set>

namespace rawpage::pdf::ops {

using cos::CosObject;
using core::Destination;
using core::OutlineAction;
using core::OutlineItem;

namespace {

constexpr int kMaxResolve = 8;   // references followed to reach one value

long long referenced(const CosObject& value)
{
    return value.isReference() ? value.reference().number : -1;
}

QString textOf(const CosObject& value)
{
    if (value.isString()) return EmbeddedFileReader::decodeTextString(value.text());
    if (value.isName()) return QString::fromStdString(value.text());
    return {};
}

// A file specification's name: a string, or a dictionary's `/UF` before its `/F`.
QString fileSpecName(const ReferenceRepair::View& view, const CosObject& value)
{
    const std::optional<CosObject> spec = view.resolve(value);
    if (!spec) return {};
    if (spec->isString()) return textOf(*spec);
    if (spec->isDictionary()) {
        if (const CosObject* uf = spec->find("UF"); uf && uf->isString()) return textOf(*uf);
        if (const CosObject* f = spec->find("F"); f && f->isString()) return textOf(*f);
    }
    return {};
}

// A destination value -- an array, a name or string naming one, or a dictionary's `/D` -- read into `action` and
// `facts`, resolved as far as this file can resolve it.
void readDestination(const ReferenceRepair::View& view, const CosObject& value, const std::map<std::string, CosObject>& named,
                     OutlineAction& action, OutlineReader::Facts& facts, int depth = 0)
{
    if (depth > kMaxResolve) return;
    if (value.isReference()) {
        if (const std::optional<CosObject> resolved = view.resolve(value))
            readDestination(view, *resolved, named, action, facts, depth + 1);
        return;
    }
    if (value.isDictionary()) {
        readDestination(view, value.get("D"), named, action, facts, depth + 1);
        return;
    }
    if (value.isName() || value.isString()) {
        action.kind = OutlineAction::Kind::GoToNamed;
        action.name = textOf(value);
        facts.name = value.text();
        facts.nameIsString = value.isString();
        const auto found = named.find(value.text());
        if (found != named.end() && found->second.isArray() && !found->second.items().empty()) {
            const CosObject& first = found->second.items().front();
            action.destination = OutlineReader::viewOf(found->second);
            if (first.isReference()) facts.enginePage = first.reference().number;
            else if (first.isInteger()) facts.engineIndex = static_cast<int>(first.integerValue());
        }
        return;
    }
    if (value.isArray() && !value.items().empty()) {
        action.kind = OutlineAction::Kind::GoTo;
        action.destination = OutlineReader::viewOf(value);
        const CosObject& first = value.items().front();
        if (first.isReference()) {
            facts.page = first.reference().number;
            facts.enginePage = facts.page;
        } else if (first.isInteger()) {
            facts.index = static_cast<int>(first.integerValue());
            facts.engineIndex = facts.index;
        }
    }
}

void readAction(const ReferenceRepair::View& view, const CosObject& value, const std::map<std::string, CosObject>& named,
                OutlineAction& action, OutlineReader::Facts& facts)
{
    const std::optional<CosObject> dictionary = view.resolve(value);
    if (!dictionary || !dictionary->isDictionary()) return;
    const CosObject& kind = dictionary->get("S");
    const std::string type = kind.isName() ? kind.text() : std::string();
    if (type == "GoTo") {
        readDestination(view, dictionary->get("D"), named, action, facts);
        return;
    }
    if (type == "GoToR") {
        action.kind = OutlineAction::Kind::GoToRemote;
        action.file = fileSpecName(view, dictionary->get("F"));
        const std::optional<CosObject> d = view.resolve(dictionary->get("D"));
        if (d && d->isArray() && !d->items().empty()) {
            action.destination = OutlineReader::viewOf(*d);
            const CosObject& first = d->items().front();
            // ANOTHER FILE'S page number, which PDFium hands the panes as an index -- the engine's reading is kept
            // for the agreement alone; the model's destination is this file's, and there is none.
            if (first.isInteger()) {
                action.destination.pageIndex = static_cast<int>(first.integerValue());
                facts.engineIndex = action.destination.pageIndex;
            }
        } else if (d && (d->isName() || d->isString())) {
            action.name = textOf(*d);
        }
        return;
    }
    if (type == "Launch") {
        action.kind = OutlineAction::Kind::Launch;
        action.file = fileSpecName(view, dictionary->get("F"));
        return;
    }
    if (type == "URI") {
        action.kind = OutlineAction::Kind::Uri;
        const std::optional<CosObject> uri = view.resolve(dictionary->get("URI"));
        if (uri && uri->isString()) action.uri = QString::fromLatin1(uri->text().data(), static_cast<qsizetype>(uri->text().size()));
        return;
    }
    if (type == "JavaScript") {
        action.kind = OutlineAction::Kind::JavaScript;
        const std::optional<CosObject> js = view.resolve(dictionary->get("JS"));
        if (js && js->isString()) action.script = textOf(*js);
        return;
    }
    if (type == "Named") {
        action.kind = OutlineAction::Kind::Named;
        const CosObject& n = dictionary->get("N");
        if (n.isName()) action.name = QString::fromStdString(n.text());
        return;
    }
    if (type == "SubmitForm") {
        action.kind = OutlineAction::Kind::SubmitForm;
        action.uri = fileSpecName(view, dictionary->get("F"));
        return;
    }
    // Stage 7 T12 (D7-12): the other form actions -- read, written, never run.
    if (type == "ResetForm") {
        action.kind = OutlineAction::Kind::ResetForm;
        return;
    }
    if (type == "ImportData") {
        action.kind = OutlineAction::Kind::ImportData;
        action.file = fileSpecName(view, dictionary->get("F"));
        return;
    }
    if (type == "Hide") {
        action.kind = OutlineAction::Kind::Hide;
        const std::optional<CosObject> target = view.resolve(dictionary->get("T"));
        if (target && target->isString()) action.name = textOf(*target);
        const CosObject& hide = dictionary->get("H");
        action.hide = !hide.isBoolean() || hide.booleanValue();
        return;
    }
    action.kind = OutlineAction::Kind::Other;
    action.name = QString::fromStdString(type);
}

std::pair<OutlineItem, OutlineReader::Facts> itemOf(const ReferenceRepair::View& view, long long number, CosObject dictionary,
                                                    const std::map<std::string, CosObject>& named)
{
    OutlineItem item;
    OutlineReader::Facts facts;
    item.id = core::OutlineItemId::ofObject(number);
    if (const std::optional<CosObject> title = view.resolve(dictionary.get("Title")); title && title->isString())
        item.title = EmbeddedFileReader::decodeTextString(title->text());

    OutlineReader::readTarget(view, dictionary, named, item.action, facts);

    if (const std::optional<CosObject> c = view.resolve(dictionary.get("C")); c && c->isArray() && c->items().size() == 3) {
        std::array<double, 3> rgb{};
        bool numbers = true;
        for (std::size_t i = 0; i < 3; ++i) {
            numbers = numbers && c->items()[i].isNumber();
            rgb[i] = std::clamp(c->items()[i].numberValue(), 0.0, 1.0);
        }
        if (numbers) item.colour = rgb;
    }
    if (const std::optional<CosObject> f = view.resolve(dictionary.get("F")); f && f->isInteger()) {
        item.italic = (f->integerValue() & 1) != 0;
        item.bold = (f->integerValue() & 2) != 0;
    }
    const CosObject& count = dictionary.get("Count");
    if (count.isInteger()) {
        facts.count = count.integerValue();
        item.openByDefault = count.integerValue() > 0;
    }
    facts.parent = referenced(dictionary.get("Parent"));
    facts.first = referenced(dictionary.get("First"));
    facts.last = referenced(dictionary.get("Last"));
    facts.next = referenced(dictionary.get("Next"));
    facts.prev = referenced(dictionary.get("Prev"));
    facts.dictionary = std::move(dictionary);
    return {std::move(item), std::move(facts)};
}

struct Walker {
    const ReferenceRepair::View& view;
    const std::map<std::string, CosObject>& named;
    OutlineReader::Reading& reading;
    std::set<long long> visited;
    std::size_t budget = core::kMaxOutlineNodes;

    void level(long long first, int depth, std::vector<OutlineItem>& into)
    {
        if (first < 0) return;
        if (depth >= core::kMaxOutlineDepth) {
            reading.truncated = true;
            return;
        }
        for (long long number = first; number >= 0;) {
            if (budget == 0 || !visited.insert(number).second) {
                reading.truncated = true;
                return;
            }
            --budget;
            std::optional<CosObject> dictionary = view.object(number);
            if (!dictionary || !dictionary->isDictionary()) {
                reading.truncated = true;
                return;
            }
            auto [item, facts] = itemOf(view, number, std::move(*dictionary), named);
            const long long firstChild = facts.first;
            const long long next = facts.next;
            reading.facts.emplace(number, std::move(facts));
            level(firstChild, depth + 1, item.children);
            into.push_back(std::move(item));
            number = next;
        }
    }
};

} // namespace

Destination OutlineReader::viewOf(const CosObject& array)
{
    Destination destination;
    if (!array.isArray() || array.items().size() < 2) return destination;
    const std::vector<CosObject>& items = array.items();
    const CosObject& mode = items[1];
    const std::string fit = mode.isName() ? mode.text() : std::string();
    if (fit == "XYZ") destination.fit = Destination::Fit::XYZ;
    else if (fit == "Fit") destination.fit = Destination::Fit::Fit;
    else if (fit == "FitH") destination.fit = Destination::Fit::FitH;
    else if (fit == "FitV") destination.fit = Destination::Fit::FitV;
    else if (fit == "FitR") destination.fit = Destination::Fit::FitR;
    else if (fit == "FitB") destination.fit = Destination::Fit::FitB;
    else if (fit == "FitBH") destination.fit = Destination::Fit::FitBH;
    else if (fit == "FitBV") destination.fit = Destination::Fit::FitBV;
    const auto parameter = [&items](std::size_t i, bool& has, double& value) {
        if (i < items.size() && items[i].isNumber()) {
            has = true;
            value = items[i].numberValue();
        }
    };
    switch (destination.fit) {
    case Destination::Fit::XYZ:
        parameter(2, destination.hasX, destination.x);
        parameter(3, destination.hasY, destination.y);
        parameter(4, destination.hasZoom, destination.zoom);
        break;
    // PDFium reports the one parameter of these whenever it is there, a null read as 0 (`PdfiumDocument`).
    case Destination::Fit::FitH:
    case Destination::Fit::FitBH:
        if (items.size() >= 3) {
            destination.hasY = true;
            destination.y = items[2].numberValue();
        }
        break;
    case Destination::Fit::FitV:
    case Destination::Fit::FitBV:
        if (items.size() >= 3) {
            destination.hasX = true;
            destination.x = items[2].numberValue();
        }
        break;
    default:
        break;
    }
    return destination;
}

void OutlineReader::readTarget(const ReferenceRepair::View& view, const CosObject& dictionary,
                               const std::map<std::string, CosObject>& named, OutlineAction& action, Facts& facts)
{
    // `/Dest` FIRST, as ISO 32000-1 §12.3.3 and PDFium both have it; `/A` only where there is none.
    if (const CosObject* dest = dictionary.find("Dest")) {
        readDestination(view, *dest, named, action, facts);
        // PDFium falls back to the action when the destination resolves to nothing -- for the panes' target alone.
        if (facts.enginePage < 0 && facts.engineIndex < 0) {
            if (const CosObject* a = dictionary.find("A")) {
                OutlineAction ignored;
                OutlineReader::Facts fallback;
                readAction(view, *a, named, ignored, fallback);
                facts.enginePage = fallback.enginePage;
                facts.engineIndex = fallback.engineIndex;
            }
        }
    } else if (const CosObject* a = dictionary.find("A")) {
        readAction(view, *a, named, action, facts);
    }
}

OutlineReader::Reading OutlineReader::read(const ReferenceRepair::View& view)
{
    Reading reading;
    const long long catalogNumber = view.catalogNumber();
    if (catalogNumber < 0) return reading;
    const std::optional<CosObject> catalog = view.object(catalogNumber);
    if (!catalog || !catalog->isDictionary()) return reading;
    const CosObject* outlines = catalog->find("Outlines");
    if (outlines == nullptr) return reading;
    reading.root = referenced(*outlines);
    std::optional<CosObject> root = view.resolve(*outlines);
    if (!root || !root->isDictionary()) return reading;
    reading.rootDictionary = root;

    const std::map<std::string, CosObject> named = ReferenceRepair::namedArrays(view);
    Walker walker{view, named, reading, {}, core::kMaxOutlineNodes};
    if (reading.root >= 0) walker.visited.insert(reading.root);
    walker.level(referenced(root->get("First")), 0, reading.outline.roots());
    return reading;
}

std::optional<std::pair<OutlineItem, OutlineReader::Facts>>
OutlineReader::readItem(const ReferenceRepair::View& view, long long number, const std::map<std::string, CosObject>& named)
{
    std::optional<CosObject> dictionary = view.object(number);
    if (!dictionary || !dictionary->isDictionary()) return std::nullopt;
    return itemOf(view, number, std::move(*dictionary), named);
}

void OutlineReader::resolveItem(OutlineItem& item, const Facts& facts, const std::map<long long, int>& indexOf)
{
    const auto place = [&indexOf](long long page) {
        const auto found = indexOf.find(page);
        return found == indexOf.end() ? -1 : found->second;
    };
    switch (item.action.kind) {
    case OutlineAction::Kind::GoTo:
        item.action.destination.pageIndex = facts.page >= 0 ? place(facts.page) : facts.index;
        item.destination = item.action.destination;
        break;
    case OutlineAction::Kind::GoToNamed:
        item.action.destination.pageIndex = facts.enginePage >= 0 ? place(facts.enginePage) : facts.engineIndex;
        item.destination = item.action.destination;
        break;
    default:
        item.destination = Destination{};
        break;
    }
}

void OutlineReader::resolve(Reading& reading, const std::map<long long, int>& indexOf)
{
    std::vector<std::vector<OutlineItem>*> stack{&reading.outline.roots()};
    while (!stack.empty()) {
        std::vector<OutlineItem>* level = stack.back();
        stack.pop_back();
        for (OutlineItem& item : *level) {
            const auto facts = reading.facts.find(item.id.object);
            if (facts != reading.facts.end()) resolveItem(item, facts->second, indexOf);
            stack.push_back(&item.children);
        }
    }
}

QString OutlineReader::engineTitle(const QString& title)
{
    QString out = title;
    for (QChar& c : out) {
        if (c.unicode() <= 0x20) c = QLatin1Char(' ');
    }
    return out;
}

core::Status OutlineReader::agreesWith(const core::Outline& engine, const Reading& resolved, const std::map<long long, int>& indexOf)
{
    const auto disagree = [](QString where) {
        return core::Error{core::ErrorCode::PermissionDenied, QLatin1String("error.outline.readingsDisagree"), std::move(where)};
    };
    if (resolved.truncated) return disagree(QStringLiteral("truncated"));
    if (engine.nodeCount() != resolved.outline.nodeCount())
        return disagree(QStringLiteral("count %1/%2").arg(engine.nodeCount()).arg(resolved.outline.nodeCount()));
    if (engine.depth() != resolved.outline.depth())
        return disagree(QStringLiteral("depth %1/%2").arg(engine.depth()).arg(resolved.outline.depth()));

    // Item by item, in the same order, at the same depth: the two trees have one shape.
    struct Pair {
        const std::vector<OutlineItem>* engine;
        const std::vector<OutlineItem>* ours;
    };
    std::vector<Pair> stack{{&engine.roots(), &resolved.outline.roots()}};
    int ordinal = 0;
    while (!stack.empty()) {
        const Pair pair = stack.back();
        stack.pop_back();
        if (pair.engine->size() != pair.ours->size()) return disagree(QStringLiteral("shape at item %1").arg(ordinal));
        for (std::size_t i = 0; i < pair.engine->size(); ++i, ++ordinal) {
            const OutlineItem& theirs = (*pair.engine)[i];
            const OutlineItem& mine = (*pair.ours)[i];
            if (engineTitle(theirs.title) != engineTitle(mine.title))
                return disagree(QStringLiteral("title of item %1").arg(ordinal));
            // The page PDFium hands the panes -- for a GoToR, the other file's page number.
            int page = -1;
            const auto facts = resolved.facts.find(mine.id.object);
            if (facts != resolved.facts.end()) {
                if (facts->second.engineIndex >= 0) {
                    page = facts->second.engineIndex;
                } else if (facts->second.enginePage >= 0) {
                    const auto found = indexOf.find(facts->second.enginePage);
                    page = found == indexOf.end() ? -1 : found->second;
                }
            }
            if (theirs.destination.pageIndex != page)
                return disagree(QStringLiteral("target of item %1 (%2/%3)").arg(ordinal).arg(theirs.destination.pageIndex).arg(page));
            stack.push_back({&theirs.children, &mine.children});
        }
    }
    return core::Status{};
}

} // namespace rawpage::pdf::ops
