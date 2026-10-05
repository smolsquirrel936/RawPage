// ui/viewer/tools/NoteTool -- the sticky note (`Ctrl+Alt+M`, §8's Viewer row). Stage 6 T6, decisions D6-12,
// D6-13 and D6-28 (`Stage6-Annotations-Plan.md` §3).
//
// A CLICK on the page asks for the note's text where the click was (`ToolHost::requestAnnotationText`), and
// when it is typed the note is made: a `Text` annotation whose icon's top-left corner is the click, and its
// `Popup`, as ONE `AddAnnotationCommand` (`AnnotationOverlay::addNote`) carrying the person's name. A drag
// makes nothing -- a note is a place, not an area -- and `Esc` in the editor makes nothing. A press on a note
// already there is the overlay's: it is picked up and moved, never covered by a second one.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_NOTETOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_NOTETOOL_H

#include "ui/viewer/tools/Tool.h"

namespace rawpage::ui {

class NoteTool final : public Tool {
public:
    ToolKind kind() const override { return ToolKind::Annotate; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

    void press(ToolHost& host, QPoint pos) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;
    bool isDragging() const override { return dragging_; }

private:
    bool dragging_ = false;
    QPoint pressedAt_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_NOTETOOL_H
