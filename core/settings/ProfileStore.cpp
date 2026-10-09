// core/settings/ProfileStore -- implementation. Stage 6 T10. See the header for the layout on disk and why a
// stamp is two files of its own.

#include "core/settings/ProfileStore.h"

#include "core/doc/Annotation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace rawpage::core {
namespace {

constexpr qint64 kMaxDescriptionBytes = 64 * 1024;
const QLatin1String kFavouritesFile("favourites.json");

Error storeError(const char* key, const QString& detail = QString())
{
    return Error{ErrorCode::InvalidArgument, QLatin1String(key), detail};
}

bool isId(const QString& id)
{
    if (id.size() != 32) return false;
    return std::all_of(id.begin(), id.end(), [](QChar c) {
        return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
    });
}

bool validName(const QString& name)
{
    const QString trimmed = name.trimmed();
    return !trimmed.isEmpty() && trimmed.size() <= ProfileStore::kMaxNameLength;
}

bool validCategory(const QString& category)
{
    if (category == QLatin1String(ProfileStore::kSignaturesCategory)) return true;
    return validName(category) && !category.trimmed().startsWith(QLatin1Char(':'));
}

Status writeWhole(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return Error{ErrorCode::IoFailure, QLatin1String("error.stamps.writeFailed"), path};
    return Status{};
}

std::optional<QByteArray> readCapped(const QString& path, qint64 cap)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > cap) return std::nullopt;
    return file.readAll();
}

} // namespace

ProfileStore::ProfileStore(QString root) : root_(std::move(root)) {}

QString ProfileStore::defaultRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/stamps");
}

QString ProfileStore::imageFormatOf(const QByteArray& bytes)
{
    if (bytes.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8))) return QStringLiteral("png");
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xD8
        && static_cast<unsigned char>(bytes[2]) == 0xFF)
        return QStringLiteral("jpg");
    return QString();
}

QString ProfileStore::descriptionPath(const QString& id) const { return QDir(root_).filePath(id + QStringLiteral(".json")); }

QString ProfileStore::imagePath(const CustomStamp& stamp) const
{
    return QDir(root_).filePath(stamp.id + QLatin1Char('.') + stamp.format);
}

void ProfileStore::load()
{
    stamps_.clear();
    builtInFavourites_.clear();
    damaged_.clear();
    const QDir dir(root_);
    if (!dir.exists()) return;

    if (dir.exists(kFavouritesFile)) {
        const std::optional<QByteArray> bytes = readCapped(dir.filePath(kFavouritesFile), kMaxDescriptionBytes);
        QJsonParseError parse{};
        const QJsonDocument json = bytes ? QJsonDocument::fromJson(*bytes, &parse) : QJsonDocument();
        if (!bytes || parse.error != QJsonParseError::NoError || !json.isObject()) {
            damaged_ << kFavouritesFile;
        } else {
            for (const QJsonValue name : json.object().value(QStringLiteral("builtIn")).toArray()) {
                if (name.isString() && validName(name.toString())) builtInFavourites_.insert(name.toString());
            }
        }
    }

    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString& file : files) {
        if (file == kFavouritesFile) continue;
        const QString id = QFileInfo(file).completeBaseName();
        if (!isId(id)) continue;   // not ours: a library holds only what it wrote
        if (static_cast<int>(stamps_.size()) >= kMaxStamps) {
            damaged_ << file;
            continue;
        }
        const std::optional<QByteArray> bytes = readCapped(dir.filePath(file), kMaxDescriptionBytes);
        QJsonParseError parse{};
        const QJsonDocument json = bytes ? QJsonDocument::fromJson(*bytes, &parse) : QJsonDocument();
        if (!bytes || parse.error != QJsonParseError::NoError || !json.isObject()) {
            damaged_ << file;
            continue;
        }
        const QJsonObject o = json.object();
        CustomStamp stamp;
        stamp.id = id;
        stamp.name = o.value(QStringLiteral("name")).toString();
        stamp.category = o.value(QStringLiteral("category")).toString();
        stamp.favourite = o.value(QStringLiteral("favourite")).toBool();
        stamp.format = o.value(QStringLiteral("format")).toString();
        stamp.created = static_cast<std::int64_t>(o.value(QStringLiteral("created")).toDouble());
        const bool formatKnown = stamp.format == QLatin1String("png") || stamp.format == QLatin1String("jpg");
        if (o.value(QStringLiteral("version")).toInt() != 1 || !validName(stamp.name) || !validCategory(stamp.category)
            || !formatKnown || !QFileInfo(imagePath(stamp)).isFile()) {
            damaged_ << file;
            continue;
        }
        stamps_.push_back(std::move(stamp));
    }
    std::sort(stamps_.begin(), stamps_.end(), [](const CustomStamp& a, const CustomStamp& b) {
        return a.created != b.created ? a.created < b.created : a.id < b.id;
    });
}

std::optional<CustomStamp> ProfileStore::find(const QString& id) const
{
    const auto found = std::find_if(stamps_.begin(), stamps_.end(), [&id](const CustomStamp& s) { return s.id == id; });
    if (found == stamps_.end()) return std::nullopt;
    return *found;
}

QStringList ProfileStore::categories() const
{
    QStringList own;
    bool signatures = false;
    for (const CustomStamp& stamp : stamps_) {
        if (stamp.category == QLatin1String(kSignaturesCategory)) signatures = true;
        else if (!own.contains(stamp.category)) own << stamp.category;
    }
    own.sort(Qt::CaseInsensitive);
    if (signatures) own.prepend(QLatin1String(kSignaturesCategory));
    return own;
}

