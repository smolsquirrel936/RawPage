// ui/viewer/ComparisonView -- two documents compared: side by side, scrolled together by ALIGNED page, or one
// over the other in two tints; every difference listed beside them and stepped through with `Ctrl+]` and
// `Ctrl+[`. Stage 5 T13, decision D5-18 (`Stage5-TextSearch-Plan.md` §3), guide cases A-103 to A-107.
//
// ---------------------------------------------------------------------------------------------
// A SURFACE OF ITS OWN OVER TWO DOCUMENTS (D5-18)
// ---------------------------------------------------------------------------------------------
// Not a `SplitViewContainer` mode: a split is N views of ONE document over one handle and one journal
// (D4-13), and a comparison is two documents. So this holds TWO `DocumentHandle`s of its own -- opened for
// it, closed by it, through the same three-step protocol `DocumentWindow` runs -- two `PageView`s, each with
// its `TilePresenter`, and NO journal. Nothing here can construct a `Command`, and a document compared
// while it is open in a tab is a second handle on its file, as a `window.new` companion is: the tab's
// journal is never reached (guide A-107). It shows no rulers, grid or guides.
//
// IT IS A WINDOW OWNED BY THE MAIN WINDOW, not a tab: a tab is a `DocumentWindow`, one document with a
// journal, and every tab command -- save, rotate, undo, reflow, find -- would have a comparison to refuse.
// Owned, so the main window's `ShortcutBinder` still sees its keys (`belongsToScope` walks the window chain)
// and it goes when that window goes. One per main window: a second comparison replaces the first.
//
// ---------------------------------------------------------------------------------------------
// WHAT RUNS WHERE (D5-3, D5-17)
// ---------------------------------------------------------------------------------------------
//   1. TEXT: both handles are attached to `DocumentIndex` -- so a page a tab over the same file already
//      extracted costs nothing -- and every page is asked for at `Background`. The UI thread extracts
//      nothing and analyzes nothing: it holds each page's `IndexedPage`, and the layout analysis that makes a
//      `PageReading` of it runs on the comparison's own thread (`ComparisonRun::setReadingSource`).
//   2. THE DIFF: `ComparisonRun`, on a thread of its own, delivered here on the UI thread's event loop.
//   3. RASTERS, at *appearance* only: asked for FROM the comparison's thread, which waits while the request
//      is posted to this thread, enqueued at `Background` with `CachePolicy::Bypass` (D5-4) and delivered
//      back -- so the engine is entered only by the render thread, as for every tile.
// Closing the window cancels all three and waits for none of them longer than the render thread's drain.
//
// ---------------------------------------------------------------------------------------------
// SCROLLED BY ALIGNED PAGE (D5-18)
// ---------------------------------------------------------------------------------------------
// When either side scrolls, the other is put at the PARTNER of the page at the leader's top edge, the same
// fraction down it. A page with no partner -- inserted on one side, deleted on the other -- holds the other
// side at the top of the next page that has one. After an inserted page, a pixel-synchronized pair would
// show page n beside page n for the rest of the document, one page out; this shows page n beside n + 1.
// Until the comparison has aligned the pages, partners are by POSITION -- said here because a person
// scrolling while the text is still being read sees that.
//
// ---------------------------------------------------------------------------------------------
// THE OVERLAY (D5-18): one aligned pair at a time, the newer drawn over the older
// ---------------------------------------------------------------------------------------------
// Both pages are rasterized at the canvas's scale through the scheduler and COMPOSITED when shown, never
// rendered as one: each pixel is (newer, min(older, newer), older) in grey levels, so ink the two share is
// grey -- one image -- ink only the older has is RED and ink only the newer has is BLUE. The same two
// colours mark the differences side by side.
//
// IT ZOOMS AND SCROLLS, AND TURNS AT THE EDGE (S5 guide I-24, the user's design, 2026-10-04). The canvas sits in a
// scroll area and has a fit and a zoom of its own -- Fit Page on entering, `Ctrl+0`, `Ctrl+2` Fit Width, `Ctrl+1`
// Actual Size, `Ctrl+=`/`Ctrl+-` and `Ctrl`+wheel the ladder, the last about the cursor. The wheel, the arrows,
// `Page Up`/`Page Down` and `Space` scroll the pair; at its bottom or top they turn to the next or previous pair --
// the wheel only after one whole notch AT the edge, so a scroll that reaches the end does not also turn.
// `Ctrl+Page Down`/`Ctrl+Page Up` and `Home`/`End` turn directly. ONLY THE VISIBLE REGION IS RASTERIZED, with half
// a screen round it, so a zoomed page is never one image of hundreds of megapixels; the composite is that region,
// placed at `overlayOrigin()` on the pair's page. Until the I-24 fix the canvas was fitted and had no wheel at all.
//
// ---------------------------------------------------------------------------------------------
// THE KEYS (D5-18, clarified at T13)
// ---------------------------------------------------------------------------------------------
// It declares the Viewer context, where §8 binds `Ctrl+]`, `Ctrl+[` and `Ctrl+Alt+C`. But every OTHER
// Viewer and Common command resolves its target as the TAB in front of the main window -- so a `Ctrl+R`,
// `Ctrl+Z` or `Ctrl+=` pressed here would rotate, undo or zoom the document behind it. It therefore claims
// every key through `ShortcutBinder`'s first-refusal handshake, as Reflow view claims its reading keys
// (D5-16), EXCEPT the three comparison commands and the Global table. Of the keys it keeps it implements the
// reading ones itself: the arrows, `Page Up`/`Page Down` and `Space` (the views' own), `Home`/`End`,
// `Ctrl+Page Up`/`Ctrl+Page Down`, and the zoom and fit chords applied to both sides.
//
// ---------------------------------------------------------------------------------------------
// THE OPTIONS ARE THE SETTINGS (D5-14's rule, D5-20's keys)
// ---------------------------------------------------------------------------------------------
// The bar's sensitivity and three ignore switches write `documents/compareSensitivity` and the three
// `documents/compareIgnore*` keys and nothing else; a change to any of them -- from the bar, the dialog or
// Preferences -- re-runs the comparison over the text already read.
//
// **What this does not claim.** The comparison is graded on `corpus/compare/`'s generated pairs (D5-23);
// whether what it reports reads as the change a person made is manual case M-08's.

