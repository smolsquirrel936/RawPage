// tst_selectionink -- Stage 5 T5, guide cases A-42 … A-48 (`Stage5-TextSearch-Testing-Guide.md` §5.7):
// a selection made with the mouse, through the PRODUCT'S OWN COMPOSITION -- the real engine extracting,
// the real analyzer ordering, the real widgets painting -- graded twice:
//
//   * its TEXT against the GENERATOR's labels (`corpus/labels/`, rule 7), never against PDFium;
//   * its HIGHLIGHT against the RENDERED INK (rule 6): the page is grabbed with nothing selected, each
//     glyph's ink is found in that raster inside a window the GENERATOR's numbers put it in, mapped to
//     the widget by THIS FILE's own arithmetic (the slot's corner and scale, never
//     `PageLayout::toContent`), and every selected glyph's ink must lie inside the product's highlight
//     rectangles while no other glyph's may. Each check has a control that fails: the same rectangles
//     moved 2 pt.
//
// WHY `tests/composition` AND NOT `tests/ui` (a deviation from the plan's file list): a highlight judged
// against ink needs the engine to draw the ink, and `rawpage_ui_tests` links none by design -- its fake
// rasterizer paints flat blue. This binary is the one that links the engine, the widgets and `app/`'s
// composition together (`Implementation-Plan.md` §2.1). The widget half -- keys, lanes, `Esc`, panes,
// tabs -- is `tests/ui/tst_selecttool.cpp`.
//
// WHAT THIS DOES NOT COVER (convention 19): a real pointer and a real screen. The offscreen platform
// rasterizes at a device pixel ratio of 1 and the gestures are synthetic events -- whether a highlight
// sits on the glyphs at 400 % on a 150 % monitor is manual case M-01's. And every document here is OURS
// (D5-23): nothing below says anything about a real document.

#include <catch2/catch_test_macros.hpp>

#include "app/CompositionRoot.h"
#include "core/render/RenderScheduler.h"
#include "core/text/TextSelection.h"
#include "tests/render/TextOracle.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/viewer/ViewportController.h"
#include "ui/viewer/tools/SelectTool.h"
#include "ui/shell/Catalog.h"
#include "ui/write/FindReplaceBar.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using test_oracle::Direction;
using test_oracle::Face;
using test_oracle::Line;

namespace {

// ---------------------------------------------------------------------------------------------
// The product, composed as `app/main.cpp` composes it, over a scratch directory
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
        // EVERY PATH THE SCRATCH DIRECTORY'S (`CLAUDE.md` -> *Traps*): settings, session and recovery.
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

    // A COPY of a corpus file, opened -- the corpus is checked byte for byte and is never opened in place.
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
        view->setFitMode(core::render::FitMode::Width);
        settle();
        REQUIRE(view->viewport()->width() > 400);
        return view;
    }

    // Until the engine thread has nothing left and every delivery has been pumped -- the composition's
    // wake handler posts `deliverReady()`, so processing events IS the pump.
    void settle()
    {
        for (int round = 0; round < 12; ++round) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            root->services().renderScheduler.waitForIdle(2000);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
        view->refresh();
        view->viewport()->repaint();
    }

    ui::SelectionOverlay& selection() const { return *window->currentDocument()->selection(); }

    void mouse(QEvent::Type type, QPointF pos, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(type, pos, view->viewport()->mapToGlobal(pos), Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QCoreApplication::processEvents();
    }

    void drag(QPointF from, QPointF to, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        mouse(QEvent::MouseButtonPress, from, modifiers);
        mouse(QEvent::MouseMove, (from + to) / 2.0, modifiers);
        mouse(QEvent::MouseMove, to, modifiers);
        mouse(QEvent::MouseButtonRelease, to, modifiers);
        settle();
    }

    void click(QPointF at, int times)
    {
        for (int i = 0; i < times; ++i) {
            mouse(QEvent::MouseButtonPress, at);
            mouse(QEvent::MouseButtonRelease, at);
        }
        settle();
    }

    // THE PAGE AS DRAWN, with nothing selected over it: the raster the ink is found in.
    QImage cleanRaster()
    {
        selection().clear();
        settle();
        return view->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
    }

    // Scroll so a page's PDF y is at a fraction of the viewport's height.
    void scrollTo(int page, double pdfY, double fraction)
    {
        const core::render::PageSlot* slot = view->layoutEngine().layout().slotForPage(page);
        REQUIRE(slot != nullptr);
        const double scale = slot->rect.size.width / slot->cropBox.size.width;
        const double y = slot->rect.top() + (slot->cropBox.top() - pdfY) * scale;
        const double dpr = view->devicePixelRatioF();
        view->controller().setScroll(
            core::DevicePoint{view->controller().scroll().x, y - fraction * view->viewport()->height() * dpr});
        settle();
    }
};

