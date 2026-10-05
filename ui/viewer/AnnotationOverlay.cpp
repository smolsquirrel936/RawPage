// ui/viewer/AnnotationOverlay -- implementation. Stage 6 T5. See the header for the one-command rule, the
// hit test and why pop-ups are view state.

#include "ui/viewer/AnnotationOverlay.h"

#include "core/cmd/Command.h"
#include "core/doc/AnnotationGeometry.h"
#include "core/text/TextSelection.h"
#include "ui/viewer/SelectionOverlay.h"

#include <QDateTime>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPainter>
#include <QPen>
#include <QStyleHints>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rawpage::ui {
namespace {

using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;

bool isTextMarkup(AnnotationSubtype subtype)
{
    return subtype == AnnotationSubtype::Highlight || subtype == AnnotationSubtype::Underline
           || subtype == AnnotationSubtype::StrikeOut || subtype == AnnotationSubtype::Squiggly;
}

bool isNote(const Annotation& a)
{
    return a.subtype == AnnotationSubtype::Text && !a.hasFlag(core::AnnotationFlag::Hidden)
           && !a.hasFlag(core::AnnotationFlag::NoView);
}

// The overlay's own colours: a selection and its handles are CHROME drawn over the page, in the accent
// every Windows selection uses, legible on white paper and on an inverted page alike.
QColor selectionColour()
{
    return QColor(0x00, 0x78, 0xd4);
}

QColor popupColour()
{
    return QColor(0xff, 0xf6, 0xc4);
}

// A pop-up's size in logical pixels when its annotation names no rectangle of its own.
constexpr double kPopupWidth = 220.0;
constexpr double kPopupHeight = 96.0;
constexpr double kPopupTitle = 20.0;
constexpr double kPopupClose = 12.0;

int dragSlop()
{
    const QStyleHints* hints = QGuiApplication::styleHints();
    return hints != nullptr ? hints->startDragDistance() : 4;
}

} // namespace

AnnotationOverlay::AnnotationOverlay(QObject* parent) : QObject(parent)
{
    nudgeTimer_ = new QTimer(this);
    nudgeTimer_->setSingleShot(true);
    nudgeTimer_->setInterval(kNudgeSettleMs);
    connect(nudgeTimer_, &QTimer::timeout, this, [this] { (void)commitNudge(); });
}

AnnotationOverlay::~AnnotationOverlay() = default;

void AnnotationOverlay::setStore(const core::AnnotationStore* store)
{
    store_ = store;
    selection_.clear();
    pinned_.clear();
    hovered_.reset();
    drag_ = Drag{};
    emit selectionChanged();
    emit changed();
}

void AnnotationOverlay::setRunner(Runner runner)
{
    runner_ = std::move(runner);
}

void AnnotationOverlay::setClock(Clock clock)
{
    clock_ = std::move(clock);
}

std::int64_t AnnotationOverlay::now() const
{
    return clock_ ? clock_() : QDateTime::currentSecsSinceEpoch();
}

void AnnotationOverlay::setAuthor(Author author)
{
    author_ = std::move(author);
}

QString AnnotationOverlay::author() const
{
    return author_ ? author_() : QString();
}

const Annotation* AnnotationOverlay::find(const AnnotationId& id) const
{
    if (store_ == nullptr) return nullptr;
    const std::optional<std::size_t> index = store_->find(id);
    return index ? &store_->at(*index) : nullptr;
}

core::Status AnnotationOverlay::run(std::unique_ptr<core::Command> command)
{
    if (!runner_)
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()};
    core::Status status = runner_(std::move(command));
    if (!status) emit refused(status.error());
    return status;
}

bool AnnotationOverlay::movable(const Annotation& a) const
{
    return !a.hasFlag(core::AnnotationFlag::Locked) && !a.hasFlag(core::AnnotationFlag::ReadOnly);
}

// ---- The selection ------------------------------------------------------------------------------

void AnnotationOverlay::select(std::vector<AnnotationId> ids)
{
    if (ids == selection_) return;
    (void)commitNudge();
    selection_ = std::move(ids);
    emit selectionChanged();
    emit changed();
}

bool AnnotationOverlay::clearSelection()
{
    if (selection_.empty()) return false;
    (void)commitNudge();
    selection_.clear();
    emit selectionChanged();
    emit changed();
    return true;
}

std::vector<Annotation> AnnotationOverlay::selected() const
{
    std::vector<Annotation> out;
    for (const AnnotationId& id : selection_) {
        if (const Annotation* a = find(id)) out.push_back(*a);
    }
    return out;
}

std::optional<core::PdfPoint> AnnotationOverlay::toPage(const Viewport& view, int page, QPointF pos) const
{
    if (view.layout == nullptr) return std::nullopt;
    return view.layout->toPage(page, core::DevicePoint{view.visible.left() + pos.x() * view.dpr,
                                                      view.visible.top() + pos.y() * view.dpr});
}

double AnnotationOverlay::tolerance(const Viewport& view) const
{
    const double scale = view.layout != nullptr ? view.layout->presentedScale() : 0.0;
    return scale > 0.0 ? kHitPixels / scale : kHitPixels;
}

