// core/Measure -- implementation. Stage 6 T12. See the header for the scale, the reading and the drawn parts.

#include "core/Measure.h"

#include "core/doc/TypedText.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace rawpage::core {
namespace {

Error measureError(const char* key, const QString& context = QString())
{
    return Error{ErrorCode::InvalidArgument, QLatin1String(key), context};
}

bool allFinite(const std::vector<double>& xy) noexcept
{
    return std::all_of(xy.begin(), xy.end(), [](double v) { return std::isfinite(v); });
}

// The corners of a closed polygon with a repeated closing corner and consecutive duplicates dropped -- what a
// person who clicked back on the first corner, or twice in one place, meant.
std::vector<PdfPoint> corners(const std::vector<double>& xy)
{
    std::vector<PdfPoint> out;
    for (std::size_t at = 0; at + 1 < xy.size(); at += 2) {
        const PdfPoint p{xy[at], xy[at + 1]};
        if (!out.empty() && std::abs(out.back().x - p.x) < 1e-9 && std::abs(out.back().y - p.y) < 1e-9) continue;
        out.push_back(p);
    }
    while (out.size() > 1 && std::abs(out.front().x - out.back().x) < 1e-9 && std::abs(out.front().y - out.back().y) < 1e-9)
        out.pop_back();
    return out;
}

double cross(PdfPoint o, PdfPoint a, PdfPoint b) noexcept
{
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

// The side of `o`→`a` that `b` is on, with a tolerance relative to the lengths involved: 0 is "on the line".
int side(PdfPoint o, PdfPoint a, PdfPoint b) noexcept
{
    const double c = cross(o, a, b);
    const double scale = std::max({std::hypot(a.x - o.x, a.y - o.y) * std::hypot(b.x - o.x, b.y - o.y), 1e-12});
    if (std::abs(c) <= 1e-12 * scale) return 0;
    return c > 0.0 ? 1 : -1;
}

bool between(PdfPoint a, PdfPoint b, PdfPoint p) noexcept
{
    return std::min(a.x, b.x) - 1e-9 <= p.x && p.x <= std::max(a.x, b.x) + 1e-9 && std::min(a.y, b.y) - 1e-9 <= p.y
           && p.y <= std::max(a.y, b.y) + 1e-9;
}

// Whether the closed segments `a`-`b` and `c`-`d` share any point.
bool segmentsMeet(PdfPoint a, PdfPoint b, PdfPoint c, PdfPoint d) noexcept
{
    const int d1 = side(c, d, a), d2 = side(c, d, b), d3 = side(a, b, c), d4 = side(a, b, d);
    if (d1 * d2 < 0 && d3 * d4 < 0) return true;
    return (d1 == 0 && between(c, d, a)) || (d2 == 0 && between(c, d, b)) || (d3 == 0 && between(a, b, c))
           || (d4 == 0 && between(a, b, d));
}

// A polygon's corner count past which `selfIntersects` -- a test of every pair of edges -- is not asked: no person
// clicks ten thousand corners, and a journal line that says so is hostile.
constexpr std::size_t kMaxAreaCorners = 10000;

const MeasureNumberFormat* single(const std::vector<MeasureNumberFormat>& chain) noexcept
{
    return chain.size() == 1 ? &chain.front() : nullptr;
}

bool displayable(const MeasureNumberFormat& f) noexcept
{
    return f.plain && std::isfinite(f.factor) && f.factor > 0.0 && decimalsOfDenominator(f.denominator).has_value();
}

QString trimmedNumber(double value)
{
    QString text = QString::number(value, 'f', 4);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) text.chop(1);
        if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    }
    if (text == QLatin1String("0") && value > 0.0) text = QString::number(value, 'g', 6);
    return text;
}

PdfPoint plus(PdfPoint a, PdfPoint v, double k) noexcept
{
    return PdfPoint{a.x + v.x * k, a.y + v.y * k};
}