// ---------------------------------------------------------------------------------------------
// THIS FILE'S OWN page-to-widget mapping (rule 6): an unrotated page, from its slot's corner and the
// ratio of its drawn width to its crop box -- none of `PageLayout::toContent`'s code.
// ---------------------------------------------------------------------------------------------

struct PageMap {
    double left = 0, top = 0, scale = 1, cropLeft = 0, cropTop = 0, dpr = 1;

    QPointF toWidget(double x, double y) const
    {
        return QPointF((left + (x - cropLeft) * scale) / dpr, (top + (cropTop - y) * scale) / dpr);
    }
    QRectF toWidget(double x0, double y0, double x1, double y1) const
    {
        return QRectF(toWidget(x0, y1), toWidget(x1, y0)).normalized();
    }
};

PageMap mapOf(const ui::PageView& view, int page)
{
    const core::render::PageSlot* slot = view.layoutEngine().layout().slotForPage(page);
    REQUIRE(slot != nullptr);
    REQUIRE(slot->rotation == core::PageRotation::None);
    const core::DeviceRect visible = view.controller().visibleRect();
    PageMap map;
    map.dpr = view.devicePixelRatioF();
    map.scale = slot->rect.size.width / slot->cropBox.size.width;
    map.left = slot->rect.left() - visible.left();
    map.top = slot->rect.top() - visible.top();
    map.cropLeft = slot->cropBox.left();
    map.cropTop = slot->cropBox.top();
    return map;
}

// The PRODUCT's highlight rectangles for one page, in widget pixels.
std::vector<QRectF> highlightsOf(const ui::SelectionOverlay& selection, const ui::PageView& view, int page,
                                 double shiftPoints = 0.0)
{
    std::vector<QRectF> out;
    const core::DeviceRect visible = view.controller().visibleRect();
    const double dpr = view.devicePixelRatioF();
    const core::render::PageLayout& layout = view.layoutEngine().layout();
    const double shift = shiftPoints * layout.presentedScale();
    for (const core::DeviceRect& r : selection.highlightRects(layout, page))
        out.emplace_back((r.left() + shift - visible.left()) / dpr, (r.top() - visible.top()) / dpr,
                         r.size.width / dpr, r.size.height / dpr);
    return out;
}

bool dark(const QImage& image, int x, int y)
{
    return x >= 0 && y >= 0 && x < image.width() && y < image.height() && qGray(image.pixel(x, y)) < 128;
}

bool insideAny(const std::vector<QRectF>& rects, QPointF p, double inflate)
{
    for (const QRectF& r : rects)
        if (r.adjusted(-inflate, -inflate, inflate, inflate).contains(p)) return true;
    return false;
}

struct InkTally {
    int glyphs = 0;       // glyphs whose ink the oracle found
    int selected = 0;     // ... of them selected
    int outside = 0;      // a selected glyph with ink OUTSIDE every highlight
    int intruding = 0;    // an unselected glyph with ink INSIDE a highlight
};

// One glyph's window: its ink must be all inside (selected) or all outside (not) the highlights.
void judge(const QImage& raster, const QRectF& window, bool selected, const std::vector<QRectF>& highlights,
           InkTally& tally)
{
    const QRect pixels = window.adjusted(1, 1, -1, -1).toAlignedRect().intersected(raster.rect());
    bool any = false, out = false, in = false;
    for (int y = pixels.top(); y <= pixels.bottom(); ++y)
        for (int x = pixels.left(); x <= pixels.right(); ++x) {
            if (!dark(raster, x, y)) continue;
            any = true;
            const QPointF centre(x + 0.5, y + 0.5);
            if (!insideAny(highlights, centre, 1.0)) out = true;
            if (insideAny(highlights, centre, -1.0)) in = true;
        }
    if (!any) return;
    ++tally.glyphs;
    if (selected) {
        ++tally.selected;
        if (out) ++tally.outside;
    } else if (in) {
        ++tally.intruding;
    }
}

// PER GLYPH for a Helvetica line -- the window is the generator's advance, 0.25 em below the baseline to
// 0.8 em above -- and PER LINE for any other face, whose advances the label does not carry.
void judgeLine(const QImage& raster, const PageMap& map, const Line& line,
               const std::function<bool(int)>& selectedChar, const std::vector<QRectF>& highlights,
               InkTally& tally)
{
    if (line.face == Face::Other) {
        judge(raster, map.toWidget(line.box[0], line.box[1], line.box[2], line.box[3]), selectedChar(0),
              highlights, tally);
        return;
    }
    const std::vector<double> advances =
        test_oracle::helveticaAdvances(line.text, line.size, line.face == Face::HelveticaBold);
    double x = line.x0;
    for (int c = 0; c < static_cast<int>(advances.size()); ++c) {
        if (line.text.at(c) != QLatin1Char(' '))
            judge(raster,
                  map.toWidget(x, line.baseline - 0.25 * line.size, x + advances[static_cast<std::size_t>(c)],
                               line.baseline + 0.8 * line.size),
                  selectedChar(c), highlights, tally);
        x += advances[static_cast<std::size_t>(c)];
    }
}

