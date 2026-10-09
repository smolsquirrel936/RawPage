// core/doc/Stamps -- implementation. Stage 6 T10. See the header for the four kinds, why a stamp is drawn at
// its natural size, and how its images are held.

#include "core/doc/Stamps.h"

#include <QCryptographicHash>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace rawpage::core {
namespace {

Error stampError(const char* key, const QString& detail = QString())
{
    return Error{ErrorCode::Unsupported, QLatin1String(key), detail};
}

const std::vector<double> kGreen{0.11, 0.45, 0.15};   // the corpus generator's /Approved
const std::vector<double> kRed{0.75, 0.10, 0.10};
const std::vector<double> kBlue{0.13, 0.25, 0.60};

QStringView withoutDynamic(QStringView name) noexcept
{
    constexpr QLatin1String prefix("Dynamic");
    return isDynamicStampName(name) ? name.mid(prefix.size()) : name;
}

// Helvetica-Bold's advances (Adobe's Core 14 AFM) for codes 32 to 90: the space, punctuation, digits and the
// capitals -- every character a stamp's title holds.
constexpr std::array<int, kHelveticaBoldLast - kHelveticaBoldFirst + 1> kHelveticaBold = {
    278, 333, 474, 556, 556, 889, 722, 278, 333, 333, 389, 584, 278, 333, 278, 278,   // 32 ' ' … 47 '/'
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556,                                 // 48 '0' … 57 '9'
    333, 333, 584, 584, 584, 611, 975,                                                // 58 ':' … 64 '@'
    722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833,                  // 65 'A' … 77 'M'
    722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611,                  // 78 'N' … 90 'Z'
};

double borderAndPadding() noexcept { return kStampBorder + kStampPadding; }

// The days since 1970-01-01 as a civil date -- Howard Hinnant's `civil_from_days`, so `core/` needs no time
// zone API the distro Qt might lack (convention 24) and the host alone says the offset (convention 11).
void civilFromDays(long long z, long long& year, int& month, int& day) noexcept
{
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const long long doe = z - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    year = yoe + era * 400 + (month <= 2 ? 1 : 0);
}

QString twoDigits(long long value) { return QStringLiteral("%1").arg(value, 2, 10, QLatin1Char('0')); }

std::uint16_t bigEndian16(const QByteArray& bytes, qsizetype at) noexcept
{
    return static_cast<std::uint16_t>((static_cast<unsigned char>(bytes[at]) << 8) | static_cast<unsigned char>(bytes[at + 1]));
}

} // namespace

// ---- Kinds and names --------------------------------------------------------------------------------

const std::vector<QLatin1String>& standardStampNames()
{
    static const std::vector<QLatin1String> names{
        QLatin1String("Approved"),     QLatin1String("Experimental"), QLatin1String("NotApproved"),
        QLatin1String("AsIs"),         QLatin1String("Expired"),      QLatin1String("NotForPublicRelease"),
        QLatin1String("Confidential"), QLatin1String("Final"),        QLatin1String("Sold"),
        QLatin1String("Departmental"), QLatin1String("ForComment"),   QLatin1String("TopSecret"),
        QLatin1String("Draft"),        QLatin1String("ForPublicRelease"),
    };
    return names;
}

bool isStandardStampName(QStringView name) noexcept
{
    const std::vector<QLatin1String>& names = standardStampNames();
    return std::any_of(names.begin(), names.end(), [name](QLatin1String n) { return name == n; });
}

const std::vector<QLatin1String>& dynamicStampNames()
{
    static const std::vector<QLatin1String> names{
        QLatin1String("DynamicApproved"), QLatin1String("DynamicReviewed"), QLatin1String("DynamicReceived"),
        QLatin1String("DynamicRevised"),  QLatin1String("DynamicConfidential"),
    };
    return names;
}

bool isDynamicStampName(QStringView name) noexcept
{
    const std::vector<QLatin1String>& names = dynamicStampNames();
    return std::any_of(names.begin(), names.end(), [name](QLatin1String n) { return name == n; });
}

