// tst_typedink -- Stage 6 T7, guide cases A-55 and A-56 through the TOOLS, and A-34/A-35's typed halves
// (`Stage6-Annotations-Testing-Guide.md` §5.9, §5.6): the text box, the callout and the typewriter driven with
// MOUSE AND KEY EVENTS over the product's own composition, graded against the engine's pixels (rule 6).
//
//   * **A-55 through the tools.** A box dragged, a callout pressed on its target and dragged to its box, a
//     typewriter clicked -- each typed into the editor the tool opens and finished with `Enter` (a typewriter's
//     second line with `Shift+Enter`): three gestures, three commands, each its own kind; the box's frame starts
//     where the drag did. A DOUBLE CLICK on the box opens its text in place, and the change is ONE more command.
//     The text is DRAWN inside the box -- ink in the box that a page without annotations does not have.
//   * **A-56 at input.** A Greek letter typed into a box's editor goes nowhere: the editor's text is unchanged
//     and the refusal names the letter, by `error.edit.typedCharacterRefused`.
//   * **Typed on a turned page.** On `annots-rotated.pdf`'s page at `/Rotate` 90, a typewriter's text is
//     written turned with the page -- `/Rotate` 90 -- so on screen its line runs ACROSS and reads LEFT TO RIGHT:
//     typed `MMMMM` then ten full stops, its ink's weight is on the left. (Across alone is not enough: a
//     quarter turn the wrong way draws the line upside down, still across -- planted control P7's first run.)
//   * **A-34, A-35 through the tools.** Saved, the file reopened FRESH: 0 differing bytes at 72 and 144 dpi and
//     turned 90°; every one undone after the save, 0 from the page as it was.
//
// WHAT THIS DOES NOT COVER (convention 19): an input method (keys are sent with their text; an IME's commit is
// `tst_annottools`'), a real 150 % monitor, the typed size a person sees against the editor's (M-06), and any
// reader but PDFium (M-06). Every document is OURS (D6-26).

#include "tests/composition/AnnotProduct.h"

#include "core/cmd/ActionRegistry.h"
#include "core/doc/PageTree.h"
#include "core/doc/TypedText.h"
#include "pdf/DocumentBackendImpl.h"
#include "ui/viewer/AnnotationTextEditor.h"
#include "ui/viewer/ToolController.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include <iostream>
#include <set>

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
    mouse(view, QEvent::MouseMove, (from + to) / 2);
    mouse(view, QEvent::MouseMove, to);
    mouse(view, QEvent::MouseButtonRelease, to);
}

void key(QWidget* target, int k, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString& text = {})
{
    QKeyEvent press(QEvent::KeyPress, k, modifiers, text);
    QCoreApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, k, modifiers, text);
    QCoreApplication::sendEvent(target, &release);
    QCoreApplication::processEvents();
}

void type(const QString& text)
{
    for (const QChar c : text) {
        QWidget* focus = QApplication::focusWidget();
        REQUIRE(focus != nullptr);
        key(focus, c.isLetterOrNumber() ? c.toUpper().unicode() : 0, Qt::NoModifier, QString(c));
    }
}

ui::AnnotationTextEditor* editorOf(Product& product)
{
    QCoreApplication::processEvents();
    ui::AnnotationTextEditor* editor = product.view->annotationTextEditor();
    REQUIRE(editor != nullptr);
    REQUIRE(editor->hasFocus());
    return editor;
}

bool dispatch(Product& product, core::ActionId id)
{
    return static_cast<bool>(product.root->services().actions.dispatch(id, core::KeyContext::Viewer));
}

// The free text made most recently, of `intent`.
std::optional<Annotation> madeWith(const Product& product, const QString& intent)
{
    std::optional<Annotation> found;
    for (const Annotation& a : product.store().all()) {
        if (a.subtype == AnnotationSubtype::FreeText && a.id.kind == core::AnnotationId::Kind::Created && a.intent == intent)
            found = a;
    }
    return found;
}

// Pixels inside `region` where `with` is darker than `without` -- ink the annotation added.
int addedInk(const QImage& with, const QImage& without, QRect region)
{
    region &= with.rect();
    int n = 0;
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x)
            if (qGray(with.pixel(x, y)) + 40 < qGray(without.pixel(x, y))) ++n;
    return n;
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

