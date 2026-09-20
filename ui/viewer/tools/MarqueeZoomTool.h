// Marquee Zoom -- `Features.md` §1.2 *"drag a rectangle to zoom to it"*, §8's `Z`, Stage 4 T7.
//
// Moved out of `ui/viewer/PageView` at T7, arithmetic unchanged, because a widget that held one
// tool's gesture inline could not hold four. Two things it carries are worth reading before
// touching the arithmetic:
//
//   * **A CLICK IS NOT A ZOOM.** A band a few pixels across is somebody clicking, and zooming to
//     it lands at 6400 % with no obvious way back. Below `kMinDragPixels` on either axis the drag
//     is abandoned, which is why a stray click with `Z` armed does nothing at all.
//   * **THE TIGHTER AXIS WINS.** The zoom is the smaller of the two ratios, so the whole dragged
//     band stays visible — the same rule Fit Page follows, and for the same reason. Taking the
//     larger would crop the band the user just drew, which is the one thing they were precise
//     about.
//
// **It changes nothing until release**, which is what makes `Esc` cheap here: there is no view to
// restore, only a rubber band to stop drawing. The hand tool is the opposite case and its header
// says so. A marquee IS deliberate navigation, so it records a view-history entry — `Alt+Left`
// after a marquee zoom goes back to where you zoomed from.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_MARQUEEZOOMTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_MARQUEEZOOMTOOL_H

#include "ui/viewer/tools/Tool.h"

#include <QRect>

namespace rawpage::ui {

class MarqueeZoomTool final : public Tool {
public:
    // Below this, on either axis, the drag is a click. Public because the case that asserts *a
    // click does not zoom* must use the same number rather than a second copy of it.
    static constexpr int kMinDragPixels = 8;

    ToolKind kind() const override { return ToolKind::MarqueeZoom; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

    bool paintsOverlay() const override { return true; }

    void press(ToolHost& host, QPoint pos) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;

    bool isDragging() const override { return dragging_; }

    void paint(QPainter& painter, const ToolHost& host) const override;

    // The band as it stands, in widget coordinates -- empty when no drag is in progress. Exposed
    // so a case reads the rectangle the tool will act on rather than inferring it from the two
    // points it sent.
    QRect band() const;

private:
    bool   dragging_ = false;
    QPoint origin_;
    QPoint current_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_MARQUEEZOOMTOOL_H
