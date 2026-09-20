// The Hand tool -- `Features.md` §1.2 *"Hand tool panning by dragging the page"*, Stage 4 T7.
//
// THE PAGE FOLLOWS THE HAND, SO THE SCROLL MOVES THE OTHER WAY. Getting that sign wrong is the
// classic hand-tool defect: it is unmistakable to a person and completely invisible to a case that
// only checks that something moved, which is why `tst_tools` asserts the DIRECTION and not merely
// the displacement. The rule moved here from `PageView` at T7 unchanged.
//
// IT IS THE ONE STAGE 4 TOOL THAT CHANGES THE VIEW *DURING* A DRAG, which is what makes `Esc`
// mean something different for it than for the marquee: the marquee has only to stop drawing,
// while this has to put the scroll back where the drag started. So it remembers the origin, and
// `cancel()` restores it -- guide case **A-42**, whose whole point is that the view is exactly
// where it was.
//
// A pan is a SCROLL and not deliberate navigation, so it records no view-history entry: `Alt+Left`
// after panning around a page should go back to wherever you last jumped FROM, not unwind the
// drag. That is `ViewportController::recordView`'s own distinction, applied at the one Stage 4
// tool that could get it wrong.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_HANDTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_HANDTOOL_H

#include "core/Geometry.h"
#include "ui/viewer/tools/Tool.h"

namespace rawpage::ui {

class HandTool final : public Tool {
public:
    ToolKind kind() const override { return ToolKind::Hand; }

    Qt::CursorShape cursor() const override { return Qt::OpenHandCursor; }
    Qt::CursorShape draggingCursor() const override { return Qt::ClosedHandCursor; }

    void press(ToolHost& host, QPoint pos) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;

    bool isDragging() const override { return dragging_; }

private:
    bool              dragging_ = false;
    QPoint            origin_;
    core::DevicePoint scrollOrigin_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_HANDTOOL_H
