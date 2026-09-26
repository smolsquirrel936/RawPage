#include "ui/viewer/SplitViewContainer.h"

#include "core/render/PagePresentation.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ViewportController.h"

#include <QEvent>
#include <QPainter>
#include <QSplitter>
#include <QSplitterHandle>
#include <QVBoxLayout>

#include <algorithm>

namespace rawpage::ui {
namespace {

// How much of the window the frozen row and the frozen column take when a spreadsheet split is
// made. A quarter is a guess about a person's table, not a measurement -- which is exactly why the
// handles are real `QSplitter` handles and this is only where they START.
constexpr int kFrozenPercent = 25;

// The two panes of a fresh two-pane split, in `QSplitter`'s arbitrary stretch units. Equal halves,
// and the person moves the handle from there.
constexpr int kEqualShare = 1000;

// Device pixels per PDF point for a view, or 0 when it has not been laid out yet. The mirror
// divides by it, so "not laid out" has to be answerable rather than assumed non-zero.
double presentedScaleOf(const PageView& view)
{
    return view.layoutEngine().layout().presentedScale();
}

// **THE DIVIDER IS PAINTED HERE, NOT BY THE STYLE -- I-12.** A stock `QSplitter` handle is a few
// pixels of `QPalette::Window`, and so is the desk every `PageView` paints around its pages
// (`PageView::paintEvent`): the two are the same colour by construction, and M-01's walk found the
// split had no visible boundary at all. The native style paints from the COLOUR SCHEME rather than
// the palette (convention 21), so asking it for a different role would be a request it may ignore;
// a handle that paints itself answers to the palette `ThemeManager` writes and to nothing else.
//
// The band is `QPalette::Mid` -- the `Border` token -- with a one-pixel line down its middle in the
// `SubduedText` token, and both are measured against the desk in BOTH themes (convention 20;
// `tst_split.cpp`). Under the pointer and while dragged the whole handle is `QPalette::Link` -- the
// `Accent` token -- so a person can see what they have hold of before they move it.
constexpr int kHandleWidth = 6;   // logical pixels; a stock handle on Windows 11 is about 3

class DividerHandle final : public QSplitterHandle {
public:
    using QSplitterHandle::QSplitterHandle;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        if (hovered_ || pressed_) {
            painter.fillRect(rect(), palette().color(QPalette::Link));
            return;
        }
        painter.fillRect(rect(), palette().color(QPalette::Mid));

        // THE LINE DOWN THE MIDDLE, in `QPalette::PlaceholderText` -- the `SubduedText` token. The
        // band alone measured deltaE 12.4 against the light desk, over the floor of 10 and still
        // pale on a 4K panel; the line is what reads as a boundary at a glance.
        const QColor line = palette().color(QPalette::PlaceholderText);
        if (orientation() == Qt::Horizontal)   // panes side by side: a vertical line
            painter.fillRect(width() / 2, 0, 1, height(), line);
        else
            painter.fillRect(0, height() / 2, width(), 1, line);
    }
    void enterEvent(QEnterEvent* event) override
    {
        hovered_ = true;
        update();
        QSplitterHandle::enterEvent(event);
    }
    void leaveEvent(QEvent* event) override
    {
        hovered_ = false;
        update();
        QSplitterHandle::leaveEvent(event);
    }
    void mousePressEvent(QMouseEvent* event) override
    {
        pressed_ = true;
        update();
        QSplitterHandle::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        pressed_ = false;
        update();
        QSplitterHandle::mouseReleaseEvent(event);
    }

private:
    bool hovered_ = false;
    bool pressed_ = false;
};

// Every splitter of a split -- the two-pane one and all three of a spreadsheet split's -- is one of
// these, so there is one divider and not three that could drift apart.
class DividerSplitter final : public QSplitter {
public:
    DividerSplitter(Qt::Orientation orientation, QWidget* parent) : QSplitter(orientation, parent)
    {
        setHandleWidth(kHandleWidth);
        setChildrenCollapsible(false);
    }

protected:
    QSplitterHandle* createHandle() override { return new DividerHandle(orientation(), this); }
};

} // namespace

