// Stage 1 T10 -- the ribbon. Guide cases A-50 (built entirely from the resource) and the
// machine-decidable half of the A3 criteria R-2, R-3, R-4 and R-7.
//
// WHAT THIS SUITE CAN AND CANNOT SEE. It runs under QT_QPA_PLATFORM=offscreen, so it cannot judge
// whether the ribbon LOOKS like a ribbon -- that is manual case M-01 and it is correct that it is
// manual. What it can do is exactly what the T2 spike's self-test did, and it inherits those checks
// rather than re-deriving them: collapse is monotone and does not depend on resize history, the
// ribbon's height never changes, the active tab survives a collapse cycle, and Tab visits the
// groups in visual order. Every one of those is a criterion that a person watching a window would
// have called "fine".

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/RawPageAction.h"
#include "ui/shell/Ribbon.h"
#include "ui/shell/RibbonModel.h"
#include "ui/shell/RibbonTabBuilder.h"

#include <QFile>
#include <QLabel>
#include <QSet>
#include <QTabBar>
#include <QToolButton>
#include <QWidget>

using namespace rawpage;
using namespace rawpage::ui;

namespace {

// The spec as shipped, plus the assertion that it was actually found. A suite that silently tested
// an empty ribbon would pass every structural case below by vacuous truth, which is the exact shape
// of green-but-worthless this project keeps catching in its own harness.
const ribbon::ParseResult& shippedSpec()
{
    static const ribbon::ParseResult& parsed = ribbon::shipped();
    return parsed;
}

struct Fixture {
    core::ActionRegistry registry;
    ActionFactory        actions{registry, [] { return core::KeyContext::Global; }};
    Ribbon               ribbon;

    Fixture()
    {
        RibbonTabBuilder builder(ribbon, actions);
        problems = builder.build(shippedSpec());

        // SHOWN, on the offscreen platform. Not decoration: a widget that has never been shown has
        // no real geometry to hand its child layouts, and `QWidget::isVisible()` is false for every
        // child of a hidden top level -- so the collapse sweep read a page of default size and the
        // overflow check read a chevron that could never report itself visible. Both produced
        // stable, plausible, meaningless numbers. Showing offscreen costs nothing and is what the
        // platform exists for.
        ribbon.show();
        resizeTo(1600);
    }

    // RESIZE AND LAY OUT, in one call, because offscreen there is no event loop to do the second
    // half. A bare resize() marks the layouts dirty and returns; the geometry a real window would
    // get on the next paint never arrives, so every width-dependent assertion below would read the
    // state from before the resize. Measured rather than assumed: without the page activate() the
    // collapse sweep reported six collapsed groups at every width from 1600 down to 400 -- a
    // perfectly monotone, perfectly reproducible result that meant nothing at all.
    //
    // Both levels are needed. The ribbon's own QVBoxLayout sizes the stacked pages, and the page's
    // RibbonPageLayout is what decides which groups collapse.
    void resizeTo(int width)
    {
        ribbon.resize(width, 200);
        ribbon.layout()->activate();
        for (int page = 0; page < ribbon.pageCount(); ++page)
            if (RibbonPageLayout* pageLayout = ribbon.layoutOfPage(page)) pageLayout->activate();
    }

    QStringList problems;
};

} // namespace

TEST_CASE("the shipped ribbon resource is found and is sound", "[ribbon]")
{
    // Guide case A-51's runtime counterpart. The BUILD-time check is tools/ribbon-validate; this
    // asserts that the file the executable actually loads is the validated one, because a build-time
    // check on a file that never gets copied proves nothing about the running program.
    INFO("looked for the resource at: " << ribbon::shippedPath().toStdString());
    REQUIRE(QFile::exists(ribbon::shippedPath()));

    const ribbon::ParseResult& parsed = shippedSpec();
    for (const ribbon::Diagnostic& diagnostic : parsed.diagnostics)
        UNSCOPED_INFO(diagnostic.formatted(ribbon::shippedPath()).toStdString());

    CHECK(parsed.ok());
    CHECK(parsed.spec.schemaVersion == ribbon::kSchemaVersion);
    CHECK(parsed.spec.tabs.size() > 1);
    CHECK(parsed.spec.controlCount() > 100);
}

