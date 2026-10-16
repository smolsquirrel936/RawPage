// Stage 6 T14 -- redaction MARKS through the product's own composition (`Stage6-Annotations-Testing-Guide.md` §5.17, guide
// case A-100; D6-25): `Ctrl+Alt+K` from the page, a drag across a phrase of `text-a4.pdf` and a drag over its margin,
// with the overlay text, the repeat and the colour set on the properties bar as a person sets them -- then saved, and the
// saved file held to three things:
//
//   * **every content stream of every page BYTE-IDENTICAL** to the original's -- the page's `/Contents` as written and
//     every byte of every stream it names, read first-party from both files;
//   * **the text still there**: a FRESH engine extracts the marked phrase from the saved file, in its place -- a mark
//     removes nothing, and applying is Stage 10's;
//   * **shown is saved** (A-34): the live page and the file reopened fresh, 0 bytes apart.
//
// WHAT THIS DOES NOT COVER (convention 19): the pointer itself (events are sent to the viewport); how Acrobat shows the
// mark and its `/RO` (M-02); and applying a mark, which does not exist yet. Every document is OURS (D6-26).

#include "tests/composition/AnnotProduct.h"

#include "core/cmd/ActionRegistry.h"
#include "core/doc/PageTree.h"
#include "core/doc/Redaction.h"
#include "core/text/PageTextModel.h"
#include "core/text/TextSelection.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/PageOps.h"
#include "ui/viewer/PropertiesBar.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/ToolDefaults.h"
#include "ui/shell/DocumentWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>

#include <iostream>

using namespace rawpage;
using annot_product::Product;
using core::Annotation;
using core::AnnotationSubtype;

namespace {

const core::text::PageReading& readingOf(Product& product, int page)
{
    ui::SelectionOverlay* text = product.document()->selection();
    REQUIRE(text != nullptr);
    QElapsedTimer timer;
    timer.start();
    while (!text->ensurePage(page, core::render::RenderPriority::Visible) && timer.elapsed() < 30000) product.settle();
    const core::text::PageReading* reading = text->text().page(page);
    REQUIRE(reading != nullptr);
    return *reading;
}

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

void key(QWidget* target, int k, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, k, modifiers);
    QCoreApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, k, modifiers);
    QCoreApplication::sendEvent(target, &release);
    QCoreApplication::processEvents();
}

void drag(Product& product, QPoint from, QPoint to)
{
    ui::PageView& view = *product.view;
    REQUIRE(view.viewport()->rect().contains(from));
    REQUIRE(view.viewport()->rect().contains(to));
    mouse(view, QEvent::MouseButtonPress, from);
    mouse(view, QEvent::MouseMove, (from + to) / 2);
    mouse(view, QEvent::MouseMove, to);
    product.settle();
    mouse(view, QEvent::MouseButtonRelease, to);
    product.settle();
}

QString textOf(const core::text::PageReading& reading)
{
    QString out;
    for (int p = 0; p < reading.size(); ++p) {
        const char32_t c = reading.charAt(p).codepoint;
        out += c < 0x10000 ? QChar(static_cast<char16_t>(c)) : QChar(0xFFFD);
    }
    return out;
}

// Each page's `/Contents` as written and every stream it names, raw, by number -- read first-party from `path`.
std::vector<std::pair<std::string, std::vector<std::pair<long long, std::string>>>> contentOf(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    auto opened = pdf::cos::openWithRepair(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    REQUIRE(opened);
    pdf::cos::XrefTable& table = opened.value().table;
    auto walked = pdf::ops::PageOps::walk(table);
    REQUIRE(walked);
    std::vector<std::pair<std::string, std::vector<std::pair<long long, std::string>>>> out;
    for (const pdf::ops::PageEntry& page : walked.value().pages) {
        auto dictionary = table.fetch(page.number);
        REQUIRE(dictionary);
        const pdf::cos::CosObject* value = dictionary.value().object.find("Contents");
        REQUIRE(value != nullptr);
        std::vector<pdf::cos::CosObject> refs;
        if (value->isReference()) refs.push_back(*value);
        else if (value->isArray()) refs = value->items();
        std::string said;
        std::vector<std::pair<long long, std::string>> streams;
        for (const pdf::cos::CosObject& ref : refs) {
            REQUIRE(ref.isReference());
            said += std::to_string(ref.reference().number) + " " + std::to_string(ref.reference().generation) + ";";
            auto stream = table.fetch(ref.reference().number);
            REQUIRE(stream);
            REQUIRE(stream.value().object.isStream());
            streams.emplace_back(ref.reference().number, std::string(stream.value().object.rawStreamData(table.source())));
        }
        out.emplace_back(said, std::move(streams));
    }
    return out;
}

core::RasterImage rasterOf(core::IDocumentBackend& backend, core::DocumentHandle handle, int page, double scale)
{
    const core::PageTree* tree = backend.pages(handle);
    REQUIRE(tree != nullptr);
    const core::PdfSize size = tree->at(page)->displaySize();
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = page;
    request.deviceScale = scale;
    request.revision = core::Revision::initial();
    request.flags = core::render::RenderFlags::Annotations;
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(size.width * scale)),
                                          static_cast<int>(std::ceil(size.height * scale))};
    core::IPageRasterizer* rasterizer = backend.rasterizer(handle);
    REQUIRE(rasterizer != nullptr);
    auto image = rasterizer->raster(request);
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

} // namespace

