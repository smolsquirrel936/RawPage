// The ribbon -- tab strip, groups, collapse, overflow, contextual tabs, key tips.
//
// ---------------------------------------------------------------------------------------------
// THIS FILE IS THE A3 VERDICT TURNED INTO PRODUCTION CODE.
//
// A3 asked whether Qt Widgets can host a real ribbon without a commercial control library. It was
// settled 2026-08-23 at T2 as **`go, with named custom work`** (Tech-Stack-Plan.md §3.3) by the
// throwaway `tools/spike-ribbon/`, which is DELETED in the same change that adds this file --
// exactly as D1-1 said it would be, and exactly as `tools/spike-pdfium/` is deleted at Stage 2. The
// spike's durable outputs were the verdict and three named pieces of work, and all three are here:
//
//   1. `RibbonPageLayout` -- a custom QLayout for collapse (~60 lines). No stock Qt layout can do
//      it, and the reason is structural rather than a gap: QHBoxLayout distributes *space* among
//      children, whereas collapse changes *what a child is*, decided from the space available. A
//      stock layout has no way to ask a child to become smaller IN KIND rather than in degree.
//   2. A chevron overflow over `QTabBar` (~30 lines). QTabBar's own overflow is a pair of scroll
//      arrows; a ribbon shows a chevron with a menu of the hidden tabs.
//   3. `restoreTabOrder()` (~15 lines) -- the one nobody predicted. See I-05 below.
//
// ---------------------------------------------------------------------------------------------
// ISSUE I-05, AND WHY IT IS A DURABLE CONSTRAINT RATHER THAN A SPIKE BUG.
//
// The user walked M-01 on 2026-08-23 and found `Tab` visiting the groups in COLLAPSE-PRIORITY order
// rather than visual order. Collapse is implemented by re-parenting the same content between the
// group body and a popup, and re-parenting appends the moved subtree to the END of the window's
// focus chain -- so the focus order was a fossil of the order the groups happened to change in.
//
// ANY collapse mechanism built on re-parenting has this. It is silent, it manifests only under the
// collapse sequence that actually occurred, and no amount of looking at a static window reveals it.
// A green offscreen run cannot see it either, which is why it took a person -- and why, once
// understood, half of it was moved back into the harness (I-06) rather than left to the next walk.
// `RibbonPageLayout` re-chains `Tab` over the groups in VISUAL order on every change of the
// collapsed set, and `tst_ribbonbuild` asserts it by driving a resize sweep.
//
// ---------------------------------------------------------------------------------------------
// AND HYSTERESIS IS NOT A POLISH DETAIL. Without a band between the width at which a group
// collapses and the width at which it re-expands, those are the same width, so a slow drag across
// the threshold oscillates once per frame. The first spike implementation had no band and the
// 20-sweep check failed by disagreeing with itself -- the collapse state depended on resize
// HISTORY. A person dragging a window would have called that "a bit flickery" and moved on, which
// is why R-2 was automated rather than left to the manual pass.
// ---------------------------------------------------------------------------------------------

#ifndef RAWPAGE_UI_SHELL_RIBBON_H
#define RAWPAGE_UI_SHELL_RIBBON_H

#include <QHash>
#include <QLayout>
#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

class QLabel;
class QStackedWidget;
class QTabBar;
class QToolButton;

namespace rawpage::ui {

// Fixed for the whole ribbon, in logical pixels -- Qt scales them per monitor. R-4 requires that a
// contextual tab appearing and disappearing does not change the ribbon's height, and R-2 requires
// the same of a group collapsing. The cheapest way to be certain of both is for the body height
// never to be a function of its content.
inline constexpr int kRibbonBodyHeight  = 92;
inline constexpr int kRibbonGroupMargin = 4;
inline constexpr int kRibbonTitleHeight = 14;

// -------------------------------------------------------------------------------------------
// RibbonGroup -- one titled group, which can collapse to a single popup button.
// -------------------------------------------------------------------------------------------
// The collapse mechanism is RE-PARENTING: the same content widget lives in the group body when
// expanded and inside a frameless popup when collapsed. Building a second copy of the content for
// the popup would double every future maintenance edit and let the two drift apart -- which is the
// shape of bug a ribbon accumulates over sixteen stages. The cost of re-parenting is I-05, and that
// cost is paid once, in `RibbonPageLayout::restoreTabOrder`.
class RibbonGroup : public QWidget {
    Q_OBJECT

public:
    RibbonGroup(QString title, QWidget* parent = nullptr);

