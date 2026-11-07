#include "ui/panes/ThumbnailsPane.h"

#include "core/Log.h"
#include "core/doc/Page.h"
#include "core/doc/PageMap.h"
#include "core/doc/PageTree.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/ShortcutBinder.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHelpEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QToolTip>

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
    setAcceptDrops(true);   // S7 T8: pages dragged here (D7-13); `QAbstractScrollArea` hands the viewport's drag events on

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
    anchor_ = 0;
    press_.reset();
    dropIndex_ = -1;
    badges_.clear();
    select(pageCount() > 0 ? std::vector<int>{0} : std::vector<int>{});

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

    // A COUNT THAT CHANGED is a remap's to describe (`remapPages`, called first); what is left past the end is
    // dropped here, so no path leaves the set naming a page the document no longer has.
    const int count = pageCount();
    currentPage_ = std::clamp(currentPage_, 0, std::max(0, count - 1));
    anchor_ = std::clamp(anchor_, 0, std::max(0, count - 1));
    std::vector<int> kept;
    for (const int page : selected_)
        if (page < count) kept.push_back(page);
    if (kept.empty() && count > 0) kept.push_back(currentPage_);
    select(std::move(kept));
    badges_.resize(std::min(badges_.size(), static_cast<std::size_t>(std::max(0, count))));

    rebuildSlots();
    updateScrollBar();
    requestVisible();
    viewport()->update();
}

void ThumbnailsPane::remapPages(const core::PageMap& map)
{
    // THE SELECTION FOLLOWS ITS PAGE (S7 T3, D7-3); a page the command removed hands it to the page that took
    // its place. The thumbnails themselves are dropped by the `modelChanged` that follows and asked for again,
    // and the cache, which moved what it held, answers them.
    const auto follow = [&map](int page) {
        const int to = map.newIndexOf(page);
        if (to != core::PageMap::kNone) return to;
        int kept = 0;
        for (int before = 0; before < page && before < map.oldCount(); ++before)
            if (map.newIndexOf(before) != core::PageMap::kNone) ++kept;
        return std::clamp(kept, 0, std::max(0, map.newCount() - 1));
    };
    currentPage_ = follow(currentPage_);
    anchor_ = follow(anchor_);

    // THE SET IS THE SAME PAGES (S7 T8, guide A-53): each where it went, a deleted one gone. A set the command emptied
    // becomes the page that took the current page's place, so a command always has something to act on next.
    std::vector<int> moved;
    for (const int page : selected_) {
        const int to = map.newIndexOf(page);
        if (to != core::PageMap::kNone) moved.push_back(to);
    }
    if (moved.empty() && map.newCount() > 0) moved.push_back(currentPage_);
    std::sort(moved.begin(), moved.end());
    // The model already counts the new pages, and `select` drops what it does not hold -- so the map's count is
    // checked here rather than the pane's tree, which a caller may not have re-pointed yet.
    std::vector<int> inside;
    for (const int page : moved)
        if (page < map.newCount()) inside.push_back(page);
    if (inside != selected_) {
        selected_ = std::move(inside);
        viewport()->update();
        emit selectionChanged();
    }
}