TEST_CASE("redaction marks made with the tool remove nothing and are shown as saved", "[redactink][A-100][A-34][D6-25]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    const auto before = contentOf(product.openedPath);

    // A PHRASE: the first 20 characters of the first long line.
    const core::text::PageReading& reading = readingOf(product, 0);
    int line = -1;
    for (std::size_t l = 0; l < reading.layout().lines.size() && line < 0; ++l) {
        const core::text::Span span = reading.lineSpan(static_cast<int>(l));
        if (span.end - span.begin >= 40) line = static_cast<int>(l);
    }
    REQUIRE(line >= 0);
    const int first = reading.lineSpan(line).begin;
    const int last = first + 19;
    const QString phrase = textOf(reading).mid(first, last - first + 1);
    view->revealPageRect(0, reading.layout().lines[static_cast<std::size_t>(line)].box);
    product.settle();

    // `Ctrl+Alt+K` FROM THE PAGE with nothing armed, then the overlay set on the properties bar as a person sets it.
    key(view, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
    REQUIRE(view->tools().armedAnnotation() == ui::AnnotationToolKind::RedactMark);
    ui::PropertiesBar* bar = product.document()->propertiesBar();
    REQUIRE(bar != nullptr);
    REQUIRE(bar->subject() == ui::PropertiesBar::Subject::ToolDefaults);
    REQUIRE(bar->overlayTextField()->isEnabled());
    REQUIRE(bar->repeatBox()->isEnabled());
    // Typed WITH THE FIELD FOCUSED, as a person types: the window's `ShortcutBinder` routes `Enter` by the focus, and
    // with the page focused it is Annotation tools' `annot.finishShape`, never the field's.
    bar->overlayTextField()->setFocus(Qt::OtherFocusReason);
    bar->overlayTextField()->setText(QStringLiteral("REDACTED"));
    key(bar->overlayTextField(), Qt::Key_Return);
    bar->repeatBox()->setChecked(true);
    bar->colourBox()->setCurrentIndex(6);   // the swatches' blue: the OUTLINE
    view->setFocus(Qt::OtherFocusReason);

    // A DRAG ACROSS THE PHRASE: a mark over its words.
    {
        const core::text::PageReading& r = readingOf(product, 0);
        const core::PdfRect a = r.charAt(first).box;
        const core::PdfRect b = r.charAt(last).box;
        drag(product, widgetAt(*view, 0, core::PdfPoint{a.left() + a.size.width * 0.15, a.bottom() + a.size.height / 2}),
             widgetAt(*view, 0, core::PdfPoint{b.right() - b.size.width * 0.15, b.bottom() + b.size.height / 2}));
    }
    // AN AREA in the left margin, beside the same line: the tool armed again (it returned to Selection unless kept).
    key(view, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
    REQUIRE(view->tools().armedAnnotation() == ui::AnnotationToolKind::RedactMark);
    const core::PdfRect box = reading.layout().lines[static_cast<std::size_t>(line)].box;
    drag(product, widgetAt(*view, 0, core::PdfPoint{8.0, box.top() + 20.0}), widgetAt(*view, 0, core::PdfPoint{40.0, box.bottom() - 20.0}));

    std::vector<const Annotation*> marks;
    for (const Annotation& a : product.store().all())
        if (a.subtype == AnnotationSubtype::Redact) marks.push_back(&a);
    REQUIRE(marks.size() == 2);
    const Annotation* text = marks[0]->quadPoints.empty() ? marks[1] : marks[0];
    const Annotation* area = marks[0]->quadPoints.empty() ? marks[0] : marks[1];
    REQUIRE_FALSE(text->quadPoints.empty());
    CHECK(area->quadPoints.empty());
    for (const Annotation* m : {text, area}) {
        CHECK(m->overlayText == QLatin1String("REDACTED"));
        CHECK(m->repeat);
        CHECK(m->color == std::vector<double>{0.0, 0.0, 1.0});
        CHECK(m->interiorColor == std::vector<double>{0.0, 0.0, 0.0});
        CHECK(m->author == QString::fromLatin1(annot_product::kLoginName));
    }
    CHECK(product.document()->journal()->position() == 2);   // two gestures, two commands

    // SAVED.
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::FileSave, core::KeyContext::Viewer));
    product.settle();
    REQUIRE_FALSE(product.document()->journal()->isDirty());

    // 1. EVERY CONTENT STREAM BYTE-IDENTICAL.
    const auto after = contentOf(product.openedPath);
    REQUIRE(after.size() == before.size());
    for (std::size_t page = 0; page < before.size(); ++page) {
        INFO("page " << page + 1);
        CHECK(after[page] == before[page]);
    }

    // 2. THE TEXT STILL THERE: a fresh engine extracts the marked phrase from the saved file.
    pdf::DocumentBackendImpl fresh;
    auto reopened = fresh.open(product.openedPath, nullptr);
    REQUIRE(reopened);
    core::text::PageTextModel model(0);
    REQUIRE(fresh.textExtractor(reopened.value())->extractPage(reopened.value(), 0, model));
    QString extracted;
    for (const core::text::TextChar& c : model.chars())
        extracted += c.codepoint < 0x10000 ? QChar(static_cast<char16_t>(c.codepoint)) : QChar(0xFFFD);
    INFO("phrase: " << phrase.toStdString());
    CHECK(extracted.contains(phrase));
    const core::AnnotationStore* store = fresh.annotations(reopened.value());
    REQUIRE(store != nullptr);
    int saved = 0;
    for (const Annotation& a : store->all()) saved += a.subtype == AnnotationSubtype::Redact ? 1 : 0;
    CHECK(saved == 2);

    // 3. SHOWN IS SAVED, at 72 and 144 dpi.
    core::IDocumentBackend& backend = product.root->services().backend;
    product.root->services().renderScheduler.waitForIdle(4000);
    long long total = 0;
    for (const double scale : {1.0, 2.0}) {
        const long long differing = differingBytes(rasterOf(backend, product.document()->handle(), 0, scale),
                                                   rasterOf(fresh, reopened.value(), 0, scale));
        CHECK(differing == 0);
        total += differing;
    }
    std::cout << "A-100 through the tool: 2 marks, " << before.size() << " page(s) of content byte-identical, the phrase \""
              << phrase.toStdString() << "\" still extracted; A-34: " << total << " differing bytes live against the file\n";
    CHECK(fresh.close(reopened.value()));
    ui::clearSessionToolChanges();
}