// ---------------------------------------------------------------------------------------------
// The label's reading, and a point on one of its characters
// ---------------------------------------------------------------------------------------------

struct LabelPage {
    std::vector<Line> lines;                   // by id
    std::vector<std::vector<int>> blocks;      // line ids, blocks in READING order
};

LabelPage labelPage(const char* name, int index)
{
    const QJsonObject label = test_oracle::loadLabel(name);
    LabelPage page;
    page.lines = test_oracle::labelLines(label, index);
    for (const QJsonValue value : label.value(QStringLiteral("pages")).toArray()) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("index")).toInt(-1) != index) continue;
        std::map<int, std::vector<int>> byId;
        for (const QJsonValue block : object.value(QStringLiteral("blocks")).toArray()) {
            std::vector<int> lines;
            for (const QJsonValue line : block.toObject().value(QStringLiteral("lines")).toArray())
                lines.push_back(line.toInt());
            byId[block.toObject().value(QStringLiteral("id")).toInt()] = lines;
        }
        for (const QJsonValue id : object.value(QStringLiteral("order")).toArray())
            page.blocks.push_back(byId[id.toInt()]);
    }
    REQUIRE_FALSE(page.lines.empty());
    return page;
}

// The generator's text from character `fromChar` of line `fromLine` up to, and not including, character
// `toChar` of line `toLine`, in the label's reading order -- a line break inside a block, a blank line
// between blocks and between pages.
struct At {
    int line = 0;
    int ch = 0;
};

QString labelText(const LabelPage& page, At from, At to, bool* started, bool* finished)
{
    QString out;
    bool firstBlock = true;
    for (const std::vector<int>& block : page.blocks) {
        bool firstLine = true;
        for (const int id : block) {
            const QString& text = page.lines[static_cast<std::size_t>(id)].text;
            const bool opens = !*started && id == from.line;
            if (opens) *started = true;
            if (*started && !*finished) {
                const int begin = opens ? from.ch : 0;
                const bool closes = id == to.line;
                const int end = closes ? to.ch : static_cast<int>(text.size());
                if (!out.isEmpty() || !opens) out += firstLine ? (firstBlock ? QString() : QStringLiteral("\n\n")) : QStringLiteral("\n");
                out += text.mid(begin, end - begin);
                if (closes) *finished = true;
            }
            firstLine = false;
        }
        if (*started) firstBlock = false;
    }
    return out;
}

// A point in a Helvetica label character's left or right quarter, in page space: a caret before it, or
// after it (`PageReading::caretAt`'s half rule).
core::PdfPoint pointOn(const Line& line, int ch, bool after)
{
    const std::vector<double> advances =
        test_oracle::helveticaAdvances(line.text, line.size, line.face == Face::HelveticaBold);
    double x = line.x0;
    for (int c = 0; c < ch; ++c) x += advances[static_cast<std::size_t>(c)];
    const double w = advances[static_cast<std::size_t>(ch)];
    return core::PdfPoint{after ? x + 0.8 * w : x + 0.2 * w, line.baseline + 0.3 * line.size};
}

void report(const char* name, const InkTally& tally, const InkTally& control)
{
    std::printf("%s: %d glyphs judged against their ink, %d selected; %d selected glyphs outside the "
                "highlight, %d unselected inside it. Control (2 pt): %d outside, %d inside\n",
                name, tally.glyphs, tally.selected, tally.outside, tally.intruding, control.outside,
                control.intruding);
}

} // namespace

// =============================================================================================
// A-42 -- a drag selects the generator's text, and the highlight is on the ink
// =============================================================================================

