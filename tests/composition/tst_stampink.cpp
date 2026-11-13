// tst_stampink -- Stage 6 T10, guide cases A-34's and A-35's stamp halves, A-77's and A-80's ink halves
// (`Stage6-Annotations-Testing-Guide.md` §5.6, §5.13): stamps placed with MOUSE EVENTS through the palette and the
// stamp tool over the product's own composition, saved, and the file reopened in a FRESH engine document.
//
//   * A-34 through the stamp tool: `Ctrl+Alt+S`'s command arms the tool and shows the palette; a standard stamp
//     and a dynamic one clicked, an image stamp clicked and a JPEG one dragged out -- four gestures, four
//     commands -- saved: the live page against the file reopened fresh, 0 differing bytes at 72 and 144 dpi and
//     turned 90 degrees, each view changed by the stamps. A-35: every one undone after the save, 0 bytes from the
//     original.
//   * A-77, the ink half: the PNG with its alpha and the JPEG, each placed with its rectangle on whole device
//     pixels at one device pixel per image pixel, rendered by the engine and compared with the SOURCE image as Qt
//     decodes it -- the PNG composited over the paper -- at the golden suite's two thresholds (`golden/README.md`):
//     a pixel differs above 8 per channel and at most 0.1 % may, and none is off by more than 96 beyond 0.01 %.
//   * A-80, the ink half: a stamp RESIZED by a handle and TURNED 30 degrees from the properties bar -- one command
//     each -- whose ink lies inside its rectangle and reaches across nearly all of it (rule 6: the rectangle is
//     judged against what is drawn, not against the arithmetic that made it).
//
//   * Stage 7 T13, guide case A-89's ink half (`Stage7-Assembly-Testing-Guide.md` §5.15; D7-36): a stamp made from
//     page 3 of refs-every-kind.pdf through the window -- the backend's page reader, `addCustomStamp`, the palette and
//     the armed tool -- its picture drawn once on the render thread and kept; one click places it at its natural
//     size; and placed at 400 % with its corner on whole device pixels it is drawn as the source page drawn by a fresh
//     engine at the stamp's scale, registered to one device pixel (Stage 7 guide I-21), the source page read by the
//     engine as P-0003 -- and page 4 graded as page 3, the control, fails.
//
// WHAT IT DOES NOT COVER (convention 19): Acrobat and Firefox (M-02, M-07; Stage 7's M-14), a real 150 % monitor
// (offscreen draws at a device-pixel ratio of 1), the custom-stamp dialog's widgets and the library
// (`tests/ui/tst_stamps`, `tests/unit/tst_profilestore`), and the file's bytes (`tests/unit/tst_stamps`). A-89's
// raster is of the stamp UNTURNED and on a blank page: a turned page stamp is graded by its matrix
// (`tests/unit/tst_stamps`' corners), not by its pixels.

#include "tests/composition/AnnotProduct.h"

#include "core/cmd/ActionRegistry.h"
#include "core/doc/Page.h"
#include "core/doc/Stamps.h"
#include "core/settings/ProfileStore.h"
#include "core/text/PageTextModel.h"
#include "pdf/DocumentBackendImpl.h"
#include "ui/dialogs/CustomStampDialog.h"
#include "ui/panes/StampPalettePane.h"
#include "ui/viewer/PropertiesBar.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/tools/StampTool.h"

#include <QMouseEvent>
#include <QSpinBox>

#include <iostream>
#include <numbers>

using namespace rawpage;
using annot_product::Product;
using annot_product::onSheet;
using core::Annotation;
using core::AnnotationSubtype;

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

void mouse(ui::PageView& view, QEvent::Type type, QPoint pos)
{
    QMouseEvent event(type, QPointF(pos), view.viewport()->mapToGlobal(QPointF(pos)), Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &event);
    QCoreApplication::processEvents();
}

void click(ui::PageView& view, QPoint pos)
{
    mouse(view, QEvent::MouseButtonPress, pos);
    mouse(view, QEvent::MouseButtonRelease, pos);
}

