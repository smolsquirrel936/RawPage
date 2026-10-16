// core/doc/Redaction -- implementation. Stage 6 T14. See the header for what a mark is and why it removes nothing.

#include "core/doc/Redaction.h"

#include "core/doc/TypedText.h"

#include <algorithm>
#include <cmath>

namespace rawpage::core {
namespace {

// The luminance of a device colour, 0 (black) to 1 (white): gray as it is, RGB by ITU-R BT.601's weights, CMYK as the
// light left after its inks.
double luminance(const std::vector<double>& colour)
{
    const auto c = [&colour](std::size_t i) { return std::clamp(colour[i], 0.0, 1.0); };
    switch (colour.size()) {
    case 1: return c(0);
    case 3: return 0.299 * c(0) + 0.587 * c(1) + 0.114 * c(2);
    case 4: return (1.0 - c(3)) * (1.0 - (0.299 * c(0) + 0.587 * c(1) + 0.114 * c(2)));
    default: return 1.0;
    }
}

Annotation markOf(int page, const RedactionStyle& style, const QString& author, std::int64_t utcNow)
{
    Annotation a = newAnnotation(AnnotationSubtype::Redact, page, author, utcNow);
    a.color = style.outline;
    a.interiorColor = style.fill;
    if (style.opacity < 1.0) a.opacity = std::clamp(style.opacity, 0.0, 1.0);
    a.overlayText = style.overlayText.left(kMaxOverlayTextLength);
    a.repeat = style.repeat;
    a.defaultAppearance = redactionDefaultAppearance(style.textSize, style.fill);
    return a;
}

} // namespace

std::vector<double> redactionTextColour(const std::vector<double>& fill)
{
    const bool dark = (fill.size() == 1 || fill.size() == 3 || fill.size() == 4) && luminance(fill) < 0.5;
    return dark ? std::vector<double>{1.0} : std::vector<double>{0.0};
}

QString redactionDefaultAppearance(double size, const std::vector<double>& fill)
{
    DefaultAppearance da;
    da.font = QStringLiteral("Helv");
    da.size = std::isfinite(size) && size > 0.0 ? size : kRedactionDefaultTextSize;
    da.colour = redactionTextColour(fill);
    return da.toString();
}

std::optional<Annotation> newTextRedaction(int page, const std::vector<double>& quadPoints, const RedactionStyle& style,
                                           const QString& author, std::int64_t utcNow)
{
    const std::optional<PdfRect> rect = markupRect(quadPoints);
    if (!rect) return std::nullopt;
    Annotation a = markOf(page, style, author, utcNow);
    a.quadPoints = std::vector<double>(quadPoints.begin(), quadPoints.begin() + static_cast<std::ptrdiff_t>(quadPoints.size() / 8 * 8));
    a.rect = *rect;
    return a;
}

std::optional<Annotation> newAreaRedaction(int page, const PdfRect& area, const RedactionStyle& style, const QString& author,
                                           std::int64_t utcNow)
{
    const std::optional<PdfRect> rect = normalizedRect(area.left(), area.bottom(), area.right(), area.top());
    if (!rect || !(rect->size.width > 0.0) || !(rect->size.height > 0.0)) return std::nullopt;
    Annotation a = markOf(page, style, author, utcNow);
    a.rect = *rect;
    return a;
}

Annotation withRedactionFill(Annotation mark, const std::vector<double>& fill)
{
    mark.interiorColor = fill;
    DefaultAppearance da = DefaultAppearance::parse(mark.defaultAppearance).value_or(DefaultAppearance{});
    if (da.font.isEmpty()) da.font = QStringLiteral("Helv");
    if (!(da.size > 0.0)) da.size = kRedactionDefaultTextSize;
    da.colour = redactionTextColour(fill);
    mark.defaultAppearance = da.toString();
    return mark;
}

Annotation newSoundComment(int page, const PdfPoint& topLeft, const SoundData& sound, const std::vector<double>& colour,
                           const QString& author, std::int64_t utcNow)
{
    Annotation a = newAnnotation(AnnotationSubtype::Sound, page, author, utcNow);
    a.rect = PdfRect{PdfPoint{topLeft.x, topLeft.y - kSoundIconSize}, PdfSize{kSoundIconSize, kSoundIconSize}};
    a.icon = QStringLiteral("Speaker");
    a.color = colour;
    a.sound = sound;
    return a;
}

} // namespace rawpage::core