QLatin1String stampImageIntent() noexcept { return QLatin1String("StampImage"); }

StampKind stampKind(const Annotation& a) noexcept
{
    if (a.subtype != AnnotationSubtype::Stamp) return StampKind::Foreign;
    if (a.intent == stampImageIntent()) return a.image.isEmpty() ? StampKind::Foreign : StampKind::Image;
    if (!a.image.isEmpty()) return StampKind::Foreign;   // pixels with no image intent: nothing says to draw them
    if (isStandardStampName(a.icon)) return StampKind::Standard;
    if (isDynamicStampName(a.icon)) return StampKind::Dynamic;
    return StampKind::Foreign;
}

QString stampTitle(QStringView name)
{
    const QStringView base = withoutDynamic(name);
    QString title;
    for (qsizetype i = 0; i < base.size(); ++i) {
        const QChar c = base[i];
        if (i > 0 && c.isUpper()) title += QLatin1Char(' ');
        title += c.toUpper();
    }
    return title;
}

std::vector<double> stampColour(QStringView name)
{
    const QStringView base = withoutDynamic(name);
    for (const char* green : {"Approved", "Final", "ForPublicRelease", "Sold", "Received"}) {
        if (base == QLatin1String(green)) return kGreen;
    }
    for (const char* red : {"NotApproved", "Confidential", "TopSecret", "Expired", "NotForPublicRelease"}) {
        if (base == QLatin1String(red)) return kRed;
    }
    return kBlue;   // Draft, Experimental, AsIs, Departmental, ForComment, Reviewed, Revised
}

int helveticaBoldWidth(int code) noexcept
{
    if (code < kHelveticaBoldFirst || code > kHelveticaBoldLast) return 0;
    return kHelveticaBold[static_cast<std::size_t>(code - kHelveticaBoldFirst)];
}

std::optional<double> helveticaBoldTextWidth(QStringView title, double size) noexcept
{
    double units = 0.0;
    for (const QChar c : title) {
        const int code = c.unicode();
        if (code < kHelveticaBoldFirst || code > kHelveticaBoldLast) return std::nullopt;
        units += helveticaBoldWidth(code);
    }
    return units * size / 1000.0;
}

// ---- Geometry ---------------------------------------------------------------------------------------

Result<PdfSize> stampNaturalSize(const Annotation& a, TypedFont han)
{
    const double margin = 2.0 * borderAndPadding();
    switch (stampKind(a)) {
    case StampKind::Standard: {
        const std::optional<double> width = helveticaBoldTextWidth(stampTitle(a.icon), kStampTitleSize);
        if (!width) return stampError("error.edit.stampNotRedrawable", a.icon);
        return PdfSize{*width + margin, kStampTitleSize + margin};
    }
    case StampKind::Dynamic: {
        const std::optional<double> title = helveticaBoldTextWidth(stampTitle(a.icon), kDynamicTitleSize);
        if (!title) return stampError("error.edit.stampNotRedrawable", a.icon);
        double widest = *title;
        const QStringList lines = a.contents.split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            Result<TypedLayout> laid = layoutTypedText(line, kDynamicLineSize, std::nullopt, han);
            if (!laid) return laid.error();
            widest = std::max(widest, laid.value().width);
        }
        const double height = kDynamicTitleSize + kStampPadding / 2.0
                              + static_cast<double>(lines.size()) * kTypedLeading * kDynamicLineSize;
        return PdfSize{widest + margin, height + margin};
    }
    case StampKind::Image: {
        const double longer = std::max(a.image.width, a.image.height);
        if (a.image.width <= 0 || a.image.height <= 0) return stampError("error.edit.stampImageUnsupported");
        const double scale = std::clamp(longer, kImageStampMinSide, kImageStampMaxSide) / longer;
        return PdfSize{a.image.width * scale, a.image.height * scale};
    }
    case StampKind::Foreign: break;
    }
    return stampError("error.edit.stampNotRedrawable", a.icon);
}

