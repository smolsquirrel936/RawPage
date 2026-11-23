#include "core/cmd/commands/StructureCommands.h"

#include "core/backend.h"

#include <QJsonArray>
#include <QJsonValue>

#include <set>

namespace rawpage::core {
namespace {

Error badLayerCommand(QLatin1String type, const char* field)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"), QString(type) + QLatin1Char('.') + QLatin1String(field)};
}

bool typeIs(const QJsonObject& json, QLatin1String type)
{
    const QJsonValue value = json.value(QStringLiteral("type"));
    return value.isString() && value.toString() == type;
}

std::optional<LayerId> idFrom(const QJsonValue& value)
{
    return value.isString() ? LayerId::fromString(value.toString()) : std::nullopt;
}

// A list of ids, each once, 1 ... `kMaxLayers` of them.
std::optional<std::vector<LayerId>> idsFrom(const QJsonValue& value)
{
    if (!value.isArray()) return std::nullopt;
    const QJsonArray array = value.toArray();
    if (array.isEmpty() || static_cast<std::size_t>(array.size()) > kMaxLayers) return std::nullopt;
    std::vector<LayerId> out;
    std::set<LayerId> seen;
    for (const QJsonValue& item : array) {
        const std::optional<LayerId> id = idFrom(item);
        if (!id || !seen.insert(*id).second) return std::nullopt;
        out.push_back(*id);
    }
    return out;
}

QJsonArray idsTo(const std::vector<LayerId>& ids)
{
    QJsonArray out;
    for (const LayerId& id : ids) out.append(id.toString());
    return out;
}

QString nameOf(const Document& document, const LayerId& id)
{
    const Layer* layer = document.layers().find(id);
    return layer != nullptr ? layer->name : id.toString();
}

} // namespace

Status layerCommandPermitted(const Document& document)
{
    if (Status status = document.permissions().may(Operation::EditLayers); !status) return status;
    return document.layers().status();
}

bool LayerEditState::apply(Document& document, MutationSink& sink, Result<LayerTree> after)
{
    refusal.reset();
    if (!after) {
        refusal = after.error();
        return false;
    }
    LayerTree next = std::move(after).value();
    if (Status valid = next.validate(); !valid) {
        refusal = valid.error();
        return false;
    }
    if (Status written = sink.setLayers(next); !written) {
        refusal = written.error();
        return false;
    }
    before = document.layers();
    next.setStatus(document.layers().status());
    document.layers() = std::move(next);
    return true;
}

bool LayerEditState::revert(Document& document, MutationSink& sink)
{
    // The journal undoes in order, so the model is what this command made and `before` is exact.
    if (!before) return false;
    if (!sink.setLayers(*before)) return false;
    document.layers() = *before;
    return true;
}

// ---- CreateLayerCommand ----------------------------------------------------------------------------------------------

CreateLayerCommand::CreateLayerCommand(Layer layer, LayerId parent) : layer_(std::move(layer)), parent_(std::move(parent)) {}

CatalogText CreateLayerCommand::name() const
{
    return CatalogText{QLatin1String("command.layer.create"), {layer_.name}};
}

bool CreateLayerCommand::apply(Document& document, MutationSink& sink)
{
    Result<LayerTree> after = [&]() -> Result<LayerTree> {
        if (!layer_.id.isCreated()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.layer.badId"), layer_.id.toString()};
        LayerTree out = document.layers();
        Layer layer = layer_;
        layer.decoration = false;
        if (Status inserted = out.insert(std::move(layer), parent_); !inserted) return inserted.error();
        return out;
    }();
    return state_.apply(document, sink, std::move(after));
}

QJsonObject CreateLayerCommand::serialize() const
{
    QJsonObject out{{QStringLiteral("type"), QLatin1String(kTypeId)}, {QStringLiteral("layer"), layer_.toJson()}};
    if (parent_.isValid()) out.insert(QStringLiteral("parent"), parent_.toString());
    return out;
}

Result<std::unique_ptr<Command>> CreateLayerCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badLayerCommand(kTypeId, "type");
    const QJsonValue layer = json.value(QStringLiteral("layer"));
    if (!layer.isObject()) return badLayerCommand(kTypeId, "layer");
    Result<Layer> parsed = Layer::fromJson(layer.toObject());
    if (!parsed || !parsed.value().id.isCreated()) return badLayerCommand(kTypeId, "layer");
    LayerId parent;
    if (const QJsonValue value = json.value(QStringLiteral("parent")); !value.isUndefined()) {
        const std::optional<LayerId> id = idFrom(value);
        if (!id) return badLayerCommand(kTypeId, "parent");
        parent = *id;
    }
    return std::unique_ptr<Command>(new CreateLayerCommand(std::move(parsed).value(), std::move(parent)));
}

// ---- DeleteLayerCommand ----------------------------------------------------------------------------------------------

DeleteLayerCommand::DeleteLayerCommand(std::vector<LayerId> ids) : ids_(std::move(ids)) {}

