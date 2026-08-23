#include "SpikeRibbon.h"

#include <QApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace spike {

// =======================================================================================
// RibbonGroup
// =======================================================================================

RibbonGroup::RibbonGroup(QString title, QWidget* parent)
    : QWidget(parent), title_(std::move(title))
{
    setFixedHeight(kBodyHeight);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(kGroupMargin, kGroupMargin, kGroupMargin, 2);
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
    button_->setFixedHeight(kBodyHeight - kTitleHeight - kGroupMargin - 2);
    button_->hide();
    // objectName, NOT accessibleName: accessibility was removed from the product on 2026-08-23
    // (Features.md -> Removed from scope). This is Qt's ordinary widget-identification mechanism,
    // used here so the focus-order self-test and --print-tab-order can name what they found.
    button_->setObjectName(title_);
    column->addWidget(button_, 1);

    caption_ = new QLabel(title_, this);
    caption_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    caption_->setFixedHeight(kTitleHeight);
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
    return qMax(content, caption) + 2 * kGroupMargin;
}

int RibbonGroup::collapsedWidth() const
{
    return button_->fontMetrics().horizontalAdvance(title_) + 24 + 2 * kGroupMargin;
}

QSize RibbonGroup::sizeHint() const
{
    return {collapsed_ ? collapsedWidth() : expandedWidth(), kBodyHeight};
}

QSize RibbonGroup::minimumSizeHint() const
{
    return {collapsedWidth(), kBodyHeight};
}

