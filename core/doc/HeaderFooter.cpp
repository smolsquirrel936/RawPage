// core/doc/HeaderFooter -- see the header.

#include "core/doc/HeaderFooter.h"

#include "core/doc/PageTree.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QLatin1String>
#include <QStringList>

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

constexpr const char* kAreaNames[kHeaderFooterAreas] = {"headerLeft", "headerCentre", "headerRight",
                                                        "footerLeft", "footerCentre", "footerRight"};

QLatin1String parityName(PageParity parity)
{
    switch (parity) {
    case PageParity::Odd: return QLatin1String("odd");
    case PageParity::Even: return QLatin1String("even");
    case PageParity::Any: break;
    }
    return QLatin1String("all");
}

// The lines one area's text becomes: its own line breaks, each line laid out in D6-7's faces.
Result<std::vector<TypedLayout>> layoutArea(const QString& text, double size, TypedFont han)
{
    std::vector<TypedLayout> out;
    if (text.isEmpty()) return out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        QString one = line;
        one.remove(QLatin1Char('\r'));
        Result<TypedLayout> laid = layoutTypedText(one, size, std::nullopt, han);
        if (!laid) {
            // Said as a PAGE's refusal, not a comment's: the same rule, the same character named (Q5(e)).
            Error error = laid.error();
            if (error.messageKey == QLatin1String("error.edit.typedCharacterRefused"))
                error.messageKey = QLatin1String("error.decorate.characterRefused");
            return error;
        }
        out.push_back(std::move(laid).value());
    }
    return out;
}

DecorationLine lineAt(const Page& page, const TypedLayout& layout, double x, double baseline, const HeaderFooterSpec& spec)
{
    DecorationLine line;
    line.matrix = shownMatrix(page, x, baseline);
    line.size = spec.size;
    line.colour = spec.colour;
    line.underline = spec.underline;
    // One line: `layoutArea` broke the text at its own line breaks and asked for no wrapping.
    for (const TypedLine& typed : layout.lines) {
        for (const TypedRun& run : typed.runs) line.runs.push_back(run);
    }
    line.width = layout.width;   // trailing spaces not counted: what the line is aligned and underlined by
    return line;
}

} // namespace

bool operator==(const DecorationLine& a, const DecorationLine& b)
{
    if (a.matrix != b.matrix || a.size != b.size || a.colour != b.colour || a.underline != b.underline || a.width != b.width
        || a.runs.size() != b.runs.size())
        return false;
    for (std::size_t i = 0; i < a.runs.size(); ++i) {
        if (a.runs[i].font != b.runs[i].font || a.runs[i].text != b.runs[i].text || a.runs[i].width != b.runs[i].width)
            return false;
    }
    return true;
}

QLatin1String decorationKindName(DecorationKind kind) noexcept
{
    switch (kind) {
    case DecorationKind::HeaderFooter: return QLatin1String("HeaderFooter");
    case DecorationKind::Bates: return QLatin1String("Bates");
    case DecorationKind::Watermark: return QLatin1String("Watermark");
    }
    return QLatin1String("HeaderFooter");
}

std::optional<DecorationKind> decorationKindByName(QStringView name) noexcept
{
    for (const DecorationKind kind : {DecorationKind::HeaderFooter, DecorationKind::Bates, DecorationKind::Watermark}) {
        if (name == decorationKindName(kind)) return kind;
    }
    return std::nullopt;
}

// ---- The spec -------------------------------------------------------------------------------------------------------

