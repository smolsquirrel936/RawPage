// tst_croptool -- Stage 7 T9 (`Stage7-Assembly-Plan.md` §6, D7-15, D7-32): the Crop tool, the Set Page Boxes, Resize
// Pages and Renumber Pages dialogs, and the six page actions T9 registers, through the window over the fake backend
// (`Stage7-Assembly-Testing-Guide.md` §5.11, A-64 and the widget halves of A-60 ... A-63).
//
// WHAT IT DOES NOT COVER (convention 19): the RASTER. The fake backend draws a test pattern and its sink records what
// it is told, so A-64's *the written CropBox drawn back on the raster* is graded here as the box the view maps back to
// the pixels it was dragged between -- the same `PageLayout` the raster is placed by -- and the ink of a real crop is
// `tests/render/tst_pageedits` and `tests/composition/tst_pagesproduct`'s. At the offscreen platform's scale of 1; the
// 150 % monitor is M-08's.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "core/settings/Settings.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/PageOpsDialogs.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/tools/CropTool.h"
#include "ui/viewer/tools/MarqueeZoomTool.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QRadioButton>
#include <QSpinBox>

#include <memory>
#include <optional>
#include <string>

using namespace rawpage;
using Catch::Approx;
using core::ActionId;

namespace {

// ---- The tool alone, over a host that maps a pixel to a point one for one -------------------------------------

struct Host final : ui::ToolHost {
    ui::ViewportController* unused = nullptr;
    ui::ViewportController& controller() override { return *unused; }
    const ui::ViewportController& controller() const override { return *unused; }
    QSize viewportSize() const override { return {800, 800}; }
    double devicePixelRatio() const override { return 1.0; }
    void requestRepaint() override { ++repaints; }
    void recordView() override {}
    ui::PagePoint pagePointAt(QPoint pos) const override { return {0, core::PdfPoint{double(pos.x()), 800.0 - pos.y()}}; }
    ui::PagePoint pagePointOn(int page, QPoint pos) const override { return {page, core::PdfPoint{double(pos.x()), 800.0 - pos.y()}}; }
    core::SnapResult snapMeasured(int page, core::PdfPoint point) override
    {
        (void)page;
        // A guide at x = 100: within two points a corner lands on it.
        if (std::abs(point.x - 100.0) <= 2.0) return core::SnapResult{core::PdfPoint{100.0, point.y}, core::SnapSource::Guide, core::SnapSource::None, std::nullopt};
        return core::SnapResult{point, core::SnapSource::None, core::SnapSource::None, std::nullopt};
    }
    bool cropToBox(int page, core::PdfRect box) override
    {
        asked.emplace_back(page, box);
        return true;
    }
    std::vector<std::pair<int, core::PdfRect>> asked;
    int repaints = 0;
};

core::PdfRect rect(double left, double bottom, double right, double top)
{
    return core::PdfRect{core::PdfPoint{left, bottom}, core::PdfSize{right - left, top - bottom}};
}

// ---- The window --------------------------------------------------------------------------------------------------

void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

struct Answers {
    ~Answers()
    {
        ui::CropPagesDialog::setAnswerForTesting({});
        ui::ResizePagesDialog::setAnswerForTesting({});
        ui::RenumberPagesDialog::setAnswerForTesting({});
    }
};

struct Shell {
    uitest::ShellFixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow* document = nullptr;
    ui::PageView* view = nullptr;

    Shell()
    {
        fixture.backend.editable = true;
        fixture.backend.pagesPerDocument = 5;
        const QString path = QFileInfo(QDir(fixture.directory.path()).filePath(QStringLiteral("crop.pdf"))).absoluteFilePath();
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("%PDF-1.7\n% a fake page\n");
        file.close();
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 850);
        window->openFiles({path});
        driveLayout(window.get());
        document = window->currentDocument();
        REQUIRE(document != nullptr);
        REQUIRE(document->journal() != nullptr);
        view = window->currentView();
        REQUIRE(view != nullptr);
        window->raiseToForeground();
        view->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        fixture.pumpTiles();
        sink()->labelPageCount = 5;
    }
    ~Shell()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    uitest::FakeSink* sink() { return fixture.backend.sinkFor(document->handle()); }
    const core::Page& page(int index) { return *document->journal()->document().pages().at(index); }

