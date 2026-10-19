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

#include "core/Geometry.h"
#include "core/Measure.h"
#include "core/Snap.h"
#include "core/doc/TypedText.h"
#include "core/settings/CommentingTypes.h"

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>

#include <Qt>

#include <functional>
#include <optional>

class QPainter;
class QPixmap;

namespace rawpage::ui {

class AnnotationOverlay;
class SelectionOverlay;
class ViewportController;

// A point on a page, in PAGE space -- PDF points, unrotated user space. `page` -1 is "no page".
struct PagePoint {
    int page = -1;
    core::PdfPoint point;

    bool isValid() const noexcept { return page >= 0; }
};

// What a press carries beyond its position (S5 T5): the modifiers held and how many clicks it is -- two,
// three and four select a word, a line and a block (D5-8). Only the Selection tool reads either today.
struct PressContext {
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    int clicks = 1;
};

// WHAT A TYPED COMMENT'S EDITOR IS (S6 T7): where it sits, how big its text is, whether it wraps, what it
// starts holding, and which characters it refuses at input (D6-7). A note's, an insertion's and a
// replacement's editor is the default: beside the click, wrapping, refusing nothing.
struct AnnotationTextRequest {
    QPoint at;                         // the editor's top-left, viewport logical pixels, when there is no box
    std::optional<QRect> box;          // where the text will be drawn: the editor covers it exactly
    std::optional<double> fontPixels;  // the text's size on screen, logical pixels: what it will be drawn at
    bool wrap = true;                  // false: a line breaks only where the person breaks it
    QString initial;                   // what it starts holding -- the text of a comment edited in place
    bool typed = false;                // drawn in an appearance: refuses what `core::firstRefusedCharacter` refuses
};

// The four Stage 4 tools and Stage 5's Snapshot (T10, D5-11), as a value. Named rather than numbered, and with an ASCII name beside
// them, for the reason `layoutModeName` and `destinationFitName` carry theirs: a surface, a log
// line and a settings file all need to say which tool without inventing a second spelling.
enum class ToolKind {
    Select,
    Hand,
    MarqueeZoom,
    Loupe,
    Snapshot,
    // Stage 6 T5 (D6-9): one of the fourteen annotation tools is armed -- which one is
    // `ToolController::armedAnnotation()` (`ui/viewer/tools/AnnotationTool.h`).
    Annotate,
};

inline constexpr int kToolKindCount = 6;

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

    // The LOUPE'S SHARP LENS (S4 D4-20): paint `source` -- a region of the viewport, in logical
    // widget pixels -- re-rasterized at the magnified scale into `lens`, over whatever the tool has
    // already drawn there. Returns false when there is nothing sharp to paint yet (the region has
    // not been still long enough, or its raster has not arrived), and the tool's enlarged frame
    // stays as it is. The host decides when to ask the engine; the tool only draws.
    virtual bool paintSharpLens(QPainter& painter, const QRect& lens, const QRectF& source) const
    {
        (void)painter;
        (void)lens;
        (void)source;
        return false;
    }

    // ---- The Selection tool's three (S5 T5, D5-8) -------------------------------------------------
    // The document window's selection, which every pane of a split shows -- null for a host with no
    // document, and in a case that has none.
    virtual SelectionOverlay* selection() const { return nullptr; }

    // The page under a viewport position, in page space -- or, over a gap or the desk, the NEAREST page
    // with the point held inside it, so a drag that leaves a page keeps selecting to its edge rather than
    // stopping dead. Invalid only when no page is laid out.
    virtual PagePoint pagePointAt(QPoint pos) const
    {
        (void)pos;
        return {};
    }

    // A viewport position in ONE NAMED page's space, wherever the position is: a column selection stays
    // on the page it started on (`core::render::PageLayout::toPage`).
    virtual PagePoint pagePointOn(int page, QPoint pos) const
    {
        (void)page;
        (void)pos;
        return {};
    }

    // S6 guide I-25: `pagePointAt`, moved so that the icon `tool`'s click hangs there is DRAWN whole on the page --
    // a click on the desk arrives at a page's edge, and an icon hung outward from it was made where nothing draws
    // it (`AnnotationOverlay::keepIconOnPage`). A host with no layout answers `pagePointAt`.
    virtual PagePoint iconPointAt(QPoint pos, core::CommentingTool tool) const
    {
        (void)tool;
        return pagePointAt(pos);
    }

    // ---- The Snapshot tool's one (S5 T10, D5-11) --------------------------------------------------
    // Copy `band` -- a region of the viewport, in logical widget pixels -- as the document draws it. The
    // host re-rasterizes it (it owns the scheduler and the request builder; the tool owns neither) and
    // the result reaches the clipboard when it arrives. Returns whether anything was asked for: false
    // over no page, and in a host that has no document.
    virtual bool takeSnapshot(const QRect& band)
    {
        (void)band;
        return false;
    }

