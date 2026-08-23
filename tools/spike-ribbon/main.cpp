// THROWAWAY -- see SpikeRibbon.h for what this answers and why it gets deleted at T10.
//
// Two modes, and the split matters:
//
//   (no argument)   a window to look at. R-1, R-3, R-5 and R-6 are judged BY A PERSON against
//                   a reference ribbon, because no offscreen run can tell you whether something
//                   looks like a ribbon. Testing guide cases M-01 and M-02.
//
//   --self-test     the part a machine can decide. R-2's twenty threshold crossings, R-4's
//                   height stability, R-7's active-tab survival, R-9's tab-switch timing, and
//                   the keyboard focus ORDER are all measurable, and a criterion left to
//                   eye-judgement is a criterion that passes whenever the person judging expects
//                   it to. Runs under QT_QPA_PLATFORM=offscreen.
//
//   --print-tab-order   dumps the real focus chain, in order, with each control's objectName.
//                   Written to diagnose the M-01 finding of 2026-08-23 and kept, because the
//                   answer to "why did Tab go there" should be one command rather than an
//                   afternoon.
//
// THE KEYBOARD CASE IS NOT AN A3 CRITERION. It began as the machine-decidable half of R-8, and
// R-8 was withdrawn on 2026-08-23 when accessibility was removed from the product (Features.md ->
// Removed from scope). It stays because keyboard operability was explicitly RETAINED: a ribbon
// whose Tab order is scrambled is broken for every keyboard user. Nothing here calls an
// accessibility API any more -- widget identity is objectName, which is Qt's ordinary mechanism
// for naming a widget and has nothing to do with screen readers.
//
// This division IS the answer to the "offscreen hides real bugs" risk in
// Stage1-AppShell-Plan.md 12: automate what has a defined right answer, and hand a person only
// the residue that genuinely needs eyes.

#include "SpikeRibbon.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QStatusBar>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace spike;

