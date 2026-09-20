// The tool family's base, and the surface a tool is allowed to touch. Stage 4 T7.
//
// ===========================================================================================
// A TOOL NAMES NO WIDGET, AND THAT IS WHAT MAKES THE FAMILY TESTABLE AND EXTENSIBLE.
// ===========================================================================================
// `Architecture-FileMap.md` §3.14 lists seventeen tools across Stages 4 to 10 — hand, select,
// marquee zoom, loupe, snapshot, markup, typewriter, note, drawing, ink, stamp, measure, redact,
// link, crop, field, object-edit. Four of them are Stage 4's. A base that took a `PageView&` would
// make every one of those thirteen later files depend on the widget, and would make the whole
// family untestable without a window.
//
// So a tool is handed a **`ToolHost`**: the four things a tool actually needs — the viewport
// arithmetic, the device pixel ratio, a way to ask for a repaint, and a way to record a view in the
// history. `ui/viewer/PageView` implements it; a case can implement it over a bare
// `ViewportController` with no widget at all.
//
// ===========================================================================================
// WHAT A TOOL MAY DO, AND THE ONE THING IT MAY NOT.
// ===========================================================================================
// A Stage 4 tool changes the VIEW: the scroll, the zoom, what is painted over the page. **It may
// not change the document.** From Stage 6 the family's whole job becomes translating a gesture into
// a `Command` handed to `CommandJournal` — the FileMap row says *"never into a direct model
// edit"* — but at Stage 4 there is no tool that edits anything, `mutation-lint` reports one command
// for the whole tree, and D4-1 is what that number proves.
//
// ===========================================================================================
// CANCELLATION IS PART OF THE BASE, NOT AN AFTERTHOUGHT.
// ===========================================================================================
// `Esc` must abandon a drag in progress and leave the view exactly where it was — which means a
// tool that changes the view DURING a drag (the hand does; the marquee does not) has to remember
// what to put back. `isDragging()` and `cancel()` are on the base because `ToolController` needs to
// ask any tool, and because a tool that forgot to implement them would silently make `Esc` do
// nothing in its own mode.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_TOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_TOOL_H

#include <QPoint>
#include <QSize>

#include <Qt>

class QPainter;
class QPixmap;

namespace rawpage::ui {

class ViewportController;

// The four Stage 4 tools, as a value. Named rather than numbered, and with an ASCII name beside
// them, for the reason `layoutModeName` and `destinationFitName` carry theirs: a surface, a log
// line and a settings file all need to say which tool without inventing a second spelling.
enum class ToolKind {
    Select,
    Hand,
    MarqueeZoom,
    Loupe,
};

inline constexpr int kToolKindCount = 4;

const char* toolKindName(ToolKind kind) noexcept;

// What a tool is allowed to reach. Deliberately small: every member here is something at least one
// of the seventeen tools genuinely needs, and nothing here is a widget.
class ToolHost {
public:
    virtual ~ToolHost() = default;

    virtual ViewportController& controller() = 0;
    virtual const ViewportController& controller() const = 0;

    // Logical-pixel size of the scrolled area, and the ratio that converts it to device pixels.
    // Both, because a tool works in the coordinates a mouse event arrives in (logical) and the
    // controller works in device pixels, and the conversion is the one place a tool can be wrong
    // on a 150 % panel while looking right on a 100 % one.
    virtual QSize viewportSize() const = 0;
    virtual double devicePixelRatio() const = 0;

    virtual void requestRepaint() = 0;

    // Records a view-history entry — `Alt+Left` / `Alt+Right`. A marquee zoom is deliberate
    // navigation and records one; a hand pan is a scroll and does not, which is the same
    // distinction `ViewportController::recordView` draws for the keyboard.
    virtual void recordView() = 0;

    // The frame the host painted most recently, at device pixel ratio — the LOUPE'S source, and
    // null for a host that does not keep one. The host keeps a frame only while a tool asks for
    // it (`Tool::needsFrame()`), because copying every paint into a pixmap to serve a tool nobody
    // armed is a cost paid for nothing.
    virtual const QPixmap* lastFrame() const { return nullptr; }
};

class Tool {
public:
    virtual ~Tool() = default;

    virtual ToolKind kind() const = 0;

    // The cursor while armed, and while a drag is in progress. Two, because the hand tool's
    // open/closed pair is the only feedback a person gets that a drag started.
    virtual Qt::CursorShape cursor() const = 0;
    virtual Qt::CursorShape draggingCursor() const { return cursor(); }

    // A tool that paints an overlay — the marquee's band, the loupe's lens — and one that needs
    // the host to keep its last frame. Both default to no, so a tool costs nothing it does not ask
    // for.
    virtual bool paintsOverlay() const { return false; }
    virtual bool needsFrame() const { return false; }

    // Wants a `mouseMoveEvent` even with no button down. Only the loupe does: a magnifier that
    // followed the cursor only while dragging would be a magnifier nobody could aim.
    virtual bool tracksHover() const { return false; }

    virtual void press(ToolHost& host, QPoint pos) = 0;
    virtual void move(ToolHost& host, QPoint pos) = 0;
    virtual void release(ToolHost& host, QPoint pos) = 0;

    // Abandon whatever is in progress and leave the view as it was before it started. Called by
    // `Esc`, and by `ToolController` when the armed tool changes under a live drag.
    virtual void cancel(ToolHost& host) = 0;

    virtual bool isDragging() const = 0;

    virtual void paint(QPainter& painter, const ToolHost& host) const
    {
        (void)painter;
        (void)host;
    }
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_TOOL_H
