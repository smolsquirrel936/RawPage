// pdf/ops/AppearanceStreamBuilder -- implementation. Stage 6 T3 (Highlight); T6 the rest of the text
// markup, the caret and the note. See the header for why we draw every appearance, and how each is drawn.

#include "pdf/ops/AppearanceStreamBuilder.h"

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
CosObject formDictionary(const core::PdfRect& bbox, CosObject extGState)
{
    std::vector<CosObject::Entry> states{{"GS0", std::move(extGState)}};
    std::vector<CosObject::Entry> resources{{"ExtGState", CosObject::makeDictionary(std::move(states))}};
    return CosObject::makeStream(std::vector<CosObject::Entry>{
        {"Type", CosObject::makeName("XObject")},
        {"Subtype", CosObject::makeName("Form")},
        {"FormType", CosObject::makeInteger(1)},
        {"BBox", numbers({bbox.left(), bbox.bottom(), bbox.right(), bbox.top()})},
        {"Resources", CosObject::makeDictionary(std::move(resources))},
    }, cos::CosStreamData{});
}

CosObject opacityState(double opacity, bool multiply)
{
    std::vector<CosObject::Entry> entries{{"Type", CosObject::makeName("ExtGState")}};
    if (multiply) entries.push_back({"BM", CosObject::makeName("Multiply")});
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
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), true)), content.bytes()};
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
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), false)), content.bytes()};
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
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), false)), content.bytes()};
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
        path({p(1, 5), p(1, 19), p(19, 19), p(19, 5), p(9, 5), p(5, 1), p(6, 5)});
        content.closePath().fillAndStroke();
        content.lineWidth(0.8 * scale);
        for (const auto& [from, to] : {std::pair{p(4, 15), p(16, 15)}, std::pair{p(4, 11.5), p(16, 11.5)},
                                       std::pair{p(4, 8), p(12, 8)}}) {
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
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), false)), content.bytes()};
}

} // namespace

bool AppearanceStreamBuilder::supports(core::AnnotationSubtype subtype) noexcept
{
    switch (subtype) {
    case core::AnnotationSubtype::Highlight:
    case core::AnnotationSubtype::Underline:
    case core::AnnotationSubtype::StrikeOut:
    case core::AnnotationSubtype::Squiggly:
    case core::AnnotationSubtype::Caret:
    case core::AnnotationSubtype::Text:
        return true;
    default:
        return false;
    }
}

bool AppearanceStreamBuilder::drawsNothing(core::AnnotationSubtype subtype) noexcept
{
    return subtype == core::AnnotationSubtype::Popup;
}

core::Result<Appearance> AppearanceStreamBuilder::normal(const core::Annotation& annotation)
{
    core::Result<Appearance> built = [&]() -> core::Result<Appearance> {
        switch (annotation.subtype) {
        case core::AnnotationSubtype::Highlight: return highlight(annotation);
        case core::AnnotationSubtype::Underline:
        case core::AnnotationSubtype::StrikeOut:
        case core::AnnotationSubtype::Squiggly: return textLine(annotation);
        case core::AnnotationSubtype::Caret: return caret(annotation);
        case core::AnnotationSubtype::Text: return note(annotation);
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
