#include "ui/viewer/RulerWidget.h"

#include <QContextMenuEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QRectF>

#include <array>
#include <cmath>

namespace rawpage::ui {
namespace {

// The smallest gap, in logical pixels, a minor tick may be drawn at. Below this the ticks merge
// into a grey band and the ruler stops being readable -- so the step goes up a rung instead.
constexpr double kMinimumTickPixels = 6.0;

// A label every `kLabelEvery` minor ticks. Five, so the labelled interval is a round multiple of
// the step in every unit rather than a different one per unit.
constexpr int kLabelEvery = 5;

// A 1-2-5 ladder in the DISPLAY unit, which is what makes the ticks round numbers in the unit the
// person chose rather than round numbers in points that read as 0.35, 0.71, 1.06.
//
// **Not the traditional inch halving (1, 1/2, 1/4, 1/8).** One ladder for five units is one rule
// to read; a per-unit table would be five, and the one a reader would check is the one that was
// not updated. The cost is that an inch ruler subdivides in tenths, which is what a decimal
// readout wants anyway -- `formatLength` prints 0.25, not a fraction.
double stepInUnit(double pixelsPerUnit)
{
    if (!(pixelsPerUnit > 0.0)) return 0.0;

    static constexpr std::array<double, 3> kLadder{1.0, 2.0, 5.0};
    double decade = 1e-4;
    for (int i = 0; i < 12; ++i) {
        for (const double rung : kLadder) {
            const double step = rung * decade;
            if (step * pixelsPerUnit >= kMinimumTickPixels) return step;
        }
        decade *= 10.0;
    }
    return 0.0;
}

} // namespace

RulerWidget::RulerWidget(Qt::Orientation orientation, QWidget* parent)
    : QWidget(parent), orientation_(orientation)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    // The drag-out gesture needs moves with a button down only, which Qt delivers without mouse
    // tracking. The CURSOR MARKER is fed from the page surface, not from this widget's own hover:
    // a marker that only moved while the pointer was over the ruler would show where the pointer
    // is not.
    setMouseTracking(false);

    if (orientation_ == Qt::Horizontal) {
        setFixedHeight(kThickness);
        setCursor(Qt::SplitVCursor);   // dragging DOWN off this ruler produces a vertical guide
    } else {
        setFixedWidth(kThickness);
        setCursor(Qt::SplitHCursor);
    }
}

QSize RulerWidget::sizeHint() const
{
    return orientation_ == Qt::Horizontal ? QSize(kThickness * 8, kThickness)
                                          : QSize(kThickness, kThickness * 8);
}

void RulerWidget::setMapping(const Mapping& mapping)
{
    mapping_ = mapping;
    update();
}

void RulerWidget::setUnit(core::Unit unit)
{
    if (unit_ == unit) return;
    unit_ = unit;
    update();
}

void RulerWidget::setCursorPixel(double pixel)
{
    if (qFuzzyCompare(cursorPixel_ + 1.0, pixel + 1.0)) return;
    cursorPixel_ = pixel;
    update();
}

double RulerWidget::readingAt(double pixel) const
{
    if (!mapping_.valid || !(mapping_.pixelsPerPoint > 0.0)) return 0.0;
    return (pixel - mapping_.zeroPixel) / mapping_.pixelsPerPoint;
}

double RulerWidget::tickStep() const
{
    if (!mapping_.valid || !(mapping_.pixelsPerPoint > 0.0)) return 0.0;

    // Pixels per DISPLAY UNIT, which is what the ladder is chosen against -- one inch is 72 points
    // and one millimetre is 2.835, so a ladder chosen in points would be a different ruler per
    // unit. `Length::from(1.0, unit)` is how many points one of the unit is.
    const double pointsPerUnit = core::Length::from(1.0, unit_).points();
    return stepInUnit(mapping_.pixelsPerPoint * pointsPerUnit);
}

double RulerWidget::axisOf(QPoint widgetPoint) const
{
    return orientation_ == Qt::Horizontal ? widgetPoint.x() : widgetPoint.y();
}

