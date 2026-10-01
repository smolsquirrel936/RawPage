#include "ui/viewer/ReflowView.h"

#include "core/cmd/KeyContext.h"
#include "core/text/ReflowText.h"
#include "ui/shell/ShortcutBinder.h"

#include <QAbstractTextDocumentLayout>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

#include <algorithm>

namespace rawpage::ui {
namespace {

// A paragraph's place, on its text block -- which is how `place()` reads the top of the view without
// searching any text.
class PlaceData final : public QTextBlockUserData {
public:
    PlaceData(int pageIndex, int blockIndex, int orderIndex) : page(pageIndex), block(blockIndex), ordinal(orderIndex) {}
    int page;
    int block;
    int ordinal;   // the block's index in its page's reading order
};

const PlaceData* placeOf(const QTextBlock& block)
{
    return static_cast<const PlaceData*>(block.userData());
}

int ordinalOf(const core::text::PageReading& reading, int block)
{
    const std::vector<int>& order = reading.layout().order;
    const auto it = std::find(order.begin(), order.end(), block);
    return it == order.end() ? -1 : static_cast<int>(it - order.begin());
}

// How long arrivals are gathered before the document is rebuilt: a window's pages arrive one by one from
// the engine thread, and a rebuild per page would re-lay the text out eight times in a row.
constexpr int kRebuildCoalesceMs = 30;

} // namespace

ReflowView::ReflowView(core::text::DocumentIndex& index, QWidget* parent) : QTextBrowser(parent), index_(index)
{
    setObjectName(QStringLiteral("reflowView"));
    setOpenLinks(false);
    setOpenExternalLinks(false);
    document()->setUndoRedoEnabled(false);

    // THE VIEWER'S CONTEXT, so `Ctrl+4` comes back and the zoom chords reach this surface (see the header).
    ShortcutBinder::declareContext(this, core::KeyContext::Viewer);

    owner_ = index_.newOwner();
    subscription_ = index_.subscribe([this](const core::text::DocumentIndex::Event& event) { onIndexEvent(event); });

    rebuildTimer_ = new QTimer(this);
    rebuildTimer_->setSingleShot(true);
    rebuildTimer_->setInterval(kRebuildCoalesceMs);
    connect(rebuildTimer_, &QTimer::timeout, this, [this] { rebuild(); });

    // A SCROLL BY THE READER ends any place still being applied -- a place that went on re-applying itself
    // would fight the person scrolling. `actionTriggered` and not `valueChanged`: the value also moves when a
    // re-layout clamps it, and that must not cancel the place the layout is on its way to.
    connect(verticalScrollBar(), &QScrollBar::actionTriggered, this, [this](int) {
        pendingPlace_.reset();
        readerScrolled_ = true;
    });
    // ANY MOVE may bring an edge of the window within a screen, and grow it.
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
        if (rebuilding_) return;
        maybeGrow(readerScrolled_);
        readerScrolled_ = false;
    });
    // THE LAYOUT GROWS AFTER A REBUILD: a place deeper than the first laid-out screen is reached when the
    // range does.
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int) {
        applyPendingPlace();
        if (!rebuilding_) maybeGrow(false);
    });
}

ReflowView::~ReflowView()
{
    // BY TOKEN, before the members go -- convention 10's threaded form, as `SelectionOverlay` does.
    index_.unsubscribe(subscription_);
    index_.release(owner_);
}

void ReflowView::attach(core::text::DocumentIndex::EntryId entry, int pageCount)
{
    if (entry == entry_ && pageCount == pageCount_) return;
    index_.release(owner_);
    entry_ = entry;
    pageCount_ = std::max(0, pageCount);
    readings_.clear();
    first_ = 0;
    last_ = -1;
    pendingPlace_.reset();
    pendingTop_.reset();
    clear();
}

double ReflowView::drawnSize(double modelSize) const
{
    const double size = modelSize > 0.0 ? modelSize : 10.0;
    return std::clamp(size * scale_, 4.0, 288.0);
}

// ---------------------------------------------------------------------------------------------------------
// The window and the place
// ---------------------------------------------------------------------------------------------------------

void ReflowView::showAt(int page, double topY)
{
    if (entry_ == 0 || pageCount_ <= 0) return;
    page = std::clamp(page, 0, pageCount_ - 1);
    pendingPlace_.reset();
    pendingTop_ = PendingTop{page, topY};

    // A FRESH WINDOW ROUND THE PAGE, keeping whatever of the old one it still covers.
    const int first = std::max(0, page - kInitialBefore);
    const int last = std::min(pageCount_ - 1, page + kInitialAfter);
    for (auto it = readings_.begin(); it != readings_.end();)
        it = (it->first < first || it->first > last) ? readings_.erase(it) : std::next(it);
    first_ = first;
    last_ = last;
    for (int p = first_; p <= last_; ++p) request(p);
    rebuild();
}

void ReflowView::showAtPlace(Place place)
{
    if (!place.isValid() || entry_ == 0 || pageCount_ <= 0) return;
    showAt(place.page, 0.0);
    pendingTop_.reset();
    pendingPlace_ = place;
    rebuild();
}

