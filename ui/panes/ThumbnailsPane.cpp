#include "ui/panes/ThumbnailsPane.h"

#include "core/Log.h"
#include "core/doc/Page.h"
#include "core/doc/PageMap.h"
#include "core/doc/PageTree.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/ShortcutBinder.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>

#include <algorithm>
#include <cmath>
#include <utility>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;
using core::render::RenderPriority;
using core::render::RenderRequest;
using core::render::TileRect;

// The column's furniture, in logical pixels. Small enough that a 900-page document's slot vector
// is arithmetic rather than a layout pass, which is what lets `rebuildSlots()` run on every resize.
constexpr int kColumnMargin = 10;
constexpr int kSlotSpacing = 12;
constexpr int kLabelHeight = 16;

// The page sheet, matching `TilePresenter::pageBackground()` -- a thumbnail and the page it stands
// for must not be two different whites.
const QColor kSheet(0xff, 0xff, 0xff);
const QColor kSheetBorder(0x90, 0x90, 0x90);

} // namespace

ThumbnailsPane::ThumbnailsPane(core::render::RenderScheduler& scheduler, QWidget* parent)
    : QAbstractScrollArea(parent), scheduler_(scheduler)
{
    setObjectName(QStringLiteral("thumbnailsPane"));

    // THE CONTEXT DECLARATION, and it is the whole of this pane's keyboard (D1-5). Every §8 Page
    // Thumbnails row resolves through `ShortcutBinder` against this, which is why `Ctrl+R` reaches
    // Stage 3's `page.rotate` with nothing in this file mentioning rotation.
    ShortcutBinder::declareContext(this, core::KeyContext::PageThumbnails);

    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAutoFillBackground(true);

    subscription_ = scheduler_.subscribeReady(
        [this](const RenderRequest& request, const core::RasterImage& image) {
            // On the PUMPING thread -- `RenderScheduler::deliverReady()`'s contract, which its own
            // suite asserts by recording the thread id at every callback. So this touches the map
            // directly, with no lock and no marshalling.
            const auto entry = entries_.find(request.pageIndex);
            if (entry == entries_.end()) return;

            // NOT OURS, OR NOT ANY MORE. Two consumers share this scheduler and a thumbnail
            // re-requested at a new scale or revision is a different request for the same page;
            // comparing the whole request rather than the page index is what keeps a superseded
            // delivery from being painted as the current one.
            if (entry->second.request != request) return;

            entry->second.ticket = {};
            if (!image.isValid()) {
                RAWPAGE_LOG_WARNING(LogSubsystem::Render)
                    << "thumbnail: invalid raster for page" << request.pageIndex;
                return;
            }

            entry->second.thumbnail = holdRaster(image);
            viewport()->update();
            emit thumbnailsArrived();
        });
}

ThumbnailsPane::~ThumbnailsPane()
{
    // BY TOKEN, AND BEFORE THE MAP GOES. A ready callback entered while this destructor unwinds
    // would look up `entries_` after it had been destroyed -- convention 10 in the form where it is
    // a use-after-free rather than a repaint of a dead widget.
    scheduler_.unsubscribeReady(subscription_);
    cancelAll();
}

int ThumbnailsPane::pageCount() const
{
    return pages_ != nullptr ? pages_->count() : 0;
}

void ThumbnailsPane::setDocument(core::DocumentHandle document, core::Revision revision,
                                 const core::PageTree* pages, const core::PageTree* enginePages)
{
    if (document == document_ && revision == revision_ && pages == pages_
        && enginePages == enginePages_) {
        return;
    }

    // EVERYTHING OF THE OLD DOCUMENT GOES. A pane that kept a thumbnail across a tab switch would
    // paint the previous document's page into this one's slot -- the same defect `TilePresenter`
    // guards against in `setDocument`, and it reads as corruption rather than as staleness.
    cancelAll();

    document_ = document;
    revision_ = revision;
    pages_ = pages;
    enginePages_ = enginePages;
    currentPage_ = 0;

    verticalScrollBar()->setValue(0);
    refresh();
}

