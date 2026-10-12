// pdf/ops/AppearanceStreamBuilder -- implementation. Stage 6 T3 (Highlight); T6 the rest of the text
// markup, the caret and the note; T7 free text; T8 the drawing kinds and ink; T10 stamps. See the header for why we draw
// every appearance, and how each is drawn.

#include "pdf/ops/AppearanceStreamBuilder.h"

#include "core/doc/AnnotationGeometry.h"
#include "core/doc/DrawnShapes.h"
#include "core/doc/Stamps.h"
#include "pdf/cos/ContentStreamBuilder.h"

#include <QLatin1String>
#include <QString>

#include <algorithm>
#include <array>
#include <cmath>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;

core::Error appearanceError(const char* key, QLatin1String subtype)
{
    return core::Error{core::ErrorCode::Unsupported, QLatin1String(key), QString(subtype)};
}

CosObject numbers(std::initializer_list<double> values)
{
    std::vector<CosObject> items;
    for (const double v : values) items.push_back(CosObject::makeReal(v));
    return CosObject::makeArray(std::move(items));
}

// The form XObject over `bbox`, with one ExtGState named GS0 -- a STREAM object, whose data descriptor
// is empty: `IncrementalWriter` writes the data it is handed beside it, and writes an object that is
// not a stream as a bare dictionary, data dropped. (That was this function's first defect, caught by
// `RevisionWriter::verify`'s new check before any case asserted the stream: guide case A-30's mechanism.)
CosObject formDictionary(const core::PdfRect& bbox, CosObject extGState, std::vector<CosObject::Entry> fonts = {})
{
    std::vector<CosObject::Entry> states{{"GS0", std::move(extGState)}};
    std::vector<CosObject::Entry> resources{{"ExtGState", CosObject::makeDictionary(std::move(states))}};
    if (!fonts.empty()) resources.push_back({"Font", CosObject::makeDictionary(std::move(fonts))});
    return CosObject::makeStream(std::vector<CosObject::Entry>{
        {"Type", CosObject::makeName("XObject")},
        {"Subtype", CosObject::makeName("Form")},
        {"FormType", CosObject::makeInteger(1)},
        {"BBox", numbers({bbox.left(), bbox.bottom(), bbox.right(), bbox.top()})},
        {"Resources", CosObject::makeDictionary(std::move(resources))},
    }, cos::CosStreamData{});
}

// The ExtGState every appearance draws through: its opacity as `/ca` and `/CA`, and -- when it is not `Normal` --
// its blend mode as `/BM` (Stage 6 T8: any of ISO 32000-2 Table 136's, where T3 had only a highlight's multiply).
CosObject opacityState(double opacity, const QString& blend)
{
    std::vector<CosObject::Entry> entries{{"Type", CosObject::makeName("ExtGState")}};
    if (!blend.isEmpty()) entries.push_back({"BM", CosObject::makeName(blend.toStdString())});
    entries.push_back({"ca", CosObject::makeReal(opacity)});
    entries.push_back({"CA", CosObject::makeReal(opacity)});
    return CosObject::makeDictionary(std::move(entries));
}

// The colour an annotation is drawn in: its `/C`, or `fallback` when it has none. Nullopt for a count
// no device space has (2, 5 …) -- refused rather than guessed.
std::optional<std::vector<double>> colourOf(const core::Annotation& a, std::vector<double> fallback)
{
    std::vector<double> colour = a.color.empty() ? std::move(fallback) : a.color;
    if (colour.size() != 1 && colour.size() != 3 && colour.size() != 4) return std::nullopt;
    for (const double v : colour) {
        if (!std::isfinite(v)) return std::nullopt;
    }
    return colour;
}

// The blend an appearance is drawn with: the annotation's `/BM` when it names one, else the kind's own --
// `Multiply` for a highlight, which keeps the text under it black, and `Normal` for everything else. `Normal`
// (and the deprecated `Compatible`) is written as nothing. Nullopt for a name ISO does not give.
std::optional<QString> blendOf(const core::Annotation& a)
{
    QString blend = a.blendMode;
    if (blend.isEmpty() && a.subtype == core::AnnotationSubtype::Highlight) blend = QStringLiteral("Multiply");
    if (blend.isEmpty() || blend == QLatin1String("Normal") || blend == QLatin1String("Compatible")) return QString();
    if (!core::isBlendModeName(blend)) return std::nullopt;
    return blend;
}

double opacityOf(const core::Annotation& a)
{
    const double opacity = a.opacity.value_or(1.0);
    return std::isfinite(opacity) ? std::clamp(opacity, 0.0, 1.0) : 1.0;
}

bool finiteRect(const core::Annotation& a)
{
    return a.rectFinite && std::isfinite(a.rect.left()) && std::isfinite(a.rect.bottom())
           && std::isfinite(a.rect.right()) && std::isfinite(a.rect.top());
}

// Whole quadrilaterals of finite numbers, or nothing.
bool quadsDrawable(const core::Annotation& a)
{
    if (a.quadPoints.size() < 8 || a.quadPoints.size() % 8 != 0 || !finiteRect(a)) return false;
    return std::all_of(a.quadPoints.begin(), a.quadPoints.end(), [](double v) { return std::isfinite(v); });
}

core::Result<Appearance> highlight(const core::Annotation& a)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    if (!quadsDrawable(a)) return appearanceError("error.pdf.appearanceGeometry", name);
    const std::optional<std::vector<double>> color = colourOf(a, {1.0, 1.0, 0.0});
    if (!color) return appearanceError("error.pdf.appearanceGeometry", name);
    const std::optional<QString> blend = blendOf(a);
    if (!blend) return appearanceError("error.pdf.appearanceUnsupported", name);

    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0").fillColor(color->data(), static_cast<int>(color->size()));
    for (std::size_t quad = 0; quad < a.quadPoints.size(); quad += 8) {
        std::array<std::pair<double, double>, 4> corners;
        double cx = 0.0, cy = 0.0;
        for (std::size_t k = 0; k < 4; ++k) {
            corners[k] = {a.quadPoints[quad + 2 * k], a.quadPoints[quad + 2 * k + 1]};
            cx += corners[k].first / 4.0;
            cy += corners[k].second / 4.0;
        }
        // Around the centre, counter-clockwise -- whatever order the file wrote them in.
        std::sort(corners.begin(), corners.end(), [cx, cy](const auto& p, const auto& q) {
            return std::atan2(p.second - cy, p.first - cx) < std::atan2(q.second - cy, q.first - cx);
        });
        content.moveTo(corners[0].first, corners[0].second);
        for (std::size_t k = 1; k < 4; ++k) content.lineTo(corners[k].first, corners[k].second);
        content.closePath();
    }
    content.fill().restore();
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), *blend)), content.bytes()};
}

// ---- Stage 6 T6: a quadrilateral as the TEXT's own frame ----------------------------------------------

// A quadrilateral read as the text it covers: where its bottom edge starts, the unit vector ALONG the text
// (`u`, the reading direction of the baseline) and the one ACROSS it towards the top (`v`), its length along
// `u` and its height along `v`. Acrobat writes top-left, top-right, bottom-left, bottom-right; ISO 32000-1's
// figure shows bottom-left, bottom-right, top-right, top-left. In the first the two horizontal edges run the
// SAME way, in the second OPPOSITE ways -- which is how the two are told apart, and why vertical text (whose
// "top" is a side of the page) is drawn right in either.
struct QuadFrame {
    double ox = 0.0, oy = 0.0;   // the bottom edge's start
    double ux = 1.0, uy = 0.0;   // along the text
    double vx = 0.0, vy = 1.0;   // across it, towards the top
    double length = 0.0;
    double height = 0.0;

    // A point `along` the bottom edge and `up` from it.
    std::pair<double, double> at(double along, double up) const
    {
        return {ox + ux * along + vx * up, oy + uy * along + vy * up};
    }
};

