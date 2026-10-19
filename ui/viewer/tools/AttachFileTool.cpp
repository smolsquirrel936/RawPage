// ui/viewer/tools/AttachFileTool -- implementation. Stage 6 T11. See the header.

#include "ui/viewer/tools/AttachFileTool.h"

#include "ui/viewer/AnnotationOverlay.h"

#include <QGuiApplication>
#include <QStyleHints>

namespace rawpage::ui {

void AttachFileTool::press(ToolHost& host, QPoint pos)
{
    (void)host;
    pressed_ = true;
    pressedAt_ = pos;
}

void AttachFileTool::move(ToolHost& host, QPoint pos)
{
    (void)host;
    (void)pos;
}

void AttachFileTool::release(ToolHost& host, QPoint pos)
{
    if (!pressed_) return;
    pressed_ = false;
    const QStyleHints* hints = QGuiApplication::styleHints();
    if ((pos - pressedAt_).manhattanLength() >= (hints != nullptr ? hints->startDragDistance() : 4)) return;
    AnnotationOverlay* annotations = host.annotationOverlay();
    const PagePoint at = host.iconPointAt(pos, AnnotationToolKind::AttachFile);   // the whole pushpin on the page (I-25)
    if (annotations == nullptr || !at.isValid()) return;
    if (annotations->attachFileAt(at.page, at.point)) host.annotationToolUsed();
}

void AttachFileTool::cancel(ToolHost& host)
{
    (void)host;
    pressed_ = false;
}

} // namespace rawpage::ui
