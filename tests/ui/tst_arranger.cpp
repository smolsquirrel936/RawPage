// Stage 4 T11 -- the window arranger. Guide cases A-64, A-65 and A-66
// (`Stage4-ViewingSurface-Testing-Guide.md` §5.12), and decision **D4-17**
// (`Stage4-ViewingSurface-Plan.md` §3).
//
// ---------------------------------------------------------------------------------------------
// THE ARITHMETIC IS JUDGED WITH NO SCREEN, AND THAT IS THE POINT OF A-64.
//
// `WindowArranger::tileGeometry` and `cascadeGeometry` are pure functions of a work area and a
// count, so *"the tiles fill the work area and do not overlap"* is checked over synthetic
// rectangles rather than over whatever the offscreen platform reports for its one screen. A case
// written against the real screen would assert something different on every machine and would have
// to be loosened until it asserted nothing.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS SUITE CANNOT SEE, NAMED HERE RATHER THAN DISCOVERED (convention 19).
//
//   * **It cannot see a taskbar.** `tile()` and `cascade()` read `QScreen::availableGeometry()`,
//     and under `offscreen` that is the whole screen. Whether a real tiling clears a real taskbar,
//     and what happens with windows on two monitors, is manual case **M-09**.
//   * **It cannot judge stacking order.** `cascade()` raises each window in turn; whether the
//     result LOOKS like a cascade is M-09's.
//   * **No real engine.** `uitest::FakeBackend` decides the page count and hands a distinct
//     `DocumentHandle` out per `open()` -- which is what makes the second-view case able to assert
//     that two views of one file hold two handles.
//   * **The lane cases assert the SCHEDULER'S COUNTERS, not a frame time.** A2's named work is
//     *an unfocused window enqueues at `Prefetch`*; whether that buys the milliseconds
//     `Tech-Stack-Plan.md` §3.3 predicts is `bench_pipeline`'s to measure, never this suite's.
//
// ---------------------------------------------------------------------------------------------
// EVERY MULTI-WINDOW CASE REQUIREs THE WINDOW COUNT BEFORE IT ACTS.
//
// `MainWindow::shellWindows()` is one list for the whole process and `mergeWindows()` targets its
// FIRST entry. A window leaked by an earlier case would silently make these cases merge into
// something else and still pass some of their assertions, so the count is a REQUIRE rather than an
// assumption.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "core/render/RenderScheduler.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/RotatePagesDialog.h"
#include "ui/shell/DocumentTabBar.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/WindowArranger.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SplitViewContainer.h"
#include "ui/viewer/TilePresenter.h"
#include "ui/viewer/ViewportController.h"

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QLayout>
#include <QRect>

#include <memory>

using namespace rawpage;
using core::ActionId;
using Catch::Matchers::WithinAbs;
using ui::WindowArranger;

namespace {

using Orientation = WindowArranger::Orientation;

struct Fixture : uitest::ShellFixture {
    Fixture() { backend.editable = true; }

    QString writePdf(const QString& name)
    {
        const QString path = QDir(directory.path()).filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("%PDF-1.7\n% the fake backend decides, not these bytes\n");
        file.close();
        return QFileInfo(path).absoluteFilePath();
    }
};

void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

// Do any two of these rectangles share a pixel?
bool anyOverlap(const QList<QRect>& cells)
{
    for (int i = 0; i < cells.size(); ++i)
        for (int j = i + 1; j < cells.size(); ++j)
            if (cells.at(i).intersects(cells.at(j))) return true;
    return false;
}

// The summed area of a set of non-overlapping rectangles.
long long coveredArea(const QList<QRect>& cells)
{
    long long total = 0;
    for (const QRect& cell : cells)
        total += static_cast<long long>(cell.width()) * cell.height();
    return total;
}

// Everything a second window needs, torn down in the right order.
struct Desk {
    Fixture                         fixture;
    std::unique_ptr<ui::MainWindow> first;

    explicit Desk(int pages = 8)
    {
        fixture.backend.pagesPerDocument = pages;
        first = std::make_unique<ui::MainWindow>(fixture.services());
        first->resize(900, 700);
    }

