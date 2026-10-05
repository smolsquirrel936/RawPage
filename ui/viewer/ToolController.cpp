#include "ui/viewer/ToolController.h"

#include "ui/viewer/ViewportController.h"

#include <QGuiApplication>
#include <QStyleHints>
#include <QTimer>

#include <cmath>

namespace rawpage::ui {
namespace {

// 60 Hz. The rate the scroll is stepped at, not the speed it moves: the speed is
// `pageDisplay/autoScrollSpeed` in pixels per SECOND, and the two are separate so that changing
// the tick rate cannot change how fast the document goes past.
constexpr int kAutoScrollTickMs = 16;

} // namespace

const char* toolKindName(ToolKind kind) noexcept
{
    switch (kind) {
    case ToolKind::Select:      return "Select";
    case ToolKind::Hand:        return "Hand";
    case ToolKind::MarqueeZoom: return "MarqueeZoom";
    case ToolKind::Loupe:       return "Loupe";
    case ToolKind::Snapshot:    return "Snapshot";
    case ToolKind::Annotate:    return "Annotate";
    }
    return "?";
}

const char* annotationToolKindName(AnnotationToolKind kind) noexcept
{
    switch (kind) {
    case AnnotationToolKind::StickyNote:      return "StickyNote";
    case AnnotationToolKind::Highlight:       return "Highlight";
    case AnnotationToolKind::Underline:       return "Underline";
    case AnnotationToolKind::Strikeout:       return "Strikeout";
    case AnnotationToolKind::TextBox:         return "TextBox";
    case AnnotationToolKind::Line:            return "Line";
    case AnnotationToolKind::Rectangle:       return "Rectangle";
    case AnnotationToolKind::Oval:            return "Oval";
    case AnnotationToolKind::Polygon:         return "Polygon";
    case AnnotationToolKind::Pencil:          return "Pencil";
    case AnnotationToolKind::Stamp:           return "Stamp";
    case AnnotationToolKind::MeasureDistance: return "MeasureDistance";
    case AnnotationToolKind::MeasureArea:     return "MeasureArea";
    case AnnotationToolKind::RedactMark:      return "RedactMark";
    case AnnotationToolKind::Squiggly:        return "Squiggly";
    case AnnotationToolKind::ReplaceText:     return "ReplaceText";
    case AnnotationToolKind::InsertText:      return "InsertText";
    }
    return "?";
}

ToolController::ToolController(ToolHost& host, QObject* parent) : QObject(parent), host_(host)
{
    autoScrollTimer_ = new QTimer(this);
    autoScrollTimer_->setInterval(kAutoScrollTickMs);
    connect(autoScrollTimer_, &QTimer::timeout, this, [this]() { step(kAutoScrollTickMs); });

    lastCursor_ = activeTool().cursor();
}

ToolController::~ToolController() = default;

Tool& ToolController::toolFor(ToolKind kind)
{
    switch (kind) {
    case ToolKind::Hand:        return hand_;
    case ToolKind::MarqueeZoom: return marquee_;
    case ToolKind::Loupe:       return loupe_;
    case ToolKind::Snapshot:    return snapshot_;
    case ToolKind::Annotate:    return annotationToolFor(annotationKind_);
    case ToolKind::Select:      break;
    }
    return select_;
}

Tool& ToolController::annotationToolFor(AnnotationToolKind which)
{
    if (which == AnnotationToolKind::StickyNote) return note_;
    if (MarkupTool::handles(which)) return markup_;
    return annotation_;
}

ToolKind ToolController::effective() const noexcept
{
    return temporaryHand_ ? ToolKind::Hand : armed_;
}

Tool& ToolController::activeTool()
{
    return toolFor(effective());
}

const Tool& ToolController::activeTool() const
{
    return const_cast<ToolController*>(this)->activeTool();
}

Qt::CursorShape ToolController::cursor() const
{
    const Tool& tool = activeTool();
    return tool.isDragging() ? tool.draggingCursor() : tool.cursor();
}

void ToolController::emitCursor()
{
    const Qt::CursorShape shape = cursor();
    if (shape == lastCursor_) return;
    lastCursor_ = shape;
    emit cursorChanged(shape);
}

void ToolController::arm(ToolKind kind)
{
    if (kind == armed_) return;

    // WHATEVER WAS IN PROGRESS IS ABANDONED FIRST. A drag left attached to a tool that is no
    // longer armed is a gesture nobody can finish and nobody can see -- and for the hand it would
    // leave the scroll wherever the pointer last was, which reads as the view jumping when a tool
    // was chosen.
    Tool& outgoing = activeTool();
    if (outgoing.isDragging()) outgoing.cancel(host_);

    armed_ = kind;
    emit armedChanged(armed_);
    emitCursor();
    host_.requestRepaint();
}

void ToolController::armAnnotation(AnnotationToolKind which)
{
    if (armed_ == ToolKind::Annotate && annotationKind_ == which) return;
    if (armed_ == ToolKind::Annotate) {
        // Another tool of the same family: whatever the outgoing one was doing is abandoned, as `arm` does.
        Tool& outgoing = activeTool();
        if (outgoing.isDragging()) outgoing.cancel(host_);
        annotationKind_ = which;
        annotation_.setWhich(which);
        markup_.setWhich(which);
        emit armedChanged(armed_);   // the same kind of tool, another tool: said all the same
        emitCursor();
        host_.requestRepaint();
        return;
    }
    annotationKind_ = which;
    annotation_.setWhich(which);
    markup_.setWhich(which);
    arm(ToolKind::Annotate);
}

namespace {

struct Arming {
    AnnotationToolKind kind;
    core::ActionId action;
};

constexpr Arming kArming[kAnnotationToolKindCount] = {
    {AnnotationToolKind::StickyNote, core::ActionId::AnnotStickyNote},
    {AnnotationToolKind::Highlight, core::ActionId::AnnotHighlight},
    {AnnotationToolKind::Underline, core::ActionId::AnnotUnderline},
    {AnnotationToolKind::Strikeout, core::ActionId::AnnotStrikeout},
    {AnnotationToolKind::TextBox, core::ActionId::AnnotTextBox},
    {AnnotationToolKind::Line, core::ActionId::AnnotLine},
    {AnnotationToolKind::Rectangle, core::ActionId::AnnotRectangle},
    {AnnotationToolKind::Oval, core::ActionId::AnnotOval},
    {AnnotationToolKind::Polygon, core::ActionId::AnnotPolygon},
    {AnnotationToolKind::Pencil, core::ActionId::AnnotPencil},
    {AnnotationToolKind::Stamp, core::ActionId::AnnotStamp},
    {AnnotationToolKind::MeasureDistance, core::ActionId::AnnotMeasureDistance},
    {AnnotationToolKind::MeasureArea, core::ActionId::AnnotMeasureArea},
    {AnnotationToolKind::RedactMark, core::ActionId::RedactMark},
    {AnnotationToolKind::Squiggly, core::ActionId::AnnotSquiggly},
    {AnnotationToolKind::ReplaceText, core::ActionId::AnnotReplaceText},
    {AnnotationToolKind::InsertText, core::ActionId::AnnotInsertText},
};

} // namespace

core::ActionId armingAction(AnnotationToolKind kind) noexcept
{
    for (const Arming& row : kArming) {
        if (row.kind == kind) return row.action;
    }
    return core::ActionId::AnnotHighlight;
}

bool createsAnnotations(AnnotationToolKind kind) noexcept
{
    return kind == AnnotationToolKind::StickyNote || kind == AnnotationToolKind::ReplaceText
           || kind == AnnotationToolKind::InsertText || markupSubtype(kind).has_value();
}

std::optional<core::AnnotationSubtype> markupSubtype(AnnotationToolKind kind) noexcept
{
    switch (kind) {
    case AnnotationToolKind::Highlight: return core::AnnotationSubtype::Highlight;
    case AnnotationToolKind::Underline: return core::AnnotationSubtype::Underline;
    case AnnotationToolKind::Strikeout: return core::AnnotationSubtype::StrikeOut;
    case AnnotationToolKind::Squiggly:  return core::AnnotationSubtype::Squiggly;
    default: return std::nullopt;
    }
}

std::optional<AnnotationToolKind> armedBy(core::ActionId action) noexcept
{
    for (const Arming& row : kArming) {
        if (row.action == action) return row.kind;
    }
    return std::nullopt;
}

std::optional<AnnotationToolKind> ToolController::armedAnnotation() const noexcept
{
    if (armed_ != ToolKind::Annotate) return std::nullopt;
    return annotationKind_;
}

void ToolController::setKeepToolSelected(bool keep)
{
    if (keep == keepToolSelected_) return;
    keepToolSelected_ = keep;
    emit keepToolSelectedChanged(keep);
}

void ToolController::setModifiers(Qt::KeyboardModifiers modifiers)
{
    const bool wanted = modifiers.testFlag(kTemporaryHandModifier);
    if (wanted == temporaryHand_) return;

    // A DRAG IN PROGRESS KEEPS ITS TOOL. Swapping the effective tool under a live gesture would
    // hand the release to a tool that never saw the press -- so the modifier is recorded and takes
    // effect at the next press, which is also what a person experiences: you do not change tools
    // mid-drag by pressing Shift.
    if (activeTool().isDragging()) return;

    temporaryHand_ = wanted;
    emitCursor();

    // AND THE PAGE IS REPAINTED, because the tool that paints an overlay just changed (S4 guide
    // I-27). The loupe's lens is painted by whichever tool is in effect, so under the temporary hand
    // it must go -- and come back when the modifier is let go. Until 2026-09-27 only the cursor
    // changed here, and the lens stayed on screen until a pan happened to repaint the page.
    host_.requestRepaint();
}

bool ToolController::press(QPoint pos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers, int clicks)
{
    setModifiers(modifiers);
    if (button != Qt::LeftButton) return false;

    Tool& tool = activeTool();

    // A `Shift`+CLICK WITH THE SELECTION TOOL ARMED EXTENDS THE SELECTION (S5 T5, D5-8) -- and `Shift` is
    // also the temporary hand. The gesture decides: the hand takes the press as it always has, and if the
    // release comes back where the press went down, it was a click, and the click extends. A drag pans.
    shiftClickFrom_ = (temporaryHand_ && armed_ == ToolKind::Select) ? std::optional<QPoint>(pos) : std::nullopt;

    tool.pressWith(host_, pos, PressContext{modifiers, clicks});
    emitCursor();
    return true;
}

bool ToolController::move(QPoint pos, Qt::KeyboardModifiers modifiers)
{
    setModifiers(modifiers);

    Tool& tool = activeTool();
    if (!tool.isDragging() && !tool.tracksHover()) return false;

    tool.move(host_, pos);
    emitCursor();
    return true;
}

bool ToolController::release(QPoint pos, Qt::MouseButton button)
{
    if (button != Qt::LeftButton) return false;

    Tool& tool = activeTool();
    if (!tool.isDragging() && !tool.tracksHover()) return false;

    tool.release(host_, pos);

    if (shiftClickFrom_) {
        const QPoint from = *shiftClickFrom_;
        shiftClickFrom_.reset();
        const int slop = QGuiApplication::styleHints() ? QGuiApplication::styleHints()->startDragDistance() : 4;
        if ((pos - from).manhattanLength() < slop) select_.extendTo(host_, pos);
    }
    emitCursor();
    return true;
}

bool ToolController::cancel()
{
    Tool& tool = activeTool();

    // AUTO-SCROLL FIRST. `Esc` during an auto-scroll stops it -- which is the thing a person
    // reaches for -- and that counts as having cancelled something, so the key is consumed.
    if (autoScrollRunning()) {
        stopAutoScroll();
        return true;
    }

    // A LOUPE IS NOT A DRAG but it is something on screen, so `Esc` puts its lens away and reports
    // that it did. Anything the reader can see disappear is a cancellation.
    if (tool.kind() == ToolKind::Loupe && loupe_.isVisible()) {
        loupe_.cancel(host_);
        emitCursor();
        return true;
    }

    // AN ANNOTATION TOOL GOES BACK TO THE SELECTION TOOL (§8's Annotation tools `Esc`, Stage 6 T5): the
    // tool is what is in front of the person, so putting it away is what `Esc` cancelled.
    if (armed_ == ToolKind::Annotate && !tool.isDragging()) {
        arm(ToolKind::Select);
        return true;
    }

    if (!tool.isDragging()) return false;   // NOTHING TO CANCEL -- A-42's control; do not consume

    tool.cancel(host_);
    shiftClickFrom_.reset();
    emitCursor();
    return true;
}

bool ToolController::stepLoupeMagnification(int steps)
{
    if (!loupe_.stepMagnification(steps)) return false;
    host_.requestRepaint();
    return true;
}

bool ToolController::isDragging() const
{
    return activeTool().isDragging();
}

void ToolController::paint(QPainter& painter) const
{
    const Tool& tool = activeTool();
    if (tool.paintsOverlay()) tool.paint(painter, host_);
}

// =============================================================================================
// Auto-scroll
// =============================================================================================

void ToolController::setAutoScrollSpeed(int pixelsPerSecond)
{
    if (pixelsPerSecond == autoScrollSpeed_) return;
    autoScrollSpeed_ = pixelsPerSecond;

    // ZERO STOPS IT. A running auto-scroll at zero speed is a control that reads back as on and
    // does nothing, which is precisely the shape Stage 2's I-11 is a warning about.
    if (autoScrollSpeed_ == 0 && autoScrollRunning()) stopAutoScroll();
}

bool ToolController::autoScrollRunning() const noexcept
{
    return autoScrollTimer_ != nullptr && autoScrollTimer_->isActive();
}

void ToolController::toggleAutoScroll()
{
    if (autoScrollRunning()) {
        stopAutoScroll();
        return;
    }
    if (autoScrollSpeed_ == 0) return;   // starting at zero would be a no-op that looked like one

    autoScrollCarry_ = 0.0;
    autoScrollTimer_->start();
    emit autoScrollChanged(true);
}

void ToolController::stopAutoScroll()
{
    if (!autoScrollRunning()) return;
    autoScrollTimer_->stop();
    autoScrollCarry_ = 0.0;
    emit autoScrollChanged(false);
}

void ToolController::advanceAutoScrollForTesting(int milliseconds)
{
    // ONLY WHILE IT IS RUNNING, because this stands in for the TIMER and a stopped timer does not
    // tick. The first version called `step()` unconditionally, and the case that pauses and then
    // advances found it: a paused auto-scroll went on scrolling, which would have made the pause
    // assertion pass only because nothing else was driving it.
    if (!autoScrollRunning()) return;
    step(milliseconds);
}

void ToolController::step(int milliseconds)
{
    if (autoScrollSpeed_ == 0) return;

    // SUB-PIXEL CARRY. At 20 px/s a 16 ms tick is 0.32 of a pixel; truncating each tick would move
    // the document zero pixels for ever, which is a speed setting that does nothing at exactly the
    // low end a person would choose. See the header.
    autoScrollCarry_ += autoScrollSpeed_ * (milliseconds / 1000.0);
    const double whole = std::trunc(autoScrollCarry_);
    if (whole == 0.0) return;
    autoScrollCarry_ -= whole;

    ViewportController& controller = host_.controller();
    const core::DevicePoint before = controller.scroll();
    controller.scrollBy(0.0, whole);

    // IT STOPS AT THE END RATHER THAN SPINNING. `setScroll` clamps, so past the last page every
    // tick would ask for a move and get none -- a timer running for ever against a wall, with the
    // document apparently frozen. Reaching the end is the natural end of the gesture.
    if (controller.scroll().y == before.y) stopAutoScroll();
}

} // namespace rawpage::ui
