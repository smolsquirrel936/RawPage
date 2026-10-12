// ui/viewer/tools/AttachFileTool -- `annot.attachFile`: a file attached at a place on a page. Stage 6 T11, decision
// D6-17 (`Features.md` §4.2's *attach a file as a comment, shown as a pushpin on the page*).
//
// A CLICK, then the file. The tool takes a click (a press and a release closer than the drag distance, the note's
// rule), and the overlay asks the window which file -- `AnnotationOverlay::attachFileAt` -- and makes ONE
// `FileAttachment` carrying it, the pushpin's point where the click was. Choosing no file makes nothing. A drag
// makes nothing either: a pushpin has one size, as Acrobat's has.
//
// The file is read on the UI thread when it is chosen, once -- its bytes go into the command, so a replay, an
// undo and a redo attach the same bytes (D6-17), and its journal line puts them in a side file (D6-23). A file
// larger than one command may carry (`core::kMaxAttachmentBytes`) is refused by name before anything is made.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_ATTACHFILETOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_ATTACHFILETOOL_H

#include "ui/viewer/tools/AnnotationTool.h"
#include "ui/viewer/tools/Tool.h"

#include <QPoint>

namespace rawpage::ui {

class AttachFileTool final : public Tool {
public:
    static bool handles(AnnotationToolKind kind) noexcept { return kind == AnnotationToolKind::AttachFile; }

    ToolKind kind() const override { return ToolKind::Annotate; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

    void press(ToolHost& host, QPoint pos) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;
    bool isDragging() const override { return pressed_; }

private:
    bool pressed_ = false;
    QPoint pressedAt_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_ATTACHFILETOOL_H