void RibbonGroup::setCollapsed(bool collapsed)
{
    if (collapsed_ == collapsed) return;
    collapsed_ = collapsed;

    if (collapsed) {
        // Re-parent the SAME content into a frameless popup. The alternative -- building a
        // second copy of the content for the popup -- is what makes the two drift apart by
        // Stage 9, and it is why this is done by re-parenting even though re-parenting is
        // the fiddlier of the two.
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
        if (content_) {
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
    for (QLayoutItem* item : items_) {
        if (auto* group = qobject_cast<RibbonGroup*>(item->widget()))
            width += group->expandedWidth() + spacing();
    }
    const QMargins m = contentsMargins();
    return {width + m.left() + m.right(), kBodyHeight + m.top() + m.bottom()};
}

QSize RibbonPageLayout::minimumSize() const
{
    int width = 0;
    for (QLayoutItem* item : items_) {
        if (auto* group = qobject_cast<RibbonGroup*>(item->widget()))
            width += group->collapsedWidth() + spacing();
    }
    const QMargins m = contentsMargins();
    return {width + m.left() + m.right(), kBodyHeight + m.top() + m.bottom()};
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

    const QMargins m = contentsMargins();
    const int available = rect.width() - m.left() - m.right();

    QList<RibbonGroup*> groups;
    for (QLayoutItem* item : items_)
        if (auto* group = qobject_cast<RibbonGroup*>(item->widget())) groups.append(group);
    if (groups.isEmpty()) return;

    // Collapse the highest-priority group first, one at a time, until the row fits. Compared
    // against the width each candidate state WOULD need, so the decision is a function of the
    // available width alone and not of the order the resizes happened to arrive in -- which is
    // the other half of not thrashing.
    QList<RibbonGroup*> byPriority = groups;
    std::stable_sort(byPriority.begin(), byPriority.end(),
                     [](RibbonGroup* a, RibbonGroup* b) { return a->priority() > b->priority(); });

    auto widthWith = [&](int collapseCount) {
        int width = 0;
        for (int i = 0; i < byPriority.size(); ++i)
            width += (i < collapseCount ? byPriority[i]->collapsedWidth()
                                        : byPriority[i]->expandedWidth()) + spacing();
        return width;
    };

    int wanted = 0;
    while (wanted < byPriority.size() && widthWith(wanted) > available) ++wanted;

    // The hysteresis band, applied only in the direction that would UN-collapse a group.
    // Collapsing promptly is correct -- the alternative is content overhanging the window --
    // but expanding must wait until there is comfortably room, or the two decisions share a
    // boundary and oscillate. See the header.
    const int current = collapsedCount();
    if (wanted < current && widthWith(wanted) > available - kHysteresis)
        wanted = current;

    for (int i = 0; i < byPriority.size(); ++i)
        byPriority[i]->setCollapsed(i < wanted);

    int x = rect.x() + m.left();
    const int y = rect.y() + m.top();
    for (RibbonGroup* group : groups) {
        const int width = group->isCollapsed() ? group->collapsedWidth() : group->expandedWidth();
        group->setGeometry(x, y, width, kBodyHeight);
        x += width + spacing();
    }

    // Re-chain Tab whenever the set of collapsed groups changed. Only then: setTabOrder over
    // every control on every relayout would run on each frame of a drag for no benefit.
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

    // Groups in VISUAL order -- which is items_ order, the order they were added, and the order
    // they are placed above. Deliberately not byPriority: priority decides what collapses, and
    // letting it also decide Tab order is the bug this function exists to undo.
    for (QLayoutItem* item : items_) {
        auto* group = qobject_cast<RibbonGroup*>(item->widget());
        if (!group) continue;

        // findChildren walks in child order, so within a group this is creation order, which is
        // left-to-right for every group here. A collapsed group's content lives in a Qt::Popup
        // -- a separate window -- so the window() check drops it and leaves the group's own
        // button in the chain instead, which is exactly right: you cannot Tab into a closed
        // popup, and you should be able to Tab onto the button that opens it.
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
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto* strip = new QWidget(this);
    auto* stripLayout = new QHBoxLayout(strip);
    stripLayout->setContentsMargins(4, 2, 4, 0);
    stripLayout->setSpacing(2);

    tabs_ = new QTabBar(strip);
    tabs_->setExpanding(false);
    tabs_->setDrawBase(false);
    tabs_->setUsesScrollButtons(false);      // R-3 uses a chevron, not arrows
    tabs_->setFocusPolicy(Qt::TabFocus);     // keyboard operability, retained
    tabs_->setObjectName(QStringLiteral("Ribbon tabs"));
    stripLayout->addWidget(tabs_, 0);
    stripLayout->addStretch(1);

    chevron_ = new QToolButton(strip);
    chevron_->setText(QStringLiteral("»"));
    chevron_->setPopupMode(QToolButton::InstantPopup);
    chevron_->setAutoRaise(true);
    chevron_->setObjectName(QStringLiteral("More tabs"));
    chevron_->setMenu(new QMenu(chevron_));
    chevron_->hide();
    stripLayout->addWidget(chevron_, 0);

    column->addWidget(strip, 0);

    stack_ = new QStackedWidget(this);
    stack_->setFixedHeight(kBodyHeight + 4);
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
        // R-4: the contextual tab is REMOVED from the strip rather than merely disabled, and
        // the body height is fixed, so appearing and disappearing cannot move the ribbon.
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

int  Ribbon::currentPage() const { return tabs_->currentIndex(); }

void Ribbon::setCurrentPage(int index) { tabs_->setCurrentIndex(index); }

void Ribbon::setBodyCollapsed(bool collapsed)
{
    if (bodyCollapsed_ == collapsed) return;
    bodyCollapsed_ = collapsed;
    // R-7: the STACK is hidden, not the pages, and the tab bar's current index is untouched,
    // which is what makes the active tab survive a collapse/restore cycle for free.
    stack_->setVisible(!collapsed);

    // Hiding a child invalidates this widget's own size hint, but nothing re-runs the PARENT's
    // layout until something asks it to. Without these two lines the ribbon keeps its expanded
    // height with an empty gap where the body was -- which is what the self-test caught, and
    // is a real finding rather than an offscreen artefact: the same gap appears on screen.
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
    // R-3: hide the tabs that do not fit and offer them from a chevron menu. QTabBar's own
    // overflow is a pair of scroll arrows, which is not what a ribbon does -- so this is a
    // named piece of custom work rather than a stock behaviour.
    const int budget = width() - chevron_->sizeHint().width() - 16;

    int used = 0;
    bool anyHidden = false;
    for (int i = 0; i < tabs_->count(); ++i) {
        if (i == contextualIndex_ && !contextualVisible_) continue;
        const int tabWidth = tabs_->tabRect(i).width();
        const bool fits = (used + tabWidth) <= budget || i == tabs_->currentIndex();
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
        QAction* action = menu->addAction(tabs_->tabText(i));
        connect(action, &QAction::triggered, this, [this, i] { setCurrentPage(i); });
    }
}

// --- R-5: key tips ---------------------------------------------------------------------
// Two levels, dismissable with Esc. Drawn as child QLabels rather than painted into the tab
// bar, because a key tip has to overlay a control it does not own -- and a real ribbon's tips
// sit over buttons inside a group, which the tab bar cannot paint into at all.
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

} // namespace spike