ReflowView::Place ReflowView::place() const
{
    if (pendingPlace_) return *pendingPlace_;
    if (pendingTop_) return Place{pendingTop_->page, -1};

    // THE PARAGRAPH AT THE TOP EDGE -- the one the top pixel row is in, or the nearest after it.
    const QTextCursor cursor = cursorForPosition(QPoint(2, 1));
    for (QTextBlock block = cursor.block(); block.isValid(); block = block.next())
        if (const PlaceData* held = placeOf(block)) return Place{held->page, held->block};
    return Place{};
}

std::vector<ReflowView::Shown> ReflowView::shown() const
{
    std::vector<Shown> out;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next())
        if (const PlaceData* held = placeOf(block)) out.push_back(Shown{Place{held->page, held->block}, block.text()});
    return out;
}

bool ReflowView::isSettled() const
{
    if (pendingTop_ || pendingPlace_ || rebuildTimer_->isActive()) return false;
    for (int p = first_; p <= last_; ++p)
        if (readings_.find(p) == readings_.end()) return false;
    return true;
}

void ReflowView::ensureWindow(int first, int last)
{
    first_ = std::max(0, first);
    last_ = std::min(pageCount_ - 1, last);
    for (int p = first_; p <= last_; ++p) request(p);
    scheduleRebuild();
}

void ReflowView::request(int page)
{
    if (readings_.count(page) != 0) return;
    if (const std::shared_ptr<const core::text::IndexedPage> held = index_.page(entry_, page)) {
        readings_[page] = held->reading();
        return;
    }
    // `Visible`: a person is waiting to read this page (D5-3's lane for text somebody asked to see).
    if (index_.request(owner_, entry_, page, core::render::RenderPriority::Visible)) ++requests_;
}

void ReflowView::onIndexEvent(const core::text::DocumentIndex::Event& event)
{
    if (event.entry != entry_ || entry_ == 0) return;
    using Kind = core::text::DocumentIndex::Event::Kind;
    switch (event.kind) {
    case Kind::PageArrived:
        if (event.page < first_ || event.page > last_ || readings_.count(event.page) != 0) return;
        if (const std::shared_ptr<const core::text::IndexedPage> held = index_.page(entry_, event.page)) {
            readings_[event.page] = held->reading();
            scheduleRebuild();
        }
        return;
    case Kind::PagesInvalidated:
        // THE TEXT MAY HAVE CHANGED -- a rotation's `Scope`. Read again; the place is kept by the rebuild.
        for (const int page : event.pages) {
            if (readings_.erase(page) != 0) request(page);
        }
        scheduleRebuild();
        return;
    case Kind::EntryDropped:
        entry_ = 0;
        readings_.clear();
        clear();
        return;
    case Kind::PagesEvicted:   // what is shown is held here, as a selection holds its pages
    case Kind::UnitsArrived:
        return;
    }
}

bool ReflowView::resolvePendingTop()
{
    if (!pendingTop_) return true;
    const PendingTop top = *pendingTop_;

    // THE BLOCK AT THE PAGE VIEW'S TOP EDGE -- or, on a page scrolled past every block, the first block of
    // the next page that has one. A page whose text has not arrived yet is waited for.
    for (int p = top.page; p <= last_; ++p) {
        const auto it = readings_.find(p);
        if (it == readings_.end()) return false;
        const core::text::PageReading& reading = *it->second;
        const int block = p == top.page ? core::text::blockAtTop(reading, top.topY)
                                        : (reading.layout().order.empty() ? -1 : reading.layout().order.front());
        if (block >= 0) {
            pendingTop_.reset();
            pendingPlace_ = Place{p, block};
            return true;
        }
    }
    // NOTHING BELOW: the reader is past the last block of the window -- show its end.
    pendingTop_.reset();
    return true;
}

void ReflowView::scheduleRebuild()
{
    rebuildTimer_->start();
}

// ---------------------------------------------------------------------------------------------------------
// The document
// ---------------------------------------------------------------------------------------------------------

