// ui/viewer/tools/AnnotationTool -- an annotation tool, ARMED. Stage 6 T5, decision D6-9
// (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// WHAT T5 ARMS, AND WHAT IT DOES NOT YET MAKE
// ===========================================================================================
// `Features.md` §8's fourteen `Ctrl+Alt+` chords each arm a tool -- the sticky note, the highlighter, the
// rectangle, the stamp, the redaction mark -- and arming one puts the page in the **Annotation tools**
// context (D6-9), which resolves through the Viewer so every page chord still works. That much is T5's,
// with the overlay (`ui/viewer/AnnotationOverlay`): with any of them armed a person can select, move,
// resize, nudge and delete what is already on the page, and `Esc` returns to the Selection tool.
//
// **What each tool CREATES is its own task's** -- markup and notes T6, typed text T7, shapes and ink T8,
// stamps T10, measurement T12, redaction marks T14 -- and arrives as the classes `Architecture-FileMap.md`
// §3.14 names (`MarkupTool`, `NoteTool`, …) on this base. Until then a press on an empty part of the page
// makes nothing: the tool is armed, says which it is, and creates nothing. That is the state T5 leaves,
// recorded rather than hidden.
//
// The kind is a value with an ASCII name, for the reason `ToolKind` carries one.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_ANNOTATIONTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_ANNOTATIONTOOL_H

#include "core/cmd/ActionId.h"
#include "ui/viewer/tools/Tool.h"

#include <optional>

namespace rawpage::ui {

// The fourteen tools §8's Viewer table arms, in its order.
enum class AnnotationToolKind {
    StickyNote,
    Highlight,
    Underline,
    Strikeout,
    TextBox,
    Line,
    Rectangle,
    Oval,
    Polygon,
    Pencil,
    Stamp,
    MeasureDistance,
    MeasureArea,
    RedactMark,
};

inline constexpr int kAnnotationToolKindCount = 14;

const char* annotationToolKindName(AnnotationToolKind kind) noexcept;

// THE COMMAND THAT ARMS EACH, and back: the fourteen Viewer rows of `Features.md` §8 (D6-9). One table,
// read both ways, so a registration and a label cannot name different tools.
core::ActionId armingAction(AnnotationToolKind kind) noexcept;
std::optional<AnnotationToolKind> armedBy(core::ActionId action) noexcept;

class AnnotationTool final : public Tool {
public:
    ToolKind kind() const override { return ToolKind::Annotate; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

    AnnotationToolKind which() const noexcept { return which_; }
    void setWhich(AnnotationToolKind which) noexcept { which_ = which; }

    // Creates nothing yet -- see the header. The overlay takes every press that lands on an annotation
    // before the tool sees it.
    void press(ToolHost&, QPoint) override {}
    void move(ToolHost&, QPoint) override {}
    void release(ToolHost&, QPoint) override {}
    void cancel(ToolHost&) override {}
    bool isDragging() const override { return false; }

private:
    AnnotationToolKind which_ = AnnotationToolKind::Highlight;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_ANNOTATIONTOOL_H
