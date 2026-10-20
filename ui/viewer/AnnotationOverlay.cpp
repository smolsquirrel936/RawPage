// ui/viewer/AnnotationOverlay -- implementation. Stage 6 T5. See the header for the one-command rule, the
// hit test and why pop-ups are view state.

#include "ui/viewer/AnnotationOverlay.h"

#include "core/Measure.h"

#include "core/doc/Attachments.h"

#include "core/cmd/Command.h"
#include "core/doc/AnnotationGeometry.h"
#include "core/doc/DrawnShapes.h"
#include "core/doc/Stamps.h"
#include "core/text/TextSelection.h"
#include "ui/viewer/SelectionOverlay.h"

#include <QFileInfo>
#include <QFile>
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

// WHAT HAS A POP-UP (D6-28): a note -- and, since M-01's walk, a CARET, whose text is the words a person inserted
// and was otherwise read only in the Comments pane and the properties bar (S6 guide I-26). The same behaviour for
// both: hover, `Always`, `Never`, and a double click to pin. Hidden or `NoView`, neither.
bool hasPopup(const Annotation& a)
{
    return (a.subtype == AnnotationSubtype::Text || a.subtype == AnnotationSubtype::Caret)
           && !a.hasFlag(core::AnnotationFlag::Hidden) && !a.hasFlag(core::AnnotationFlag::NoView);
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
// S6 guide I-29: a thread's pop-up. A reply is indented this much a level, to three levels; a thread is listed to this
// many entries, so a hostile 10,000-deep chain is a bounded paint; the wheel moves this many lines a notch.
constexpr double kPopupIndent = 10.0;
constexpr int kPopupMaxDepth = 3;
constexpr std::size_t kPopupMaxEntries = 200;
constexpr double kPopupWheelLines = 3.0;

// What a pop-up is written and measured in: the application's font, so `popupRect` -- which has no painter --
// measures what `paint` draws.
QFont popupFont()
{
    return QGuiApplication::font();
}

// The height `text` takes wrapped to `width`.
double wrappedHeight(const QFontMetricsF& metrics, double width, const QString& text)
{
    if (text.isEmpty()) return 0.0;
    return metrics.boundingRect(QRectF(0, 0, std::max(1.0, width), 1.0e6), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text)
        .height();
}

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

std::optional<Annotation> AnnotationOverlay::placedIcon(core::CommentingTool tool, int page, core::PdfPoint at)
{
    Annotation a;
    a.page = page;
    switch (tool) {
    case core::CommentingTool::StickyNote:
        // Acrobat's note: the `Comment` icon, `NoZoom` and `NoRotate` so it stays an upright icon, its top-left
        // corner where the person clicked (a `NoRotate` icon is drawn from that corner, D6-30).
        a.rect = core::PdfRect{core::PdfPoint{at.x, at.y - kNoteSize}, core::PdfSize{kNoteSize, kNoteSize}};
        a.flags = core::AnnotationFlag::Print | core::AnnotationFlag::NoZoom | core::AnnotationFlag::NoRotate;
        return a;
    case core::CommentingTool::AttachFile:
        a.rect = core::PdfRect{core::PdfPoint{at.x - kPushPinWidth / 2.0, at.y}, core::PdfSize{kPushPinWidth, kPushPinHeight}};
        a.flags = core::AnnotationFlag::Print;
        return a;
    case core::CommentingTool::RecordAudio:
    case core::CommentingTool::AttachAudio:
        // The rectangle core makes, asked of core rather than written a second time.
        return core::newSoundComment(page, at, core::SoundData{}, {}, QString(), 0);
    default:
        return std::nullopt;
    }
}

core::PdfPoint AnnotationOverlay::keepIconOnPage(const Viewport& view, int page, core::CommentingTool tool, core::PdfPoint at)
{
    const std::optional<Annotation> icon = placedIcon(tool, page, at);
    const core::render::PageSlot* slot = view.layout != nullptr ? view.layout->slotForPage(page) : nullptr;
    if (!icon || slot == nullptr) return at;
    // THE BOX TESTED IS THE BOX DRAWN: for an upright note on a turned page, another box than its rectangle -- and
    // moving the rectangle in page space moves that box by the same amount, so the shift found is the shift made.
    const std::optional<core::PdfRect> drawn = drawnRect(view, *icon);
    if (!drawn) return at;
    const core::PdfRect& crop = slot->cropBox;
    const auto shift = [](double low, double high, double pageLow, double pageHigh) {
        if (low < pageLow || high - low > pageHigh - pageLow) return pageLow - low;
        if (high > pageHigh) return pageHigh - high;
        return 0.0;
    };
    return core::PdfPoint{at.x + shift(drawn->left(), drawn->right(), crop.left(), crop.right()),
                          at.y + shift(drawn->bottom(), drawn->top(), crop.bottom(), crop.top())};
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
    for (const AnnotationId& id : shownPopups(view)) {
        const std::optional<PopupLayout> popup = popupLayout(view, id);
        if (!popup || !popup->box.contains(pos)) continue;
        if (popup->close.contains(pos)) {
            pinned_.erase(std::remove(pinned_.begin(), pinned_.end(), id), pinned_.end());
            if (hovered_ == id) hovered_.reset();
            emit changed();
            return true;
        }
        // Its reply button is the pane's to answer (`replyButtonAt`): it opens an editor, which is no drag.
        if (popup->button.contains(pos)) return true;
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

    // A double click on a note or a caret -- or a reply over one -- opens or closes its pop-up (D6-28, I-26, I-29).
    if (clicks == 2 && hasPopup(*a)) {
        select({*hit});
        togglePopup(*hit);
        return true;
    }
    // S6 T14: a double click on a sound plays it -- the window's player; no byte changes.
    if (clicks == 2 && a->subtype == AnnotationSubtype::Sound) {
        select({*hit});
        emit soundRequested(*hit);
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
        // S6 T12: a measurement resized measures what it now spans, and /Contents that held its reading follows it.
        after = core::withFreshReading(*a, std::move(*after));
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
    if (!style.blendMode.isEmpty() && core::isBlendModeName(style.blendMode)) a.blendMode = style.blendMode;   // S6 T8
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
        // S6 T14: a redaction mark, with its whole style (D6-25).
        if (subtype == AnnotationSubtype::Redact && style.redaction) {
            if (std::optional<Annotation> mark = core::newTextRedaction(page, quads, *style.redaction, author(), now()))
                made.push_back(std::move(*mark));
            continue;
        }
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

AnnotationOverlay::MarkOutcome AnnotationOverlay::markRedaction(SelectionOverlay& text, const core::RedactionStyle& style)
{
    Style marking;
    marking.redaction = style;
    return markText(text, AnnotationSubtype::Redact, marking);
}

bool AnnotationOverlay::markArea(int page, const core::PdfRect& area, const core::RedactionStyle& style)
{
    if (!isActive()) return false;
    std::optional<Annotation> mark = core::newAreaRedaction(page, area, style, author(), now());
    if (!mark) return false;
    return static_cast<bool>(run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{std::move(*mark)})));
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
    // THE NOTE: Acrobat's `Comment` icon, its rectangle and flags `placedIcon`'s -- the box a click's placement was
    // kept on the page by (I-25), so the two cannot disagree.
    Annotation note = newAnnotation(core::AnnotationSubtype::Text, page);
    const std::optional<Annotation> icon = placedIcon(core::CommentingTool::StickyNote, page, at);
    note.rect = icon->rect;
    note.icon = QStringLiteral("Comment");
    note.contents = contents;
    note.flags = icon->flags;
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

// ---- A file attached on the page (S6 T11) -----------------------------------------------------------

bool AnnotationOverlay::attachFileAt(int page, core::PdfPoint at)
{
    if (!fileChooser_) return false;
    const std::optional<QString> path = fileChooser_();
    if (!path || path->isEmpty()) return false;
    if (const core::Status made = addFileAttachment(page, at, *path); !made) {
        reportRefusal(made.error());
        return false;
    }
    return true;
}

core::Status AnnotationOverlay::addFileAttachment(int page, core::PdfPoint at, const QString& path)
{
    // READ ONCE, NOW: the bytes are the command's (D6-17). The size is asked before the read, so a file too large
    // to carry is refused without reading it.
    QFile file(path);
    const QString name = QFileInfo(path).fileName();
    if (file.size() > core::kMaxAttachmentBytes)
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.attachment.tooLarge"), name};
    if (!file.open(QIODevice::ReadOnly))
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.attachment.unreadable"), name};
    QByteArray bytes = file.readAll();
    file.close();
    if (bytes.size() > core::kMaxAttachmentBytes)
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.attachment.tooLarge"), name};

    const core::PdfRect rect = placedIcon(core::CommentingTool::AttachFile, page, at)->rect;
    core::EmbeddedFile embedded = core::newEmbeddedFile(name, std::move(bytes), QString(), now());
    // Acrobat's pushpin blue, the colour the corpus generator gives one -- a file attachment has no tool default.
    Annotation pin = core::newFileAttachment(page, rect, std::move(embedded), {0.13, 0.4, 0.86}, author(), now());
    return run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{std::move(pin)}));
}

// ---- A sound comment (S6 T14) -----------------------------------------------------------------------

bool AnnotationOverlay::recordSoundAt(int page, core::PdfPoint at)
{
    if (!soundRecorder_ || !isActive()) return false;
    const std::optional<core::SoundData> sound = soundRecorder_();
    if (!sound) return false;
    if (const core::Status made = addSound(page, at, *sound); !made) return false;   // `run` said why
    return true;
}

bool AnnotationOverlay::attachSoundAt(int page, core::PdfPoint at)
{
    if (!soundChooser_ || !isActive()) return false;
    const std::optional<QString> path = soundChooser_();
    if (!path || path->isEmpty()) return false;
    if (const core::Status made = addSoundFile(page, at, *path); !made) {
        // A refusal before any command -- a file that is no PCM WAV -- said as a refused command is.
        if (QString(made.error().messageKey).startsWith(QLatin1String("error.sound."))) reportRefusal(made.error());
        return false;
    }
    return true;
}

core::Status AnnotationOverlay::addSound(int page, core::PdfPoint at, const core::SoundData& sound)
{
    // Acrobat's note yellow, the colour the corpus generator gives a speaker -- a sound has no tool default.
    Annotation a = core::newSoundComment(page, at, sound, {1.0, 0.85, 0.3}, author(), now());
    return run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{std::move(a)}));
}

