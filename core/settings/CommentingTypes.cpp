// core/settings/CommentingTypes -- implementation. Stage 6 T8. See the header for the family and its validators.

#include "core/settings/CommentingTypes.h"

#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>

namespace rawpage::core {
namespace {

struct ToolRow {
    CommentingTool tool;
    std::string_view token;
};

constexpr ToolRow kTools[] = {
#define RAWPAGE_COMMENTING_TOOL(Enum, Token) {CommentingTool::Enum, Token},
#include "core/settings/CommentingTools.def"
};

struct DefaultRow {
    CommentingTool tool;
    ToolProperty property;
    std::string_view text;
};

constexpr DefaultRow kDefaults[] = {
#define RAWPAGE_TOOL_DEFAULT(Tool, Property, Default) {CommentingTool::Tool, ToolProperty::Property, Default},
#include "core/settings/ToolDefaults.def"
};

constexpr std::array<std::string_view, kToolPropertyCount> kPropertyNames = {
    "Colour", "FillColour", "Opacity", "Thickness", "LineStyle", "BlendMode", "FontSize"};

constexpr std::array<std::string_view, 3> kLineStyles = {"solid", "dashed", "dotted"};
constexpr std::array<std::string_view, 8> kBlends = {"normal", "multiply", "screen", "overlay",
                                                     "darken", "lighten", "difference", "exclusion"};
constexpr std::array<const char*, 8> kPdfBlends = {"", "Multiply", "Screen", "Overlay",
                                                   "Darken", "Lighten", "Difference", "Exclusion"};
constexpr std::array<std::string_view, 3> kPopups = {"always", "onHover", "never"};

QString number(double value)
{
    // The shortest text that reads back as the value, in the C locale -- the file's spelling.
    return QString::number(value, 'g', 6);
}

std::optional<double> numberIn(const QString& text, double minimum, double maximum)
{
    bool ok = false;
    const double value = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(value) || value < minimum || value > maximum) return std::nullopt;
    return value;
}

// A colour: 1, 3 or 4 components, each 0 to 1; `allowNone` lets an empty text mean no colour at all.
std::optional<std::vector<double>> colourFrom(const QString& text, bool allowNone)
{
    const QStringList parts = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return allowNone ? std::optional<std::vector<double>>(std::vector<double>{}) : std::nullopt;
    if (parts.size() != 1 && parts.size() != 3 && parts.size() != 4) return std::nullopt;
    std::vector<double> out;
    for (const QString& part : parts) {
        const std::optional<double> v = numberIn(part, 0.0, 1.0);
        if (!v) return std::nullopt;
        out.push_back(*v);
    }
    return out;
}

QString colourText(const std::vector<double>& colour)
{
    QStringList parts;
    for (const double v : colour) parts << number(v);
    return parts.join(QLatin1Char(' '));
}

template <class Names>
int indexOf(const Names& names, std::string_view name)
{
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return static_cast<int>(i);
    }
    return -1;
}

} // namespace

std::string_view commentingToolToken(CommentingTool tool) noexcept
{
    for (const ToolRow& row : kTools) {
        if (row.tool == tool) return row.token;
    }
    return "?";
}

std::optional<CommentingTool> commentingToolFromToken(std::string_view token) noexcept
{
    for (const ToolRow& row : kTools) {
        if (row.token == token) return row.tool;
    }
    return std::nullopt;
}

std::string_view toolPropertyName(ToolProperty property) noexcept
{
    return kPropertyNames[static_cast<std::size_t>(property)];
}

std::optional<ToolProperty> toolPropertyFromName(std::string_view name) noexcept
{
    const int at = indexOf(kPropertyNames, name);
    if (at < 0) return std::nullopt;
    return static_cast<ToolProperty>(at);
}

std::string_view lineStyleName(LineStyle style) noexcept
{
    return kLineStyles[static_cast<std::size_t>(style)];
}

std::optional<LineStyle> lineStyleFromName(std::string_view name) noexcept
{
    const int at = indexOf(kLineStyles, name);
    if (at < 0) return std::nullopt;
    return static_cast<LineStyle>(at);
}

std::string_view toolBlendName(ToolBlend blend) noexcept
{
    return kBlends[static_cast<std::size_t>(blend)];
}

std::optional<ToolBlend> toolBlendFromName(std::string_view name) noexcept
{
    const int at = indexOf(kBlends, name);
    if (at < 0) return std::nullopt;
    return static_cast<ToolBlend>(at);
}

QString pdfBlendName(ToolBlend blend)
{
    return QString::fromLatin1(kPdfBlends[static_cast<std::size_t>(blend)]);
}

std::optional<ToolBlend> toolBlendFromPdf(const QString& name)
{
    if (name.isEmpty() || name == QLatin1String("Normal") || name == QLatin1String("Compatible")) return ToolBlend::Normal;
    for (std::size_t i = 1; i < kPdfBlends.size(); ++i) {
        if (name == QLatin1String(kPdfBlends[i])) return static_cast<ToolBlend>(i);
    }
    return std::nullopt;
}

QString blendNameFor(bool highlight, ToolBlend blend)
{
    const ToolBlend own = highlight ? ToolBlend::Multiply : ToolBlend::Normal;
    if (blend == own) return QString();
    return blend == ToolBlend::Normal ? QStringLiteral("Normal") : pdfBlendName(blend);
}

std::vector<double> dashFor(LineStyle style, double width)
{
    const double w = std::isfinite(width) && width > 0.0 ? width : 1.0;
    switch (style) {
    case LineStyle::Solid: return {};
    case LineStyle::Dashed: return {std::max(3.0, 3.0 * w), std::max(3.0, 3.0 * w)};
    case LineStyle::Dotted: return {std::max(1.0, w), std::max(2.0, 2.0 * w)};
    }
    return {};
}

