#include "ui/viewer/SelectionOverlay.h"

#include "core/Log.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;
using core::text::CopiedText;
using core::text::Granularity;
using core::text::PageReading;
using core::text::TextPosition;
using core::text::TextSelection;
using core::text::TextUnit;

// A page-space rectangle in CONTENT device pixels, through the layout's own mapping -- all four corners,
// because under a quarter turn the PDF bottom-left is not the device top-left (`PageTransform::toDevice`
// makes the same argument for itself).
std::optional<core::DeviceRect> toContent(const core::render::PageLayout& layout, int page, const core::PdfRect& rect)
{
    double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
    bool first = true;
    for (const core::PdfPoint corner : {core::PdfPoint{rect.left(), rect.bottom()}, core::PdfPoint{rect.right(), rect.top()},
                                        core::PdfPoint{rect.left(), rect.top()}, core::PdfPoint{rect.right(), rect.bottom()}}) {
        const std::optional<core::DevicePoint> device = layout.toContent(page, corner);
        if (!device) return std::nullopt;
        if (first) {
            left = right = device->x;
            top = bottom = device->y;
            first = false;
        } else {
            left = std::min(left, device->x);
            right = std::max(right, device->x);
            top = std::min(top, device->y);
            bottom = std::max(bottom, device->y);
        }
    }
    return core::DeviceRect{core::DevicePoint{left, top}, core::DeviceSize{right - left, bottom - top}};
}

} // namespace

SelectionOverlay::SelectionOverlay(core::text::DocumentIndex& index, QObject* parent)
    : QObject(parent), index_(index), owner_(index.newOwner())
{
    // Removed by TOKEN in the destructor -- convention 10's form for a subscription list.
    subscription_ = index_.subscribe([this](const core::text::DocumentIndex::Event& event) { onIndexEvent(event); });
}

void SelectionOverlay::notifyChanged()
{
    emit changed();
    const bool present = hasSelection();
    if (present == hadSelection_) return;
    hadSelection_ = present;
    emit presenceChanged(present);
}

SelectionOverlay::~SelectionOverlay()
{
    index_.unsubscribe(subscription_);
    cancelRequests();
}

void SelectionOverlay::cancelRequests()
{
    // Only THIS object's interest: a page a search still wants keeps its ticket (`DocumentIndex::release`).
    index_.release(owner_);
    requested_.clear();
}

void SelectionOverlay::setDocument(core::text::DocumentIndex::EntryId entry, int pageCount)
{
    cancelRequests();
    entry_ = entry;
    text_.reset(entry != 0 ? pageCount : 0);
    selection_ = TextSelection{};
    beforeGesture_ = TextSelection{};
    gestureActive_ = false;
    gestureResolved_ = true;
    anchor_ = Point{};
    focus_ = Point{};
    anchorUnit_.reset();
    pendingCopy_ = false;
    notifyChanged();
}

bool SelectionOverlay::ensurePage(int page, core::render::RenderPriority priority)
{
    if (entry_ == 0 || page < 0 || page >= text_.pageCount()) return false;
    if (text_.has(page)) return true;

    // THE INDEX HOLDS IT -- extracted for another window over this file, for a search, or for this window
    // earlier -- so its reading is taken, and nothing is asked of the engine (S5 T6, guide A-66).
    if (const std::shared_ptr<const core::text::IndexedPage> held = index_.page(entry_, page)) {
        text_.insert(held->reading());
        return true;
    }

    // ASKED OF THE INDEX, which asks the engine once per page for every owner and raises a queued ask when
    // this one is more urgent -- a page the index build queued at `Background` is the page a person just
    // started selecting on (D5-3).
    const long long before = index_.stats().requested;
    (void)index_.request(owner_, entry_, page, priority);
    requestsMade_ += static_cast<int>(index_.stats().requested - before);
    requested_.insert(page);
    return false;
}

void SelectionOverlay::takePage(int page)
{
    const std::shared_ptr<const core::text::IndexedPage> held = index_.page(entry_, page);
    if (held == nullptr || text_.has(page)) return;
    // A FAILED PAGE IS READ AS EMPTY (the index stored it so, and logged why): it contributes no text, which
    // is the truth about it -- nothing is invented in its place (guide A-26's "fails alone").
    text_.insert(held->reading());

    if (!gestureResolved_) recompute();

    if (pendingCopy_) {
        const int left = static_cast<int>(selection_.missingPages(text_).size());
        emit copyProgress(pendingTotal_ - left, pendingTotal_);
        if (left == 0) finishCopy();
    }
    notifyChanged();
}

