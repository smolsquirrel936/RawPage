// core/doc/Annotation -- implementation. Stage 6 T2. See the header for what is modelled and why.

#include "core/doc/Annotation.h"

#include <QDate>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QStringList>

#include <array>
#include <cmath>
#include <utility>

namespace rawpage::core {
namespace {

// Indexed by `AnnotationSubtype`. `XfaWidget` has no PDF name: PDFium invents the constant for its
// XFA forms, and no file writes `/Subtype /XFAWidget`.
constexpr std::array<const char*, 29> kSubtypeNames = {
    "",          "Text",       "Link",       "FreeText",  "Line",           "Square",
    "Circle",    "Polygon",    "PolyLine",   "Highlight", "Underline",      "Squiggly",
    "StrikeOut", "Stamp",      "Caret",      "Ink",       "Popup",          "FileAttachment",
    "Sound",     "Movie",      "Widget",     "Screen",    "PrinterMark",    "TrapNet",
    "Watermark", "3D",         "RichMedia",  "",          "Redact",
};

Error malformed(const QString& what)
{
    return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"), what};
}

QJsonArray numbersToJson(const std::vector<double>& values)
{
    QJsonArray out;
    for (const double value : values) out.append(value);
    return out;
}

// Stage 6 T10: an image stamp's pixels, the bytes as base64. Read leniently, like every field here -- the
// command's parser is where a hostile size or a mismatched header is refused (`core/doc/Stamps`).
QJsonObject imageToJson(const StampImage& image)
{
    QJsonObject json;
    json.insert(QStringLiteral("encoding"),
                image.encoding == StampImage::Encoding::Jpeg ? QStringLiteral("jpeg") : QStringLiteral("flate"));
    json.insert(QStringLiteral("width"), image.width);
    json.insert(QStringLiteral("height"), image.height);
    json.insert(QStringLiteral("components"), image.components);
    json.insert(QStringLiteral("data"), QString::fromLatin1(image.data.toBase64()));
    if (!image.alpha.isEmpty()) json.insert(QStringLiteral("alpha"), QString::fromLatin1(image.alpha.toBase64()));
    return json;
}

std::optional<StampImage> imageFromJson(const QJsonValue& value)
{
    if (!value.isObject()) return std::nullopt;
    const QJsonObject json = value.toObject();
    StampImage image;
    const QString encoding = json.value(QStringLiteral("encoding")).toString();
    if (encoding == QLatin1String("jpeg")) image.encoding = StampImage::Encoding::Jpeg;
    else if (encoding == QLatin1String("flate")) image.encoding = StampImage::Encoding::Flate;
    else return std::nullopt;
    image.width = json.value(QStringLiteral("width")).toInt();
    image.height = json.value(QStringLiteral("height")).toInt();
    image.components = json.value(QStringLiteral("components")).toInt();
    const auto bytes = [&json](const char* key, QByteArray& out) {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        if (!v.isString()) return false;
        auto decoded = QByteArray::fromBase64Encoding(v.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) return false;
        out = std::move(*decoded);
        return true;
    };
    if (!bytes("data", image.data) || !bytes("alpha", image.alpha) || image.data.isEmpty()) return std::nullopt;
    return image;
}

} // namespace

// ---- Stage 6 T11: an embedded file ------------------------------------------------------------------

QJsonObject EmbeddedFile::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("name"), name);
    if (!description.isEmpty()) json.insert(QStringLiteral("description"), description);
    if (!mime.isEmpty()) json.insert(QStringLiteral("mime"), mime);
    if (size >= 0) json.insert(QStringLiteral("size"), static_cast<qint64>(size));
    if (!created.raw.isEmpty()) json.insert(QStringLiteral("created"), created.raw);
    if (!modified.raw.isEmpty()) json.insert(QStringLiteral("modified"), modified.raw);
    if (!checksum.isEmpty()) json.insert(QStringLiteral("checksum"), QString::fromLatin1(checksum.toHex()));
    if (carried) json.insert(QStringLiteral("data"), QString::fromLatin1(data.toBase64()));
    return json;
}

