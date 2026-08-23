// Stage 1 T10 -- decision D1-7(b). Guide case A-54.
//
// A SELF-CHECK NOBODY HAS SEEN FAIL IS A SELF-CHECK NOBODY KNOWS WORKS. That sentence is the whole
// reason this file exists, and it is Stage 0's own I-15 restated: a mechanism verified only against
// input it accepts has been verified against nothing. So each case here PLANTS a violation of a
// specific kind and asserts the check names it -- and the two kinds are different on purpose,
// because they are what the two halves of D1-7 catch separately.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/RawPageAction.h"

#include <QAction>
#include <QMenu>
#include <QWidget>

using namespace rawpage;
using namespace rawpage::ui;

namespace {

struct Fixture {
    core::ActionRegistry registry;
    ActionFactory        factory{registry, [] { return core::KeyContext::Global; }};
    QWidget              window;
};

} // namespace

TEST_CASE("a window whose actions all came from the factory is sound", "[wiring]")
{
    // The positive control, and it is not decoration. Without it, "the check reports violations" is
    // equally satisfied by a check that reports everything -- which would make the architecture it
    // guards impossible to implement and would be discovered as a wall of false findings.
    Fixture fixture;
    fixture.factory.create(core::ActionId::FileOpen, &fixture.window);
    fixture.factory.create(core::ActionId::FileSave, &fixture.window);
    fixture.window.addAction(fixture.window.findChild<RawPageAction*>());

    CHECK(ActionFactory::wiringComplaints(&fixture.window).isEmpty());
    CHECK(ActionFactory::wiringIsSound(&fixture.window));
}

TEST_CASE("a planted bare QAction is named as a bare QAction", "[wiring]")
{
    // KIND ONE. In the product this cannot be written: layer-lint's `bare-qaction` rule refuses the
    // construction outside ActionFactory.cpp, and gates.layer_lint.qaction_* covers four spellings
    // of it. tests/ are outside the lint's scanned paths, which is exactly what lets the violation
    // be planted here -- the static rule and the runtime rule are checked against each other rather
    // than one being assumed from the other.
    Fixture fixture;
    fixture.factory.create(core::ActionId::FileOpen, &fixture.window);

    auto* smuggled = new QAction(&fixture.window);
    smuggled->setObjectName(QStringLiteral("smuggled"));

    const QList<QString> complaints = ActionFactory::wiringComplaints(&fixture.window);
    REQUIRE(complaints.size() == 1);
    CHECK(complaints.first().contains(QStringLiteral("bare QAction")));
    CHECK(complaints.first().contains(QStringLiteral("smuggled")));
}

TEST_CASE("a second connect on a legitimate action is named as a direct connection", "[wiring]")
{
    // KIND TWO, AND THE REASON THERE ARE TWO MECHANISMS AT ALL. The construction here is entirely
    // correct -- ActionFactory made it, the lint saw nothing and had nothing to see -- and the
    // action now does two things when triggered, one of which never reaches ActionRegistry and is
    // therefore invisible to the CLI, to a chord and to the Stage 14 batch runner.
    //
    // No static rule can catch this: the connect is legal, and forbidding connect() in ui/ would
    // forbid ui/ from working.
    Fixture fixture;
    RawPageAction* action = fixture.factory.create(core::ActionId::FileOpen, &fixture.window);
    REQUIRE(action->isCorrectlyWired());

    bool sideEffect = false;
    QObject::connect(action, &QAction::triggered, &fixture.window, [&] { sideEffect = true; });

    const QList<QString> complaints = ActionFactory::wiringComplaints(&fixture.window);
    REQUIRE(complaints.size() == 1);
    CHECK(complaints.first().contains(QStringLiteral("2 receivers")));
    CHECK(complaints.first().contains(QStringLiteral("1 are accounted for")));
    CHECK(complaints.first().contains(QStringLiteral("file.open")));
    CHECK_FALSE(sideEffect);   // nothing was triggered; the finding is structural
}

TEST_CASE("both kinds are reported together rather than the first one only", "[wiring]")
{
    Fixture fixture;
    RawPageAction* action = fixture.factory.create(core::ActionId::FileOpen, &fixture.window);
    QObject::connect(action, &QAction::triggered, &fixture.window, [] {});
    auto* smuggled = new QAction(&fixture.window);
    smuggled->setObjectName(QStringLiteral("smuggled"));

    CHECK(ActionFactory::wiringComplaints(&fixture.window).size() == 2);
}

TEST_CASE("a non-command menu item is exempt, and only through the one function that stamps it",
          "[wiring]")
{
    // The ribbon's overflow chevron offers the tabs that did not fit, and a QMenu's items ARE
    // QActions -- Qt gives no choice. A ribbon TAB is not a command: it has no ActionId and there is
    // nothing for the registry to route.
    //
    // The exemption is a dynamic PROPERTY set by ActionFactory::addNonCommandMenuItem, not an object
    // name and not a parent type, so it cannot be earned by a widget that merely resembles one. This
    // case asserts both directions: stamped is exempt, and the identical item built by hand is not.
    Fixture fixture;
    auto* menu = new QMenu(&fixture.window);

    ActionFactory::addNonCommandMenuItem(menu, QStringLiteral("Home"));
    CHECK(ActionFactory::wiringComplaints(&fixture.window).isEmpty());

    auto* unstamped = new QAction(QStringLiteral("Create"), menu);
    menu->addAction(unstamped);
    CHECK(ActionFactory::wiringComplaints(&fixture.window).size() == 1);
}

TEST_CASE("an action for an unregistered id is built disabled and says why", "[wiring]")
{
    // Stage 1 declares 358 ids and registers a handful, so this is the ordinary case for most of the
    // ribbon rather than an edge. The button exists, is placed, is sized and is disabled -- omitting
    // it instead would make the ribbon's real width and collapse behaviour a fiction until Stage 13,
    // which is precisely what T10 exists to get right early.
    Fixture fixture;
    RawPageAction* action = fixture.factory.create(core::ActionId::SignCertify, &fixture.window);

    REQUIRE_FALSE(fixture.registry.isRegistered(core::ActionId::SignCertify));
    CHECK_FALSE(action->isEnabled());
    CHECK_FALSE(action->toolTip().isEmpty());
    CHECK(action->toolTip().contains(action->text()));

    // And the resolved LABEL comes from ActionId.def through the catalog -- never from the caller,
    // which is what stops a button and the tool finder disagreeing about a command's name.
    CHECK_FALSE(action->text().startsWith(QStringLiteral("action.")));
}

TEST_CASE("a registered action is enabled and dispatches through the registry", "[wiring]")
{
    Fixture fixture;
    int dispatched = 0;
    REQUIRE(fixture.registry.register_(core::ActionId::FileOpen,
                                       [&](core::KeyContext) { ++dispatched; }));

    RawPageAction* action = fixture.factory.create(core::ActionId::FileOpen, &fixture.window);
    CHECK(action->isEnabled());

    action->trigger();
    CHECK(dispatched == 1);
}
