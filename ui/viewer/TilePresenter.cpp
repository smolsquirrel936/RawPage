#include "ui/viewer/TilePresenter.h"

#include "core/Log.h"
#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "core/render/DpiPolicy.h"
#include "core/render/PagePresentation.h"

#include <QPainter>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;
using core::render::RenderPriority;
using core::render::RenderRequest;
using core::render::TileRect;

// A tile's rectangle in CONTENT space. The tile is in RASTER pixels within its page, and a capped
// resolution renders fewer pixels and presents them larger -- `Resolution::upscale`. Forgetting the
// factor here is `DpiPolicy`'s named zoom bug arriving at the one place that would show it.
QRectF contentRectFor(const core::render::PageSlot& slot, const TileRect& tile, double upscale)
{
    return QRectF(slot.rect.left() + tile.x * upscale, slot.rect.top() + tile.y * upscale,
                  tile.width * upscale, tile.height * upscale);
}

// The checkerboard `pageDisplay/transparencyGrid` paints under a page. Two greys rather than
// grey-and-white, because a white square is indistinguishable from an opaque white page -- which is
// the one thing this option exists to tell apart.
void paintTransparencyGrid(QPainter& painter, const QRectF& rect)
{
    constexpr int kSquare = 8;
    const QColor light(0xd8, 0xd8, 0xd8);
    const QColor dark(0xb4, 0xb4, 0xb4);

    painter.save();
    painter.setClipRect(rect);
    painter.fillRect(rect, light);

    // Anchored to the RECT rather than to the widget, so the pattern does not crawl across the page
    // as the view scrolls -- a checkerboard that moves under a stationary page reads as a rendering
    // fault rather than as a background.
    const int originX = static_cast<int>(std::floor(rect.left()));
    const int originY = static_cast<int>(std::floor(rect.top()));
    for (int y = 0; y < static_cast<int>(std::ceil(rect.height())) + kSquare; y += kSquare) {
        for (int x = 0; x < static_cast<int>(std::ceil(rect.width())) + kSquare; x += kSquare) {
            if (((x / kSquare) + (y / kSquare)) % 2 == 0) continue;
            painter.fillRect(QRectF(originX + x, originY + y, kSquare, kSquare), dark);
        }
    }
    painter.restore();
}

} // namespace

QColor TilePresenter::pageBackground()
{
    // The sheet. Deliberately not pure white: a page drawn on a white widget with no border is
    // indistinguishable from no page at all, which is the exact confusion guide case A-53 exists to
    // resolve, and the paint below puts a border around it for the same reason.
    return QColor(0xff, 0xff, 0xff);
}

TilePresenter::TilePresenter(core::render::RenderScheduler& scheduler, QObject* parent)
    : QObject(parent), scheduler_(scheduler)
{
    subscription_ = scheduler_.subscribeReady(
        [this](const RenderRequest& request, const core::RasterImage& image) {
            // Delivered on the PUMPING thread -- `RenderScheduler::deliverReady()`'s contract, and
            // its suite asserts that no subscriber is ever entered from a worker. So this touches
            // the maps directly, with no lock and no marshalling.
            const auto ticket = tickets_.find(request);
            if (ticket == tickets_.end()) return;   // another presenter's tile, or one we cancelled
            tickets_.erase(ticket);

            if (!image.isValid()) {
                RAWPAGE_LOG_WARNING(LogSubsystem::Render)
                    << "tile: invalid raster for page" << request.pageIndex;
                return;
            }

            // ONE CONVERSION IN THE PRODUCT, and it is `ui/viewer/HeldRaster` since Stage 4 T5
            // gave the boundary a second consumer. `bytes` keeps the buffer alive and `QImage`
            // borrows it -- a deep copy of the header would not be enough, and a deep copy of the
            // pixels would double the resident bytes of every tile on screen to buy nothing.
            tiles_[request] = holdRaster(image);
            // The superseded tile it replaces is let go (S6 T5): the fresh one now covers it.
            for (auto it = stale_.begin(); it != stale_.end();) {
                it = (twinOf(it->first) == request) ? stale_.erase(it) : std::next(it);
            }
            emit tilesArrived();
        });
}

TilePresenter::~TilePresenter()
{
    // BY TOKEN, and before the maps go. A ready callback entered while this destructor unwinds
    // would touch `tickets_` after it had been destroyed -- convention 10 in the form that is a
    // use-after-free rather than a repaint.
    scheduler_.unsubscribeReady(subscription_);
    cancelAll();
}

