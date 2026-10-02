#include "ui/shell/Ribbon.h"

#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"

#include <QApplication>
#include <QColor>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace rawpage::ui {

// =======================================================================================
// RibbonGroup
// =======================================================================================

RibbonGroup::RibbonGroup(QString title, QWidget* parent)
    : QWidget(parent), title_(std::move(title))
{
    setFixedHeight(kRibbonBodyHeight);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(kRibbonGroupMargin, kRibbonGroupMargin, kRibbonGroupMargin, 2);
    column->setSpacing(2);

    body_ = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body_);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(4);
    column->addWidget(body_, 1);

    button_ = new QToolButton(this);
    button_->setText(title_);
    button_->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button_->setPopupMode(QToolButton::InstantPopup);
    button_->setAutoRaise(true);
    button_->setFixedHeight(kRibbonBodyHeight - kRibbonTitleHeight - kRibbonGroupMargin - 2);
    button_->hide();
    // objectName, NOT accessibleName: accessibility left the product on 2026-08-23 (Features.md ->
    // Removed from scope). This is Qt's ordinary widget-identification mechanism, used so the
    // widget suite can name what it found.
    button_->setObjectName(title_);
    column->addWidget(button_, 1);

    caption_ = new QLabel(title_, this);
    caption_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    caption_->setFixedHeight(kRibbonTitleHeight);
    QFont small = caption_->font();
    small.setPointSizeF(small.pointSizeF() * 0.85);
    caption_->setFont(small);
    caption_->setEnabled(false);            // the muted group caption of a real ribbon
    column->addWidget(caption_, 0);

    setObjectName(title_);
    connect(button_, &QToolButton::pressed, this, &RibbonGroup::showPopup);
}

void RibbonGroup::setContent(QWidget* content)
{
    content_ = content;
    content->setParent(body_);
    qobject_cast<QHBoxLayout*>(body_->layout())->addWidget(content);
}

int RibbonGroup::expandedWidth() const
{
    const int content = content_ ? content_->sizeHint().width() : 0;
    const int caption = caption_->fontMetrics().horizontalAdvance(title_) + 12;
    return qMax(content, caption) + 2 * kRibbonGroupMargin;
}

int RibbonGroup::collapsedWidth() const
{
    return button_->fontMetrics().horizontalAdvance(title_) + 24 + 2 * kRibbonGroupMargin;
}

QSize RibbonGroup::sizeHint() const
{
    return {collapsed_ ? collapsedWidth() : expandedWidth(), kRibbonBodyHeight};
}

QSize RibbonGroup::minimumSizeHint() const
{
    return {collapsedWidth(), kRibbonBodyHeight};
}

void RibbonGroup::setCollapsed(bool collapsed)
{
    if (collapsed_ == collapsed) return;
    collapsed_ = collapsed;

    if (collapsed) {
        // Re-parent the SAME content into a frameless popup -- see the header for why this rather
        // than a second copy, and for the tab-order cost it incurs (I-05).
        if (!popup_) {
            popup_ = new QWidget(this, Qt::Popup);
            auto* popupLayout = new QHBoxLayout(popup_);
            popupLayout->setContentsMargins(6, 6, 6, 6);
        }
        if (content_) {
            qobject_cast<QHBoxLayout*>(body_->layout())->removeWidget(content_);
            qobject_cast<QHBoxLayout*>(popup_->layout())->addWidget(content_);
        }
        body_->hide();
        button_->show();
    } else {
        if (popup_) popup_->hide();
        if (content_ && popup_) {
            qobject_cast<QHBoxLayout*>(popup_->layout())->removeWidget(content_);
            content_->setParent(body_);
            qobject_cast<QHBoxLayout*>(body_->layout())->addWidget(content_);
            content_->show();
        }
        button_->hide();
        body_->show();
    }
    updateGeometry();
}

void RibbonGroup::showPopup()
{
    if (!collapsed_ || !popup_) return;
    popup_->adjustSize();
    popup_->move(mapToGlobal(QPoint(0, height())));
    popup_->show();
}

