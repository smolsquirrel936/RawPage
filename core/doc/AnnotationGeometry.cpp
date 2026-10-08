// core/doc/AnnotationGeometry -- implementation. Stage 6 T5. See the header for the one mapping and for
// why each kind is hit by its own shape.

#include "core/doc/AnnotationGeometry.h"

#include "core/doc/DrawnShapes.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace rawpage::core {
namespace {

bool finiteRect(const PdfRect& r)
{
    return std::isfinite(r.left()) && std::isfinite(r.bottom()) && std::isfinite(r.size.width)
           && std::isfinite(r.size.height) && r.size.width >= 0.0 && r.size.height >= 0.0;
}

// x' = to.left + (x - from.left) * sx, and likewise y. A side of no extent keeps a scale of one, so a
// zero-width box moved is a translation rather than a division by zero.
struct Mapping {
    double sx = 1.0, sy = 1.0;
    double fromX = 0.0, fromY = 0.0, toX = 0.0, toY = 0.0;

    double x(double v) const { return toX + (v - fromX) * sx; }
    double y(double v) const { return toY + (v - fromY) * sy; }
    void points(std::vector<double>& xy) const
    {
        for (std::size_t at = 0; at + 1 < xy.size(); at += 2) {
            xy[at] = x(xy[at]);
            xy[at + 1] = y(xy[at + 1]);
        }
    }
};

Mapping mappingBetween(const PdfRect& from, const PdfRect& to)
{
    Mapping m;
    m.sx = from.size.width > 0.0 ? to.size.width / from.size.width : 1.0;
    m.sy = from.size.height > 0.0 ? to.size.height / from.size.height : 1.0;
    m.fromX = from.left();
    m.fromY = from.bottom();
    m.toX = to.left();
    m.toY = to.bottom();
    return m;
}

bool near(const std::vector<double>& a, const std::vector<double>& b, double tolerance)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(std::abs(a[i] - b[i]) <= tolerance)) return false;
    }
    return true;
}

double distanceToSegment(PdfPoint p, double x1, double y1, double x2, double y2)
{
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double length2 = dx * dx + dy * dy;
    double t = length2 > 0.0 ? ((p.x - x1) * dx + (p.y - y1) * dy) / length2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const double cx = x1 + t * dx - p.x;
    const double cy = y1 + t * dy - p.y;
    return std::sqrt(cx * cx + cy * cy);
}

// The distance from `p` to the polyline through `xy` (x y pairs), closed when `closed`.
double distanceToPolyline(PdfPoint p, const std::vector<double>& xy, bool closed)
{
    const std::size_t n = xy.size() / 2;
    if (n == 0) return std::numeric_limits<double>::infinity();
    if (n == 1) return std::hypot(p.x - xy[0], p.y - xy[1]);
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i + 1 < n; ++i)
        best = std::min(best, distanceToSegment(p, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3]));
    if (closed) best = std::min(best, distanceToSegment(p, xy[2 * n - 2], xy[2 * n - 1], xy[0], xy[1]));
    return best;
}

// Even-odd point in polygon.
bool insidePolygon(PdfPoint p, const std::vector<double>& xy)
{
    const std::size_t n = xy.size() / 2;
    bool inside = false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double xi = xy[2 * i], yi = xy[2 * i + 1];
        const double xj = xy[2 * j], yj = xy[2 * j + 1];
        if (((yi > p.y) != (yj > p.y)) && (p.x < (xj - xi) * (p.y - yi) / (yj - yi) + xi)) inside = !inside;
    }
    return n >= 3 && inside;
}

// The box the outline is drawn on: the rectangle less `/RD`, and less half the border, which a producer
// strokes inside the rectangle so the whole line shows.
PdfRect outlineBox(const Annotation& a, double border)
{
    double left = a.rect.left(), bottom = a.rect.bottom(), right = a.rect.right(), top = a.rect.top();
    if (a.rectDifferences.size() == 4) {
        left += a.rectDifferences[0];
        bottom += a.rectDifferences[1];
        right -= a.rectDifferences[2];
        top -= a.rectDifferences[3];
    }
    left += border / 2.0;
    bottom += border / 2.0;
    right -= border / 2.0;
    top -= border / 2.0;
    if (right < left) left = right = (left + right) / 2.0;
    if (top < bottom) bottom = top = (bottom + top) / 2.0;
    return PdfRect{PdfPoint{left, bottom}, PdfSize{right - left, top - bottom}};
}