TEST_CASE("a drag selects the generator's text and the highlight is on its ink", "[selectionink][A-42]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/columns-two.pdf");
    const LabelPage page = labelPage("columns-two", 0);

    // Column 1's first body block: from `frame` on its first line to `scope` on its third.
    const Line& first = page.lines[3];
    const Line& third = page.lines[5];
    REQUIRE(first.face == Face::HelveticaRegular);
    const int fromChar = static_cast<int>(first.text.indexOf(QStringLiteral("frame")));
    const int toChar = static_cast<int>(third.text.indexOf(QStringLiteral("scope"))) + 4;   // the `e`
    product.scrollTo(0, first.baseline, 0.3);
    const PageMap map = mapOf(*view, 0);
    const QImage raster = product.cleanRaster();

    product.drag(map.toWidget(pointOn(first, fromChar, false).x, pointOn(first, fromChar, false).y),
                 map.toWidget(pointOn(third, toChar, true).x, pointOn(third, toChar, true).y));

    bool started = false, finished = false;
    const QString expected = labelText(page, At{3, fromChar}, At{5, toChar + 1}, &started, &finished);
    REQUIRE(finished);
    CHECK(product.selection().copiedText().plain == expected);

    // THE INK: every glyph on the page the oracle can find, judged.
    const auto selectedChar = [&](int id) {
        return [id, fromChar, toChar](int c) {
            if (id == 3) return c >= fromChar;
            if (id == 4) return true;
            if (id == 5) return c <= toChar;
            return false;
        };
    };
    const std::vector<QRectF> highlights = highlightsOf(product.selection(), *view, 0);
    const std::vector<QRectF> moved = highlightsOf(product.selection(), *view, 0, 2.0);
    REQUIRE_FALSE(highlights.empty());
    InkTally tally, control;
    for (const Line& line : page.lines) {
        judgeLine(raster, map, line, selectedChar(static_cast<int>(&line - page.lines.data())), highlights, tally);
        judgeLine(raster, map, line, selectedChar(static_cast<int>(&line - page.lines.data())), moved, control);
    }
    report("A-42", tally, control);
    CHECK(tally.selected > 60);
    CHECK(tally.outside == 0);
    CHECK(tally.intruding == 0);
    // THE CONTROL: the same rectangles moved 2 pt put selected ink outside them, or other ink inside.
    CHECK(control.outside + control.intruding > 0);
}

// =============================================================================================
// A-43 -- word, line and block, against the generator's
// =============================================================================================

TEST_CASE("two three and four clicks select the generator's word line and block", "[selectionink][A-43]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/columns-two.pdf");
    const LabelPage page = labelPage("columns-two", 0);
    const Line& line = page.lines[4];   // column 1, the block's second line
    product.scrollTo(0, line.baseline, 0.4);
    const PageMap map = mapOf(*view, 0);

    const int wordStart = static_cast<int>(line.text.indexOf(QStringLiteral("budget")));
    const QPointF onWord = map.toWidget(pointOn(line, wordStart + 2, false).x, pointOn(line, wordStart + 2, false).y);

    product.click(onWord, 2);
    CHECK(product.selection().copiedText().plain == QStringLiteral("budget"));

    product.click(map.toWidget(20.0, 20.0), 1);   // a first click elsewhere: the next gesture starts at one
    product.click(onWord, 3);
    CHECK(product.selection().copiedText().plain == line.text);

    product.click(map.toWidget(20.0, 20.0), 1);
    product.click(onWord, 4);
    QString block;
    for (const std::vector<int>& lines : page.blocks) {
        if (std::find(lines.begin(), lines.end(), 4) == lines.end()) continue;
        for (const int id : lines) block += (block.isEmpty() ? QString() : QStringLiteral("\n")) + page.lines[static_cast<std::size_t>(id)].text;
    }
    CHECK(product.selection().copiedText().plain == block);
}

// =============================================================================================
// A-44 -- a selection crosses a page boundary, in continuous mode
// =============================================================================================

TEST_CASE("a drag across a page break selects both pages in order", "[selectionink][A-44]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-900.pdf");
    REQUIRE(core::render::isContinuous(view->controller().layoutMode()));
    const LabelPage page0 = labelPage("text-900", 0);
    const LabelPage page1 = labelPage("text-900", 1);

    // Page 1's last body line and page 2's first body line -- the page break between them on screen.
    const std::vector<int>& lastBlock = page0.blocks.back();
    const Line& last = page0.lines[static_cast<std::size_t>(lastBlock.back())];
    const Line& head = page1.lines[1];
    product.scrollTo(0, last.baseline, 0.35);
    const PageMap map0 = mapOf(*view, 0);
    const PageMap map1 = mapOf(*view, 1);
    const int fromChar = static_cast<int>(last.text.lastIndexOf(QLatin1Char(' '))) + 1;
    const int toChar = static_cast<int>(head.text.indexOf(QLatin1Char(' '))) - 1;
    const QPointF from = map0.toWidget(pointOn(last, fromChar, false).x, pointOn(last, fromChar, false).y);
    const QPointF to = map1.toWidget(pointOn(head, toChar, true).x, pointOn(head, toChar, true).y);
    REQUIRE(view->viewport()->rect().contains(from.toPoint()));
    REQUIRE(view->viewport()->rect().contains(to.toPoint()));
    const QImage raster = product.cleanRaster();

    product.drag(from, to);

    // The end of page 1, a blank line, then page 2 from its top: its header, then its first body line.
    bool started0 = false, finished0 = false;
    const QString tail = labelText(page0, At{lastBlock.back(), fromChar}, At{-1, 0}, &started0, &finished0);
    bool started1 = false, finished1 = false;
    const QString headText = labelText(page1, At{page1.blocks.front().front(), 0}, At{1, toChar + 1}, &started1, &finished1);
    REQUIRE(finished1);
    CHECK(product.selection().copiedText().plain == tail + QStringLiteral("\n\n") + headText);

    // The highlight on both pages is on their ink: the selected tail of page 1, and page 2's header and
    // first word.
    InkTally tally, control;
    const auto judgePage = [&](const LabelPage& page, const PageMap& map, int index, const std::function<bool(int, int)>& picked) {
        const std::vector<QRectF> highlights = highlightsOf(product.selection(), *view, index);
        const std::vector<QRectF> moved = highlightsOf(product.selection(), *view, index, 2.0);
        for (std::size_t id = 0; id < page.lines.size(); ++id) {
            const auto selected = [&, id](int c) { return picked(static_cast<int>(id), c); };
            judgeLine(raster, map, page.lines[id], selected, highlights, tally);
            judgeLine(raster, map, page.lines[id], selected, moved, control);
        }
    };
    judgePage(page0, map0, 0, [&](int id, int c) { return id == lastBlock.back() && c >= fromChar; });
    judgePage(page1, map1, 1, [&](int id, int c) { return id == 0 || (id == 1 && c <= toChar); });
    report("A-44", tally, control);
    CHECK(tally.selected > 10);
    CHECK(tally.outside == 0);
    CHECK(tally.intruding == 0);
    CHECK(control.outside + control.intruding > 0);
}

