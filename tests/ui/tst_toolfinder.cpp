// Stage 1 T11 -- the tool finder. Guide case A-56.
//
// The ranking is a free function over the registry, so the ORDER of the results is asserted here
// without a window at all. The widget half is asserted through a real MainWindow, because "typing a
// name and pressing Enter runs the command" is the claim, and a ranking function cannot make it.

#include <catch2/catch_test_macros.hpp>

#include "app/Services.h"
#include "tests/ui/ShellFixture.h"
#include "core/cmd/ActionRegistry.h"
#include "core/LogBuffer.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/SidebarDock.h"
#include "ui/shell/ToolFinder.h"

#include <QDir>
#include <QApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QLineEdit>
#include <QTemporaryDir>

#include <algorithm>

using namespace rawpage;

namespace {

using core::ActionId;
using ui::toolfinder::Match;
using ui::toolfinder::MatchKind;

struct Fixture : uitest::ShellFixture {
    // Every member and `services()` come from `tests/ui/ShellFixture.h` (S2 T13). They were seven
    // copies of the same eight fields until `app::Services` grew a render pipeline and the copies
    // became a seven-way edit; the header states what a fixture deliberately does NOT provide.

    std::vector<Match> rank(const QString& query, int limit = 20)
    {
        return ui::toolfinder::rank(query, actions, keyBindings, core::KeyContext::Global, limit);
    }
};

int indexOfFirst(const std::vector<Match>& matches, MatchKind kind)
{
    for (std::size_t i = 0; i < matches.size(); ++i)
        if (matches[i].kind == kind) return static_cast<int>(i);
    return -1;
}

} // namespace

TEST_CASE("an exact name outranks a prefix which outranks a fuzzy subsequence", "[toolfinder]")
{
    // A-56's ranking claim. Asserted as a property of the WHOLE list rather than as three named
    // rows: "the kinds never get better as you go down" holds no matter what the catalog says, and
    // a case pinned to three particular labels would start failing the first time somebody renames
    // a command for perfectly good reasons.
    Fixture fixture;
    const std::vector<Match> matches = fixture.rank(QStringLiteral("close"), 0);
    REQUIRE(matches.size() > 3);

    for (std::size_t i = 1; i < matches.size(); ++i)
        CHECK(matches[i - 1].kind <= matches[i].kind);

    // And the specific one the catalog does guarantee: an id whose label IS the query ranks first.
    CHECK(matches.front().kind == MatchKind::Exact);
    CHECK(matches.front().id == ActionId::FileClose);
    CHECK(matches.front().label == ui::catalog::translateAction(ActionId::FileClose));

    const int firstPrefix      = indexOfFirst(matches, MatchKind::Prefix);
    const int firstSubsequence = indexOfFirst(matches, MatchKind::Subsequence);
    REQUIRE(firstPrefix > 0);
    REQUIRE(firstSubsequence > firstPrefix);
}

TEST_CASE("a word inside a label is findable and so is a scattered subsequence", "[toolfinder]")
{
    Fixture fixture;

    // "All" is the second word of "Close All" -- the case a plain startsWith search misses, and the
    // one a person hits constantly because they remember the noun and not the verb.
    const std::vector<Match> words = fixture.rank(QStringLiteral("all"), 0);
    const auto closeAll = std::find_if(words.begin(), words.end(), [](const Match& match) {
        return match.id == ActionId::FileCloseAll;
    });
    REQUIRE(closeAll != words.end());
    CHECK(closeAll->kind == MatchKind::WordStart);

    // The loosest kind: the letters in order, not together.
    const std::vector<Match> scattered = fixture.rank(QStringLiteral("clsall"), 0);
    CHECK(std::any_of(scattered.begin(), scattered.end(), [](const Match& match) {
        return match.id == ActionId::FileCloseAll && match.kind == MatchKind::Subsequence;
    }));
}