    // Takes ownership. Its `sizeHint().width()` is what the group asks for when expanded.
    void setContent(QWidget* content);

    // HIGHER COLLAPSES FIRST -- the least important group folds away before the most important one,
    // so the order has to be data (res/ribbon.json's `collapsePriority`) rather than child order.
    // Deliberately not the same axis as visual order; conflating the two is I-05.
    void setCollapsePriority(int priority) { priority_ = priority; }
    int  collapsePriority() const { return priority_; }

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return collapsed_; }

    int expandedWidth() const;
    int collapsedWidth() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void showPopup();

    QString           title_;
    QPointer<QWidget> content_;
    QWidget*          body_    = nullptr;   // hosts content_ when expanded
    QToolButton*      button_  = nullptr;   // shown when collapsed
    QLabel*           caption_ = nullptr;
    QWidget*          popup_   = nullptr;   // hosts content_ when collapsed
    bool              collapsed_ = false;
    int               priority_  = 0;
};

// -------------------------------------------------------------------------------------------
// RibbonPageLayout -- the custom QLayout that R-2 turns on. Named custom work #1.
// -------------------------------------------------------------------------------------------
class RibbonPageLayout : public QLayout {
    Q_OBJECT

public:
    explicit RibbonPageLayout(QWidget* parent);
    ~RibbonPageLayout() override;

    void         addItem(QLayoutItem* item) override;
    int          count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;

    QSize sizeHint() const override;
    QSize minimumSize() const override;
    void  setGeometry(const QRect& rect) override;

    // Re-chains Tab across the groups in VISUAL order. Named custom work #3; see I-05 in the file
    // header for why this exists and why it is not optional.
    void restoreTabOrder();

    // Instrumentation. A criterion judged by eye is a criterion that passes whenever the person
    // judging expects it to -- R-2's twenty sweeps are counted, not watched.
    int  collapsedCount() const;
    int  relayoutCount() const { return relayouts_; }
    void resetRelayoutCount() { relayouts_ = 0; }

    // The band, in logical pixels, deliberately wider than one physical pixel at 200 % scaling.
    static constexpr int kHysteresis = 16;

private:
    QList<QLayoutItem*> items_;
    int relayouts_    = 0;
    int collapseMask_ = -1;   // which groups were collapsed last time; -1 = never laid out
};

// -------------------------------------------------------------------------------------------
// Ribbon -- tab strip, overflow chevron, contextual tabs, F9 collapse, key tips.
// -------------------------------------------------------------------------------------------
class Ribbon : public QWidget {
    Q_OBJECT

public:
    explicit Ribbon(QWidget* parent = nullptr);

    int  addPage(const QString& title, QWidget* page, bool contextual = false);
    void setContextualVisible(bool visible);
    bool isContextualVisible() const { return contextualVisible_; }

    int  pageCount() const;
    int  currentPage() const;
    void setCurrentPage(int index);
    QString pageTitle(int index) const;

    // R-7: collapse to the tab strip alone, and restore with the active tab intact.
    void setBodyCollapsed(bool collapsed);
    bool isBodyCollapsed() const { return bodyCollapsed_; }

    // R-5: two levels of Alt key tips, dismissed with Esc.
    void showKeyTips();
    void hideKeyTips();
    bool keyTipsVisible() const { return !tips_.isEmpty(); }
    bool handleKeyTip(int key);

    RibbonPageLayout* layoutOfPage(int index) const;

    // True when a tab is hidden behind the chevron. Read by the widget suite so R-3 is asserted
    // rather than looked at -- and it reports the chevron's OWN state rather than QWidget::isVisible,
    // which is false for every child of a window that has not been shown and would make the
    // assertion a statement about the test harness instead of about the ribbon.
    bool hasOverflow() const;

signals:
    void pageChanged(int index);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void relayoutTabs();
    void rebuildOverflowMenu();

    QTabBar*        tabs_    = nullptr;
    QToolButton*    chevron_ = nullptr;   // R-3, named custom work #2
    QStackedWidget* stack_   = nullptr;
    int             contextualIndex_   = -1;
    bool            contextualVisible_ = false;
    bool            bodyCollapsed_     = false;
    int             tipLevel_          = 0;
    QList<QWidget*> tips_;
    QHash<int, int> tipTargets_;          // key -> tab index, level 1
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_SHELL_RIBBON_H
