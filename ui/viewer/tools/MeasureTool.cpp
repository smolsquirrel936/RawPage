// ui/viewer/tools/MeasureTool -- implementation. Stage 6 T12. See the header for the gestures.

#include "ui/viewer/tools/MeasureTool.h"

#include "core/doc/DrawnShapes.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/ToolDefaults.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QStyleHints>

#include <cmath>

namespace rawpage::ui {
namespace {

int dragSlop()
{
    const QStyleHints* hints = QGuiApplication::styleHints();
    return hints != nullptr ? hints->startDragDistance() : 4;
}

bool samePoint(core::PdfPoint a, core::PdfPoint b)
{
    return std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6;
}

// The preview's ink: the accent the drawing tools use, so a gesture in progress reads the same for every tool.
const QColor kAccent(0x00, 0x78, 0xd4);

std::vector<double> flat(const std::vector<core::PdfPoint>& points)
{
    std::vector<double> out;
    out.reserve(points.size() * 2);
    for (const core::PdfPoint& p : points) {
        out.push_back(p.x);
        out.push_back(p.y);
    }
    return out;
}

} // namespace

bool MeasureTool::handles(AnnotationToolKind kind) noexcept
{
    return kindOf(kind).has_value();
}

std::optional<core::MeasureKind> MeasureTool::kindOf(AnnotationToolKind kind) noexcept
{
    switch (kind) {
    case AnnotationToolKind::MeasureDistance:
    case AnnotationToolKind::Dimension: return core::MeasureKind::Distance;
    case AnnotationToolKind::MeasurePerimeter: return core::MeasureKind::Perimeter;
    case AnnotationToolKind::MeasureArea: return core::MeasureKind::Area;
    default: return std::nullopt;
    }
}

void MeasureTool::setWhich(AnnotationToolKind which) noexcept
{
    which_ = which;
    points_.clear();
    dragging_ = false;
    leaderPhase_ = false;
    hover_.reset();
}

bool MeasureTool::dragged(QPoint pos) const
{
    return (pos - pressedAt_).manhattanLength() >= dragSlop();
}

std::optional<core::SnapResult> MeasureTool::landing(ToolHost& host, QPoint pos) const
{
    PagePoint at = points_.empty() ? host.pagePointAt(pos) : host.pagePointOn(page_, pos);
    if (!at.isValid()) return std::nullopt;
    // `Shift` HOLDS THE NEW SEGMENT TO 45 DEGREES from the last point -- a constraint the person chose, so no snap
    // pulls the point off it.
    if (shift_ && !points_.empty()) {
        return core::SnapResult{core::constrainedTo45(points_.back(), at.point), core::SnapSource::None,
                                core::SnapSource::None, std::nullopt};
    }
    return host.snapMeasured(at.page, at.point);
}

void MeasureTool::press(ToolHost& host, QPoint pos)
{
    pressWith(host, pos, PressContext{});
}

void MeasureTool::pressWith(ToolHost& host, QPoint pos, const PressContext& context)
{
    shift_ = context.modifiers.testFlag(Qt::ShiftModifier);
    at_ = pos;
    const std::optional<core::MeasureKind> kind = kindOf(which_);
    if (!kind) return;

    if (leaderPhase_) {
        // A DIMENSION'S THIRD CLICK sets its line where the pointer has moved it.
        make(host, leaderAt(host, pos));
        return;
    }

    if (*kind != core::MeasureKind::Distance) {
        // A DOUBLE CLICK finishes: its first press placed the point already.
        if (!points_.empty() && context.clicks >= 2) {
            (void)finish(host);
            return;
        }
        const bool first = points_.empty();
        const PagePoint under = first ? host.pagePointAt(pos) : PagePoint{page_, {}};
        if (first) {
            if (!under.isValid()) return;
            page_ = under.page;
        }
        const std::optional<core::SnapResult> landed = landing(host, pos);
        if (!landed) return;
        if (points_.empty() || !samePoint(landed->point, points_.back())) points_.push_back(landed->point);
        host.requestRepaint();
        return;
    }

    if (points_.empty()) {
        const PagePoint under = host.pagePointAt(pos);
        if (!under.isValid()) return;
        page_ = under.page;
        const std::optional<core::SnapResult> landed = landing(host, pos);
        if (!landed) return;
        points_.push_back(landed->point);
        dragging_ = true;
        pressedAt_ = pos;
        host.requestRepaint();
        return;
    }
    // ANCHORED BY A CLICK: this press is the second point.
    const std::optional<core::SnapResult> landed = landing(host, pos);
    if (!landed || samePoint(landed->point, points_.front())) return;
    points_.push_back(landed->point);
    secondPlaced(host);
}

void MeasureTool::move(ToolHost& host, QPoint pos)
{
    moveWith(host, pos, shift_ ? Qt::ShiftModifier : Qt::NoModifier);
}

void MeasureTool::moveWith(ToolHost& host, QPoint pos, Qt::KeyboardModifiers modifiers)
{
    shift_ = modifiers.testFlag(Qt::ShiftModifier);
    at_ = pos;
    hover_ = leaderPhase_ ? std::nullopt : landing(host, pos);
    host.requestRepaint();
}

void MeasureTool::release(ToolHost& host, QPoint pos)
{
    at_ = pos;
    if (!dragging_) return;
    dragging_ = false;
    // A CLICK ANCHORS the first point and the next press places the second; a DRAG places it here.
    if (!dragged(pos)) {
        host.requestRepaint();
        return;
    }
    const std::optional<core::SnapResult> landed = landing(host, pos);
    if (!landed || samePoint(landed->point, points_.front())) {
        reset(host);
        return;
    }
    points_.push_back(landed->point);
    secondPlaced(host);
}

void MeasureTool::secondPlaced(ToolHost& host)
{
    if (which_ == AnnotationToolKind::Dimension && !calibrating_) {
        leaderPhase_ = true;
        hover_.reset();
        host.requestRepaint();
        return;
    }
    make(host, 0.0);
}

double MeasureTool::leaderAt(const ToolHost& host, QPoint pos) const
{
    if (points_.size() < 2) return 0.0;
    const PagePoint at = host.pagePointOn(page_, pos);
    if (!at.isValid()) return 0.0;
    const core::PdfPoint p1 = points_[0], p2 = points_[1];
    const double length = std::hypot(p2.x - p1.x, p2.y - p1.y);
    if (!(length > 0.0)) return 0.0;
    // The clockwise normal: where ISO 32000-1 Table 175 puts a positive `/LL`.
    const double nx = (p2.y - p1.y) / length, ny = -(p2.x - p1.x) / length;
    return (at.point.x - p1.x) * nx + (at.point.y - p1.y) * ny;
}

bool MeasureTool::finish(ToolHost& host)
{
    if (points_.empty()) return false;
    const std::optional<core::MeasureKind> kind = kindOf(which_);
    if (leaderPhase_) {
        make(host, leaderAt(host, at_));
        return true;
    }
    // Too few points make nothing: a person who pressed `Enter` after one click meant to stop.
    const std::size_t needed = kind == core::MeasureKind::Area ? 3u : 2u;
    if (points_.size() < needed) {
        reset(host);
        return true;
    }
    make(host, 0.0);
    return true;
}

void MeasureTool::make(ToolHost& host, double leader)
{
    const std::optional<core::MeasureKind> kind = kindOf(which_);
    const std::vector<core::PdfPoint> points = points_;
    const int page = page_;
    reset(host);
    if (!kind) return;

    if (calibrating_ && *kind == core::MeasureKind::Distance && points.size() == 2) {
        // CALIBRATION: the length measured is handed on, and nothing is made.
        calibrating_ = false;
        (void)host.calibrateFrom(std::hypot(points[1].x - points[0].x, points[1].y - points[0].y));
        return;
    }

    AnnotationOverlay* annotations = host.annotationOverlay();
    if (annotations == nullptr) return;
    const ToolHost::MeasureSettings settings = host.measureSettings();
    core::Annotation base = styledWith(annotations->newAnnotation(core::measureSubtype(*kind), page), which_,
                                       host.toolDefaults(which_));
    core::Result<core::Annotation> made =
        core::newMeasurement(std::move(base), *kind, flat(points), settings.scale, leader, settings.writeToComment);
    if (!made) {
        annotations->reportRefusal(made.error());
        return;
    }
    const std::optional<core::Annotation> fitted = core::fittedShape(std::move(made).value());
    if (!fitted) return;
    const core::Result<core::MeasureReading> reading = core::measured(*fitted);
    if (annotations->addMade({*fitted})) {
        if (reading) host.measurementMade(reading.value().text);
        host.annotationToolUsed();
    }
}

void MeasureTool::reset(ToolHost& host)
{
    points_.clear();
    dragging_ = false;
    leaderPhase_ = false;
    hover_.reset();
    host.requestRepaint();
}

void MeasureTool::cancel(ToolHost& host)
{
    reset(host);
}

QString MeasureTool::runningReading(const ToolHost& host) const
{
    const std::optional<core::MeasureKind> kind = kindOf(which_);
    if (!kind || points_.empty()) return QString();
    std::vector<core::PdfPoint> points = points_;
    if (!leaderPhase_ && hover_) points.push_back(hover_->point);
    if (*kind == core::MeasureKind::Distance && points.size() > 2) points.resize(2);
    const core::Result<core::MeasureReading> reading = core::measureAt(*kind, flat(points), host.measureSettings().scale);
    return reading ? reading.value().text : QString();
}

void MeasureTool::paint(QPainter& painter, const ToolHost& host) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // THE SNAP THE NEXT CLICK WOULD TAKE, by its kind -- before the first click too.
    if (hover_) {
        const int page = points_.empty() ? -1 : page_;
        std::optional<QPointF> at;
        if (page >= 0) {
            at = host.widgetPointOf(page, hover_->point);
        } else {
            const PagePoint under = host.pagePointAt(at_);
            if (under.isValid()) at = host.widgetPointOf(under.page, hover_->point);
        }
        if (at) {
            painter.setPen(QPen(kAccent, 1.5));
            painter.setBrush(Qt::NoBrush);
            const double r = 5.0;
            if (hover_->pointKind) {
                switch (*hover_->pointKind) {
                case core::SnapPointKind::Endpoint:
                    painter.drawRect(QRectF(at->x() - r, at->y() - r, 2 * r, 2 * r));
                    break;
                case core::SnapPointKind::Midpoint: {
                    QPainterPath triangle;
                    triangle.moveTo(at->x(), at->y() - r);
                    triangle.lineTo(at->x() + r, at->y() + r);
                    triangle.lineTo(at->x() - r, at->y() + r);
                    triangle.closeSubpath();
                    painter.drawPath(triangle);
                    break;
                }
                case core::SnapPointKind::Intersection:
                    painter.drawLine(QPointF(at->x() - r, at->y() - r), QPointF(at->x() + r, at->y() + r));
                    painter.drawLine(QPointF(at->x() - r, at->y() + r), QPointF(at->x() + r, at->y() - r));
                    break;
                case core::SnapPointKind::OnPath:
                    painter.drawEllipse(*at, r, r);
                    break;
                }
            } else if (hover_->snapped()) {
                painter.drawLine(QPointF(at->x() - r, at->y()), QPointF(at->x() + r, at->y()));
                painter.drawLine(QPointF(at->x(), at->y() - r), QPointF(at->x(), at->y() + r));
            }
        }
    }