std::optional<core::PdfRect> AnnotationOverlay::drawnRect(const Viewport& view, const Annotation& a)
{
    if (!a.hasFlag(core::AnnotationFlag::NoRotate)) return a.rect;
    if (view.layout == nullptr) return std::nullopt;
    const std::optional<core::DevicePoint> anchor =
        view.layout->toContent(a.page, core::PdfPoint{a.rect.left(), a.rect.top()});
    if (!anchor) return std::nullopt;
    const double scale = view.layout->presentedScale();
    const core::DevicePoint far{anchor->x + a.rect.size.width * scale, anchor->y + a.rect.size.height * scale};
    const std::optional<core::PdfPoint> p = view.layout->toPage(a.page, *anchor);
    const std::optional<core::PdfPoint> q = view.layout->toPage(a.page, far);
    if (!p || !q) return std::nullopt;
    return core::PdfRect{core::PdfPoint{std::min(p->x, q->x), std::min(p->y, q->y)},
                         core::PdfSize{std::abs(q->x - p->x), std::abs(q->y - p->y)}};
}

std::optional<AnnotationId> AnnotationOverlay::annotationAt(const Viewport& view, QPoint pos) const
{
    if (store_ == nullptr || view.layout == nullptr) return std::nullopt;
    const core::DevicePoint content{view.visible.left() + pos.x() * view.dpr, view.visible.top() + pos.y() * view.dpr};
    const std::optional<core::render::PageLayout::DocumentPoint> at = view.layout->toDocument(content);
    if (!at) return std::nullopt;
    // THE TOPMOST: the last entry of the page's listing is the last drawn.
    const std::vector<core::AnnotationListingEntry>& listing = store_->listing(at->pageIndex);
    const double tol = tolerance(view);
    for (auto it = listing.rbegin(); it != listing.rend(); ++it) {
        if (!it->annotation) continue;
        const Annotation& a = store_->at(*it->annotation);
        if (a.hasFlag(core::AnnotationFlag::NoRotate)) {
            // Hit where it is DRAWN: the same annotation, carried into the box a reader keeps upright.
            const std::optional<core::PdfRect> drawn = drawnRect(view, a);
            const std::optional<Annotation> upright = drawn ? core::mappedInto(a, *drawn) : std::nullopt;
            if (upright && core::hitsAnnotation(*upright, at->point, tol)) return a.id;
            continue;
        }
        if (core::hitsAnnotation(a, at->point, tol)) return a.id;
    }
    return std::nullopt;
}