TEST_CASE("each result carries the chord that would actually fire", "[toolfinder]")
{
    // Not a list of chords kept here: `KeyBindingStore` is the source, defaults from Features.md §8
    // and overrides on top. Showing a chord the user has remapped away is worse than showing none.
    Fixture fixture;

    const std::vector<Match> before = fixture.rank(QStringLiteral("close"), 0);
    REQUIRE_FALSE(before.empty());
    CHECK(before.front().id == ActionId::FileClose);
    CHECK(before.front().chord == QStringLiteral("Ctrl+W"));

    // THE REMAP IS OF THE CHORD, NOT OF THE ACTION, and the difference is why this case is written
    // this way. Binding a SECOND chord to `file.close` would leave `Ctrl+W` firing it as well, and
    // the finder would rightly keep showing `Ctrl+W`; giving `Ctrl+W` to something else is what
    // actually takes it away.
    const std::optional<core::Chord> chord = core::Chord::parse("Ctrl+W");
    REQUIRE(chord);
    REQUIRE(fixture.keyBindings.rebind(*chord, core::KeyContext::Global, ActionId::ViewToggleSidebar));

    const std::vector<Match> after = fixture.rank(QStringLiteral("close"), 0);
    REQUIRE_FALSE(after.empty());
    CHECK(after.front().id == ActionId::FileClose);
    CHECK(after.front().chord.isEmpty());

    // AND THE ACTION THAT TOOK IT NOW SHOWS `Ctrl+W`, NOT `F4` -- which is a change of behaviour
    // made at T14 and asserted here because this is the case that would otherwise have hidden it.
    // Until T14 an override ADDED a chord, so `view.toggleSidebar` answered to both `Ctrl+W` and
    // its §8 default `F4`, and this case asserted `F4`. That state cannot survive a restart: D1-10
    // persists `shortcuts/<context>/<action>` = ONE chord, so which of the two came back depended
    // on nothing the user could see. An override now REPLACES the action's default chords in its
    // context -- see `core/cmd/KeyBindingStore.h`, which owns the rule.
    const std::vector<Match> sidebar = fixture.rank(QStringLiteral("Toggle Sidebar"), 0);
    REQUIRE_FALSE(sidebar.empty());
    CHECK(sidebar.front().id == ActionId::ViewToggleSidebar);
    CHECK(sidebar.front().chord == QStringLiteral("Ctrl+W"));

    // And `F4` now resolves to nothing at all, which is the half a chord-shaped assertion would
    // miss: the superseded row is still in the defaults table and must stop answering.
    const std::optional<core::Chord> f4 = core::Chord::parse("F4");
    REQUIRE(f4);
    CHECK_FALSE(fixture.keyBindings.lookup(*f4, core::KeyContext::Global).has_value());
}

TEST_CASE("a command that is declared but not built yet is shown disabled rather than hidden",
          "[toolfinder]")
{
    // A-56's other half, and it is a product decision rather than a rendering one: the ribbon shows
    // those commands greyed with a tooltip saying they are not built yet, and a search that cannot
    // find a button the user is looking at is a search they stop using.
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    // `file.saveAs` is one of the ~340 declared and unregistered. It used to be `file.close`, which
    // T12 registered -- and picking a command that a later task will register is how a case like
    // this quietly stops testing what it says it does.
    const std::vector<Match> matches = fixture.rank(QStringLiteral("save as"), 0);
    const auto unregistered = std::find_if(matches.begin(), matches.end(), [](const Match& match) {
        return match.id == ActionId::FileSaveAs;
    });
    REQUIRE(unregistered != matches.end());
    CHECK_FALSE(unregistered->registered);
    CHECK_FALSE(unregistered->enabled);

    const std::vector<Match> live = fixture.rank(QStringLiteral("sidebar"), 0);
    const auto registered = std::find_if(live.begin(), live.end(), [](const Match& match) {
        return match.id == ActionId::ViewToggleSidebar;
    });
    REQUIRE(registered != live.end());
    CHECK(registered->registered);
    CHECK(registered->enabled);
}

TEST_CASE("an empty query lists nothing and a query nothing matches lists nothing", "[toolfinder]")
{
    Fixture fixture;
    CHECK(fixture.rank(QString()).empty());
    CHECK(fixture.rank(QStringLiteral("   ")).empty());
    CHECK(fixture.rank(QStringLiteral("qzxjvw")).empty());

    // The limit bounds what a popup renders. 0 means unbounded, which is what a case asking "does
    // this rank at all" wants -- and the two must not disagree about the ORDER.
    const std::vector<Match> bounded   = fixture.rank(QStringLiteral("page"), 5);
    const std::vector<Match> unbounded = fixture.rank(QStringLiteral("page"), 0);
    REQUIRE(bounded.size() == 5);
    REQUIRE(unbounded.size() > 5);
    for (std::size_t i = 0; i < bounded.size(); ++i) CHECK(bounded[i].id == unbounded[i].id);
}