core::Status AnnotationOverlay::addSoundFile(int page, core::PdfPoint at, const QString& path)
{
    // READ ONCE, NOW: the samples are the command's (D6-18). The size is asked first, so a file far too large to carry
    // is refused without reading it -- a WAV's header is 44 bytes, and anything past the bound plus a generous header
    // cannot hold samples a command may carry.
    QFile file(path);
    const QString name = QFileInfo(path).fileName();
    if (file.size() > core::kMaxSoundBytes + 1024 * 1024)
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.sound.tooLarge"), name};
    if (!file.open(QIODevice::ReadOnly))
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.sound.unreadable"), name};
    const QByteArray bytes = file.readAll();
    file.close();
    core::Result<core::SoundData> sound = core::soundFromWav(bytes);
    if (!sound) return sound.error();
    return addSound(page, at, sound.value());
}

// ---- Typed text (S6 T7) --------------------------------------------------------------------------

core::Status AnnotationOverlay::addFreeText(int page, core::FreeTextKind kind, core::PdfPoint topLeft,
                                            std::optional<double> width, std::optional<core::PdfPoint> target, int rotate,
                                            const QString& text, const TypedStyle& style)
{
    Annotation a = newAnnotation(AnnotationSubtype::FreeText, page);
    a.intent = QString(core::freeTextIntent(kind));
    a.rotate = ((rotate % 360) + 360) % 360 / 90 * 90;
    a.contents = text;
    const std::vector<double> colour =
        style.colour.size() == 1 || style.colour.size() == 3 || style.colour.size() == 4 ? style.colour : std::vector<double>{0.0};
    a.color = colour;
    if (style.opacity < 1.0) a.opacity = std::clamp(style.opacity, 0.0, 1.0);
    // S6 T8: the tool's fill, a box's and a callout's -- a typewriter has none (D6-8).
    if (kind != core::FreeTextKind::Typewriter
        && (style.fill.size() == 1 || style.fill.size() == 3 || style.fill.size() == 4))
        a.interiorColor = style.fill;
    a.borderWidth = kind == core::FreeTextKind::Typewriter ? 0.0 : std::max(0.0, style.borderWidth);
    const double size = style.fontSize > 0.0 ? style.fontSize : core::kFreeTextDefaultSize;
    a.defaultAppearance = core::freeTextDefaultAppearance(core::TypedFont::Helvetica, size, colour);
    // Where the text will start: a box of one line, which the fitting below grows to what was typed.
    const double inset = *a.borderWidth + core::kFreeTextPadding;
    const double w = kind == core::FreeTextKind::Typewriter ? size + 2.0 * inset : width.value_or(kFreeTextWidthPoints);
    a.rect = core::freeTextBoxAt(topLeft, std::max(w, size + 2.0 * inset), size * core::kTypedLeading + 2.0 * inset, a.rotate);
    if (kind == core::FreeTextKind::Callout) {
        a.lineEndings = {QStringLiteral("OpenArrow")};
        a.callout = core::calloutLeader(target.value_or(core::PdfPoint{a.rect.left(), a.rect.bottom()}), a.rect);
    }
    core::Result<Annotation> fitted = core::fittedFreeText(a, style.han);
    if (!fitted) {
        emit refused(fitted.error());
        return fitted.error();
    }
    return run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{std::move(fitted).value()}));
}

