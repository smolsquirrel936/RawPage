// ui/viewer/SelectionOverlay -- one document window's text selection: the page text it reads, the gesture
// that makes it, the highlight painted over the tiles, and the copy. Stage 5 T5, decisions D5-8, D5-9 and
// D5-10 (`Stage5-TextSearch-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// ONE PER DOCUMENT WINDOW, SHOWN IN EVERY PANE
// ---------------------------------------------------------------------------------------------
// D5-8: *one selection per document window, shown in every pane of a split*. So this is not a child
// of a `PageView` -- a split has up to four -- but of `ui/shell/DocumentWindow`, which hands the same
// object to every pane it adopts. A pane paints it and its `SelectTool` drives it; none owns it. A
// second window over the same file (`window.new`) is a second `DocumentWindow` and has its own, because
// it is a second window. The selection is NOT session state and dies with the window.
//
// ---------------------------------------------------------------------------------------------
// WHERE THE TEXT COMES FROM (D5-3)
// ---------------------------------------------------------------------------------------------
// From the engine thread, through `RenderScheduler::enqueueText`, and never from the UI thread. A page a
// person is selecting on is asked for at `Visible` -- someone is waiting for it -- and the gesture is
// kept as the POINTS it was made at until the text arrives, so a drag that starts on a page never
// extracted completes when the page does (§5.3's "the drag resumes", guide A-55). A copy of pages not yet
// extracted -- `Ctrl+A` then `Ctrl+C` on a 900-page file -- asks for the rest at `Background`, with
// progress, and `Esc` cancels it and leaves the clipboard as it was (D5-9, guide A-51).
//
// The pages it has read are kept in a `core::text::DocumentText` for as long as the window lives. That is
// the selection's own store, keyed by this window's handle; D5-4's shared, budgeted, `DocumentId`-keyed
// `core/text/DocumentIndex` is T6's and does not exist yet.
//
// ---------------------------------------------------------------------------------------------
// PAINTED, NEVER RASTERIZED (D4-11, applied to a fourth overlay)
// ---------------------------------------------------------------------------------------------
// The highlight is painted over pixels the scheduler already returned, through the layout's own mapping
// (`PageLayout::toContent`), so making, extending and clearing a selection enqueues no tile and changes
// no cache key (guide A-53). A page's TEXT request is not a tile: it is asked for once and kept.
//
// ---------------------------------------------------------------------------------------------
// THE CLIPBOARD (D5-10)
// ---------------------------------------------------------------------------------------------
// One `QMimeData` with `text/plain` and `text/html`; Qt's Windows platform publishes the HTML as
// CF_HTML itself (guide A-50 reads it back through Win32). `platform::IRichClipboard` is not built at
// Stage 5. **Nothing here writes the document** (D5-1): a copy writes the clipboard and nothing else.
//
// It holds no catalog key (`ui/viewer/` holds none): what happened is reported by signal, and the shell
// words it.

#ifndef RAWPAGE_UI_VIEWER_SELECTIONOVERLAY_H
#define RAWPAGE_UI_VIEWER_SELECTIONOVERLAY_H

#include "core/backend.h"
#include "core/render/PagePresentation.h"
#include "core/render/RenderScheduler.h"
#include "core/text/TextSelection.h"

#include <QColor>
#include <QObject>

#include <map>
#include <optional>

class QMimeData;
class QPainter;

namespace rawpage::ui {

class SelectionOverlay : public QObject {
    Q_OBJECT

public:
    explicit SelectionOverlay(core::render::RenderScheduler& scheduler, QObject* parent = nullptr);
    ~SelectionOverlay() override;

    // The document, or none. Forgets the selection and every page read, and cancels what was asked for.
    void setDocument(core::DocumentHandle document, int pageCount);
    core::DocumentHandle document() const noexcept { return document_; }

    // ---- The gesture -- `ui/viewer/tools/SelectTool`'s calls, in PAGE space -------------------------
    struct PressOptions {
        core::text::Granularity granularity = core::text::Granularity::Character;
        bool column = false;   // `Alt`+drag (D5-8)
        bool extend = false;   // `Shift`+click: the anchor stays, the focus moves
    };
    void press(int page, core::PdfPoint point, const PressOptions& options);
    // The pointer moved. For a COLUMN, `page` must be the page the press was on -- the tool asks the
    // host for the point in that page's space, wherever the pointer is.
    void drag(int page, core::PdfPoint point);
    void release();
    // `Esc` under a live drag: back to what was selected before the press.
    void cancelGesture();
    bool isGestureActive() const noexcept { return gestureActive_; }
    // The page a live column gesture is on, or -1.
    int columnGesturePage() const noexcept;

