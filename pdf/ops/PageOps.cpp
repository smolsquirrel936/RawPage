// pdf/ops/PageOps -- implementation. Stage 3 T3; Stage 7 T2 the page order. See the header for the walk's four rules.

#include "pdf/ops/PageOps.h"

#include "pdf/cos/XrefTable.h"
#include "pdf/ops/EmbeddedFileReader.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;

core::Error pageError(core::ErrorCode code, const char* key, long long detail = -1)
{
    return core::Error{code, QLatin1String(key),
                       detail < 0 ? QString() : QString::number(static_cast<qlonglong>(detail))};
}

std::optional<std::array<double, 4>> boxOf(const CosObject& value)
{
    if (!value.isArray() || value.items().size() != 4)
        return std::nullopt;
    std::array<double, 4> box{};
    for (std::size_t at = 0; at < 4; ++at) {
        if (!value.items()[at].isNumber())
            return std::nullopt;
        box[at] = value.items()[at].numberValue();
    }
    return box;
}

struct Frame {
    long long number = 0;
    int depth = 0;
    int inheritedRotate = 0;
    std::optional<std::array<double, 4>> inheritedBox;
    std::optional<std::array<double, 4>> inheritedCrop;
};

} // namespace

int PageOps::normalizeRotate(const CosObject& value) noexcept
{
    if (!value.isNumber())
        return 0;
    const double raw = value.numberValue();
    if (!std::isfinite(raw) || std::fabs(raw) > 1e9)
        return 0;
    // Truncating division, as PDFium's `GetPageRotation` does -- so -90 is one quarter turn back,
    // and 45 is none.
    const long long quarters = static_cast<long long>(raw) / 90;
    return static_cast<int>(((quarters % 4) + 4) % 4) * 90;
}

core::Result<PageWalk> PageOps::walk(cos::XrefTable& table)
{
    const CosObject& root = table.trailer().get("Root");
    if (!root.isReference())
        return pageError(core::ErrorCode::ParseFailure, "error.pdf.noPageTree");
    auto catalog = table.fetch(root.reference().number);
    if (!catalog || !catalog.value().object.isDictionary())
        return pageError(core::ErrorCode::ParseFailure, "error.pdf.noPageTree");
    const CosObject& pagesRef = catalog.value().object.get("Pages");
    if (!pagesRef.isReference())
        return pageError(core::ErrorCode::ParseFailure, "error.pdf.noPageTree");
    {
        auto top = table.fetch(pagesRef.reference().number);
        if (!top || !top.value().object.isDictionary())
            return pageError(core::ErrorCode::ParseFailure, "error.pdf.noPageTree");
    }

    PageWalk result;
    std::set<long long> visited;
    // Depth first, children pushed in reverse so they pop in document order.
    std::vector<Frame> stack{Frame{pagesRef.reference().number, 0, 0, std::nullopt, std::nullopt}};

    while (!stack.empty()) {
        Frame frame = stack.back();
        stack.pop_back();

        if (!visited.insert(frame.number).second) {
            result.notes.push_back({PageWalkNote::NodeVisitedTwice, frame.number});
            continue;
        }
        auto fetched = table.fetch(frame.number);
        if (!fetched || !fetched.value().object.isDictionary()) {
            result.notes.push_back({PageWalkNote::KidUnreadable, frame.number});
            continue;
        }
        const CosObject& node = fetched.value().object;

        const CosObject* ownRotate = node.find("Rotate");
        const std::optional<std::array<double, 4>> ownBox = boxOf(node.get("MediaBox"));
        const std::optional<std::array<double, 4>> ownCrop = boxOf(node.get("CropBox"));
        const CosObject& kids = node.get("Kids");

        if (!kids.isArray()) {
            PageEntry page;
            page.number = frame.number;
            if (ownRotate != nullptr && ownRotate->isNumber())
                page.ownRotate = normalizeRotate(*ownRotate);
            page.inheritedRotate = frame.inheritedRotate;
            page.mediaBox = ownBox ? ownBox : frame.inheritedBox;
            page.cropBox = ownCrop ? ownCrop : frame.inheritedCrop;
            result.pages.push_back(page);
            continue;
        }

        if (frame.depth >= kMaxDepth) {
            result.notes.push_back({PageWalkNote::DepthLimitExceeded, frame.number});
            continue;
        }
        const int passRotate = (ownRotate != nullptr && ownRotate->isNumber())
                                   ? normalizeRotate(*ownRotate)
                                   : frame.inheritedRotate;
        const auto passBox = ownBox ? ownBox : frame.inheritedBox;
        const auto passCrop = ownCrop ? ownCrop : frame.inheritedCrop;
        for (auto kid = kids.items().rbegin(); kid != kids.items().rend(); ++kid) {
            if (!kid->isReference()) {
                result.notes.push_back({PageWalkNote::KidNotAReference, frame.number});
                continue;
            }
            stack.push_back(Frame{kid->reference().number, frame.depth + 1, passRotate, passBox, passCrop});
        }
    }
    return result;
}

