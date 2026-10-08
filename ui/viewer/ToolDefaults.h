// ui/viewer/ToolDefaults -- what an annotation tool draws with: the settings family `commenting/default<Property>.<tool>`
// under the SESSION's own changes. Stage 6 T8, decision D6-13 (`Stage6-Annotations-Plan.md` §3); it replaces T5's
// `sessionToolDefaults` (D6-31(c)), which held the defaults for the life of the process and nothing else.
//
// TWO LAYERS, AS ACROBAT HAS THEM. The properties bar changes the armed tool's CURRENT properties -- this session's,
// for every window -- and `Ctrl+Alt+Enter` (`annot.setToolDefault`, §8) makes them the tool's DEFAULTS, written to
// the family and so kept across sessions; the Commenting page edits the defaults directly. So what a tool draws
// with is the session's change where the bar made one, else the family as the settings file holds it, else the
// table's default (`core/settings/ToolDefaults.def`). Writing the defaults clears the session's change for that
// tool: what it draws with is then what was written.
//
// `styledWith` is the one place a tool's defaults become an annotation's fields, so every tool applies a colour,
// a fill, a width, a dash and a blend the same way -- and only where the tool has that property.

#ifndef RAWPAGE_UI_VIEWER_TOOLDEFAULTS_H
#define RAWPAGE_UI_VIEWER_TOOLDEFAULTS_H

#include "core/doc/Annotation.h"
#include "core/settings/CommentingTypes.h"

#include <optional>

namespace rawpage::core {
class Settings;
}

namespace rawpage::ui {

// What `tool` draws with now: the session's change, else `settings`' family (null: the table's defaults).
core::ToolDefaults effectiveToolDefaults(core::CommentingTool tool, const core::Settings* settings);

// The session's change to `tool`'s properties -- the properties bar's -- shared by every window of the process.
std::optional<core::ToolDefaults> sessionToolChange(core::CommentingTool tool);
void setSessionToolChange(core::CommentingTool tool, const core::ToolDefaults& values);
void clearSessionToolChange(core::CommentingTool tool);
// Every tool's session change dropped -- a case's fixture, which shares the process with the next case.
void clearSessionToolChanges();

// `Ctrl+Alt+Enter`: `tool`'s current properties written as its defaults, and its session change cleared.
void writeToolDefaults(core::CommentingTool tool, core::Settings& settings);

// `a` with `values` applied as `tool` applies them: its colour as `/C`, a fill as `/IC` where the kind has an
// inside, the opacity as `/CA` (below 1 only), the thickness as `/BS /W`, the line style as `/BS /S` and `/D`,
// and the blend as `/BM` -- each only where `ToolDefaults.def` declares it for the tool.
core::Annotation styledWith(core::Annotation a, core::CommentingTool tool, const core::ToolDefaults& values);

// The `/BM` an annotation of `subtype` is written with for `blend`: nothing for the kind's own (a highlight's
// `Multiply`, everyone else's `Normal`), else the name -- `Normal` included, for a highlight told not to multiply.
QString blendNameFor(core::AnnotationSubtype subtype, core::ToolBlend blend);

// The tool that makes an annotation like `a` -- a Square the rectangle, a cloudy Polygon the cloud, a Line with an
// open arrow at its end the arrow -- or nullopt for a kind no tool makes (a stamp, a pop-up).
std::optional<core::CommentingTool> toolOfAnnotation(const core::Annotation& a);
// `a`'s properties as a tool's values, over `base` for those it does not have: what `Ctrl+Alt+Enter` writes for a
// selected annotation.
core::ToolDefaults toolDefaultsFrom(const core::Annotation& a, core::ToolDefaults base);

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLDEFAULTS_H