// The box a caption's glyphs sit in, below and above its baseline, in ems: Helvetica's descender and a little over
// its cap height.
constexpr double kCaptionBelow = 0.22;
constexpr double kCaptionAbove = 0.75;

QString firstLine(const QString& text)
{
    qsizetype at = 0;
    while (at < text.size() && text.at(at) != QLatin1Char('\r') && text.at(at) != QLatin1Char('\n')) ++at;
    return text.left(at).trimmed();
}

} // namespace

// ---- Units ---------------------------------------------------------------------------------------

std::string_view measureUnitToken(MeasureUnit unit) noexcept
{
    switch (unit) {
    case MeasureUnit::Points:      return "pt";
    case MeasureUnit::Inches:      return "in";
    case MeasureUnit::Feet:        return "ft";
    case MeasureUnit::Yards:       return "yd";
    case MeasureUnit::Miles:       return "mi";
    case MeasureUnit::Millimetres: return "mm";
    case MeasureUnit::Centimetres: return "cm";
    case MeasureUnit::Metres:      return "m";
    case MeasureUnit::Kilometres:  return "km";
    }
    return "in";
}

std::optional<MeasureUnit> measureUnitFromToken(std::string_view token) noexcept
{
    for (int i = 0; i < kMeasureUnitCount; ++i) {
        const auto unit = static_cast<MeasureUnit>(i);
        if (measureUnitToken(unit) == token) return unit;
    }
    return std::nullopt;
}

double pointsPerMeasureUnit(MeasureUnit unit) noexcept
{
    switch (unit) {
    case MeasureUnit::Points:      return 1.0;
    case MeasureUnit::Inches:      return 72.0;
    case MeasureUnit::Feet:        return 864.0;
    case MeasureUnit::Yards:       return 2592.0;
    case MeasureUnit::Miles:       return 4561920.0;
    case MeasureUnit::Millimetres: return 72.0 / 25.4;
    case MeasureUnit::Centimetres: return 720.0 / 25.4;
    case MeasureUnit::Metres:      return 72000.0 / 25.4;
    case MeasureUnit::Kilometres:  return 72000000.0 / 25.4;
    }
    return 72.0;
}

MeasureUnit paperUnitFor(MeasureUnit unit) noexcept
{
    switch (unit) {
    case MeasureUnit::Points: return MeasureUnit::Points;
    case MeasureUnit::Inches:
    case MeasureUnit::Feet:
    case MeasureUnit::Yards:
    case MeasureUnit::Miles: return MeasureUnit::Inches;
    case MeasureUnit::Millimetres:
    case MeasureUnit::Centimetres: return MeasureUnit::Millimetres;
    case MeasureUnit::Metres:
    case MeasureUnit::Kilometres: return MeasureUnit::Centimetres;
    }
    return MeasureUnit::Inches;
}

// ---- The scale -------------------------------------------------------------------------------------

bool MeasureScale::isValid() const noexcept
{
    return std::isfinite(ratio) && ratio >= kMinScaleRatio && ratio <= kMaxScaleRatio && precision >= 0
           && precision <= kMaxMeasurePrecision;
}

double MeasureScale::toUnits(double points) const noexcept
{
    return points * ratio / pointsPerMeasureUnit(unit);
}

QString scaleString(const MeasureScale& scale)
{
    const MeasureUnit paper = paperUnitFor(scale.unit);
    const double real = scale.ratio * pointsPerMeasureUnit(paper) / pointsPerMeasureUnit(scale.unit);
    return QStringLiteral("1 %1 = %2 %3")
        .arg(QLatin1String(measureUnitToken(paper).data()), trimmedNumber(real),
             QLatin1String(measureUnitToken(scale.unit).data()));
}

