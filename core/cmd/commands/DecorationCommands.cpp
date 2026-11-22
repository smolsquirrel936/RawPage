// core/cmd/commands/DecorationCommands -- see the header.

#include "core/cmd/commands/DecorationCommands.h"

#include "core/backend.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/BatesAllocator.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"

#include <QJsonValue>

#include <algorithm>
#include <iterator>
#include <set>

namespace rawpage::core {
namespace {

Error badDecorationCommand(QLatin1String type, const char* field)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"),
                 QString(type) + QLatin1Char('.') + QLatin1String(field)};
}

bool typeIs(const QJsonObject& json, QLatin1String type)
{
    const QJsonValue value = json.value(QStringLiteral("type"));
    return value.isString() && value.toString() == type;
}

Result<QString> keyFrom(const QJsonObject& json, QLatin1String type)
{
    const QJsonValue value = json.value(QStringLiteral("key"));
    if (!value.isString()) return badDecorationCommand(type, "key");
    const QString key = value.toString();
    if (key.size() != 32) return badDecorationCommand(type, "key");
    for (const QChar c : key) {
        if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))))
            return badDecorationCommand(type, "key");
    }
    return key;
}

struct Decoded {
    HeaderFooterSpec spec;
    FieldValues values;
    QString key;
};

Result<Decoded> decodeApply(const QJsonObject& json, QLatin1String type)
{
    if (!typeIs(json, type)) return badDecorationCommand(type, "type");
    auto key = keyFrom(json, type);
    if (!key) return key.error();
    const QJsonValue spec = json.value(QStringLiteral("spec"));
    const QJsonValue values = json.value(QStringLiteral("values"));
    if (!spec.isObject()) return badDecorationCommand(type, "spec");
    if (!values.isObject()) return badDecorationCommand(type, "values");
    auto decodedSpec = specFromJson(spec.toObject());
    if (!decodedSpec) return badDecorationCommand(type, "spec");
    auto decodedValues = fieldValuesFromJson(values.toObject());
    if (!decodedValues) return badDecorationCommand(type, "values");
    return Decoded{std::move(decodedSpec).value(), std::move(decodedValues).value(), std::move(key).value()};
}

CatalogText countName(const char* key, std::size_t pages)
{
    return CatalogText{QLatin1String(key), {QString::number(static_cast<qulonglong>(pages))}};
}

} // namespace

// ---- DecorationApply ----------------------------------------------------------------------------------------------

DecorationApply::DecorationApply(DecorationKind kind, HeaderFooterSpec spec, FieldValues values, bool replace, QString key)
    : kind_(kind), spec_(std::move(spec)), values_(std::move(values)), replace_(replace),
      key_(key.isEmpty() ? newCommandKey() : std::move(key))
{
}

Status DecorationApply::permitted(const Document& document) const
{
    if (Status status = document.permissions().may(Operation::EditPages); !status) return status;
    Result<std::vector<int>> pages = headerFooterPages(spec_, document.pages());
    if (!pages) return Status{};   // apply refuses it by key, with nothing changed
    for (const int page : pages.value()) {
        if (Status status = document.permissions().mayEditPage(page); !status) return status;
    }
    return Status{};
}

Scope DecorationApply::scope() const
{
    std::set<int> all(pages_.begin(), pages_.end());
    all.insert(taken_.begin(), taken_.end());
    return Scope::pages(std::vector<int>(all.begin(), all.end()));
}

