#include "ui/viewer/ComparisonView.h"

#include "core/Log.h"
#include "core/cmd/ActionId.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/cmd/KeyContext.h"
#include "core/doc/Page.h"
#include "core/doc/PageTree.h"
#include "core/doc/Revision.h"
#include "core/render/RenderCache.h"
#include "core/render/RenderScheduler.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"
#include "platform/platform.h"
#include "ui/panes/DifferencesPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/LoupeRaster.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ViewportController.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <map>
#include <mutex>

namespace rawpage::ui {

using core::LogSubsystem;
using core::text::CompareSide;
using core::text::Difference;
using core::text::DifferenceKind;
using core::text::PageMatch;

namespace {

constexpr int kOlder = 0;
constexpr int kNewer = 1;

int at(CompareSide side)
{
    return side == CompareSide::Base ? kOlder : kNewer;
}

// THE TWO TINTS (the header): red for what only the older document has, blue for what only the newer has.
// Constants and not the palette, for `PageView::searchHitColour`'s reason -- they are drawn on the PAGE,
// whose paper is the document's in either theme -- and the overlay's composite produces exactly these two
// hues, so a mark and the ink it marks agree.
QColor tint(int side, bool current)
{
    QColor colour = side == kOlder ? QColor(220, 30, 30) : QColor(30, 90, 230);
    colour.setAlpha(current ? 110 : 60);
    return colour;
}

// Margin round the overlay's pages, in logical pixels.
constexpr int kOverlayMargin = 12;

// The overlay's Actual Size: logical pixels per point at 100 %, the page views' nominal 96 dpi
// (`core/render/DpiPolicy`'s `PhysicalDpi::nominal`).
constexpr double kActualSizePerPoint = 96.0 / 72.0;

// A wheel notch, in `QWheelEvent::angleDelta` units.
constexpr int kWheelNotch = 120;

// The gap between two pairs in the continuous overlay, in logical pixels.
constexpr int kOverlayGap = 12;

} // namespace

// =============================================================================================================
// THE RASTER BRIDGE -- the comparison's thread asks, this thread enqueues, the scheduler delivers here.
// =============================================================================================================
// `ComparisonRun` calls its raster provider ON ITS OWN THREAD and lets it block (D5-17(a)). The scheduler's
// cache and its delivery belong to the pumping thread, so the provider posts the enqueue here and waits for the
// pixels to be delivered here -- the engine is entered by the render thread alone, at `Background`, with
// `CachePolicy::Bypass` (D5-4: a comparison's rasters are consumed once and must not evict the page a person is
// reading). `abort()` releases every waiter, which is what lets the view's destructor join the run.
class ComparisonView::RasterBridge : public QObject {
public:
    RasterBridge(core::render::RenderScheduler& scheduler,
                 std::array<std::vector<core::render::RenderRequest>, 2> requests)
        : scheduler_(scheduler), requests_(std::move(requests))
    {
        subscription_ = scheduler_.subscribeReady(
            [this](const core::render::RenderRequest& request, const core::RasterImage& image) {
                onReady(request, image);
            });
    }

    ~RasterBridge() override
    {
        abort();
        scheduler_.unsubscribeReady(subscription_);
    }

    // ON THE COMPARISON'S THREAD. Empty when the bridge is aborted or the page cannot be drawn.
    std::optional<core::RasterImage> fetch(CompareSide side, int page)
    {
        const std::vector<core::render::RenderRequest>& list = requests_[static_cast<std::size_t>(at(side))];
        if (page < 0 || page >= static_cast<int>(list.size())) return std::nullopt;

        std::unique_lock<std::mutex> lock(mutex_);
        if (aborted_) return std::nullopt;
        const int id = nextId_++;
        slots_[id].request = list[static_cast<std::size_t>(page)];
        lock.unlock();

        QMetaObject::invokeMethod(this, [this, id] { issue(id); }, Qt::QueuedConnection);

        lock.lock();
        done_.wait(lock, [this, id] { return aborted_ || slots_[id].done; });
        std::optional<core::RasterImage> out;
        const auto it = slots_.find(id);
        if (it != slots_.end()) {
            if (it->second.done && it->second.image.isValid()) out = std::move(it->second.image);
            slots_.erase(it);
        }
        return out;
    }

    // ON THIS THREAD: every waiter released, every ticket cancelled, nothing more enqueued.
    void abort()
    {
        std::vector<core::render::RenderTicket> tickets;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            aborted_ = true;
            for (auto& [id, slot] : slots_)
                if (slot.ticket.isValid()) tickets.push_back(slot.ticket);
        }
        for (const core::render::RenderTicket ticket : tickets) scheduler_.cancel(ticket);
        done_.notify_all();
    }

private:
    struct Slot {
        core::render::RenderRequest request;
        core::render::RenderTicket  ticket;
        core::RasterImage           image;
        bool                        done = false;
    };

    void issue(int id)
    {
        core::render::RenderRequest request;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (aborted_) return;
            const auto it = slots_.find(id);
            if (it == slots_.end()) return;
            request = it->second.request;
        }
        const core::render::RenderTicket ticket = scheduler_.enqueue(
            request, core::render::RenderPriority::Background, core::render::CachePolicy::Bypass);
        bool refused = false;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto it = slots_.find(id);
            if (it != slots_.end()) {
                if (ticket.isValid()) {
                    it->second.ticket = ticket;
                } else {
                    it->second.done = true;   // refused: delivered as "cannot be drawn"
                    refused = true;
                }
            }
        }
        if (refused) done_.notify_all();
    }

    void onReady(const core::render::RenderRequest& request, const core::RasterImage& image)
    {
        bool any = false;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [id, slot] : slots_) {
                if (slot.done || !slot.ticket.isValid() || !(slot.request == request)) continue;
                slot.done = true;
                slot.image = image;
                any = true;
            }
        }
        if (any) done_.notify_all();
    }

    core::render::RenderScheduler& scheduler_;
    std::array<std::vector<core::render::RenderRequest>, 2> requests_;
    core::render::RenderScheduler::SubscriptionToken subscription_ = 0;

    std::mutex              mutex_;
    std::condition_variable done_;
    std::map<int, Slot>     slots_;
    int                     nextId_ = 1;
    bool                    aborted_ = false;
};

// =============================================================================================================
// THE OVERLAY'S CANVAS -- the pair's page (or, continuous, every pair's), the composite of the region in view on
// each, and the current difference outlined. It is the scroll area's widget, sized to what it lays out at the
// overlay's scale and never smaller than the viewport (`aimOverlay`).
// =============================================================================================================
// ONE PAIR THE OVERLAY SHOWS (the header): its two rasters, the region of its page they were asked for, and what
// was composited from them, placed at `origin` on the page.
struct ComparisonView::OverlaySlot {
    std::unique_ptr<LoupeRaster> older;
    std::unique_ptr<LoupeRaster> newer;
    core::render::RenderRequest  olderRequest;
    core::render::RenderRequest  newerRequest;
    QRect  region;      // device pixels on the pair's page
    QImage composite;
    QPoint origin;      // where `composite` is, the same pixels
};

class ComparisonView::OverlayCanvas : public QWidget {
public:
    OverlayCanvas(ComparisonView& owner, QWidget* parent) : QWidget(parent), owner_(owner)
    {
        setObjectName(QStringLiteral("comparisonOverlay"));
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(120, 120);
    }

    // Where the current pair's page is, in logical pixels.
    QRectF pageRect() const { return owner_.overlayPairRect(owner_.pair_); }

    // A page-space rectangle of `page`, on this canvas, on pair `pair`.
    QRectF onCanvas(int pair, const core::Page& page, const core::PdfRect& rect) const
    {
        return mapRect(page, rect, owner_.overlayScale_ / ratio()).translated(owner_.overlayPairRect(pair).topLeft());
    }

    double ratio() const { return devicePixelRatioF() > 0.0 ? devicePixelRatioF() : 1.0; }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPainter painter(this);
        painter.fillRect(event->rect(), palette().color(QPalette::Window));
        const std::size_t laid = std::min(owner_.pairRects_.size(), owner_.pairs_.size());
        for (std::size_t p = 0; p < laid; ++p) {
            const QRectF page = owner_.pairRects_[p];
            if (page.isEmpty() || !page.intersects(QRectF(event->rect()))) continue;
            // THE PAPER FIRST, so a region not yet rasterized after a scroll is the page and not a hole.
            painter.fillRect(page, Qt::white);
            const auto slot = owner_.slots_.find(static_cast<int>(p));
            if (slot == owner_.slots_.end() || slot->second->composite.isNull()) continue;
            const QImage& image = slot->second->composite;
            const double dpr = image.devicePixelRatio() > 0.0 ? image.devicePixelRatio() : 1.0;
            const QPointF at = page.topLeft() + QPointF(slot->second->origin) / dpr;
            painter.drawImage(QRectF(at, QSizeF(image.width() / dpr, image.height() / dpr)), image);
        }

