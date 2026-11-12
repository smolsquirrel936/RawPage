// tst_linkproduct -- links through the product's own composition. Stage 7 T12, decision D7-12, guide cases A-83 ... A-85
// and A-87 (`Stage7-Assembly-Testing-Guide.md` §5.14).
//
// `app/CompositionRoot` and `ui/MainWindow` as `main.cpp` builds them, over the real engine and copies of
// `corpus/assembly/refs-every-kind.pdf` and `corpus/ordinary/text-a4.pdf`. The shell is a RECORDER
// (`Options::openExternally`), so nothing here opens a web page or launches a program; a link FOLLOWED is graded by the
// IDENTITY the page it lands on shows (D7-17), read from that page's text as the engine extracts it.
//
// WHAT IT DOES NOT COVER (convention 19): the QUESTION before a link opens something is answered through
// `LinkOpenDialog::setAnswerForTesting` -- offscreen there is nobody to ask -- and the link properties through
// `LinkPropertiesDialog::setAnswerForTesting`; the dialogs a person sees, a browser opening, and what Acrobat and Firefox
// draw and follow are M-06's. A-84's page of addresses is a page THIS FILE builds (D7-35(i)): the corpus gained no file.

#include <catch2/catch_test_macros.hpp>

#include "AnnotProduct.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/LinkActions.h"
#include "core/text/LayoutAnalyzer.h"
#include "ui/dialogs/LinkPropertiesDialog.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/viewer/ToolController.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include <cmath>
#include <iostream>
#include <memory>
#include <set>

using namespace rawpage;
using annot_product::Product;
using core::LinkFollow;

