#include "core/cmd/ActionRegistry.h"

#include "core/Log.h"

namespace rawpage::core {
namespace {

// The message keys are catalog keys, not sentences -- Result's contract. `ui/` resolves them
// through tr(); the CLI resolves them through the same catalog, which is what makes a batch report
// translatable without a second string table.
constexpr QLatin1String kUnknownActionKey{"error.action.unknown"};
constexpr QLatin1String kDisabledActionKey{"error.action.unavailable"};
constexpr QLatin1String kDuplicateRegistrationKey{"error.action.alreadyRegistered"};
constexpr QLatin1String kNoHandlerKey{"error.action.noHandler"};

// The string id, not the enum value, goes into the error context: an enum value means nothing in a
// log or a batch report, and the string is the name the user's file actually used.
QString contextFor(ActionId id)
{
    const std::string_view stringId = actionStringId(id);
    return QString::fromUtf8(stringId.data(), static_cast<qsizetype>(stringId.size()));
}

} // namespace

Result<void> ActionRegistry::register_(ActionId id, Registration registration)
{
    const auto index = static_cast<std::size_t>(id);
    if (index >= kActionCount)
        return Error{ErrorCode::InvalidArgument, kUnknownActionKey};

    // A registration without a handler would dispatch successfully and do nothing, which is
    // property 1's failure mode arriving through the front door instead.
    if (!registration.handler)
        return Error{ErrorCode::InvalidArgument, kNoHandlerKey, contextFor(id)};

    Slot& slot = slots_[index];

    // Property 3. The first registration survives, deliberately: the second caller is the one that
    // has to change, and leaving the first intact keeps the failure diagnosable rather than turning
    // it into whichever-ran-last.
    if (slot.occupied) {
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "ActionRegistry: duplicate registration for" << contextFor(id)
            << "-- the first registration is kept";
        return Error{ErrorCode::InvalidArgument, kDuplicateRegistrationKey, contextFor(id)};
    }

    slot.registration = std::move(registration);
    slot.occupied = true;
    ++registeredCount_;
    return {};
}

Result<void> ActionRegistry::register_(ActionId id, Handler handler, Enable enable)
{
    Registration registration;
    registration.handler = std::move(handler);
    registration.enable = std::move(enable);
    return register_(id, std::move(registration));
}

Result<void> ActionRegistry::dispatch(ActionId id, KeyContext context) const
{
    const auto index = static_cast<std::size_t>(id);

    // Property 1. An id outside the enum can only arrive from a string that was resolved
    // incorrectly, so it is the same failure as an unregistered one from the caller's point of
    // view: the name does not name anything runnable.
    if (index >= kActionCount)
        return Error{ErrorCode::NotFound, kUnknownActionKey};

    const Slot& slot = slots_[index];
    if (!slot.occupied) {
        // Not a warning. At Stage 1 roughly 260 of 327 declared ids are unregistered on purpose,
        // and a warning per dispatch would train everyone to ignore the log by Stage 3.
        RAWPAGE_LOG_DEBUG(LogSubsystem::Ui)
            << "ActionRegistry: dispatch of unregistered action" << contextFor(id);
        return Error{ErrorCode::NotFound, kUnknownActionKey, contextFor(id)};
    }

    // Property 2. A DISTINCT code from the unregistered case, so a CLI or batch report can say
    // "that is not available right now" rather than "no such action" -- one is document state, the
    // other is a typo, and they call for opposite responses from the user.
    if (slot.registration.enable && !slot.registration.enable())
        return Error{ErrorCode::Unavailable, kDisabledActionKey, contextFor(id)};

    slot.registration.handler(context);
    return {};
}

bool ActionRegistry::isRegistered(ActionId id) const
{
    const auto index = static_cast<std::size_t>(id);
    return index < kActionCount && slots_[index].occupied;
}

bool ActionRegistry::isEnabled(ActionId id) const
{
    const auto index = static_cast<std::size_t>(id);
    if (index >= kActionCount) return false;

    const Slot& slot = slots_[index];
    if (!slot.occupied) return false;
    if (!slot.registration.enable) return true;   // no predicate means always available
    return slot.registration.enable();
}

bool ActionRegistry::isChecked(ActionId id) const
{
    const auto index = static_cast<std::size_t>(id);
    if (index >= kActionCount) return false;

    const Slot& slot = slots_[index];
    if (!slot.occupied || !slot.registration.checked) return false;
    return slot.registration.checked();
}

bool ActionRegistry::isToggle(ActionId id) const
{
    const auto index = static_cast<std::size_t>(id);
    if (index >= kActionCount) return false;

    const Slot& slot = slots_[index];
    return slot.occupied && static_cast<bool>(slot.registration.checked);
}

std::string_view ActionRegistry::unavailableKey(ActionId id) const
{
    const Slot& slot = slots_[static_cast<std::size_t>(id)];
    return slot.occupied ? slot.registration.unavailableKey : std::string_view{};
}

std::string_view ActionRegistry::tooltipKey(ActionId id) const
{
    const auto index = static_cast<std::size_t>(id);
    if (index >= kActionCount) return {};

    const Slot& slot = slots_[index];
    return slot.occupied ? slot.registration.tooltipKey : std::string_view{};
}

std::vector<ActionRegistry::ActionState> ActionRegistry::allActions() const
{
    std::vector<ActionState> states;
    states.reserve(kActionCount);
    for (const ActionDescriptor& descriptor : kActions)
        states.push_back(ActionState{descriptor.id, isRegistered(descriptor.id)});
    return states;
}

} // namespace rawpage::core