Result<EmbeddedFile> EmbeddedFile::fromJson(const QJsonObject& json)
{
    EmbeddedFile file;
    const auto text = [&json](const char* key, QString& out) {
        const QJsonValue value = json.value(QLatin1String(key));
        if (value.isUndefined()) return true;
        if (!value.isString()) return false;
        out = value.toString();
        return true;
    };
    QString created, modified, checksum;
    if (!text("name", file.name) || file.name.isEmpty() || !text("description", file.description)
        || !text("mime", file.mime) || !text("created", created) || !text("modified", modified) || !text("checksum", checksum))
        return malformed(QStringLiteral("attachment fields"));
    if (!created.isEmpty()) file.created = PdfDate::parse(created);
    if (!modified.isEmpty()) file.modified = PdfDate::parse(modified);
    if (!checksum.isEmpty()) {
        const QByteArray hex = checksum.toLatin1();
        file.checksum = QByteArray::fromHex(hex);
        if (hex.size() != 32 || file.checksum.size() != 16 || file.checksum.toHex() != hex.toLower())
            return malformed(QStringLiteral("attachment checksum"));
    }
    if (const QJsonValue size = json.value(QStringLiteral("size")); !size.isUndefined()) {
        // A whole number of bytes a double holds exactly -- a hostile line's 1e300 is not a size.
        const double value = size.toDouble(-1.0);
        if (!size.isDouble() || value < 0.0 || value > 9.0e15 || value != std::floor(value))
            return malformed(QStringLiteral("attachment size"));
        file.size = static_cast<long long>(value);
    }
    if (const QJsonValue data = json.value(QStringLiteral("data")); !data.isUndefined()) {
        if (!data.isString()) return malformed(QStringLiteral("attachment data"));
        auto decoded = QByteArray::fromBase64Encoding(data.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) return malformed(QStringLiteral("attachment data"));
        file.data = std::move(*decoded);
        file.carried = true;
        // A file RawPage attached says its own length, and a line that disagrees is not one RawPage wrote.
        if (file.size != static_cast<long long>(file.data.size())) return malformed(QStringLiteral("attachment size"));
    }
    return file;
}

namespace {

std::vector<double> numbersFromJson(const QJsonValue& value)
{
    std::vector<double> out;
    for (const QJsonValue item : value.toArray()) out.push_back(item.toDouble());
    return out;
}

QString subtypeKey(const Annotation& annotation)
{
    return annotation.subtypeName.isEmpty()
               ? QString(annotationSubtypeName(annotation.subtype))
               : annotation.subtypeName;
}

// Two digits at `at`, or -1. Every field of a PDF date after the year is optional, and a reader that
// required them would refuse the `D:2026` real producers write.
int digits(const QString& text, int at, int count)
{
    if (at + count > text.size()) return -1;
    int value = 0;
    for (int i = 0; i < count; ++i) {
        const QChar c = text.at(at + i);
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) return -1;
        value = value * 10 + (c.unicode() - '0');
    }
    return value;
}

} // namespace

QLatin1String annotationSubtypeName(AnnotationSubtype subtype)
{
    const int index = static_cast<int>(subtype);
    if (index < 0 || index >= static_cast<int>(kSubtypeNames.size())) return QLatin1String("");
    return QLatin1String(kSubtypeNames[static_cast<std::size_t>(index)]);
}

AnnotationSubtype annotationSubtypeFromName(std::string_view name)
{
    if (name.empty()) return AnnotationSubtype::Unknown;
    for (std::size_t index = 1; index < kSubtypeNames.size(); ++index) {
        if (name == kSubtypeNames[index]) return static_cast<AnnotationSubtype>(index);
    }
    return AnnotationSubtype::Unknown;
}

// --- PdfDate ----------------------------------------------------------------------------------

