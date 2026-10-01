// tst_reflowink -- Stage 5 T11, guide cases A-92 and A-93 over a REAL page (`Stage5-TextSearch-Testing-Guide.md`
// §5.13): Reflow view through the product's own composition -- the engine extracting, the analyzer ordering
// -- over `columns-two.pdf`, graded against the GENERATOR's labels (`corpus/labels/columns-two.json`, rule 7),
// never against the analyzer it is built from.
//
//   * **A-92.** Page 1's paragraphs are the label's blocks, in the label's reading order -- column 1, then
//     column 2 -- each block's lines joined, as re-wrapping joins them.
//   * **A-93.** With the page view scrolled so the top edge cuts through a block of column 1, the reflow
//     opens at the label's first block not wholly above that edge; scrolled in the reflow to column 2's first
//     body block, the page view returns with that block's top at its top edge.
//
// WHY `tests/composition` (as `tst_selectionink`): a real page needs the engine, which `rawpage_ui_tests` links
// none of. The widget half -- the chords, the zoom, the keys, the hit -- is `tests/ui/tst_reflow.cpp`.
//
// WHAT THIS DOES NOT COVER (convention 19): whether the re-wrapped text READS well -- manual case M-09; a
// table block, which the reflow writes a line at a time and nothing here grades; and every document here is
// OURS (D5-23).

#include <catch2/catch_test_macros.hpp>

#include "app/CompositionRoot.h"
#include "core/cmd/ActionRegistry.h"
#include "core/render/RenderScheduler.h"
#include "tests/render/TextOracle.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ReflowView.h"
#include "ui/viewer/ViewportController.h"

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLayout>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTextBlock>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <vector>

using namespace rawpage;
using test_oracle::Line;

namespace {

struct Product {
    QTemporaryDir                         scratch;
    std::unique_ptr<app::CompositionRoot> root;
    std::unique_ptr<ui::MainWindow>       window;
    ui::PageView*                         view = nullptr;

    Product()
    {
        REQUIRE(scratch.isValid());
        app::CompositionRoot::Options options;
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
        return view;
    }

    ui::DocumentWindow& document() const { return *window->currentDocument(); }
    ui::ReflowView& reflow() const { return *document().reflow(); }

    void settle()
    {
        for (int round = 0; round < 8; ++round) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            root->services().renderScheduler.waitForIdle(4000);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
        }
        view->refresh();
    }

    void settleReflow()
    {
        QElapsedTimer timer;
        timer.start();
        do settle();
        while (!reflow().isSettled() && timer.elapsed() < 20000);
        REQUIRE(reflow().isSettled());
    }

    void toggle()
    {
        REQUIRE(root->services().actions.dispatch(core::ActionId::ViewReflow, core::KeyContext::Viewer));
        settle();
        if (document().isReflowing()) settleReflow();
    }
};

// The generator's page: its lines by id and its blocks in reading order, each with its text joined as
// re-wrapping joins it and its box (the union of its lines' boxes).
struct LabelBlock {
    QString text;
    double  top = 0.0;
    double  bottom = 0.0;
    double  left = 1e9;
    double  right = -1e9;
};

std::vector<LabelBlock> labelBlocks(const char* name, int index)
{
    const QJsonObject label = test_oracle::loadLabel(name);
    const std::vector<Line> lines = test_oracle::labelLines(label, index);
    std::vector<LabelBlock> out;
    for (const QJsonValue value : label.value(QStringLiteral("pages")).toArray()) {
        const QJsonObject page = value.toObject();
        if (page.value(QStringLiteral("index")).toInt(-1) != index) continue;
        std::map<int, std::vector<int>> byId;
        for (const QJsonValue block : page.value(QStringLiteral("blocks")).toArray()) {
            std::vector<int> ids;
            for (const QJsonValue line : block.toObject().value(QStringLiteral("lines")).toArray())
                ids.push_back(line.toInt());
            byId[block.toObject().value(QStringLiteral("id")).toInt()] = ids;
        }
        for (const QJsonValue id : page.value(QStringLiteral("order")).toArray()) {
            LabelBlock block;
            block.top = -1e9;
            block.bottom = 1e9;
            for (const int line : byId[id.toInt()]) {
                const Line& l = lines[static_cast<std::size_t>(line)];
                if (!block.text.isEmpty()) block.text += QLatin1Char(' ');
                block.text += l.text;
                block.bottom = std::min(block.bottom, l.box[1]);
                block.top = std::max(block.top, l.box[3]);
                block.left = std::min(block.left, l.box[0]);
                block.right = std::max(block.right, l.box[2]);
            }
            out.push_back(block);
        }
    }
    REQUIRE_FALSE(out.empty());
    return out;
}

// A label block as DRAWN: the top-most and bottom-most content-space y of its box's four corners -- the
// screen's own measure, whichever way the page is turned (I-06).
struct Drawn {
    double top = 1e18;
    double bottom = -1e18;
};

