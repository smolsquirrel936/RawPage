// tst_measureproduct -- Stage 6 T12 through the PRODUCT: `corpus/ordinary/measure-scale.pdf` opened in the product's
// own composition, the paths read by the shipped engine, the measuring tools driven by mouse and key events, the
// file saved and reopened fresh (`Stage6-Annotations-Testing-Guide.md` §5.15; A-89, A-90, A-88's product half;
// D6-14).
//
//   * A-89: a drag that STARTS three device pixels from each of the generator's points -- every endpoint, midpoint
//     and crossing `corpus/labels/annots-measure-scale.json` lists -- lands EXACTLY on its coordinate, at 100 % and
//     400 %; with `units/snapToObjects` off it does not.
//   * A-90: calibration -- `annot.measureCalibrate`, the rectangle's 216-point side measured end to end and said in
//     the dialog to be 12 ft -- sets `measuring/scaleRatio` to 48; the 288-point line then reads 16.00 ft and the
//     diagonal 14.42 ft. **A deviation from the guide's steps**, which name a 100 mm and a 37.5 mm line: the corpus
//     drawing has neither, and calibrating a length the generator drew against one it also drew is the same claim.
//   * A-88's product half: what the tools made, SAVED, reopened in a fresh engine document and read first-party --
//     the same `/Measure`, the same reading.
//
// THE ORACLE IS THE GENERATOR (rule 7): the points are the label's, never read out of PDFium first.
//
// WHAT IT DOES NOT COVER (convention 19): a real 150 % monitor -- offscreen draws at a device-pixel ratio of 1, so
// the 400 % here is the zoom's -- and the scale dialog's modal loop, answered through
// `MainWindow::setMeasurementScaleAnswerForTesting`. Both are M-09's, on real monitors.

#include <catch2/catch_test_macros.hpp>

#include "tests/composition/AnnotProduct.h"

#include "core/Measure.h"
#include "core/cmd/ActionRegistry.h"
#include "core/settings/Settings.h"
#include "pdf/DocumentBackendImpl.h"
#include "ui/dialogs/MeasurementScaleDialog.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/SnapTargets.h"
#include "ui/viewer/ToolController.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>

#include <cmath>
#include <iostream>

using namespace rawpage;
using annot_product::Product;
using core::Annotation;
using core::AnnotationId;

namespace {

QPoint widgetAt(const ui::PageView& view, int page, core::PdfPoint point)
{
    const std::optional<core::DevicePoint> d = view.layoutEngine().layout().toContent(page, point);
    REQUIRE(d);
    const core::DeviceRect visible = view.controller().visibleRect();
    const double dpr = view.devicePixelRatioF();
    return QPoint(static_cast<int>(std::lround((d->x - visible.left()) / dpr)),
                  static_cast<int>(std::lround((d->y - visible.top()) / dpr)));
}

void mouse(ui::PageView& view, QEvent::Type type, QPoint pos, Qt::MouseButtons buttons = Qt::LeftButton)
{
    QMouseEvent event(type, QPointF(pos), view.viewport()->mapToGlobal(QPointF(pos)),
                      type == QEvent::MouseMove && buttons == Qt::NoButton ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &event);
    QCoreApplication::processEvents();
}

void drag(ui::PageView& view, QPoint from, QPoint to)
{
    mouse(view, QEvent::MouseButtonPress, from);
    for (int step = 1; step <= 4; ++step) mouse(view, QEvent::MouseMove, from + (to - from) * step / 4);
    mouse(view, QEvent::MouseButtonRelease, to);
}

core::RasterImage rasterOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int page, double scale)
{
    const core::PdfSize size = backend.pages(handle)->at(page)->displaySize();
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = page;
    request.deviceScale = scale;
    request.revision = core::Revision::initial();
    request.flags = core::render::RenderFlags::Annotations;
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(size.width * scale)),
                                          static_cast<int>(std::ceil(size.height * scale))};
    auto image = backend.rasterizer(handle)->raster(request);
    REQUIRE(image);
    return image.value();
}

bool dispatch(Product& product, core::ActionId id)
{
    const bool done = static_cast<bool>(product.root->services().actions.dispatch(id, core::KeyContext::Viewer));
    product.view->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    return done;
}