MeasureDictionary measureDictionaryFor(const MeasureScale& scale)
{
    int denominator = 1;
    for (int i = 0; i < std::clamp(scale.precision, 0, kMaxMeasurePrecision); ++i) denominator *= 10;
    const QString unit = QLatin1String(measureUnitToken(scale.unit).data());

    MeasureDictionary m;
    m.ratio = scaleString(scale);
    MeasureNumberFormat x;
    x.unit = unit;
    x.factor = scale.ratio / pointsPerMeasureUnit(scale.unit);
    x.denominator = denominator;
    MeasureNumberFormat d = x;
    d.factor = 1.0;
    MeasureNumberFormat a = d;
    a.unit = measureUnitLabel(MeasureKind::Area, scale.unit);
    m.x = {x};
    m.distance = {d};
    m.area = {a};
    return m;
}

Result<double> calibratedRatio(double measuredPoints, double known, MeasureUnit unit)
{
    if (!std::isfinite(measuredPoints) || !(measuredPoints > 0.0) || !std::isfinite(known) || !(known > 0.0))
        return measureError("error.measure.calibration");
    const double ratio = known * pointsPerMeasureUnit(unit) / measuredPoints;
    if (!std::isfinite(ratio) || ratio < kMinScaleRatio || ratio > kMaxScaleRatio)
        return measureError("error.measure.calibration", trimmedNumber(ratio));
    return ratio;
}

bool isBoundedMeasure(const MeasureDictionary& measure) noexcept
{
    if (measure.subtype.size() > 64 || measure.ratio.size() > 256) return false;
    for (const std::vector<MeasureNumberFormat>* chain : {&measure.x, &measure.y, &measure.distance, &measure.area}) {
        if (chain->size() > 16) return false;
        for (const MeasureNumberFormat& f : *chain) {
            if (f.unit.size() > 64 || f.thousands.size() > 8 || f.decimal.size() > 8) return false;
            // ANY finite factor: a file's `/C 0` is read and held (and never drawn -- `measured` refuses it), and a
            // command that moves such a measurement must read back what it wrote (S6 guide I-18's lesson).
            if (!std::isfinite(f.factor) || f.denominator < 1) return false;
        }
    }
    return true;
}

// ---- The arithmetic -------------------------------------------------------------------------------

double polylineLength(const std::vector<double>& xy, bool closed) noexcept
{
    const std::size_t n = xy.size() / 2;
    double length = 0.0;
    for (std::size_t i = 1; i < n; ++i)
        length += std::hypot(xy[2 * i] - xy[2 * i - 2], xy[2 * i + 1] - xy[2 * i - 1]);
    if (closed && n > 2) length += std::hypot(xy[0] - xy[2 * n - 2], xy[1] - xy[2 * n - 1]);
    return length;
}

bool selfIntersects(const std::vector<double>& xy) noexcept
{
    const std::vector<PdfPoint> p = corners(xy);
    const std::size_t n = p.size();
    if (n < 3 || n > kMaxAreaCorners) return n > kMaxAreaCorners;
    for (std::size_t i = 0; i < n; ++i) {
        const PdfPoint a = p[i], b = p[(i + 1) % n];
        for (std::size_t j = i + 1; j < n; ++j) {
            const PdfPoint c = p[j], d = p[(j + 1) % n];
            const bool adjacent = j == i + 1 || (i == 0 && j == n - 1);
            if (!adjacent) {
                if (segmentsMeet(a, b, c, d)) return true;
                continue;
            }
            // Two edges that share a corner meet THERE; they cross only by folding back along each other.
            const PdfPoint shared = j == i + 1 ? b : a;
            const PdfPoint u = j == i + 1 ? a : b;
            const PdfPoint w = j == i + 1 ? d : c;
            if (side(shared, u, w) == 0) {
                const double dot = (u.x - shared.x) * (w.x - shared.x) + (u.y - shared.y) * (w.y - shared.y);
                if (dot > 0.0) return true;
            }
        }
    }
    return false;
}