LineStyle lineStyleOf(const QString& borderStyle, const std::vector<double>& dash)
{
    if (borderStyle != QLatin1String("D")) return LineStyle::Solid;
    if (dash.size() >= 2 && dash[0] <= dash[1] / 2.0) return LineStyle::Dotted;
    return LineStyle::Dashed;
}

ToolDefaults builtInToolDefaults(CommentingTool tool)
{
    ToolDefaults values;
    for (const DefaultRow& row : kDefaults) {
        if (row.tool != tool) continue;
        const QString text = QString::fromUtf8(row.text.data(), static_cast<qsizetype>(row.text.size()));
        // A default the validator refuses is a defect in the table; `tst_stage6settings` holds every row to it.
        (void)parseToolProperty(row.property, text, values);
    }
    return values;
}

QString formatToolProperty(ToolProperty property, const ToolDefaults& values)
{
    switch (property) {
    case ToolProperty::Colour: return colourText(values.colour);
    case ToolProperty::FillColour: return colourText(values.fillColour);
    case ToolProperty::Opacity: return number(values.opacity);
    case ToolProperty::Thickness: return number(values.thickness);
    case ToolProperty::LineStyle: {
        const std::string_view name = lineStyleName(values.lineStyle);
        return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
    }
    case ToolProperty::BlendMode: {
        const std::string_view name = toolBlendName(values.blendMode);
        return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
    }
    case ToolProperty::FontSize: return number(values.fontSize);
    }
    return QString();
}

bool parseToolProperty(ToolProperty property, const QString& text, ToolDefaults& values)
{
    switch (property) {
    case ToolProperty::Colour: {
        const std::optional<std::vector<double>> colour = colourFrom(text, false);
        if (!colour) return false;
        values.colour = *colour;
        return true;
    }
    case ToolProperty::FillColour: {
        const std::optional<std::vector<double>> colour = colourFrom(text, true);
        if (!colour) return false;
        values.fillColour = *colour;
        return true;
    }
    case ToolProperty::Opacity: {
        const std::optional<double> v = numberIn(text, 0.0, 1.0);
        if (!v) return false;
        values.opacity = *v;
        return true;
    }
    case ToolProperty::Thickness: {
        const std::optional<double> v = numberIn(text, 0.0, kToolThicknessMax);
        if (!v) return false;
        values.thickness = *v;
        return true;
    }
    case ToolProperty::LineStyle: {
        const std::optional<LineStyle> style = lineStyleFromName(text.trimmed().toStdString());
        if (!style) return false;
        values.lineStyle = *style;
        return true;
    }
    case ToolProperty::BlendMode: {
        const std::optional<ToolBlend> blend = toolBlendFromName(text.trimmed().toStdString());
        if (!blend) return false;
        values.blendMode = *blend;
        return true;
    }
    case ToolProperty::FontSize: {
        const std::optional<double> v = numberIn(text, kToolFontSizeMin, kToolFontSizeMax);
        if (!v) return false;
        values.fontSize = *v;
        return true;
    }
    }
    return false;
}

std::optional<ToolDefaultKey> ToolDefaultKey::find(CommentingTool tool, ToolProperty property)
{
    if (!toolHasProperty(tool, property)) return std::nullopt;
    return ToolDefaultKey(tool, property);
}

std::optional<ToolDefaultKey> ToolDefaultKey::fromPath(const QString& path)
{
    const QString prefix = QString::fromLatin1(pathPrefix().data(), static_cast<qsizetype>(pathPrefix().size()));
    if (!path.startsWith(prefix)) return std::nullopt;
    const QString rest = path.mid(prefix.size());
    const qsizetype dot = rest.indexOf(QLatin1Char('.'));
    if (dot <= 0 || rest.indexOf(QLatin1Char('.'), dot + 1) >= 0) return std::nullopt;
    const std::optional<ToolProperty> property = toolPropertyFromName(rest.left(dot).toStdString());
    const std::optional<CommentingTool> tool = commentingToolFromToken(rest.mid(dot + 1).toStdString());
    if (!property || !tool) return std::nullopt;
    return find(*tool, *property);
}

QString ToolDefaultKey::path() const
{
    const std::string_view property = toolPropertyName(property_);
    const std::string_view tool = commentingToolToken(tool_);
    return QString::fromLatin1(pathPrefix().data(), static_cast<qsizetype>(pathPrefix().size()))
           + QString::fromLatin1(property.data(), static_cast<qsizetype>(property.size())) + QLatin1Char('.')
           + QString::fromLatin1(tool.data(), static_cast<qsizetype>(tool.size()));
}

QString ToolDefaultKey::defaultText() const
{
    for (const DefaultRow& row : kDefaults) {
        if (row.tool == tool_ && row.property == property_)
            return QString::fromUtf8(row.text.data(), static_cast<qsizetype>(row.text.size()));
    }
    return QString();
}

const std::vector<ToolDefaultKey>& allToolDefaultKeys()
{
    static const std::vector<ToolDefaultKey> keys = [] {
        std::vector<ToolDefaultKey> out;
        for (const DefaultRow& row : kDefaults) {
            if (std::optional<ToolDefaultKey> key = ToolDefaultKey::find(row.tool, row.property)) out.push_back(*key);
        }
        return out;
    }();
    return keys;
}

std::string_view popupBehaviourName(PopupBehaviour behaviour) noexcept
{
    return kPopups[static_cast<std::size_t>(behaviour)];
}

std::optional<PopupBehaviour> popupBehaviourFromName(std::string_view name) noexcept
{
    const int at = indexOf(kPopups, name);
    if (at < 0) return std::nullopt;
    return static_cast<PopupBehaviour>(at);
}

} // namespace rawpage::core
