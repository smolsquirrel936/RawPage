#include "ui/viewer/tools/MarqueeZoomTool.h"

#include "ui/viewer/ViewportController.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPalette>

#include <algorithm>

namespace rawpage::ui {

QRect MarqueeZoomTool::band() const
{
    if (!dragging_) return {};
    return QRect(origin_, current_).normalized();
}

void MarqueeZoomTool::press(ToolHost& host, QPoint pos)
{
    (void)host;
    dragging_ = true;
    origin_ = pos;
    current_ = pos;
}

void MarqueeZoomTool::move(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;
    current_ = pos;
    host.requestRepaint();   // the rubber band, and nothing else -- the view does not move yet
}

void MarqueeZoomTool::cancel(ToolHost& host)
{
    if (!dragging_) return;
    dragging_ = false;

    // NOTHING TO RESTORE. This tool has changed no view state during the drag, so cancelling is
    // exactly *stop drawing the band* -- which is why guide case A-42's "the zoom and scroll are
    // exactly as before" is a statement about this tool having done nothing rather than about it
    // having undone something.
    host.requestRepaint();
}

void MarqueeZoomTool::release(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;

    current_ = pos;
    const QRect rect = band();
    dragging_ = false;
    host.requestRepaint();

    // A CLICK IS NOT A ZOOM -- see the header.
    if (rect.width() < kMinDragPixels || rect.height() < kMinDragPixels) return;

    ViewportController& controller = host.controller();
    const double ratio = host.devicePixelRatio();
    const double bandWidth = std::max(1.0, rect.width() * ratio);
    const double bandHeight = std::max(1.0, rect.height() * ratio);

    // THE TIGHTER AXIS, so the whole band stays visible -- see the header.
    const double factor = std::min(controller.viewportSize().width / bandWidth,
                                   controller.viewportSize().height / bandHeight);

    const core::DevicePoint centre{controller.scroll().x + (rect.center().x() * ratio),
                                   controller.scroll().y + (rect.center().y() * ratio)};

    const core::render::Zoom target = core::render::Zoom{controller.zoom().factor * factor}.clamped();
    const double applied = target.factor / controller.zoom().factor;

    // THE FIT INTENT IS CLEARED FIRST. A marquee zoom is an explicit zoom, and leaving a fit
    // standing would have the next resize recompute it and throw the zoom away -- which reads as
    // the tool working and then spontaneously undoing itself.
    controller.setFitMode(FitMode::None);
    controller.setZoom(target);
    controller.setScroll(core::DevicePoint{centre.x * applied - controller.viewportSize().width / 2.0,
                                           centre.y * applied - controller.viewportSize().height / 2.0});

    // DELIBERATE NAVIGATION, unlike a pan -- see the header.
    host.recordView();
}

void MarqueeZoomTool::paint(QPainter& painter, const ToolHost& host) const
{
    (void)host;
    if (!dragging_) return;

    const QRect rect = band();
    if (rect.isEmpty()) return;

    // FROM THE PALETTE, never a hard-coded blue. A marquee drawn in a fixed colour is a
    // dark-theme contrast defect in a brand-new surface, which is exactly what Stage 1's I-40 was
    // (convention 20). The APPLICATION's palette rather than a widget's, because a tool names no
    // widget (see `Tool.h`) -- and `ui/shell/ThemeManager` writes that palette from its tokens, so
    // reading it here is reading the theme.
    const QColor highlight = QGuiApplication::palette().color(QPalette::Highlight);
    QColor wash = highlight;
    wash.setAlpha(60);

    painter.fillRect(rect, wash);
    painter.setPen(highlight);
    painter.drawRect(rect);
}

} // namespace rawpage::ui