// =============================================================================================
// A-45 -- a multi-column drag follows the READING order
// =============================================================================================

TEST_CASE("a drag from the foot of one column to the head of the next follows the reading order", "[selectionink][A-45]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/columns-two.pdf");
    const LabelPage page = labelPage("columns-two", 0);

    // Column 1's foot and column 2's head, from the label's READING ORDER: the first block right of the
    // gutter opens column 2, and the last line read before it closes column 1. (By geometry alone the
    // lowest line left of the gutter is the FOOTNOTE, which is read after both columns.)
    int foot = -1, headLine = -1;
    for (const std::vector<int>& block : page.blocks) {
        const Line& opening = page.lines[static_cast<std::size_t>(block.front())];
        if (opening.x0 > 280.0) {
            headLine = block.front();
            break;
        }
        if (opening.size <= 12.0 && opening.baseline < 760.0) foot = block.back();
    }
    REQUIRE(foot >= 0);
    REQUIRE(headLine >= 0);
    const Line& footLine = page.lines[static_cast<std::size_t>(foot)];
    const Line& head = page.lines[static_cast<std::size_t>(headLine)];

    // The foot is on screen for the press; the head is ABOVE the viewport, and the move goes there -- a
    // drag's pointer may leave the widget, and the point is still the page's (`pagePointAt`).
    product.scrollTo(0, footLine.baseline, 0.6);
    const PageMap map = mapOf(*view, 0);
    const int fromChar = static_cast<int>(footLine.text.lastIndexOf(QLatin1Char(' '))) + 1;
    const int toChar = static_cast<int>(head.text.indexOf(QLatin1Char(' '))) - 1;
    product.drag(map.toWidget(pointOn(footLine, fromChar, false).x, pointOn(footLine, fromChar, false).y),
                 map.toWidget(pointOn(head, toChar, true).x, pointOn(head, toChar, true).y));

    bool started = false, finished = false;
    const QString expected = labelText(page, At{foot, fromChar}, At{headLine, toChar + 1}, &started, &finished);
    REQUIRE(finished);
    const QString copied = product.selection().copiedText().plain;
    CHECK(copied == expected);

    // THE CONTROL, spelled out: nothing of column 2 BELOW its head -- which is what a selection by the
    // rectangle between the two points would have taken.
    for (const std::vector<int>& block : page.blocks)
        for (const int id : block) {
            const Line& line = page.lines[static_cast<std::size_t>(id)];
            if (line.x0 > 280.0 && id != headLine && line.baseline < head.baseline && line.baseline > footLine.baseline)
                CHECK_FALSE(copied.contains(line.text));
        }
}

// =============================================================================================
// A-46 -- Alt+drag over a table's column is that column, copied as a one-column table
// =============================================================================================