bool DecorationApply::apply(Document& document, MutationSink& sink, std::optional<Error>& refusal)
{
    refusal.reset();
    if (Status valid = validate(spec_); !valid) {
        refusal = valid.error();
        return false;
    }
    if (kind_ == DecorationKind::Bates && !usesBates(spec_)) {
        refusal = Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.badSpec"), QStringLiteral("bates")};
        return false;
    }
    Result<std::vector<int>> pages = headerFooterPages(spec_, document.pages());
    if (!pages) {
        refusal = pages.error();
        return false;
    }
    const std::vector<int>& at = pages.value();
    if (usesBates(spec_)) {
        const long long last = spec_.bates.start + static_cast<long long>(at.size()) - 1;
        if (last > largestBates(spec_.bates.digits)) {
            refusal = Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.batesOverflow"), values_.fileName};
            return false;
        }
    }
    // EVERY PAGE LAID OUT before the sink hears of it: a field that expands to a character no face can show refuses the
    // whole command, with nothing changed.
    std::vector<PageDecoration> decorations;
    decorations.reserve(at.size());
    const int count = document.pages().count();
    for (std::size_t i = 0; i < at.size(); ++i) {
        const Page* page = document.pages().at(at[i]);
        if (!page) return false;
        FieldPage field{at[i], count, page->label(), std::nullopt};
        if (usesBates(spec_)) field.bates = spec_.bates.start + static_cast<long long>(i);
        Result<PageDecoration> laid = layoutHeaderFooter(spec_, kind_, *page, values_, field);
        if (!laid) {
            refusal = laid.error();
            return false;
        }
        decorations.push_back(std::move(laid).value());
    }
    std::vector<int> taken;
    if (replace_) {
        Result<std::vector<int>> took = sink.takeDecorations(kind_, key_);
        if (!took) {
            refusal = took.error();
            return false;
        }
        taken = std::move(took).value();
    }
    if (Status added = sink.addPageDecorations(at, decorations, key_); !added) {
        // THE SECOND HALF REFUSED: the first is taken back, so a refusal changes nothing (`Command.h`).
        if (!taken.empty()) (void)sink.restoreDecorations(key_);
        refusal = added.error();
        return false;
    }
    pages_ = at;
    taken_ = std::move(taken);
    return true;
}

bool DecorationApply::revert(MutationSink& sink)
{
    if (pages_.empty()) return false;
    if (!sink.removePageDecorations(pages_, key_)) return false;
    if (!taken_.empty() && !sink.restoreDecorations(key_)) return false;
    return true;
}

// ---- ApplyHeaderFooterCommand ---------------------------------------------------------------------------------------

ApplyHeaderFooterCommand::ApplyHeaderFooterCommand(HeaderFooterSpec spec, FieldValues values, bool replace, QString key)
    : state_(DecorationKind::HeaderFooter, std::move(spec), std::move(values), replace, std::move(key))
{
}

CatalogText ApplyHeaderFooterCommand::name() const
{
    return countName(state_.replaces() ? "command.headerFooter.update" : "command.headerFooter.apply", state_.pages().size());
}

bool ApplyHeaderFooterCommand::apply(Document& document, MutationSink& sink)
{
    return state_.apply(document, sink, refusal_);
}

bool ApplyHeaderFooterCommand::revert(Document&, MutationSink& sink)
{
    return state_.revert(sink);
}

QJsonObject ApplyHeaderFooterCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("key"), state_.key()},
                       {QStringLiteral("replace"), state_.replaces()},
                       {QStringLiteral("spec"), specToJson(state_.spec())},
                       {QStringLiteral("values"), fieldValuesToJson(state_.values())}};
}

Result<std::unique_ptr<Command>> ApplyHeaderFooterCommand::deserialize(const QJsonObject& json)
{
    auto decoded = decodeApply(json, kTypeId);
    if (!decoded) return decoded.error();
    const QJsonValue replace = json.value(QStringLiteral("replace"));
    if (!replace.isBool()) return badDecorationCommand(kTypeId, "replace");
    Decoded d = std::move(decoded).value();
    return std::unique_ptr<Command>(new ApplyHeaderFooterCommand(std::move(d.spec), std::move(d.values), replace.toBool(), std::move(d.key)));
}

// ---- ApplyBatesCommand ----------------------------------------------------------------------------------------------

ApplyBatesCommand::ApplyBatesCommand(HeaderFooterSpec spec, FieldValues values, QString key)
    : state_(DecorationKind::Bates, std::move(spec), std::move(values), false, std::move(key))
{
}

CatalogText ApplyBatesCommand::name() const
{
    return countName("command.bates.apply", state_.pages().size());
}

