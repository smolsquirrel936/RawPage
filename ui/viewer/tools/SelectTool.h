// The Selection tool -- `Features.md` §8's `V`, Stage 4 T7.
//
// ===========================================================================================
// IT SELECTS NOTHING, IT IS THE DEFAULT, AND BOTH OF THOSE ARE DELIBERATE.
// ===========================================================================================
// Text selection needs `core/text/PageTextModel` and `ITextExtractor`, which are **Stage 5's**:
// the backend census says so out loud at every launch — *3 of 7 interfaces implemented; missing:
// ITextExtractor, …*. So this tool arms, shows an I-beam over the page, and does nothing with a
// drag. `Stage4-ViewingSurface-Plan.md` §11 lists that as an explicit non-goal and the guide's
// out-of-scope table repeats it, so a bug report saying *dragging selects nothing* is answered
// before it is filed.
//
// **It exists now rather than at Stage 5** for the reason `PageView` gave when it carried the
// three-tool enum at Stage 2 T14: `V` is bound in §8, `tool.select` is registered, and a chord
// that resolves to nothing at all is indistinguishable from a chord that is broken. Arming a tool
// that says *I am the selection tool and I have nothing to select yet* is the honest state; having
// `V` do nothing is not.
//
// It is also the DEFAULT tool, which matters more than it looks: the default is the mode a person
// is in when they have not chosen one, and a viewer whose default was the hand would make every
// click a pan. Stage 5 gives this tool its subject and changes nothing about that.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_SELECTTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_SELECTTOOL_H

#include "ui/viewer/tools/Tool.h"

namespace rawpage::ui {

class SelectTool final : public Tool {
public:
    ToolKind kind() const override { return ToolKind::Select; }

    // AN I-BEAM, not an arrow. It is the one piece of feedback the tool can honestly give: the
    // pointer says *this surface is about to be treated as text*, which is true of the mode even
    // while the selection it would make is Stage 5's.
    Qt::CursorShape cursor() const override { return Qt::IBeamCursor; }

    // Every one of these is deliberately empty. They are not `= 0` left unimplemented — the base
    // requires them — and they are not a stub pretending to work: this is convention 6's shape
    // for a tool, where *absent* is expressed by doing nothing visible rather than by a handler
    // that reports success.
    void press(ToolHost&, QPoint) override {}
    void move(ToolHost&, QPoint) override {}
    void release(ToolHost&, QPoint) override {}
    void cancel(ToolHost&) override {}

    // NEVER. `Esc` with the selection tool armed must therefore fall through to *nothing was
    // cancelled*, which is the control in guide case A-42: `Esc` with no drag in progress changes
    // neither the tool nor the view.
    bool isDragging() const override { return false; }
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_SELECTTOOL_H
