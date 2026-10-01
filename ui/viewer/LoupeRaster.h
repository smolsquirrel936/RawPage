// The Loupe's SHARP lens: one region of one page, rasterized at the magnified scale. Stage 4,
// decision **D4-20** (`Stage4-ViewingSurface-Plan.md` §3), guide cases A-99 and A-100.
//
// ---------------------------------------------------------------------------------------------
// A THIRD CLIENT OF THE ONE SCHEDULER, AND THE SMALLEST.
//
// `TilePresenter` asks for the viewport and `ThumbnailsPane` for whole pages at thumbnail scale;
// this asks for exactly one rectangle -- what the lens shows -- at `presentedScale x
// the lens's magnification` (a rung of `LoupeTool::kMagnifications`, stepped by `Alt`+wheel, D4-22). Same `RenderScheduler`, same `RenderCache`, same single worker behind PDFium's
// engine lock, and no rasterizer, cache or thread of its own (D4-10's rule for the thumbnails pane,
// met a second time). The request itself is built by `TilePresenter::requestAt`, so the revision,
// the flags and the unsaved-rotation delta that make up the cache key are shaped in one place.
//
// ---------------------------------------------------------------------------------------------
// ONE REQUEST AT A TIME, AND THE OLD ONE CANCELLED.
//
// `aim()` with a request other than the one held or pending cancels the pending ticket and drops
// the held image before it enqueues the new one. So sweeping the lens across a page cannot build a
// queue in front of the viewport's own tiles: at most one lens job exists, and a lens that has
// moved on never has its stale pixels painted. The DEBOUNCE -- asking only once the region has been
// still -- is `PageView`'s, because only the view knows when the region changed.
//
// ---------------------------------------------------------------------------------------------
// THE LANE IS `Visible`. The lens is what a person is looking at, and it asks only while the page
// under it is already drawn. D4-20's Revisit is the lane if guide case A-100's measurement ever
// says the viewport pays for it.
//
// ---------------------------------------------------------------------------------------------
// A SECOND INSTANCE SERVES THE SNAPSHOT (S5 T10, D5-11): one region of one page at a scale, the same
// request builder, the same lane -- the shape D5-11 names -- but asked for with `CachePolicy::Bypass`.
// A snapshot is CONSUMED once, onto the clipboard, and up to 200 MB of it inserted into the tile
// budget would evict the page a person is reading for pixels nobody will look at again (D5-4's
// argument for a comparison's rasters, met by a second consumer). A failed raster is DELIVERED as
// `failed()` -- a lens that never sharpens shows its enlarged frame, but a snapshot that silently
// never arrives is a copy the person believes they made.

#ifndef RAWPAGE_UI_VIEWER_LOUPERASTER_H
#define RAWPAGE_UI_VIEWER_LOUPERASTER_H

#include "core/render/RenderRequest.h"
#include "core/render/RenderScheduler.h"
#include "ui/viewer/HeldRaster.h"

#include <QImage>
#include <QObject>

namespace rawpage::ui {

class LoupeRaster : public QObject {
    Q_OBJECT

public:
    explicit LoupeRaster(core::render::RenderScheduler& scheduler, QObject* parent = nullptr,
                         core::render::CachePolicy cachePolicy = core::render::CachePolicy::Use);
    ~LoupeRaster() override;

    LoupeRaster(const LoupeRaster&) = delete;
    LoupeRaster& operator=(const LoupeRaster&) = delete;

    // Wants `request`. The same request as the one held or pending is a no-op; any other cancels
    // what was pending, drops what was held, and enqueues this one in the Visible lane. An invalid
    // request clears instead -- the scheduler would refuse it anyway.
    void aim(const core::render::RenderRequest& request);

    // Wants nothing: cancels the pending ticket and drops the held image. The lens hidden, the
    // document changed, the loupe disarmed.
    void clear();

    // The held raster if -- and only if -- it is the one for `request`. A lens that has moved on is
    // told null and paints its enlarged frame instead: a stale raster painted into a moved lens
    // would magnify the wrong part of the page.
    const QImage* imageFor(const core::render::RenderRequest& request) const;

    // For the cases (A-99): what was last aimed at, whether it is still on its way, and how many
    // requests this lens has ever enqueued -- a sweep that enqueued per move is the defect the
    // debounce exists to prevent, and only a count can say it did not.
    const core::render::RenderRequest& aimed() const noexcept { return aimed_; }
    bool isPending() const noexcept { return ticket_.isValid(); }
    long long enqueuedCount() const noexcept { return enqueued_; }

signals:
    // The aimed raster arrived. `PageView` connects it to a repaint; a signal so this names no widget.
    void arrived();

    // The aimed raster came back invalid. Nothing is held; the request is no longer pending.
    void failed();

private:
    void cancelPending();

    core::render::RenderScheduler&                   scheduler_;
    core::render::CachePolicy                        cachePolicy_ = core::render::CachePolicy::Use;
    core::render::RenderScheduler::SubscriptionToken subscription_ = 0;
    core::render::RenderRequest                      aimed_;
    core::render::RenderTicket                       ticket_;
    bool                                             aiming_ = false;
    HeldRaster                                       held_;
    long long                                        enqueued_ = 0;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_LOUPERASTER_H