#ifndef RAWPAGE_UI_VIEWER_COMPARISONVIEW_H
#define RAWPAGE_UI_VIEWER_COMPARISONVIEW_H

#include "app/Services.h"
#include "core/backend.h"
#include "core/render/DpiPolicy.h"
#include "core/render/RenderRequest.h"
#include "core/text/DocumentComparer.h"
#include "core/text/DocumentIndex.h"

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QWidget>

#include <array>
#include <memory>
#include <optional>
#include <optional>
#include <vector>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QKeyEvent;
class QLabel;
class QPushButton;
class QScrollArea;
class QSplitter;
class QStackedWidget;
class QWheelEvent;

namespace rawpage::ui {

class DifferencesPane;
class LoupeRaster;
class PageView;

class ComparisonView : public QWidget {
    Q_OBJECT

public:
    enum class Mode { SideBySide, Overlay };
    enum class State { Reading, Comparing, Finished, Cancelled };

    // One side: its file and a handle the BACKEND HAS ALREADY OPENED, which this view now owns and closes.
    struct Side {
        QString              path;
        core::DocumentHandle handle;
    };

    ComparisonView(app::Services services, Side older, Side newer, QWidget* parent = nullptr);
    ~ComparisonView() override;

    // ---- What a case reads ---------------------------------------------------------------------------------
    PageView*        view(core::text::CompareSide side) const noexcept;
    DifferencesPane* differencesPane() const noexcept { return pane_; }
    const QString&   path(core::text::CompareSide side) const noexcept;
    core::DocumentHandle handle(core::text::CompareSide side) const noexcept;

