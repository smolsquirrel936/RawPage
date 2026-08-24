// Stage 1 T11 -- the tool finder. Guide case A-56.
//
// The ranking is a free function over the registry, so the ORDER of the results is asserted here
// without a window at all. The widget half is asserted through a real MainWindow, because "typing a
// name and pressing Enter runs the command" is the claim, and a ranking function cannot make it.

#include <catch2/catch_test_macros.hpp>

#include "app/Services.h"
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
#include <QTemporaryDir>

#include <algorithm>

using namespace rawpage;

namespace {

using core::ActionId;
using ui::toolfinder::Match;
using ui::toolfinder::MatchKind;

struct Fixture {
    QTemporaryDir         directory;
    core::Settings        settings{QDir(directory.path()).filePath(QStringLiteral("settings.ini"))};
    core::RecentFiles     recentFiles{settings};
    core::ActionRegistry  actions;
    core::KeyBindingStore keyBindings;
    core::LogBuffer       logBuffer;
    platform::Platform    platform;

    app::Services services()
    {
        return app::Services{settings, recentFiles, actions, keyBindings, logBuffer, platform};
    }

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