bool insideExpanded(const PdfRect& r, PdfPoint p, double by)
{
    return p.x >= r.left() - by && p.x <= r.right() + by && p.y >= r.bottom() - by && p.y <= r.top() + by;
}

// HOW FAR A CLOUD'S BUMPS REACH past the edges they are drawn along. Since T8 the model holds `/BE`, and a
// cloud of a known intensity reaches exactly as far as `core::cloudPath` draws its bumps -- the builder's own
// geometry (`core/doc/DrawnShapes`). A producer's cloud whose `/BE` says nothing, or a border that is not
// cloudy, falls back to the `/RD` a producer writes for one (ISO 32000-1 Table 177): the whole band its bumps
// may occupy, a decision (D6-30) rather than its ink.
double cloudReachOf(const Annotation& a)
{
    if (a.cloudIntensity && a.vertices.size() >= 6) return core::cloudReach(a.vertices, *a.cloudIntensity);
    if (a.rectDifferences.size() != 4) return 0.0;
    return std::max({a.rectDifferences[0], a.rectDifferences[1], a.rectDifferences[2], a.rectDifferences[3], 0.0});
}

// AN ENDING drawn at `tip` of a line coming from `from` (ISO 32000-1 Table 176): an arrow as the WEDGE its
// barbs span -- swept back thirty degrees, as long as `size` -- and a circle, a square, a diamond or a bar as
// the disk round the tip. A head's size is a producer's choice ISO does not fix; 4.5 times the border, and
// no less than 9 points, is the reader's convention and the corpus generator's (`corpus/make-annots.py`).
bool hitsEnding(PdfPoint p, double tipX, double tipY, double fromX, double fromY, const QString& ending, double border,
                double band)
{
    if (ending.isEmpty() || ending == QLatin1String("None")) return false;
    const double size = lineEndingSize(border);   // the builder's head (`core/doc/DrawnShapes`)
    const double length = std::hypot(fromX - tipX, fromY - tipY);
    if (length <= 0.0) return std::hypot(p.x - tipX, p.y - tipY) <= size / 2.0 + band;
    const double ux = (fromX - tipX) / length;
    const double uy = (fromY - tipY) / length;
    if (ending.endsWith(QLatin1String("Arrow"))) {
        // `ROpenArrow` and `RClosedArrow` point the other way: their barbs sweep forward, past the tip.
        const double sign = ending.startsWith(QLatin1Char('R')) ? -1.0 : 1.0;
        const double cos30 = 0.8660254037844386;
        const double sin30 = 0.5;
        const double bx = sign * ux;
        const double by = sign * uy;
        const std::vector<double> wedge{tipX, tipY,
                                        tipX + size * (bx * cos30 - by * sin30), tipY + size * (by * cos30 + bx * sin30),
                                        tipX + size * (bx * cos30 + by * sin30), tipY + size * (by * cos30 - bx * sin30)};
        return insidePolygon(p, wedge) || distanceToPolyline(p, wedge, true) <= band;
    }
    return std::hypot(p.x - tipX, p.y - tipY) <= size / 2.0 + band;
}

} // namespace

std::optional<Annotation> mappedInto(const Annotation& a, const PdfRect& to)
{
    if (!finiteRect(a.rect) || !finiteRect(to)) return std::nullopt;
    const Mapping m = mappingBetween(a.rect, to);
    Annotation out = a;
    out.rect = to;
    m.points(out.quadPoints);
    m.points(out.vertices);
    m.points(out.line);
    m.points(out.callout);
    for (std::vector<double>& stroke : out.inkList) m.points(stroke);
    if (out.rectDifferences.size() == 4) {
        out.rectDifferences[0] *= m.sx;
        out.rectDifferences[1] *= m.sy;
        out.rectDifferences[2] *= m.sx;
        out.rectDifferences[3] *= m.sy;
    }
    return out;
}

