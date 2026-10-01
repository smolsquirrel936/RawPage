// tst_snapshotink -- Stage 5 T10, guide cases A-90 and A-114's pixel halves
// (`Stage5-TextSearch-Testing-Guide.md` §5.12): a snapshot taken with the mouse, through the PRODUCT'S OWN
// COMPOSITION -- the real engine rasterizing -- graded against a FRESH RENDER of the page made by this file.
//
//   * **A-90.** The snapshot equals the same region cropped out of a whole-page render this file asked the
//     scheduler for itself, within the golden suite's thresholds (read from `golden/manifest.json`, not
//     restated); with the inversion, the grid, the guides, a selection and search highlights all ON it is
//     unchanged -- and the control is that the VIEW is changed by them; a view rotated 90 degrees gives the
//     rotated page.
//   * **A-114's control.** At 300 dpi the snapshot equals a fresh render at 300 dpi, which the snapshot taken
//     AS DISPLAYED, scaled up to the same size, fails -- the difference between re-rasterizing and grabbing.
//
// WHY `tests/composition` AND NOT `tests/ui` (a deviation from the plan's file list, `tst_selectionink`'s): ink
// needs the engine, which `rawpage_ui_tests` links none of. The widget half -- the key, `Esc`, the sizes, the
// bound -- is `tests/ui/tst_snapshot.cpp`.
//
// WHAT THIS DOES NOT COVER (convention 19): the region's PLACEMENT is checked against this file's own page
// arithmetic to a pixel, but the crop then reads the product's tile rectangle -- a region off by one pixel
// in both would agree here and is what the pixel-size check beside it is for. Offscreen renders at a device
// pixel ratio of 1, and the clipboard is Qt's in-process one: what an image editor receives is M-03's. And
// every document here is OURS (D5-23).

#include <catch2/catch_test_macros.hpp>

#include "app/CompositionRoot.h"
#include "core/cmd/ActionRegistry.h"
#include "core/render/RenderScheduler.h"
#include "core/settings/Settings.h"
#include "tests/render/TextOracle.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/HeldRaster.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/ViewportController.h"
#include "ui/write/FindReplaceBar.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>

using namespace rawpage;

namespace {

// ---------------------------------------------------------------------------------------------
// The golden suite's thresholds, READ from where they are stated (D2-13)
// ---------------------------------------------------------------------------------------------

struct Thresholds {
    int    channelTolerance = -1;
    double differingFraction = -1.0;
    int    grossChannelTolerance = -1;
    double grossFraction = -1.0;
};

Thresholds goldenThresholds()
{
    QFile file(QStringLiteral(RAWPAGE_GOLDEN_MANIFEST));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonObject t = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("thresholds")).toObject();
    Thresholds thresholds;
    thresholds.channelTolerance = t.value(QStringLiteral("channelTolerance")).toInt(-1);
    thresholds.differingFraction = t.value(QStringLiteral("differingFraction")).toDouble(-1.0);
    thresholds.grossChannelTolerance = t.value(QStringLiteral("grossChannelTolerance")).toInt(-1);
    thresholds.grossFraction = t.value(QStringLiteral("grossFraction")).toDouble(-1.0);
    REQUIRE(thresholds.channelTolerance >= 0);
    REQUIRE(thresholds.differingFraction >= 0.0);
    REQUIRE(thresholds.grossChannelTolerance >= 0);
    REQUIRE(thresholds.grossFraction >= 0.0);
    return thresholds;
}

struct Difference {
    bool      sameSize = false;
    long long pixels = 0;
    long long differing = 0;
    long long gross = 0;

    bool within(const Thresholds& t) const
    {
        return sameSize && pixels > 0 && differing <= static_cast<long long>(t.differingFraction * pixels)
               && gross <= static_cast<long long>(t.grossFraction * pixels);
    }
};