void drag(ui::PageView& view, QPoint from, QPoint to)
{
    mouse(view, QEvent::MouseButtonPress, from);
    for (int step = 1; step <= 4; ++step) mouse(view, QEvent::MouseMove, from + (to - from) * step / 4);
    mouse(view, QEvent::MouseButtonRelease, to);
}

bool dispatch(Product& product, core::ActionId id)
{
    const bool done = static_cast<bool>(product.root->services().actions.dispatch(id, core::KeyContext::Viewer));
    product.view->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    return done;
}

QByteArray corpusBytes(const char* relative)
{
    QFile file(test_oracle::corpusPath(relative));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

core::RasterImage rasterOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int page, double scale,
                           core::PageRotation view)
{
    const core::PdfSize size = backend.pages(handle)->at(page)->displaySize();
    const bool swap = core::swapsAxes(view);
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = page;
    request.deviceScale = scale;
    request.revision = core::Revision::initial();
    request.viewRotation = view;
    request.flags = core::render::RenderFlags::Annotations;
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil((swap ? size.height : size.width) * scale)),
                                          static_cast<int>(std::ceil((swap ? size.width : size.height) * scale))};
    auto image = backend.rasterizer(handle)->raster(request);
    REQUIRE(image);
    return image.value();
}

long long differingBytes(const core::RasterImage& a, const core::RasterImage& b)
{
    if (a.width != b.width || a.height != b.height) return std::max(a.pixels.size(), b.pixels.size());
    long long n = 0;
    for (qsizetype i = 0; i < a.pixels.size(); ++i)
        if (a.pixels[i] != b.pixels[i]) ++n;
    return n;
}

struct ShownView {
    double scale;
    core::PageRotation rotation;
    const char* name;
};
const ShownView kShownViews[] = {{1.0, core::PageRotation::None, "72 dpi"},
                                 {2.0, core::PageRotation::None, "144 dpi"},
                                 {1.0, core::PageRotation::Clockwise90, "72 dpi turned 90"}};

// The bounds of every pixel that differs between two rasters of one page -- what the annotations drew.
QRect inkBetween(const QImage& with, const QImage& without)
{
    REQUIRE(with.size() == without.size());
    int left = with.width(), top = with.height(), right = -1, bottom = -1;
    for (int y = 0; y < with.height(); ++y)
        for (int x = 0; x < with.width(); ++x)
            if (with.pixel(x, y) != without.pixel(x, y)) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    return right < 0 ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}

struct Agreement {
    double differing = 0.0;   // the fraction above 8 per channel
    double gross = 0.0;       // the fraction above 96
    int worst = 0;
};

// `drawn` against `source` pixel for pixel; `source` composited over `paper` where it is translucent.
Agreement agreement(const QImage& drawn, QImage source, const QImage& paper)
{
    REQUIRE(drawn.size() == source.size());
    source = source.convertToFormat(QImage::Format_ARGB32);
    long long differing = 0, gross = 0;
    int worst = 0;
    for (int y = 0; y < drawn.height(); ++y) {
        for (int x = 0; x < drawn.width(); ++x) {
            const QRgb s = source.pixel(x, y);
            const QRgb p = paper.pixel(x, y);
            const int a = qAlpha(s);
            const auto over = [a](int c, int under) { return (c * a + under * (255 - a) + 127) / 255; };
            const int want[3] = {over(qRed(s), qRed(p)), over(qGreen(s), qGreen(p)), over(qBlue(s), qBlue(p))};
            const QRgb d = drawn.pixel(x, y);
            const int got[3] = {qRed(d), qGreen(d), qBlue(d)};
            int delta = 0;
            for (int c = 0; c < 3; ++c) delta = std::max(delta, std::abs(want[c] - got[c]));
            worst = std::max(worst, delta);
            if (delta > 8) ++differing;
            if (delta > 96) ++gross;
        }
    }
    const double n = static_cast<double>(drawn.width()) * drawn.height();
    return Agreement{differing / n, gross / n, worst};
}