std::optional<QRectF> AnnotationOverlay::widgetRectOf(const Viewport& view, int page, const core::PdfRect& rect)
{
    if (view.layout == nullptr) return std::nullopt;
    double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
    bool first = true;
    for (const core::PdfPoint corner : {core::PdfPoint{rect.left(), rect.bottom()}, core::PdfPoint{rect.right(), rect.top()},
                                        core::PdfPoint{rect.left(), rect.top()}, core::PdfPoint{rect.right(), rect.bottom()}}) {
        const std::optional<core::DevicePoint> device = view.layout->toContent(page, corner);
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
    const double dpr = view.dpr > 0.0 ? view.dpr : 1.0;
    return QRectF((left - view.visible.left()) / dpr, (top - view.visible.top()) / dpr, (right - left) / dpr,
                  (bottom - top) / dpr);
}

// The eight handles of the ONE selected annotation, when it may be resized: corners and edge middles,
// clockwise from the top-left.
int AnnotationOverlay::handleAt(const Viewport& view, QPoint pos) const
{
    if (selection_.size() != 1) return -1;
    const Annotation* a = find(selection_.front());
    if (a == nullptr || !core::isResizable(a->subtype) || !movable(*a)) return -1;
    const std::optional<core::PdfRect> drawn = drawnRect(view, *a);
    const std::optional<QRectF> r = drawn ? widgetRectOf(view, a->page, *drawn) : std::nullopt;
    if (!r) return -1;
    const QPointF centres[8] = {r->topLeft(),     QPointF(r->center().x(), r->top()),    r->topRight(),
                                QPointF(r->right(), r->center().y()), r->bottomRight(),
                                QPointF(r->center().x(), r->bottom()), r->bottomLeft(),
                                QPointF(r->left(), r->center().y())};
    for (int i = 0; i < 8; ++i) {
        if (std::abs(pos.x() - centres[i].x()) <= kHandlePixels + 1 && std::abs(pos.y() - centres[i].y()) <= kHandlePixels + 1)
            return i;
    }
    return -1;
}

// ---- Gestures ----------------------------------------------------------------------------------

bool AnnotationOverlay::press(const Viewport& view, QPoint pos, Qt::KeyboardModifiers modifiers, int clicks)
{
    if (!isActive() || view.layout == nullptr) return false;
    (void)commitNudge();
    dragView_ = view;

    // A POP-UP FIRST: it is drawn over everything, so it is what the person aimed at.
    for (const AnnotationId& id : [this, &view] {
             std::vector<AnnotationId> shown;
             if (hovered_) shown.push_back(*hovered_);
             shown.insert(shown.end(), pinned_.begin(), pinned_.end());
             if (popupBehaviour_ == PopupBehaviour::Always && store_ != nullptr && view.layout != nullptr) {
                 for (const core::render::PageSlot& slot : view.layout->slotsIntersecting(view.visible)) {
                     for (const std::size_t index : store_->onPage(slot.pageIndex)) {
                         if (isNote(store_->at(index))) shown.push_back(store_->at(index).id);
                     }
                 }
             }
             return shown;
         }()) {
        const std::optional<QRectF> box = popupRect(view, id);
        if (!box || !box->contains(pos)) continue;
        const QRectF close(box->right() - kPopupClose - 4, box->top() + 4, kPopupClose, kPopupClose);
        if (close.contains(pos)) {
            pinned_.erase(std::remove(pinned_.begin(), pinned_.end(), id), pinned_.end());
            if (hovered_ == id) hovered_.reset();
            emit changed();
            return true;
        }
        drag_ = Drag{DragKind::Popup, id, -1, pos, pos};
        return true;
    }

    // A HANDLE of the one selected annotation: a resize.
    if (const int handle = handleAt(view, pos); handle >= 0 && !(modifiers & Qt::AltModifier)) {
        const Annotation* a = find(selection_.front());
        drag_ = Drag{DragKind::Resize, a->id, a->page, pos, pos, handle};
        return true;
    }

    const std::optional<AnnotationId> hit = annotationAt(view, pos);
    if (!hit) {
        // A press on the page lets the selection go -- unless it adds to one (`Ctrl`) or pans (`Shift`,
        // the temporary hand), neither of which is a person choosing nothing.
        if (!(modifiers & (Qt::ControlModifier | Qt::ShiftModifier))) clearSelection();
        return false;
    }
    const Annotation* a = find(*hit);
    if (a == nullptr) return false;

    if (modifiers & Qt::ControlModifier) {
        std::vector<AnnotationId> ids = selection_;
        const auto at = std::find(ids.begin(), ids.end(), *hit);
        if (at != ids.end()) ids.erase(at);
        else ids.push_back(*hit);
        select(std::move(ids));
        return true;
    }

    // A double click on a note opens or closes its pop-up (D6-28).
    if (clicks == 2 && isNote(*a)) {
        select({*hit});
        togglePopup(*hit);
        return true;
    }

    // TEXT MARKUP BELONGS TO ITS WORDS (D6-30): a drag on it is the tool's, a click selects it.
    if (isTextMarkup(a->subtype) && !(modifiers & Qt::AltModifier)) {
        drag_ = Drag{DragKind::MarkupClick, *hit, a->page, pos, pos};
        return false;
    }

    if (std::find(selection_.begin(), selection_.end(), *hit) == selection_.end()) select({*hit});
    if (modifiers & Qt::AltModifier) {
        drag_ = Drag{DragKind::Duplicate, *hit, a->page, pos, pos};
    } else if (movable(*a)) {
        drag_ = Drag{DragKind::Move, *hit, a->page, pos, pos};
    } else {
        drag_ = Drag{};   // selected, and not picked up: a locked one stays where it is (D6-27)
    }
    return true;
}

bool AnnotationOverlay::move(const Viewport& view, QPoint pos, Qt::KeyboardModifiers modifiers)
{
    if (drag_.kind == DragKind::None) return false;
    const QPoint travelled = pos - drag_.startPos;
    if (drag_.kind == DragKind::MarkupClick) {
        if (travelled.manhattanLength() > dragSlop()) drag_ = Drag{};   // a drag over the words: the tool's
        return false;
    }
    if (drag_.kind == DragKind::Popup) {
        popupOffset_[drag_.id] += QPointF(pos - drag_.pos);
        drag_.pos = pos;
        emit changed();
        return true;
    }
    dragView_ = view;
    drag_.pos = pos;
    drag_.constrained = modifiers.testFlag(Qt::ShiftModifier);
    drag_.moved = drag_.moved || travelled.manhattanLength() > dragSlop();
    emit changed();
    return true;
}

std::optional<core::PdfPoint> AnnotationOverlay::dragOffset(const Viewport& view) const
{
    QPointF delta = drag_.pos - drag_.startPos;
    if (drag_.constrained) {
        // To the nearest eighth of a turn ON SCREEN -- what a person sees as level, plumb or diagonal.
        const double length = std::hypot(delta.x(), delta.y());
        const double step = std::numbers::pi / 4.0;
        const double angle = std::round(std::atan2(delta.y(), delta.x()) / step) * step;
        delta = QPointF(length * std::cos(angle), length * std::sin(angle));
        if (std::abs(delta.x()) < 1e-9) delta.setX(0.0);
        if (std::abs(delta.y()) < 1e-9) delta.setY(0.0);
    }
    const std::optional<core::PdfPoint> from = toPage(view, drag_.page, QPointF(drag_.startPos));
    const std::optional<core::PdfPoint> to = toPage(view, drag_.page, QPointF(drag_.startPos) + delta);
    if (!from || !to) return std::nullopt;
    return core::PdfPoint{to->x - from->x, to->y - from->y};
}

std::optional<core::PdfRect> AnnotationOverlay::resizedRect(const Viewport& view) const
{
    const Annotation* a = find(drag_.id);
    if (a == nullptr) return std::nullopt;
    const std::optional<QRectF> start = widgetRectOf(view, a->page, a->rect);
    if (!start) return std::nullopt;
    const QPointF d = drag_.pos - drag_.startPos;
    double left = start->left(), top = start->top(), right = start->right(), bottom = start->bottom();
    const int h = drag_.handle;
    if (h == 0 || h == 6 || h == 7) left += d.x();
    if (h == 2 || h == 3 || h == 4) right += d.x();
    if (h == 0 || h == 1 || h == 2) top += d.y();
    if (h == 4 || h == 5 || h == 6) bottom += d.y();
    // `Shift` on a corner keeps the proportions: the larger change of the two decides, the opposite corner stays.
    const bool corner = h == 0 || h == 2 || h == 4 || h == 6;
    if (drag_.constrained && corner && start->width() > 0 && start->height() > 0) {
        const double scale = std::max(std::abs(right - left) / start->width(), std::abs(bottom - top) / start->height());
        const double w = start->width() * scale;
        const double hgt = start->height() * scale;
        if (h == 0 || h == 6) left = right - w;
        else right = left + w;
        if (h == 0 || h == 2) top = bottom - hgt;
        else bottom = top + hgt;
    }
    constexpr double kMinimum = 4.0;   // logical pixels: a resize never inverts or vanishes a box
    if (right - left < kMinimum) {
        if (h == 0 || h == 6 || h == 7) left = right - kMinimum;
        else right = left + kMinimum;
    }
    if (bottom - top < kMinimum) {
        if (h == 0 || h == 1 || h == 2) top = bottom - kMinimum;
        else bottom = top + kMinimum;
    }
    const std::optional<core::PdfPoint> p = toPage(view, a->page, QPointF(left, top));
    const std::optional<core::PdfPoint> q = toPage(view, a->page, QPointF(right, bottom));
    if (!p || !q) return std::nullopt;
    return core::PdfRect{core::PdfPoint{std::min(p->x, q->x), std::min(p->y, q->y)},
                         core::PdfSize{std::abs(q->x - p->x), std::abs(q->y - p->y)}};
}

bool AnnotationOverlay::release(const Viewport& view, QPoint pos, Qt::KeyboardModifiers modifiers)
{
    (void)modifiers;
    if (drag_.kind == DragKind::None) return false;
    const Drag drag = drag_;
    drag_.pos = pos;
    dragView_ = view;

    switch (drag.kind) {
    case DragKind::None:
        return false;
    case DragKind::MarkupClick:
        drag_ = Drag{};
        select({drag.id});
        return false;   // the tool's release ends its own (click) gesture
    case DragKind::Popup:
        drag_ = Drag{};
        return true;
    case DragKind::Move: {
        const bool moved = drag_.moved || (pos - drag.startPos).manhattanLength() > dragSlop();
        const std::optional<core::PdfPoint> offset = moved ? dragOffset(view) : std::nullopt;
        drag_ = Drag{};
        emit changed();
        if (offset && !core::isNoMove(offset->x, offset->y))
            (void)run(std::make_unique<core::MoveAnnotationCommand>(selection_, offset->x, offset->y, now()));
        return true;
    }
    case DragKind::Duplicate: {
        const bool moved = drag_.moved || (pos - drag.startPos).manhattanLength() > dragSlop();
        const std::optional<core::PdfPoint> offset = moved ? dragOffset(view) : std::nullopt;
        drag_ = Drag{};
        emit changed();
        if (!offset) return true;
        // THE COPIES, made new (D6-4): a fresh `/NM` and dates, everything drawn carried across, moved.
        // A thread, a state and a pop-up are the original's and stay with it; the author is whoever made
        // the copy (`identity/author`, since T6).
        std::vector<Annotation> copies;
        std::vector<AnnotationId> ids;
        for (const Annotation& source : selected()) {
            // T6 (D6-31(f) closed): the copy is the PERSON's, as anything they make is.
            Annotation copy = core::newAnnotation(source.subtype, source.page, author(), now());
            const AnnotationId id = copy.id;
            const QString nm = copy.nm;
            const core::PdfDate created = copy.created;
            const QString madeBy = copy.author;
            copy = core::translated(source, offset->x, offset->y);
            copy.id = id;
            copy.nm = nm;
            copy.author = madeBy;
            copy.created = created;
            copy.modified = created;
            copy.inReplyTo.reset();
            copy.replyType = core::ReplyType::Reply;
            copy.stateModel.clear();
            copy.state.clear();
            copy.popup.reset();
            copy.parent.reset();
            copy.open.reset();
            copy.hasAppearance = true;
            copy.flags &= ~core::LockAnnotationCommand::kLockFlags;
            ids.push_back(copy.id);
            copies.push_back(std::move(copy));
        }
        if (copies.empty()) return true;
        if (run(std::make_unique<core::AddAnnotationCommand>(std::move(copies)))) select(std::move(ids));
        return true;
    }
    case DragKind::Resize: {
        const bool moved = drag_.moved || (pos - drag.startPos).manhattanLength() > dragSlop();
        const std::optional<core::PdfRect> rect = moved ? resizedRect(view) : std::nullopt;
        const Annotation* a = find(drag.id);
        drag_ = Drag{};
        emit changed();
        if (!rect || a == nullptr) return true;
        std::optional<Annotation> after = core::mappedInto(*a, *rect);
        if (!after || *after == *a) return true;
        after->modified = core::PdfDate::fromUtcSeconds(now());
        (void)run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{*after}));
        return true;
    }
    }
    return false;
}