        // THE CURRENT DIFFERENCE, outlined in its side's colour, on the pair it is on.
        const int index = owner_.current_;
        if (index < 0 || index >= static_cast<int>(owner_.ordered_.size())) return;
        const Difference& difference = owner_.ordered_[static_cast<std::size_t>(index)];
        for (std::size_t p = 0; p < laid; ++p) {
            if (owner_.pairRects_[p].isEmpty()) continue;
            const auto [olderPage, newerPage] = owner_.pairs_[p];
            for (const int side : {kOlder, kNewer}) {
                const int pageIndex = side == kOlder ? olderPage : newerPage;
                const int differencePage = side == kOlder ? difference.basePage : difference.revisedPage;
                if (pageIndex < 0 || pageIndex != differencePage) continue;
                const core::Page* info = owner_.sides_[static_cast<std::size_t>(side)].pages->at(pageIndex);
                if (info == nullptr) continue;
                QColor edge = tint(side, true);
                edge.setAlpha(255);
                painter.setPen(QPen(edge, 2.0));
                painter.setBrush(Qt::NoBrush);
                for (const core::PdfRect& rect :
                     owner_.marksOn(difference, side == kOlder ? CompareSide::Base : CompareSide::Revised))
                    painter.drawRect(onCanvas(static_cast<int>(p), *info, rect).adjusted(-2.0, -2.0, 2.0, 2.0));
            }
        }
    }

    void wheelEvent(QWheelEvent* event) override { owner_.overlayWheel(event); }

private:
    // A page-space rectangle on a whole-page raster of `page` at `scale` logical pixels per point: the crop
    // box's top-left at the origin, the page's own /Rotate applied, as the rasterizer draws it.
    static QRectF mapRect(const core::Page& page, const core::PdfRect& rect, double scale)
    {
        const core::PdfRect crop = page.cropBox();
        const double w = crop.right() - crop.left();
        const double h = crop.top() - crop.bottom();
        const auto map = [&](double x, double y) {
            const double u = x - crop.left();
            const double v = crop.top() - y;
            switch (page.rotation()) {
            case core::PageRotation::Clockwise90:  return QPointF(h - v, u);
            case core::PageRotation::Half: return QPointF(w - u, h - v);
            case core::PageRotation::Clockwise270: return QPointF(v, w - u);
            case core::PageRotation::None:      break;
            }
            return QPointF(u, v);
        };
        const QPointF a = map(rect.left(), rect.top());
        const QPointF b = map(rect.right(), rect.bottom());
        return QRectF(QPointF(std::min(a.x(), b.x()) * scale, std::min(a.y(), b.y()) * scale),
                      QPointF(std::max(a.x(), b.x()) * scale, std::max(a.y(), b.y()) * scale));
    }

    ComparisonView& owner_;
};

// =============================================================================================================
// Construction and teardown
// =============================================================================================================

ComparisonView::ComparisonView(app::Services services, Side older, Side newer, QWidget* parent)
    : QWidget(parent), services_(services)
{
    setObjectName(QStringLiteral("comparisonView"));
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose, true);
    ShortcutBinder::declareContext(this, core::KeyContext::Viewer);

    sides_[kOlder].file = std::move(older);
    sides_[kNewer].file = std::move(newer);
    for (Held& held : sides_) held.pages = services_.backend.pages(held.file.handle);

    setWindowTitle(catalog::translate(catalog::key::CompareViewTitle)
                       .arg(QFileInfo(sides_[kOlder].file.path).fileName(),
                            QFileInfo(sides_[kNewer].file.path).fileName()));

    options_ = core::text::CompareOptions::fromSettings(services_.settings);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    buildBar();

    // THE TWO PAGES, side by side, each under its file's name, and the overlay's canvas in their place.
    auto* body = new QSplitter(Qt::Horizontal, this);
    stack_ = new QStackedWidget(body);
    auto* sideBySide = new QSplitter(Qt::Horizontal, stack_);
    sideBySide->setObjectName(QStringLiteral("comparisonSides"));
    const platform::IDisplayTopology* topology = services_.platform.displayTopology.get();
    for (const int side : {kOlder, kNewer}) {
        auto* host = new QWidget(sideBySide);
        auto* hostColumn = new QVBoxLayout(host);
        hostColumn->setContentsMargins(0, 0, 0, 0);
        hostColumn->setSpacing(2);
        auto* name = new QLabel(QFileInfo(sides_[static_cast<std::size_t>(side)].file.path).fileName(), host);
        name->setContentsMargins(6, 4, 6, 0);
        hostColumn->addWidget(name);
        auto* view = new PageView(services_.renderScheduler, topology, host);
        view->setObjectName(side == kOlder ? QStringLiteral("comparisonOlder") : QStringLiteral("comparisonNewer"));
        hostColumn->addWidget(view, 1);
        sideBySide->addWidget(host);
        (side == kOlder ? older_ : newer_) = view;
    }
    stack_->addWidget(sideBySide);
    // THE OVERLAY SCROLLS (I-24): its canvas in a scroll area whose vertical bar is always there, so Fit Width
    // never narrows the viewport it was fitted to by growing a bar.
    overlayScroll_ = new QScrollArea(stack_);
    overlayScroll_->setObjectName(QStringLiteral("comparisonOverlayScroll"));
    overlayScroll_->setFrameShape(QFrame::NoFrame);
    overlayScroll_->setFocusPolicy(Qt::NoFocus);
    overlayScroll_->setWidgetResizable(false);
    overlayScroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    canvas_ = new OverlayCanvas(*this, overlayScroll_);
    overlayScroll_->setWidget(canvas_);
    stack_->addWidget(overlayScroll_);
    body->addWidget(stack_);
    pane_ = new DifferencesPane(body);
    body->addWidget(pane_);
    body->setStretchFactor(0, 4);
    body->setStretchFactor(1, 1);
    column->addWidget(body, 1);

    // THE DOCUMENTS, as each tab hands one to a pane: the engine's tree, which is the model here -- nothing
    // edits these handles -- at the initial revision. Fit Width, so the two pages of a pair are the same
    // size whatever the window's width.
    ViewSettings settings;
    settings.defaultZoom = core::render::FitMode::Width;
    for (const int side : {kOlder, kNewer}) {
        PageView* view = side == kOlder ? older_ : newer_;
        const Held& held = sides_[static_cast<std::size_t>(side)];
        view->setDocument(held.file.handle, core::Revision::initial(), held.pages);
        view->applySettings(settings);
        view->installEventFilter(this);
        connect(&view->controller(), &ViewportController::changed, this, [this, view] { syncFrom(view); });
    }
    canvas_->installEventFilter(this);
    overlayScroll_->viewport()->installEventFilter(this);
    // A PERSON'S SCROLL moves the current pair, continuous, to the one at the top; a scroll of ours holds it.
    for (QScrollBar* bar : {overlayScroll_->horizontalScrollBar(), overlayScroll_->verticalScrollBar()}) {
        connect(bar, &QScrollBar::valueChanged, this, [this] {
            if (continuous_ && !holdingPair_ && mode_ == Mode::Overlay) pair_ = pairAtTop();
            aimOverlayRegion(false);
        });
    }

    connect(pane_, &DifferencesPane::differenceChosen, this, &ComparisonView::goToDifference);

    settingsSubscription_ = services_.settings.subscribe([this](const core::Settings::Change& change) {
        if (change.group == core::SettingsGroup::Documents) onSettingsChanged();
    });

    rebuildPairs();
    askForText();
}

ComparisonView::~ComparisonView()
{
    services_.settings.unsubscribe(settingsSubscription_);

    // THE RUN FIRST: its thread may be waiting on a raster, which `abort()` releases, and is then joined.
    stopRun();

    // Then every other holder of a ticket or a request on the two handles.
    if (indexSubscription_ != 0) services_.documentIndex.unsubscribe(indexSubscription_);
    if (owner_ != 0) services_.documentIndex.release(owner_);
    slots_.clear();
    for (PageView* view : {older_, newer_}) {
        if (view == nullptr) continue;
        disconnect(&view->controller(), nullptr, this, nullptr);
        view->removeEventFilter(this);
        view->setDocument(core::DocumentHandle{}, core::Revision::initial(), nullptr);
    }
    if (pane_ != nullptr) disconnect(pane_, nullptr, this, nullptr);
    // Convention 10: every lambda the bar's controls hold `this` in.
    for (QObject* control : std::initializer_list<QObject*>{modeChoice_, sensitivityChoice_, ignoreHeaderFooter_,
                                                            ignorePageNumbers_, ignoreWhitespace_, cancelButton_})
        if (control != nullptr) disconnect(control, nullptr, this, nullptr);

    // THE THREE-STEP CLOSE (`DocumentWindow.h`), for each handle: out of the index, cancel and drain, drop the
    // tiles, close.
    for (const Held& held : sides_) {
        const core::DocumentHandle handle = held.file.handle;
        if (!handle.isValid()) continue;
        services_.documentIndex.detach(handle);
        services_.renderScheduler.cancelDocument(handle);
    }
    if (!services_.renderScheduler.waitForIdle(5000)) {
        RAWPAGE_LOG_ERROR(LogSubsystem::Render)
            << "compare: the render scheduler did not go idle within 5000 ms; closing anyway";
    }
    for (const Held& held : sides_) {
        const core::DocumentHandle handle = held.file.handle;
        if (!handle.isValid()) continue;
        services_.renderCache.dropDocument(handle);
        if (const core::Status closed = services_.backend.close(handle); !closed) {
            RAWPAGE_LOG_WARNING(LogSubsystem::Render)
                << "compare: backend refused to close" << held.file.path << "--" << closed.error().messageKey;
        }
    }
}