    void mouse(QEvent::Type type, QPoint pos) const
    {
        QMouseEvent event(type, QPointF(pos), QPointF(pos), Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QCoreApplication::processEvents();
    }
    void drag(QPoint from, QPoint to) const
    {
        mouse(QEvent::MouseButtonPress, from);
        mouse(QEvent::MouseMove, (from + to) / 2);
        mouse(QEvent::MouseMove, to);
        mouse(QEvent::MouseButtonRelease, to);
    }
    bool key(Qt::Key key) const
    {
        QWidget* focus = window->focusWidget();
        REQUIRE(focus != nullptr);
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        event.ignore();
        QCoreApplication::sendEvent(focus, &event);
        QCoreApplication::processEvents();
        return event.isAccepted();
    }
    // Two points well inside page 1's slot, in viewport pixels.
    std::pair<QPoint, QPoint> band() const
    {
        const std::optional<QPointF> topLeft = view->widgetPointOf(0, core::PdfPoint{100.0, 700.0});
        const std::optional<QPointF> bottomRight = view->widgetPointOf(0, core::PdfPoint{400.0, 200.0});
        REQUIRE(topLeft);
        REQUIRE(bottomRight);
        return {topLeft->toPoint() + QPoint(3, 1), bottomRight->toPoint() - QPoint(2, 5)};
    }
};

} // namespace

// ==========================================================================================
// A-64 -- the crop tool's box is where it was dragged
// ==========================================================================================

TEST_CASE("ui.croptool the tool hands its host a page-space box, snapped, and a click or Esc hands nothing", "[croptool][A-64]")
{
    Host host;
    ui::CropTool tool;
    // A drag from (101, 100) to (300, 400) in pixels: the left edge 1 point from the guide, so it lands on it.
    tool.press(host, QPoint(101, 100));
    tool.move(host, QPoint(200, 300));
    REQUIRE(tool.box());
    tool.release(host, QPoint(300, 400));
    REQUIRE(host.asked.size() == 1);
    CHECK(host.asked.front().first == 0);
    CHECK(host.asked.front().second == rect(100, 400, 300, 700));   // y up: pixels 100 ... 400 are points 700 ... 400
    CHECK_FALSE(tool.isDragging());

    // A click, and a drag under the marquee's threshold on either side: nothing.
    const int tiny = ui::MarqueeZoomTool::kMinDragPixels - 2;
    for (const QPoint end : {QPoint(150, 150), QPoint(150 + tiny, 400), QPoint(400, 150 + tiny)}) {
        tool.press(host, QPoint(150, 150));
        tool.move(host, end);
        tool.release(host, end);
    }
    // `Esc` mid-drag: nothing, and the release that follows hands nothing either.
    tool.press(host, QPoint(150, 150));
    tool.move(host, QPoint(400, 400));
    tool.cancel(host);
    tool.release(host, QPoint(400, 400));
    CHECK(host.asked.size() == 1);
}

