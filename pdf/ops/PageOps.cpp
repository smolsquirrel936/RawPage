// pdf/ops/PageOps -- implementation. Stage 3 T3. See the header for the walk's four rules.

#include "pdf/ops/PageOps.h"

#include "pdf/cos/XrefTable.h"

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

} // namespace rawpage::pdf::ops