std::optional<QuadFrame> frameOf(const std::vector<double>& q, std::size_t at)
{
    const double x1 = q[at], y1 = q[at + 1], x2 = q[at + 2], y2 = q[at + 3];
    const double x3 = q[at + 4], y3 = q[at + 5], x4 = q[at + 6], y4 = q[at + 7];
    double bx0, by0, bx1, by1, tx, ty;   // the bottom edge, and a point of the top edge
    if ((x2 - x1) * (x4 - x3) + (y2 - y1) * (y4 - y3) >= 0.0) {
        bx0 = x3; by0 = y3; bx1 = x4; by1 = y4; tx = x1; ty = y1;   // Acrobat's order
    } else {
        bx0 = x1; by0 = y1; bx1 = x2; by1 = y2; tx = x4; ty = y4;   // ISO's
    }
    QuadFrame f;
    f.length = std::hypot(bx1 - bx0, by1 - by0);
    if (!(f.length > 1e-6)) return std::nullopt;
    f.ox = bx0;
    f.oy = by0;
    f.ux = (bx1 - bx0) / f.length;
    f.uy = (by1 - by0) / f.length;
    f.vx = -f.uy;
    f.vy = f.ux;
    f.height = (tx - bx0) * f.vx + (ty - by0) * f.vy;
    if (f.height < 0.0) {   // the normal that points at the top edge
        f.vx = -f.vx;
        f.vy = -f.vy;
        f.height = -f.height;
    }
    if (!(f.height > 1e-6)) return std::nullopt;
    return f;
}

// PDFium's own generation draws a 1-point line for every size of text (`cpdf_generateap.cpp`, read
// 2026-10-05) -- D6-6's second oracle. So does this, up to a line 14 points tall, which is every size of body
// text; past it the line grows with the text, as Acrobat's does, so a 72-point heading is not struck through
// with a hairline. WHERE the strikeout and the squiggle go is the glyphs' business, not PDFium's (D6-32).
double markupStroke(double height)
{
    return std::max(1.0, height / 14.0);
}

core::Result<Appearance> textLine(const core::Annotation& a)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    if (!quadsDrawable(a)) return appearanceError("error.pdf.appearanceGeometry", name);
    const std::optional<std::vector<double>> color = colourOf(a, {0.0, 0.0, 0.0});
    if (!color) return appearanceError("error.pdf.appearanceGeometry", name);

    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0").strokeColor(color->data(), static_cast<int>(color->size()));
    bool drew = false;
    for (std::size_t quad = 0; quad < a.quadPoints.size(); quad += 8) {
        const std::optional<QuadFrame> f = frameOf(a.quadPoints, quad);
        if (!f) continue;
        const double w = markupStroke(f->height);
        if (a.subtype == core::AnnotationSubtype::Squiggly) {
            // INSIDE THE DESCENDER BAND (guide A-50): a zigzag between 7 % and 16 % of the line's height above
            // its foot, at 45 degrees, in a stroke thin enough to stay there. PDFium's own -- 2 points tall and
            // 1 wide at every size -- spilled above the baseline and below the descenders of 10-point text
            // (rows 297-305 against a band of 298-303, measured at T6 on `text-a4.pdf`).
            const double base = 0.07 * f->height;
            const double amp = std::max(0.6, 0.09 * f->height);
            content.lineWidth(std::max(0.5, f->height / 28.0)).lineJoin(1);
            const auto [sx, sy] = f->at(0.0, base + amp);
            content.moveTo(sx, sy);
            double along = amp;
            bool up = false;
            while (along < f->length) {
                const auto [x, y] = f->at(along, up ? base + amp : base);
                content.lineTo(x, y);
                along += amp;
                up = !up;
            }
            const double remainder = f->length - (along - amp);   // the last part-step, at 45 degrees
            const auto [ex, ey] = f->at(f->length, up ? base + remainder : base + amp - remainder);
            content.lineTo(ex, ey);
        } else {
            // Underline: one stroke's width above the bottom edge -- under the baseline, in the descender band,
            // where PDFium draws it. Strikeout: 42 % of the way up the line's box, THROUGH THE X-HEIGHT -- not
            // its middle, which PDFium chooses and which sits above the x-height's middle third on a box that
            // reaches the font's ascent (guide A-50, measured at T6; D6-32).
            content.lineWidth(w);
            const double up = a.subtype == core::AnnotationSubtype::Underline ? w : 0.42 * f->height;
            const auto [x0, y0] = f->at(0.0, up);
            const auto [x1, y1] = f->at(f->length, up);
            content.moveTo(x0, y0).lineTo(x1, y1);
        }
        content.stroke();
        drew = true;
    }
    if (!drew) return appearanceError("error.pdf.appearanceGeometry", name);
    content.restore();
    // The form's box IS the rectangle (ISO 32000-1 Algorithm 8.1 scales any other box into it), so a stroke
    // past the rectangle is clipped: RawPage's markup tools make the rectangle the quadrilaterals' bounds,
    // grown (`core::markupRect`), and a producer's rectangle is the producer's.
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), blendOf(a).value_or(QString()))), content.bytes()};
}

// A caret: Acrobat's filled wedge, its point at the top centre of the rectangle and its foot notched.
core::Result<Appearance> caret(const core::Annotation& a)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    if (!finiteRect(a) || !(a.rect.size.width > 0.0) || !(a.rect.size.height > 0.0))
        return appearanceError("error.pdf.appearanceGeometry", name);
    const std::optional<std::vector<double>> color = colourOf(a, {0.0, 0.0, 1.0});
    if (!color) return appearanceError("error.pdf.appearanceGeometry", name);
    const double l = a.rect.left(), b = a.rect.bottom(), w = a.rect.size.width, h = a.rect.size.height;
    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0").fillColor(color->data(), static_cast<int>(color->size()));
    content.moveTo(l, b).lineTo(l + w / 2.0, b + h).lineTo(l + w, b).lineTo(l + w / 2.0, b + h * 0.3).closePath();
    content.fill().restore();
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), blendOf(a).value_or(QString()))), content.bytes()};
}

// A sticky note's icon, drawn into its rectangle on a 20-unit grid. Two of ISO 32000-1 Table 172's names:
// `Comment` -- the speech bubble Acrobat writes and RawPage's note tool creates -- and `Note`, the default
// when `/Name` is absent. Any other name is refused BY NAME rather than drawn as one of these: an edit that
// would redraw a `Key` or a `Help` note is refused, and its producer's appearance stays (D6-6).
core::Result<Appearance> note(const core::Annotation& a)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    if (!finiteRect(a) || !(a.rect.size.width > 0.0) || !(a.rect.size.height > 0.0))
        return appearanceError("error.pdf.appearanceGeometry", name);
    const QString icon = a.icon.isEmpty() ? QStringLiteral("Note") : a.icon;
    if (icon != QLatin1String("Comment") && icon != QLatin1String("Note"))
        return appearanceError("error.pdf.appearanceUnsupported", name);
    const std::optional<std::vector<double>> color = colourOf(a, {1.0, 0.82, 0.0});
    if (!color) return appearanceError("error.pdf.appearanceGeometry", name);

    const double sx = a.rect.size.width / 20.0, sy = a.rect.size.height / 20.0;
    const double l = a.rect.left(), b = a.rect.bottom();
    const auto p = [&](double x, double y) { return std::pair<double, double>{l + x * sx, b + y * sy}; };
    const double black[1] = {0.0};
    const double scale = std::min(sx, sy);

    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0").fillColor(color->data(), static_cast<int>(color->size()));
    content.strokeColor(black, 1).lineWidth(0.6 * scale).lineJoin(1);
    const auto path = [&content](std::initializer_list<std::pair<double, double>> points) {
        bool first = true;
        for (const auto& [x, y] : points) {
            if (first) content.moveTo(x, y);
            else content.lineTo(x, y);
            first = false;
        }
    };
    if (icon == QLatin1String("Comment")) {
        // A SPEECH BUBBLE ON PDFium's PROPORTIONS (Stage 6 T8, guide I-12): a box a unit in from the top and the sides
        // and four and a half up from the foot, its tail from 4.5 to 8.5 down to 6.5 at the foot, three lines at its
        // quarters two units in from each side, all in a one-unit stroke -- what PDFium generates for a note with no
        // `/AP` (measured against its raster at T8), in `/C` rather than its fixed yellow. T6's bubble, drawn
        // from the corpus generator's, overlapped PDFium's at ink IoU 0.877 against D6-6's 0.90 (A-65). Its top-left
        // ink corner is the rectangle's, where the note was clicked.
        content.lineWidth(1.0 * scale);
        path({p(0.5, 4.5), p(0.5, 19.5), p(19.5, 19.5), p(19.5, 4.5), p(8.5, 4.5), p(6.5, 0.5), p(4.5, 4.5)});
        content.closePath().fillAndStroke();
        const double step = (19.5 - 4.5) / 4.0;
        for (int line = 1; line <= 3; ++line) {
            const auto from = p(2.5, 19.5 - line * step);
            const auto to = p(17.5, 19.5 - line * step);
            content.moveTo(from.first, from.second).lineTo(to.first, to.second).stroke();
        }
    } else {
        // A page with its lower-right corner turned up.
        path({p(3, 1), p(3, 19), p(17, 19), p(17, 6), p(12, 1)});
        content.closePath().fillAndStroke();
        path({p(12, 1), p(12, 6), p(17, 6)});
        content.stroke();
        content.lineWidth(0.8 * scale);
        for (const double y : {15.0, 12.0, 9.0}) {
            const auto from = p(6, y);
            const auto to = p(14, y);
            content.moveTo(from.first, from.second).lineTo(to.first, to.second).stroke();
        }
    }
    content.restore();
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), blendOf(a).value_or(QString()))), content.bytes()};
}