// The golden suite's measure (`tests/golden/GoldenSuite.cpp`'s `compare`): the largest channel delta per pixel,
// counted above each tolerance.
Difference differ(const QImage& expectedIn, const QImage& actualIn, const Thresholds& t)
{
    Difference d;
    const QImage expected = expectedIn.convertToFormat(QImage::Format_RGB32);
    const QImage actual = actualIn.convertToFormat(QImage::Format_RGB32);
    d.sameSize = expected.size() == actual.size();
    if (!d.sameSize) return d;
    d.pixels = static_cast<long long>(expected.width()) * expected.height();
    for (int y = 0; y < expected.height(); ++y) {
        const auto* e = reinterpret_cast<const QRgb*>(expected.constScanLine(y));
        const auto* a = reinterpret_cast<const QRgb*>(actual.constScanLine(y));
        for (int x = 0; x < expected.width(); ++x) {
            const int delta = std::max({std::abs(qRed(e[x]) - qRed(a[x])), std::abs(qGreen(e[x]) - qGreen(a[x])),
                                        std::abs(qBlue(e[x]) - qBlue(a[x]))});
            if (delta > t.channelTolerance) ++d.differing;
            if (delta > t.grossChannelTolerance) ++d.gross;
        }
    }
    return d;
}

// How much of an image is INK -- so a comparison of two blank regions cannot pass for the wrong reason.
double inkFraction(const QImage& image)
{
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    long long dark = 0;
    for (int y = 0; y < rgb.height(); ++y)
        for (int x = 0; x < rgb.width(); ++x)
            if (qGray(rgb.pixel(x, y)) < 128) ++dark;
    return rgb.isNull() ? 0.0 : static_cast<double>(dark) / (static_cast<double>(rgb.width()) * rgb.height());
}

// ---------------------------------------------------------------------------------------------
// The product, composed as `app/main.cpp` composes it, over a scratch directory (`tst_selectionink`'s)
// ---------------------------------------------------------------------------------------------

struct Product {
    QTemporaryDir                         scratch;
    std::unique_ptr<app::CompositionRoot> root;
    std::unique_ptr<ui::MainWindow>       window;
    ui::PageView*                         view = nullptr;

    Product()
    {
        REQUIRE(scratch.isValid());
        app::CompositionRoot::Options options;
        options.settingsPath = QDir(scratch.path()).filePath(QStringLiteral("settings.ini"));
        options.sessionPath = QDir(scratch.path()).filePath(QStringLiteral("session.json"));
        options.recoveryRoot = QDir(scratch.path()).filePath(QStringLiteral("recovery"));
        root = std::make_unique<app::CompositionRoot>(std::move(options));
        window = std::make_unique<ui::MainWindow>(root->services());
        window->resize(1000, 800);
        window->show();
        QCoreApplication::processEvents();
    }