Result<double> polygonArea(const std::vector<double>& xy)
{
    if (xy.size() % 2 != 0 || !allFinite(xy)) return measureError("error.measure.geometry");
    const std::vector<PdfPoint> p = corners(xy);
    if (p.size() < 3) return measureError("error.measure.tooFewPoints");
    if (p.size() > kMaxAreaCorners) return measureError("error.measure.geometry");
    if (selfIntersects(xy)) return measureError("error.measure.selfIntersecting");
    double twice = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const PdfPoint a = p[i], b = p[(i + 1) % p.size()];
        twice += a.x * b.y - b.x * a.y;
    }
    return std::abs(twice) / 2.0;
}

// ---- Measurements as annotations --------------------------------------------------------------------

std::optional<MeasureKind> measureKindOf(const Annotation& a) noexcept
{
    if (a.subtype == AnnotationSubtype::Line && a.intent == QLatin1String("LineDimension")) return MeasureKind::Distance;
    if (a.subtype == AnnotationSubtype::PolyLine && a.intent == QLatin1String("PolyLineDimension"))
        return MeasureKind::Perimeter;
    if (a.subtype == AnnotationSubtype::Polygon && a.intent == QLatin1String("PolygonDimension")) return MeasureKind::Area;
    return std::nullopt;
}

QLatin1String measureIntent(MeasureKind kind) noexcept
{
    switch (kind) {
    case MeasureKind::Distance:  return QLatin1String("LineDimension");
    case MeasureKind::Perimeter: return QLatin1String("PolyLineDimension");
    case MeasureKind::Area:      return QLatin1String("PolygonDimension");
    }
    return QLatin1String("LineDimension");
}

AnnotationSubtype measureSubtype(MeasureKind kind) noexcept
{
    switch (kind) {
    case MeasureKind::Distance:  return AnnotationSubtype::Line;
    case MeasureKind::Perimeter: return AnnotationSubtype::PolyLine;
    case MeasureKind::Area:      return AnnotationSubtype::Polygon;
    }
    return AnnotationSubtype::Line;
}

std::optional<int> decimalsOfDenominator(int denominator) noexcept
{
    int decimals = 0;
    for (long long d = 1; d <= 1000000000LL; d *= 10, ++decimals) {
        if (d == denominator) return decimals;
    }
    return std::nullopt;
}

QString formatMeasureNumber(double value, int decimals, const QString& thousands, const QString& decimal)
{
    if (!std::isfinite(value)) return QString();
    // Through QString::number, which is the C locale's -- a reading is a number, and the separators are the
    // number format's to choose, never the person's desktop's.
    const QString plain = QString::number(std::abs(value), 'f', std::clamp(decimals, 0, 9));
    const qsizetype point = plain.indexOf(QLatin1Char('.'));
    const QString whole = point < 0 ? plain : plain.left(point);
    const QString fraction = point < 0 ? QString() : plain.mid(point + 1);
    QString grouped;
    for (qsizetype i = 0; i < whole.size(); ++i) {
        if (i > 0 && (whole.size() - i) % 3 == 0) grouped += thousands;
        grouped += whole.at(i);
    }
    QString out = (value < 0.0 && plain != QString(plain.size(), QLatin1Char('0')) ? QStringLiteral("-") : QString()) + grouped;
    if (!fraction.isEmpty()) out += decimal + fraction;
    return out;
}

QString measureUnitLabel(MeasureKind kind, MeasureUnit unit)
{
    const QString symbol = QLatin1String(measureUnitToken(unit).data());
    return kind == MeasureKind::Area ? QStringLiteral("sq ") + symbol : symbol;
}

