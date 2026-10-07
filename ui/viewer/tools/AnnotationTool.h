// ui/viewer/tools/AnnotationTool -- an annotation tool, ARMED. Stage 6 T5, decision D6-9
// (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// WHAT T5 ARMS, AND WHAT IT DOES NOT YET MAKE
// ===========================================================================================
// `Features.md` §8's fourteen `Ctrl+Alt+` chords each arm a tool -- the sticky note, the highlighter, the
// rectangle, the stamp, the redaction mark -- and arming one puts the page in the **Annotation tools**
// context (D6-9), which resolves through the Viewer so every page chord still works. That much is T5's,
// with the overlay (`ui/viewer/AnnotationOverlay`): with any of them armed a person can select, move,
// resize, nudge and delete what is already on the page, and `Esc` returns to the Selection tool.
//
// **What each tool CREATES is its own task's** -- markup and notes T6, typed text T7 (`TypewriterTool`), shapes and ink T8,
// stamps T10, measurement T12, redaction marks T14 -- and arrives as the classes `Architecture-FileMap.md`
// §3.14 names (`MarkupTool`, `NoteTool`, …). This class is what is armed for a kind whose task has not come:
// a press on an empty part of the page makes nothing, the tool says which it is and that it creates
// nothing yet. **Since T6** the text markup tools (`ui/viewer/tools/MarkupTool`) and the sticky note
// (`ui/viewer/tools/NoteTool`) create; `createsAnnotations` says which kinds do.
//
// NINETEEN KINDS, FOURTEEN CHORDS. §8 arms fourteen by chord; T6 adds the three text-markup kinds no chord
// names -- the squiggly (`annot.squiggly`, declared since Stage 1), replace text and insert text (minted at
// T6, chordless) -- and T7 the two typed kinds beside the text box: the callout (`annot.callout`, declared
// since Stage 1) and the typewriter (`annot.typewriter`, minted at T7, chordless, D6-8) -- reached from the
// tool finder, the QAT and the batch runner like every chordless command.
//
// The kind is a value with an ASCII name, for the reason `ToolKind` carries one.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_ANNOTATIONTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_ANNOTATIONTOOL_H

#include "core/cmd/ActionId.h"
#include "core/doc/Annotation.h"
#include "ui/viewer/tools/Tool.h"

#include <optional>

namespace rawpage::ui {

// The fourteen tools §8's Viewer table arms, in its order -- and T6's three and T7's two after them.
enum class AnnotationToolKind {
    StickyNote,
    Highlight,
    Underline,
    Strikeout,
    TextBox,
    Line,
    Rectangle,
    Oval,
    Polygon,
    Pencil,
    Stamp,
    MeasureDistance,
    MeasureArea,
    RedactMark,
    // Stage 6 T6: chordless (see the header).
    Squiggly,
    ReplaceText,
    InsertText,
    // Stage 6 T7: the two typed kinds beside the text box, chordless (see the header).
    Callout,
    Typewriter,
};

inline constexpr int kAnnotationToolKindCount = 19;
// The first fourteen are §8's Viewer rows, each with its `Ctrl+Alt+` chord.
inline constexpr int kChordedAnnotationToolCount = 14;

// Whether arming `kind` makes something: the six text-markup kinds and the sticky note (T6), and the text
// box, the callout and the typewriter (T7).
bool createsAnnotations(AnnotationToolKind kind) noexcept;
// The four kinds that MARK the text they are dragged across -- highlight, underline, strikeout, squiggly --
// and the subtype each writes; nullopt for every other kind.
std::optional<core::AnnotationSubtype> markupSubtype(AnnotationToolKind kind) noexcept;

const char* annotationToolKindName(AnnotationToolKind kind) noexcept;

// THE COMMAND THAT ARMS EACH, and back: the fourteen Viewer rows of `Features.md` §8 (D6-9). One table,
// read both ways, so a registration and a label cannot name different tools.
core::ActionId armingAction(AnnotationToolKind kind) noexcept;
std::optional<AnnotationToolKind> armedBy(core::ActionId action) noexcept;

class AnnotationTool final : public Tool {
public:
    ToolKind kind() const override { return ToolKind::Annotate; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

    AnnotationToolKind which() const noexcept { return which_; }
    void setWhich(AnnotationToolKind which) noexcept { which_ = which; }

    // Creates nothing yet -- see the header. The overlay takes every press that lands on an annotation
    // before the tool sees it.
    void press(ToolHost&, QPoint) override {}
    void move(ToolHost&, QPoint) override {}
    void release(ToolHost&, QPoint) override {}
    void cancel(ToolHost&) override {}
    bool isDragging() const override { return false; }

private:
    AnnotationToolKind which_ = AnnotationToolKind::Highlight;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_ANNOTATIONTOOL_H