bool ApplyBatesCommand::apply(Document& document, MutationSink& sink)
{
    return state_.apply(document, sink, refusal_);
}

bool ApplyBatesCommand::revert(Document&, MutationSink& sink)
{
    return state_.revert(sink);
}

QJsonObject ApplyBatesCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("key"), state_.key()},
                       {QStringLiteral("spec"), specToJson(state_.spec())},
                       {QStringLiteral("values"), fieldValuesToJson(state_.values())}};
}

Result<std::unique_ptr<Command>> ApplyBatesCommand::deserialize(const QJsonObject& json)
{
    auto decoded = decodeApply(json, kTypeId);
    if (!decoded) return decoded.error();
    Decoded d = std::move(decoded).value();
    if (!usesBates(d.spec)) return badDecorationCommand(kTypeId, "spec");
    return std::unique_ptr<Command>(new ApplyBatesCommand(std::move(d.spec), std::move(d.values), std::move(d.key)));
}

// ---- DecorationRemove -------------------------------------------------------------------------------------------------

DecorationRemove::DecorationRemove(DecorationKind kind, QString key)
    : kind_(kind), key_(key.isEmpty() ? newCommandKey() : std::move(key))
{
}

Status DecorationRemove::permitted(const Document& document) const
{
    return document.permissions().may(Operation::EditPages);
}

bool DecorationRemove::apply(MutationSink& sink, std::optional<Error>& refusal)
{
    refusal.reset();
    Result<std::vector<int>> took = sink.takeDecorations(kind_, key_);
    if (!took) {
        refusal = took.error();
        return false;
    }
    if (took.value().empty()) {   // nothing of this kind anywhere: no change, and no undo step
        refusal = Error{ErrorCode::NotFound, QLatin1String("error.decorate.noneFound"), QString(decorationKindName(kind_))};
        return false;
    }
    pages_ = std::move(took).value();
    return true;
}

bool DecorationRemove::revert(MutationSink& sink)
{
    if (pages_.empty()) return false;
    return static_cast<bool>(sink.restoreDecorations(key_));
}

// ---- RemoveHeaderFooterCommand, RemoveBatesCommand ---------------------------------------------------------------------

RemoveHeaderFooterCommand::RemoveHeaderFooterCommand(QString key) : state_(DecorationKind::HeaderFooter, std::move(key)) {}

CatalogText RemoveHeaderFooterCommand::name() const
{
    return countName("command.headerFooter.remove", state_.pages().size());
}

bool RemoveHeaderFooterCommand::apply(Document&, MutationSink& sink)
{
    return state_.apply(sink, refusal_);
}

bool RemoveHeaderFooterCommand::revert(Document&, MutationSink& sink)
{
    return state_.revert(sink);
}

QJsonObject RemoveHeaderFooterCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)}, {QStringLiteral("key"), state_.key()}};
}

Result<std::unique_ptr<Command>> RemoveHeaderFooterCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badDecorationCommand(kTypeId, "type");
    auto key = keyFrom(json, kTypeId);
    if (!key) return key.error();
    return std::unique_ptr<Command>(new RemoveHeaderFooterCommand(std::move(key).value()));
}

RemoveBatesCommand::RemoveBatesCommand(QString key) : state_(DecorationKind::Bates, std::move(key)) {}

CatalogText RemoveBatesCommand::name() const
{
    return countName("command.bates.remove", state_.pages().size());
}

bool RemoveBatesCommand::apply(Document&, MutationSink& sink)
{
    return state_.apply(sink, refusal_);
}

bool RemoveBatesCommand::revert(Document&, MutationSink& sink)
{
    return state_.revert(sink);
}

QJsonObject RemoveBatesCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)}, {QStringLiteral("key"), state_.key()}};
}

Result<std::unique_ptr<Command>> RemoveBatesCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badDecorationCommand(kTypeId, "type");
    auto key = keyFrom(json, kTypeId);
    if (!key) return key.error();
    return std::unique_ptr<Command>(new RemoveBatesCommand(std::move(key).value()));
}

} // namespace rawpage::core
