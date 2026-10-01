// Stage 5 T10 -- the Snapshot tool, decision D5-11 (`Stage5-TextSearch-Plan.md` §3). Guide cases A-91 and
// A-114, and A-90's widget half (`Stage5-TextSearch-Testing-Guide.md` §5.12).
//
// WHAT IT DOES NOT COVER (convention 19), and where that is:
//
//   * **Whether the image is the document's INK.** The fake rasterizer paints one flat blue and draws no
//     glyph, so "the image equals a fresh render of the region" and "a bitmap scaled up from the view would
//     fail" need the engine -- `tests/composition/tst_snapshotink.cpp`, A-90's and A-114's pixel halves, for
//     `tst_selectionink`'s reason. What IS asserted here with a flat colour: that the overlays and the
//     inversion are not in the copy (the inverted blue is a different colour), and every SIZE.
//   * **The real clipboard.** Offscreen, `QClipboard` is Qt's in-process one: what reaches the system
//     clipboard as CF_DIB, and what an image editor makes of it, is manual case M-03's.
//   * **A real pointer.** The drags are synthetic mouse events on the viewport, as in `tst_tools`.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/render/RenderScheduler.h"
#include "core/settings/Settings.h"
#include "tests/ui/ShellFixture.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/GridGuideLayer.h"
#include "ui/viewer/LoupeRaster.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/TilePresenter.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/ViewportController.h"
#include "ui/viewer/tools/MarqueeZoomTool.h"
#include "ui/viewer/tools/SnapshotTool.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

using namespace rawpage;
using core::ActionId;
using ui::SnapshotTool;