    State state() const noexcept { return state_; }
    bool  isFinished() const noexcept { return state_ == State::Finished; }
    const core::text::CompareOptions& options() const noexcept { return options_; }
    // The last result, or null before the first run finishes.
    const core::text::ComparisonResult* result() const noexcept { return result_.get(); }
    // The differences in the order the pane lists them and `Ctrl+]` steps: by the aligned pair they are on,
    // in the newer document's order, then top to bottom.
    const std::vector<core::text::Difference>& differences() const noexcept { return ordered_; }
    QString statusText() const;

    // The page on the other side aligned with `page` of `side`, or -1 for a page inserted or deleted.
    int partnerOf(core::text::CompareSide side, int page) const;

    // ---- Mode ----------------------------------------------------------------------------------------------
    Mode mode() const noexcept { return mode_; }
    void setMode(Mode mode);

    // ---- The differences -----------------------------------------------------------------------------------
    int  currentDifference() const noexcept { return current_; }
    bool hasDifferences() const noexcept { return !ordered_.empty(); }
    // `Ctrl+]` and `Ctrl+[`: the next or the previous difference, wrapping at either end with a message.
    void nextDifference();
    void previousDifference();
    // Shows difference `index` in both views (or the overlay) and selects its row.
    void goToDifference(int index);

    // ---- The overlay ---------------------------------------------------------------------------------------
    // The aligned pairs the overlay steps through, as (older page, newer page), -1 for a missing side.
    const std::vector<std::pair<int, int>>& overlayPairs() const noexcept { return pairs_; }
    int  overlayPair() const noexcept { return pair_; }
    void setOverlayPair(int index);
    // The composite as last built, in device pixels; null until both rasters of the pair have arrived.
    const QImage& overlayImage() const noexcept { return composite_; }
    // Device pixels per PDF point the overlay's rasters were asked at.
    double overlayScale() const noexcept { return overlayScale_; }
    // Where the composite's top-left is on the pair's page raster, in device pixels: (0, 0) whenever the whole page
    // is in view, as at Fit Page.
    QPoint overlayOrigin() const noexcept { return compositeOrigin_; }
    // The overlay's fit -- `None` once a zoom is chosen -- and the zoom it shows, whichever set it.
    core::render::FitMode overlayFit() const noexcept { return overlayFit_; }
    core::render::Zoom    overlayZoom() const;
    void setOverlayFit(core::render::FitMode fit);
    // A zoom, about `anchor` on the canvas (logical pixels) or the middle of what is in view.
    void setOverlayZoom(core::render::Zoom zoom, std::optional<QPointF> anchor = std::nullopt);
    QScrollArea* overlayScrollArea() const noexcept { return overlayScroll_; }
    // Where the pair's page is on the overlay's canvas, in logical pixels.
    QRectF overlayPageRect() const;