void SelectionOverlay::onIndexEvent(const core::text::DocumentIndex::Event& event)
{
    if (event.entry != entry_ || entry_ == 0) return;
    switch (event.kind) {
    case core::text::DocumentIndex::Event::Kind::PageArrived:
        // ONLY A PAGE THIS OBJECT ASKED FOR: a search building the index delivers every page of the
        // document, and laying 900 of them out here, on the UI thread, for a selection nobody made would be
        // work nobody sees. Pages are taken when a gesture, a paint or a copy needs them.
        if (requested_.erase(event.page) == 0) return;
        takePage(event.page);
        return;
    case core::text::DocumentIndex::Event::Kind::PagesInvalidated: {
        // THE TEXT MAY HAVE CHANGED (a mutation's `Scope`): forgotten here too, and read again when the page
        // is next shown or copied. The selection's carets stay.
        bool any = false;
        for (const int page : event.pages) any = text_.erase(page) || any;
        if (any) notifyChanged();
        return;
    }
    case core::text::DocumentIndex::Event::Kind::PagesEvicted:
        return;   // the index's budget, not this selection's: the pages read here are still true
    case core::text::DocumentIndex::Event::Kind::EntryDropped:
        requested_.clear();
        return;
    }
}

// =====================================================================================================
// The gesture
// =====================================================================================================

void SelectionOverlay::press(int page, core::PdfPoint point, const PressOptions& options)
{
    if (entry_ == 0 || page < 0 || page >= text_.pageCount()) return;
    beforeGesture_ = selection_;
    beforeAnchor_ = anchor_;
    beforeAnchorUnit_ = anchorUnit_;
    beforeOptions_ = pressOptions_;

    // `Shift`+CLICK EXTENDS a flow: the anchor stays and only the focus moves. With nothing selected, or
    // a column selected, it is an ordinary press -- there is no flow anchor to keep.
    const bool extend = options.extend && selection_.kind() == TextSelection::Kind::Flow && !selection_.isEmpty();
    if (extend) {
        if (!anchorUnit_ && anchor_.page < 0) anchorUnit_ = TextUnit{selection_.start(), selection_.start()};
        pressOptions_.column = false;
        pressOptions_.extend = true;
    } else {
        pressOptions_ = options;
        anchor_ = Point{page, point};
        anchorUnit_.reset();
    }
    focus_ = Point{page, point};
    gestureActive_ = true;
    gestureResolved_ = false;

    ensurePage(page, core::render::RenderPriority::Visible);
    if (!extend && anchor_.page >= 0) ensurePage(anchor_.page, core::render::RenderPriority::Visible);
    recompute();
}

void SelectionOverlay::drag(int page, core::PdfPoint point)
{
    if (!gestureActive_) return;
    focus_ = Point{page, point};
    ensurePage(page, core::render::RenderPriority::Visible);
    gestureResolved_ = false;
    recompute();
}

void SelectionOverlay::release()
{
    gestureActive_ = false;
}

void SelectionOverlay::cancelGesture()
{
    if (!gestureActive_) return;
    gestureActive_ = false;
    gestureResolved_ = true;
    // ALL OF IT, anchor included: a `Shift`+click after the cancel extends what is on screen, not the
    // press that was abandoned.
    selection_ = beforeGesture_;
    anchor_ = beforeAnchor_;
    anchorUnit_ = beforeAnchorUnit_;
    pressOptions_ = beforeOptions_;
    notifyChanged();
}

int SelectionOverlay::columnGesturePage() const noexcept
{
    return gestureActive_ && pressOptions_.column ? anchor_.page : -1;
}

void SelectionOverlay::recompute()
{
    if (gestureResolved_) return;

    if (pressOptions_.column) {
        // A COLUMN needs no text to be a rectangle -- but a rectangle over a page whose text is not here
        // selects nothing yet, and says so by being resolved the moment the page arrives.
        selection_ = TextSelection::column(
            anchor_.page, core::PdfRect{anchor_.point, core::PdfSize{focus_.point.x - anchor_.point.x,
                                                                     focus_.point.y - anchor_.point.y}});
        gestureResolved_ = text_.has(anchor_.page);
        notifyChanged();
        return;
    }

    // A FLOW needs the anchor's unit and the focus's, each from its page's reading. Until both pages have
    // been read the gesture waits as points -- the drag goes on, and resolves when the text arrives.
    std::optional<TextUnit> anchor = anchorUnit_;
    if (!anchor) {
        const PageReading* page = text_.page(anchor_.page);
        if (page == nullptr) return;
        anchor = core::text::unitAt(*page, anchor_.point, pressOptions_.granularity);
    }
    const PageReading* focusPage = text_.page(focus_.page);
    if (focusPage == nullptr) return;
    const TextUnit focus = core::text::unitAt(*focusPage, focus_.point, pressOptions_.granularity);

    selection_ = TextSelection::spanning(*anchor, focus);
    gestureResolved_ = true;
    notifyChanged();
}

// =====================================================================================================
// The selection
// =====================================================================================================

bool SelectionOverlay::clear()
{
    if (selection_.isEmpty()) return false;
    selection_ = TextSelection{};
    anchor_ = Point{};
    focus_ = Point{};
    anchorUnit_.reset();
    gestureResolved_ = true;
    notifyChanged();
    return true;
}