    ~Product()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
        root.reset();
    }

    ui::PageView* open(const char* relative)
    {
        const QString source = test_oracle::corpusPath(relative);
        const QString target = QDir(scratch.path()).filePath(QFileInfo(source).fileName());
        QFile::remove(target);
        REQUIRE(QFile::copy(source, target));
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        window->openFiles({target});
        for (QWidget* widget : window->findChildren<QWidget*>())
            if (QLayout* layout = widget->layout()) layout->activate();
        QCoreApplication::processEvents();
        view = window->currentView();
        REQUIRE(view != nullptr);
        view->setFocus(Qt::OtherFocusReason);
        settle();
        return view;
    }

    void settle()
    {
        for (int round = 0; round < 12; ++round) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            root->services().renderScheduler.waitForIdle(4000);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        view->refresh();
        view->viewport()->repaint();
    }

    core::Settings& settings() { return root->services().settings; }

    void mouse(QEvent::Type type, QPoint pos)
    {
        QMouseEvent event(type, QPointF(pos), view->viewport()->mapToGlobal(QPointF(pos)), Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QCoreApplication::processEvents();
    }

    // A snapshot of `band`, through the tool: armed by its command, dragged, the raster pumped home.
    QImage snapshot(const QRect& band)
    {
        REQUIRE(root->services().actions.dispatch(core::ActionId::ToolSnapshot, core::KeyContext::Viewer));
        REQUIRE(view->tools().armed() == ui::ToolKind::Snapshot);
        QGuiApplication::clipboard()->setText(QStringLiteral("before"));
        mouse(QEvent::MouseButtonPress, band.topLeft());
        mouse(QEvent::MouseMove, band.center());
        mouse(QEvent::MouseMove, band.bottomRight());
        mouse(QEvent::MouseButtonRelease, band.bottomRight());
        settle();
        const QImage image = QGuiApplication::clipboard()->image();
        REQUIRE_FALSE(image.isNull());
        return image;
    }

    // A FRESH RENDER of a whole page, asked for by THIS FILE -- a request built here, field by field, not by
    // `TilePresenter::requestAt` -- through the same scheduler, bypassing the cache.
    QImage freshPage(int page, double scale)
    {
        const core::render::PageSlot* slot = view->layoutEngine().layout().slotForPage(page);
        REQUIRE(slot != nullptr);
        const double presented = view->layoutEngine().layout().presentedScale();
        core::render::RenderRequest request;
        request.document = view->document();
        request.pageIndex = page;
        request.deviceScale = scale;
        request.revision = view->revision();
        request.viewRotation = view->controller().viewRotation();
        request.flags = view->renderFlags();
        request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(slot->rect.size.width / presented * scale)),
                                              static_cast<int>(std::ceil(slot->rect.size.height / presented * scale))};
        REQUIRE(request.isValid());

        core::render::RenderScheduler& scheduler = root->services().renderScheduler;
        std::optional<QImage> arrived;
        const auto token = scheduler.subscribeReady(
            [&](const core::render::RenderRequest& done, const core::RasterImage& raster) {
                if (!(done == request) || !raster.isValid()) return;
                // A DEEP COPY: the held image BORROWS the raster's bytes, and `convertToFormat` to the format it
                // already has is a shallow copy -- this file's first run read freed memory here.
                const ui::HeldRaster held = ui::holdRaster(raster);
                arrived = held.image.copy().convertToFormat(QImage::Format_RGB32);
            });
        scheduler.enqueue(request, core::render::RenderPriority::Visible, core::render::CachePolicy::Bypass);
        QElapsedTimer timer;
        timer.start();
        while (!arrived && timer.elapsed() < 30000) settle();
        scheduler.unsubscribeReady(token);
        REQUIRE(arrived.has_value());
        return *arrived;
    }

    // A band well inside page 1's sheet, in widget pixels, from THIS file's reading of the slot.
    QRect bandInside(int insetX, int insetY, int width, int height) const
    {
        const core::render::PageSlot* slot = view->layoutEngine().layout().slotForPage(0);
        REQUIRE(slot != nullptr);
        const core::DeviceRect visible = view->controller().visibleRect();
        const double dpr = view->devicePixelRatioF();
        const int left = static_cast<int>(std::ceil((slot->rect.left() - visible.left()) / dpr));
        const int top = static_cast<int>(std::ceil((slot->rect.top() - visible.top()) / dpr));
        const QRect band(left + insetX, top + insetY, width, height);
        // ON SCREEN, or a grab of it reads the black outside the widget and every comparison of two grabs
        // agrees for the wrong reason -- this file's first run did exactly that, with the view scrolled.
        REQUIRE(view->viewport()->rect().contains(band));
        return band;
    }
};

// The viewport as painted, cropped to `band` -- in the GRAB's pixels, which carry its own ratio.
QImage grabBand(const ui::PageView& view, const QRect& band)
{
    const QImage grab = view.viewport()->grab().toImage();
    const double r = grab.devicePixelRatio();
    return grab.copy(QRect(static_cast<int>(band.x() * r), static_cast<int>(band.y() * r),
                           static_cast<int>(band.width() * r), static_cast<int>(band.height() * r)));
}