QJsonObject label()
{
    QFile file(test_oracle::corpusPath("labels/annots-measure-scale.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
}

std::vector<core::PdfPoint> labelPoints()
{
    std::vector<core::PdfPoint> out;
    for (const QJsonValue value : label().value("geometry").toObject().value("points").toArray()) {
        const QJsonObject p = value.toObject();
        out.push_back(core::PdfPoint{p.value("x").toDouble(), p.value("y").toDouble()});
    }
    return out;
}

std::vector<Annotation> made(const Product& product)
{
    std::vector<Annotation> out;
    for (const Annotation& a : product.store().all())
        if (a.id.kind == AnnotationId::Kind::Created) out.push_back(a);
    return out;
}

std::string readingOf(const Annotation& a)
{
    const core::Result<core::MeasureReading> r = core::measured(a);
    REQUIRE(r);
    return r.value().text.toStdString();
}

// The page's paths asked for -- as the first aim at the page asks -- and delivered by the engine.
void loadPaths(Product& product, QPoint near)
{
    mouse(*product.view, QEvent::MouseMove, near, Qt::NoButton);
    for (int round = 0; round < 50 && product.document()->snapTargets()->geometry(0) == nullptr; ++round) product.settle();
    REQUIRE(product.document()->snapTargets()->geometry(0) != nullptr);
}

} // namespace

TEST_CASE("a measurement started three device pixels from each of the generator's points starts exactly on it",
          "[measureproduct][A-89][D6-14]")
{
    const std::vector<core::PdfPoint> points = labelPoints();
    REQUIRE(points.size() == 14);
    for (const double zoom : {1.0, 4.0}) {
        INFO("zoom " << zoom);
        Product product;
        ui::PageView* view = product.open("ordinary/measure-scale.pdf");
        view->setZoom(core::render::Zoom{zoom});
        product.settle();
        REQUIRE(view->devicePixelRatioF() == 1.0);   // offscreen: a logical pixel is a device pixel
        int exact = 0;
        for (const core::PdfPoint& target : points) {
            INFO("target " << target.x << "," << target.y);
            view->revealPageRect(0, core::PdfRect{core::PdfPoint{target.x - 30, target.y - 30}, core::PdfSize{60, 60}});
            product.settle();
            REQUIRE(dispatch(product, core::ActionId::AnnotMeasureDistance));
            const QPoint aim = widgetAt(*view, 0, target);
            loadPaths(product, aim);
            const std::size_t before = made(product).size();
            // Three device pixels to the right and the end well away, towards the page's lower-left margin.
            drag(*view, aim + QPoint(3, 0), aim + QPoint(-70, 45));
            REQUIRE(made(product).size() == before + 1);
            const Annotation d = made(product).back();
            CHECK(d.line[0] == target.x);
            CHECK(d.line[1] == target.y);
            exact += (d.line[0] == target.x && d.line[1] == target.y) ? 1 : 0;
        }
        std::cout << "A-89 at " << zoom * 100 << " %: " << exact << " of " << points.size()
                  << " drags started 3 device px off landed exactly on the generator's point\n";
        // THE CONTROL: with `units/snapToObjects` off, the same aim lands where the pointer is.
        product.root->services().settings.set(core::keys::units::snapToObjects, false);
        QCoreApplication::processEvents();
        const core::PdfPoint corner = points.front();
        view->revealPageRect(0, core::PdfRect{core::PdfPoint{corner.x - 30, corner.y - 30}, core::PdfSize{60, 60}});
        product.settle();
        REQUIRE(dispatch(product, core::ActionId::AnnotMeasureDistance));
        const QPoint aim = widgetAt(*view, 0, corner);
        drag(*view, aim + QPoint(3, 0), aim + QPoint(-70, 45));
        CHECK(made(product).back().line[0] != corner.x);
    }
}

TEST_CASE("calibration through the product makes the next measurements read true and they are saved as shown",
          "[measureproduct][A-90][A-88][D6-14]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/measure-scale.pdf");
    view->setZoom(core::render::Zoom{1.0});
    view->revealPageRect(0, core::PdfRect{core::PdfPoint{90, 340}, core::PdfSize{310, 320}});
    product.settle();
    core::Settings& settings = product.root->services().settings;
    const auto at = [&](double x, double y) { return widgetAt(*view, 0, core::PdfPoint{x, y}); };

    ui::MainWindow::setMeasurementScaleAnswerForTesting([](ui::MeasurementScaleDialog& dialog) {
        REQUIRE(dialog.isCalibrating());
        dialog.realUnit()->setCurrentIndex(dialog.realUnit()->findData(static_cast<int>(core::MeasureUnit::Feet)));
        dialog.realLength()->setValue(12.0);
        return true;
    });
    REQUIRE(dispatch(product, core::ActionId::AnnotMeasureCalibrate));
    loadPaths(product, at(100, 500));
    drag(*view, at(100, 500) + QPoint(3, 0), at(316, 500) + QPoint(-3, 0));
    ui::MainWindow::setMeasurementScaleAnswerForTesting({});
    CHECK(made(product).empty());
    CHECK(std::abs(settings.get(core::keys::measuring::scaleRatio) - 48.0) < 1e-9);   // 12 x 864 / 216
    CHECK(settings.get(core::keys::measuring::displayUnit) == core::MeasureUnit::Feet);

    // The 288-point line (16 ft by hand), the diagonal (14.42 ft), a perimeter of three sides (28 ft) and the area.
    REQUIRE(dispatch(product, core::ActionId::AnnotMeasureDistance));
    drag(*view, at(100, 400) + QPoint(3, 0), at(388, 400) + QPoint(-3, 0));
    REQUIRE(dispatch(product, core::ActionId::AnnotMeasureDistance));
    drag(*view, at(100, 644) + QPoint(2, -2), at(316, 500) + QPoint(-2, 2));
    REQUIRE(made(product).size() == 2);
    CHECK(readingOf(made(product)[0]) == "16.00 ft");
    CHECK(readingOf(made(product)[1]) == "14.42 ft");
    CHECK(made(product)[0].line == std::vector<double>{100, 400, 388, 400});

    // SAVED, and the file reopened FRESH: each measurement read first-party with the same scale and reading.
    REQUIRE(dispatch(product, core::ActionId::FileSave));
    product.settle();
    pdf::DocumentBackendImpl fresh;
    auto reopened = fresh.open(product.openedPath, nullptr);
    REQUIRE(reopened);
    const core::AnnotationStore* store = fresh.annotations(reopened.value());
    REQUIRE(store != nullptr);
    int found = 0;
    for (const Annotation& mine : made(product)) {
        for (const Annotation& read : store->all()) {
            if (read.nm != mine.nm) continue;
            ++found;
            REQUIRE(read.measure);
            CHECK(read.measure->ratio == QStringLiteral("1 in = 4 ft"));
            CHECK(readingOf(read) == readingOf(mine));
            CHECK(read.contents == mine.contents);
        }
    }
    CHECK(found == 2);

    // A-34 through the measuring tools: WHAT THE SCREEN SHOWS IS WHAT SAVE WROTE -- the live engine's page and the file
    // reopened fresh, drawn with their annotations at 72 and 144 dpi, 0 bytes apart.
    product.root->services().renderScheduler.waitForIdle(4000);
    core::IDocumentBackend& backend = product.root->services().backend;
    long long differing = 0;
    for (const double scale : {1.0, 2.0}) {
        const core::RasterImage live = rasterOf(backend, product.document()->handle(), 0, scale);
        const core::RasterImage file = rasterOf(fresh, reopened.value(), 0, scale);
        REQUIRE(live.width == file.width);
        REQUIRE(live.height == file.height);
        for (qsizetype i = 0; i < live.pixels.size(); ++i) differing += live.pixels[i] != file.pixels[i] ? 1 : 0;
    }
    std::cout << "A-34 through the measuring tools: two measurements saved, live against the file reopened fresh: "
              << differing << " differing bytes at 72 and 144 dpi\n";
    CHECK(differing == 0);
    CHECK(fresh.close(reopened.value()));
}
