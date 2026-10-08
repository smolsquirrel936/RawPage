// core/doc/DrawnShapes -- implementation. Stage 6 T8. See the header for why each shape is computed once.

#include "core/doc/DrawnShapes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rawpage::core {
namespace {

constexpr double kPi = 3.14159265358979323846;
// A quarter circle as a cubic: the control points this far along the tangents, as a fraction of the radius.
constexpr double kKappa = 0.5522847498307936;

struct Vec {
    double x = 0.0, y = 0.0;
};

Vec operator+(Vec a, Vec b) { return {a.x + b.x, a.y + b.y}; }
Vec operator-(Vec a, Vec b) { return {a.x - b.x, a.y - b.y}; }
Vec operator*(Vec a, double s) { return {a.x * s, a.y * s}; }
double length(Vec v) { return std::hypot(v.x, v.y); }
Vec rotated(Vec v, double radians)
{
    const double c = std::cos(radians), s = std::sin(radians);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}
PdfPoint point(Vec v) { return PdfPoint{v.x, v.y}; }
Vec vec(PdfPoint p) { return {p.x, p.y}; }

bool finitePoints(const std::vector<double>& xy)
{
    return std::all_of(xy.begin(), xy.end(), [](double v) { return std::isfinite(v); });
}

double distanceToSegment(Vec p, Vec a, Vec b)
{
    const Vec d = b - a;
    const double length2 = d.x * d.x + d.y * d.y;
    double t = length2 > 0.0 ? ((p.x - a.x) * d.x + (p.y - a.y) * d.y) / length2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    return length(a + d * t - p);
}

// Whether segments ab and cd cross (properly, or touching).
bool segmentsCross(Vec a, Vec b, Vec c, Vec d)
{
    const auto cross = [](Vec o, Vec p, Vec q) { return (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x); };
    const double d1 = cross(c, d, a), d2 = cross(c, d, b), d3 = cross(a, b, c), d4 = cross(a, b, d);
    return ((d1 > 0.0) != (d2 > 0.0)) && ((d3 > 0.0) != (d4 > 0.0)) && d1 != 0.0 && d2 != 0.0 && d3 != 0.0 && d4 != 0.0;
}

// Bounds of x y pairs, grown into `into`.
void include(double& left, double& bottom, double& right, double& top, double x, double y)
{
    left = std::min(left, x);
    right = std::max(right, x);
    bottom = std::min(bottom, y);
    top = std::max(top, y);
}

std::optional<PdfRect> grown(double left, double bottom, double right, double top, double by)
{
    if (!(left <= right) || !(bottom <= top)) return std::nullopt;
    return normalizedRect(left - by, bottom - by, right + by, top + by);
}

// The margin a stroke of `width` and its anti-aliasing need past the geometry it is drawn on.
double strokeMargin(const Annotation& a)
{
    const double width = a.borderWidth && std::isfinite(*a.borderWidth) && *a.borderWidth >= 0.0 ? *a.borderWidth : 1.0;
    return width / 2.0 + 1.0;
}

double borderOf(const Annotation& a)
{
    return a.borderWidth && std::isfinite(*a.borderWidth) && *a.borderWidth >= 0.0 ? *a.borderWidth : 1.0;
}

} // namespace

// ---- Line endings ---------------------------------------------------------------------------------

bool isLineEndingName(const QString& name)
{
    static const char* const kNames[] = {"None",   "Square",    "Circle",       "Diamond",      "OpenArrow",
                                         "ClosedArrow", "Butt", "ROpenArrow", "RClosedArrow", "Slash"};
    for (const char* known : kNames) {
        if (name == QLatin1String(known)) return true;
    }
    return false;
}

double lineEndingSize(double width)
{
    return std::max(9.0, 4.5 * (std::isfinite(width) && width > 0.0 ? width : 0.0));
}

