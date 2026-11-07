// ui/viewer/tools/CropTool -- `page.crop`'s tool: drag a box on a page, and the page is cropped to it. Stage 7 T9
// (`Stage7-Assembly-Plan.md` D7-15, D7-32; `Features.md` §5.1, *crop pages by dragging a crop box*).
//
// THE BOX IS IN PAGE SPACE FROM THE PRESS ON. The press names the page (`ToolHost::pagePointAt`) and every later
// position is read on THAT page (`pagePointOn`), so a drag that leaves the page keeps cropping it to its edge rather
// than jumping to the next; each corner is SNAPPED as a measurement's is (`ToolHost::snapMeasured`, `core/Snap.h`) --
// the page's path ends and corners, the annotations' sides, the guides, the grid -- under the one device-pixel
// threshold, so a crop dragged to a drawn frame lands on it. The band painted is the two snapped corners drawn back
// through the view (`widgetPointOf`), so what is seen is what will be written.
//
// ON RELEASE IT HANDS THE BOX OVER (`ToolHost::cropToBox`) and keeps nothing: the window decides what a person is
// asked -- the Crop Pages dialog, the box filled in -- and with nobody to ask runs ONE `CropPagesCommand` on the page.
// The tool constructs no command itself, as the Snapshot tool takes no raster (D4-1's rule for a tool: a host). A
// drag under `MarqueeZoomTool::kMinDragPixels` on either side is a click and crops nothing; `Esc` during the drag
// abandons it and nothing is asked.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_CROPTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_CROPTOOL_H

#include "ui/viewer/tools/Tool.h"

#include <QRect>

#include <optional>

namespace rawpage::ui {

class CropTool final : public Tool {
public:
    ToolKind kind() const override { return ToolKind::Crop; }
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

#endif // RAWPAGE_UI_VIEWER_TOOLS_CROPTOOL_H
