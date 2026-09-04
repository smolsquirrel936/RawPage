// Stage 2 T13 -- ui/viewer/PageView and ui/viewer/TilePresenter, offscreen.
//
// Guide cases A-52 (*the layout gives `PageView` real pixels*), A-53 (*a page is actually
// painted*) and the WIDGET HALF of A-39 (*a `PageView` destroyed with tiles in flight does not
// crash*), whose `core/` half went green at T11 with this half explicitly owed.
//
// ---------------------------------------------------------------------------------------------
// EVERY CASE HERE IS WRITTEN AGAINST STAGE 1'S I-36 AND I-19, AND THE ORDER IS THE POINT.
//
// **Assert the geometry the layout gave the widget, THEN assert what depends on it.** Eleven ribbon
// cases were green for six days while the layout handed the tab bar zero pixels, because each asked
// the bar what it CONTAINED and none asked what the layout GAVE it. A viewer suite is the same trap
// with a bigger surface: `heldTileCount()` can be perfectly correct for a widget that is 0x0.
//
// So: `show()` under `offscreen`, `activate()` every layout level (convention 12 -- a bare
// `resize()` marks layouts dirty and returns), assert the size the widget ACTUALLY got, and only
// then look at tiles.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS SUITE CANNOT SEE, NAMED HERE RATHER THAN DISCOVERED (convention 19).
//
// It cannot judge appearance. `grab()` gives pixels and a case may assert what colour they are; it
// may not conclude that a page LOOKS right, and a case here claiming to have checked a look would
// be a defect in the case. Manual cases M-01 … M-04 are that half.
//
// It also does not cover the wake path: no `CompositionRoot` here means nobody installs the
// scheduler's wake handler, so `ShellFixture::pumpTiles()` calls `deliverReady()` itself. What that
// leaves untested is *a tile appearing without anyone asking*, which is the composition root's and
// is what a person sees in M-03.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/PageTree.h"
#include "core/render/PagePresentation.h"
#include "tests/ui/ShellFixture.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/TilePresenter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLayout>
#include <QScrollBar>

#include <set>

using namespace rawpage;

namespace {

struct Fixture : uitest::ShellFixture {
    // A file the FAKE backend accepts. Its bytes are irrelevant: `tests/ui` links no engine, and
    // the claim under test is the shell's and the viewer's rather than PDFium's (D1-4).
    QString writeFile(const QString& name)
    {
        const QString path = QDir(directory.path()).filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("not really a PDF; the fake backend decides");
        file.close();
        return QFileInfo(path).absoluteFilePath();
    }
};

// Convention 12, spelled out once: `show()` then `activate()` EVERY layout level involved. A bare
// `resize()` marks layouts dirty and returns, and offscreen there is no event loop to do the rest.
void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>()) {
        if (QLayout* layout = widget->layout()) layout->activate();
    }
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

bool imageContainsColour(const QImage& image, QRgb wanted)
{
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if ((image.pixel(x, y) & 0x00ffffff) == (wanted & 0x00ffffff)) return true;
    return false;
}

constexpr QRgb kFakeTile = qRgb(uitest::kFakeTileRed, uitest::kFakeTileGreen,
                                uitest::kFakeTileBlue);

} // namespace

// =============================================================================================
// A-52 -- the layout gives PageView real pixels. Assert this before anything that depends on it.
// =============================================================================================

TEST_CASE("the layout gives the page view real pixels", "[pageview]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(1200, 900);

    window.openFiles({fixture.writeFile(QStringLiteral("report.pdf"))});
    REQUIRE(window.documentCount() == 1);

    driveLayout(&window);

    ui::DocumentWindow* document = window.currentDocument();
    REQUIRE(document != nullptr);
    ui::PageView* view = document->view();
    REQUIRE(view != nullptr);

    // THE SIZE THE WIDGET ACTUALLY GOT, first. `resize(1200)` returning a 0-pixel viewer is a
    // FINDING, not a failed assertion -- I-36's lesson, and the reason this check comes before
    // every tile assertion in this file.
    CHECK(view->width() > 400);
    CHECK(view->height() > 300);

    // And within the window's central area: a viewer wider than its window would mean the layout
    // gave it a size nobody can see, which passes a `> 0` check and is still wrong.
    CHECK(view->width() <= window.width());
    CHECK(view->height() <= window.height());

    // The controller was told the same size, in DEVICE pixels. The two spaces are equal only at
    // 100 %, so this is asserted as a ratio rather than as an equality.
    const double dpr = view->devicePixelRatioF();
    CHECK(view->controller().viewportSize().width
          == view->viewport()->width() * dpr);
    CHECK(view->controller().viewportSize().height
          == view->viewport()->height() * dpr);
}