Drawn drawn(const ui::PageView& view, int page, const LabelBlock& block)
{
    Drawn out;
    for (const double x : {block.left, block.right})
        for (const double y : {block.bottom, block.top}) {
            const auto content = view.layoutEngine().layout().toContent(page, core::PdfPoint{x, y});
            REQUIRE(content.has_value());
            out.top = std::min(out.top, content->y);
            out.bottom = std::max(out.bottom, content->y);
        }
    return out;
}

QString collapsed(QString text)
{
    return text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" ")).trimmed();
}

} // namespace

TEST_CASE("the reflow of columns-two is the generator's blocks in reading order", "[reflowink][A-92]")
{
    Product product;
    product.open("ordinary/columns-two.pdf");
    product.toggle();
    REQUIRE(product.document().isReflowing());

    const std::vector<LabelBlock> expected = labelBlocks("columns-two", 0);
    std::vector<QString> shown;
    for (const ui::ReflowView::Shown& paragraph : product.reflow().shown()) {
        if (paragraph.place.page != 0) continue;
        shown.push_back(collapsed(paragraph.text));
    }
    INFO("paragraphs shown for page 1: " << shown.size() << ", blocks labelled: " << expected.size());
    {
        std::string both = "\nSHOWN:\n";
        for (const QString& t : shown) both += "  [" + t.left(70).toStdString() + "]\n";
        both += "LABELLED:\n";
        for (const LabelBlock& b : expected) both += "  [" + collapsed(b.text).left(70).toStdString() + "]\n";
        INFO(both);
        REQUIRE(shown.size() == expected.size());
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("block " << i << "\n  label: " << expected[i].text.toStdString() << "\n  shown: " << shown[i].toStdString());
        CHECK(shown[i] == collapsed(expected[i].text));
    }

    // THE CONTROL: across the gutter -- column 1's first body line followed by column 2's -- is not what it
    // shows. The label's body blocks 2 and 8 begin column 1 and column 2.
    const QString reflowed = collapsed(product.reflow().toPlainText());
    CHECK(reflowed.indexOf(collapsed(expected[2].text)) < reflowed.indexOf(collapsed(expected[8].text)));
}

TEST_CASE("the reflow opens at the block at the top of the page and returns to the block at its own top",
          "[reflowink][A-93]")
{
    Product product;
    ui::PageView* view = product.open("ordinary/columns-two.pdf");
    const std::vector<LabelBlock> blocks = labelBlocks("columns-two", 0);

    // THE TOP EDGE THROUGH THE MIDDLE OF COLUMN 1's SECOND BODY BLOCK (label block 3).
    const double cut = (blocks[3].top + blocks[3].bottom) / 2.0;
    view->putPageRectAtTop(0, core::PdfRect{core::PdfPoint{0.0, cut}, core::PdfSize{0.0, 0.0}});
    product.settle();
    const ui::PagePoint top = view->pagePointAt(QPoint(view->viewport()->width() / 2, 0));
    REQUIRE(top.page == 0);
    REQUIRE(std::abs(top.point.y - cut) < 1.5);

    // THIS FILE's expectation, from the labels: the first block in reading order not wholly above the edge.
    std::size_t first = blocks.size();
    for (std::size_t i = 0; i < blocks.size(); ++i)
        if (blocks[i].bottom < top.point.y) {
            first = i;
            break;
        }
    REQUIRE(first < blocks.size());

    product.toggle();
    REQUIRE(product.document().isReflowing());
    const QTextBlock atTop = product.reflow().cursorForPosition(QPoint(2, 1)).block();
    INFO("the reflow's top paragraph: " << atTop.text().toStdString());
    CHECK(collapsed(atTop.text()) == collapsed(blocks[first].text));

    // SCROLLED IN THE REFLOW to column 2's first body block (label block 8), as the reader's own scroll.
    QTextBlock target;
    for (QTextBlock block = product.reflow().document()->begin(); block.isValid(); block = block.next())
        if (collapsed(block.text()) == collapsed(blocks[8].text)) target = block;
    REQUIRE(target.isValid());
    QScrollBar* bar = product.reflow().verticalScrollBar();
    bar->triggerAction(QAbstractSlider::SliderMove);
    bar->setValue(static_cast<int>(product.reflow().document()->documentLayout()->blockBoundingRect(target).top()));
    QCoreApplication::processEvents();
    REQUIRE(collapsed(product.reflow().cursorForPosition(QPoint(2, 1)).block().text()) == collapsed(blocks[8].text));

    product.toggle();
    REQUIRE_FALSE(product.document().isReflowing());
    const ui::PagePoint back = view->pagePointAt(QPoint(view->viewport()->width() / 2, 0));
    REQUIRE(back.page == 0);
    INFO("the top edge at y " << back.point.y << ", the label's block top at " << blocks[8].top);
    // THE BLOCK'S TOP AT THE TOP EDGE: the whole of its first line on screen, and nothing above it but the
    // difference between the label's box and the engine's glyph boxes, which the product places by (measured
    // at 2.2 pt on its first run).
    CHECK(back.point.y >= blocks[8].top);
    CHECK(back.point.y - blocks[8].top < 3.0);
}