void ComparisonView::buildBar()
{
    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("comparisonBar"));
    auto* rows = new QVBoxLayout(bar);
    rows->setContentsMargins(8, 6, 8, 6);
    rows->setSpacing(4);

    auto* row = new QHBoxLayout();
    row->setSpacing(10);

    modeChoice_ = new QComboBox(bar);
    modeChoice_->setObjectName(QStringLiteral("comparisonMode"));
    modeChoice_->addItem(catalog::translate(catalog::key::CompareViewSideBySide));
    modeChoice_->addItem(catalog::translate(catalog::key::CompareViewOverlay));
    connect(modeChoice_, &QComboBox::activated, this,
            [this](int index) { setMode(index == 1 ? Mode::Overlay : Mode::SideBySide); });
    row->addWidget(modeChoice_);

    // CONTINUOUS, an overlay's switch, shown only there (A-128).
    continuousBox_ = new QCheckBox(catalog::translate(catalog::key::CompareViewContinuous), bar);
    continuousBox_->setObjectName(QStringLiteral("comparisonContinuous"));
    continuousBox_->setVisible(false);
    connect(continuousBox_, &QCheckBox::clicked, this, [this](bool checked) { setOverlayContinuous(checked); });
    row->addWidget(continuousBox_);

    row->addWidget(new QLabel(catalog::translate(catalog::key::CompareDialogSensitivity), bar));
    sensitivityChoice_ = new QComboBox(bar);
    sensitivityChoice_->setObjectName(QStringLiteral("comparisonSensitivity"));
    sensitivityChoice_->addItem(catalog::translate(catalog::key::CompareSensitivityTextOnly));
    sensitivityChoice_->addItem(catalog::translate(catalog::key::CompareSensitivityTextAndLayout));
    sensitivityChoice_->addItem(catalog::translate(catalog::key::CompareSensitivityAppearance));
    connect(sensitivityChoice_, &QComboBox::activated, this, [this](int) { writeSetting(0); });
    row->addWidget(sensitivityChoice_);

    ignoreHeaderFooter_ = new QCheckBox(catalog::translate(catalog::key::CompareIgnoreHeaderFooter), bar);
    ignoreHeaderFooter_->setObjectName(QStringLiteral("comparisonIgnoreHeaderFooter"));
    ignorePageNumbers_ = new QCheckBox(catalog::translate(catalog::key::CompareIgnorePageNumbers), bar);
    ignorePageNumbers_->setObjectName(QStringLiteral("comparisonIgnorePageNumbers"));
    ignoreWhitespace_ = new QCheckBox(catalog::translate(catalog::key::CompareIgnoreWhitespace), bar);
    ignoreWhitespace_->setObjectName(QStringLiteral("comparisonIgnoreWhitespace"));
    connect(ignoreHeaderFooter_, &QCheckBox::clicked, this, [this] { writeSetting(1); });
    connect(ignorePageNumbers_, &QCheckBox::clicked, this, [this] { writeSetting(2); });
    connect(ignoreWhitespace_, &QCheckBox::clicked, this, [this] { writeSetting(3); });
    row->addWidget(ignoreHeaderFooter_);
    row->addWidget(ignorePageNumbers_);
    row->addWidget(ignoreWhitespace_);

    // THE STATUS TAKES THE SLACK AND NO WIDTH FROM ITS TEXT (Stage 5 guide I-11, the find bar's I-10 in this
    // window): a progress line that asked for its width would make the window's minimum a function of it.
    // It clips when the bar is narrow, and its tooltip holds the whole line.
    status_ = new QLabel(bar);
    status_->setObjectName(QStringLiteral("comparisonStatus"));
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    status_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row->addWidget(status_, 1);
    cancelButton_ = new QPushButton(catalog::translate(catalog::key::CompareViewCancel), bar);
    cancelButton_->setObjectName(QStringLiteral("comparisonCancel"));
    connect(cancelButton_, &QPushButton::clicked, this, &ComparisonView::cancel);
    row->addWidget(cancelButton_);
    rows->addLayout(row);

    legend_ = new QLabel(catalog::translate(catalog::key::CompareViewOverlayLegend), bar);
    legend_->setObjectName(QStringLiteral("comparisonLegend"));
    rows->addWidget(legend_);

    static_cast<QVBoxLayout*>(layout())->addWidget(bar);
    syncBarFromSettings();
}

// =============================================================================================================
// Accessors
// =============================================================================================================

PageView* ComparisonView::view(CompareSide side) const noexcept
{
    return side == CompareSide::Base ? older_ : newer_;
}

const QString& ComparisonView::path(CompareSide side) const noexcept
{
    return sides_[static_cast<std::size_t>(at(side))].file.path;
}

core::DocumentHandle ComparisonView::handle(CompareSide side) const noexcept
{
    return sides_[static_cast<std::size_t>(at(side))].file.handle;
}

QString ComparisonView::statusText() const
{
    return status_->text();
}

int ComparisonView::partnerOf(CompareSide side, int page) const
{
    const std::vector<int>& map = side == CompareSide::Base ? olderToNewer_ : newerToOlder_;
    if (result_ != nullptr) {
        return page >= 0 && page < static_cast<int>(map.size()) ? map[static_cast<std::size_t>(page)] : -1;
    }
    // BEFORE THE PAGES ARE ALIGNED, partners are by position (the header).
    const Held& otherSide = sides_[static_cast<std::size_t>(side == CompareSide::Base ? kNewer : kOlder)];
    const int count = otherSide.pages != nullptr ? otherSide.pages->count() : 0;
    return page >= 0 && page < count ? page : -1;
}

void ComparisonView::setStatus(const QString& text)
{
    status_->setText(text);
    status_->setToolTip(text);
    emit statusChanged(text);
}

// =============================================================================================================
// The text, the run and the settings
// =============================================================================================================

void ComparisonView::askForText()
{
    core::text::DocumentIndex& index = services_.documentIndex;
    if (owner_ == 0) {
        owner_ = index.newOwner();
        indexSubscription_ = index.subscribe([this](const core::text::DocumentIndex::Event& event) { onIndexEvent(event); });
        // BOTH ATTACHED BEFORE EITHER IS ASKED: a page already in the index is taken at once, and the run must
        // not start because one side looked complete before the other had a page count.
        for (Held& held : sides_) {
            const int count = held.pages != nullptr ? held.pages->count() : 0;
            held.entry = index.attach(services_.backend.documentId(held.file.handle), held.file.handle, count);
            held.text.assign(static_cast<std::size_t>(count), nullptr);
            held.textHeld = 0;
        }
    }
    state_ = State::Reading;
    cancelButton_->setVisible(true);
    onProgress(0, 0);
    for (const int side : {kOlder, kNewer}) {
        Held& held = sides_[static_cast<std::size_t>(side)];
        for (int page = 0; page < static_cast<int>(held.text.size()); ++page) {
            if (held.text[static_cast<std::size_t>(page)]) continue;
            if (index.request(owner_, held.entry, page, core::render::RenderPriority::Background))
                take(side == kOlder ? CompareSide::Base : CompareSide::Revised, page);
        }
    }
    take(CompareSide::Base, -1);   // nothing to take; starts the run when every page was already here
}

void ComparisonView::onIndexEvent(const core::text::DocumentIndex::Event& event)
{
    if (event.kind != core::text::DocumentIndex::Event::Kind::PageArrived) return;
    // BOTH SIDES MAY BE ONE ENTRY -- a file compared with itself, or two files the backend calls one.
    for (const int side : {kOlder, kNewer}) {
        if (sides_[static_cast<std::size_t>(side)].entry == event.entry)
            take(side == kOlder ? CompareSide::Base : CompareSide::Revised, event.page);
    }
}

void ComparisonView::take(CompareSide side, int page)
{
    Held& held = sides_[static_cast<std::size_t>(at(side))];
    if (page >= 0 && page < static_cast<int>(held.text.size()) && !held.text[static_cast<std::size_t>(page)]) {
        std::shared_ptr<const core::text::IndexedPage> indexed = services_.documentIndex.page(held.entry, page);
        if (!indexed) return;
        held.text[static_cast<std::size_t>(page)] = std::move(indexed);
        ++held.textHeld;
    }
    if (state_ != State::Reading) return;

    const int total = static_cast<int>(sides_[kOlder].text.size() + sides_[kNewer].text.size());
    const int done = sides_[kOlder].textHeld + sides_[kNewer].textHeld;
    if (done < total) {
        setStatus(catalog::translate(catalog::key::CompareStatusReading).arg(done).arg(total));
        pane_->setStatus(status_->text());
        return;
    }
    startRun();
}

