// Fixture: never compiled. Three spellings of "derives from Command", and two classes that do not.
#pragma once

namespace rawpage::core {

class FixtureStampCommand final : public Command {
public:
    bool apply();
};

class FixtureTiltCommand
    : public core::Command
{
};

struct FixtureShearCommand : private Helper, public ::rawpage::core::Command {
};

// NOT commands: a base whose name only STARTS with Command, and an enum class with an underlying type.
class FixtureHistory : public CommandHistory {
};
enum class FixtureKind : int { A };

} // namespace rawpage::core
