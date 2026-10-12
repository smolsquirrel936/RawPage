// core/cmd/CommandRegistry -- implementation, generated from core/cmd/Commands.def. Stage 3 T5.

#include "core/cmd/CommandRegistry.h"

// One include per table row's header column. Listed by hand because a macro cannot generate an
// #include; a row whose header is missing here fails to compile below, on the row's own name.
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/AttachmentCommands.h"
#include "core/cmd/commands/PageCommands.h"

#include <QJsonValue>

namespace rawpage::core {

Result<std::unique_ptr<Command>> CommandRegistry::deserialize(const QJsonObject& json)
{
    const QJsonValue type = json.value(QStringLiteral("type"));
    if (!type.isString()) {
        return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"),
                     QStringLiteral("type")};
    }
    const QString typeId = type.toString();

#define RAWPAGE_COMMAND(Class, TypeId, Header)                                                    \
    if (typeId == QLatin1String(TypeId))                                                           \
        return Class::deserialize(json);
#include "core/cmd/Commands.def"

    // Named with a bounded prefix of what arrived: a hostile journal can carry a 10 MB type string
    // (guide case A-45), and a context that echoed it whole would put that into the log.
    return Error{ErrorCode::NotFound, QLatin1String("error.command.unknownType"), typeId.left(64)};
}

std::vector<QLatin1String> CommandRegistry::typeIds()
{
    return {
#define RAWPAGE_COMMAND(Class, TypeId, Header) QLatin1String(TypeId),
#include "core/cmd/Commands.def"
    };
}

} // namespace rawpage::core