core::Status AnnotationOverlay::editFreeText(const AnnotationId& id, const QString& text, core::TypedFont hanFallback)
{
    (void)commitNudge();
    const Annotation* a = find(id);
    if (a == nullptr || a->subtype != AnnotationSubtype::FreeText)
        return core::Error{core::ErrorCode::NotFound, QLatin1String("error.pdf.annotationUnknown"), id.toString()};
    if (a->contents == text) return core::Status{};
    Annotation after = *a;
    after.contents = text;
    after.modified = core::PdfDate::fromUtcSeconds(now());
    if (!a->hasFlag(core::AnnotationFlag::Locked)) {
        core::Result<Annotation> fitted = core::fittedFreeText(after, hanFallback);
        if (!fitted) {
            emit refused(fitted.error());
            return fitted.error();
        }
        after = std::move(fitted).value();
    } else if (const std::optional<char32_t> refusedCharacter = core::firstRefusedCharacter(text)) {
        const core::Error error{core::ErrorCode::InvalidArgument, QLatin1String("error.edit.typedCharacterRefused"),
                                core::describeCharacter(*refusedCharacter)};
        emit refused(error);
        return error;
    }
    return run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{std::move(after)}));
}

core::Status AnnotationOverlay::setFontSize(double size)
{
    (void)commitNudge();
    if (!(size >= core::kToolFontSizeMin && size <= core::kToolFontSizeMax))
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.command.malformed"), QStringLiteral("fontSize")};
    std::vector<Annotation> after;
    for (const Annotation& a : selected()) {
        if (a.subtype != AnnotationSubtype::FreeText) continue;
        core::DefaultAppearance da = core::DefaultAppearance::parse(a.defaultAppearance).value_or(core::DefaultAppearance{});
        if (std::abs(da.size - size) < 1e-9) continue;
        if (da.font.isEmpty()) da.font = QStringLiteral("Helv");
        if (da.colour.empty()) da.colour = a.color.empty() ? std::vector<double>{0.0} : a.color;
        da.size = size;
        Annotation changed = a;
        changed.defaultAppearance = da.toString();
        changed.modified = core::PdfDate::fromUtcSeconds(now());
        // Refitted to its lines at the new size -- a locked one keeps its size, and `permitted` refuses it anyway.
        if (!a.hasFlag(core::AnnotationFlag::Locked)) {
            core::Result<Annotation> fitted = core::fittedFreeText(changed, core::TypedFont::Simplified);
            if (!fitted) {
                emit refused(fitted.error());
                return fitted.error();
            }
            changed = std::move(fitted).value();
        }
        after.push_back(std::move(changed));
    }
    if (after.empty()) return core::Status{};
    return run(std::make_unique<core::EditAnnotationCommand>(std::move(after)));
}