CatalogText DeleteLayerCommand::name() const
{
    if (ids_.size() == 1) return CatalogText{QLatin1String("command.layer.delete"), {firstName_.isEmpty() ? ids_.front().toString() : firstName_}};
    return CatalogText{QLatin1String("command.layer.deleteMany"), {QString::number(static_cast<qulonglong>(ids_.size()))}};
}

bool DeleteLayerCommand::apply(Document& document, MutationSink& sink)
{
    if (!ids_.empty()) firstName_ = nameOf(document, ids_.front());
    Result<LayerTree> after = [&]() -> Result<LayerTree> {
        if (ids_.empty()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.layer.notFound"), QString()};
        LayerTree out = document.layers();
        for (const LayerId& id : ids_) {
            if (Status removed = out.remove(id); !removed) return removed.error();
        }
        return out;
    }();
    return state_.apply(document, sink, std::move(after));
}

QJsonObject DeleteLayerCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)}, {QStringLiteral("ids"), idsTo(ids_)}};
}

Result<std::unique_ptr<Command>> DeleteLayerCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badLayerCommand(kTypeId, "type");
    std::optional<std::vector<LayerId>> ids = idsFrom(json.value(QStringLiteral("ids")));
    if (!ids) return badLayerCommand(kTypeId, "ids");
    return std::unique_ptr<Command>(new DeleteLayerCommand(std::move(*ids)));
}

// ---- MergeLayersCommand ----------------------------------------------------------------------------------------------

MergeLayersCommand::MergeLayersCommand(std::vector<LayerId> from, LayerId into) : from_(std::move(from)), into_(std::move(into)) {}

CatalogText MergeLayersCommand::name() const
{
    return CatalogText{QLatin1String("command.layer.merge"),
                       {QString::number(static_cast<qulonglong>(from_.size())), intoName_.isEmpty() ? into_.toString() : intoName_}};
}

bool MergeLayersCommand::apply(Document& document, MutationSink& sink)
{
    intoName_ = nameOf(document, into_);
    Result<LayerTree> after = [&]() -> Result<LayerTree> {
        if (from_.empty()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.layer.notFound"), QString()};
        LayerTree out = document.layers();
        for (const LayerId& id : from_) {
            if (Status merged = out.merge(id, into_); !merged) return merged.error();
        }
        return out;
    }();
    return state_.apply(document, sink, std::move(after));
}

QJsonObject MergeLayersCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)}, {QStringLiteral("from"), idsTo(from_)}, {QStringLiteral("into"), into_.toString()}};
}

Result<std::unique_ptr<Command>> MergeLayersCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badLayerCommand(kTypeId, "type");
    std::optional<std::vector<LayerId>> from = idsFrom(json.value(QStringLiteral("from")));
    const std::optional<LayerId> into = idFrom(json.value(QStringLiteral("into")));
    if (!from) return badLayerCommand(kTypeId, "from");
    if (!into) return badLayerCommand(kTypeId, "into");
    return std::unique_ptr<Command>(new MergeLayersCommand(std::move(*from), *into));
}

// ---- SetLayerPropertiesCommand ---------------------------------------------------------------------------------------

SetLayerPropertiesCommand::SetLayerPropertiesCommand(LayerId id, LayerProperties properties)
    : id_(std::move(id)), properties_(std::move(properties))
{
}

CatalogText SetLayerPropertiesCommand::name() const
{
    return CatalogText{QLatin1String("command.layer.properties"), {properties_.name}};
}

bool SetLayerPropertiesCommand::apply(Document& document, MutationSink& sink)
{
    Result<LayerTree> after = [&]() -> Result<LayerTree> {
        if (LayerProperties::of(document.layers(), id_) == properties_ && document.layers().find(id_) != nullptr)
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.layer.unchanged"), properties_.name.left(64)};
        LayerTree out = document.layers();
        if (Status set = out.setProperties(id_, properties_); !set) return set.error();
        return out;
    }();
    return state_.apply(document, sink, std::move(after));
}

QJsonObject SetLayerPropertiesCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("id"), id_.toString()},
                       {QStringLiteral("properties"), properties_.toJson()}};
}

Result<std::unique_ptr<Command>> SetLayerPropertiesCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badLayerCommand(kTypeId, "type");
    const std::optional<LayerId> id = idFrom(json.value(QStringLiteral("id")));
    if (!id) return badLayerCommand(kTypeId, "id");
    const QJsonValue properties = json.value(QStringLiteral("properties"));
    if (!properties.isObject()) return badLayerCommand(kTypeId, "properties");
    Result<LayerProperties> parsed = LayerProperties::fromJson(properties.toObject());
    if (!parsed) return badLayerCommand(kTypeId, "properties");
    return std::unique_ptr<Command>(new SetLayerPropertiesCommand(*id, std::move(parsed).value()));
}

} // namespace rawpage::core