int stampRotation(const Annotation& a) noexcept { return ((a.rotate % 360) + 360) % 360; }

PdfRect stampRect(PdfPoint centre, PdfSize natural, double sx, double sy, int degrees) noexcept
{
    const double radians = static_cast<double>(degrees) * std::numbers::pi / 180.0;
    const double c = std::abs(std::cos(radians));
    const double s = std::abs(std::sin(radians));
    // Exact at the quarter turns, where cos and sin are a rounding error away from 0 and 1.
    const auto snapped = [](double v) { return std::abs(v) < 1e-12 ? 0.0 : (std::abs(v - 1.0) < 1e-12 ? 1.0 : v); };
    const double w = natural.width * sx * snapped(c) + natural.height * sy * snapped(s);
    const double h = natural.width * sx * snapped(s) + natural.height * sy * snapped(c);
    return PdfRect{PdfPoint{centre.x - w / 2.0, centre.y - h / 2.0}, PdfSize{w, h}};
}

std::optional<std::pair<double, double>> stampScale(const Annotation& a, PdfSize natural) noexcept
{
    const PdfRect unit = stampRect(PdfPoint{0.0, 0.0}, natural, 1.0, 1.0, stampRotation(a));
    if (!(unit.size.width > 0.0) || !(unit.size.height > 0.0) || !a.rectFinite) return std::nullopt;
    const double sx = a.rect.size.width / unit.size.width;
    const double sy = a.rect.size.height / unit.size.height;
    if (!std::isfinite(sx) || !std::isfinite(sy) || sx <= 0.0 || sy <= 0.0) return std::nullopt;
    return std::pair{sx, sy};
}

Result<Annotation> rotatedStamp(const Annotation& a, int degrees, TypedFont han)
{
    Result<PdfSize> natural = stampNaturalSize(a, han);
    if (!natural) return natural.error();
    const std::optional<std::pair<double, double>> scale = stampScale(a, natural.value());
    if (!scale) return stampError("error.edit.stampNotRedrawable", a.icon);
    Annotation turned = a;
    turned.rotate = ((degrees % 360) + 360) % 360;
    const PdfPoint centre{a.rect.left() + a.rect.size.width / 2.0, a.rect.bottom() + a.rect.size.height / 2.0};
    turned.rect = stampRect(centre, natural.value(), scale->first, scale->second, turned.rotate);
    return turned;
}

Result<Annotation> stampFittedTo(const Annotation& a, const PdfRect& box, TypedFont han)
{
    Result<PdfSize> natural = stampNaturalSize(a, han);
    if (!natural) return natural.error();
    const PdfRect unit = stampRect(PdfPoint{0.0, 0.0}, natural.value(), 1.0, 1.0, stampRotation(a));
    if (!(unit.size.width > 0.0) || !(unit.size.height > 0.0) || !(box.size.width > 0.0) || !(box.size.height > 0.0))
        return stampError("error.edit.stampNotRedrawable", a.icon);
    const double scale = std::min(box.size.width / unit.size.width, box.size.height / unit.size.height);
    Annotation fitted = a;
    const PdfPoint centre{box.left() + box.size.width / 2.0, box.bottom() + box.size.height / 2.0};
    fitted.rect = stampRect(centre, natural.value(), scale, scale, stampRotation(a));
    return fitted;
}

// ---- Making one ------------------------------------------------------------------------------------

QStringList dynamicStampLines(const QString& author, std::int64_t utcSeconds, int utcOffsetSeconds)
{
    const long long local = static_cast<long long>(utcSeconds) + utcOffsetSeconds;
    long long days = local / 86400;
    long long seconds = local % 86400;
    if (seconds < 0) {
        seconds += 86400;
        --days;
    }
    long long year = 0;
    int month = 0;
    int day = 0;
    civilFromDays(days, year, month, day);
    const QString when = QStringLiteral("%1-%2-%3 %4:%5")
                             .arg(year, 4, 10, QLatin1Char('0'))
                             .arg(twoDigits(month), twoDigits(day), twoDigits(seconds / 3600), twoDigits((seconds / 60) % 60));
    QStringList lines;
    const QString name = author.trimmed();
    if (!name.isEmpty()) lines << name;
    lines << when;
    return lines;
}

