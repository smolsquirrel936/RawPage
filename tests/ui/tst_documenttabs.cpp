// Stage 1 T12 -- document tabs and the stub DocumentWindow. Guide cases A-57 and the second half of
// A-58, plus the structural half of manual case M-03.
//
// WHAT IS MANUAL AND WHY. M-03 walks the GESTURES: dragging a tab to reorder it, dragging one clear
// of the strip, dropping it back on another window. Offscreen there is no pointer, so what is
// asserted here is everything those gestures END in -- reorder, release, adopt, detach, close --
// each reachable as a function and each also reachable as a command. A case here claiming to have
// tested a drag would be a defect in the case.

#include <catch2/catch_test_macros.hpp>

#include "app/Services.h"
#include "core/cmd/ActionRegistry.h"
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
    platform::Platform    platform;

    app::Services services()
    {
        return app::Services{settings, recentFiles, actions, keyBindings, platform};
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
    // The gesture is M-03; this is what the gesture ends in. The drop-onto-another-strip branch of
    // `detachDocument` needs a pointer position over a real strip and stays manual -- what is
    // asserted here is the branch that makes a window, and that the document went WITH it rather
    // than being rebuilt from its path.
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
    // try to register them again, which would have been seventeen "duplicate registration" errors.
    CHECK(fixture.actions.registeredCount() == 17);

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
