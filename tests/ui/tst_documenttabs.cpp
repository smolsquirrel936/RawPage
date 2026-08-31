// Stage 1 T12 -- document tabs and the stub DocumentWindow. Guide cases A-57 and the second half of
// A-58, plus the structural half of manual case M-03.
//
// WHAT IS MANUAL AND WHY. M-03 walks the GESTURES: dragging a tab to reorder it, dragging one clear
// of the strip, dropping it back on another window. Offscreen there is no pointer, so what is
// asserted here is everything those gestures END in -- reorder, release, adopt, detach, close --
// each reachable as a function and each also reachable as a command. A case here claiming to have
// tested a drag would be a defect in the case.
//
// AND WHAT THAT LEFT UNGUARDED, WHICH COST AN S2 (issue I-34). "Everything the gesture ends in" was
// asserted by calling `adoptDocument` directly -- which proves the DESTINATION works and says
// nothing about whether the gesture can reach it. Re-dock was documented as shipped in four places,
// green in CI, and impossible to perform by hand for as long as it existed; the user's first M-03
// walk found it. Two cases below close that gap from both ends: one drives `detachDocument` with a
// position over a real strip (which turns out not to need a pointer at all -- the strip can be
// asked where it is), and one drives the tab bar's own handlers with synthetic mouse events to
// assert WHEN it reports. Synthetic events prove the state machine, not that a real pointer
// produces them; the pointer stays M-03's.

#include <catch2/catch_test_macros.hpp>

#include "app/Services.h"
#include "core/cmd/ActionRegistry.h"
#include "core/LogBuffer.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/settings/RecentFiles.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentTabBar.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Ribbon.h"
#include "ui/shell/StatusBar.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPoint>
#include <QTemporaryDir>

using namespace rawpage;

namespace {

using core::ActionId;

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

    // A file that IS named `.pdf` and is NOT a PDF. D1-2 says the shell checks the name and never
    // the bytes, so a stub tab must open for this -- that is guide case A-58's whole point, and a
    // fixture holding a real PDF could not tell the two behaviours apart.
    QString writePdf(const QString& name)
    {
        const QString path = QDir(directory.path()).filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("this is plain text, not a PDF at all");
        file.close();
        return QFileInfo(path).absoluteFilePath();
    }
};

} // namespace

TEST_CASE("a stub DocumentWindow holds a path and nothing else", "[tabs]")
{
    // A-57, D1-2's guard against the stub calcifying. What can be checked mechanically is that the
    // class adds no state of its own to QWidget's meta-object and that everything it reports is
    // derived from the path it was given; that no MEMBER Stage 2 would have to remove exists is a
    // claim about the header, and the header is where it is made. Saying so is more useful than a
    // test that pretends otherwise.
    ui::DocumentWindow document(QStringLiteral("C:/documents/quarterly report.pdf"));

    CHECK(document.path() == QStringLiteral("C:/documents/quarterly report.pdf"));
    CHECK(document.title() == QStringLiteral("quarterly report.pdf"));

    // No Q_PROPERTY, no slot, no signal of its own: the meta-object is QWidget's plus the class
    // name. A `Document` or a `CommandJournal` arriving as a property is the shape this catches.
    CHECK(ui::DocumentWindow::staticMetaObject.propertyCount()
          == QWidget::staticMetaObject.propertyCount());
    CHECK(ui::DocumentWindow::staticMetaObject.methodCount()
          == QWidget::staticMetaObject.methodCount());

    // A path with no file name still names itself something rather than rendering an empty tab.
    ui::DocumentWindow nameless{QString()};
    CHECK(nameless.title() == ui::catalog::translate(ui::catalog::key::DocumentUntitled));
}