// =======================================================================================
// RibbonPageLayout
// =======================================================================================

RibbonPageLayout::RibbonPageLayout(QWidget* parent) : QLayout(parent)
{
    setContentsMargins(2, 2, 2, 2);
    setSpacing(2);
}

RibbonPageLayout::~RibbonPageLayout()
{
    while (QLayoutItem* item = takeAt(0)) delete item;
}

void RibbonPageLayout::addItem(QLayoutItem* item) { items_.append(item); }
int  RibbonPageLayout::count() const { return static_cast<int>(items_.size()); }

QLayoutItem* RibbonPageLayout::itemAt(int index) const
{
    return (index >= 0 && index < items_.size()) ? items_.at(index) : nullptr;
}

QLayoutItem* RibbonPageLayout::takeAt(int index)
{
    return (index >= 0 && index < items_.size()) ? items_.takeAt(index) : nullptr;
}

QSize RibbonPageLayout::sizeHint() const
{
    int width = 0;
    for (QLayoutItem* item : items_)
        if (auto* group = qobject_cast<RibbonGroup*>(item->widget()))
            width += group->expandedWidth() + spacing();

    const QMargins margins = contentsMargins();
    return {width + margins.left() + margins.right(),
            kRibbonBodyHeight + margins.top() + margins.bottom()};
}

QSize RibbonPageLayout::minimumSize() const
{
    // THE SUM OF THE COLLAPSED GROUPS -- the width below which this row genuinely cannot show its
    // content, and nothing smaller.
    //
    // T10 returned the WIDEST SINGLE collapsed group here, deliberately and for a real reason: this
    // layout's minimum propagated all the way up to the main window, and with the shipped resource
    // -- 49 groups, the widest tab holding eight -- the honest sum held the whole application open
    // at about 1044 logical pixels. On a 1366-wide laptop at 150 % scaling that is wider than the
    // screen, and nothing on screen explained what was holding the window open. Two things hid
    // behind it: the tab strip could never run out of room, so the chevron overflow -- one of the
    // three pieces of named custom work the A3 verdict rests on -- was unreachable in the shipped
    // configuration while its code looked correct. (Issue I-19.)
    //
    // The floor of one group bought a narrowable window at the cost of a row clipped at the window
    // edge. T11 pays that cost off instead of carrying it: `Ribbon` puts this layout inside a
    // QScrollArea, whose own minimum is a scrollbar and a frame, so the number below no longer
    // reaches the window at all -- it tells the scroll area how wide the content is, which is
    // exactly the question a scroll area exists to ask. The window narrows; the row scrolls.
    int width = 0;
    for (QLayoutItem* item : items_)
        if (auto* group = qobject_cast<RibbonGroup*>(item->widget()))
            width += group->collapsedWidth() + spacing();

    const QMargins margins = contentsMargins();
    return {width + margins.left() + margins.right(),
            kRibbonBodyHeight + margins.top() + margins.bottom()};
}

int RibbonPageLayout::collapsedCount() const
{
    int collapsed = 0;
    for (QLayoutItem* item : items_)
        if (auto* group = qobject_cast<RibbonGroup*>(item->widget()))
            if (group->isCollapsed()) ++collapsed;
    return collapsed;
}

void RibbonPageLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    ++relayouts_;

    const QMargins margins   = contentsMargins();
    const int      available = rect.width() - margins.left() - margins.right();

    QList<RibbonGroup*> groups;
    for (QLayoutItem* item : items_)
        if (auto* group = qobject_cast<RibbonGroup*>(item->widget())) groups.append(group);
    if (groups.isEmpty()) return;

    // Collapse the highest-priority group first, one at a time, until the row fits -- compared
    // against the width each candidate state WOULD need, so the decision is a function of the
    // available width alone and not of the order the resizes happened to arrive in. That is the
    // other half of not thrashing.
    QList<RibbonGroup*> byPriority = groups;
    std::stable_sort(byPriority.begin(), byPriority.end(),
                     [](RibbonGroup* a, RibbonGroup* b) {
                         return a->collapsePriority() > b->collapsePriority();
                     });

    const auto widthWith = [&](int collapseCount) {
        int width = 0;
        for (int i = 0; i < byPriority.size(); ++i)
            width += (i < collapseCount ? byPriority[i]->collapsedWidth()
                                        : byPriority[i]->expandedWidth()) + spacing();
        return width;
    };

    int wanted = 0;
    while (wanted < byPriority.size() && widthWith(wanted) > available) ++wanted;

    // The hysteresis band, applied ONLY in the direction that would un-collapse a group. Collapsing
    // promptly is correct -- the alternative is content overhanging the window -- but expanding must
    // wait until there is comfortably room, or the two decisions share a boundary and oscillate.
    const int current = collapsedCount();
    if (wanted < current && widthWith(wanted) > available - kHysteresis) wanted = current;

    for (int i = 0; i < byPriority.size(); ++i) byPriority[i]->setCollapsed(i < wanted);

    int       x = rect.x() + margins.left();
    const int y = rect.y() + margins.top();
    for (RibbonGroup* group : groups) {
        const int width = group->isCollapsed() ? group->collapsedWidth() : group->expandedWidth();
        group->setGeometry(x, y, width, kRibbonBodyHeight);
        x += width + spacing();
    }

    // Re-chain Tab whenever the set of collapsed groups changed -- and only then. Running
    // setTabOrder over every control on every relayout would run on each frame of a drag for no
    // benefit, and a fix that costs a frame is a fix somebody eventually removes.
    int mask = 0;
    for (int i = 0; i < groups.size() && i < 31; ++i)
        if (groups[i]->isCollapsed()) mask |= (1 << i);
    if (mask != collapseMask_) {
        collapseMask_ = mask;
        restoreTabOrder();
    }
}

void RibbonPageLayout::restoreTabOrder()
{
    QWidget* previous = nullptr;

    // Groups in VISUAL order -- which is items_ order, the order they were added, and the order they
    // are placed above. Deliberately NOT byPriority: priority decides what collapses, and letting it
    // also decide Tab order is the bug this function exists to undo (I-05).
    for (QLayoutItem* item : items_) {
        auto* group = qobject_cast<RibbonGroup*>(item->widget());
        if (!group) continue;

        // findChildren walks in child order, which here is creation order, which is left-to-right.
        // A collapsed group's content lives in a Qt::Popup -- a separate window -- so the window()
        // check drops it and leaves the group's own button in the chain instead. That is exactly
        // right: you cannot Tab into a closed popup, and you should be able to Tab onto the button
        // that opens it.
        for (QWidget* child : group->findChildren<QWidget*>()) {
            if (!(child->focusPolicy() & Qt::TabFocus)) continue;
            if (child->window() != group->window()) continue;
            if (previous) QWidget::setTabOrder(previous, child);
            previous = child;
        }
    }
}

// =======================================================================================
// Ribbon
// =======================================================================================

