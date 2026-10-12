// core/cmd/commands/AttachmentCommands -- implementation. Stage 6 T11. See the header.

#include "core/cmd/commands/AttachmentCommands.h"

#include "core/backend.h"
#include "core/doc/Document.h"

#include <QJsonArray>
#include <QJsonValue>

#include <set>

namespace rawpage::core {
namespace {

Error badCommand(QLatin1String typeId, const QString& field)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"),
                 QString(typeId) + QLatin1Char('.') + field};
}

CatalogText countedName(const char* one, const char* many, std::size_t count)
{
    if (count == 1) return CatalogText{QLatin1String(one), {}};
    return CatalogText{QLatin1String(many), {QString::number(count)}};
}

bool keyIsBounded(const QString& key)
{
    return !key.isEmpty() && key.size() <= kMaxAttachmentNameLength;
}

} // namespace

// ==========================================================================================
// AddAttachmentCommand
// ==========================================================================================

AddAttachmentCommand::AddAttachmentCommand(std::vector<DocumentAttachment> files) : files_(std::move(files)) {}

CatalogText AddAttachmentCommand::name() const
{
    return countedName("command.attachment.add", "command.attachment.addMany", files_.size());
}

bool AddAttachmentCommand::apply(Document& document, MutationSink& sink)
{
    Attachments& held = document.attachments();
    if (!held.status() || files_.empty() || files_.size() > kMaxAttachmentsPerCommand) return false;
    // Everything checked against the model before the sink hears of it: keys new to the document and to each
    // other, every file carrying its bytes.
    std::set<QString> keys;
    for (const DocumentAttachment& file : files_) {
        if (!keyIsBounded(file.key) || !file.file.carried || held.find(file.key) || !keys.insert(file.key).second)
            return false;
    }
    if (!sink.insertAttachments(files_)) return false;
    for (const DocumentAttachment& file : files_) (void)held.insert(file);   // validated above: cannot fail
    return true;
}

bool AddAttachmentCommand::revert(Document& document, MutationSink& sink)
{
    Attachments& held = document.attachments();
    std::vector<QString> keys;
    for (const DocumentAttachment& file : files_) {
        if (!held.find(file.key)) return false;
        keys.push_back(file.key);
    }
    if (keys.empty() || !sink.removeAttachments(keys)) return false;
    for (const QString& key : keys) (void)held.remove(key);
    return true;
}

QJsonObject AddAttachmentCommand::serialize() const
{
    QJsonArray files;
    for (const DocumentAttachment& file : files_) files.append(file.toJson());
    return QJsonObject{{QStringLiteral("type"), kTypeId}, {QStringLiteral("files"), files}};
}

Result<std::unique_ptr<Command>> AddAttachmentCommand::deserialize(const QJsonObject& json)
{
    const QJsonValue files = json.value(QStringLiteral("files"));
    if (!files.isArray() || files.toArray().isEmpty()
        || static_cast<std::size_t>(files.toArray().size()) > kMaxAttachmentsPerCommand)
        return badCommand(kTypeId, QStringLiteral("files"));
    std::vector<DocumentAttachment> parsed;
    std::set<QString> keys;
    for (const QJsonValue& item : files.toArray()) {
        if (!item.isObject()) return badCommand(kTypeId, QStringLiteral("files"));
        Result<DocumentAttachment> file = DocumentAttachment::fromJson(item.toObject());
        if (!file) return badCommand(kTypeId, QStringLiteral("file"));
        // What apply() would refuse anyway, refused as the malformed line it is: an attachment with no bytes, a
        // key or a name past the bounds, a file larger than one command may carry, one key twice.
        const DocumentAttachment& f = file.value();
        if (!keyIsBounded(f.key) || f.file.name.size() > kMaxAttachmentNameLength || !f.file.carried
            || f.file.data.size() > kMaxAttachmentBytes || !keys.insert(f.key).second)
            return badCommand(kTypeId, QStringLiteral("file"));
        parsed.push_back(std::move(file).value());
    }
    return std::unique_ptr<Command>(new AddAttachmentCommand(std::move(parsed)));
}

// ==========================================================================================
// DeleteAttachmentCommand
// ==========================================================================================

DeleteAttachmentCommand::DeleteAttachmentCommand(std::vector<QString> keys) : keys_(std::move(keys)) {}

CatalogText DeleteAttachmentCommand::name() const
{
    return countedName("command.attachment.delete", "command.attachment.deleteMany", keys_.size());
}

bool DeleteAttachmentCommand::apply(Document& document, MutationSink& sink)
{
    Attachments& held = document.attachments();
    if (!held.status() || keys_.empty() || keys_.size() > kMaxAttachmentsPerCommand) return false;
    std::set<QString> seen;
    std::vector<DocumentAttachment> removed;
    for (const QString& key : keys_) {
        const std::optional<std::size_t> at = held.find(key);
        if (!at || !seen.insert(key).second) return false;
        removed.push_back(held.files()[*at]);
    }
    if (!sink.removeAttachments(keys_)) return false;
    for (const QString& key : keys_) (void)held.remove(key);
    removed_ = std::move(removed);
    return true;
}

bool DeleteAttachmentCommand::revert(Document& document, MutationSink& sink)
{
    Attachments& held = document.attachments();
    if (removed_.empty()) return false;
    for (const DocumentAttachment& file : removed_) {
        if (held.find(file.key)) return false;
    }
    if (!sink.insertAttachments(removed_)) return false;
    for (const DocumentAttachment& file : removed_) (void)held.insert(file);
    removed_.clear();
    return true;
}

QJsonObject DeleteAttachmentCommand::serialize() const
{
    QJsonArray keys;
    for (const QString& key : keys_) keys.append(key);
    return QJsonObject{{QStringLiteral("type"), kTypeId}, {QStringLiteral("keys"), keys}};
}

Result<std::unique_ptr<Command>> DeleteAttachmentCommand::deserialize(const QJsonObject& json)
{
    const QJsonValue keys = json.value(QStringLiteral("keys"));
    if (!keys.isArray() || keys.toArray().isEmpty() || static_cast<std::size_t>(keys.toArray().size()) > kMaxAttachmentsPerCommand)
        return badCommand(kTypeId, QStringLiteral("keys"));
    std::vector<QString> parsed;
    std::set<QString> seen;
    for (const QJsonValue& item : keys.toArray()) {
        if (!item.isString() || !keyIsBounded(item.toString()) || !seen.insert(item.toString()).second)
            return badCommand(kTypeId, QStringLiteral("keys"));
        parsed.push_back(item.toString());
    }
    return std::unique_ptr<Command>(new DeleteAttachmentCommand(std::move(parsed)));
}

} // namespace rawpage::core
