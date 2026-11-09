// core/cmd/commands/BookmarkCommands -- the outline's six commands. Stage 7 T11, decisions D7-10 and D7-34
// (`Stage7-Assembly-Plan.md` §3): `CreateBookmarkCommand`, `EditBookmarkCommand`, `DeleteBookmarkCommand`,
// `ReparentBookmarkCommand`, `SetBookmarkTargetCommand` and `GenerateBookmarksCommand`.
//
// ===========================================================================================
// AN ITEM IS NAMED BY ITS ID, A PLACE BY (PARENT, INDEX)
// ===========================================================================================
// Each command names items by `OutlineItemId` -- an item the file holds by its object, one RawPage made by the key
// it was given when it was made (`newBookmarkId`), for its whole life -- and places by a parent and an index into
// that parent's children as they stand WHEN THE COMMAND RUNS, so the journal can replay it (D7-24). A command that
// makes items CARRIES them, ids included, so a replay, an undo and a redo make the same items.
//
// ===========================================================================================
// THE WHOLE OUTLINE GOES TO THE SINK
// ===========================================================================================
// `apply()` computes the outline after the command from the model's, checks it against `Outline::validate`'s
// bounds, hands it WHOLE to `MutationSink::setOutline` -- which may refuse, by key -- and only then makes it the
// model's. `revert()` hands the outline before. The sink decides which dictionaries that means: an item whose
// title, colour, style, open state, target and place are as the file has them is not written (D7-10). SINK FIRST,
// MODEL SECOND, as every command does. One command is one undo step however many items it touches -- a delete of a
// subtree, a generated outline of three hundred items (D7-1).
//
// They draw nothing on any page: their scope is `Scope::nothing()`, and the bookmarks pane is handed the model
// again on the journal's `mutated`. `permitted()` adds the outline's own question to the document's: an outline
// whose two readings disagree is refused by `error.outline.readingsDisagree` before anything changes.

#ifndef RAWPAGE_CORE_CMD_COMMANDS_BOOKMARKCOMMANDS_H
#define RAWPAGE_CORE_CMD_COMMANDS_BOOKMARKCOMMANDS_H

#include "core/Result.h"
#include "core/cmd/Command.h"
#include "core/doc/Document.h"
#include "core/doc/Outline.h"

#include <memory>
#include <optional>
#include <vector>

namespace rawpage::core {

// A NEW ITEM'S ID: a key of 128 random bits, spelled as a page command's key. `ordinal` names the items one command
// makes -- `<key>-0`, `<key>-1` ... -- so a generated outline of hundreds draws one key.
QString newBookmarkKey();
OutlineItemId newBookmarkId(const QString& key, int ordinal);

// WHAT EVERY BOOKMARK COMMAND KEEPS: the outline before it ran, and the refusal the sink gave. A helper, not a base
// class -- `tools/mutation-lint` finds a command by its deriving from `Command` itself, and a shared base would hide all
// six from it.
struct OutlineEditState {
    std::optional<Outline> before;
    std::optional<Error> refusal;
    // `after` checked, handed to the sink, then made the model's; false, changing nothing, on any refusal.
    bool apply(Document& document, MutationSink& sink, Result<Outline> after);
    bool revert(Document& document, MutationSink& sink);
};
// The document's question and the outline's (see the header).
Status outlineCommandPermitted(const Document& document);

// NEW ITEMS -- one from the current view or a text selection, or several pasted -- placed at `at`.
class CreateBookmarkCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"bookmark.create"};
    CreateBookmarkCommand(OutlinePosition at, std::vector<OutlineItem> items);
    const std::vector<OutlineItem>& items() const noexcept { return items_; }
    const OutlinePosition& position() const noexcept { return at_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.bookmark.create: %1 title, or command.bookmark.createMany: %1 count
    QJsonObject serialize() const override;   // {"type", "parent", "index", "items": [...]}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::nothing(); }
    Operation operation() const override { return Operation::EditOutline; }
    Status permitted(const Document& document) const override { return outlineCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override { return state_.apply(document, sink, after(document.outline())); }
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }
    // The outline after the command, from `before` -- or a refusal, changing nothing.
    Result<Outline> after(const Outline& before) const;

private:
    OutlineEditState state_;
    OutlinePosition at_;
    std::vector<OutlineItem> items_;
};