    // Stops a run in progress; what it had is discarded and the view says so.
    void cancel();

signals:
    // A run finished (`result()` is set) or was cancelled.
    void finished();
    void statusChanged(const QString& text);
    void currentDifferenceChanged(int index);
    // The overlay's composite was rebuilt.
    void overlayChanged();

protected:
    // A closed comparison is RETIRED: its run stopped and its settings subscription deaf, before the deferred
    // delete -- so writing the next comparison's options does not re-run the one it replaces.
    void closeEvent(QCloseEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    class RasterBridge;
    class OverlayCanvas;
    friend class OverlayCanvas;

    struct Held {
        Side                      file;
        const core::PageTree*     pages = nullptr;
        core::text::DocumentIndex::EntryId entry = 0;
        std::vector<std::shared_ptr<const core::text::IndexedPage>> text;
        int                       textHeld = 0;
    };

    void buildBar();
    void askForText();
    void onIndexEvent(const core::text::DocumentIndex::Event& event);
    void take(core::text::CompareSide side, int page);
    void startRun();
    void stopRun();
    void onProgress(int done, int total);
    void onFinished(const core::text::ComparisonResult& result);
    void onSettingsChanged();
    void syncBarFromSettings();
    void writeSetting(int control);

    void order(const core::text::ComparisonResult& result);
    void applyMarks();
    void setStatus(const QString& text);

    // Side by side, aligned.
    void syncFrom(PageView* leader);
    PageView* other(PageView* view) const;
    core::text::CompareSide sideOf(const PageView* view) const;

    // The overlay.
    void rebuildPairs();
    // The scale and the canvas's size from the fit or zoom; then, with `request`, the region in view.
    void aimOverlay(bool request = true);
    // The region in view, asked for unless the one already asked for covers it -- or always, with `force`.
    void aimOverlayRegion(bool force);
    void composite();
    // Pair `index`, scrolled to its top or, with `atBottom`, its bottom.
    void showOverlayPair(int index, bool atBottom);
    // The current difference's marks brought into view.
    void revealOnOverlay(const core::text::Difference& difference);
    void overlayWheel(QWheelEvent* event);
    // Scrolls `pixels` down (or up, negative); at the edge in that direction, turns the pair instead.
    void scrollOverlay(int pixels);
    void zoomOverlay(int direction, std::optional<QPointF> anchor = std::nullopt);

    // The keys this surface keeps (see the header).
    bool shellTakes(const QKeyEvent* event) const;
    bool handleKey(QObject* watched, QKeyEvent* event);
    void zoomBoth(int direction);
    void fitBoth(int fit);

    // Where a page-level or region change is drawn: its rectangles, or the whole page.
    std::vector<core::PdfRect> marksOn(const core::text::Difference& difference, core::text::CompareSide side) const;

    app::Services services_;
    std::array<Held, 2> sides_;

    PageView*        older_ = nullptr;
    PageView*        newer_ = nullptr;
    OverlayCanvas*   canvas_ = nullptr;
    QScrollArea*     overlayScroll_ = nullptr;
    QStackedWidget*  stack_ = nullptr;
    DifferencesPane* pane_ = nullptr;
    QComboBox*       modeChoice_ = nullptr;
    QComboBox*       sensitivityChoice_ = nullptr;
    QCheckBox*       ignoreHeaderFooter_ = nullptr;
    QCheckBox*       ignorePageNumbers_ = nullptr;
    QCheckBox*       ignoreWhitespace_ = nullptr;
    QPushButton*     cancelButton_ = nullptr;
    QLabel*          status_ = nullptr;
    QLabel*          legend_ = nullptr;

    core::text::CompareOptions options_;
    State state_ = State::Reading;
    Mode  mode_ = Mode::SideBySide;

    core::text::DocumentIndex::Owner owner_ = 0;
    core::text::DocumentIndex::SubscriptionToken indexSubscription_ = 0;
    int settingsSubscription_ = 0;

    std::shared_ptr<RasterBridge>                 bridge_;
    std::unique_ptr<core::text::ComparisonRun>    run_;
    std::shared_ptr<const core::text::ComparisonResult> result_;
    std::vector<core::text::Difference>           ordered_;
    std::vector<int> olderToNewer_;
    std::vector<int> newerToOlder_;
    int  current_ = -1;
    bool syncing_ = false;
    bool retired_ = false;

    std::vector<std::pair<int, int>> pairs_;
    int    pair_ = -1;
    double overlayScale_ = 0.0;
    QSizeF overlayPageSize_;                                       // the pair's page, logical pixels
    core::render::FitMode overlayFit_ = core::render::FitMode::Page;
    core::render::Zoom    overlayZoom_ = core::render::Zoom::actualSize();
    QRect  overlayRegion_;                                         // asked for, device pixels on the page
    QPoint compositeOrigin_;                                       // where `composite_` is, the same pixels
    int    overscroll_ = 0;                                        // wheel units past an edge, signed
    int    wheelZoom_ = 0;                                         // `Ctrl`+wheel units not yet a step
    std::unique_ptr<LoupeRaster> olderRaster_;
    std::unique_ptr<LoupeRaster> newerRaster_;
    core::render::RenderRequest olderRequest_;
    core::render::RenderRequest newerRequest_;
    QImage composite_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_COMPARISONVIEW_H