void TilePresenter::setDocument(core::DocumentHandle document, core::Revision revision,
                               const core::PageTree* pages, const core::PageTree* enginePages)
{
    if (document == document_ && revision == revision_ && pages == pages_ && enginePages == enginePages_) return;

    if (document == document_ && pages == pages_ && enginePages == enginePages_ && document.isValid()) {
        // THE SAME DOCUMENT AT A NEW REVISION: what is held is kept on screen until replaced (see the header).
        cancelAll();
        for (auto& [request, held] : tiles_) stale_.insert_or_assign(request, std::move(held));
        tiles_.clear();
        revision_ = revision;
        return;
    }

    clear();
    document_ = document;
    revision_ = revision;
    pages_ = pages;
    enginePages_ = enginePages;
}

void TilePresenter::setRenderFlags(core::render::RenderFlags flags)
{
    flags_ = flags;
}

void TilePresenter::setPrefetchPages(int pages)
{
    prefetchPages_ = std::max(0, pages);
}

void TilePresenter::setBackground(bool background)
{
    // NOTHING HELD IS DROPPED AND NOTHING QUEUED IS RE-QUEUED. The lane is a property of the
    // enqueue, not of the tile -- the request is byte-identical either way and so is its cache key
    // (D2-12) -- so a window coming forward takes effect on its next `requestTiles()`, which its
    // activation provokes anyway. Cancelling and re-asking at the higher lane would throw away
    // work already inside the rasterizer to get it back a few milliseconds sooner.
    background_ = background;
}

std::set<int> TilePresenter::heldPages() const
{
    std::set<int> pages;
    for (const auto& [request, held] : tiles_) pages.insert(request.pageIndex);
    return pages;
}

std::vector<RenderRequest> TilePresenter::heldRequests() const
{
    std::vector<RenderRequest> out;
    out.reserve(tiles_.size());
    for (const auto& [request, held] : tiles_) out.push_back(request);
    return out;
}

void TilePresenter::clear()
{
    cancelAll();
    tiles_.clear();
    stale_.clear();
}

RenderRequest TilePresenter::twinOf(const RenderRequest& stale) const
{
    RenderRequest twin = stale;
    twin.revision = revision_;
    return twin;
}

void TilePresenter::cancelAll()
{
    for (const auto& [request, ticket] : tickets_) scheduler_.cancel(ticket);
    tickets_.clear();
}

RenderRequest TilePresenter::requestFor(int pageIndex, const TileRect& tile,
                                        const core::render::Resolution& resolution,
                                        core::PageRotation viewRotation) const
{
    // The RASTER scale, never the presented one. The rasterizer produces `resolution.dpi` and the
    // painter enlarges by `upscale`; passing the presented scale here would ask the engine for the
    // pixels the cap exists to avoid.
    return requestAt(pageIndex, tile, resolution.rasterScale(), viewRotation);
}

RenderRequest TilePresenter::requestAt(int pageIndex, const TileRect& tile, double deviceScale,
                                       core::PageRotation viewRotation) const
{
    RenderRequest request;
    request.document = document_;
    request.pageIndex = pageIndex;
    request.tile = tile;
    request.deviceScale = deviceScale;
    request.revision = revision_;

    // PART OF THE KEY. A rotated tile is different pixels, so a request that omitted this would be
    // served the unrotated tile straight out of the cache -- the rotate command appearing to do
    // nothing, with no error and no failing test (D2-12's argument, on a field T10 did not have).
    request.viewRotation = viewRotation;
    // THE UNSAVED ROTATION, as a delta on top of the `/Rotate` the engine already applies (Stage 3 T10,
    // D3-15). Also part of the key: a tile drawn before a rotation must not be served after it.
    if (pages_ != nullptr && enginePages_ != nullptr && enginePages_ != pages_) {
        const int model = static_cast<int>(pages_->rotation(pageIndex));
        const int engine = static_cast<int>(enginePages_->rotation(pageIndex));
        request.pageRotationDelta = core::rotateBy(core::PageRotation::None, (((model - engine) % 360) + 360) % 360 / 90);
    }
    request.flags = flags_;
    return request;
}