void ThumbnailsPane::modelChanged(core::Revision revision)
{
    if (revision == revision_) return;
    revision_ = revision;

    // THE SELECTION AND THE SCROLL STAY. A rotation is the document moving under the pane, not a
    // document arriving: yanking the person back to page 1 because they rotated page 40 would be a
    // worse answer than a stale thumbnail.
    //
    // Every held thumbnail is dropped rather than kept, because the revision is part of the
    // request's key (D2-12): a thumbnail rendered before the rotation is not this revision's
    // picture of the page, and keeping it would show the un-rotated page until the new one landed.
    for (auto& [page, entry] : entries_) {
        if (entry.ticket.isValid()) scheduler_.cancel(entry.ticket);
    }
    entries_.clear();

    rebuildSlots();
    requestVisible();
    viewport()->update();
}

void ThumbnailsPane::remapPages(const core::PageMap& map)
{
    // THE SELECTION FOLLOWS ITS PAGE (S7 T3, D7-3); a page the command removed hands it to the page that took
    // its place. The thumbnails themselves are dropped by the `modelChanged` that follows and asked for again,
    // and the cache, which moved what it held, answers them.
    const int to = map.newIndexOf(currentPage_);
    if (to != core::PageMap::kNone) {
        currentPage_ = to;
        return;
    }
    int kept = 0;
    for (int before = 0; before < currentPage_ && before < map.oldCount(); ++before)
        if (map.newIndexOf(before) != core::PageMap::kNone) ++kept;
    currentPage_ = std::clamp(kept, 0, std::max(0, map.newCount() - 1));
}

void ThumbnailsPane::setCurrentPage(int pageIndex)
{
    const int count = pageCount();
    if (count <= 0) {
        currentPage_ = 0;
        return;
    }

    const int clamped = std::clamp(pageIndex, 0, count - 1);
    if (clamped == currentPage_) return;
    currentPage_ = clamped;

    // Scroll it into view, which may change what is requested -- but emit nothing. See the header:
    // this is the programmatic path and a gesture is the only thing that navigates.
    if (clamped < static_cast<int>(slots_.size())) {
        const QRect& rect = slots_[static_cast<std::size_t>(clamped)].rect;
        const int top = verticalScrollBar()->value();
        const int height = viewport()->height();
        if (rect.top() < top) verticalScrollBar()->setValue(rect.top());
        else if (rect.bottom() > top + height)
            verticalScrollBar()->setValue(rect.bottom() - height);
    }

    requestVisible();
    viewport()->update();
}

void ThumbnailsPane::setThumbnailWidth(int width)
{
    const int clamped = std::clamp(width, kMinThumbnailWidth, kMaxThumbnailWidth);
    if (clamped == thumbnailWidth_) return;
    thumbnailWidth_ = clamped;

    // A NEW SCALE IS A NEW REQUEST FOR EVERY PAGE -- `deviceScale` is part of the cache key, so the
    // held thumbnails are not this size's pixels and painting them scaled would be a blurry page
    // that never sharpens. They go, and the visible ones are asked for again.
    for (auto& [page, entry] : entries_) {
        if (entry.ticket.isValid()) scheduler_.cancel(entry.ticket);
    }
    entries_.clear();

    refresh();
}

void ThumbnailsPane::larger()
{
    setThumbnailWidth(thumbnailWidth_ + kThumbnailWidthStep);
}

void ThumbnailsPane::smaller()
{
    setThumbnailWidth(thumbnailWidth_ - kThumbnailWidthStep);
}

void ThumbnailsPane::activateCurrentPage()
{
    if (pageCount() <= 0) return;
    emit activated(currentPage_);
}

std::vector<int> ThumbnailsPane::visiblePages() const
{
    std::vector<int> pages;
    const auto [first, last] = visibleRange();
    if (first < 0) return pages;
    for (int index = first; index <= last; ++index) pages.push_back(index);
    return pages;
}

std::vector<int> ThumbnailsPane::requestedPages() const
{
    std::vector<int> pages;
    pages.reserve(entries_.size());
    for (const auto& [page, entry] : entries_) pages.push_back(page);
    std::sort(pages.begin(), pages.end());
    return pages;
}

std::size_t ThumbnailsPane::pendingTicketCount() const
{
    std::size_t pending = 0;
    for (const auto& [page, entry] : entries_)
        if (entry.ticket.isValid()) ++pending;
    return pending;
}

std::size_t ThumbnailsPane::heldThumbnailCount() const
{
    std::size_t held = 0;
    for (const auto& [page, entry] : entries_)
        if (!entry.thumbnail.isNull()) ++held;
    return held;
}

