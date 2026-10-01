#include "ui/viewer/tools/SnapshotTool.h"

#include "ui/viewer/tools/MarqueeZoomTool.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {

int SnapshotTool::Resolution::dpi() const noexcept
{
    return static_cast<int>(std::lround(deviceScale * 72.0));
}

int SnapshotTool::Resolution::askedDpi() const noexcept
{
    return static_cast<int>(std::lround(askedScale * 72.0));
}

long long SnapshotTool::pixelsAt(double widthPoints, double heightPoints, double scale)
{
    const auto axis = [scale](double points) {
        return static_cast<long long>(std::ceil(std::max(0.0, points) * scale)) + 1;
    };
    return axis(widthPoints) * axis(heightPoints);
}

SnapshotTool::Resolution SnapshotTool::resolve(double widthPoints, double heightPoints, int settingDpi,
                                               double viewScale)
{
    Resolution resolution;

    // THE SENTINEL, THEN THE FLOOR -- see the header. Anything above the ceiling was clamped by the
    // table already; it is clamped again here because this is also reachable from a case.
    if (settingDpi <= 0) {
        resolution.askedScale = viewScale > 0.0 ? viewScale : 1.0;
    } else {
        resolution.askedScale = std::clamp(settingDpi, kMinDpi, kMaxDpi) / 72.0;
    }
    resolution.deviceScale = resolution.askedScale;

    if (widthPoints <= 0.0 || heightPoints <= 0.0) return resolution;
    if (pixelsAt(widthPoints, heightPoints, resolution.deviceScale) <= kMaxPixels) return resolution;

    // THE BOUND. The square root gets within a pixel row of the largest scale that fits; the step
    // down that follows is what makes it fit, because the rounding outward on both axes is not a
    // quantity a square root knows about. A few iterations at most -- each is half a percent.
    double scale = resolution.deviceScale
                   * std::sqrt(static_cast<double>(kMaxPixels)
                               / static_cast<double>(pixelsAt(widthPoints, heightPoints, resolution.deviceScale)));
    while (scale > 0.0 && pixelsAt(widthPoints, heightPoints, scale) > kMaxPixels) scale *= 0.995;

    resolution.deviceScale = scale;
    resolution.reduced = true;
    return resolution;
}

QRect SnapshotTool::band() const
{
    if (!dragging_) return {};
    return QRect(origin_, current_).normalized();
}

void SnapshotTool::press(ToolHost& host, QPoint pos)
{
    (void)host;
    dragging_ = true;
    origin_ = pos;
    current_ = pos;
}

void SnapshotTool::move(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;
    current_ = pos;
    host.requestRepaint();
}

void SnapshotTool::cancel(ToolHost& host)
{
    if (!dragging_) return;
    dragging_ = false;

    // NOTHING WAS ASKED FOR YET -- the band goes to the host only on release -- so `Esc` is *stop
    // drawing the band*, and the clipboard is untouched (A-91).
    host.requestRepaint();
}

void SnapshotTool::release(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;

    current_ = pos;
    const QRect rect = band();
    dragging_ = false;
    host.requestRepaint();

    // A CLICK IS NOT A SNAPSHOT, at the marquee's own threshold -- see the header.
    if (rect.width() < MarqueeZoomTool::kMinDragPixels || rect.height() < MarqueeZoomTool::kMinDragPixels)
        return;

    host.takeSnapshot(rect);
}

void SnapshotTool::paint(QPainter& painter, const ToolHost& host) const
{
    (void)host;
    if (!dragging_) return;

    const QRect rect = band();
    if (rect.isEmpty()) return;

    // THE PALETTE'S HIGHLIGHT, as the marquee's band (convention 20) -- and a DASHED edge with no wash,
    // so the region being copied is seen as it will be copied rather than tinted.
    const QColor highlight = QGuiApplication::palette().color(QPalette::Highlight);
    QPen pen(highlight);
    pen.setStyle(Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect);
}

} // namespace rawpage::ui
