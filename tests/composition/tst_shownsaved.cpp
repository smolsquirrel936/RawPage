// tst_shownsaved -- Stage 6 T15, guide case A-108 (`Stage6-Annotations-Testing-Guide.md` §5.18): SHOWN IS SAVED
// for what the tools make, the stage-wide sweep, decisions D6-2 and D6-6 (`Stage6-Annotations-Plan.md` §3).
//
// A-34 asks that the page the live engine draws after an edit be, byte for byte, the page the saved file draws
// reopened fresh. Each tool's own suite already makes that comparison for its own kinds, through its tool, in three
// views -- `tst_markupink` (T6: the markup group and notes), `tst_typedink` (T7), `tst_drawink` (T8: the shapes and
// the pencil), `tst_stampink` (T10), `tst_measureproduct` (T12), `tst_exchangeproduct` (T13, an import) and
// `tst_redactink` (T14). A-108 is the sweep over what is left: the kinds and gestures T5 … T14 made that no suite
// compared in pixels --
//   * an ERASE (T8): `annot.eraser` across a pencil stroke, one `EditAnnotation` that splits it;
//   * a PASTE (T13, D6-22): the stroke selected, `edit.copy`, `edit.paste` -- a copy written by our builder;
//   * the PUSHPIN (T11): `annot.attachFile` and a click, a `FileAttachment` carrying a file;
//   * a SOUND (T14): `annot.attachAudio` and a click, a `Sound` carrying a WAV's samples.
// All four in ONE document beside a pencil stroke, saved together, so a kind drawn right alone and wrong beside
// another would show too.
//
// And S6 guide I-25 (§8): a note, a pushpin and a speaker clicked on the DESK beside each side of a turned page are
// drawn whole on the page -- its own case below, with what it does not cover.
//
// WHAT THIS DOES NOT COVER (convention 19): the kinds the suites above compare are not compared AGAIN here -- the
// sweep is the set of those cases and this one, and the guide's A-108 result names each; the recording (T14's
// `annot.recordAudio`) makes the same `Sound` the attached WAV makes and is A-101's.

#include <catch2/catch_test_macros.hpp>

#include "tests/composition/AnnotProduct.h"

#include "core/cmd/ActionRegistry.h"
#include "core/doc/Annotation.h"
#include "core/doc/Redaction.h"
#include "pdf/DocumentBackendImpl.h"
#include "ui/viewer/ToolController.h"

#include <QApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include <cstdint>
#include <iostream>

using namespace rawpage;
using annot_product::Product;
using core::Annotation;
using core::AnnotationSubtype;