TEST_CASE("an Alt drag over a ruled table's second column copies that column as a table", "[selectionink][A-46]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/table-ruled.pdf");
    const LabelPage page = labelPage("table-ruled", 0);
    const QJsonObject grid = test_oracle::loadLabel("table-ruled")
                                 .value(QStringLiteral("pages")).toArray().at(0).toObject()
                                 .value(QStringLiteral("tables")).toArray().at(0).toObject();
    const QJsonArray box = grid.value(QStringLiteral("box")).toArray();
    const double top = box.at(3).toDouble(), bottom = box.at(1).toDouble();

    // The table's cells are label lines inside its box; its column edges are their distinct left edges.
    std::vector<double> edges;
    std::vector<const Line*> cells;
    for (const Line& line : page.lines)
        if (line.baseline < top && line.baseline > bottom && line.x0 < box.at(2).toDouble()) {
            cells.push_back(&line);
            if (std::none_of(edges.begin(), edges.end(), [&](double e) { return std::abs(e - line.x0) < 2.0; }))
                edges.push_back(line.x0);
        }
    std::sort(edges.begin(), edges.end());
    REQUIRE(edges.size() == static_cast<std::size_t>(grid.value(QStringLiteral("cols")).toInt()));
    std::vector<const Line*> second;
    for (const Line* line : cells)
        if (std::abs(line->x0 - edges[1]) < 2.0) second.push_back(line);
    std::sort(second.begin(), second.end(), [](const Line* a, const Line* b) { return a->baseline > b->baseline; });
    QString expected;
    for (const Line* line : second) expected += (expected.isEmpty() ? QString() : QStringLiteral("\n")) + line->text;

    product.scrollTo(0, top, 0.25);
    const PageMap map = mapOf(*view, 0);
    product.drag(map.toWidget(edges[1] - 3.0, top + 2.0), map.toWidget(edges[2] - 3.0, bottom - 2.0),
                 ui::SelectTool::kColumnModifier);

    const core::text::CopiedText copied = product.selection().copiedText();
    CHECK(product.selection().selection().kind() == core::text::TextSelection::Kind::Column);
    CHECK(copied.isTable);
    CHECK(copied.plain == expected);
    CHECK(copied.html.count(QStringLiteral("<tr>")) == grid.value(QStringLiteral("rows")).toInt());

    // THE CONTROL: the same drag without Alt reads the table row by row, so it holds other columns' cells.
    product.drag(map.toWidget(edges[1] - 3.0, top + 2.0), map.toWidget(edges[2] - 3.0, bottom - 2.0));
    CHECK(product.selection().selection().kind() == core::text::TextSelection::Kind::Flow);
    CHECK_FALSE(product.selection().copiedText().plain == expected);
}

// =============================================================================================
// A-47 -- right to left
// =============================================================================================

TEST_CASE("a right-to-left line dragged from its right edge copies in logical order", "[selectionink][A-47]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/rtl-text.pdf");
    const LabelPage page = labelPage("rtl-text", 0);
    product.scrollTo(0, 700.0, 0.4);
    const PageMap map = mapOf(*view, 0);
    const QImage raster = product.cleanRaster();

    for (const int id : {3, 6}) {   // a plain Hebrew line, and the mixed one with its digits
        const Line& line = page.lines[static_cast<std::size_t>(id)];
        REQUIRE(line.direction == Direction::Rtl);
        const double y = line.baseline + 0.3 * line.size;
        // From the RIGHT edge leftwards: the first LOGICAL character is the rightmost.
        product.drag(map.toWidget(line.x1 + 2.0, y), map.toWidget(line.x0 - 2.0, y));
        INFO(line.text.toStdString());
        CHECK(product.selection().copiedText().plain == line.text);

        // The highlight covers the line's ink and no other line's (per line: the label carries no
        // Hebrew advances).
        const std::vector<QRectF> highlights = highlightsOf(product.selection(), *view, 0);
        const std::vector<QRectF> moved = highlightsOf(product.selection(), *view, 0, 2.0);
        InkTally tally, control;
        for (const Line& other : page.lines) {
            const bool selected = &other == &line;
            judgeLine(raster, map, other, [selected](int) { return selected; }, highlights, tally);
            if (selected) judgeLine(raster, map, other, [](int) { return true; }, moved, control);
        }
        report("A-47", tally, control);
        CHECK(tally.selected == 1);
        CHECK(tally.outside == 0);
        CHECK(tally.intruding == 0);
        CHECK(control.outside > 0);
    }
}

// =============================================================================================
// A-48 -- vertical CJK
// =============================================================================================

TEST_CASE("a drag down a vertical column selects down it and copies right column first", "[selectionink][A-48]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/cjk-vertical.pdf");
    const LabelPage page = labelPage("cjk-vertical", 0);
    const Line& right = page.lines[3];
    const Line& left = page.lines[4];
    REQUIRE(right.direction == Direction::Ttb);
    REQUIRE(right.x0 > left.x0);
    product.scrollTo(0, 620.0, 0.5);
    const PageMap map = mapOf(*view, 0);
    const QImage raster = product.cleanRaster();

    const double cx = (right.x0 + right.x1) / 2.0;
    product.drag(map.toWidget(cx, right.box[3] - 2.0), map.toWidget(cx, right.box[1] + 2.0));
    CHECK(product.selection().copiedText().plain == right.text);

    const std::vector<QRectF> highlights = highlightsOf(product.selection(), *view, 0);
    const std::vector<QRectF> moved = highlightsOf(product.selection(), *view, 0, 2.0);
    InkTally tally, control;
    for (const Line& line : page.lines) {
        const bool selected = &line == &right;
        judgeLine(raster, map, line, [selected](int) { return selected; }, highlights, tally);
        if (selected) judgeLine(raster, map, line, [](int) { return true; }, moved, control);
    }
    report("A-48", tally, control);
    CHECK(tally.selected == 1);
    CHECK(tally.outside == 0);
    CHECK(tally.intruding == 0);
    CHECK(control.outside > 0);

    // From the top of the RIGHT column to the foot of the LEFT one: the right column first -- the reading
    // order of vertical writing -- then the left.
    const double lx = (left.x0 + left.x1) / 2.0;
    product.drag(map.toWidget(cx, right.box[3] - 2.0), map.toWidget(lx, left.box[1] + 2.0));
    CHECK(product.selection().copiedText().plain == right.text + QStringLiteral("\n") + left.text);
}