namespace {

struct Fixture : uitest::ShellFixture {
    QString writeFile(const QString& name)
    {
        const QString path = QDir(directory.path()).filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("the fake backend decides, not these bytes");
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

// A window with a document in front, focused in the Viewer, as `tst_tools`' `Viewer` -- with the first page
// optionally A3, for A-114's bound.
struct Viewer {
    Fixture                         fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::PageView*                   view = nullptr;

    explicit Viewer(bool a3 = false)
    {
        fixture.backend.pagesPerDocument = 4;
        if (a3) fixture.backend.pageBoxes[0] = core::PdfRect{core::PdfPoint{0.0, 0.0}, core::PdfSize{841.89, 1190.55}};
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 850);
        window->openFiles({fixture.writeFile(QStringLiteral("snapshot.pdf"))});
        driveLayout(window.get());

        view = window->currentView();
        REQUIRE(view != nullptr);
        window->raiseToForeground();
        view->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        REQUIRE(view->width() > 400);
        REQUIRE(view->height() > 300);
        REQUIRE(window->shortcutBinder()->activeContext() == core::KeyContext::Viewer);
        fixture.pumpTiles();
    }

    bool press(Qt::Key key, const QString& text = QString()) const
    {
        QWidget* focus = window->focusWidget();
        REQUIRE(focus != nullptr);
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
        event.ignore();
        QCoreApplication::sendEvent(focus, &event);
        QCoreApplication::processEvents();
        return event.isAccepted();
    }

    void mouse(QEvent::Type type, QPoint pos) const
    {
        QMouseEvent event(type, QPointF(pos), QPointF(pos), Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QCoreApplication::processEvents();
    }

    void drag(QPoint from, QPoint to) const
    {
        mouse(QEvent::MouseButtonPress, from);
        mouse(QEvent::MouseMove, (from + to) / 2);
        mouse(QEvent::MouseMove, to);
        mouse(QEvent::MouseButtonRelease, to);
    }

    // `S` AS A KEY EVENT -- the chord, not `setTool` -- and the tool armed by it.
    void armSnapshot() const
    {
        CHECK(press(Qt::Key_S, QStringLiteral("s")));
        REQUIRE(view->tools().armed() == ui::ToolKind::Snapshot);
    }

    // A band well inside page 1 at 100 %: the page's top-left is a margin into the viewport.
    QRect bandOnPage() const
    {
        const core::render::PageSlot* slot = view->layoutEngine().layout().slotForPage(0);
        REQUIRE(slot != nullptr);
        const core::DeviceRect visible = view->controller().visibleRect();
        const double dpr = view->devicePixelRatioF();
        const int left = static_cast<int>(std::ceil((slot->rect.left() - visible.left()) / dpr)) + 40;
        const int top = static_cast<int>(std::ceil((slot->rect.top() - visible.top()) / dpr)) + 40;
        return QRect(left, top, 201, 121);
    }

    QImage snapshot(const QRect& band)
    {
        drag(band.topLeft(), band.bottomRight());
        fixture.pumpTiles();
        return QGuiApplication::clipboard()->image();
    }

    void setDpi(int dpi)
    {
        fixture.settings.set(core::keys::pageDisplay::snapshotDpi, dpi);
        QCoreApplication::processEvents();
        REQUIRE(view->settings().snapshotDpi == fixture.settings.get(core::keys::pageDisplay::snapshotDpi));
    }
};

void resetClipboard()
{
    QGuiApplication::clipboard()->setText(QStringLiteral("what the person had copied"));
    REQUIRE(QGuiApplication::clipboard()->image().isNull());
}

bool clipboardUntouched()
{
    return QGuiApplication::clipboard()->image().isNull()
           && QGuiApplication::clipboard()->text() == QStringLiteral("what the person had copied");
}

// The size a band of `band` widget pixels is at `dpi`, by THIS FILE's arithmetic: the view's scale is page
// 1's drawn width over the width this fixture gave it (the fake's A4, 595.276 pt), the band in points is its
// device pixels over that, and at `dpi` it is points x dpi / 72 -- none of `SnapshotTool::resolve`'s code.
QSize expectedAt(const ui::PageView& view, const QRect& band, double dpi)
{
    const core::render::PageSlot* slot = view.layoutEngine().layout().slotForPage(0);
    REQUIRE(slot != nullptr);
    const double viewScale = slot->rect.size.width / 595.276;
    const double dpr = view.devicePixelRatioF();
    return QSize(static_cast<int>(std::lround(band.width() * dpr / viewScale * dpi / 72.0)),
                 static_cast<int>(std::lround(band.height() * dpr / viewScale * dpi / 72.0)));
}

bool withinOnePixel(const QSize& a, const QSize& b)
{
    return std::abs(a.width() - b.width()) <= 1 && std::abs(a.height() - b.height()) <= 1;
}

} // namespace

// ---- The arithmetic, with no window --------------------------------------------------------------------

TEST_CASE("snapshot resolution is the sentinel the floor the ceiling and the bound", "[snapshot][A-114]")
{
    // 0 IS THE VIEW'S SCALE, whatever it is.
    SnapshotTool::Resolution r = SnapshotTool::resolve(100.0, 100.0, 0, 2.5);
    CHECK(r.deviceScale == 2.5);
    CHECK_FALSE(r.reduced);

    // 72 ... 600 is dots per inch; 1 ... 71 is taken as 72; above 600 is 600.
    CHECK(SnapshotTool::resolve(100.0, 100.0, 72, 2.5).dpi() == 72);
    CHECK(SnapshotTool::resolve(100.0, 100.0, 300, 2.5).dpi() == 300);
    CHECK(SnapshotTool::resolve(100.0, 100.0, 600, 2.5).dpi() == 600);
    CHECK(SnapshotTool::resolve(100.0, 100.0, 40, 2.5).dpi() == 72);
    CHECK(SnapshotTool::resolve(100.0, 100.0, 1, 2.5).dpi() == 72);
    CHECK(SnapshotTool::resolve(100.0, 100.0, 9000, 2.5).dpi() == 600);

    // THE BOUND: a whole A3 page at 600 dpi is ~70 megapixels and is REDUCED to the largest scale that
    // fits -- inside 50 megapixels and within 1 % of it -- and says from what.
    r = SnapshotTool::resolve(841.89, 1190.55, 600, 1.0);
    CHECK(r.reduced);
    CHECK(r.askedDpi() == 600);
    CHECK(SnapshotTool::pixelsAt(841.89, 1190.55, r.askedScale) > SnapshotTool::kMaxPixels);
    CHECK(SnapshotTool::pixelsAt(841.89, 1190.55, r.deviceScale) <= SnapshotTool::kMaxPixels);
    CHECK(SnapshotTool::pixelsAt(841.89, 1190.55, r.deviceScale) > SnapshotTool::kMaxPixels * 99 / 100);
    CHECK(r.dpi() < 600);
    CHECK(r.dpi() > 480);

    // THE CONTROL: the same page at 300 dpi (~17 megapixels) is not reduced.
    CHECK_FALSE(SnapshotTool::resolve(841.89, 1190.55, 300, 1.0).reduced);
}

// ---- The tool alone, over a host with no widget ---------------------------------------------------------
// WHY THIS CASE EXISTS: the window case below cannot tell WHICH layer refused a click -- the view also holds
// the part of a band on the page to the threshold -- and a planted control that removed the TOOL's threshold
// stayed green there. Here the host counts what it is asked for, so the tool's own refusal is asserted.

namespace {
struct CountingHost final : ui::ToolHost {
    ui::ViewportController viewport;
    int                    asked = 0;
    QRect                  last;