SplitViewContainer::SplitViewContainer(core::render::RenderScheduler& scheduler,
                                       const platform::IDisplayTopology* topology, QWidget* parent)
    : QWidget(parent), scheduler_(scheduler), topology_(topology)
{
    setObjectName(QStringLiteral("splitViewContainer"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    // THE PRIMARY VIEW IS BUILT HERE AND `viewAdded` IS NOT EMITTED FOR IT. Nothing is connected
    // yet -- this runs inside the constructor -- so an emit would reach nobody, and a caller that
    // expected one would be silently un-adopted. `DocumentWindow` adopts `primaryView()` directly
    // and connects the signal for the panes a split adds later.
    PageView* primary = makeView();
    column->addWidget(primary);
    active_ = primary;
}

SplitViewContainer::~SplitViewContainer()
{
    // Convention 10: every view's `FocusIn` filter and every controller's `changed` lambda holds
    // `this`, and `~QWidget` deletes the children AFTER this body has run -- so a view that emitted
    // either on its way out would call into a half-destroyed container. Both are removed here
    // rather than relied on to be harmless.
    for (PageView* view : views_) {
        view->removeEventFilter(this);
        disconnect(&view->controller(), nullptr, this, nullptr);
    }
}

PageView* SplitViewContainer::makeView()
{
    auto* view = new PageView(scheduler_, topology_, this);
    views_.push_back(view);
    observe(view);
    return view;
}

void SplitViewContainer::observe(PageView* view)
{
    view->installEventFilter(this);

    // THE CONTROLLER, NOT `PageView::viewChanged`. `viewChanged` carries the page, the page count
    // and the zoom -- not the scroll -- and it is emitted from `refresh()`, which is what writing a
    // follower's scroll would provoke. The controller's own `changed` is the one signal that fires
    // for every state change including a plain scroll, which is the whole quantity being mirrored.
    connect(&view->controller(), &ViewportController::changed, this,
            [this, view] { propagateScroll(view); });
}

bool SplitViewContainer::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn) {
        // `std::find` over a vector of at most four, rather than a `qobject_cast` and a parent
        // walk: the filter is installed on the views themselves, so a match is exact and a focus
        // event from anything else is left entirely alone.
        const auto it = std::find(views_.begin(), views_.end(), watched);
        if (it != views_.end()) setActive(static_cast<PageView*>(watched));
    }
    return QWidget::eventFilter(watched, event);
}

void SplitViewContainer::setActive(PageView* view)
{
    if (view == nullptr || view == active_) return;
    active_ = view;
    emit activeViewChanged(view);
}

void SplitViewContainer::setSynchronizedScrolling(bool on)
{
    if (synchronized_ == on) return;
    synchronized_ = on;

    // TURNING IT ON RE-ALIGNS IMMEDIATELY. A synchronization that only took effect at the next
    // scroll would leave two panes visibly out of step for as long as nobody touched them, which
    // reads as a switch that did nothing.
    if (on && mode_ != Mode::Spreadsheet) propagateScroll(active_);
}