core::Result<std::optional<CosObject>> PageOps::rotatedPage(cos::XrefTable& table,
                                                            const PageEntry& page, int degrees)
{
    if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270)
        return pageError(core::ErrorCode::InvalidArgument, "error.pdf.rotationNotRightAngle",
                         degrees);

    auto fetched = table.fetch(page.number);
    if (!fetched || !fetched.value().object.isDictionary())
        return pageError(core::ErrorCode::NotFound, "error.pdf.objectMissing", page.number);
    const CosObject& current = fetched.value().object;
    const cos::XrefEntry* placed = table.find(page.number);

    // Re-read from the CURRENT dictionary rather than trusting `page`: the entry may describe an
    // older revision than the table it is handed with, and D3-5 computes against the newest.
    const CosObject* own = current.find("Rotate");
    const std::optional<int> ownNow =
        (own != nullptr && own->isNumber()) ? std::optional<int>(normalizeRotate(*own))
                                            : std::nullopt;
    const int effectiveNow = ownNow.value_or(page.inheritedRotate);
    if (effectiveNow == degrees)
        return std::optional<CosObject>{};

    // Every entry but /Rotate, in source order, and /Rotate re-added last only when it differs from
    // what the page would inherit anyway (the header's refinement of D3-5).
    std::vector<CosObject::Entry> entries;
    entries.reserve(current.entries().size() + 1);
    for (const CosObject::Entry& entry : current.entries()) {
        if (entry.key != "Rotate")
            entries.push_back(entry);
    }
    if (degrees != page.inheritedRotate)
        entries.push_back({"Rotate", CosObject::makeInteger(degrees)});

    // A PAGE THAT IS A STREAM stays one (Stage 3 guide issue I-12, found by `fuzz_cos_roundtrip`). The
    // walk calls any reachable dictionary without `/Kids` a page, and a stream is a dictionary -- so a
    // damaged file can make an object stream's container page 1. Written back as a plain dictionary,
    // the container loses its data and every object packed inside it goes with it: the catalog, the
    // page tree. So a loose stream keeps its data, copied verbatim; a stream that came out of an object
    // stream has no data in this buffer to keep, and nothing packed can live inside it.
    const bool looseStream = current.isStream() && placed != nullptr && placed->type == cos::XrefEntryType::InFile;
    if (looseStream) {
        const std::size_t size = table.source().size();
        const cos::CosStreamData& data = current.streamData();
        if (data.dataOffset <= size && data.dataLength <= size - data.dataOffset)
            return std::optional<CosObject>{CosObject::makeStream(std::move(entries), data)};
    }
    return std::optional<CosObject>{CosObject::makeDictionary(std::move(entries))};
}

// ==========================================================================================
// The page order -- Stage 7 T2. See the header.
// ==========================================================================================

