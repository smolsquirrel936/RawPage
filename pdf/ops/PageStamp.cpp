// pdf/ops/PageStamp -- implementation. Stage 7 T13. See the header for what is read, what is drawn and what is not.

#include "pdf/ops/PageStamp.h"

#include "core/doc/CopiedPages.h"
#include "core/doc/Page.h"
#include "core/doc/Stamps.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/cos/filters/Filter.h"
#include "pdf/cos/filters/FlateFilter.h"
#include "pdf/ops/PageOps.h"

#include <QLatin1String>
#include <QString>

#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;

core::Error stampError(const char* what, long long detail = -1)
{
    return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.edit.stampPageUnsupported"),
                       detail < 0 ? QLatin1String(what) : QStringLiteral("%1:%2").arg(QLatin1String(what)).arg(detail)};
}

// A payload's objects bounded as the sink bounds a graph's (`MutationSinkImpl::parseGraph`).
constexpr std::size_t kMaxEntries = 2'000'000;

std::optional<core::PdfRect> rectOf(const std::optional<std::array<double, 4>>& box)
{
    if (!box) return std::nullopt;
    const std::array<double, 4>& b = *box;
    for (const double v : b) {
        if (!std::isfinite(v)) return std::nullopt;
    }
    return core::normalizedRect(b[0], b[1], b[2], b[3]);
}

// The payload, read: its table, its one page and what that page shows.
struct Read {
    cos::XrefTable table;
    long long page = 0;
    CosObject dictionary;
    core::PdfRect box;
    int rotate = 0;
    std::set<long long> frame;   // the page, its node, the catalog: never copied into a form
};

core::Result<Read> readPayload(const QByteArray& graph)
{
    if (graph.isEmpty() || graph.size() > core::kMaxCopiedGraphBytes) return stampError("size", graph.size());
    auto table = cos::XrefTable::load(std::string_view(graph.constData(), static_cast<std::size_t>(graph.size())));
    if (!table) return stampError("xref");
    if (table.value().isEncrypted()) return stampError("encrypted");
    if (table.value().entryCount() > kMaxEntries) return stampError("objects", static_cast<long long>(table.value().entryCount()));
    Read out{std::move(table).value(), 0, CosObject::makeNull(), {}, 0, {}};
    auto walk = PageOps::walk(out.table);
    if (!walk || !walk.value().notes.empty()) return stampError("pages");
    if (walk.value().pages.size() != 1) return stampError("pages", static_cast<long long>(walk.value().pages.size()));
    const PageEntry& entry = walk.value().pages.front();
    out.page = entry.number;
    auto fetched = out.table.fetch(entry.number);
    if (!fetched || !fetched.value().object.isDictionary()) return stampError("page", entry.number);
    out.dictionary = fetched.value().object;
    // What the page shows, as `core::Page` resolves it: the MediaBox (US Letter where none is written, as PDFium
    // draws one) and the CropBox within it.
    const std::optional<core::PdfRect> media = rectOf(entry.mediaBox);
    core::Page shown(0, media.value_or(core::PdfRect{core::PdfPoint{0.0, 0.0}, core::PdfSize{612.0, 792.0}}));
    shown.setCropBox(rectOf(entry.cropBox));
    out.box = shown.cropBox();
    out.rotate = ((entry.effectiveRotate() % 360) + 360) % 360;
    if (out.rotate % 90 != 0) out.rotate = 0;   // `PageOps` normalizes it; a stray value draws upright, as PDFium draws it
    if (!(out.box.size.width >= 3.0) || !(out.box.size.height >= 3.0) || out.box.size.width > 14400.0
        || out.box.size.height > 14400.0)
        return stampError("box");
    out.frame.insert(entry.number);
    if (auto shape = PageOps::shape(out.table)) {
        for (const PageTreeNode& node : shape.value().nodes) out.frame.insert(node.number);
    }
    const CosObject& root = out.table.trailer().get("Root");
    if (root.isReference()) out.frame.insert(root.reference().number);
    return out;
}

// `value` resolved one level -- a reference to what it names, anything else itself; null for a reference that does not
// fetch.
CosObject resolved(cos::XrefTable& table, const CosObject& value)
{
    if (!value.isReference()) return value;
    auto fetched = table.fetch(value.reference().number);
    return fetched ? fetched.value().object : CosObject::makeNull();
}