TEST_CASE("ui.croptool page.crop arms the tool, and a drag crops the page to the box it was dragged between", "[croptool][A-64]")
{
    Shell shell;
    REQUIRE(shell.fixture.actions.isRegistered(ActionId::PageCrop));
    REQUIRE(shell.fixture.actions.dispatch(ActionId::PageCrop, core::KeyContext::Viewer));
    REQUIRE(shell.view->tools().armed() == ui::ToolKind::Crop);
    CHECK(shell.view->tools().cursor() == Qt::CrossCursor);

    const auto [from, to] = shell.band();
    const ui::PagePoint start = shell.view->pagePointAt(from);
    const ui::PagePoint end = shell.view->pagePointOn(start.page, to);
    REQUIRE(start.page == 0);
    const core::PdfRect dragged = rect(std::min(start.point.x, end.point.x), std::min(start.point.y, end.point.y),
                                       std::max(start.point.x, end.point.x), std::max(start.point.y, end.point.y));

    // `Esc` MID-DRAG: no command.
    const int before = shell.sink()->pageCommands;
    shell.mouse(QEvent::MouseButtonPress, from);
    shell.mouse(QEvent::MouseMove, to);
    REQUIRE(shell.view->tools().isDragging());
    CHECK(shell.key(Qt::Key_Escape));
    shell.mouse(QEvent::MouseButtonRelease, to);
    CHECK(shell.sink()->pageCommands == before);
    CHECK_FALSE(shell.document->journal()->canUndo());

    // THE DRAG, with nobody to ask (convention 16): ONE crop of page 1, to the box dragged.
    shell.drag(from, to);
    REQUIRE(shell.document->journal()->canUndo());
    CHECK(shell.sink()->pageCommands == before + 1);   // ONE command: one call to the sink
    REQUIRE(shell.sink()->pageBoxes.count(0) == 1);
    const core::PageBoxes written = shell.sink()->pageBoxes.at(0);
    REQUIRE(written.crop);
    // Within a device pixel: one point per pixel at this view's scale, which the case reads rather than assumes.
    const double pixel = 1.0 / shell.view->pixelsPerPoint();
    CHECK(written.crop->left() == Approx(dragged.left()).margin(pixel));
    CHECK(written.crop->bottom() == Approx(dragged.bottom()).margin(pixel));
    CHECK(written.crop->right() == Approx(dragged.right()).margin(pixel));
    CHECK(written.crop->top() == Approx(dragged.top()).margin(pixel));
    CHECK(shell.page(0).cropBox() == *written.crop);   // the model's page shows it
    CHECK(written.media == shell.page(0).mediaBox());   // and only the CropBox changed
    // DRAWN BACK: the written box's corners, mapped to the viewport again, are the pixels the drag went between.
    // (The view has relaid the cropped page; the box is mapped through the layout it had, the one it was dragged on --
    // by undoing, which puts the page back, then mapping.)
    REQUIRE(shell.document->journal()->undo());
    QCoreApplication::processEvents();
    const std::optional<QPointF> a = shell.view->widgetPointOf(0, core::PdfPoint{written.crop->left(), written.crop->top()});
    const std::optional<QPointF> b = shell.view->widgetPointOf(0, core::PdfPoint{written.crop->right(), written.crop->bottom()});
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->x() == Approx(from.x()).margin(1.0));
    CHECK(a->y() == Approx(from.y()).margin(1.0));
    CHECK(b->x() == Approx(to.x()).margin(1.0));
    CHECK(b->y() == Approx(to.y()).margin(1.0));
    // Still armed: the tool is sticky, as the snapshot is.
    CHECK(shell.view->tools().armed() == ui::ToolKind::Crop);
}

TEST_CASE("ui.croptool a dragged box fills in the Set Page Boxes dialog where a person can answer", "[croptool][A-64][A-60]")
{
    Answers answers;
    Shell shell;
    std::optional<std::array<double, 4>> offered;
    std::vector<int> offeredPages;
    ui::CropPagesDialog::setAnswerForTesting([&](int, const std::vector<int>& initial, std::array<double, 4> margins, double) {
        offered = margins;
        offeredPages = initial;
        ui::CropPagesDialog::Choice choice;
        choice.pages = {0, 1, 2};   // the person widens it to three pages
        choice.left = margins[0];
        choice.bottom = margins[1];
        choice.right = margins[2];
        choice.top = margins[3];
        return std::optional<ui::CropPagesDialog::Choice>(choice);
    });
    REQUIRE(shell.fixture.actions.dispatch(ActionId::PageCrop, core::KeyContext::Viewer));
    const auto [from, to] = shell.band();
    shell.drag(from, to);
    REQUIRE(offered);
    CHECK(offeredPages == std::vector<int>{0});
    const core::PdfRect media = shell.page(0).mediaBox();
    // ONE command over the three pages, each inset from its own MediaBox by the dragged box's margins.
    REQUIRE(shell.sink()->pageCommands == 1);
    for (int page = 0; page < 3; ++page) {
        INFO("page " << page);
        REQUIRE(shell.sink()->pageBoxes.count(page) == 1);
        const core::PdfRect crop = *shell.sink()->pageBoxes.at(page).crop;
        CHECK(crop.left() == Approx(media.left() + (*offered)[0]));
        CHECK(crop.top() == Approx(media.top() - (*offered)[3]));
    }
    CHECK(shell.sink()->pageBoxes.count(3) == 0);
}

// ==========================================================================================
// The three dialogs, widget by widget
// ==========================================================================================

