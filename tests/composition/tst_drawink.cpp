// tst_drawink -- Stage 6 T8, guide case A-34's and A-35's drawing halves and A-63 in pixels
// (`Stage6-Annotations-Testing-Guide.md` §5.6, §5.10): the shapes, the cloud and the pencil made with MOUSE AND KEY
// EVENTS over the product's own composition, saved, and the file reopened in a FRESH engine document.
//
//   * A-34 through the drawing tools: a rectangle, an oval and an arrow dragged; a polygon clicked and finished with
//     `Enter`, a polyline finished with a double click, a cloud; a pencil stroke -- seven gestures, seven commands --
//     saved with *Save*: the live page against the file reopened fresh, 0 differing bytes at 72 and 144 dpi and
//     turned 90 degrees, and each view changed by the edits (rule 1).
//   * A-35: every one undone after the save -- 0 bytes from the original page.
//   * A-63 in pixels: a highlight at 50 % under its own multiply over a line of black text -- the glyphs' ink stays
//     dark and the paper between them goes yellow; THE CONTROL, the same highlight told `Normal` at full opacity
//     on another line, covers the glyphs (the measure can tell the two apart).
//
// WHAT IT DOES NOT COVER (convention 19): a real 150 % monitor (offscreen draws at a device-pixel ratio of 1), any
// reader but PDFium (M-02), and the bar's and the page's own controls (`tests/ui/tst_drawtools`).

#include "tests/composition/AnnotProduct.h"

#include "core/cmd/ActionRegistry.h"
#include "pdf/DocumentBackendImpl.h"
#include "ui/viewer/ToolController.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include <iostream>

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

void drag(ui::PageView& view, QPoint from, QPoint to)
{
    mouse(view, QEvent::MouseButtonPress, from);
    for (int step = 1; step <= 4; ++step) mouse(view, QEvent::MouseMove, from + (to - from) * step / 4);
    mouse(view, QEvent::MouseButtonRelease, to);
}

void click(ui::PageView& view, QPoint pos)
{
    mouse(view, QEvent::MouseButtonPress, pos);
    mouse(view, QEvent::MouseButtonRelease, pos);
}

void key(QWidget* target, int k, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent override(QEvent::ShortcutOverride, k, modifiers);
    override.ignore();
    QCoreApplication::sendEvent(target, &override);
    QKeyEvent press(QEvent::KeyPress, k, modifiers);
    QCoreApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, k, modifiers);
    QCoreApplication::sendEvent(target, &release);
    QCoreApplication::processEvents();
}

bool dispatch(Product& product, core::ActionId id)
{
    const bool done = static_cast<bool>(product.root->services().actions.dispatch(id, core::KeyContext::Viewer));
    product.view->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    return done;
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

} // namespace

TEST_CASE("the drawing tools' shapes and a pencil stroke are shown as they are saved and undone to the page",
          "[drawink][A-34][A-35][A-58]")
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
    const auto show = [&](const core::PdfRect& around) {
        view->revealPageRect(0, around);
        product.settle();
    };
    const auto at = [&](double x, double y) { return widgetAt(*view, 0, core::PdfPoint{x, y}); };

    // Dragged: a rectangle, an oval, an arrow -- near the top of the page.
    show(core::PdfRect{core::PdfPoint{40, 720}, core::PdfSize{520, 100}});
    REQUIRE(dispatch(product, core::ActionId::AnnotRectangle));
    drag(*view, at(60, 800), at(200, 740));
    REQUIRE(dispatch(product, core::ActionId::AnnotOval));
    drag(*view, at(220, 800), at(360, 740));
    REQUIRE(dispatch(product, core::ActionId::AnnotArrow));
    drag(*view, at(380, 740), at(540, 800));
    product.settle();
    REQUIRE(journal.position() == 3);

    // Clicked: a polygon finished by `Enter`, a polyline by a double click, a cloud by `Enter`.
    show(core::PdfRect{core::PdfPoint{40, 560}, core::PdfSize{520, 140}});
    REQUIRE(dispatch(product, core::ActionId::AnnotPolygon));
    for (const auto& [x, y] : {std::pair{60.0, 690.0}, std::pair{170.0, 680.0}, std::pair{120.0, 600.0}}) click(*view, at(x, y));
    key(view, Qt::Key_Return);
    REQUIRE(dispatch(product, core::ActionId::AnnotPolyline));
    for (const auto& [x, y] : {std::pair{200.0, 600.0}, std::pair{240.0, 690.0}, std::pair{290.0, 600.0}}) click(*view, at(x, y));
    {
        const QPoint last = at(340, 690);
        mouse(*view, QEvent::MouseButtonPress, last);
        mouse(*view, QEvent::MouseButtonRelease, last);
        QMouseEvent twice(QEvent::MouseButtonDblClick, QPointF(last), view->viewport()->mapToGlobal(QPointF(last)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &twice);
        mouse(*view, QEvent::MouseButtonRelease, last);
    }
    REQUIRE(dispatch(product, core::ActionId::AnnotCloud));
    for (const auto& [x, y] : {std::pair{380.0, 690.0}, std::pair{540.0, 690.0}, std::pair{540.0, 590.0}, std::pair{380.0, 590.0}})
        click(*view, at(x, y));
    key(view, Qt::Key_Return);
    product.settle();
    REQUIRE(journal.position() == 6);

    // A pencil stroke, a wave across the middle of the page.
    show(core::PdfRect{core::PdfPoint{40, 440}, core::PdfSize{520, 100}});
    REQUIRE(dispatch(product, core::ActionId::AnnotPencil));
    mouse(*view, QEvent::MouseButtonPress, at(60, 490));
    for (int i = 1; i <= 40; ++i) mouse(*view, QEvent::MouseMove, at(60 + i * 12, 490 + 30 * std::sin(i * 0.4)));
    mouse(*view, QEvent::MouseButtonRelease, at(540, 490 + 30 * std::sin(16.0)));
    product.settle();
    REQUIRE(journal.position() == 7);   // seven gestures, seven commands

    std::vector<AnnotationSubtype> kinds;
    for (const Annotation& a : product.store().all())
        if (a.id.kind == core::AnnotationId::Kind::Created) kinds.push_back(a.subtype);
    CHECK(kinds == std::vector<AnnotationSubtype>{AnnotationSubtype::Square, AnnotationSubtype::Circle, AnnotationSubtype::Line,
                                                  AnnotationSubtype::Polygon, AnnotationSubtype::PolyLine,
                                                  AnnotationSubtype::Polygon, AnnotationSubtype::Ink});

    // SAVED, and the file reopened FRESH: the live page and the file's, 0 bytes apart in every view.
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
        std::cout << "A-34 through the drawing tools: seven shapes and strokes saved, live against the file reopened fresh: "
                  << total << " differing bytes over three views\n";
        CHECK(fresh.close(reopened.value()));
    }

    // EVERY ONE UNDONE after the save, and the page is the original.
    while (journal.canUndo()) REQUIRE(journal.undo());
    product.settle();
    product.root->services().renderScheduler.waitForIdle(4000);
    long long fromOriginal = 0;
    for (std::size_t i = 0; i < std::size(kShownViews); ++i)
        fromOriginal += differingBytes(rasterOf(backend, handle, 0, kShownViews[i].scale, kShownViews[i].rotation), original[i]);
    std::cout << "A-35 through the drawing tools: all seven undone after the save: " << fromOriginal << " bytes from the original\n";
    CHECK(fromOriginal == 0);
}

