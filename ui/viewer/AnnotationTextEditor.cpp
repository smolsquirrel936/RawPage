// ui/viewer/AnnotationTextEditor -- implementation. Stage 6 T6. See the header for why the text comes first.

#include "ui/viewer/AnnotationTextEditor.h"

#include "core/doc/TypedText.h"

#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>

#include <algorithm>

namespace rawpage::ui {
namespace {

bool isEnter(int key)
{
    return key == Qt::Key_Return || key == Qt::Key_Enter;
}

// A KEY'S TEXT IS NOT ALWAYS TEXT ENTERING. Windows gives the editing keys text of their own -- Backspace U+0008,
// `Ctrl+A` … `Ctrl+Z` U+0001 … U+001A, Delete U+007F -- which the editor acts on and never inserts. Checked as
// typed text, each was refused and swallowed, so nothing typed could be corrected (S6 guide I-38). Text made of
// control characters alone is an edit; anything else is checked, a surrogate pair included.
bool isEditingKeyText(const QString& text)
{
    return std::all_of(text.cbegin(), text.cend(), [](QChar c) { return c.category() == QChar::Other_Control; });
}

} // namespace

AnnotationTextEditor::AnnotationTextEditor(QWidget* parent) : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("annotationTextEditor"));
    setTabChangesFocus(true);
    setFrameShape(QFrame::Box);
}

// THE FOCUS GOES BACK TO WHAT THE EDITOR WAS OPENED OVER, BEFORE IT HIDES. A focused widget that hides hands the focus
// to the next in the window's chain -- on the page, out of the page and past the refocus that would bring it back, so
// a pop-up the editor answered closed as if the person had gone elsewhere (S6 guide I-34). One that has lost the
// focus already -- the person clicked away -- leaves it where they put it.
void AnnotationTextEditor::returnFocus()
{
    if (hasFocus() && parentWidget() != nullptr) parentWidget()->setFocus(Qt::OtherFocusReason);
}

bool AnnotationTextEditor::commit()
{
    if (finished_) return false;
    finished_ = true;
    const QString text = toPlainText();
    returnFocus();
    hide();
    deleteLater();
    emit committed(text);
    return true;
}

bool AnnotationTextEditor::abandon()
{
    if (finished_) return false;
    finished_ = true;
    returnFocus();
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
    if (!event->text().isEmpty() && !isEditingKeyText(event->text()) && !admits(event->text())) {
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