// ---- Stage 6 T11: a file attachment's icon -------------------------------------------------------------

// A FileAttachment's icon, drawn into its rectangle on a 16 by 24 grid -- the size Acrobat and the corpus generator
// give one. Two of ISO 32000-1 Table 184's names: `PushPin`, Acrobat's default and what `annot.attachFile` creates
// -- a round head in `/C` over a pin, the generator's own shape -- and `Paperclip`. `Graph` and `Tag` are refused BY
// NAME, as the note refuses icons it does not draw: a move carries their producer's appearance, and an edit that
// would redraw one is refused (D6-6).
core::Result<Appearance> fileAttachment(const core::Annotation& a)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    if (!finiteRect(a) || !(a.rect.size.width > 0.0) || !(a.rect.size.height > 0.0))
        return appearanceError("error.pdf.appearanceGeometry", name);
    const QString icon = a.icon.isEmpty() ? QStringLiteral("PushPin") : a.icon;
    if (icon != QLatin1String("PushPin") && icon != QLatin1String("Paperclip"))
        return appearanceError("error.pdf.appearanceUnsupported", name);
    const std::optional<std::vector<double>> color = colourOf(a, {0.13, 0.4, 0.86});
    if (!color) return appearanceError("error.pdf.appearanceGeometry", name);

    const double sx = a.rect.size.width / 16.0, sy = a.rect.size.height / 24.0;
    const double l = a.rect.left(), b = a.rect.bottom();
    const auto x = [&](double u) { return l + u * sx; };
    const auto y = [&](double v) { return b + v * sy; };
    const double scale = std::min(sx, sy);

    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0").fillColor(color->data(), static_cast<int>(color->size()));
    content.strokeColor(color->data(), static_cast<int>(color->size())).lineWidth(1.2 * scale).lineCap(1);
    if (icon == QLatin1String("PushPin")) {
        // The head: a circle of radius 5 centred 6 below the top, as four Bezier quarters (k = 0.5523), filled and
        // stroked; then the pin from under the head to a unit above the foot.
        const double cx = 8.0, cy = 18.0, r = 5.0, k = 0.5523 * r;
        content.moveTo(x(cx + r), y(cy));
        content.curveTo(x(cx + r), y(cy + k), x(cx + k), y(cy + r), x(cx), y(cy + r));
        content.curveTo(x(cx - k), y(cy + r), x(cx - r), y(cy + k), x(cx - r), y(cy));
        content.curveTo(x(cx - r), y(cy - k), x(cx - k), y(cy - r), x(cx), y(cy - r));
        content.curveTo(x(cx + k), y(cy - r), x(cx + r), y(cy - k), x(cx + r), y(cy));
        content.closePath().fillAndStroke();
        content.moveTo(x(8.0), y(13.0)).lineTo(x(8.0), y(1.0)).stroke();
    } else {
        // A paper clip: two nested loops of one wire, open at the top right.
        content.moveTo(x(11.0), y(17.0)).lineTo(x(11.0), y(5.0));
        content.curveTo(x(11.0), y(2.0), x(5.0), y(2.0), x(5.0), y(5.0));
        content.lineTo(x(5.0), y(20.0));
        content.curveTo(x(5.0), y(23.0), x(13.0), y(23.0), x(13.0), y(20.0));
        content.lineTo(x(13.0), y(8.0));
        content.stroke();
    }
    content.restore();
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), blendOf(a).value_or(QString()))), content.bytes()};
}

// ---- Stage 6 T7: free text ----------------------------------------------------------------------------

CosObject integers(const std::vector<int>& values)
{
    std::vector<CosObject> items;
    items.reserve(values.size());
    for (const int v : values) items.push_back(CosObject::makeInteger(v));
    return CosObject::makeArray(std::move(items));
}

// ---- Stage 6 T8: paths, endings, outlines ------------------------------------------------------------

void polylinePath(cos::ContentStreamBuilder& content, const std::vector<double>& xy, bool close)
{
    content.moveTo(xy[0], xy[1]);
    for (std::size_t at = 2; at + 1 < xy.size(); at += 2) content.lineTo(xy[at], xy[at + 1]);
    if (close) content.closePath();
}

void cubicPath(cos::ContentStreamBuilder& content, const std::vector<core::CubicSegment>& segments, bool close)
{
    content.moveTo(segments.front().p0.x, segments.front().p0.y);
    for (const core::CubicSegment& s : segments) content.curveTo(s.c1.x, s.c1.y, s.c2.x, s.c2.y, s.p3.x, s.p3.y);
    if (close) content.closePath();
}

