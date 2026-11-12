// ui/viewer/tools/LinkTool -- `link.create`'s tool: drag a rectangle on a page, and a link is made over it. Stage 7 T12
// (`Stage7-Assembly-Plan.md` D7-12, D7-35; `Features.md` §5.10, *create a link by dragging a rectangle*).
//
// THE CROP TOOL'S GESTURE (`ui/viewer/tools/CropTool`), for the same reasons: the box is in PAGE space from the press on --
// the press names the page and every later position is read on that page, so a drag that leaves it keeps its edge -- and
// each corner is SNAPPED as a measurement's is, so a link dragged to a drawn frame lands on it. The band painted is the
// two snapped corners drawn back through the view, so what is seen is what will be written (rule 6, A-83).
//
// ON RELEASE IT HANDS THE BOX OVER (`ToolHost::linkFromBox`) and keeps nothing: the window asks where the link goes --
// `LinkPropertiesDialog` -- and runs ONE `AddAnnotationCommand` with a `Link`. The tool constructs no command itself (D4-1's
// rule for a tool: a host). A drag under `MarqueeZoomTool::kMinDragPixels` on either side is a click and makes nothing;
// `Esc` during the drag abandons it and nothing is asked. A link made FROM TEXT is not this tool's: `link.create` with a
// text selection makes one link per line of it (`MainWindow::createLinksFromSelection`).

#ifndef RAWPAGE_UI_VIEWER_TOOLS_LINKTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_LINKTOOL_H

#include "ui/viewer/tools/Tool.h"

#include <QRect>

#include <optional>

namespace rawpage::ui {

class LinkTool final : public Tool {
public:
    ToolKind kind() const override { return ToolKind::Link; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

    bool paintsOverlay() const override { return true; }

    void press(ToolHost& host, QPoint pos) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;

    bool isDragging() const override { return page_ >= 0; }

    void paint(QPainter& painter, const ToolHost& host) const override;

    // The box as it stands, in the page's own points, snapped -- nothing when no drag is in progress.
    std::optional<core::PdfRect> box() const;
    int page() const noexcept { return page_; }

private:
    int page_ = -1;
    QPoint pressedAt_;
    QPoint movedTo_;
    core::PdfPoint origin_;
    core::PdfPoint corner_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_LINKTOOL_H