// The page's content as the streams it is drawn from, in order. An absent or empty `/Contents` is no stream at all.
core::Result<std::vector<CosObject>> contentStreams(cos::XrefTable& table, const CosObject& contents)
{
    std::vector<CosObject> out;
    const CosObject value = resolved(table, contents);
    if (value.isNull()) return out;
    if (value.isStream()) {
        out.push_back(value);
        return out;
    }
    if (!value.isArray()) return stampError("contents");
    for (const CosObject& item : value.items()) {
        const CosObject stream = resolved(table, item);
        if (stream.isNull()) continue;   // a reference to nothing draws nothing, as PDFium skips it
        if (!stream.isStream()) return stampError("contents");
        out.push_back(stream);
    }
    return out;
}

// Every payload number reached from `roots`, the frame left out -- breadth first, each once.
std::vector<long long> reachable(cos::XrefTable& table, const std::vector<CosObject>& roots, const std::set<long long>& frame)
{
    std::set<long long> seen;
    std::deque<CosObject> queue(roots.begin(), roots.end());
    while (!queue.empty()) {
        const CosObject value = std::move(queue.front());
        queue.pop_front();
        switch (value.type()) {
        case cos::CosType::Reference: {
            const long long number = value.reference().number;
            if (frame.count(number) != 0 || !seen.insert(number).second) break;
            if (seen.size() > kMaxEntries) return {};
            if (auto fetched = table.fetch(number)) queue.push_back(fetched.value().object);
            break;
        }
        case cos::CosType::Array:
            for (const CosObject& item : value.items()) queue.push_back(item);
            break;
        case cos::CosType::Dictionary:
        case cos::CosType::Stream:
            for (const CosObject::Entry& entry : value.entries()) queue.push_back(entry.value);
            break;
        default:
            break;
        }
    }
    return std::vector<long long>(seen.begin(), seen.end());
}

// `value` with every reference renumbered through `map`: a number not in it -- the frame, or what does not fetch -- null.
CosObject mapped(const std::map<long long, cos::CosRef>& map, const CosObject& value)
{
    switch (value.type()) {
    case cos::CosType::Reference: {
        const auto found = map.find(value.reference().number);
        return found != map.end() ? CosObject::makeReference(found->second.number, found->second.generation) : CosObject::makeNull();
    }
    case cos::CosType::Array: {
        std::vector<CosObject> items;
        items.reserve(value.items().size());
        for (const CosObject& item : value.items()) items.push_back(mapped(map, item));
        return CosObject::makeArray(std::move(items));
    }
    case cos::CosType::Dictionary:
    case cos::CosType::Stream: {
        std::vector<CosObject::Entry> entries;
        entries.reserve(value.entries().size());
        for (const CosObject::Entry& entry : value.entries()) entries.push_back({entry.key, mapped(map, entry.value)});
        return value.isStream() ? CosObject::makeStream(std::move(entries), cos::CosStreamData{})
                                : CosObject::makeDictionary(std::move(entries));
    }
    default:
        return value;
    }
}

std::string bytesOf(const cos::Bytes& bytes) { return std::string(bytes.begin(), bytes.end()); }

// The form's content: ONE stream as it is written -- its bytes and its filters -- or several decoded and joined, the
// result `/FlateDecode`d. Refused for a stream no filter here reads, so a stamp never paints a page short.
struct Content {
    std::string data;
    std::optional<CosObject> filter;
    std::optional<CosObject> parms;
};

core::Result<Content> contentOf(cos::XrefTable& table, const std::vector<CosObject>& streams)
{
    Content out;
    if (streams.size() == 1) {
        if (!cos::decodeStream(streams.front(), table.source())) return stampError("filter");
        out.data = std::string(streams.front().rawStreamData(table.source()));
        if (const CosObject* filter = streams.front().find("Filter")) out.filter = *filter;
        if (const CosObject* parms = streams.front().find("DecodeParms")) out.parms = *parms;
        return out;
    }
    std::string joined;
    for (const CosObject& stream : streams) {
        auto decoded = cos::decodeStream(stream, table.source());
        if (!decoded) return stampError("filter");
        if (!joined.empty()) joined += '\n';
        joined += bytesOf(decoded.value());
    }
    if (joined.empty()) return out;
    auto packed = cos::filters::flateEncode(joined);
    if (!packed) return stampError("filter");
    out.data = bytesOf(packed.value());
    out.filter = CosObject::makeName("FlateDecode");
    return out;
}

CosObject boxArray(const core::PdfRect& box)
{
    return CosObject::makeArray({CosObject::makeReal(box.left()), CosObject::makeReal(box.bottom()),
                                 CosObject::makeReal(box.right()), CosObject::makeReal(box.top())});
}

} // namespace