void ComparisonView::startRun()
{
    stopRun();
    state_ = State::Comparing;
    cancelButton_->setVisible(true);

    // EACH PAGE'S FACTS FROM THE ENGINE'S TREE, ITS TEXT FROM THE INDEX: the reading is left null and made on
    // the run's thread (the header). The raster requests are built here, on the thread that owns the trees.
    std::array<core::text::CompareDocument, 2> documents;
    std::array<std::vector<core::render::RenderRequest>, 2> requests;
    for (const int side : {kOlder, kNewer}) {
        const Held& held = sides_[static_cast<std::size_t>(side)];
        const int count = held.pages != nullptr ? held.pages->count() : 0;
        for (int page = 0; page < count; ++page) {
            const core::Page* info = held.pages->at(page);
            core::text::ComparePage compared;
            core::render::RenderRequest request;
            if (info != nullptr) {
                compared.cropBox = info->cropBox();
                compared.rotation = info->rotation();
                compared.label = info->label();
                const core::PdfSize display = info->displaySize();
                request.document = held.file.handle;
                request.pageIndex = page;
                request.deviceScale = 1.0;   // 72 dpi (D5-17)
                request.revision = core::Revision::initial();
                request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(display.width)),
                                                      static_cast<int>(std::ceil(display.height))};
            }
            documents[static_cast<std::size_t>(side)].push_back(std::move(compared));
            requests[static_cast<std::size_t>(side)].push_back(request);
        }
    }

    core::text::RasterProvider rasters;
    if (options_.sensitivity == core::text::CompareSensitivity::Appearance) {
        bridge_ = std::make_shared<RasterBridge>(services_.renderScheduler, std::move(requests));
        rasters = [bridge = bridge_.get()](CompareSide side, int page) { return bridge->fetch(side, page); };
    }

    run_ = std::make_unique<core::text::ComparisonRun>(std::move(documents[kOlder]), std::move(documents[kNewer]),
                                                       options_, std::move(rasters));
    run_->setReadingSource(
        [older = sides_[kOlder].text, newer = sides_[kNewer].text](CompareSide side, int page)
            -> std::shared_ptr<const core::text::PageReading> {
            const auto& text = side == CompareSide::Base ? older : newer;
            if (page < 0 || page >= static_cast<int>(text.size()) || !text[static_cast<std::size_t>(page)])
                return nullptr;
            return text[static_cast<std::size_t>(page)]->reading();
        });
    run_->setProgressHandler([this](int done, int total) { onProgress(done, total); });
    run_->setFinishedHandler([this](const core::text::ComparisonResult& result) { onFinished(result); });
    // FROM THE RUN'S THREAD, carrying nothing: delivered on this one's event loop.
    run_->setWakeHandler([this] {
        QMetaObject::invokeMethod(this, [this] { if (run_) run_->deliverReady(); }, Qt::QueuedConnection);
    });
    onProgress(0, 0);
    run_->start();
}

void ComparisonView::stopRun()
{
    if (bridge_) bridge_->abort();
    run_.reset();
    bridge_.reset();
}

void ComparisonView::onProgress(int done, int total)
{
    if (state_ == State::Comparing) {
        setStatus(catalog::translate(catalog::key::CompareStatusComparing).arg(done).arg(total));
    } else if (state_ == State::Reading) {
        const int all = static_cast<int>(sides_[kOlder].text.size() + sides_[kNewer].text.size());
        setStatus(catalog::translate(catalog::key::CompareStatusReading)
                      .arg(sides_[kOlder].textHeld + sides_[kNewer].textHeld)
                      .arg(all));
    }
    if (result_ == nullptr) pane_->setStatus(status_->text());
}

void ComparisonView::onFinished(const core::text::ComparisonResult& result)
{
    cancelButton_->setVisible(false);
    if (result.cancelled) {
        state_ = State::Cancelled;
        setStatus(catalog::translate(catalog::key::CompareStatusCancelled));
        emit finished();
        return;
    }

    result_ = std::make_shared<const core::text::ComparisonResult>(result);
    state_ = State::Finished;

    // THE ALIGNMENT, both ways.
    olderToNewer_.assign(sides_[kOlder].text.size(), -1);
    newerToOlder_.assign(sides_[kNewer].text.size(), -1);
    for (const PageMatch& match : result_->pages) {
        if (match.base < 0 || match.revised < 0) continue;
        if (match.base < static_cast<int>(olderToNewer_.size())) olderToNewer_[static_cast<std::size_t>(match.base)] = match.revised;
        if (match.revised < static_cast<int>(newerToOlder_.size())) newerToOlder_[static_cast<std::size_t>(match.revised)] = match.base;
    }
    rebuildPairs();
    order(*result_);
    current_ = -1;
    pane_->setDifferences(ordered_);
    applyMarks();

    QString summary = ordered_.empty() ? catalog::translate(catalog::key::CompareStatusNone)
                                       : catalog::translate(catalog::key::CompareStatusCount).arg(ordered_.size());
    if (result_->rastersMissing > 0)
        summary += QLatin1Char(' ') + catalog::translate(catalog::key::CompareStatusRastersMissing).arg(result_->rastersMissing);
    setStatus(summary);

    // THE NEWER SIDE FOLLOWS THE OLDER, NOW BY ALIGNED PAGE.
    syncFrom(older_);
    if (mode_ == Mode::Overlay) {
        pair_ = pairs_.empty() ? -1 : 0;
        showOverlayPair(0, false);
    }
    emit finished();
}

void ComparisonView::cancel()
{
    if (state_ != State::Reading && state_ != State::Comparing) return;
    stopRun();
    if (owner_ != 0) services_.documentIndex.release(owner_);
    state_ = State::Cancelled;
    cancelButton_->setVisible(false);
    setStatus(catalog::translate(catalog::key::CompareStatusCancelled));
    if (result_ == nullptr) pane_->setStatus(status_->text());
    emit finished();
}

void ComparisonView::closeEvent(QCloseEvent* event)
{
    retired_ = true;
    stopRun();
    QWidget::closeEvent(event);
}

void ComparisonView::onSettingsChanged()
{
    if (retired_) return;
    const core::text::CompareOptions now = core::text::CompareOptions::fromSettings(services_.settings);
    syncBarFromSettings();
    if (now.sensitivity == options_.sensitivity && now.ignoreHeaderFooter == options_.ignoreHeaderFooter
        && now.ignorePageNumbers == options_.ignorePageNumbers && now.ignoreWhitespace == options_.ignoreWhitespace)
        return;
    options_ = now;

    // RE-RUN over the text already read; a comparison still reading takes the new options when it starts, and
    // one cancelled while reading asks again for what it lacks.
    const bool allRead = sides_[kOlder].textHeld == static_cast<int>(sides_[kOlder].text.size())
                         && sides_[kNewer].textHeld == static_cast<int>(sides_[kNewer].text.size());
    if (state_ == State::Reading) return;
    if (allRead) {
        startRun();
    } else {
        askForText();
    }
}

void ComparisonView::syncBarFromSettings()
{
    const QSignalBlocker a(sensitivityChoice_);
    const QSignalBlocker b(ignoreHeaderFooter_);
    const QSignalBlocker c(ignorePageNumbers_);
    const QSignalBlocker d(ignoreWhitespace_);
    const core::text::CompareOptions now = core::text::CompareOptions::fromSettings(services_.settings);
    sensitivityChoice_->setCurrentIndex(static_cast<int>(now.sensitivity));
    ignoreHeaderFooter_->setChecked(now.ignoreHeaderFooter);
    ignorePageNumbers_->setChecked(now.ignorePageNumbers);
    ignoreWhitespace_->setChecked(now.ignoreWhitespace);
}

void ComparisonView::writeSetting(int control)
{
    // THE BAR WRITES THE KEY AND NOTHING ELSE (the header): the subscription re-runs the comparison, exactly
    // as it does for a change made in Preferences.
    namespace keys = core::keys::documents;
    switch (control) {
    case 0: {
        const int index = sensitivityChoice_->currentIndex();
        services_.settings.set(keys::compareSensitivity,
                               index == 2   ? core::text::CompareSensitivity::Appearance
                               : index == 1 ? core::text::CompareSensitivity::TextAndLayout
                                            : core::text::CompareSensitivity::TextOnly);
        break;
    }
    case 1: services_.settings.set(keys::compareIgnoreHeaderFooter, ignoreHeaderFooter_->isChecked()); break;
    case 2: services_.settings.set(keys::compareIgnorePageNumbers, ignorePageNumbers_->isChecked()); break;
    case 3: services_.settings.set(keys::compareIgnoreWhitespace, ignoreWhitespace_->isChecked()); break;
    default: break;
    }
    // A key a policy locks does not move: the bar shows what is in force.
    syncBarFromSettings();
}

// =============================================================================================================
// The differences
// =============================================================================================================

