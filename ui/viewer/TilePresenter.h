// Asks the scheduler for tiles, paints what has arrived, and cancels what scrolled away.
// Stage 2 T13.
//
// ---------------------------------------------------------------------------------------------
// THIS IS THE ONLY PLACE IN THE PRODUCT WHERE A `RasterImage` BECOMES A `QImage`.
//
// `core/backend.h` returns width, height, **stride**, pixel format and bytes, deliberately and not
// a `QImage`: `core/` may not name a QtGui type, and the engine boundary carries buffers rather
// than objects. The far side of that boundary is here. Two things about the conversion are not
// optional and each has a failure that looks like a rendering defect rather than an arithmetic one:
//
//   * **`bytesPerLine` is passed to `QImage`, never `width * 4`.** A rasterizer is entitled to
//     align its rows, and a `QImage` constructed over a padded buffer without its stride draws the
//     page sheared into diagonal garbage.
//   * **The bytes are kept alive by the `QByteArray` the image was built over.** `QImage`'s
//     borrowing constructor does not copy, so the buffer must outlive every paint. Each entry here
//     holds both, and the `QImage` is never handed out on its own.
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
    void setDocument(core::DocumentHandle document, core::Revision revision,
                     const core::PageTree* pages);

    core::DocumentHandle document() const noexcept { return document_; }

    // Asks for everything `viewport` needs and cancels everything it no longer does. `viewport` is
    // in CONTENT space -- presented device pixels, the space `PageLayout` produces.
    void requestTiles(const core::render::PageLayout& layout,
                      const core::render::Resolution& resolution,
                      const core::DeviceRect& viewport,
                      core::PageRotation viewRotation = core::PageRotation::None);

    // Paints into `painter`, whose coordinate system is the WIDGET's (logical pixels, origin at the
    // viewport's top-left). `devicePixelRatio` converts, and is the one place the two spaces meet.
    void paint(QPainter& painter, const core::render::PageLayout& layout,
               const core::render::Resolution& resolution, const core::DeviceRect& viewport,
               double devicePixelRatio) const;

    // Drops every tile and cancels every ticket. Called when the document closes and when the
    // resolution moves far enough that nothing held is worth keeping.
    void clear();

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
    struct Held {
        // BOTH, and the order matters: `image` borrows `bytes`, so the buffer is destroyed second.
        QByteArray bytes;
        QImage image;
    };

    void cancelAll();
    core::render::RenderRequest requestFor(int pageIndex, const core::render::TileRect& tile,
                                           const core::render::Resolution& resolution,
                                           core::PageRotation viewRotation) const;

    core::render::RenderScheduler& scheduler_;
    core::render::TileGrid grid_;

    core::DocumentHandle document_;
    core::Revision revision_;
    const core::PageTree* pages_ = nullptr;

    std::unordered_map<core::render::RenderRequest, Held> tiles_;
    std::unordered_map<core::render::RenderRequest, core::render::RenderTicket> tickets_;

    core::render::RenderScheduler::SubscriptionToken subscription_ = 0;
    mutable PaintStats lastPaint_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TILEPRESENTER_H
