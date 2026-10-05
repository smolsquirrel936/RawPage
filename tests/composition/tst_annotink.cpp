// tst_annotink -- Stage 6 T5, guide cases A-41 and A-48 (`Stage6-Annotations-Testing-Guide.md` §5.7), and
// D6-2's *nothing flashes*: the annotation overlay over the PRODUCT'S OWN COMPOSITION, graded against the
// RENDERED INK (rule 6) -- the engine's pixels, located by this file and by nothing the overlay computes.
//
//   * **A-41.** For every annotation of `annots-every-type.pdf` and of each of `annots-rotated.pdf`'s four
//     pages, at two device scales (1.0 and 1.5 pixels a point) and a view scrolled by an odd number of
//     pixels: its INK is the difference between the page drawn with annotations and without, through the
//     shipped scheduler; a point INSIDE that ink selects it, and a point three device pixels OUTSIDE it
//     does not. For a kind drawn as a BOX -- a note's icon, a stamp, free text, a caret, an attachment's
//     pin, a sound -- and for TEXT MARKUP, "outside" is three pixels past the box or the quadrilateral as
//     well as past the ink: D6-30 hits those over what they mark, and the case says so rather than
//     grading them against a rule the product does not claim. For a SHAPE -- a rectangle, an oval, a line,
//     a polygon, a polyline, ink -- the outside point is three pixels beside the stroke, which is inside
//     the rectangle: a hit test that answered for rectangles fails there.
//   * **A-48.** With an annotation selected (outline and handles drawn), a snapshot over it equals a fresh
//     render of the region within the golden thresholds -- and the control: the VIEW there is not.
//   * **The hold.** An edit's first paint shows the superseded tiles where the new ones are still coming,
//     and no white sheet; once they arrive none is held.
//
// WHAT THIS DOES NOT COVER (convention 19): a real monitor at 150 % -- offscreen draws at a device-pixel
// ratio of 1, so "a device scale of 1.5" here is the zoom's, not the screen's (M-01 walks both monitors);
// the pointer itself (events are sent to the viewport); and every document is OURS (D6-26).

#include <catch2/catch_test_macros.hpp>

#include "app/CompositionRoot.h"
#include "tests/composition/AnnotProduct.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/Document.h"
#include "core/render/RenderScheduler.h"
#include "tests/render/TextOracle.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/HeldRaster.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/TilePresenter.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/ViewportController.h"

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
#include <QLayout>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>

using namespace rawpage;
using core::Annotation;
using core::AnnotationSubtype;
using annot_product::Product;
using annot_product::differingPixels;
using annot_product::onSheet;
using annot_product::toWidget;

namespace {

// Graded as a BOX -- outside means past what it marks as well as past its ink (D6-30): text markup, every
// kind drawn as a box, and a cloud, whose bumps the model does not hold.
bool isBox(const Annotation& a)
{
    if (a.subtype == AnnotationSubtype::Polygon && a.rectDifferences.size() == 4) return true;   // a cloud
    switch (a.subtype) {
    case AnnotationSubtype::Square:
    case AnnotationSubtype::Circle:
    case AnnotationSubtype::Line:
    case AnnotationSubtype::Polygon:
    case AnnotationSubtype::PolyLine:
    case AnnotationSubtype::Ink:
        return false;
    default:
        return true;   // a box, or text markup: hit over what it marks (D6-30)
    }
}

struct Graded {
    int annotations = 0;
    int inside = 0;
    int outside = 0;
    int besideStroke = 0;   // outside points INSIDE the rectangle -- the ones a rectangle test would fail
};

// A-41 over one page as the view shows it now.
void gradePage(Product& product, int pageIndex, Graded& graded)
{
    ui::PageView& view = *product.view;
    const QImage with = product.page(pageIndex, true);
    const QImage without = product.page(pageIndex, false);
    REQUIRE(with.size() == without.size());
    const int w = with.width();
    const int h = with.height();
    std::vector<char> ink(static_cast<std::size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        const auto* a = reinterpret_cast<const QRgb*>(with.constScanLine(y));
        const auto* b = reinterpret_cast<const QRgb*>(without.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int delta = std::max({std::abs(qRed(a[x]) - qRed(b[x])), std::abs(qGreen(a[x]) - qGreen(b[x])),
                                        std::abs(qBlue(a[x]) - qBlue(b[x]))});
            ink[static_cast<std::size_t>(y) * w + x] = delta > 24 ? 1 : 0;
        }
    }
    const auto isInk = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && ink[static_cast<std::size_t>(y) * w + x] != 0; };