PdfDate PdfDate::parse(const QString& raw)
{
    PdfDate date;
    date.raw = raw;
    QString text = raw.trimmed();
    if (text.startsWith(QLatin1String("D:"))) text = text.mid(2);

    const int year = digits(text, 0, 4);
    if (year < 0) return date;
    // Each later field defaults to its smallest value when absent (ISO 32000-1 §7.9.4).
    int month = digits(text, 4, 2);
    int day = digits(text, 6, 2);
    int hour = digits(text, 8, 2);
    int minute = digits(text, 10, 2);
    int second = digits(text, 12, 2);
    int at = 4;
    if (month < 0) { month = 1; day = 1; hour = 0; minute = 0; second = 0; }
    else {
        at = 6;
        if (day < 0) { day = 1; hour = 0; minute = 0; second = 0; }
        else {
            at = 8;
            if (hour < 0) { hour = 0; minute = 0; second = 0; }
            else {
                at = 10;
                if (minute < 0) { minute = 0; second = 0; }
                else {
                    at = 12;
                    if (second < 0) second = 0;
                    else at = 14;
                }
            }
        }
    }
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59)
        return date;
    const QDate calendar(year, month, day);
    if (!calendar.isValid()) return date;

    // The zone: Z, +HH'mm', -HH'mm', or nothing (which ISO 32000-1 leaves UNKNOWN; read as UTC, the
    // reading every reader we compare with makes).
    long long offsetSeconds = 0;
    if (at < text.size()) {
        const QChar sign = text.at(at);
        if (sign == QLatin1Char('+') || sign == QLatin1Char('-')) {
            const int hours = digits(text, at + 1, 2);
            int minutes = 0;
            int next = at + 3;
            if (next < text.size() && text.at(next) == QLatin1Char('\'')) ++next;
            if (const int m = digits(text, next, 2); m >= 0) minutes = m;
            if (hours < 0 || hours > 23 || minutes > 59) return date;
            offsetSeconds = (hours * 3600LL + minutes * 60LL) * (sign == QLatin1Char('-') ? -1 : 1);
        }
    }

    // Days since 1970-01-01 from the Julian day number, so no time-zone database is consulted --
    // the distro Qt 6.4 that builds `core/` on Linux may have none (convention 24).
    const long long days = calendar.toJulianDay() - 2440588LL;
    date.utcSeconds = days * 86400LL + hour * 3600LL + minute * 60LL + second - offsetSeconds;
    return date;
}

PdfDate PdfDate::fromUtcSeconds(std::int64_t utcSeconds)
{
    long long days = utcSeconds / 86400;
    long long rest = utcSeconds % 86400;
    if (rest < 0) { rest += 86400; --days; }
    const QDate calendar = QDate::fromJulianDay(days + 2440588LL);
    PdfDate date;
    date.raw = QStringLiteral("D:%1%2%3%4Z")
                   .arg(calendar.toString(QStringLiteral("yyyyMMdd")))
                   .arg(rest / 3600, 2, 10, QLatin1Char('0'))
                   .arg((rest / 60) % 60, 2, 10, QLatin1Char('0'))
                   .arg(rest % 60, 2, 10, QLatin1Char('0'));
    date.utcSeconds = utcSeconds;
    return date;
}

QString PdfDate::utcIso() const
{
    if (!utcSeconds) return QString();
    long long seconds = *utcSeconds;
    long long days = seconds / 86400;
    long long rest = seconds % 86400;
    if (rest < 0) { rest += 86400; --days; }
    const QDate calendar = QDate::fromJulianDay(days + 2440588LL);
    return QStringLiteral("%1T%2:%3:%4Z")
        .arg(calendar.toString(QStringLiteral("yyyy-MM-dd")))
        .arg(rest / 3600, 2, 10, QLatin1Char('0'))
        .arg((rest / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(rest % 60, 2, 10, QLatin1Char('0'));
}

// --- AnnotationId -----------------------------------------------------------------------------

AnnotationId AnnotationId::ofObject(long long number)
{
    AnnotationId id;
    id.kind = Kind::Object;
    id.object = number;
    return id;
}

AnnotationId AnnotationId::ofDirect(int page, int position)
{
    AnnotationId id;
    id.kind = Kind::Direct;
    id.page = page;
    id.position = position;
    return id;
}

AnnotationId AnnotationId::ofCreated(const QString& nm)
{
    AnnotationId id;
    id.kind = Kind::Created;
    id.nm = nm;
    return id;
}

bool AnnotationId::isValid() const noexcept
{
    switch (kind) {
    case Kind::Object: return object > 0;
    case Kind::Direct: return page >= 0 && position >= 0;
    case Kind::Created: return !nm.isEmpty();
    }
    return false;
}

bool AnnotationId::operator<(const AnnotationId& other) const
{
    if (kind != other.kind) return kind < other.kind;
    switch (kind) {
    case Kind::Object: return object < other.object;
    case Kind::Direct: return page != other.page ? page < other.page : position < other.position;
    case Kind::Created: return nm < other.nm;
    }
    return false;
}

QString AnnotationId::toString() const
{
    switch (kind) {
    case Kind::Object: return QStringLiteral("obj:%1").arg(object);
    case Kind::Direct: return QStringLiteral("direct:%1:%2").arg(page).arg(position);
    case Kind::Created: return QStringLiteral("nm:") + nm;
    }
    return QString();
}

std::optional<AnnotationId> AnnotationId::fromString(const QString& text)
{
    if (text.startsWith(QLatin1String("obj:"))) {
        bool ok = false;
        const long long number = text.mid(4).toLongLong(&ok);
        if (ok && number > 0) return ofObject(number);
    } else if (text.startsWith(QLatin1String("direct:"))) {
        const QStringList parts = text.mid(7).split(QLatin1Char(':'));
        bool okPage = false;
        bool okPosition = false;
        if (parts.size() == 2) {
            const int page = parts[0].toInt(&okPage);
            const int position = parts[1].toInt(&okPosition);
            if (okPage && okPosition && page >= 0 && position >= 0) return ofDirect(page, position);
        }
    } else if (text.startsWith(QLatin1String("nm:")) && text.size() > 3) {
        return ofCreated(text.mid(3));
    }
    return std::nullopt;
}

// --- Geometry ---------------------------------------------------------------------------------

std::optional<PdfRect> normalizedRect(double x0, double y0, double x1, double y1)
{
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1))
        return std::nullopt;
    const double left = std::min(x0, x1);
    const double bottom = std::min(y0, y1);
    return PdfRect{PdfPoint{left, bottom}, PdfSize{std::max(x0, x1) - left, std::max(y0, y1) - bottom}};
}

