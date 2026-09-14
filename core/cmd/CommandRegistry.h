// core/cmd/CommandRegistry -- serialized command in, `Command` out. Stage 3 T5, decision D3-12.
//
// GENERATED FROM `core/cmd/Commands.def`, not populated at runtime. `Implementation-Plan.md` sketched
// `void add<T>()`, a registry filled by calls; a table is what `tools/mutation-lint` can read without
// compiling, and it makes "every command is registered" a property of one file rather than of
// whichever startup code remembered to call `add`. So there is nothing to add and nothing to forget.
//
// WHO CALLS IT: the recovery journal replaying a crashed session (T9) and, from Stage 14, the batch
// runner. Both read JSON that came from a file, so `deserialize` is a parser of hostile input: an
// unknown type is `NotFound`, a malformed known type is `ParseFailure`, and neither crashes (guide
// cases A-42 and A-45).

#ifndef RAWPAGE_CORE_CMD_COMMANDREGISTRY_H
#define RAWPAGE_CORE_CMD_COMMANDREGISTRY_H

#include "core/Result.h"
#include "core/cmd/Command.h"

#include <QJsonObject>
#include <QLatin1String>

#include <memory>
#include <vector>

namespace rawpage::core {

class CommandRegistry {
public:
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    // Every row of `Commands.def`, in table order.
    static std::vector<QLatin1String> typeIds();
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMANDREGISTRY_H
