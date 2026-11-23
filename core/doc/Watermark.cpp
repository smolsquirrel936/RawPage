// core/doc/Watermark -- see the header.

#include "core/doc/Watermark.h"

#include "core/doc/PageTree.h"
#include "core/doc/Stamps.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace rawpage::core {
namespace {

Error badSpec(const char* field)
{
    return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.badSpec"), QLatin1String(field)};
}

bool inRange(double value, double low, double high)
{
    return std::isfinite(value) && value >= low && value <= high;
}

bool isKey(const QString& id)
{
    if (id.size() != 32) return false;
    return std::all_of(id.begin(), id.end(), [](QChar c) {
        return (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
    });
}

// cos and sin of `degrees`, exact at the quarter turns -- a 90-degree watermark is not 6e-17 off true.
std::pair<double, double> turn(double degrees) noexcept
{
    const double radians = degrees * 3.14159265358979323846 / 180.0;
    double c = std::cos(radians);
    double s = std::sin(radians);
    const auto snap = [](double v) {
        if (std::fabs(v) < 1e-12) return 0.0;
        if (std::fabs(v - 1.0) < 1e-12) return 1.0;
        if (std::fabs(v + 1.0) < 1e-12) return -1.0;
        return v;
    };
    return {snap(c), snap(s)};
}

// `a` then `b`, as PDF concatenates matrices: a point is mapped by `a` first.
std::array<double, 6> then(const std::array<double, 6>& a, const std::array<double, 6>& b) noexcept
{
    return {a[0] * b[0] + a[1] * b[2],        a[0] * b[1] + a[1] * b[3],
            a[2] * b[0] + a[3] * b[2],        a[2] * b[1] + a[3] * b[3],
            a[4] * b[0] + a[5] * b[2] + b[4], a[4] * b[1] + a[5] * b[3] + b[5]};
}

Result<TypedLayout> layoutText(const WatermarkSpec& spec)
{
    QString text = spec.text;
    text.remove(QLatin1Char('\r'));
    Result<TypedLayout> laid = layoutTypedText(text, spec.size, std::nullopt, spec.han);
    if (!laid) {
        Error error = laid.error();
        if (error.messageKey == QLatin1String("error.edit.typedCharacterRefused"))
            error.messageKey = QLatin1String("error.decorate.characterRefused");
        return error;
    }
    return laid;
}

constexpr WatermarkSource kSources[] = {WatermarkSource::Text, WatermarkSource::Image, WatermarkSource::Page};
constexpr WatermarkUsage kUsages[] = {WatermarkUsage::Always, WatermarkUsage::PrintOnly, WatermarkUsage::ScreenOnly};
constexpr WatermarkAnchor kAnchors[] = {WatermarkAnchor::TopLeft,    WatermarkAnchor::Top,    WatermarkAnchor::TopRight,
                                        WatermarkAnchor::Left,       WatermarkAnchor::Centre, WatermarkAnchor::Right,
                                        WatermarkAnchor::BottomLeft, WatermarkAnchor::Bottom, WatermarkAnchor::BottomRight};

} // namespace

QLatin1String watermarkSourceName(WatermarkSource source) noexcept
{
    switch (source) {
    case WatermarkSource::Image: return QLatin1String("image");
    case WatermarkSource::Page: return QLatin1String("page");
    case WatermarkSource::Text: break;
    }
    return QLatin1String("text");
}

QLatin1String watermarkUsageName(WatermarkUsage usage) noexcept
{
    switch (usage) {
    case WatermarkUsage::PrintOnly: return QLatin1String("print");
    case WatermarkUsage::ScreenOnly: return QLatin1String("screen");
    case WatermarkUsage::Always: break;
    }
    return QLatin1String("always");
}

QLatin1String watermarkAnchorName(WatermarkAnchor anchor) noexcept
{
    switch (anchor) {
    case WatermarkAnchor::TopLeft: return QLatin1String("topLeft");
    case WatermarkAnchor::Top: return QLatin1String("top");
    case WatermarkAnchor::TopRight: return QLatin1String("topRight");
    case WatermarkAnchor::Left: return QLatin1String("left");
    case WatermarkAnchor::Right: return QLatin1String("right");
    case WatermarkAnchor::BottomLeft: return QLatin1String("bottomLeft");
    case WatermarkAnchor::Bottom: return QLatin1String("bottom");
    case WatermarkAnchor::BottomRight: return QLatin1String("bottomRight");
    case WatermarkAnchor::Centre: break;
    }
    return QLatin1String("centre");
}

// ---- The spec -------------------------------------------------------------------------------------------------------

Status validate(const WatermarkSpec& spec)
{
    if (!inRange(spec.opacity, kMinWatermarkOpacity, 1.0)) return badSpec("opacity");
    if (!inRange(spec.rotation, -kMaxWatermarkRotation, kMaxWatermarkRotation)) return badSpec("rotation");
    const double maxScale = spec.scaleMode == WatermarkScale::Absolute ? kMaxWatermarkAbsoluteScale : kMaxWatermarkRelativeScale;
    if (!inRange(spec.scale, kMinWatermarkScale, maxScale)) return badSpec("scale");
    if (!inRange(spec.offsetX, -kMaxWatermarkOffset, kMaxWatermarkOffset)) return badSpec("offsetX");
    if (!inRange(spec.offsetY, -kMaxWatermarkOffset, kMaxWatermarkOffset)) return badSpec("offsetY");
    if (!inRange(spec.tileGap, 0.0, kMaxWatermarkTileGap)) return badSpec("tileGap");
    if (spec.range.size() > kMaxAreaLength) return badSpec("range");
    if (spec.sourceName.size() > kMaxSourceNameLength) return badSpec("sourceName");
    switch (spec.source) {
    case WatermarkSource::Text: {
        if (!inRange(spec.size, kMinWatermarkTextSize, kMaxWatermarkTextSize)) return badSpec("size");
        for (const double c : spec.colour) {
            if (!inRange(c, 0.0, 1.0)) return badSpec("colour");
        }
        if (spec.text.size() > kMaxWatermarkTextLength) return badSpec("text");
        if (std::optional<char32_t> c = firstRefusedCharacter(spec.text))
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.characterRefused"), describeCharacter(*c)};
        if (spec.text.trimmed().isEmpty()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.empty")};
        break;
    }
    case WatermarkSource::Image:
    case WatermarkSource::Page: {
        if (!spec.keepSourceOf.isEmpty()) {
            if (!isKey(spec.keepSourceOf)) return badSpec("keepSourceOf");
            if (!inRange(spec.sourceSize.width, 1e-3, kMaxWatermarkOffset) || !inRange(spec.sourceSize.height, 1e-3, kMaxWatermarkOffset))
                return badSpec("sourceSize");
            break;
        }
        if (spec.source == WatermarkSource::Image) {
            if (spec.image.isEmpty() || invalidStampImage(spec.image)) return badSpec("image");
        } else if (spec.page.isEmpty() || invalidStampPage(spec.page)) {
            return badSpec("page");
        }
        break;
    }
    }
    return Status{};
}

Result<std::vector<int>> watermarkPages(const WatermarkSpec& spec, const PageTree& pages)
{
    return decoratedPages(spec.range, spec.parity, pages);
}

Result<PdfSize> watermarkNaturalSize(const WatermarkSpec& spec)
{
    switch (spec.source) {
    case WatermarkSource::Text: {
        Result<TypedLayout> laid = layoutText(spec);
        if (!laid) return laid.error();
        if (laid.value().width <= 0.0 || laid.value().height <= 0.0)
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.empty")};
        return PdfSize{laid.value().width, laid.value().height};
    }
    case WatermarkSource::Image:
        if (!spec.keepSourceOf.isEmpty()) return spec.sourceSize;
        return PdfSize{spec.image.width * kWatermarkPixelPoints, spec.image.height * kWatermarkPixelPoints};
    case WatermarkSource::Page:
        if (!spec.keepSourceOf.isEmpty()) return spec.sourceSize;
        return PdfSize{spec.page.width, spec.page.height};
    }
    return badSpec("source");
}

QJsonObject watermarkToJson(const WatermarkSpec& spec, bool withSource)
{
    QJsonObject json{{QStringLiteral("source"), watermarkSourceName(spec.source)},
                     {QStringLiteral("opacity"), spec.opacity},
                     {QStringLiteral("rotation"), spec.rotation},
                     {QStringLiteral("scaleMode"), spec.scaleMode == WatermarkScale::Absolute ? QStringLiteral("absolute")
                                                                                              : QStringLiteral("relative")},
                     {QStringLiteral("scale"), spec.scale},
                     {QStringLiteral("anchor"), watermarkAnchorName(spec.anchor)},
                     {QStringLiteral("offset"), QJsonArray{spec.offsetX, spec.offsetY}},
                     {QStringLiteral("tile"), spec.tile},
                     {QStringLiteral("tileGap"), spec.tileGap},
                     {QStringLiteral("behind"), spec.behind},
                     {QStringLiteral("usage"), watermarkUsageName(spec.usage)},
                     {QStringLiteral("range"), spec.range},
                     {QStringLiteral("parity"), pageParityName(spec.parity)}};
    if (!spec.sourceName.isEmpty()) json.insert(QStringLiteral("sourceName"), spec.sourceName);
    if (spec.source == WatermarkSource::Text) {
        json.insert(QStringLiteral("text"), spec.text);
        json.insert(QStringLiteral("size"), spec.size);
        json.insert(QStringLiteral("colour"), QJsonArray{spec.colour[0], spec.colour[1], spec.colour[2]});
        json.insert(QStringLiteral("han"), QLatin1String(typedFontSpec(spec.han).resource));
        return json;
    }
    if (!spec.keepSourceOf.isEmpty()) json.insert(QStringLiteral("keepSourceOf"), spec.keepSourceOf);
    // THE NATURAL SIZE ALWAYS: what an update that keeps the picture lays it out by, once the bytes are not carried.
    if (const Result<PdfSize> natural = watermarkNaturalSize(spec))
        json.insert(QStringLiteral("sourceSize"), QJsonArray{natural.value().width, natural.value().height});
    if (withSource && spec.keepSourceOf.isEmpty()) {
        if (spec.source == WatermarkSource::Image) json.insert(QStringLiteral("image"), stampImageJson(spec.image));
        else json.insert(QStringLiteral("page"), stampPageJson(spec.page));
    }
    return json;
}

Result<WatermarkSpec> watermarkFromJson(const QJsonObject& json, bool sourceRequired)
{
    WatermarkSpec spec;
    const QString source = json.value(QStringLiteral("source")).toString();
    bool known = false;
    for (const WatermarkSource s : kSources) {
        if (source == watermarkSourceName(s)) {
            spec.source = s;
            known = true;
        }
    }
    if (!known) return badSpec("source");
    const auto number = [&json](const char* key, double& into) {
        const QJsonValue value = json.value(QLatin1String(key));
        if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
        into = value.toDouble();
        return true;
    };
    for (const auto& [key, into] : {std::pair{"opacity", &spec.opacity}, std::pair{"rotation", &spec.rotation},
                                    std::pair{"scale", &spec.scale}, std::pair{"tileGap", &spec.tileGap}}) {
        if (!number(key, *into)) return badSpec(key);
    }
    const QString mode = json.value(QStringLiteral("scaleMode")).toString();
    if (mode == QLatin1String("absolute")) spec.scaleMode = WatermarkScale::Absolute;
    else if (mode == QLatin1String("relative")) spec.scaleMode = WatermarkScale::Relative;
    else return badSpec("scaleMode");
    known = false;
    const QString anchor = json.value(QStringLiteral("anchor")).toString();
    for (const WatermarkAnchor a : kAnchors) {
        if (anchor == watermarkAnchorName(a)) {
            spec.anchor = a;
            known = true;
        }
    }
    if (!known) return badSpec("anchor");
    const QJsonValue offset = json.value(QStringLiteral("offset"));
    if (!offset.isArray() || offset.toArray().size() != 2 || !offset.toArray().at(0).isDouble() || !offset.toArray().at(1).isDouble())
        return badSpec("offset");
    spec.offsetX = offset.toArray().at(0).toDouble();
    spec.offsetY = offset.toArray().at(1).toDouble();
    for (const auto& [key, into] : {std::pair{"tile", &spec.tile}, std::pair{"behind", &spec.behind}}) {
        const QJsonValue value = json.value(QLatin1String(key));
        if (!value.isBool()) return badSpec(key);
        *into = value.toBool();
    }
    known = false;
    const QString usage = json.value(QStringLiteral("usage")).toString();
    for (const WatermarkUsage u : kUsages) {
        if (usage == watermarkUsageName(u)) {
            spec.usage = u;
            known = true;
        }
    }
    if (!known) return badSpec("usage");
    const QJsonValue range = json.value(QStringLiteral("range"));
    if (!range.isString()) return badSpec("range");
    spec.range = range.toString();
    const std::optional<PageParity> parity = pageParityByName(json.value(QStringLiteral("parity")).toString());
    if (!parity) return badSpec("parity");
    spec.parity = *parity;
    const QJsonValue name = json.value(QStringLiteral("sourceName"));
    if (!name.isUndefined()) {
        if (!name.isString()) return badSpec("sourceName");
        spec.sourceName = name.toString();
    }

    if (spec.source == WatermarkSource::Text) {
        const QJsonValue text = json.value(QStringLiteral("text"));
        if (!text.isString()) return badSpec("text");
        spec.text = text.toString();
        if (!number("size", spec.size)) return badSpec("size");
        const QJsonValue colour = json.value(QStringLiteral("colour"));
        if (!colour.isArray() || colour.toArray().size() != 3) return badSpec("colour");
        for (int i = 0; i < 3; ++i) {
            const QJsonValue c = colour.toArray().at(i);
            if (!c.isDouble()) return badSpec("colour");
            spec.colour[static_cast<std::size_t>(i)] = c.toDouble();
        }
        const std::optional<TypedFont> han = typedFontByResource(json.value(QStringLiteral("han")).toString());
        if (!han || !isCjk(*han)) return badSpec("han");
        spec.han = *han;
    } else {
        const QJsonValue keep = json.value(QStringLiteral("keepSourceOf"));
        if (!keep.isUndefined()) {
            if (!keep.isString()) return badSpec("keepSourceOf");
            spec.keepSourceOf = keep.toString();
        }
        const QJsonValue size = json.value(QStringLiteral("sourceSize"));
        if (!size.isUndefined()) {
            if (!size.isArray() || size.toArray().size() != 2 || !size.toArray().at(0).isDouble() || !size.toArray().at(1).isDouble())
                return badSpec("sourceSize");
            spec.sourceSize = PdfSize{size.toArray().at(0).toDouble(), size.toArray().at(1).toDouble()};
        }
        const char* key = spec.source == WatermarkSource::Image ? "image" : "page";
        const QJsonValue bytes = json.value(QLatin1String(key));
        if (!bytes.isUndefined()) {
            if (spec.source == WatermarkSource::Image) {
                std::optional<StampImage> image = stampImageFromJsonValue(bytes);
                if (!image) return badSpec(key);
                spec.image = std::move(*image);
            } else {
                std::optional<StampPage> page = stampPageFromJsonValue(bytes);
                if (!page) return badSpec(key);
                spec.page = std::move(*page);
            }
            spec.sourceSize = PdfSize{};   // the bytes say it
        } else if (spec.keepSourceOf.isEmpty()) {
            if (sourceRequired) return badSpec(key);
            // A MARK'S RECORD: the picture is the document's, and its natural size stands for it -- checked as an update
            // keeping it would be.
            WatermarkSpec probe = spec;
            probe.keepSourceOf = QString(32, QLatin1Char('0'));
            if (Status valid = validate(probe); !valid) return valid.error();
            return spec;
        }
    }
    if (Status valid = validate(spec); !valid) return valid.error();
    return spec;
}

// ---- Layout ----------------------------------------------------------------------------------------------------------

PdfSize watermarkTurnedSize(PdfSize natural, double scale, double degrees) noexcept
{
    const auto [c, s] = turn(degrees);
    return PdfSize{scale * (natural.width * std::fabs(c) + natural.height * std::fabs(s)),
                   scale * (natural.width * std::fabs(s) + natural.height * std::fabs(c))};
}

std::array<double, 6> watermarkShownMatrix(PdfSize natural, double scale, double degrees, double left, double bottom) noexcept
{
    const auto [c, s] = turn(degrees);
    const PdfSize turned = watermarkTurnedSize(natural, scale, degrees);
    const double cx = left + turned.width / 2.0;
    const double cy = bottom + turned.height / 2.0;
    const double a = scale * c;
    const double b = scale * s;
    const double cc = -scale * s;
    const double d = scale * c;
    // The natural box's centre lands on the turned box's.
    return {a, b, cc, d, cx - (a * natural.width / 2.0 + cc * natural.height / 2.0), cy - (b * natural.width / 2.0 + d * natural.height / 2.0)};
}

Result<PageDecoration> layoutWatermark(const WatermarkSpec& spec, const Page& page)
{
    if (Status valid = validate(spec); !valid) return valid.error();
    Result<PdfSize> sized = watermarkNaturalSize(spec);
    if (!sized) return sized.error();
    const PdfSize natural = sized.value();
    if (!(natural.width > 0.0) || !(natural.height > 0.0)) return badSpec("source");

    PageDecoration out;
    out.kind = DecorationKind::Watermark;
    out.box = page.cropBox();
    out.behind = spec.behind;
    out.subtype = QStringLiteral("Watermark");
    if (!spec.behind) out.wrap = PageContentWrap{{1.0, 0.0, 0.0, 1.0, 0.0, 0.0}, out.box};

    WatermarkDrawing drawing;
    drawing.source = spec.source;
    drawing.natural = natural;
    drawing.opacity = spec.opacity;
    drawing.usage = spec.usage;
    drawing.keepSourceOf = spec.keepSourceOf;
    drawing.spec = watermarkToJson(spec, false);
    if (spec.source == WatermarkSource::Text) {
        Result<TypedLayout> laid = layoutText(spec);
        if (!laid) return laid.error();
        const double leading = kTypedLeading * spec.size;
        const double ascent = kTypedAscent * spec.size;
        int k = 0;
        for (const TypedLine& typed : laid.value().lines) {
            DecorationLine line;
            line.size = spec.size;
            line.colour = spec.colour;
            line.runs = typed.runs;
            line.width = typed.width;
            line.matrix = {1.0, 0.0, 0.0, 1.0, (natural.width - typed.width) / 2.0, natural.height - ascent - k * leading};
            ++k;
            if (typed.width > 0.0) drawing.lines.push_back(std::move(line));
        }
    } else if (spec.keepSourceOf.isEmpty()) {
        if (spec.source == WatermarkSource::Image) drawing.image = spec.image;
        else drawing.page = spec.page;
    }

    // WHERE, on the page as shown: the turned box anchored, then offset.
    const PdfSize shown = page.displaySize();
    const double unit = spec.scaleMode == WatermarkScale::Absolute
                            ? spec.scale
                            : spec.scale * std::min(shown.width / watermarkTurnedSize(natural, 1.0, spec.rotation).width,
                                                    shown.height / watermarkTurnedSize(natural, 1.0, spec.rotation).height);
    if (!std::isfinite(unit) || unit <= 0.0) return badSpec("scale");
    const PdfSize turned = watermarkTurnedSize(natural, unit, spec.rotation);
    const int column = static_cast<int>(spec.anchor) % 3;
    const int row = static_cast<int>(spec.anchor) / 3;
    const double ax = column * shown.width / 2.0 + spec.offsetX;
    const double ay = shown.height - row * shown.height / 2.0 + spec.offsetY;
    const double left = ax - column * turned.width / 2.0;
    const double bottom = ay - (2 - row) * turned.height / 2.0;
    const std::array<double, 6> toPage = shownMatrix(page, 0.0, 0.0);

    std::vector<std::pair<double, double>> corners;
    if (!spec.tile) {
        corners.emplace_back(left, bottom);
    } else {
        const double sx = turned.width + spec.tileGap;
        const double sy = turned.height + spec.tileGap;
        if (!(sx > 0.0) || !(sy > 0.0)) return badSpec("tileGap");
        // Every copy whose box touches the page: left + i*sx + width > 0 and left + i*sx < the page's width.
        const long long iLow = static_cast<long long>(std::floor((-turned.width - left) / sx)) + 1;
        const long long iHigh = static_cast<long long>(std::ceil((shown.width - left) / sx)) - 1;
        const long long jLow = static_cast<long long>(std::floor((-turned.height - bottom) / sy)) + 1;
        const long long jHigh = static_cast<long long>(std::ceil((shown.height - bottom) / sy)) - 1;
        const long long count = std::max(0LL, iHigh - iLow + 1) * std::max(0LL, jHigh - jLow + 1);
        if (count > kMaxWatermarkTiles)
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.tooManyTiles"), QString::number(count)};
        // Down the page from the top row, each row left to right: the order a reader paints them in.
        for (long long j = jHigh; j >= jLow; --j) {
            for (long long i = iLow; i <= iHigh; ++i) corners.emplace_back(left + static_cast<double>(i) * sx, bottom + static_cast<double>(j) * sy);
        }
    }
    for (const auto& [x, y] : corners) drawing.tiles.push_back(then(watermarkShownMatrix(natural, unit, spec.rotation, x, y), toPage));
    out.watermark = std::move(drawing);
    return out;
}

} // namespace rawpage::core