// An ellipse inscribed in `box`, as four quarter-circle cubics -- counter-clockwise from its right-hand point.
void ellipsePath(cos::ContentStreamBuilder& content, const core::PdfRect& box)
{
    constexpr double kKappa = 0.5522847498307936;
    const double rx = box.size.width / 2.0, ry = box.size.height / 2.0;
    const double cx = box.left() + rx, cy = box.bottom() + ry;
    const double kx = rx * kKappa, ky = ry * kKappa;
    content.moveTo(cx + rx, cy);
    content.curveTo(cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
    content.curveTo(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
    content.curveTo(cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
    content.curveTo(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
    content.closePath();
}

// An ENDING (ISO 32000-1 Table 176) at `tip` of a line from `from`: an open shape stroked, a closed one filled
// with `fill` when there is one and stroked (`core::lineEnding` is its geometry, the hit test's and the
// rectangle's too). The caller has set the stroke colour, the width and a solid line.
void drawEnding(cos::ContentStreamBuilder& content, const QString& ending, core::PdfPoint tip, core::PdfPoint from,
                double size, double spreadDegrees, bool fill)
{
    const std::optional<core::EndingShape> shape = core::lineEnding(ending, tip, from, size, spreadDegrees);
    if (!shape) return;
    switch (shape->kind) {
    case core::EndingShape::Kind::Open:
        polylinePath(content, shape->points, false);
        content.stroke();
        return;
    case core::EndingShape::Kind::Closed:
        polylinePath(content, shape->points, true);
        if (fill) content.fillAndStroke();
        else content.stroke();
        return;
    case core::EndingShape::Kind::Circle: {
        const double r = shape->radius;
        ellipsePath(content, core::PdfRect{core::PdfPoint{shape->centre.x - r, shape->centre.y - r}, core::PdfSize{2 * r, 2 * r}});
        if (fill) content.fillAndStroke();
        else content.stroke();
        return;
    }
    }
}

// A colour that may be absent -- ISO 32000-1's "no colour", transparent -- and nullopt-inside-ok for a count no
// device space has, which is refused.
struct OptionalColour {
    bool valid = true;
    std::vector<double> colour;   // empty: none
};

OptionalColour optionalColour(const std::vector<double>& colour)
{
    OptionalColour out;
    if (colour.empty()) return out;
    if (colour.size() != 1 && colour.size() != 3 && colour.size() != 4) {
        out.valid = false;
        return out;
    }
    for (const double v : colour) {
        if (!std::isfinite(v)) {
            out.valid = false;
            return out;
        }
    }
    out.colour = colour;
    return out;
}

// The width, the stroke colour, and a DASHED line for a `/BS /S /D` border (its `/D`, or ISO's default `[3]`).
// False for a dash array no reader can draw.
bool strokeStyle(cos::ContentStreamBuilder& content, const core::Annotation& a, double width)
{
    content.lineWidth(width);
    if (a.borderStyle != QLatin1String("D")) return true;
    std::vector<double> dash = a.dashPattern.empty() ? std::vector<double>{3.0} : a.dashPattern;
    if (dash.size() > 16) return false;
    bool any = false;
    for (const double v : dash) {
        if (!std::isfinite(v) || v < 0.0) return false;
        any = any || v > 0.0;
    }
    if (!any) return false;
    content.dash(dash.data(), static_cast<int>(dash.size()), 0.0);
    return true;
}

// Endings are drawn SOLID whatever the line's dash: a dashed arrowhead is not an arrowhead.
void solidLine(cos::ContentStreamBuilder& content, const core::Annotation& a)
{
    if (a.borderStyle == QLatin1String("D")) content.dash(nullptr, 0, 0.0);
}

// What a T8 kind refuses before it draws: a measurement (T12 draws its readout), and a width that is not one.
std::optional<core::Error> refusedShape(const core::Annotation& a)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    if (a.intent.endsWith(QLatin1String("Dimension"))) return appearanceError("error.pdf.appearanceUnsupported", name);
    if (!finiteRect(a)) return appearanceError("error.pdf.appearanceGeometry", name);
    return std::nullopt;
}

// A Line: its stroke from `/L`'s first point to its second, and `/LE`'s two endings -- an ending's closed shape
// filled with `/IC` when there is one (ISO 32000-1 Table 175).
core::Result<Appearance> line(const core::Annotation& a)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    if (std::optional<core::Error> refused = refusedShape(a)) return *refused;
    if (a.line.size() != 4 || !std::all_of(a.line.begin(), a.line.end(), [](double v) { return std::isfinite(v); }))
        return appearanceError("error.pdf.appearanceGeometry", name);
    for (const QString& ending : a.lineEndings) {
        if (!core::isLineEndingName(ending)) return appearanceError("error.pdf.appearanceUnsupported", name);
    }
    const OptionalColour stroke = optionalColour(a.color);
    const OptionalColour fill = optionalColour(a.interiorColor);
    const std::optional<QString> blend = blendOf(a);
    if (!stroke.valid || !fill.valid) return appearanceError("error.pdf.appearanceGeometry", name);
    if (!blend) return appearanceError("error.pdf.appearanceUnsupported", name);

    const double width = core::drawnBorderWidth(a);
    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0");
    if (!stroke.colour.empty() && width > 0.0) {
        content.strokeColor(stroke.colour.data(), static_cast<int>(stroke.colour.size()));
        if (!fill.colour.empty()) content.fillColor(fill.colour.data(), static_cast<int>(fill.colour.size()));
        if (!strokeStyle(content, a, width)) return appearanceError("error.pdf.appearanceGeometry", name);
        content.lineJoin(1);
        content.moveTo(a.line[0], a.line[1]).lineTo(a.line[2], a.line[3]).stroke();
        solidLine(content, a);
        const core::PdfPoint start{a.line[0], a.line[1]}, end{a.line[2], a.line[3]};
        const double size = core::lineEndingSize(width);
        drawEnding(content, a.lineEndings.size() > 0 ? a.lineEndings[0] : QString(), start, end, size,
                   core::kArrowSpreadDegrees, !fill.colour.empty());
        drawEnding(content, a.lineEndings.size() > 1 ? a.lineEndings[1] : QString(), end, start, size,
                   core::kArrowSpreadDegrees, !fill.colour.empty());
    }
    content.restore();
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), *blend)), content.bytes()};
}

// The box a Square's or a Circle's outline is drawn round: the rectangle less `/RD` (ISO 32000-1 Table 177).
core::PdfRect lessDifferences(const core::Annotation& a)
{
    double left = a.rect.left(), bottom = a.rect.bottom(), right = a.rect.right(), top = a.rect.top();
    if (a.rectDifferences.size() == 4) {
        left += a.rectDifferences[0];
        bottom += a.rectDifferences[1];
        right -= a.rectDifferences[2];
        top -= a.rectDifferences[3];
    }
    if (right < left) left = right = (left + right) / 2.0;
    if (top < bottom) bottom = top = (bottom + top) / 2.0;
    return core::PdfRect{core::PdfPoint{left, bottom}, core::PdfSize{right - left, top - bottom}};
}

// A Square or a Circle: filled with `/IC`, outlined in `/C` -- inside the rectangle less `/RD`, the outline
// inset by half its width so the whole of it shows. A cloudy Square's bumps go round that box; a cloudy Circle
// is refused (no reader agrees on one, and RawPage makes none).
core::Result<Appearance> squareOrCircle(const core::Annotation& a)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    if (std::optional<core::Error> refused = refusedShape(a)) return *refused;
    const bool circle = a.subtype == core::AnnotationSubtype::Circle;
    if (circle && a.cloudIntensity) return appearanceError("error.pdf.appearanceUnsupported", name);
    const OptionalColour stroke = optionalColour(a.color);
    const OptionalColour fill = optionalColour(a.interiorColor);
    const std::optional<QString> blend = blendOf(a);
    if (!stroke.valid || !fill.valid) return appearanceError("error.pdf.appearanceGeometry", name);
    if (!blend) return appearanceError("error.pdf.appearanceUnsupported", name);

    const double width = core::drawnBorderWidth(a);
    const bool outlined = !stroke.colour.empty() && width > 0.0;
    const core::PdfRect outer = lessDifferences(a);
    const double inset = outlined && !a.cloudIntensity ? width / 2.0 : 0.0;
    const core::PdfRect box{core::PdfPoint{outer.left() + inset, outer.bottom() + inset},
                            core::PdfSize{std::max(outer.size.width - 2 * inset, 0.0), std::max(outer.size.height - 2 * inset, 0.0)}};
    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0");
    if (!fill.colour.empty()) content.fillColor(fill.colour.data(), static_cast<int>(fill.colour.size()));
    if (outlined) {
        content.strokeColor(stroke.colour.data(), static_cast<int>(stroke.colour.size()));
        if (!strokeStyle(content, a, width)) return appearanceError("error.pdf.appearanceGeometry", name);
    }
    if (outlined || !fill.colour.empty()) {
        if (a.cloudIntensity) {
            const std::vector<double> corners{box.left(), box.bottom(), box.right(), box.bottom(),
                                              box.right(), box.top(), box.left(), box.top()};
            const std::vector<core::CubicSegment> cloud = core::cloudPath(corners, *a.cloudIntensity);
            if (cloud.empty()) return appearanceError("error.pdf.appearanceGeometry", name);
            content.lineJoin(1);
            cubicPath(content, cloud, true);
        } else if (circle) {
            ellipsePath(content, box);
        } else {
            content.rectangle(box.left(), box.bottom(), box.size.width, box.size.height);
        }
        if (outlined && !fill.colour.empty()) content.fillAndStroke();
        else if (outlined) content.stroke();
        else content.fill();
    }
    content.restore();
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), *blend)), content.bytes()};
}

