#include "core/cmd/commands/DestinationCommands.h"

#include "core/backend.h"

#include <QJsonArray>
#include <QJsonValue>

#include <set>

namespace rawpage::core {
namespace {

Error badDestinationCommand(QLatin1String type, const char* field)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"),
                 QString(type) + QLatin1Char('.') + QLatin1String(field)};
}

bool typeIs(const QJsonObject& json, QLatin1String type)
{
    const QJsonValue value = json.value(QStringLiteral("type"));
    return value.isString() && value.toString() == type;
}

std::optional<QString> nameFrom(const QJsonObject& json, const char* key)
{
    const QJsonValue value = json.value(QLatin1String(key));
    if (!value.isString() || value.toString().isEmpty() || value.toString().size() > kMaxDestinationNameLength) return std::nullopt;
    return value.toString();
}

template <typename Visit>
void walkItems(std::vector<OutlineItem>& items, Visit&& visit)
{
    for (OutlineItem& item : items) {
        visit(item);
        walkItems(item.children, visit);
    }
}

} // namespace

Status destinationCommandPermitted(const Document& document)
{
    if (Status status = document.permissions().may(Operation::EditOutline); !status) return status;
    return document.destinations().status();
}

DestinationReferences referencesTo(const Document& document, const std::vector<QString>& names, const QString& to)
{
    const std::set<QString> named(names.begin(), names.end());
    const auto names_ = [&named](const OutlineAction& action) {
        return action.kind == OutlineAction::Kind::GoToNamed && named.count(action.name) != 0;
    };
    DestinationReferences out;
    for (const Annotation& a : document.annotations().all()) {
        if (a.subtype != AnnotationSubtype::Link || !names_(a.action)) continue;
        Annotation after = a;
        if (to.isEmpty()) after.action = OutlineAction{};   // cleared: it keeps its rectangle and goes nowhere
        else after.action.name = to;
        out.linksBefore.push_back(a);
        out.linksAfter.push_back(std::move(after));
    }
    Outline outline = document.outline();
    walkItems(outline.roots(), [&](OutlineItem& item) {
        if (!names_(item.action)) return;
        ++out.bookmarks;
        if (to.isEmpty()) {
            item.action = OutlineAction{};
            item.destination = Destination{};
        } else {
            item.action.name = to;
        }
    });
    if (out.bookmarks != 0) out.outlineAfter = std::move(outline);
    return out;
}

bool DestinationEditState::apply(Document& document, MutationSink& sink, Result<Destinations> after,
                                 std::vector<Annotation> linksWere, std::vector<Annotation> linksNow,
                                 std::optional<Outline> outlineAfter)
{
    refusal.reset();
    if (!after) {
        refusal = after.error();
        return false;
    }
    Destinations next = std::move(after).value();
    if (Status valid = next.validate(); !valid) {
        refusal = valid.error();
        return false;
    }
    // What else it changes must be changeable: the links' pages annotated, the outline editable.
    for (const Annotation& link : linksWere) {
        if (Status page = document.permissions().mayAnnotate(link.page); !page) {
            refusal = page.error();
            return false;
        }
    }
    if (outlineAfter) {
        if (Status editable = document.outline().status(); !editable) {
            refusal = editable.error();
            return false;
        }
    }

    // SINK FIRST, each step put back if a later one refuses.
    const Destinations was = document.destinations();
    if (Status shown = sink.setDestinations(next); !shown) {
        refusal = shown.error();
        return false;
    }
    if (!linksWere.empty()) {
        if (Status changed = sink.replaceAnnotations(linksWere, linksNow); !changed) {
            refusal = changed.error();
            sink.setDestinations(was);
            return false;
        }
    }
    if (outlineAfter) {
        if (Status changed = sink.setOutline(*outlineAfter); !changed) {
            refusal = changed.error();
            if (!linksWere.empty()) sink.replaceAnnotations(linksNow, linksWere);
            sink.setDestinations(was);
            return false;
        }
    }

    // Then the model -- none of which can refuse what the checks above allowed.
    if (!linksNow.empty() && !document.annotations().replace(linksNow)) return false;
    before = was;
    next.setStatus(was.status());
    document.destinations() = std::move(next);
    linksBefore = std::move(linksWere);
    linksAfter = std::move(linksNow);
    outlineBefore.reset();
    if (outlineAfter) {
        outlineBefore = document.outline();
        outlineAfter->setStatus(document.outline().status());
        document.outline() = std::move(*outlineAfter);
    }
    return true;
}

bool DestinationEditState::revert(Document& document, MutationSink& sink)
{
    // The journal undoes in order, so the model is what this command made, and each "before" is exact.
    if (!before) return false;
    if (outlineBefore && !sink.setOutline(*outlineBefore)) return false;
    if (!linksBefore.empty() && !sink.replaceAnnotations(linksAfter, linksBefore)) return false;
    if (!sink.setDestinations(*before)) return false;
    if (outlineBefore) document.outline() = *outlineBefore;
    if (!linksBefore.empty() && !document.annotations().replace(linksBefore)) return false;
    document.destinations() = *before;
    return true;
}

// ---- CreateDestinationCommand ------------------------------------------------------------------------

CreateDestinationCommand::CreateDestinationCommand(NamedDestination entry) : entry_(std::move(entry)) {}

CatalogText CreateDestinationCommand::name() const
{
    return CatalogText{QLatin1String("command.destination.create"), {entry_.name}};
}