void TilePresenter::requestTiles(const core::render::PageLayout& layout,
                                 const core::render::Resolution& resolution,
                                 const core::DeviceRect& viewport,
                                 core::PageRotation viewRotation)
{
    if (!document_.isValid() || pages_ == nullptr) return;

    const double upscale = resolution.upscale > 0.0 ? resolution.upscale : 1.0;

    // Everything still wanted after this pass. What is NOT in it is cancelled and dropped, which is
    // how the eviction set becomes an action rather than a report.
    std::unordered_map<RenderRequest, char> wanted;

    // THE VISIBLE PAGES, PLUS `performance/prefetchPages` ON EACH SIDE. The extra pages are asked
    // for at `Prefetch` priority, which starves under scrolling and is supposed to: speculative work
    // that runs instead of visible work is the defect, not the starvation (`RenderScheduler`).
    //
    // Extending the VIEWPORT rectangle instead would have been one line and wrong: `prefetchPages`
    // counts pages, and a viewport grown by a page's height covers a different number of pages in a
    // document of mixed page sizes -- which `corpus/ordinary/mixed-page-sizes.pdf` exists to be.
    std::vector<core::render::PageSlot> wantedSlots = layout.slotsIntersecting(viewport);
    if (prefetchPages_ > 0 && !wantedSlots.empty()) {
        const int firstVisible = wantedSlots.front().pageIndex;
        const int lastVisible = wantedSlots.back().pageIndex;
        for (int step = 1; step <= prefetchPages_; ++step) {
            if (const core::render::PageSlot* before = layout.slotForPage(firstVisible - step))
                wantedSlots.push_back(*before);
            if (const core::render::PageSlot* after = layout.slotForPage(lastVisible + step))
                wantedSlots.push_back(*after);
        }
    }

    for (const core::render::PageSlot& slot : wantedSlots) {
        const core::Page* page = pages_->at(slot.pageIndex);
        if (page == nullptr) continue;

        // A PREFETCHED PAGE IS NOT IN THE VIEWPORT AT ALL, so clipping the viewport to it yields an
        // empty rectangle and the loop below would skip it. What it wants instead is the page's own
        // bounds -- everything of it, at prefetch priority.
        const bool offscreen = slot.rect.bottom() <= viewport.top()
                               || slot.rect.top() >= viewport.bottom();

        // The viewport, clipped to this page and moved into the page's own RASTER space -- or the
        // whole page, for one that is being prefetched from outside the viewport entirely.
        const double left = offscreen ? 0.0
                                      : std::max(viewport.left(), slot.rect.left()) - slot.rect.left();
        const double top = offscreen ? 0.0
                                     : std::max(viewport.top(), slot.rect.top()) - slot.rect.top();
        const double right = offscreen
                                 ? slot.rect.size.width
                                 : std::min(viewport.right(), slot.rect.right()) - slot.rect.left();
        const double bottom = offscreen
                                  ? slot.rect.size.height
                                  : std::min(viewport.bottom(), slot.rect.bottom()) - slot.rect.top();
        if (right <= left || bottom <= top) continue;

        const TileRect pageViewport{
            static_cast<int>(std::floor(left / upscale)),
            static_cast<int>(std::floor(top / upscale)),
            static_cast<int>(std::ceil((right - left) / upscale)),
            static_cast<int>(std::ceil((bottom - top) / upscale)),
        };

        // What we hold or have asked for on this page, so `TileGrid` can compute the eviction set
        // rather than this class guessing at one.
        std::vector<TileRect> held;
        for (const auto& [request, entry] : tiles_)
            if (request.pageIndex == slot.pageIndex) held.push_back(request.tile);
        for (const auto& [request, ticket] : tickets_)
            if (request.pageIndex == slot.pageIndex) held.push_back(request.tile);

        const core::render::TileSet tiles =
            grid_.tilesFor(*page, resolution, pageViewport, held, viewRotation);

        auto ask = [&](const TileRect& tile, RenderPriority priority) {
            const RenderRequest request = requestFor(slot.pageIndex, tile, resolution, viewRotation);
            wanted[request] = 1;
            if (tiles_.count(request) != 0 || tickets_.count(request) != 0) return;

            const core::render::RenderTicket ticket = scheduler_.enqueue(request, priority);
            if (ticket.isValid()) tickets_[request] = ticket;
        };

        // S4 T11 -- A2's named work. A window that is not in front asks at `Prefetch`, so the
        // window the person IS looking at keeps the single worker (I-05's engine lock) to itself.
        const RenderPriority priority = (offscreen || background_) ? RenderPriority::Prefetch
                                                                   : RenderPriority::Visible;
        for (const TileRect& tile : tiles.visible) ask(tile, priority);
        for (const TileRect& tile : tiles.prefetch) ask(tile, RenderPriority::Prefetch);
    }

    // CANCEL, then drop. A ticket left behind would keep a job the user has scrolled past ahead of
    // one they are looking at -- plan 5.3's whole argument for cancellation being the feature.
    for (auto it = tickets_.begin(); it != tickets_.end();) {
        if (wanted.count(it->first) != 0) {
            ++it;
            continue;
        }
        scheduler_.cancel(it->second);
        it = tickets_.erase(it);
    }

    for (auto it = tiles_.begin(); it != tiles_.end();) {
        it = (wanted.count(it->first) != 0) ? std::next(it) : tiles_.erase(it);
    }
    // A stale tile stays only while its twin is wanted AND still on its way: one served at once -- from the
    // cache, re-keyed by the scheduler -- or one nobody asks for any more has nothing left to stand in for.
    for (auto it = stale_.begin(); it != stale_.end();) {
        const RenderRequest twin = twinOf(it->first);
        it = (wanted.count(twin) != 0 && tickets_.count(twin) != 0) ? std::next(it) : stale_.erase(it);
    }
}

