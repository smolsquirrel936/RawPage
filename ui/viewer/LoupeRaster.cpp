#include "ui/viewer/LoupeRaster.h"

#include "core/Log.h"

namespace rawpage::ui {

using core::LogSubsystem;
using core::render::RenderPriority;
using core::render::RenderRequest;

LoupeRaster::LoupeRaster(core::render::RenderScheduler& scheduler, QObject* parent,
                         core::render::CachePolicy cachePolicy)
    : QObject(parent), scheduler_(scheduler), cachePolicy_(cachePolicy)
{
    subscription_ = scheduler_.subscribeReady(
        [this](const RenderRequest& request, const core::RasterImage& image) {
            // On the pumping thread (`RenderScheduler::deliverReady`), so no lock. Only the aimed
            // request is taken: a tile for the viewport, the thumbnails or a lens that has since
            // moved on is somebody else's or nobody's.
            if (!aiming_ || !ticket_.isValid() || !(request == aimed_)) return;
            ticket_ = {};

            if (!image.isValid()) {
                RAWPAGE_LOG_WARNING(LogSubsystem::Render)
                    << "loupe: invalid raster for page" << request.pageIndex;
                emit failed();
                return;
            }
            held_ = holdRaster(image);
            emit arrived();
        });
}

LoupeRaster::~LoupeRaster()
{
    // BY TOKEN, and before the members go -- convention 10's threaded form, as `TilePresenter` does.
    scheduler_.unsubscribeReady(subscription_);
    cancelPending();
}

void LoupeRaster::aim(const RenderRequest& request)
{
    if (!request.isValid()) {
        clear();
        return;
    }
    if (aiming_ && request == aimed_) return;   // held or on its way: nothing to do

    cancelPending();
    held_ = HeldRaster{};
    aimed_ = request;
    aiming_ = true;

    const core::render::RenderTicket ticket = scheduler_.enqueue(request, RenderPriority::Visible, cachePolicy_);
    if (ticket.isValid()) {
        ticket_ = ticket;
        ++enqueued_;
    }
}

void LoupeRaster::clear()
{
    cancelPending();
    held_ = HeldRaster{};
    aimed_ = RenderRequest{};
    aiming_ = false;
}

const QImage* LoupeRaster::imageFor(const RenderRequest& request) const
{
    if (!aiming_ || held_.isNull() || !(request == aimed_)) return nullptr;
    return &held_.image;
}

void LoupeRaster::cancelPending()
{
    if (ticket_.isValid()) scheduler_.cancel(ticket_);
    ticket_ = {};
}

} // namespace rawpage::ui
