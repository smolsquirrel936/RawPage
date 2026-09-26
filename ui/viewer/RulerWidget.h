// A ruler along one edge of the page surface -- Stage 4 T8, `Architecture-FileMap.md` §3.14.
//
// ===========================================================================================
// THE RULER MEASURES WHAT IS ON THE SCREEN. A GUIDE IS A PAGE FACT.
// ===========================================================================================
// Its reading at a pixel is the distance from the page's DRAWN top-left corner along the screen
// axis, in `units/rulerUnit`. That is deliberately not the same statement as a guide's position,
// which is PDF points in page space (D4-7): under a view rotation the page's x axis runs down the
// screen, and a ruler that insisted on reporting page x along the top would be measuring a
// direction the person is not pointing in. Both are derived from the same `PageSlot`, so they
// agree -- see `ui/viewer/GridGuideLayer.h`, which says the same thing from the other side.
//
// The mapping is pushed in rather than pulled: this widget holds no document, no layout and no
// controller. `ui/shell/DocumentWindow` recomputes a `Mapping` whenever the view changes and hands
// it over, which is what lets a case drive a ruler with three numbers and no window behind it.
//
// ===========================================================================================
// IT IS CHROME, SO IT IS PAINTED FROM THE PALETTE -- UNLIKE THE GRID.
// ===========================================================================================
// Convention 20 (*a colour is judged in BOTH themes*) and 21 (*the native style paints from the
// COLOUR SCHEME*): a ruler sits beside the ribbon and the status bar, so every colour here comes
// from `QPalette` and it follows the live theme with them. The grid and the guides do the
// opposite, and the reason is in `core/settings/SettingsKeys.def` at `units/gridColour`: those are
// painted over the PAGE, which is the document's own white in either theme. **The one exception is
// the cursor marker and its readout**, a fixed red -- see `markerColour()` for why.
//
// ===========================================================================================
// DRAGGING A GUIDE OUT OF IT IS THE RULER'S ONLY GESTURE.
// ===========================================================================================
// `Features.md` §1.3: *"drag a guide out of a ruler onto the page"*. Qt keeps the mouse grab on
// the widget the press landed on, so the drag is tracked here and reported in GLOBAL coordinates;
// the shell maps them into the page surface, which is the only object that knows where the pages
// are. A drag that ends back on the ruler drops nothing.

#ifndef RAWPAGE_UI_VIEWER_RULERWIDGET_H
#define RAWPAGE_UI_VIEWER_RULERWIDGET_H

#include "core/Units.h"

#include <QColor>
#include <QString>
#include <QWidget>

namespace rawpage::ui {

class RulerWidget : public QWidget {
    Q_OBJECT

public:
    // Where the page is, in this ruler's axis. All three numbers are in LOGICAL widget pixels and
    // PDF points, which is the pair a `QPaintEvent` and a document actually meet in.
    struct Mapping {
        bool   valid = false;
        double pixelsPerPoint = 1.0;   // logical pixels per PDF point == presentedScale / dpr
        double zeroPixel = 0.0;        // widget coordinate of the page's near edge
        double extentPoints = 0.0;     // the page's length along this axis
    };

    // 22 logical pixels: two rows of tick and one of a 7-point number, which is the smallest that
    // stays legible at `general/uiFontScale` 0.75 and the largest that does not read as a second
    // toolbar.
    static constexpr int kThickness = 22;

    // The cursor marker's width in logical pixels (2026-09-26, the user's request: one was too thin
    // to find against the ticks). Guide case A-88 measures it.
    static constexpr double kMarkerWidth = 2.0;

    explicit RulerWidget(Qt::Orientation orientation, QWidget* parent = nullptr);

    Qt::Orientation orientation() const noexcept { return orientation_; }

    void setMapping(const Mapping& mapping);
    const Mapping& mapping() const noexcept { return mapping_; }

    void setUnit(core::Unit unit);
    core::Unit unit() const noexcept { return unit_; }

    // The pointer's position along this ruler's axis, in widget pixels, or a negative number for
    // *not over the page surface*. Tracking the cursor is the second half of what a ruler is for:
    // without it a person reads where the page edge is but not where their hand is.
    void setCursorPixel(double pixel);
    double cursorPixel() const noexcept { return cursorPixel_; }

    // THE CURSOR READOUT -- the reading at the marker, in the ruler's unit, as the number painted
    // beside it; empty when there is no marker. `Features.md` §1.3's *"read out the cursor's page
    // coordinates live"*, and it is HERE rather than in the status bar on the user's decision of
    // 2026-09-26 (`Stage4-ViewingSurface-Testing-Guide.md` I-17): the number sits where the eye
    // already is when it reads the ticks.
    QString cursorReadout() const;

    // THE MARKER IS RED, and a FIXED red rather than a palette role -- the one colour in this widget
    // that does not follow the theme, on the user's request of 2026-09-26. The old marker was
    // `QPalette::Highlight`, the accent blue, which is also the colour of a selected control and did
    // not read as *here is your pointer*. Convention 20 still binds it: the readout is white on this
    // red, so it carries its own background, and guide case A-88 measures the line against the ruler
    // in BOTH themes.
    static QColor markerColour();

    // The reading at a widget coordinate, in POINTS. Public because it is the one piece of
    // arithmetic here worth asserting directly.
    double readingAt(double pixel) const;

    // The distance between minor ticks, in the display unit, for the current mapping -- exposed
    // for the same reason. Zero when nothing is mapped.
    double tickStep() const;

signals:
    // A guide being dragged out. `global` is in screen coordinates because the drop lands on
    // another widget entirely; the shell converts.
    void guideDragged(Qt::Orientation rulerOrientation, QPoint global);
    void guideDropped(Qt::Orientation rulerOrientation, QPoint global);

    // The unit menu and *clear all guides* are built by the shell, which owns the catalog: a
    // viewer widget that named `shell.` keys would be a second place the ruler's words live.
    void contextMenuRequested(QPoint global);

protected:
    QSize sizeHint() const override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    double axisOf(QPoint widgetPoint) const;

    Qt::Orientation orientation_;
    Mapping         mapping_;
    core::Unit      unit_ = core::Unit::Inches;
    double          cursorPixel_ = -1.0;
    bool            dragging_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_RULERWIDGET_H