// A Polygon (closed, filled with `/IC`, its border cloudy when `/BE` says so) or a PolyLine (open, with `/LE`'s
// endings): the vertices joined round, in `/C`.
core::Result<Appearance> polygon(const core::Annotation& a)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    if (std::optional<core::Error> refused = refusedShape(a)) return *refused;
    const bool closed = a.subtype == core::AnnotationSubtype::Polygon;
    const std::size_t n = a.vertices.size() / 2;
    if (a.vertices.size() % 2 != 0 || n < (closed ? 3u : 2u)
        || !std::all_of(a.vertices.begin(), a.vertices.end(), [](double v) { return std::isfinite(v); }))
        return appearanceError("error.pdf.appearanceGeometry", name);
    for (const QString& ending : a.lineEndings) {
        if (!core::isLineEndingName(ending)) return appearanceError("error.pdf.appearanceUnsupported", name);
    }
    const OptionalColour stroke = optionalColour(a.color);
    const OptionalColour fill = optionalColour(a.interiorColor);
    const std::optional<QString> blend = blendOf(a);
    if (!stroke.valid || !fill.valid) return appearanceError("error.pdf.appearanceGeometry", name);
    if (!blend) return appearanceError("error.pdf.appearanceUnsupported", name);

    const double width = core::drawnBorderWidth(a);
    const bool outlined = !stroke.colour.empty() && width > 0.0;
    const bool filled = closed && !fill.colour.empty();
    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0");
    if (!fill.colour.empty()) content.fillColor(fill.colour.data(), static_cast<int>(fill.colour.size()));
    if (outlined) {
        content.strokeColor(stroke.colour.data(), static_cast<int>(stroke.colour.size()));
        if (!strokeStyle(content, a, width)) return appearanceError("error.pdf.appearanceGeometry", name);
    }
    content.lineJoin(1);
    if (outlined || filled) {
        if (closed && a.cloudIntensity) {
            const std::vector<core::CubicSegment> cloud = core::cloudPath(a.vertices, *a.cloudIntensity);
            if (cloud.empty()) return appearanceError("error.pdf.appearanceGeometry", name);
            cubicPath(content, cloud, true);
        } else {
            polylinePath(content, a.vertices, closed);
        }
        if (outlined && filled) content.fillAndStroke();
        else if (outlined) content.stroke();
        else content.fill();
    }
    if (!closed && outlined) {
        solidLine(content, a);
        const double size = core::lineEndingSize(width);
        drawEnding(content, a.lineEndings.size() > 0 ? a.lineEndings[0] : QString(), core::PdfPoint{a.vertices[0], a.vertices[1]},
                   core::PdfPoint{a.vertices[2], a.vertices[3]}, size, core::kArrowSpreadDegrees, !fill.colour.empty());
        drawEnding(content, a.lineEndings.size() > 1 ? a.lineEndings[1] : QString(),
                   core::PdfPoint{a.vertices[2 * n - 2], a.vertices[2 * n - 1]},
                   core::PdfPoint{a.vertices[2 * n - 4], a.vertices[2 * n - 3]}, size, core::kArrowSpreadDegrees,
                   !fill.colour.empty());
    }
    content.restore();
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), *blend)), content.bytes()};
}

// Ink (D6-15): each stroke as the centripetal Catmull-Rom curve through its samples (`core::inkCurve`), with
// round caps and joins -- a stroke of one sample is a dot. The samples themselves stay as written in `/InkList`.
core::Result<Appearance> ink(const core::Annotation& a)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    if (std::optional<core::Error> refused = refusedShape(a)) return *refused;
    if (a.inkList.empty()) return appearanceError("error.pdf.appearanceGeometry", name);
    for (const std::vector<double>& stroke : a.inkList) {
        if (stroke.size() < 2 || stroke.size() % 2 != 0
            || !std::all_of(stroke.begin(), stroke.end(), [](double v) { return std::isfinite(v); }))
            return appearanceError("error.pdf.appearanceGeometry", name);
    }
    const OptionalColour stroke = optionalColour(a.color);
    const std::optional<QString> blend = blendOf(a);
    if (!stroke.valid) return appearanceError("error.pdf.appearanceGeometry", name);
    if (!blend) return appearanceError("error.pdf.appearanceUnsupported", name);

    const double width = core::drawnBorderWidth(a);
    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0");
    if (!stroke.colour.empty() && width > 0.0) {
        content.strokeColor(stroke.colour.data(), static_cast<int>(stroke.colour.size()));
        if (!strokeStyle(content, a, width)) return appearanceError("error.pdf.appearanceGeometry", name);
        content.lineCap(1).lineJoin(1);
        for (const std::vector<double>& samples : a.inkList) {
            if (samples.size() == 2) {
                content.moveTo(samples[0], samples[1]).lineTo(samples[0], samples[1]).stroke();
                continue;
            }
            cubicPath(content, core::inkCurve(samples), false);
            content.stroke();
        }
    }
    content.restore();
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), *blend)), content.bytes()};
}