namespace {

QPoint at(const ui::PageView& view, double x, double y)
{
    const std::optional<core::DevicePoint> d = view.layoutEngine().layout().toContent(0, core::PdfPoint{x, y});
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

bool dispatch(Product& product, core::ActionId id)
{
    const bool done = static_cast<bool>(product.root->services().actions.dispatch(id, core::KeyContext::Viewer));
    product.view->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    return done;
}

void le16(QByteArray& out, std::uint16_t v)
{
    out.append(static_cast<char>(v & 0xff));
    out.append(static_cast<char>(v >> 8));
}

void le32(QByteArray& out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i) out.append(static_cast<char>((v >> (8 * i)) & 0xff));
}

// A tenth of a second of 16-bit mono PCM at 8 kHz, as a WAV file.
QByteArray toneWav()
{
    QByteArray data;
    for (int f = 0; f < 800; ++f) le16(data, static_cast<std::uint16_t>(static_cast<std::int16_t>((f % 40) * 700 - 14000)));
    QByteArray body("WAVEfmt ", 8);
    le32(body, 16);
    le16(body, 1);
    le16(body, 1);
    le32(body, 8000);
    le32(body, 16000);
    le16(body, 2);
    le16(body, 16);
    body.append("data", 4);
    le32(body, static_cast<std::uint32_t>(data.size()));
    body.append(data);
    QByteArray out("RIFF", 4);
    le32(out, static_cast<std::uint32_t>(body.size()));
    return out + body;
}

core::RasterImage rasterOf(core::IDocumentBackend& backend, core::DocumentHandle handle, double scale, core::PageRotation view)
{
    const core::PdfSize size = backend.pages(handle)->at(0)->displaySize();
    const bool swap = core::swapsAxes(view);
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = 0;
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

struct TwinsReset {
    ~TwinsReset()
    {
        ui::MainWindow::setAttachmentAnswersForTesting({});
        ui::MainWindow::setAudioAnswersForTesting({});
        QGuiApplication::clipboard()->clear();
    }
};

int countOf(const Product& product, AnnotationSubtype subtype)
{
    int n = 0;
    for (const Annotation& a : product.store().all())
        if (a.subtype == subtype && a.id.kind == core::AnnotationId::Kind::Created) ++n;
    return n;
}

} // namespace

TEST_CASE("an erase a paste a pushpin and a sound made with their tools are shown as they are saved",
          "[shownsaved][A-108][A-34][A-35]")
{
    Product product;
    TwinsReset twins;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    core::IDocumentBackend& backend = product.root->services().backend;
    const core::DocumentHandle handle = product.document()->handle();
    core::CommandJournal& journal = *product.document()->journal();

    std::vector<core::RasterImage> original;
    for (const ShownView& shown : kShownViews) original.push_back(rasterOf(backend, handle, shown.scale, shown.rotation));

    // A PENCIL STROKE, a wave across the upper page -- the stroke the eraser and the paste work on.
    view->revealPageRect(0, core::PdfRect{core::PdfPoint{60, 560}, core::PdfSize{480, 200}});
    product.settle();
    REQUIRE(dispatch(product, core::ActionId::AnnotPencil));
    mouse(*view, QEvent::MouseButtonPress, at(*view, 80, 660));
    for (int i = 1; i <= 40; ++i) mouse(*view, QEvent::MouseMove, at(*view, 80 + i * 10, 660 + 25 * std::sin(i * 0.4)));
    mouse(*view, QEvent::MouseButtonRelease, at(*view, 480, 660 + 25 * std::sin(16.0)));
    product.settle();
    REQUIRE(journal.position() == 1);
    REQUIRE(countOf(product, AnnotationSubtype::Ink) == 1);

    // THE ERASER, dragged down through the stroke's middle: ONE `EditAnnotation`, the stroke now two.
    REQUIRE(dispatch(product, core::ActionId::AnnotEraser));
    mouse(*view, QEvent::MouseButtonPress, at(*view, 280, 720));
    for (int i = 1; i <= 12; ++i) mouse(*view, QEvent::MouseMove, at(*view, 280, 720 - i * 10));
    mouse(*view, QEvent::MouseButtonRelease, at(*view, 280, 600));
    product.settle();
    REQUIRE(journal.position() == 2);
    std::optional<core::AnnotationId> inkId;
    for (const Annotation& a : product.store().all()) {
        if (a.subtype != AnnotationSubtype::Ink) continue;
        CHECK(a.inkList.size() == 2);   // split where the eraser crossed
        inkId = a.id;
    }
    REQUIRE(inkId);

    // A PASTE: the stroke selected, copied and pasted -- one more command, a second ink written by our builder.
    REQUIRE(dispatch(product, core::ActionId::ToolSelect));
    product.overlay()->select({*inkId});
    REQUIRE(dispatch(product, core::ActionId::EditCopy));
    REQUIRE(dispatch(product, core::ActionId::EditPaste));
    product.settle();
    REQUIRE(journal.position() == 3);
    CHECK(countOf(product, AnnotationSubtype::Ink) == 2);

    // THE PUSHPIN: armed, a click, the file the twin chooses attached there.
    const QString attachedFile = QDir(product.scratch.path()).filePath(QStringLiteral("notes.txt"));
    {
        QFile file(attachedFile);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("A-108\n");
    }
    const QString wavFile = QDir(product.scratch.path()).filePath(QStringLiteral("tone.wav"));
    {
        QFile file(wavFile);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(toneWav());
    }
    ui::MainWindow::setAttachmentAnswersForTesting(
        ui::MainWindow::AttachmentAnswers{[&] { return std::optional<QString>(attachedFile); }, {}, {}});
    ui::MainWindow::setAudioAnswersForTesting(
        ui::MainWindow::AudioAnswers{{}, [&]() -> std::optional<QString> { return wavFile; }});
    view->revealPageRect(0, core::PdfRect{core::PdfPoint{380, 420}, core::PdfSize{180, 160}});
    product.settle();
    REQUIRE(dispatch(product, core::ActionId::AnnotAttachFile));
    mouse(*view, QEvent::MouseButtonPress, at(*view, 420, 540));
    mouse(*view, QEvent::MouseButtonRelease, at(*view, 420, 540));
    product.settle();
    REQUIRE(journal.position() == 4);
    CHECK(countOf(product, AnnotationSubtype::FileAttachment) == 1);

    // THE SOUND: armed, a click, the WAV the twin chooses placed as a speaker.
    REQUIRE(dispatch(product, core::ActionId::AnnotAttachAudio));
    mouse(*view, QEvent::MouseButtonPress, at(*view, 500, 460));
    mouse(*view, QEvent::MouseButtonRelease, at(*view, 500, 460));
    product.settle();
    REQUIRE(journal.position() == 5);
    CHECK(countOf(product, AnnotationSubtype::Sound) == 1);

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
            const core::RasterImage live = rasterOf(backend, handle, kShownViews[i].scale, kShownViews[i].rotation);
            const core::RasterImage file = rasterOf(fresh, reopened.value(), kShownViews[i].scale, kShownViews[i].rotation);
            const long long differing = differingBytes(live, file);
            CHECK(differing == 0);
            CHECK(differingBytes(live, original[i]) > 0);   // rule 1: the edits are in the pixels
            total += differing;
        }
        std::cout << "A-108: an erase, a paste, a pushpin and a sound beside a pencil stroke, saved; live against the file"
                     " reopened fresh: " << total << " differing bytes over three views\n";
        CHECK(fresh.close(reopened.value()));
    }

    // EVERY ONE UNDONE after the save (A-35), and the page is the original.
    while (journal.canUndo()) REQUIRE(journal.undo());
    product.settle();
    product.root->services().renderScheduler.waitForIdle(4000);
    long long fromOriginal = 0;
    for (std::size_t i = 0; i < std::size(kShownViews); ++i)
        fromOriginal += differingBytes(rasterOf(backend, handle, kShownViews[i].scale, kShownViews[i].rotation), original[i]);
    std::cout << "A-108: all five undone after the save: " << fromOriginal << " bytes from the original\n";
    CHECK(fromOriginal == 0);
}

