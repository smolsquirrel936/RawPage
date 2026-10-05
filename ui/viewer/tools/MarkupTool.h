// ui/viewer/tools/MarkupTool -- the text markup tools: highlight, underline, strikeout and squiggly, and the
// two text edits, replace and insert. Stage 6 T6, decisions D6-1 and D6-12 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// A MARKUP TOOL IS THE SELECTION TOOL THAT MARKS WHAT IT SELECTED
// ===========================================================================================
// A drag selects text exactly as the Selection tool does -- in reading order, across pages, a word on two
// clicks and a line on three, a column with `Alt` -- through the window's `SelectionOverlay`, and on the
// release what it selected is MARKED: one `AddAnnotationCommand` over the selected runs
// (`AnnotationOverlay::markText`), and the selection is let go. A click that selects nothing marks nothing.
// That is what makes a highlight cover the glyphs a person dragged across (guide case A-49): the quads are
// the selection's own runs, never a rectangle the pointer drew.
//
//   * **Replace** does the same drag, then asks for the replacement (`ToolHost::requestAnnotationText`) and
//     makes the strikeout and its caret as ONE command when it is typed (guide case A-51).
//   * **Insert** is a CLICK: the caret goes on the boundary between the two characters nearest it (guide
//     case A-52), holding what is typed.
//
// Colour and opacity are the tool's defaults (`sessionToolDefaults`, D6-31(c)) when the mark is made. After
// it, `ToolHost::annotationToolUsed` returns to the Selection tool unless `K` keeps this one.
//
// A press on an annotation is the overlay's first (`ui/viewer/PageView`); a press on a highlight starts a
// selection here and a click on it selects the highlight there (D6-30).

#ifndef RAWPAGE_UI_VIEWER_TOOLS_MARKUPTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_MARKUPTOOL_H

#include "ui/viewer/tools/AnnotationTool.h"
#include "ui/viewer/tools/Tool.h"

namespace rawpage::ui {

class MarkupTool final : public Tool {
public:
    ToolKind kind() const override { return ToolKind::Annotate; }
    // An I-beam: the surface this tool works on is text.
    Qt::CursorShape cursor() const override { return Qt::IBeamCursor; }

    AnnotationToolKind which() const noexcept { return which_; }
    void setWhich(AnnotationToolKind which) noexcept { which_ = which; }
    // Whether this tool is the one for `kind`: the four markup kinds, replace and insert.
    static bool handles(AnnotationToolKind kind) noexcept;

    void press(ToolHost& host, QPoint pos) override { pressWith(host, pos, PressContext{}); }
    void pressWith(ToolHost& host, QPoint pos, const PressContext& context) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;
    bool isDragging() const override { return dragging_; }

private:
    AnnotationToolKind which_ = AnnotationToolKind::Highlight;
    bool dragging_ = false;
    QPoint pressedAt_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_MARKUPTOOL_H