// --- JSON -------------------------------------------------------------------------------------
// Keyed by the names `corpus/labels/annots-*.json` uses, so a person reading a journal line and a
// label reads one vocabulary. Absent stays absent: an empty string, an empty array and a missing key
// are written as nothing, and read back as the default -- which is what makes the round trip exact.

QJsonObject Annotation::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("id"), id.toString());
    json.insert(QStringLiteral("subtype"), subtypeKey(*this));
    json.insert(QStringLiteral("page"), page);
    json.insert(QStringLiteral("rect"), numbersToJson({rect.left(), rect.bottom(), rect.right(), rect.top()}));
    if (!rectFinite) json.insert(QStringLiteral("rectFinite"), false);
    json.insert(QStringLiteral("flags"), static_cast<double>(flags));

    auto numbers = [&json](const char* key, const std::vector<double>& values) {
        if (!values.empty()) json.insert(QLatin1String(key), numbersToJson(values));
    };
    numbers("quadPoints", quadPoints);
    numbers("vertices", vertices);
    numbers("line", line);
    numbers("callout", callout);
    numbers("rectDifferences", rectDifferences);
    numbers("color", color);
    numbers("interiorColor", interiorColor);
    if (!inkList.empty()) {
        QJsonArray strokes;
        for (const std::vector<double>& stroke : inkList) strokes.append(numbersToJson(stroke));
        json.insert(QStringLiteral("inkList"), strokes);
    }
    if (!lineEndings.empty()) {
        QJsonArray endings;
        for (const QString& ending : lineEndings) endings.append(ending);
        json.insert(QStringLiteral("lineEndings"), endings);
    }
    if (opacity) json.insert(QStringLiteral("opacity"), *opacity);
    if (borderWidth) json.insert(QStringLiteral("borderWidth"), *borderWidth);
    numbers("dashPattern", dashPattern);
    if (cloudIntensity) json.insert(QStringLiteral("cloudIntensity"), *cloudIntensity);

    auto text = [&json](const char* key, const QString& value) {
        if (!value.isEmpty()) json.insert(QLatin1String(key), value);
    };
    text("nm", nm);
    text("author", author);
    text("subject", subject);
    text("contents", contents);
    text("richText", richText);
    text("intent", intent);
    text("icon", icon);
    text("defaultAppearance", defaultAppearance);
    text("created", created.raw);
    text("modified", modified.raw);
    text("stateModel", stateModel);
    text("state", state);
    text("borderStyle", borderStyle);
    text("blendMode", blendMode);

    // Object numbers as 64-bit INTEGERS, not doubles: a file may name object 2^60, and a double would
    // round it to a different object. A reference to an annotation RawPage created is its identity's
    // text, `nm:<nm>` (Stage 6 T6), so the two can never be mistaken for each other.
    const auto reference = [&json](const char* key, const std::optional<AnnotationId>& target) {
        if (!target) return;
        if (target->kind == AnnotationId::Kind::Object)
            json.insert(QLatin1String(key), static_cast<qint64>(target->object));
        else
            json.insert(QLatin1String(key), target->toString());
    };
    reference("inReplyTo", inReplyTo);
    if (replyType == ReplyType::Group) json.insert(QStringLiteral("replyType"), QStringLiteral("Group"));
    reference("popup", popup);
    reference("parent", parent);
    if (open) json.insert(QStringLiteral("open"), *open);
    if (rotate != 0) json.insert(QStringLiteral("rotate"), rotate);
    if (justification != 0) json.insert(QStringLiteral("justification"), justification);
    if (hasAppearance) json.insert(QStringLiteral("hasAppearance"), true);
    if (!image.isEmpty()) json.insert(QStringLiteral("image"), imageToJson(image));
    if (attachment) json.insert(QStringLiteral("attachment"), attachment->toJson());
    return json;
}