// AN ITEM'S PROPERTIES: its title, colour, style and open state. Its target and its place are other commands'.
class EditBookmarkCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"bookmark.edit"};
    struct Properties {
        QString title;
        std::optional<std::array<double, 3>> colour;
        bool italic = false;
        bool bold = false;
        bool open = false;
        bool operator==(const Properties&) const = default;
        static Properties of(const OutlineItem& item);
    };
    EditBookmarkCommand(OutlineItemId id, Properties properties);
    const OutlineItemId& id() const noexcept { return id_; }
    const Properties& properties() const noexcept { return properties_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.bookmark.edit: %1 title
    QJsonObject serialize() const override;   // {"type", "id", "title", "colour"?, "italic", "bold", "open"}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::nothing(); }
    Operation operation() const override { return Operation::EditOutline; }
    Status permitted(const Document& document) const override { return outlineCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override { return state_.apply(document, sink, after(document.outline())); }
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }
    // The outline after the command, from `before` -- or a refusal, changing nothing.
    Result<Outline> after(const Outline& before) const;

private:
    OutlineEditState state_;
    OutlineItemId id_;
    Properties properties_;
};

// ITEMS REMOVED -- each with its subtree, or with its children PROMOTED into its place (`keepChildren`), in order.
class DeleteBookmarkCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"bookmark.delete"};
    DeleteBookmarkCommand(std::vector<OutlineItemId> ids, bool keepChildren);
    const std::vector<OutlineItemId>& ids() const noexcept { return ids_; }
    bool keepsChildren() const noexcept { return keepChildren_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.bookmark.delete, or command.bookmark.deleteMany: %1 count
    QJsonObject serialize() const override;   // {"type", "ids": [...], "keepChildren"}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::nothing(); }
    Operation operation() const override { return Operation::EditOutline; }
    Status permitted(const Document& document) const override { return outlineCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override { return state_.apply(document, sink, after(document.outline())); }
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }
    // The outline after the command, from `before` -- or a refusal, changing nothing.
    Result<Outline> after(const Outline& before) const;

private:
    OutlineEditState state_;
    std::vector<OutlineItemId> ids_;
    bool keepChildren_ = false;
};

// AN ITEM MOVED, with its subtree, to `to` -- counted in the destination's children with the item already taken out.
// What demote, promote, move up, move down and a drag all are. A move onto the item itself or anything under it is
// refused by `error.outline.cycle`: a tree has no such place.
class ReparentBookmarkCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"bookmark.reparent"};
    ReparentBookmarkCommand(OutlineItemId id, OutlinePosition to);
    const OutlineItemId& id() const noexcept { return id_; }
    const OutlinePosition& destination() const noexcept { return to_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.bookmark.move: %1 title
    QJsonObject serialize() const override;   // {"type", "id", "parent", "index"}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::nothing(); }
    Operation operation() const override { return Operation::EditOutline; }
    Status permitted(const Document& document) const override { return outlineCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override { return state_.apply(document, sink, after(document.outline())); }
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }
    // The outline after the command, from `before` -- or a refusal, changing nothing.
    Result<Outline> after(const Outline& before) const;

private:
    OutlineEditState state_;
    OutlineItemId id_;
    OutlinePosition to_;
};

// WHAT AN ITEM DOES: a destination in this document, or an action -- written, never run (D7-12).
class SetBookmarkTargetCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"bookmark.setTarget"};
    SetBookmarkTargetCommand(OutlineItemId id, OutlineAction action);
    const OutlineItemId& id() const noexcept { return id_; }
    const OutlineAction& action() const noexcept { return action_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.bookmark.setTarget: %1 title
    QJsonObject serialize() const override;   // {"type", "id", "action"}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::nothing(); }
    Operation operation() const override { return Operation::EditOutline; }
    Status permitted(const Document& document) const override { return outlineCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override { return state_.apply(document, sink, after(document.outline())); }
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }
    // The outline after the command, from `before` -- or a refusal, changing nothing.
    Result<Outline> after(const Outline& before) const;

private:
    OutlineEditState state_;
    OutlineItemId id_;
    OutlineAction action_;
};

// A GENERATED OR IMPORTED TREE placed at `at` -- what `core/doc/OutlineGenerator` and an import made, carried whole,
// so a replay places the same items without reading the pages again.
class GenerateBookmarksCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"bookmark.generate"};
    enum class Source : std::uint8_t { Headings, Fonts, Pattern, Import };
    GenerateBookmarksCommand(OutlinePosition at, std::vector<OutlineItem> items, Source source);
    const std::vector<OutlineItem>& items() const noexcept { return items_; }
    Source source() const noexcept { return source_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.bookmark.generate or command.bookmark.import: %1 count
    QJsonObject serialize() const override;   // {"type", "parent", "index", "source", "items": [...]}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::nothing(); }
    Operation operation() const override { return Operation::EditOutline; }
    Status permitted(const Document& document) const override { return outlineCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override { return state_.apply(document, sink, after(document.outline())); }
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }
    // The outline after the command, from `before` -- or a refusal, changing nothing.
    Result<Outline> after(const Outline& before) const;

private:
    OutlineEditState state_;
    OutlinePosition at_;
    std::vector<OutlineItem> items_;
    Source source_ = Source::Headings;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMANDS_BOOKMARKCOMMANDS_H