bool AnnotationOverlay::isDragging() const noexcept
{
    return drag_.kind == DragKind::Move || drag_.kind == DragKind::Resize || drag_.kind == DragKind::Duplicate
           || drag_.kind == DragKind::Popup;
}

bool AnnotationOverlay::cancel()
{
    if (!isDragging()) return false;
    drag_ = Drag{};
    emit changed();
    return true;
}

std::optional<core::PdfRect> AnnotationOverlay::previewRect() const
{
    const Annotation* a = find(drag_.id);
    if (a == nullptr) return std::nullopt;
    if (drag_.kind == DragKind::Resize) return resizedRect(dragView_);
    if (drag_.kind == DragKind::Move || drag_.kind == DragKind::Duplicate) {
        const std::optional<core::PdfPoint> d = dragOffset(dragView_);
        if (!d) return std::nullopt;
        return core::translated(*a, d->x, d->y).rect;
    }
    return std::nullopt;
}

// ---- Keys ---------------------------------------------------------------------------------------

bool AnnotationOverlay::nudge(const Viewport& view, int dx, int dy, bool fine)
{
    if (!isActive() || selection_.empty() || (dx == 0 && dy == 0)) return false;
    const Annotation* a = find(selection_.front());
    if (a == nullptr) return false;
    // THE SCREEN'S DIRECTION IN PAGE SPACE: under a turned page or view, right on screen is up the page.
    const std::optional<QRectF> r = widgetRectOf(view, a->page, a->rect);
    if (!r) return false;
    const std::optional<core::PdfPoint> from = toPage(view, a->page, r->center());
    const std::optional<core::PdfPoint> to = toPage(view, a->page, r->center() + QPointF(dx, dy));
    if (!from || !to) return false;
    const double vx = to->x - from->x;
    const double vy = to->y - from->y;
    const double length = std::hypot(vx, vy);
    if (length <= 0.0) return false;
    const double step = fine ? kFineNudgePoints : kNudgePoints;
    nudgeX_ += vx / length * step;
    nudgeY_ += vy / length * step;
    nudgeTimer_->start();
    emit changed();
    return true;
}

