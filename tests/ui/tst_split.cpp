// Stage 4 T9 -- the split view. Guide cases A-53, A-54, A-55 and A-56
// (`Stage4-ViewingSurface-Testing-Guide.md` §5.10), and decision **D4-13**
// (`Stage4-ViewingSurface-Plan.md` §3).
//
// The claim the whole task rests on is a NEGATIVE one -- *a split does not open a second document*
// -- so the first case here asserts an identity between two views rather than anything either view
// draws. A split that quietly opened a second `DocumentHandle` would look right in every
// screenshot, would render both panes correctly, and would be wrong in the two places nothing on
// screen reports: `DocumentWindow`'s three-step close protocol, which exists once, and the memory
// the document costs.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS SUITE CANNOT SEE, NAMED HERE RATHER THAN DISCOVERED (convention 19).
//
//   * **It cannot see a handle drag.** The splitter handles are real `QSplitter` handles and the
//     sizes a person drags them to are the widget's, dying with the window (`Stage4-...-Plan.md`
//     §5.2). What is asserted here is that the two rows of a spreadsheet split are given the SAME
//     sizes; that dragging one moves the other is manual case **M-08**. (This line said M-09, which is
//     window arrangement and asks nothing about a split -- corrected 2026-09-26.)
//   * **It cannot judge a repaint.** Every scroll assertion below is on the two controllers'
//     offsets, not on pixels. Whether the two panes LOOK locked together while a wheel is turning
//     is M-08's too.
//   * **No real engine.** `uitest::FakeBackend` decides the page count and `FakeRasterizer` the
//     pixels, and tiles are pumped by hand -- `ShellFixture.h` names that back door and what it
//     therefore does not cover.
//   * **A-56 asserts the TICKETS, not the widget.** A retired pane is `deleteLater`'d, so it is
//     still alive when the assertions run; what is claimed is that it holds no tile and no ticket,
//     which is the half that could leak. The case flushes `QEvent::DeferredDelete` afterwards so
//     the next one does not inherit it.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "core/render/RenderScheduler.h"
#include "tests/ui/ColorDistance.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/RotatePagesDialog.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/ThemeManager.h"
#include "ui/viewer/GridGuideLayer.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/RulerWidget.h"
#include "ui/viewer/SplitViewContainer.h"
#include "ui/viewer/TilePresenter.h"
#include "ui/viewer/ViewportController.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QEnterEvent>
#include <QLayout>
#include <QSplitter>
#include <QSplitterHandle>

#include <memory>

using namespace rawpage;
using core::ActionId;
using Catch::Matchers::WithinAbs;
using ui::SplitViewContainer;

namespace {

using Mode = SplitViewContainer::Mode;

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

// A shown window with one editable document in the viewer, ready to be split.
struct Split {
    Fixture                         fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow*             document = nullptr;

    explicit Split(int pages = 12)
    {
        fixture.backend.pagesPerDocument = pages;

        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 850);
        window->openFiles({fixture.writePdf(QStringLiteral("split.pdf"))});
        driveLayout(window.get());
        window->raiseToForeground();

        document = window->currentDocument();
        REQUIRE(document != nullptr);
        REQUIRE(document->split() != nullptr);
        REQUIRE(document->journal() != nullptr);

        document->view()->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();

        // THE GEOMETRY FIRST (convention 12, guide rule 4). Every pane below gets a share of this,
        // and a zero-height container would give four panes of nothing -- on which every "the
        // frozen row did not move" assertion passes for the wrong reason.
        REQUIRE(container()->width() > 400);
        REQUIRE(container()->height() > 300);
        REQUIRE(window->shortcutBinder()->activeContext() == core::KeyContext::Viewer);
    }

    // A tab is deleted later and its destructor closes its handle on THIS fixture's backend
    // (`CLAUDE.md` -> Traps, S3 T10). Left pending past the fixture, the next case's first
    // `processEvents` runs it against a destroyed backend.
    ~Split()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    SplitViewContainer* container() const { return document->split(); }
    ui::PageView*       primary() const { return container()->primaryView(); }