namespace {

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

// --- R-1's vocabulary of controls -------------------------------------------
// A large button, a split button, two rows of small buttons, a gallery, and a combo, all on
// one baseline grid. The list is from the criterion, not from what was convenient to build.

QToolButton* largeButton(const QString& text)
{
    auto* button = new QToolButton;
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setText(text);
    button->setAutoRaise(true);
    button->setFixedSize(60, kBodyHeight - kTitleHeight - 2 * kGroupMargin - 2);
    button->setObjectName(text);
    return button;
}

QToolButton* splitButton(const QString& text)
{
    auto* button = largeButton(text);
    button->setPopupMode(QToolButton::MenuButtonPopup);
    auto* menu = new QMenu(button);
    menu->addAction(QStringLiteral("Recent"));
    menu->addAction(QStringLiteral("Browse…"));
    button->setMenu(menu);
    return button;
}

QWidget* smallButtonRows(const QStringList& labels)
{
    auto* host = new QWidget;
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(1);
    for (int i = 0; i < labels.size(); ++i) {
        auto* button = new QToolButton;
        button->setText(labels.at(i));
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setFixedHeight(22);
        button->setObjectName(labels.at(i));
        grid->addWidget(button, i % 2, i / 2);
    }
    return host;
}

QWidget* gallery(const QStringList& entries)
{
    auto* list = new QListWidget;
    list->setFlow(QListView::LeftToRight);
    list->setViewMode(QListView::IconMode);
    list->setFixedHeight(kBodyHeight - kTitleHeight - 2 * kGroupMargin - 2);
    list->setFixedWidth(150);
    list->setObjectName(QStringLiteral("Gallery"));
    for (const QString& entry : entries) list->addItem(entry);
    return list;
}

RibbonGroup* makeGroup(const QString& title, int priority, QWidget* content)
{
    auto* group = new RibbonGroup(title);
    group->setPriority(priority);
    group->setContent(content);
    return group;
}

QWidget* buildHomePage()
{
    auto* page = new QWidget;
    auto* layout = new RibbonPageLayout(page);

    auto* clipboard = new QWidget;
    {
        auto* row = new QHBoxLayout(clipboard);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(splitButton(QStringLiteral("Paste")));
        row->addWidget(smallButtonRows({QStringLiteral("Cut"), QStringLiteral("Copy"),
                                        QStringLiteral("Format"), QStringLiteral("Clear")}));
    }
    layout->addWidget(makeGroup(QStringLiteral("Clipboard"), 3, clipboard));

    auto* tools = new QWidget;
    {
        auto* row = new QHBoxLayout(tools);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(largeButton(QStringLiteral("Select")));
        row->addWidget(largeButton(QStringLiteral("Hand")));
        row->addWidget(smallButtonRows({QStringLiteral("Zoom in"), QStringLiteral("Zoom out"),
                                        QStringLiteral("Fit page"), QStringLiteral("Fit width")}));
    }
    layout->addWidget(makeGroup(QStringLiteral("Tools"), 1, tools));

    auto* styles = new QWidget;
    {
        auto* row = new QHBoxLayout(styles);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(gallery({QStringLiteral("Plain"), QStringLiteral("Bold"),
                                QStringLiteral("Note"), QStringLiteral("Warn")}));
    }
    layout->addWidget(makeGroup(QStringLiteral("Styles"), 4, styles));

    auto* view = new QWidget;
    {
        auto* column = new QVBoxLayout(view);
        column->setContentsMargins(0, 0, 0, 0);
        auto* combo = new QComboBox;
        combo->addItems({QStringLiteral("Single page"), QStringLiteral("Continuous"),
                         QStringLiteral("Two-up")});
        combo->setObjectName(QStringLiteral("Page layout"));
        column->addWidget(combo);
        auto* check = new QCheckBox(QStringLiteral("Rulers"));
        // Found by the --print-tab-order dump, which reported this control as "QCheckBox".
        // A control the diagnostic cannot name is one whose focus-order failures are unreadable.
        check->setObjectName(QStringLiteral("Rulers"));
        column->addWidget(check);
        column->addStretch(1);
    }
    layout->addWidget(makeGroup(QStringLiteral("View"), 2, view));

    return page;
}

QWidget* buildSimplePage(const QString& groupTitle, const QStringList& buttons)
{
    auto* page = new QWidget;
    auto* layout = new RibbonPageLayout(page);
    auto* content = new QWidget;
    auto* row = new QHBoxLayout(content);
    row->setContentsMargins(0, 0, 0, 0);
    for (const QString& text : buttons) row->addWidget(largeButton(text));
    layout->addWidget(makeGroup(groupTitle, 1, content));
    return page;
}

// --- The window -------------------------------------------------------------

class SpikeWindow : public QMainWindow {
public:
    SpikeWindow()
    {
        ribbon_ = new Ribbon(this);
        ribbon_->addPage(QStringLiteral("Home"), buildHomePage());
        ribbon_->addPage(QStringLiteral("Edit"),
            buildSimplePage(QStringLiteral("Text"), {QStringLiteral("Add"), QStringLiteral("Erase")}));
        ribbon_->addPage(QStringLiteral("Comment"),
            buildSimplePage(QStringLiteral("Markup"), {QStringLiteral("Note"), QStringLiteral("Highlight")}));
        ribbon_->addPage(QStringLiteral("Pages"),
            buildSimplePage(QStringLiteral("Assemble"), {QStringLiteral("Insert"), QStringLiteral("Rotate")}));
        ribbon_->addPage(QStringLiteral("Forms"),
            buildSimplePage(QStringLiteral("Fields"), {QStringLiteral("Text box"), QStringLiteral("Check")}));
        ribbon_->addPage(QStringLiteral("Protect"),
            buildSimplePage(QStringLiteral("Security"), {QStringLiteral("Encrypt"), QStringLiteral("Redact")}));
        ribbon_->addPage(QStringLiteral("View"),
            buildSimplePage(QStringLiteral("Windows"), {QStringLiteral("Split"), QStringLiteral("Tile")}));
        contextual_ = ribbon_->addPage(QStringLiteral("Table Tools"),
            buildSimplePage(QStringLiteral("Table"), {QStringLiteral("Row"), QStringLiteral("Column")}),
            /*contextual*/ true);

        auto* central = new QWidget(this);
        auto* column = new QVBoxLayout(central);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        column->addWidget(ribbon_, 0);

        auto* canvas = new QLabel(
            QStringLiteral("Spike canvas.\n\n"
                           "F9  collapse the ribbon to its tab strip and back  (R-7)\n"
                           "Alt key tips, two levels, Esc to dismiss           (R-5)\n"
                           "F4  toggle the contextual tab                      (R-4)\n"
                           "Drag the window narrower to collapse groups        (R-2)\n"
                           "Narrow further until tabs overflow to the chevron   (R-3)"),
            central);
        canvas->setAlignment(Qt::AlignCenter);
        column->addWidget(canvas, 1);
        setCentralWidget(central);

        statusBar()->showMessage(QStringLiteral("A3 ribbon spike — throwaway, deleted at T10"));
        resize(1100, 520);
    }