bool AnnotationOverlay::hasPendingNudge() const noexcept
{
    return nudgeX_ != 0.0 || nudgeY_ != 0.0;
}

core::Status AnnotationOverlay::commitNudge()
{
    nudgeTimer_->stop();
    if (!hasPendingNudge()) return core::Status{};
    const double dx = nudgeX_;
    const double dy = nudgeY_;
    nudgeX_ = 0.0;
    nudgeY_ = 0.0;
    emit changed();
    if (selection_.empty() || core::isNoMove(dx, dy)) return core::Status{};
    return run(std::make_unique<core::MoveAnnotationCommand>(selection_, dx, dy, now()));
}

core::Status AnnotationOverlay::deleteSelection()
{
    (void)commitNudge();
    if (selection_.empty())
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.malformed"), QString()};
    // WHAT A PERSON MEANS BY *DELETE THIS COMMENT*: the comment and its pop-up (the command deletes exactly
    // what it names, `DeleteAnnotationCommand`). Its replies are the Comments pane's to offer (T9).
    std::vector<AnnotationId> ids = selection_;
    for (const AnnotationId& id : selection_) {
        const Annotation* a = find(id);
        if (a == nullptr || !a->popup || store_ == nullptr) continue;
        if (const std::optional<std::size_t> popup = store_->find(*a->popup)) {
            const AnnotationId popupId = store_->at(*popup).id;
            if (std::find(ids.begin(), ids.end(), popupId) == ids.end()) ids.push_back(popupId);
        }
    }
    core::Status status = run(std::make_unique<core::DeleteAnnotationCommand>(std::move(ids)));
    if (status) clearSelection();
    return status;
}

core::Status AnnotationOverlay::setAppearance(const core::AnnotationAppearance& appearance)
{
    (void)commitNudge();
    if (selection_.empty() || appearance.isEmpty()) return core::Status{};
    return run(std::make_unique<core::SetAnnotationAppearanceCommand>(selection_, appearance, now()));
}

core::Status AnnotationOverlay::setContents(const QString& text)
{
    (void)commitNudge();
    std::vector<Annotation> after;
    for (Annotation a : selected()) {
        if (a.contents == text) continue;
        a.contents = text;
        a.modified = core::PdfDate::fromUtcSeconds(now());
        after.push_back(std::move(a));
    }
    if (after.empty()) return core::Status{};
    return run(std::make_unique<core::EditAnnotationCommand>(std::move(after)));
}

core::Status AnnotationOverlay::setLocks(std::uint32_t set, std::uint32_t clear)
{
    (void)commitNudge();
    if (selection_.empty() || (set | clear) == 0) return core::Status{};
    return run(std::make_unique<core::LockAnnotationCommand>(selection_, set, clear, now()));
}

// ---- Making annotations (S6 T6) ------------------------------------------------------------------

Annotation AnnotationOverlay::newAnnotation(core::AnnotationSubtype subtype, int page) const
{
    return core::newAnnotation(subtype, page, author(), now());
}

void AnnotationOverlay::applyStyle(Annotation& a, const Style& style) const
{
    if (style.colour.size() == 1 || style.colour.size() == 3 || style.colour.size() == 4) a.color = style.colour;
    if (style.opacity < 1.0) a.opacity = std::clamp(style.opacity, 0.0, 1.0);
}

