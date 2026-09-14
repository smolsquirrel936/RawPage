// core/cmd/commands/PageCommands -- commands over pages. Stage 3 T5: `RotatePageCommand` only; insert,
// delete, move, duplicate, extract, replace, crop and the rest arrive with Stage 7 (D3-8).
//
// RotatePageCommand is the first-milestone mutation and the shape every later command copies, so the
// choices below are made for the hundredth command as much as for this one.
//
// A RELATIVE QUARTER TURN, NOT A TARGET ANGLE. Flow D writes `RotatePageCommand{page, +90}`: the
// command carries a page and a turn of 90, 180 or 270 degrees clockwise. Its inverse is then exact
// in any state -- revert turns the other way -- which a target angle is not: "set page 3 to 90"
// cannot be undone without also remembering what page 3 was, and a remembered "from" goes wrong the
// moment a recovery replay meets a document the journal did not expect. The dialog's "90° counter-
// clockwise" is a turn of 270.
//
// SINK FIRST, MODEL SECOND. `apply()` checks the page exists, asks the sink to take the new rotation,
// and only when the sink accepts does it change the model -- which, for an index already checked,
// cannot fail. So a `false` return has changed neither, which is the contract `Command.h` states and
// `CommandJournal` checks.

#ifndef RAWPAGE_CORE_CMD_COMMANDS_PAGECOMMANDS_H
#define RAWPAGE_CORE_CMD_COMMANDS_PAGECOMMANDS_H

#include "core/Result.h"
#include "core/cmd/Command.h"

#include <memory>

namespace rawpage::core {

class RotatePageCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"page.rotate"};

    // The largest page index a serialized command may name. ISO 32000-1 Annex C.1 puts the
    // implementation limit on indirect objects at 8,388,607 (2^23 - 1), and every page is one, so an
    // index at or beyond it names no page in any conforming file -- and a recovery journal claiming one
    // is damaged or hostile (guide case A-45).
    static constexpr int kMaxPageIndex = 8'388'606;

    // `quarterTurns` is 90, 180 or 270 -- clockwise. -90 is accepted and normalized to 270; anything
    // that is not a whole quarter turn, or is a full turn, is not a rotation, and `isValid()` says so.
    RotatePageCommand(int pageIndex, int degreesClockwise);

    bool isValid() const noexcept;
    int pageIndex() const noexcept { return pageIndex_; }
    int degrees() const noexcept { return degrees_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.page.rotate: %1 page number, %2 degrees
    Scope scope() const override { return Scope::page(pageIndex_); }
    Operation operation() const override { return Operation::EditPages; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "page.rotate", "page": n, "degrees": d}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    bool turn(Document& document, MutationSink& sink, int degrees);

    int pageIndex_ = -1;
    int degrees_ = 0;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMANDS_PAGECOMMANDS_H