core::Status AnnotationOverlay::setRotation(int degrees)
{
    (void)commitNudge();
    std::vector<Annotation> after;
    for (const Annotation& a : selected()) {
        if (a.subtype != AnnotationSubtype::Stamp || core::stampRotation(a) == ((degrees % 360) + 360) % 360) continue;
        core::Result<Annotation> turned = core::rotatedStamp(a, degrees);
        if (!turned) {
            emit refused(turned.error());
            return turned.error();
        }
        turned.value().modified = core::PdfDate::fromUtcSeconds(now());
        after.push_back(std::move(turned).value());
    }
    if (after.empty()) return core::Status{};
    return run(std::make_unique<core::EditAnnotationCommand>(std::move(after)));
}

// ---- The drawing tools (S6 T8) ------------------------------------------------------------------------

core::Status AnnotationOverlay::addMade(std::vector<Annotation> annotations)
{
    if (annotations.empty())
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.pdf.annotationPlacement"), QString()};
    return run(std::make_unique<core::AddAnnotationCommand>(std::move(annotations)));
}

core::Status AnnotationOverlay::eraseInk(int page, const std::vector<double>& path, double radius)
{
    if (store_ == nullptr || page < 0) return core::Status{};
    // THE INK THE ERASER TOUCHED, topmost first -- the last of the page's listing is the last drawn -- and only
    // that one (see the header of `ui/viewer/tools/InkTool`): one gesture, one command.
    const std::vector<core::AnnotationListingEntry>& listing = store_->listing(page);
    for (auto it = listing.rbegin(); it != listing.rend(); ++it) {
        if (!it->annotation) continue;
        const Annotation& a = store_->at(*it->annotation);
        if (a.subtype != AnnotationSubtype::Ink || a.hasFlag(core::AnnotationFlag::Hidden)
            || a.hasFlag(core::AnnotationFlag::NoView))
            continue;
        const core::ErasedInk erased = core::erasedStrokes(a.inkList, path, radius);
        if (!erased.changed) continue;
        if (erased.strokes.empty())
            return run(std::make_unique<core::DeleteAnnotationCommand>(std::vector<AnnotationId>{a.id}));
        Annotation after = a;
        after.inkList = erased.strokes;
        after.modified = core::PdfDate::fromUtcSeconds(now());
        // The rectangle shrinks to what is left: our builder redraws the ink, and its box is the rectangle.
        if (const std::optional<Annotation> fitted = core::fittedShape(after)) after = *fitted;
        return run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{std::move(after)}));
    }
    return core::Status{};
}

