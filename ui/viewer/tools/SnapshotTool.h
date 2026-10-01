// The Snapshot tool -- `Features.md` §8's `S`, Stage 5 T10, decision **D5-11**
// (`Stage5-TextSearch-Plan.md` §3), guide cases A-90, A-91 and A-114.
//
// ===========================================================================================
// THE GESTURE IS THE MARQUEE'S, AND WHAT IT DOES WITH THE BAND IS THE LOUPE'S.
// ===========================================================================================
// A band is dragged exactly as Marquee Zoom drags one -- and **a click is not a snapshot**, for the
// marquee's reason and at the marquee's own threshold (`MarqueeZoomTool::kMinDragPixels`, not a
// second copy of it). On release the band goes to `ToolHost::takeSnapshot`, and the HOST rasterizes
// it: the region re-rendered through the shipped scheduler with a request built by
// `TilePresenter::requestAt`, as the sharp loupe does (D4-20). The tool names no widget and no
// scheduler (`Tool.h`); it only draws the band and says where it was.
//
// **Re-rendered, never grabbed.** A grab of the widget is the reader's view of the document --
// the inversion, the grid, the guides, the selection and the search highlights painted over it
// (D4-11) -- and at 50 % it is a blurry 50 % crop. A raster of the region is the DOCUMENT's
// appearance, at whatever resolution was asked, and carries none of those because none of them is
// ever rasterized. That is the whole of A-90's control.
//
// ===========================================================================================
// THE RESOLUTION -- `pageDisplay/snapshotDpi`, ONE SENTINEL AND ONE BOUND.
// ===========================================================================================
// `resolve()` is the arithmetic, public and widget-free so a case can drive it without a window:
//
//   * **0, the default, is AS DISPLAYED** -- the view's device scale, which is what a person sees.
//     One sentinel rather than a boolean beside a number (D4-5: two facts about one choice).
//   * **72 to 600 is dots per inch**; the table clamps above 600, and a value from 1 to 71 is taken
//     as 72 HERE rather than by the table, because the table's floor has to be 0 for the sentinel.
//   * **The image is bounded at `kMaxPixels`, 50 megapixels** (200 MB of BGRA). A whole A3 page at
//     600 dpi is 7016 x 9921, about 70 megapixels and 280 MB (D5-11 said "280 megapixels, over a
//     gigabyte" until T10 measured it: the 280 was the megabytes); the region is then taken at the
//     largest resolution that fits, and the result SAYS it was reduced so the status bar can.
//
// What the bound counts is the image the rasterizer will actually produce: the region rounded
// OUTWARD to whole pixels on both edges, as `PageView` rounds it, so a region that is 50 megapixels
// "exactly" cannot come back one row over.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_SNAPSHOTTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_SNAPSHOTTOOL_H

#include "ui/viewer/tools/Tool.h"

#include <QRect>

namespace rawpage::ui {

class SnapshotTool final : public Tool {
public:
    // D5-11's bound, in pixels: 50 megapixels, 200 MB at four bytes a pixel.
    static constexpr long long kMaxPixels = 50LL * 1000 * 1000;

    // The resolution range `pageDisplay/snapshotDpi` names when it is not 0.
    static constexpr int kMinDpi = 72;
    static constexpr int kMaxDpi = 600;

    // What a snapshot of a region `widthPoints` x `heightPoints` (PDF points) is taken at.
    struct Resolution {
        double deviceScale = 0.0;    // device pixels per PDF point the raster is asked for at
        double askedScale = 0.0;     // ... and what the setting asked for, before the bound
        bool   reduced = false;      // the bound lowered it

        // The resolution in dots per inch, rounded to the nearest -- what the status bar says.
        int dpi() const noexcept;
        int askedDpi() const noexcept;
    };

    // `settingDpi` is `pageDisplay/snapshotDpi` as read; `viewScale` is the view's device pixels per
    // point (`PageLayout::presentedScale()`), which 0 means. A non-positive region resolves to the
    // asked scale, unreduced -- there is nothing to bound.
    static Resolution resolve(double widthPoints, double heightPoints, int settingDpi, double viewScale);

    // The size in pixels a region of `widthPoints` x `heightPoints` comes back at, at `scale`: each
    // axis rounded outward, plus the one pixel a region whose edges fall between pixels gains. An
    // upper bound on what `PageView::snapshotRegionFor` asks for, which is what the bound must hold.
    static long long pixelsAt(double widthPoints, double heightPoints, double scale);

    ToolKind kind() const override { return ToolKind::Snapshot; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

    bool paintsOverlay() const override { return true; }

    void press(ToolHost& host, QPoint pos) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;

    bool isDragging() const override { return dragging_; }

    void paint(QPainter& painter, const ToolHost& host) const override;

    // The band as it stands, in widget coordinates -- empty when no drag is in progress.
    QRect band() const;

private:
    bool   dragging_ = false;
    QPoint origin_;
    QPoint current_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_SNAPSHOTTOOL_H