TEST_CASE("opening a file gives it a tab and opening it twice does not", "[tabs]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    REQUIRE(window.documentCount() == 0);

    const QString first  = fixture.writePdf(QStringLiteral("first.pdf"));
    const QString second = fixture.writePdf(QStringLiteral("second.pdf"));

    window.openFiles({first, second});
    REQUIRE(window.documentCount() == 2);
    CHECK(window.documentTabs()->tabText(0) == QStringLiteral("first.pdf"));
    CHECK(window.documentTabs()->tabText(1) == QStringLiteral("second.pdf"));
    CHECK(window.currentDocument() != nullptr);
    CHECK(window.currentDocument()->path() == second);

    // A-58's second half, which was not runnable until this task: the file's CONTENTS are plain
    // text, and a stub tab opens for it because the check is the name and the suffix.
    CHECK(window.documentAt(0)->path() == first);

    // The same path again focuses the tab that already has it rather than adding a second.
    window.activateDocument(0);
    window.openFiles({second});
    CHECK(window.documentCount() == 2);
    CHECK(window.currentDocument()->path() == second);

    // And the two refusals still refuse -- neither a missing file nor a file that is not named
    // `.pdf` gets a tab.
    window.openFiles({QDir(fixture.directory.path()).filePath(QStringLiteral("nothing.pdf")),
                      fixture.writePdf(QStringLiteral("notes.txt"))});
    CHECK(window.documentCount() == 2);
}

TEST_CASE("the window title and the status bar follow the current document", "[tabs]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    const QString empty = ui::catalog::translate(ui::catalog::key::WindowTitle);
    CHECK(window.windowTitle() == empty);
    CHECK_FALSE(window.documentTabs()->isVisibleTo(&window));

    window.openFiles({fixture.writePdf(QStringLiteral("report.pdf"))});
    CHECK(window.windowTitle() == ui::catalog::translate(ui::catalog::key::WindowTitleWithDocument)
                                     .arg(QStringLiteral("report.pdf")));
    CHECK(window.documentTabs()->isVisibleTo(&window));

    // The status bar has been built and disabled since T9 for want of a document. Its controls are
    // live now -- and the page total still reads an em dash, because a stub has no pages and
    // reporting "1 of 1" would be inventing one.
    CHECK(window.statusBar()->pageBox()->isEnabled());
    CHECK(window.statusBar()->zoomLabel()->isEnabled());

    window.closeDocument(0);
    CHECK(window.documentCount() == 0);
    CHECK(window.windowTitle() == empty);
    CHECK_FALSE(window.statusBar()->pageBox()->isEnabled());
    CHECK_FALSE(window.documentTabs()->isVisibleTo(&window));
}

TEST_CASE("reordering the strip reorders the documents behind it", "[tabs]")
{
    // The half of a drag that is not the drag: QTabBar moves the tab and emits `tabMoved`, and if
    // the stack does not follow, tab 3 shows the document that used to be at 3. That is a defect
    // nobody sees until they have three tabs open and it is the kind M-03 would find by hand -- so
    // it is asserted here instead, and M-03 only has to judge the gesture.
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    window.openFiles({fixture.writePdf(QStringLiteral("a.pdf")),
                      fixture.writePdf(QStringLiteral("b.pdf")),
                      fixture.writePdf(QStringLiteral("c.pdf"))});
    REQUIRE(window.documentCount() == 3);

    window.documentTabs()->moveTab(0, 2);

    CHECK(window.documentTabs()->tabText(0) == QStringLiteral("b.pdf"));
    CHECK(window.documentTabs()->tabText(2) == QStringLiteral("a.pdf"));
    CHECK(window.documentAt(0)->title() == QStringLiteral("b.pdf"));
    CHECK(window.documentAt(2)->title() == QStringLiteral("a.pdf"));
}

