// core/cmd/commands/PageCommands -- implementation. Stage 3 T5. See the header for why a turn is relative.

#include "core/cmd/commands/PageCommands.h"

#include "core/backend.h"
#include "core/doc/Document.h"

#include <QJsonValue>

#include <cmath>

namespace rawpage::core {
namespace {

int normalizeTurn(int degrees)
{
    return ((degrees % 360) + 360) % 360;
}

Error badCommand(const char* field)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"),
                 QLatin1String(RotatePageCommand::kTypeId) + QLatin1Char('.') + QLatin1String(field)};
}

} // namespace

RotatePageCommand::RotatePageCommand(int pageIndex, int degreesClockwise)
    : pageIndex_(pageIndex), degrees_(degreesClockwise % 90 == 0 ? normalizeTurn(degreesClockwise) : -1)
{
}

bool RotatePageCommand::isValid() const noexcept
{
    return pageIndex_ >= 0 && pageIndex_ <= kMaxPageIndex
           && (degrees_ == 90 || degrees_ == 180 || degrees_ == 270);
}

CatalogText RotatePageCommand::name() const
{
    return CatalogText{QLatin1String("command.page.rotate"),
                       {QString::number(pageIndex_ + 1), QString::number(degrees_)}};
}

bool RotatePageCommand::turn(Document& document, MutationSink& sink, int degrees)
{
    // Validate first: nothing below may run for a page that does not exist.
    if (!isValid() || pageIndex_ >= document.pages().count())
        return false;

    const int target = normalizeTurn(static_cast<int>(document.pages().rotation(pageIndex_)) + degrees);
    if (!sink.setPageRotation(pageIndex_, target))
        return false;                       // the engine side refused; the model is untouched

    // Cannot fail for an index checked above -- and if it ever could, the sink would already hold the
    // new rotation, which is exactly the partial application CommandJournal exists to report.
    document.pages().setRotation(pageIndex_, target);
    return true;
}

bool RotatePageCommand::apply(Document& document, MutationSink& sink)
{
    return turn(document, sink, degrees_);
}

bool RotatePageCommand::revert(Document& document, MutationSink& sink)
{
    return turn(document, sink, 360 - degrees_);
}

QJsonObject RotatePageCommand::serialize() const
{
    return QJsonObject{{QStringLiteral("type"), QLatin1String(kTypeId)},
                       {QStringLiteral("page"), pageIndex_},
                       {QStringLiteral("degrees"), degrees_}};
}

Result<std::unique_ptr<Command>> RotatePageCommand::deserialize(const QJsonObject& json)
{
    // Every field checked for presence, type and range before a command exists: this is the recovery
    // journal's parser, and the journal is a file on disk (guide case A-45).
    const QJsonValue type = json.value(QStringLiteral("type"));
    if (!type.isString() || type.toString() != QLatin1String(kTypeId))
        return badCommand("type");

    const QJsonValue page = json.value(QStringLiteral("page"));
    const QJsonValue degrees = json.value(QStringLiteral("degrees"));
    if (!page.isDouble() || !degrees.isDouble())
        return badCommand(!page.isDouble() ? "page" : "degrees");

    // A JSON number is a double: it must be a whole number in range before it becomes an int.
    const double pageValue = page.toDouble();
    if (!std::isfinite(pageValue) || std::trunc(pageValue) != pageValue || pageValue < 0.0
        || pageValue > static_cast<double>(kMaxPageIndex)) {
        return badCommand("page");
    }
    const double degreeValue = degrees.toDouble();
    if (degreeValue != 90.0 && degreeValue != 180.0 && degreeValue != 270.0)
        return badCommand("degrees");

    return std::unique_ptr<Command>(
        new RotatePageCommand(static_cast<int>(pageValue), static_cast<int>(degreeValue)));
}

} // namespace rawpage::core