Ribbon::Ribbon(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("ribbon"));

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto* strip = new QWidget(this);
    strip->setObjectName(QStringLiteral("ribbonTabStrip"));
    auto* stripLayout = new QHBoxLayout(strip);
    stripLayout->setContentsMargins(4, 2, 4, 0);
    stripLayout->setSpacing(2);

    tabs_ = new QTabBar(strip);
    tabs_->setExpanding(false);
    tabs_->setDrawBase(false);
    tabs_->setUsesScrollButtons(false);      // R-3 uses a chevron, not arrows

    // AND THEREFORE ITS WIDTH PREFERENCE IS MEANINGLESS TO THE LAYOUT. With scroll buttons off,
    // QTabBar's minimumSizeHint is the width of ALL its tabs -- it has no other way to stay usable
    // -- and that minimum propagates up to the main window. With nine tabs that alone held the
    // window open at 714 logical pixels and, worse, meant the tab strip could never actually run
    // out of room, so the chevron this line exists to enable was unreachable.
    //
    // Ignored, not a hand-set minimum: this widget's preferred width is not a smaller number, it is
    // not a number at all. Overflow is decided by relayoutTabs() from the width the ribbon actually
    // got. Part of issue I-19, and found the same way -- by asking a real ribbon to be narrow.
    tabs_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    tabs_->setFocusPolicy(Qt::TabFocus);     // keyboard operability, retained
    tabs_->setObjectName(QStringLiteral("ribbonTabs"));

    // AND THEREFORE THE TAB BAR CARRIES THE STRETCH ITSELF -- issue I-36, and the other half of the
    // line above. `Ignored` tells QBoxLayout that this widget's size hint is not a number, so the
    // layout has nothing to give it *unless* a stretch factor says how much of the leftover space is
    // its share. With `addWidget(tabs_, 0)` followed by a separate `addStretch(1)`, the whole of the
    // strip's width went to the stretch item and the tab bar was laid out 0 pixels wide -- at every
    // window size, so NOT ONE TAB WAS EVER DRAWN. The corner widget still needs to sit hard right,
    // which it now does because an expanding tab bar pushes it there; the spacer it used to need is
    // what was eating the strip.
    stripLayout->addWidget(tabs_, 1);
    strip_ = stripLayout;

    chevron_ = new QToolButton(strip);
    // Through the catalog, not as a literal. A chevron looks like punctuation rather than text, and
    // that is exactly the reasoning that put an em dash in front of a CJK reader at T9: the glyph a
    // locale uses for "there is more this way" is an editorial decision, not a constant.
    chevron_->setText(catalog::translate(catalog::key::RibbonOverflowGlyph));
    chevron_->setToolTip(catalog::translate(catalog::key::RibbonOverflowTooltip));
    chevron_->setPopupMode(QToolButton::InstantPopup);
    chevron_->setAutoRaise(true);
    chevron_->setObjectName(QStringLiteral("ribbonOverflow"));
    chevron_->setMenu(new QMenu(chevron_));
    chevron_->hide();
    stripLayout->addWidget(chevron_, 0);

    column->addWidget(strip, 0);

    stack_ = new QStackedWidget;
    stack_->setObjectName(QStringLiteral("ribbonPages"));

    // T11, issue I-19. The stack is the scroll area's WIDGET rather than a child of this widget:
    // with `setWidgetResizable`, QScrollArea gives it the viewport's width or its own minimum,
    // whichever is larger, which is precisely "fill the space, and scroll when the space is not
    // enough". The page layout's minimum stops being a constraint on the main window and becomes a
    // statement about the content -- see RibbonPageLayout::minimumSize.
    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("ribbonBodyScroll"));
    scroll_->setWidget(stack_);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_->setFocusPolicy(Qt::NoFocus);   // Tab visits the buttons, never the scroll area

    // THE SCROLLBAR'S HEIGHT IS RESERVED AT EVERY WIDTH, and the ribbon is that much taller for it.
    // The alternative -- growing when the bar appears -- moves the whole window's content by a dozen
    // pixels at exactly the moment somebody is dragging its edge, and a ribbon whose height depends
    // on the width is the flicker R-4 is judged against wearing a different hat. A fixed reservation
    // costs the same twelve pixels at all times and never surprises anyone.
    scroll_->setFixedHeight(kRibbonBodyHeight + 4
                            + scroll_->horizontalScrollBar()->sizeHint().height());
    column->addWidget(scroll_, 0);

    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && index < stack_->count()) stack_->setCurrentIndex(index);
        emit pageChanged(index);
    });
}

int Ribbon::addPage(const QString& title, QWidget* page, bool contextual,
                    const QString& accessKey)
{
    const int index = tabs_->addTab(title);
    accessKeys_.append(accessKey.trimmed().left(1).toUpper());
    stack_->addWidget(page);
    if (contextual) {
        contextualIndex_ = index;
        // R-4: the contextual tab is REMOVED from the strip rather than merely disabled, and the
        // body height is fixed, so appearing and disappearing cannot move the ribbon.
        tabs_->setTabVisible(index, false);
        tabs_->setTabTextColor(index, QColor(0x8A, 0x2B, 0xE2));
    }
    return index;
}

