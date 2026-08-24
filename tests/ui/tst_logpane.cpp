// Stage 1 T14 -- `ui/panes/LogPane`, the in-app log viewer of `Features.md` §9.5.
//
// Guide case A-94. Offscreen, so nothing here judges the pane's appearance; what is asserted is
// that records reach it, that the filters narrow what it shows, and that what a test reads is the
// same text a user would paste into a support ticket.

#include <catch2/catch_test_macros.hpp>

#include "app/Services.h"
#include "core/Log.h"
#include "core/LogBuffer.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "ui/panes/LogPane.h"
#include "ui/shell/MainWindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

using namespace rawpage;

namespace {

using core::ActionId;
using core::LogLevel;
using core::LogSubsystem;

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

    void log(LogLevel level, LogSubsystem subsystem, const QString& message)
    {
        logBuffer.append(core::LogRecordData{level, subsystem, message});
    }
};

// The pane marshals a notification to its own thread with a queued call, because from Stage 2 a
// record arrives on a render worker. Offscreen there is no event loop running, so the queued calls
// have to be drained by hand -- and doing that here rather than reaching for `appendIfVisible`
// keeps the test on the path the product takes.
void drain()
{
    QCoreApplication::sendPostedEvents();
    QCoreApplication::processEvents();
}

} // namespace

TEST_CASE("records already in the buffer are there when the pane opens", "[logpane]")
{
    // A pane that only showed records logged AFTER it was opened would be empty at exactly the
    // moment someone opens it -- which is after something went wrong.
    Fixture fixture;
    fixture.log(LogLevel::Error, LogSubsystem::Parse, QStringLiteral("something went wrong"));
    fixture.log(LogLevel::Info, LogSubsystem::Ui, QStringLiteral("and something else"));

    ui::LogPane pane(fixture.logBuffer);
    CHECK(pane.visibleRecordCount() == 2);
    CHECK(pane.visibleText().contains(QStringLiteral("something went wrong")));
}

TEST_CASE("a record logged while the pane is open arrives", "[logpane]")
{
    Fixture fixture;
    ui::LogPane pane(fixture.logBuffer);
    REQUIRE(pane.visibleRecordCount() == 0);

    fixture.log(LogLevel::Warning, LogSubsystem::Save, QStringLiteral("a late arrival"));
    drain();

    CHECK(pane.visibleRecordCount() == 1);
    CHECK(pane.visibleText().contains(QStringLiteral("a late arrival")));
}

TEST_CASE("the level filter narrows and the subsystem filter narrows", "[logpane]")
{
    Fixture fixture;
    fixture.log(LogLevel::Error, LogSubsystem::Render, QStringLiteral("an error in render"));
    fixture.log(LogLevel::Info, LogSubsystem::Render, QStringLiteral("some news from render"));
    fixture.log(LogLevel::Error, LogSubsystem::Parse, QStringLiteral("an error in parse"));

    ui::LogPane pane(fixture.logBuffer);
    REQUIRE(pane.visibleRecordCount() == 3);

    // "Error and above" is the NARROWEST, not the widest -- the level ordering runs Error(0) to
    // Trace(4), and a filter that read it the other way would show everything at every setting and
    // look like it worked.
    pane.setLevelFilter(LogLevel::Error);
    CHECK(pane.visibleRecordCount() == 2);
    CHECK_FALSE(pane.visibleText().contains(QStringLiteral("some news")));

    pane.setLevelFilter(LogLevel::Trace);
    REQUIRE(pane.visibleRecordCount() == 3);

    core::LogSubsystemMask onlyRender = core::LogSubsystemMask::none();
    onlyRender.add(LogSubsystem::Render);
    pane.setSubsystemFilter(onlyRender);
    CHECK(pane.visibleRecordCount() == 2);
    CHECK_FALSE(pane.visibleText().contains(QStringLiteral("an error in parse")));
}