core::Result<core::StampPage> PageStamp::fromPayload(const QByteArray& graph)
{
    auto read = readPayload(graph);
    if (!read) return read.error();
    const bool turned = read.value().rotate == 90 || read.value().rotate == 270;
    core::StampPage page{graph, turned ? read.value().box.size.height : read.value().box.size.width,
                         turned ? read.value().box.size.width : read.value().box.size.height};
    // Drawn once here, its objects kept nowhere: what a stamp will meet when it is placed, met now.
    CollectedAppearanceObjects dry;
    auto drawn = form(page, dry);
    if (!drawn) return drawn.error();
    return page;
}

core::Result<PageStamp::Form> PageStamp::form(const core::StampPage& page, AppearanceObjects& objects)
{
    auto read = readPayload(page.graph);
    if (!read) return read.error();
    Read& r = read.value();
    auto streams = contentStreams(r.table, r.dictionary.get("Contents"));
    if (!streams) return streams.error();
    auto content = contentOf(r.table, streams.value());
    if (!content) return content.error();

    // Every object the form names, each reserved under its key before any is written.
    const std::string prefix = "stamppage:" + core::stampPageKey(page).toStdString() + ":";
    std::vector<CosObject> roots{r.dictionary.get("Resources"), r.dictionary.get("Group")};
    if (content.value().filter) roots.push_back(*content.value().filter);
    if (content.value().parms) roots.push_back(*content.value().parms);
    const std::vector<long long> numbers = reachable(r.table, roots, r.frame);
    std::map<long long, cos::CosRef> map;
    for (const long long number : numbers) map.emplace(number, objects.reserved(prefix + std::to_string(number)));
    for (const long long number : numbers) {
        const std::string key = prefix + std::to_string(number);
        auto fetched = r.table.fetch(number);
        if (!fetched) {
            objects.shared(key, CosObject::makeNull());
            continue;
        }
        const CosObject& value = fetched.value().object;
        if (value.isStream())
            objects.sharedStream(key, mapped(map, value), std::string(value.rawStreamData(r.table.source())));
        else
            objects.shared(key, mapped(map, value));
    }

    std::vector<CosObject::Entry> entries{
        {"Type", CosObject::makeName("XObject")},
        {"Subtype", CosObject::makeName("Form")},
        {"BBox", boxArray(r.box)},
    };
    const CosObject resources = r.dictionary.get("Resources");
    entries.push_back({"Resources", resources.isNull() ? CosObject::makeDictionary({}) : mapped(map, resources)});
    if (const CosObject group = r.dictionary.get("Group"); !group.isNull()) entries.push_back({"Group", mapped(map, group)});
    if (content.value().filter) entries.push_back({"Filter", mapped(map, *content.value().filter)});
    if (content.value().parms) entries.push_back({"DecodeParms", mapped(map, *content.value().parms)});
    const cos::CosRef at = objects.sharedStream(prefix + "form", CosObject::makeStream(std::move(entries), cos::CosStreamData{}),
                                                std::move(content.value().data));
    return Form{at, r.box, r.rotate};
}

std::array<double, 6> PageStamp::showMatrix(const core::PdfRect& box, int rotate, double width, double height) noexcept
{
    const double x0 = box.left();
    const double y0 = box.bottom();
    const double x1 = box.right();
    const double y1 = box.top();
    const bool turned = rotate == 90 || rotate == 270;
    const double shownWidth = turned ? box.size.height : box.size.width;
    const double shownHeight = turned ? box.size.width : box.size.height;
    // The page as a reader shows it -- turned CLOCKWISE by `/Rotate` (ISO 32000-1 Table 30), its lower-left corner at
    // the origin -- as `x' = a x + c y + e`, `y' = b x + d y + f`.
    std::array<double, 6> m{1.0, 0.0, 0.0, 1.0, -x0, -y0};
    switch (rotate) {
    case 90: m = {0.0, -1.0, 1.0, 0.0, -y0, x1}; break;
    case 180: m = {-1.0, 0.0, 0.0, -1.0, x1, y1}; break;
    case 270: m = {0.0, 1.0, -1.0, 0.0, y1, -x0}; break;
    default: break;
    }
    const double sx = shownWidth > 0.0 ? width / shownWidth : 1.0;
    const double sy = shownHeight > 0.0 ? height / shownHeight : 1.0;
    return {m[0] * sx, m[1] * sy, m[2] * sx, m[3] * sy, m[4] * sx, m[5] * sy};
}

} // namespace rawpage::pdf::ops