void ComparisonView::order(const core::text::ComparisonResult& result)
{
    // THE ORDER `Ctrl+]` STEPS IN: by the aligned pair a difference is on -- the newer document's order, a
    // deleted page in its place -- then top to bottom on it, then as the comparer reported them. The comparer
    // reports page-level changes first (`ComparisonResult`), which would make the first step jump to a page
    // inserted halfway through.
    struct Keyed {
        int    pair = 0;
        double top = 0.0;
        int    original = 0;
    };
    std::vector<Keyed> keys;
    keys.reserve(result.differences.size());
    for (std::size_t i = 0; i < result.differences.size(); ++i) {
        const Difference& difference = result.differences[i];
        int pair = static_cast<int>(pairs_.size());
        for (std::size_t p = 0; p < pairs_.size(); ++p) {
            if ((difference.revisedPage >= 0 && pairs_[p].second == difference.revisedPage)
                || (difference.basePage >= 0 && pairs_[p].first == difference.basePage)) {
                pair = static_cast<int>(p);
                break;
            }
        }
        double top = 1e9;   // a change with no rectangle -- a whole page -- comes first on its pair
        const std::vector<core::PdfRect>& rects = !difference.revisedRects.empty() ? difference.revisedRects : difference.baseRects;
        if (!rects.empty()) top = std::max_element(rects.begin(), rects.end(), [](const core::PdfRect& a, const core::PdfRect& b) {
                                      return a.top() < b.top();
                                  })->top();
        keys.push_back(Keyed{pair, top, static_cast<int>(i)});
    }
    std::stable_sort(keys.begin(), keys.end(), [](const Keyed& a, const Keyed& b) {
        if (a.pair != b.pair) return a.pair < b.pair;
        return a.top > b.top;   // PDF space: a larger y is higher on the page
    });
    ordered_.clear();
    ordered_.reserve(keys.size());
    for (const Keyed& key : keys) ordered_.push_back(result.differences[static_cast<std::size_t>(key.original)]);
}

std::vector<core::PdfRect> ComparisonView::marksOn(const Difference& difference, CompareSide side) const
{
    const int page = side == CompareSide::Base ? difference.basePage : difference.revisedPage;
    if (page < 0) return {};
    const std::vector<core::PdfRect>& rects = side == CompareSide::Base ? difference.baseRects : difference.revisedRects;
    if (!rects.empty()) return rects;

    // A PAGE-LEVEL CHANGE IS THE WHOLE PAGE on the side it has a page on.
    switch (difference.kind) {
    case DifferenceKind::PageInserted:
    case DifferenceKind::PageDeleted:
    case DifferenceKind::PageMoved:
    case DifferenceKind::PageReplaced: {
        const Held& held = sides_[static_cast<std::size_t>(at(side))];
        if (const core::Page* info = held.pages != nullptr ? held.pages->at(page) : nullptr) return {info->cropBox()};
        return {};
    }
    default:
        return {};
    }
}

void ComparisonView::applyMarks()
{
    std::array<std::vector<PageView::PageMark>, 2> marks;
    for (std::size_t i = 0; i < ordered_.size(); ++i) {
        const bool current = static_cast<int>(i) == current_;
        for (const int side : {kOlder, kNewer}) {
            const CompareSide which = side == kOlder ? CompareSide::Base : CompareSide::Revised;
            const int page = side == kOlder ? ordered_[i].basePage : ordered_[i].revisedPage;
            for (const core::PdfRect& rect : marksOn(ordered_[i], which))
                marks[static_cast<std::size_t>(side)].push_back(PageView::PageMark{page, rect, tint(side, current), current});
        }
    }
    older_->setPageMarks(std::move(marks[kOlder]));
    newer_->setPageMarks(std::move(marks[kNewer]));
    canvas_->update();
}

void ComparisonView::nextDifference()
{
    if (ordered_.empty()) return;
    const int count = static_cast<int>(ordered_.size());
    if (current_ + 1 >= count && current_ >= 0) {
        goToDifference(0);
        setStatus(catalog::translate(catalog::key::CompareStatusWrappedToFirst));
        return;
    }
    goToDifference(current_ + 1);
}

void ComparisonView::previousDifference()
{
    if (ordered_.empty()) return;
    const int count = static_cast<int>(ordered_.size());
    if (current_ == 0) {
        goToDifference(count - 1);
        setStatus(catalog::translate(catalog::key::CompareStatusWrappedToLast));
        return;
    }
    goToDifference(current_ < 0 ? count - 1 : current_ - 1);
}

void ComparisonView::goToDifference(int index)
{
    if (index < 0 || index >= static_cast<int>(ordered_.size())) return;
    current_ = index;
    const Difference& difference = ordered_[static_cast<std::size_t>(index)];
    applyMarks();
    pane_->setCurrent(index);
    setStatus(catalog::translate(catalog::key::CompareStatusCurrent)
                  .arg(index + 1)
                  .arg(ordered_.size())
                  .arg(DifferencesPane::kindText(difference)));

    // BOTH VIEWS GO TO IT: each side that has the change shows it; a side without -- the older one of an
    // insertion -- follows by aligned page from the side that has it.
    const std::vector<core::PdfRect> olderMarks = marksOn(difference, CompareSide::Base);
    const std::vector<core::PdfRect> newerMarks = marksOn(difference, CompareSide::Revised);
    const auto unite = [](const std::vector<core::PdfRect>& rects) {
        core::PdfRect all = rects.front();
        for (const core::PdfRect& rect : rects) {
            const double left = std::min(all.left(), rect.left());
            const double bottom = std::min(all.bottom(), rect.bottom());
            const double right = std::max(all.right(), rect.right());
            const double top = std::max(all.top(), rect.top());
            all = core::PdfRect{core::PdfPoint{left, bottom}, core::PdfSize{right - left, top - bottom}};
        }
        return all;
    };

    if (mode_ == Mode::Overlay) {
        for (std::size_t p = 0; p < pairs_.size(); ++p) {
            if ((difference.revisedPage >= 0 && pairs_[p].second == difference.revisedPage)
                || (difference.basePage >= 0 && pairs_[p].first == difference.basePage)) {
                // THE PAIR, AND THE CHANGE IN VIEW on it -- at a zoom the top of the page may not show it.
                showOverlayPair(static_cast<int>(p), false);
                revealOnOverlay(difference);
                aimOverlayRegion(false);
                break;
            }
        }
    } else {
        syncing_ = true;
        if (!olderMarks.empty()) older_->revealPageRect(difference.basePage, unite(olderMarks));
        if (!newerMarks.empty()) newer_->revealPageRect(difference.revisedPage, unite(newerMarks));
        syncing_ = false;
        if (olderMarks.empty() && !newerMarks.empty()) syncFrom(newer_);
        else if (newerMarks.empty() && !olderMarks.empty()) syncFrom(older_);
    }
    canvas_->update();
    emit currentDifferenceChanged(index);
}

// =============================================================================================================
// Side by side, aligned
// =============================================================================================================

PageView* ComparisonView::other(PageView* view) const
{
    return view == older_ ? newer_ : older_;
}

CompareSide ComparisonView::sideOf(const PageView* view) const
{
    return view == older_ ? CompareSide::Base : CompareSide::Revised;
}

void ComparisonView::syncFrom(PageView* leader)
{
    if (syncing_ || leader == nullptr || mode_ != Mode::SideBySide) return;
    PageView* follower = other(leader);
    const core::render::PageLayout& from = leader->layoutEngine().layout();
    if (from.isEmpty() || follower->layoutEngine().layout().isEmpty()) return;

    syncing_ = true;

    // THE ZOOM FIRST: the same fit, or the same factor, so the partner's place is measured at the leader's scale.
    if (leader->controller().fitMode() != follower->controller().fitMode()) {
        follower->setFitMode(leader->controller().fitMode());
    } else if (leader->controller().fitMode() == core::render::FitMode::None
               && std::abs(leader->controller().zoom().factor - follower->controller().zoom().factor) > 1e-6) {
        follower->setZoom(leader->controller().zoom());
    }

    // THE PAGE AT THE LEADER'S TOP EDGE, and how far down it that edge is.
    const core::DeviceRect visible = leader->controller().visibleRect();
    const core::render::PageSlot* top = nullptr;
    for (const core::render::PageSlot& slot : from.pageSlots()) {
        if (slot.rect.bottom() > visible.top()) {
            top = &slot;
            break;
        }
    }
    if (top == nullptr) top = &from.pageSlots().back();
    const double height = std::max(1.0, top->rect.size.height);
    double fraction = (visible.top() - top->rect.top()) / height;
    const double gap = fraction < 0.0 ? visible.top() - top->rect.top() : 0.0;   // in the margin above a page
    fraction = std::clamp(fraction, 0.0, 1.0);

    // ITS PARTNER, or -- for a page with none -- the top of the next page that has one.
    const CompareSide side = sideOf(leader);
    int partner = partnerOf(side, top->pageIndex);
    if (partner < 0) {
        fraction = 0.0;
        const int count = static_cast<int>(sides_[static_cast<std::size_t>(at(side))].text.size());
        for (int page = top->pageIndex + 1; page < count && partner < 0; ++page) partner = partnerOf(side, page);
        if (partner < 0) {
            for (int page = top->pageIndex - 1; page >= 0 && partner < 0; --page) partner = partnerOf(side, page);
            fraction = 1.0;
        }
    }

    const core::render::PageLayout& to = follower->layoutEngine().layout();
    if (const core::render::PageSlot* slot = partner >= 0 ? to.slotForPage(partner) : nullptr) {
        const double y = slot->rect.top() + fraction * slot->rect.size.height + gap;
        follower->controller().setScroll(core::DevicePoint{leader->controller().scroll().x, y});
    }
    syncing_ = false;
}

// =============================================================================================================
// The overlay
// =============================================================================================================