namespace {

struct Answers {
    ~Answers()
    {
        ui::LinkPropertiesDialog::setAnswerForTesting({});
        ui::LinkOpenDialog::setAnswerForTesting({});
        ui::DestinationNameDialog::setAnswerForTesting({});
    }
};

// The product, its shell a recorder.
struct Linking {
    QStringList handedToShell;
    std::unique_ptr<Product> product;
    Linking()
    {
        product = std::make_unique<Product>([this](app::CompositionRoot::Options& options, const QString&) {
            options.openExternally = [this](const QString& path) {
                handedToShell << path;
                return true;
            };
        });
    }
    Product& p() { return *product; }
    bool dispatch(core::ActionId id, core::KeyContext context = core::KeyContext::Viewer)
    {
        const bool done = static_cast<bool>(p().root->services().actions.dispatch(id, context));
        QCoreApplication::processEvents();
        return done;
    }
};

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

QString textOf(const core::text::PageReading& reading)
{
    QString out;
    for (int p = 0; p < reading.size(); ++p) {
        const char32_t c = reading.charAt(p).codepoint;
        out += c < 0x10000 ? QChar(static_cast<char16_t>(c)) : QChar(0xFFFD);
    }
    return out;
}

// The identity the page in front SHOWS -- D7-17's oracle: the token of capitals, a hyphen and four digits.
QString shownIdentity(Product& product)
{
    const int page = product.view->controller().currentPage();
    const QRegularExpression identity(QStringLiteral("[A-Z][A-Z0-9]*-[0-9]{4}"));
    const QRegularExpressionMatch match = identity.match(textOf(readingOf(product, page)));
    return match.hasMatch() ? match.captured(0) : QString();
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

// A CLICK with the Hand tool on the middle of `link`, its page brought into view first.
void clickLink(Product& product, const core::Annotation& link)
{
    ui::PageView& view = *product.view;
    view.revealPageRect(link.page, link.rect);
    product.settle();
    const QPoint at = widgetAt(view, link.page,
                               core::PdfPoint{link.rect.left() + link.rect.size.width / 2, link.rect.bottom() + link.rect.size.height / 2});
    REQUIRE(view.viewport()->rect().contains(at));
    mouse(view, QEvent::MouseButtonPress, at);
    mouse(view, QEvent::MouseButtonRelease, at);
    product.settle();
}

const core::Annotation& linkObject(Product& product, long long object)
{
    const std::optional<std::size_t> index = product.store().findObject(object);
    REQUIRE(index);
    return product.store().at(*index);
}

const core::Annotation& byName(Product& product, const QString& nm)
{
    const std::optional<std::size_t> index = product.store().find(core::AnnotationId::ofCreated(nm));
    REQUIRE(index);
    return product.store().at(*index);
}

// A one-page PDF of `lines` in 11-point Helvetica, a line every 20 points from the top.
QByteArray pageOfLines(const QStringList& lines)
{
    QByteArray content = "BT /F1 11 Tf 14 TL 72 740 Td\n";
    for (const QString& line : lines) {
        QByteArray escaped;
        for (const char c : line.toLatin1()) {
            if (c == '\\' || c == '(' || c == ')') escaped += '\\';
            escaped += c;
        }
        content += "(" + escaped + ") Tj 0 -20 Td\n";
    }
    content += "ET\n";
    std::vector<QByteArray> objects{
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "endstream",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
    };
    QByteArray out = "%PDF-1.7\n";
    std::vector<qsizetype> offsets;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(out.size());
        out += QByteArray::number(static_cast<int>(i + 1)) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const qsizetype xref = out.size();
    out += "xref\n0 " + QByteArray::number(static_cast<int>(objects.size() + 1)) + "\n0000000000 65535 f \n";
    for (const qsizetype offset : offsets) out += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    out += "trailer\n<< /Size " + QByteArray::number(static_cast<int>(objects.size() + 1)) + " /Root 1 0 R >>\nstartxref\n"
           + QByteArray::number(xref) + "\n%%EOF\n";
    return out;
}

} // namespace

// ==========================================================================================
// A-85 -- following: to the page it shows; what is asked, refused and never run
// ==========================================================================================

TEST_CASE("ui.composition.links a link clicked with the hand goes to the page it shows; a URL and a file asked, a program refused, a script never run",
          "[linkproduct][A-85]")
{
    Answers answers;
    Linking linking;
    Product& product = linking.p();
    ui::PageView* view = product.open("assembly/refs-every-kind.pdf");
    view->setZoom(core::render::Zoom{0.8});
    product.settle();
    view->setTool(ui::ToolKind::Hand);
    REQUIRE(view->tools().effective() == ui::ToolKind::Hand);

    // BY REFERENCE, BY A GoTo ACTION, BY A NAME IN THE TREE AND IN THE DICTIONARY, BY A GoTo TO A NAME: each lands on the
    // page whose identity the label gives (D7-17).
    for (const auto& [object, identity] : {std::pair{19LL, "P-0009"}, std::pair{20LL, "P-0006"}, std::pair{21LL, "P-0007"},
                                           std::pair{22LL, "P-0004"}, std::pair{23LL, "P-0010"}}) {
        INFO("link " << object);
        clickLink(product, linkObject(product, object));
        REQUIRE(product.window->lastLinkFollow());
        CHECK(product.window->lastLinkFollow()->step == LinkFollow::Step::Navigate);
        CHECK(shownIdentity(product) == QLatin1String(identity));
    }
    CHECK(linking.handedToShell.isEmpty());

    // LINKS OF EVERY OTHER KIND, made on page 1, one command.
    const auto make = [&product](double y, core::OutlineAction action) {
        return core::newLink(0, core::PdfRect{core::PdfPoint{400, y}, core::PdfSize{150, 16}}, {}, std::move(action),
                             QStringLiteral("Tester"), 1791504000);
    };
    core::OutlineAction uri;
    uri.kind = core::OutlineAction::Kind::Uri;
    uri.uri = QStringLiteral("https://example.com/followed");
    core::OutlineAction program;
    program.kind = core::OutlineAction::Kind::Launch;
    program.file = QStringLiteral("calc.exe");
    core::OutlineAction script;
    script.kind = core::OutlineAction::Kind::JavaScript;
    script.script = QStringLiteral("app.launchURL('https://example.com/planted');");
    core::OutlineAction file;
    file.kind = core::OutlineAction::Kind::Launch;
    file.file = QStringLiteral("notes.txt");
    core::OutlineAction print;
    print.kind = core::OutlineAction::Kind::Named;
    print.name = QStringLiteral("Print");
    const std::vector<core::Annotation> made{make(100, uri), make(130, program), make(160, script), make(190, file), make(220, print)};
    REQUIRE(product.document()->journal()->run(std::make_unique<core::AddAnnotationCommand>(made)));
    product.settle();

    // THE URL -- ASKED: "no" opens nothing; "yes" hands the address to the shell, ONCE.
    std::vector<std::pair<LinkFollow::Step, QString>> asked;
    bool yes = false;
    ui::LinkOpenDialog::setAnswerForTesting([&asked, &yes](LinkFollow::Step step, const QString& what) {
        asked.emplace_back(step, what);
        return yes;
    });
    clickLink(product, byName(product, made[0].nm));
    REQUIRE(asked.size() == 1);
    CHECK(asked.back().first == LinkFollow::Step::OpenUri);
    CHECK(asked.back().second == uri.uri);
    CHECK(linking.handedToShell.isEmpty());
    yes = true;
    clickLink(product, byName(product, made[0].nm));
    CHECK(linking.handedToShell == QStringList{uri.uri});

    // A PROGRAM -- REFUSED by D6-17's list, never asked, nothing handed over.
    asked.clear();
    clickLink(product, byName(product, made[1].nm));
    CHECK(product.window->lastLinkFollow()->step == LinkFollow::Step::Refused);
    CHECK(asked.empty());
    // A SCRIPT -- NEVER RUN: its planted side effect, a URL handed to the shell, absent.
    clickLink(product, byName(product, made[2].nm));
    CHECK(product.window->lastLinkFollow()->step == LinkFollow::Step::NotRun);
    CHECK(product.window->lastLinkFollow()->reason == QLatin1String("links.follow.script"));
    // A MENU COMMAND -- the print -- written and never run.
    clickLink(product, byName(product, made[4].nm));
    CHECK(product.window->lastLinkFollow()->step == LinkFollow::Step::NotRun);
    CHECK(asked.empty());
    CHECK(linking.handedToShell == QStringList{uri.uri});
    // ANOTHER FILE -- asked, then handed over by its path beside the document.
    clickLink(product, byName(product, made[3].nm));
    REQUIRE(asked.size() == 1);
    CHECK(asked.back().first == LinkFollow::Step::LaunchFile);
    REQUIRE(linking.handedToShell.size() == 2);
    CHECK(QFileInfo(linking.handedToShell.back()).fileName() == QStringLiteral("notes.txt"));
    CHECK(QFileInfo(linking.handedToShell.back()).absolutePath() == QFileInfo(product.openedPath).absolutePath());

    // A GoToR -- ASKED, then the file opened (this one: its tab in front) at its page.
    asked.clear();
    clickLink(product, linkObject(product, 24));
    REQUIRE(asked.size() == 1);
    CHECK(asked.back().first == LinkFollow::Step::OpenRemote);
    CHECK(shownIdentity(product) == QStringLiteral("P-0005"));
    CHECK(linking.handedToShell.size() == 2);

    // A DRAG that starts on a link is a PAN, not a follow: the last link followed is still the GoToR's, to page 5.
    const core::Annotation& toNine = linkObject(product, 19);
    view->revealPageRect(toNine.page, toNine.rect);
    product.settle();
    const QPoint from = widgetAt(*view, toNine.page, core::PdfPoint{toNine.rect.left() + 20, toNine.rect.bottom() + 5});
    mouse(*view, QEvent::MouseButtonPress, from);
    mouse(*view, QEvent::MouseMove, from + QPoint(0, 40));
    mouse(*view, QEvent::MouseButtonRelease, from + QPoint(0, 40));
    product.settle();
    CHECK(product.window->lastLinkFollow()->step == LinkFollow::Step::OpenRemote);

    // `ENTER` ON A SELECTED LINK follows it (D7-35): the Selection tool, the link picked, the key's command.
    view->setTool(ui::ToolKind::Select);
    product.overlay()->select({core::AnnotationId::ofObject(21)});
    REQUIRE(linking.dispatch(core::ActionId::AnnotFinishShape, core::KeyContext::AnnotationTools));
    product.settle();
    CHECK(shownIdentity(product) == QStringLiteral("P-0007"));
}

// ==========================================================================================
// A-83 -- a link made from text, one per line, over its glyphs; and the tool's rectangle on the raster
// ==========================================================================================

TEST_CASE("ui.composition.links link.create with text selected makes one link per line, each over its line's glyphs, one step",
          "[linkproduct][A-83]")
{
    Answers answers;
    Linking linking;
    Product& product = linking.p();
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    const core::text::PageReading& reading = readingOf(product, 0);
    // THREE LINES of the body, from the start of one to the end of the line two below it.
    int first = -1;
    for (int p = 0; p < reading.size() && first < 0; ++p) {
        if (reading.charAt(p).fontSize < 12 && reading.charAt(p).codepoint > U' ') first = p;
    }
    REQUIRE(first >= 0);
    const int firstLine = reading.lineOf(first);
    const int last = reading.lineSpan(firstLine + 2).end - 1;
    // THE SELECTION TOOL'S DRAG, from the caret before the first glyph to the caret after the last -- a person's gesture.
    REQUIRE(view->tools().armed() == ui::ToolKind::Select);
    const auto inGlyph = [&reading](int position, double across) {
        const core::PdfRect b = reading.charAt(position).box;
        return core::PdfPoint{b.left() + b.size.width * across, b.bottom() + b.size.height / 2};
    };
    view->revealPageRect(0, reading.charAt(first).box);
    product.settle();
    const QPoint from = widgetAt(*view, 0, inGlyph(first, 0.15));
    const QPoint to = widgetAt(*view, 0, inGlyph(last, 0.85));
    REQUIRE(view->viewport()->rect().contains(from));
    REQUIRE(view->viewport()->rect().contains(to));
    mouse(*view, QEvent::MouseButtonPress, from);
    mouse(*view, QEvent::MouseMove, (from + to) / 2);
    mouse(*view, QEvent::MouseMove, to);
    product.settle();
    mouse(*view, QEvent::MouseButtonRelease, to);
    product.settle();
    REQUIRE(product.document()->selection()->hasSelection());

    ui::LinkPropertiesDialog::setAnswerForTesting([](const core::Annotation&, int, const QStringList&, const core::Destination&) {
        ui::LinkPropertiesDialog::Choice choice;
        choice.action.kind = core::OutlineAction::Kind::Uri;
        choice.action.uri = QStringLiteral("https://example.com/from-text");
        return std::optional<ui::LinkPropertiesDialog::Choice>{choice};
    });
    const std::size_t before = product.store().size();
    const std::size_t steps = product.document()->journal()->position();
    REQUIRE(linking.dispatch(core::ActionId::LinkCreate));
    product.settle();
    CHECK(product.document()->journal()->position() == steps + 1);   // ONE command
    std::vector<core::Annotation> links;
    for (const core::Annotation& a : product.store().all()) {
        if (a.id.kind == core::AnnotationId::Kind::Created && a.subtype == core::AnnotationSubtype::Link) links.push_back(a);
    }
    REQUIRE(product.store().size() == before + links.size());
    CHECK(links.size() == 3);   // one per line
    // EACH SELECTED GLYPH'S BOX inside one link's quadrilateral -- the glyphs' own line -- and every link's action the one asked.
    for (int p = first; p <= last; ++p) {
        if (reading.charAt(p).codepoint <= U' ') continue;
        const core::PdfRect box = reading.charAt(p).box;
        const core::PdfPoint centre{box.left() + box.size.width / 2, box.bottom() + box.size.height / 2};
        int inside = 0;
        for (const core::Annotation& link : links) {
            const std::optional<core::PdfRect> quad = core::boundsOfPoints(link.quadPoints);
            REQUIRE(quad);
            if (quad->contains(centre)) ++inside;
        }
        INFO("glyph " << p);
        CHECK(inside == 1);
    }
    for (const core::Annotation& link : links) {
        CHECK(link.quadPoints.size() == 8);
        CHECK(link.action.uri == QStringLiteral("https://example.com/from-text"));
    }
}

TEST_CASE("ui.composition.links the Link tool's rectangle is drawn where it was dragged, its border on the raster",
          "[linkproduct][A-83][A-86]")
{
    Answers answers;
    Linking linking;
    Product& product = linking.p();
    ui::PageView* view = product.open("assembly/refs-none.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();
    ui::LinkPropertiesDialog::setAnswerForTesting([](const core::Annotation&, int, const QStringList&, const core::Destination&) {
        ui::LinkPropertiesDialog::Choice choice;
        core::Destination to;
        to.pageIndex = 5;
        to.fit = core::Destination::Fit::Fit;
        choice.action = core::OutlineAction::goTo(to);
        choice.width = 2.0;
        choice.colour = {0.8, 0, 0};
        return std::optional<ui::LinkPropertiesDialog::Choice>{choice};
    });
    REQUIRE(linking.dispatch(core::ActionId::LinkCreate));
    REQUIRE(view->tools().armed() == ui::ToolKind::Link);
    const core::PdfRect wanted{core::PdfPoint{100, 600}, core::PdfSize{200, 40}};
    view->revealPageRect(0, wanted);
    product.settle();
    const QPoint from = widgetAt(*view, 0, core::PdfPoint{wanted.left(), wanted.top()});
    const QPoint to = widgetAt(*view, 0, core::PdfPoint{wanted.right(), wanted.bottom()});
    mouse(*view, QEvent::MouseButtonPress, from);
    mouse(*view, QEvent::MouseMove, (from + to) / 2);
    mouse(*view, QEvent::MouseMove, to);
    mouse(*view, QEvent::MouseButtonRelease, to);
    product.settle();
    std::optional<core::Annotation> made;
    for (const core::Annotation& a : product.store().all())
        if (a.id.kind == core::AnnotationId::Kind::Created && a.subtype == core::AnnotationSubtype::Link) made = a;
    REQUIRE(made);
    const double pixel = 1.5 / view->pixelsPerPoint();
    CHECK(std::abs(made->rect.left() - wanted.left()) <= pixel);
    CHECK(std::abs(made->rect.right() - wanted.right()) <= pixel);
    CHECK(std::abs(made->rect.bottom() - wanted.bottom()) <= pixel);
    CHECK(std::abs(made->rect.top() - wanted.top()) <= pixel);

    // ON THE RASTER (rule 6): the page drawn by the engine, its annotations on, is RED along the rectangle's edges and
    // unchanged inside it -- a border, and no fill.
    const QImage drawn = product.page(0, true);
    const QImage bare = product.page(0, false);
    const QRectF onSheet = annot_product::onSheet(*view, 0, made->rect);
    int red = 0, edge = 0;
    for (int x = static_cast<int>(onSheet.left()) + 3; x < static_cast<int>(onSheet.right()) - 3; x += 4) {
        for (const int y : {static_cast<int>(std::lround(onSheet.top() + 1)), static_cast<int>(std::lround(onSheet.bottom() - 2))}) {
            ++edge;
            const QRgb p = drawn.pixel(x, y);
            if (qRed(p) > 150 && qGreen(p) < 100 && qBlue(p) < 100) ++red;
        }
    }
    const QPoint inside = onSheet.center().toPoint();
    std::cout << "A-83/A-86 link border: " << red << " of " << edge << " edge samples red\n";
    CHECK(red >= edge * 9 / 10);
    CHECK(drawn.pixel(inside) == bare.pixel(inside));
}

// ==========================================================================================
// A-84's engine half -- the addresses a page's text holds, as PDFium extracts it; A-87 through the window
// ==========================================================================================

TEST_CASE("ui.composition.links link.autoDetect links exactly the addresses on a page, no near-miss, in one step", "[linkproduct][A-84]")
{
    Linking linking;
    Product& product = linking.p();
    const QStringList lines{
        QStringLiteral("Visit https://example.com/path?q=1 today."),
        QStringLiteral("Write to mailto:someone@example.org now"),
        QStringLiteral("Docs at www.rawpage.test/docs."),
        QStringLiteral("(see https://example.com/a_(b))"),
        QStringLiteral("example.com http:/broken.example www.nodot http://localhost"),
        QStringLiteral("ftp://files.example.com emailme@example.com xhttps://bad.example"),
    };
    const QStringList expected{QStringLiteral("https://example.com/path?q=1"), QStringLiteral("mailto:someone@example.org"),
                               QStringLiteral("http://www.rawpage.test/docs"), QStringLiteral("https://example.com/a_(b)")};
    const QString path = QDir(product.scratch.path()).filePath(QStringLiteral("addresses.pdf"));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(pageOfLines(lines));
    }
    product.openedPath = path;
    product.window->openFiles({path});
    product.view = product.window->currentView();
    REQUIRE(product.view != nullptr);
    product.settle();
    const std::size_t steps = product.document()->journal()->position();
    REQUIRE(linking.dispatch(core::ActionId::LinkAutoDetect, core::KeyContext::Global));
    QElapsedTimer timer;
    timer.start();
    while (product.document()->journal()->position() == steps && timer.elapsed() < 30000) product.settle();
    CHECK(product.document()->journal()->position() == steps + 1);   // ONE command
    QStringList found;
    for (const core::Annotation& a : product.store().all()) {
        if (a.subtype == core::AnnotationSubtype::Link) found << a.action.uri;
    }
    found.sort();
    QStringList want = expected;
    want.sort();
    CHECK(found == want);
    // AGAIN: every address is linked already, and nothing is made.
    REQUIRE(linking.dispatch(core::ActionId::LinkAutoDetect, core::KeyContext::Global));
    product.settle();
    product.settle();
    CHECK(product.document()->journal()->position() == steps + 1);
}

TEST_CASE("ui.composition.links link.verify reports the link a deleted page broke, and nothing else", "[linkproduct][A-87]")
{
    Linking linking;
    Product& product = linking.p();
    product.open("assembly/refs-every-kind.pdf");
    REQUIRE(linking.dispatch(core::ActionId::LinkVerify, core::KeyContext::Global));
    CHECK(product.window->lastLinkReport().isEmpty());
    REQUIRE(product.document()->journal()->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{8})));
    product.settle();
    REQUIRE(linking.dispatch(core::ActionId::LinkVerify, core::KeyContext::Global));
    REQUIRE(product.window->lastLinkReport().size() == 1);
    CHECK(product.window->lastLinkReport().front().contains(QLatin1Char('1')));   // page 1, where the link is
    CHECK(linking.handedToShell.isEmpty());
}