bool CreateDestinationCommand::apply(Document& document, MutationSink& sink)
{
    Result<Destinations> after = [&]() -> Result<Destinations> {
        // A new entry names a page the document has, and goes in the name tree (D7-35).
        if (entry_.form != NamedDestination::Form::NameTree || entry_.destination.pageIndex < 0
            || entry_.destination.pageIndex >= document.pages().count())
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.destination.badDestination"), entry_.name};
        Destinations out = document.destinations();
        if (Status inserted = out.insert(entry_); !inserted) return inserted.error();
        return out;
    }();
    return state_.apply(document, sink, std::move(after), {}, {}, std::nullopt);
}

QJsonObject CreateDestinationCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)}, {QStringLiteral("entry"), entry_.toJson()}};
}

Result<std::unique_ptr<Command>> CreateDestinationCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badDestinationCommand(kTypeId, "type");
    const QJsonValue entry = json.value(QStringLiteral("entry"));
    if (!entry.isObject()) return badDestinationCommand(kTypeId, "entry");
    Result<NamedDestination> parsed = NamedDestination::fromJson(entry.toObject());
    if (!parsed) return badDestinationCommand(kTypeId, "entry");
    return std::unique_ptr<Command>(new CreateDestinationCommand(std::move(parsed).value()));
}

// ---- RenameDestinationCommand ------------------------------------------------------------------------

RenameDestinationCommand::RenameDestinationCommand(QString from, QString to) : from_(std::move(from)), to_(std::move(to)) {}

CatalogText RenameDestinationCommand::name() const
{
    return CatalogText{QLatin1String("command.destination.rename"), {from_, to_}};
}

bool RenameDestinationCommand::apply(Document& document, MutationSink& sink)
{
    Result<Destinations> after = [&]() -> Result<Destinations> {
        if (from_ == to_) return Error{ErrorCode::InvalidArgument, QLatin1String("error.destination.nameUnchanged"), to_};
        Destinations out = document.destinations();
        if (Status renamed = out.rename(from_, to_); !renamed) return renamed.error();
        return out;
    }();
    DestinationReferences references = after ? referencesTo(document, {from_}, to_) : DestinationReferences{};
    bookmarks_ = references.bookmarks;
    return state_.apply(document, sink, std::move(after), std::move(references.linksBefore), std::move(references.linksAfter),
                        std::move(references.outlineAfter));
}

QJsonObject RenameDestinationCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)}, {QStringLiteral("from"), from_}, {QStringLiteral("to"), to_}};
}

Result<std::unique_ptr<Command>> RenameDestinationCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badDestinationCommand(kTypeId, "type");
    const std::optional<QString> from = nameFrom(json, "from");
    const std::optional<QString> to = nameFrom(json, "to");
    if (!from || !to) return badDestinationCommand(kTypeId, "name");
    return std::unique_ptr<Command>(new RenameDestinationCommand(*from, *to));
}

// ---- DeleteDestinationCommand ------------------------------------------------------------------------

DeleteDestinationCommand::DeleteDestinationCommand(std::vector<QString> names) : names_(std::move(names)) {}

CatalogText DeleteDestinationCommand::name() const
{
    if (names_.size() == 1) return CatalogText{QLatin1String("command.destination.delete"), {names_.front()}};
    return CatalogText{QLatin1String("command.destination.deleteMany"), {QString::number(static_cast<qulonglong>(names_.size()))}};
}

bool DeleteDestinationCommand::apply(Document& document, MutationSink& sink)
{
    Result<Destinations> after = [&]() -> Result<Destinations> {
        if (names_.empty()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.destination.noSuchName"), QString()};
        Destinations out = document.destinations();
        for (const QString& name : names_) {
            auto removed = out.remove(name);
            if (!removed) return removed.error();
        }
        return out;
    }();
    DestinationReferences references = after ? referencesTo(document, names_, QString()) : DestinationReferences{};
    bookmarks_ = references.bookmarks;
    return state_.apply(document, sink, std::move(after), std::move(references.linksBefore), std::move(references.linksAfter),
                        std::move(references.outlineAfter));
}

QJsonObject DeleteDestinationCommand::serialize() const
{
    QJsonArray names;
    for (const QString& name : names_) names.append(name);
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)}, {QStringLiteral("names"), names}};
}

Result<std::unique_ptr<Command>> DeleteDestinationCommand::deserialize(const QJsonObject& json)
{
    if (!typeIs(json, kTypeId)) return badDestinationCommand(kTypeId, "type");
    const QJsonValue value = json.value(QStringLiteral("names"));
    if (!value.isArray()) return badDestinationCommand(kTypeId, "names");
    const QJsonArray array = value.toArray();
    if (array.isEmpty() || static_cast<std::size_t>(array.size()) > kMaxNamedDestinations)
        return badDestinationCommand(kTypeId, "names");
    std::vector<QString> names;
    std::set<QString> seen;
    for (const QJsonValue& name : array) {
        if (!name.isString() || name.toString().isEmpty() || name.toString().size() > kMaxDestinationNameLength
            || !seen.insert(name.toString()).second)
            return badDestinationCommand(kTypeId, "names");
        names.push_back(name.toString());
    }
    return std::unique_ptr<Command>(new DeleteDestinationCommand(std::move(names)));
}

} // namespace rawpage::core