    Ribbon* ribbon() const { return ribbon_; }

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (ribbon_->keyTipsVisible() && ribbon_->handleKeyTip(event->key())) return;

        switch (event->key()) {
        case Qt::Key_F9:
            ribbon_->setBodyCollapsed(!ribbon_->isBodyCollapsed());
            return;
        case Qt::Key_F4:
            ribbon_->setContextualVisible(!ribbon_->isContextualVisible());
            return;
        case Qt::Key_Alt:
            ribbon_->keyTipsVisible() ? ribbon_->hideKeyTips() : ribbon_->showKeyTips();
            return;
        default:
            break;
        }
        QMainWindow::keyPressEvent(event);
    }

private:
    Ribbon* ribbon_ = nullptr;
    int contextual_ = -1;
};

// --- The self-test ----------------------------------------------------------

struct Outcome {
    bool passed = false;
    QString detail;
};

// Walks the real focus chain the way Tab does, and reports what it visits, in order.
// `QWidget::nextInFocusChain()` is the chain itself, so this is what the user sees rather
// than what the widget tree suggests they ought to.
QStringList tabOrderOf(QWidget* root)
{
    QStringList names;
    QWidget* widget = root;
    // The chain is circular; bound the walk so a corrupted one reports rather than hangs.
    for (int guard = 0; guard < 2000; ++guard) {
        widget = widget->nextInFocusChain();
        if (!widget || widget == root) break;
        if (!(widget->focusPolicy() & Qt::TabFocus)) continue;
        if (!widget->isVisible() || !widget->isEnabled()) continue;
        names << (widget->objectName().isEmpty()
                      ? QString::fromLatin1(widget->metaObject()->className())
                      : widget->objectName());
    }
    return names;
}

// R-2. The criterion says "resize across the threshold 20 times with no layout thrash", and
// this is what that means precisely: sweeping the width down then up must produce a collapse
// count that is monotone in each direction, and every sweep must produce the SAME count at the
// same width. An oscillating layout fails the second condition even when it looks stable.
Outcome checkR2(SpikeWindow& window)
{
    Ribbon* ribbon = window.ribbon();
    ribbon->setCurrentPage(0);
    RibbonPageLayout* layout = ribbon->layoutOfPage(0);
    if (!layout) return {false, QStringLiteral("no RibbonPageLayout on page 0")};

    constexpr int kWide = 1200;
    constexpr int kNarrow = 380;
    constexpr int kStep = 10;

    QList<int> firstDownSweep;
    int heightSeen = -1;
    bool heightStable = true;

    for (int pass = 0; pass < 20; ++pass) {
        QList<int> downSweep;
        for (int width = kWide; width >= kNarrow; width -= kStep) {
            window.resize(width, window.height());
            QApplication::processEvents();
            downSweep.append(layout->collapsedCount());
            if (heightSeen < 0) heightSeen = ribbon->height();
            else if (ribbon->height() != heightSeen) heightStable = false;
        }
        for (int width = kNarrow; width <= kWide; width += kStep) {
            window.resize(width, window.height());
            QApplication::processEvents();
        }

        if (pass == 0) {
            firstDownSweep = downSweep;
        } else if (downSweep != firstDownSweep) {
            return {false, QStringLiteral("pass %1 disagreed with pass 0 — the collapse state "
                                          "depends on resize history, i.e. it thrashes").arg(pass)};
        }

        for (int i = 1; i < downSweep.size(); ++i) {
            if (downSweep[i] < downSweep[i - 1])
                return {false, QStringLiteral("collapse count fell from %1 to %2 while NARROWING")
                                   .arg(downSweep[i - 1]).arg(downSweep[i])};
        }
    }

    if (!heightStable)
        return {false, QStringLiteral("ribbon height changed during collapse")};

    const int maxCollapsed = firstDownSweep.isEmpty() ? 0 : firstDownSweep.last();
    return {true, QStringLiteral("20 sweeps identical; collapse count 0 → %1, monotone; "
                                 "ribbon height constant at %2 px")
                      .arg(maxCollapsed).arg(heightSeen)};
}