// ==========================================================================================
// S6 guide I-25: an icon a click places is DRAWN whole on its page
// ==========================================================================================
// A click on the desk arrives at the nearest page's EDGE (`PageView::pagePointAt`), and a note's icon is hung from its
// top-left corner, upright on screen -- so a note clicked to the right of a page or below it lay wholly off the page,
// where nothing draws it: in the file and the Comments pane, absent from the page (M-01's walk, 2026-10-20). A pushpin
// and a speaker are hung from the same point in PAGE space, so on a turned page another side failed for them.
//
// Each tool clicked on the desk beside each side of each of `annots-rotated.pdf`'s four pages (`/Rotate` 0, 90, 180,
// 270), and graded twice: what is DRAWN (`AnnotationOverlay::drawnRect`) inside the crop box; and -- rule 6, the
// oracle the fix does not share -- the page as the engine draws it, before and after, changed over a box spanning at
// least `kInkShare` of the icon on screen each way. An icon off the page changes nothing; one cut by an edge, less.
// Each placement undone before the next, so no icon is graded over another.
//
// NOT COVERED (convention 19): a turned VIEW (`drawnRect` handles it; no case here turns one), a stamp (dragged to a
// size, and placed by its own rule), and the pop-up, which RawPage paints over the view beside the icon and may lie
// on the desk -- it is view state (D6-28), not page ink.