void ComparisonView::setMode(Mode mode)
{
    if (mode_ == mode) return;
    mode_ = mode;
    {
        const QSignalBlocker blocker(modeChoice_);
        modeChoice_->setCurrentIndex(mode == Mode::Overlay ? 1 : 0);
    }
    stack_->setCurrentIndex(mode == Mode::Overlay ? 1 : 0);
    continuousBox_->setVisible(mode == Mode::Overlay);
    if (mode == Mode::Overlay) {
        // THE PAIR OF THE CURRENT DIFFERENCE, or of the page at the older view's top.
        int pair = -1;
        if (current_ >= 0) {
            const Difference& difference = ordered_[static_cast<std::size_t>(current_)];
            for (std::size_t p = 0; p < pairs_.size() && pair < 0; ++p)
                if ((difference.revisedPage >= 0 && pairs_[p].second == difference.revisedPage)
                    || (difference.basePage >= 0 && pairs_[p].first == difference.basePage))
                    pair = static_cast<int>(p);
        }
        if (pair < 0) {
            const int page = older_->controller().currentPage();
            for (std::size_t p = 0; p < pairs_.size() && pair < 0; ++p)
                if (pairs_[p].first == page) pair = static_cast<int>(p);
        }
        canvas_->setFocus(Qt::OtherFocusReason);
        showOverlayPair(pair >= 0 ? pair : 0, false);
        if (current_ >= 0) {
            revealOnOverlay(ordered_[static_cast<std::size_t>(current_)]);
            aimOverlayRegion(false);
        }
    } else {
        slots_.clear();
        older_->setFocus(Qt::OtherFocusReason);
        syncFrom(older_);
    }
}

void ComparisonView::rebuildPairs()
{
    pairs_.clear();
    if (result_ != nullptr) {
        for (const PageMatch& match : result_->pages) pairs_.emplace_back(match.base, match.revised);
    } else {
        // BY POSITION until the pages are aligned.
        const int olderCount = sides_[kOlder].pages != nullptr ? sides_[kOlder].pages->count() : 0;
        const int newerCount = sides_[kNewer].pages != nullptr ? sides_[kNewer].pages->count() : 0;
        for (int page = 0; page < std::max(olderCount, newerCount); ++page)
            pairs_.emplace_back(page < olderCount ? page : -1, page < newerCount ? page : -1);
    }
    if (pair_ >= static_cast<int>(pairs_.size())) pair_ = pairs_.empty() ? -1 : 0;
    // A SLOT IS A PAIR BY INDEX, and the indices just changed.
    slots_.clear();
    pairRects_.clear();
}

void ComparisonView::setOverlayPair(int index)
{
    showOverlayPair(index, false);
}

void ComparisonView::showOverlayPair(int index, bool atBottom)
{
    if (pairs_.empty()) return;
    pair_ = std::clamp(index, 0, static_cast<int>(pairs_.size()) - 1);
    overscroll_ = 0;
    aimOverlay(false);
    // ITS TOP, OR ITS BOTTOM: of the canvas when it is the one pair laid out, of its own place when every pair is.
    QScrollBar* bar = overlayScroll_->verticalScrollBar();
    holdingPair_ = true;
    if (continuous_) {
        const QRectF rect = overlayPairRect(pair_);
        const double viewport = overlayScroll_->viewport()->height();
        bar->setValue(static_cast<int>(std::lround(atBottom ? rect.bottom() + kOverlayMargin - viewport
                                                            : rect.top() - kOverlayMargin)));
    } else {
        bar->setValue(atBottom ? bar->maximum() : bar->minimum());
    }
    holdingPair_ = false;
    aimOverlayRegion(true);
}

void ComparisonView::setOverlayContinuous(bool continuous)
{
    if (continuous_ == continuous) return;
    continuous_ = continuous;
    {
        const QSignalBlocker blocker(continuousBox_);
        continuousBox_->setChecked(continuous);
    }
    // THE SAME PAIR, AT THE TOP, either way.
    slots_.clear();
    if (mode_ == Mode::Overlay) showOverlayPair(pair_ < 0 ? 0 : pair_, false);
}

const QImage& ComparisonView::overlayImage() const
{
    static const QImage none;
    const auto slot = slots_.find(pair_);
    return slot != slots_.end() ? slot->second->composite : none;
}

QPoint ComparisonView::overlayOrigin() const
{
    const auto slot = slots_.find(pair_);
    return slot != slots_.end() ? slot->second->origin : QPoint();
}

QRectF ComparisonView::overlayPageRect() const
{
    return overlayPairRect(pair_);
}

QRectF ComparisonView::overlayPairRect(int index) const
{
    return index >= 0 && index < static_cast<int>(pairRects_.size()) ? pairRects_[static_cast<std::size_t>(index)]
                                                                       : QRectF();
}

int ComparisonView::pairAtTop() const
{
    // THE FIRST PAIR STILL BELOW THE VIEW'S TOP EDGE, past the margin a pair is shown from.
    const double top = overlayScroll_->verticalScrollBar()->value() + kOverlayMargin + 1.0;
    int last = pair_;
    for (std::size_t p = 0; p < pairRects_.size(); ++p) {
        if (pairRects_[p].isEmpty()) continue;
        if (pairRects_[p].bottom() >= top) return static_cast<int>(p);
        last = static_cast<int>(p);
    }
    return last;
}

int ComparisonView::pairAt(QPointF point) const
{
    int last = pair_;
    for (std::size_t p = 0; p < pairRects_.size(); ++p) {
        if (pairRects_[p].isEmpty()) continue;
        if (pairRects_[p].bottom() >= point.y()) return static_cast<int>(p);
        last = static_cast<int>(p);
    }
    return last;
}

core::render::Zoom ComparisonView::overlayZoom() const
{
    if (overlayFit_ == core::render::FitMode::None || overlayScale_ <= 0.0) return overlayZoom_;
    return core::render::Zoom{overlayScale_ / canvas_->ratio() / kActualSizePerPoint};
}

void ComparisonView::setOverlayFit(core::render::FitMode fit)
{
    overlayFit_ = fit == core::render::FitMode::Width ? fit : core::render::FitMode::Page;
    if (continuous_) showOverlayPair(pair_, false);   // the same pair at the top, as the layout moved under it
    else aimOverlay();
}

void ComparisonView::setOverlayZoom(core::render::Zoom zoom, std::optional<QPointF> anchor)
{
    // THE POINT UNDER THE ANCHOR STAYS UNDER IT: its place on the pair it is over, in points, before the scale
    // changes -- the current pair, or continuous, whichever pair is there.
    QScrollBar* across = overlayScroll_->horizontalScrollBar();
    QScrollBar* down = overlayScroll_->verticalScrollBar();
    const QSize viewport = overlayScroll_->viewport()->size();
    const QPointF at = anchor.value_or(
        QPointF(across->value() + viewport.width() / 2.0, down->value() + viewport.height() / 2.0));
    const QPointF inViewport = at - QPointF(across->value(), down->value());
    const int under = pairAt(at);
    const double before = overlayScale_ > 0.0 ? overlayScale_ / canvas_->ratio() : 1.0;
    const QPointF onPage = (at - overlayPairRect(under).topLeft()) / before;

    overlayFit_ = core::render::FitMode::None;
    overlayZoom_ = zoom.clamped();
    aimOverlay(false);
    const double after = overlayScale_ > 0.0 ? overlayScale_ / canvas_->ratio() : 1.0;
    const QPointF moved = overlayPairRect(under).topLeft() + onPage * after - inViewport;
    holdingPair_ = true;
    across->setValue(static_cast<int>(std::lround(moved.x())));
    down->setValue(static_cast<int>(std::lround(moved.y())));
    holdingPair_ = false;
    if (continuous_) pair_ = pairAtTop();
    aimOverlayRegion(true);
}

void ComparisonView::zoomOverlay(int direction, std::optional<QPointF> anchor)
{
    setOverlayZoom(ViewportController::nextZoomStep(overlayZoom(), direction), anchor);
}