void TilePresenter::paint(QPainter& painter, const core::render::PageLayout& layout,
                          const core::render::Resolution& resolution,
                          const core::DeviceRect& viewport, double devicePixelRatio,
                          bool transparencyGrid) const
{
    lastPaint_ = PaintStats{};

    const double dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    const double upscale = resolution.upscale > 0.0 ? resolution.upscale : 1.0;

    // Content space to WIDGET space: subtract the scroll, then divide by the device-pixel ratio.
    // This is the only conversion between the two in the viewer, and it is a lambda rather than
    // four expressions so that a change to it cannot be made in three of the four places.
    auto toWidget = [&](const QRectF& content) {
        return QRectF((content.x() - viewport.left()) / dpr, (content.y() - viewport.top()) / dpr,
                      content.width() / dpr, content.height() / dpr);
    };

    for (const core::render::PageSlot& slot : layout.slotsIntersecting(viewport)) {
        const QRectF sheet(slot.rect.left(), slot.rect.top(), slot.rect.size.width,
                           slot.rect.size.height);
        const QRectF sheetWidget = toWidget(sheet);

        // THE SHEET FIRST, then the tiles on top. A page whose tiles have not arrived is still a
        // page-shaped white rectangle with a border rather than a hole -- see the header on why a
        // missing tile is drawn and not skipped.
        if (transparencyGrid) paintTransparencyGrid(painter, sheetWidget);
        else                  painter.fillRect(sheetWidget, pageBackground());
        painter.setPen(QColor(0x80, 0x80, 0x80));
        painter.drawRect(sheetWidget.adjusted(-0.5, -0.5, 0.5, 0.5));
        ++lastPaint_.pagesDrawn;

        // THE SUPERSEDED TILES FIRST (S6 T5), so a fresh one that has arrived paints over them: what was on
        // screen stays there until what replaces it is ready, instead of a white sheet in between.
        for (const auto& [request, held] : stale_) {
            if (request.pageIndex != slot.pageIndex || request.deviceScale != resolution.rasterScale()) continue;
            if (tickets_.count(twinOf(request)) == 0) continue;
            const QRectF content = contentRectFor(slot, request.tile, upscale);
            if (!content.intersects(QRectF(viewport.left(), viewport.top(), viewport.size.width, viewport.size.height)))
                continue;
            painter.drawImage(toWidget(content), held.image);
            ++lastPaint_.staleDrawn;
        }

        for (const auto& [request, held] : tiles_) {
            if (request.pageIndex != slot.pageIndex) continue;
            if (request.deviceScale != resolution.rasterScale()) continue;

            const QRectF content = contentRectFor(slot, request.tile, upscale);
            if (!content.intersects(QRectF(viewport.left(), viewport.top(), viewport.size.width,
                                           viewport.size.height))) {
                continue;
            }

            painter.drawImage(toWidget(content), held.image);
            ++lastPaint_.tilesDrawn;
        }

        for (const auto& [request, ticket] : tickets_) {
            if (request.pageIndex == slot.pageIndex) ++lastPaint_.placeholders;
        }
    }
}

} // namespace rawpage::ui