void SelectionOverlay::selectAll()
{
    if (entry_ == 0 || text_.pageCount() <= 0) return;
    gestureActive_ = false;
    gestureResolved_ = true;
    selection_ = TextSelection::wholeDocument(text_.pageCount());
    anchor_ = Point{};
    anchorUnit_ = TextUnit{selection_.start(), selection_.start()};
    notifyChanged();
}

// =====================================================================================================
// The copy
// =====================================================================================================

CopiedText SelectionOverlay::copiedText() const
{
    return selection_.copy(text_);
}

QMimeData* SelectionOverlay::mimeDataFor(const CopiedText& copied)
{
    auto* data = new QMimeData;
    // PLAIN AND HTML IN ONE OBJECT, and the paste target chooses (D5-9): Notepad takes the first, Word the
    // second, Excel the table in the second. Qt's Windows platform publishes `text/html` as CF_HTML.
    data->setText(copied.plain);
    if (!copied.html.isEmpty()) data->setHtml(copied.html);
    return data;
}

SelectionOverlay::CopyOutcome SelectionOverlay::copy()
{
    if (selection_.isEmpty()) return CopyOutcome::NothingSelected;

    // PAGES NOT READ YET ARE READ FIRST, at `Background`, and the copy completes when the last arrives
    // (D5-9). Nothing reaches the clipboard until then, so `Esc` in the meantime leaves it as it was.
    const std::vector<int> missing = selection_.missingPages(text_);
    if (!missing.empty()) {
        // A PAGE THE INDEX ALREADY HOLDS IS TAKEN HERE (S5 T6) -- a search may have read every one of them
        // -- so the copy waits only for what is really still to come, and waits for nothing when that is
        // nothing: no page would ever arrive to finish it.
        for (const int page : missing) ensurePage(page, core::render::RenderPriority::Background);
        const std::size_t left = selection_.missingPages(text_).size();
        if (left > 0) {
            pendingCopy_ = true;
            pendingTotal_ = selection_.lastPage() - selection_.firstPage() + 1;
            emit copyProgress(pendingTotal_ - static_cast<int>(left), pendingTotal_);
            return CopyOutcome::Pending;
        }
    }

    pendingCopy_ = false;
    const CopiedText written = copiedText();
    if (written.isEmpty()) {
        if (written.unmappedSkipped > 0) {
            // THE S1 THIS STAGE IS MOST LIKELY TO SHIP QUIETLY (guide A-21): glyphs with no trustworthy
            // Unicode are not text, and a paste of U+FFFD or of the engine's guess would be characters the
            // document does not contain. Nothing reaches the clipboard, and the shell says why.
            emit copyNotExtractable(written.unmappedSkipped);
            return CopyOutcome::NotExtractable;
        }
        return CopyOutcome::NothingSelected;
    }

    if (QClipboard* clipboard = QGuiApplication::clipboard()) clipboard->setMimeData(mimeDataFor(written));
    emit copied(written.characters, written.unmappedSkipped);
    return CopyOutcome::Copied;
}

void SelectionOverlay::finishCopy()
{
    pendingCopy_ = false;
    (void)copy();
}

bool SelectionOverlay::cancelPendingCopy()
{
    if (!pendingCopy_) return false;
    pendingCopy_ = false;
    // Everything still queued goes: a page on screen is asked for again by the next paint, at `Visible`.
    cancelRequests();
    emit copyCancelled();
    return true;
}

// =====================================================================================================
// Painting
// =====================================================================================================

std::vector<core::DeviceRect> SelectionOverlay::highlightRects(const core::render::PageLayout& layout, int page) const
{
    std::vector<core::DeviceRect> out;
    const PageReading* reading = text_.page(page);
    if (reading == nullptr || !selection_.covers(page)) return out;
    for (const core::PdfRect& rect : selection_.highlightOn(*reading))
        if (const std::optional<core::DeviceRect> device = toContent(layout, page, rect)) out.push_back(*device);
    return out;
}

void SelectionOverlay::paint(QPainter& painter, const core::render::PageLayout& layout,
                             const core::DeviceRect& visible, double devicePixelRatio, const QColor& colour)
{
    if (selection_.isEmpty()) return;
    const double dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;

    painter.save();
    for (const core::render::PageSlot& slot : layout.slotsIntersecting(visible)) {
        if (!selection_.covers(slot.pageIndex)) continue;
        if (!text_.has(slot.pageIndex)) {
            // ON SCREEN AND SELECTED, SO SOMEBODY IS WAITING: asked for at `Visible`. Its highlight is
            // painted when it arrives -- `changed` repaints.
            ensurePage(slot.pageIndex, core::render::RenderPriority::Visible);
            continue;
        }
        for (const core::DeviceRect& rect : highlightRects(layout, slot.pageIndex)) {
            painter.fillRect(QRectF((rect.left() - visible.left()) / dpr, (rect.top() - visible.top()) / dpr,
                                    rect.size.width / dpr, rect.size.height / dpr),
                             colour);
        }
    }
    painter.restore();
}

} // namespace rawpage::ui