void Ribbon::setContextualVisible(bool visible)
{
    if (contextualIndex_ < 0 || contextualVisible_ == visible) return;
    contextualVisible_ = visible;
    if (!visible && tabs_->currentIndex() == contextualIndex_) tabs_->setCurrentIndex(0);
    tabs_->setTabVisible(contextualIndex_, visible);
    relayoutTabs();
}

int Ribbon::pageCount() const { return tabs_->count(); }

int Ribbon::currentPage() const { return tabs_->currentIndex(); }

void Ribbon::setCurrentPage(int index) { tabs_->setCurrentIndex(index); }

QString Ribbon::pageTitle(int index) const { return tabs_->tabText(index); }

bool Ribbon::hasOverflow() const { return !chevron_->isHidden(); }

bool Ribbon::hasBodyScroll() const
{
    // The scrollbar's RANGE rather than its visibility, for the reason hasOverflow() gives about
    // the chevron: a child of a window that has not been shown is not visible no matter how the
    // layout came out, and a case asserting visibility would be asserting something about the test
    // harness. A maximum above zero means there is content off the right-hand edge.
    return scroll_->horizontalScrollBar()->maximum() > 0;
}

void Ribbon::attachControl(QWidget* control, core::ActionId id)
{
    controlIds_.insert(control, id);
    control->setContextMenuPolicy(Qt::CustomContextMenu);

    // Connected with `this` as the receiver and no destructor disconnect, which is a deliberate
    // departure from the rule I-12 wrote down rather than an oversight of it. That rule exists
    // because `~QWidget` HIDES children, and a signal emitted by hiding reaches a half-destroyed
    // receiver. `customContextMenuRequested` is emitted only by a context-menu event on a live
    // widget; nothing in destruction can raise it.
    connect(control, &QWidget::customContextMenuRequested, this,
            [this, control, id](const QPoint& position) {
                emit controlContextMenuRequested(id, control->mapToGlobal(position));
            });
}

void Ribbon::contextMenuEvent(QContextMenuEvent* event)
{
    // ONLY DISABLED CONTROLS REACH HERE. An enabled one has `CustomContextMenu` and accepts the
    // event itself, so there is no double emission; a disabled one never sees the event at all and
    // Qt hands it to the nearest enabled ancestor, which propagates up to this widget.
    for (QWidget* widget = childAt(event->pos()); widget && widget != this;
         widget = widget->parentWidget()) {
        const auto found = controlIds_.constFind(widget);
        if (found == controlIds_.constEnd()) continue;
        emit controlContextMenuRequested(found.value(), event->globalPos());
        event->accept();
        return;
    }
    QWidget::contextMenuEvent(event);
}

void Ribbon::setCornerWidget(QWidget* widget)
{
    if (corner_ == widget) return;
    if (corner_) {
        strip_->removeWidget(corner_);
        corner_->deleteLater();
    }
    corner_ = widget;
    if (!corner_) return;

    corner_->setParent(strip_->parentWidget());
    // Before the chevron, which stays at the end: the chevron is about the tab strip and belongs
    // where a strip that ran out of room ends, and a search box that jumped position whenever a tab
    // overflowed would be a search box nobody could find twice.
    strip_->insertWidget(strip_->count() - 1, corner_);
}

void Ribbon::setBodyCollapsed(bool collapsed)
{
    if (bodyCollapsed_ == collapsed) return;
    bodyCollapsed_ = collapsed;
    // R-7: the BODY is hidden, not the pages, and the tab bar's current index is untouched -- which
    // is what makes the active tab survive a collapse/restore cycle for free. The thing hidden is
    // the scroll area rather than the stack inside it: hiding the stack alone would leave the scroll
    // area's reserved height as an empty band under the tabs.
    scroll_->setVisible(!collapsed);

    // Hiding a child invalidates this widget's own size hint, but nothing re-runs the PARENT's
    // layout until something asks it to. Without these two lines the ribbon keeps its expanded
    // height with an empty gap where the body was -- caught by the spike's self-test, and a real
    // finding rather than an offscreen artefact: the same gap appears on screen.
    updateGeometry();
    if (QWidget* owner = parentWidget(); owner && owner->layout()) owner->layout()->activate();
}

