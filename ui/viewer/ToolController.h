// The active-tool state machine -- Stage 4 T7, `Architecture-FileMap.md` §3.14.
//
// ===========================================================================================
// WHAT IT OWNS, AND WHY IT IS NOT FOUR IF-STATEMENTS IN `PageView`.
// ===========================================================================================
// Stage 2 T14 put a three-value enum and a `dragging_` flag in `PageView`, and said so: *"the
// minimum that makes the three bindings honest rather than the maximum that could be built"*. The
// maximum is this file, and the four things it adds are each a behaviour no amount of branching in
// a widget expresses cleanly:
//
//   1. **Sticky arming.** A tool stays armed until another is chosen. (Stage 6's annotation tools
//      add the other mode -- one-shot, returning to Select after a use -- which is what
//      `annot.keepToolSelected` toggles. The switch lives here, off, with no Stage 4 caller.)
//   2. **The temporary hand.** `Features.md` §1.2: *"Temporary hand tool while a modifier key is
//      held, without leaving the current tool."* The armed tool does not change; the EFFECTIVE
//      one does, and reverts on release.
//   3. **`Esc`.** Abandon a drag and leave the view where it was -- and report whether anything
//      was actually abandoned, because `Esc` that cancelled nothing must not be consumed.
//   4. **Auto-scroll.** A timer, a signed speed, and the rule that it stops at the end of the
//      document rather than spinning against a clamped scroll offset.
//
// ===========================================================================================
// THE TEMPORARY-HAND MODIFIER IS `Shift`, AND THE CHOICE IS FORCED RATHER THAN PREFERRED.
// ===========================================================================================
// Every other candidate is already spoken for by `Features.md` §8, which this stage does not
// touch (plan §10):
//
//   * **`Space`** -- the conventional answer, and in §8's Viewer table it is `nav.nextScreen`.
//     Holding it would page the document while arming the hand.
//   * **`Alt`** -- §8's Global table binds a bare `Alt` to `app.ribbonKeyTips`, which fires on
//     press. Holding it would open the ribbon's key tips.
//   * **`Ctrl`** -- the wheel-zoom modifier, and `pageDisplay/wheelZooms` swaps what it means.
//     Overloading it would make one key mean two things during one gesture.
//
// `Shift` is what is left, and it is legitimate rather than merely vacant: a bare modifier is not
// a chord `ShortcutBinder::chordFor` will produce -- its header says it returns nothing for a bare
// `Ctrl` or `Shift` -- so it cannot collide with a dispatch. **What it does collide with, said
// out loud:** `Shift+Space` is `nav.prevScreen`, so holding `Shift` to pan and then pressing
// `Space` pages backwards as well. Harmless, because the hand does nothing without a drag, and
// recorded so the next reader does not rediscover it as a defect.
//
// ===========================================================================================
// AUTO-SCROLL: ONE COMMAND, A SIGNED SETTING, AND NO THIRD ID.
// ===========================================================================================
// `Features.md` §1.2 asks for *"auto-scroll at an adjustable speed, with pause and reverse"*.
// **Pause is the toggle** -- `view.autoScroll` stops what is running and starts what is not, which
// is what a pause is when there is nothing else to resume into. **Speed and direction are one
// signed value**, `pageDisplay/autoScrollSpeed` in device pixels per second, so reverse is a
// negative speed and is reachable from Preferences, the settings file and the batch runner rather
// than only from a key. Minting a third `ActionId` for *reverse* would be a permanent public name
// for the sign of a number that already has a home.

#ifndef RAWPAGE_UI_VIEWER_TOOLCONTROLLER_H
#define RAWPAGE_UI_VIEWER_TOOLCONTROLLER_H

#include "ui/viewer/tools/AnnotationTool.h"
#include "ui/viewer/tools/DrawingTool.h"
#include "ui/viewer/tools/HandTool.h"
#include "ui/viewer/tools/InkTool.h"
#include "ui/viewer/tools/LoupeTool.h"
#include "ui/viewer/tools/MarkupTool.h"
#include "ui/viewer/tools/MarqueeZoomTool.h"
#include "ui/viewer/tools/NoteTool.h"
#include "ui/viewer/tools/TypewriterTool.h"
#include "ui/viewer/tools/SelectTool.h"
#include "ui/viewer/tools/SnapshotTool.h"
#include "ui/viewer/tools/Tool.h"

#include <QObject>
#include <QPoint>

#include <Qt>

#include <optional>

class QPainter;
class QTimer;

namespace rawpage::ui {

class ToolController : public QObject {
    Q_OBJECT

public:
    // `host` must outlive this object -- it is the widget that owns it.
    explicit ToolController(ToolHost& host, QObject* parent = nullptr);
    ~ToolController() override;

    // ---- Arming -------------------------------------------------------------------------------
    // Sticky: the tool stays until another is chosen. Arming a different tool CANCELS whatever the
    // current one was doing, rather than leaving a half-finished drag attached to a tool nobody
    // can see any more.
    void arm(ToolKind kind);
    ToolKind armed() const noexcept { return armed_; }

    // ---- The annotation tools (Stage 6 T5, D6-9) ---------------------------------------------------
    // Arms `ToolKind::Annotate` as `which` -- one of §8's fourteen. Arming another annotation tool while
    // one is armed swaps it, and says so through `armedChanged`.
    void armAnnotation(AnnotationToolKind which);
    // The annotation tool armed, or none when the armed tool is not one.
    std::optional<AnnotationToolKind> armedAnnotation() const noexcept;
    // `K` (§8, Annotation tools): keep the tool armed after it is used, or return to the Selection tool.
    // Read since T6 by the host when a tool reports it made something (`ToolHost::annotationToolUsed`). Since
    // T8 it is `commenting/stickyTool`: the shell sets it from the key on every view, and `K` writes the key.
    bool keepToolSelected() const noexcept { return keepToolSelected_; }
    void setKeepToolSelected(bool keep);

