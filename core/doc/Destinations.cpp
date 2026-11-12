#include "core/doc/Destinations.h"

#include "core/doc/Outline.h"   // destinationToJson, destinationFromJson
#include "core/doc/PageMap.h"

#include <QCryptographicHash>
#include <QJsonValue>

#include <algorithm>
#include <cstring>
#include <set>

namespace rawpage::core {
namespace {

Error badDestination(const char* what)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.destination.malformed"), QString::fromLatin1(what)};
}

void addInt(QByteArray& out, qint64 value)
{
    const quint64 bits = static_cast<quint64>(value);
    for (int shift = 56; shift >= 0; shift -= 8) out.append(static_cast<char>((bits >> shift) & 0xFF));
}

void addReal(QByteArray& out, double value)
{
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    addInt(out, static_cast<qint64>(bits));
}

void addText(QByteArray& out, const QString& text)
{
    const QByteArray bytes = text.toUtf8();
    addInt(out, bytes.size());
    out.append(bytes);
}

} // namespace

const char* destinationFitName(Destination::Fit fit) noexcept
{
    switch (fit) {
    case Destination::Fit::Unknown: return "Unknown";
    case Destination::Fit::XYZ:     return "XYZ";
    case Destination::Fit::Fit:     return "Fit";
    case Destination::Fit::FitH:    return "FitH";
    case Destination::Fit::FitV:    return "FitV";
    case Destination::Fit::FitR:    return "FitR";
    case Destination::Fit::FitB:    return "FitB";
    case Destination::Fit::FitBH:   return "FitBH";
    case Destination::Fit::FitBV:   return "FitBV";
    }
    return "Unknown";
}

const char* namedDestinationFormName(NamedDestination::Form form) noexcept
{
    return form == NamedDestination::Form::Dictionary ? "dictionary" : "nameTree";
}

bool isWritableDestinationName(const QString& name)
{
    if (name.isEmpty() || name.size() > kMaxDestinationNameLength) return false;
    if (name.front() == QLatin1Char(' ') || name.back() == QLatin1Char(' ')) return false;
    return std::all_of(name.begin(), name.end(), [](QChar c) { return c.unicode() >= 0x20 && c.unicode() <= 0x7E; });
}

QJsonObject NamedDestination::toJson() const
{
    return QJsonObject{{QStringLiteral("name"), name},
                       {QStringLiteral("form"), QLatin1String(namedDestinationFormName(form))},
                       {QStringLiteral("dest"), destinationToJson(destination)}};
}

Result<NamedDestination> NamedDestination::fromJson(const QJsonObject& json)
{
    NamedDestination entry;
    const QJsonValue name = json.value(QStringLiteral("name"));
    if (!name.isString() || name.toString().isEmpty() || name.toString().size() > kMaxDestinationNameLength)
        return badDestination("name");
    entry.name = name.toString();
    const QJsonValue form = json.value(QStringLiteral("form"));
    if (form == QJsonValue(QLatin1String("nameTree"))) entry.form = Form::NameTree;
    else if (form == QJsonValue(QLatin1String("dictionary"))) entry.form = Form::Dictionary;
    else return badDestination("form");
    const QJsonValue dest = json.value(QStringLiteral("dest"));
    if (!dest.isObject()) return badDestination("dest");
    Result<Destination> destination = destinationFromJson(dest.toObject());
    if (!destination) return destination.error();
    entry.destination = destination.value();
    return entry;
}

const NamedDestination* Destinations::find(const QString& name) const
{
    const auto at = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const NamedDestination& entry) { return entry.name == name; });
    return at == entries_.end() ? nullptr : &*at;
}

std::size_t Destinations::unresolvedCount() const
{
    return static_cast<std::size_t>(
        std::count_if(entries_.begin(), entries_.end(), [](const NamedDestination& entry) {
            return !entry.destination.isResolved();
        }));
}

Destinations Destinations::refused(Error why, std::vector<NamedDestination> entries)
{
    Destinations table;
    table.entries_ = std::move(entries);
    table.status_ = std::move(why);
    return table;
}

