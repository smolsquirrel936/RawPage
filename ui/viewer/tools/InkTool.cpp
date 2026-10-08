// ui/viewer/tools/InkTool -- implementation. Stage 6 T8. See the header for the pencil and the eraser.

#include "ui/viewer/tools/InkTool.h"

#include "core/doc/DrawnShapes.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/ToolDefaults.h"

#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {
namespace {

// Page units per DEVICE pixel at the host's zoom: what a fixed number of device pixels is on the page.
double pagePerDevicePixel(const ToolHost& host)
{
    const double ppp = host.pixelsPerPoint() > 0.0 ? host.pixelsPerPoint() : 1.0;
    const double dpr = host.devicePixelRatio() > 0.0 ? host.devicePixelRatio() : 1.0;
    return 1.0 / (ppp * dpr);
}

QColor toQColor(const std::vector<double>& colour)
{
    const auto c = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
    if (colour.size() == 1) return QColor(c(colour[0]), c(colour[0]), c(colour[0]));
    if (colour.size() == 3) return QColor(c(colour[0]), c(colour[1]), c(colour[2]));
    return QColor(Qt::black);
}

} // namespace

void InkTool::press(ToolHost& host, QPoint pos)
{
    const PagePoint p = host.pagePointAt(pos);
    if (!p.isValid()) return;
    page_ = p.page;
    dragging_ = true;
    samples_.clear();
    drawn_.clear();
    minimum_ = kSampleSpacingPixels * pagePerDevicePixel(host);
    add(host, pos);
}

void InkTool::add(ToolHost& host, QPoint pos)
{
    const PagePoint p = host.pagePointOn(page_, pos);
    if (!p.isValid()) return;
    drawn_.push_back(pos);
    if (samples_.size() >= 2
        && std::hypot(p.point.x - samples_[samples_.size() - 2], p.point.y - samples_.back()) <= minimum_)
        return;   // within a quarter of a device pixel of the last sample kept (D6-15)
    samples_.push_back(p.point.x);
    samples_.push_back(p.point.y);
    host.requestRepaint();
}

void InkTool::move(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;
    add(host, pos);
}

void InkTool::release(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;
    add(host, pos);
    dragging_ = false;
    host.requestRepaint();
    AnnotationOverlay* annotations = host.annotationOverlay();
    if (annotations == nullptr || samples_.size() < 2) return;

    if (which_ == AnnotationToolKind::Eraser) {
        // The eraser stays armed: a person erases in several strokes.
        (void)annotations->eraseInk(page_, samples_, kEraserPixels * pagePerDevicePixel(host));
        samples_.clear();
        drawn_.clear();
        return;
    }
    core::Annotation a = annotations->newAnnotation(core::AnnotationSubtype::Ink, page_);
    a.inkList = {core::dedupedSamples(samples_, minimum_)};
    samples_.clear();
    drawn_.clear();
    a = styledWith(std::move(a), which_, host.toolDefaults(which_));
    const std::optional<core::Annotation> fitted = core::fittedShape(std::move(a));
    if (!fitted) return;
    if (annotations->addMade({*fitted})) host.annotationToolUsed();
}

void InkTool::cancel(ToolHost& host)
{
    dragging_ = false;
    samples_.clear();
    drawn_.clear();
    host.requestRepaint();
}

void InkTool::paint(QPainter& painter, const ToolHost& host) const
{
    if (!dragging_ || drawn_.empty()) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (which_ == AnnotationToolKind::Eraser) {
        // The eraser's reach round the pointer, and the path it has swept.
        const double r = kEraserPixels / (host.devicePixelRatio() > 0.0 ? host.devicePixelRatio() : 1.0);
        painter.setPen(QPen(QColor(0x00, 0x78, 0xd4), 1.0, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(drawn_.back()), r, r);
    } else {
        // The stroke so far, in the pencil's colour and width -- straight segments; the curve is the appearance's.
        const core::ToolDefaults values = host.toolDefaults(which_);
        const double ppp = host.pixelsPerPoint() > 0.0 ? host.pixelsPerPoint() : 1.0;
        QPen pen(toQColor(values.colour), std::max(1.0, values.thickness * ppp), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        if (drawn_.size() == 1) painter.drawPoint(drawn_.front());
        else painter.drawPolyline(drawn_.data(), static_cast<int>(drawn_.size()));
    }
    painter.restore();
}

} // namespace rawpage::ui