    const core::AnnotationStore& store = product.document()->journal()->document().annotations();
    const ui::AnnotationOverlay::Viewport viewport = view.annotationViewport();
    std::vector<std::pair<const Annotation*, QRectF>> drawn;
    for (const std::size_t index : store.onPage(pageIndex)) {
        const Annotation& a = store.at(index);
        if (a.subtype == AnnotationSubtype::Popup || a.hasFlag(core::AnnotationFlag::Hidden)
            || a.hasFlag(core::AnnotationFlag::NoView))
            continue;
        // WHERE IT IS DRAWN -- a `NoRotate` note is upright at its corner on a turned page (`drawnRect`); its
        // ink, found below, is what says whether that is true.
        const std::optional<core::PdfRect> at = ui::AnnotationOverlay::drawnRect(viewport, a);
        REQUIRE(at);
        drawn.emplace_back(&a, onSheet(view, pageIndex, *at));
    }
    const auto nearAnother = [&](const Annotation* self, double x, double y, double margin) {
        for (const auto& [other, r] : drawn) {
            if (other != self && r.adjusted(-margin, -margin, margin, margin).contains(QPointF(x, y))) return true;
        }
        return false;
    };
    const double scale = viewport.layout->presentedScale();

    for (const auto& [a, rect] : drawn) {
        INFO(core::annotationSubtypeName(a->subtype).data() << " " << a->id.toString().toStdString() << " on page "
                                                            << pageIndex + 1 << " at " << scale << " px/pt");
        // Its own ink: inside its rectangle (with a margin for heads and clouds), and nobody else's.
        const QRectF region = rect.adjusted(-8, -8, 8, 8);
        std::vector<QPoint> own;
        for (int y = std::max(0, static_cast<int>(region.top())); y < std::min(h, static_cast<int>(region.bottom()) + 1); ++y) {
            for (int x = std::max(0, static_cast<int>(region.left())); x < std::min(w, static_cast<int>(region.right()) + 1); ++x) {
                if (isInk(x, y) && !nearAnother(a, x, y, 3)) own.emplace_back(x, y);
            }
        }
        REQUIRE_FALSE(own.empty());   // drawn at all: an annotation with no ink tests nothing
        ++graded.annotations;
        const auto ownInk = [&](int x, int y) {
            return isInk(x, y) && region.contains(QPointF(x, y)) && !nearAnother(a, x, y, 3);
        };

        // INSIDE: the ink pixel nearest the ink's centroid whose neighbours are ink too -- the ink's own interior,
        // not an anti-aliased edge, and on a line its middle rather than the head a row-major scan meets first.
        double cx = 0.0, cy = 0.0;
        for (const QPoint& p : own) {
            cx += p.x();
            cy += p.y();
        }
        cx /= static_cast<double>(own.size());
        cy /= static_cast<double>(own.size());
        QPoint inside = own.front();
        double best = 1e18;
        bool interior = false;
        for (const QPoint& p : own) {
            const bool solid = ownInk(p.x() - 1, p.y()) && ownInk(p.x() + 1, p.y()) && ownInk(p.x(), p.y() - 1)
                               && ownInk(p.x(), p.y() + 1);
            const double d = std::hypot(p.x() - cx, p.y() - cy);
            if ((solid && !interior) || (solid == interior && d < best)) {
                inside = p;
                best = d;
                interior = interior || solid;
            }
        }
        CHECK(product.overlay()->annotationAt(viewport, toWidget(view, pageIndex, inside.x(), inside.y())) == a->id);
        ++graded.inside;

        // OUTSIDE: three device pixels past the ink along the inside point's row -- to the right, else the
        // left -- and, for a box or text markup, past the box as well (D6-30).
        // CLEAR BY THREE WHOLE PIXELS: a press is read at its pixel's CORNER (`annotationAt` takes the widget's
        // integer position), so an ink pixel two indices away is two pixels away, not three. The first run of
        // this case measured that, as a "miss" three pixels out that was a hit two pixels out.
        const auto clearOfInk = [&](int x, int y) {
            for (int dy = -3; dy <= 3; ++dy)
                for (int dx = -3; dx <= 3; ++dx)
                    if (ownInk(x + dx, y + dy)) return false;
            return x >= 0 && y >= 0 && x < w && y < h && !nearAnother(a, x, y, 4);
        };
        // "Three pixels outside" is a CLEARANCE: the first point along the row with no ink of its own within three
        // pixels in any direction -- a shallow line's neighbouring rows reach past its row's last pixel.
        std::optional<QPoint> outside;
        for (const int direction : {+1, -1}) {
            int x = inside.x();
            const int limit = direction > 0 ? static_cast<int>(region.right()) + 24 : static_cast<int>(region.left()) - 24;
            if (isBox(*a)) {
                x = direction > 0 ? static_cast<int>(std::ceil(rect.right())) + 3 : static_cast<int>(std::floor(rect.left())) - 3;
            }
            while (x != limit && !clearOfInk(x, inside.y())) x += direction;
            if (x != limit && clearOfInk(x, inside.y())) {
                outside = QPoint(x, inside.y());
                break;
            }
        }
        if (!outside) continue;   // hemmed in on both sides by another annotation's margin: nothing fair to ask
        INFO("inside sheet px " << inside.x() << "," << inside.y() << " outside " << outside->x() << "," << outside->y()
                                << " box " << rect.left() << "," << rect.top() << " " << rect.width() << "x" << rect.height());
        CHECK_FALSE(product.overlay()->annotationAt(viewport, toWidget(view, pageIndex, outside->x(), outside->y())) == a->id);
        ++graded.outside;
        if (rect.contains(QPointF(*outside))) ++graded.besideStroke;
    }
}

} // namespace