    ui::ViewportController& controller() override { return viewport; }
    const ui::ViewportController& controller() const override { return viewport; }
    QSize  viewportSize() const override { return QSize(800, 600); }
    double devicePixelRatio() const override { return 1.0; }
    void   requestRepaint() override {}
    void   recordView() override {}
    bool   takeSnapshot(const QRect& band) override
    {
        ++asked;
        last = band;
        return true;
    }
};
} // namespace

TEST_CASE("the Snapshot tool hands a band to its host and a click to nobody", "[snapshot][A-91]")
{
    CountingHost host;
    SnapshotTool tool;
    const int tiny = ui::MarqueeZoomTool::kMinDragPixels - 2;   // a QRect from two points counts both ends

    for (const QPoint end : {QPoint(100, 100), QPoint(100 + tiny, 300), QPoint(300, 100 + tiny)}) {
        tool.press(host, QPoint(100, 100));
        tool.move(host, end);
        tool.release(host, end);
    }
    CHECK(host.asked == 0);

    // `Esc` mid-drag: nothing either.
    tool.press(host, QPoint(100, 100));
    tool.move(host, QPoint(300, 250));
    tool.cancel(host);
    tool.release(host, QPoint(300, 250));
    CHECK(host.asked == 0);

    // THE CONTROL: a real band is handed over, as the widget pixels it was dragged in.
    tool.press(host, QPoint(100, 100));
    tool.move(host, QPoint(300, 250));
    tool.release(host, QPoint(300, 250));
    CHECK(host.asked == 1);
    CHECK(host.last == QRect(QPoint(100, 100), QPoint(300, 250)));
}

// ---- The tool, through the window ----------------------------------------------------------------------

TEST_CASE("S arms the Snapshot tool from a key event and tool.snapshot is a sticky tool", "[snapshot][A-90]")
{
    Viewer viewer;
    CHECK(viewer.fixture.actions.isRegistered(ActionId::ToolSnapshot));
    CHECK(viewer.view->tools().armed() == ui::ToolKind::Select);

    viewer.armSnapshot();
    CHECK(viewer.view->tools().cursor() == Qt::CrossCursor);

    // STICKY, like the other four: a snapshot taken leaves the tool armed for the next.
    resetClipboard();
    const QImage first = viewer.snapshot(viewer.bandOnPage());
    CHECK_FALSE(first.isNull());
    CHECK(viewer.view->tools().armed() == ui::ToolKind::Snapshot);

    // And `V` -- another tool's chord -- disarms it, as `ToolController::arm` does for every tool.
    CHECK(viewer.press(Qt::Key_V, QStringLiteral("v")));
    CHECK(viewer.view->tools().armed() == ui::ToolKind::Select);
}

TEST_CASE("Esc abandons a snapshot drag and a click is not a snapshot", "[snapshot][A-91]")
{
    Viewer viewer;
    viewer.armSnapshot();
    const QRect band = viewer.bandOnPage();

    // `Esc` MID-DRAG: the press and a move, then the key, then the release -- nothing copied.
    resetClipboard();
    viewer.mouse(QEvent::MouseButtonPress, band.topLeft());
    viewer.mouse(QEvent::MouseMove, band.bottomRight());
    REQUIRE(viewer.view->tools().snapshot().isDragging());
    CHECK(viewer.press(Qt::Key_Escape));
    CHECK_FALSE(viewer.view->tools().snapshot().isDragging());
    viewer.mouse(QEvent::MouseButtonRelease, band.bottomRight());
    viewer.fixture.pumpTiles();
    CHECK(clipboardUntouched());
    CHECK_FALSE(viewer.view->snapshotRaster().isPending());

    // A CLICK, and a drag under the marquee's threshold on either axis: nothing. `kMinDragPixels - 2`, as
    // `tst_tools` drags it: a `QRect` from two points counts BOTH end pixels, so an offset of `- 1` is a band
    // exactly at the threshold -- which this case's first run sent, and which rightly copied.
    const int tiny = ui::MarqueeZoomTool::kMinDragPixels - 2;
    viewer.drag(band.topLeft(), band.topLeft());
    viewer.drag(band.topLeft(), band.topLeft() + QPoint(tiny, 200));
    viewer.drag(band.topLeft(), band.topLeft() + QPoint(200, tiny));
    viewer.fixture.pumpTiles();
    CHECK(clipboardUntouched());

    // A BAND OVER THE DESK, beside every page, is over no page and copies nothing -- and nor does one that
    // clips the page's corner by less than the threshold: a sliver of a page is not a snapshot either.
    {
        const core::render::PageSlot* slot = viewer.view->layoutEngine().layout().slotForPage(0);
        REQUIRE(slot != nullptr);
        const core::DeviceRect visible = viewer.view->controller().visibleRect();
        const double dpr = viewer.view->devicePixelRatioF();
        const int right = static_cast<int>(std::ceil((slot->rect.right() - visible.left()) / dpr));
        const int left = static_cast<int>(std::floor((slot->rect.left() - visible.left()) / dpr));
        const int top = static_cast<int>(std::floor((slot->rect.top() - visible.top()) / dpr));
        REQUIRE(viewer.view->viewport()->width() - right > 30);   // there IS desk to the right to drag over
        viewer.drag(QPoint(right + 5, top + 20), QPoint(viewer.view->viewport()->width() - 5, top + 120));
        viewer.drag(QPoint(std::max(0, left - 10), top + 20), QPoint(left + tiny, top + 120));
        viewer.fixture.pumpTiles();
        CHECK(clipboardUntouched());
    }

    // THE CONTROL (convention 7): the same band dragged to the end DOES reach the clipboard -- so the three
    // assertions above are about the gesture, not a clipboard this suite cannot write.
    const QImage image = viewer.snapshot(band);
    CHECK_FALSE(image.isNull());
    CHECK_FALSE(clipboardUntouched());
}

TEST_CASE("a snapshot carries none of the overlays or the inversion", "[snapshot][A-90]")
{
    Viewer viewer;
    viewer.armSnapshot();
    const QRect band = viewer.bandOnPage();

    resetClipboard();
    const QImage plain = viewer.snapshot(band);
    REQUIRE_FALSE(plain.isNull());
    const QRgb blue = qRgb(uitest::kFakeTileRed, uitest::kFakeTileGreen, uitest::kFakeTileBlue);
    CHECK(plain.pixel(plain.width() / 2, plain.height() / 2) == blue);

    // EVERY VIEW TRANSFORM AND OVERLAY ON: inversion, the grid, a guide, and the transparency checkerboard.
    ui::DocumentWindow* document = viewer.window->currentDocument();
    REQUIRE(document != nullptr);
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ViewInvertColors, core::KeyContext::Viewer));
    REQUIRE(viewer.view->invertColors());
    viewer.fixture.settings.set(core::keys::units::showGrid, true);
    viewer.fixture.settings.set(core::keys::units::showGuides, true);
    viewer.fixture.settings.set(core::keys::pageDisplay::transparencyGrid, true);
    QCoreApplication::processEvents();
    viewer.fixture.pumpTiles();

