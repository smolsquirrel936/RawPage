// Asks the scheduler for tiles, paints what has arrived, and cancels what scrolled away.
// Stage 2 T13.
//
// ---------------------------------------------------------------------------------------------
// WHERE A `RasterImage` BECOMES A `QImage` -- `ui/viewer/HeldRaster`, SINCE STAGE 4 T5.
//
// `core/backend.h` returns width, height, **stride**, pixel format and bytes, deliberately and not
// a `QImage`: `core/` may not name a QtGui type, and the engine boundary carries buffers rather
// than objects. The far side of that boundary was this file alone from Stage 2 T13 until T5 gave it
// a second consumer -- `ui/panes/ThumbnailsPane` asks the same scheduler for a whole page at
// thumbnail scale -- at which point the conversion moved into `HeldRaster` rather than being
// copied. Its header carries the two rules, both of which fail silently: the stride must be passed
// (a padded buffer read as tight draws the page sheared into diagonal garbage) and the bytes must
// outlive every paint (`QImage`'s borrowing constructor does not copy). This class stores the pair
// it returns and never hands the image out on its own.
//
// ---------------------------------------------------------------------------------------------
// A MISSING TILE IS DRAWN, NOT SKIPPED.
//
// Scrolling a large document always outruns the renderer somewhere, and a viewport that left the
// not-yet-arrived tiles blank would flash white rectangles across the page on every drag -- which
// reads as a rendering bug and is in fact working exactly as designed. So an absent tile is filled
// with the page's own background, and `placeholderCount()` reports how many there were, because
// "the page looks right" and "every tile arrived" are different claims and only the second is
// checkable from a test.
//
// ---------------------------------------------------------------------------------------------
// CANCELLATION IS DRIVEN BY THE TILE SET, NOT BY A GUESS.
//
// `TileGrid` returns three sets -- visible, prefetch and evict -- and `evict` is computed from what
// the caller says it is holding. So this class hands `TileGrid` its live ticket set on every pass
// and cancels exactly what comes back, rather than inventing a heuristic about which tiles are far
// enough away. Plan 5.3 calls cancellation *the feature*; this is the call site that uses it.
//
// ---------------------------------------------------------------------------------------------
// SUBSCRIPTION AND LIFETIME. The scheduler's ready-callback is taken with a TOKEN and dropped in
// the destructor -- convention 10's threaded form, where a subscriber that outlives its object is
// a use-after-free rather than a repaint of a dead widget. The WAKE handler is deliberately NOT
// taken here: `RenderScheduler::setWakeHandler` REPLACES, so two `PageView`s would leave one of
// them silent. `app/CompositionRoot` owns that one and pumps `deliverReady()` -- convention 18's
// rule about `core/Log.h`'s single sink, applied to the second single-owner callback in the
// product.

#ifndef RAWPAGE_UI_VIEWER_TILEPRESENTER_H
#define RAWPAGE_UI_VIEWER_TILEPRESENTER_H

#include "core/Geometry.h"
#include "core/backend.h"
#include "core/doc/Revision.h"
#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "core/render/TileGrid.h"
#include "ui/viewer/HeldRaster.h"

#include <QColor>
#include <QImage>
#include <QObject>

#include <set>
#include <unordered_map>
#include <vector>

class QPainter;

namespace rawpage::core {
class PageTree;
}

namespace rawpage::core::render {
class PageLayout;
struct Resolution;
}

namespace rawpage::ui {

class TilePresenter : public QObject {
    Q_OBJECT

public:
    TilePresenter(core::render::RenderScheduler& scheduler, QObject* parent = nullptr);
    ~TilePresenter() override;

