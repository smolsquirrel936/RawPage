// ui/viewer/AnnotationTextEditor -- implementation. Stage 6 T6. See the header for why the text comes first.

#include "ui/viewer/AnnotationTextEditor.h"

#include <QFocusEvent>
#include <QKeyEvent>

namespace rawpage::ui {
namespace {

bool isEnter(int key)
{
    return key == Qt::Key_Return || key == Qt::Key_Enter;
}

} // namespace

AnnotationTextEditor::AnnotationTextEditor(QWidget* parent) : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("annotationTextEditor"));
    setTabChangesFocus(true);
    setFrameShape(QFrame::Box);
}

bool AnnotationTextEditor::commit()
{
    if (finished_) return false;
    finished_ = true;
    const QString text = toPlainText();
    hide();
    deleteLater();
    emit committed(text);
    return true;
}

bool AnnotationTextEditor::abandon()
{
    if (finished_) return false;
    finished_ = true;
    hide();
    deleteLater();
    emit abandoned();
    return true;
}

bool AnnotationTextEditor::event(QEvent* event)
{
    // `Enter` and `Esc` are the editor's, before any chord is resolved (see the header).
    if (event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (isEnter(key->key()) || key->key() == Qt::Key_Escape) {
            event->accept();
            return true;
        }
    }
    return QPlainTextEdit::event(event);
}

void AnnotationTextEditor::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        abandon();
        event->accept();
        return;
    }
    if (isEnter(event->key()) && !(event->modifiers() & Qt::ShiftModifier)) {
        commit();
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void AnnotationTextEditor::focusOutEvent(QFocusEvent* event)
{
    QPlainTextEdit::focusOutEvent(event);
    // A pop-up of the editor's own (its context menu) is not the person leaving it.
    if (finished_ || event->reason() == Qt::PopupFocusReason) return;
    if (toPlainText().isEmpty()) abandon();
    else commit();
}

} // namespace rawpage::ui