    // `Enter` (§8's Annotation tools row, `annot.finishShape`, S6 T8): the polygon, polyline or cloud in progress
    // made from its vertices. False when none is in progress.
    bool finishShape();
    bool shapeInProgress() const noexcept;
    // The drawing tool, for the cases that assert a polygon's vertices (S6 T8). Read-only: driven by events.
    const DrawingTool& drawing() const noexcept { return drawing_; }

    // What input is actually going to -- `armed()`, or `Hand` while the temporary modifier is
    // held. The distinction is the whole of behaviour 2 above, and it is why a case must read this
    // rather than `armed()` when it asks what a drag will do.
    ToolKind effective() const noexcept;

    Tool&       activeTool();
    const Tool& activeTool() const;

    Qt::CursorShape cursor() const;

    // ---- The temporary hand -------------------------------------------------------------------
    static constexpr Qt::KeyboardModifier kTemporaryHandModifier = Qt::ShiftModifier;

    // Called with the modifiers of EVERY key and mouse event the host sees, which is what makes
    // the release reliable: a modifier released outside the widget produces no key-release event
    // here, so the state is re-derived from the next event that arrives rather than remembered
    // from the last one. Guide case A-41's second clause is exactly that path.
    void setModifiers(Qt::KeyboardModifiers modifiers);
    bool temporaryHandActive() const noexcept { return temporaryHand_; }

    // The loupe's state -- where the lens is and whether it is pinned -- for the cases that assert
    // how it answers a click and a drag (S4 guide I-26). Read-only: the loupe is driven by events.
    const LoupeTool& loupe() const noexcept { return loupe_; }

    // The Snapshot tool's band, for the cases that assert what a drag would copy (S5 T10).
    const SnapshotTool& snapshot() const noexcept { return snapshot_; }

    // Steps the lens's magnification `steps` rungs and repaints -- `Alt`+wheel (decision D4-22).
    // Returns whether it changed.
    bool stepLoupeMagnification(int steps);

    // ---- Input ---------------------------------------------------------------------------------
    // Each returns whether the tool took the event; the host falls back to its base class when not.
    // `clicks` is how many presses in a row this one is -- the host counts them (S5 T5, D5-8).
    bool press(QPoint pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers, int clicks = 1);
    bool move(QPoint pos, Qt::KeyboardModifiers modifiers);
    bool release(QPoint pos, Qt::MouseButton button);

    // `Esc`. Returns whether anything was actually abandoned -- **false must not consume the key**,
    // because `edit.cancel` is `Common`'s and something else may want it. Guide case A-42's control.
    bool cancel();

    bool isDragging() const;
    void paint(QPainter& painter) const;

    // ---- Auto-scroll ----------------------------------------------------------------------------
    // Device pixels per second; NEGATIVE runs backwards. Zero stops it, because a running
    // auto-scroll at zero speed is a control that reads back as on and does nothing.
    void setAutoScrollSpeed(int pixelsPerSecond);
    int  autoScrollSpeed() const noexcept { return autoScrollSpeed_; }

    void toggleAutoScroll();
    void stopAutoScroll();
    bool autoScrollRunning() const noexcept;

    // Advances auto-scroll by `milliseconds` without waiting for the timer. It is how a case
    // measures the rate at all -- a headless run has no wall clock it can trust to a frame -- and
    // it is a BACK DOOR, so every case that uses it says what it therefore does not cover
    // (convention 19): that the timer fires at all, which is manual case M-12's.
    void advanceAutoScrollForTesting(int milliseconds);

signals:
    void armedChanged(ToolKind kind);
    void keepToolSelectedChanged(bool keep);
    void cursorChanged(Qt::CursorShape cursor);
    void autoScrollChanged(bool running);

private:
    void       emitCursor();
    Tool&      toolFor(ToolKind kind);
    // S6 T6: the tool object an annotation kind is -- the markup tool, the note, or T5's armed placeholder.
    Tool&      annotationToolFor(AnnotationToolKind which);
    void       step(int milliseconds);

    ToolHost& host_;

    SelectTool      select_;
    HandTool        hand_;
    MarqueeZoomTool marquee_;
    LoupeTool       loupe_;
    SnapshotTool    snapshot_;
    AnnotationTool  annotation_;
    MarkupTool      markup_;     // S6 T6
    NoteTool        note_;       // S6 T6
    TypewriterTool  typed_;      // S6 T7: the text box, the callout, the typewriter
    DrawingTool     drawing_;    // S6 T8: line, arrow, rectangle, oval, polygon, polyline, cloud
    InkTool         ink_;        // S6 T8: the pencil and the eraser
    AnnotationToolKind annotationKind_ = AnnotationToolKind::Highlight;
    bool            keepToolSelected_ = false;

    ToolKind armed_ = ToolKind::Select;
    bool     temporaryHand_ = false;

    // S5 T5: where a `Shift`-press under the Selection tool went down -- a release there is a
    // `Shift`+click, which extends the selection, and anywhere else a pan (see `press`).
    std::optional<QPoint> shiftClickFrom_;
    Qt::CursorShape lastCursor_ = Qt::ArrowCursor;

    QTimer* autoScrollTimer_ = nullptr;
    int     autoScrollSpeed_ = 0;

    // Sub-pixel carry. Without it a slow speed truncates to zero pixels every tick and the
    // document never moves -- a speed setting that reads back as set and does nothing, at exactly
    // the low end where a person would choose it.
    double autoScrollCarry_ = 0.0;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLCONTROLLER_H