// R-4. A contextual tab appearing and disappearing must not change the ribbon's height nor
// disturb the tab strip. Ten cycles in the criterion; sixty here, because a height change that
// only shows up under repetition is still a height change.
Outcome checkR4(SpikeWindow& window)
{
    Ribbon* ribbon = window.ribbon();
    const int heightBefore = ribbon->height();
    const int pageBefore = ribbon->currentPage();

    for (int i = 0; i < 60; ++i) {
        ribbon->setContextualVisible(true);
        QApplication::processEvents();
        if (ribbon->height() != heightBefore)
            return {false, QStringLiteral("height changed to %1 on show, cycle %2")
                               .arg(ribbon->height()).arg(i)};
        ribbon->setContextualVisible(false);
        QApplication::processEvents();
        if (ribbon->height() != heightBefore)
            return {false, QStringLiteral("height changed to %1 on hide, cycle %2")
                               .arg(ribbon->height()).arg(i)};
    }

    if (ribbon->currentPage() != pageBefore)
        return {false, QStringLiteral("active tab moved from %1 to %2")
                           .arg(pageBefore).arg(ribbon->currentPage())};

    return {true, QStringLiteral("60 show/hide cycles; height constant at %1 px; active tab kept")
                      .arg(heightBefore)};
}

// R-7. F9 collapses to the tab strip and restores, preserving the active tab.
Outcome checkR7(SpikeWindow& window)
{
    Ribbon* ribbon = window.ribbon();
    ribbon->setCurrentPage(3);
    QApplication::processEvents();
    const int expandedHeight = ribbon->height();

    ribbon->setBodyCollapsed(true);
    QApplication::processEvents();
    const int collapsedHeight = ribbon->height();

    if (collapsedHeight >= expandedHeight)
        return {false, QStringLiteral("collapsing did not shrink the ribbon (%1 → %2)")
                           .arg(expandedHeight).arg(collapsedHeight)};

    ribbon->setBodyCollapsed(false);
    QApplication::processEvents();

    if (ribbon->currentPage() != 3)
        return {false, QStringLiteral("active tab was %1 after restore, expected 3")
                           .arg(ribbon->currentPage())};
    if (ribbon->height() != expandedHeight)
        return {false, QStringLiteral("height did not return: %1 vs %2")
                           .arg(ribbon->height()).arg(expandedHeight)};

    return {true, QStringLiteral("%1 px → %2 px → %3 px; active tab 3 preserved")
                      .arg(expandedHeight).arg(collapsedHeight).arg(ribbon->height())};
}