Annotation AnnotationOverlay::caretAt(int page, const core::text::InsertionPoint& place, const Style& style) const
{
    // Acrobat's proportions: a wedge about half the line's height wide and three fifths of it tall, standing
    // on the line's foot -- and never so small a person cannot see or click it.
    const double tall = std::max(6.0, 0.6 * place.across);
    const double wide = std::max(4.0, 0.5 * place.across);
    Annotation caret = newAnnotation(core::AnnotationSubtype::Caret, page);
    caret.rect = place.vertical
                     ? core::PdfRect{core::PdfPoint{place.at.x, place.at.y - wide / 2.0}, core::PdfSize{tall, wide}}
                     : core::PdfRect{core::PdfPoint{place.at.x - wide / 2.0, place.at.y}, core::PdfSize{wide, tall}};
    applyStyle(caret, style);
    return caret;
}

AnnotationOverlay::MarkOutcome AnnotationOverlay::markText(SelectionOverlay& text, core::AnnotationSubtype subtype,
                                                           const Style& style, std::optional<QString> replacement)
{
    if (!isActive()) return MarkOutcome::Refused;
    if (pendingConnection_) disconnect(pendingConnection_);
    pendingMark_.reset();

    // NOT READY: the gesture has not met its pages' text, or a page it covers has not been read. Asked for
    // at `Visible` -- it is on screen -- and finished when it arrives (see the header).
    const core::text::TextSelection& selection = text.selection();
    const std::vector<int> missing = selection.missingPages(text.text());
    if (!text.isResolved() || !missing.empty()) {
        for (const int page : missing) (void)text.ensurePage(page, core::render::RenderPriority::Visible);
        pendingMark_ = PendingMark{QPointer<SelectionOverlay>(&text), subtype, style, std::move(replacement)};
        pendingConnection_ = connect(&text, &SelectionOverlay::changed, this, [this] { retryPendingMark(); });
        return MarkOutcome::Pending;
    }
    if (selection.isEmpty()) return MarkOutcome::Nothing;

    // ONE ANNOTATION PER PAGE the selection touches, its runs as quadrilaterals.
    std::vector<Annotation> made;
    for (int page = selection.firstPage(); page <= selection.lastPage(); ++page) {
        const core::text::PageReading* reading = text.text().page(page);
        if (reading == nullptr) continue;
        std::vector<double> quads = selection.markupQuadsOn(*reading);
        const std::optional<core::PdfRect> rect = core::markupRect(quads);
        if (!rect) continue;
        Annotation a = newAnnotation(subtype, page);
        a.quadPoints = std::move(quads);
        a.rect = *rect;
        applyStyle(a, style);
        made.push_back(std::move(a));
    }
    if (made.empty()) return MarkOutcome::Nothing;

    if (replacement) {
        // THE CARET at the selection's end, on its last page -- the replacement's text -- and every strikeout
        // in its group (D6-12): one item in the Comments pane, one command, one undo step.
        const int lastPage = made.back().page;
        const core::text::PageReading* reading = text.text().page(lastPage);
        const core::text::TextPosition end = selection.end();
        int caret = 0;
        if (reading != nullptr) caret = end.page == lastPage ? std::min(end.caret, reading->size()) : reading->size();
        const std::optional<core::text::InsertionPoint> place =
            reading != nullptr ? core::text::insertionPointAt(*reading, caret) : std::nullopt;
        if (!place) return MarkOutcome::Nothing;
        Annotation mark = caretAt(lastPage, *place, style);
        mark.contents = *replacement;
        for (Annotation& strike : made) {
            strike.inReplyTo = mark.id;
            strike.replyType = core::ReplyType::Group;
        }
        made.insert(made.begin(), std::move(mark));   // the caret first, as Acrobat writes a replace
    }
    if (!run(std::make_unique<core::AddAnnotationCommand>(std::move(made)))) return MarkOutcome::Refused;
    text.clear();
    return MarkOutcome::Made;
}

void AnnotationOverlay::retryPendingMark()
{
    if (!pendingMark_) return;
    if (pendingMark_->text.isNull()) {
        pendingMark_.reset();
        if (pendingConnection_) disconnect(pendingConnection_);
        return;
    }
    // Only once the selection can be marked: a page still on its way leaves the mark waiting.
    SelectionOverlay& text = *pendingMark_->text;
    if (!text.isResolved() || !text.selection().missingPages(text.text()).empty()) return;
    PendingMark mark = *pendingMark_;
    (void)markText(text, mark.subtype, mark.style, std::move(mark.replacement));
}

core::Status AnnotationOverlay::insertText(SelectionOverlay& text, int page, core::PdfPoint at, const QString& inserted,
                                           const Style& style)
{
    // THE BOUNDARY NEAREST THE CLICK when the page's text is here -- and the click itself when it is not, or
    // the page has none: a caret is a mark on the page as well as between letters.
    std::optional<core::text::InsertionPoint> place;
    (void)text.ensurePage(page, core::render::RenderPriority::Visible);
    if (const core::text::PageReading* reading = text.text().page(page); reading != nullptr && !reading->isEmpty())
        place = core::text::insertionPointAt(*reading, reading->caretAt(at));
    if (!place) place = core::text::InsertionPoint{at, 12.0, false};
    Annotation caret = caretAt(page, *place, style);
    caret.contents = inserted;
    return run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{std::move(caret)}));
}