RibbonPageLayout* Ribbon::layoutOfPage(int index) const
{
    QWidget* page = stack_->widget(index);
    return page ? qobject_cast<RibbonPageLayout*>(page->layout()) : nullptr;
}

void Ribbon::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    relayoutTabs();
}

void Ribbon::relayoutTabs()
{
    // R-3, named custom work #2. QTabBar's own overflow is a pair of scroll arrows, which is not
    // what a ribbon does -- so hidden tabs are offered from a chevron menu instead.
    const int corner = corner_ ? corner_->sizeHint().width() + strip_->spacing() : 0;
    const int budget = width() - chevron_->sizeHint().width() - corner - 16;

    int  used      = 0;
    bool anyHidden = false;
    for (int i = 0; i < tabs_->count(); ++i) {
        if (i == contextualIndex_ && !contextualVisible_) continue;
        const int  tabWidth = tabs_->tabRect(i).width();
        const bool fits     = (used + tabWidth) <= budget || i == tabs_->currentIndex();
        tabs_->setTabVisible(i, fits);
        if (fits) used += tabWidth; else anyHidden = true;
    }

    chevron_->setVisible(anyHidden);
    if (anyHidden) rebuildOverflowMenu();
}

void Ribbon::rebuildOverflowMenu()
{
    QMenu* menu = chevron_->menu();
    menu->clear();
    for (int i = 0; i < tabs_->count(); ++i) {
        if (tabs_->isTabVisible(i)) continue;
        if (i == contextualIndex_ && !contextualVisible_) continue;

        // Through ActionFactory, even though a ribbon tab is NOT a command. `addAction(QString)`
        // builds a QAction internally, and layer-lint's `bare-qaction` rule allows that in exactly
        // one file -- so the construction happens there and comes back stamped as non-command,
        // rather than this file being carved out of the rule. See ActionFactory.h.
        QAction* action = ActionFactory::addNonCommandMenuItem(menu, tabs_->tabText(i));
        connect(action, &QAction::triggered, this, [this, i] { setCurrentPage(i); });
    }
}

// --- R-5: key tips ---------------------------------------------------------------------
// Two levels, dismissable with Esc. Drawn as child QLabels rather than painted into the tab bar,
// because a key tip has to overlay a control it does not own -- and a real ribbon's tips sit over
// buttons inside a group, which the tab bar cannot paint into at all.
void Ribbon::showKeyTips()
{
    hideKeyTips();
    tipLevel_ = 1;
    tipTargets_.clear();

    // The ribbon takes focus so the next key lands in keyPressEvent(). Remembered rather than
    // assumed: the tips are dismissed with Esc and the person expects to be back where they were,
    // and offscreen there may be no focus widget at all, which QPointer handles by staying null.
    focusBeforeTips_ = QApplication::focusWidget();
    setFocusPolicy(Qt::StrongFocus);
    setFocus(Qt::OtherFocusReason);
    qApp->installEventFilter(this);   // I-16: a mouse press anywhere dismisses them

    for (int i = 0; i < tabs_->count(); ++i) {
        if (!tabs_->isTabVisible(i)) continue;

        // FROM THE CATALOG, never from the title (I-42). A tab with no access key gets no tip --
        // `RibbonTabBuilder` has already reported it as a problem, and inventing one here would
        // hide that report behind a letter nobody chose.
        const QString letter = accessKeyOfPage(i);
        if (letter.isEmpty()) continue;
        const int key = letter.at(0).unicode();
        if (tipTargets_.contains(key)) continue;   // first wins; the suite asserts there are none
        tipTargets_.insert(key, i);

        auto* tip = new QLabel(letter, this);
        tip->setObjectName(QStringLiteral("ribbonKeyTip"));
        tip->setFrameShape(QFrame::Box);
        tip->setAlignment(Qt::AlignCenter);
        tip->setAutoFillBackground(true);
        tip->setFixedSize(18, 16);
        const QRect rect = tabs_->tabRect(i);
        tip->move(tabs_->mapTo(this, rect.bottomLeft()) + QPoint(rect.width() / 2 - 9, -4));
        tip->show();
        tips_.append(tip);
    }
}