Result<Annotation> Annotation::fromJson(const QJsonObject& json)
{
    Annotation a;
    const std::optional<AnnotationId> id = AnnotationId::fromString(json.value(QStringLiteral("id")).toString());
    if (!id) return malformed(QStringLiteral("annotation id"));
    a.id = *id;

    a.subtypeName = json.value(QStringLiteral("subtype")).toString();
    a.subtype = annotationSubtypeFromName(a.subtypeName.toStdString());
    if (QString(annotationSubtypeName(a.subtype)) == a.subtypeName) a.subtypeName.clear();
    a.page = json.value(QStringLiteral("page")).toInt(-1);

    const std::vector<double> rect = numbersFromJson(json.value(QStringLiteral("rect")));
    if (rect.size() != 4) return malformed(QStringLiteral("annotation rect"));
    a.rect = PdfRect{PdfPoint{rect[0], rect[1]}, PdfSize{rect[2] - rect[0], rect[3] - rect[1]}};
    a.rectFinite = json.value(QStringLiteral("rectFinite")).toBool(true);
    a.flags = static_cast<std::uint32_t>(json.value(QStringLiteral("flags")).toDouble());

    a.quadPoints = numbersFromJson(json.value(QStringLiteral("quadPoints")));
    a.vertices = numbersFromJson(json.value(QStringLiteral("vertices")));
    a.line = numbersFromJson(json.value(QStringLiteral("line")));
    a.callout = numbersFromJson(json.value(QStringLiteral("callout")));
    a.rectDifferences = numbersFromJson(json.value(QStringLiteral("rectDifferences")));
    a.color = numbersFromJson(json.value(QStringLiteral("color")));
    a.interiorColor = numbersFromJson(json.value(QStringLiteral("interiorColor")));
    for (const QJsonValue stroke : json.value(QStringLiteral("inkList")).toArray())
        a.inkList.push_back(numbersFromJson(stroke));
    for (const QJsonValue ending : json.value(QStringLiteral("lineEndings")).toArray())
        a.lineEndings.push_back(ending.toString());
    if (json.contains(QStringLiteral("opacity"))) a.opacity = json.value(QStringLiteral("opacity")).toDouble();
    if (json.contains(QStringLiteral("borderWidth")))
        a.borderWidth = json.value(QStringLiteral("borderWidth")).toDouble();
    a.dashPattern = numbersFromJson(json.value(QStringLiteral("dashPattern")));
    if (json.contains(QStringLiteral("cloudIntensity")))
        a.cloudIntensity = json.value(QStringLiteral("cloudIntensity")).toDouble();
    a.borderStyle = json.value(QStringLiteral("borderStyle")).toString();
    a.blendMode = json.value(QStringLiteral("blendMode")).toString();

    a.nm = json.value(QStringLiteral("nm")).toString();
    a.author = json.value(QStringLiteral("author")).toString();
    a.subject = json.value(QStringLiteral("subject")).toString();
    a.contents = json.value(QStringLiteral("contents")).toString();
    a.richText = json.value(QStringLiteral("richText")).toString();
    a.intent = json.value(QStringLiteral("intent")).toString();
    a.icon = json.value(QStringLiteral("icon")).toString();
    a.defaultAppearance = json.value(QStringLiteral("defaultAppearance")).toString();
    if (json.contains(QStringLiteral("created")))
        a.created = PdfDate::parse(json.value(QStringLiteral("created")).toString());
    if (json.contains(QStringLiteral("modified")))
        a.modified = PdfDate::parse(json.value(QStringLiteral("modified")).toString());
    a.stateModel = json.value(QStringLiteral("stateModel")).toString();
    a.state = json.value(QStringLiteral("state")).toString();

    // A reference: an integer is an object number, a string a created identity -- and anything else, a
    // `direct:` identity among them (PDF points only at indirect objects), is malformed.
    const auto reference = [&json](const char* key, std::optional<AnnotationId>& target) -> bool {
        const QJsonValue value = json.value(QLatin1String(key));
        if (value.isUndefined()) return true;
        if (value.isString()) {
            const std::optional<AnnotationId> named = AnnotationId::fromString(value.toString());
            if (!named || named->kind == AnnotationId::Kind::Direct || !named->isValid()) return false;
            target = *named;
            return true;
        }
        if (!value.isDouble()) return false;
        target = AnnotationId::ofObject(value.toInteger());
        return true;
    };
    if (!reference("inReplyTo", a.inReplyTo)) return malformed(QStringLiteral("annotation inReplyTo"));
    if (json.value(QStringLiteral("replyType")).toString() == QLatin1String("Group"))
        a.replyType = ReplyType::Group;
    if (!reference("popup", a.popup)) return malformed(QStringLiteral("annotation popup"));
    if (!reference("parent", a.parent)) return malformed(QStringLiteral("annotation parent"));
    if (json.contains(QStringLiteral("open"))) a.open = json.value(QStringLiteral("open")).toBool();
    a.rotate = json.value(QStringLiteral("rotate")).toInt();
    a.justification = json.value(QStringLiteral("justification")).toInt();
    if (a.justification < 0 || a.justification > 2) return malformed(QStringLiteral("annotation justification"));
    a.hasAppearance = json.value(QStringLiteral("hasAppearance")).toBool();
    if (json.contains(QStringLiteral("image"))) {
        const std::optional<StampImage> image = imageFromJson(json.value(QStringLiteral("image")));
        if (!image) return malformed(QStringLiteral("annotation image"));
        a.image = *image;
    }
    if (json.contains(QStringLiteral("attachment"))) {
        const QJsonValue value = json.value(QStringLiteral("attachment"));
        if (!value.isObject()) return malformed(QStringLiteral("annotation attachment"));
        Result<EmbeddedFile> file = EmbeddedFile::fromJson(value.toObject());
        if (!file) return file.error();
        a.attachment = std::move(file).value();
    }
    return a;
}