core::Status AnnotationOverlay::addNote(int page, core::PdfPoint at, const QString& contents, const Style& style)
{
    // THE NOTE: Acrobat's -- the `Comment` icon, `NoZoom` and `NoRotate` so it stays an upright icon -- its
    // top-left corner where the person clicked (a `NoRotate` icon is drawn from that corner, D6-30).
    Annotation note = newAnnotation(core::AnnotationSubtype::Text, page);
    note.rect = core::PdfRect{core::PdfPoint{at.x, at.y - kNoteSize}, core::PdfSize{kNoteSize, kNoteSize}};
    note.icon = QStringLiteral("Comment");
    note.contents = contents;
    note.flags = core::AnnotationFlag::Print | core::AnnotationFlag::NoZoom | core::AnnotationFlag::NoRotate;
    applyStyle(note, style);

    // ITS POP-UP, written once and closed (D6-28): beside the icon, where a reader opens it. No appearance,
    // no author and no dates of its own -- it is the note's window, not a comment.
    Annotation popup = newAnnotation(core::AnnotationSubtype::Popup, page);
    popup.rect = core::PdfRect{core::PdfPoint{at.x + kNoteSize + 4.0, at.y - kPopupHeightPoints},
                               core::PdfSize{kPopupWidthPoints, kPopupHeightPoints}};
    popup.author.clear();
    popup.created = core::PdfDate{};
    popup.modified = core::PdfDate{};
    popup.flags = note.flags;
    popup.open = false;
    popup.hasAppearance = false;
    popup.parent = note.id;
    note.popup = popup.id;
    return run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{std::move(note), std::move(popup)}));
}

// ---- Pop-ups ------------------------------------------------------------------------------------

void AnnotationOverlay::setPopupBehaviour(PopupBehaviour behaviour)
{
    if (popupBehaviour_ == behaviour) return;
    popupBehaviour_ = behaviour;
    hovered_.reset();
    emit changed();
}

void AnnotationOverlay::hover(const Viewport& view, std::optional<QPoint> pos)
{
    if (popupBehaviour_ != PopupBehaviour::OnHover || drag_.kind != DragKind::None) return;
    std::optional<AnnotationId> now;
    if (pos) {
        // The pop-up itself keeps it: a person moving onto it to read it must not make it go.
        if (hovered_) {
            const std::optional<QRectF> box = popupRect(view, *hovered_);
            if (box && box->contains(*pos)) return;
        }
        if (const std::optional<AnnotationId> hit = annotationAt(view, *pos)) {
            const Annotation* a = find(*hit);
            if (a != nullptr && isNote(*a)) now = hit;
        }
    }
    if (now == hovered_) return;
    hovered_ = now;
    emit changed();
}

bool AnnotationOverlay::togglePopup(const AnnotationId& id)
{
    const Annotation* a = find(id);
    if (a == nullptr || !isNote(*a)) return false;
    const auto at = std::find(pinned_.begin(), pinned_.end(), id);
    if (at != pinned_.end()) {
        pinned_.erase(at);
        if (hovered_ == id) hovered_.reset();
    } else {
        pinned_.push_back(id);
    }
    emit changed();
    return true;
}

bool AnnotationOverlay::isPopupShown(const AnnotationId& id) const
{
    const Annotation* a = find(id);
    if (a == nullptr || !isNote(*a)) return false;
    // A double click is a person asking: it shows whatever the preference says about the rest.
    if (std::find(pinned_.begin(), pinned_.end(), id) != pinned_.end()) return true;
    switch (popupBehaviour_) {
    case PopupBehaviour::Always: return true;
    case PopupBehaviour::OnHover: return hovered_ == id;
    case PopupBehaviour::Never: return false;
    }
    return false;
}

std::optional<QRectF> AnnotationOverlay::popupRect(const Viewport& view, const AnnotationId& id) const
{
    if (!isPopupShown(id)) return std::nullopt;
    const Annotation* a = find(id);
    if (a == nullptr) return std::nullopt;
    std::optional<QRectF> box;
    // Its own `Popup` annotation's rectangle when it has one -- where the producer meant it to open.
    if (a->popup && store_ != nullptr) {
        if (const std::optional<std::size_t> popup = store_->find(*a->popup))
            box = widgetRectOf(view, store_->at(*popup).page, store_->at(*popup).rect);
    }
    if (!box || box->width() < 40 || box->height() < 30) {
        const std::optional<core::PdfRect> drawn = drawnRect(view, *a);
        const std::optional<QRectF> note = drawn ? widgetRectOf(view, a->page, *drawn) : std::nullopt;
        if (!note) return std::nullopt;
        box = QRectF(note->right() + 6, note->top(), kPopupWidth, kPopupHeight);
    }
    if (const auto moved = popupOffset_.find(id); moved != popupOffset_.end()) box->translate(moved->second);
    return box;
}

// ---- Painting -----------------------------------------------------------------------------------