TEST_CASE("the result list can always be put away", "[toolfinder]")
{
    // ISSUE I-44, and the worst defect this stage has produced. The list is a `Qt::Popup` WINDOW,
    // which grabs the mouse for the whole application while it is up -- so "the list will not close"
    // is not a cosmetic complaint, it is an application that answers no click anywhere. A query that
    // matched nothing still filled the list with the "nothing matched" ROW, so the list was shown;
    // `Esc` was not handled, an outside click was not handled, and `Enter` on an unselectable row
    // returned early without hiding. Force-closing the process was the only way out.
    //
    // Each of the three exits is asserted separately, because each was independently missing.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.show();

    ui::ToolFinder* finder = window.toolFinder();
    REQUIRE(finder != nullptr);

    // Cleared first, deliberately: `setQuery` goes through `QLineEdit::setText`, which emits nothing
    // when the text does not change -- so re-typing the SAME query after a dismissal would leave the
    // list hidden and the next assertion would pass for the wrong reason. Found by this case failing
    // on its second use of the helper.
    const auto typeSomethingWithNoHits = [&] {
        finder->setQuery(QString());
        REQUIRE_FALSE(finder->resultsVisible());  // an empty query shows nothing at all
        finder->setQuery(QStringLiteral("qzxjvw"));
        REQUIRE(finder->results().empty());       // nothing matched...
        REQUIRE(finder->resultsVisible());        // ...and the list is up anyway, saying so
    };

    // 1. Esc.
    typeSomethingWithNoHits();
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(finder->searchBox(), &escape);
    CHECK_FALSE(finder->resultsVisible());
    CHECK(finder->query() == QStringLiteral("qzxjvw"));   // the list went, the typing stayed

    // 2. A click somewhere else. The grab is why the popup receives it at all, and the coordinates
    //    being outside its own rect is what makes it "somewhere else".
    typeSomethingWithNoHits();
    auto* list = finder->findChild<QListWidget*>(QStringLiteral("toolFinderResults"));
    REQUIRE(list != nullptr);
    const QPointF outside(-40, -40);
    QMouseEvent click(QEvent::MouseButtonPress, outside, outside, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(list, &click);
    CHECK_FALSE(finder->resultsVisible());

    // 3. THE LIST SURVIVES THE FOCUS CHANGE THAT SHOWING IT CAUSES. This is the regression the
    //    first version of the I-44 fix shipped for one build: a `Qt::Popup` takes activation, so the
    //    search box gets a FocusOut with reason `PopupFocusReason` the moment the results appear,
    //    and dismissing on that closed the list in the same turn it opened -- every search looked
    //    as though nothing matched. Offscreen the real focus change does not happen, so the event
    //    is sent by hand; what this covers is the RULE, not the platform's behaviour.
    typeSomethingWithNoHits();
    QFocusEvent toPopup(QEvent::FocusOut, Qt::PopupFocusReason);
    QApplication::sendEvent(finder->searchBox(), &toPopup);
    CHECK(finder->resultsVisible());

    // ...while a real focus loss -- clicking into another widget -- still puts it away.
    QFocusEvent elsewhere(QEvent::FocusOut, Qt::MouseFocusReason);
    QApplication::sendEvent(finder->searchBox(), &elsewhere);
    CHECK_FALSE(finder->resultsVisible());

    // 4. Enter on the row that has no command behind it. It used to return false and leave the
    //    popup holding the grab.
    typeSomethingWithNoHits();
    CHECK_FALSE(finder->activate(0));
    finder->searchBox()->returnPressed();
    CHECK_FALSE(finder->resultsVisible());
}

TEST_CASE("choosing a result runs the command through the registry", "[toolfinder]")
{
    // The claim `Features.md` §1.1 actually makes -- "locates any command by name and runs it" --
    // and it runs it the same way the ribbon button does, which is why the sidebar below really
    // moves. Nothing in ToolFinder holds a handler of its own.
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    // SHOWN, offscreen. `view.toggleSidebar` asks the pane whether it is visible, and a child of a
    // window that was never shown answers "no" however its state was set -- so on a hidden window
    // the command takes the show branch twice and the case would pass a toggle that never toggled.
    // Measured rather than reasoned about: the first version of this case checked `isVisible`, then
    // `isHidden`, and got `false != false` from both.
    window.show();

    ui::ToolFinder* finder = window.toolFinder();
    REQUIRE(finder != nullptr);
    REQUIRE(window.sidebar() != nullptr);

    finder->setQuery(QStringLiteral("Toggle Sidebar"));
    REQUIRE_FALSE(finder->results().empty());
    REQUIRE(finder->results().front().id == ActionId::ViewToggleSidebar);

    const bool visibleBefore = window.sidebar()->isVisible();
    REQUIRE(visibleBefore);
    CHECK(finder->activate(0));
    CHECK_FALSE(window.sidebar()->isVisible());

    // And back, through the same command. A toggle that only works in one direction is the defect
    // that hides behind a single dispatch.
    CHECK(finder->activate(0));
    CHECK(window.sidebar()->isVisible());

    // An unregistered hit is offered and refuses. A dispatch that failed silently would leave the
    // user believing a command ran -- the exact failure `ErrorCode::NotFound` was introduced for.
    finder->setQuery(QStringLiteral("Save As"));
    REQUIRE_FALSE(finder->results().empty());
    CHECK_FALSE(finder->results().front().registered);
    CHECK_FALSE(finder->activate(0));

    CHECK_FALSE(finder->activate(-1));
    CHECK_FALSE(finder->activate(9999));
}

TEST_CASE("typing keeps reaching the search box after the result list opens", "[toolfinder]")
{
    // ISSUE I-54 (S2), and the THIRD defect this one popup has produced -- after I-44's freeze and
    // I-45's regression in its fix.
    //
    // `Qt::Popup` is a WINDOW. While one is up, Qt delivers key presses to the active popup rather
    // than to whatever held focus before it opened, so the first character reached the line edit,
    // the list opened, and every character after that arrived at a QListWidget which spent it on
    // its own keyboard search. The query stopped growing and the caret appeared to desert the box.
    // `setFocusPolicy(Qt::NoFocus)` does not prevent this and never could: focus policy decides
    // which widget takes focus WITHIN a window, and this is a question about which window the
    // keyboard is talking to.
    //
    // WHY NINE GREEN CASES MISSED IT: every other case in this file sends its keys to
    // `searchBox()`, which is the one address a real keystroke does NOT arrive at while the list is
    // up. That is convention 19's back door -- the destination was proven and the route was never
    // driven -- and it is the same shape as I-34 and I-43.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.show();

    ui::ToolFinder* finder = window.toolFinder();
    REQUIRE(finder != nullptr);

    finder->setQuery(QStringLiteral("op"));
    REQUIRE(finder->resultsVisible());

    auto* list = finder->findChild<QListWidget*>(QStringLiteral("toolFinderResults"));
    REQUIRE(list != nullptr);
    REQUIRE(list->count() > 1);   // the Down control below needs somewhere to move to

    // THE CARET'S HALF OF I-54, asserted as a MECHANISM because offscreen it can be nothing else.
    // Forwarding the keys fixed the typing and left the blinking cursor gone: a `QLineEdit` in a
    // DEACTIVATED window stops painting its cursor whether or not it still holds focus, and showing
    // a popup deactivates the window behind it. `WA_ShowWithoutActivating` stops that, and the focus
    // proxy sends any focus the list is given back to the box -- together they are what `QCompleter`
    // does. Under `offscreen` there is no activation to lose and no cursor to paint, so what this
    // case can honestly assert is that the two are set; **the proof that the caret blinks is a
    // manual one**, exactly as convention 21 records for `QStyleHints::setColorScheme` at I-41.
    CHECK(list->testAttribute(Qt::WA_ShowWithoutActivating));
    CHECK(list->focusProxy() == finder->searchBox());

    // A KEYSTROKE ADDRESSED TO THE POPUP, which is where the platform actually delivers it.
    QKeyEvent letter(QEvent::KeyPress, Qt::Key_E, Qt::NoModifier, QStringLiteral("e"));
    QApplication::sendEvent(list, &letter);
    CHECK(finder->query() == QStringLiteral("ope"));

    // ...and Backspace, so the box can be corrected as well as extended. Before the fix this was
    // swallowed too, which is what made a mistyped query feel unrecoverable.
    QKeyEvent backspace(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QApplication::sendEvent(list, &backspace);
    CHECK(finder->query() == QStringLiteral("op"));

    // THE CONTROL, in the other direction: the keys that ARE the list's job must not be forwarded,
    // or choosing a result with the arrow keys would stop working -- a fix that traded one half of
    // the keyboard for the other would pass a test that only checked the letters.
    //
    // The query is chosen so the control can actually run. Down moves between rows a person can
    // CHOOSE, and `QAbstractItemView` skips disabled ones -- so a query whose only enabled hit is
    // its first row leaves the arrow key correctly with nowhere to go. This case was first written
    // against "op", whose one enabled hit is `Open…`, and it failed against a CORRECT fix. That is
    // convention 7: a control that cannot exercise the thing it claims to is worse than none.
    finder->setQuery(QStringLiteral("toggle"));
    REQUIRE(finder->resultsVisible());
    int choosableRows = 0;
    for (int row = 0; row < list->count(); ++row)
        if (list->item(row)->flags() != Qt::NoItemFlags) ++choosableRows;
    REQUIRE(choosableRows > 1);   // `Toggle Ribbon` and `Toggle Sidebar` are both registered

    const int before = list->currentRow();
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(list, &down);
    CHECK(list->currentRow() != before);
    CHECK(finder->query() == QStringLiteral("toggle"));   // navigation leaves the query untouched

    // Esc still reaches the dismissal from the popup as well as from the box (I-44's exit must
    // survive the forwarding, since Esc is handled BEFORE the watched == list_ branch).
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(list, &escape);
    CHECK_FALSE(finder->resultsVisible());
    CHECK(finder->query() == QStringLiteral("toggle"));
}
