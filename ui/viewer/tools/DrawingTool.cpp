// ui/viewer/tools/DrawingTool -- implementation. Stage 6 T8. See the header for the two gestures.

#include "ui/viewer/tools/DrawingTool.h"

#include "core/doc/DrawnShapes.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/ToolDefaults.h"

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

core::AnnotationSubtype subtypeOf(AnnotationToolKind kind)
{
    switch (kind) {
    case AnnotationToolKind::Line:
    case AnnotationToolKind::Arrow: return core::AnnotationSubtype::Line;
    case AnnotationToolKind::Rectangle: return core::AnnotationSubtype::Square;
    case AnnotationToolKind::Oval: return core::AnnotationSubtype::Circle;
    case AnnotationToolKind::Polyline: return core::AnnotationSubtype::PolyLine;
    default: break;
    }
    return core::AnnotationSubtype::Polygon;
}

bool samePoint(core::PdfPoint a, core::PdfPoint b)
{
    return std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6;
}

} // namespace

bool DrawingTool::handles(AnnotationToolKind kind) noexcept
{
    switch (kind) {
    case AnnotationToolKind::Line:
    case AnnotationToolKind::Arrow:
    case AnnotationToolKind::Rectangle:
    case AnnotationToolKind::Oval:
    case AnnotationToolKind::Polygon:
    case AnnotationToolKind::Polyline:
    case AnnotationToolKind::Cloud:
        return true;
    default:
        return false;
    }
}

bool DrawingTool::isPolygonal(AnnotationToolKind kind) noexcept
{
    return kind == AnnotationToolKind::Polygon || kind == AnnotationToolKind::Polyline || kind == AnnotationToolKind::Cloud;
}

bool DrawingTool::dragged(QPoint pos) const
{
    return (pos - pressedAt_).manhattanLength() >= dragSlop();
}

std::optional<core::PdfPoint> DrawingTool::pointAt(ToolHost& host, QPoint pos, std::optional<core::PdfPoint> from,
                                                   bool square) const
{
    const PagePoint p = host.pagePointOn(page_, pos);
    if (!p.isValid()) return std::nullopt;
    if (!shift_ || !from) return p.point;
    return square ? core::constrainedSquare(*from, p.point) : core::constrainedTo45(*from, p.point);
}

void DrawingTool::press(ToolHost& host, QPoint pos)
{
    pressWith(host, pos, PressContext{});
}

void DrawingTool::pressWith(ToolHost& host, QPoint pos, const PressContext& context)
{
    shift_ = context.modifiers.testFlag(Qt::ShiftModifier);
    at_ = pos;
    if (isPolygonal(which_)) {
        // A DOUBLE CLICK finishes: its first press placed the vertex already.
        if (!vertices_.empty() && context.clicks >= 2) {
            (void)finish(host);
            return;
        }
        if (vertices_.empty()) {
            const PagePoint first = host.pagePointAt(pos);
            if (!first.isValid()) return;
            page_ = first.page;
            vertices_.push_back(first.point);
        } else {
            const std::optional<core::PdfPoint> p = pointAt(host, pos, vertices_.back(), false);
            if (p && !samePoint(*p, vertices_.back())) vertices_.push_back(*p);
        }
        host.requestRepaint();
        return;
    }
    const PagePoint start = host.pagePointAt(pos);
    if (!start.isValid()) return;
    page_ = start.page;
    pressedAt_ = pos;
    dragging_ = true;
    host.requestRepaint();
}

void DrawingTool::move(ToolHost& host, QPoint pos)
{
    moveWith(host, pos, shift_ ? Qt::ShiftModifier : Qt::NoModifier);
}

void DrawingTool::moveWith(ToolHost& host, QPoint pos, Qt::KeyboardModifiers modifiers)
{
    shift_ = modifiers.testFlag(Qt::ShiftModifier);
    at_ = pos;
    if (dragging_ || !vertices_.empty()) host.requestRepaint();
}