namespace {

Result<Annotation> centred(Annotation a, PdfPoint centre, TypedFont han)
{
    Result<PdfSize> natural = stampNaturalSize(a, han);
    if (!natural) return natural.error();
    a.rect = stampRect(centre, natural.value(), 1.0, 1.0, 0);
    return a;
}

} // namespace

Result<Annotation> newStandardStamp(const QString& name, int page, PdfPoint centre, const QString& author,
                                    std::int64_t utcNow)
{
    if (!isStandardStampName(name)) return stampError("error.edit.stampUnknown", name);
    Annotation a = newAnnotation(AnnotationSubtype::Stamp, page, author, utcNow);
    a.icon = name;
    a.subject = name;
    a.color = stampColour(name);
    return centred(std::move(a), centre, TypedFont::Simplified);
}

Result<Annotation> newDynamicStamp(const QString& name, int page, PdfPoint centre, const QString& author,
                                   std::int64_t utcNow, int utcOffsetSeconds, TypedFont han)
{
    if (!isDynamicStampName(name)) return stampError("error.edit.stampUnknown", name);
    Annotation a = newAnnotation(AnnotationSubtype::Stamp, page, author, utcNow);
    a.icon = name;
    a.subject = withoutDynamic(name).toString();
    a.color = stampColour(name);
    a.contents = dynamicStampLines(author, utcNow, utcOffsetSeconds).join(QLatin1Char('\n'));
    return centred(std::move(a), centre, han);
}

Result<Annotation> newImageStamp(const StampImage& image, const QString& subject, int page, PdfPoint centre,
                                 const QString& author, std::int64_t utcNow)
{
    if (const std::optional<QString> invalid = invalidStampImage(image))
        return stampError("error.edit.stampImageUnsupported", *invalid);
    Annotation a = newAnnotation(AnnotationSubtype::Stamp, page, author, utcNow);
    a.intent = stampImageIntent();
    a.subject = subject;
    a.image = image;
    return centred(std::move(a), centre, TypedFont::Simplified);
}

// ---- Images ---------------------------------------------------------------------------------------

Result<StampImage> stampImageFromJpeg(const QByteArray& jpeg)
{
    const auto refused = [](const char* why) { return stampError("error.edit.stampImageUnsupported", QLatin1String(why)); };
    if (jpeg.size() > kMaxStampImageBytes) return refused("size");
    if (jpeg.size() < 4 || static_cast<unsigned char>(jpeg[0]) != 0xFF || static_cast<unsigned char>(jpeg[1]) != 0xD8)
        return refused("jpeg");
    qsizetype at = 2;
    while (at + 4 <= jpeg.size()) {
        if (static_cast<unsigned char>(jpeg[at]) != 0xFF) return refused("jpeg");
        const unsigned char marker = static_cast<unsigned char>(jpeg[at + 1]);
        if (marker == 0xFF) {   // fill bytes before a marker
            ++at;
            continue;
        }
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {   // no length
            at += 2;
            continue;
        }
        if (marker == 0xD9 || marker == 0xDA) return refused("jpeg");   // the image ended, or began, before a frame
        const std::uint16_t length = bigEndian16(jpeg, at + 2);
        if (length < 2 || at + 2 + length > jpeg.size()) return refused("jpeg");
        const bool frame = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        if (frame) {
            // Baseline, extended sequential and progressive Huffman are what `/DCTDecode` reads (ISO 32000-1
            // §7.4.8); lossless, hierarchical and arithmetic-coded frames are not.
            if (marker != 0xC0 && marker != 0xC1 && marker != 0xC2) return refused("coding");
            if (length < 8) return refused("jpeg");
            const int precision = static_cast<unsigned char>(jpeg[at + 4]);
            StampImage image;
            image.encoding = StampImage::Encoding::Jpeg;
            image.height = bigEndian16(jpeg, at + 5);
            image.width = bigEndian16(jpeg, at + 7);
            image.components = static_cast<unsigned char>(jpeg[at + 9]);
            image.data = jpeg;
            if (precision != 8) return refused("precision");
            if (image.components != 1 && image.components != 3) return refused("components");
            if (image.width <= 0 || image.height <= 0 || image.width > kMaxStampImageSide || image.height > kMaxStampImageSide)
                return refused("size");
            return image;
        }
        at += 2 + length;
    }
    return refused("jpeg");
}

