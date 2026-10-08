// ui/viewer/ToolDefaults -- implementation. Stage 6 T8. See the header for the two layers.

#include "ui/viewer/ToolDefaults.h"

#include "core/doc/AnnotationGeometry.h"
#include "core/doc/TypedText.h"
#include "core/settings/Settings.h"

#include <algorithm>
#include <array>

namespace rawpage::ui {
namespace {

std::array<std::optional<core::ToolDefaults>, core::kCommentingToolCount>& sessionChanges()
{
    static std::array<std::optional<core::ToolDefaults>, core::kCommentingToolCount> changes{};
    return changes;
}

bool has(core::CommentingTool tool, core::ToolProperty property)
{
    return core::toolHasProperty(tool, property);
}

// Whether a kind has an inside a fill colours: a box, an ellipse, a closed polygon. A line's `/IC` fills its
// closed endings (ISO 32000-1 Table 175) -- a choice the properties bar makes on a selected line, not a tool's.
bool hasInside(core::AnnotationSubtype subtype)
{
    return subtype == core::AnnotationSubtype::Square || subtype == core::AnnotationSubtype::Circle
           || subtype == core::AnnotationSubtype::Polygon || subtype == core::AnnotationSubtype::FreeText;
}

} // namespace

QString blendNameFor(core::AnnotationSubtype subtype, core::ToolBlend blend)
{
    return core::blendNameFor(subtype == core::AnnotationSubtype::Highlight, blend);
}

core::ToolDefaults effectiveToolDefaults(core::CommentingTool tool, const core::Settings* settings)
{
    if (const std::optional<core::ToolDefaults>& change = sessionChanges()[static_cast<std::size_t>(tool)]) return *change;
    return settings != nullptr ? settings->toolDefaults(tool) : core::builtInToolDefaults(tool);
}

std::optional<core::ToolDefaults> sessionToolChange(core::CommentingTool tool)
{
    return sessionChanges()[static_cast<std::size_t>(tool)];
}

void setSessionToolChange(core::CommentingTool tool, const core::ToolDefaults& values)
{
    sessionChanges()[static_cast<std::size_t>(tool)] = values;
}

void clearSessionToolChange(core::CommentingTool tool)
{
    sessionChanges()[static_cast<std::size_t>(tool)].reset();
}

void clearSessionToolChanges()
{
    for (std::optional<core::ToolDefaults>& change : sessionChanges()) change.reset();
}

void writeToolDefaults(core::CommentingTool tool, core::Settings& settings)
{
    const core::ToolDefaults values = effectiveToolDefaults(tool, &settings);
    settings.setToolDefaults(tool, values);
    clearSessionToolChange(tool);
}

core::Annotation styledWith(core::Annotation a, core::CommentingTool tool, const core::ToolDefaults& values)
{
    if (has(tool, core::ToolProperty::Colour)) a.color = values.colour;
    if (has(tool, core::ToolProperty::FillColour) && hasInside(a.subtype)) a.interiorColor = values.fillColour;
    if (has(tool, core::ToolProperty::Opacity)) {
        if (values.opacity < 1.0) a.opacity = values.opacity;
        else a.opacity.reset();
    }
    if (has(tool, core::ToolProperty::Thickness)) a.borderWidth = values.thickness;
    if (has(tool, core::ToolProperty::LineStyle)) {
        a.borderStyle = values.lineStyle == core::LineStyle::Solid ? QStringLiteral("S") : QStringLiteral("D");
        a.dashPattern = core::dashFor(values.lineStyle, values.thickness);
    }
    if (has(tool, core::ToolProperty::BlendMode)) a.blendMode = blendNameFor(a.subtype, values.blendMode);
    return a;
}

std::optional<core::CommentingTool> toolOfAnnotation(const core::Annotation& a)
{
    using core::AnnotationSubtype;
    using core::CommentingTool;
    switch (a.subtype) {
    case AnnotationSubtype::Square: return CommentingTool::Rectangle;
    case AnnotationSubtype::Circle: return CommentingTool::Oval;
    case AnnotationSubtype::Polygon: return a.cloudIntensity ? CommentingTool::Cloud : CommentingTool::Polygon;
    case AnnotationSubtype::PolyLine: return CommentingTool::Polyline;
    case AnnotationSubtype::Line:
        return a.lineEndings.size() == 2 && a.lineEndings[1] == QLatin1String("OpenArrow") ? CommentingTool::Arrow
                                                                                            : CommentingTool::Line;
    case AnnotationSubtype::Ink: return CommentingTool::Pencil;
    case AnnotationSubtype::Highlight: return CommentingTool::Highlight;
    case AnnotationSubtype::Underline: return CommentingTool::Underline;
    case AnnotationSubtype::StrikeOut: return CommentingTool::Strikeout;
    case AnnotationSubtype::Squiggly: return CommentingTool::Squiggly;
    case AnnotationSubtype::Text: return CommentingTool::StickyNote;
    case AnnotationSubtype::Caret: return CommentingTool::InsertText;
    case AnnotationSubtype::FreeText:
        switch (core::freeTextKind(a)) {
        case core::FreeTextKind::Callout: return CommentingTool::Callout;
        case core::FreeTextKind::Typewriter: return CommentingTool::Typewriter;
        case core::FreeTextKind::Box: return CommentingTool::TextBox;
        }
        break;
    default: break;
    }
    return std::nullopt;
}

core::ToolDefaults toolDefaultsFrom(const core::Annotation& a, core::ToolDefaults base)
{
    if (a.color.size() == 1 || a.color.size() == 3 || a.color.size() == 4) base.colour = a.color;
    base.fillColour = a.interiorColor;
    base.opacity = a.opacity.value_or(1.0);
    base.thickness = std::min(core::drawnBorderWidth(a), core::kToolThicknessMax);
    base.lineStyle = core::lineStyleOf(a.borderStyle, a.dashPattern);
    const core::ToolBlend own = a.subtype == core::AnnotationSubtype::Highlight ? core::ToolBlend::Multiply : core::ToolBlend::Normal;
    base.blendMode = a.blendMode.isEmpty() ? own : core::toolBlendFromPdf(a.blendMode).value_or(own);
    if (a.subtype == core::AnnotationSubtype::FreeText)
        base.fontSize = std::clamp(core::freeTextStyle(a).size, core::kToolFontSizeMin, core::kToolFontSizeMax);
    return base;
}

} // namespace rawpage::ui