const RenderRequest* ThumbnailsPane::requestForPage(int pageIndex) const
{
    const auto entry = entries_.find(pageIndex);
    return entry == entries_.end() ? nullptr : &entry->second.request;
}

void ThumbnailsPane::refresh()
{
    rebuildSlots();
    updateScrollBar();
    requestVisible();
    viewport()->update();
}

// =============================================================================================
// Geometry
// =============================================================================================

void ThumbnailsPane::rebuildSlots()
{
    slots_.clear();
    contentHeight_ = 0;

    const int count = pageCount();
    if (count <= 0) return;

    slots_.reserve(static_cast<std::size_t>(count));

    int y = kColumnMargin;
    for (int index = 0; index < count; ++index) {
        // THE MODEL'S DISPLAY SIZE, which already accounts for the page's rotation -- including an
        // unsaved one, because `pages_` is the journal's tree. A thumbnail column of mixed page
        // sizes is what `corpus/ordinary/mixed-page-sizes.pdf` exists to be, so each slot's height
        // is computed from its own page rather than from one nominal aspect ratio.
        const core::PdfSize size = pages_->displaySize(index);
        const double aspect = (size.width > 0.0 && size.height > 0.0) ? size.height / size.width
                                                                      : std::sqrt(2.0);
        const int imageHeight = std::max(1, static_cast<int>(std::lround(thumbnailWidth_ * aspect)));

        Slot slot;
        slot.pageIndex = index;
        slot.imageRect = QRect(kColumnMargin, y, thumbnailWidth_, imageHeight);
        slot.rect = QRect(kColumnMargin, y, thumbnailWidth_, imageHeight + kLabelHeight);
        slots_.push_back(slot);

        y += imageHeight + kLabelHeight + kSlotSpacing;
    }

    contentHeight_ = y - kSlotSpacing + kColumnMargin;
}

void ThumbnailsPane::updateScrollBar()
{
    QScrollBar* bar = verticalScrollBar();
    const int height = viewport()->height();
    bar->setRange(0, std::max(0, contentHeight_ - height));
    bar->setPageStep(std::max(1, height));
    bar->setSingleStep(std::max(1, thumbnailWidth_ / 2));
}

std::pair<int, int> ThumbnailsPane::visibleRange() const
{
    if (slots_.empty() || viewport()->height() <= 0 || viewport()->width() <= 0) return {-1, -1};

    const int top = verticalScrollBar()->value();
    const int bottom = top + viewport()->height();

    int first = -1;
    int last = -1;
    for (const Slot& slot : slots_) {
        if (slot.rect.bottom() < top) continue;
        if (slot.rect.top() >= bottom) break;
        if (first < 0) first = slot.pageIndex;
        last = slot.pageIndex;
    }
    return {first, last};
}

std::pair<int, int> ThumbnailsPane::requestRange() const
{
    const auto [first, last] = visibleRange();
    if (first < 0) return {-1, -1};

    // ONE SCREEN OF MARGIN EACH WAY -- plan §5.4's bound, expressed in the only unit that makes it
    // scroll-rate independent: the number of slots the viewport itself holds. A margin in pages
    // would be a different amount of pre-rendering at every thumbnail size.
    const int screen = last - first + 1;
    const int count = pageCount();
    return {std::max(0, first - screen), std::min(count - 1, last + screen)};
}

// =============================================================================================
// Requests -- D4-10
// =============================================================================================

