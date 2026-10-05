// core/cmd/Command -- the one method of the interface with a default. Stage 6 T3; see the header.

#include "core/cmd/Command.h"

#include "core/doc/Document.h"

namespace rawpage::core {

Status Command::permitted(const Document& document) const
{
    return document.permissions().may(operation());
}

} // namespace rawpage::core
