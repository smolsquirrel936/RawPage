// The Selection tool -- `Features.md` §8's `V`. Stage 4 T7 made it; Stage 5 T5 gave it its subject
// (decision D5-8, `Stage5-TextSearch-Plan.md` §3).
//
// ===========================================================================================
// WHAT IT DOES WITH A GESTURE
// ===========================================================================================
//   * A DRAG selects text in the document's READING order, across pages (`core/text/TextSelection`).
//   * Two, three and four CLICKS select the word, the line and the block under the pointer, and a drag
//     that started so extends by the same unit.
//   * `Alt`+DRAG selects a COLUMN -- the spans of every line the rectangle crosses, on the page the drag
//     started on (`Features.md` §8's Viewer row, Q2). `Alt` here is a modifier held during a mouse
//     gesture, which is not the key tips: `ShortcutBinder` answers a lone `Alt` on its RELEASE and only
//     if nothing else happened while it was down, and a mouse press is something else (D4-22).
//   * `Shift`+CLICK extends the selection. `Shift` is also Stage 4's temporary-hand modifier, and the two
//     are reconciled by the gesture's shape rather than by taking one away: a `Shift`+DRAG pans, as it did,
//     and a `Shift`+CLICK -- a press and release that did not move -- extends. `ToolController` owns the
//     decision, because it is the one object that sees both tools (D5-8 as clarified at T5).
//
// The selection itself -- one per document window, shown in every pane -- is `ui/viewer/SelectionOverlay`'s,
// reached through `ToolHost::selection()`. This tool names no widget: it turns viewport positions into
// page points through the host and hands them on.
//
// It is still the DEFAULT tool: the mode a person is in when they have not chosen one, and a viewer whose
// default was the hand would make every click a pan.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_SELECTTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_SELECTTOOL_H

#include "ui/viewer/tools/Tool.h"

namespace rawpage::ui {

class SelectTool final : public Tool {
public:
    ToolKind kind() const override { return ToolKind::Select; }

    // AN I-BEAM: the pointer says *this surface is text*.
    Qt::CursorShape cursor() const override { return Qt::IBeamCursor; }

    void press(ToolHost& host, QPoint pos) override { pressWith(host, pos, PressContext{}); }
    void pressWith(ToolHost& host, QPoint pos, const PressContext& context) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;

    // `Esc` under a live drag: the selection goes back to what it was before the press.
    void cancel(ToolHost& host) override;

    bool isDragging() const override { return dragging_; }

    // A `Shift`+click, decided by `ToolController` under the temporary hand (see the header).
    void extendTo(ToolHost& host, QPoint pos);

    // The modifier that makes a drag a column, stated once so a case asks the tool rather than copying it.
    static constexpr Qt::KeyboardModifier kColumnModifier = Qt::AltModifier;

private:
    bool dragging_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_SELECTTOOL_H