    // The document this presenter draws. Setting a different one drops every tile and every ticket
    // held for the old one -- a presenter that kept them would paint the previous document's pixels
    // into the new one's rectangles, which is a defect that looks like corruption.
    // `enginePages` (Stage 3 T10, D3-15): what the engine loaded, when it differs from `pages`, the model.
    //
    // A NEW REVISION OF THE SAME DOCUMENT KEEPS WHAT IS ON SCREEN UNTIL ITS REPLACEMENT ARRIVES (Stage 6 T5,
    // D6-2's *nothing flashes*). Every held tile becomes STALE rather than being dropped, and is painted --
    // under the fresh ones -- for as long as a request for the SAME tile at the new revision is in flight;
    // when that tile arrives, or is no longer wanted, the stale one goes. Until T5 an edit dropped every tile
    // of the document here, and the page was a white sheet until the cache or the engine refilled it. A
    // stale tile whose twin is never asked for -- a page whose size a rotation changed -- is never painted.
    void setDocument(core::DocumentHandle document, core::Revision revision,
                     const core::PageTree* pages, const core::PageTree* enginePages = nullptr);
    // How many superseded tiles are held while their replacements are on the way (for the cases).
    std::size_t staleTileCount() const noexcept { return stale_.size(); }

    core::DocumentHandle document() const noexcept { return document_; }

    // Asks for everything `viewport` needs and cancels everything it no longer does. `viewport` is
    // in CONTENT space -- presented device pixels, the space `PageLayout` produces.
    // The flags every request carries -- from `pageDisplay/antialias*` (S2 T15). They are part of
    // `RenderRequest`'s key, so changing them does not invalidate anything: the new requests simply
    // miss, and `PageView` drops the now-unreachable tiles explicitly.
    void setRenderFlags(core::render::RenderFlags flags);
    core::render::RenderFlags renderFlags() const noexcept { return flags_; }

    // How many pages BEYOND those the viewport touches are requested speculatively --
    // `performance/prefetchPages`. Counts PAGES; within a page the ring of tiles is `TileGrid`'s
    // own `prefetchRings`, a different knob in a different unit.
    void setPrefetchPages(int pages);
    int prefetchPages() const noexcept { return prefetchPages_; }

    // ---- THE BACKGROUND LANE -- S4 T11, and it is A2's named work ---------------------------
    // A window that is not the one the person is looking at asks for its viewport at
    // `RenderPriority::Prefetch` instead of `RenderPriority::Visible`. Nothing else changes: the
    // same tiles are asked for, they arrive, and the picture is identical -- what moves is which
    // queue they wait in, so a second window can no longer take a worker away from the first.
    //
    // WHY IT IS HERE AND NOT IN THE SCHEDULER. The scheduler cannot know which window is in front,
    // and a rule it inferred would be a policy decision in the layer that is meant to have none.
    // What it CAN do is order the lanes, which it already does; this is the one caller that knows
    // which lane it belongs in. `Tech-Stack-Plan.md` §3.3's *A2 at Stage 4* names this exact
    // change as the condition under which the two-window column fits the frame budget.
    //
    // It is NOT `Speculative`. Speculative is *a page the user has not reached* -- the thumbnails
    // pane -- and a background window's viewport is a page somebody IS looking at, merely not the
    // one in front. Demoting it two lanes would put a visible page behind a nine-hundred-slot pane.
    void setBackground(bool background);
    bool isBackground() const noexcept { return background_; }

    void requestTiles(const core::render::PageLayout& layout,
                      const core::render::Resolution& resolution,
                      const core::DeviceRect& viewport,
                      core::PageRotation viewRotation = core::PageRotation::None);

    // Paints into `painter`, whose coordinate system is the WIDGET's (logical pixels, origin at the
    // viewport's top-left). `devicePixelRatio` converts, and is the one place the two spaces meet.
    // `transparencyGrid` paints a checkerboard under the page instead of a white sheet, so a
    // transparent region reads as transparent rather than as white-on-white. It is a VIEWER
    // decision and not a render flag: the raster is identical either way, and what changes is what
    // is behind it.
    void paint(QPainter& painter, const core::render::PageLayout& layout,
               const core::render::Resolution& resolution, const core::DeviceRect& viewport,
               double devicePixelRatio, bool transparencyGrid = false) const;

    // Drops every tile and cancels every ticket. Called when the document closes and when the
    // resolution moves far enough that nothing held is worth keeping.
    void clear();