void AnnotationOverlay::reportRefusal(const core::Error& error)
{
    emit refused(error);
}

// ---- Pop-ups ------------------------------------------------------------------------------------

void AnnotationOverlay::setPopupBehaviour(PopupBehaviour behaviour)
{
    if (popupBehaviour_ == behaviour) return;
    popupBehaviour_ = behaviour;
    hovered_.reset();
    emit changed();
}

void AnnotationOverlay::setNumbers(std::map<AnnotationId, int> numbers)
{
    if (numbers_ == numbers) return;
    numbers_ = std::move(numbers);
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
        // A reply over a note reaches the NOTE's pop-up, which lists it (I-29).
        if (const std::optional<AnnotationId> hit = annotationAt(view, *pos)) now = popupOwner(*hit);
    }
    if (now == hovered_) return;
    hovered_ = now;
    emit changed();
}

bool AnnotationOverlay::togglePopup(const AnnotationId& id)
{
    const std::optional<AnnotationId> owner = popupOwner(id);
    if (!owner) return false;
    const auto at = std::find(pinned_.begin(), pinned_.end(), *owner);
    if (at != pinned_.end()) {
        pinned_.erase(at);
        if (hovered_ == *owner) hovered_.reset();
    } else {
        pinned_.push_back(*owner);
    }
    emit changed();
    return true;
}

bool AnnotationOverlay::pinPopup(const AnnotationId& id)
{
    const std::optional<AnnotationId> owner = popupOwner(id);
    if (!owner) return false;
    if (std::find(pinned_.begin(), pinned_.end(), *owner) == pinned_.end()) pinned_.push_back(*owner);
    emit changed();
    return true;
}

bool AnnotationOverlay::isPopupShown(const AnnotationId& id) const
{
    const Annotation* a = find(id);
    if (a == nullptr || !hasPopup(*a)) return false;
    // A double click is a person asking: it shows whatever the preference says about the rest.
    if (std::find(pinned_.begin(), pinned_.end(), id) != pinned_.end()) return true;
    switch (popupBehaviour_) {
    case PopupBehaviour::Always: return popupOwner(id) == id;   // a reply's thread is its owner's pop-up (I-29)
    case PopupBehaviour::OnHover: return hovered_ == id;
    case PopupBehaviour::Never: return false;
    }
    return false;
}

std::optional<QRectF> AnnotationOverlay::popupRect(const Viewport& view, const AnnotationId& id) const
{
    const std::optional<PopupLayout> popup = popupLayout(view, id);
    if (!popup) return std::nullopt;
    return popup->box;
}

// ---- A thread's pop-up (S6 guide I-29) ----------------------------------------------------------

std::size_t AnnotationOverlay::threadRoot(std::size_t index) const
{
    // Up the `/RT /R` chain only: a group member (a replace edit's strikeout) is its caret's item, not its reply.
    // `parentOf` is none past a cycle, a missing parent and another page, so this ends; the count bounds it anyway.
    for (std::size_t steps = 0; steps < store_->all().size(); ++steps) {
        if (store_->at(index).replyType != core::ReplyType::Reply) break;
        const std::optional<std::size_t> parent = store_->parentOf(index);
        if (!parent) break;
        index = *parent;
    }
    return index;
}

std::optional<AnnotationId> AnnotationOverlay::popupOwner(const AnnotationId& id) const
{
    if (store_ == nullptr) return std::nullopt;
    std::optional<std::size_t> at = store_->find(id);
    std::optional<std::size_t> owner;
    for (std::size_t steps = 0; at && steps <= store_->all().size(); ++steps) {
        const Annotation& a = store_->at(*at);
        if (hasPopup(a)) owner = at;
        if (a.replyType != core::ReplyType::Reply) break;
        at = store_->parentOf(*at);
    }
    if (!owner) return std::nullopt;
    return store_->at(*owner).id;
}

std::vector<AnnotationId> AnnotationOverlay::popupThread(const AnnotationId& id) const
{
    std::vector<AnnotationId> ids;
    if (store_ == nullptr) return ids;
    const std::optional<std::size_t> at = store_->find(id);
    if (!at) return ids;
    // The root, then each reply under it, depth first and oldest first -- walked iteratively, a seen set against
    // anything the store did not already break, and at most `kPopupMaxEntries`.
    std::vector<std::size_t> stack{threadRoot(*at)};
    std::vector<bool> seen(store_->all().size(), false);
    while (!stack.empty() && ids.size() < kPopupMaxEntries) {
        const std::size_t index = stack.back();
        stack.pop_back();
        if (seen[index]) continue;
        seen[index] = true;
        ids.push_back(store_->at(index).id);
        const std::vector<std::size_t> replies = store_->replies(index);
        for (auto reply = replies.rbegin(); reply != replies.rend(); ++reply) stack.push_back(*reply);
    }
    return ids;
}