TEST_CASE("a filtered-out record still does not appear once it arrives live", "[logpane]")
{
    // The filter has to be applied on the APPEND path as well as on the rebuild path. Two code
    // paths, one rule, and the second one is the one that gets forgotten -- the symptom being a
    // pane that respects the filter until the next log line lands.
    Fixture fixture;
    ui::LogPane pane(fixture.logBuffer);
    pane.setLevelFilter(LogLevel::Error);

    fixture.log(LogLevel::Info, LogSubsystem::Ui, QStringLiteral("beneath the filter"));
    drain();
    CHECK(pane.visibleRecordCount() == 0);

    fixture.log(LogLevel::Error, LogSubsystem::Ui, QStringLiteral("above the filter"));
    drain();
    CHECK(pane.visibleRecordCount() == 1);
}

TEST_CASE("clearing empties the pane and the buffer behind it", "[logpane]")
{
    // Both, because a pane that cleared only its own view would refill itself the next time the
    // filter changed -- which reads as the button not working.
    Fixture fixture;
    fixture.log(LogLevel::Info, LogSubsystem::Ui, QStringLiteral("one"));
    fixture.log(LogLevel::Info, LogSubsystem::Ui, QStringLiteral("two"));

    ui::LogPane pane(fixture.logBuffer);
    REQUIRE(pane.visibleRecordCount() == 2);

    pane.clear();
    CHECK(pane.visibleRecordCount() == 0);
    CHECK(fixture.logBuffer.size() == 0);

    pane.setLevelFilter(LogLevel::Error);
    CHECK(pane.visibleRecordCount() == 0);
}

TEST_CASE("the pane's line shape is the log file's line shape", "[logpane]")
{
    // One format, so a log read on screen and the same log read in a support bundle are the same
    // text. The alternative is a support ticket whose lines nobody can grep for.
    const core::LogEntry entry{LogLevel::Warning, LogSubsystem::Ocr,
                               QStringLiteral("a page came back empty"), QDateTime::currentDateTime()};
    const QString line = ui::LogPane::formatEntry(entry);

    CHECK(line.contains(QStringLiteral("[warning][ocr]")));
    CHECK(line.endsWith(QStringLiteral("a page came back empty")));
}

TEST_CASE("the window builds the pane hidden and pane.log toggles it", "[logpane]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.show();

    ui::LogPane* pane = window.logPane();
    REQUIRE(pane != nullptr);

    // HIDDEN at startup. A shell that opens with its log pane showing looks like a shell in
    // trouble, and this is the assertion that keeps a debugging convenience from shipping.
    CHECK_FALSE(pane->isVisible());

    REQUIRE(fixture.actions.dispatch(ActionId::PaneLog, core::KeyContext::Global));
    CHECK(pane->isVisible());

    // A TOGGLE, so the ribbon renders a check mark -- and `ActionFactory` only calls `setCheckable`
    // when a `checked` predicate exists, which is what issue I-21 was about.
    CHECK(fixture.actions.isToggle(ActionId::PaneLog));
    CHECK(fixture.actions.isChecked(ActionId::PaneLog));

    REQUIRE(fixture.actions.dispatch(ActionId::PaneLog, core::KeyContext::Global));
    CHECK_FALSE(pane->isVisible());
    CHECK_FALSE(fixture.actions.isChecked(ActionId::PaneLog));
}

TEST_CASE("destroying a pane with a record in flight is safe", "[logpane]")
{
    // The pane subscribes with a handler that holds `this` and posts to itself. The buffer outlives
    // every window, so a notification arriving after the destructor started is issue I-12 arriving
    // through a fifth door -- and a POSTED one that lands after the widget is gone is the same bug
    // one step further along. Both are covered: unsubscribe happens first, and ~QObject drops this
    // object's own posted events.
    Fixture fixture;
    {
        ui::LogPane pane(fixture.logBuffer);
        fixture.log(LogLevel::Info, LogSubsystem::Ui, QStringLiteral("in flight"));
        // NOT drained: the queued call is still pending when the pane goes.
    }
    drain();

    fixture.log(LogLevel::Info, LogSubsystem::Ui, QStringLiteral("after the pane"));
    drain();

    CHECK(fixture.logBuffer.size() == 2);
    SUCCEED("no notification reached a destroyed pane");
}