void Ribbon::hideKeyTips()
{
    dismissKeyTips(true);
}

void Ribbon::dismissKeyTips(bool restoreFocus)
{
    // The level first, so the focus change below -- which raises `focusOutEvent` -- finds nothing to do.
    clearTips();
    tipLevel_ = 0;
    qApp->removeEventFilter(this);

    // Focus goes back where it was, and the ribbon stops being focusable -- it is not a control and
    // must not join the tab order (I-05 is about exactly that order).
    setFocusPolicy(Qt::NoFocus);
    if (restoreFocus && focusBeforeTips_) focusBeforeTips_->setFocus(Qt::OtherFocusReason);
    focusBeforeTips_.clear();
}

void Ribbon::clearTips()
{
    // LATER, not now: a press on a tip reaches `eventFilter` with that tip as its receiver, and deleting
    // the receiver inside its own delivery is a use after free. Hidden at once, so nothing shows them.
    for (QWidget* tip : std::as_const(tips_)) {
        tip->hide();
        tip->deleteLater();
    }
    tips_.clear();
}

void Ribbon::focusOutEvent(QFocusEvent* event)
{
    if (tipLevel_ != 0) dismissKeyTips(false);
    QWidget::focusOutEvent(event);
}

bool Ribbon::eventFilter(QObject* watched, QEvent* event)
{
    if (tipLevel_ != 0) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::NonClientAreaMouseButtonPress:
            // The focus goes back first, and the press then moves it on if what it lands on takes it --
            // so a press on a widget that takes none leaves the person where they were, not on a ribbon
            // nothing can type into.
            dismissKeyTips(true);
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QString Ribbon::accessKeyOfPage(int index) const
{
    return (index >= 0 && index < accessKeys_.size()) ? accessKeys_.at(index) : QString();
}

void Ribbon::keyPressEvent(QKeyEvent* event)
{
    // Only while the tips are up. With them down this widget does not hold focus at all, so this is
    // belt and braces rather than a branch anyone reaches.
    if (tipLevel_ != 0 && handleKeyTip(event->key())) {
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool Ribbon::handleKeyTip(int key)
{
    if (tipLevel_ == 0) return false;

    if (key == Qt::Key_Escape) {
        if (tipLevel_ == 2) { showKeyTips(); return true; }   // back out one level
        hideKeyTips();
        return true;
    }

    if (tipLevel_ == 1) {
        const auto found = tipTargets_.constFind(key);
        if (found == tipTargets_.constEnd()) return false;
        setCurrentPage(found.value());

        // Level two: tips over the groups of the tab just chosen.
        clearTips();
        tipLevel_ = 2;
        QWidget* page = stack_->currentWidget();
        if (!page) return true;
        const QList<RibbonGroup*> groups = page->findChildren<RibbonGroup*>();
        for (int i = 0; i < groups.size(); ++i) {
            auto* tip = new QLabel(QString::number(i + 1), this);
            tip->setObjectName(QStringLiteral("ribbonKeyTip"));
            tip->setFrameShape(QFrame::Box);
            tip->setAlignment(Qt::AlignCenter);
            tip->setAutoFillBackground(true);
            tip->setFixedSize(18, 16);
            tip->move(groups[i]->mapTo(this, QPoint(4, 4)));
            tip->show();
            tips_.append(tip);
        }
        return true;
    }

    hideKeyTips();
    return true;
}

} // namespace rawpage::ui
