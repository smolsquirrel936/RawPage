// ui/viewer/tools/DrawingTool -- the shapes: line (`Ctrl+Alt+L`), arrow (`annot.arrow`), rectangle (`Ctrl+Alt+R`),
// oval (`Ctrl+Alt+O`), polygon (`Ctrl+Alt+N`), polyline (`annot.polyline`) and cloud (`annot.cloud`, minted at T8).
// Stage 6 T8, decisions D6-1, D6-9 and D6-35 (`Stage6-Annotations-Plan.md` §3).
//
// TWO GESTURES, ONE COMMAND EACH (D6-1):
//   * **A drag** -- a line or an arrow from the press to the release, a rectangle or an oval in the box the drag
//     spans. `Shift` keeps a line to 45-degree steps and a box square (`core::constrainedTo45`,
//     `core::constrainedSquare`). A click that does not drag makes nothing.
//   * **Clicks** -- a polygon, a polyline or a cloud, a vertex at each click, a rubber band to the pointer
//     between them; `Shift` keeps each new edge to 45-degree steps. `Enter` (`annot.finishShape`, §8's
//     Annotation tools row) or a DOUBLE click finishes it; `Esc` abandons it -- nothing made -- and returns to
//     the Selection tool (guide A-46). Every vertex is on the page the first click was on.
// The annotation is made ONCE, when the gesture ends: its style the tool's defaults (`ToolHost::toolDefaults`,
// the settings family `commenting/default<Property>.<tool>` under the session's own changes, D6-13), its
// rectangle what the builder draws (`core::fittedShape`). An arrow is a Line whose end is an `OpenArrow`; a
// cloud is a Polygon with `/BE /S /C` and `/IT /PolygonCloud`, as Acrobat writes one.
//
// `SHIFT IS THIS TOOL'S`, not the temporary hand's (`usesShift`): §8 makes `Shift` the constraint of a drawing
// drag, and a hand that took the press would pan instead. Every other tool keeps the hand.
//
// While a gesture is in flight the tool paints a PREVIEW with Qt, and the document is untouched until it ends.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_DRAWINGTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_DRAWINGTOOL_H

#include "ui/viewer/tools/AnnotationTool.h"
#include "ui/viewer/tools/Tool.h"

#include <vector>

namespace rawpage::ui {

class DrawingTool final : public Tool {
public:
    // The seven kinds this tool makes.
    static bool handles(AnnotationToolKind kind) noexcept;
    // The three made by clicks, finished by `Enter` or a double click.
    static bool isPolygonal(AnnotationToolKind kind) noexcept;

    ToolKind kind() const override { return ToolKind::Annotate; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }
    bool paintsOverlay() const override { return true; }
    // Between a polygon's clicks the rubber band follows the pointer with no button down.
    bool tracksHover() const override { return !vertices_.empty(); }
    bool usesShift() const override { return true; }

    AnnotationToolKind which() const noexcept { return which_; }
    void setWhich(AnnotationToolKind which) noexcept { which_ = which; }

    void press(ToolHost& host, QPoint pos) override;
    void pressWith(ToolHost& host, QPoint pos, const PressContext& context) override;
    void move(ToolHost& host, QPoint pos) override;
    void moveWith(ToolHost& host, QPoint pos, Qt::KeyboardModifiers modifiers) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;
    bool isDragging() const override { return dragging_ || !vertices_.empty(); }
    void paint(QPainter& painter, const ToolHost& host) const override;

    // `Enter`: a polygon in progress made from its vertices. Returns whether one was in progress.
    bool finish(ToolHost& host);
    bool inProgress() const noexcept { return !vertices_.empty(); }
    // The vertices placed so far, in page space -- for a case.
    const std::vector<core::PdfPoint>& vertices() const noexcept { return vertices_; }

private:
    bool dragged(QPoint pos) const;
    // `pos` on the gesture's page, constrained by `Shift` against `from`.
    std::optional<core::PdfPoint> pointAt(ToolHost& host, QPoint pos, std::optional<core::PdfPoint> from, bool square) const;
    // Makes `a` (geometry set) through the overlay as one command, styled from the tool's defaults.
    void make(ToolHost& host, core::Annotation a);

    AnnotationToolKind which_ = AnnotationToolKind::Rectangle;
    bool dragging_ = false;
    bool shift_ = false;
    int page_ = -1;
    QPoint pressedAt_;
    QPoint at_;
    std::vector<core::PdfPoint> vertices_;   // a polygon in progress
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_DRAWINGTOOL_H
