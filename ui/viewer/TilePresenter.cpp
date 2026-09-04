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

QImage::Format formatFor(core::PixelFormat format) noexcept
{
    // Both are 32 bits per pixel in BGRA byte order on a little-endian machine, which is what
    // `core/backend.h` says and what makes this a wrap rather than a conversion. The alpha byte is
    // the only difference, and getting it backwards produces a page that is either transparent or
    // opaque where it should be the other -- visible only over a non-white background.
    return format == core::PixelFormat::Bgra8888 ? QImage::Format_ARGB32 : QImage::Format_RGB32;
}

// A tile's rectangle in CONTENT space. The tile is in RASTER pixels within its page, and a capped
// resolution renders fewer pixels and presents them larger -- `Resolution::upscale`. Forgetting the
// factor here is `DpiPolicy`'s named zoom bug arriving at the one place that would show it.
QRectF contentRectFor(const core::render::PageSlot& slot, const TileRect& tile, double upscale)
{
    return QRectF(slot.rect.left() + tile.x * upscale, slot.rect.top() + tile.y * upscale,
                  tile.width * upscale, tile.height * upscale);
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

            Held held;
            held.bytes = image.pixels;
            held.image = QImage(reinterpret_cast<const uchar*>(held.bytes.constData()), image.width,
                                image.height, image.bytesPerLine, formatFor(image.format));

            // A DEEP COPY of the QImage's header is not enough and a deep copy of the pixels is not
            // wanted: `held.bytes` keeps the buffer alive and `QImage` borrows it, which is the
            // arrangement `core/backend.h` designed the boundary for.
            tiles_[request] = std::move(held);
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
                               const core::PageTree* pages)
{
    if (document == document_ && revision == revision_ && pages == pages_) return;

    clear();
    document_ = document;
    revision_ = revision;
    pages_ = pages;
}

std::set<int> TilePresenter::heldPages() const
{
    std::set<int> pages;
    for (const auto& [request, held] : tiles_) pages.insert(request.pageIndex);
    return pages;
}

void TilePresenter::clear()
{
    cancelAll();
    tiles_.clear();
}

void TilePresenter::cancelAll()
{
    for (const auto& [request, ticket] : tickets_) scheduler_.cancel(ticket);
    tickets_.clear();
}

RenderRequest TilePresenter::requestFor(int pageIndex, const TileRect& tile,
                                        const core::render::Resolution& resolution) const
{
    RenderRequest request;
    request.document = document_;
    request.pageIndex = pageIndex;
    request.tile = tile;

    // The RASTER scale, never the presented one. The rasterizer produces `resolution.dpi` and the
    // painter enlarges by `upscale`; passing the presented scale here would ask the engine for the
    // pixels the cap exists to avoid.
    request.deviceScale = resolution.rasterScale();
    request.revision = revision_;
    return request;
}

void TilePresenter::requestTiles(const core::render::PageLayout& layout,
                                 const core::render::Resolution& resolution,
                                 const core::DeviceRect& viewport)
{
    if (!document_.isValid() || pages_ == nullptr) return;

    const double upscale = resolution.upscale > 0.0 ? resolution.upscale : 1.0;

    // Everything still wanted after this pass. What is NOT in it is cancelled and dropped, which is
    // how the eviction set becomes an action rather than a report.
    std::unordered_map<RenderRequest, char> wanted;

    for (const core::render::PageSlot& slot : layout.slotsIntersecting(viewport)) {
        const core::Page* page = pages_->at(slot.pageIndex);
        if (page == nullptr) continue;

        // The viewport, clipped to this page and moved into the page's own RASTER space.
        const double left = std::max(viewport.left(), slot.rect.left()) - slot.rect.left();
        const double top = std::max(viewport.top(), slot.rect.top()) - slot.rect.top();
        const double right = std::min(viewport.right(), slot.rect.right()) - slot.rect.left();
        const double bottom = std::min(viewport.bottom(), slot.rect.bottom()) - slot.rect.top();
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
            grid_.tilesFor(*page, resolution, pageViewport, held);

        auto ask = [&](const TileRect& tile, RenderPriority priority) {
            const RenderRequest request = requestFor(slot.pageIndex, tile, resolution);
            wanted[request] = 1;
            if (tiles_.count(request) != 0 || tickets_.count(request) != 0) return;

            const core::render::RenderTicket ticket = scheduler_.enqueue(request, priority);
            if (ticket.isValid()) tickets_[request] = ticket;
        };

        for (const TileRect& tile : tiles.visible) ask(tile, RenderPriority::Visible);
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
}

void TilePresenter::paint(QPainter& painter, const core::render::PageLayout& layout,
                          const core::render::Resolution& resolution,
                          const core::DeviceRect& viewport, double devicePixelRatio) const
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
        painter.fillRect(sheetWidget, pageBackground());
        painter.setPen(QColor(0x80, 0x80, 0x80));
        painter.drawRect(sheetWidget.adjusted(-0.5, -0.5, 0.5, 0.5));
        ++lastPaint_.pagesDrawn;

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
