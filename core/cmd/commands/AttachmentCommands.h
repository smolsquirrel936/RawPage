// core/cmd/commands/AttachmentCommands -- files attached to, and removed from, the document as a whole. Stage 6
// T11, decision D6-17: the last two of the eleven commands `Stage6-Annotations-Plan.md` §4 names,
// `AddAttachmentCommand` and `DeleteAttachmentCommand`. **A file attached at a place on a page is not one of
// these**: it is a `FileAttachment` ANNOTATION, made by `AddAnnotationCommand` (`annot.attachFile`) and deleted,
// moved and answered as every annotation is.
//
// ===========================================================================================
// A FILE IS NAMED BY ITS KEY, AND CARRIED WHOLE
// ===========================================================================================
// Both commands name a file by its key in `/Names /EmbeddedFiles` (`core/doc/Attachments` says why the key is
// its identity). `AddAttachmentCommand` CARRIES each file's bytes, so a replay, an undo and a redo attach the
// same bytes; its journal line holds them, and a line holding more than 64 KiB of them puts them in a side file
// named by its hash (D6-23, `core/cmd/RecoveryJournal`). `DeleteAttachmentCommand` carries only the keys, and
// keeps what it removed for its revert -- the sink lists the very file specification again, from disk.
//
// SINK FIRST, MODEL SECOND, as every command does: everything checkable is checked against the model, the sink
// is told (it may refuse), and the model then changes in a way that cannot fail. A set is one undo step (D6-1).
// They render nothing: an attachment is no ink on any page, so their scope is `Scope::nothing()`.

#ifndef RAWPAGE_CORE_CMD_COMMANDS_ATTACHMENTCOMMANDS_H
#define RAWPAGE_CORE_CMD_COMMANDS_ATTACHMENTCOMMANDS_H

#include "core/Result.h"
#include "core/cmd/Command.h"
#include "core/doc/Attachments.h"

#include <memory>
#include <vector>

namespace rawpage::core {

// ATTACHES files, each under the key it carries -- a key no file of the document already has
// (`Attachments::uniqueKey` makes one), each carrying its bytes (`newEmbeddedFile`).
class AddAttachmentCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"attachment.add"};

    explicit AddAttachmentCommand(std::vector<DocumentAttachment> files);

    const std::vector<DocumentAttachment>& files() const noexcept { return files_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.attachment.add, or command.attachment.addMany: %1 count
    Scope scope() const override { return Scope::nothing(); }
    Operation operation() const override { return Operation::Attach; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "attachment.add", "files": [ {"key", "file"}, … ]}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<DocumentAttachment> files_;
};

// REMOVES the files with these keys from the tree. Each file specification stays in the file, listed nowhere.
class DeleteAttachmentCommand final : public Command {
public:
    static constexpr QLatin1String kTypeId{"attachment.delete"};

    explicit DeleteAttachmentCommand(std::vector<QString> keys);

    const std::vector<QString>& keys() const noexcept { return keys_; }

    QLatin1String typeId() const override { return kTypeId; }
    CatalogText name() const override;      // command.attachment.delete, or command.attachment.deleteMany: %1 count
    Scope scope() const override { return Scope::nothing(); }
    Operation operation() const override { return Operation::Attach; }

    bool apply(Document& document, MutationSink& sink) override;
    bool revert(Document& document, MutationSink& sink) override;

    QJsonObject serialize() const override;   // {"type": "attachment.delete", "keys": ["readme.txt", …]}
    static Result<std::unique_ptr<Command>> deserialize(const QJsonObject& json);

private:
    std::vector<QString> keys_;
    std::vector<DocumentAttachment> removed_;   // kept by apply() for revert(), in the order of `keys_`
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_COMMANDS_ATTACHMENTCOMMANDS_H