void AnnotationOverlay::setReplyLabel(QString label)
{
    if (replyLabel_ == label) return;
    replyLabel_ = std::move(label);
    emit changed();
}

std::vector<AnnotationId> AnnotationOverlay::shownPopups(const Viewport& view) const
{
    std::vector<AnnotationId> shown;
    const auto add = [&shown](const AnnotationId& id) {
        if (std::find(shown.begin(), shown.end(), id) == shown.end()) shown.push_back(id);
    };
    if (hovered_) add(*hovered_);
    for (const AnnotationId& id : pinned_) add(id);
    if (popupBehaviour_ == PopupBehaviour::Always && store_ != nullptr && view.layout != nullptr) {
        for (const core::render::PageSlot& slot : view.layout->slotsIntersecting(view.visible)) {
            for (const std::size_t index : store_->onPage(slot.pageIndex)) {
                if (isPopupShown(store_->at(index).id)) add(store_->at(index).id);
            }
        }
    }
    return shown;
}

std::optional<AnnotationOverlay::PopupLayout> AnnotationOverlay::popupLayout(const Viewport& view, const AnnotationId& id) const
{
    if (!isPopupShown(id)) return std::nullopt;
    const Annotation* a = find(id);
    if (a == nullptr) return std::nullopt;
    std::optional<QRectF> base;
    // Its own `Popup` annotation's rectangle when it has one -- where the producer meant it to open.
    if (a->popup && store_ != nullptr) {
        if (const std::optional<std::size_t> popup = store_->find(*a->popup))
            base = widgetRectOf(view, store_->at(*popup).page, store_->at(*popup).rect);
    }
    if (!base || base->width() < 40 || base->height() < 30) {
        const std::optional<core::PdfRect> drawn = drawnRect(view, *a);
        const std::optional<QRectF> note = drawn ? widgetRectOf(view, a->page, *drawn) : std::nullopt;
        if (!note) return std::nullopt;
        base = QRectF(note->right() + 6, note->top(), kPopupWidth, kPopupHeight);
    }
    if (const auto moved = popupOffset_.find(id); moved != popupOffset_.end()) base->translate(moved->second);

    // THE THREAD, measured at the body's width -- a reply's author on a line of its own above its text.
    PopupLayout out;
    QFont font = popupFont();
    const QFontMetricsF plain(font);
    font.setBold(true);
    const QFontMetricsF bold(font);
    const double width = base->width() - 12.0 - 6.0;   // the margins, and room for the scroll mark
    const std::vector<AnnotationId> thread = popupThread(id);
    double y = 0.0;
    for (std::size_t i = 0; i < thread.size(); ++i) {
        const Annotation* entry = find(thread[i]);
        if (entry == nullptr) continue;
        PopupEntry e;
        e.id = entry->id;
        if (i > 0) {
            e.author = entry->author;
            const std::optional<std::size_t> index = store_->find(entry->id);
            int depth = 0;
            for (std::optional<std::size_t> up = index; up && depth <= kPopupMaxDepth; up = store_->parentOf(*up)) ++depth;
            e.depth = std::min(depth - 1, kPopupMaxDepth);
            y += 6.0;   // the rule between two entries
        }
        e.text = entry->contents;
        const double indent = e.depth * kPopupIndent;
        e.top = y;
        e.height = (e.author.isEmpty() ? 0.0 : bold.lineSpacing()) + wrappedHeight(plain, width - indent, e.text);
        y += e.height;
        out.entries.push_back(std::move(e));
    }
    out.content = y;

    // THE FRAME: the title, the body and -- when a reply can be made from here -- the button at its foot. It grows
    // with the thread from the size it was given to `kPopupMaxShare` of the pane, and past that the body scrolls.
    const bool hasButton = !replyLabel_.isEmpty() && isActive();
    const double buttonHeight = hasButton ? plain.lineSpacing() + 6.0 : 0.0;
    const double chrome = 2.0 + kPopupTitle + 4.0 + 6.0 + (hasButton ? buttonHeight + 6.0 : 0.0);
    const double paneHeight = view.visible.size.height / std::max(0.01, view.dpr);
    const double cap = std::max(base->height(), paneHeight * kPopupMaxShare);
    const double height = std::clamp(chrome + out.content, base->height(), cap);
    out.box = QRectF(base->left(), base->top(), base->width(), height);
    out.title = QRectF(out.box.left() + 6, out.box.top() + 2, out.box.width() - kPopupClose - 16, kPopupTitle);
    out.close = QRectF(out.box.right() - kPopupClose - 4, out.box.top() + 4, kPopupClose, kPopupClose);
    if (hasButton) {
        const double buttonWidth = plain.horizontalAdvance(replyLabel_) + 16.0;
        out.button = QRectF(out.box.right() - 6 - buttonWidth, out.box.bottom() - 6 - buttonHeight, buttonWidth, buttonHeight);
    }
    const double bodyTop = out.box.top() + kPopupTitle + 4;
    const double bodyBottom = hasButton ? out.button.top() - 6 : out.box.bottom() - 6;
    out.body = QRectF(out.box.left() + 6, bodyTop, out.box.width() - 12, std::max(0.0, bodyBottom - bodyTop));
    const double overflow = std::max(0.0, out.content - out.body.height());
    const auto scrolled = popupScroll_.find(id);
    out.scroll = std::clamp(scrolled != popupScroll_.end() ? scrolled->second : 0.0, 0.0, overflow);
    return out;
}

