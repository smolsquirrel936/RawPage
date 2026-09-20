#include "ui/viewer/tools/LoupeTool.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

#include <algorithm>

namespace rawpage::ui {

QRect LoupeTool::lensRect(const ToolHost& host) const
{
    const QSize viewport = host.viewportSize();
    QRect lens(centre_.x() - kLensSize / 2, centre_.y() - kLensSize / 2, kLensSize, kLensSize);

    // CLAMPED INSIDE THE VIEWPORT. A lens hanging half off the edge is a lens you cannot read at
    // exactly the moment you are looking at a margin, which is where small print lives.
    if (lens.right() > viewport.width()) lens.moveRight(viewport.width());
    if (lens.bottom() > viewport.height()) lens.moveBottom(viewport.height());
    if (lens.left() < 0) lens.moveLeft(0);
    if (lens.top() < 0) lens.moveTop(0);
    return lens;
}

void LoupeTool::press(ToolHost& host, QPoint pos)
{
    // A PRESS PINS IT. Nothing scrolls and nothing zooms -- see the header, and guide case A-43.
    pinned_ = !pinned_;
    centre_ = pos;
    visible_ = true;
    host.requestRepaint();
}

void LoupeTool::move(ToolHost& host, QPoint pos)
{
    if (pinned_) return;
    centre_ = pos;
    visible_ = true;
    host.requestRepaint();
}

void LoupeTool::release(ToolHost& host, QPoint pos)
{
    (void)host;
    (void)pos;
}

void LoupeTool::cancel(ToolHost& host)
{
    if (!visible_ && !pinned_) return;
    visible_ = false;
    pinned_ = false;
    host.requestRepaint();
}

void LoupeTool::paint(QPainter& painter, const ToolHost& host) const
{
    if (!visible_) return;

    const QPixmap* frame = host.lastFrame();
    if (frame == nullptr || frame->isNull()) return;

    const QRect lens = lensRect(host);

    // THE SOURCE REGION, in the frame's own DEVICE pixels. The frame is captured at the device
    // pixel ratio, so a lens described in logical pixels has to be scaled into it -- and forgetting
    // that gives a magnifier that is correct at 100 % and shows the wrong part of the page on a
    // 150 % panel, which no offscreen case can see (manual M-12's half).
    const double ratio = host.devicePixelRatio();
    const double span = kLensSize / kMagnification;
    const QRectF source((centre_.x() - span / 2.0) * ratio, (centre_.y() - span / 2.0) * ratio,
                        span * ratio, span * ratio);

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setClipRect(lens);
    painter.drawPixmap(lens, *frame, source);
    painter.setClipping(false);

    // A BORDER, from the palette rather than a fixed colour (convention 20): without one the
    // magnified region and the page around it are the same picture at two scales, and the eye
    // cannot find the seam.
    painter.setPen(QGuiApplication::palette().color(QPalette::Highlight));
    painter.drawRect(lens.adjusted(0, 0, -1, -1));
    painter.restore();
}

} // namespace rawpage::ui