core::Result<PageTreeShape> PageOps::shape(cos::XrefTable& table)
{
    auto walked = walk(table);
    if (!walked) return walked.error();
    if (!walked.value().notes.empty())
        return pageError(core::ErrorCode::Unsupported, "error.pdf.pageTreeDamaged", walked.value().notes.front().objectNumber);

    const CosObject& root = table.trailer().get("Root");
    auto catalog = table.fetch(root.reference().number);
    const long long top = catalog.value().object.get("Pages").reference().number;

    PageTreeShape out;
    out.nodes.push_back(PageTreeNode{top, -1, {}, 0});
    // Depth first; the walk read this tree with no note, so every kid is a reference to a dictionary, once.
    std::vector<int> stack{0};
    while (!stack.empty()) {
        const int at = stack.back();
        stack.pop_back();
        auto node = table.fetch(out.nodes[static_cast<std::size_t>(at)].number);
        if (!node || !node.value().object.isDictionary()) continue;
        const CosObject kids = node.value().object.get("Kids");
        for (const CosObject& kid : kids.items()) {
            if (!kid.isReference()) continue;
            auto value = table.fetch(kid.reference().number);
            if (!value || !value.value().object.isDictionary()) continue;
            PageTreeNode::Kid entry;
            entry.number = kid.reference().number;
            if (value.value().object.get("Kids").isArray()) {
                entry.leaf = false;
                entry.node = static_cast<int>(out.nodes.size());
                out.nodes.push_back(PageTreeNode{entry.number, at, {}, 0});
                stack.push_back(entry.node);
            } else {
                out.parentOf[entry.number] = at;
            }
            out.nodes[static_cast<std::size_t>(at)].kids.push_back(entry);
        }
    }
    // Pages beneath each node, children before parents: every child was appended after its parent.
    for (std::size_t at = out.nodes.size(); at-- > 0;) {
        PageTreeNode& node = out.nodes[at];
        for (const PageTreeNode::Kid& kid : node.kids)
            node.leaves += kid.leaf ? 1 : out.nodes[static_cast<std::size_t>(kid.node)].leaves;
    }
    return out;
}

std::vector<std::map<std::string, CosObject>> PageOps::inheritedValues(cos::XrefTable& table, const PageTreeShape& shape)
{
    std::vector<std::map<std::string, CosObject>> out(shape.nodes.size());
    for (std::size_t at = 0; at < shape.nodes.size(); ++at) {
        const PageTreeNode& node = shape.nodes[at];
        if (node.parent >= 0) out[at] = out[static_cast<std::size_t>(node.parent)];   // parents come first
        auto value = table.fetch(node.number);
        if (!value || !value.value().object.isDictionary()) continue;
        for (const char* key : kInheritable) {
            if (const CosObject* own = value.value().object.find(key)) {
                CosObject held = *own;
                held.clearSpansRecursively();
                out[at][key] = std::move(held);
            }
        }
    }
    return out;
}