std::optional<QRectF> AnnotationOverlay::replyButtonRect(const Viewport& view, const AnnotationId& id) const
{
    const std::optional<PopupLayout> popup = popupLayout(view, id);
    if (!popup || popup->button.isEmpty()) return std::nullopt;
    return popup->button;
}

std::optional<AnnotationId> AnnotationOverlay::replyButtonAt(const Viewport& view, QPoint pos) const
{
    if (!isActive() || view.layout == nullptr) return std::nullopt;
    for (const AnnotationId& id : shownPopups(view)) {
        const std::optional<PopupLayout> popup = popupLayout(view, id);
        if (!popup || !popup->box.contains(pos)) continue;
        // The first pop-up under the pointer is the one it is on: its button, or nothing.
        if (popup->button.contains(pos)) return id;
        return std::nullopt;
    }
    return std::nullopt;
}

core::Status AnnotationOverlay::reply(const Viewport& view, const AnnotationId& id, const QString& text)
{
    if (text.trimmed().isEmpty()) return {};   // an empty reply is no reply -- as the Comments pane's
    if (store_ == nullptr)
        return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.command.noSink"), QString()};
    const std::optional<std::size_t> at = store_->find(id);
    if (!at) return core::Error{core::ErrorCode::NotFound, QLatin1String("error.pdf.annotationUnknown"), id.toString()};
    const Annotation root = store_->at(threadRoot(*at));
    // The page's crop box: where a reply to anything but a note is hung, in its right margin (`core::newReply`).
    core::PdfRect pageBox{core::PdfPoint{0, 0}, core::PdfSize{612, 792}};
    if (view.layout != nullptr) {
        for (const core::render::PageSlot& slot : view.layout->pageSlots()) {
            if (slot.pageIndex == root.page) pageBox = slot.cropBox;
        }
    }
    Annotation made = core::newReply(root, pageBox, text, core::kReplyColour, author(), now());
    return run(std::make_unique<core::ReplyToAnnotationCommand>(std::vector<Annotation>{std::move(made)}));
}

bool AnnotationOverlay::scrollPopup(const Viewport& view, QPoint pos, int angleDeltaY)
{
    for (const AnnotationId& id : shownPopups(view)) {
        const std::optional<PopupLayout> popup = popupLayout(view, id);
        if (!popup || !popup->box.contains(pos)) continue;
        const double overflow = std::max(0.0, popup->content - popup->body.height());
        if (overflow <= 0.0 || angleDeltaY == 0) return false;   // nothing to scroll: the page's wheel
        const double line = QFontMetricsF(popupFont()).lineSpacing();
        const double step = -angleDeltaY / 120.0 * kPopupWheelLines * line;
        popupScroll_[id] = std::clamp(popup->scroll + step, 0.0, overflow);
        emit changed();
        return true;   // over a pop-up that scrolls, even at its end: the page does not move under it
    }
    return false;
}