void AnnotationOverlay::paint(QPainter& painter, const Viewport& view) const
{
    if (store_ == nullptr || view.layout == nullptr) return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor accent = selectionColour();

    // THE SELECTION: a dashed outline round each, and the handles of a lone one that can be resized.
    QPen outline(accent, 1.0, Qt::DashLine);
    for (const AnnotationId& id : selection_) {
        const Annotation* a = find(id);
        if (a == nullptr) continue;
        const std::optional<core::PdfRect> drawn = drawnRect(view, *a);
        const std::optional<QRectF> r = drawn ? widgetRectOf(view, a->page, *drawn) : std::nullopt;
        if (!r) continue;
        painter.setPen(outline);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(r->adjusted(-2, -2, 2, 2));
        if (selection_.size() == 1 && core::isResizable(a->subtype) && movable(*a)) {
            painter.setPen(QPen(accent, 1.0));
            painter.setBrush(Qt::white);
            const QPointF centres[8] = {r->topLeft(), QPointF(r->center().x(), r->top()), r->topRight(),
                                        QPointF(r->right(), r->center().y()), r->bottomRight(),
                                        QPointF(r->center().x(), r->bottom()), r->bottomLeft(),
                                        QPointF(r->left(), r->center().y())};
            for (const QPointF& c : centres)
                painter.drawRect(QRectF(c.x() - kHandlePixels, c.y() - kHandlePixels, 2 * kHandlePixels, 2 * kHandlePixels));
        }
    }

    // THE PREVIEW of a gesture in flight -- or of a nudge not yet committed: where it is going. Qt draws it
    // until the command's own tiles arrive (D6-2); the document has not changed yet.
    const auto preview = [&](int page, const core::PdfRect& rect) {
        if (const std::optional<QRectF> r = widgetRectOf(view, page, rect)) {
            painter.setPen(QPen(accent, 2.0, Qt::DashLine));
            painter.setBrush(QColor(accent.red(), accent.green(), accent.blue(), 40));
            painter.drawRect(*r);
        }
    };
    if (drag_.moved && (drag_.kind == DragKind::Move || drag_.kind == DragKind::Resize || drag_.kind == DragKind::Duplicate)) {
        const Viewport& dragView = dragView_.layout == view.layout ? view : dragView_;
        if (drag_.kind == DragKind::Resize) {
            if (const Annotation* a = find(drag_.id)) {
                if (const std::optional<core::PdfRect> rect = resizedRect(dragView)) preview(a->page, *rect);
            }
        } else if (const std::optional<core::PdfPoint> d = dragOffset(dragView)) {
            for (const Annotation& a : selected()) preview(a.page, core::translated(a, d->x, d->y).rect);
        }
    }
    if (hasPendingNudge()) {
        for (const Annotation& a : selected()) preview(a.page, core::translated(a, nudgeX_, nudgeY_).rect);
    }

    // THE POP-UPS (D6-28): the note's author and text, never a tile and never a command.
    std::vector<AnnotationId> shown = pinned_;
    if (hovered_) shown.push_back(*hovered_);
    if (popupBehaviour_ == PopupBehaviour::Always) {
        for (const core::render::PageSlot& slot : view.layout->slotsIntersecting(view.visible)) {
            for (const std::size_t index : store_->onPage(slot.pageIndex)) {
                if (isNote(store_->at(index))) shown.push_back(store_->at(index).id);
            }
        }
    }
    std::sort(shown.begin(), shown.end());
    shown.erase(std::unique(shown.begin(), shown.end()), shown.end());
    for (const AnnotationId& id : shown) {
        const std::optional<QRectF> box = popupRect(view, id);
        const Annotation* a = find(id);
        if (!box || a == nullptr) continue;
        painter.setPen(QPen(QColor(0x80, 0x70, 0x30), 1.0));
        painter.setBrush(popupColour());
        painter.drawRoundedRect(*box, 3, 3);
        painter.setPen(Qt::black);
        QFont title = painter.font();
        title.setBold(true);
        painter.setFont(title);
        const QRectF titleRect(box->left() + 6, box->top() + 2, box->width() - kPopupClose - 16, kPopupTitle);
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetricsF(title).elidedText(a->author, Qt::ElideRight, titleRect.width()));
        const QRectF close(box->right() - kPopupClose - 4, box->top() + 4, kPopupClose, kPopupClose);
        painter.drawLine(close.topLeft(), close.bottomRight());
        painter.drawLine(close.topRight(), close.bottomLeft());
        title.setBold(false);
        painter.setFont(title);
        painter.drawText(QRectF(box->left() + 6, box->top() + kPopupTitle + 2, box->width() - 12,
                                box->height() - kPopupTitle - 6),
                         Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, a->contents);
    }
    painter.restore();
}

void AnnotationOverlay::modelChanged()
{
    std::vector<AnnotationId> kept;
    for (const AnnotationId& id : selection_) {
        if (find(id) != nullptr) kept.push_back(id);
    }
    const bool selectionMoved = kept.size() != selection_.size();
    selection_ = std::move(kept);
    pinned_.erase(std::remove_if(pinned_.begin(), pinned_.end(), [this](const AnnotationId& id) { return find(id) == nullptr; }),
                  pinned_.end());
    if (hovered_ && find(*hovered_) == nullptr) hovered_.reset();
    if (selectionMoved) emit selectionChanged();
    emit changed();
}

} // namespace rawpage::ui
