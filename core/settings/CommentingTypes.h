// core/settings/CommentingTypes -- the vocabulary of the Commenting preferences: which tool, which property,
// the values they hold, and the `commenting/default<Property>.<tool>` key family. Stage 6 T8, decision D6-13
// (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// A FAMILY, DECLARED ONCE, THAT CANNOT BE SPELLED WRONG (D1-10, D6-13)
// ===========================================================================================
// Twenty-three tools times seven properties is not a table anyone maintains as rows, and most pairs mean
// nothing -- a pencil has no font size. So the family is the tools (`CommentingTools.def`) crossed with the
// properties, kept only where `ToolDefaults.def` has a row, and a key exists only as a `ToolDefaultKey`:
// `ToolDefaultKey::of<Tool, Property>()` is a COMPILE error for a pair with no row (`toolHasProperty` is
// `constexpr`, and a `static_assert` asks it), and `ToolDefaultKey::find` answers nullopt for one at run
// time. There is no constructor from a string -- `fromPath` resolves a path a FILE holds and refuses one
// outside the family -- so `shortcuts/<context>/<action>`'s rule holds here too: an undeclared key cannot be
// named in code.
//
// ===========================================================================================
// THE VALUES ARE TEXT IN THE FILE, VALIDATED ON EVERY READ
// ===========================================================================================
// One `parse…`/`format…` pair per property type, used by the settings store, the Preferences page and the
// table's defaults alike, so the file, the page and the default cannot disagree about what `1 1 0` means. A
// value the validator refuses reads as its row's default (`Settings::toolDefaults`), never as a guess.
//
// Pure: QtCore only -- `tst_stage6settings` in `rawpage_tests`.

#ifndef RAWPAGE_CORE_SETTINGS_COMMENTINGTYPES_H
#define RAWPAGE_CORE_SETTINGS_COMMENTINGTYPES_H

#include <QString>

#include <optional>
#include <string_view>
#include <vector>

namespace rawpage::core {

// ---- The tools ---------------------------------------------------------------------------------------
enum class CommentingTool {
#define RAWPAGE_COMMENTING_TOOL(Enum, Token) Enum,
#include "core/settings/CommentingTools.def"
};

inline constexpr int kCommentingToolCount = 0
#define RAWPAGE_COMMENTING_TOOL(Enum, Token) +1
#include "core/settings/CommentingTools.def"
    ;

// The token the family's paths use: `highlight`, `textBox`.
std::string_view commentingToolToken(CommentingTool tool) noexcept;
std::optional<CommentingTool> commentingToolFromToken(std::string_view token) noexcept;

// ---- The properties --------------------------------------------------------------------------------
// Stage 6 T14 appended two (D6-25): a redaction mark's `OverlayText` and whether it repeats. Appended, never inserted:
// a property's number is its index in the editors.
enum class ToolProperty { Colour, FillColour, Opacity, Thickness, LineStyle, BlendMode, FontSize, OverlayText, Repeat };
inline constexpr int kToolPropertyCount = 9;

// `Colour`, `FillColour` … -- the `<Property>` of `commenting/default<Property>.<tool>`.
std::string_view toolPropertyName(ToolProperty property) noexcept;
std::optional<ToolProperty> toolPropertyFromName(std::string_view name) noexcept;

// Whether `ToolDefaults.def` declares `property` for `tool`. Constexpr, so `ToolDefaultKey::of` can refuse a pair
// at compile time.
constexpr bool toolHasProperty(CommentingTool tool, ToolProperty property)
{
#define RAWPAGE_TOOL_DEFAULT(Tool, Property, Default)                                                      \
    if (tool == CommentingTool::Tool && property == ToolProperty::Property) return true;
#include "core/settings/ToolDefaults.def"
    return false;
}

// ---- The values ------------------------------------------------------------------------------------
// How an outline is drawn: solid, or `/BS /S /D` with the dash `dashFor` gives.
enum class LineStyle { Solid, Dashed, Dotted };
// The blends a tool may draw with -- the separable ones a person reaches for; the PDF name `pdfBlendName` gives.
enum class ToolBlend { Normal, Multiply, Screen, Overlay, Darken, Lighten, Difference, Exclusion };

std::string_view lineStyleName(LineStyle style) noexcept;           // solid, dashed, dotted
std::optional<LineStyle> lineStyleFromName(std::string_view name) noexcept;
std::string_view toolBlendName(ToolBlend blend) noexcept;           // normal, multiply, …
std::optional<ToolBlend> toolBlendFromName(std::string_view name) noexcept;
// `/BM`'s name: empty for `Normal` (written as nothing), else `Multiply`, `Screen` …
QString pdfBlendName(ToolBlend blend);
// The tool blend a PDF `/BM` name is, for the ones a tool offers; nullopt for any other.
std::optional<ToolBlend> toolBlendFromPdf(const QString& name);
// The `/BM` an annotation is written with for `blend`: nothing for its kind's own -- a highlight's `Multiply`,
// everything else's `Normal` -- so one drawn the way its kind always is carries no key a PDF 1.7 reader lacks;
// else the name, `Normal` included (a highlight told not to multiply).
QString blendNameFor(bool highlight, ToolBlend blend);

// The `/BS /D` dash array a style is drawn with at `width`: none for solid; for dashed, dashes and gaps three
// times the width (three points at least); for dotted, a width on and two off (a point and two at least).
std::vector<double> dashFor(LineStyle style, double width);
// The style a `/BS` reads as: solid unless its style is `D`, then dotted when its first dash is no longer than
// its first gap's half, else dashed.
LineStyle lineStyleOf(const QString& borderStyle, const std::vector<double>& dash);

// Every property of one tool's defaults. A property the tool has no row for carries `builtInToolDefaults`'
// value and is neither written nor read.
struct ToolDefaults {
    std::vector<double> colour{0.0, 0.0, 0.0};   // 1, 3 or 4 components, 0 to 1
    std::vector<double> fillColour;              // empty: no fill
    double opacity = 1.0;
    double thickness = 1.0;
    LineStyle lineStyle = LineStyle::Solid;
    ToolBlend blendMode = ToolBlend::Normal;
    double fontSize = 12.0;
    QString overlayText;                         // T14: a redaction mark's, at most `kToolOverlayTextMax` characters
    bool repeat = false;                         // T14: whether a mark's overlay text repeats