ComposedTree PageOps::composeTree(const PageTreeShape& shape, const std::vector<long long>& order,
                                  const std::set<long long>& fresh)
{
    ComposedTree out;

    // Every kid in document order, depth first, and each page's place in that order.
    std::vector<std::pair<int, std::size_t>> walkOrder;   // (node, kid)
    {
        std::vector<std::pair<int, std::size_t>> pending{{0, 0}};
        while (!pending.empty()) {
            auto [node, kid] = pending.back();
            pending.pop_back();
            const PageTreeNode& n = shape.nodes[static_cast<std::size_t>(node)];
            if (kid >= n.kids.size()) continue;
            pending.emplace_back(node, kid + 1);
            walkOrder.emplace_back(node, kid);
            if (!n.kids[kid].leaf) pending.emplace_back(n.kids[kid].node, 0);
        }
    }
    std::map<long long, std::size_t> place;
    for (std::size_t at = 0; at < walkOrder.size(); ++at) {
        const PageTreeNode::Kid& kid = shape.nodes[static_cast<std::size_t>(walkOrder[at].first)].kids[walkOrder[at].second];
        if (kid.leaf) place.emplace(kid.number, at);
    }

    // THE PAGES THAT STAY WHERE THEY ARE: the longest run of the order's pages already in document order -- a
    // longest increasing subsequence of their places -- so a move re-lists only what moved, and a delete
    // nothing. Every other page of the order (moved, new, or back after a save removed it) is re-listed right
    // after the page before it in the order -- the last staying page before it, and the floating ones before
    // it in turn -- or, before every staying page, ahead of the first.
    std::vector<long long> kept;   // pages of the order the tree has, in the order's order
    for (const long long page : order) {
        if (fresh.count(page) == 0 && place.count(page) != 0) kept.push_back(page);
    }
    std::set<long long> staying;
    {
        std::vector<std::size_t> tails;      // index into `kept` of the smallest tail of each length
        std::vector<long long> previous(kept.size(), -1);
        for (std::size_t at = 0; at < kept.size(); ++at) {
            const std::size_t where = place.at(kept[at]);
            const auto slot = std::lower_bound(tails.begin(), tails.end(), where,
                                               [&](std::size_t t, std::size_t value) { return place.at(kept[t]) < value; });
            if (slot != tails.begin()) previous[at] = static_cast<long long>(*(slot - 1));
            if (slot == tails.end()) tails.push_back(at);
            else *slot = at;
        }
        for (long long at = tails.empty() ? -1 : static_cast<long long>(tails.back()); at >= 0; at = previous[static_cast<std::size_t>(at)])
            staying.insert(kept[static_cast<std::size_t>(at)]);
    }
    std::map<long long, std::vector<long long>> after;
    std::vector<long long> leading;
    long long anchor = -1;
    for (const long long page : order) {
        if (staying.count(page) != 0) {
            anchor = page;
            continue;
        }
        (anchor < 0 ? leading : after[anchor]).push_back(page);
    }

    // Every node's kids rebuilt: a staying page in its own place, followed by what the order lists after it.
    std::vector<std::vector<PageTreeNode::Kid>> rebuilt(shape.nodes.size());
    bool leadingPlaced = false;
    for (const auto& [node, kid] : walkOrder) {
        const PageTreeNode::Kid& k = shape.nodes[static_cast<std::size_t>(node)].kids[kid];
        std::vector<PageTreeNode::Kid>& into = rebuilt[static_cast<std::size_t>(node)];
        if (!k.leaf) {
            into.push_back(k);
            continue;
        }
        if (staying.count(k.number) == 0) continue;   // gone, or re-listed after its new predecessor
        if (!leadingPlaced) {
            for (const long long page : leading) into.push_back(PageTreeNode::Kid{true, page, -1});
            leadingPlaced = true;
        }
        into.push_back(k);
        if (const auto found = after.find(k.number); found != after.end()) {
            for (const long long added : found->second) into.push_back(PageTreeNode::Kid{true, added, -1});
        }
    }
    if (!leadingPlaced) {
        // Not one page stays (every page is new): the order is listed in the root, in order.
        for (const long long page : leading) rebuilt[0].push_back(PageTreeNode::Kid{true, page, -1});
    }

    // A node left with no page leaves its parent; then every node's pages, children before parents.
    std::vector<int> leaves(shape.nodes.size(), 0);
    for (std::size_t at = shape.nodes.size(); at-- > 0;) {
        std::vector<PageTreeNode::Kid>& kids = rebuilt[at];
        kids.erase(std::remove_if(kids.begin(), kids.end(),
                                  [&](const PageTreeNode::Kid& k) { return !k.leaf && leaves[static_cast<std::size_t>(k.node)] == 0; }),
                   kids.end());
        for (const PageTreeNode::Kid& k : kids) leaves[at] += k.leaf ? 1 : leaves[static_cast<std::size_t>(k.node)];
    }

    for (std::size_t at = 0; at < shape.nodes.size(); ++at) {
        const PageTreeNode& node = shape.nodes[at];
        if (at != 0 && leaves[at] == 0) continue;   // gone from its parent: nothing lists it
        bool same = rebuilt[at].size() == node.kids.size();
        for (std::size_t k = 0; same && k < node.kids.size(); ++k) same = rebuilt[at][k].number == node.kids[k].number;
        if (!same) {
            std::vector<CosObject> items;
            items.reserve(rebuilt[at].size());
            for (const PageTreeNode::Kid& k : rebuilt[at]) items.push_back(CosObject::makeReference(k.number, 0));
            out.kids.emplace(node.number, std::move(items));
        }
        if (leaves[at] != node.leaves) out.counts.emplace(node.number, leaves[at]);
        for (const PageTreeNode::Kid& k : rebuilt[at]) {
            if (k.leaf) out.parents.emplace(k.number, node.number);
        }
    }
    return out;
}

// ---- Page labels ---------------------------------------------------------------------------------