void DrawingTool::release(ToolHost& host, QPoint pos)
{
    at_ = pos;
    if (!dragging_) return;   // a polygon's clicks are its presses
    dragging_ = false;
    host.requestRepaint();
    if (!dragged(pos)) return;   // a click makes nothing: a shape is dragged out

    const PagePoint startPoint = host.pagePointOn(page_, pressedAt_);
    if (!startPoint.isValid()) return;
    const core::PdfPoint start = startPoint.point;
    const bool box = which_ == AnnotationToolKind::Rectangle || which_ == AnnotationToolKind::Oval;
    const std::optional<core::PdfPoint> end = pointAt(host, pos, start, box);
    if (!end) return;

    AnnotationOverlay* annotations = host.annotationOverlay();
    if (annotations == nullptr) return;
    core::Annotation a = annotations->newAnnotation(subtypeOf(which_), page_);
    if (box) {
        const std::optional<core::PdfRect> rect = core::normalizedRect(start.x, start.y, end->x, end->y);
        if (!rect || !(rect->size.width > 0.0) || !(rect->size.height > 0.0)) return;
        a.rect = *rect;
    } else {
        if (samePoint(start, *end)) return;
        a.line = {start.x, start.y, end->x, end->y};
        if (which_ == AnnotationToolKind::Arrow) a.lineEndings = {QStringLiteral("None"), QStringLiteral("OpenArrow")};
    }
    make(host, std::move(a));
}

bool DrawingTool::finish(ToolHost& host)
{
    if (vertices_.empty()) return false;
    std::vector<core::PdfPoint> points = std::move(vertices_);
    vertices_.clear();
    host.requestRepaint();
    const bool closed = which_ != AnnotationToolKind::Polyline;
    // Too few corners make nothing: a person who pressed `Enter` after one click meant to stop.
    if (points.size() < (closed ? 3u : 2u)) return true;
    AnnotationOverlay* annotations = host.annotationOverlay();
    if (annotations == nullptr) return true;
    core::Annotation a = annotations->newAnnotation(subtypeOf(which_), page_);
    for (const core::PdfPoint& p : points) {
        a.vertices.push_back(p.x);
        a.vertices.push_back(p.y);
    }
    if (which_ == AnnotationToolKind::Cloud) {
        // Acrobat's cloud: a Polygon whose border is cloudy, of intensity 1, and that says so in `/IT`.
        a.cloudIntensity = 1.0;
        a.intent = QStringLiteral("PolygonCloud");
    }
    make(host, std::move(a));
    return true;
}

void DrawingTool::make(ToolHost& host, core::Annotation a)
{
    AnnotationOverlay* annotations = host.annotationOverlay();
    if (annotations == nullptr) return;
    a = styledWith(std::move(a), which_, host.toolDefaults(which_));
    const std::optional<core::Annotation> fitted = core::fittedShape(std::move(a));
    if (!fitted) return;
    if (annotations->addMade({*fitted})) host.annotationToolUsed();
}

void DrawingTool::cancel(ToolHost& host)
{
    dragging_ = false;
    vertices_.clear();
    host.requestRepaint();
}

void DrawingTool::paint(QPainter& painter, const ToolHost& host) const
{
    // A PREVIEW of what the gesture will make, in the accent colour, dashed -- never the annotation itself.
    if (!dragging_ && vertices_.empty()) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0x00, 0x78, 0xd4), 1.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    if (dragging_) {
        if (dragged(at_)) {
            if (which_ == AnnotationToolKind::Rectangle) painter.drawRect(QRect(pressedAt_, at_).normalized());
            else if (which_ == AnnotationToolKind::Oval) painter.drawEllipse(QRect(pressedAt_, at_).normalized());
            else painter.drawLine(pressedAt_, at_);
        }
    } else {
        QPainterPath path;
        bool first = true;
        for (const core::PdfPoint& p : vertices_) {
            const std::optional<QPointF> w = host.widgetPointOf(page_, p);
            if (!w) continue;
            if (first) path.moveTo(*w);
            else path.lineTo(*w);
            first = false;
        }
        if (!first) path.lineTo(QPointF(at_));   // the rubber band to the pointer
        painter.drawPath(path);
    }
    painter.restore();
}

} // namespace rawpage::ui
