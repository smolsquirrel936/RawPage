// core/settings/ProfileStore -- implementation. Stage 6 T10. See the header for the layout on disk and why a
// stamp is two files of its own.

#include "core/settings/ProfileStore.h"

#include "core/doc/Annotation.h"
#include "core/doc/CopiedPages.h"
#include "core/doc/Stamps.h"

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

QString ProfileStore::previewPath(const QString& id) const { return QDir(root_).filePath(id + QStringLiteral(".preview.png")); }

void ProfileStore::load()
{
    stamps_.clear();
    builtInFavourites_.clear();
    damaged_.clear();
    headerFooterProfiles_.clear();
    watermarkProfiles_.clear();
    impositionProfiles_.clear();
    const QDir dir(root_);
    if (!dir.exists()) return;
    loadProfiles(ProfileKind::HeaderFooter);   // S7 T14
    loadProfiles(ProfileKind::Watermark);      // S7 T15
    loadProfiles(ProfileKind::Imposition);     // S7 T16

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
        // Stage 7 T13: a page's size checked as a command checks it -- the graph itself is read when it is placed.
        const bool page = stamp.format == QLatin1String("pdf");
        if (page) {
            stamp.pageWidth = o.value(QStringLiteral("pageWidth")).toDouble();
            stamp.pageHeight = o.value(QStringLiteral("pageHeight")).toDouble();
        }
        const bool sized = !page || !invalidStampPage(StampPage{QByteArray("%PDF-"), stamp.pageWidth, stamp.pageHeight});
        const bool formatKnown = stamp.format == QLatin1String("png") || stamp.format == QLatin1String("jpg") || page;
        if (o.value(QStringLiteral("version")).toInt() != 1 || !validName(stamp.name) || !validCategory(stamp.category)
            || !formatKnown || !sized || !QFileInfo(imagePath(stamp)).isFile()) {
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
    if (stamp.format == QLatin1String("pdf")) {
        o.insert(QStringLiteral("pageWidth"), stamp.pageWidth);
        o.insert(QStringLiteral("pageHeight"), stamp.pageHeight);
    }
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
    return keep(std::move(stamp), image);
}

Result<CustomStamp> ProfileStore::keep(CustomStamp stamp, const QByteArray& file)
{
    // The file first: a description is never on disk without the file it names.
    if (Status written = writeWhole(imagePath(stamp), file); !written) return written.error();
    if (Status written = writeDescription(stamp); !written) {
        QFile::remove(imagePath(stamp));
        return written.error();
    }
    stamps_.push_back(stamp);
    return stamp;
}

Result<CustomStamp> ProfileStore::addPage(const QString& name, const QString& category, const StampPage& page,
                                          std::int64_t utcNow, const QString& sourceName)
{
    if (invalidStampPage(page)) return storeError("error.stamps.notAPage", sourceName);
    if (!validName(name) || !validCategory(category.trimmed())) return storeError("error.stamps.badName", name);
    if (static_cast<int>(stamps_.size()) >= kMaxStamps) return storeError("error.stamps.full");
    if (!QDir().mkpath(root_)) return Error{ErrorCode::IoFailure, QLatin1String("error.stamps.writeFailed"), root_};
    CustomStamp stamp;
    stamp.id = newAnnotationName();
    stamp.name = name.trimmed();
    stamp.category = category.trimmed();
    stamp.format = QStringLiteral("pdf");
    stamp.created = utcNow;
    stamp.pageWidth = page.width;
    stamp.pageHeight = page.height;
    return keep(std::move(stamp), page.graph);
}

Result<StampPage> ProfileStore::pageOf(const QString& id) const
{
    const std::optional<CustomStamp> stamp = find(id);
    if (!stamp || stamp->format != QLatin1String("pdf")) return storeError("error.stamps.unknown", id);
    std::optional<QByteArray> bytes = readCapped(imagePath(*stamp), kMaxCopiedGraphBytes);
    StampPage page{bytes.value_or(QByteArray()), stamp->pageWidth, stamp->pageHeight};
    if (!bytes || invalidStampPage(page))
        return Error{ErrorCode::IoFailure, QLatin1String("error.stamps.damaged"), stamp->id + QStringLiteral(".pdf")};
    return page;
}

Status ProfileStore::setPreview(const QString& id, const QByteArray& png)
{
    if (!find(id)) return storeError("error.stamps.unknown", id);
    if (imageFormatOf(png) != QLatin1String("png") || png.size() > kMaxImageBytes) return storeError("error.stamps.notAnImage", id);
    return writeWhole(previewPath(id), png);
}

std::optional<QByteArray> ProfileStore::previewBytes(const QString& id) const
{
    if (!find(id)) return std::nullopt;
    std::optional<QByteArray> bytes = readCapped(previewPath(id), kMaxImageBytes);
    if (!bytes || imageFormatOf(*bytes) != QLatin1String("png")) return std::nullopt;
    return bytes;
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
    QFile::remove(previewPath(id));
    stamps_.erase(found);
    return Status{};
}

Result<QByteArray> ProfileStore::imageBytes(const QString& id) const
{
    const std::optional<CustomStamp> stamp = find(id);
    if (!stamp) return storeError("error.stamps.unknown", id);
    if (stamp->format == QLatin1String("pdf"))
        return Error{ErrorCode::IoFailure, QLatin1String("error.stamps.damaged"), stamp->id + QStringLiteral(".pdf")};
    std::optional<QByteArray> bytes = readCapped(imagePath(*stamp), kMaxImageBytes);
    if (!bytes || imageFormatOf(*bytes) != stamp->format)
        return Error{ErrorCode::IoFailure, QLatin1String("error.stamps.damaged"), stamp->id + QStringLiteral(".") + stamp->format};
    return std::move(*bytes);
}

// ---- Named profiles (Stage 7 T14) -- see the header ----------------------------------------------------------------

QString ProfileStore::profileDirectory(ProfileKind kind) const
{
    switch (kind) {
    case ProfileKind::Watermark: return QDir(root_).filePath(QStringLiteral("profiles/watermark"));
    case ProfileKind::Imposition: return QDir(root_).filePath(QStringLiteral("profiles/imposition"));
    case ProfileKind::HeaderFooter: break;
    }
    return QDir(root_).filePath(QStringLiteral("profiles/headerFooter"));
}

std::vector<ProfileStore::Profile>& ProfileStore::profilesOf(ProfileKind kind)
{
    if (kind == ProfileKind::Imposition) return impositionProfiles_;   // S7 T16
    return kind == ProfileKind::Watermark ? watermarkProfiles_ : headerFooterProfiles_;
}
const std::vector<ProfileStore::Profile>& ProfileStore::profilesOf(ProfileKind kind) const
{
    if (kind == ProfileKind::Imposition) return impositionProfiles_;
    return kind == ProfileKind::Watermark ? watermarkProfiles_ : headerFooterProfiles_;
}

qint64 ProfileStore::profileCap(ProfileKind kind)
{
    // S7 T15: a watermark profile carries its picture (an image's bytes, a page's graph) as base64 in its spec.
    return kind == ProfileKind::Watermark ? kMaxWatermarkProfileBytes : kMaxDescriptionBytes;
}

void ProfileStore::loadProfiles(ProfileKind kind)
{
    std::vector<Profile>& into = profilesOf(kind);
    into.clear();
    const QDir dir(profileDirectory(kind));
    if (!dir.exists()) return;
    for (const QString& file : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        const QString id = QFileInfo(file).completeBaseName();
        if (!isId(id)) continue;   // not ours
        const QString shown = QStringLiteral("profiles/") + dir.dirName() + QLatin1Char('/') + file;
        if (static_cast<int>(into.size()) >= kMaxProfiles) {
            damaged_ << shown;
            continue;
        }
        const std::optional<QByteArray> bytes = readCapped(dir.filePath(file), profileCap(kind));
        QJsonParseError parse{};
        const QJsonDocument json = bytes ? QJsonDocument::fromJson(*bytes, &parse) : QJsonDocument();
        const QJsonObject o = json.object();
        const QString name = o.value(QStringLiteral("name")).toString();
        if (!bytes || parse.error != QJsonParseError::NoError || !json.isObject() || !validName(name)
            || !o.value(QStringLiteral("spec")).isObject()
            || std::any_of(into.begin(), into.end(), [&name](const Profile& p) { return p.name == name.trimmed(); })) {
            damaged_ << shown;
            continue;
        }
        into.push_back(Profile{id, name.trimmed(), o.value(QStringLiteral("spec")).toObject()});
    }
}

QStringList ProfileStore::profileNames(ProfileKind kind) const
{
    QStringList names;
    for (const Profile& p : profilesOf(kind)) names << p.name;
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) {
        const int folded = a.compare(b, Qt::CaseInsensitive);
        return folded != 0 ? folded < 0 : a < b;
    });
    return names;
}