TEST_CASE("every tab and group in the resource is present in the widget tree", "[ribbon]")
{
    Fixture fixture;
    REQUIRE(fixture.problems.isEmpty());

    const ribbon::Spec& spec = shippedSpec().spec;
    REQUIRE(fixture.ribbon.pageCount() == static_cast<int>(spec.tabs.size()));

    for (int index = 0; index < static_cast<int>(spec.tabs.size()); ++index) {
        const ribbon::Tab& tab = spec.tabs[static_cast<std::size_t>(index)];

        RibbonPageLayout* layout = fixture.ribbon.layoutOfPage(index);
        REQUIRE(layout != nullptr);
        INFO("tab " << tab.id.toStdString());
        CHECK(layout->count() == static_cast<int>(tab.groups.size()));
    }
}

TEST_CASE("no control exists that the resource did not declare", "[ribbon]")
{
    // The half of A-50 that is easy to forget, and the one that actually protects D1-8. "Everything
    // in the JSON is on screen" is satisfied by a builder that ALSO hard-codes three buttons of its
    // own -- and the moment one exists, adding a command stops being a JSON line and the mechanism
    // is dead while still passing its test.
    Fixture fixture;
    REQUIRE(fixture.problems.isEmpty());

    QSet<QString> declared;
    for (const ribbon::Tab& tab : shippedSpec().spec.tabs)
        for (const ribbon::Group& group : tab.groups)
            for (const ribbon::Control& control : group.controls)
                declared.insert(control.actionStringId);

    const QList<RawPageAction*> built = fixture.ribbon.findChildren<RawPageAction*>();
    CHECK(built.size() == shippedSpec().spec.controlCount());

    for (RawPageAction* action : built) {
        INFO("built action " << action->objectName().toStdString());
        CHECK(declared.contains(action->objectName()));
    }
}

TEST_CASE("an unknown action id is refused with its own line number", "[ribbon]")
{
    // A-51 as a unit: the SAME parser tools/ribbon-validate runs, so a build-time diagnostic and a
    // runtime one cannot disagree. The line number matters as much as the message -- "unknown action
    // id" without a location in a four-hundred-line resource is a search, not a diagnostic.
    const QByteArray bad = QByteArrayLiteral(
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"tabs\": [\n"
        "    { \"id\": \"home\", \"title\": \"ribbon.tab.home\", \"groups\": [\n"
        "      { \"id\": \"g\", \"title\": \"ribbon.group.file\", \"controls\": [\n"
        "        { \"action\": \"file.opne\" }\n"
        "      ] }\n"
        "    ] }\n"
        "  ]\n"
        "}\n");

    const ribbon::ParseResult parsed = ribbon::parse(bad);
    REQUIRE(parsed.diagnostics.size() == 1);
    CHECK(parsed.diagnostics.front().message.contains(QStringLiteral("file.opne")));
    CHECK(parsed.diagnostics.front().line == 6);
}

TEST_CASE("a duplicate placement and an empty group are each refused", "[ribbon]")
{
    // The other two rules D1-8 names. Asserted together because the interesting property is that
    // BOTH are reported from one pass: a validator that stops at the first problem turns fixing a
    // stale resource into one build-edit-build cycle per typo.
    const QByteArray bad = QByteArrayLiteral(
        "{\n"
        "  \"schemaVersion\": 1,\n"
        "  \"tabs\": [\n"
        "    { \"id\": \"home\", \"title\": \"ribbon.tab.home\", \"groups\": [\n"
        "      { \"id\": \"a\", \"title\": \"ribbon.group.file\", \"controls\": [\n"
        "        { \"action\": \"file.open\" },\n"
        "        { \"action\": \"file.open\" }\n"
        "      ] },\n"
        "      { \"id\": \"b\", \"title\": \"ribbon.group.tools\", \"controls\": [] }\n"
        "    ] }\n"
        "  ]\n"
        "}\n");

    const ribbon::ParseResult parsed = ribbon::parse(bad);
    REQUIRE(parsed.diagnostics.size() == 2);

    QStringList messages;
    for (const ribbon::Diagnostic& diagnostic : parsed.diagnostics) messages << diagnostic.message;
    CHECK(messages.filter(QStringLiteral("placed more than once")).size() == 1);
    CHECK(messages.filter(QStringLiteral("no controls")).size() == 1);
}