// R-9. Measured, never judged: p95 of a tab switch over 100 iterations against one 60 Hz
// frame. The figure is reported whatever it is -- a slow switch is a painting problem with
// known fixes, not evidence that Qt Widgets cannot host a ribbon.
Outcome checkR9(SpikeWindow& window)
{
    Ribbon* ribbon = window.ribbon();
    QList<double> samples;
    samples.reserve(100);

    for (int i = 0; i < 100; ++i) {
        const int target = i % 7;
        QElapsedTimer timer;
        timer.start();
        ribbon->setCurrentPage(target);
        QApplication::processEvents();
        samples.append(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    }

    std::sort(samples.begin(), samples.end());
    const double p50 = samples.at(samples.size() / 2);
    const double p95 = samples.at(static_cast<int>(samples.size() * 0.95));
    const double worst = samples.last();

    return {p95 <= 16.0,
            QStringLiteral("p50 %1 ms · p95 %2 ms · max %3 ms over 100 switches (budget 16 ms)")
                .arg(p50, 0, 'f', 3).arg(p95, 0, 'f', 3).arg(worst, 0, 'f', 3)};
}

// KEYBOARD OPERABILITY. Tab must visit the ribbon's controls in the order they appear on screen,
// and every control must carry an objectName so a failure here is readable.
//
// NOT an A3 criterion. This began as the machine-decidable half of R-8, and R-8 was withdrawn on
// 2026-08-23 when accessibility left the product (Features.md -> Removed from scope). The check
// stays because keyboard operability was explicitly RETAINED: a ribbon whose Tab order is scrambled
// is broken for every keyboard user, which was never an accessibility-only concern.
//
// This case exists because the user found the bug walking M-01 on 2026-08-23: focus ran
// Clipboard -> View -> Tools while the groups read Clipboard, Tools, Styles, View. Collapse is
// implemented by re-parenting, re-parenting appends to the end of the focus chain, and so Tab
// order became a fossil of the order the groups collapsed in. Left as an eye-check it would
// come back silently the moment a group's priority changed.
Outcome checkKeyboardOrder(SpikeWindow& window)
{
    Ribbon* ribbon = window.ribbon();
    ribbon->setCurrentPage(0);

    // Deliberately driven through a full collapse/expand cycle first. At a width where nothing
    // has ever collapsed the chain is trivially correct, so a check that never narrows the
    // window cannot fail -- and would have passed on the very build the user reported.
    for (int width : {1200, 420, 700, 1100}) {
        window.resize(width, window.height());
        QApplication::processEvents();
    }

    RibbonPageLayout* pageLayout = ribbon->layoutOfPage(0);
    QWidget* page = pageLayout ? pageLayout->parentWidget() : nullptr;
    if (!page) return {false, QStringLiteral("no Home page")};

    QList<RibbonGroup*> groups = page->findChildren<RibbonGroup*>();
    std::sort(groups.begin(), groups.end(),
              [](RibbonGroup* a, RibbonGroup* b) { return a->x() < b->x(); });

    // Walk the chain and map each focusable control back to the group that owns it. The owning
    // group indices must be non-decreasing: Tab may visit several controls within one group,
    // but it must never return to a group it has already left.
    QStringList visited;
    int lastGroup = -1;
    QWidget* widget = ribbon;
    for (int guard = 0; guard < 2000; ++guard) {
        widget = widget->nextInFocusChain();
        if (!widget || widget == ribbon) break;
        if (!(widget->focusPolicy() & Qt::TabFocus)) continue;
        if (!widget->isVisible() || !widget->isEnabled()) continue;

        int owner = -1;
        for (int i = 0; i < groups.size(); ++i)
            if (groups[i]->isAncestorOf(widget)) { owner = i; break; }
        if (owner < 0) continue;                       // the tab bar itself

        if (widget->objectName().isEmpty())
            return {false, QStringLiteral("a %1 in group '%2' has no objectName -- reachable by Tab "
                                          "but unnameable, so any failure here would be unreadable")
                               .arg(QString::fromLatin1(widget->metaObject()->className()),
                                    groups[owner]->objectName())};

        if (owner < lastGroup)
            return {false, QStringLiteral("Tab reached '%1' in group '%2' (visual position %3) "
                                          "after already leaving group %4 -- focus order is not "
                                          "visual order. Chain so far: %5")
                               .arg(widget->objectName(), groups[owner]->objectName())
                               .arg(owner).arg(lastGroup)
                               .arg(visited.join(QStringLiteral(" -> ")))};
        lastGroup = owner;
        visited << widget->objectName();
    }

    if (visited.isEmpty()) return {false, QStringLiteral("no focusable control found")};
    return {true, QStringLiteral("after a 1200 -> 420 -> 700 -> 1100 px cycle, %1 controls in "
                                 "visual order, each named: %2")
                      .arg(visited.size()).arg(visited.join(QStringLiteral(" -> ")))};
}

int runSelfTest()
{
    SpikeWindow window;
    window.show();
    QApplication::processEvents();

    struct Case { const char* id; const char* what; Outcome (*run)(SpikeWindow&); };
    const Case cases[] = {
        {"R-2", "group collapse across 20 threshold crossings", checkR2},
        {"R-4", "contextual tab does not move the ribbon",      checkR4},
        {"R-7", "F9 collapse and restore keeps the active tab", checkR7},
        {"R-9", "tab switch within one 60 Hz frame",            checkR9},
        {"KB ", "Tab visits controls in visual order, all named", checkKeyboardOrder},
    };

    int failures = 0;
    out() << "A3 ribbon spike — self-test (the machine-decidable criteria)\n\n";
    for (const Case& item : cases) {
        const Outcome outcome = item.run(window);
        if (!outcome.passed) ++failures;
        out() << (outcome.passed ? "  PASS  " : "  FAIL  ")
              << item.id << "  " << item.what << "\n        " << outcome.detail << "\n";
        out().flush();
    }

    out() << "\n  R-1, R-3, R-5 and R-6 are NOT in this list. They need a person, a reference\n"
             "  ribbon, and two monitors at different scale factors — testing guide cases M-01\n"
             "  and M-02, both signed off 2026-08-23. Reporting them from here would be marking\n"
             "  a manual case on the user's behalf.\n"
             "  KB is not an A3 criterion: it is retained keyboard-operability coverage, kept\n"
             "  after R-8 was withdrawn with the accessibility removal.\n\n";
    out() << (failures == 0 ? "self-test: all measured criteria met\n"
                            : QStringLiteral("self-test: %1 criterion/criteria NOT met\n").arg(failures));
    out().flush();
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication application(argc, argv);

    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.contains(QStringLiteral("--self-test"))) return runSelfTest();

    if (arguments.contains(QStringLiteral("--print-tab-order"))) {
        SpikeWindow window;
        window.resize(1100, 520);
        window.show();
        QApplication::processEvents();
        out() << "tab order at 1100 px, Home tab:\n";
        for (const QString& name : tabOrderOf(window.ribbon())) out() << "  " << name << "\n";
        out().flush();
        return 0;
    }

    SpikeWindow window;
    window.show();
    return QApplication::exec();
}
