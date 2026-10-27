// ui/viewer/tools/MeasureTool -- the measuring tools: distance (`Ctrl+Alt+D`), perimeter (`annot.measurePerimeter`),
// area (`Ctrl+Alt+A`) and the dimension callout (`annot.dimension`, minted at T12) -- and calibration
// (`annot.measureCalibrate`, minted at T12). Stage 6 T12, decision D6-14 (`Stage6-Annotations-Plan.md` §3).
//
// THE GESTURES, ONE COMMAND EACH (D6-1):
//   * **A distance**: a drag from one point to the other, or a click on each -- the first click anchors, the
//     second measures.
//   * **A dimension callout**: the two points as a distance, then the pointer moves the dimension line off them on
//     leader lines and a click sets it there (`/LL`); `Enter` sets it where it is.
//   * **A perimeter or an area**: a click at each point, `Enter` (`annot.finishShape`), a double click or -- since
//     S6 guide I-48 -- a right click to finish, the right click placing no point, as `Enter` places none; two points
//     at least for a perimeter, three for an area; `Esc` abandons and returns to the Selection tool.
// Every point is SNAPPED where it lands (`ToolHost::snapMeasured`: the page's path endpoints, midpoints and
// crossings first, then a point on a path, the annotations' sides, the guides and the grid -- `core/Snap.h`), unless
// `Shift` holds the new segment to 45-degree steps from the last point instead. While a gesture is in flight the
// tool paints the snap it would take -- a square at an endpoint, a triangle at a midpoint, a cross at a crossing, a
// ring on a path -- and the RUNNING READING beside the pointer, in the scale the `measuring/*` keys state.
//
// THE POINTER IS HIDDEN WHILE A SNAP IS SHOWN (S6 guide I-49, the user's choice). A snap catches within eight device
// pixels of the pointer, so its marker always lay under the cross, and Windows draws the pointer above anything a
// widget paints: the person could not tell a square from a triangle. While a marker is up the cursor is
// `Qt::BlankCursor` and the marker is what the person aims with; with nothing caught the cross comes back.
//
// What is MADE is `core::newMeasurement`'s: the subtype and `/IT` of its kind, `/Measure` at the scale, the reading
// in `/Contents` when `measuring/writeToComment` is on, styled from the tool's defaults and fitted to what it draws
// (`core::fittedShape`), through the overlay as ONE `AddAnnotationCommand`. A measurement this build will not state
// -- an area that crosses itself -- is refused by name, and nothing is made.
//
// CALIBRATION is the distance tool told to calibrate: the length it measures is not made into anything -- it is
// handed to the host (`ToolHost::calibrateFrom`), which asks what it is known to be and sets the scale.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_MEASURETOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_MEASURETOOL_H

#include "core/Measure.h"
#include "core/Snap.h"
#include "ui/viewer/tools/AnnotationTool.h"
#include "ui/viewer/tools/Tool.h"

#include <optional>
#include <vector>

namespace rawpage::ui {

class MeasureTool final : public Tool {
public:
    // The four kinds this tool makes, and the measurement each is.
    static bool handles(AnnotationToolKind kind) noexcept;
    static std::optional<core::MeasureKind> kindOf(AnnotationToolKind kind) noexcept;

    ToolKind kind() const override { return ToolKind::Annotate; }
    // The cross, or none while a snap marker is shown (I-49, see the header).
    Qt::CursorShape cursor() const override { return showsSnapMarker() ? Qt::BlankCursor : Qt::CrossCursor; }
    bool paintsOverlay() const override { return true; }
    // The snap a click would take is shown before the first one.
    bool tracksHover() const override { return true; }
    bool usesShift() const override { return true; }

    AnnotationToolKind which() const noexcept { return which_; }
    void setWhich(AnnotationToolKind which) noexcept;
    // The next distance CALIBRATES rather than making a measurement (see the header).
    void setCalibrating(bool calibrating) noexcept { calibrating_ = calibrating; }
    bool calibrating() const noexcept { return calibrating_; }

    void press(ToolHost& host, QPoint pos) override;
    void pressWith(ToolHost& host, QPoint pos, const PressContext& context) override;
    void move(ToolHost& host, QPoint pos) override;
    void moveWith(ToolHost& host, QPoint pos, Qt::KeyboardModifiers modifiers) override;
    void release(ToolHost& host, QPoint pos) override;
    // A perimeter's or an area's right click finishes it (I-48); taken only while one is in progress.
    bool secondaryPress(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;
    bool isDragging() const override { return dragging_ || !points_.empty(); }
    void paint(QPainter& painter, const ToolHost& host) const override;

    // `Enter`: the gesture in progress finished -- a perimeter or an area from its points, a dimension at its
    // current offset. Returns whether one was in progress.
    bool finish(ToolHost& host);
    bool inProgress() const noexcept { return !points_.empty(); }

    // For the cases: the points placed so far, where the pointer's point would land and what it snapped to, and the
    // running reading.
    const std::vector<core::PdfPoint>& points() const noexcept { return points_; }
    const std::optional<core::SnapResult>& hover() const noexcept { return hover_; }
    QString runningReading(const ToolHost& host) const;
    // A snap marker is painted where the next click would land -- the case for the hidden pointer (I-49).
    bool showsSnapMarker() const noexcept;

private:
    // Where `pos` lands on the gesture's page (the page under it, for the first point): `Shift`'s 45 degrees from the
    // last point, else snapped. Nullopt off every page.
    std::optional<core::SnapResult> landing(ToolHost& host, QPoint pos) const;
    // The two points of a distance are down: measured, or (a dimension) on to its leader.
    void secondPlaced(ToolHost& host);
    // The dimension line's offset from the two points when the pointer is at `pos` (`/LL`, signed).
    double leaderAt(const ToolHost& host, QPoint pos) const;
    void make(ToolHost& host, double leader);
    void reset(ToolHost& host);
    bool dragged(QPoint pos) const;

    AnnotationToolKind which_ = AnnotationToolKind::MeasureDistance;
    bool calibrating_ = false;
    int page_ = -1;
    std::vector<core::PdfPoint> points_;
    bool dragging_ = false;       // a distance's first press, held
    bool leaderPhase_ = false;    // a dimension's two points placed; the pointer moves its line
    bool shift_ = false;
    QPoint pressedAt_;
    QPoint at_;
    std::optional<core::SnapResult> hover_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_MEASURETOOL_H