    // THE CONTROL: the VIEW really is different now -- the inverted blue is what is on screen.
    const QImage onScreen = viewer.view->viewport()->grab().toImage();
    const QPoint centre = band.center();
    CHECK(onScreen.pixel(centre) != blue);

    resetClipboard();
    const QImage withOverlays = viewer.snapshot(band);
    REQUIRE_FALSE(withOverlays.isNull());
    CHECK(withOverlays.size() == plain.size());

    // THE SNAPSHOT IS NOT: every pixel is the document's blue, or -- where the transparent page lets the
    // sheet through -- the white the snapshot is composited onto. Never the inversion, never the grid.
    int other = 0;
    for (int y = 0; y < withOverlays.height(); ++y)
        for (int x = 0; x < withOverlays.width(); ++x) {
            const QRgb p = withOverlays.pixel(x, y);
            if (p != blue && p != qRgb(255, 255, 255)) ++other;
        }
    CHECK(other == 0);
    CHECK_FALSE(withOverlays.hasAlphaChannel());
}

TEST_CASE("a snapshot is asked for once at Visible and bypasses the tile cache", "[snapshot][A-90]")
{
    Viewer viewer;
    viewer.armSnapshot();
    const QRect band = viewer.bandOnPage();

    const long long bypassingBefore = viewer.fixture.renderScheduler.stats().enqueuedBypassingCache;
    const long long enqueuedBefore = viewer.view->snapshotRaster().enqueuedCount();
    resetClipboard();
    viewer.drag(band.topLeft(), band.bottomRight());
    CHECK(viewer.view->snapshotRaster().enqueuedCount() == enqueuedBefore + 1);
    CHECK(viewer.fixture.renderScheduler.stats().enqueuedBypassingCache == bypassingBefore + 1);

    // THE REQUEST IS `TilePresenter`'s SHAPE: the document, the revision and the view's flags of a tile.
    const ui::PageView::SnapshotRegion region = viewer.view->snapshotRegionFor(band);
    REQUIRE(region.request.isValid());
    REQUIRE_FALSE(viewer.view->tiles().heldRequests().empty());
    const core::render::RenderRequest tile = viewer.view->tiles().heldRequests().front();
    CHECK(region.request.document == tile.document);
    CHECK(region.request.revision == tile.revision);
    CHECK(region.request.flags == tile.flags);
    CHECK(region.request.viewRotation == tile.viewRotation);

    viewer.fixture.pumpTiles();
    CHECK_FALSE(QGuiApplication::clipboard()->image().isNull());
    // AND IT WAS NOT KEPT: nothing in the tile cache is at the snapshot's request.
    CHECK_FALSE(viewer.fixture.renderCache.lookup(region.request).has_value());
}

