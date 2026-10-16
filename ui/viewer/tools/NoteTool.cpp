// ui/viewer/tools/NoteTool -- implementation. Stage 6 T6. See the header.

#include "ui/viewer/tools/NoteTool.h"

#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/PropertiesBar.h"

#include <QGuiApplication>
#include <QStyleHints>

namespace rawpage::ui {

void NoteTool::press(ToolHost& host, QPoint pos)
{
    (void)host;
    pressedAt_ = pos;
    dragging_ = true;
}

void NoteTool::move(ToolHost& host, QPoint pos)
{
    (void)host;
    (void)pos;
}

void NoteTool::release(ToolHost& host, QPoint pos)
{
    if (!dragging_) return;
    dragging_ = false;
    const QStyleHints* hints = QGuiApplication::styleHints();
    if ((pos - pressedAt_).manhattanLength() >= (hints != nullptr ? hints->startDragDistance() : 4)) return;
    AnnotationOverlay* annotations = host.annotationOverlay();
    const PagePoint at = host.pagePointAt(pos);
    if (annotations == nullptr || !at.isValid()) return;

    const core::ToolDefaults defaults = host.toolDefaults(AnnotationToolKind::StickyNote);
    const AnnotationOverlay::Style style{defaults.colour, defaults.opacity, QString(), std::nullopt};
    ToolHost* const where = &host;
    (void)host.requestAnnotationText(pos, [where, annotations, at, style](const QString& text) {
        if (annotations->addNote(at.page, at.point, text, style)) where->annotationToolUsed();
    });
}

void NoteTool::cancel(ToolHost& host)
{
    (void)host;
    dragging_ = false;
}

} // namespace rawpage::ui