    // A request for `tile` of `pageIndex` at an ARBITRARY device scale, shaped exactly as this
    // presenter shapes its own -- the document, the revision, the render flags, the view rotation
    // and the unsaved-rotation delta (D3-15). Public for the Loupe's sharp lens (S4 D4-20), which
    // asks for its region at the magnified scale: a second request builder would be a second place
    // to forget a field of the cache key, which is D2-12's stale-tile bug waiting. Invalid -- and
    // refused by the scheduler -- while no document is set.
    core::render::RenderRequest requestAt(int pageIndex, const core::render::TileRect& tile,
                                          double deviceScale, core::PageRotation viewRotation) const;

    // ---- What a case can assert, rather than a screenshot ------------------------------------
    std::size_t heldTileCount() const noexcept { return tiles_.size(); }
    std::size_t pendingTicketCount() const noexcept { return tickets_.size(); }

    // WHICH PAGES are held, rather than how many tiles. A count is the wrong measure for the
    // eviction claim and the suite found that out: after scrolling from the top of a twelve-page
    // document to the bottom, an implementation that never evicted still holds roughly twice a
    // viewport's worth -- which passes any count bound loose enough not to be brittle. What is
    // actually claimed is that page 0's tiles are GONE, and only a set of pages can say so.
    std::set<int> heldPages() const;

    // THE KEYS THEMSELVES. Added when a negative control refused to fire: a case asserting that
    // rotating the view "re-keyed the tiles" was really asserting that new tiles were requested,
    // which is true anyway because a rotated page has a different shape and therefore different
    // tile rectangles. Only the key can say whether the rotation is IN the key -- and if it is not,
    // the rotated view is served the upright pixels out of the cache with no error and no symptom
    // beyond a command that appears not to work (D2-12).
    std::vector<core::render::RenderRequest> heldRequests() const;

    // Filled in by the most recent `paint()`. `placeholders` is the count of tiles that were asked
    // for and had not arrived -- the difference between "a page is on screen" and "the whole page
    // is on screen", which A-53 needs to be able to state precisely.
    struct PaintStats {
        int pagesDrawn = 0;
        int tilesDrawn = 0;
        int placeholders = 0;
        int staleDrawn = 0;   // S6 T5: superseded tiles painted while their replacements were on the way
    };
    const PaintStats& lastPaint() const noexcept { return lastPaint_; }

    // The colour a page's sheet is painted before any tile lands on it. Exposed so a case can
    // assert that a rendered tile is NOT this colour, which is what distinguishes a drawn page from
    // an empty rectangle the right size.
    static QColor pageBackground();

signals:
    // At least one tile arrived. `PageView` connects it to `update()`; it is a signal rather than a
    // direct call so that the presenter names no widget.
    void tilesArrived();

private:
    void cancelAll();
    core::render::RenderRequest requestFor(int pageIndex, const core::render::TileRect& tile,
                                           const core::render::Resolution& resolution,
                                           core::PageRotation viewRotation) const;

    core::render::RenderScheduler& scheduler_;
    core::render::TileGrid grid_;
    core::render::RenderFlags flags_ = core::render::RenderFlags::Annotations;
    int prefetchPages_ = 1;
    bool background_ = false;

    core::DocumentHandle document_;
    core::Revision revision_;
    const core::PageTree* pages_ = nullptr;
    const core::PageTree* enginePages_ = nullptr;

    std::unordered_map<core::render::RenderRequest, HeldRaster> tiles_;
    // S6 T5: the previous revision's tiles, painted until their replacements arrive (see `setDocument`).
    std::unordered_map<core::render::RenderRequest, HeldRaster> stale_;
    // The request `stale` is replaced by: the same tile at the current revision.
    core::render::RenderRequest twinOf(const core::render::RenderRequest& stale) const;
    std::unordered_map<core::render::RenderRequest, core::render::RenderTicket> tickets_;

    core::render::RenderScheduler::SubscriptionToken subscription_ = 0;
    mutable PaintStats lastPaint_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TILEPRESENTER_H