// Stage 7 T13 (Stage 7 guide I-21, the user's choice): `drawn` against `source`, both opaque, each pixel against the
// NEAREST of the source's 3 x 3 around it -- a glyph the engine places a row away is not seen, two rows is. `differing`
// above 16 per channel, `gross` above 96.
Agreement registered(const QImage& drawn, const QImage& source)
{
    REQUIRE(drawn.size() == source.size());
    long long differing = 0, gross = 0;
    int worst = 0;
    for (int y = 0; y < drawn.height(); ++y) {
        for (int x = 0; x < drawn.width(); ++x) {
            const QRgb d = drawn.pixel(x, y);
            int best = 256;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int sx = x + dx, sy = y + dy;
                    if (sx < 0 || sy < 0 || sx >= source.width() || sy >= source.height()) continue;
                    const QRgb s = source.pixel(sx, sy);
                    best = std::min(best, std::max({std::abs(qRed(d) - qRed(s)), std::abs(qGreen(d) - qGreen(s)),
                                                    std::abs(qBlue(d) - qBlue(s))}));
                }
            }
            worst = std::max(worst, best);
            if (best > 16) ++differing;
            if (best > 96) ++gross;
        }
    }
    const double n = static_cast<double>(drawn.width()) * drawn.height();
    return Agreement{differing / n, gross / n, worst};
}

} // namespace

TEST_CASE("stamps placed with the tool are shown as they are saved and undone to the page", "[stampink][A-34][A-35][A-75]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    core::IDocumentBackend& backend = product.root->services().backend;
    const core::DocumentHandle handle = product.document()->handle();
    core::CommandJournal& journal = *product.document()->journal();
    product.root->services().renderScheduler.waitForIdle(4000);
    std::vector<core::RasterImage> original;
    for (const ShownView& v : kShownViews) original.push_back(rasterOf(backend, handle, 0, v.scale, v.rotation));
    view->revealPageRect(0, core::PdfRect{core::PdfPoint{40, 560}, core::PdfSize{520, 260}});
    product.settle();
    const auto at = [&](double x, double y) { return widgetAt(*view, 0, core::PdfPoint{x, y}); };

    // `Ctrl+Alt+S`'s command: the tool armed and the palette shown.
    REQUIRE(dispatch(product, core::ActionId::AnnotStamp));
    CHECK(view->tools().armedAnnotation() == ui::AnnotationToolKind::Stamp);
    ui::StampPalettePane* palette = product.window->stampPalette();
    REQUIRE(palette != nullptr);
    CHECK(palette->isVisible());

    REQUIRE(palette->choose(QStringLiteral("standard:NotApproved")));
    click(*view, at(150, 780));
    REQUIRE(dispatch(product, core::ActionId::AnnotStamp));
    REQUIRE(palette->choose(QStringLiteral("dynamic:DynamicReviewed")));
    click(*view, at(420, 780));
    // An image stamp: the palette's choice of a custom stamp, made here from the corpus PNG (this composition has no
    // library -- `Options::stampRoot` -- which `tests/ui/tst_stamps` drives).
    auto seal = ui::decodeStampImage(corpusBytes("stamps/seal-rgba.png"));
    REQUIRE(seal);
    REQUIRE(dispatch(product, core::ActionId::AnnotStamp));
    ui::setCurrentStamp(ui::StampChoice{ui::StampChoice::Kind::Custom, QStringLiteral("Seal"), QString(), seal.value()});
    click(*view, at(150, 640));
    auto signature = ui::decodeStampImage(corpusBytes("stamps/signature.jpg"));
    REQUIRE(signature);
    REQUIRE(dispatch(product, core::ActionId::AnnotStamp));
    ui::setCurrentStamp(ui::StampChoice{ui::StampChoice::Kind::Custom, QStringLiteral("Signature"), QString(), signature.value()});
    drag(*view, at(300, 680), at(540, 590));
    product.settle();
    REQUIRE(journal.position() == 4);   // four gestures, four commands

    std::vector<Annotation> made;
    for (const Annotation& a : product.store().all())
        if (a.id.kind == core::AnnotationId::Kind::Created) made.push_back(a);
    REQUIRE(made.size() == 4);
    CHECK(made[0].icon == QStringLiteral("NotApproved"));
    CHECK(made[1].icon == QStringLiteral("DynamicReviewed"));
    CHECK(made[1].contents.startsWith(QString::fromLatin1(annot_product::kLoginName)));   // baked: who placed it
    CHECK(made[2].intent == QStringLiteral("StampImage"));
    CHECK(made[3].image.encoding == core::StampImage::Encoding::Jpeg);
    // The JPEG dragged out: fitted into the box dragged, 240 by 90 points, keeping its 160:60 proportions.
    CHECK(std::abs(made[3].rect.size.width - 240.0) < 2.0);
    CHECK(std::abs(made[3].rect.size.height - 90.0) < 2.0);

    REQUIRE(dispatch(product, core::ActionId::FileSave));
    product.settle();
    REQUIRE_FALSE(journal.isDirty());
    product.root->services().renderScheduler.waitForIdle(4000);
    {
        pdf::DocumentBackendImpl fresh;
        auto reopened = fresh.open(product.openedPath, nullptr);
        REQUIRE(reopened);
        long long total = 0;
        for (std::size_t i = 0; i < std::size(kShownViews); ++i) {
            INFO(kShownViews[i].name);
            const core::RasterImage live = rasterOf(backend, handle, 0, kShownViews[i].scale, kShownViews[i].rotation);
            const core::RasterImage file = rasterOf(fresh, reopened.value(), 0, kShownViews[i].scale, kShownViews[i].rotation);
            const long long differing = differingBytes(live, file);
            CHECK(differing == 0);
            CHECK(differingBytes(live, original[i]) > 0);
            total += differing;
        }
        std::cout << "A-34 through the stamp tool: four stamps saved, live against the file reopened fresh: " << total
                  << " differing bytes over three views\n";
        CHECK(fresh.close(reopened.value()));
    }

    while (journal.canUndo()) REQUIRE(journal.undo());
    product.settle();
    product.root->services().renderScheduler.waitForIdle(4000);
    long long fromOriginal = 0;
    for (std::size_t i = 0; i < std::size(kShownViews); ++i)
        fromOriginal += differingBytes(rasterOf(backend, handle, 0, kShownViews[i].scale, kShownViews[i].rotation), original[i]);
    std::cout << "A-35 through the stamp tool: all four undone after the save: " << fromOriginal << " bytes from the original\n";
    CHECK(fromOriginal == 0);
}

