// ui/viewer/AnnotationOverlay -- one document window's annotations as something a person points at: the
// selection, its handles, a drag that moves, resizes or duplicates, the nudge, delete, and the pop-ups.
// Stage 6 T5, decisions D6-1, D6-9, D6-27, D6-28 and D6-30 (`Stage6-Annotations-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// ONE PER DOCUMENT WINDOW, SHOWN IN EVERY PANE -- `SelectionOverlay`'s shape (D5-8)
// ---------------------------------------------------------------------------------------------
// The selection is the window's, so a split shows it in every pane and either pane can drag it. A pane
// hands each call its own `Viewport` -- its layout, its scroll, its device-pixel ratio -- because every
// position a gesture arrives in is that pane's. The overlay reads the annotations from the DOCUMENT's
// store (`core/doc/AnnotationStore`) and never writes it: its whole mutation vocabulary is a `Command`
// handed to the runner `ui/shell/DocumentWindow` installs, which is `CommandJournal::run` (D6-1).
//
// ---------------------------------------------------------------------------------------------
// ONE GESTURE, ONE COMMAND (D6-1, guide case A-42)
// ---------------------------------------------------------------------------------------------
// A drag is ONE `MoveAnnotationCommand`, built from the offset at release; a resize ONE
// `EditAnnotationCommand`; an `Alt`+drag ONE `AddAnnotationCommand` of the copy; `Esc` during any of them
// builds none. A burst of arrow presses is ONE move, committed when no arrow has been pressed for
// `kNudgeSettleMs` or the focus leaves (D6-9). While a gesture is in flight the overlay draws a PREVIEW
// with Qt -- an outline where the annotation is going -- and the document is untouched until it ends.
//
// **Text markup is not dragged** (D6-30): a highlight belongs to its words, so a press on one starts the
// tool's own gesture (a text selection, with the Selection tool) and a CLICK on it -- a press and a release
// with no drag between -- selects it.
//
// ---------------------------------------------------------------------------------------------
// HIT WHERE IT IS DRAWN (D6-30, guide case A-41)
// ---------------------------------------------------------------------------------------------
// `core::hitsAnnotation`, at a tolerance of `kHitPixels` DEVICE pixels converted to page units at the
// pane's own scale -- so a hit is as easy at 400 % as at 50 %, and the same on a 150 % monitor as on a
// 100 % one. The topmost annotation wins: the last in its page's listing, which is the last drawn.
//
// ---------------------------------------------------------------------------------------------
// POP-UPS ARE VIEW STATE (D6-28)
// ---------------------------------------------------------------------------------------------
// A note's pop-up is painted here -- the note's author and text in a box -- and opening, moving and
// closing it constructs no command and changes no byte. `commenting/popupBehaviour` says when they show.
//
// It holds no catalog key (`ui/viewer/` holds none): a refusal is reported by signal, with its key.

#ifndef RAWPAGE_UI_VIEWER_ANNOTATIONOVERLAY_H
#define RAWPAGE_UI_VIEWER_ANNOTATIONOVERLAY_H

#include "core/Result.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/Annotation.h"
#include "core/doc/AnnotationStore.h"
#include "core/render/PagePresentation.h"

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QPainter;
class QTimer;

namespace rawpage::core {
class Command;
}

namespace rawpage::ui {

class AnnotationOverlay : public QObject {
    Q_OBJECT

public:
    // Where a gesture was made: the pane's layout, the part of it on screen, and its device-pixel ratio.
    struct Viewport {
        const core::render::PageLayout* layout = nullptr;
        core::DeviceRect visible;
        double dpr = 1.0;
    };

    using Runner = std::function<core::Status(std::unique_ptr<core::Command>)>;
    using Clock = std::function<std::int64_t()>;

    // When pop-ups show -- `commenting/popupBehaviour`'s three values (D6-28).
    enum class PopupBehaviour { Always, OnHover, Never };