Annotation translated(const Annotation& a, double dx, double dy)
{
    Annotation out = a;
    out.rect.origin = PdfPoint{a.rect.left() + dx, a.rect.bottom() + dy};
    const auto shift = [dx, dy](std::vector<double>& xy) {
        for (std::size_t at = 0; at + 1 < xy.size(); at += 2) {
            xy[at] += dx;
            xy[at + 1] += dy;
        }
    };
    shift(out.quadPoints);
    shift(out.vertices);
    shift(out.line);
    shift(out.callout);
    for (std::vector<double>& stroke : out.inkList) shift(stroke);
    return out;
}

bool isMappedCopy(const Annotation& before, const Annotation& after, double tolerance)
{
    const std::optional<Annotation> carried = mappedInto(before, after.rect);
    if (!carried) return false;
    if (!near(carried->quadPoints, after.quadPoints, tolerance) || !near(carried->vertices, after.vertices, tolerance)
        || !near(carried->line, after.line, tolerance) || !near(carried->callout, after.callout, tolerance)
        || !near(carried->rectDifferences, after.rectDifferences, tolerance)
        || carried->inkList.size() != after.inkList.size())
        return false;
    for (std::size_t i = 0; i < after.inkList.size(); ++i) {
        if (!near(carried->inkList[i], after.inkList[i], tolerance)) return false;
    }
    // Every other field the same: the geometry is compared above, and `/M` is the edit's own stamp.
    Annotation a = *carried;
    Annotation b = after;
    a.quadPoints = b.quadPoints;
    a.vertices = b.vertices;
    a.line = b.line;
    a.callout = b.callout;
    a.rectDifferences = b.rectDifferences;
    a.inkList = b.inkList;
    a.modified = b.modified;
    return a == b;
}

bool isNoMove(double dx, double dy)
{
    return !std::isfinite(dx) || !std::isfinite(dy) || (dx == 0.0 && dy == 0.0);
}

bool isResizable(AnnotationSubtype subtype)
{
    switch (subtype) {
    case AnnotationSubtype::Square:
    case AnnotationSubtype::Circle:
    case AnnotationSubtype::FreeText:
    case AnnotationSubtype::Stamp:
    case AnnotationSubtype::Ink:
    case AnnotationSubtype::Polygon:
    case AnnotationSubtype::PolyLine:
    case AnnotationSubtype::Line:
        return true;
    default:
        return false;
    }
}

double drawnBorderWidth(const Annotation& a)
{
    if (a.borderWidth && std::isfinite(*a.borderWidth) && *a.borderWidth >= 0.0) return *a.borderWidth;
    return 1.0;
}

