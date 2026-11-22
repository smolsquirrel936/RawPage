// core/doc/BatesAllocator -- see the header.

#include "core/doc/BatesAllocator.h"

#include <QLatin1String>

#include <algorithm>

namespace rawpage::core {

long long largestBates(int digits) noexcept
{
    long long out = 0;
    for (int i = 0; i < std::clamp(digits, kMinBatesDigits, kMaxBatesDigits); ++i) out = out * 10 + 9;
    return out;
}

Result<BatesShare> BatesAllocator::take(const QString& document, int pages)
{
    if (pages <= 0) return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.noPages")};
    const long long last = next_ + pages - 1;
    if (next_ < 0 || last > largestBates(format_.digits) || last > kMaxBatesNumber)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.batesOverflow"), document};
    BatesShare share{document, pages, next_, last};
    next_ = last + 1;
    shares_.push_back(share);
    return share;
}

Status BatesAllocator::giveBack(const BatesShare& share)
{
    if (shares_.empty() || !(shares_.back() == share))
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.badSpec"), QStringLiteral("share")};
    next_ = share.first;
    shares_.pop_back();
    return Status{};
}

} // namespace rawpage::core
