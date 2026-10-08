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
// ---------------------------------------------------------------------------------------------
// WHAT THE TOOLS MAKE (Stage 6 T6) -- one command each, D6-1
// ---------------------------------------------------------------------------------------------
// Text markup is made FROM THE TEXT SELECTION (`ui/viewer/SelectionOverlay`): one annotation per page the
// selection touches, its quadrilaterals the selected runs of each line (`TextSelection::markupQuadsOn`),
// all of them ONE `AddAnnotationCommand`. A selection whose pages' text has not arrived yet -- or a gesture
// not yet resolved against it -- is marked when it arrives: `markText` answers `Pending` and finishes the
// mark itself. A REPLACE is the same strikeout grouped under a caret holding the replacement (`/IRT` the
// caret, `/RT /Group`, D6-12) -- caret and strikeouts one command, one undo step (guide A-51). An INSERTION
// is a caret on the boundary between the two characters nearest the click (`core::text::insertionPointAt`).
// A NOTE is a `Text` annotation with the `Comment` icon and its `Popup`, which name each other (D6-28: the
// pop-up is written once, closed, when the note is made). Each carries the person's name (`setAuthor`,
// D6-13) and the instant from the clock. TYPED TEXT (T7) -- a box, a callout, a typewriter -- is a `FreeText`
// fitted to its text by `core::fittedFreeText`, made and edited in place as one command each.
//
// It holds no catalog key (`ui/viewer/` holds none): a refusal is reported by signal, with its key.

#ifndef RAWPAGE_UI_VIEWER_ANNOTATIONOVERLAY_H
#define RAWPAGE_UI_VIEWER_ANNOTATIONOVERLAY_H

#include "core/Result.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/Annotation.h"
#include "core/doc/AnnotationStore.h"
#include "core/doc/TypedText.h"
#include "core/render/PagePresentation.h"

#include <QObject>
#include <QPoint>
#include <QPointer>
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

namespace rawpage::core::text {
struct InsertionPoint;
}

namespace rawpage::core {
class Command;
}

namespace rawpage::ui {

class SelectionOverlay;

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
    using Author = std::function<QString()>;

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
    // WHO a made annotation is by (S6 T6, D6-13): `identity/author`, else the login name -- asked each time
    // something is made, so a change on the Identity page applies to the next comment.
    void setAuthor(Author author);
    QString author() const;
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

    // ---- Making annotations (S6 T6) -- see the header ---------------------------------------------
    struct Style {
        std::vector<double> colour;   // /C: 1, 3 or 4 components; empty for the kind's own
        double opacity = 1.0;         // /CA, written only below 1
        QString blendMode;            // /BM (S6 T8): empty for the kind's own -- see `ui::blendNameFor`
    };
    // A new annotation of `subtype` on `page`: its `/NM`, the person's name, the instant.
    core::Annotation newAnnotation(core::AnnotationSubtype subtype, int page) const;

    enum class MarkOutcome { Made, Pending, Nothing, Refused };
    // Marks what `text` has selected with `subtype` -- or, with a `replacement`, strikes it out under a caret
    // holding the replacement. The text selection is cleared once the mark is made.
    MarkOutcome markText(SelectionOverlay& text, core::AnnotationSubtype subtype, const Style& style,
                         std::optional<QString> replacement = std::nullopt);
    bool hasPendingMark() const noexcept { return pendingMark_.has_value(); }
    // An insertion mark at the boundary nearest `at` on `page`, holding `inserted`.
    core::Status insertText(SelectionOverlay& text, int page, core::PdfPoint at, const QString& inserted,
                            const Style& style);
    // A sticky note whose icon's top-left corner is `at`, holding `contents`, and its pop-up.
    core::Status addNote(int page, core::PdfPoint at, const QString& contents, const Style& style);
    // ---- Typed text (S6 T7, D6-8) ---------------------------------------------------------------------
    struct TypedStyle {
        std::vector<double> colour{0.0};   // the text's, its outline's and a callout's leader's
        double opacity = 1.0;
        double borderWidth = 1.0;          // a typewriter has no outline, whatever this says
        double fontSize = core::kFreeTextDefaultSize;
        core::TypedFont han = core::TypedFont::Simplified;   // the person's language (D6-34)
        std::vector<double> fill;          // /IC (S6 T8): a box's and a callout's fill; empty for none
    };
    // A box, a callout or a typewriter on `page`, holding `text`, FITTED to it and made as ONE command: its
    // frame's top-left corner -- as the text reads, turned `rotate` -- at `topLeft`, `width` points wide (a
    // box and a callout; a typewriter fits its text), a callout's leader pointing at `target`. A character
    // no face can show is refused by name and nothing is made (D6-7).
    core::Status addFreeText(int page, core::FreeTextKind kind, core::PdfPoint topLeft, std::optional<double> width,
                             std::optional<core::PdfPoint> target, int rotate, const QString& text,
                             const TypedStyle& style);
    // Free text `id` given `text`, refitted -- a box keeps its width, a typewriter fits both ways, a callout
    // keeps its target -- as ONE `EditAnnotationCommand`. A `Locked` one keeps its size (D6-27: its text
    // may change, its size may not). `hanFallback` decides an ideograph only when `/DA` names no CJK face.
    core::Status editFreeText(const core::AnnotationId& id, const QString& text, core::TypedFont hanFallback);
    // A text box made with a click rather than a drag is this wide, in page units.
    static constexpr double kFreeTextWidthPoints = 180.0;

    // S6 T8: the selected free text set in `size` points and refitted to its lines -- ONE `EditAnnotationCommand`
    // (the properties bar's font size; D6-34(g) left it to T8).
    core::Status setFontSize(double size);

    // ---- The drawing tools (S6 T8) ------------------------------------------------------------------
    // Annotations a tool made -- their identity, author and instant from `newAnnotation`, their geometry, style
    // and rectangle the tool's -- as ONE `AddAnnotationCommand` (D6-1).
    core::Status addMade(std::vector<core::Annotation> annotations);
    // THE ERASER (D6-15): the topmost ink on `page` that `path` (page space, x y pairs) comes within `radius` of,
    // its covered samples removed and its strokes split -- ONE `EditAnnotationCommand`, or ONE
    // `DeleteAnnotationCommand` when nothing of it is left. Nothing touched makes nothing.
    core::Status eraseInk(int page, const std::vector<double>& path, double radius);

    // A refusal the page found before any command -- a character typed that no face can show, a comment
    // whose text is locked -- said the way a refused command is: as `refused`.
    void reportRefusal(const core::Error& error);

    // The size of the note icon, and of its pop-up, in page units.
    static constexpr double kNoteSize = 20.0;
    static constexpr double kPopupWidthPoints = 200.0;
    static constexpr double kPopupHeightPoints = 100.0;

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
    // S6 T6: a caret over `place`, styled.
    core::Annotation caretAt(int page, const core::text::InsertionPoint& place, const Style& style) const;
    void applyStyle(core::Annotation& a, const Style& style) const;
    void retryPendingMark();

    const core::AnnotationStore* store_ = nullptr;
    Runner runner_;
    Clock clock_;
    Author author_;

    // S6 T6: a mark waiting for its selection's text.
    struct PendingMark {
        QPointer<SelectionOverlay> text;
        core::AnnotationSubtype subtype = core::AnnotationSubtype::Highlight;
        Style style;
        std::optional<QString> replacement;
    };
    std::optional<PendingMark> pendingMark_;
    QMetaObject::Connection pendingConnection_;
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