// =============================================================================================
// A-51 over text-900.pdf -- Ctrl+A and Ctrl+C from key events, the whole document read at Background
// =============================================================================================

TEST_CASE("Ctrl+A and Ctrl+C on a 900-page document copy every page after reading them at Background", "[selectionink][A-51]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/text-900.pdf");
    core::render::RenderScheduler& scheduler = product.root->services().renderScheduler;

    const auto key = [view](Qt::Key k) {
        QKeyEvent event(QEvent::KeyPress, k, Qt::ControlModifier);
        event.ignore();
        QCoreApplication::sendEvent(view, &event);
        QCoreApplication::processEvents();
        return event.isAccepted();
    };

    CHECK(key(Qt::Key_A));
    REQUIRE(product.selection().selection().lastPage() == 899);
    product.settle();
    const std::size_t missing = product.selection().selection().missingPages(product.selection().text()).size();
    REQUIRE(missing > 800);   // Q6: `Ctrl+A` itself read only what is on screen

    const long long background = scheduler.stats().enqueuedBackground;
    QElapsedTimer timer;
    timer.start();
    CHECK(key(Qt::Key_C));
    CHECK(product.selection().isCopyPending());
    while (product.selection().isCopyPending() && timer.elapsed() < 120000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        scheduler.waitForIdle(50);
    }
    const qint64 elapsed = timer.elapsed();
    REQUIRE_FALSE(product.selection().isCopyPending());

    CHECK(scheduler.stats().enqueuedBackground - background == static_cast<long long>(missing));
    const QString copied = QGuiApplication::clipboard()->text();
    CHECK(copied.startsWith(QStringLiteral("unique-page-0001 of 900")));
    CHECK(copied.contains(QStringLiteral("unique-page-0900 of 900")));
    std::printf("A-51: Ctrl+A, Ctrl+C over text-900.pdf -- %zu pages read at Background and %lld characters on "
                "the clipboard in %lld ms\n",
                missing, static_cast<long long>(copied.size()), static_cast<long long>(elapsed));
}

// =============================================================================================
// A-74's ink half and A-21's bar half -- the find bar over the real engine (S5 T7)
// =============================================================================================

namespace {

// The find bar's hit rectangles for one page, in widget pixels -- `highlightsOf`'s twin for the search.
std::vector<QRectF> searchHighlightsOf(const ui::SelectionOverlay& overlay, const ui::PageView& view, int page,
                                       double shiftPoints = 0.0)
{
    std::vector<QRectF> out;
    const core::DeviceRect visible = view.controller().visibleRect();
    const double dpr = view.devicePixelRatioF();
    const core::render::PageLayout& layout = view.layoutEngine().layout();
    const double shift = shiftPoints * layout.presentedScale();
    for (const core::DeviceRect& r : overlay.searchHighlightRects(layout, page))
        out.emplace_back((r.left() + shift - visible.left()) / dpr, (r.top() - visible.top()) / dpr,
                         r.size.width / dpr, r.size.height / dpr);
    return out;
}

void sendKey(Product& product, Qt::Key key, Qt::KeyboardModifiers modifiers, const QString& text)
{
    QWidget* focus = product.window->focusWidget();
    REQUIRE(focus != nullptr);
    QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
    QCoreApplication::sendEvent(focus, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
    QCoreApplication::sendEvent(focus, &release);
    QCoreApplication::processEvents();
}

// `Ctrl+F` from the page: the bar opens above the panes, and the view, a bar's height shorter, settles.
ui::FindReplaceBar& openTheBar(Product& product)
{
    product.view->setFocus(Qt::OtherFocusReason);
    sendKey(product, Qt::Key_F, Qt::ControlModifier, QString());
    ui::FindReplaceBar& bar = *product.window->currentDocument()->findBar();
    REQUIRE(bar.isOpen());
    product.settle();
    return bar;
}

// The query typed key by key into the open bar, and the search waited for -- the product's path.
ui::FindReplaceBar& findFromThePage(Product& product, const QString& query)
{
    ui::FindReplaceBar& bar = *product.window->currentDocument()->findBar();
    if (!bar.isOpen()) openTheBar(product);
    REQUIRE(product.window->focusWidget() == bar.field());
    for (const QChar ch : query) sendKey(product, Qt::Key_unknown, Qt::NoModifier, QString(ch));
    QElapsedTimer timer;
    timer.start();
    while ((bar.isSearchPending() || bar.isSearching() || bar.searchesFinished() == 0) && timer.elapsed() < 30000)
        product.settle();
    REQUIRE(bar.searchesFinished() >= 1);
    product.settle();
    return bar;
}

} // namespace

TEST_CASE("a search's highlights are on the ink of exactly the words it found", "[selectionink][A-74]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/columns-two.pdf");
    const LabelPage page = labelPage("columns-two", 0);
    const QString word = QStringLiteral("viewport");   // six times on page 1, in both columns

