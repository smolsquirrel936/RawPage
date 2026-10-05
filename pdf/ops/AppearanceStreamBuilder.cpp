// pdf/ops/AppearanceStreamBuilder -- implementation. Stage 6 T3. See the header for why we draw every
// appearance, and how a highlight is drawn.

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

core::Result<Appearance> highlight(const core::Annotation& a)
{
    const QLatin1String name = core::annotationSubtypeName(a.subtype);
    if (a.quadPoints.size() < 8 || a.quadPoints.size() % 8 != 0 || !a.rectFinite)
        return appearanceError("error.pdf.appearanceGeometry", name);
    for (const double v : a.quadPoints) {
        if (!std::isfinite(v)) return appearanceError("error.pdf.appearanceGeometry", name);
    }
    std::vector<double> color = a.color;
    if (color.empty()) color = {1.0, 1.0, 0.0};
    if (color.size() != 1 && color.size() != 3 && color.size() != 4)
        return appearanceError("error.pdf.appearanceGeometry", name);
    const double opacity = std::clamp(a.opacity.value_or(1.0), 0.0, 1.0);

    cos::ContentStreamBuilder content;
    content.save().graphicsState("GS0").fillColor(color.data(), static_cast<int>(color.size()));
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

    CosObject gs = CosObject::makeDictionary({
        {"Type", CosObject::makeName("ExtGState")},
        {"BM", CosObject::makeName("Multiply")},
        {"ca", CosObject::makeReal(opacity)},
        {"CA", CosObject::makeReal(opacity)},
    });
    return Appearance{formDictionary(a.rect, std::move(gs)), content.bytes()};
}

} // namespace

bool AppearanceStreamBuilder::supports(core::AnnotationSubtype subtype) noexcept
{
    return subtype == core::AnnotationSubtype::Highlight;
}

core::Result<Appearance> AppearanceStreamBuilder::normal(const core::Annotation& annotation)
{
    core::Result<Appearance> built = [&]() -> core::Result<Appearance> {
        switch (annotation.subtype) {
        case core::AnnotationSubtype::Highlight: return highlight(annotation);
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