TEST_CASE("an image stamp is drawn as its source image at one pixel a pixel", "[stampink][A-77]")
{
    // US Letter, 612 by 792 POINTS: a page of whole points rasterizes at one pixel a point with no stretch -- an A4
    // page, 841.89 points tall, is drawn into 842 rows and every image on it resampled (measured: 18 % of the
    // signature's pixels off when it was tried).
    Product product;
    ui::PageView* view = product.open("ordinary/text-letter.pdf");
    // ONE DEVICE PIXEL A POINT: what the view's zoom presents at 100 %, undone -- so an image placed at its natural
    // size (one point a pixel) is drawn one device pixel a pixel, and resampling cannot excuse a difference.
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    const double presented = view->layoutEngine().layout().presentedScale();
    view->setZoom(core::render::Zoom{1.0 / presented});
    product.settle();
    REQUIRE(std::abs(view->layoutEngine().layout().presentedScale() - 1.0) < 1e-9);

    // Each on WHOLE points -- whole device pixels -- and over the page's own drawing where it has any: the source is
    // composited over the page as drawn without its annotations.
    struct Case {
        const char* file;
        core::PdfPoint corner;
    };
    for (const Case c : {Case{"stamps/seal-rgba.png", core::PdfPoint{60, 60}}, Case{"stamps/signature.jpg", core::PdfPoint{260, 60}}}) {
        INFO(c.file);
        const QByteArray bytes = corpusBytes(c.file);
        auto image = ui::decodeStampImage(bytes);
        REQUIRE(image);
        auto made = core::newImageStamp(image.value(), QString(), 0, core::PdfPoint{0, 0}, QStringLiteral("A-77"), 1791106200);
        REQUIRE(made);
        Annotation stamp = made.value();
        stamp.rect = core::PdfRect{c.corner, core::PdfSize{static_cast<double>(image.value().width), static_cast<double>(image.value().height)}};
        REQUIRE(product.overlay()->addMade({stamp}));
        product.settle();

        const QImage with = product.page(0, true);
        const QImage without = product.page(0, false);
        const QRectF where = onSheet(*view, 0, stamp.rect);
        const QRect pixels(static_cast<int>(std::lround(where.left())), static_cast<int>(std::lround(where.top())),
                           image.value().width, image.value().height);
        REQUIRE(std::abs(where.width() - image.value().width) < 1e-6);
        REQUIRE(std::abs(where.left() - std::round(where.left())) < 1e-6);
        REQUIRE(std::abs(where.top() - std::round(where.top())) < 1e-6);
        const QImage source = QImage::fromData(bytes);
        const Agreement a = agreement(with.copy(pixels), source, without.copy(pixels));
        std::cout << "A-77 " << c.file << ": " << a.differing * 100.0 << " % of its pixels above 8, " << a.gross * 100.0
                  << " % above 96, the worst " << a.worst << "\n";
        CHECK(a.differing <= 0.001);
        CHECK(a.gross <= 0.0001);
    }
}