RenderRequest ThumbnailsPane::requestFor(int pageIndex) const
{
    RenderRequest request;
    request.document = document_;
    request.pageIndex = pageIndex;
    request.revision = revision_;

    const core::PdfSize size = pages_->displaySize(pageIndex);
    if (size.width <= 0.0 || size.height <= 0.0) return request;   // invalid; refused by enqueue

    // DEVICE pixels, so a thumbnail is as sharp on a 150 % panel as on a 100 % one. The request
    // carries the scale and the tile is the whole page at it -- a thumbnail has no tile grid,
    // because the thing that makes tiles worth having is partial visibility and a thumbnail is
    // either on screen or not.
    const double ratio = devicePixelRatioF();
    const int widthPx = std::max(1, static_cast<int>(std::lround(thumbnailWidth_ * ratio)));
    request.deviceScale = widthPx / size.width;
    request.tile = TileRect{0, 0, widthPx,
                            std::max(1, static_cast<int>(std::lround(size.height
                                                                     * request.deviceScale)))};

    // THE UNSAVED ROTATION, as a delta on top of the `/Rotate` the engine already applies (D3-15).
    // Part of the key, so a thumbnail drawn before a rotation is never served after one.
    if (pages_ != nullptr && enginePages_ != nullptr && enginePages_ != pages_) {
        const int model = static_cast<int>(pages_->rotation(pageIndex));
        const int engine = static_cast<int>(enginePages_->rotation(pageIndex));
        request.pageRotationDelta =
            core::rotateBy(core::PageRotation::None, (((model - engine) % 360) + 360) % 360 / 90);
    }

    // FLAGS NONE -- plan §5.4. A thumbnail is a picture of the page, and the viewer's anti-aliasing
    // and transparency-grid preferences are about reading it at size. Sharing the viewer's flags
    // would also split the cache key for no visible gain at 128 pixels wide.
    request.flags = core::render::RenderFlags::None;
    return request;
}

void ThumbnailsPane::requestVisible()
{
    // A PANE NOBODY CAN SEE ASKS FOR NOTHING -- see the header override comments.
    if (!isVisible() || pages_ == nullptr || !document_.isValid()) {
        cancelAll();
        return;
    }

    const auto [first, last] = requestRange();
    if (first < 0) {
        cancelAll();
        return;
    }

    // WHAT SCROLLED AWAY IS CANCELLED, not merely forgotten. `RenderScheduler::cancel` removes a
    // job that has not started so the rasterizer is never entered, which is the only reason a
    // speculative stream this wide is affordable behind one worker.
    std::vector<int> stale;
    for (const auto& [page, entry] : entries_)
        if (page < first || page > last) stale.push_back(page);
    for (const int page : stale) dropEntry(page);

    for (int page = first; page <= last; ++page) {
        const RenderRequest wanted = requestFor(page);
        if (!wanted.isValid()) continue;

        const auto existing = entries_.find(page);
        if (existing != entries_.end()) {
            if (existing->second.request == wanted) continue;   // already asked, at this key
            dropEntry(page);
        }

        Entry entry;
        entry.request = wanted;
        // SPECULATIVE, ALWAYS. `RenderScheduler.h`: *"speculative work that runs instead of visible
        // work is the defect"* -- and this pane is the priority's first product caller.
        entry.ticket = scheduler_.enqueue(wanted, RenderPriority::Speculative);
        entries_.emplace(page, std::move(entry));
    }
}

void ThumbnailsPane::dropEntry(int pageIndex)
{
    const auto entry = entries_.find(pageIndex);
    if (entry == entries_.end()) return;
    if (entry->second.ticket.isValid()) scheduler_.cancel(entry->second.ticket);
    entries_.erase(entry);
}

void ThumbnailsPane::cancelAll()
{
    for (auto& [page, entry] : entries_)
        if (entry.ticket.isValid()) scheduler_.cancel(entry.ticket);
    entries_.clear();
}

// =============================================================================================
// Events
// =============================================================================================

void ThumbnailsPane::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBar();
    requestVisible();
}

void ThumbnailsPane::scrollContentsBy(int dx, int dy)
{
    QAbstractScrollArea::scrollContentsBy(dx, dy);
    requestVisible();
    viewport()->update();
}

void ThumbnailsPane::showEvent(QShowEvent* event)
{
    QAbstractScrollArea::showEvent(event);
    refresh();
}

void ThumbnailsPane::hideEvent(QHideEvent* event)
{
    QAbstractScrollArea::hideEvent(event);
    cancelAll();
}