    // THE GENERATOR'S OCCURRENCES of the word, per line -- the oracle, read from the label (rule 7).
    std::vector<std::vector<std::pair<int, int>>> occurrences(page.lines.size());
    int expected = 0;
    for (std::size_t id = 0; id < page.lines.size(); ++id) {
        const QString lower = page.lines[id].text.toLower();
        for (qsizetype at = lower.indexOf(word); at >= 0; at = lower.indexOf(word, at + word.size())) {
            occurrences[id].emplace_back(static_cast<int>(at), static_cast<int>(at + word.size()));
            ++expected;
        }
    }
    REQUIRE(expected == 6);

    // THE BAR OPENS FIRST and the view, shorter by its height, re-tiles -- that is opening a toolbar, not a
    // search, so the counters are read after it.
    openTheBar(product);
    // THE FIRST OCCURRENCE ON SCREEN, so the bar's reveal of the current hit has nothing to scroll.
    std::size_t firstLine = 0;
    while (firstLine < occurrences.size() && occurrences[firstLine].empty()) ++firstLine;
    REQUIRE(firstLine < occurrences.size());
    product.scrollTo(0, page.lines[firstLine].baseline, 0.3);
    const core::DevicePoint scrolledTo = view->controller().scroll();
    const core::render::RenderScheduler::Stats before = product.root->services().renderScheduler.stats();
    ui::FindReplaceBar& bar = findFromThePage(product, word);
    int onPageOne = 0;
    for (const auto& hit : bar.hits()) onPageOne += hit.page == 0 ? 1 : 0;
    CHECK(onPageOne == expected);

    // THE PRODUCT'S RECTANGLES, every hit, then the page grabbed WITHOUT them: the raster the ink is in.
    ui::SelectionOverlay& overlay = product.selection();
    const PageMap map = mapOf(*view, 0);
    const std::vector<QRectF> highlights = searchHighlightsOf(overlay, *view, 0);
    const std::vector<QRectF> moved = searchHighlightsOf(overlay, *view, 0, 2.0);
    REQUIRE_FALSE(highlights.empty());
    const std::vector<core::text::SearchHit> hits = overlay.searchHits();
    overlay.clearSearchHits();
    product.settle();
    const QImage raster = view->viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32);
    overlay.setSearchHits(hits, 0);

    InkTally tally, control;
    for (std::size_t id = 0; id < page.lines.size(); ++id) {
        const auto& ranges = occurrences[id];
        const auto found = [&ranges](int c) {
            return std::any_of(ranges.begin(), ranges.end(), [c](const auto& r) { return c >= r.first && c < r.second; });
        };
        judgeLine(raster, map, page.lines[id], found, highlights, tally);
        judgeLine(raster, map, page.lines[id], found, moved, control);
    }
    report("A-74", tally, control);
    CHECK(tally.selected > 20);
    CHECK(tally.outside == 0);
    CHECK(tally.intruding == 0);
    CHECK(control.outside + control.intruding > 0);

    // AND NO TILE WAS RASTERIZED TO SHOW THEM: the search asked the engine for text and nothing else, and
    // the highlights are paint (D4-11). The reveal could have scrolled -- this page's first hit is on
    // screen, so it did not.
    const core::render::RenderScheduler::Stats after = product.root->services().renderScheduler.stats();
    REQUIRE(view->controller().scroll().x == scrolledTo.x);
    REQUIRE(view->controller().scroll().y == scrolledTo.y);
    CHECK(after.completed - after.textCompleted == before.completed - before.textCompleted);
}

TEST_CASE("the find bar says cjk-embedded's glyphs cannot be searched and finds none of them", "[selectionink][A-21]")
{
    Product product;
    product.open("ordinary/cjk-embedded.pdf");
    ui::FindReplaceBar& bar = findFromThePage(product, QStringLiteral("\u4E2D"));
    CHECK(bar.hits().empty());
    const QString status = bar.statusText();
    INFO("status: " << status.toStdString());
    CHECK(status.contains(ui::catalog::translate(ui::catalog::key::SearchBarNoMatches)));
    // THE REPORT, in words a person reads: the 90 glyphs T2 measured, and that they cannot be searched.
    CHECK(status.contains(ui::catalog::translate(ui::catalog::key::SearchBarUnmapped).arg(90).arg(1)));
}