TEST_CASE("collapse is monotone across twenty sweeps and never depends on resize history", "[ribbon]")
{
    // A3 criterion R-2, and the one that found the hysteresis bug at T2. Without a band between the
    // width at which a group collapses and the width at which it re-expands, those are the same
    // width and a slow drag oscillates once per frame -- and the symptom a person reports is "a bit
    // flickery", if they report it at all.
    Fixture fixture;
    RibbonPageLayout* layout = fixture.ribbon.layoutOfPage(0);
    REQUIRE(layout != nullptr);

    // The sweep starts wider than the tab needs when fully expanded, so that "nothing collapsed" is
    // a state the sweep actually visits. Measured from the layout rather than hard-coded: the Home
    // tab asks for 1684 logical pixels with the resource as shipped, so a sweep starting at a
    // plausible-looking 1600 would have begun with a group already folded and quietly turned the
    // interesting end of the range into a constant.
    const int widest = layout->sizeHint().width() + 120;

    QList<int> firstSweep;
    for (int sweep = 0; sweep < 20; ++sweep) {
        QList<int> collapsing;
        for (int width = widest; width >= 400; width -= 40) {
            fixture.resizeTo(width);
            collapsing << layout->collapsedCount();
        }
        for (int width = 400; width <= widest; width += 40) fixture.resizeTo(width);

        // MONOTONE while narrowing: the count may rise and must never fall, because falling means
        // a group re-expanded as the window got smaller.
        for (int i = 1; i < collapsing.size(); ++i) {
            INFO("sweep " << sweep << " step " << i);
            CHECK(collapsing[i] >= collapsing[i - 1]);
        }

        // IDENTICAL across sweeps: the collapse state is a function of the available width alone.
        // If it depended on how the width was reached, this is where that shows up.
        if (sweep == 0) firstSweep = collapsing;
        else CHECK(collapsing == firstSweep);
    }

    CHECK(firstSweep.first() == 0);
    CHECK(firstSweep.last() > 0);
}

TEST_CASE("the ribbon height is constant while groups collapse and a contextual tab comes and goes",
          "[ribbon]")
{
    // R-2 and R-4 share one assertion because they share one failure: a ribbon whose height moves
    // makes every widget below it jump, and nothing else in the window is at fault.
    Fixture fixture;
    const int height = fixture.ribbon.sizeHint().height();

    for (int width = 1600; width >= 260; width -= 60) {
        fixture.resizeTo(width);
        CHECK(fixture.ribbon.sizeHint().height() == height);
    }

    fixture.resizeTo(1600);
    for (int cycle = 0; cycle < 60; ++cycle) {
        fixture.ribbon.setContextualVisible(cycle % 2 == 0);
        CHECK(fixture.ribbon.sizeHint().height() == height);
    }
    CHECK(fixture.ribbon.isContextualVisible() == false);
}

TEST_CASE("F9 collapses the ribbon to its tab strip and restores the active tab", "[ribbon]")
{
    // R-7. The active tab surviving is not incidental: the STACK is hidden rather than the pages,
    // and the tab bar's current index is never touched, which is what makes it survive for free.
    Fixture fixture;
    const int expanded = fixture.ribbon.sizeHint().height();

    fixture.ribbon.setCurrentPage(2);
    REQUIRE(fixture.ribbon.currentPage() == 2);

    fixture.ribbon.setBodyCollapsed(true);
    fixture.ribbon.layout()->activate();
    const int collapsed = fixture.ribbon.sizeHint().height();
    CHECK(collapsed < expanded);
    CHECK(fixture.ribbon.currentPage() == 2);

    fixture.ribbon.setBodyCollapsed(false);
    fixture.ribbon.layout()->activate();
    CHECK(fixture.ribbon.sizeHint().height() == expanded);
    CHECK(fixture.ribbon.currentPage() == 2);
}