void SplitViewContainer::propagateScroll(PageView* leader)
{
    if (mirroring_ || leader == nullptr) return;

    // S4 T11. ANNOUNCED BEFORE THE `views_.size() < 2` RETURN, because the cross-window mirror's
    // commonest subject is a document that is not split at all -- and after the `mirroring_` guard,
    // so a pane this container has just written does not go on to lead a view in another window.
    emit scrolled(leader);

    if (views_.size() < 2) return;

    const core::DevicePoint from = leader->controller().scroll();
    const double leaderScale = presentedScaleOf(*leader);

    // A pane that has not been laid out has no scale to convert through. Mirroring into it with an
    // assumed 1.0 would put it at the leader's DEVICE offset under its own zoom, which is the
    // precise bug the header's point-space argument exists to avoid.
    if (leaderScale <= 0.0) return;

    mirroring_ = true;

    // The follower's offset is the leader's carried through PDF-point space. `ratio` is exactly
    // 1.0 whenever the two panes are at the same zoom, so the common case is a copy.
    const auto mirror = [&](PageView* follower, bool x, bool y) {
        if (follower == nullptr || follower == leader) return;
        const double scale = presentedScaleOf(*follower);
        if (scale <= 0.0) return;
        const double ratio = scale / leaderScale;

        core::DevicePoint to = follower->controller().scroll();
        if (x) to.x = from.x * ratio;
        if (y) to.y = from.y * ratio;
        follower->controller().setScroll(to);
    };

    if (mode_ == Mode::Spreadsheet) {
        // ONLY THE BODY LEADS. A frozen pane cannot be scrolled by the person -- its scrollbars
        // are its own and it has no gesture of its own worth following -- and letting one lead
        // would make the freeze depend on which pane was touched last.
        if (leader == primaryView()) {
            mirror(frozenRow_, /*x=*/true, /*y=*/false);
            mirror(frozenColumn_, /*x=*/false, /*y=*/true);
        }
    } else if (synchronized_) {
        for (PageView* follower : views_) mirror(follower, /*x=*/true, /*y=*/true);
    }

    mirroring_ = false;
}

void SplitViewContainer::setMode(Mode mode)
{
    if (mode == mode_) return;

    teardown();
    mode_ = mode;

    switch (mode) {
    case Mode::None:        break;                 // `teardown()` has already left one view here
    case Mode::Horizontal:
    case Mode::Vertical:    buildTwoPane(mode); break;
    case Mode::Spreadsheet: buildSpreadsheet(); break;
    }

    // THE PANE THE PERSON WAS IN SURVIVES THE CHANGE. `teardown()` retires everything but the
    // primary, so whatever was active may be gone; re-pointing at the primary here rather than
    // leaving a dangling pointer is the same reason `retire()` emits `viewRetired` first.
    setActive(primaryView());
    primaryView()->setFocus(Qt::OtherFocusReason);
}

void SplitViewContainer::buildTwoPane(Mode mode)
{
    // INVERTED ON PURPOSE -- see the header. A HORIZONTAL divider stacks the panes VERTICALLY.
    outer_ = new DividerSplitter(mode == Mode::Horizontal ? Qt::Vertical : Qt::Horizontal, this);

    PageView* second = makeView();

    // EXPLICITLY OUT OF THE LAYOUT FIRST. `QLayout` does drop an item whose widget is reparented
    // away, through the `ChildRemoved` event -- but relying on that leaves the intent unstated and
    // the failure, a stale layout item over a widget in another parent, is invisible until a
    // resize.
    layout()->removeWidget(primaryView());

    outer_->addWidget(primaryView());
    outer_->addWidget(second);
    outer_->setSizes({kEqualShare, kEqualShare});

    layout()->addWidget(outer_);
    outer_->show();

    // ADOPTED AFTER IT IS IN THE LAYOUT, so the document arrives at a pane that has a size. A
    // `setDocument` on a zero-height widget lays out nothing and asks for no tiles, and the pane
    // would stay blank until something else resized it.
    emit viewAdded(second);
    propagateScroll(primaryView());
}