bool hitsAnnotation(const Annotation& a, PdfPoint p, double tolerance)
{
    if (a.hasFlag(AnnotationFlag::Hidden) || a.hasFlag(AnnotationFlag::NoView)) return false;
    if (a.subtype == AnnotationSubtype::Popup || !a.rectFinite || !finiteRect(a.rect)) return false;
    tolerance = std::max(0.0, tolerance);
    const double border = drawnBorderWidth(a);
    const double band = border / 2.0 + tolerance;

    switch (a.subtype) {
    case AnnotationSubtype::Highlight:
    case AnnotationSubtype::Underline:
    case AnnotationSubtype::StrikeOut:
    case AnnotationSubtype::Squiggly:
    case AnnotationSubtype::Redact: {
        // The words marked, quadrilateral by quadrilateral; a mark with none is its rectangle.
        if (a.quadPoints.size() < 8) return insideExpanded(a.rect, p, tolerance);
        for (std::size_t q = 0; q + 8 <= a.quadPoints.size(); q += 8) {
            const std::vector<double> quad(a.quadPoints.begin() + static_cast<std::ptrdiff_t>(q),
                                           a.quadPoints.begin() + static_cast<std::ptrdiff_t>(q + 8));
            // Acrobat writes TL TR BL BR, ISO's figure BL BR TR TL: put the corners round the centre.
            std::vector<double> ring{quad[0], quad[1], quad[2], quad[3], quad[6], quad[7], quad[4], quad[5]};
            if (insidePolygon(p, ring) || distanceToPolyline(p, ring, true) <= tolerance) return true;
            std::vector<double> iso{quad[0], quad[1], quad[2], quad[3], quad[4], quad[5], quad[6], quad[7]};
            if (insidePolygon(p, iso)) return true;
        }
        return false;
    }
    case AnnotationSubtype::Square: {
        const PdfRect box = outlineBox(a, border);
        if (!a.interiorColor.empty() && insideExpanded(box, p, band)) return true;
        const std::vector<double> ring{box.left(), box.bottom(), box.right(), box.bottom(),
                                       box.right(), box.top(),    box.left(),  box.top()};
        return distanceToPolyline(p, ring, true) <= band;
    }
    case AnnotationSubtype::Circle: {
        const PdfRect box = outlineBox(a, border);
        const double rx = box.size.width / 2.0;
        const double ry = box.size.height / 2.0;
        const double cx = box.left() + rx;
        const double cy = box.bottom() + ry;
        const double dx = p.x - cx;
        const double dy = p.y - cy;
        if (rx <= 0.0 || ry <= 0.0) return std::hypot(dx, dy) <= band;
        const double f = std::sqrt((dx / rx) * (dx / rx) + (dy / ry) * (dy / ry));
        if (!a.interiorColor.empty() && f <= 1.0) return true;
        // Along the ray from the centre: how far the point is from where that ray crosses the ellipse.
        const double radial = std::hypot(dx, dy);
        const double off = f > 0.0 ? std::abs(radial - radial / f) : std::min(rx, ry);
        return off <= band;
    }
    case AnnotationSubtype::Line: {
        if (a.line.size() < 4) return insideExpanded(a.rect, p, tolerance);
        if (distanceToSegment(p, a.line[0], a.line[1], a.line[2], a.line[3]) <= band) return true;
        // Its endings, at each end as `/LE` names them.
        const QString start = a.lineEndings.size() > 0 ? a.lineEndings[0] : QString();
        const QString end = a.lineEndings.size() > 1 ? a.lineEndings[1] : QString();
        return hitsEnding(p, a.line[0], a.line[1], a.line[2], a.line[3], start, border, band)
               || hitsEnding(p, a.line[2], a.line[3], a.line[0], a.line[1], end, border, band);
    }
    case AnnotationSubtype::Polygon:
    case AnnotationSubtype::PolyLine: {
        if (a.vertices.size() < 4) return insideExpanded(a.rect, p, tolerance);
        const bool closed = a.subtype == AnnotationSubtype::Polygon;
        if (closed && !a.interiorColor.empty() && insidePolygon(p, a.vertices)) return true;
        if (distanceToPolyline(p, a.vertices, closed) <= band + cloudReachOf(a)) return true;
        if (closed || a.lineEndings.empty()) return false;
        const std::size_t n = a.vertices.size();
        const QString start = a.lineEndings.size() > 0 ? a.lineEndings[0] : QString();
        const QString end = a.lineEndings.size() > 1 ? a.lineEndings[1] : QString();
        return hitsEnding(p, a.vertices[0], a.vertices[1], a.vertices[2], a.vertices[3], start, border, band)
               || hitsEnding(p, a.vertices[n - 2], a.vertices[n - 1], a.vertices[n - 4], a.vertices[n - 3], end, border, band);
    }
    case AnnotationSubtype::Ink: {
        if (a.inkList.empty()) return insideExpanded(a.rect, p, tolerance);
        for (const std::vector<double>& stroke : a.inkList) {
            if (distanceToPolyline(p, stroke, false) <= band) return true;
        }
        return false;
    }
    default:
        // A box: a note's icon, a stamp, free text, a caret, an attachment's pin, a sound, a link.
        return insideExpanded(a.rect, p, tolerance);
    }
}

} // namespace rawpage::core