Result<MeasureReading> measured(const Annotation& a)
{
    const std::optional<MeasureKind> kind = measureKindOf(a);
    if (!kind || !a.measure) return measureError("error.measure.notMeasurement");
    const MeasureDictionary& m = *a.measure;
    const MeasureNumberFormat* x = single(m.x);
    const MeasureNumberFormat* q = single(*kind == MeasureKind::Area ? m.area : m.distance);
    if (m.subtype != QLatin1String("RL") || !m.y.empty() || x == nullptr || q == nullptr || !displayable(*x)
        || !displayable(*q))
        return measureError("error.measure.unsupportedScale");

    double value = 0.0;
    switch (*kind) {
    case MeasureKind::Distance:
        if (a.line.size() != 4 || !allFinite(a.line)) return measureError("error.measure.geometry");
        value = std::hypot(a.line[2] - a.line[0], a.line[3] - a.line[1]) * x->factor * q->factor;
        break;
    case MeasureKind::Perimeter:
        if (a.vertices.size() < 4 || a.vertices.size() % 2 != 0 || !allFinite(a.vertices))
            return measureError("error.measure.geometry");
        value = polylineLength(a.vertices, false) * x->factor * q->factor;
        break;
    case MeasureKind::Area: {
        Result<double> area = polygonArea(a.vertices);
        if (!area) return area.error();
        value = area.value() * x->factor * x->factor * q->factor;
        break;
    }
    }
    MeasureReading reading;
    reading.value = value;
    reading.unit = q->unit;
    reading.text = formatMeasureNumber(value, *decimalsOfDenominator(q->denominator), q->thousands, q->decimal);
    if (!q->unit.isEmpty()) reading.text += QLatin1Char(' ') + q->unit;
    return reading;
}

Result<MeasureReading> measureAt(MeasureKind kind, const std::vector<double>& points, const MeasureScale& scale)
{
    if (!scale.isValid()) return measureError("error.measure.unsupportedScale");
    Annotation probe;
    probe.subtype = measureSubtype(kind);
    probe.intent = measureIntent(kind);
    if (kind == MeasureKind::Distance) probe.line = points;
    else probe.vertices = points;
    probe.measure = measureDictionaryFor(scale);
    return measured(probe);
}

Result<Annotation> newMeasurement(Annotation base, MeasureKind kind, const std::vector<double>& points,
                                  const MeasureScale& scale, double leaderLength, bool writeToComment)
{
    if (!scale.isValid()) return measureError("error.measure.unsupportedScale");
    if (!allFinite(points) || points.size() % 2 != 0) return measureError("error.measure.geometry");
    const std::size_t n = points.size() / 2;
    if (kind == MeasureKind::Distance ? n != 2 : n < (kind == MeasureKind::Area ? 3u : 2u))
        return measureError("error.measure.tooFewPoints");
    if (!std::isfinite(leaderLength)) return measureError("error.measure.geometry");

    Annotation a = std::move(base);
    a.subtype = measureSubtype(kind);
    a.subtypeName.clear();
    a.intent = measureIntent(kind);
    a.line.clear();
    a.vertices.clear();
    if (kind == MeasureKind::Distance) {
        a.line = points;
        // A dimension: arrows at both ends pointing out to the points measured, its reading on top of it.
        a.lineEndings = {QStringLiteral("ClosedArrow"), QStringLiteral("ClosedArrow")};
        a.interiorColor = a.color;   // `/IC` is what a Line's closed endings are filled with (ISO 32000-1 Table 175)
        a.leaderLength = leaderLength;
        // A dimension callout's leaders run a little past its line, as a draughtsman draws them (ISO's `/LLE`).
        a.leaderExtension = leaderLength != 0.0 ? kDimensionLeaderExtension : 0.0;
        a.caption = true;
        a.captionPosition = QStringLiteral("Top");
    } else {
        a.vertices = points;
    }
    a.measure = measureDictionaryFor(scale);
    Result<MeasureReading> reading = measured(a);
    if (!reading) return reading.error();
    if (reading.value().value <= 0.0) return measureError("error.measure.geometry");
    if (writeToComment) a.contents = reading.value().text;
    return a;
}

Annotation withFreshReading(const Annotation& before, Annotation after)
{
    if (!measureKindOf(after) || before.contents.isEmpty()) return after;
    const Result<MeasureReading> was = measured(before);
    if (!was || was.value().text != before.contents || after.contents != before.contents) return after;
    if (const Result<MeasureReading> now = measured(after)) after.contents = now.value().text;
    return after;
}

