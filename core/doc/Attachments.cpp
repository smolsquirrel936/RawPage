// core/doc/Attachments -- implementation. Stage 6 T11. See the header.

#include "core/doc/Attachments.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QStringList>

#include <algorithm>
#include <array>
#include <utility>

namespace rawpage::core {
namespace {

Error malformedAttachment(const QString& what)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"), what};
}

bool keyLess(const QString& a, const QString& b)
{
    return nameTreeKeyBytes(a) < nameTreeKeyBytes(b);
}

// The extension after the last dot, lower case, of `name` as Windows would read it: trailing dots and spaces
// are not part of a Windows file name, so `setup.exe.` and `setup.exe ` run as `setup.exe`.
QString lastExtension(const QString& name)
{
    QString trimmed = name;
    while (!trimmed.isEmpty() && (trimmed.endsWith(QLatin1Char('.')) || trimmed.endsWith(QLatin1Char(' '))))
        trimmed.chop(1);
    const qsizetype slash = std::max(trimmed.lastIndexOf(QLatin1Char('/')), trimmed.lastIndexOf(QLatin1Char('\\')));
    trimmed = trimmed.mid(slash + 1);
    const qsizetype dot = trimmed.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? QString() : trimmed.mid(dot + 1).toLower();
}

} // namespace

QJsonObject DocumentAttachment::toJson() const
{
    return QJsonObject{{QStringLiteral("key"), key}, {QStringLiteral("file"), file.toJson()}};
}

Result<DocumentAttachment> DocumentAttachment::fromJson(const QJsonObject& json)
{
    const QJsonValue key = json.value(QStringLiteral("key"));
    const QJsonValue file = json.value(QStringLiteral("file"));
    if (!key.isString() || key.toString().isEmpty() || !file.isObject()) return malformedAttachment(QStringLiteral("attachment"));
    Result<EmbeddedFile> parsed = EmbeddedFile::fromJson(file.toObject());
    if (!parsed) return parsed.error();
    return DocumentAttachment{key.toString(), std::move(parsed).value()};
}

AttachmentRef AttachmentRef::ofDocument(const QString& key)
{
    AttachmentRef ref;
    ref.kind = Kind::Document;
    ref.key = key;
    return ref;
}

AttachmentRef AttachmentRef::ofAnnotation(const AnnotationId& id)
{
    AttachmentRef ref;
    ref.kind = Kind::Annotation;
    ref.annotation = id;
    return ref;
}

Attachments::Attachments()
    : status_(Error{ErrorCode::Unavailable, QLatin1String("error.attachment.notRead"), QString()})
{
}

Attachments Attachments::fromReading(std::vector<DocumentAttachment> files)
{
    Attachments out;
    out.status_ = Status{};
    out.files_ = std::move(files);
    return out;
}

Attachments Attachments::refused(Error why)
{
    Attachments out;
    out.status_ = std::move(why);
    return out;
}

std::optional<std::size_t> Attachments::find(const QString& key) const
{
    for (std::size_t at = 0; at < files_.size(); ++at) {
        if (files_[at].key == key) return at;
    }
    return std::nullopt;
}

Status Attachments::insert(DocumentAttachment file)
{
    if (!status_) return status_;
    if (file.key.isEmpty()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.attachment.noName"), QString()};
    if (find(file.key)) return Error{ErrorCode::InvalidArgument, QLatin1String("error.attachment.duplicateKey"), file.key};
    // Where its key sorts: after every key that sorts before it, so a file read out of order is left where it is.
    const auto at = std::find_if(files_.begin(), files_.end(),
                                 [&file](const DocumentAttachment& held) { return keyLess(file.key, held.key); });
    files_.insert(at, std::move(file));
    return Status{};
}

Result<DocumentAttachment> Attachments::remove(const QString& key)
{
    if (!status_) return status_.error();
    const std::optional<std::size_t> at = find(key);
    if (!at) return Error{ErrorCode::NotFound, QLatin1String("error.attachment.unknown"), key};
    DocumentAttachment removed = files_[*at];
    files_.erase(files_.begin() + static_cast<std::ptrdiff_t>(*at));
    return removed;
}

QString Attachments::uniqueKey(const QString& name) const
{
    if (!find(name)) return name;
    const qsizetype dot = name.lastIndexOf(QLatin1Char('.'));
    const QString stem = dot > 0 ? name.left(dot) : name;
    const QString extension = dot > 0 ? name.mid(dot) : QString();
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(stem, QString::number(n), extension);
        if (!find(candidate)) return candidate;
    }
}

QByteArray Attachments::digest() const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView(status_ ? "read" : "refused"));
    for (const DocumentAttachment& file : files_) {
        const QByteArray line = QJsonDocument(file.toJson()).toJson(QJsonDocument::Compact);
        const QByteArray length = QByteArray::number(line.size());
        hash.addData(length);
        hash.addData(QByteArrayView(":"));
        hash.addData(line);
    }
    return hash.result();
}

QByteArray nameTreeKeyBytes(const QString& key)
{
    const bool ascii = std::all_of(key.begin(), key.end(), [](QChar c) { return c.unicode() >= 0x20 && c.unicode() < 0x7F; });
    if (ascii) return key.toLatin1();
    QByteArray out("\xFE\xFF", 2);
    for (const QChar c : key) {
        out.append(static_cast<char>(c.unicode() >> 8));
        out.append(static_cast<char>(c.unicode() & 0xFF));
    }
    return out;
}