void ComparisonView::aimOverlay(bool request)
{
    if (mode_ != Mode::Overlay || pair_ < 0 || pair_ >= static_cast<int>(pairs_.size())) return;

    // EACH PAIR'S SIZE, in points: the bigger of its two pages, each way.
    const auto sizeOf = [this](std::size_t p) {
        const auto [olderPage, newerPage] = pairs_[p];
        QSizeF size;
        for (const auto& [side, page] : {std::pair<int, int>{kOlder, olderPage}, std::pair<int, int>{kNewer, newerPage}}) {
            const Held& held = sides_[static_cast<std::size_t>(side)];
            const core::Page* info = page >= 0 && held.pages != nullptr ? held.pages->at(page) : nullptr;
            if (info == nullptr) continue;
            size = size.expandedTo(QSizeF(info->displaySize().width, info->displaySize().height));
        }
        return size;
    };
    std::vector<std::size_t> laid;
    if (continuous_) {
        for (std::size_t p = 0; p < pairs_.size(); ++p) laid.push_back(p);
    } else {
        laid.push_back(static_cast<std::size_t>(pair_));
    }

    // ONE SCALE FOR EVERY PAIR, so a pixel of a composite is the same point of both pages: the fit's, over the
    // pair shown or, continuous, the biggest of each way; or the zoom's.
    QSizeF fitTo;
    for (const std::size_t p : laid) fitTo = fitTo.expandedTo(sizeOf(p));
    if (fitTo.width() <= 0.0 || fitTo.height() <= 0.0) return;
    const QSize viewport = overlayScroll_->viewport()->size();
    const double availableWidth = std::max(60.0, viewport.width() - 2.0 * kOverlayMargin);
    const double availableHeight = std::max(60.0, viewport.height() - 2.0 * kOverlayMargin);
    double perPoint = 0.0;   // logical pixels
    switch (overlayFit_) {
    case core::render::FitMode::Width: perPoint = availableWidth / fitTo.width(); break;
    case core::render::FitMode::None: perPoint = overlayZoom_.clamped().factor * kActualSizePerPoint; break;
    default: perPoint = std::min(availableWidth / fitTo.width(), availableHeight / fitTo.height()); break;
    }
    overlayScale_ = perPoint * canvas_->ratio();

    // THE LAYOUT: each pair centred across, one under another, `kOverlayGap` apart -- and the canvas the pairs and
    // their margins, never smaller than the viewport, so a page smaller than the window is centred on its
    // background rather than at a corner.
    const int canvasWidth =
        std::max(viewport.width(), static_cast<int>(std::ceil(fitTo.width() * perPoint)) + 2 * kOverlayMargin);
    pairRects_.assign(pairs_.size(), QRectF());
    double y = kOverlayMargin;
    for (const std::size_t p : laid) {
        const QSizeF size = sizeOf(p) * perPoint;
        if (size.isEmpty()) continue;
        pairRects_[p] = QRectF(std::max<double>(kOverlayMargin, (canvasWidth - size.width()) / 2.0), y, size.width(),
                               size.height());
        y += size.height() + kOverlayGap;
    }
    const int canvasHeight =
        std::max(viewport.height(), static_cast<int>(std::ceil(y - kOverlayGap)) + kOverlayMargin);
    if (canvas_->size() != QSize(canvasWidth, canvasHeight)) canvas_->resize(canvasWidth, canvasHeight);
    canvas_->update();
    if (request) aimOverlayRegion(true);
}

void ComparisonView::aimOverlayRegion(bool force)
{
    if (mode_ != Mode::Overlay || overlayScale_ <= 0.0 || pairs_.empty()) return;

    // WHAT IS IN VIEW, on the canvas -- and what is asked for: that, with half a viewport more on every side, so
    // a short scroll shows pixels already drawn and asks for nothing.
    const double dpr = canvas_->ratio();
    const QPointF scrolled(overlayScroll_->horizontalScrollBar()->value(), overlayScroll_->verticalScrollBar()->value());
    const QRectF visible(scrolled, QSizeF(overlayScroll_->viewport()->size()));
    const QRectF wanted = visible.adjusted(-visible.width() / 2.0, -visible.height() / 2.0, visible.width() / 2.0,
                                           visible.height() / 2.0);
    const auto toDevice = [dpr](const QRectF& r) {
        if (r.isEmpty()) return QRect();
        const QPoint topLeft(static_cast<int>(std::floor(r.left() * dpr)), static_cast<int>(std::floor(r.top() * dpr)));
        const QPoint bottomRight(static_cast<int>(std::ceil(r.right() * dpr)), static_cast<int>(std::ceil(r.bottom() * dpr)));
        return QRect(topLeft, QSize(bottomRight.x() - topLeft.x(), bottomRight.y() - topLeft.y()));
    };

    // THE PAIRS NEAR THE VIEW keep or get a slot; the others' are dropped, their rasters with them.
    std::vector<int> near;
    for (std::size_t p = 0; p < pairRects_.size() && p < pairs_.size(); ++p)
        if (!pairRects_[p].isEmpty() && pairRects_[p].intersects(wanted)) near.push_back(static_cast<int>(p));
    for (auto it = slots_.begin(); it != slots_.end();) {
        if (std::find(near.begin(), near.end(), it->first) == near.end()) it = slots_.erase(it);
        else ++it;
    }

    for (const int p : near) {
        const QRectF rect = pairRects_[static_cast<std::size_t>(p)];
        const QRect whole(0, 0, static_cast<int>(std::ceil(rect.width() * dpr)), static_cast<int>(std::ceil(rect.height() * dpr)));
        const QRect inView = toDevice(visible.intersected(rect).translated(-rect.topLeft())) & whole;
        std::unique_ptr<OverlaySlot>& held = slots_[p];
        if (!held) {
            held = std::make_unique<OverlaySlot>();
            held->older = std::make_unique<LoupeRaster>(services_.renderScheduler);
            held->newer = std::make_unique<LoupeRaster>(services_.renderScheduler);
            for (LoupeRaster* raster : {held->older.get(), held->newer.get()}) {
                connect(raster, &LoupeRaster::arrived, this, [this, p] { composite(p); });
                connect(raster, &LoupeRaster::failed, this, [this, p] { composite(p); });
            }
        }
        OverlaySlot& slot = *held;
        if (!force && !slot.region.isEmpty() && (inView.isEmpty() || slot.region.contains(inView))) continue;
        slot.region = toDevice(wanted.intersected(rect).translated(-rect.topLeft())) & whole;

        // EACH SIDE'S PART OF IT -- the region clipped to that page, which may be the smaller; a side with none of
        // it is an invalid request, and white.
        const auto [olderPage, newerPage] = pairs_[static_cast<std::size_t>(p)];
        const auto requestFor = [this, &slot](int side, int pageIndex) {
            core::render::RenderRequest request;
            const Held& sideHeld = sides_[static_cast<std::size_t>(side)];
            const core::Page* info = pageIndex >= 0 && sideHeld.pages != nullptr ? sideHeld.pages->at(pageIndex) : nullptr;
            if (info == nullptr) return request;
            const core::PdfSize display = info->displaySize();
            const QRect own = slot.region & QRect(0, 0, static_cast<int>(std::ceil(display.width * overlayScale_)),
                                                  static_cast<int>(std::ceil(display.height * overlayScale_)));
            if (own.isEmpty()) return request;
            request.document = sideHeld.file.handle;
            request.pageIndex = pageIndex;
            request.deviceScale = overlayScale_;
            request.revision = core::Revision::initial();
            request.tile = core::render::TileRect{own.x(), own.y(), own.width(), own.height()};
            return request;
        };
        slot.olderRequest = requestFor(kOlder, olderPage);
        slot.newerRequest = requestFor(kNewer, newerPage);
        slot.older->aim(slot.olderRequest);
        slot.newer->aim(slot.newerRequest);
        composite(p);
    }
}

void ComparisonView::composite(int pair)
{
    const auto found = slots_.find(pair);
    if (found == slots_.end()) return;
    OverlaySlot& slot = *found->second;

    // EACH SIDE ONTO WHITE -- a missing side IS white -- then per pixel (newer, min(older, newer), older) in
    // grey levels: shared ink grey, older-only red, newer-only blue (the header). Both parts start at the
    // region's top-left, since each is the region clipped at its right and bottom only.
    const bool wantOlder = slot.olderRequest.isValid();
    const bool wantNewer = slot.newerRequest.isValid();
    const QImage* olderImage = wantOlder ? slot.older->imageFor(slot.olderRequest) : nullptr;
    const QImage* newerImage = wantNewer ? slot.newer->imageFor(slot.newerRequest) : nullptr;
    if ((wantOlder && olderImage == nullptr) || (wantNewer && newerImage == nullptr)) return;

    const int width = slot.region.width();
    const int height = slot.region.height();
    if (width <= 0 || height <= 0) return;

    const auto onWhite = [width, height](const QImage* image) {
        QImage out(width, height, QImage::Format_RGB32);
        out.fill(Qt::white);
        if (image != nullptr) {
            QPainter painter(&out);
            painter.drawImage(QPoint(0, 0), *image);
        }
        return out;
    };
    const QImage older = onWhite(olderImage);
    const QImage newer = onWhite(newerImage);

    QImage out(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        const auto* o = reinterpret_cast<const QRgb*>(older.constScanLine(y));
        const auto* n = reinterpret_cast<const QRgb*>(newer.constScanLine(y));
        auto* d = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int a = qGray(o[x]);
            const int b = qGray(n[x]);
            d[x] = qRgb(b, std::min(a, b), a);
        }
    }
    out.setDevicePixelRatio(canvas_->ratio());
    slot.composite = std::move(out);
    slot.origin = slot.region.topLeft();
    canvas_->update();
    emit overlayChanged();
}

void ComparisonView::revealOnOverlay(const Difference& difference)
{
    QRectF marks;
    for (std::size_t p = 0; p < pairRects_.size() && p < pairs_.size(); ++p) {
        if (pairRects_[p].isEmpty()) continue;
        const auto [olderPage, newerPage] = pairs_[p];
        for (const int side : {kOlder, kNewer}) {
            const int pageIndex = side == kOlder ? olderPage : newerPage;
            if (pageIndex < 0 || pageIndex != (side == kOlder ? difference.basePage : difference.revisedPage)) continue;
            const core::Page* info = sides_[static_cast<std::size_t>(side)].pages->at(pageIndex);
            if (info == nullptr) continue;
            for (const core::PdfRect& rect : marksOn(difference, side == kOlder ? CompareSide::Base : CompareSide::Revised))
                marks |= canvas_->onCanvas(static_cast<int>(p), *info, rect);
        }
    }
    if (marks.isEmpty()) return;
    // A SCROLL OF OURS: the pair the difference is on stays the current one, though the reveal may bring the end
    // of the one before it into view.
    const QPointF centre = marks.center();
    holdingPair_ = true;
    overlayScroll_->ensureVisible(static_cast<int>(centre.x()), static_cast<int>(centre.y()),
                                  static_cast<int>(marks.width() / 2.0) + 40, static_cast<int>(marks.height() / 2.0) + 40);
    holdingPair_ = false;
}