namespace {

QString romanOf(long long number)
{
    // PDFium's `MakeRoman`: thousands as repeated m, the number taken modulo a million.
    static const std::array<std::pair<long long, const char*>, 13> kRoman{{
        {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"}, {50, "l"},
        {40, "xl"}, {10, "x"}, {9, "ix"}, {5, "v"}, {4, "iv"}, {1, "i"}}};
    number %= 1000000;
    QString out;
    for (const auto& [value, glyphs] : kRoman) {
        while (number >= value) {
            number -= value;
            out += QLatin1String(glyphs);
        }
    }
    return out;
}

QString lettersOf(long long number)
{
    // PDFium's `MakeLetters`: a, b ... z, aa, bb ... -- one letter repeated, the count modulo 1000.
    if (number <= 0) return {};
    --number;
    const long long count = (number / 26 + 1) % 1000;
    return QString(static_cast<int>(count), QChar(QLatin1Char('a').unicode() + static_cast<int>(number % 26)));
}

} // namespace

std::vector<PageLabelRange> PageOps::readLabels(cos::XrefTable& table, int pageCount)
{
    std::vector<PageLabelRange> out;
    const CosObject& root = table.trailer().get("Root");
    if (!root.isReference() || pageCount <= 0) return out;
    auto catalog = table.fetch(root.reference().number);
    if (!catalog || !catalog.value().object.isDictionary()) return out;
    const CosObject* labels = catalog.value().object.find("PageLabels");
    if (labels == nullptr) return out;

    // The number tree's pairs, from /Nums at every node; visited, bounded.
    std::map<long long, CosObject> ranges;
    std::vector<CosObject> stack{*labels};
    std::set<long long> visited;
    while (!stack.empty() && ranges.size() < 1'000'000) {
        CosObject node = std::move(stack.back());
        stack.pop_back();
        if (node.isReference()) {
            if (!visited.insert(node.reference().number).second) continue;
            auto fetched = table.fetch(node.reference().number);
            if (!fetched) continue;
            node = fetched.value().object;
        }
        if (!node.isDictionary()) continue;
        const std::vector<CosObject>& nums = node.get("Nums").items();
        for (std::size_t at = 0; at + 1 < nums.size(); at += 2) {
            if (!nums[at].isInteger()) continue;
            CosObject value = nums[at + 1];
            if (value.isReference()) {
                auto fetched = table.fetch(value.reference().number);
                if (!fetched) continue;
                value = fetched.value().object;
            }
            value.clearSpansRecursively();
            if (value.isDictionary()) ranges.insert_or_assign(nums[at].integerValue(), std::move(value));
        }
        for (const CosObject& kid : node.get("Kids").items()) stack.push_back(kid);
    }

    out.resize(static_cast<std::size_t>(pageCount));
    for (int page = 0; page < pageCount; ++page) {
        PageLabelRange& label = out[static_cast<std::size_t>(page)];
        auto range = ranges.upper_bound(page);
        if (range == ranges.begin()) {
            label.style = "D";                 // no range covers it: PDFium shows its own number
            label.value = page + 1;
            continue;
        }
        --range;
        const CosObject& dictionary = range->second;
        const CosObject& style = dictionary.get("S");
        label.style = style.isName() ? style.text() : std::string();
        if (const CosObject* prefix = dictionary.find("P")) label.prefix = *prefix;
        const CosObject& start = dictionary.get("St");
        label.value = (start.isNumber() ? start.integerValue(1) : 1) + (page - range->first);
    }
    return out;
}

QString PageOps::formatLabel(const PageLabelRange& label)
{
    QString out;
    if (label.prefix && label.prefix->isString()) out += EmbeddedFileReader::decodeTextString(label.prefix->text());
    if (label.style == "D") out += QString::number(label.value);
    else if (label.style == "r") out += romanOf(label.value);
    else if (label.style == "R") out += romanOf(label.value).toUpper();
    else if (label.style == "a") out += lettersOf(label.value);
    else if (label.style == "A") out += lettersOf(label.value).toUpper();
    return out;
}

CosObject PageOps::labelTree(const std::vector<PageLabelRange>& labels)
{
    std::vector<CosObject> nums;
    for (std::size_t page = 0; page < labels.size(); ++page) {
        const PageLabelRange& label = labels[page];
        const bool counts = page > 0 && labels[page - 1].style == label.style && labels[page - 1].prefix == label.prefix
                            && !label.style.empty() && labels[page - 1].value + 1 == label.value;
        const bool repeats = page > 0 && label.style.empty() && labels[page - 1].style.empty()
                             && labels[page - 1].prefix == label.prefix;
        if (counts || repeats) continue;
        std::vector<CosObject::Entry> range;
        if (!label.style.empty()) range.push_back({"S", CosObject::makeName(label.style)});
        if (label.prefix) range.push_back({"P", *label.prefix});
        if (!label.style.empty() && label.value != 1) range.push_back({"St", CosObject::makeInteger(label.value)});
        nums.push_back(CosObject::makeInteger(static_cast<long long>(page)));
        nums.push_back(CosObject::makeDictionary(std::move(range)));
    }
    return CosObject::makeDictionary({{"Nums", CosObject::makeArray(std::move(nums))}});
}

} // namespace rawpage::pdf::ops