    // A hit is this many DEVICE pixels out from what is drawn: under the three A-41 grades as outside.
    static constexpr double kHitPixels = 2.0;
    // A handle's half-width, in logical pixels: what a person aims a resize at.
    static constexpr double kHandlePixels = 4.0;
    // D6-9: a burst of nudges is committed this long after its last arrow.
    static constexpr int kNudgeSettleMs = 500;
    // A plain arrow moves one point; `Ctrl` with it a tenth -- in PAGE units, so the same keys make the
    // same change at every zoom, along the SCREEN's directions (D6-31).
    static constexpr double kNudgePoints = 1.0;
    static constexpr double kFineNudgePoints = 0.1;

    explicit AnnotationOverlay(QObject* parent = nullptr);
    ~AnnotationOverlay() override;

    // THE DOCUMENT'S STORE -- read, never written -- and what runs a command. A null store, or no runner,
    // makes the overlay inert: a companion window, a document nothing may edit.
    void setStore(const core::AnnotationStore* store);
    void setRunner(Runner runner);
    void setClock(Clock clock);
    const core::AnnotationStore* store() const noexcept { return store_; }
    bool isActive() const noexcept { return store_ != nullptr && store_->status() && static_cast<bool>(runner_); }

    // ---- The selection ----------------------------------------------------------------------------
    const std::vector<core::AnnotationId>& selection() const noexcept { return selection_; }
    bool hasSelection() const noexcept { return !selection_.empty(); }
    void select(std::vector<core::AnnotationId> ids);
    bool clearSelection();
    // The annotations selected, as the store holds them now.
    std::vector<core::Annotation> selected() const;

    // The topmost annotation drawn at `pos` (viewport logical pixels), or none.
    std::optional<core::AnnotationId> annotationAt(const Viewport& view, QPoint pos) const;

    // ---- Gestures ---------------------------------------------------------------------------------
    // Each returns whether the overlay took the event; the pane gives an event the overlay did not take
    // to the armed tool. `Shift` constrains a drag -- to 45 degrees, a resize to its proportions -- and
    // `Alt` makes the drag a duplicate; `Ctrl`+click adds to or takes from the selection.
    bool press(const Viewport& view, QPoint pos, Qt::KeyboardModifiers modifiers, int clicks = 1);
    bool move(const Viewport& view, QPoint pos, Qt::KeyboardModifiers modifiers);
    bool release(const Viewport& view, QPoint pos, Qt::KeyboardModifiers modifiers);
    bool isDragging() const noexcept;
    // `Esc` during a gesture: abandoned, nothing constructed. False when no gesture was in flight.
    bool cancel();

    // ---- Keys -------------------------------------------------------------------------------------
    // An arrow while something is selected (D6-9): accumulated, and committed as ONE move after
    // `kNudgeSettleMs` or `commitNudge()`. `dx`/`dy` are SCREEN steps (-1, 0, 1). False with nothing
    // selected, which leaves the arrow to scroll the page.
    bool nudge(const Viewport& view, int dx, int dy, bool fine);
    bool hasPendingNudge() const noexcept;
    core::Status commitNudge();
    // The pending nudge's offset in page units (for the preview, and for a case).
    QPointF pendingNudge() const noexcept { return QPointF(nudgeX_, nudgeY_); }

    core::Status deleteSelection();
    core::Status setAppearance(const core::AnnotationAppearance& appearance);
    core::Status setContents(const QString& text);
    core::Status setLocks(std::uint32_t set, std::uint32_t clear);