    bool operator==(const ToolDefaults&) const = default;
};

// The table's defaults for `tool`.
ToolDefaults builtInToolDefaults(CommentingTool tool);

// One property's value in the file, as text, and back. `parse…` refuses anything out of its range.
QString formatToolProperty(ToolProperty property, const ToolDefaults& values);
// Writes the parsed value into `values`; false (and `values` unchanged) for text the validator refuses.
bool parseToolProperty(ToolProperty property, const QString& text, ToolDefaults& values);

inline constexpr double kToolThicknessMax = 24.0;
inline constexpr double kToolFontSizeMin = 4.0;
inline constexpr double kToolFontSizeMax = 144.0;
// T14: an overlay text in the settings file -- printable, on one line, no longer than a command carries.
inline constexpr int kToolOverlayTextMax = 512;

// ---- The key -----------------------------------------------------------------------------------------
class ToolDefaultKey {
public:
    // THE ONLY WAY TO NAME A KEY IN CODE -- and a pair `ToolDefaults.def` does not declare does not compile.
    template <CommentingTool Tool, ToolProperty Property>
    static constexpr ToolDefaultKey of()
    {
        static_assert(toolHasProperty(Tool, Property),
                      "undeclared tool default: core/settings/ToolDefaults.def has no row for this tool and property");
        return ToolDefaultKey(Tool, Property);
    }
    // At run time: the key for a pair, or nullopt when `ToolDefaults.def` has no row for it.
    static std::optional<ToolDefaultKey> find(CommentingTool tool, ToolProperty property);
    // A path a settings FILE holds, resolved: nullopt for anything outside the family.
    static std::optional<ToolDefaultKey> fromPath(const QString& path);

    CommentingTool tool() const noexcept { return tool_; }
    ToolProperty property() const noexcept { return property_; }

    // `commenting/defaultColour.highlight`.
    QString path() const;
    // The row's default, as the file would hold it.
    QString defaultText() const;

    static constexpr std::string_view pathPrefix() { return "commenting/default"; }

private:
    constexpr ToolDefaultKey(CommentingTool tool, ToolProperty property) : tool_(tool), property_(property) {}

    CommentingTool tool_;
    ToolProperty property_;
};

// Every declared key, in `ToolDefaults.def`'s order -- what the Preferences page and a reset walk.
const std::vector<ToolDefaultKey>& allToolDefaultKeys();

// ---- The one other Commenting value: when a note's pop-up shows (D6-28) --------------------------------
enum class PopupBehaviour { Always, OnHover, Never };
std::string_view popupBehaviourName(PopupBehaviour behaviour) noexcept;   // always, onHover, never
std::optional<PopupBehaviour> popupBehaviourFromName(std::string_view name) noexcept;

} // namespace rawpage::core

#endif // RAWPAGE_CORE_SETTINGS_COMMENTINGTYPES_H