TEST_CASE("a stamp resized by a handle and turned from the bar keeps its ink inside its rectangle", "[stampink][A-80]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    core::CommandJournal& journal = *product.document()->journal();
    view->revealPageRect(0, core::PdfRect{core::PdfPoint{100, 50}, core::PdfSize{400, 250}});
    product.settle();

    auto made = core::newStandardStamp(QStringLiteral("Approved"), 0, core::PdfPoint{300, 170}, QStringLiteral("A-80"), 1791106200);
    REQUIRE(made);
    REQUIRE(product.overlay()->addMade({made.value()}));
    product.settle();
    const core::AnnotationId id = made.value().id;
    const auto current = [&]() { return product.store().all()[*product.store().find(id)]; };

    // RESIZED by its bottom-right handle, the Selection tool's: selected with a click, then the corner dragged out.
    const auto widgetRect = [&]() {
        const std::optional<QRectF> r = view->widgetRectOf(0, current().rect);
        REQUIRE(r);
        return *r;
    };
    click(*view, widgetRect().center().toPoint());
    REQUIRE(product.overlay()->selection() == std::vector<core::AnnotationId>{id});
    const QPoint corner = widgetRect().bottomRight().toPoint();
    drag(*view, corner, corner + QPoint(60, 20));
    product.settle();
    REQUIRE(journal.position() == 2);   // placed, resized: one command each
    CHECK(current().rect.size.width > made.value().rect.size.width + 40.0);

    // THE UNTURNED INK, measured -- what the turned ink is held to below, so the rectangle is judged against the
    // ink and not against the arithmetic that made it (rule 6).
    const QRect unturned = inkBetween(product.page(0, true), product.page(0, false));
    REQUIRE_FALSE(unturned.isNull());

    // TURNED 30 degrees from the properties bar -- the turn is the bar's, one command.
    if (!product.document()->propertiesBarShown()) product.document()->togglePropertiesBar();
    ui::PropertiesBar* bar = product.document()->propertiesBar();
    REQUIRE(bar != nullptr);
    REQUIRE(bar->rotationBox()->isEnabled());
    bar->rotationBox()->setValue(30);
    product.settle();
    REQUIRE(journal.position() == 3);
    CHECK(current().rotate == 30);

    // THE INK, against the rectangle: inside it (a pixel's rounding allowed), and reaching across nearly all of it.
    const QImage with = product.page(0, true);
    const QImage without = product.page(0, false);
    const QRect ink = inkBetween(with, without);
    const QRectF rect = onSheet(*view, 0, current().rect);
    std::cout << "A-80: the turned stamp's ink " << ink.width() << " x " << ink.height() << " px inside its rectangle "
              << rect.width() << " x " << rect.height() << " px\n";
    REQUIRE_FALSE(ink.isNull());
    CHECK(ink.left() >= std::floor(rect.left()) - 1);
    CHECK(ink.top() >= std::floor(rect.top()) - 1);
    CHECK(ink.right() <= std::ceil(rect.right()) + 1);
    CHECK(ink.bottom() <= std::ceil(rect.bottom()) + 1);
    CHECK(ink.width() >= 0.9 * rect.width());
    CHECK(ink.height() >= 0.9 * rect.height());
    // AND NOT SQUEEZED: a form's turned box is mapped onto WHATEVER rectangle it has (ISO 32000-1 Algorithm 8.1), so
    // ink inside the rectangle proves nothing alone -- a turn that kept the unturned rectangle passed every check above
    // (planted P7). The turned ink is the MEASURED unturned ink turned: a rounded rectangle w by h turned 30 degrees
    // spans w cos + h sin less 2 r (cos + sin - 1), r its outer corner radius -- the border's corner and half its
    // stroke, in pixels at the scale the unturned ink was drawn at -- within 2 pixels.
    const auto natural = core::stampNaturalSize(current());
    REQUIRE(natural);
    const double c = std::cos(std::numbers::pi / 6), sn = std::sin(std::numbers::pi / 6);
    const double w0 = unturned.width(), h0 = unturned.height();
    const double perPoint = w0 / natural.value().width;
    const double cornerLoss = 2.0 * (core::kStampCorner + core::kStampBorder / 2.0) * perPoint * (c + sn - 1.0);
    const double wantWidth = w0 * c + h0 * sn - cornerLoss, wantHeight = w0 * sn + h0 * c - cornerLoss;
    std::cout << "A-80: unturned ink " << w0 << " x " << h0 << " px; turned, expected " << wantWidth << " x " << wantHeight
              << " px\n";
    CHECK(std::abs(ink.width() - wantWidth) <= 2.0);
    CHECK(std::abs(ink.height() - wantHeight) <= 2.0);

    // And each undone in turn: the turn, then the resize.
    REQUIRE(journal.undo());
    CHECK(current().rotate == 0);
    REQUIRE(journal.undo());
    CHECK(std::abs(current().rect.size.width - made.value().rect.size.width) < 1e-6);
}