// S5 guide I-06: the place both ways ON A PAGE SHOWN TURNED. At T11 the way in read the page-space HEIGHT at
// the edge and the way back put one at it, both of which mean "the screen's down" only on an upright page:
// leaving at 90 degrees put the page's top edge at the viewport's, and at 180 and 270 the block was not on
// screen at all (measured 321, 192 and 555 px from the edge). Each direction is asserted here in the
// screen's own measure -- content-space y, from the layout -- at all four view rotations.
TEST_CASE("the reflow's place survives a toggle both ways at every view rotation", "[reflowink][A-93][I-06]")
{
    for (int turns = 0; turns < 4; ++turns) {
        DYNAMIC_SECTION("view turned " << turns * 90 << " degrees")
        {
            Product product;
            ui::PageView* view = product.open("ordinary/columns-two.pdf");
            for (int t = 0; t < turns; ++t) view->rotateViewClockwise();
            product.settle();
            const std::vector<LabelBlock> blocks = labelBlocks("columns-two", 0);

            // Device pixels per point, so a tolerance in points means the same at any zoom.
            const auto& layout = view->layoutEngine().layout();
            const auto c0 = layout.toContent(0, core::PdfPoint{0.0, 0.0});
            const auto c1 = layout.toContent(0, core::PdfPoint{1.0, 0.0});
            REQUIRE((c0 && c1));
            const double perPoint = std::hypot(c1->x - c0->x, c1->y - c0->y);
            const double slack = 3.0 * perPoint;   // the label's boxes against the engine's glyph boxes

            // IN: the top edge through the middle of column 1's second body block (label block 3), as drawn.
            const double cx = (blocks[3].left + blocks[3].right) / 2.0;
            const double cy = (blocks[3].top + blocks[3].bottom) / 2.0;
            view->putPageRectAtTop(0, core::PdfRect{core::PdfPoint{cx, cy}, core::PdfSize{0.0, 0.0}});
            product.settle();
            const double edgeIn = view->controller().visibleRect().top();
            // THIS FILE's expectation: the first label block, in reading order, not wholly above the edge as
            // drawn -- with no block's drawn bottom so near the edge that label and engine could disagree.
            std::size_t first = blocks.size();
            for (std::size_t i = 0; i < blocks.size(); ++i) {
                const Drawn d = drawn(*view, 0, blocks[i]);
                INFO("block " << i << " drawn bottom " << d.bottom << ", edge " << edgeIn);
                REQUIRE(std::abs(d.bottom - edgeIn) > slack);
                if (first == blocks.size() && d.bottom > edgeIn) first = i;
            }
            REQUIRE(first < blocks.size());

            product.toggle();
            REQUIRE(product.document().isReflowing());
            const QTextBlock atTop = product.reflow().cursorForPosition(QPoint(2, 1)).block();
            INFO("expected label block " << first << "; the reflow's top paragraph: " << atTop.text().toStdString());
            CHECK(collapsed(atTop.text()) == collapsed(blocks[first].text));

            // BACK: scrolled in the reflow to column 2's first body block (label block 8), then Ctrl+4.
            QTextBlock target;
            for (QTextBlock block = product.reflow().document()->begin(); block.isValid(); block = block.next())
                if (collapsed(block.text()) == collapsed(blocks[8].text)) target = block;
            REQUIRE(target.isValid());
            QScrollBar* bar = product.reflow().verticalScrollBar();
            bar->triggerAction(QAbstractSlider::SliderMove);
            bar->setValue(static_cast<int>(product.reflow().document()->documentLayout()->blockBoundingRect(target).top()));
            QCoreApplication::processEvents();
            REQUIRE(collapsed(product.reflow().cursorForPosition(QPoint(2, 1)).block().text()) == collapsed(blocks[8].text));

            product.toggle();
            REQUIRE_FALSE(product.document().isReflowing());
            const double edgeOut = view->controller().visibleRect().top();
            const Drawn d = drawn(*view, 0, blocks[8]);
            INFO("edge " << edgeOut << ", label block 8 drawn " << d.top << " .. " << d.bottom << ", slack " << slack);
            // ITS TOP AS DRAWN AT THE EDGE: on screen, and nothing above it but the glyph boxes' difference.
            CHECK(d.top >= edgeOut - 0.5);
            CHECK(d.top - edgeOut < slack);
        }
    }
}