core::Result<Appearance> freeText(const core::Annotation& a, AppearanceObjects& objects)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    const std::optional<core::FreeTextFrame> frame = core::freeTextFrame(a);
    if (!frame || !finiteRect(a)) return appearanceError("error.pdf.appearanceGeometry", name);
    const core::FreeTextStyle style = core::freeTextStyle(a);
    // `/DA` names the face; one this build does not draw is refused, never set in another (see the header).
    std::optional<core::TypedFont> daFace;
    if (!style.font.isEmpty()) {
        daFace = core::typedFontByResource(style.font);
        if (!daFace) return appearanceError("error.pdf.appearanceUnsupported", name);
    }
    const core::FreeTextKind kind = core::freeTextKind(a);
    const std::optional<std::vector<double>> lineColour = colourOf(a, {0.0});
    if (!lineColour) return appearanceError("error.pdf.appearanceGeometry", name);
    std::vector<double> textColour = style.textColour;
    // `assign`, not `= {0.0}`: GCC 13 at -O2 under ASan reads a one-element initializer list's copy as
    // out of bounds (-Warray-bounds, a false positive) and `-Werror` makes it fatal -- CI run 37648525480.
    if (textColour.size() != 1 && textColour.size() != 3 && textColour.size() != 4) textColour.assign(1, 0.0);
    for (const double v : textColour) {
        if (!std::isfinite(v)) return appearanceError("error.pdf.appearanceGeometry", name);
    }

    const double inset = frame->inset;
    const std::optional<double> wrap = kind == core::FreeTextKind::Typewriter
                                           ? std::nullopt
                                           : std::optional<double>(std::max(frame->width - 2.0 * inset, 0.0));
    core::Result<core::TypedLayout> laid =
        core::layoutTypedText(a.contents, style.size, wrap, style.han.value_or(core::TypedFont::Simplified));
    if (!laid) return laid.error();
    const core::TypedLayout& layout = laid.value();

    // The fonts: every face the text uses, and the one `/DA` names -- under its own name, so `/DA` resolves
    // against the appearance's resources as written (`Helvetica` as well as `Helv`).
    std::vector<CosObject::Entry> fonts;
    std::vector<std::string> named;
    const auto addFont = [&](core::TypedFont face, const std::string& resource) {
        if (std::find(named.begin(), named.end(), resource) != named.end()) return;
        named.push_back(resource);
        const cos::CosRef at = AppearanceStreamBuilder::fontObject(face, objects);
        fonts.push_back({resource, CosObject::makeReference(at.number, at.generation)});
    };
    if (daFace) addFont(*daFace, style.font.toStdString());
    for (const core::TypedLine& line : layout.lines) {
        for (const core::TypedRun& run : line.runs) addFont(run.font, core::typedFontSpec(run.font).resource);
    }

    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0");
    const core::PdfRect& box = frame->box;
    const double border = core::freeTextBorderWidth(a);
    if (!a.interiorColor.empty()) {
        if (a.interiorColor.size() != 1 && a.interiorColor.size() != 3 && a.interiorColor.size() != 4)
            return appearanceError("error.pdf.appearanceGeometry", name);
        content.fillColor(a.interiorColor.data(), static_cast<int>(a.interiorColor.size()));
        content.rectangle(box.left(), box.bottom(), box.size.width, box.size.height).fill();
    }
    if (border > 0.0 && box.size.width > border && box.size.height > border) {
        content.strokeColor(lineColour->data(), static_cast<int>(lineColour->size())).lineWidth(border);
        content.rectangle(box.left() + border / 2.0, box.bottom() + border / 2.0, box.size.width - border,
                          box.size.height - border).stroke();
    }
    if (kind == core::FreeTextKind::Callout && a.callout.size() >= 4 && a.callout.size() % 2 == 0) {
        for (const double v : a.callout) {
            if (!std::isfinite(v)) return appearanceError("error.pdf.appearanceGeometry", name);
        }
        // Stage 6 T8: every ending ISO names, drawn by the line kinds' own drawer -- T7 drew two arrows and
        // refused the rest.
        const QString ending = a.lineEndings.empty() ? QStringLiteral("None") : a.lineEndings.front();
        if (!core::isLineEndingName(ending)) return appearanceError("error.pdf.appearanceUnsupported", name);
        content.strokeColor(lineColour->data(), static_cast<int>(lineColour->size()));
        content.fillColor(lineColour->data(), static_cast<int>(lineColour->size()));
        content.lineWidth(border > 0.0 ? border : 1.0).lineJoin(1);
        content.moveTo(a.callout[0], a.callout[1]);
        for (std::size_t k = 2; k + 1 < a.callout.size(); k += 2) content.lineTo(a.callout[k], a.callout[k + 1]);
        content.stroke();
        // A callout's head is its own size, 25 degrees off the leader (T7), and a closed one is filled in the line's
        // colour, as T7 drew its arrow.
        drawEnding(content, ending, core::PdfPoint{a.callout[0], a.callout[1]}, core::PdfPoint{a.callout[2], a.callout[3]},
                   core::kCalloutHead, 25.0, true);
    }

    // THE TEXT, in the frame: turned with `/Rotate`, clipped to the box.
    const auto& m = frame->toPage;
    content.save().concat(m[0], m[1], m[2], m[3], m[4], m[5]);
    content.rectangle(0.0, 0.0, frame->width, frame->height).clip().endPath();
    content.beginText().fillColor(textColour.data(), static_cast<int>(textColour.size()));
    const double available = std::max(frame->width - 2.0 * inset, 0.0);
    std::optional<core::TypedFont> current;
    for (std::size_t i = 0; i < layout.lines.size(); ++i) {
        const core::TypedLine& line = layout.lines[i];
        if (line.runs.empty()) continue;
        double x = inset;
        if (a.justification == 1) x += (available - line.width) / 2.0;
        else if (a.justification == 2) x += available - line.width;
        const double y = frame->height - inset - core::kTypedAscent * layout.size
                         - static_cast<double>(i) * core::kTypedLeading * layout.size;
        content.textMatrix(1.0, 0.0, 0.0, 1.0, x, y);
        for (const core::TypedRun& run : line.runs) {
            if (current != run.font) {
                content.font(core::typedFontSpec(run.font).resource, layout.size);
                current = run.font;
            }
            const std::string codes = AppearanceStreamBuilder::encodeText(run.font, run.text);
            if (codes.empty()) return appearanceError("error.pdf.appearanceGeometry", name);
            content.showText(codes);
        }
    }
    content.endText().restore().restore();
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), blendOf(a).value_or(QString())), std::move(fonts)), content.bytes()};
}

// ---- Stage 6 T10: stamps (D6-16, D6-37) ------------------------------------------------------------------

// A rounded rectangle, counter-clockwise from its bottom edge: four straight sides and four quarter circles.
void roundedRect(cos::ContentStreamBuilder& content, double x, double y, double w, double h, double r)
{
    r = std::clamp(r, 0.0, std::min(w, h) / 2.0);
    constexpr double k = 0.5523;   // a quarter circle's cubic handle, as a fraction of its radius
    content.moveTo(x + r, y).lineTo(x + w - r, y);
    content.curveTo(x + w - r + k * r, y, x + w, y + r - k * r, x + w, y + r);
    content.lineTo(x + w, y + h - r);
    content.curveTo(x + w, y + h - r + k * r, x + w - r + k * r, y + h, x + w - r, y + h);
    content.lineTo(x + r, y + h);
    content.curveTo(x + r - k * r, y + h, x, y + h - r + k * r, x, y + h - r);
    content.lineTo(x, y + r);
    content.curveTo(x, y + r - k * r, x + r - k * r, y, x + r, y);
    content.closePath();
}

// The form of a stamp: its NATURAL box as `/BBox`, its turn as `/Matrix` (none at 0), counter-clockwise -- exact
// at the quarter turns -- and its resources: GS0, and the fonts and XObjects it paints.
CosObject stampForm(const core::PdfSize& natural, int degrees, CosObject extGState, std::vector<CosObject::Entry> fonts,
                    std::vector<CosObject::Entry> xobjects)
{
    std::vector<CosObject::Entry> states{{"GS0", std::move(extGState)}};
    std::vector<CosObject::Entry> resources{{"ExtGState", CosObject::makeDictionary(std::move(states))}};
    if (!fonts.empty()) resources.push_back({"Font", CosObject::makeDictionary(std::move(fonts))});
    if (!xobjects.empty()) resources.push_back({"XObject", CosObject::makeDictionary(std::move(xobjects))});
    std::vector<CosObject::Entry> entries{
        {"Type", CosObject::makeName("XObject")},
        {"Subtype", CosObject::makeName("Form")},
        {"FormType", CosObject::makeInteger(1)},
        {"BBox", numbers({0.0, 0.0, natural.width, natural.height})},
    };
    if (degrees != 0) {
        const double radians = static_cast<double>(degrees) * 3.14159265358979323846 / 180.0;
        const auto snapped = [](double v) {
            const double r = std::round(v);
            return std::abs(v - r) < 1e-12 ? r : v;
        };
        const double c = snapped(std::cos(radians));
        const double s = snapped(std::sin(radians));
        entries.push_back({"Matrix", numbers({c, s, -s, c, 0.0, 0.0})});
    }
    entries.push_back({"Resources", CosObject::makeDictionary(std::move(resources))});
    return CosObject::makeStream(std::move(entries), cos::CosStreamData{});
}

// Base-14 Helvetica-Bold, NOT EMBEDDED, written with its advances for the codes a title holds (D6-34(d)'s rule).
cos::CosRef helveticaBoldObject(AppearanceObjects& objects)
{
    std::vector<int> widths;
    for (int code = core::kHelveticaBoldFirst; code <= core::kHelveticaBoldLast; ++code)
        widths.push_back(core::helveticaBoldWidth(code));
    return objects.shared("font:HeBo", CosObject::makeDictionary({
        {"Type", CosObject::makeName("Font")},
        {"Subtype", CosObject::makeName("Type1")},
        {"BaseFont", CosObject::makeName("Helvetica-Bold")},
        {"Encoding", CosObject::makeName("WinAnsiEncoding")},
        {"FirstChar", CosObject::makeInteger(core::kHelveticaBoldFirst)},
        {"LastChar", CosObject::makeInteger(core::kHelveticaBoldLast)},
        {"Widths", integers(widths)},
    }));
}