EmbeddedFile newEmbeddedFile(const QString& name, QByteArray bytes, const QString& description, std::int64_t utcNow)
{
    EmbeddedFile file;
    const qsizetype slash = std::max(name.lastIndexOf(QLatin1Char('/')), name.lastIndexOf(QLatin1Char('\\')));
    file.name = name.mid(slash + 1);
    file.description = description;
    file.mime = mimeTypeForName(file.name);
    file.size = bytes.size();
    file.checksum = QCryptographicHash::hash(bytes, QCryptographicHash::Md5);
    file.created = PdfDate::fromUtcSeconds(utcNow);
    file.modified = file.created;
    file.data = std::move(bytes);
    file.carried = true;
    return file;
}

Annotation newFileAttachment(int page, const PdfRect& rect, EmbeddedFile file, const std::vector<double>& colour,
                             const QString& author, std::int64_t utcNow)
{
    Annotation a = newAnnotation(AnnotationSubtype::FileAttachment, page, author, utcNow);
    a.rect = rect;
    a.icon = QStringLiteral("PushPin");
    a.color = colour;
    a.contents = file.name;
    a.attachment = std::move(file);
    return a;
}

QString mimeTypeForName(const QString& name)
{
    // The types a reviewer attaches most, and the PDF one the open decides by. A fixed table, so the type a file
    // is written with does not depend on what the machine that attached it had registered.
    static const std::array<std::pair<const char*, const char*>, 24> kTypes{{
        {"pdf", "application/pdf"},   {"txt", "text/plain"},         {"csv", "text/csv"},
        {"htm", "text/html"},         {"html", "text/html"},         {"xml", "application/xml"},
        {"json", "application/json"}, {"png", "image/png"},          {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},       {"gif", "image/gif"},          {"tif", "image/tiff"},
        {"tiff", "image/tiff"},       {"bmp", "image/bmp"},          {"zip", "application/zip"},
        {"rtf", "application/rtf"},   {"doc", "application/msword"}, {"xls", "application/vnd.ms-excel"},
        {"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {"xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {"pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
        {"wav", "audio/wav"},         {"mp3", "audio/mpeg"},         {"mp4", "video/mp4"},
    }};
    const QString extension = lastExtension(name);
    for (const auto& [suffix, type] : kTypes) {
        if (extension == QLatin1String(suffix)) return QLatin1String(type);
    }
    return QStringLiteral("application/octet-stream");
}

bool isExecutableName(const QString& name)
{
    // What Windows runs, or hands to an interpreter that runs, when such a file is opened: programs and their
    // installers, scripts and their hosts, shortcuts and shell objects, control-panel and help files that execute,
    // registry files that change the machine. Stage 10's TrustPolicy owns the list after this stage.
    static const std::array<const char*, 52> kExecutable{{
        "exe",  "com",  "scr",  "pif",  "bat",  "cmd",  "msi",  "msp",  "mst",  "msc",  "cpl",  "dll",  "ocx",
        "sys",  "drv",  "vbs",  "vbe",  "js",   "jse",  "wsf",  "wsh",  "wsc",  "ws",   "sct",  "hta",  "ps1",
        "psm1", "psd1", "ps1xml", "psc1", "jar", "lnk", "url",  "scf",  "reg",  "inf",  "ins",  "isp",  "chm",
        "hlp",  "application", "appref-ms", "gadget", "appx", "appxbundle", "msix", "msixbundle", "shb", "shs",
        "vb",   "settingcontent-ms", "library-ms",
    }};
    const QString extension = lastExtension(name);
    if (extension.isEmpty()) return false;
    return std::any_of(kExecutable.begin(), kExecutable.end(),
                       [&extension](const char* suffix) { return extension == QLatin1String(suffix); });
}

bool isPdfAttachment(const EmbeddedFile& file)
{
    return file.mime.compare(QLatin1String("application/pdf"), Qt::CaseInsensitive) == 0
           || lastExtension(file.name) == QLatin1String("pdf");
}

QString attachmentSessionFolder()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("RawPage/attachments/%1").arg(QCoreApplication::applicationPid()));
}

QString safeFileName(const QString& name)
{
    const qsizetype slash = std::max(name.lastIndexOf(QLatin1Char('/')), name.lastIndexOf(QLatin1Char('\\')));
    QString out = name.mid(slash + 1);
    for (QChar& c : out) {
        static const QString kRefused = QStringLiteral("<>:\"/\\|?*");
        if (c.unicode() < 0x20 || c.unicode() == 0x7F || kRefused.contains(c)) c = QLatin1Char('_');
    }
    while (!out.isEmpty() && (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' ')))) out.chop(1);
    while (!out.isEmpty() && out.startsWith(QLatin1Char(' '))) out.remove(0, 1);
    if (out.isEmpty()) return QStringLiteral("attachment");

    // A device name is a device whatever its extension: `CON.txt` opens the console.
    const qsizetype dot = out.indexOf(QLatin1Char('.'));
    const QString stem = (dot < 0 ? out : out.left(dot)).trimmed().toUpper();
    static const QStringList kDevices{QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"),
                                      QStringLiteral("NUL"), QStringLiteral("CONIN$"), QStringLiteral("CONOUT$")};
    const bool numbered = (stem.size() == 4 && (stem.startsWith(QLatin1String("COM")) || stem.startsWith(QLatin1String("LPT")))
                           && stem[3] >= QLatin1Char('0') && stem[3] <= QLatin1Char('9'));
    if (kDevices.contains(stem) || numbered) out.prepend(QLatin1Char('_'));

    constexpr qsizetype kMaxLength = 200;
    if (out.size() > kMaxLength) {
        const qsizetype last = out.lastIndexOf(QLatin1Char('.'));
        const QString extension = (last > 0 && out.size() - last <= 16) ? out.mid(last) : QString();
        out = out.left(kMaxLength - extension.size()) + extension;
    }
    return out;
}

} // namespace rawpage::core