// --- Creation (Stage 6 T3, D6-4) -----------------------------------------------------------------

QString newAnnotationName()
{
    QString hex;
    hex.reserve(32);
    for (int word = 0; word < 4; ++word)
        hex += QStringLiteral("%1").arg(QRandomGenerator::system()->generate(), 8, 16, QLatin1Char('0'));
    return hex;
}

Annotation newAnnotation(AnnotationSubtype subtype, int page, const QString& author, std::int64_t utcNow)
{
    Annotation a;
    a.nm = newAnnotationName();
    a.id = AnnotationId::ofCreated(a.nm);
    a.subtype = subtype;
    a.page = page;
    a.author = author;
    a.created = PdfDate::fromUtcSeconds(utcNow);
    a.modified = a.created;
    a.flags = AnnotationFlag::Print;
    a.hasAppearance = true;   // RawPage writes the /AP of everything it creates (D6-6)
    return a;
}

Annotation newReply(const Annotation& parent, const PdfRect& pageBox, const QString& contents,
                    const std::vector<double>& colour, const QString& author, std::int64_t utcNow)
{
    constexpr double kNote = 20.0;          // a note's icon, `ui/viewer/AnnotationOverlay::kNoteSize`
    constexpr double kMarginInset = 52.0;   // 540 on a 612-point page: the generator's margin column
    Annotation a = newAnnotation(AnnotationSubtype::Text, parent.page, author, utcNow);
    a.icon = QStringLiteral("Comment");
    a.contents = contents;
    a.flags = AnnotationFlag::Print | AnnotationFlag::NoZoom | AnnotationFlag::NoRotate;
    a.inReplyTo = parent.id;
    a.replyType = ReplyType::Reply;
    if (parent.subtype == AnnotationSubtype::Text) {
        a.rect = parent.rect;
        a.color = parent.color;
    } else {
        a.rect = PdfRect{PdfPoint{pageBox.right() - kMarginInset, parent.rect.bottom() - 4.0}, PdfSize{kNote, kNote}};
        a.color = colour;
    }
    return a;
}