constexpr double kHelveticaBoldCapHeight = 0.718;   // the AFM's CapHeight, in ems

// A standard or a dynamic stamp: a rounded border in its colour round its tint, the title, and a dynamic stamp's
// lines -- every line centred.
core::Result<Appearance> wordStamp(const core::Annotation& a, AppearanceObjects& objects, const core::PdfSize& natural)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    const std::optional<std::vector<double>> colour = colourOf(a, core::stampColour(a.icon));
    if (!colour) return appearanceError("error.pdf.appearanceGeometry", name);
    std::vector<double> tint = *colour;
    // A twelfth of the colour on white: in gray and RGB white is 1, in CMYK it is 0.
    for (double& v : tint) v = tint.size() == 4 ? std::clamp(v, 0.0, 1.0) * 0.12 : 1.0 - (1.0 - std::clamp(v, 0.0, 1.0)) * 0.12;

    const bool dynamic = core::stampKind(a) == core::StampKind::Dynamic;
    const double titleSize = dynamic ? core::kDynamicTitleSize : core::kStampTitleSize;
    const QString title = core::stampTitle(a.icon);
    const std::optional<double> titleWidth = core::helveticaBoldTextWidth(title, titleSize);
    if (!titleWidth) return appearanceError("error.edit.stampNotRedrawable", name);

    std::vector<CosObject::Entry> fonts;
    std::vector<std::string> named;
    const auto addFont = [&](const std::string& resource, cos::CosRef at) {
        if (std::find(named.begin(), named.end(), resource) != named.end()) return;
        named.push_back(resource);
        fonts.push_back({resource, CosObject::makeReference(at.number, at.generation)});
    };
    addFont("HeBo", helveticaBoldObject(objects));

    std::vector<core::TypedLayout> lines;
    if (dynamic) {
        for (const QString& line : a.contents.split(QLatin1Char('\n'))) {
            core::Result<core::TypedLayout> laid =
                core::layoutTypedText(line, core::kDynamicLineSize, std::nullopt, core::TypedFont::Simplified);
            if (!laid) return laid.error();
            for (const core::TypedLine& typed : laid.value().lines) {
                for (const core::TypedRun& run : typed.runs)
                    addFont(core::typedFontSpec(run.font).resource, AppearanceStreamBuilder::fontObject(run.font, objects));
            }
            lines.push_back(std::move(laid).value());
        }
    }

    const double border = core::kStampBorder;
    const double inner = core::kStampBorder + core::kStampPadding;
    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0");
    content.fillColor(tint.data(), static_cast<int>(tint.size()));
    content.strokeColor(colour->data(), static_cast<int>(colour->size())).lineWidth(border).lineJoin(1);
    roundedRect(content, border / 2.0, border / 2.0, natural.width - border, natural.height - border, core::kStampCorner);
    content.fillAndStroke();

    content.beginText().fillColor(colour->data(), static_cast<int>(colour->size()));
    const double titleBaseline = dynamic ? natural.height - inner - titleSize * 0.8
                                         : (natural.height - kHelveticaBoldCapHeight * titleSize) / 2.0;
    content.font("HeBo", titleSize).textMatrix(1.0, 0.0, 0.0, 1.0, (natural.width - *titleWidth) / 2.0, titleBaseline);
    std::string titleCodes;
    for (const QChar c : title) titleCodes += static_cast<char>(c.unicode());
    content.showText(titleCodes);
    double top = natural.height - inner - titleSize - core::kStampPadding / 2.0;
    for (const core::TypedLayout& layout : lines) {
        for (const core::TypedLine& line : layout.lines) {
            const double baseline = top - core::kTypedAscent * layout.size;
            top -= core::kTypedLeading * layout.size;
            if (line.runs.empty()) continue;
            content.textMatrix(1.0, 0.0, 0.0, 1.0, (natural.width - line.width) / 2.0, baseline);
            for (const core::TypedRun& run : line.runs) {
                content.font(core::typedFontSpec(run.font).resource, layout.size);
                const std::string codes = AppearanceStreamBuilder::encodeText(run.font, run.text);
                if (codes.empty()) return appearanceError("error.pdf.appearanceGeometry", name);
                content.showText(codes);
            }
        }
    }
    content.endText().restore();
    return Appearance{stampForm(natural, core::stampRotation(a), opacityState(opacityOf(a), blendOf(a).value_or(QString())),
                                std::move(fonts), {}),
                      content.bytes()};
}

// An image stamp: its picture painted over the whole natural box, through one image XObject shared by key.
core::Result<Appearance> imageStamp(const core::Annotation& a, AppearanceObjects& objects, const core::PdfSize& natural)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    const core::StampImage& image = a.image;
    if (core::invalidStampImage(image)) return appearanceError("error.edit.stampImageUnsupported", name);
    const std::string key = core::stampImageKey(image).toStdString();
    const char* space = image.components == 1 ? "DeviceGray" : "DeviceRGB";
    std::vector<CosObject::Entry> entries{
        {"Type", CosObject::makeName("XObject")},
        {"Subtype", CosObject::makeName("Image")},
        {"Width", CosObject::makeInteger(image.width)},
        {"Height", CosObject::makeInteger(image.height)},
        {"ColorSpace", CosObject::makeName(space)},
        {"BitsPerComponent", CosObject::makeInteger(8)},
        {"Filter", CosObject::makeName(image.encoding == core::StampImage::Encoding::Jpeg ? "DCTDecode" : "FlateDecode")},
    };
    if (!image.alpha.isEmpty()) {
        const cos::CosRef mask = objects.sharedStream("smask:" + key, CosObject::makeStream({
            {"Type", CosObject::makeName("XObject")},
            {"Subtype", CosObject::makeName("Image")},
            {"Width", CosObject::makeInteger(image.width)},
            {"Height", CosObject::makeInteger(image.height)},
            {"ColorSpace", CosObject::makeName("DeviceGray")},
            {"BitsPerComponent", CosObject::makeInteger(8)},
            {"Filter", CosObject::makeName("FlateDecode")},
        }, cos::CosStreamData{}), image.alpha.toStdString());
        entries.push_back({"SMask", CosObject::makeReference(mask.number, mask.generation)});
    }
    const cos::CosRef picture =
        objects.sharedStream("image:" + key, CosObject::makeStream(std::move(entries), cos::CosStreamData{}), image.data.toStdString());

    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0").concat(natural.width, 0.0, 0.0, natural.height, 0.0, 0.0).paintXObject("Im0").restore();
    return Appearance{stampForm(natural, core::stampRotation(a), opacityState(opacityOf(a), blendOf(a).value_or(QString())), {},
                                {{"Im0", CosObject::makeReference(picture.number, picture.generation)}}),
                      content.bytes()};
}

core::Result<Appearance> stamp(const core::Annotation& a, AppearanceObjects& objects)
{
    const core::StampKind kind = core::stampKind(a);
    if (kind == core::StampKind::Foreign)
        return appearanceError("error.edit.stampNotRedrawable", core::annotationSubtypeName(a.subtype));
    core::Result<core::PdfSize> natural = core::stampNaturalSize(a);
    if (!natural) return natural.error();
    if (kind == core::StampKind::Image) return imageStamp(a, objects, natural.value());
    return wordStamp(a, objects, natural.value());
}

class DryRun final : public AppearanceObjects {
public:
    cos::CosRef shared(const std::string&, cos::CosObject) override { return cos::CosRef{0, 0}; }
    cos::CosRef sharedStream(const std::string&, cos::CosObject, std::string) override { return cos::CosRef{0, 0}; }
};

} // namespace

cos::CosRef CollectedAppearanceObjects::shared(const std::string& key, cos::CosObject object)
{
    for (const Entry& entry : entries_) {
        if (entry.key == key) return entry.at;
    }
    const cos::CosRef at{first_ + static_cast<long long>(entries_.size()), 0};
    entries_.push_back(Entry{key, at, std::move(object), std::nullopt});
    return at;
}

