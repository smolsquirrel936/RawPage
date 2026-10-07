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

// ---- Stage 6 T7: free text ----------------------------------------------------------------------------

CosObject integers(const std::vector<int>& values)
{
    std::vector<CosObject> items;
    items.reserve(values.size());
    for (const int v : values) items.push_back(CosObject::makeInteger(v));
    return CosObject::makeArray(std::move(items));
}

// An arrowhead at `tip`, pointing away from `from`: two strokes (open) or a filled wedge (closed), each
// side `core::kCalloutHead` long and 25 degrees off the shaft.
void arrowhead(cos::ContentStreamBuilder& content, double tipX, double tipY, double fromX, double fromY, bool closed)
{
    const double dx = tipX - fromX, dy = tipY - fromY;
    const double length = std::hypot(dx, dy);
    if (!(length > 1e-9)) return;
    const double ux = dx / length, uy = dy / length;
    const double spread = 25.0 * 3.14159265358979323846 / 180.0;
    const double c = std::cos(spread), s = std::sin(spread);
    const double h = core::kCalloutHead;
    const double ax = tipX - h * (ux * c - uy * s), ay = tipY - h * (uy * c + ux * s);
    const double bx = tipX - h * (ux * c + uy * s), by = tipY - h * (uy * c - ux * s);
    if (closed) {
        content.moveTo(ax, ay).lineTo(tipX, tipY).lineTo(bx, by).closePath().fillAndStroke();
    } else {
        content.moveTo(ax, ay).lineTo(tipX, tipY).lineTo(bx, by).stroke();
    }
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
    if (textColour.size() != 1 && textColour.size() != 3 && textColour.size() != 4) textColour = {0.0};
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
        const QString ending = a.lineEndings.empty() ? QStringLiteral("None") : a.lineEndings.front();
        const bool open = ending == QLatin1String("OpenArrow");
        const bool closed = ending == QLatin1String("ClosedArrow");
        if (!open && !closed && ending != QLatin1String("None")) return appearanceError("error.pdf.appearanceUnsupported", name);
        content.strokeColor(lineColour->data(), static_cast<int>(lineColour->size()));
        content.fillColor(lineColour->data(), static_cast<int>(lineColour->size()));
        content.lineWidth(border > 0.0 ? border : 1.0).lineJoin(1);
        content.moveTo(a.callout[0], a.callout[1]);
        for (std::size_t k = 2; k + 1 < a.callout.size(); k += 2) content.lineTo(a.callout[k], a.callout[k + 1]);
        content.stroke();
        if (open || closed) arrowhead(content, a.callout[0], a.callout[1], a.callout[2], a.callout[3], closed);
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
    return Appearance{formDictionary(a.rect, opacityState(opacityOf(a), false), std::move(fonts)), content.bytes()};
}

class DryRun final : public AppearanceObjects {
public:
    cos::CosRef shared(const std::string&, cos::CosObject) override { return cos::CosRef{0, 0}; }
};

} // namespace

cos::CosRef CollectedAppearanceObjects::shared(const std::string& key, cos::CosObject object)
{
    for (const Entry& entry : entries_) {
        if (entry.key == key) return entry.at;
    }
    const cos::CosRef at{first_ + static_cast<long long>(entries_.size()), 0};
    entries_.push_back(Entry{key, at, std::move(object)});
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