TEST_CASE("pageops the Set Page Boxes dialog reads a box, its margins, white margins or a removal", "[pageops][crop][A-60][A-61]")
{
    ui::CropPagesDialog dialog(10, {2, 3}, {10, 20, 30, 40}, 6.0);
    CHECK(dialog.rangeField()->text() == QStringLiteral("3-4"));
    CHECK(dialog.marginField(0)->value() == 10.0);
    CHECK(dialog.marginField(3)->value() == 40.0);
    CHECK(dialog.paddingField()->value() == 6.0);
    CHECK_FALSE(dialog.paddingField()->isEnabled());   // margins chosen: the padding field waits
    auto chosen = dialog.choice();
    REQUIRE(chosen);
    CHECK(chosen.value().pages == std::vector<int>{2, 3});
    CHECK(chosen.value().box == core::PageBoxKind::Crop);
    CHECK(chosen.value().how == ui::CropPagesDialog::How::Margins);
    CHECK(chosen.value().right == 30.0);

    dialog.howButton(ui::CropPagesDialog::How::WhiteMargins)->setChecked(true);
    CHECK(dialog.paddingField()->isEnabled());
    CHECK_FALSE(dialog.marginField(0)->isEnabled());
    dialog.sameSizeBox()->setChecked(true);
    chosen = dialog.choice();
    REQUIRE(chosen);
    CHECK(chosen.value().how == ui::CropPagesDialog::How::WhiteMargins);
    CHECK(chosen.value().sameSize);
    CHECK(chosen.value().padding == 6.0);

    // A MEDIABOX IS NEVER REMOVED: refused before the dialog closes.
    dialog.howButton(ui::CropPagesDialog::How::Remove)->setChecked(true);
    dialog.boxKindBox()->setCurrentIndex(dialog.boxKindBox()->findData(static_cast<int>(core::PageBoxKind::Media)));
    chosen = dialog.choice();
    REQUIRE_FALSE(chosen);
    CHECK(std::string(chosen.error().messageKey.data()) == "error.pages.mediaBoxRequired");
    dialog.boxKindBox()->setCurrentIndex(dialog.boxKindBox()->findData(static_cast<int>(core::PageBoxKind::Trim)));
    CHECK(dialog.choice());
    dialog.rangeField()->setText(QStringLiteral("4-2"));
    CHECK_FALSE(dialog.choice());
}

TEST_CASE("pageops the Resize Pages dialog reads a named size, a turned one, a custom one and the anchor", "[pageops][resize][A-62]")
{
    ui::ResizePagesDialog dialog(4, {0}, core::PaperSize::Letter);
    CHECK(dialog.widthField()->value() == 612.0);
    CHECK(dialog.heightField()->value() == 792.0);
    CHECK_FALSE(dialog.widthField()->isEnabled());
    dialog.landscapeBox()->setChecked(true);
    CHECK(dialog.widthField()->value() == 792.0);
    auto chosen = dialog.choice();
    REQUIRE(chosen);
    CHECK(chosen.value().size == core::PdfSize{792.0, 612.0});
    CHECK(chosen.value().scale);
    CHECK(chosen.value().anchor == core::ResizeAnchor::Centre);

    dialog.sizeBox()->setCurrentIndex(dialog.sizeBox()->findData(-1));   // Custom
    CHECK(dialog.widthField()->isEnabled());
    dialog.widthField()->setValue(500.0);
    dialog.heightField()->setValue(700.0);
    dialog.scaleBox()->setChecked(false);
    dialog.anchorBox()->setCurrentIndex(static_cast<int>(core::ResizeAnchor::BottomRight));
    dialog.rangeField()->setText(QStringLiteral("1-4"));
    chosen = dialog.choice();
    REQUIRE(chosen);
    CHECK(chosen.value().pages == std::vector<int>{0, 1, 2, 3});
    CHECK(chosen.value().size == core::PdfSize{500.0, 700.0});
    CHECK_FALSE(chosen.value().scale);
    CHECK(chosen.value().anchor == core::ResizeAnchor::BottomRight);
}

TEST_CASE("pageops the Renumber Pages dialog reads a range, a style, a prefix and a start", "[pageops][renumber][A-63]")
{
    ui::RenumberPagesDialog dialog(9, {0, 1, 2});
    CHECK(dialog.rangeField()->text() == QStringLiteral("1-3"));
    dialog.styleBox()->setCurrentIndex(dialog.styleBox()->findData(static_cast<int>(core::PageLabelStyle::RomanLower)));
    dialog.prefixField()->setText(QStringLiteral("Front "));
    dialog.startField()->setValue(3);
    auto chosen = dialog.choice();
    REQUIRE(chosen);
    CHECK(chosen.value().pages == std::vector<int>{0, 1, 2});
    CHECK(chosen.value().style == core::PageLabelStyle::RomanLower);
    CHECK(chosen.value().prefix == QStringLiteral("Front "));
    CHECK(chosen.value().start == 3);
    CHECK(dialog.styleBox()->count() == 6);
}