std::optional<EndingShape> lineEnding(const QString& name, PdfPoint tipPoint, PdfPoint fromPoint, double size,
                                      double spreadDegrees)
{
    if (name.isEmpty() || name == QLatin1String("None") || !isLineEndingName(name)) return std::nullopt;
    const Vec tip = vec(tipPoint);
    const Vec back = vec(fromPoint) - tip;
    const double len = length(back);
    if (!(len > 1e-9) || !std::isfinite(len) || !(size > 0.0)) return std::nullopt;
    const Vec u = back * (1.0 / len);          // from the tip back along the shaft
    const Vec n{-u.y, u.x};                     // across it
    EndingShape shape;
    const auto add = [&shape](Vec v) {
        shape.points.push_back(v.x);
        shape.points.push_back(v.y);
    };

    if (name.endsWith(QLatin1String("Arrow"))) {
        // `ROpenArrow` and `RClosedArrow` point the other way: their barbs sweep forward, past the tip.
        const Vec along = name.startsWith(QLatin1Char('R')) ? u * -1.0 : u;
        const double spread = spreadDegrees * kPi / 180.0;
        add(tip + rotated(along, spread) * size);
        add(tip);
        add(tip + rotated(along, -spread) * size);
        shape.kind = name.contains(QLatin1String("Closed")) ? EndingShape::Kind::Closed : EndingShape::Kind::Open;
        return shape;
    }
    if (name == QLatin1String("Circle")) {
        shape.kind = EndingShape::Kind::Circle;
        shape.centre = point(tip);
        shape.radius = 0.3 * size;
        return shape;
    }
    if (name == QLatin1String("Square")) {
        const double h = 0.3 * size;   // half a side: its corners 0.42 of the size from the tip
        add(tip + u * h + n * h);
        add(tip - u * h + n * h);
        add(tip - u * h - n * h);
        add(tip + u * h - n * h);
        shape.kind = EndingShape::Kind::Closed;
        return shape;
    }
    if (name == QLatin1String("Diamond")) {
        const double h = 0.4 * size;
        add(tip + u * h);
        add(tip + n * h);
        add(tip - u * h);
        add(tip - n * h);
        shape.kind = EndingShape::Kind::Closed;
        return shape;
    }
    // A bar across the tip: square to the shaft (`Butt`), or turned 30 degrees from square (`Slash`).
    const Vec across = name == QLatin1String("Slash") ? rotated(n, 30.0 * kPi / 180.0) : n;
    add(tip + across * (0.4 * size));
    add(tip - across * (0.4 * size));
    shape.kind = EndingShape::Kind::Open;
    return shape;
}

// ---- Curves -----------------------------------------------------------------------------------------

std::vector<CubicSegment> inkCurve(const std::vector<double>& stroke)
{
    std::vector<CubicSegment> out;
    const std::size_t n = stroke.size() / 2;
    if (n < 2 || !finitePoints(stroke)) return out;
    const auto at = [&stroke](std::size_t i) { return Vec{stroke[2 * i], stroke[2 * i + 1]}; };
    out.reserve(n - 1);
    // Centripetal (alpha 1/2): each knot interval is the square root of the chord. The ends repeat their
    // sample, which makes the first and last control point the sample itself (d3's `curveCatmullRom`).
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const Vec p1 = at(i), p2 = at(i + 1);
        const Vec p0 = i > 0 ? at(i - 1) : p1;
        const Vec p3 = i + 2 < n ? at(i + 2) : p2;
        const double l01 = std::sqrt(length(p1 - p0)), l12 = std::sqrt(length(p2 - p1)), l23 = std::sqrt(length(p3 - p2));
        Vec c1 = p1, c2 = p2;
        constexpr double kEpsilon = 1e-12;
        if (l01 > kEpsilon) {
            const double a = 2.0 * l01 * l01 + 3.0 * l01 * l12 + l12 * l12;
            const double d = 3.0 * l01 * (l01 + l12);
            c1 = (p1 * a - p0 * (l12 * l12) + p2 * (l01 * l01)) * (1.0 / d);
        }
        if (l23 > kEpsilon) {
            const double b = 2.0 * l23 * l23 + 3.0 * l23 * l12 + l12 * l12;
            const double d = 3.0 * l23 * (l23 + l12);
            c2 = (p2 * b + p1 * (l23 * l23) - p3 * (l12 * l12)) * (1.0 / d);
        }
        out.push_back(CubicSegment{point(p1), point(c1), point(c2), point(p2)});
    }
    return out;
}