// The region the product asked for, cropped out of `page` -- and checked against this file's own placement of
// the band on the sheet to a pixel first, so the crop is not merely agreeing with itself.
QImage cropAsAsked(const ui::PageView& view, const QRect& band, const QImage& page, double scale)
{
    const ui::PageView::SnapshotRegion region = view.snapshotRegionFor(band);
    REQUIRE(region.request.isValid());
    const core::render::PageSlot* slot = view.layoutEngine().layout().slotForPage(region.request.pageIndex);
    const core::DeviceRect visible = view.controller().visibleRect();
    const double dpr = view.devicePixelRatioF();
    const double factor = scale / view.layoutEngine().layout().presentedScale();
    // The band in content pixels, CLIPPED to the sheet -- a band over the desk copies only the page.
    const QRectF sheet(slot->rect.left(), slot->rect.top(), slot->rect.size.width, slot->rect.size.height);
    const QRectF part = sheet.intersected(QRectF(visible.left() + band.left() * dpr, visible.top() + band.top() * dpr,
                                                 band.width() * dpr, band.height() * dpr));
    const double expectX = (part.left() - sheet.left()) * factor;
    const double expectY = (part.top() - sheet.top()) * factor;
    const core::render::TileRect& tile = region.request.tile;
    INFO("tile at " << tile.x << "," << tile.y << " " << tile.width << "x" << tile.height
                    << ", this file puts the band at " << expectX << "," << expectY << " " << part.width() * factor
                    << "x" << part.height() * factor);
    CHECK(std::abs(tile.x - expectX) <= 1.0);
    CHECK(std::abs(tile.y - expectY) <= 1.0);
    CHECK(std::abs(tile.width - part.width() * factor) <= 2.0);
    CHECK(std::abs(tile.height - part.height() * factor) <= 2.0);
    return page.copy(tile.x, tile.y, tile.width, tile.height);
}

} // namespace

TEST_CASE("a snapshot is the document as drawn and nothing else", "[snapshotink][A-90]")
{
    const Thresholds thresholds = goldenThresholds();
    Product product;
    ui::PageView* view = product.open("ordinary/columns-two.pdf");

    // THE FIND BAR OPEN FROM THE START, empty: it takes its height from the panes, and a view that changed
    // size between the two snapshots would compare two different regions -- this case's first run did.
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::FindFind, core::KeyContext::Viewer));
    ui::FindReplaceBar& bar = *product.window->currentDocument()->findBar();
    view->setFitMode(core::render::FitMode::Width);
    view->controller().setScroll(core::DevicePoint{0.0, 0.0});
    product.settle();

    // A REGION OF TEXT: the top of the first column, inked enough that a blank comparison cannot pass.
    const QRect band = product.bandInside(50, 60, 300, 160);
    const QImage snapshot = product.snapshot(band);
    const double scale = view->layoutEngine().layout().presentedScale();   // pageDisplay/snapshotDpi is 0
    const QImage expected = cropAsAsked(*view, band, product.freshPage(0, scale), scale);
    INFO("ink " << inkFraction(expected));
    REQUIRE(inkFraction(expected) > 0.01);

    Difference d = differ(expected, snapshot, thresholds);
    INFO("differing " << d.differing << " gross " << d.gross << " of " << d.pixels);
    CHECK(d.within(thresholds));

    // EVERY VIEW TRANSFORM AND OVERLAY ON: the inversion, the grid, the guides, a selection of the whole
    // document and the find bar's highlights.
    const QImage cleanView = grabBand(*view, band);
    const core::DevicePoint scroll = view->controller().scroll();
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::ViewInvertColors, core::KeyContext::Viewer));
    product.settings().set(core::keys::units::showGrid, true);
    product.settings().set(core::keys::units::showGuides, true);
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::EditSelectAll, core::KeyContext::Viewer));
    bar.field()->setFocus(Qt::OtherFocusReason);
    // TYPED, as `tst_selectionink` types it: the bar searches on an EDIT, which `setText` is not.
    for (const QChar ch : QStringLiteral("viewport")) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString(ch));
        QCoreApplication::sendEvent(bar.field(), &press);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(ch));
        QCoreApplication::sendEvent(bar.field(), &release);
    }
    QCoreApplication::processEvents();
    QElapsedTimer timer;
    timer.start();
    while ((bar.isSearchPending() || bar.isSearching() || bar.searchesFinished() == 0) && timer.elapsed() < 30000)
        product.settle();
    REQUIRE_FALSE(bar.hits().empty());
    // THE FIND BAR TOOK THE VIEW TO ITS FIRST HIT; put it back, so the band is over the same text.
    view->controller().setScroll(scroll);
    product.settle();
    REQUIRE(view->invertColors());
    REQUIRE(product.window->currentDocument()->selection()->hasSelection());

    // THE CONTROL: the VIEW really is different -- what a widget grab would have copied.
    const QImage overlaidView = grabBand(*view, band);
    const Difference viewMoved = differ(cleanView, overlaidView, thresholds);
    INFO("the view under the overlays: differing " << viewMoved.differing << " of " << viewMoved.pixels);
    CHECK(viewMoved.sameSize);
    CHECK_FALSE(viewMoved.within(thresholds));

    // AND THE SNAPSHOT IS NOT: the same image as before, and still the fresh render.
    const QImage overlaid = product.snapshot(band);
    d = differ(snapshot, overlaid, thresholds);
    INFO("the snapshot under the overlays: differing " << d.differing << " gross " << d.gross << " of " << d.pixels);
    CHECK(d.within(thresholds));
    d = differ(expected, overlaid, thresholds);
    CHECK(d.within(thresholds));
}

