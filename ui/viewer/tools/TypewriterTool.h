// ui/viewer/tools/TypewriterTool -- typed text: the text box (`Ctrl+Alt+T`, §8's Viewer row), the callout
// (`annot.callout`) and the typewriter (`annot.typewriter`, minted at T7). Stage 6 T7, decisions D6-7, D6-8
// and D6-34 (`Stage6-Annotations-Plan.md` §3).
//
// All three are ONE subtype, `FreeText`, told apart by `/IT` (D6-8: the typewriter is an annotation, never
// page content), and one gesture each:
//   * **Text box**: a DRAG sets where the box goes and how wide it is; a CLICK puts a box of
//     `AnnotationOverlay::kFreeTextWidthPoints` with its top-left corner there.
//   * **Callout**: the PRESS is what it points at; a DRAG takes the box to where it is released (its top-left
//     corner), a CLICK puts it up and to the right of the point.
//   * **Typewriter**: a CLICK is where the text starts; it has no box and no outline, and fits what is typed.
// Each then asks for its text FIRST (`ToolHost::requestAnnotationText`), in an editor over the box at the
// text's own size -- so it wraps where the appearance will -- which refuses at input any character no face can
// show and names it (D6-7). When the person finishes, the annotation is made ONCE, fitted to its text
// (`AnnotationOverlay::addFreeText`): one command, one undo step. `Esc` in the editor makes nothing. The text
// is turned with the page as it is shown (`ToolHost::displayedRotation`), so it reads upright where it was
// typed, and an ideograph is drawn in the person's input language (`ToolHost::hanPreference`, D6-34).
//
// A press on free text already there is the overlay's: it is picked up and moved; a DOUBLE click on it edits
// its text in place (`PageView::editTypedTextAt`).

#ifndef RAWPAGE_UI_VIEWER_TOOLS_TYPEWRITERTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_TYPEWRITERTOOL_H

#include "ui/viewer/tools/AnnotationTool.h"
#include "ui/viewer/tools/Tool.h"

namespace rawpage::ui {

class TypewriterTool final : public Tool {
public:
    // The three kinds this tool makes.
    static bool handles(AnnotationToolKind kind) noexcept;

    ToolKind kind() const override { return ToolKind::Annotate; }
    Qt::CursorShape cursor() const override
    {
        return which_ == AnnotationToolKind::Typewriter ? Qt::IBeamCursor : Qt::CrossCursor;
    }
    bool paintsOverlay() const override { return true; }

    AnnotationToolKind which() const noexcept { return which_; }
    void setWhich(AnnotationToolKind which) noexcept { which_ = which; }

    void press(ToolHost& host, QPoint pos) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;
    bool isDragging() const override { return dragging_; }
    void paint(QPainter& painter, const ToolHost& host) const override;

    // A callout made with a click puts its box this far up and to the right of the point, in logical pixels.
    static constexpr int kCalloutOffsetX = 40;
    static constexpr int kCalloutOffsetY = 48;
    // A callout's box is this wide, in page units.
    static constexpr double kCalloutWidthPoints = 140.0;

private:
    bool dragged(QPoint pos) const;

    AnnotationToolKind which_ = AnnotationToolKind::TextBox;
    bool dragging_ = false;
    QPoint pressedAt_;
    QPoint at_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_TYPEWRITERTOOL_H