Result<QJsonObject> ProfileStore::profile(ProfileKind kind, const QString& name) const
{
    for (const Profile& p : profilesOf(kind)) {
        if (p.name == name.trimmed()) return p.spec;
    }
    return storeError("error.profiles.unknown", name);
}

Status ProfileStore::saveProfile(ProfileKind kind, const QString& name, const QJsonObject& spec)
{
    if (!validName(name)) return storeError("error.stamps.badName", name);
    std::vector<Profile>& profiles = profilesOf(kind);
    auto found = std::find_if(profiles.begin(), profiles.end(), [&name](const Profile& p) { return p.name == name.trimmed(); });
    if (found == profiles.end() && static_cast<int>(profiles.size()) >= kMaxProfiles) return storeError("error.profiles.full");
    const QString directory = profileDirectory(kind);
    if (!QDir().mkpath(directory)) return Error{ErrorCode::IoFailure, QLatin1String("error.stamps.writeFailed"), directory};
    Profile profile{found != profiles.end() ? found->id : newAnnotationName(), name.trimmed(), spec};
    QJsonObject o;
    o.insert(QStringLiteral("version"), 1);
    o.insert(QStringLiteral("name"), profile.name);
    o.insert(QStringLiteral("spec"), profile.spec);
    if (Status written = writeWhole(QDir(directory).filePath(profile.id + QStringLiteral(".json")),
                                    QJsonDocument(o).toJson(QJsonDocument::Indented));
        !written)
        return written;
    if (found != profiles.end()) *found = std::move(profile);
    else profiles.push_back(std::move(profile));
    return Status{};
}

Status ProfileStore::removeProfile(ProfileKind kind, const QString& name)
{
    std::vector<Profile>& profiles = profilesOf(kind);
    const auto found = std::find_if(profiles.begin(), profiles.end(), [&name](const Profile& p) { return p.name == name.trimmed(); });
    if (found == profiles.end()) return storeError("error.profiles.unknown", name);
    if (!QFile::remove(QDir(profileDirectory(kind)).filePath(found->id + QStringLiteral(".json"))))
        return Error{ErrorCode::IoFailure, QLatin1String("error.stamps.writeFailed"), name};
    profiles.erase(found);
    return Status{};
}

} // namespace rawpage::core
