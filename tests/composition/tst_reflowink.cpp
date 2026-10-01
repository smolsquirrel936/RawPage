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
            }
            out.push_back(block);
        }
    }
    REQUIRE_FALSE(out.empty());
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
    view->putPageHeightAtTop(0, cut);
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