TEST_CASE("a snapshot's resolution is pageDisplay/snapshotDpi", "[snapshot][A-114]")
{
    Viewer viewer;
    viewer.armSnapshot();
    const QRect band = viewer.bandOnPage();
    const double dpr = viewer.view->devicePixelRatioF();

    // 0 -- AS DISPLAYED: the band's own device pixels, within one.
    viewer.setDpi(0);
    resetClipboard();
    QImage image = viewer.snapshot(band);
    INFO("at 0: " << image.width() << " x " << image.height());
    CHECK(withinOnePixel(image.size(), QSize(static_cast<int>(std::lround(band.width() * dpr)),
                                             static_cast<int>(std::lround(band.height() * dpr)))));

    QSize at72;
    for (const int dpi : {72, 300, 600}) {
        viewer.setDpi(dpi);
        resetClipboard();
        image = viewer.snapshot(band);
        const QSize expected = expectedAt(*viewer.view, band, dpi);
        INFO("at " << dpi << ": " << image.width() << " x " << image.height() << ", expected "
                   << expected.width() << " x " << expected.height());
        CHECK(withinOnePixel(image.size(), expected));
        if (dpi == 72) at72 = image.size();
    }

    // 40 IS TAKEN AS 72.
    viewer.setDpi(40);
    resetClipboard();
    image = viewer.snapshot(band);
    CHECK(image.size() == at72);

    // AND THE STATUS BAR SAYS WHAT WAS TAKEN, at the resolution it was taken at.
    CHECK(viewer.window->statusBar()->message()->text()
          == ui::catalog::translate(ui::catalog::key::StatusSnapshotCopied)
                 .arg(image.width())
                 .arg(image.height())
                 .arg(72));
}

