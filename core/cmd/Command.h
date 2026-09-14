// core/cmd/Command.h -- the only vocabulary for changing a document. Stage 3 T5.
//
// Every document change in the product -- from this stage's page rotation to Write mode's typing at
// Stage 13 -- is a `Command`, run through `core/cmd/CommandJournal`. Flow D
// (`Architecture-FileMap.md` §5) is the design; `Tech-Stack-Plan.md` §5.3 is the contract; this file
// is where both stop being diagrams, and every later command copies what this one does.
//
// -------------------------------------------------------------------------------------------
// THE CONTRACT
// -------------------------------------------------------------------------------------------
//   * **`apply()` changes the MODEL and tells the ENGINE the intent -- and never touches a byte.**
//     The `MutationSink` it is handed is the only channel to `pdf/`; `pdf/ops/` decides which COS
//     objects the intent means (D3-5).
//   * **`revert()` is the EXACT inverse.** Apply then revert must restore `Document::modelHash()` and
//     leave the sink with nothing to write; `tst_reversibility` measures that over the corpus for
//     every registered command (guide case A-43), and a planted command whose revert lies must fail it
//     (A-44).
//   * **Returning false means NOTHING CHANGED.** Partial application is a bug, not a failure mode:
//     `CommandJournal` compares the model before and after a refused `apply()` and reports a mismatch
//     through its partial-application handler (A-40). A command validates everything it can before
//     touching either the model or the sink.
//   * **`name()` is a CATALOG KEY AND ARGUMENTS, never a sentence** (D3-10(a), guide issue I-01).
//     `core/` has no `tr()` and holds no user-facing English (convention 5); `ui/` resolves it for the
//     undo menu.
//   * **`serialize()` names the command by its `typeId()` -- a row in `core/cmd/Commands.def` -- and
//     carries everything `apply()` and `revert()` need.** The recovery journal writes it to disk and
//     reads it back (T9), so `CommandRegistry::deserialize` is a parser of hostile input (A-45).
//   * **`operation()` is what `Permissions::may()` is asked** before `apply()` runs. It is the one
//     method beyond `Tech-Stack-Plan.md` §5.3's list, added at T5: the pre-flight has to know WHICH
//     kind of edit is being attempted, and Stage 10's permission bits answer per operation.
//
// A NEW COMMAND SHIPS WITH ITS INVERSE AND ITS TESTS, OR IT DOES NOT SHIP. `tools/mutation-lint`
// (T6) fails the build for a class under `core/cmd/commands/` with no `Commands.def` row, no test
// tagged `[serialize:Class]` or no test tagged `[revert:Class]` (D3-12).

#ifndef RAWPAGE_CORE_CMD_COMMAND_H
#define RAWPAGE_CORE_CMD_COMMAND_H

#include "core/doc/Permissions.h"
#include "core/doc/Scope.h"

#include <QJsonObject>
#include <QLatin1String>
#include <QStringList>

namespace rawpage::core {

class Document;
class MutationSink;

// A message a person will read, as `core/` can express one: a catalog key and its `%1`… arguments.
struct CatalogText {
    QLatin1String key{""};
    QStringList arguments;

    friend bool operator==(const CatalogText& a, const CatalogText& b)
    {
        return a.key == b.key && a.arguments == b.arguments;
    }
};

class Command {
public:
    virtual ~Command() = default;

    // The permanent name of this command in serialized form: its `Commands.def` row.
    virtual QLatin1String typeId() const = 0;

    virtual CatalogText name() const = 0;
    virtual Scope scope() const = 0;
    virtual Operation operation() const = 0;

    virtual bool apply(Document& document, MutationSink& sink) = 0;
    virtual bool revert(Document& document, MutationSink& sink) = 0;

    // `{"type": typeId(), ...}`. Deserialized by `CommandRegistry::deserialize`.
    virtual QJsonObject serialize() const = 0;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMAND_H