void RulerWidget::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    const QPalette& colours = palette();

    painter.fillRect(event->rect(), colours.color(QPalette::Window));

    const bool horizontal = orientation_ == Qt::Horizontal;
    const double length = horizontal ? width() : height();
    const double across = horizontal ? height() : width();

    // THE PAGE'S EXTENT, SHADED. Without it the ruler says where the numbers are but not where the
    // sheet is, and at any zoom that shows the desk around the page the zero is off screen.
    if (mapping_.valid && mapping_.extentPoints > 0.0) {
        const double from = mapping_.zeroPixel;
        const double to = mapping_.zeroPixel + mapping_.extentPoints * mapping_.pixelsPerPoint;
        const QRectF page = horizontal ? QRectF(from, 0.0, to - from, across)
                                       : QRectF(0.0, from, across, to - from);
        painter.fillRect(page, colours.color(QPalette::Base));
    }

    painter.setPen(colours.color(QPalette::Mid));
    if (horizontal) painter.drawLine(QLineF(0.0, across - 0.5, length, across - 0.5));
    else            painter.drawLine(QLineF(across - 0.5, 0.0, across - 0.5, length));

    const double step = tickStep();
    if (mapping_.valid && step > 0.0) {
        QFont small = font();
        small.setPointSizeF(std::max(6.0, small.pointSizeF() - 2.0));
        painter.setFont(small);

        const double unitInPoints = core::Length::from(1.0, unit_).points();
        const double pixelsPerStep = step * unitInPoints * mapping_.pixelsPerPoint;

        // Walk in TICK INDICES rather than adding the step to a running coordinate: 5000 additions
        // of 0.1 accumulate a visible error by the right-hand edge, and the ruler would disagree
        // with `readingAt` at exactly the end a person measures to.
        const double firstReading = readingAt(0.0) / unitInPoints;
        const long long firstIndex = static_cast<long long>(std::floor(firstReading / step));
        const long long lastIndex =
            static_cast<long long>(std::ceil(readingAt(length) / unitInPoints / step));

        // A ruler asked to draw more ticks than it has pixels is a mapping that has gone wrong
        // (a zero scale, a NaN); the bound turns that into a blank ruler rather than a hang.
        const long long maximum = static_cast<long long>(length / kMinimumTickPixels) + 4;

        long long drawn = 0;
        for (long long i = firstIndex; i <= lastIndex && drawn <= maximum; ++i, ++drawn) {
            const double value = static_cast<double>(i) * step;             // in the display unit
            const double pixel = mapping_.zeroPixel + value * unitInPoints * mapping_.pixelsPerPoint;
            if (pixel < -pixelsPerStep || pixel > length + pixelsPerStep) continue;

            const bool labelled = (((i % kLabelEvery) + kLabelEvery) % kLabelEvery) == 0;
            const double tick = labelled ? across * 0.6 : across * 0.3;

            painter.setPen(colours.color(labelled ? QPalette::WindowText : QPalette::Mid));
            if (horizontal) painter.drawLine(QLineF(pixel, across - tick, pixel, across - 1.0));
            else            painter.drawLine(QLineF(across - tick, pixel, across - 1.0, pixel));

            if (!labelled) continue;

            // The NUMBER only -- the unit is named once, in the corner box, rather than on every
            // fifth tick where it would not fit.
            const QString text = QString::number(value, 'g', 4);
            painter.setPen(colours.color(QPalette::WindowText));
            if (horizontal) {
                painter.drawText(QRectF(pixel + 2.0, 0.0, pixelsPerStep * kLabelEvery, across - tick),
                                 Qt::AlignLeft | Qt::AlignVCenter, text);
            } else {
                // Rotated, so a vertical ruler reads along its own axis rather than stacking
                // digits one above another.
                //
                // **THE ORIGIN IS THE TICK SIDE OF THE LABEL STRIP, not x = 0 -- I-13.** A quarter
                // turn in Qt's y-down space maps local (x, y) to (-y, x), so the box's height runs
                // towards NEGATIVE x: anchored at 0 it lay wholly off the widget's left edge and
                // every number was clipped away. Anchored at `across - tick` it runs back across
                // [0, across - tick] -- the strip the horizontal ruler's numbers occupy.
                painter.save();
                painter.translate(across - tick, pixel + 2.0);
                painter.rotate(90.0);
                painter.drawText(QRectF(0.0, 0.0, pixelsPerStep * kLabelEvery, across - tick),
                                 Qt::AlignLeft | Qt::AlignVCenter, text);
                painter.restore();
            }
        }
    }

    if (cursorPixel_ >= 0.0) {
        painter.setPen(colours.color(QPalette::Highlight));
        if (horizontal) painter.drawLine(QLineF(cursorPixel_, 0.0, cursorPixel_, across));
        else            painter.drawLine(QLineF(0.0, cursorPixel_, across, cursorPixel_));
    }
}

void RulerWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !mapping_.valid) {
        QWidget::mousePressEvent(event);
        return;
    }
    dragging_ = true;
    emit guideDragged(orientation_, event->globalPosition().toPoint());
    event->accept();
}

void RulerWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!dragging_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    emit guideDragged(orientation_, event->globalPosition().toPoint());
    event->accept();
}

void RulerWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!dragging_ || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    dragging_ = false;
    emit guideDropped(orientation_, event->globalPosition().toPoint());
    event->accept();
}

void RulerWidget::contextMenuEvent(QContextMenuEvent* event)
{
    emit contextMenuRequested(event->globalPos());
    event->accept();
}

} // namespace rawpage::ui