TEST_CASE("stamps.page_source: a stamp made from a PDF page through the window is drawn as the page at its scale",
          "[stampink][A-89][D7-36]")
{
    // A BLANK US Letter page -- whole points, and nothing under the stamp for its transparent ground to show -- made by
    // the copier, so the page under test is the product's own blank page and not a fixture of this case.
    Product product([](app::CompositionRoot::Options& options, const QString& scratch) {
        options.stampRoot = QDir(scratch).filePath(QStringLiteral("stamps"));
    });
    core::IDocumentBackend& backend = product.root->services().backend;
    auto blank = backend.blankPages(core::PdfSize{612.0, 792.0}, 1);
    REQUIRE(blank);
    const QString target = QDir(product.scratch.path()).filePath(QStringLiteral("blank-letter.pdf"));
    {
        QFile file(target);
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write(blank.value().graph) == blank.value().graph.size());
    }
    ui::PageView* view = product.openFile(target);
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    const double presented = view->layoutEngine().layout().presentedScale();
    view->setZoom(core::render::Zoom{1.0 / presented});
    product.settle();
    REQUIRE(std::abs(view->layoutEngine().layout().presentedScale() - 1.0) < 1e-9);

    // MADE AS THE DIALOG MAKES IT: the window's own page reader -- the backend's `stampPage` -- then `addCustomStamp`,
    // which keeps it in the library, chooses it in the palette and arms the tool, and draws its picture once.
    const QString source = test_oracle::corpusPath("assembly/refs-every-kind.pdf");
    auto page = backend.stampPage(source, 2);
    REQUIRE(page);
    ui::CustomStampDialog::Made made;
    made.name = QStringLiteral("P-0003");
    made.category = QStringLiteral("Pages");
    made.page = page.value();
    made.sourceName = QStringLiteral("refs-every-kind.pdf");
    REQUIRE(product.window->addCustomStamp(made));
    core::ProfileStore* library = product.root->services().stamps;
    REQUIRE(library != nullptr);
    REQUIRE(library->stamps().size() == 1);
    const QString id = library->stamps().front().id;
    CHECK(product.window->stampPalette()->chosenKey() == QStringLiteral("custom:") + id);
    CHECK(view->tools().armedAnnotation() == ui::AnnotationToolKind::Stamp);
    CHECK_FALSE(ui::currentStamp().page.isEmpty());
    // ITS PICTURE, drawn on the render thread and kept beside the page: 128 pixels on its longer side.
    QElapsedTimer timer;
    timer.start();
    while (!library->previewBytes(id) && timer.elapsed() < 30000) product.settle();
    REQUIRE(library->previewBytes(id).has_value());
    const QImage picture = QImage::fromData(*library->previewBytes(id));
    CHECK(picture.height() == core::kThumbnailEdge);
    CHECK(std::abs(picture.width() - core::kThumbnailEdge * 595.276 / 841.89) <= 1.0);

    // A CLICK of the armed tool places it: ONE command, a page stamp at its natural size.
    view->revealPageRect(0, core::PdfRect{core::PdfPoint{100, 300}, core::PdfSize{400, 400}});
    product.settle();
    core::CommandJournal& journal = *product.document()->journal();
    click(*view, widgetAt(*view, 0, core::PdfPoint{300, 500}));
    product.settle();
    REQUIRE(journal.position() == 1);
    std::vector<Annotation> placed;
    for (const Annotation& a : product.store().all())
        if (a.id.kind == core::AnnotationId::Kind::Created) placed.push_back(a);
    REQUIRE(placed.size() == 1);
    CHECK(core::stampKind(placed.front()) == core::StampKind::Page);
    CHECK(placed.front().subject == QStringLiteral("P-0003"));
    CHECK(std::abs(placed.front().rect.size.height - core::kImageStampMaxSide) < 1e-6);
    REQUIRE(journal.undo());
    product.settle();

    // GRADED ON THE RASTER, AT 400 %: the stamp on WHOLE device pixels, against the source page drawn by a fresh engine
    // into the same pixels -- REGISTERED to one device pixel (the user's choice, 2026-11-13; Stage 7 guide I-21): each
    // pixel of the stamp against the nearest of the source's 3 x 3 around it, at most 1 % of them above 16 per channel
    // and NONE above 96. The golden suite's own thresholds (`golden/README.md`) are printed beside it every run and not
    // graded: PDFium draws the page's large identity ONE DEVICE ROW from where it draws it on the page, a mechanism not
    // established. The registration does not see a glyph one row off; it sees two, a missing glyph, a wrong colour --
    // and, at 400 %, a wrong WEIGHT: graded at 100 %, the page's fonts lost (planted P1 -- PDFium then sets the text in
    // its stock Helvetica, regular for bold) stayed inside a pixel's registration; at 400 % the same plant is red. The
    // engine draws a page into a WHOLE number of pixels each way
    // (`PdfiumRasterizer`: the page's size times the scale, rounded up), so the page at the stamp's scale is 611 by 864
    // pixels for a natural box of 152.73 by 216 points: the stamp is placed at its natural size ROUNDED to those whole
    // pixels -- 152.75 points, 0.01 % wider -- so both draws make one transform of the page. Placed at 152.73 the
    // comparison measured that rounding, not the stamp (the first run, 2026-11-13).
    constexpr double kZoom = 4.0;
    view->setZoom(core::render::Zoom{kZoom / presented});
    product.settle();
    REQUIRE(std::abs(view->layoutEngine().layout().presentedScale() - kZoom) < 1e-9);
    auto stamp = core::newPageStamp(page.value(), QStringLiteral("P-0003"), 0, core::PdfPoint{0, 0}, QStringLiteral("A-89"), 1791106200);
    REQUIRE(stamp);
    Annotation at = stamp.value();
    const core::PdfSize natural = at.rect.size;
    CHECK(std::abs(natural.width - 152.734) < 0.01);
    const double scale = (kZoom * natural.height - 1e-9) / 841.89;   // a hair under, so `ceil` gives 864 rows, not 865
    const int columns = static_cast<int>(std::ceil(595.276 * scale));
    const int rows = static_cast<int>(std::ceil(841.89 * scale));
    REQUIRE(columns == 611);
    REQUIRE(rows == 864);
    at.rect = core::PdfRect{core::PdfPoint{100.0, 400.0}, core::PdfSize{columns / kZoom, rows / kZoom}};
    REQUIRE(product.overlay()->addMade({at}));
    product.settle();
    const QImage with = product.page(0, true);
    const QImage without = product.page(0, false);
    const QRectF where = onSheet(*view, 0, at.rect);
    REQUIRE(std::abs(where.left() - std::round(where.left())) < 1e-6);
    REQUIRE(std::abs(where.top() - std::round(where.top())) < 1e-6);
    const QRect pixels(static_cast<int>(std::lround(where.left())), static_cast<int>(std::lround(where.top())), columns, rows);

    pdf::DocumentBackendImpl fresh;
    auto opened = fresh.open(source, nullptr);
    REQUIRE(opened);
    core::render::RenderRequest request;
    request.document = opened.value();
    request.pageIndex = 2;
    request.deviceScale = scale;
    request.revision = core::Revision::initial();
    request.flags = core::render::RenderFlags::None;
    request.tile = core::render::TileRect{0, 0, columns, rows};
    auto drawn = fresh.rasterizer(opened.value())->raster(request);
    REQUIRE(drawn);
    const QImage sourceImage = ui::holdRaster(drawn.value()).image.copy().convertToFormat(QImage::Format_ARGB32);
    // The page the stamp was made from SHOWS P-0003 -- read by the engine, not by our page map.
    core::text::PageTextModel text;
    REQUIRE(fresh.textExtractor(opened.value())->extractPage(opened.value(), 2, text));
    CHECK(text.text().contains(QStringLiteral("P-0003")));
    CHECK(fresh.close(opened.value()));

    CHECK(with.copy(pixels) != without.copy(pixels));   // the stamp drew something there
    const Agreement strict = agreement(with.copy(pixels), sourceImage, without.copy(pixels));
    const Agreement a = registered(with.copy(pixels), sourceImage);
    std::cout << "A-89: page 3 as a stamp, " << columns << " x " << rows << " px against the page drawn at " << scale
              << ": registered to a pixel, " << a.differing * 100.0 << " % above 16, " << a.gross * 100.0
              << " % above 96, the worst " << a.worst << "; unregistered (I-21, not graded), " << strict.differing * 100.0
              << " % above 8, " << strict.gross * 100.0 << " % above 96\n";
    CHECK(a.differing <= 0.01);
    CHECK(a.gross == 0.0);

    // THE CONTROL (convention 7): the registered comparison against the WRONG page -- page 4, P-0004 -- must fail it.
    pdf::DocumentBackendImpl other;
    auto otherOpened = other.open(source, nullptr);
    REQUIRE(otherOpened);
    request.document = otherOpened.value();
    request.pageIndex = 3;
    auto wrong = other.rasterizer(otherOpened.value())->raster(request);
    REQUIRE(wrong);
    const Agreement control = registered(with.copy(pixels), ui::holdRaster(wrong.value()).image.copy().convertToFormat(QImage::Format_ARGB32));
    std::cout << "A-89's control, page 4 for page 3: " << control.differing * 100.0 << " % above 16, " << control.gross * 100.0
              << " % above 96\n";
    CHECK(control.gross > 0.0);
    CHECK(other.close(otherOpened.value()));
}
