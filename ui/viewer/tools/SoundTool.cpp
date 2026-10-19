// ui/viewer/tools/SoundTool -- implementation. Stage 6 T14. See the header.

#include "ui/viewer/tools/SoundTool.h"

#include "ui/viewer/AnnotationOverlay.h"

#include <QGuiApplication>
#include <QStyleHints>

namespace rawpage::ui {

void SoundTool::press(ToolHost& host, QPoint pos)
{
    (void)host;
    pressed_ = true;
    pressedAt_ = pos;
}

void SoundTool::move(ToolHost& host, QPoint pos)
{
    (void)host;
    (void)pos;
}

void SoundTool::release(ToolHost& host, QPoint pos)
{
    if (!pressed_) return;
    pressed_ = false;
    const QStyleHints* hints = QGuiApplication::styleHints();
    if ((pos - pressedAt_).manhattanLength() >= (hints != nullptr ? hints->startDragDistance() : 4)) return;
    AnnotationOverlay* annotations = host.annotationOverlay();
    const PagePoint at = host.iconPointAt(pos, which_);   // the whole speaker on the page (I-25)
    if (annotations == nullptr || !at.isValid()) return;
    const bool made = which_ == AnnotationToolKind::RecordAudio ? annotations->recordSoundAt(at.page, at.point)
                                                                : annotations->attachSoundAt(at.page, at.point);
    if (made) host.annotationToolUsed();
}

void SoundTool::cancel(ToolHost& host)
{
    (void)host;
    pressed_ = false;
}

} // namespace rawpage::ui
