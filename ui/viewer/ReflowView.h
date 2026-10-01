// Reflow view -- `Features.md` §8's Viewer `Ctrl+4`, Stage 5 T11, decision **D5-16**
// (`Stage5-TextSearch-Plan.md` §3), guide cases A-92 … A-94.
//
// ===========================================================================================
// A READ-ONLY TEXT SURFACE OF THE ANALYZER'S BLOCKS, AND NOTHING THE PAGE DRAWS.
// ===========================================================================================
// Each block of each page is one paragraph (`core/text/ReflowText`), re-wrapped to the window by Qt's
// text layout and drawn in Qt's fonts at the model's size, weight and italic -- the page's typography is
// exactly what reflow gives up. **Text only**: no image, no vector art, no annotation. **It asks the
// engine for nothing but text**: no render request is made while it shows (A-94), and its pages come from
// `core/text/DocumentIndex` as the selection's do -- at `Visible`, because a person is waiting to read them.
//
// ===========================================================================================
// A WINDOW OF PAGES, GROWN AS THE READER SCROLLS.
// ===========================================================================================
// A 900-page document reflowed whole is seven million characters laid out before the first line shows. So
// the reflow holds a WINDOW of pages around the reader's place -- `kInitialBefore` before, `kInitialAfter`
// after -- and grows it by `kGrowBy` pages when the scroll comes within a screen of either end. Pages
// arrive from the engine thread in any order; the document is rebuilt from what has arrived, coalesced on
// a short timer, and the reader's place is kept across every rebuild.
//
// ===========================================================================================
// THE READER'S PLACE IS A BLOCK, BOTH WAYS.
// ===========================================================================================
// `Place` is (page, block). Entering, `showAt(page, topY)` is told the page and the page-space height at
// the page view's top edge, and resolves the block there with `blockAtTop` once that page's text has
// arrived. Leaving, `place()` is the paragraph at the top of this view, and the shell puts that block's
// top at the top of the page view (`PageView::putPageHeightAtTop`). Each paragraph carries its place as
// block user data, so neither direction searches text.
//
// ===========================================================================================
// THE KEYS IT KEEPS, AND THE ONES IT LEAVES.
// ===========================================================================================
// It declares the `Viewer` context (`ShortcutBinder`), so `Ctrl+4` comes back here and the zoom chords
// reach it -- **`Ctrl+=`, `Ctrl+-` and `Ctrl+0` change the TEXT size** (`zoomIn`, `zoomOut`,
// `resetZoom`), through `MainWindow`'s zoom handlers asking the document window which surface is shown.
// The keys a reader scrolls with -- the arrows, `Page Up`/`Page Down`, `Home`/`End`, `Space` -- are
// CLAIMED through Qt's `ShortcutOverride` handshake, which `ShortcutBinder` offers the focused widget
// first: left to the Viewer table they would page the hidden page view. `Ctrl+C` and `Ctrl+A` are the
// text browser's own, as every read-only text surface's are (S5 guide I-04). **Find does not run in it**
// (D5-16): activating a hit takes the document window back to the page.

#ifndef RAWPAGE_UI_VIEWER_REFLOWVIEW_H
#define RAWPAGE_UI_VIEWER_REFLOWVIEW_H

#include "core/text/DocumentIndex.h"
#include "core/text/TextSelection.h"

#include <QTextBrowser>

#include <map>
#include <memory>
#include <optional>
#include <vector>

class QTimer;

namespace rawpage::ui {

class ReflowView : public QTextBrowser {
    Q_OBJECT

public:
    struct Place {
        int page = -1;
        int block = -1;

        bool isValid() const noexcept { return page >= 0 && block >= 0; }
        friend bool operator==(const Place& a, const Place& b) noexcept
        {
            return a.page == b.page && a.block == b.block;
        }
    };

    static constexpr int kInitialBefore = 2;
    static constexpr int kInitialAfter = 6;
    static constexpr int kGrowBy = 6;
    // The window grows BY ITSELF -- after a rebuild, when less than a screen of text follows the reader's
    // place -- only up to this many pages: pages with little or no text (a scan, a page of figures) would
    // otherwise have the reflow extract the whole document at `Visible` to fill one screen. A reader's own
    // scroll to an edge grows it without this bound.
    static constexpr int kMaxAutomaticPages = 48;

    // The text size steps: each chord multiplies or divides by `kZoomStep`, inside the bounds.
    static constexpr double kZoomStep = 1.25;
    static constexpr double kMinScale = 0.5;
    static constexpr double kMaxScale = 4.0;

    explicit ReflowView(core::text::DocumentIndex& index, QWidget* parent = nullptr);
    ~ReflowView() override;

    // The document this reflows: its index entry and its page count. A different entry forgets everything.
    // Not `setDocument`, which is `QTextEdit`'s and takes the text document this class builds.
    void attach(core::text::DocumentIndex::EntryId entry, int pageCount);

    // Shows the reflow at the block at the top of a page view: `page`, and the page-space height at that
    // view's top edge. The block is resolved once the page's text is here.
    void showAt(int page, double topY);
    // Shows the reflow at a known block (a case; the session).
    void showAtPlace(Place place);

    // The paragraph at the top of this view, or the place it is still on its way to.
    Place place() const;

    void zoomIn();
    void zoomOut();
    void resetZoom();
    double textScale() const noexcept { return scale_; }

    // ---- For the cases ------------------------------------------------------------------------------------
    int firstPage() const noexcept { return first_; }
    int lastPage() const noexcept { return last_; }
    // Every page of the window has arrived and is shown, and the reader's place is applied.
    bool isSettled() const;
    // How many text requests this reflow has made of the index.
    long long requestsMade() const noexcept { return requests_; }
    // The point size a paragraph of `modelSize` points is drawn at.
    double drawnSize(double modelSize) const;
    // Every paragraph shown, in order, with its place -- what a case compares with the generator's blocks.
    struct Shown {
        Place   place;
        QString text;
    };
    std::vector<Shown> shown() const;

protected:
    bool event(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void onIndexEvent(const core::text::DocumentIndex::Event& event);
    void ensureWindow(int first, int last);
    void request(int page);
    void scheduleRebuild();
    void rebuild();
    void applyPendingPlace();
    void maybeGrow(bool byReader);
    bool resolvePendingTop();

    core::text::DocumentIndex& index_;
    core::text::DocumentIndex::Owner owner_ = 0;
    core::text::DocumentIndex::SubscriptionToken subscription_ = 0;
    core::text::DocumentIndex::EntryId entry_ = 0;
    int pageCount_ = 0;

    int first_ = 0;
    int last_ = -1;
    std::map<int, std::shared_ptr<const core::text::PageReading>> readings_;

    // Where the reader is to be put: a block, or -- on the way in -- a page and a height still waiting for
    // that page's text. Cleared once applied; a scroll by the reader never fights one.
    std::optional<Place> pendingPlace_;
    struct PendingTop {
        int page = -1;
        double topY = 0.0;
    };
    std::optional<PendingTop> pendingTop_;

    double scale_ = 1.0;
    QTimer* rebuildTimer_ = nullptr;
    bool rebuilding_ = false;
    bool readerScrolled_ = false;   // the scroll bar's last move was the reader's own action
    long long requests_ = 0;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_REFLOWVIEW_H