void SplitViewContainer::buildSpreadsheet()
{
    // THE FREEZE IS TAKEN WHERE THE PERSON IS. Freezing at the document's origin instead would
    // show the top of page 1 in the frozen row no matter how far down the body was, which is not
    // what a frozen header row means to anyone who has used one.
    freezeOrigin_ = primaryView()->controller().scroll();

    corner_ = makeView();
    frozenRow_ = makeView();
    frozenColumn_ = makeView();

    innerTop_ = new DividerSplitter(Qt::Horizontal, this);
    innerBottom_ = new DividerSplitter(Qt::Horizontal, this);
    outer_ = new DividerSplitter(Qt::Vertical, this);

    layout()->removeWidget(primaryView());      // as `buildTwoPane` does, and for the same reason

    innerTop_->addWidget(corner_);
    innerTop_->addWidget(frozenRow_);
    innerBottom_->addWidget(frozenColumn_);
    innerBottom_->addWidget(primaryView());

    outer_->addWidget(innerTop_);
    outer_->addWidget(innerBottom_);

    const int frozen = kEqualShare * kFrozenPercent / 100;
    const int body = kEqualShare - frozen;
    innerTop_->setSizes({frozen, body});
    innerBottom_->setSizes({frozen, body});
    outer_->setSizes({frozen, body});

    // THE TWO ROWS' HANDLES MOVE TOGETHER, or the frozen column is one width above the split line
    // and another below it -- which is not a frozen column, it is two panes that happen to be on
    // the left. The guard is the same SHAPE as the scroll mirror's and a different flag:
    // `setSizes` makes the other splitter emit `splitterMoved` straight back.
    const auto linkHandles = [this](QSplitter* moved, QSplitter* other) {
        connect(moved, &QSplitter::splitterMoved, this, [this, moved, other](int, int) {
            if (linkingHandles_) return;
            linkingHandles_ = true;
            other->setSizes(moved->sizes());
            linkingHandles_ = false;
        });
    };
    linkHandles(innerTop_, innerBottom_);
    linkHandles(innerBottom_, innerTop_);

    layout()->addWidget(outer_);
    outer_->show();

    for (PageView* view : {corner_, frozenRow_, frozenColumn_}) emit viewAdded(view);

    // THE THREE FROZEN PANES ARE PLACED ONCE, HERE, and then only ever follow. `propagateScroll`
    // moves the two that track an axis; the corner is written directly because nothing ever moves
    // it again and a mirror with both axes frozen has nothing to say.
    mirroring_ = true;
    corner_->controller().setScroll(freezeOrigin_);
    frozenRow_->controller().setScroll(core::DevicePoint{primaryView()->controller().scroll().x, freezeOrigin_.y});
    frozenColumn_->controller().setScroll(core::DevicePoint{freezeOrigin_.x, primaryView()->controller().scroll().y});
    mirroring_ = false;
}

void SplitViewContainer::retire(PageView* view)
{
    // ANNOUNCED BEFORE ANYTHING IS DONE TO IT, so a holder drops the pointer while it is still a
    // live view rather than after it has been emptied.
    emit viewRetired(view);

    view->removeEventFilter(this);
    disconnect(&view->controller(), nullptr, this, nullptr);

    views_.erase(std::remove(views_.begin(), views_.end(), view), views_.end());

    // THE TICKETS GO NOW, THE WIDGET GOES LATER -- guide case A-56, and the header argues both
    // halves. An empty document drops every tile and cancels every ticket through
    // `TilePresenter::setDocument`.
    view->setDocument(core::DocumentHandle{}, core::Revision::initial(), nullptr);

    view->hide();
    view->setParent(nullptr);
    view->deleteLater();
}

void SplitViewContainer::teardown()
{
    if (outer_ == nullptr) return;

    // THE PRIMARY COMES OUT OF THE SPLITTER FIRST. Deleting a `QSplitter` deletes its children,
    // and the primary is the one view that must survive every mode change.
    PageView* primary = primaryView();
    primary->setParent(this);

    // AND IT BECOMES ACTIVE BEFORE ANYTHING IS RETIRED, so nothing holding `activeView()` is left
    // pointing at a pane that is about to be emptied. `viewRetired` tells a holder to drop the
    // pane; this tells it what to hold instead, and the order of the two is what keeps the gap
    // between them from existing.
    setActive(primary);

    // A copy, because `retire()` erases from `views_` as it goes.
    const std::vector<PageView*> retiring(views_.begin() + 1, views_.end());
    for (PageView* view : retiring) retire(view);

    // The splitters are detached and deferred exactly as a view is, and for the same reason: this
    // can run from inside an application event filter that is still delivering a key press.
    for (QSplitter* splitter : {innerTop_, innerBottom_, outer_}) {
        if (splitter == nullptr) continue;
        splitter->hide();
        splitter->setParent(nullptr);
        splitter->deleteLater();
    }
    outer_ = innerTop_ = innerBottom_ = nullptr;
    corner_ = frozenRow_ = frozenColumn_ = nullptr;

    layout()->addWidget(primary);
    primary->show();
    mode_ = Mode::None;
}

} // namespace rawpage::ui