TEST_CASE("Ctrl+Tab steps through the documents and wraps at both ends", "[tabs]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    window.openFiles({fixture.writePdf(QStringLiteral("a.pdf")),
                      fixture.writePdf(QStringLiteral("b.pdf")),
                      fixture.writePdf(QStringLiteral("c.pdf"))});
    window.activateDocument(0);

    // DISPATCHED, not called. The chords arrive at T13; what exists now is the command, and it is
    // the same command the chord will reach.
    REQUIRE(fixture.actions.dispatch(ActionId::WindowNextTab, core::KeyContext::Global));
    CHECK(window.documentTabs()->currentIndex() == 1);

    REQUIRE(fixture.actions.dispatch(ActionId::WindowPrevTab, core::KeyContext::Global));
    REQUIRE(fixture.actions.dispatch(ActionId::WindowPrevTab, core::KeyContext::Global));
    CHECK(window.documentTabs()->currentIndex() == 2);   // wrapped backwards past the start

    REQUIRE(fixture.actions.dispatch(ActionId::WindowNextTab, core::KeyContext::Global));
    CHECK(window.documentTabs()->currentIndex() == 0);   // and forwards past the end
}

TEST_CASE("closing is reachable as a command and refuses when there is nothing to close", "[tabs]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    // Nothing open: `file.close` exists and is not applicable, which is a DIFFERENT answer from "no
    // such command" and is what lets a batch report tell a typo from a state.
    const core::Result<void> nothing =
        fixture.actions.dispatch(ActionId::FileClose, core::KeyContext::Global);
    REQUIRE_FALSE(nothing);
    CHECK(nothing.error().code == core::ErrorCode::Unavailable);

    window.openFiles({fixture.writePdf(QStringLiteral("a.pdf")),
                      fixture.writePdf(QStringLiteral("b.pdf"))});
    REQUIRE(window.documentCount() == 2);

    REQUIRE(fixture.actions.dispatch(ActionId::FileClose, core::KeyContext::Global));
    CHECK(window.documentCount() == 1);

    window.openFiles({fixture.writePdf(QStringLiteral("c.pdf"))});
    REQUIRE(fixture.actions.dispatch(ActionId::FileCloseAll, core::KeyContext::Global));
    CHECK(window.documentCount() == 0);
}

TEST_CASE("a tab torn out keeps its identity and can be re-docked", "[tabs]")
{
    // THE STRUCTURAL BEHAVIOUR D1-2 EXISTS TO LET US BUILD NOW. The pointer gesture is M-03; what
    // it ends in is a release and an adopt, and the document that comes out is the SAME object that
    // went in -- which is what makes this a move rather than a close and a re-open. At Stage 2 that
    // object owns a journal and a scroll position, and a move that rebuilt it would lose both.
    Fixture fixture;
    ui::MainWindow window(fixture.services());

    window.openFiles({fixture.writePdf(QStringLiteral("a.pdf")),
                      fixture.writePdf(QStringLiteral("b.pdf"))});
    ui::DocumentWindow* moved = window.documentAt(1);
    REQUIRE(moved != nullptr);

    ui::MainWindow other(fixture.services());
    REQUIRE(other.documentCount() == 0);

    ui::DocumentWindow* released = window.releaseDocument(1);
    CHECK(released == moved);                       // the same object, not a copy of its path
    CHECK(window.documentCount() == 1);
    CHECK(released->parent() == nullptr);           // and genuinely handed over

    other.adoptDocument(released);
    CHECK(other.documentCount() == 1);
    CHECK(other.currentDocument() == moved);
    CHECK(other.documentTabs()->tabText(0) == QStringLiteral("b.pdf"));

    // Back again, and into a chosen position rather than the end.
    window.adoptDocument(other.releaseDocument(0), 0);
    CHECK(other.documentCount() == 0);
    CHECK(window.documentCount() == 2);
    CHECK(window.documentAt(0) == moved);
}

