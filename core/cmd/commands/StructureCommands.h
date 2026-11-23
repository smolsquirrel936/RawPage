// core/cmd/commands/StructureCommands -- the layers' four commands. Stage 7 T17, decision D7-9 (`Stage7-Assembly-Plan.md`
// §3, §4): `CreateLayerCommand`, `DeleteLayerCommand`, `MergeLayersCommand` and `SetLayerPropertiesCommand`. The tag
// tree's commands join them at T18; `FlattenLayers` and the commands that rewrite content are Stage 10's (Q2).
//
// ===========================================================================================
// THE WHOLE TREE GOES TO THE SINK, SINK FIRST
// ===========================================================================================
// `apply()` computes the tree after the command from the model's (`core::LayerTree`'s editing half), checks it, hands it
// WHOLE to `MutationSink::setLayers` -- which may refuse, by key -- and only then changes the model. `revert()` hands the
// tree before. The sink decides which objects that means; a tree equal to the disk's is not written (D7-9's shape is
// D7-10's). A command carries only its intent -- ids, a name, properties -- so a replay of the journal computes the same
// tree from the same document (D7-24).
//
// ===========================================================================================
// WHAT A COMMAND DOES NOT DO
// ===========================================================================================
// No command shows or hides a layer: that is view state (`core::LayerView`), changes no byte and is no edit. A delete
// ORPHANS the group's content and a merge REPOINTS it; neither touches a content stream (Q2: that parser is Stage 10's).
// A watermark's group (D7-45) is refused by every command, by `error.layer.notEditable`.
//
// Every command's scope is the whole document: the default state, a usage, a delete or a merge can each change what
// every page draws where no view is set -- a name alone could not, and costs the same redraw for the rarity of it.

#ifndef RAWPAGE_CORE_CMD_COMMANDS_STRUCTURECOMMANDS_H
#define RAWPAGE_CORE_CMD_COMMANDS_STRUCTURECOMMANDS_H

#include "core/Result.h"
#include "core/cmd/Command.h"
#include "core/doc/Document.h"
#include "core/doc/LayerTree.h"

#include <memory>
#include <optional>
#include <vector>

namespace rawpage::core {

// WHAT A LAYER COMMAND KEEPS FOR ITS UNDO. A helper, not a base class -- `tools/mutation-lint` finds a command by its
// deriving from `Command` itself.
struct LayerEditState {
    std::optional<LayerTree> before;
    std::optional<Error> refusal;

    // `after` handed to the sink, then made the model's -- or refused, changing nothing.
    bool apply(Document& document, MutationSink& sink, Result<LayerTree> after);
    bool revert(Document& document, MutationSink& sink);
};

// The document's question and the tree's: its permissions, and whether its layers were read whole.
Status layerCommandPermitted(const Document& document);

// A NEW, EMPTY GROUP -- under `parent` (invalid: the top level), at the end. Its id is created (`LayerId::ofCreated`).
class CreateLayerCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"layer.create"};
    CreateLayerCommand(Layer layer, LayerId parent);
    const Layer& layer() const noexcept { return layer_; }
    const LayerId& parent() const noexcept { return parent_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.layer.create: %1 name
    QJsonObject serialize() const override;   // {"type", "layer": {...}, "parent"?}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::wholeDocument(); }
    Operation operation() const override { return Operation::EditLayers; }
    Status permitted(const Document& document) const override { return layerCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }

private:
    LayerEditState state_;
    Layer layer_;
    LayerId parent_;
};

// GROUPS DELETED -- each out of `/OCGs` and every list, its content orphaned (see the header).
class DeleteLayerCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"layer.delete"};
    explicit DeleteLayerCommand(std::vector<LayerId> ids);
    const std::vector<LayerId>& ids() const noexcept { return ids_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.layer.delete: %1 name, or command.layer.deleteMany: %1 count
    QJsonObject serialize() const override;   // {"type", "ids": [...]}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::wholeDocument(); }
    Operation operation() const override { return Operation::EditLayers; }
    Status permitted(const Document& document) const override { return layerCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }

private:
    LayerEditState state_;
    std::vector<LayerId> ids_;
    QString firstName_;   // what `name()` says, read at the last apply
};

// GROUPS MERGED INTO ONE -- each `from` gone into `into`, whose name and properties are kept (see the header).
class MergeLayersCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"layer.merge"};
    MergeLayersCommand(std::vector<LayerId> from, LayerId into);
    const std::vector<LayerId>& from() const noexcept { return from_; }
    const LayerId& into() const noexcept { return into_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.layer.merge: %1 count, %2 into
    QJsonObject serialize() const override;   // {"type", "from": [...], "into"}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::wholeDocument(); }
    Operation operation() const override { return Operation::EditLayers; }
    Status permitted(const Document& document) const override { return layerCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }

private:
    LayerEditState state_;
    std::vector<LayerId> from_;
    LayerId into_;
    QString intoName_;
};

// ONE GROUP'S PROPERTIES -- name, default state, lock, usage, where it nests and its radio group.
class SetLayerPropertiesCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"layer.setProperties"};
    SetLayerPropertiesCommand(LayerId id, LayerProperties properties);
    const LayerId& id() const noexcept { return id_; }
    const LayerProperties& properties() const noexcept { return properties_; }
    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;   // command.layer.properties: %1 name
    QJsonObject serialize() const override;   // {"type", "id", "properties": {...}}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

    Scope scope() const override { return Scope::wholeDocument(); }
    Operation operation() const override { return Operation::EditLayers; }
    Status permitted(const Document& document) const override { return layerCommandPermitted(document); }
    std::optional<Error> refusal() const override { return state_.refusal; }
    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override { return state_.revert(document, sink); }

private:
    LayerEditState state_;
    LayerId id_;
    LayerProperties properties_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMANDS_STRUCTURECOMMANDS_H
