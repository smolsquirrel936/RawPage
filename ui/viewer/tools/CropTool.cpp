// ui/viewer/tools/CropTool -- implementation. Stage 7 T9. See the header.

#include "ui/viewer/tools/CropTool.h"

#include "ui/viewer/tools/MarqueeZoomTool.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPalette>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {

std::optional<core::PdfRect> CropTool::box() const
{
    if (page_ < 0) return std::nullopt;
    const double left = std::min(origin_.x, corner_.x);
    const double bottom = std::min(origin_.y, corner_.y);
    return core::PdfRect{core::PdfPoint{left, bottom},
                         core::PdfSize{std::max(origin_.x, corner_.x) - left, std::max(origin_.y, corner_.y) - bottom}};
}

void CropTool::press(ToolHost& host, QPoint pos)
{
    const PagePoint at = host.pagePointAt(pos);
    if (!at.isValid()) return;   // no page laid out: nothing to crop
    page_ = at.page;
    pressedAt_ = pos;
    movedTo_ = pos;
    origin_ = host.snapMeasured(page_, at.point).point;
    corner_ = origin_;
}

void CropTool::move(ToolHost& host, QPoint pos)
{
    if (page_ < 0) return;
    movedTo_ = pos;
    const PagePoint at = host.pagePointOn(page_, pos);
    if (at.isValid()) corner_ = host.snapMeasured(page_, at.point).point;
    host.requestRepaint();
}

void CropTool::release(ToolHost& host, QPoint pos)
{
    if (page_ < 0) return;
    move(host, pos);
    const int page = page_;
    const std::optional<core::PdfRect> asked = box();
    page_ = -1;
    host.requestRepaint();
    // A CLICK IS NOT A CROP, at the marquee's threshold on the SCREEN -- a crop a few points wide at a high zoom is a box.
    const QRect band = QRect(pressedAt_, movedTo_).normalized();
    if (!asked || band.width() < MarqueeZoomTool::kMinDragPixels || band.height() < MarqueeZoomTool::kMinDragPixels) return;
    host.cropToBox(page, *asked);
}

void CropTool::cancel(ToolHost& host)
{
    if (page_ < 0) return;
    page_ = -1;   // nothing was handed over yet: `Esc` is *stop drawing the box*, and nothing is asked
    host.requestRepaint();
}

void CropTool::paint(QPainter& painter, const ToolHost& host) const
{
    const std::optional<core::PdfRect> asked = box();
    if (!asked) return;
    const std::optional<QPointF> a = host.widgetPointOf(page_, core::PdfPoint{asked->left(), asked->bottom()});
    const std::optional<QPointF> b = host.widgetPointOf(page_, core::PdfPoint{asked->right(), asked->top()});
    if (!a || !b) return;
    const QRectF band = QRectF(*a, *b).normalized();
    // What stays is framed, dashed, in the palette's highlight -- judged in both themes (convention 20), as the
    // snapshot's band is.
    const QColor highlight = QGuiApplication::palette().color(QPalette::Highlight);
    QPen pen(highlight);
    pen.setStyle(Qt::DashLine);
    pen.setWidthF(1.0);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(band);
}

} // namespace rawpage::ui