// ==========================================================================================
// The actions, through the window
// ==========================================================================================

TEST_CASE("pageops boxes, resize and renumber run from their actions as one command each, and are disabled with nobody to ask", "[pageops][crop][resize][renumber]")
{
    Shell shell;
    // NOBODY TO ASK (convention 16): the three dialogs' actions are disabled; the crop tool and the thumbnails' are not.
    CHECK_FALSE(shell.fixture.actions.isEnabled(ActionId::PageSetBoxes));
    CHECK_FALSE(shell.fixture.actions.isEnabled(ActionId::PageResize));
    CHECK_FALSE(shell.fixture.actions.isEnabled(ActionId::PageRenumber));
    CHECK(shell.fixture.actions.isEnabled(ActionId::PageCrop));
    CHECK(shell.fixture.actions.isEnabled(ActionId::PageEmbedThumbnails));
    CHECK(shell.fixture.actions.isEnabled(ActionId::PageRemoveThumbnails));

    Answers answers;
    ui::CropPagesDialog::setAnswerForTesting([](int, const std::vector<int>&, std::array<double, 4>, double) {
        ui::CropPagesDialog::Choice choice;
        choice.pages = {1};
        choice.box = core::PageBoxKind::Trim;
        choice.left = choice.bottom = choice.right = choice.top = 18.0;
        return std::optional<ui::CropPagesDialog::Choice>(choice);
    });
    ui::ResizePagesDialog::setAnswerForTesting([](int, const std::vector<int>&) {
        return std::optional<ui::ResizePagesDialog::Choice>(ui::ResizePagesDialog::Choice{{0, 4}, core::PdfSize{595.276, 841.89}, true,
                                                                                           core::ResizeAnchor::Centre});
    });
    ui::RenumberPagesDialog::setAnswerForTesting([](int, const std::vector<int>&) {
        return std::optional<ui::RenumberPagesDialog::Choice>(ui::RenumberPagesDialog::Choice{{0, 1}, core::PageLabelStyle::RomanLower,
                                                                                               QStringLiteral("p"), 1});
    });
    REQUIRE(shell.fixture.actions.isEnabled(ActionId::PageSetBoxes));

    const core::PdfRect media = shell.page(1).mediaBox();
    REQUIRE(shell.fixture.actions.dispatch(ActionId::PageSetBoxes, core::KeyContext::Global));
    REQUIRE(shell.sink()->pageBoxes.count(1) == 1);
    CHECK(*shell.sink()->pageBoxes.at(1).trim == rect(media.left() + 18, media.bottom() + 18, media.right() - 18, media.top() - 18));
    CHECK(shell.page(1).trimBox() == *shell.sink()->pageBoxes.at(1).trim);

    REQUIRE(shell.fixture.actions.dispatch(ActionId::PageResize, core::KeyContext::Global));
    CHECK(shell.sink()->wrapped == 2);
    CHECK(shell.page(4).mediaBox() == rect(0, 0, 595.276, 841.89));

    REQUIRE(shell.fixture.actions.dispatch(ActionId::PageRenumber, core::KeyContext::Global));
    // The fake sink spells a label as its prefix and number; the model takes whatever the sink says every page shows.
    CHECK(shell.page(0).label() == QStringLiteral("p1"));
    CHECK(shell.page(1).label() == QStringLiteral("p2"));

    for (int step = 0; step < 3; ++step) REQUIRE(shell.document->journal()->undo());
    CHECK_FALSE(shell.document->journal()->canUndo());   // three commands, three steps
    CHECK(shell.page(4).mediaBox() == media);
    CHECK(shell.sink()->wrapped == 0);
    CHECK(shell.page(0).label().isEmpty());

    // THE THUMBNAILS' REMOVAL is one command over every page.
    REQUIRE(shell.fixture.actions.dispatch(ActionId::PageRemoveThumbnails, core::KeyContext::Global));
    CHECK(shell.sink()->thumbnailsSet.size() == 5);
    CHECK(shell.document->journal()->canUndo());
}
