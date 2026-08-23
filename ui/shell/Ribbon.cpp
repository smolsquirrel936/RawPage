#include "ui/shell/Ribbon.h"

#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"

#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QResizeEvent>
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
    // THE WIDEST SINGLE COLLAPSED GROUP -- deliberately NOT the sum of them all.
    //
    // Summing is the obvious answer and it is wrong, and it took a real ribbon to show why. This
    // layout's minimum propagates all the way up to the main window, so with the resource as
    // shipped -- 49 groups, the widest tab holding eight -- the sum made the whole application
    // un-narrowable below about 1044 logical pixels. On a 1366-wide laptop at 150 % scaling that is
    // wider than the screen, and the user cannot resize their way out of it because nothing on
    // screen explains what is holding the window open.
    //
    // Two things went unnoticed behind it, which is what makes this worth the paragraph. Snapping
    // back to 1044 on a resize to 240 also meant the tab strip could never run out of room, so the
    // chevron overflow -- one of the three pieces of named custom work the A3 verdict rests on --
    // was unreachable in the shipped configuration while its code looked perfectly correct. The
    // spike never saw either: it had four groups.
    //
    // So the floor is one group. Collapse absorbs the first shortfall; past that the row is clipped
    // at the window edge, which is what a narrow ribbon does in the reference (P-4). A horizontal
    // scroll for the body is the better answer and it is a real feature rather than a line here --
    // it belongs with the QAT work at T11. Issue I-19.
    int widest = 0;
    for (QLayoutItem* item : items_)
        if (auto* group = qobject_cast<RibbonGroup*>(item->widget()))
            widest = qMax(widest, group->collapsedWidth() + spacing());

    const QMargins margins = contentsMargins();
    return {widest + margins.left() + margins.right(),
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
    stripLayout->addWidget(tabs_, 0);
    stripLayout->addStretch(1);

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

    stack_ = new QStackedWidget(this);
    stack_->setObjectName(QStringLiteral("ribbonPages"));
    stack_->setFixedHeight(kRibbonBodyHeight + 4);
    column->addWidget(stack_, 0);

    connect(tabs_, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0 && index < stack_->count()) stack_->setCurrentIndex(index);
        emit pageChanged(index);
    });
}

int Ribbon::addPage(const QString& title, QWidget* page, bool contextual)
{
    const int index = tabs_->addTab(title);
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

void Ribbon::setBodyCollapsed(bool collapsed)
{
    if (bodyCollapsed_ == collapsed) return;
    bodyCollapsed_ = collapsed;
    // R-7: the STACK is hidden, not the pages, and the tab bar's current index is untouched -- which
    // is what makes the active tab survive a collapse/restore cycle for free.
    stack_->setVisible(!collapsed);

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
    const int budget = width() - chevron_->sizeHint().width() - 16;

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

    for (int i = 0; i < tabs_->count(); ++i) {
        if (!tabs_->isTabVisible(i)) continue;
        const QString text = tabs_->tabText(i);
        if (text.isEmpty()) continue;
        const int key = text.at(0).toUpper().unicode();
        tipTargets_.insert(key, i);

        auto* tip = new QLabel(QString(QChar(key)), this);
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
    qDeleteAll(tips_);
    tips_.clear();
    tipLevel_ = 0;
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
        qDeleteAll(tips_);
        tips_.clear();
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
