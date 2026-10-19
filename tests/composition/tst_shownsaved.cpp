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
// WHAT THIS DOES NOT COVER (convention 19): the kinds the suites above compare are not compared AGAIN here -- the
// sweep is the set of those cases and this one, and the guide's A-108 result names each; the recording (T14's
// `annot.recordAudio`) makes the same `Sound` the attached WAV makes and is A-101's.

#include <catch2/catch_test_macros.hpp>

#include "tests/composition/AnnotProduct.h"

#include "core/cmd/ActionRegistry.h"
#include "core/doc/Annotation.h"
#include "pdf/DocumentBackendImpl.h"
#include "ui/viewer/ToolController.h"

#include <QClipboard>
#include <QGuiApplication>
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