void ThumbnailsPane::setCurrentPage(int pageIndex)
{
    const int count = pageCount();
    if (count <= 0) {
        currentPage_ = 0;
        return;
    }

    const int clamped = std::clamp(pageIndex, 0, count - 1);
    // A SELECTION OF ONE PAGE FOLLOWS THE VIEW; a set of several stays (see the header).
    const bool single = selected_.size() <= 1;
    if (clamped == currentPage_ && (!single || (selected_.size() == 1 && selected_.front() == clamped))) return;
    currentPage_ = clamped;
    if (single) {
        anchor_ = clamped;
        select({clamped});
    }

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

// =============================================================================================
// The set -- Stage 7 T8 (D7-13)
// =============================================================================================

std::vector<int> ThumbnailsPane::commandPages() const
{
    if (!selected_.empty()) return selected_;
    if (pageCount() > 0) return {currentPage_};
    return {};
}

void ThumbnailsPane::select(std::vector<int> pages)
{
    const int count = pageCount();
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    pages.erase(std::remove_if(pages.begin(), pages.end(), [count](int page) { return page < 0 || page >= count; }),
                pages.end());
    if (pages == selected_) return;
    selected_ = std::move(pages);
    viewport()->update();
    emit selectionChanged();
}

void ThumbnailsPane::setSelectedPages(std::vector<int> pages)
{
    select(std::move(pages));
    if (selected_.empty()) return;
    // The current page moves INTO the set, so the keyboard's place is one of the pages a command will act on.
    if (!std::binary_search(selected_.begin(), selected_.end(), currentPage_)) {
        currentPage_ = selected_.front();
        anchor_ = currentPage_;
    }
    viewport()->update();
}

void ThumbnailsPane::selectAll()
{
    std::vector<int> every(static_cast<std::size_t>(std::max(0, pageCount())));
    for (std::size_t page = 0; page < every.size(); ++page) every[page] = static_cast<int>(page);
    select(std::move(every));
}

void ThumbnailsPane::pressPage(int page, Qt::KeyboardModifiers modifiers)
{
    const bool control = modifiers.testFlag(Qt::ControlModifier);
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        // A RUN FROM THE ANCHOR, which stays where it was: a second `Shift`+click re-draws the run from the same
        // place, as a file manager does.
        std::vector<int> run;
        for (int at = std::min(anchor_, page); at <= std::max(anchor_, page); ++at) run.push_back(at);
        if (control) run.insert(run.end(), selected_.begin(), selected_.end());
        select(std::move(run));
    } else if (control) {
        std::vector<int> toggled = selected_;
        const auto found = std::lower_bound(toggled.begin(), toggled.end(), page);
        if (found != toggled.end() && *found == page) toggled.erase(found);
        else toggled.insert(found, page);
        select(std::move(toggled));
        anchor_ = page;
    } else {
        select({page});
        anchor_ = page;
    }
    currentPage_ = page;
    viewport()->update();
}

void ThumbnailsPane::setBadges(std::vector<core::AnnotationStore::PageCarries> badges)
{
    badges_ = std::move(badges);
    viewport()->update();
}

core::AnnotationStore::PageCarries ThumbnailsPane::badgesOf(int pageIndex) const
{
    if (pageIndex < 0 || pageIndex >= static_cast<int>(badges_.size())) return {};
    return badges_[static_cast<std::size_t>(pageIndex)];
}

namespace {

// The badges, left to right along the sheet's bottom edge: comments, fields, links -- each a square of this side.
constexpr int kBadge = 14;
constexpr int kBadgeInset = 3;

std::vector<int> badgeKinds(const core::AnnotationStore::PageCarries& carries)
{
    std::vector<int> kinds;
    if (carries.comments) kinds.push_back(0);
    if (carries.fields) kinds.push_back(1);
    if (carries.links) kinds.push_back(2);
    return kinds;
}

} // namespace

QRect ThumbnailsPane::badgeRect(int pageIndex) const
{
    const std::size_t kinds = badgeKinds(badgesOf(pageIndex)).size();
    if (kinds == 0 || pageIndex < 0 || pageIndex >= static_cast<int>(slots_.size())) return {};
    const QRect image = slots_[static_cast<std::size_t>(pageIndex)].imageRect.translated(0, -verticalScrollBar()->value());
    const int width = static_cast<int>(kinds) * kBadge + static_cast<int>(kinds - 1) * 2;
    return QRect(image.left() + kBadgeInset, image.bottom() - kBadgeInset - kBadge + 1, width, kBadge);
}