Status ProfileStore::writeDescription(const CustomStamp& stamp) const
{
    QJsonObject o;
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("name"), stamp.name);
    o.insert(QStringLiteral("category"), stamp.category);
    o.insert(QStringLiteral("favourite"), stamp.favourite);
    o.insert(QStringLiteral("format"), stamp.format);
    o.insert(QStringLiteral("created"), static_cast<double>(stamp.created));
    return writeWhole(descriptionPath(stamp.id), QJsonDocument(o).toJson(QJsonDocument::Indented));
}

Status ProfileStore::writeFavourites() const
{
    QJsonArray names;
    for (const QString& name : builtInFavourites_) names.append(name);
    QJsonObject o;
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("builtIn"), names);
    return writeWhole(QDir(root_).filePath(kFavouritesFile), QJsonDocument(o).toJson(QJsonDocument::Indented));
}

Result<CustomStamp> ProfileStore::add(const QString& name, const QString& category, const QByteArray& image,
                                      std::int64_t utcNow, const QString& sourceName)
{
    const QString format = imageFormatOf(image);
    if (format.isEmpty() || image.size() > kMaxImageBytes) return storeError("error.stamps.notAnImage", sourceName);
    if (!validName(name) || !validCategory(category.trimmed())) return storeError("error.stamps.badName", name);
    if (static_cast<int>(stamps_.size()) >= kMaxStamps) return storeError("error.stamps.full");
    if (!QDir().mkpath(root_)) return Error{ErrorCode::IoFailure, QLatin1String("error.stamps.writeFailed"), root_};

    CustomStamp stamp;
    stamp.id = newAnnotationName();
    stamp.name = name.trimmed();
    stamp.category = category.trimmed();
    stamp.format = format;
    stamp.created = utcNow;
    // The image first: a description is never on disk without the image it names.
    if (Status written = writeWhole(imagePath(stamp), image); !written) return written.error();
    if (Status written = writeDescription(stamp); !written) {
        QFile::remove(imagePath(stamp));
        return written.error();
    }
    stamps_.push_back(stamp);
    return stamp;
}

Status ProfileStore::rename(const QString& id, const QString& name)
{
    const auto found = std::find_if(stamps_.begin(), stamps_.end(), [&id](const CustomStamp& s) { return s.id == id; });
    if (found == stamps_.end()) return storeError("error.stamps.unknown", id);
    if (!validName(name)) return storeError("error.stamps.badName", name);
    CustomStamp changed = *found;
    changed.name = name.trimmed();
    if (Status written = writeDescription(changed); !written) return written;
    *found = std::move(changed);
    return Status{};
}

Status ProfileStore::setCategory(const QString& id, const QString& category)
{
    const auto found = std::find_if(stamps_.begin(), stamps_.end(), [&id](const CustomStamp& s) { return s.id == id; });
    if (found == stamps_.end()) return storeError("error.stamps.unknown", id);
    if (!validCategory(category.trimmed())) return storeError("error.stamps.badName", category);
    CustomStamp changed = *found;
    changed.category = category.trimmed();
    if (Status written = writeDescription(changed); !written) return written;
    *found = std::move(changed);
    return Status{};
}

Status ProfileStore::setFavourite(const QString& stamp, bool favourite)
{
    const auto found = std::find_if(stamps_.begin(), stamps_.end(), [&stamp](const CustomStamp& s) { return s.id == stamp; });
    if (found != stamps_.end()) {
        CustomStamp changed = *found;
        changed.favourite = favourite;
        if (Status written = writeDescription(changed); !written) return written;
        *found = std::move(changed);
        return Status{};
    }
    if (!validName(stamp) || isId(stamp)) return storeError("error.stamps.unknown", stamp);
    const std::set<QString> before = builtInFavourites_;
    if (favourite) builtInFavourites_.insert(stamp);
    else builtInFavourites_.erase(stamp);
    if (!QDir().mkpath(root_)) {
        builtInFavourites_ = before;
        return Error{ErrorCode::IoFailure, QLatin1String("error.stamps.writeFailed"), root_};
    }
    if (Status written = writeFavourites(); !written) {
        builtInFavourites_ = before;
        return written;
    }
    return Status{};
}

bool ProfileStore::isFavourite(const QString& stamp) const
{
    if (const std::optional<CustomStamp> custom = find(stamp)) return custom->favourite;
    return builtInFavourites_.count(stamp) != 0;
}

Status ProfileStore::remove(const QString& id)
{
    const auto found = std::find_if(stamps_.begin(), stamps_.end(), [&id](const CustomStamp& s) { return s.id == id; });
    if (found == stamps_.end()) return storeError("error.stamps.unknown", id);
    // The description first: an image with no description is not a stamp, and `load` never lists it.
    if (!QFile::remove(descriptionPath(id))) return Error{ErrorCode::IoFailure, QLatin1String("error.stamps.writeFailed"), id};
    QFile::remove(imagePath(*found));
    stamps_.erase(found);
    return Status{};
}

Result<QByteArray> ProfileStore::imageBytes(const QString& id) const
{
    const std::optional<CustomStamp> stamp = find(id);
    if (!stamp) return storeError("error.stamps.unknown", id);
    std::optional<QByteArray> bytes = readCapped(imagePath(*stamp), kMaxImageBytes);
    if (!bytes || imageFormatOf(*bytes) != stamp->format)
        return Error{ErrorCode::IoFailure, QLatin1String("error.stamps.damaged"), stamp->id + QStringLiteral(".") + stamp->format};
    return std::move(*bytes);
}

} // namespace rawpage::core