double AnnotationOverlay::popupScroll(const AnnotationId& id) const
{
    const auto at = popupScroll_.find(id);
    return at != popupScroll_.end() ? at->second : 0.0;
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

    // THE SEQUENCE NUMBERS (S6 T9, guide A-72): a badge at the top-left of where each numbered annotation is
    // drawn -- the selection's accent, so it reads as the product's and not the document's.
    if (!numbers_.empty()) {
        const QFont before = painter.font();
        QFont badgeFont = before;
        badgeFont.setBold(true);
        const QFontMetricsF metrics(badgeFont);
        painter.setFont(badgeFont);
        for (const core::render::PageSlot& slot : view.layout->slotsIntersecting(view.visible)) {
            for (const std::size_t index : store_->onPage(slot.pageIndex)) {
                const Annotation& a = store_->at(index);
                const auto number = numbers_.find(a.id);
                if (number == numbers_.end()) continue;
                const std::optional<core::PdfRect> drawn = drawnRect(view, a);
                const std::optional<QRectF> r = drawn ? widgetRectOf(view, a.page, *drawn) : std::nullopt;
                if (!r) continue;
                const QString text = QString::number(number->second);
                const double width = std::max(metrics.height(), metrics.horizontalAdvance(text) + 6.0);
                const QRectF badge(r->left() - width / 2, r->top() - metrics.height() / 2, width, metrics.height());
                painter.setPen(Qt::NoPen);
                painter.setBrush(accent);
                painter.drawRoundedRect(badge, metrics.height() / 2, metrics.height() / 2);
                painter.setPen(Qt::white);
                painter.drawText(badge, Qt::AlignCenter, text);
            }
        }
        painter.setFont(before);
    }

    // THE POP-UPS (D6-28): a thread's -- its root's author and text, then each reply's (I-29) -- never a tile and
    // never a command. Painted last first, so the one a press reaches first is the one on top.
    const std::vector<AnnotationId> shown = shownPopups(view);
    const QColor rule(0x80, 0x70, 0x30);
    for (auto id = shown.rbegin(); id != shown.rend(); ++id) {
        const std::optional<PopupLayout> popup = popupLayout(view, *id);
        const Annotation* a = find(*id);
        if (!popup || a == nullptr) continue;
        const Annotation* root = popup->entries.empty() ? a : find(popup->entries.front().id);
        painter.setPen(QPen(rule, 1.0));
        painter.setBrush(popupColour());
        painter.drawRoundedRect(popup->box, 3, 3);
        painter.setPen(Qt::black);
        QFont font = popupFont();
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(popup->title, Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetricsF(font).elidedText(root != nullptr ? root->author : a->author, Qt::ElideRight,
                                                        popup->title.width()));
        painter.drawLine(popup->close.topLeft(), popup->close.bottomRight());
        painter.drawLine(popup->close.topRight(), popup->close.bottomLeft());

        // THE BODY, clipped and scrolled: each entry, a reply indented under the one it answers.
        painter.save();
        painter.setClipRect(popup->body);
        const double boldLine = QFontMetricsF(font).lineSpacing();
        for (std::size_t i = 0; i < popup->entries.size(); ++i) {
            const PopupEntry& e = popup->entries[i];
            const double indent = e.depth * kPopupIndent;
            const double top = popup->body.top() + e.top - popup->scroll;
            if (top > popup->body.bottom() || top + e.height < popup->body.top()) continue;
            double y = top;
            if (i > 0) {
                painter.setPen(QPen(QColor(rule.red(), rule.green(), rule.blue(), 90), 1.0));
                painter.drawLine(QPointF(popup->body.left() + indent, top - 3), QPointF(popup->body.right() - 6, top - 3));
                painter.setPen(Qt::black);
            }
            if (!e.author.isEmpty()) {
                font.setBold(true);
                painter.setFont(font);
                painter.drawText(QRectF(popup->body.left() + indent, y, popup->body.width() - 6 - indent, boldLine),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 QFontMetricsF(font).elidedText(e.author, Qt::ElideRight, popup->body.width() - 6 - indent));
                y += boldLine;
            }
            font.setBold(false);
            painter.setFont(font);
            painter.drawText(QRectF(popup->body.left() + indent, y, popup->body.width() - 6 - indent, e.height - (y - top) + 1),
                             Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, e.text);
        }
        painter.restore();

        // THE SCROLL MARK, when the thread is taller than the body: where the body is in it.
        const double overflow = popup->content - popup->body.height();
        if (overflow > 0.0 && popup->content > 0.0) {
            const double thumb = std::max(12.0, popup->body.height() * popup->body.height() / popup->content);
            const double travel = popup->body.height() - thumb;
            const QRectF mark(popup->body.right() - 3, popup->body.top() + travel * popup->scroll / overflow, 3, thumb);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(rule.red(), rule.green(), rule.blue(), 140));
            painter.drawRoundedRect(mark, 1.5, 1.5);
        }

        // THE REPLY BUTTON at its foot.
        if (!popup->button.isEmpty()) {
            painter.setPen(QPen(rule, 1.0));
            painter.setBrush(QColor(0xff, 0xfb, 0xe6));
            painter.drawRoundedRect(popup->button, 3, 3);
            font.setBold(false);
            painter.setFont(font);
            painter.setPen(Qt::black);
            painter.drawText(popup->button, Qt::AlignCenter, replyLabel_);
        }
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
    for (auto at = popupScroll_.begin(); at != popupScroll_.end();) at = find(at->first) == nullptr ? popupScroll_.erase(at) : std::next(at);
    if (selectionMoved) emit selectionChanged();
    emit changed();
}

} // namespace rawpage::ui
