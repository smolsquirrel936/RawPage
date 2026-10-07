// ui/viewer/AnnotationTextEditor -- implementation. Stage 6 T6. See the header for why the text comes first.

#include "ui/viewer/AnnotationTextEditor.h"

#include "core/doc/TypedText.h"

#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>

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
    if (!event->text().isEmpty() && !admits(event->text())) {
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

bool AnnotationTextEditor::admits(const QString& text)
{
    if (!refuses_) return true;
    const std::optional<char32_t> refused = core::firstRefusedCharacter(text);
    if (!refused) return true;
    emit characterRefused(static_cast<uint>(*refused));
    return false;
}

void AnnotationTextEditor::inputMethodEvent(QInputMethodEvent* event)
{
    // An input method's COMMIT is text entering; its preedit is not yet. A refused commit clears the preedit
    // and enters nothing.
    if (!event->commitString().isEmpty() && !admits(event->commitString())) {
        QInputMethodEvent nothing(QString(), {});
        QPlainTextEdit::inputMethodEvent(&nothing);
        event->accept();
        return;
    }
    QPlainTextEdit::inputMethodEvent(event);
}

void AnnotationTextEditor::insertFromMimeData(const QMimeData* source)
{
    // A paste or a drop: refused whole, never in part -- the person sees nothing of it went in.
    if (source != nullptr && source->hasText() && !admits(source->text())) return;
    QPlainTextEdit::insertFromMimeData(source);
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