Status validate(const HeaderFooterSpec& spec)
{
    if (!inRange(spec.size, kMinDecorationSize, kMaxDecorationSize)) return badSpec("size");
    for (const double c : spec.colour) {
        if (!inRange(c, 0.0, 1.0)) return badSpec("colour");
    }
    for (const auto& [value, field] : {std::pair{spec.top, "top"}, std::pair{spec.bottom, "bottom"},
                                       std::pair{spec.left, "left"}, std::pair{spec.right, "right"}}) {
        if (!inRange(value, 0.0, kMaxDecorationMargin)) return badSpec(field);
    }
    if (spec.bates.digits < kMinBatesDigits || spec.bates.digits > kMaxBatesDigits) return badSpec("batesDigits");
    if (spec.bates.start < 0 || spec.bates.start > kMaxBatesNumber) return badSpec("batesStart");
    if (spec.bates.prefix.size() > kMaxBatesAffix) return badSpec("batesPrefix");
    if (spec.bates.suffix.size() > kMaxBatesAffix) return badSpec("batesSuffix");
    if (spec.range.size() > kMaxAreaLength) return badSpec("range");
    bool any = false;
    for (const QString& area : spec.areas) {
        if (area.size() > kMaxAreaLength) return badSpec("area");
        if (!area.trimmed().isEmpty()) any = true;
    }
    // Every character TYPED must be one a face can show -- a field's expansion is the command's to check.
    for (const QString& text : spec.areas) {
        if (std::optional<char32_t> c = firstRefusedCharacter(text))
            return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.characterRefused"), describeCharacter(*c)};
    }
    if (usesBates(spec)) {
        for (const QString& text : {spec.bates.prefix, spec.bates.suffix}) {
            if (std::optional<char32_t> c = firstRefusedCharacter(text))
                return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.characterRefused"), describeCharacter(*c)};
        }
    }
    if (!any) return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.empty")};
    return Status{};
}

bool usesBates(const HeaderFooterSpec& spec)
{
    return std::any_of(spec.areas.begin(), spec.areas.end(),
                       [](const QString& area) { return area.contains(QLatin1String("<<bates>>")); });
}

Result<std::vector<int>> headerFooterPages(const HeaderFooterSpec& spec, const PageTree& pages)
{
    return decoratedPages(spec.range, spec.parity, pages);
}