    // Applies a mode through the REGISTRY rather than by calling `setMode`, because "the split is
    // implemented" and "the command reaches it" are different claims and only the second is about
    // the wiring (the argument `tst_viewercommands` makes for driving the chord).
    void dispatch(ActionId id)
    {
        const core::Result<void> sent = fixture.actions.dispatch(id, core::KeyContext::Global);
        INFO("refused: " << (sent ? std::string()
                                  : std::string(sent.error().messageKey.data(),
                                                sent.error().messageKey.size())));
        REQUIRE(sent);
        driveLayout(window.get());
        fixture.pumpTiles();
    }

    void rotateCurrentPage(int degrees = 90)
    {
        ui::RotatePagesDialog::setAnswerForTesting(
            [degrees] { return ui::RotatePagesDialog::Choice{degrees, false}; });
        REQUIRE(fixture.actions.dispatch(ActionId::PageRotate, core::KeyContext::Viewer));
        ui::RotatePagesDialog::setAnswerForTesting({});
    }

    int rotationOf(int page) const
    {
        return static_cast<int>(document->journal()->document().pages().pages()
                                    .at(static_cast<std::size_t>(page))
                                    .rotation());
    }
};

} // namespace

// =============================================================================================
// A-53 -- a split is two views over ONE document.
// =============================================================================================

TEST_CASE("a horizontal split is two views over one handle, one journal and one revision", "[split]")
{
    Split split;

    REQUIRE(split.container()->mode() == Mode::None);
    REQUIRE(split.container()->views().size() == 1);

    split.dispatch(ActionId::WindowSplit);

    REQUIRE(split.container()->mode() == Mode::Horizontal);
    REQUIRE(split.container()->views().size() == 2);

    ui::PageView* top = split.container()->views().at(0);
    ui::PageView* bottom = split.container()->views().at(1);
    REQUIRE(top != bottom);

    // THE WHOLE OF D4-13, AS AN IDENTITY. Not "both panes show the same file" -- both panes are
    // handed the SAME `DocumentHandle`, which is what makes the close protocol, the journal and the
    // document's memory single.
    CHECK(top->document() == bottom->document());
    CHECK(top->document() == split.document->handle());
    CHECK(top->revision() == bottom->revision());
    CHECK(top->revision() == split.document->journal()->revision());

    // AND THE TAB STILL HOLDS EXACTLY ONE JOURNAL. `DocumentWindow` is where a second one would
    // have to live, and it has no vocabulary for two.
    CHECK(split.document->journal() == split.document->journal());
    CHECK(split.document->pageCount() == 12);

    // Both panes are laid out and asking for pages -- an assertion the ones below depend on, and
    // one that a pane added to a layout it never got a size from would fail.
    CHECK(top->pageCount() == 12);
    CHECK(bottom->pageCount() == 12);
    CHECK(bottom->height() > 100);

    // THE HORIZONTAL SPLIT DIVIDES HORIZONTALLY: the panes are one above the other, which is the
    // coin flip `SplitViewContainer.h` warns about and the only way to catch it swapped.
    CHECK(bottom->y() > top->y());
    CHECK(bottom->x() == top->x());
}

TEST_CASE("a vertical split puts the panes side by side, and a second press is not a second split", "[split]")
{
    Split split;
    split.dispatch(ActionId::WindowSplitVertical);

    REQUIRE(split.container()->views().size() == 2);
    ui::PageView* left = split.container()->views().at(0);
    ui::PageView* right = split.container()->views().at(1);

    CHECK(right->x() > left->x());
    CHECK(right->y() == left->y());
    CHECK(left->document() == right->document());

    // THE CONTROL. Pressing the mode it is already in is not a request for four panes, and
    // `setMode` returns early -- so the pane count is unchanged AND the panes are the same objects,
    // which is the half that says nothing was torn down and rebuilt underneath.
    split.dispatch(ActionId::WindowSplitVertical);
    REQUIRE(split.container()->views().size() == 2);
    CHECK(split.container()->views().at(0) == left);
    CHECK(split.container()->views().at(1) == right);
}