void ThumbnailsPane::paintBadges(QPainter& painter, int pageIndex, const QRect& imageRect) const
{
    const std::vector<int> kinds = badgeKinds(badgesOf(pageIndex));
    if (kinds.empty()) return;
    // PALETTE COLOURS, so a badge reads in both themes (convention 20): the window's own background and text,
    // which every theme keeps apart, on a sheet that is always white.
    const QColor fill = palette().color(QPalette::Window);
    const QColor ink = palette().color(QPalette::WindowText);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    int x = imageRect.left() + kBadgeInset;
    const int y = imageRect.bottom() - kBadgeInset - kBadge + 1;
    for (const int kind : kinds) {
        const QRectF box(x, y, kBadge, kBadge);
        painter.setPen(QPen(ink, 1.0));
        painter.setBrush(fill);
        painter.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 3.0, 3.0);
        painter.setBrush(Qt::NoBrush);
        const QRectF in = box.adjusted(3.5, 3.5, -3.5, -3.5);
        if (kind == 0) {
            // A comment: a speech bubble with its tail.
            QPainterPath bubble;
            bubble.addRoundedRect(QRectF(in.left(), in.top(), in.width(), in.height() * 0.7), 1.5, 1.5);
            bubble.moveTo(in.left() + in.width() * 0.3, in.top() + in.height() * 0.7);
            bubble.lineTo(in.left() + in.width() * 0.2, in.bottom());
            bubble.lineTo(in.left() + in.width() * 0.55, in.top() + in.height() * 0.7);
            painter.drawPath(bubble);
        } else if (kind == 1) {
            // A form field: a box with a text cursor in it.
            painter.drawRect(in);
            painter.drawLine(QPointF(in.left() + 2.0, in.top() + 1.5), QPointF(in.left() + 2.0, in.bottom() - 1.5));
        } else {
            // A link: two links of a chain.
            painter.drawRoundedRect(QRectF(in.left(), in.center().y() - 1.75, in.width() * 0.6, 3.5), 1.75, 1.75);
            painter.drawRoundedRect(QRectF(in.left() + in.width() * 0.4, in.center().y() - 1.75, in.width() * 0.6, 3.5), 1.75, 1.75);
        }
        x += kBadge + 2;
    }
    painter.restore();
}

// =============================================================================================
// Dragging -- Stage 7 T8 (D7-13)
// =============================================================================================

int ThumbnailsPane::slotAt(QPoint viewportPos) const
{
    const QPoint content = viewportPos + QPoint(0, verticalScrollBar()->value());
    for (const Slot& slot : slots_)
        if (slot.rect.contains(content)) return slot.pageIndex;
    return -1;
}

int ThumbnailsPane::insertionIndexAt(QPoint viewportPos) const
{
    if (!document_.isValid() || slots_.empty()) return -1;
    const int y = viewportPos.y() + verticalScrollBar()->value();
    for (const Slot& slot : slots_)
        if (y < slot.rect.center().y()) return slot.pageIndex;
    return static_cast<int>(slots_.size());
}

QMimeData* ThumbnailsPane::dragData() const
{
    QJsonArray pages;
    for (const int page : commandPages()) pages.append(page);
    const QJsonObject payload{{QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid())},
                              {QStringLiteral("document"), QString::number(static_cast<qulonglong>(document_.id))},
                              {QStringLiteral("pages"), pages}};
    auto* mime = new QMimeData;
    mime->setData(QLatin1String(kDragMimeType), QJsonDocument(payload).toJson(QJsonDocument::Compact));
    return mime;
}

std::optional<std::pair<core::DocumentHandle, std::vector<int>>> ThumbnailsPane::readDrag(const QMimeData* mime) const
{
    // A HANDLE MEANS SOMETHING ONLY IN THE PROCESS THAT MADE IT: a drag from another RawPage is refused here, and
    // pages travel between processes on the clipboard instead (`page.copy`).
    if (mime == nullptr || !mime->hasFormat(QLatin1String(kDragMimeType))) return std::nullopt;
    const QJsonDocument parsed = QJsonDocument::fromJson(mime->data(QLatin1String(kDragMimeType)));
    if (!parsed.isObject()) return std::nullopt;
    const QJsonObject root = parsed.object();
    if (root.value(QStringLiteral("pid")).toInteger(-1) != static_cast<qint64>(QCoreApplication::applicationPid()))
        return std::nullopt;
    bool ok = false;
    const core::DocumentHandle source{root.value(QStringLiteral("document")).toString().toULongLong(&ok)};
    if (!ok || !source.isValid()) return std::nullopt;
    std::vector<int> pages;
    for (const QJsonValue item : root.value(QStringLiteral("pages")).toArray()) {
        const int page = item.toInt(-1);
        if (page < 0 || (!pages.empty() && page <= pages.back())) return std::nullopt;
        pages.push_back(page);
    }
    if (pages.empty()) return std::nullopt;
    return std::make_pair(source, std::move(pages));
}