Status Destinations::insert(NamedDestination entry)
{
    if (!isWritableDestinationName(entry.name))
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.destination.badName"), entry.name.left(64)};
    if (find(entry.name) != nullptr)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.destination.nameTaken"), entry.name};
    if (entries_.size() >= kMaxNamedDestinations)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.destination.tooMany"), QString::number(entries_.size())};
    entries_.push_back(std::move(entry));
    return Status{};
}

Result<std::pair<NamedDestination, std::size_t>> Destinations::remove(const QString& name)
{
    const auto at = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const NamedDestination& entry) { return entry.name == name; });
    if (at == entries_.end()) return Error{ErrorCode::NotFound, QLatin1String("error.destination.noSuchName"), name};
    const std::size_t index = static_cast<std::size_t>(at - entries_.begin());
    NamedDestination removed = std::move(*at);
    entries_.erase(at);
    return std::make_pair(std::move(removed), index);
}

Status Destinations::restore(NamedDestination entry, std::size_t at)
{
    if (at > entries_.size() || find(entry.name) != nullptr)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.destination.nameTaken"), entry.name};
    entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(at), std::move(entry));
    return Status{};
}

Status Destinations::rename(const QString& from, const QString& to)
{
    const auto at = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const NamedDestination& entry) { return entry.name == from; });
    if (at == entries_.end()) return Error{ErrorCode::NotFound, QLatin1String("error.destination.noSuchName"), from};
    if (!isWritableDestinationName(to))
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.destination.badName"), to.left(64)};
    if (to != from && find(to) != nullptr)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.destination.nameTaken"), to};
    at->name = to;
    return Status{};
}

void Destinations::applyPageMap(const PageMap& map)
{
    for (NamedDestination& entry : entries_) {
        Destination& d = entry.destination;
        if (d.pageIndex < 0) continue;
        d.pageIndex = d.pageIndex < map.oldCount() ? map.newIndexOf(d.pageIndex) : -1;
        if (d.pageIndex < 0) d.pageIndex = -1;
    }
}

Status Destinations::validate() const
{
    if (entries_.size() > kMaxNamedDestinations)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.destination.tooMany"), QString::number(entries_.size())};
    std::set<QString> seen;
    for (const NamedDestination& entry : entries_) {
        if (entry.name.isEmpty() || entry.name.size() > kMaxDestinationNameLength)
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.destination.badName"), entry.name.left(64)};
        if (!seen.insert(entry.name).second)
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.destination.nameTaken"), entry.name};
    }
    return Status{};
}

std::vector<const NamedDestination*> Destinations::sortedByName() const
{
    std::vector<const NamedDestination*> out;
    out.reserve(entries_.size());
    for (const NamedDestination& entry : entries_) out.push_back(&entry);
    std::stable_sort(out.begin(), out.end(), [](const NamedDestination* a, const NamedDestination* b) { return a->name < b->name; });
    return out;
}

std::vector<const NamedDestination*> Destinations::sortedByPage() const
{
    std::vector<const NamedDestination*> out;
    out.reserve(entries_.size());
    for (const NamedDestination& entry : entries_) out.push_back(&entry);
    std::stable_sort(out.begin(), out.end(), [](const NamedDestination* a, const NamedDestination* b) {
        const bool ra = a->destination.isResolved();
        const bool rb = b->destination.isResolved();
        if (ra != rb) return ra;   // an unresolved entry last
        if (a->destination.pageIndex != b->destination.pageIndex) return a->destination.pageIndex < b->destination.pageIndex;
        return a->name < b->name;
    });
    return out;
}

QByteArray Destinations::digest() const
{
    QByteArray buffer;
    addInt(buffer, status_ ? 1 : 0);
    addInt(buffer, static_cast<qint64>(entries_.size()));
    for (const NamedDestination& entry : entries_) {
        addText(buffer, entry.name);
        addInt(buffer, static_cast<qint64>(entry.form));
        const Destination& d = entry.destination;
        addInt(buffer, d.pageIndex);
        addInt(buffer, static_cast<qint64>(d.fit));
        addInt(buffer, (d.hasX ? 1 : 0) | (d.hasY ? 2 : 0) | (d.hasZoom ? 4 : 0));
        addReal(buffer, d.x);
        addReal(buffer, d.y);
        addReal(buffer, d.zoom);
    }
    return QCryptographicHash::hash(buffer, QCryptographicHash::Sha256);
}

} // namespace rawpage::core