Result<std::vector<int>> decoratedPages(const QString& range, PageParity parity, const PageTree& pages)
{
    std::vector<int> all;
    if (range.trimmed().isEmpty()) {
        for (int page = 0; page < pages.count(); ++page) all.push_back(page);
    } else {
        Result<std::vector<int>> parsed = parsePageRanges(range, pages.count());
        if (!parsed) return parsed.error();
        all = std::move(parsed).value();
    }
    std::vector<int> kept = filterPages(all, parity, PageOrientation::Any, pages);
    if (kept.empty()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.noPages")};
    return kept;
}

QLatin1String pageParityName(PageParity parity) noexcept
{
    return parityName(parity);
}

std::optional<PageParity> pageParityByName(QStringView name) noexcept
{
    for (const PageParity parity : {PageParity::Any, PageParity::Odd, PageParity::Even}) {
        if (name == parityName(parity)) return parity;
    }
    return std::nullopt;
}

QJsonObject specToJson(const HeaderFooterSpec& spec)
{
    QJsonObject areas;
    for (int i = 0; i < kHeaderFooterAreas; ++i) {
        if (!spec.areas[static_cast<std::size_t>(i)].isEmpty()) areas.insert(QLatin1String(kAreaNames[i]), spec.areas[static_cast<std::size_t>(i)]);
    }
    return QJsonObject{{QStringLiteral("areas"), areas},
                       {QStringLiteral("size"), spec.size},
                       {QStringLiteral("colour"), QJsonArray{spec.colour[0], spec.colour[1], spec.colour[2]}},
                       {QStringLiteral("underline"), spec.underline},
                       {QStringLiteral("top"), spec.top},
                       {QStringLiteral("bottom"), spec.bottom},
                       {QStringLiteral("left"), spec.left},
                       {QStringLiteral("right"), spec.right},
                       {QStringLiteral("shrink"), spec.shrink},
                       {QStringLiteral("range"), spec.range},
                       {QStringLiteral("parity"), parityName(spec.parity)},
                       {QStringLiteral("han"), QLatin1String(typedFontSpec(spec.han).resource)},
                       {QStringLiteral("bates"), QJsonObject{{QStringLiteral("prefix"), spec.bates.prefix},
                                                             {QStringLiteral("suffix"), spec.bates.suffix},
                                                             {QStringLiteral("digits"), spec.bates.digits},
                                                             {QStringLiteral("start"), static_cast<double>(spec.bates.start)}}}};
}

Result<HeaderFooterSpec> specFromJson(const QJsonObject& json)
{
    HeaderFooterSpec spec;
    const QJsonValue areas = json.value(QStringLiteral("areas"));
    if (!areas.isObject()) return badSpec("areas");
    const QJsonObject areaObject = areas.toObject();
    for (auto it = areaObject.begin(); it != areaObject.end(); ++it) {
        int at = -1;
        for (int i = 0; i < kHeaderFooterAreas; ++i) {
            if (it.key() == QLatin1String(kAreaNames[i])) at = i;
        }
        if (at < 0 || !it.value().isString()) return badSpec("areas");
        spec.areas[static_cast<std::size_t>(at)] = it.value().toString();
    }
    const auto number = [&json](const char* key, double& into) {
        const QJsonValue value = json.value(QLatin1String(key));
        if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
        into = value.toDouble();
        return true;
    };
    for (const auto& [key, into] : {std::pair{"size", &spec.size}, std::pair{"top", &spec.top}, std::pair{"bottom", &spec.bottom},
                                    std::pair{"left", &spec.left}, std::pair{"right", &spec.right}}) {
        if (!number(key, *into)) return badSpec(key);
    }
    const QJsonValue colour = json.value(QStringLiteral("colour"));
    if (!colour.isArray() || colour.toArray().size() != 3) return badSpec("colour");
    for (int i = 0; i < 3; ++i) {
        const QJsonValue c = colour.toArray().at(i);
        if (!c.isDouble()) return badSpec("colour");
        spec.colour[static_cast<std::size_t>(i)] = c.toDouble();
    }
    for (const auto& [key, into] : {std::pair{"underline", &spec.underline}, std::pair{"shrink", &spec.shrink}}) {
        const QJsonValue value = json.value(QLatin1String(key));
        if (!value.isBool()) return badSpec(key);
        *into = value.toBool();
    }
    const QJsonValue range = json.value(QStringLiteral("range"));
    if (!range.isString()) return badSpec("range");
    spec.range = range.toString();
    const QString parity = json.value(QStringLiteral("parity")).toString();
    if (parity == QLatin1String("all")) spec.parity = PageParity::Any;
    else if (parity == QLatin1String("odd")) spec.parity = PageParity::Odd;
    else if (parity == QLatin1String("even")) spec.parity = PageParity::Even;
    else return badSpec("parity");
    const std::optional<TypedFont> han = typedFontByResource(json.value(QStringLiteral("han")).toString());
    if (!han || !isCjk(*han)) return badSpec("han");
    spec.han = *han;
    const QJsonValue bates = json.value(QStringLiteral("bates"));
    if (!bates.isObject()) return badSpec("bates");
    const QJsonObject b = bates.toObject();
    if (!b.value(QStringLiteral("prefix")).isString() || !b.value(QStringLiteral("suffix")).isString()) return badSpec("bates");
    spec.bates.prefix = b.value(QStringLiteral("prefix")).toString();
    spec.bates.suffix = b.value(QStringLiteral("suffix")).toString();
    const QJsonValue digits = b.value(QStringLiteral("digits"));
    const QJsonValue start = b.value(QStringLiteral("start"));
    if (!digits.isDouble() || !start.isDouble()) return badSpec("bates");
    const double d = digits.toDouble();
    const double s = start.toDouble();
    if (std::trunc(d) != d || std::trunc(s) != s || d < kMinBatesDigits || d > kMaxBatesDigits || s < 0.0
        || s > static_cast<double>(kMaxBatesNumber))
        return badSpec("bates");
    spec.bates.digits = static_cast<int>(d);
    spec.bates.start = static_cast<long long>(s);
    if (Status valid = validate(spec); !valid) return valid.error();
    return spec;
}

// ---- The fields -------------------------------------------------------------------------------------------------------

QJsonObject fieldValuesToJson(const FieldValues& values)
{
    return QJsonObject{{QStringLiteral("date"), values.date.toString(Qt::ISODate)},
                       {QStringLiteral("time"), values.time.toString(QStringLiteral("HH:mm:ss"))},
                       {QStringLiteral("file"), values.fileName},
                       {QStringLiteral("path"), values.path}};
}

Result<FieldValues> fieldValuesFromJson(const QJsonObject& json)
{
    FieldValues out;
    for (const char* key : {"date", "time", "file", "path"}) {
        if (!json.value(QLatin1String(key)).isString()) return badSpec(key);
    }
    out.date = QDate::fromString(json.value(QStringLiteral("date")).toString(), Qt::ISODate);
    out.time = QTime::fromString(json.value(QStringLiteral("time")).toString(), QStringLiteral("HH:mm:ss"));
    if (!out.date.isValid()) return badSpec("date");
    if (!out.time.isValid()) return badSpec("time");
    out.fileName = json.value(QStringLiteral("file")).toString();
    out.path = json.value(QStringLiteral("path")).toString();
    if (out.fileName.size() > 4096 || out.path.size() > 32768) return badSpec("path");
    return out;
}

QString formatBates(const BatesFormat& bates, long long number)
{
    return bates.prefix + QString::number(number).rightJustified(bates.digits, QLatin1Char('0')) + bates.suffix;
}

QString expandFields(QStringView text, const FieldValues& values, const FieldPage& page, const BatesFormat& bates)
{
    QString out;
    out.reserve(text.size());
    qsizetype at = 0;
    while (at < text.size()) {
        const qsizetype open = text.indexOf(QLatin1String("<<"), at);
        if (open < 0) {
            out += text.mid(at);
            break;
        }
        const qsizetype close = text.indexOf(QLatin1String(">>"), open + 2);
        if (close < 0) {
            out += text.mid(at);
            break;
        }
        out += text.mid(at, open - at);
        const QStringView field = text.mid(open + 2, close - open - 2);
        const qsizetype colon = field.indexOf(QLatin1Char(':'));
        const QStringView name = colon < 0 ? field : field.left(colon);
        const QString format = colon < 0 ? QString() : field.mid(colon + 1).toString();
        bool known = true;
        if (name == QLatin1String("page") && colon < 0) out += QString::number(page.index + 1);
        else if (name == QLatin1String("count") && colon < 0) out += QString::number(page.count);
        else if (name == QLatin1String("label") && colon < 0) out += page.label.isEmpty() ? QString::number(page.index + 1) : page.label;
        else if (name == QLatin1String("file") && colon < 0) out += values.fileName;
        else if (name == QLatin1String("path") && colon < 0) out += values.path;
        else if (name == QLatin1String("date")) out += values.date.toString(format.isEmpty() ? QStringLiteral("yyyy-MM-dd") : format);
        else if (name == QLatin1String("time")) out += values.time.toString(format.isEmpty() ? QStringLiteral("HH:mm") : format);
        else if (name == QLatin1String("bates") && colon < 0 && page.bates) out += formatBates(bates, *page.bates);
        else known = false;
        if (!known) out += text.mid(open, close + 2 - open);   // as typed
        at = close + 2;
    }
    return out;
}

// ---- Layout ----------------------------------------------------------------------------------------------------------

PdfPoint shownToPage(const Page& page, double x, double y) noexcept
{
    const PdfRect box = page.cropBox();
    switch (page.rotation()) {
    case PageRotation::Clockwise90: return PdfPoint{box.right() - y, box.bottom() + x};
    case PageRotation::Half: return PdfPoint{box.right() - x, box.top() - y};
    case PageRotation::Clockwise270: return PdfPoint{box.left() + y, box.top() - x};
    case PageRotation::None: break;
    }
    return PdfPoint{box.left() + x, box.bottom() + y};
}

std::array<double, 6> shownMatrix(const Page& page, double x, double y) noexcept
{
    const PdfPoint at = shownToPage(page, x, y);
    switch (page.rotation()) {
    case PageRotation::Clockwise90: return {0.0, 1.0, -1.0, 0.0, at.x, at.y};
    case PageRotation::Half: return {-1.0, 0.0, 0.0, -1.0, at.x, at.y};
    case PageRotation::Clockwise270: return {0.0, -1.0, 1.0, 0.0, at.x, at.y};
    case PageRotation::None: break;
    }
    return {1.0, 0.0, 0.0, 1.0, at.x, at.y};
}

Result<PageDecoration> layoutHeaderFooter(const HeaderFooterSpec& spec, DecorationKind kind, const Page& page,
                                          const FieldValues& values, const FieldPage& field)
{
    if (Status valid = validate(spec); !valid) return valid.error();
    PageDecoration out;
    out.kind = kind;
    out.box = page.cropBox();
    const PdfSize shown = page.displaySize();
    const double leading = kTypedLeading * spec.size;
    const double ascent = kTypedAscent * spec.size;

    bool inHeader = false;
    bool inFooter = false;
    int headerLines = 0;
    int footerLines = 0;
    for (int area = 0; area < kHeaderFooterAreas; ++area) {
        const QString& typed = spec.areas[static_cast<std::size_t>(area)];
        if (typed.isEmpty()) continue;
        const QString text = expandFields(typed, values, field, spec.bates);
        Result<std::vector<TypedLayout>> laid = layoutArea(text, spec.size, spec.han);
        if (!laid) return laid.error();
        const std::vector<TypedLayout>& lines = laid.value();
        if (lines.empty()) continue;
        const bool header = area < 3;
        const int column = area % 3;
        const int count = static_cast<int>(lines.size());
        for (int i = 0; i < count; ++i) {
            const TypedLayout& layout = lines[static_cast<std::size_t>(i)];
            if (layout.width <= 0.0) continue;   // an empty line still takes its place, drawing nothing
            double x = spec.left;
            if (column == 1) x = (shown.width - layout.width) / 2.0;
            else if (column == 2) x = shown.width - spec.right - layout.width;
            // The header's FIRST line hangs `top` below the top edge; the footer's LAST stands `bottom` above the bottom.
            const double baseline = header ? shown.height - spec.top - ascent - i * leading
                                           : spec.bottom + (leading - ascent) + (count - 1 - i) * leading;
            out.lines.push_back(lineAt(page, layout, x, baseline, spec));
        }
        if (header) {
            inHeader = true;
            headerLines = std::max(headerLines, count);
        } else {
            inFooter = true;
            footerLines = std::max(footerLines, count);
        }
    }
    if (inHeader != inFooter) out.subtype = inHeader ? QStringLiteral("Header") : QStringLiteral("Footer");

    // THE WRAPPER (see the header): identity and clipped to what the page shows -- or, with `shrink`, the content
    // scaled to the space between the bands, centred across it.
    PageContentWrap wrap;
    wrap.clip = out.box;
    if (spec.shrink && (inHeader || inFooter) && shown.width > 0.0 && shown.height > 0.0) {
        const double headerBand = inHeader ? spec.top + headerLines * leading + kShrinkGap : 0.0;
        const double footerBand = inFooter ? spec.bottom + footerLines * leading + kShrinkGap : 0.0;
        const double available = shown.height - headerBand - footerBand;
        if (available <= 0.0) return Error{ErrorCode::InvalidArgument, QLatin1String("error.decorate.noRoom")};
        const double s = std::min(1.0, available / shown.height);
        // The shrunk page, as shown: centred across, its top under the header band.
        const double x0 = (shown.width - shown.width * s) / 2.0;
        const double y0 = footerBand + (available - shown.height * s) / 2.0;
        const PdfPoint a = shownToPage(page, x0, y0);
        const PdfPoint b = shownToPage(page, x0 + shown.width * s, y0 + shown.height * s);
        const double left = std::min(a.x, b.x);
        const double bottom = std::min(a.y, b.y);
        wrap.matrix = {s, 0.0, 0.0, s, left - s * out.box.left(), bottom - s * out.box.bottom()};
    }
    out.wrap = wrap;
    return out;
}

} // namespace rawpage::core