    // THE GESTURE: what it has placed, the rubber band to the pointer, and -- for a dimension -- the line where the
    // pointer has moved it.
    if (!points_.empty()) {
        painter.setPen(QPen(kAccent, 1.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        QPainterPath path;
        bool first = true;
        for (const core::PdfPoint& p : points_) {
            const std::optional<QPointF> w = host.widgetPointOf(page_, p);
            if (!w) continue;
            if (first) path.moveTo(*w);
            else path.lineTo(*w);
            first = false;
        }
        if (leaderPhase_ && points_.size() == 2) {
            const double ll = leaderAt(host, at_);
            const core::PdfPoint p1 = points_[0], p2 = points_[1];
            const double length = std::hypot(p2.x - p1.x, p2.y - p1.y);
            if (length > 0.0) {
                const double nx = (p2.y - p1.y) / length, ny = -(p2.x - p1.x) / length;
                const core::PdfPoint q1{p1.x + nx * ll, p1.y + ny * ll}, q2{p2.x + nx * ll, p2.y + ny * ll};
                const std::optional<QPointF> w1 = host.widgetPointOf(page_, q1), w2 = host.widgetPointOf(page_, q2);
                const std::optional<QPointF> a1 = host.widgetPointOf(page_, p1), a2 = host.widgetPointOf(page_, p2);
                if (w1 && w2 && a1 && a2) {
                    path.moveTo(*a1);
                    path.lineTo(*w1);
                    path.lineTo(*w2);
                    path.lineTo(*a2);
                }
            }
        } else if (!first && hover_) {
            if (const std::optional<QPointF> w = host.widgetPointOf(page_, hover_->point)) path.lineTo(*w);
            if (which_ == AnnotationToolKind::MeasureArea && points_.size() >= 2) {
                if (const std::optional<QPointF> start = host.widgetPointOf(page_, points_.front())) path.lineTo(*start);
            }
        }
        painter.drawPath(path);

        // THE RUNNING READING beside the pointer, on a white tab -- the page's paper, in either theme.
        const QString reading = runningReading(host);
        if (!reading.isEmpty()) {
            const QFontMetrics metrics(painter.font());
            const QRect text = metrics.boundingRect(reading).adjusted(-4, -2, 4, 2);
            const QRect box = text.translated(at_ + QPoint(14, -10) - text.bottomLeft());
            painter.setPen(QPen(kAccent, 1.0));
            painter.setBrush(QColor(255, 255, 255, 235));
            painter.drawRect(box);
            painter.setPen(Qt::black);
            painter.drawText(box, Qt::AlignCenter, reading);
        }
    }
    painter.restore();
}

} // namespace rawpage::ui