std::vector<double> dedupedSamples(const std::vector<double>& xy, double minimum)
{
    std::vector<double> out;
    if (xy.size() < 2) return out;
    out.reserve(xy.size());
    for (std::size_t at = 0; at + 1 < xy.size(); at += 2) {
        if (!std::isfinite(xy[at]) || !std::isfinite(xy[at + 1])) continue;
        if (!out.empty()) {
            const double dx = xy[at] - out[out.size() - 2];
            const double dy = xy[at + 1] - out[out.size() - 1];
            if (std::hypot(dx, dy) <= minimum) continue;
        }
        out.push_back(xy[at]);
        out.push_back(xy[at + 1]);
    }
    return out;
}

// ---- Clouds -----------------------------------------------------------------------------------------

double cloudBump(double intensity)
{
    const double i = std::isfinite(intensity) ? std::clamp(intensity, 0.0, 2.0) : 0.0;
    return 5.0 + 5.0 * i;
}

namespace {

// Each edge's spans: its direction, its outward normal, its span count and step.
struct CloudEdge {
    Vec start, u, n;
    int count = 1;
    double step = 0.0;
};

std::vector<CloudEdge> cloudEdges(const std::vector<double>& vertices, double intensity)
{
    std::vector<CloudEdge> edges;
    const std::size_t n = vertices.size() / 2;
    if (n < 3 || !finitePoints(vertices)) return edges;
    // The polygon's winding, by its signed area: the bumps go on the side away from its inside.
    double area = 0.0;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++)
        area += vertices[2 * j] * vertices[2 * i + 1] - vertices[2 * i] * vertices[2 * j + 1];
    if (!(std::abs(area) > 1e-9)) return edges;
    const double bump = cloudBump(intensity);
    for (std::size_t i = 0; i < n; ++i) {
        const Vec a{vertices[2 * i], vertices[2 * i + 1]};
        const Vec b{vertices[2 * ((i + 1) % n)], vertices[2 * ((i + 1) % n) + 1]};
        const double span = length(b - a);
        if (!(span > 1e-9)) continue;
        CloudEdge edge;
        edge.start = a;
        edge.u = (b - a) * (1.0 / span);
        // Counter-clockwise (positive area): the inside is on the left, so outward is the right-hand normal.
        edge.n = area > 0.0 ? Vec{edge.u.y, -edge.u.x} : Vec{-edge.u.y, edge.u.x};
        edge.count = std::max(1, static_cast<int>(std::floor(span / bump)));
        edge.step = span / edge.count;
        edges.push_back(edge);
    }
    return edges;
}

} // namespace

std::vector<CubicSegment> cloudPath(const std::vector<double>& vertices, double intensity)
{
    std::vector<CubicSegment> out;
    for (const CloudEdge& edge : cloudEdges(vertices, intensity)) {
        const double radius = edge.step / 2.0;
        const double k = radius * kKappa;
        for (int part = 0; part < edge.count; ++part) {
            const Vec a = edge.start + edge.u * (edge.step * part);
            const Vec b = a + edge.u * edge.step;
            const Vec top = (a + b) * 0.5 + edge.n * radius;
            out.push_back(CubicSegment{point(a), point(a + edge.n * k), point(top - edge.u * k), point(top)});
            out.push_back(CubicSegment{point(top), point(top + edge.u * k), point(b + edge.n * k), point(b)});
        }
    }
    return out;
}

double cloudReach(const std::vector<double>& vertices, double intensity)
{
    double reach = 0.0;
    for (const CloudEdge& edge : cloudEdges(vertices, intensity)) reach = std::max(reach, edge.step / 2.0);
    return reach;
}

// ---- Rectangles ---------------------------------------------------------------------------------------