    // ---- The selection -------------------------------------------------------------------------------
    const core::text::TextSelection& selection() const noexcept { return selection_; }
    bool hasSelection() const noexcept { return !selection_.isEmpty(); }
    // `Esc` with nothing live: clears, and says whether there was anything to clear -- `false` must not
    // consume the key (Stage 4's A-42 control, guide A-54).
    bool clear();
    // `Ctrl+A`: the whole DOCUMENT (D5-9, Q6), lazily -- nothing is extracted until a page is shown or
    // copied.
    void selectAll();

    // ---- The copy ------------------------------------------------------------------------------------
    enum class CopyOutcome {
        Copied,            // on the clipboard now
        Pending,           // pages are being extracted first; `copied` or `copyCancelled` follows
        NothingSelected,
        NotExtractable,    // the selection holds only glyphs with no trustworthy Unicode (D5-2)
    };
    CopyOutcome copy();
    bool isCopyPending() const noexcept { return pendingCopy_; }
    // `Esc` during a pending copy. The clipboard is left as it was.
    bool cancelPendingCopy();

    // What a copy would write NOW, from the pages already read -- for the cases, and what `copy()` uses.
    core::text::CopiedText copiedText() const;

    // The one place a copy becomes a `QMimeData` -- public so guide A-50's Win32 read-back publishes
    // exactly what the product publishes. The caller owns the result.
    static QMimeData* mimeDataFor(const core::text::CopiedText& copied);

    // ---- Painting ------------------------------------------------------------------------------------
    // Every selected run on every page `layout` shows inside `visible`, in widget pixels. A page covered
    // by the selection whose text has not been read is asked for at `Visible` -- it is on screen.
    void paint(QPainter& painter, const core::render::PageLayout& layout, const core::DeviceRect& visible,
               double devicePixelRatio, const QColor& colour);

    // The same rectangles in CONTENT device pixels, for a case to compare with the ink (rule 6).
    std::vector<core::DeviceRect> highlightRects(const core::render::PageLayout& layout, int page) const;

    // ---- The text ------------------------------------------------------------------------------------
    const core::text::DocumentText& text() const noexcept { return text_; }
    // Asks for a page's text if it has not been read or asked for; true when it is already here.
    bool ensurePage(int page, core::render::RenderPriority priority);
    // How many text requests this object has made -- A-24's "a selection asks at `Visible`" is read
    // from the scheduler's own counters, and this is the case's check that the asks were ours.
    int requestsMade() const noexcept { return requestsMade_; }

signals:
    // Anything a pane paints changed: the selection, or a page's text arrived under it.
    void changed();
    // Whether anything is selected, emitted only when that flips -- what `edit.copy`'s enablement follows,
    // without the whole command set being re-asked on every mouse move of a drag.
    void presenceChanged(bool hasSelection);
    void copyProgress(int done, int total);
    void copied(int characters, int unmappedSkipped);
    void copyNotExtractable(int unmappedSkipped);
    void copyCancelled();

private:
    void onText(const core::render::TextRequest& request, const core::render::TextResult& result);
    void notifyChanged();
    void recompute();
    void finishCopy();
    void cancelRequests();

    core::render::RenderScheduler& scheduler_;
    core::render::RenderScheduler::SubscriptionToken subscription_ = 0;

    core::DocumentHandle document_;
    core::text::DocumentText text_;
    core::text::TextSelection selection_;

    // Tickets for pages asked for and not yet delivered, by page.
    std::map<int, core::render::RenderTicket> requested_;
    int requestsMade_ = 0;

    // THE GESTURE, as the points it was made at -- resolved into `selection_` whenever the pages it
    // needs have been read, which may be after the release.
    struct Point {
        int page = -1;
        core::PdfPoint point;
    };
    bool gestureActive_ = false;
    bool gestureResolved_ = true;
    PressOptions pressOptions_;
    Point anchor_;
    Point focus_;
    // An anchor that is already a pair of carets -- `Ctrl+A`'s, extended by a `Shift`+click.
    std::optional<core::text::TextUnit> anchorUnit_;

    // What a cancelled gesture puts back.
    core::text::TextSelection beforeGesture_;
    Point beforeAnchor_;
    std::optional<core::text::TextUnit> beforeAnchorUnit_;
    PressOptions beforeOptions_;

    bool pendingCopy_ = false;
    int pendingTotal_ = 0;
    bool hadSelection_ = false;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_SELECTIONOVERLAY_H