void ThumbnailsPane::dragEnterEvent(QDragEnterEvent* event)
{
    const auto drag = readDrag(event->mimeData());
    if (!drag || pageCount() <= 0) {
        event->ignore();
        return;
    }
    event->setDropAction(drag->first == document_ ? Qt::MoveAction : Qt::CopyAction);
    event->accept();
    dropIndex_ = insertionIndexAt(event->position().toPoint());
    viewport()->update();
}

void ThumbnailsPane::dragMoveEvent(QDragMoveEvent* event)
{
    const auto drag = readDrag(event->mimeData());
    if (!drag || pageCount() <= 0) {
        event->ignore();
        return;
    }
    // THE COLUMN SCROLLS under a drag held near its top or bottom edge, so a page can be carried past what fits.
    const int y = event->position().toPoint().y();
    const int edge = std::max(8, viewport()->height() / 12);
    if (y < edge) verticalScrollBar()->setValue(verticalScrollBar()->value() - verticalScrollBar()->singleStep());
    else if (y > viewport()->height() - edge)
        verticalScrollBar()->setValue(verticalScrollBar()->value() + verticalScrollBar()->singleStep());
    event->setDropAction(drag->first == document_ ? Qt::MoveAction : Qt::CopyAction);
    event->accept();
    const int index = insertionIndexAt(event->position().toPoint());
    if (index != dropIndex_) {
        dropIndex_ = index;
        viewport()->update();
    }
}

void ThumbnailsPane::dragLeaveEvent(QDragLeaveEvent* event)
{
    dropIndex_ = -1;
    viewport()->update();
    event->accept();
}

void ThumbnailsPane::dropEvent(QDropEvent* event)
{
    const auto drag = readDrag(event->mimeData());
    const int index = insertionIndexAt(event->position().toPoint());
    dropIndex_ = -1;
    viewport()->update();
    if (!drag || index < 0) {
        event->ignore();
        return;
    }
    // A REQUEST, NOT A CHANGE: the window runs the command (see the header).
    if (drag->first == document_) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
        emit movePagesRequested(drag->second, index);
    } else {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        emit pagesDropped(drag->first, drag->second, index);
    }
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
    // `Alt+Down` are §8's *move the selected pages earlier / later* -- `page.moveEarlier` and
    // `page.moveLater`, registered at Stage 7 T8, so `ShortcutBinder` takes them before this widget
    // sees them; and where they are DISABLED (the set already first or last, a document that cannot
    // be edited) the binder leaves the key unconsumed, and two things would then try to take it: the
    // selection walk below, which would move the selection on a chord that means reorder, and
    // `QAbstractScrollArea::keyPressEvent`, which scrolls on an arrow and does not look at the
    // modifiers at all. So this ignores the event rather than passing it to the base.
    //
    // Both halves were found by the CONTROL case at Stage 4, not by the case (conventions 7 and 22):
    // the first fix looked right, and the control was still red because Qt was eating what the pane
    // had just stopped eating. `Shift`, alone, is the one modifier this widget owns (S7 T8): it
    // extends the set from the anchor, as a file manager's does.
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (isArrow && modifiers != Qt::NoModifier && modifiers != Qt::ShiftModifier) {
        event->ignore();
        return;
    }

    int step = 0;
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Left:
        step = -1;
        break;
    case Qt::Key_Down:
    case Qt::Key_Right:
        step = 1;
        break;
    default:
        QAbstractScrollArea::keyPressEvent(event);
        return;
    }
    // THE CURRENT PAGE MOVES, and the set with it -- alone, or the run from the anchor with `Shift`. No navigation:
    // `Enter` (`page.goToSelected`) is the key that goes to the page.
    const int next = std::clamp(currentPage_ + step, 0, count - 1);
    pressPage(next, modifiers);
    const QRect& rect = slots_[static_cast<std::size_t>(next)].rect;
    const int top = verticalScrollBar()->value();
    if (rect.top() < top) verticalScrollBar()->setValue(rect.top());
    else if (rect.bottom() > top + viewport()->height()) verticalScrollBar()->setValue(rect.bottom() - viewport()->height());
    requestVisible();
    event->accept();
}