TEST_CASE("a whole A3 page at 600 dpi is reduced to fit 50 megapixels and says so", "[snapshot][A-114]")
{
    Viewer viewer(/*a3=*/true);
    viewer.view->setFitMode(core::render::FitMode::Page);
    viewer.fixture.pumpTiles();
    viewer.armSnapshot();

    // THE WHOLE VIEWPORT: the band covers page 1 and the desk round it; the copy is page 1, clipped.
    const QRect whole(1, 1, viewer.view->viewport()->width() - 2, viewer.view->viewport()->height() - 2);
    const ui::PageView::SnapshotRegion probe = viewer.view->snapshotRegionFor(whole);
    REQUIRE(probe.request.isValid());
    REQUIRE(probe.request.pageIndex == 0);
    CHECK(std::abs(probe.widthPoints - 841.89) < 2.0);
    CHECK(std::abs(probe.heightPoints - 1190.55) < 2.0);

    viewer.setDpi(600);
    resetClipboard();
    const QImage image = viewer.snapshot(whole);
    REQUIRE_FALSE(image.isNull());

    // ON THE IMAGE'S SIZE, not on the message alone (the guide's rule 1): inside the bound, and not far
    // inside it -- the LARGEST resolution that fits, not some smaller safe one.
    const long long pixels = static_cast<long long>(image.width()) * image.height();
    INFO("reduced to " << image.width() << " x " << image.height() << " = " << pixels << " pixels");
    CHECK(pixels <= SnapshotTool::kMaxPixels);
    CHECK(pixels > SnapshotTool::kMaxPixels * 98 / 100);
    // The A3 page's proportions survive the reduction.
    CHECK(std::abs(static_cast<double>(image.height()) / image.width() - 1190.55 / 841.89) < 0.01);

    const SnapshotTool::Resolution resolution = SnapshotTool::resolve(probe.widthPoints, probe.heightPoints, 600, 1.0);
    REQUIRE(resolution.reduced);
    CHECK(viewer.window->statusBar()->message()->text()
          == ui::catalog::translate(ui::catalog::key::StatusSnapshotReduced)
                 .arg(image.width())
                 .arg(image.height())
                 .arg(resolution.dpi())
                 .arg(600));

    // THE CONTROL: the same page at 300 dpi is taken at 300, whole, and the message says no reduction.
    viewer.setDpi(300);
    resetClipboard();
    const QImage at300 = viewer.snapshot(whole);
    CHECK(withinOnePixel(at300.size(), QSize(static_cast<int>(std::lround(841.89 * 300 / 72.0)),
                                             static_cast<int>(std::lround(1190.55 * 300 / 72.0)))));
    CHECK(viewer.window->statusBar()->message()->text()
          == ui::catalog::translate(ui::catalog::key::StatusSnapshotCopied)
                 .arg(at300.width())
                 .arg(at300.height())
                 .arg(300));
}

TEST_CASE("a snapshot taken in a rotated view is rotated", "[snapshot][A-90]")
{
    Viewer viewer;
    viewer.armSnapshot();
    const QRect band = viewer.bandOnPage();
    resetClipboard();
    const QImage upright = viewer.snapshot(band);

    // ROTATED 90 DEGREES: the request carries the view's rotation, and a band of the same shape on the
    // rotated page is the same shape of image -- what moves is WHICH part of the page it is, which the
    // ink case asserts. Here: the request is in the rotated raster's space.
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ViewRotateViewCw, core::KeyContext::Viewer));
    viewer.fixture.pumpTiles();
    const QRect rotatedBand = viewer.bandOnPage();
    const ui::PageView::SnapshotRegion region = viewer.view->snapshotRegionFor(rotatedBand);
    REQUIRE(region.request.isValid());
    CHECK(region.request.viewRotation == core::PageRotation::Clockwise90);
    resetClipboard();
    const QImage rotated = viewer.snapshot(rotatedBand);
    CHECK(withinOnePixel(rotated.size(), upright.size()));
}
