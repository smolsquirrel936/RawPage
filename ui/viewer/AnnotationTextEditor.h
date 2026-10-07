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
// A TYPED COMMENT REFUSES WHAT CANNOT BE DRAWN, AS IT IS TYPED (S6 T7, D6-7). Free text is drawn in an
// appearance, in five faces; a character none of them holds would be drawn as a box or as nothing. With
// `setRefusesUnshowable`, every way text enters -- a key, an input method's commit, a paste, a drop -- is
// checked BEFORE it reaches the document (`core::firstRefusedCharacter`), refused whole, and the first refused
// character reported (`characterRefused`) so the page can name it. A note's text is never drawn, and refuses
// nothing.
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

    // S6 T7: refuse at input what no face can show -- see the header.
    void setRefusesUnshowable(bool refuses) noexcept { refuses_ = refuses; }
    bool refusesUnshowable() const noexcept { return refuses_; }

signals:
    void committed(const QString& text);
    void abandoned();
    // The first character of something refused, as a code point.
    void characterRefused(uint character);

protected:
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;

private:
    // Whether `text` may enter; when not, says which character, once.
    bool admits(const QString& text);

    bool finished_ = false;
    bool refuses_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_ANNOTATIONTEXTEDITOR_H