TEST_CASE("a box a callout and a typewriter made with their tools are drawn where typed and shown as saved",
          "[typedink][A-55][A-34][A-35]")
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

    // A BOX, dragged out at the top right of the page and typed into.
    const core::PdfPoint boxFrom{340.0, 810.0};
    const core::PdfPoint boxTo{520.0, 780.0};
    show(core::PdfRect{core::PdfPoint{330, 760}, core::PdfSize{200, 60}});
    REQUIRE(dispatch(product, core::ActionId::AnnotTextBox));
    drag(*view, widgetAt(*view, 0, boxFrom), widgetAt(*view, 0, boxTo));
    (void)editorOf(product);
    type(QStringLiteral("Typed into a box with its tool"));
    key(QApplication::focusWidget(), Qt::Key_Return);
    product.settle();
    REQUIRE(journal.position() == 1);
    const std::optional<Annotation> box = madeWith(product, QString());
    REQUIRE(box);
    CHECK(box->contents == QStringLiteral("Typed into a box with its tool"));
    const auto frame = core::freeTextFrame(*box);
    REQUIRE(frame);
    // Where the drag began is the frame's top-left, within a logical pixel's worth of page units.
    CHECK(std::abs(frame->box.left() - boxFrom.x) < 1.5);
    CHECK(std::abs(frame->box.top() - boxFrom.y) < 1.5);
    CHECK(std::abs(frame->box.size.width - (boxTo.x - boxFrom.x)) < 1.5);
    CHECK(view->tools().armed() == ui::ToolKind::Select);   // a tool that made something lets go (D6-33(c))

    // A CALLOUT, pressed on what it points at and dragged to where its box goes.
    const core::PdfPoint target{120.0, 700.0};
    const core::PdfPoint calloutBox{220.0, 760.0};
    show(core::PdfRect{core::PdfPoint{100, 680}, core::PdfSize{260, 100}});
    REQUIRE(dispatch(product, core::ActionId::AnnotCallout));
    drag(*view, widgetAt(*view, 0, target), widgetAt(*view, 0, calloutBox));
    (void)editorOf(product);
    type(QStringLiteral("Look here"));
    key(QApplication::focusWidget(), Qt::Key_Return);
    product.settle();
    REQUIRE(journal.position() == 2);
    const std::optional<Annotation> callout = madeWith(product, QStringLiteral("FreeTextCallout"));
    REQUIRE(callout);
    REQUIRE(callout->callout.size() == 6);
    CHECK(std::abs(callout->callout[0] - target.x) < 1.5);   // it points where the press was
    CHECK(std::abs(callout->callout[1] - target.y) < 1.5);

    // A TYPEWRITER, clicked in the bottom margin, two lines.
    const core::PdfPoint typedAt{60.0, 70.0};
    show(core::PdfRect{core::PdfPoint{40, 30}, core::PdfSize{200, 60}});
    REQUIRE(dispatch(product, core::ActionId::AnnotTypewriter));
    const QPoint click = widgetAt(*view, 0, typedAt);
    mouse(*view, QEvent::MouseButtonPress, click);
    mouse(*view, QEvent::MouseButtonRelease, click);
    (void)editorOf(product);
    type(QStringLiteral("first line"));
    key(QApplication::focusWidget(), Qt::Key_Return, Qt::ShiftModifier);
    type(QStringLiteral("second"));
    key(QApplication::focusWidget(), Qt::Key_Return);
    product.settle();
    REQUIRE(journal.position() == 3);   // three gestures, three commands
    const std::optional<Annotation> typewriter = madeWith(product, QStringLiteral("FreeTextTypeWriter"));
    REQUIRE(typewriter);
    CHECK(typewriter->contents == QStringLiteral("first line\nsecond"));
    CHECK(typewriter->borderWidth == std::optional<double>(0.0));

    // THE TEXT IS DRAWN IN EACH BOX: ink a page without annotations does not have.
    {
        const QImage with = product.page(0, true);
        const QImage without = product.page(0, false);
        for (const Annotation* a : {&*box, &*callout, &*typewriter}) {
            INFO(a->intent.toStdString());
            const auto f = core::freeTextFrame(*a);
            REQUIRE(f);
            CHECK(addedInk(with, without, onSheet(*view, 0, f->box).toAlignedRect()) > 40);
        }
    }

    // EDITED IN PLACE: a double click opens the box's own text over it; the change is ONE command.
    show(core::PdfRect{core::PdfPoint{330, 740}, core::PdfSize{200, 80}});
    const QPoint inBox = widgetAt(*view, 0, core::PdfPoint{frame->box.left() + 20, frame->box.top() - 8});
    mouse(*view, QEvent::MouseButtonPress, inBox);
    mouse(*view, QEvent::MouseButtonRelease, inBox);
    mouse(*view, QEvent::MouseButtonPress, inBox);
    mouse(*view, QEvent::MouseButtonRelease, inBox);
    ui::AnnotationTextEditor* editor = editorOf(product);
    CHECK(editor->toPlainText() == box->contents);
    editor->selectAll();
    const QString longer = QStringLiteral("Changed in place, and now long enough to need more lines than one");
    type(longer);
    key(QApplication::focusWidget(), Qt::Key_Return);
    product.settle();
    REQUIRE(journal.position() == 4);
    const Annotation& edited = product.store().at(*product.store().find(box->id));
    CHECK(edited.contents == longer);
    // REFITTED: the box keeps its width and top and grows down to its lines.
    CHECK(std::abs(edited.rect.size.width - box->rect.size.width) < 1e-6);
    CHECK(std::abs(edited.rect.top() - box->rect.top()) < 1e-6);
    CHECK(edited.rect.size.height > box->rect.size.height + 10.0);

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
        std::cout << "A-34 through the typed tools: a box, a callout, a typewriter and an edit saved, live against "
                     "the file reopened fresh: " << total << " differing bytes over three views\n";
        CHECK(fresh.close(reopened.value()));
    }

    // EVERY ONE UNDONE after the save, and the page is the original.
    for (int step = 0; step < 4; ++step) REQUIRE(dispatch(product, core::ActionId::EditUndo));
    product.settle();
    REQUIRE(journal.position() == 0);
    product.root->services().renderScheduler.waitForIdle(4000);
    long long undone = 0;
    for (std::size_t i = 0; i < std::size(kShownViews); ++i) {
        const long long differing =
            differingBytes(rasterOf(backend, handle, 0, kShownViews[i].scale, kShownViews[i].rotation), original[i]);
        CHECK(differing == 0);
        undone += differing;
    }
    std::cout << "A-35 through the typed tools: all four undone after the save, " << undone << " bytes from the original\n";
}

