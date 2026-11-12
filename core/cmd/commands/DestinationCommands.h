// core/cmd/commands/DestinationCommands -- the named destinations' three commands. Stage 7 T12, decisions D7-10, D7-12 and
// D7-35 (`Stage7-Assembly-Plan.md` §3): `CreateDestinationCommand`, `RenameDestinationCommand` and
// `DeleteDestinationCommand`.
//
// ===========================================================================================
// A NAME IS WHAT OTHER THINGS POINT AT -- SO THEY CHANGE IN THE SAME COMMAND
// ===========================================================================================
// A link or a bookmark reaches a named destination by its NAME (`OutlineAction::Kind::GoToNamed`). Renaming one
// therefore renames every link and bookmark that names it, in the same command, and deleting one CLEARS every link
// and bookmark that names it -- each keeps its rectangle or its title and goes nowhere -- and says which (`report()`),
// because a name nothing resolves is a broken link that looks fine until someone clicks it. One command, one undo
// step (D7-1); the undo puts back every one of them.
//
// What is affected is worked out from the document WHEN THE COMMAND RUNS, never carried, so a replay of the journal
// reaches the same links -- the journal's order is the order they ran (D7-24) -- and the command carries only names.
//
// ===========================================================================================
// THE WHOLE TABLE GOES TO THE SINK, SINK FIRST
// ===========================================================================================
// `apply()` computes the table after the command, checks it (`Destinations::validate`), hands it WHOLE to
// `MutationSink::setDestinations`, then the changed links to `replaceAnnotations` and the changed outline to
// `setOutline` -- each of which may refuse, by key, and a refusal puts back what went before it -- and only then
// changes the model. `revert()` hands each back in the reverse order. The sink decides which objects that means: a
// table equal to the disk's is not written (D7-10).
//
// `permitted()` adds the table's own question to the document's: a table whose two readings disagree is refused by
// `error.destination.readingsDisagree` before anything changes -- and, where links or bookmarks name the destination,
// theirs: an outline read-only refuses a rename that would leave its bookmarks naming nothing.

#ifndef RAWPAGE_CORE_CMD_COMMANDS_DESTINATIONCOMMANDS_H
#define RAWPAGE_CORE_CMD_COMMANDS_DESTINATIONCOMMANDS_H

#include "core/Result.h"
#include "core/cmd/Command.h"
#include "core/doc/Annotation.h"
#include "core/doc/Destinations.h"
#include "core/doc/Document.h"
#include "core/doc/Outline.h"

#include <memory>
#include <optional>
#include <vector>

namespace rawpage::core {

// WHAT A DESTINATION COMMAND CHANGED, AND WHAT IT KEEPS FOR ITS UNDO. A helper, not a base class -- `tools/mutation-lint`
// finds a command by its deriving from `Command` itself, and a shared base would hide all three from it.
struct DestinationEditState {
    std::optional<Destinations> before;
    std::optional<Outline> outlineBefore;     // set only when the outline changed
    std::vector<Annotation> linksBefore;      // the links changed, as they were
    std::vector<Annotation> linksAfter;       // ... and as they became
    std::optional<Error> refusal;

    // The table, the links and the outline after the command: each handed to the sink, then made the model's -- or
    // refused, changing nothing.
    bool apply(Document& document, MutationSink& sink, Result<Destinations> after, std::vector<Annotation> linksBefore,
               std::vector<Annotation> linksAfter, std::optional<Outline> outlineAfter);
    bool revert(Document& document, MutationSink& sink);
};

// The document's question and the table's.
Status destinationCommandPermitted(const Document& document);

// THE LINKS AND BOOKMARKS NAMING `name`, each as it is and as it would be with that name changed to `to` -- or cleared,
// when `to` is empty. The outline is nullopt when no bookmark names it.
struct DestinationReferences {
    std::vector<Annotation> linksBefore;
    std::vector<Annotation> linksAfter;
    std::optional<Outline> outlineAfter;
    std::size_t bookmarks = 0;   // how many bookmarks it changed
};
DestinationReferences referencesTo(const Document& document, const std::vector<QString>& names, const QString& to);

// A NEW ENTRY -- from the current view, in the name tree (D7-35). Its destination must name a page.
class CreateDestinationCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"destination.create"};
    explicit CreateDestinationCommand(NamedDestination entry);
    const NamedDestination& entry() const noexcept { return entry_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.destination.create: %1 name
    QJsonObject serialize() const override;   // {"type", "entry": {...}}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::nothing(); }
    Operation operation() const override { return Operation::EditOutline; }
    Status permitted(const Document& document) const override { return destinationCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }

private:
    DestinationEditState state_;
    NamedDestination entry_;
};

// AN ENTRY CALLED SOMETHING ELSE -- in its place and its table -- and every link and bookmark naming it renamed with it.
class RenameDestinationCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"destination.rename"};
    RenameDestinationCommand(QString from, QString to);
    const QString& from() const noexcept { return from_; }
    const QString& to() const noexcept { return to_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.destination.rename: %1 from, %2 to
    QJsonObject serialize() const override;   // {"type", "from", "to"}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::nothing(); }
    Operation operation() const override { return Operation::EditOutline; }
    Status permitted(const Document& document) const override { return destinationCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }
    // How many links and bookmarks the last apply renamed with it.
    std::size_t renamedLinks() const noexcept { return state_.linksAfter.size(); }
    std::size_t renamedBookmarks() const noexcept { return bookmarks_; }

private:
    DestinationEditState state_;
    QString from_;
    QString to_;
    std::size_t bookmarks_ = 0;
};

// ENTRIES REMOVED -- and every link and bookmark naming one CLEARED, and said (`report()`).
class DeleteDestinationCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"destination.delete"};
    explicit DeleteDestinationCommand(std::vector<QString> names);
    const std::vector<QString>& names() const noexcept { return names_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.destination.delete: %1 name, or command.destination.deleteMany: %1 count
    QJsonObject serialize() const override;   // {"type", "names": [...]}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::nothing(); }
    Operation operation() const override { return Operation::EditOutline; }
    Status permitted(const Document& document) const override { return destinationCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }
    // WHAT THE DELETE CLEARED (D7-12): every link -- by identity, with its page -- and how many bookmarks.
    const std::vector<Annotation>& clearedLinks() const noexcept { return state_.linksBefore; }
    std::size_t clearedBookmarks() const noexcept { return bookmarks_; }

private:
    DestinationEditState state_;
    std::vector<QString> names_;
    std::size_t bookmarks_ = 0;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMANDS_DESTINATIONCOMMANDS_H