TEST_CASE("a selected mark's overlay is changed from the properties bar as one command", "[redactink][A-100][D6-25]")
{
    // The corpus's mark selected with a click, its overlay text typed into the bar and its repeat ticked: two commands,
    // each ONE undo step, each redrawing its `/RO` -- and the page's content untouched by either.
    Product product;
    ui::PageView* view = product.open("ordinary/annots-every-type.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    const Annotation* mark = nullptr;
    for (const Annotation& a : product.store().all())
        if (a.subtype == AnnotationSubtype::Redact) mark = &a;
    REQUIRE(mark != nullptr);
    const core::AnnotationId id = mark->id;
    product.overlay()->select({id});
    product.settle();
    ui::PropertiesBar* bar = product.document()->propertiesBar();
    REQUIRE(bar->subject() == ui::PropertiesBar::Subject::Selection);
    REQUIRE(bar->overlayTextField()->isEnabled());
    CHECK(bar->overlayTextField()->text() == QLatin1String("REDACTED"));
    CHECK_FALSE(bar->repeatBox()->isChecked());
    CHECK(bar->fillBox()->isEnabled());
    bar->overlayTextField()->setFocus(Qt::OtherFocusReason);
    bar->overlayTextField()->setText(QStringLiteral("ACCOUNT"));
    key(bar->overlayTextField(), Qt::Key_Return);
    product.settle();
    CHECK(product.document()->journal()->position() == 1);
    bar->repeatBox()->setChecked(true);
    product.settle();
    CHECK(product.document()->journal()->position() == 2);
    const std::optional<std::size_t> at = product.store().find(id);
    REQUIRE(at);
    CHECK(product.store().all()[*at].overlayText == QLatin1String("ACCOUNT"));
    CHECK(product.store().all()[*at].repeat);
    // A selection that is not all marks greys both.
    for (const Annotation& a : product.store().all()) {
        if (a.subtype == AnnotationSubtype::Square) {
            product.overlay()->select({a.id});
            break;
        }
    }
    product.settle();
    CHECK_FALSE(bar->overlayTextField()->isEnabled());
    CHECK_FALSE(bar->repeatBox()->isEnabled());
}

TEST_CASE("text already selected is marked for redaction by the command at once", "[redactink][A-100][D6-25]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    (void)readingOf(product, 0);
    ui::SelectionOverlay* selection = product.document()->selection();
    selection->selectAll();
    product.settle();
    REQUIRE(selection->hasSelection());
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::RedactMark, core::KeyContext::Viewer));
    product.settle();
    // One mark a page the selection crosses (D6-33(g)'s rule for markup): `text-a4.pdf` has three.
    int marks = 0;
    for (const Annotation& a : product.store().all()) marks += a.subtype == AnnotationSubtype::Redact ? 1 : 0;
    CHECK(marks == view->pageCount());
    CHECK(product.document()->journal()->position() == 1);   // ONE command
    CHECK(view->tools().armed() == ui::ToolKind::Select);   // marked, and nothing left armed
    CHECK_FALSE(selection->hasSelection());
}