    // ---- Pop-ups (D6-28) --------------------------------------------------------------------------
    void setPopupBehaviour(PopupBehaviour behaviour);
    PopupBehaviour popupBehaviour() const noexcept { return popupBehaviour_; }
    // Whether moves with no button down are wanted: pop-ups that show on hover.
    bool wantsHover() const noexcept { return popupBehaviour_ == PopupBehaviour::OnHover; }
    // The pointer is over `pos` (or has left, with `pos` null): repaints when the hovered note changes.
    void hover(const Viewport& view, std::optional<QPoint> pos);
    // A double click on a note pins its pop-up open, or closes it.
    bool togglePopup(const core::AnnotationId& id);
    bool isPopupShown(const core::AnnotationId& id) const;
    // Where a shown pop-up is drawn, in viewport logical pixels; empty when it is not shown.
    std::optional<QRectF> popupRect(const Viewport& view, const core::AnnotationId& id) const;

    // ---- Painting ---------------------------------------------------------------------------------
    // The selection's outline and handles, a gesture's preview and the pop-ups -- over the tiles, never
    // in them (D4-11).
    void paint(QPainter& painter, const Viewport& view) const;
    // Where a page-space rectangle is drawn, in viewport logical pixels.
    static std::optional<QRectF> widgetRectOf(const Viewport& view, int page, const core::PdfRect& rect);

    // WHERE AN ANNOTATION IS DRAWN, in page space -- its `/Rect`, except for a `NoRotate` one (ISO 32000-1
    // §12.5.3), which a reader keeps UPRIGHT on screen with its top-left at the rectangle's upper-left corner:
    // on a page or view turned, that is another box than the rectangle. Measured at T5 against PDFium 153 on
    // `annots-rotated.pdf` at every quarter turn and two zooms -- and PDFium does NOT honour `NoZoom` (the icon
    // scales with the page), so neither does this.
    static std::optional<core::PdfRect> drawnRect(const Viewport& view, const core::Annotation& a);

    // The document changed -- a command, an undo, a redo. A selected annotation the store no longer
    // holds leaves the selection; a pinned pop-up likewise.
    void modelChanged();

    // The page and the rectangle the drag in flight would leave behind, in page space -- for a case.
    std::optional<core::PdfRect> previewRect() const;

signals:
    void changed();
    void selectionChanged();
    // A gesture's command was refused -- by a lock, a page that cannot be edited, a kind this build
    // cannot draw. The shell words it.
    void refused(const core::Error& error);

private:
    enum class DragKind { None, Move, Resize, Duplicate, Popup, MarkupClick };
    struct Drag {
        DragKind kind = DragKind::None;
        core::AnnotationId id;
        int page = -1;
        QPoint startPos;
        QPoint pos;
        int handle = -1;            // Resize: 0..7, clockwise from the top-left corner
        bool constrained = false;
        bool moved = false;
    };

    const core::Annotation* find(const core::AnnotationId& id) const;
    std::optional<core::PdfPoint> toPage(const Viewport& view, int page, QPointF pos) const;
    double tolerance(const Viewport& view) const;
    int handleAt(const Viewport& view, QPoint pos) const;
    // The drag's result: the offset in page units (Move, Duplicate) or the new rectangle (Resize).
    std::optional<core::PdfPoint> dragOffset(const Viewport& view) const;
    std::optional<core::PdfRect> resizedRect(const Viewport& view) const;
    core::Status run(std::unique_ptr<core::Command> command);
    std::int64_t now() const;
    // D6-27, asked before a gesture starts so a locked annotation is not even picked up.
    bool movable(const core::Annotation& a) const;

    const core::AnnotationStore* store_ = nullptr;
    Runner runner_;
    Clock clock_;
    std::vector<core::AnnotationId> selection_;

    Drag drag_;
    // The last viewport a drag was made in -- what `previewRect` and the paint read.
    Viewport dragView_;

    QTimer* nudgeTimer_ = nullptr;
    double nudgeX_ = 0.0;
    double nudgeY_ = 0.0;

    PopupBehaviour popupBehaviour_ = PopupBehaviour::OnHover;
    std::vector<core::AnnotationId> pinned_;               // shown by a double click
    std::optional<core::AnnotationId> hovered_;
    std::map<core::AnnotationId, QPointF> popupOffset_;    // moved by a drag of its title, logical px
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_ANNOTATIONOVERLAY_H