TEST_CASE("tearing a tab out makes a window that owns it", "[tabs]")
{
    // The gesture is M-03; this is what the gesture ends in. What is asserted here is the branch
    // that makes a window, and that the document went WITH it rather than being rebuilt from its
    // path. The drop-onto-another-strip branch was recorded here as manual-only until I-34 showed
    // that was never true -- see "a lone document can still be dropped" below.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.openFiles({fixture.writePdf(QStringLiteral("a.pdf")),
                      fixture.writePdf(QStringLiteral("b.pdf"))});

    ui::DocumentWindow* moved = window.documentAt(1);
    ui::MainWindow*     torn  = window.detachDocument(1);

    REQUIRE(torn != nullptr);
    CHECK(torn != &window);
    CHECK(window.documentCount() == 1);
    CHECK(torn->documentCount() == 1);
    CHECK(torn->currentDocument() == moved);
    CHECK(ui::MainWindow::activeShell() == torn);

    // WA_DeleteOnClose: nothing else owns a torn-off window, so closing it is what ends it -- and
    // the window set has to lose it, which is what the next dispatch depends on.
    torn->close();

    // `WA_DeleteOnClose` posts a DeferredDelete; it is the EVENT LOOP that destroys the window, and
    // there is none in a Catch2 case. Delivering that one event by hand is what makes this assert
    // the product's behaviour rather than the harness's -- without it the check reads "still in the
    // set", which is true and means nothing.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    CHECK(ui::MainWindow::shellWindows().contains(&window));
    CHECK_FALSE(ui::MainWindow::shellWindows().contains(torn));
}

TEST_CASE("a Global command acts on the window that is active", "[tabs]")
{
    // WHY THE HANDLERS CAPTURE NOTHING. `ActionRegistry` is one object shared by every window, and
    // registration happens once; a handler holding the window that registered it would send every
    // window's commands to the first one -- and to freed memory once that window closed.
    Fixture fixture;

    ui::MainWindow first(fixture.services());
    first.openFiles({fixture.writePdf(QStringLiteral("a.pdf")),
                     fixture.writePdf(QStringLiteral("b.pdf"))});

    ui::MainWindow second(fixture.services());
    second.openFiles({fixture.writePdf(QStringLiteral("c.pdf"))});

    // Registration happened once -- the second window found the commands already there and did not
    // try to register them again, which would have been twenty-two "duplicate registration" errors.
    CHECK(fixture.actions.registeredCount() == 22);

    second.raiseToForeground();
    CHECK(ui::MainWindow::activeShell() == &second);

    REQUIRE(fixture.actions.dispatch(ActionId::FileCloseAll, core::KeyContext::Global));
    CHECK(second.documentCount() == 0);
    CHECK(first.documentCount() == 2);   // the window nobody was looking at is untouched

    first.raiseToForeground();
    CHECK(ui::MainWindow::activeShell() == &first);
    REQUIRE(fixture.actions.dispatch(ActionId::FileCloseAll, core::KeyContext::Global));
    CHECK(first.documentCount() == 0);
}

TEST_CASE("the window set drops a window as it is destroyed", "[tabs]")
{
    // The other half of the same problem, and the one that crashes rather than misbehaves: a
    // destroyed window left in the set is a window a Global command dispatches into.
    Fixture fixture;
    ui::MainWindow keep(fixture.services());
    const qsizetype before = ui::MainWindow::shellWindows().size();

    {
        ui::MainWindow temporary(fixture.services());
        CHECK(ui::MainWindow::shellWindows().size() == before + 1);
        CHECK(ui::MainWindow::activeShell() == &temporary);
    }

    CHECK(ui::MainWindow::shellWindows().size() == before);
    CHECK(ui::MainWindow::activeShell() == &keep);

    // And a command dispatched now reaches the surviving window rather than the destroyed one. A
    // command with no enablement predicate, deliberately: what is under test is that the dispatch
    // lands somewhere alive, and a refusal from an enablement check would look the same as one.
    REQUIRE(fixture.actions.dispatch(ActionId::ViewToggleRibbon, core::KeyContext::Global));
    CHECK(keep.ribbon()->isBodyCollapsed());
}

TEST_CASE("Ctrl+F6 walks the window set and refuses when there is one window", "[tabs]")
{
    Fixture fixture;
    ui::MainWindow only(fixture.services());

    const core::Result<void> alone =
        fixture.actions.dispatch(ActionId::WindowNext, core::KeyContext::Global);
    REQUIRE_FALSE(alone);
    CHECK(alone.error().code == core::ErrorCode::Unavailable);

    ui::MainWindow second(fixture.services());
    second.raiseToForeground();
    REQUIRE(ui::MainWindow::activeShell() == &second);

    REQUIRE(fixture.actions.dispatch(ActionId::WindowNext, core::KeyContext::Global));
    CHECK(ui::MainWindow::activeShell() == &only);

    REQUIRE(fixture.actions.dispatch(ActionId::WindowPrev, core::KeyContext::Global));
    CHECK(ui::MainWindow::activeShell() == &second);
}