    // ---- The annotation tools' three (S6 T6) ------------------------------------------------------
    // The document window's annotations -- what a tool hands what it made to, and through which it runs the
    // one command a gesture means (D6-1). Null for a host with no document, and in a case that has none.
    virtual AnnotationOverlay* annotationOverlay() const { return nullptr; }

    // A one-off text editor at `pos` (viewport logical pixels) for what a note, an insertion or a replacement
    // says. `commit` runs with the typed text when the person finishes -- `Enter`, or the focus leaving with
    // something typed -- and NOT when they abandon it (`Esc`, or leaving it empty). Returns whether an editor
    // opened: false in a host that has none, where nothing is made.
    // Not virtual (S6 T7): the request below is what a host implements, and this is its default form.
    bool requestAnnotationText(QPoint pos, std::function<void(const QString&)> commit)
    {
        AnnotationTextRequest request;
        request.at = pos;
        return requestAnnotationText(request, std::move(commit));
    }
    // S6 T7: the same, described in full -- a typed comment's editor over the box it will be drawn in.
    virtual bool requestAnnotationText(const AnnotationTextRequest& request, std::function<void(const QString&)> commit)
    {
        (void)request;
        (void)commit;
        return false;
    }

    // S6 T7: the quarter turns page `page` is SHOWN at -- its own `/Rotate` and the view's -- so typed text is
    // written turned the same way and reads upright where it was typed (D6-34).
    virtual int displayedRotation(int page) const
    {
        (void)page;
        return 0;
    }

    // S6 T7: the CJK face an ideograph typed now is drawn in when nothing else decides it -- the person's input
    // language, else the system's (D6-34). The rule is core's (`core::hanPreference`); the languages are the host's.
    virtual core::TypedFont hanPreference() const { return core::TypedFont::Simplified; }

    // S6 T7: logical pixels per page unit at the current zoom -- what an editor sizes typed text by.
    virtual double pixelsPerPoint() const { return 1.0; }

    // A tool made something: the host goes back to the Selection tool unless `K` keeps this one armed (§8's
    // Annotation tools row, `annot.keepToolSelected`).
    virtual void annotationToolUsed() {}

    // ---- The drawing tools' two (S6 T8) ---------------------------------------------------------------
    // What `tool` draws with now: the session's change, else the settings family (`ui/viewer/ToolDefaults`).
    // A host with no settings answers the table's defaults.
    virtual core::ToolDefaults toolDefaults(core::CommentingTool tool) const { return core::builtInToolDefaults(tool); }

    // Where a page-space point is in the viewport, logical pixels -- what a preview is painted at, so a polygon
    // in progress stays on its page when the view scrolls. Nullopt for a page not laid out.
    virtual std::optional<QPointF> widgetPointOf(int page, core::PdfPoint point) const
    {
        (void)page;
        (void)point;
        return std::nullopt;
    }

    // ---- The measuring tools' four (S6 T12, D6-14) ----------------------------------------------------
    // WHERE A MEASURED POINT LANDS: `point` on `page` snapped to the page's paths -- their endpoints, midpoints and
    // crossings, and a point on one with `measuring/snapToPaths` -- then the annotations' sides, the guides and the
    // grid, all under the one device-pixel threshold (`core/Snap.h`). A host with no document returns it unsnapped.
    virtual core::SnapResult snapMeasured(int page, core::PdfPoint point)
    {
        (void)page;
        return core::SnapResult{point, core::SnapSource::None, core::SnapSource::None, std::nullopt};
    }
    // What a new measurement is made with: the scale the three `measuring/*` keys state, and whether its reading
    // goes in `/Contents`.
    struct MeasureSettings {
        core::MeasureScale scale;
        bool writeToComment = true;
    };
    virtual MeasureSettings measureSettings() const { return {}; }
    // A distance measured to CALIBRATE: `points` long on the page. The host asks what it is known to be and sets the
    // scale. Returns whether it asked.
    virtual bool calibrateFrom(double points)
    {
        (void)points;
        return false;
    }
    // A measurement was made, reading `reading` -- for the session's history and the status bar.
    virtual void measurementMade(const QString& reading) { (void)reading; }
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

    // S6 T8: `Shift` is this tool's -- a drawing tool's constraint (§8) -- and not the temporary hand's
    // (`ToolController::kTemporaryHandModifier`), which would otherwise take the press and pan.
    virtual bool usesShift() const { return false; }

    virtual void press(ToolHost& host, QPoint pos) = 0;
    // The press with what it carries (S5 T5). Every tool but the Selection tool ignores both, which is
    // what the default says.
    virtual void pressWith(ToolHost& host, QPoint pos, const PressContext& context)
    {
        (void)context;
        press(host, pos);
    }
    virtual void move(ToolHost& host, QPoint pos) = 0;
    // The move with its modifiers (S6 T8): a drawing tool constrains its drag while `Shift` is held. Every other
    // tool ignores them, which is what the default says.
    virtual void moveWith(ToolHost& host, QPoint pos, Qt::KeyboardModifiers modifiers)
    {
        (void)modifiers;
        move(host, pos);
    }
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