TEST_CASE("an annotation is hit where its ink is and not three pixels beside it", "[annotink][A-41][D6-30]")
{
    Graded graded;
    for (const double zoom : {1.0, 1.5}) {
        Product product;
        for (const char* file : {"ordinary/annots-every-type.pdf", "ordinary/annots-rotated.pdf"}) {
            INFO("file " << file << " zoom " << zoom);
            ui::PageView* view = product.open(file);
            view->setZoom(core::render::Zoom{zoom});
            product.settle();
            const int pages = view->pageCount();
            for (int page = 0; page < pages; ++page) {
                view->goToPage(page);
                // AN ODD OFFSET (`CLAUDE.md`'s 150 % trap, in the form offscreen can show): the view scrolled by an
                // odd number of logical pixels, so no edge lands where an even layout would put it.
                view->horizontalScrollBar()->setValue(view->horizontalScrollBar()->value() + 3);
                view->verticalScrollBar()->setValue(view->verticalScrollBar()->value() + 7);
                product.settle();
                gradePage(product, page, graded);
            }
        }
    }
    std::cout << "A-41: " << graded.annotations << " annotations graded, " << graded.inside << " inside and "
              << graded.outside << " outside points, " << graded.besideStroke
              << " of them beside a stroke inside its rectangle\n";
    // annots-every-type's 20 drawn and annots-rotated's 4 a page over 4 pages, at two scales.
    CHECK(graded.annotations >= 2 * (20 + 16));
    CHECK(graded.outside >= graded.annotations * 3 / 4);
    CHECK(graded.besideStroke >= 2 * 4);   // the shapes' half -- what a rectangle hit test could not pass
}

