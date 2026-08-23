// THROWAWAY. This program exists to answer one question and then be deleted.
//
// RISK A3 (Tech-Stack-Plan.md 3.3, and decision D1-1 in Stage1-AppShell-Plan.md 3):
// *can Qt Widgets host a real ribbon -- sizing, collapse, overflow, contextual tabs, key
// tips, per-monitor DPI -- without a commercial control library?*
//
// It is a go/no-go, and the nine acceptance criteria R-1 ... R-9 were fixed in
// Stage1-AppShell-Plan.md 5.3 BEFORE this file was written, so the spike is graded against
// them rather than against whatever it happened to achieve. Nothing here is production code:
// T10 productionizes whichever techniques survive into ui/shell/Ribbon, and deletes this
// directory, exactly as tools/spike-pdfium/ is deleted at Stage 2.
//
// What it does NOT do: dispatch through ActionRegistry, read res/ribbon.json, translate a
// string, or persist anything. Every one of those is a T10 concern and building them here
// would be the failure mode D1-1 exists to prevent -- a spike that quietly becomes the
// implementation.
//
// THE CRITERION THIS FILE IS SHAPED BY IS R-2. Group collapse is the part of a ribbon that
// toolkits usually cannot do, so it gets the custom QLayout below and everything else is
// arranged around it.

#ifndef RAWPAGE_SPIKE_RIBBON_H
#define RAWPAGE_SPIKE_RIBBON_H

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

namespace spike {

// Fixed for the whole spike: R-4 requires that a contextual tab appearing and disappearing
// does not change the ribbon's height, and R-2 requires the same of a group collapsing. The
// cheapest way to be sure of both is for the body height never to be a function of content.
inline constexpr int kBodyHeight  = 92;   // logical pixels; Qt scales it per monitor
inline constexpr int kGroupMargin = 4;
inline constexpr int kTitleHeight = 14;

// ---------------------------------------------------------------------------------------
// RibbonGroup -- one titled group, which can collapse to a single popup button.
// ---------------------------------------------------------------------------------------
// The collapse mechanism is re-parenting: the SAME content widget lives in the group body
// when expanded and inside a frameless popup when collapsed. Rebuilding the content into a
// second widget would double every future maintenance edit and let the two drift, which is
// the shape of bug a ribbon accumulates over sixteen stages.
class RibbonGroup : public QWidget {
    Q_OBJECT
public:
    RibbonGroup(QString title, QWidget* parent = nullptr);

    // Takes ownership. Its sizeHint().width() is what the group asks for when expanded.
    void setContent(QWidget* content);

    // Lower collapses LAST. A real ribbon collapses the least important group first, so the
    // order has to be data rather than child order.
    void setPriority(int priority) { priority_ = priority; }
    int  priority() const { return priority_; }

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return collapsed_; }

    int expandedWidth() const;
    int collapsedWidth() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void showPopup();

    QString              title_;
    QPointer<QWidget>    content_;
    QWidget*             body_    = nullptr;   // hosts content_ when expanded
    QToolButton*         button_  = nullptr;   // shown when collapsed
    QLabel*              caption_ = nullptr;
    QWidget*             popup_   = nullptr;   // hosts content_ when collapsed
    bool                 collapsed_ = false;
    int                  priority_  = 0;
};

// ---------------------------------------------------------------------------------------
// RibbonPageLayout -- the custom QLayout that R-2 turns on.
// ---------------------------------------------------------------------------------------
// No stock Qt layout can do this: QHBoxLayout distributes *space*, but collapse is a change
// of what a child IS, decided from the space available, and a stock layout has no way to ask
// a child to become smaller in kind rather than in degree.
//
// HYSTERESIS IS NOT A POLISH DETAIL. Without it, the width at which a group collapses is also
// the width at which the freed space lets it expand again, so a slow drag across the threshold
// oscillates once per frame -- which is precisely the "layout thrash" R-2's twenty crossings
// are looking for. The band below is in logical pixels and is deliberately wider than one
// physical pixel at 200 % scaling.
class RibbonPageLayout : public QLayout {
    Q_OBJECT
public:
    explicit RibbonPageLayout(QWidget* parent);
    ~RibbonPageLayout() override;

    void addItem(QLayoutItem* item) override;
    int  count() const override;
    QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;

    QSize sizeHint() const override;
    QSize minimumSize() const override;
    void  setGeometry(const QRect& rect) override;

    // Re-chains Tab across the groups in VISUAL order.
    //
    // Collapse is implemented by re-parenting (see RibbonGroup::setCollapsed), and re-parenting
    // appends the moved subtree to the END of the window's focus chain. So after any collapse
    // or expand, Tab order is a fossil of the order the groups happened to change in -- not of
    // where they sit on screen. Found 2026-08-23 by the user walking M-01: focus went
    // Clipboard → View → Tools while the groups read Clipboard, Tools, Styles, View.
    //
    // This is NOT a spike artefact. Any collapse mechanism built on re-parenting has it, it is
    // silent, and it only manifests under the collapse sequence that actually occurred -- which
    // is why the self-test now asserts it rather than leaving it to M-08.
    void restoreTabOrder();

    // Instrumentation for the self-test. A criterion judged by eye is a criterion that passes
    // whenever the person judging expects it to.
    int  collapsedCount() const;
    int  relayoutCount() const { return relayouts_; }
    void resetRelayoutCount() { relayouts_ = 0; }

    static constexpr int kHysteresis = 16;

private:
    QList<QLayoutItem*> items_;
    int relayouts_ = 0;
    int collapseMask_ = -1;      // which groups were collapsed last time; -1 = never laid out
};

// ---------------------------------------------------------------------------------------
// Ribbon -- tab strip, overflow chevron, contextual tabs, F9 collapse, key tips.
// ---------------------------------------------------------------------------------------
class Ribbon : public QWidget {
    Q_OBJECT
public:
    explicit Ribbon(QWidget* parent = nullptr);

    int  addPage(const QString& title, QWidget* page, bool contextual = false);
    void setContextualVisible(bool visible);
    bool isContextualVisible() const { return contextualVisible_; }

    int  currentPage() const;
    void setCurrentPage(int index);

    // R-7: collapse to the tab strip alone, and restore with the active tab intact.
    void setBodyCollapsed(bool collapsed);
    bool isBodyCollapsed() const { return bodyCollapsed_; }

    // R-5: two levels of Alt key tips, dismissed with Esc.
    void showKeyTips();
    void hideKeyTips();
    bool keyTipsVisible() const { return !tips_.isEmpty(); }
    bool handleKeyTip(int key);

    RibbonPageLayout* layoutOfPage(int index) const;

signals:
    void pageChanged(int index);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void relayoutTabs();
    void rebuildOverflowMenu();

    QTabBar*        tabs_        = nullptr;
    QToolButton*    chevron_     = nullptr;   // R-3
    QStackedWidget* stack_       = nullptr;
    int             contextualIndex_  = -1;
    bool            contextualVisible_ = false;
    bool            bodyCollapsed_    = false;
    int             tipLevel_         = 0;
    QList<QWidget*> tips_;
    QHash<int, int> tipTargets_;              // key -> tab index, level 1
};

} // namespace spike

#endif // RAWPAGE_SPIKE_RIBBON_H