TEST_CASE("a snapshot in a rotated view is the rotated page", "[snapshotink][A-90]")
{
    const Thresholds thresholds = goldenThresholds();
    Product product;
    ui::PageView* view = product.open("ordinary/columns-two.pdf");
    view->setFitMode(core::render::FitMode::Page);
    product.settle();

    const QRect whole(0, 0, view->viewport()->width(), view->viewport()->height());
    const QImage upright = product.snapshot(whole);

    REQUIRE(product.root->services().actions.dispatch(core::ActionId::ViewRotateViewCw, core::KeyContext::Viewer));
    view->setFitMode(core::render::FitMode::Page);
    product.settle();
    REQUIRE(view->controller().viewRotation() == core::PageRotation::Clockwise90);

    const QImage rotated = product.snapshot(whole);
    const double scale = view->layoutEngine().layout().presentedScale();
    const QImage expected = cropAsAsked(*view, whole, product.freshPage(0, scale), scale);
    const Difference d = differ(expected, rotated, thresholds);
    INFO("differing " << d.differing << " gross " << d.gross << " of " << d.pixels);
    CHECK(d.within(thresholds));

    // THE CONTROL: it is not the upright page -- a portrait page rotated is landscape.
    CHECK(upright.height() > upright.width());
    CHECK(rotated.width() > rotated.height());
}

TEST_CASE("a snapshot at 300 dpi is a render at 300 dpi and not the view scaled up", "[snapshotink][A-114]")
{
    const Thresholds thresholds = goldenThresholds();
    Product product;
    ui::PageView* view = product.open("ordinary/columns-two.pdf");
    view->setFitMode(core::render::FitMode::None);
    view->setZoom(core::render::Zoom{1.0});
    view->controller().setScroll(core::DevicePoint{0.0, 0.0});
    product.settle();

    const QRect band = product.bandInside(60, 70, 240, 140);
    const QImage asDisplayed = product.snapshot(band);

    product.settings().set(core::keys::pageDisplay::snapshotDpi, 300);
    QCoreApplication::processEvents();
    REQUIRE(view->settings().snapshotDpi == 300);
    const QImage at300 = product.snapshot(band);

    const double scale = 300.0 / 72.0;
    const QImage expected = cropAsAsked(*view, band, product.freshPage(0, scale), scale);
    REQUIRE(inkFraction(expected) > 0.01);
    Difference d = differ(expected, at300, thresholds);
    INFO("at 300: differing " << d.differing << " gross " << d.gross << " of " << d.pixels);
    CHECK(d.within(thresholds));

    // THE CONTROL: the as-displayed snapshot scaled up to the same size -- a bitmap merely enlarged -- fails.
    const QImage enlarged = asDisplayed.scaled(at300.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    d = differ(expected, enlarged, thresholds);
    INFO("enlarged: differing " << d.differing << " gross " << d.gross << " of " << d.pixels);
    CHECK_FALSE(d.within(thresholds));
}