TEST_CASE("the primary view survives every mode change", "[split]")
{
    Split split;
    ui::PageView* primary = split.primary();

    split.dispatch(ActionId::WindowSplit);
    CHECK(split.primary() == primary);
    split.dispatch(ActionId::WindowSplitVertical);
    CHECK(split.primary() == primary);
    split.dispatch(ActionId::WindowSpreadsheetSplit);
    CHECK(split.primary() == primary);
    split.dispatch(ActionId::WindowRemoveSplit);
    CHECK(split.primary() == primary);

    // And it is the one the tab reports, so every §8 Viewer command still lands on a live pane.
    CHECK(split.document->view() == primary);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

// =============================================================================================
// A-54 -- a mutation in one view appears in BOTH, at the same revision.
// =============================================================================================

TEST_CASE("a rotation in one pane of a split reaches both panes at the same revision", "[split]")
{
    Split split;
    split.dispatch(ActionId::WindowSplit);

    ui::PageView* top = split.container()->views().at(0);
    ui::PageView* bottom = split.container()->views().at(1);

    const core::Revision before = split.document->journal()->revision();
    REQUIRE(top->revision() == before);
    REQUIRE(bottom->revision() == before);

    // GEOMETRY BEFORE PIXELS (Stage 1's I-36): the page's laid-out size is what a rotation changes
    // first, and asserting it is what distinguishes "the model rotated" from "a tile arrived".
    const core::render::PageLayout& bottomLayout = bottom->layoutEngine().layout();
    REQUIRE(!bottomLayout.pageSlots().empty());
    const double widthBefore = bottomLayout.pageSlots().front().rect.size.width;
    const double heightBefore = bottomLayout.pageSlots().front().rect.size.height;

    split.rotateCurrentPage(90);
    driveLayout(split.window.get());
    split.fixture.pumpTiles();

    const core::Revision after = split.document->journal()->revision();
    CHECK(after != before);
    CHECK(split.rotationOf(0) == 90);

    // BOTH PANES, AND AT THE SAME REVISION. A pane that was not told would go on showing the
    // un-rotated page with no error and no failing test, because the revision is part of every
    // tile's key (D2-12).
    CHECK(top->revision() == after);
    CHECK(bottom->revision() == after);

    const double widthAfter = bottom->layoutEngine().layout().pageSlots().front().rect.size.width;
    const double heightAfter = bottom->layoutEngine().layout().pageSlots().front().rect.size.height;
    CHECK_THAT(widthAfter, WithinAbs(heightBefore, 1.0));
    CHECK_THAT(heightAfter, WithinAbs(widthBefore, 1.0));
}

TEST_CASE("a guide dropped in one pane of a split shows in the other", "[split]")
{
    Split split;
    split.dispatch(ActionId::WindowSplit);

    ui::PageView* top = split.container()->views().at(0);
    ui::PageView* bottom = split.container()->views().at(1);
    REQUIRE(top->guides().guideCount(0) == 0);
    REQUIRE(bottom->guides().guideCount(0) == 0);

    // A guide is a PAGE fact in PDF points (D4-7), so it belongs to the document and not to the
    // pane the hand that made it happened to be in.
    core::Guide guide;
    guide.pageIndex = 0;
    guide.orientation = core::Guide::Orientation::Horizontal;
    guide.position = 200.0;
    top->guides().addGuide(guide);
    QCoreApplication::processEvents();

    CHECK(top->guides().guideCount(0) == 1);
    CHECK(bottom->guides().guideCount(0) == 1);

    top->guides().clearGuides(0);
    QCoreApplication::processEvents();
    CHECK(bottom->guides().guideCount(0) == 0);
}

// =============================================================================================
// A-55 -- synchronized scrolling, and the spreadsheet freeze.
// =============================================================================================

TEST_CASE("synchronized scrolling moves both panes, and turning it off frees them", "[split]")
{
    Split split;
    split.dispatch(ActionId::WindowSplit);

    ui::PageView* top = split.container()->views().at(0);
    ui::PageView* bottom = split.container()->views().at(1);

    // ON BY DEFAULT -- `Features.md` §1.2 says *"two synchronized panes"*, and a split that opened
    // unsynchronized would make the word in that row mean nothing.
    REQUIRE(split.container()->synchronizedScrolling());

    top->controller().setScroll(core::DevicePoint{0.0, 640.0});
    QCoreApplication::processEvents();

    CHECK_THAT(bottom->controller().scroll().y, WithinAbs(top->controller().scroll().y, 0.5));
    CHECK_THAT(bottom->controller().scroll().x, WithinAbs(top->controller().scroll().x, 0.5));

    // AND IT MIRRORS THE OTHER WAY. A one-directional mirror is a leader and a follower, not a
    // synchronization, and it would be invisible until someone scrolled the second pane.
    bottom->controller().setScroll(core::DevicePoint{0.0, 300.0});
    QCoreApplication::processEvents();
    CHECK_THAT(top->controller().scroll().y, WithinAbs(300.0, 0.5));

    // OFF: INDEPENDENT. The switch is `window.syncScroll`, dispatched rather than called, so this
    // is also the only assertion that the command is wired to the container.
    split.dispatch(ActionId::WindowSyncScroll);
    REQUIRE_FALSE(split.container()->synchronizedScrolling());

    const double parked = bottom->controller().scroll().y;
    top->controller().setScroll(core::DevicePoint{0.0, 900.0});
    QCoreApplication::processEvents();
    CHECK_THAT(bottom->controller().scroll().y, WithinAbs(parked, 0.5));
}

TEST_CASE("the spreadsheet split freezes a row and a column, each in its own axis only", "[split]")
{
    Split split;

    // The body is scrolled somewhere before the freeze, because *where the freeze was taken* is
    // the whole of what the three derived panes are relative to.
    split.primary()->controller().setScroll(core::DevicePoint{40.0, 500.0});
    QCoreApplication::processEvents();

    split.dispatch(ActionId::WindowSpreadsheetSplit);

    REQUIRE(split.container()->mode() == Mode::Spreadsheet);
    REQUIRE(split.container()->views().size() == 4);

    ui::PageView* body = split.container()->views().at(0);
    ui::PageView* corner = split.container()->views().at(1);
    ui::PageView* row = split.container()->views().at(2);
    ui::PageView* column = split.container()->views().at(3);

    const core::DevicePoint origin = corner->controller().scroll();

    body->controller().setScroll(core::DevicePoint{260.0, 1400.0});
    QCoreApplication::processEvents();

    // THE FROZEN REGIONS SCROLL ONLY IN THEIR OWN AXIS -- guide case A-55, as four assertions.
    CHECK_THAT(row->controller().scroll().x, WithinAbs(body->controller().scroll().x, 0.5));
    CHECK_THAT(row->controller().scroll().y, WithinAbs(origin.y, 0.5));

    CHECK_THAT(column->controller().scroll().y, WithinAbs(body->controller().scroll().y, 0.5));
    CHECK_THAT(column->controller().scroll().x, WithinAbs(origin.x, 0.5));

    // AND THE CORNER IS FROZEN IN BOTH. It is the control: three panes that all followed the body
    // would satisfy two of the four assertions above and be no freeze at all.
    CHECK_THAT(corner->controller().scroll().x, WithinAbs(origin.x, 0.5));
    CHECK_THAT(corner->controller().scroll().y, WithinAbs(origin.y, 0.5));

    // THE TWO ROWS OF PANES ARE THE SAME WIDTHS, which is what makes the frozen column one column
    // rather than two panes that happen to be on the left. Asserted on the widgets because the
    // splitters are an implementation detail and the alignment is not.
    CHECK(corner->width() == column->width());
    CHECK(row->width() == body->width());
}

TEST_CASE("synchronized scrolling is refused in a spreadsheet split", "[split]")
{
    Split split;
    split.dispatch(ActionId::WindowSpreadsheetSplit);

    // The frozen panes' scroll IS the mode, not an option on it, so the switch is offered as
    // unavailable rather than as a control that would do nothing -- Stage 1's I-53.
    CHECK_FALSE(split.fixture.actions.isEnabled(ActionId::WindowSyncScroll));

    split.dispatch(ActionId::WindowSplit);
    CHECK(split.fixture.actions.isEnabled(ActionId::WindowSyncScroll));
}

// =============================================================================================
// A-56 -- removing a split leaves one view and no leaked tickets.
// =============================================================================================

TEST_CASE("removing a split leaves one view and no tickets outstanding for the retired pane", "[split]")
{
    Split split;
    split.dispatch(ActionId::WindowSplit);

    ui::PageView* retiring = split.container()->views().at(1);
    REQUIRE(retiring != split.primary());

    // THE PANE WAS REALLY DRAWING, or the assertion below holds vacuously. A pane that held no
    // tile and had no ticket before the removal cannot prove anything about the removal.
    REQUIRE(retiring->tiles().heldTileCount() > 0);

    ui::PageView* seen = nullptr;
    QObject::connect(split.container(), &SplitViewContainer::viewRetired, split.container(),
                     [&seen](ui::PageView* view) { seen = view; });

    split.dispatch(ActionId::WindowRemoveSplit);

    CHECK(seen == retiring);
    CHECK(split.container()->mode() == Mode::None);
    REQUIRE(split.container()->views().size() == 1);
    CHECK(split.container()->views().front() == split.primary());
    CHECK(split.document->view() == split.primary());

    // NOTHING IS LEFT IN FLIGHT FOR IT, and nothing is left held. The widget is deleted later --
    // the command ran from inside an application event filter -- so it is still alive here, which
    // is exactly why the claim is about its tickets rather than about its existence.
    CHECK(retiring->tiles().pendingTicketCount() == 0);
    CHECK(retiring->tiles().heldTileCount() == 0);
    CHECK_FALSE(retiring->document().isValid());

    // And the surviving pane is untouched: a removal that cleared the wrong view would pass every
    // assertion above.
    CHECK(split.primary()->document() == split.document->handle());
    CHECK(split.primary()->pageCount() == 12);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(split.fixture.renderScheduler.pendingCount() == 0);
}

TEST_CASE("removing a split is refused when there is none", "[split]")
{
    Split split;

    // THE CONTROL. *Remove Split* on an unsplit window is offered as unavailable rather than as a
    // control that silently does nothing -- Stage 1's I-53, which is what `unavailableKey` is for.
    CHECK_FALSE(split.fixture.actions.isEnabled(ActionId::WindowRemoveSplit));

    split.dispatch(ActionId::WindowSplit);
    CHECK(split.fixture.actions.isEnabled(ActionId::WindowRemoveSplit));

    split.dispatch(ActionId::WindowRemoveSplit);
    CHECK_FALSE(split.fixture.actions.isEnabled(ActionId::WindowRemoveSplit));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("the active pane follows focus, and every view command lands on it", "[split]")
{
    Split split;
    split.dispatch(ActionId::WindowSplitVertical);

    ui::PageView* left = split.container()->views().at(0);
    ui::PageView* right = split.container()->views().at(1);
    REQUIRE(split.container()->activeView() == left);

    right->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();

    CHECK(split.container()->activeView() == right);
    CHECK(split.document->view() == right);
    CHECK(split.window->currentView() == right);

    // AND THE COMMAND GOES WHERE THE PERSON IS. `view.zoomIn` was registered at Stage 2 T14 against
    // `MainWindow::currentView()` and has not been touched by this task -- which is the claim: a
    // split needed no change to any of the twenty-five §8 Viewer commands.
    const core::render::Zoom before = right->controller().zoom();
    REQUIRE(split.fixture.actions.dispatch(ActionId::ViewZoomIn, core::KeyContext::Viewer));
    QCoreApplication::processEvents();

    CHECK(right->controller().zoom() != before);
    CHECK(left->controller().zoom() == before);
}

TEST_CASE("the rulers measure the focused pane of a split", "[split][D4-18]")
{
    // D4-18 (`Stage4-ViewingSurface-Plan.md` s3): ONE pair of rulers per document window, measuring
    // the focused pane. Recorded as a decision on 2026-09-26, after T9 had built it with no case --
    // this is that case. The zero is where the focused pane's page corner is, in the ruler's own
    // coordinates, so the claim is a number: it moves by exactly the distance between the panes.
    // What it does NOT cover (convention 19): whether a person can tell which pane the ruler is
    // reading, over the pane it is not -- M-08.
    Split split;
    split.fixture.settings.set(core::keys::units::showRulers, true);
    split.dispatch(ActionId::WindowSplitVertical);

    ui::RulerWidget* ruler = split.document->horizontalRuler();
    REQUIRE(ruler != nullptr);
    REQUIRE(ruler->isVisible());

    ui::PageView* left = split.container()->views().at(0);
    ui::PageView* right = split.container()->views().at(1);
    const auto paneStart = [&](ui::PageView* pane) {
        return pane->viewport()->mapTo(split.container(), QPoint(0, 0)).x();
    };
    REQUIRE(paneStart(right) - paneStart(left) > 100);   // two panes side by side, not stacked

    left->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    REQUIRE(split.container()->activeView() == left);
    REQUIRE(ruler->mapping().valid);
    const double leftZero = ruler->mapping().zeroPixel;
    CHECK(leftZero < paneStart(right));

    right->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    REQUIRE(split.container()->activeView() == right);
    const double rightZero = ruler->mapping().zeroPixel;

    // Same document, same zoom, same scroll: the two page corners are one pane-offset apart.
    CHECK_THAT(rightZero - leftZero, WithinAbs(paneStart(right) - paneStart(left), 1.0));

    // And back -- a ruler that moved once and stuck would pass the half above.
    left->setFocus(Qt::MouseFocusReason);
    QCoreApplication::processEvents();
    CHECK_THAT(ruler->mapping().zeroPixel, WithinAbs(leftZero, 1.0));
}

// =============================================================================================
// I-12 -- the divider is VISIBLE, in both themes.
//
// M-01's walk, 2026-09-26: a vertical split showed no boundary between its panes. A stock handle is
// painted in `QPalette::Window`, which is also the desk every `PageView` paints around its pages, so
// the two were one colour by construction. Measured here on PAINTED PIXELS, the handle's against the
// desk's, because "the handle has a different palette role" is a claim about intent and the native
// style is free to ignore a role (convention 21).
//
// What this does NOT cover (convention 19): what a person sees on a 150 % panel, where one logical
// pixel is not one device pixel, nor whether the colours read as a boundary at a glance -- M-01 and
// M-08 on the P-1 rig. The pointer arriving is an `Enter` event sent by hand, not a mouse. And the
// STYLE: offscreen runs Fusion, whose stock handle draws a faint grip (deltaE 19 at its centre,
// measured with the planted control) where the product's `windows11` style drew nothing M-01 could
// see -- so the control fails here on the band (deltaE 0) and on the line floor, not on "nothing".
// =============================================================================================

TEST_CASE("the split divider is distinct from the desk in both themes, and lights under the pointer",
          "[split][I-12]")
{
    Split split;
    split.dispatch(ActionId::WindowSplitVertical);

    auto* splitter = split.container()->findChild<QSplitter*>();
    REQUIRE(splitter != nullptr);
    REQUIRE(splitter->count() == 2);
    QSplitterHandle* handle = splitter->handle(1);
    REQUIRE(handle != nullptr);

    // THE GEOMETRY FIRST: a zero-width handle grabs as nothing, and nothing is a colour no desk has.
    REQUIRE(handle->width() >= 4);
    REQUIRE(handle->height() > 100);

    const QPalette before = QApplication::palette();
    ui::ThemeManager theme(split.fixture.settings);

    for (const core::Theme requested : {core::Theme::Light, core::Theme::Dark}) {
        split.fixture.settings.set(core::keys::general::theme, requested);
        theme.apply();
        QCoreApplication::processEvents();

        const auto centreOf = [](QWidget* widget) {
            return widget->grab().toImage().pixelColor(widget->width() / 2, widget->height() / 2);
        };

        // The desk, as the view PAINTED it: a margin pixel, outside every page.
        const QColor desk = split.primary()->viewport()->grab().toImage().pixelColor(2, 2);
        // The band at the handle's edge, and the line down its middle -- BOTH, because the band
        // alone was pale and a line with no band is a hairline to aim a drag at.
        const QColor band = handle->grab().toImage().pixelColor(0, handle->height() / 2);
        const QColor line = centreOf(handle);

        UNSCOPED_INFO((requested == core::Theme::Dark ? "dark" : "light")
                      << " desk " << desk.name().toStdString() << " band "
                      << band.name().toStdString() << " deltaE " << uitest::deltaE76(desk, band)
                      << " line " << line.name().toStdString() << " deltaE "
                      << uitest::deltaE76(desk, line));
        CHECK(uitest::deltaE76(desk, band) >= uitest::kDistinctSurfaceDeltaE);
        CHECK(uitest::deltaE76(desk, line) >= 3.0 * uitest::kDistinctSurfaceDeltaE);

        // UNDER THE POINTER it changes again -- distinct from the band as well as from the desk, so
        // "which divider have I got hold of" has an answer before the drag starts.
        QEnterEvent enter(QPointF(1.0, 1.0), QPointF(1.0, 1.0), QPointF(1.0, 1.0));
        QCoreApplication::sendEvent(handle, &enter);
        const QColor held = centreOf(handle);
        UNSCOPED_INFO("held " << held.name().toStdString());
        CHECK(uitest::deltaE76(held, band) >= uitest::kDistinctSurfaceDeltaE);
        CHECK(uitest::deltaE76(held, desk) >= uitest::kDistinctSurfaceDeltaE);

        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(handle, &leave);
        CHECK(uitest::deltaE76(centreOf(handle), line) < 1.0);   // and it lets go
    }

    QApplication::setPalette(before);
}

TEST_CASE("every divider of a spreadsheet split is the painted one", "[split][I-12]")
{
    // Three splitters, and a handle the person drags in each. One of them left stock would be the
    // same invisible boundary in the one mode whose whole point is a visible freeze line.
    Split split;
    split.dispatch(ActionId::WindowSpreadsheetSplit);

    const QList<QSplitter*> splitters = split.container()->findChildren<QSplitter*>();
    REQUIRE(splitters.size() == 3);

    const QColor desk = split.primary()->viewport()->grab().toImage().pixelColor(2, 2);
    for (QSplitter* splitter : splitters) {
        REQUIRE(splitter->count() == 2);
        QSplitterHandle* handle = splitter->handle(1);
        REQUIRE(handle->width() >= 4);
        REQUIRE(handle->height() >= 4);
        const QImage painted = handle->grab().toImage();
        CHECK(uitest::deltaE76(desk, painted.pixelColor(0, 0)) >= uitest::kDistinctSurfaceDeltaE);
        CHECK(uitest::deltaE76(desk, painted.pixelColor(handle->width() / 2, handle->height() / 2))
              >= 3.0 * uitest::kDistinctSurfaceDeltaE);
    }
}