TEST_CASE("the tab bar is allocated real width so the tabs are on screen", "[ribbon]")
{
    // ISSUE I-36, and the case this suite did not have. Everything else here asks the tab bar what
    // it CONTAINS -- how many pages, which one is current, which ones the chevron took -- and every
    // one of those answers was correct while the widget itself was laid out zero pixels wide, so the
    // shipped ribbon showed a body with no tabs above it from T10 until a person went looking for
    // the View tab. `tabRect()` is measured from the bar's own contents and is a fine number in a
    // widget nobody can see; the question a test has to ask instead is what the LAYOUT gave it.
    Fixture fixture;

    auto* tabs = fixture.ribbon.findChild<QTabBar*>(QStringLiteral("ribbonTabs"));
    REQUIRE(tabs != nullptr);

    const int current = fixture.ribbon.currentPage();
    REQUIRE(current >= 0);
    INFO("tab bar geometry: " << tabs->geometry().width() << "x" << tabs->geometry().height());
    CHECK(tabs->width() >= tabs->tabRect(current).width());
    CHECK(tabs->height() > 0);

    // And with a corner widget in the strip, which is how the shell actually builds it: the tool
    // finder goes in beside the tabs (MainWindow, T11), and the reason the spacer that broke this
    // existed at all was to hold that widget against the right-hand edge. It still is held there,
    // and the tabs still have the rest.
    auto* corner = new QLabel(QStringLiteral("find"));
    corner->setFixedWidth(180);
    fixture.ribbon.setCornerWidget(corner);
    fixture.resizeTo(1600);
    // Convention 12: offscreen nothing re-runs a layout for you, and the resize above does not
    // touch the strip's own layout because the strip's SIZE did not change -- only its contents.
    REQUIRE(corner->parentWidget() != nullptr);
    // `setParent()` leaves a widget hidden, and a hidden widget is an EMPTY layout item -- so
    // without this the strip would hand the tab bar the corner's share too and the assertion below
    // would pass by accident. The shell gets the same show for free: it sets its corner widget
    // before the window is shown, and showing a window shows the children it has not been told to
    // hide.
    corner->show();
    corner->parentWidget()->layout()->activate();

    CHECK(tabs->width() >= tabs->tabRect(current).width());
    CHECK(corner->x() > tabs->x() + tabs->tabRect(current).width());
}

TEST_CASE("tabs overflow to the chevron when the ribbon is too narrow for them", "[ribbon]")
{
    // R-3. QTabBar's own overflow is a pair of scroll arrows; a ribbon uses a chevron with a menu,
    // and that is one of the three pieces of named custom work the A3 verdict rests on.
    Fixture fixture;
    fixture.resizeTo(1600);
    CHECK_FALSE(fixture.ribbon.hasOverflow());

    // Narrow enough that the tab strip cannot hold nine tabs. This is only reachable because the
    // ribbon body sits in a scroll area whose own minimum is a frame and a scrollbar, so the page
    // layout's content minimum never reaches the window -- see I-19 and the comment on
    // RibbonPageLayout::minimumSize. Before T10's first fix the widget snapped back to 1044 and this
    // assertion was a statement about a window that had refused to resize; T11 replaced that fix's
    // clipped row with the scroll below.
    fixture.resizeTo(240);
    INFO("ribbon width after asking for 240: " << fixture.ribbon.width());
    CHECK(fixture.ribbon.width() < 400);
    CHECK(fixture.ribbon.hasOverflow());

    fixture.resizeTo(1600);
    CHECK_FALSE(fixture.ribbon.hasOverflow());
}