namespace {

QPoint widgetOf(const ui::PageView& view, core::DevicePoint content)
{
    const core::DeviceRect visible = view.controller().visibleRect();
    const double dpr = view.devicePixelRatioF();
    return QPoint(static_cast<int>(std::lround((content.x - visible.left()) / dpr)),
                  static_cast<int>(std::lround((content.y - visible.top()) / dpr)));
}

// The bounding box of the pixels `after` changed, in the page image's pixels; empty when none did.
QRect changedBox(const QImage& before, const QImage& after)
{
    REQUIRE(before.size() == after.size());
    QRect box;
    for (int y = 0; y < before.height(); ++y)
        for (int x = 0; x < before.width(); ++x)
            if (before.pixel(x, y) != after.pixel(x, y)) box |= QRect(x, y, 1, 1);
    return box;
}

constexpr double kInkShare = 0.6;

} // namespace

TEST_CASE("an icon clicked beside a page is drawn whole on it", "[shownsaved][I-25]")
{
    Product product;
    TwinsReset twins;
    const QString attachedFile = QDir(product.scratch.path()).filePath(QStringLiteral("notes.txt"));
    {
        QFile file(attachedFile);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("I-25\n");
    }
    const QString wavFile = QDir(product.scratch.path()).filePath(QStringLiteral("tone.wav"));
    {
        QFile file(wavFile);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(toneWav());
    }
    ui::MainWindow::setAttachmentAnswersForTesting(
        ui::MainWindow::AttachmentAnswers{[&] { return std::optional<QString>(attachedFile); }, {}, {}});
    ui::MainWindow::setAudioAnswersForTesting(
        ui::MainWindow::AudioAnswers{{}, [&]() -> std::optional<QString> { return wavFile; }});

    ui::PageView* view = product.open("ordinary/annots-rotated.pdf");
    view->setZoom(core::render::Zoom{0.35});   // every page whole in the viewport, with the desk round it
    product.settle();
    REQUIRE(view->pageCount() == 4);
    core::CommandJournal& journal = *product.document()->journal();

    struct Tool {
        core::ActionId action;
        AnnotationSubtype made;
        double width;   // the icon's size in page units, as `AnnotationOverlay::placedIcon` hangs it
        double height;
        const char* name;
    };
    const Tool tools[] = {
        {core::ActionId::AnnotStickyNote, AnnotationSubtype::Text, ui::AnnotationOverlay::kNoteSize,
         ui::AnnotationOverlay::kNoteSize, "note"},
        {core::ActionId::AnnotAttachFile, AnnotationSubtype::FileAttachment, ui::AnnotationOverlay::kPushPinWidth,
         ui::AnnotationOverlay::kPushPinHeight, "pushpin"},
        {core::ActionId::AnnotAttachAudio, AnnotationSubtype::Sound, core::kSoundIconSize, core::kSoundIconSize, "speaker"},
    };
    const char* const sides[] = {"right of", "below", "above", "left of"};

    int placed = 0;
    int whole = 0;
    // THE PAGE'S SLOT, COPIED: every command reloads the document (D6-2) and lays it out again, so a pointer into the
    // layout held across one dangles. Revealed first, since an undo may have scrolled the view.
    const auto slotOf = [&](int page) {
        const core::render::PageSlot* found = view->layoutEngine().layout().slotForPage(page);
        REQUIRE(found != nullptr);
        view->revealPageRect(page, found->cropBox);
        product.settle();
        found = view->layoutEngine().layout().slotForPage(page);
        REQUIRE(found != nullptr);
        return *found;
    };
    for (int page = 0; page < 4; ++page) {
        (void)slotOf(page);
        const QImage before = product.page(page, true);
        for (int side = 0; side < 4; ++side) {
            for (const Tool& tool : tools) {
                const core::render::PageSlot slot = slotOf(page);
                const core::DeviceRect r = slot.rect;
                // The desk just outside this side, a third of the way along it.
                const core::DevicePoint desk[] = {
                    {r.right() + 6.0, r.top() + r.size.height / 3.0},
                    {r.left() + r.size.width / 3.0, r.bottom() + 3.0},
                    {r.left() + 2.0 * r.size.width / 3.0, r.top() - 3.0},
                    {r.left() - 6.0, r.top() + 2.0 * r.size.height / 3.0},
                };
                const QPoint click = widgetOf(*view, desk[side]);
                INFO("click " << click.x() << "," << click.y() << " in a viewport " << view->viewport()->width()
                              << " x " << view->viewport()->height());
                REQUIRE(view->viewport()->rect().contains(click));
                REQUIRE(view->layoutEngine().layout().pageAt(desk[side]) == -1);   // on the desk, not on a page
                INFO("page " << page << " (/Rotate " << page * 90 << "), " << tool.name << " clicked " << sides[side]
                             << " it");
                const std::size_t count = product.store().size();
                REQUIRE(dispatch(product, tool.action));
                mouse(*view, QEvent::MouseButtonPress, click);
                mouse(*view, QEvent::MouseButtonRelease, click);
                if (tool.made == AnnotationSubtype::Text) {
                    QWidget* editor = QApplication::focusWidget();
                    REQUIRE(editor != nullptr);
                    QKeyEvent letter(QEvent::KeyPress, Qt::Key_N, Qt::NoModifier, QStringLiteral("n"));
                    QCoreApplication::sendEvent(editor, &letter);
                    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                    QCoreApplication::sendEvent(editor, &enter);
                }
                product.settle();
                REQUIRE(product.store().size() > count);
                std::optional<Annotation> made;
                for (const Annotation& a : product.store().all())
                    if (a.subtype == tool.made && a.id.kind == core::AnnotationId::Kind::Created && a.page == page) made = a;
                REQUIRE(made);
                ++placed;

                // WHAT IS DRAWN lies inside the crop box.
                const std::optional<core::PdfRect> drawn = ui::AnnotationOverlay::drawnRect(view->annotationViewport(), *made);
                REQUIRE(drawn);
                const core::PdfRect& crop = slot.cropBox;
                INFO("drawn " << drawn->left() << " " << drawn->bottom() << " " << drawn->right() << " " << drawn->top()
                              << ", rect " << made->rect.left() << " " << made->rect.bottom() << " " << made->rect.right()
                              << " " << made->rect.top() << ", flags " << made->flags << ", crop " << crop.left() << " "
                              << crop.bottom() << " " << crop.right() << " " << crop.top());
                const bool inside = drawn->left() >= crop.left() - 1e-6 && drawn->right() <= crop.right() + 1e-6
                                    && drawn->bottom() >= crop.bottom() - 1e-6 && drawn->top() <= crop.top() + 1e-6;
                CHECK(inside);

                // AND THE ENGINE DRAWS IT: the changed box spans most of the icon on screen. Upright on screen for a
                // note; a pushpin and a speaker turn with the page, so their sides trade on a quarter turn.
                const double scale = view->layoutEngine().layout().presentedScale();
                const bool turned = tool.made != AnnotationSubtype::Text && core::swapsAxes(slot.rotation);
                const double wide = (turned ? tool.height : tool.width) * scale;
                const double tall = (turned ? tool.width : tool.height) * scale;
                const QRect ink = changedBox(before, product.page(page, true));
                INFO("ink " << ink.width() << " x " << ink.height() << " px of an icon " << wide << " x " << tall);
                CHECK(ink.width() >= kInkShare * wide);
                CHECK(ink.height() >= kInkShare * tall);
                if (inside && ink.width() >= kInkShare * wide && ink.height() >= kInkShare * tall) ++whole;

                REQUIRE(journal.undo());
                product.settle();
                REQUIRE(product.store().size() == count);
            }
        }
    }
    std::cout << "I-25: " << placed << " icons clicked on the desk beside four turned pages; " << whole
              << " drawn whole on their page\n";
    CHECK(placed == 4 * 4 * 3);
}