Result<StampImage> stampImageFromPixels(int width, int height, const QByteArray& rgb, const QByteArray& alpha)
{
    const auto refused = [](const char* why) { return stampError("error.edit.stampImageUnsupported", QLatin1String(why)); };
    if (width <= 0 || height <= 0 || width > kMaxStampImageSide || height > kMaxStampImageSide
        || static_cast<qint64>(width) * height > kMaxStampImagePixels)
        return refused("size");
    const qint64 pixels = static_cast<qint64>(width) * height;
    if (rgb.size() != pixels * 3) return refused("samples");
    if (!alpha.isEmpty() && alpha.size() != pixels) return refused("alpha");
    StampImage image;
    image.encoding = StampImage::Encoding::Flate;
    image.width = width;
    image.height = height;
    image.components = 3;
    // `qCompress` writes the uncompressed length in four bytes, then a zlib stream -- which is what
    // `/FlateDecode` reads, once the four are dropped.
    image.data = qCompress(rgb, 9).mid(4);
    const bool opaque = std::all_of(alpha.begin(), alpha.end(), [](char v) { return static_cast<unsigned char>(v) == 0xFF; });
    if (!alpha.isEmpty() && !opaque) image.alpha = qCompress(alpha, 9).mid(4);
    return image;
}

std::optional<QString> invalidStampImage(const StampImage& image)
{
    switch (image.encoding) {
    case StampImage::Encoding::None: return QStringLiteral("image.encoding");
    case StampImage::Encoding::Jpeg: {
        if (!image.alpha.isEmpty()) return QStringLiteral("image.alpha");
        // The header must say what the fields say: a writer trusting a line's size over the file's would write
        // an image every reader draws differently.
        Result<StampImage> read = stampImageFromJpeg(image.data);
        if (!read || read.value().width != image.width || read.value().height != image.height
            || read.value().components != image.components)
            return QStringLiteral("image.data");
        return std::nullopt;
    }
    case StampImage::Encoding::Flate:
        if (image.width <= 0 || image.height <= 0 || image.width > kMaxStampImageSide || image.height > kMaxStampImageSide
            || static_cast<qint64>(image.width) * image.height > kMaxStampImagePixels)
            return QStringLiteral("image.size");
        if (image.components != 1 && image.components != 3) return QStringLiteral("image.components");
        if (image.data.isEmpty() || image.data.size() > kMaxStampImageBytes || image.alpha.size() > kMaxStampImageBytes)
            return QStringLiteral("image.data");
        return std::nullopt;
    }
    return QStringLiteral("image.encoding");
}

QString stampImageKey(const StampImage& image)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const QByteArray header = QStringLiteral("%1 %2 %3 %4 %5 %6;")
                                  .arg(static_cast<int>(image.encoding))
                                  .arg(image.width)
                                  .arg(image.height)
                                  .arg(image.components)
                                  .arg(image.data.size())
                                  .arg(image.alpha.size())
                                  .toLatin1();
    hash.addData(header);
    hash.addData(image.data);
    hash.addData(image.alpha);
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace rawpage::core