// ---- Where a measurement's parts are drawn ----------------------------------------------------------

double helveticaTextWidth(const QString& text, double size) noexcept
{
    double thousandths = 0.0;
    for (const QChar c : text) {
        if (const std::optional<std::uint8_t> code = winAnsiCode(c.unicode())) thousandths += helveticaWidth(*code);
    }
    return thousandths * size / 1000.0;
}

MeasureCaption captionAbove(PdfPoint a, PdfPoint b, const QString& text, double offset)
{
    MeasureCaption caption;
    caption.text = text;
    caption.width = helveticaTextWidth(text, caption.size);
    const double length = std::hypot(b.x - a.x, b.y - a.y);
    PdfPoint d = length > 1e-12 ? PdfPoint{(b.x - a.x) / length, (b.y - a.y) / length} : PdfPoint{1.0, 0.0};
    // UPRIGHT: the text runs left to right, or up a vertical line -- never upside down.
    if (d.x < -1e-9 || (std::abs(d.x) <= 1e-9 && d.y < 0.0)) d = PdfPoint{-d.x, -d.y};
    const PdfPoint up{-d.y, d.x};
    const PdfPoint middle{(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
    caption.direction = d;
    caption.origin = plus(plus(middle, d, -caption.width / 2.0), up, offset);
    const PdfPoint foot = plus(caption.origin, up, -kCaptionBelow * caption.size);
    const PdfPoint head = plus(caption.origin, up, kCaptionAbove * caption.size);
    caption.corners = {foot, plus(foot, d, caption.width), plus(head, d, caption.width), head};
    return caption;
}

std::optional<DimensionGeometry> dimensionGeometry(const Annotation& a)
{
    if (a.subtype != AnnotationSubtype::Line || a.line.size() != 4 || !allFinite(a.line)) return std::nullopt;
    const PdfPoint p1{a.line[0], a.line[1]}, p2{a.line[2], a.line[3]};
    const double length = std::hypot(p2.x - p1.x, p2.y - p1.y);
    if (!(length > 1e-9)) return std::nullopt;
    const PdfPoint d{(p2.x - p1.x) / length, (p2.y - p1.y) / length};
    const PdfPoint n{d.y, -d.x};   // clockwise from the line's direction: where a positive `/LL` puts the line

    const double ll = std::isfinite(a.leaderLength) ? a.leaderLength : 0.0;
    const double lle = std::isfinite(a.leaderExtension) ? std::max(a.leaderExtension, 0.0) : 0.0;
    const double llo = std::isfinite(a.leaderOffset) ? std::max(a.leaderOffset, 0.0) : 0.0;
    DimensionGeometry g;
    g.start = plus(p1, n, ll);
    g.end = plus(p2, n, ll);
    if (ll != 0.0 || lle > 0.0) {
        const double s = ll < 0.0 ? -1.0 : 1.0;
        for (const PdfPoint& p : {p1, p2}) {
            const PdfPoint from = plus(p, n, s * llo);
            const PdfPoint to = plus(p, n, ll + s * lle);
            g.leaders.insert(g.leaders.end(), {from.x, from.y, to.x, to.y});
        }
    }
    if (!a.caption) return g;

    QString text;
    if (const Result<MeasureReading> reading = measured(a)) text = reading.value().text;
    else text = firstLine(a.contents);
    if (text.isEmpty()) return g;

    const double half = (a.borderWidth && std::isfinite(*a.borderWidth) ? std::max(*a.borderWidth, 0.0) : 1.0) / 2.0;
    const bool top = a.captionPosition == QLatin1String("Top");
    const double size = kMeasureCaptionSize;
    const double offset = top ? half + kMeasureCaptionGap + kCaptionBelow * size
                              : -(kCaptionAbove - kCaptionBelow) * size / 2.0;
    MeasureCaption caption = captionAbove(g.start, g.end, text, offset);
    if (a.captionOffset.size() == 2 && allFinite(a.captionOffset)) {
        const PdfPoint up{-caption.direction.y, caption.direction.x};
        const PdfPoint shift{caption.direction.x * a.captionOffset[0] + up.x * a.captionOffset[1],
                             caption.direction.y * a.captionOffset[0] + up.y * a.captionOffset[1]};
        caption.origin = plus(caption.origin, shift, 1.0);
        for (PdfPoint& c : caption.corners) c = plus(c, shift, 1.0);
    }
    if (!top) {
        // The line is broken round the caption: a gap of the caption's width and a gap each side, about the point
        // of the line the caption's centre is over, clamped to the line.
        const PdfPoint centre = plus(caption.origin, caption.direction, caption.width / 2.0);
        const double along = (centre.x - g.start.x) * d.x + (centre.y - g.start.y) * d.y;
        const double halfGap = caption.width / 2.0 + kMeasureCaptionGap;
        const double from = std::clamp(along - halfGap, 0.0, length);
        const double to = std::clamp(along + halfGap, 0.0, length);
        g.gap = std::make_pair(plus(g.start, d, from), plus(g.start, d, to));
    }
    g.caption = std::move(caption);
    return g;
}

std::optional<MeasureCaption> measureLabel(const Annotation& a)
{
    const std::optional<MeasureKind> kind = measureKindOf(a);
    if (!kind || *kind == MeasureKind::Distance) return std::nullopt;
    const Result<MeasureReading> reading = measured(a);
    if (!reading) return std::nullopt;
    const std::size_t n = a.vertices.size() / 2;
    const double size = kMeasureCaptionSize;

    if (*kind == MeasureKind::Perimeter) {
        std::size_t longest = 1;
        double best = -1.0;
        for (std::size_t i = 1; i < n; ++i) {
            const double l = std::hypot(a.vertices[2 * i] - a.vertices[2 * i - 2], a.vertices[2 * i + 1] - a.vertices[2 * i - 1]);
            if (l > best) {
                best = l;
                longest = i;
            }
        }
        const double half = (a.borderWidth && std::isfinite(*a.borderWidth) ? std::max(*a.borderWidth, 0.0) : 1.0) / 2.0;
        return captionAbove(PdfPoint{a.vertices[2 * longest - 2], a.vertices[2 * longest - 1]},
                            PdfPoint{a.vertices[2 * longest], a.vertices[2 * longest + 1]}, reading.value().text,
                            half + kMeasureCaptionGap + kCaptionBelow * size);
    }

    // An area's reading, level, on the polygon's centroid -- the centre of its area, which lies inside a convex
    // figure; the mean of its corners for a degenerate one.
    double cx = 0.0, cy = 0.0, twice = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x0 = a.vertices[2 * i], y0 = a.vertices[2 * i + 1];
        const double x1 = a.vertices[(2 * i + 2) % (2 * n)], y1 = a.vertices[(2 * i + 3) % (2 * n)];
        const double c = x0 * y1 - x1 * y0;
        twice += c;
        cx += (x0 + x1) * c;
        cy += (y0 + y1) * c;
    }
    PdfPoint centre;
    if (std::abs(twice) > 1e-9) {
        centre = PdfPoint{cx / (3.0 * twice), cy / (3.0 * twice)};
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            centre.x += a.vertices[2 * i] / static_cast<double>(n);
            centre.y += a.vertices[2 * i + 1] / static_cast<double>(n);
        }
    }
    const double width = helveticaTextWidth(reading.value().text, size);
    const PdfPoint left{centre.x - width / 2.0, centre.y};
    return captionAbove(left, PdfPoint{centre.x + width / 2.0, centre.y}, reading.value().text,
                        -(kCaptionAbove - kCaptionBelow) * size / 2.0);
}

} // namespace rawpage::core