TEST_CASE("a highlight under multiply keeps the text under it dark and tints the paper", "[drawink][A-63]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    view->setZoom(core::render::Zoom{2.0});
    product.settle();

    // Two body lines of the page, from the GENERATOR's content stream.
    std::vector<test_oracle::Line> lines;
    for (const test_oracle::Line& line : test_oracle::contentLines("ordinary/text-a4.pdf", 0))
        if (line.face == test_oracle::Face::HelveticaRegular && line.size <= 12 && line.x1 - line.x0 > 200) lines.push_back(line);
    REQUIRE(lines.size() >= 2);
    const auto quadOver = [](const test_oracle::Line& line) {
        const double bottom = line.baseline - 0.25 * line.size, top = line.baseline + 0.95 * line.size;
        return std::vector<double>{line.x0, top, line.x1, top, line.x0, bottom, line.x1, bottom};
    };
    Annotation multiply = product.overlay()->newAnnotation(AnnotationSubtype::Highlight, 0);
    multiply.quadPoints = quadOver(lines[0]);
    multiply.rect = *core::markupRect(multiply.quadPoints);
    multiply.color = {1.0, 1.0, 0.0};
    multiply.opacity = 0.5;   // its own blend: multiply, written as nothing
    Annotation normal = product.overlay()->newAnnotation(AnnotationSubtype::Highlight, 0);
    normal.quadPoints = quadOver(lines[1]);
    normal.rect = *core::markupRect(normal.quadPoints);
    normal.color = {1.0, 1.0, 0.0};
    normal.blendMode = QStringLiteral("Normal");   // THE CONTROL: covers what it is over
    REQUIRE(product.overlay()->addMade({multiply, normal}));
    product.settle();

    const QImage with = product.page(0, true);
    const QImage without = product.page(0, false);
    struct Measure {
        int glyph = 0, glyphDark = 0, paper = 0, paperTinted = 0;
    };
    const auto measure = [&](const std::vector<double>& quad) {
        Measure m;
        const QRect region = onSheet(*view, 0, *core::boundsOfPoints(quad)).toAlignedRect().adjusted(2, 2, -2, -2);
        for (int y = region.top(); y <= region.bottom(); ++y) {
            for (int x = region.left(); x <= region.right(); ++x) {
                const QRgb before = without.pixel(x, y);
                const QRgb after = with.pixel(x, y);
                if (qGray(before) < 60) {   // a glyph's ink
                    ++m.glyph;
                    if (qGray(after) < 90) ++m.glyphDark;
                } else if (qGray(before) > 245) {   // the paper
                    ++m.paper;
                    if (qBlue(after) < 200 && qRed(after) > 230 && qGreen(after) > 230) ++m.paperTinted;
                }
            }
        }
        return m;
    };
    const Measure under = measure(multiply.quadPoints);
    const Measure covered = measure(normal.quadPoints);
    std::cout << "A-63: under multiply at 50 %: " << under.glyphDark << " of " << under.glyph << " glyph pixels still dark, "
              << under.paperTinted << " of " << under.paper << " paper pixels yellow; the Normal control: "
              << covered.glyphDark << " of " << covered.glyph << " still dark\n";
    REQUIRE(under.glyph > 200);
    REQUIRE(under.paper > 1000);
    CHECK(under.glyphDark >= under.glyph * 99 / 100);    // the text stays dark
    CHECK(under.paperTinted >= under.paper * 99 / 100);  // the paper between is tinted
    REQUIRE(covered.glyph > 200);
    CHECK(covered.glyphDark < covered.glyph / 10);       // the control covers the text: the measure tells them apart
}
