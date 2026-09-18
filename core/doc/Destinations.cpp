#include "core/doc/Destinations.h"

#include <algorithm>

namespace rawpage::core {

const char* destinationFitName(Destination::Fit fit) noexcept
{
    switch (fit) {
    case Destination::Fit::Unknown: return "Unknown";
    case Destination::Fit::XYZ:     return "XYZ";
    case Destination::Fit::Fit:     return "Fit";
    case Destination::Fit::FitH:    return "FitH";
    case Destination::Fit::FitV:    return "FitV";
    case Destination::Fit::FitR:    return "FitR";
    case Destination::Fit::FitB:    return "FitB";
    case Destination::Fit::FitBH:   return "FitBH";
    case Destination::Fit::FitBV:   return "FitBV";
    }
    return "Unknown";
}

const NamedDestination* Destinations::find(const QString& name) const
{
    const auto at = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const NamedDestination& entry) { return entry.name == name; });
    return at == entries_.end() ? nullptr : &*at;
}

std::size_t Destinations::unresolvedCount() const
{
    return static_cast<std::size_t>(
        std::count_if(entries_.begin(), entries_.end(), [](const NamedDestination& entry) {
            return !entry.destination.isResolved();
        }));
}

} // namespace rawpage::core