std::optional<Annotation> fittedShape(Annotation a)
{
    double left = std::numeric_limits<double>::infinity(), bottom = left;
    double right = -left, top = -left;
    const double margin = strokeMargin(a);
    const auto endings = [&](PdfPoint tip, PdfPoint from, const QString& name) {
        const std::optional<EndingShape> shape = lineEnding(name, tip, from, lineEndingSize(borderOf(a)));
        if (!shape) return;
        if (shape->kind == EndingShape::Kind::Circle) {
            include(left, bottom, right, top, shape->centre.x - shape->radius, shape->centre.y - shape->radius);
            include(left, bottom, right, top, shape->centre.x + shape->radius, shape->centre.y + shape->radius);
            return;
        }
        for (std::size_t at = 0; at + 1 < shape->points.size(); at += 2)
            include(left, bottom, right, top, shape->points[at], shape->points[at + 1]);
    };

    switch (a.subtype) {
    case AnnotationSubtype::Square:
    case AnnotationSubtype::Circle:
        if (!a.rectFinite || !(a.rect.size.width > 0.0) || !(a.rect.size.height > 0.0)) return std::nullopt;
        return a;
    case AnnotationSubtype::Line: {
        if (a.line.size() != 4 || !finitePoints(a.line)) return std::nullopt;
        const PdfPoint start{a.line[0], a.line[1]}, end{a.line[2], a.line[3]};
        if (!(std::hypot(end.x - start.x, end.y - start.y) > 1e-9)) return std::nullopt;
        include(left, bottom, right, top, start.x, start.y);
        include(left, bottom, right, top, end.x, end.y);
        endings(start, end, a.lineEndings.size() > 0 ? a.lineEndings[0] : QString());
        endings(end, start, a.lineEndings.size() > 1 ? a.lineEndings[1] : QString());
        break;
    }
    case AnnotationSubtype::Polygon:
    case AnnotationSubtype::PolyLine: {
        const bool closed = a.subtype == AnnotationSubtype::Polygon;
        const std::size_t n = a.vertices.size() / 2;
        if (a.vertices.size() % 2 != 0 || n < (closed ? 3u : 2u) || !finitePoints(a.vertices)) return std::nullopt;
        for (std::size_t i = 0; i < n; ++i) include(left, bottom, right, top, a.vertices[2 * i], a.vertices[2 * i + 1]);
        if (!(right - left > 1e-9) && !(top - bottom > 1e-9)) return std::nullopt;
        if (closed && a.cloudIntensity) {
            if (cloudPath(a.vertices, *a.cloudIntensity).empty()) return std::nullopt;
            // The bumps reach past the edges: the rectangle holds them.
            const double reach = cloudReach(a.vertices, *a.cloudIntensity);
            left -= reach;
            bottom -= reach;
            right += reach;
            top += reach;
        }
        if (!closed) {
            endings(PdfPoint{a.vertices[0], a.vertices[1]}, PdfPoint{a.vertices[2], a.vertices[3]},
                    a.lineEndings.size() > 0 ? a.lineEndings[0] : QString());
            endings(PdfPoint{a.vertices[2 * n - 2], a.vertices[2 * n - 1]}, PdfPoint{a.vertices[2 * n - 4], a.vertices[2 * n - 3]},
                    a.lineEndings.size() > 1 ? a.lineEndings[1] : QString());
        }
        break;
    }
    case AnnotationSubtype::Ink: {
        bool any = false;
        for (const std::vector<double>& stroke : a.inkList) {
            if (stroke.size() < 2 || stroke.size() % 2 != 0 || !finitePoints(stroke)) return std::nullopt;
            any = true;
            for (std::size_t at = 0; at + 1 < stroke.size(); at += 2) include(left, bottom, right, top, stroke[at], stroke[at + 1]);
            // The curve's control points: the curve lies inside their hull, so inside these bounds.
            for (const CubicSegment& s : inkCurve(stroke)) {
                include(left, bottom, right, top, s.c1.x, s.c1.y);
                include(left, bottom, right, top, s.c2.x, s.c2.y);
            }
        }
        if (!any) return std::nullopt;
        break;
    }
    default:
        return a;   // not a drawn shape: its rectangle is its own
    }
    const std::optional<PdfRect> rect = grown(left, bottom, right, top, margin);
    if (!rect) return std::nullopt;
    a.rect = *rect;
    a.rectFinite = true;
    return a;
}

