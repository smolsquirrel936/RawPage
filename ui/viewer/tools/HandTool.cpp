#include "ui/viewer/tools/HandTool.h"

#include "ui/viewer/ViewportController.h"

namespace rawpage::ui {

void HandTool::press(ToolHost& host, QPoint pos)
{
    dragging_ = true;
    origin_ = pos;
    scrollOrigin_ = host.controller().scroll();
}

void HandTool::move(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;

    // THE SIGN IS THE WHOLE TOOL. Dragging right moves the PAGE right, which means showing
    // content further LEFT, which means a SMALLER scroll offset. See the header.
    const double ratio = host.devicePixelRatio();
    const QPoint moved = pos - origin_;
    host.controller().setScroll(core::DevicePoint{scrollOrigin_.x - moved.x() * ratio,
                                                  scrollOrigin_.y - moved.y() * ratio});
}

void HandTool::release(ToolHost& host, QPoint pos)
{
    (void)host;
    (void)pos;
    // Nothing to finish: the scroll has been following the pointer the whole way, which is what
    // makes a pan feel like a pan rather than like a committed gesture.
    dragging_ = false;
}

void HandTool::cancel(ToolHost& host)
{
    if (!dragging_) return;
    dragging_ = false;

    // BACK WHERE IT STARTED. `setScroll` clamps, so a restore onto a document that has since got
    // shorter lands inside it rather than past its end -- which is `ViewportController`'s rule and
    // not one this tool re-implements.
    host.controller().setScroll(scrollOrigin_);
    host.requestRepaint();
}

} // namespace rawpage::ui