void ThumbnailsPane::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    const int page = slotAt(event->pos());
    if (page < 0) {
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier);
    event->accept();

    // A PLAIN PRESS INSIDE A SET OF SEVERAL keeps the set until the release, so the set can be dragged; the
    // release makes it this page alone if no drag began (see the header).
    if (modifiers == Qt::NoModifier && selected_.size() > 1 && std::binary_search(selected_.begin(), selected_.end(), page)) {
        currentPage_ = page;
        press_ = Drag{event->pos(), page, true};
        viewport()->update();
        return;
    }

    pressPage(page, modifiers);
    if (std::binary_search(selected_.begin(), selected_.end(), page)) press_ = Drag{event->pos(), page, false};
    else press_.reset();
    // A GESTURE -- and only a plain one navigates. This is the one path besides `page.goToSelected` that
    // emits, and it is why `setCurrentPage` cannot be allowed to emit as well -- see the header.
    if (modifiers == Qt::NoModifier) emit activated(page);
}

void ThumbnailsPane::mouseMoveEvent(QMouseEvent* event)
{
    if (!press_ || !(event->buttons() & Qt::LeftButton)
        || (event->pos() - press_->origin).manhattanLength() < QApplication::startDragDistance()) {
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }
    // THE DRAG BEGINS: the set as it stands. `exec` runs its own event loop until the drop; what the drop means is
    // the TARGET pane's to say (a move here, an insert in another window), so the result is not read here.
    press_.reset();
    auto* drag = new QDrag(this);
    drag->setMimeData(dragData());
    drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
    event->accept();
}

void ThumbnailsPane::mouseReleaseEvent(QMouseEvent* event)
{
    if (press_ && press_->collapse && event->button() == Qt::LeftButton) {
        const int page = press_->page;
        press_.reset();
        pressPage(page, Qt::NoModifier);
        emit activated(page);
        event->accept();
        return;
    }
    press_.reset();
    QAbstractScrollArea::mouseReleaseEvent(event);
}

bool ThumbnailsPane::viewportEvent(QEvent* event)
{
    // WHAT THE BADGES MEAN, in words, under the pointer -- a mark nobody can name is a mark nobody reads.
    if (event->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(event);
        const int page = slotAt(help->pos());
        if (page >= 0 && badgeRect(page).contains(help->pos())) {
            const core::AnnotationStore::PageCarries carries = badgesOf(page);
            QStringList words;
            if (carries.comments) words << catalog::translate(catalog::key::PagesBadgeComments);
            if (carries.fields) words << catalog::translate(catalog::key::PagesBadgeFields);
            if (carries.links) words << catalog::translate(catalog::key::PagesBadgeLinks);
            QToolTip::showText(help->globalPos(), words.join(QLatin1Char('\n')), viewport());
            return true;
        }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    return QAbstractScrollArea::viewportEvent(event);
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

        // THE SET FILLED, THE CURRENT PAGE FRAMED (S7 T8): the frame is where the arrows move from, and it is drawn
        // only while the pane has the focus -- the keyboard's place means nothing to a pane the keys do not reach.
        const bool selected = std::binary_search(selected_.begin(), selected_.end(), index);
        if (selected) {
            painter.fillRect(slotRect.adjusted(-4, -4, 4, 4), palette().highlight());
        }
        if (index == currentPage_ && hasFocus()) {
            painter.save();
            painter.setPen(QPen(palette().color(QPalette::WindowText), 1.0, Qt::DotLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(slotRect.adjusted(-6, -6, 5, 5));
            painter.restore();
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
        painter.setPen(selected ? palette().color(QPalette::HighlightedText)
                                : palette().color(QPalette::WindowText));
        painter.drawText(QRect(slotRect.left(), imageRect.bottom(), slotRect.width(), kLabelHeight),
                         Qt::AlignHCenter | Qt::AlignVCenter, label);

        paintBadges(painter, index, imageRect);
    }

    // THE DROP MARKER: a bar across the column in the gap the drop will land in (S7 T8) -- the insertion point,
    // drawn in the selection's colour so it reads as "your pages go here".
    if (dropIndex_ >= 0 && !slots_.empty()) {
        const int count = static_cast<int>(slots_.size());
        const int y = (dropIndex_ < count ? slots_[static_cast<std::size_t>(dropIndex_)].rect.top() - kSlotSpacing / 2
                                          : slots_.back().rect.bottom() + kSlotSpacing / 2)
                      - top;
        painter.fillRect(QRect(kColumnMargin - 6, y - 1, thumbnailWidth_ + 12, 3), palette().highlight());
    }
}

} // namespace rawpage::ui