    // A tab is deleted later and its destructor closes its handle on THIS fixture's backend
    // (`CLAUDE.md` -> Traps, S3 T10). Left pending past the fixture, the next case's first
    // `processEvents` runs it against a destroyed backend.
    ~Desk()
    {
        for (ui::MainWindow* window : ui::MainWindow::shellWindows())
            if (window != first.get() && window->testAttribute(Qt::WA_DeleteOnClose)) window->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        first.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    ui::DocumentWindow* open(const QString& name)
    {
        first->openFiles({fixture.writePdf(name)});
        driveLayout(first.get());
        first->raiseToForeground();
        return first->currentDocument();
    }

    void dispatch(ActionId id)
    {
        const core::Result<void> sent = fixture.actions.dispatch(id, core::KeyContext::Global);
        INFO("refused: " << (sent ? std::string()
                                  : std::string(sent.error().messageKey.data(),
                                                sent.error().messageKey.size())));
        REQUIRE(sent);
        QCoreApplication::processEvents();
    }
};

} // namespace

// =============================================================================================
// A-64 -- tile and cascade over synthetic screen geometry
// =============================================================================================

TEST_CASE("tiling covers the work area exactly and never overlaps", "[arranger]")
{
    const QRect work(0, 0, 1920, 1040);

    for (const int count : {1, 2, 3, 4, 5, 7}) {
        for (const Orientation orientation : {Orientation::Horizontal, Orientation::Vertical}) {
            INFO("count " << count << " orientation " << (orientation == Orientation::Horizontal ? "H" : "V"));
            const QList<QRect> cells = WindowArranger::tileGeometry(work, count, orientation);

            REQUIRE(cells.size() == count);
            CHECK_FALSE(anyOverlap(cells));

            // EXACT COVER, not "roughly the right size". A tiling computed edge-by-edge loses a
            // pixel to truncation wherever the division is not whole, and one-pixel gaps between
            // tiled windows are visible from across the room -- which is why `shares()` returns
            // sizes that sum to the total rather than positions computed from a ratio.
            CHECK(coveredArea(cells)
                  == static_cast<long long>(work.width()) * work.height());

            for (const QRect& cell : cells) {
                CHECK(work.contains(cell));
                CHECK(cell.width() >= WindowArranger::kMinTileWidth);
                CHECK(cell.height() >= WindowArranger::kMinTileHeight);
            }
        }
    }
}

TEST_CASE("the tiling orientation names the divider, and the minimum forces a grid", "[arranger]")
{
    const QRect work(0, 0, 1920, 1040);

    // HORIZONTAL IS A HORIZONTAL DIVIDER: full-width strips, one above another. This is the coin
    // flip `WindowArranger.h` and `SplitViewContainer.h` both record, and swapping the two would
    // look like a working product.
    const QList<QRect> stacked = WindowArranger::tileGeometry(work, 2, Orientation::Horizontal);
    REQUIRE(stacked.size() == 2);
    CHECK(stacked.at(0).width() == work.width());
    CHECK(stacked.at(1).top() > stacked.at(0).top());

    const QList<QRect> sideBySide = WindowArranger::tileGeometry(work, 2, Orientation::Vertical);
    REQUIRE(sideBySide.size() == 2);
    CHECK(sideBySide.at(0).height() == work.height());
    CHECK(sideBySide.at(1).left() > sideBySide.at(0).left());

    // EIGHT FULL-WIDTH STRIPS ON A 1040-PIXEL WORK AREA IS 130 PIXELS EACH -- half the minimum. So
    // the strips stop at four and the surplus is laid ACROSS them, which is the grid fallback, and
    // the cover and the minimum both still hold.
    const QList<QRect> many = WindowArranger::tileGeometry(work, 8, Orientation::Horizontal);
    REQUIRE(many.size() == 8);
    CHECK_FALSE(anyOverlap(many));
    CHECK(coveredArea(many) == static_cast<long long>(work.width()) * work.height());
    for (const QRect& cell : many) {
        CHECK(cell.height() >= WindowArranger::kMinTileHeight);
        CHECK(cell.width() >= WindowArranger::kMinTileWidth);
    }
    CHECK(many.at(0).width() < work.width());   // it really did become a grid

    // WHAT THE MINIMUM CANNOT DO, asserted rather than left implied (convention 19). A work area
    // too small to hold `count` cells at the minimum gets smaller cells: no arrangement makes a
    // 300x200 desktop hold four readable windows, and a window pushed off the work area to keep it
    // large would be worse than a small one. The cover and the non-overlap still hold, and they
    // are what the case claims here.
    const QRect tiny(0, 0, 300, 200);
    const QList<QRect> squeezed = WindowArranger::tileGeometry(tiny, 4, Orientation::Horizontal);
    REQUIRE(squeezed.size() == 4);
    CHECK_FALSE(anyOverlap(squeezed));
    CHECK(coveredArea(squeezed) == static_cast<long long>(tiny.width()) * tiny.height());
    CHECK(squeezed.at(0).height() < WindowArranger::kMinTileHeight);
}

TEST_CASE("a cascade steps diagonally and keeps every window inside the work area", "[arranger]")
{
    const QRect work(100, 50, 1600, 1000);
    const int step = 30;

    const QList<QRect> cells = WindowArranger::cascadeGeometry(work, 6, step);
    REQUIRE(cells.size() == 6);

    // THE STEP IS ASSERTED AGAINST THE VALUE THE ARRANGEMENT WAS GIVEN, never against a constant:
    // the product asks the style what a title bar is worth, and a theme with taller title bars must
    // not redden this.
    CHECK(cells.at(1).left() - cells.at(0).left() == step);
    CHECK(cells.at(1).top() - cells.at(0).top() == step);
    CHECK(cells.at(0).size() == cells.at(5).size());

    // EVERY ONE WHOLLY INSIDE THE WORK AREA -- which is a stronger claim than *"its title bar is
    // on screen"* and the one that can be made without knowing what a title bar is worth here.
    for (const QRect& cell : cells) CHECK(work.contains(cell));

    // AND THE DIAGONAL RESTARTS rather than marching off the bottom-right. With a 1600x1000 work
    // area a cascaded window is 1200x750, leaving 250 pixels of room, so the run is nine windows
    // long and the tenth is back at the origin.
    const QList<QRect> wrapped = WindowArranger::cascadeGeometry(work, 40, step);
    REQUIRE(wrapped.size() == 40);
    for (const QRect& cell : wrapped) CHECK(work.contains(cell));
    CHECK(wrapped.contains(wrapped.at(0)));
    CHECK(wrapped.count(wrapped.at(0)) > 1);   // it wrapped at least once
}

TEST_CASE("arranging with one window changes nothing, and the commands say why", "[arranger]")
{
    // THE CONTROL A-64 ASKS FOR, and it is asserted from both ends: the geometry does not move, AND
    // the command is refused rather than silently doing nothing. Only the second distinguishes a
    // one-window tiling from a tiling that is broken (Stage 1's I-53).
    Desk desk;
    REQUIRE(desk.open(QStringLiteral("only.pdf")) != nullptr);
    REQUIRE(ui::MainWindow::shellWindows().size() == 1);

    const QRect before = desk.first->geometry();

    for (const ActionId id : {ActionId::WindowTileHorizontal, ActionId::WindowTileVertical,
                              ActionId::WindowCascade, ActionId::WindowMerge}) {
        REQUIRE(desk.fixture.actions.isRegistered(id));
        const core::Result<void> sent = desk.fixture.actions.dispatch(id, core::KeyContext::Global);
        CHECK_FALSE(sent);
        CHECK(sent.error().code == core::ErrorCode::Unavailable);
    }

    // AND THE ARITHMETIC ITSELF IS A NO-OP FOR ONE WINDOW: one cell, the whole work area.
    WindowArranger::tile(Orientation::Horizontal);
    WindowArranger::cascade();
    CHECK(desk.first->geometry() == before);

    // `window.list` IS OFFERED WITH ONE WINDOW and is not among the four above: a list of one is a
    // true statement about the application, whereas *tile one window* is not an operation. It is
    // refused here only because an offscreen run has nobody to show a popup to (I-23's predicate).
    REQUIRE(desk.fixture.actions.isRegistered(ActionId::WindowList));
    CHECK(WindowArranger::windowList().size() == 1);
}

// =============================================================================================
// A-65 -- merge re-docks every detached tab, in order
// =============================================================================================

TEST_CASE("merge puts every detached tab back into one window, in order", "[arranger]")
{
    Desk desk;

    const QStringList names{QStringLiteral("a.pdf"), QStringLiteral("b.pdf"),
                            QStringLiteral("c.pdf"), QStringLiteral("d.pdf"),
                            QStringLiteral("e.pdf")};
    QList<ui::DocumentWindow*> documents;
    for (const QString& name : names) documents.append(desk.open(name));
    REQUIRE(desk.first->documentCount() == 5);

    // TORN OFF THE WAY A PERSON DOES IT. `QPoint(4000, 4000)` is nowhere near any strip, which is
    // what makes this the make-a-window branch rather than the drop-on-another-window one
    // (tst_documenttabs records that distinction).
    //
    // **`d` IS TORN OFF BEFORE `e`, AND THAT IS NOT ARBITRARY.** The merged order is the window
    // creation order with each window's tabs in tab order, because that is the only order the
    // arranger can see -- a detached window records nothing about which tab its document used to
    // be. So *"in their original order"* holds exactly when the windows were made in that order,
    // which is what a person tearing tabs out left to right produces, and what this case walks.
    // **What no merge can restore** (convention 19): tearing `e` out first and `d` second puts them
    // back as `e`, `d`. Nothing in the product claims otherwise and no case here asserts it.
    ui::MainWindow* second = desk.first->detachDocument(3, QPoint(4000, 4000));
    REQUIRE(second != nullptr);
    ui::MainWindow* third = desk.first->detachDocument(3, QPoint(4000, 4000));
    REQUIRE(third != nullptr);

    REQUIRE(ui::MainWindow::shellWindows().size() == 3);
    REQUIRE(desk.first->documentCount() == 3);

    // THE SCROLL POSITION IS TAKEN BEFORE THE MERGE, because *"each keeping its handle, journal and
    // scroll position"* is what distinguishes a MOVE from a close and a re-open -- and a re-open
    // would look identical in the tab strip.
    driveLayout(third);
    const core::DocumentHandle handleOfE = documents.at(4)->handle();
    core::CommandJournal* journalOfE = documents.at(4)->journal();
    documents.at(4)->view()->controller().setScroll(core::DevicePoint{0.0, 120.0});
    const core::DevicePoint scrollOfE = documents.at(4)->view()->controller().scroll();

    const int moved = WindowArranger::mergeWindows();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    CHECK(moved == 2);
    REQUIRE(ui::MainWindow::shellWindows().size() == 1);
    CHECK(ui::MainWindow::shellWindows().first() == desk.first.get());
    REQUIRE(desk.first->documentCount() == 5);

    // IN THEIR ORIGINAL ORDER, which is only a meaningful claim because the target is the FIRST
    // window rather than the active one -- merging into whichever window the person stood in would
    // make the answer depend on where they clicked.
    for (int i = 0; i < names.size(); ++i) {
        INFO("tab " << i);
        CHECK(desk.first->documentAt(i) == documents.at(i));
        CHECK(desk.first->documentTabs()->tabText(i) == names.at(i));
    }

    // AND EACH KEPT WHAT MAKES IT ITSELF.
    CHECK(documents.at(4)->handle() == handleOfE);
    CHECK(documents.at(4)->journal() == journalOfE);
    CHECK_THAT(documents.at(4)->view()->controller().scroll().y, WithinAbs(scrollOfE.y, 0.5));
}

// =============================================================================================
// A-66 -- the window list, and `window.new` as a second view of one document
// =============================================================================================

TEST_CASE("the window list names every window and switching activates it", "[arranger]")
{
    Desk desk;
    REQUIRE(desk.open(QStringLiteral("first.pdf")) != nullptr);
    REQUIRE(desk.open(QStringLiteral("second.pdf")) != nullptr);

    ui::MainWindow* torn = desk.first->detachDocument(1, QPoint(4000, 4000));
    REQUIRE(torn != nullptr);
    REQUIRE(ui::MainWindow::shellWindows().size() == 2);

    const QStringList list = WindowArranger::windowList();
    REQUIRE(list.size() == 2);
    CHECK(list.at(0).contains(QStringLiteral("first.pdf")));
    CHECK(list.at(1).contains(QStringLiteral("second.pdf")));

    // SWITCHING ACTIVATES. `activeShell()` is what every Global command resolves through, so this
    // is the claim that matters rather than anything about a menu: choosing a row in the list must
    // be the same thing as bringing that window forward.
    CHECK(ui::MainWindow::activeShell() == torn);
    WindowArranger::activateWindow(0);
    CHECK(ui::MainWindow::activeShell() == desk.first.get());
    WindowArranger::activateWindow(1);
    CHECK(ui::MainWindow::activeShell() == torn);

    // OUT OF RANGE IS A NO-OP rather than a crash: the list a menu was built from can be stale by
    // the time it is clicked, because closing a window is one event away.
    WindowArranger::activateWindow(9);
    CHECK(ui::MainWindow::activeShell() == torn);
}

TEST_CASE("window.new opens a second view of one document, with its own handle and one journal",
          "[arranger]")
{
    Desk desk;
    ui::DocumentWindow* leader = desk.open(QStringLiteral("shared.pdf"));
    REQUIRE(leader != nullptr);
    REQUIRE(leader->journal() != nullptr);

    desk.dispatch(ActionId::WindowNew);
    REQUIRE(ui::MainWindow::shellWindows().size() == 2);

    ui::MainWindow* second = ui::MainWindow::shellWindows().at(1);
    driveLayout(second);
    ui::DocumentWindow* companion = second->currentDocument();
    REQUIRE(companion != nullptr);

    // D4-13'S EXCEPTION, AS AN IDENTITY AND A NON-IDENTITY IN THE SAME BREATH. Same file, DIFFERENT
    // handle -- two windows are two `DocumentWindow`s and each runs the three-step close protocol,
    // so a shared handle would be closed out from under the other. A split shares one for exactly
    // the opposite reason.
    CHECK(companion->path() == leader->path());
    CHECK(companion->handle() != leader->handle());
    CHECK(companion->view()->document() == companion->handle());

    // AND ONE JOURNAL FOR THE FILE. Two journals would be two undo stacks and two Saves racing for
    // the same bytes -- the "changed on disk" path D3-16 exists to refuse, manufactured by the
    // product itself. So the companion has none, is not editable and is never dirty.
    CHECK(companion->journal() == nullptr);
    CHECK(companion->isCompanion());
    CHECK(companion->leader() == leader);
    CHECK(leader->companions().size() == 1);
    CHECK_FALSE(companion->isEditable());
    CHECK_FALSE(companion->isDirty());
}

TEST_CASE("a rotation in one window appears in the second view of the same document", "[arranger]")
{
    Desk desk;
    ui::DocumentWindow* leader = desk.open(QStringLiteral("rotate.pdf"));
    REQUIRE(leader != nullptr);

    desk.dispatch(ActionId::WindowNew);
    REQUIRE(ui::MainWindow::shellWindows().size() == 2);
    ui::MainWindow* second = ui::MainWindow::shellWindows().at(1);
    second->resize(900, 700);
    driveLayout(second);

    ui::DocumentWindow* companion = second->currentDocument();
    REQUIRE(companion != nullptr);

    // GEOMETRY BEFORE PIXELS (Stage 1's I-36): the laid-out page size is what a rotation changes
    // first, and asserting it is what distinguishes *the model rotated* from *a tile arrived*.
    const core::render::PageLayout& companionLayout = companion->view()->layoutEngine().layout();
    REQUIRE(!companionLayout.pageSlots().empty());
    const double widthBefore = companionLayout.pageSlots().front().rect.size.width;
    const double heightBefore = companionLayout.pageSlots().front().rect.size.height;

    const core::Revision before = leader->revision();
    REQUIRE(companion->revision() == before);

    // THE ROTATION IS RUN IN THE LEADER'S WINDOW, through the registry, which is where the one
    // journal is. Raising it first is what makes `activeShell()` -- and therefore the command --
    // resolve there.
    desk.first->raiseToForeground();
    ui::RotatePagesDialog::setAnswerForTesting([] { return ui::RotatePagesDialog::Choice{90, false}; });
    REQUIRE(desk.fixture.actions.dispatch(ActionId::PageRotate, core::KeyContext::Viewer));
    ui::RotatePagesDialog::setAnswerForTesting({});

    driveLayout(second);
    desk.fixture.pumpTiles();

    const core::Revision after = leader->revision();
    CHECK(after != before);

    // THE SECOND VIEW IS AT THE SAME REVISION AND SHOWS THE SAME, UNSAVED, ROTATION. It lays out
    // from the LEADER'S model tree while rasterizing through its own handle (D3-15 one window
    // further out) -- a companion that read its own engine tree would show the page upright, with
    // no error and nothing on screen to say so.
    CHECK(companion->revision() == after);
    CHECK(companion->view()->revision() == after);

    const double widthAfter = companion->view()->layoutEngine().layout().pageSlots().front().rect.size.width;
    const double heightAfter = companion->view()->layoutEngine().layout().pageSlots().front().rect.size.height;
    CHECK_THAT(widthAfter, WithinAbs(heightBefore, 1.0));
    CHECK_THAT(heightAfter, WithinAbs(widthBefore, 1.0));
}

TEST_CASE("closing the leader detaches the second view rather than taking it down", "[arranger]")
{
    Desk desk;
    ui::DocumentWindow* leader = desk.open(QStringLiteral("closing.pdf"));
    REQUIRE(leader != nullptr);

    desk.dispatch(ActionId::WindowNew);
    ui::MainWindow* second = ui::MainWindow::shellWindows().at(1);
    driveLayout(second);
    ui::DocumentWindow* companion = second->currentDocument();
    REQUIRE(companion != nullptr);
    REQUIRE(companion->leader() == leader);

    const core::DocumentHandle companionHandle = companion->handle();

    // THE LEADER'S TAB GOES, and with it the model page tree the companion was laying out from. A
    // companion left pointing at it would lay out over freed memory on its next paint -- which is
    // why the detach is in the destructor rather than in whatever closed the tab.
    REQUIRE(desk.first->closeDocument(0, ui::CloseDisposition::Discard));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();

    CHECK_FALSE(companion->isCompanion());
    CHECK(companion->leader() == nullptr);

    // AND IT GOES ON SHOWING THE FILE, through the handle it has always had. This is the correct
    // answer rather than a fallback: the leader's disposition has just been settled, and what this
    // handle rasterizes is the bytes on disk.
    CHECK(companion->handle() == companionHandle);
    CHECK(companion->view()->document() == companionHandle);
    driveLayout(second);
    CHECK(companion->view()->pageCount() > 0);
}

TEST_CASE("two views of one document scroll together until window.syncScroll unlinks them",
          "[arranger]")
{
    Desk desk;
    ui::DocumentWindow* leader = desk.open(QStringLiteral("sync.pdf"));
    REQUIRE(leader != nullptr);

    desk.dispatch(ActionId::WindowNew);
    ui::MainWindow* second = ui::MainWindow::shellWindows().at(1);
    second->resize(900, 700);
    driveLayout(second);
    ui::DocumentWindow* companion = second->currentDocument();
    REQUIRE(companion != nullptr);

    // LAID OUT FIRST. The mirror converts through `PageLayout::presentedScale()` and refuses a view
    // that has no scale yet, so a case that skipped the layout would assert that nothing moved --
    // and would pass for the wrong reason (convention 12, guide rule 4).
    driveLayout(desk.first.get());
    REQUIRE(leader->view()->layoutEngine().layout().presentedScale() > 0.0);
    REQUIRE(companion->view()->layoutEngine().layout().presentedScale() > 0.0);

    // ON BY DEFAULT: `window.new` is asked for in order to look at two parts of one document, and a
    // pair that opened unlinked would make the feature something you have to find twice.
    REQUIRE(leader->synchronizedWithOtherWindows());
    REQUIRE(companion->synchronizedWithOtherWindows());

    leader->view()->controller().setScroll(core::DevicePoint{0.0, 200.0});
    QCoreApplication::processEvents();
    CHECK_THAT(companion->view()->controller().scroll().y, WithinAbs(200.0, 1.0));

    // AND IT MIRRORS BOTH WAYS without echoing. Two windows each following the other is a loop with
    // no fixed point; the guard is what makes this a single assignment rather than a hang.
    companion->view()->controller().setScroll(core::DevicePoint{0.0, 80.0});
    QCoreApplication::processEvents();
    CHECK_THAT(leader->view()->controller().scroll().y, WithinAbs(80.0, 1.0));

    // `window.syncScroll` UNLINKS THEM. One switch, two subjects: with no split it is the windows
    // that it frees, which is `Features.md` §1.1's row rather than §1.2's.
    desk.first->raiseToForeground();
    REQUIRE(desk.fixture.actions.dispatch(ActionId::WindowSyncScroll, core::KeyContext::Global));
    CHECK_FALSE(leader->synchronizedWithOtherWindows());
    CHECK_FALSE(companion->synchronizedWithOtherWindows());

    const double parked = companion->view()->controller().scroll().y;
    leader->view()->controller().setScroll(core::DevicePoint{0.0, 400.0});
    QCoreApplication::processEvents();
    CHECK_THAT(companion->view()->controller().scroll().y, WithinAbs(parked, 0.5));
}

// =============================================================================================
// A2's named work -- a window that is not in front queues its viewport at `Prefetch`
// =============================================================================================

TEST_CASE("a background window asks for its viewport at Prefetch, not Visible", "[arranger]")
{
    // `Tech-Stack-Plan.md` §3.3's *A2 at Stage 4* records the two-window column as leaving the
    // 16.667 ms budget under a NAMED condition, and names this as the work that answers it. The
    // claim is about the LANE and nothing else: the same tiles are asked for and they arrive, so
    // no picture and no count of requests can distinguish the two states -- only the counters can.
    Desk desk;
    ui::DocumentWindow* leader = desk.open(QStringLiteral("lanes.pdf"));
    REQUIRE(leader != nullptr);

    desk.dispatch(ActionId::WindowNew);
    REQUIRE(ui::MainWindow::shellWindows().size() == 2);
    ui::MainWindow* second = ui::MainWindow::shellWindows().at(1);
    second->resize(900, 700);
    driveLayout(second);
    ui::DocumentWindow* companion = second->currentDocument();
    REQUIRE(companion != nullptr);
    desk.fixture.pumpTiles();

    // WITH THE SECOND WINDOW IN FRONT it is the FIRST that is in the background, which is the state
    // `window.new` leaves behind and therefore the one worth asserting first.
    CHECK(companion->view()->tiles().isBackground() == false);
    CHECK(leader->view()->tiles().isBackground() == true);

    desk.first->raiseToForeground();
    QCoreApplication::processEvents();
    CHECK(leader->view()->tiles().isBackground() == false);
    CHECK(companion->view()->tiles().isBackground() == true);

    // NOW SCROLL THE BACKGROUND WINDOW so it has new tiles to ask for. Its sync link is cut first,
    // or scrolling it would scroll the foreground window too and both would enqueue.
    leader->setSynchronizedWithOtherWindows(false);
    desk.fixture.pumpTiles();

    const auto before = desk.fixture.renderScheduler.stats();
    companion->view()->controller().setScroll(core::DevicePoint{0.0, 900.0});
    driveLayout(second);
    desk.fixture.pumpTiles();
    const auto after = desk.fixture.renderScheduler.stats();

    INFO("visible " << before.enqueuedVisible << " -> " << after.enqueuedVisible
                    << ", prefetch " << before.enqueuedPrefetch << " -> " << after.enqueuedPrefetch);
    CHECK(after.enqueuedPrefetch > before.enqueuedPrefetch);
    CHECK(after.enqueuedVisible == before.enqueuedVisible);

    // AND THE CONTROL, which is the half convention 22 says the criterion will not ask for: the
    // SAME gesture in the SAME window, once it is the one in front, goes into the `Visible` lane.
    // Without it "the background window queues at Prefetch" and "this build queues everything at
    // Prefetch" are the same passing case.
    second->raiseToForeground();
    QCoreApplication::processEvents();

    const auto beforeFront = desk.fixture.renderScheduler.stats();
    companion->view()->controller().setScroll(core::DevicePoint{0.0, 1800.0});
    driveLayout(second);
    desk.fixture.pumpTiles();
    const auto afterFront = desk.fixture.renderScheduler.stats();

    INFO("front visible " << beforeFront.enqueuedVisible << " -> " << afterFront.enqueuedVisible);
    CHECK(afterFront.enqueuedVisible > beforeFront.enqueuedVisible);
}