TEST_CASE("the content size follows the document and the zoom", "[pageview]")
{
    Fixture fixture;
    fixture.backend.pagesPerDocument = 4;

    ui::MainWindow window(fixture.services());
    window.resize(1200, 900);
    window.openFiles({fixture.writeFile(QStringLiteral("four.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentDocument()->view();
    REQUIRE(view != nullptr);
    REQUIRE(view->pageCount() == 4);

    const core::DeviceSize atOne = view->controller().contentSize();
    CHECK(atOne.height > 0.0);

    // Four A4 pages stacked plus gaps and margins is taller than four bare pages, and much taller
    // than one -- a content size that did not grow with the page count is a layout that laid out
    // one page and reported the whole document.
    CHECK(atOne.height > 4.0 * 841.89);

    view->controller().setZoom(core::render::Zoom{2.0});
    view->refresh();
    const core::DeviceSize atTwo = view->controller().contentSize();

    // Doubling the zoom roughly doubles the content; "roughly" because the margins and gaps are
    // window furniture and deliberately do NOT scale -- a 12-pixel gutter at 400 % would be a
    // hand's width apart.
    CHECK(atTwo.height > atOne.height * 1.9);
    CHECK(atTwo.height < atOne.height * 2.1);
}

TEST_CASE("the scrollbars are in logical pixels and reach the end of the document", "[pageview]")
{
    Fixture fixture;
    fixture.backend.pagesPerDocument = 6;

    ui::MainWindow window(fixture.services());
    window.resize(1000, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("six.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentDocument()->view();
    REQUIRE(view != nullptr);

    const double dpr = view->devicePixelRatioF();
    const double contentHeight = view->controller().contentSize().height;
    const double viewHeight = view->controller().viewportSize().height;

    // LOGICAL pixels, because a scrollbar is a widget. Setting the range in device pixels on a
    // 150 % monitor would make the last third of every document unreachable, which is a defect
    // nobody on a 100 % machine can reproduce.
    CHECK(view->verticalScrollBar()->maximum()
          == static_cast<int>(std::max(0.0, (contentHeight - viewHeight) / dpr)));

    view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
    QCoreApplication::processEvents();

    // The bottom of the last page is on screen: the scroll offset plus the viewport reaches the
    // content's end, within one logical pixel of integer rounding on the scrollbar.
    CHECK(view->controller().scroll().y + viewHeight >= contentHeight - dpr);
}

// =============================================================================================
// A-53 -- a page is ACTUALLY PAINTED. This is the case that distinguishes "tiles were requested"
// from "a page is on screen".
// =============================================================================================

TEST_CASE("a page is actually painted, not merely requested", "[pageview]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(1200, 900);
    window.openFiles({fixture.writeFile(QStringLiteral("drawn.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentDocument()->view();
    REQUIRE(view != nullptr);
    REQUIRE(view->width() > 400);   // A-52 first, always

    // Tiles WERE requested. Asserted separately from the paint below, because the two are what
    // this case exists to tell apart.
    CHECK(view->tiles().pendingTicketCount() + view->tiles().heldTileCount() > 0);

    REQUIRE(fixture.pumpTiles() > 0);
    CHECK(view->tiles().heldTileCount() > 0);

    QCoreApplication::processEvents();
    const QImage painted = view->viewport()->grab().toImage();
    REQUIRE_FALSE(painted.isNull());

    // THE PIXELS. The fake rasterizer fills a flat blue that nothing else in the viewer draws --
    // not the white sheet, not the grey border, not the palette's window colour -- so this can only
    // be true if the request was built, queued, run, delivered, converted to a `QImage` and painted
    // at the right rectangle. A `grab()` of a widget that merely asked for tiles is uniform.
    CHECK(imageContainsColour(painted, kFakeTile));

    // NOT UNIFORM: the page's sheet and border are there too, so the surface is a page on a desk
    // rather than a wall of one colour -- which a fill-everything bug would also satisfy above.
    std::set<QRgb> distinct;
    for (int y = 0; y < painted.height(); y += 7)
        for (int x = 0; x < painted.width(); x += 7) distinct.insert(painted.pixel(x, y));
    CHECK(distinct.size() >= 2);

    // Every tile the presenter drew was drawn, and nothing was left waiting.
    CHECK(view->tiles().lastPaint().pagesDrawn > 0);
    CHECK(view->tiles().lastPaint().tilesDrawn > 0);
    CHECK(view->tiles().lastPaint().placeholders == 0);
}

TEST_CASE("a view with no document paints no page", "[pageview]")
{
    // THE NEGATIVE CONTROL for A-53. Without it, a `PageView` that filled its whole viewport with
    // the tile colour for any reason at all would satisfy the case above.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(800, 600);
    driveLayout(&window);

    ui::DocumentWindow empty(fixture.services(), QString(), core::DocumentHandle{});
    empty.resize(800, 600);
    driveLayout(&empty);

    REQUIRE(empty.view() != nullptr);
    CHECK(empty.view()->tiles().heldTileCount() == 0);
    CHECK(empty.view()->tiles().pendingTicketCount() == 0);

    const QImage painted = empty.view()->viewport()->grab().toImage();
    REQUIRE_FALSE(painted.isNull());
    CHECK_FALSE(imageContainsColour(painted, kFakeTile));
}

// =============================================================================================
// Tile requests follow the viewport, and cancellation is driven by the tile set.
// =============================================================================================

TEST_CASE("scrolling away cancels the tiles that left the viewport", "[pageview]")
{
    Fixture fixture;
    fixture.backend.pagesPerDocument = 12;

    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("long.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentDocument()->view();
    REQUIRE(view != nullptr);
    REQUIRE(view->height() > 200);

    fixture.pumpTiles();
    const std::set<int> atTop = view->tiles().heldPages();
    REQUIRE_FALSE(atTop.empty());
    REQUIRE(atTop.count(0) == 1);

    // Jump to the far end of a twelve-page document. Nothing held at the top is within a prefetch
    // ring of the bottom, so every one of those tiles must be dropped -- plan §5.3's "cancellation
    // is the feature", exercised through the widget rather than through the scheduler's own suite.
    view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
    QCoreApplication::processEvents();
    fixture.pumpTiles();

    const std::set<int> atBottom = view->tiles().heldPages();
    REQUIRE_FALSE(atBottom.empty());

    // PAGES, NOT A TILE COUNT, and the case said something weaker until its negative control
    // refused to fail. A presenter that never evicted holds roughly twice a viewport's worth after
    // this jump -- which satisfies any count bound loose enough not to be brittle. What is actually
    // claimed is that the FIRST page's tiles are gone, and only a set of pages can state it.
    CHECK(atBottom.count(0) == 0);
    CHECK(*atBottom.begin() > 0);

    // And the page the user is looking at moved with the scroll.
    CHECK(view->controller().currentPage() > 0);
}

TEST_CASE("the render request carries the RASTER scale and a real revision", "[pageview]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("keys.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentDocument()->view();
    REQUIRE(view != nullptr);

    fixture.pumpTiles();
    REQUIRE(view->tiles().heldTileCount() > 0);

    // Every tile the cache holds was keyed on a VALID request (D2-12: a defaulted `Revision` is
    // what makes a stale tile after a Stage 3 mutation the default behaviour), and the scheduler
    // refuses an invalid one outright -- so a held tile is itself the proof the key was sound.
    CHECK(fixture.renderCache.entryCount() > 0);
    CHECK(fixture.renderScheduler.stats().enqueued > 0);
    CHECK(fixture.renderScheduler.stats().completed > 0);
    CHECK(fixture.renderScheduler.stats().failed == 0);

    // The rasterizer was asked for the RASTER resolution, not the presented one. At an uncapped
    // zoom they are equal, and `upscale` is 1.0 -- which is what is asserted, because a cap that
    // had bitten here would mean `DpiPolicy` capped an ordinary 100 % view.
    CHECK(view->layoutEngine().resolution().upscale == 1.0);
    CHECK(view->layoutEngine().resolution().rasterScale()
          == view->layoutEngine().resolution().presentedScale());
}

// =============================================================================================
// A-39's widget half -- a PageView destroyed with tiles in flight.
// =============================================================================================

TEST_CASE("a page view destroyed with tiles in flight does not crash", "[pageview]")
{
    Fixture fixture;
    fixture.backend.pagesPerDocument = 20;

    // PAUSED, so the jobs are queued and never start. That is what makes "in flight" a state this
    // case can hold rather than a race it hopes to hit.
    fixture.renderScheduler.pause();

    {
        ui::MainWindow window(fixture.services());
        window.resize(1400, 1000);
        window.openFiles({fixture.writeFile(QStringLiteral("inflight.pdf"))});
        driveLayout(&window);

        ui::PageView* view = window.currentDocument()->view();
        REQUIRE(view != nullptr);
        REQUIRE(view->tiles().pendingTicketCount() > 0);
        REQUIRE(fixture.renderScheduler.pendingCount() > 0);
    }

    // The window went, which destroyed the tab, which destroyed the view, which destroyed the
    // presenter -- and the presenter's destructor unsubscribed BY TOKEN and cancelled every ticket.
    // A ready callback surviving into a destroyed presenter is a use-after-free rather than a
    // repaint of a dead widget (convention 10's threaded form).
    CHECK(fixture.renderScheduler.pendingCount() == 0);

    fixture.renderScheduler.resume();
    fixture.pumpTiles();

    // Nothing was rasterized, because everything was cancelled before it started -- A-36's number,
    // reached through the widget.
    CHECK(fixture.renderScheduler.stats().cancelledBeforeStart > 0);
    CHECK(fixture.renderScheduler.stats().started == 0);
}

TEST_CASE("closing a tab closes its handle and drops its tiles", "[pageview]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(1000, 800);
    window.openFiles({fixture.writeFile(QStringLiteral("closeme.pdf"))});
    driveLayout(&window);
    fixture.pumpTiles();

    REQUIRE(fixture.renderCache.entryCount() > 0);
    REQUIRE(fixture.backend.closes == 0);

    window.closeDocument(0);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // Three things, in the order `DocumentWindow`'s destructor does them: the jobs are gone, the
    // tiles are gone, and the engine handle is closed. A viewer that only cancelled would keep a
    // closed 900-page document's megabytes for the life of the process.
    CHECK(window.documentCount() == 0);
    CHECK(fixture.backend.closes == 1);
    CHECK(fixture.renderCache.entryCount() == 0);
}

// =============================================================================================
// The monitor the view thinks it is on -- the T12 wire.
// =============================================================================================

TEST_CASE("a view with no display topology falls back and says so", "[pageview]")
{
    // `platform::Platform` in the fixture has every interface null, which is the honest state of a
    // test bundle -- so this asserts the FALLBACK path, and asserts that it is reported rather than
    // silently substituted. On the P-1 rig the shipped product takes the topology path instead,
    // and `platform.displaytopology` is where that is measured.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(800, 600);
    window.openFiles({fixture.writeFile(QStringLiteral("metrics.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentDocument()->view();
    REQUIRE(view != nullptr);

    const ui::ScreenMetrics metrics = view->screenMetrics();
    CHECK(metrics.source != ui::ScreenMetrics::Source::Topology);
    CHECK(metrics.physical.dpi >= 48.0);
    CHECK(metrics.scale.factor > 0.0);
}