TEST_CASE("a character no face can show is refused as it is typed and named", "[typedink][A-56]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    std::vector<core::Error> refusals;
    QObject::connect(product.overlay(), &ui::AnnotationOverlay::refused,
                     [&refusals](const core::Error& error) { refusals.push_back(error); });

    view->revealPageRect(0, core::PdfRect{core::PdfPoint{330, 740}, core::PdfSize{200, 80}});
    product.settle();
    REQUIRE(dispatch(product, core::ActionId::AnnotTextBox));
    const QPoint at = widgetAt(*view, 0, core::PdfPoint{340, 800});
    mouse(*view, QEvent::MouseButtonPress, at);
    mouse(*view, QEvent::MouseButtonRelease, at);
    ui::AnnotationTextEditor* editor = editorOf(product);
    CHECK(editor->refusesUnshowable());
    type(QStringLiteral("Ab"));
    type(QStringLiteral("Ω"));   // Omega
    CHECK(editor->toPlainText() == QStringLiteral("Ab"));
    REQUIRE(refusals.size() == 1u);
    CHECK(QString(refusals.front().messageKey) == QStringLiteral("error.edit.typedCharacterRefused"));
    CHECK(refusals.front().context == core::describeCharacter(0x03A9));
    // CJK is accepted, and drawn in a CJK face `/DA` names.
    type(QStringLiteral(" 中文"));
    key(QApplication::focusWidget(), Qt::Key_Return);
    product.settle();
    const std::optional<Annotation> made = madeWith(product, QString());
    REQUIRE(made);
    CHECK(made->contents == QStringLiteral("Ab 中文"));
    const core::FreeTextStyle style = core::freeTextStyle(*made);
    REQUIRE(style.han);
    CHECK(core::isCjk(*style.han));
}

TEST_CASE("typed text on a turned page is written turned with it and reads across the screen", "[typedink][D6-34]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/annots-rotated.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    // The page shown at /Rotate 90 (the fixture's second).
    const core::PageTree& tree = product.document()->journal()->document().pages();
    int page = -1;
    for (int i = 0; i < tree.count(); ++i)
        if (tree.at(i)->rotation() == core::PageRotation::Clockwise90) page = i;
    REQUIRE(page >= 0);
    const core::PdfRect crop = tree.at(page)->cropBox();
    const core::PdfPoint where{crop.left() + 60, crop.top() - 60};
    view->revealPageRect(page, core::PdfRect{core::PdfPoint{where.x - 20, where.y - 200}, core::PdfSize{120, 220}});
    product.settle();

    REQUIRE(dispatch(product, core::ActionId::AnnotTypewriter));
    const QPoint click = widgetAt(*view, page, where);
    mouse(*view, QEvent::MouseButtonPress, click);
    mouse(*view, QEvent::MouseButtonRelease, click);
    (void)editorOf(product);
    type(QStringLiteral("MMMMM.........."));
    key(QApplication::focusWidget(), Qt::Key_Return);
    product.settle();
    const std::optional<Annotation> made = madeWith(product, QStringLiteral("FreeTextTypeWriter"));
    REQUIRE(made);
    CHECK(made->rotate == 90);
    // On screen the text's ink runs ACROSS: the added ink's extent is wider than it is tall.
    const QImage with = product.page(page, true);
    const QImage without = product.page(page, false);
    const QRect region = onSheet(*view, page, made->rect).toAlignedRect() & with.rect();
    int left = INT_MAX, right = -1, top = INT_MAX, bottom = -1;
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x)
            if (qGray(with.pixel(x, y)) + 40 < qGray(without.pixel(x, y))) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    REQUIRE(right >= left);
    CHECK(right - left > 3 * (bottom - top));
    // UPRIGHT: the heavy capitals first, so the ink's weight lies left of the middle.
    long long weight = 0, sum = 0;
    for (int y = top; y <= bottom; ++y)
        for (int x = left; x <= right; ++x)
            if (qGray(with.pixel(x, y)) + 40 < qGray(without.pixel(x, y))) {
                sum += x;
                ++weight;
            }
    REQUIRE(weight > 0);
    const double centroid = static_cast<double>(sum) / static_cast<double>(weight);
    CHECK(centroid < left + 0.5 * (right - left));
}