// ---- Gestures ---------------------------------------------------------------------------------------------

PdfPoint constrainedTo45(PdfPoint from, PdfPoint to)
{
    const double dx = to.x - from.x, dy = to.y - from.y;
    if (!(std::hypot(dx, dy) > 0.0)) return to;
    const double step = kPi / 4.0;
    const double angle = std::round(std::atan2(dy, dx) / step) * step;
    const double ux = std::cos(angle), uy = std::sin(angle);
    const double along = dx * ux + dy * uy;
    // Exact zeros on the axes: `cos(pi/2)` is 6e-17, and a vertical line should be vertical.
    const auto clean = [](double v) { return std::abs(v) < 1e-12 ? 0.0 : v; };
    return PdfPoint{from.x + clean(ux) * along, from.y + clean(uy) * along};
}

PdfPoint constrainedSquare(PdfPoint from, PdfPoint to)
{
    const double dx = to.x - from.x, dy = to.y - from.y;
    const double side = std::max(std::abs(dx), std::abs(dy));
    return PdfPoint{from.x + (dx < 0.0 ? -side : side), from.y + (dy < 0.0 ? -side : side)};
}

ErasedInk erasedStrokes(const std::vector<std::vector<double>>& strokes, const std::vector<double>& path, double radius)
{
    ErasedInk out;
    const std::size_t pathPoints = path.size() / 2;
    if (pathPoints == 0 || !finitePoints(path) || !(radius >= 0.0)) {
        out.strokes = strokes;
        return out;
    }
    const auto pathAt = [&path](std::size_t i) { return Vec{path[2 * i], path[2 * i + 1]}; };
    const auto covered = [&](Vec p) {
        if (pathPoints == 1) return length(p - pathAt(0)) <= radius;
        for (std::size_t i = 0; i + 1 < pathPoints; ++i) {
            if (distanceToSegment(p, pathAt(i), pathAt(i + 1)) <= radius) return true;
        }
        return false;
    };
    const auto crossed = [&](Vec a, Vec b) {
        for (std::size_t i = 0; i + 1 < pathPoints; ++i) {
            if (segmentsCross(a, b, pathAt(i), pathAt(i + 1))) return true;
        }
        return false;
    };

    for (const std::vector<double>& stroke : strokes) {
        const std::size_t n = stroke.size() / 2;
        if (n == 0) continue;
        const auto at = [&stroke](std::size_t i) { return Vec{stroke[2 * i], stroke[2 * i + 1]}; };
        if (n == 1) {   // a dot: kept unless covered
            if (covered(at(0))) out.changed = true;
            else out.strokes.push_back(stroke);
            continue;
        }
        std::vector<std::vector<double>> pieces;
        std::vector<double> piece;
        bool cut = false;
        for (std::size_t i = 0; i < n; ++i) {
            const Vec p = at(i);
            if (covered(p)) {
                cut = true;
                if (!piece.empty()) pieces.push_back(std::move(piece));
                piece.clear();
                continue;
            }
            // The eraser crossed between the last sample and this one without covering either: split there.
            if (!piece.empty() && i > 0 && crossed(at(i - 1), p)) {
                cut = true;
                pieces.push_back(std::move(piece));
                piece.clear();
            }
            piece.push_back(p.x);
            piece.push_back(p.y);
        }
        if (!piece.empty()) pieces.push_back(std::move(piece));
        if (!cut) {
            out.strokes.push_back(stroke);
            continue;
        }
        out.changed = true;
        for (std::vector<double>& kept : pieces) {
            if (kept.size() >= 4) out.strokes.push_back(std::move(kept));   // a single sample left is a fragment
        }
    }
    return out;
}

} // namespace rawpage::core
