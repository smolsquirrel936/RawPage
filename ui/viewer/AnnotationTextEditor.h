// ui/viewer/AnnotationTextEditor -- the one-off editor a note, an insertion and a replacement are typed into.
// Stage 6 T6, decisions D6-1 and D6-12 (`Stage6-Annotations-Plan.md` §3).
//
// WHY THE TEXT IS ASKED FOR FIRST. A note made empty and then given its text would be TWO commands -- an add
// and an edit -- and two undo steps for one gesture; a replace whose strikeout and caret came before the
// replacement would be worse, a group made in two halves (guide case A-51: one undo step). So the tool asks
// for the text, and the annotation is made ONCE, with it, when the person finishes typing.
//
// HOW A PERSON FINISHES. `Enter` commits; `Shift+Enter` is a new line; `Esc` abandons and makes nothing; the
// focus leaving commits what was typed -- the click elsewhere on the page that ends a note in every reader --
// and abandons an editor left empty. It finishes once: whichever comes first wins, and the editor deletes
// itself.
//
// ITS KEYS ARE ITS OWN. The window's `ShortcutBinder` offers the focused widget first refusal of every chord
// (Qt's `ShortcutOverride`), and a text editor claims printable keys -- so typing `K` here does not toggle
// keep-the-tool (§8's Annotation tools row). `Enter` and `Esc` are claimed too: Annotation tools binds both,
// and `edit.cancel` stays unregistered (Stage 4's I-06) so the editor's `Esc` reaches it either way.
//
// It holds no catalog key: what it shows is what the person types.

#ifndef RAWPAGE_UI_VIEWER_ANNOTATIONTEXTEDITOR_H
#define RAWPAGE_UI_VIEWER_ANNOTATIONTEXTEDITOR_H

#include <QPlainTextEdit>
#include <QString>

namespace rawpage::ui {

class AnnotationTextEditor : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit AnnotationTextEditor(QWidget* parent = nullptr);

    // Commits what is typed, as `Enter` does. False when it had already finished.
    bool commit();
    // Abandons, as `Esc` does.
    bool abandon();
    bool isFinished() const noexcept { return finished_; }

signals:
    void committed(const QString& text);
    void abandoned();

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    bool finished_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_ANNOTATIONTEXTEDITOR_H
