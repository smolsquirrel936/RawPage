// pdf/ops/MutationSinkImpl -- implementation. Stage 3 T3. See the header for desired state.

#include "pdf/ops/MutationSinkImpl.h"

#include "pdf/cos/XrefRepair.h"

namespace rawpage::pdf::ops {
namespace {

core::Error sinkError(core::ErrorCode code, const char* key, long long detail = -1)
{
    return core::Error{code, QLatin1String(key),
                       detail < 0 ? QString() : QString::number(static_cast<qlonglong>(detail))};
}

// -90 -> 270, 450 -> 90; nullopt for anything that is not a whole number of quarter turns.
std::optional<int> normalizeDegrees(int degrees)
{
    if (degrees % 90 != 0)
        return std::nullopt;
    return ((degrees % 360) + 360) % 360;
}

// The table a sink compares against. Through `openWithRepair()`, the reader's one entry point, so a
// damaged file's sink sees the same objects the rest of the product does; whether a REPAIRED file's
// update is written as a complete section is the save path's decision (D3-6, T8), not this class's.
core::Result<cos::XrefTable> tableFor(std::string_view bytes)
{
    auto opened = cos::openWithRepair(bytes);
    if (!opened)
        return opened.error();
    return std::move(opened.value().table);
}

} // namespace

MutationSinkImpl::MutationSinkImpl(std::string_view bytes, cos::XrefTable table, PageWalk walk)
    : bytes_(bytes), table_(std::move(table)), walk_(std::move(walk))
{
}

core::Result<std::unique_ptr<MutationSinkImpl>> MutationSinkImpl::open(std::string_view bytes)
{
    auto table = tableFor(bytes);
    if (!table)
        return table.error();
    auto walk = PageOps::walk(table.value());
    if (!walk)
        return walk.error();
    return std::unique_ptr<MutationSinkImpl>(
        new MutationSinkImpl(bytes, std::move(table).value(), std::move(walk).value()));
}

core::Status MutationSinkImpl::setPageRotation(int pageIndex, int degrees)
{
    // Validated in full before anything changes: A-33's "refused without trace".
    if (pageIndex < 0 || pageIndex >= pageCount())
        return sinkError(core::ErrorCode::InvalidArgument, "error.pdf.pageOutOfRange", pageIndex);
    const std::optional<int> normalized = normalizeDegrees(degrees);
    if (!normalized)
        return sinkError(core::ErrorCode::InvalidArgument, "error.pdf.rotationNotRightAngle",
                         degrees);

    const PageEntry& page = walk_.pages[static_cast<std::size_t>(pageIndex)];
    if (*normalized == page.effectiveRotate())
        desired_.erase(pageIndex);          // back to the disk: nothing to write for this page
    else
        desired_[pageIndex] = *normalized;
    return core::Status{};
}

int MutationSinkImpl::desiredRotation(int pageIndex) const
{
    if (const auto found = desired_.find(pageIndex); found != desired_.end())
        return found->second;
    if (pageIndex < 0 || pageIndex >= pageCount())
        return 0;
    return walk_.pages[static_cast<std::size_t>(pageIndex)].effectiveRotate();
}

core::Result<cos::ObjectDelta> MutationSinkImpl::delta()
{
    cos::ObjectDelta delta(table_);
    for (const auto& [pageIndex, degrees] : desired_) {
        const PageEntry& page = walk_.pages[static_cast<std::size_t>(pageIndex)];
        auto dictionary = PageOps::rotatedPage(table_, page, degrees);
        if (!dictionary)
            return dictionary.error();
        if (!dictionary.value())
            continue;
        if (dictionary.value()->isStream()) {
            // `rotatedPage` returns a stream only when its data lies in this buffer (issue I-12).
            const cos::CosStreamData& data = dictionary.value()->streamData();
            std::string payload(bytes_.substr(data.dataOffset, data.dataLength));
            delta.replace(page.number, std::move(*dictionary.value()), std::move(payload));
        } else {
            delta.replace(page.number, std::move(*dictionary.value()));
        }
    }
    return delta;
}

core::Status MutationSinkImpl::adopt(std::string_view bytes)
{
    auto table = tableFor(bytes);
    if (!table)
        return table.error();
    auto walk = PageOps::walk(table.value());
    if (!walk)
        return walk.error();
    if (walk.value().pages.size() != walk_.pages.size())
        return sinkError(core::ErrorCode::Corrupted, "error.pdf.pageTreeChanged",
                         static_cast<long long>(walk.value().pages.size()));

    bytes_ = bytes;
    table_ = std::move(table).value();
    walk_ = std::move(walk).value();
    for (auto it = desired_.begin(); it != desired_.end();) {
        if (it->second == walk_.pages[static_cast<std::size_t>(it->first)].effectiveRotate())
            it = desired_.erase(it);
        else
            ++it;
    }
    return core::Status{};
}

} // namespace rawpage::pdf::ops