void ReflowView::rebuild()
{
    rebuildTimer_->stop();
    resolvePendingTop();

    // THE PLACE TO KEEP: one still being applied, or the paragraph at the top now.
    if (!pendingPlace_ && !pendingTop_ && !document()->isEmpty()) {
        const Place now = place();
        if (now.isValid()) pendingPlace_ = now;
    }

    rebuilding_ = true;
    document()->clear();
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    bool firstParagraph = true;
    const QFont base = font();

    for (const auto& [pageIndex, reading] : readings_) {
        if (pageIndex < first_ || pageIndex > last_) continue;
        const core::text::ReflowPage reflow = core::text::reflowParagraphs(*reading);
        bool firstOnPage = true;
        for (const core::text::ReflowParagraph& paragraph : reflow.paragraphs) {
            QTextBlockFormat format;
            format.setBottomMargin(paragraph.tableRow ? 2.0 * scale_ : 8.0 * scale_);
            // A PAGE BEGINS WITH MORE ROOM: the one trace of the page a reflow keeps.
            if (firstOnPage && !firstParagraph) format.setTopMargin(24.0 * scale_);
            format.setLayoutDirection(paragraph.rightToLeft ? Qt::RightToLeft : Qt::LeftToRight);
            if (firstParagraph) {
                cursor.setBlockFormat(format);
            } else {
                cursor.insertBlock(format);
            }
            for (const core::text::ReflowRun& run : paragraph.runs) {
                QTextCharFormat chars;
                chars.setFontFamilies(base.families());
                chars.setFontPointSize(drawnSize(run.size));
                chars.setFontWeight(run.bold ? QFont::Bold : QFont::Normal);
                chars.setFontItalic(run.italic);
                cursor.insertText(run.text, chars);
            }
            cursor.block().setUserData(new PlaceData(paragraph.page, paragraph.block,
                                                     ordinalOf(*reading, paragraph.block)));
            firstParagraph = false;
            firstOnPage = false;
        }
    }
    cursor.endEditBlock();
    rebuilding_ = false;

    applyPendingPlace();
    maybeGrow(false);
}

void ReflowView::applyPendingPlace()
{
    if (!pendingPlace_ || rebuilding_) return;
    const Place target = *pendingPlace_;
    const auto reading = readings_.find(target.page);
    if (reading == readings_.end()) return;   // still on its way
    const int targetOrdinal = std::max(0, ordinalOf(*reading->second, target.block));

    // THE FIRST PARAGRAPH AT OR AFTER THE PLACE, in (page, reading order).
    QTextBlock found;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
        const PlaceData* held = placeOf(block);
        if (held == nullptr) continue;
        if (held->page > target.page || (held->page == target.page && held->ordinal >= targetOrdinal)) {
            found = block;
            break;
        }
    }
    if (!found.isValid()) return;

    const int y = static_cast<int>(document()->documentLayout()->blockBoundingRect(found).top());
    QScrollBar* bar = verticalScrollBar();
    rebuilding_ = true;
    bar->setValue(y);
    rebuilding_ = false;
    // APPLIED once the bar could go there; until the layout is long enough the place stays pending and the
    // next range change applies it again.
    if (bar->value() == y || y > bar->maximum() + viewport()->height() * 4) pendingPlace_.reset();
}

void ReflowView::maybeGrow(bool byReader)
{
    // NOT WHILE THE WINDOW IS STILL ARRIVING: growth is for a window that is all here and still too short.
    if (entry_ == 0 || pendingTop_) return;
    for (int p = first_; p <= last_; ++p)
        if (readings_.find(p) == readings_.end()) return;
    if (!byReader && last_ - first_ + 1 >= kMaxAutomaticPages) return;

    const QScrollBar* bar = verticalScrollBar();
    const int screen = std::max(1, viewport()->height());
    // AFTER THE PLACE FIRST: less than a screen below the top of the view -- including a place the bar cannot
    // yet reach, which is exactly a window too short to put it at the top.
    if (bar->maximum() - bar->value() < screen && last_ < pageCount_ - 1) {
        ensureWindow(first_, last_ + kGrowBy);
    } else if (!pendingPlace_ && bar->value() < screen && first_ > 0) {
        // BEFORE only once the place has landed: the rebuild keeps the paragraph at the top where it is.
        ensureWindow(first_ - kGrowBy, last_);
    }
}

void ReflowView::zoomIn()
{
    scale_ = std::min(kMaxScale, scale_ * kZoomStep);
    rebuild();
}

void ReflowView::zoomOut()
{
    scale_ = std::max(kMinScale, scale_ / kZoomStep);
    rebuild();
}

void ReflowView::resetZoom()
{
    scale_ = 1.0;
    rebuild();
}

// ---------------------------------------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------------------------------------

bool ReflowView::event(QEvent* event)
{
    // THE KEYS A READER SCROLLS WITH ARE THIS SURFACE'S (see the header): claimed through the handshake
    // `ShortcutBinder` offers the focused widget first, so the Viewer table does not page the hidden view.
    if (event->type() == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const Qt::KeyboardModifiers modifiers = key->modifiers() & ~Qt::KeypadModifier;
        const bool plain = modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier;
        switch (key->key()) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
        case Qt::Key_Space:
            if (plain) {
                event->accept();
                return true;
            }
            break;
        case Qt::Key_Home:
        case Qt::Key_End:
            if (plain || modifiers == Qt::ControlModifier) {
                event->accept();
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QTextBrowser::event(event);
}

void ReflowView::resizeEvent(QResizeEvent* event)
{
    // RE-WRAPPED TO THE NEW WIDTH, AT THE SAME PLACE: the paragraph at the top before is at the top after.
    const Place before = document()->isEmpty() ? Place{} : place();
    QTextBrowser::resizeEvent(event);
    if (before.isValid() && !pendingTop_) {
        pendingPlace_ = before;
        applyPendingPlace();
    }
}

} // namespace rawpage::ui