Annotation newStateReply(const Annotation& parent, const QString& model, const QString& state, const QString& author,
                         std::int64_t utcNow)
{
    Annotation a = newAnnotation(AnnotationSubtype::Text, parent.page, author, utcNow);
    a.icon = QStringLiteral("Comment");
    a.flags = AnnotationFlag::Hidden | AnnotationFlag::Print | AnnotationFlag::NoZoom | AnnotationFlag::NoRotate;
    a.rect = parent.rect;
    a.inReplyTo = parent.id;
    a.replyType = ReplyType::Reply;
    a.stateModel = model;
    a.state = state;
    a.hasAppearance = false;   // a status is drawn by no reader (Acrobat writes none)
    return a;
}

bool isStateModelName(const QString& model)
{
    return model == QLatin1String("Review") || model == QLatin1String("Marked");
}

bool isStateOfModel(const QString& model, const QString& state)
{
    if (model == QLatin1String("Review"))
        return state == QLatin1String("Accepted") || state == QLatin1String("Rejected")
               || state == QLatin1String("Cancelled") || state == QLatin1String("Completed")
               || state == QLatin1String("None");
    if (model == QLatin1String("Marked")) return state == QLatin1String("Marked") || state == QLatin1String("Unmarked");
    return false;
}

std::optional<PdfRect> markupRect(const std::vector<double>& quadPoints)
{
    if (quadPoints.size() < 8 || quadPoints.size() % 8 != 0) return std::nullopt;
    const std::optional<PdfRect> bounds = boundsOfPoints(quadPoints);
    if (!bounds) return std::nullopt;
    double tallest = 0.0;
    for (std::size_t at = 0; at + 8 <= quadPoints.size(); at += 8) {
        // The first corner to the third: the quadrilateral's side, top to bottom, in Acrobat's order.
        tallest = std::max(tallest, std::hypot(quadPoints[at] - quadPoints[at + 4], quadPoints[at + 1] - quadPoints[at + 5]));
    }
    const double grow = std::max(2.0, 0.15 * tallest);
    return PdfRect{PdfPoint{bounds->left() - grow, bounds->bottom() - grow},
                   PdfSize{bounds->size.width + 2 * grow, bounds->size.height + 2 * grow}};
}

bool isBorderStyleName(const QString& name)
{
    return name == QLatin1String("S") || name == QLatin1String("D") || name == QLatin1String("B")
           || name == QLatin1String("I") || name == QLatin1String("U");
}

bool isBlendModeName(const QString& name)
{
    static const char* const kNames[] = {"Normal",     "Compatible", "Multiply",  "Screen",     "Overlay",
                                         "Darken",     "Lighten",    "ColorDodge", "ColorBurn", "HardLight",
                                         "SoftLight",  "Difference", "Exclusion", "Hue",        "Saturation",
                                         "Color",      "Luminosity"};
    for (const char* known : kNames) {
        if (name == QLatin1String(known)) return true;
    }
    return false;
}

QString identityAuthor(const QString& configured, const QString& loginName)
{
    const QString name = configured.trimmed();
    return name.isEmpty() ? loginName.trimmed() : name;
}

std::optional<PdfRect> boundsOfPoints(const std::vector<double>& coordinates)
{
    if (coordinates.size() < 2) return std::nullopt;
    double left = coordinates[0], right = coordinates[0];
    double bottom = coordinates[1], top = coordinates[1];
    for (std::size_t at = 0; at + 1 < coordinates.size(); at += 2) {
        const double x = coordinates[at];
        const double y = coordinates[at + 1];
        if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
        left = std::min(left, x);
        right = std::max(right, x);
        bottom = std::min(bottom, y);
        top = std::max(top, y);
    }
    return normalizedRect(left, bottom, right, top);
}

} // namespace rawpage::core