void ThumbnailsPane::keyPressEvent(QKeyEvent* event)
{
    // THE FOUR ARROWS, and nothing else. See the header override comment: every other row in §8's
    // table is dispatched, and handling one here too would be a second implementation of it.
    const int count = pageCount();
    if (count <= 0) {
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }

    const bool isArrow = event->key() == Qt::Key_Up || event->key() == Qt::Key_Down
                         || event->key() == Qt::Key_Left || event->key() == Qt::Key_Right;

    // A MODIFIED ARROW IS SOMEBODY ELSE'S CHORD, AND THIS BRANCH IS LOAD-BEARING. `Alt+Up` and
    // `Alt+Down` are §8's *move the selected pages earlier / later* -- Stage 7's
    // `page.moveEarlier` and `page.moveLater`, unregistered here, so `ShortcutBinder` answers
    // `NotImplemented` and deliberately leaves the key UNCONSUMED for whatever else would have had
    // it. Two things then try to take it, and BOTH had to be stopped: the selection walk below,
    // which would move the selection on a chord that means reorder; and
    // `QAbstractScrollArea::keyPressEvent`, which scrolls on an arrow and does not look at the
    // modifiers at all. So this ignores the event rather than passing it to the base -- *leave the
    // key alone* means the widget lets go of it too, not only the binder.
    //
    // Both halves were found by the CONTROL case, not by the case (conventions 7 and 22): the
    // first fix looked right, and the control was still red because Qt was eating what the pane had
    // just stopped eating.
    if (isArrow && event->modifiers() != Qt::NoModifier) {
        event->ignore();
        return;
    }

    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Left:
        setCurrentPage(currentPage_ - 1);
        event->accept();
        return;
    case Qt::Key_Down:
    case Qt::Key_Right:
        setCurrentPage(currentPage_ + 1);
        event->accept();
        return;
    default:
        break;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void ThumbnailsPane::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    const QPoint content = event->pos() + QPoint(0, verticalScrollBar()->value());
    for (const Slot& slot : slots_) {
        if (!slot.rect.contains(content)) continue;
        setCurrentPage(slot.pageIndex);
        // A GESTURE. This is the one path besides `page.goToSelected` that emits, and it is why
        // `setCurrentPage` above cannot be allowed to emit as well -- see the header.
        emit activated(slot.pageIndex);
        event->accept();
        return;
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void ThumbnailsPane::paintEvent(QPaintEvent* event)
{
    QPainter painter(viewport());
    painter.fillRect(event->rect(), palette().base());

    if (pageCount() <= 0) {
        painter.setPen(palette().color(QPalette::Disabled, QPalette::WindowText));
        painter.drawText(viewport()->rect().adjusted(kColumnMargin, kColumnMargin, -kColumnMargin,
                                                     -kColumnMargin),
                         Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap,
                         catalog::translate(catalog::key::PanesThumbnailsEmpty));
        return;
    }

    const int top = verticalScrollBar()->value();
    const auto [first, last] = visibleRange();
    if (first < 0) return;

    for (int index = first; index <= last; ++index) {
        const Slot& slot = slots_[static_cast<std::size_t>(index)];
        const QRect imageRect = slot.imageRect.translated(0, -top);
        const QRect slotRect = slot.rect.translated(0, -top);

        if (index == currentPage_) {
            painter.fillRect(slotRect.adjusted(-4, -4, 4, 4), palette().highlight());
        }

        // THE SHEET FIRST, ALWAYS. A thumbnail that has not arrived is drawn as a blank page rather
        // than skipped, for `TilePresenter`'s reason: a column that left gaps where the renderer
        // had not caught up would flicker holes on every scroll and read as a defect.
        painter.fillRect(imageRect, kSheet);

        const auto entry = entries_.find(index);
        if (entry != entries_.end() && !entry->second.thumbnail.isNull()) {
            painter.drawImage(imageRect, entry->second.thumbnail.image);
        }

        painter.setPen(kSheetBorder);
        painter.drawRect(imageRect.adjusted(0, 0, -1, -1));

        // THE LABEL, NOT THE INDEX, when the document has one. ISO 32000-1 12.4.2 lets a file
        // number its front matter i, ii, iii and restart at 1, and a thumbnail column that numbered
        // those 1, 2, 3 would disagree with both the page and the status bar.
        const core::Page* page = pages_->at(index);
        const QString label = (page != nullptr && !page->label().isEmpty())
                                  ? page->label()
                                  : QString::number(index + 1);
        painter.setPen(index == currentPage_ ? palette().color(QPalette::HighlightedText)
                                             : palette().color(QPalette::WindowText));
        painter.drawText(QRect(slotRect.left(), imageRect.bottom(), slotRect.width(), kLabelHeight),
                         Qt::AlignHCenter | Qt::AlignVCenter, label);
    }
}

} // namespace rawpage::ui