TEST_CASE("a selected annotation's outline and handles are not in a snapshot", "[annotink][A-48]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/annots-every-type.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    const core::AnnotationStore& store = product.document()->journal()->document().annotations();
    std::optional<core::AnnotationId> square;
    for (const Annotation& a : store.all()) {
        if (a.subtype == AnnotationSubtype::Square) square = a.id;
    }
    REQUIRE(square);
    const Annotation& a = store.at(*store.find(*square));
    view->goToPage(a.page);
    product.settle();
    product.overlay()->select({*square});
    product.settle();
    const std::optional<QRectF> drawnAt = ui::AnnotationOverlay::widgetRectOf(view->annotationViewport(), a.page, a.rect);
    REQUIRE(drawnAt);
    const QRect band = drawnAt->adjusted(-12, -12, 12, 12).toAlignedRect();
    REQUIRE(view->viewport()->rect().contains(band));

    // THE CONTROL: the view over the band shows the selection -- the handles are painted there.
    const QImage grab = view->viewport()->grab().toImage().copy(band).convertToFormat(QImage::Format_RGB32);
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::ToolSnapshot, core::KeyContext::Viewer));
    // The selection survives arming the snapshot: the overlay is still painting it.
    REQUIRE(product.overlay()->hasSelection());
    QGuiApplication::clipboard()->setText(QStringLiteral("before"));
    const auto mouse = [view](QEvent::Type type, QPoint pos) {
        QMouseEvent event(type, QPointF(pos), view->viewport()->mapToGlobal(QPointF(pos)), Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QCoreApplication::processEvents();
    };
    mouse(QEvent::MouseButtonPress, band.topLeft());
    mouse(QEvent::MouseMove, band.center());
    mouse(QEvent::MouseMove, band.bottomRight());
    mouse(QEvent::MouseButtonRelease, band.bottomRight());
    product.settle();
    const QImage snapshot = QGuiApplication::clipboard()->image().convertToFormat(QImage::Format_RGB32);
    REQUIRE_FALSE(snapshot.isNull());
    REQUIRE(snapshot.size() == grab.size());   // as displayed, 1:1 at a device-pixel ratio of 1

    // The handles' accent appears in the view and nowhere in the snapshot.
    const auto accentPixels = [](const QImage& image) {
        long long n = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QRgb p = image.pixel(x, y);
                if (qRed(p) < 40 && qGreen(p) > 90 && qGreen(p) < 150 && qBlue(p) > 180) ++n;   // 0078d4
            }
        return n;
    };
    CHECK(accentPixels(grab) > 20);
    CHECK(accentPixels(snapshot) == 0);
}

TEST_CASE("an edit keeps the page drawn until its new tiles arrive", "[annotink][D6-2]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/annots-every-type.pdf");
    product.settle();
    const ui::TilePresenter& tiles = view->tiles();
    REQUIRE(tiles.heldTileCount() > 0);
    const QImage before = view->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
    const core::AnnotationStore& store = product.document()->journal()->document().annotations();
    std::vector<core::AnnotationId> owned;
    for (const std::size_t index : store.onPage(0)) owned.push_back(store.at(index).id);
    REQUIRE(product.document()->journal()->run(
        std::make_unique<core::MoveAnnotationCommand>(owned, 0.0, 40.0, 1791590400)));

    // THE FIRST PAINT AFTER THE EDIT, before any tile came back: what was on screen still is -- to the pixel,
    // since no tile has arrived and every one dropped is shown as it was. Until T5 the dropped tiles were white.
    view->viewport()->repaint();
    const ui::TilePresenter::PaintStats first = tiles.lastPaint();
    const QImage during = view->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
    std::cout << "hold: first paint " << first.tilesDrawn << " fresh, " << first.staleDrawn << " held, "
              << first.placeholders << " still on the way\n";
    CHECK(first.staleDrawn > 0);
    CHECK(differingPixels(before, during) == 0);

    product.settle();
    CHECK(tiles.staleTileCount() == 0);
    CHECK(tiles.lastPaint().staleDrawn == 0);
    // And the edit did reach the pixels once its tiles came (rule 1).
    CHECK(differingPixels(before, view->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32)) > 0);
}