cos::CosRef CollectedAppearanceObjects::sharedStream(const std::string& key, cos::CosObject dictionary, std::string data)
{
    for (const Entry& entry : entries_) {
        if (entry.key == key) return entry.at;
    }
    const cos::CosRef at{first_ + static_cast<long long>(entries_.size()), 0};
    entries_.push_back(Entry{key, at, std::move(dictionary), std::move(data)});
    return at;
}

const cos::CosObject* CollectedAppearanceObjects::find(cos::CosRef at) const
{
    for (const Entry& entry : entries_) {
        if (entry.at == at) return &entry.object;
    }
    return nullptr;
}

cos::CosRef AppearanceStreamBuilder::fontObject(core::TypedFont font, AppearanceObjects& objects)
{
    const core::TypedFontSpec& spec = core::typedFontSpec(font);
    const std::string resource = spec.resource;
    if (font == core::TypedFont::Helvetica) {
        // NOT EMBEDDED, and its advances written: a reader sets the text on the advances layout used.
        std::vector<int> widths;
        for (int code = 32; code <= 255; ++code) widths.push_back(core::helveticaWidth(static_cast<std::uint8_t>(code)));
        return objects.shared("font:" + resource, CosObject::makeDictionary({
            {"Type", CosObject::makeName("Font")},
            {"Subtype", CosObject::makeName("Type1")},
            {"BaseFont", CosObject::makeName(spec.baseFont)},
            {"Encoding", CosObject::makeName(spec.encoding)},
            {"FirstChar", CosObject::makeInteger(32)},
            {"LastChar", CosObject::makeInteger(255)},
            {"Widths", integers(widths)},
        }));
    }
    // A predefined CJK collection, NOT EMBEDDED: Type0 over a CIDFontType0, every CID 1,000 units wide.
    // `/Flags` 6 -- symbolic (its glyphs are not the standard Latin set) and serif (Song, Ming, Mincho and
    // Myeongjo all are), which is what a reader's substitution matches on.
    const cos::CosRef descriptor = objects.shared("font:" + resource + ":descriptor", CosObject::makeDictionary({
        {"Type", CosObject::makeName("FontDescriptor")},
        {"FontName", CosObject::makeName(spec.baseFont)},
        {"Flags", CosObject::makeInteger(6)},
        {"FontBBox", integers({-25, -254, 1000, 880})},
        {"ItalicAngle", CosObject::makeInteger(0)},
        {"Ascent", CosObject::makeInteger(880)},
        {"Descent", CosObject::makeInteger(-120)},
        {"CapHeight", CosObject::makeInteger(880)},
        {"StemV", CosObject::makeInteger(93)},
    }));
    const cos::CosRef cid = objects.shared("font:" + resource + ":cid", CosObject::makeDictionary({
        {"Type", CosObject::makeName("Font")},
        {"Subtype", CosObject::makeName("CIDFontType0")},
        {"BaseFont", CosObject::makeName(spec.baseFont)},
        {"CIDSystemInfo", CosObject::makeDictionary({{"Registry", CosObject::makeString("Adobe")},
                                                     {"Ordering", CosObject::makeString(spec.ordering)},
                                                     {"Supplement", CosObject::makeInteger(spec.supplement)}})},
        {"FontDescriptor", CosObject::makeReference(descriptor.number, descriptor.generation)},
        {"DW", CosObject::makeInteger(1000)},
    }));
    return objects.shared("font:" + resource, CosObject::makeDictionary({
        {"Type", CosObject::makeName("Font")},
        {"Subtype", CosObject::makeName("Type0")},
        {"BaseFont", CosObject::makeName(spec.baseFont)},
        {"Encoding", CosObject::makeName(spec.encoding)},
        {"DescendantFonts", CosObject::makeArray({CosObject::makeReference(cid.number, cid.generation)})},
    }));
}

std::string AppearanceStreamBuilder::encodeText(core::TypedFont font, QStringView text)
{
    std::string out;
    out.reserve(static_cast<std::size_t>(text.size()) * 2);
    for (const QChar c : text) {
        const char32_t u = c == QLatin1Char('\t') ? U' ' : static_cast<char32_t>(c.unicode());
        if (font == core::TypedFont::Helvetica) {
            const std::optional<std::uint8_t> code = core::winAnsiCode(u);
            if (!code) return {};
            out += static_cast<char>(*code);
        } else {
            if (!core::cjkCovers(font, u)) return {};
            out += static_cast<char>((u >> 8) & 0xFF);
            out += static_cast<char>(u & 0xFF);
        }
    }
    return out;
}

bool AppearanceStreamBuilder::supports(core::AnnotationSubtype subtype) noexcept
{
    switch (subtype) {
    case core::AnnotationSubtype::Highlight:
    case core::AnnotationSubtype::Underline:
    case core::AnnotationSubtype::StrikeOut:
    case core::AnnotationSubtype::Squiggly:
    case core::AnnotationSubtype::Caret:
    case core::AnnotationSubtype::Text:
    case core::AnnotationSubtype::FreeText:
    // Stage 6 T8: the drawing kinds.
    case core::AnnotationSubtype::Line:
    case core::AnnotationSubtype::Square:
    case core::AnnotationSubtype::Circle:
    case core::AnnotationSubtype::Polygon:
    case core::AnnotationSubtype::PolyLine:
    case core::AnnotationSubtype::Ink:
    // Stage 6 T10: stamps -- a foreign one is refused by `normal`, not here: whether one can be drawn is the
    // annotation's question, not its kind's.
    case core::AnnotationSubtype::Stamp:
    // Stage 6 T11: a file attachment's pushpin or paper clip.
    case core::AnnotationSubtype::FileAttachment:
        return true;
    default:
        return false;
    }
}

bool AppearanceStreamBuilder::drawsNothing(core::AnnotationSubtype subtype) noexcept
{
    return subtype == core::AnnotationSubtype::Popup;
}

bool AppearanceStreamBuilder::drawsNothing(const core::Annotation& annotation) noexcept
{
    return drawsNothing(annotation.subtype) || annotation.isStateReply();
}

core::Result<Appearance> AppearanceStreamBuilder::normal(const core::Annotation& annotation)
{
    DryRun nowhere;
    return normal(annotation, nowhere);
}

core::Result<Appearance> AppearanceStreamBuilder::normal(const core::Annotation& annotation, AppearanceObjects& objects)
{
    core::Result<Appearance> built = [&]() -> core::Result<Appearance> {
        switch (annotation.subtype) {
        case core::AnnotationSubtype::Highlight: return highlight(annotation);
        case core::AnnotationSubtype::Underline:
        case core::AnnotationSubtype::StrikeOut:
        case core::AnnotationSubtype::Squiggly: return textLine(annotation);
        case core::AnnotationSubtype::Caret: return caret(annotation);
        case core::AnnotationSubtype::Text: return note(annotation);
        case core::AnnotationSubtype::FreeText: return freeText(annotation, objects);
        case core::AnnotationSubtype::Line: return line(annotation);
        case core::AnnotationSubtype::Square:
        case core::AnnotationSubtype::Circle: return squareOrCircle(annotation);
        case core::AnnotationSubtype::Polygon:
        case core::AnnotationSubtype::PolyLine: return polygon(annotation);
        case core::AnnotationSubtype::Ink: return ink(annotation);
        case core::AnnotationSubtype::Stamp: return stamp(annotation, objects);
        case core::AnnotationSubtype::FileAttachment: return fileAttachment(annotation);
        default:
            return appearanceError("error.pdf.appearanceUnsupported", core::annotationSubtypeName(annotation.subtype));
        }
    }();
    if (!built) return built;
    // D6-6: every stream we generate is re-read by the reader before anyone else sees it.
    if (core::Status wellFormed = cos::ContentStreamBuilder::check(built.value().data); !wellFormed)
        return wellFormed.error();
    return built;
}

} // namespace rawpage::pdf::ops