void ComparisonView::scrollOverlay(int pixels)
{
    if (pixels == 0) return;
    QScrollBar* bar = overlayScroll_->verticalScrollBar();
    const bool down = pixels > 0;
    const bool atEdge = down ? bar->value() >= bar->maximum() : bar->value() <= bar->minimum();
    if (!atEdge) {
        bar->setValue(bar->value() + pixels);
        return;
    }
    // CONTINUOUS, AN EDGE IS THE DOCUMENT'S: there is no pair to turn to.
    if (continuous_) return;
    const int next = pair_ + (down ? 1 : -1);
    if (next >= 0 && next < static_cast<int>(pairs_.size())) showOverlayPair(next, !down);
}

void ComparisonView::overlayWheel(QWheelEvent* event)
{
    event->accept();
    const QPoint angle = event->angleDelta();
    if (event->modifiers() & Qt::ControlModifier) {
        // THE LADDER, A RUNG A NOTCH, about the cursor.
        wheelZoom_ += angle.y();
        for (; wheelZoom_ >= kWheelNotch; wheelZoom_ -= kWheelNotch) zoomOverlay(+1, event->position());
        for (; wheelZoom_ <= -kWheelNotch; wheelZoom_ += kWheelNotch) zoomOverlay(-1, event->position());
        return;
    }
    int across = angle.x();
    int down = angle.y();
    if ((event->modifiers() & Qt::ShiftModifier) && across == 0) std::swap(across, down);
    QScrollBar* horizontal = overlayScroll_->horizontalScrollBar();
    QScrollBar* vertical = overlayScroll_->verticalScrollBar();
    const int lines = QApplication::wheelScrollLines();
    if (across != 0) horizontal->setValue(horizontal->value() - across * lines * horizontal->singleStep() / kWheelNotch);
    if (down == 0) return;

    // DOWN IS A NEGATIVE ANGLE. Inside the pair the wheel scrolls; AT an edge it turns only once a whole notch
    // has gone past it, counted across events -- so the notch that reaches the end does not also turn, and a
    // touchpad's small deltas add up to one turn rather than one each. Continuous, it only scrolls.
    const bool towardBottom = down < 0;
    const bool atEdge = towardBottom ? vertical->value() >= vertical->maximum() : vertical->value() <= vertical->minimum();
    if (!atEdge) {
        overscroll_ = 0;
        vertical->setValue(vertical->value() - down * lines * vertical->singleStep() / kWheelNotch);
        return;
    }
    if (continuous_) return;
    if ((overscroll_ < 0) != (down < 0)) overscroll_ = 0;
    overscroll_ += down;
    if (std::abs(overscroll_) < kWheelNotch) return;
    overscroll_ = 0;
    const int next = pair_ + (towardBottom ? 1 : -1);
    if (next >= 0 && next < static_cast<int>(pairs_.size())) showOverlayPair(next, !towardBottom);
}

// =============================================================================================================
// The keys (the header)
// =============================================================================================================

bool ComparisonView::shellTakes(const QKeyEvent* event) const
{
    for (const core::Chord& chord : ShortcutBinder::chordCandidatesFor(event)) {
        const std::optional<core::KeyBinding> binding = services_.keyBindings.resolve(chord, core::KeyContext::Viewer);
        if (!binding) continue;
        if (binding->action == core::ActionId::NavNextDifference || binding->action == core::ActionId::NavPrevDifference
            || binding->action == core::ActionId::CompareDocuments)
            return true;
        return binding->context == core::KeyContext::Global && binding->kind != core::BindingKind::WidgetNative;
    }
    return false;
}

bool ComparisonView::event(QEvent* event)
{
    // FIRST REFUSAL, from wherever the focus is in this window -- the probe travels up to here unless a child
    // took it (`ShortcutBinder::resolveKey`). Accepted means the key is delivered to the focused widget and
    // never dispatched.
    if (event->type() == QEvent::ShortcutOverride) {
        if (!shellTakes(static_cast<QKeyEvent*>(event))) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

bool ComparisonView::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Resize && overlayScroll_ != nullptr && watched == overlayScroll_->viewport())
        aimOverlay();
    if (event->type() == QEvent::KeyPress && (watched == older_ || watched == newer_ || watched == canvas_)) {
        if (handleKey(watched, static_cast<QKeyEvent*>(event))) {
            event->accept();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool ComparisonView::handleKey(QObject* watched, QKeyEvent* event)
{
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    const bool plain = modifiers == Qt::NoModifier;
    const bool ctrl = modifiers == Qt::ControlModifier;
    const bool shift = modifiers == Qt::ShiftModifier;
    const int key = event->key();

    if (watched == canvas_) {
        // THE OVERLAY'S OWN ZOOM (I-24): the side views' chords, on the overlay.
        if (ctrl && (key == Qt::Key_Equal || key == Qt::Key_Plus)) { zoomOverlay(+1); return true; }
        if (ctrl && key == Qt::Key_Minus) { zoomOverlay(-1); return true; }
        if (ctrl && key == Qt::Key_0) { setOverlayFit(core::render::FitMode::Page); return true; }
        if (ctrl && key == Qt::Key_1) { setOverlayZoom(core::render::Zoom::actualSize()); return true; }
        if (ctrl && key == Qt::Key_2) { setOverlayFit(core::render::FitMode::Width); return true; }

        // SCROLL, AND TURN AT THE EDGE; `Ctrl` with a page or arrow key, `Home` and `End` turn directly.
        QScrollBar* vertical = overlayScroll_->verticalScrollBar();
        QScrollBar* horizontal = overlayScroll_->horizontalScrollBar();
        const int line = QApplication::wheelScrollLines() * vertical->singleStep();
        if (ctrl && (key == Qt::Key_PageDown || key == Qt::Key_Down)) { showOverlayPair(pair_ + 1, false); return true; }
        if (ctrl && (key == Qt::Key_PageUp || key == Qt::Key_Up)) { showOverlayPair(pair_ - 1, false); return true; }
        if (plain && key == Qt::Key_Down) { scrollOverlay(line); return true; }
        if (plain && key == Qt::Key_Up) { scrollOverlay(-line); return true; }
        if (plain && (key == Qt::Key_PageDown || key == Qt::Key_Space)) { scrollOverlay(vertical->pageStep()); return true; }
        if ((plain && key == Qt::Key_PageUp) || (shift && key == Qt::Key_Space)) { scrollOverlay(-vertical->pageStep()); return true; }
        // LEFT AND RIGHT SCROLL ACROSS a page wider than the window, and turn where there is nothing to scroll.
        if ((plain || ctrl) && (key == Qt::Key_Right || key == Qt::Key_Left)) {
            const bool right = key == Qt::Key_Right;
            if (plain && horizontal->maximum() > horizontal->minimum()) {
                horizontal->setValue(horizontal->value() + (right ? line : -line));
                return true;
            }
            showOverlayPair(pair_ + (right ? 1 : -1), false);
            return true;
        }
        if ((plain || ctrl) && key == Qt::Key_Home) { showOverlayPair(0, false); return true; }
        if ((plain || ctrl) && key == Qt::Key_End) { showOverlayPair(static_cast<int>(pairs_.size()) - 1, false); return true; }
        return false;
    }

    if (ctrl && (key == Qt::Key_Equal || key == Qt::Key_Plus)) { zoomBoth(1); return true; }
    if (ctrl && key == Qt::Key_Minus) { zoomBoth(-1); return true; }
    if (ctrl && key == Qt::Key_0) { fitBoth(0); return true; }
    if (ctrl && key == Qt::Key_1) { fitBoth(2); return true; }
    if (ctrl && key == Qt::Key_2) { fitBoth(1); return true; }

    auto* view = static_cast<PageView*>(watched);
    if ((plain || ctrl) && key == Qt::Key_Home) { view->firstPage(); return true; }
    if ((plain || ctrl) && key == Qt::Key_End) { view->lastPage(); return true; }
    if (ctrl && key == Qt::Key_PageDown) { view->nextPage(); return true; }
    if (ctrl && key == Qt::Key_PageUp) { view->prevPage(); return true; }
    if (plain && key == Qt::Key_Space) { view->nextScreen(); return true; }
    if (shift && key == Qt::Key_Space) { view->prevScreen(); return true; }
    return false;   // the arrows and `Page Up`/`Page Down` are the view's own
}

void ComparisonView::zoomBoth(int direction)
{
    PageView* leader = newer_->hasFocus() ? newer_ : older_;
    if (direction > 0) leader->zoomIn();
    else leader->zoomOut();
    syncFrom(leader);
}

void ComparisonView::fitBoth(int fit)
{
    PageView* leader = newer_->hasFocus() ? newer_ : older_;
    if (fit == 2) leader->setZoom(core::render::Zoom::actualSize());
    else leader->setFitMode(fit == 0 ? core::render::FitMode::Page : core::render::FitMode::Width);
    syncFrom(leader);
}

} // namespace rawpage::ui
