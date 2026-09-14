// pdf/cos/ObjectAllocator -- implementation. Stage 3 T2. See the header for why numbers are never reused.

#include "pdf/cos/ObjectAllocator.h"

#include "pdf/cos/XrefTable.h"

#include <algorithm>

namespace rawpage::pdf::cos {

ObjectAllocator ObjectAllocator::forTable(const XrefTable& table) noexcept
{
    const long long declared = table.declaredSize();
    const long long present = table.largestObjectNumber() + 1;
    return ObjectAllocator(std::max({declared, present, 1LL}));
}

ObjectAllocator::ObjectAllocator(long long firstNumber) noexcept
    : first_(std::max(firstNumber, 1LL)), next_(std::max(firstNumber, 1LL))
{
}

long long ObjectAllocator::allocate() noexcept
{
    return next_++;
}

} // namespace rawpage::pdf::cos