// ===========================================================================================
// I-33 and I-34 -- both found by the user walking M-03 on 2026-08-30, and both `detachDocument`
// being asked the wrong question: one never asked the count, the other asked the position too
// early.
// ===========================================================================================

TEST_CASE("a window's only document is not detached into a new window", "[tabs]")
{
    // I-33. The move is a null one -- the same lone document ends up alone in a window either way,
    // and the user is left with an empty shell to close.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.openFiles({fixture.writePdf(QStringLiteral("only.pdf"))});
    REQUIRE(window.documentCount() == 1);

    const int windowsBefore = ui::MainWindow::shellWindows().size();

    CHECK(window.detachDocument(0, QPoint(4000, 4000)) == nullptr);
    CHECK(window.documentCount() == 1);                             // the tab stayed put
    CHECK(ui::MainWindow::shellWindows().size() == windowsBefore);  // and no window appeared

    // THE CONTROL, because a guard that refused everything would pass all three checks above and be
    // a worse defect than the one it fixes. A second document makes the same call legitimate.
    window.openFiles({fixture.writePdf(QStringLiteral("second.pdf"))});
    ui::MainWindow* torn = window.detachDocument(1, QPoint(4000, 4000));
    REQUIRE(torn != nullptr);
    CHECK(window.documentCount() == 1);
    CHECK(torn->documentCount() == 1);

    torn->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("a lone document can still be dropped onto another window's strip", "[tabs]")
{
    // I-33's GUARD PLACEMENT, which is the whole subtlety of that fix. A torn-off window holds
    // exactly one document by construction, so a count guard at the top of `detachDocument` would
    // make dragging it back the one re-dock nobody could perform -- the commonest one there is, and
    // a required step of M-03. The guard sits after this branch, and this case is what says so.
    Fixture fixture;
    ui::MainWindow source(fixture.services());
    ui::MainWindow target(fixture.services());

    source.openFiles({fixture.writePdf(QStringLiteral("lone.pdf"))});
    target.openFiles({fixture.writePdf(QStringLiteral("host.pdf"))});
    source.show();
    target.show();

    ui::DocumentWindow* moved = source.documentAt(0);
    REQUIRE(moved != nullptr);

    const QPoint overTargetStrip =
        target.documentTabs()->mapToGlobal(target.documentTabs()->rect().center());

    CHECK(source.detachDocument(0, overTargetStrip) == &target);
    CHECK(source.documentCount() == 0);
    CHECK(target.documentCount() == 2);
    CHECK(target.currentDocument() == moved);   // the same object, not one rebuilt from the path
}

TEST_CASE("the tear-off gesture reports where the pointer was RELEASED", "[tabs]")
{
    // I-34, and the assertion that would have caught it: the drag must report NOTHING when it
    // crosses the threshold, and must report the RELEASE position when the button comes up.
    // Emitting at the crossing gave the gesture one sample to land on a ~25 px strip, which made
    // re-dock unreachable by hand while every automated case here stayed green.
    //
    // A bare tab bar, because this is about the widget's own state machine -- and because letting a
    // MainWindow receive the signal would spawn a window as a side effect of a test about timing.
    ui::DocumentTabBar bar;
    bar.addTab(QStringLiteral("a.pdf"));
    bar.addTab(QStringLiteral("b.pdf"));
    bar.resize(400, 32);
    bar.show();

    int    reports = 0;
    int    reportedIndex = -1;
    QPoint reportedPos;
    QObject::connect(&bar, &ui::DocumentTabBar::tearOffRequested,
                     [&](int index, const QPoint& pos) {
                         ++reports;
                         reportedIndex = index;
                         reportedPos = pos;
                     });

    const QPoint grab = bar.tabRect(0).center();
    QMouseEvent  press(QEvent::MouseButtonPress, grab, bar.mapToGlobal(grab), Qt::LeftButton,
                       Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&bar, &press);

    // Clear of the strip by more than the threshold -- the moment the old code fired.
    const QPoint crossed(grab.x(), bar.rect().top() - (ui::DocumentTabBar::kTearOffDistance + 10));
    QMouseEvent  moveOut(QEvent::MouseMove, crossed, bar.mapToGlobal(crossed), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&bar, &moveOut);

    CHECK(reports == 0);   // THE REGRESSION ASSERTION. This read 1 before the fix.

    // On, to somewhere else entirely -- as a hand does on its way to another window's strip.
    const QPoint ended(grab.x() + 260, crossed.y() - 90);
    QMouseEvent  moveOn(QEvent::MouseMove, ended, bar.mapToGlobal(ended), Qt::NoButton,
                        Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&bar, &moveOn);
    CHECK(reports == 0);

    QMouseEvent release(QEvent::MouseButtonRelease, ended, bar.mapToGlobal(ended), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&bar, &release);

    CHECK(reports == 1);
    CHECK(reportedIndex == 0);
    CHECK(reportedPos == bar.mapToGlobal(ended));   // the release, not the crossing
}

TEST_CASE("a torn-off window closes when its last document is moved back", "[tabs]")
{
    // I-35, found by the user walking M-03 on 2026-08-30 once re-dock could actually be performed:
    // the window the tab came FROM stayed behind, empty. A window emptied by a move has done its
    // job. This is the whole round trip -- tear out, drag back -- which is M-03's step in one case.
    Fixture fixture;
    ui::MainWindow home(fixture.services());
    home.openFiles({fixture.writePdf(QStringLiteral("stays.pdf")),
                    fixture.writePdf(QStringLiteral("travels.pdf"))});
    home.show();

    ui::DocumentWindow* travelling = home.documentAt(1);
    ui::MainWindow*     torn       = home.detachDocument(1, QPoint());
    REQUIRE(torn != nullptr);
    REQUIRE(torn->documentCount() == 1);
    torn->show();

    const QPoint overHomeStrip =
        home.documentTabs()->mapToGlobal(home.documentTabs()->rect().center());

    CHECK(torn->detachDocument(0, overHomeStrip) == &home);
    CHECK(home.documentCount() == 2);
    CHECK(home.currentDocument() == travelling);   // the same object came home

    // `WA_DeleteOnClose` posts a DeferredDelete; the event loop is what runs it, and there is none
    // in a Catch2 case -- so deliver that one event by hand, exactly as the tear-off case above
    // does. Without it this reads "still in the set", which is true and means nothing.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK_FALSE(ui::MainWindow::shellWindows().contains(torn));
}

TEST_CASE("a window that does not own itself is emptied but not closed", "[tabs]")
{
    // I-35's OTHER HALF, and the control: `closeIfEmptied` must not close everything it is handed.
    // The primary window is a stack object in `app/main.cpp` -- Qt may not delete it, and `close()`
    // would only hide it, leaving a hidden entry in the window set for `Ctrl+F6` to walk into and
    // for a single-instance forward to call `openFiles()` on. An empty window on screen is the
    // state a launch with no files produces, so stopping there is correct rather than a shortfall.
    Fixture fixture;
    ui::MainWindow source(fixture.services());   // no WA_DeleteOnClose -- as `main()` builds it
    ui::MainWindow target(fixture.services());
    source.openFiles({fixture.writePdf(QStringLiteral("lone.pdf"))});
    target.openFiles({fixture.writePdf(QStringLiteral("host.pdf"))});
    source.show();
    target.show();

    const QPoint overTargetStrip =
        target.documentTabs()->mapToGlobal(target.documentTabs()->rect().center());

    CHECK(source.detachDocument(0, overTargetStrip) == &target);
    CHECK(source.documentCount() == 0);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(ui::MainWindow::shellWindows().contains(&source));   // still there, and still visible
    CHECK(source.isVisible());
}
