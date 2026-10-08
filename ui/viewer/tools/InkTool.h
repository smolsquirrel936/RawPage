// ui/viewer/tools/InkTool -- the pencil (`Ctrl+Alt+P`) and the eraser (`annot.eraser`). Stage 6 T8, decisions D6-1
// and D6-15 (`Stage6-Annotations-Plan.md` §3).
//
// **The pencil**: each press-drag-release is ONE stroke and ONE `Ink` annotation -- one command, one undo step.
// The samples are kept as drawn, less any within a quarter of a DEVICE pixel of the one before
// (`core::dedupedSamples`), so a stroke at 400 % keeps the detail a person drew and one at 50 % no more points
// than the screen showed; the appearance draws the centripetal curve through them (D6-15). A click with no drag
// is a dot.
//
// **The eraser**: a drag over ink removes the SAMPLES within `kEraserPixels` device pixels of the eraser's path
// and splits each stroke where it did (`core::erasedStrokes`). One gesture is ONE command over the ink
// annotation it touched FIRST (the topmost under the press, else the first the path crossed): an
// `EditAnnotationCommand` when a stroke is left, a `DeleteAnnotationCommand` when none is -- so an erase is one
// undo step and the undo puts back exactly the samples there were (A-60). A locked ink is refused, as a move
// of it is (D6-27).
//
// Both draw a Qt preview while dragging -- the stroke so far, or the eraser's circle -- and touch nothing until
// the release.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_INKTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_INKTOOL_H

#include "ui/viewer/tools/AnnotationTool.h"
#include "ui/viewer/tools/Tool.h"

#include <vector>

namespace rawpage::ui {

class InkTool final : public Tool {
public:
    static bool handles(AnnotationToolKind kind) noexcept
    {
        return kind == AnnotationToolKind::Pencil || kind == AnnotationToolKind::Eraser;
    }

    // The eraser's reach, in DEVICE pixels -- the same at every zoom, as a hit is (D6-30).
    static constexpr double kEraserPixels = 6.0;
    // Samples closer than this to the last kept, in DEVICE pixels, are not kept (D6-15).
    static constexpr double kSampleSpacingPixels = 0.25;

    ToolKind kind() const override { return ToolKind::Annotate; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }
    bool paintsOverlay() const override { return true; }

    AnnotationToolKind which() const noexcept { return which_; }
    void setWhich(AnnotationToolKind which) noexcept { which_ = which; }

    void press(ToolHost& host, QPoint pos) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;
    bool isDragging() const override { return dragging_; }
    void paint(QPainter& painter, const ToolHost& host) const override;

private:
    void add(ToolHost& host, QPoint pos);

    AnnotationToolKind which_ = AnnotationToolKind::Pencil;
    bool dragging_ = false;
    int page_ = -1;
    std::vector<double> samples_;   // page space, x y pairs -- the stroke or the eraser's path
    std::vector<QPoint> drawn_;     // the same, where the pointer was -- for the preview
    double minimum_ = 0.0;          // kSampleSpacingPixels in page units, at the press
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_INKTOOL_H