TEST_CASE("a ribbon narrower than its collapsed groups scrolls rather than clipping", "[ribbon]")
{
    // ISSUE I-19's OPEN HALF, carried from T10 and paid off here.
    //
    // T10 left the row clipped at the window edge below the width of one collapsed group, which is
    // what the reference (P-4) does and is still a row whose right-hand end the user cannot reach.
    // The body now scrolls, and the layout is free to ask for the width it actually needs.
    Fixture fixture;

    fixture.resizeTo(1600);
    CHECK_FALSE(fixture.ribbon.hasBodyScroll());

    fixture.resizeTo(240);
    INFO("ribbon width after asking for 240: " << fixture.ribbon.width());
    REQUIRE(fixture.ribbon.width() < 400);

    // THE WIDEST PAGE IS WHAT SETS THE SCROLL EXTENT, and that is QStackedWidget's doing rather
    // than ours: its minimum is the largest of its pages', so every tab is laid out at the width the
    // widest tab needs. Measured here rather than assumed -- the first version of this case asserted
    // the CURRENT page was fully collapsed and found four of its six groups collapsed, because page
    // 0 is not the widest and was given room the widest page had asked for. The behaviour is worth
    // keeping: the scroll range does not jump when the user switches tabs.
    RibbonPageLayout* widest = nullptr;
    for (int index = 0; index < fixture.ribbon.pageCount(); ++index) {
        RibbonPageLayout* page = fixture.ribbon.layoutOfPage(index);
        if (!page) continue;
        if (!widest || page->minimumSize().width() > widest->minimumSize().width()) widest = page;
    }
    REQUIRE(widest != nullptr);

    // On that page every group is collapsed -- collapse is tried first and exhausted before
    // scrolling starts, which is the order that matters: a ribbon that scrolled while it still had
    // groups left to collapse would be hiding content it could have shown.
    CHECK(widest->collapsedCount() == widest->count());
    CHECK(widest->minimumSize().width() > fixture.ribbon.width());
    CHECK(fixture.ribbon.hasBodyScroll());

    // And the current page collapsed too -- what is asserted is that the width reached the layout
    // at all, which is the half a bare resize() would have left unproven (I-19's other lesson).
    RibbonPageLayout* current = fixture.ribbon.layoutOfPage(fixture.ribbon.currentPage());
    REQUIRE(current != nullptr);
    CHECK(current->collapsedCount() > 0);

    // And back. The scroll is a consequence of the width alone, like the collapse decision above
    // it, so widening puts it away without anybody resetting anything.
    fixture.resizeTo(1600);
    CHECK_FALSE(fixture.ribbon.hasBodyScroll());
}

TEST_CASE("Tab visits the groups in visual order after any change of the collapsed set", "[ribbon]")
{
    // ISSUE I-05, automated. The user found this by walking M-01: focus ran Clipboard, View, Tools
    // while the groups read Clipboard, Tools, Styles, View -- because collapse re-parents, and
    // re-parenting appends the moved subtree to the END of the focus chain. So Tab order became a
    // fossil of the order the groups happened to collapse in.
    //
    // The check drives a real collapse sequence first, because the bug does not exist before one
    // has happened -- which is exactly why a static offscreen assertion missed it and a person
    // pressing Tab did not.
    Fixture fixture;
    RibbonPageLayout* layout = fixture.ribbon.layoutOfPage(0);
    REQUIRE(layout != nullptr);

    fixture.resizeTo(500);
    REQUIRE(layout->collapsedCount() > 0);
    fixture.resizeTo(1600);

    // Walk the chain from the first focusable child of the first group and check the group
    // boundaries are crossed left to right.
    QList<QWidget*> groupsInVisualOrder;
    for (int i = 0; i < layout->count(); ++i)
        if (auto* group = qobject_cast<RibbonGroup*>(layout->itemAt(i)->widget()))
            groupsInVisualOrder << group;
    REQUIRE(groupsInVisualOrder.size() > 2);

    const auto owningGroupIndex = [&](QWidget* widget) {
        for (int i = 0; i < groupsInVisualOrder.size(); ++i)
            if (groupsInVisualOrder[i]->isAncestorOf(widget)) return i;
        return -1;
    };

    QWidget* cursor = groupsInVisualOrder.first()->findChild<QToolButton*>();
    REQUIRE(cursor != nullptr);

    int lastGroup = 0;
    int visited   = 0;
    for (int step = 0; step < 400; ++step) {
        QWidget* next = cursor->nextInFocusChain();
        if (!next) break;
        cursor = next;

        const int group = owningGroupIndex(cursor);
        if (group < 0) continue;
        ++visited;

        // The chain may stay within a group or step to the NEXT one; it must never jump backwards,
        // which is precisely what the priority-ordered chain did.
        INFO("step " << step << " reached group " << group << " from " << lastGroup);
        CHECK(group >= lastGroup);
        lastGroup = group;
        if (lastGroup == groupsInVisualOrder.size() - 1) break;
    }

    CHECK(visited > 0);
    CHECK(lastGroup == groupsInVisualOrder.size() - 1);
}

