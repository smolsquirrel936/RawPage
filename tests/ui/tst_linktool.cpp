// tst_linktool -- Stage 7 T12 (`Stage7-Assembly-Plan.md` §6, D7-12, D7-35): the Link tool, the link properties dialog's
// answer, and the Destinations pane's three commands, through the window over the fake backend
// (`Stage7-Assembly-Testing-Guide.md` §5.14, A-83's tool half, and A-82's surface half).
//
// WHAT IT DOES NOT COVER (convention 19): the ENGINE. The fake backend has no text extractor, draws a test pattern and its
// sink records what it is told, so a link made from SELECTED TEXT, its quadrilaterals on the glyphs, a link FOLLOWED to
// the page it shows, and its border on the raster are `tests/composition/tst_linkproduct.cpp`'s; what the shipped sink
// writes is `tests/unit/tst_links.cpp` and `tst_destinations.cpp`'s. At the offscreen platform's scale of 1.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/LinkPropertiesDialog.h"
#include "ui/panes/DestinationsPane.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/tools/LinkTool.h"
#include "ui/viewer/tools/MarqueeZoomTool.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSpinBox>

#include <memory>
#include <optional>

using namespace rawpage;
using Catch::Approx;
using core::ActionId;

namespace {

struct Host final : ui::ToolHost {
    ui::ViewportController* unused = nullptr;
    ui::ViewportController& controller() override { return *unused; }
    const ui::ViewportController& controller() const override { return *unused; }
    QSize viewportSize() const override { return {800, 800}; }
    double devicePixelRatio() const override { return 1.0; }
    void requestRepaint() override {}
    void recordView() override {}
    ui::PagePoint pagePointAt(QPoint pos) const override { return {0, core::PdfPoint{double(pos.x()), 800.0 - pos.y()}}; }
    ui::PagePoint pagePointOn(int page, QPoint pos) const override { return {page, core::PdfPoint{double(pos.x()), 800.0 - pos.y()}}; }
    bool linkFromBox(int page, core::PdfRect box) override
    {
        asked.emplace_back(page, box);
        return true;
    }
    std::vector<std::pair<int, core::PdfRect>> asked;
};

core::PdfRect rect(double left, double bottom, double right, double top)
{
    return core::PdfRect{core::PdfPoint{left, bottom}, core::PdfSize{right - left, top - bottom}};
}

void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

// The answers a case gives, put back when it ends -- they are process-wide.
struct Answers {
    ~Answers()
    {
        ui::LinkPropertiesDialog::setAnswerForTesting({});
        ui::DestinationNameDialog::setAnswerForTesting({});
        ui::LinkOpenDialog::setAnswerForTesting({});
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
        const QString path = QFileInfo(QDir(fixture.directory.path()).filePath(QStringLiteral("links.pdf"))).absoluteFilePath();
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
    }
    ~Shell()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

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
    std::pair<QPoint, QPoint> band() const
    {
        const std::optional<QPointF> topLeft = view->widgetPointOf(0, core::PdfPoint{100.0, 700.0});
        const std::optional<QPointF> bottomRight = view->widgetPointOf(0, core::PdfPoint{300.0, 650.0});
        REQUIRE(topLeft);
        REQUIRE(bottomRight);
        return {topLeft->toPoint(), bottomRight->toPoint()};
    }
    const core::Document& model() const { return document->journal()->document(); }
};

} // namespace

// ==========================================================================================
// A-83 -- the Link tool's rectangle is where it was dragged
// ==========================================================================================

TEST_CASE("ui.linktool the tool hands its host a page-space box, and a click or Esc hands nothing", "[linktool][A-83]")
{
    Host host;
    ui::LinkTool tool;
    tool.press(host, QPoint(100, 100));
    tool.move(host, QPoint(200, 120));
    REQUIRE(tool.box());
    tool.release(host, QPoint(300, 140));
    REQUIRE(host.asked.size() == 1);
    CHECK(host.asked.front().first == 0);
    CHECK(host.asked.front().second == rect(100, 660, 300, 700));   // y up: pixels 100 ... 140 are points 700 ... 660
    CHECK_FALSE(tool.isDragging());
    CHECK(tool.kind() == ui::ToolKind::Link);

    const int tiny = ui::MarqueeZoomTool::kMinDragPixels - 2;
    for (const QPoint end : {QPoint(150, 150), QPoint(150 + tiny, 400), QPoint(400, 150 + tiny)}) {
        tool.press(host, QPoint(150, 150));
        tool.move(host, end);
        tool.release(host, end);
    }
    tool.press(host, QPoint(150, 150));
    tool.move(host, QPoint(400, 400));
    tool.cancel(host);
    tool.release(host, QPoint(400, 400));
    CHECK(host.asked.size() == 1);
}

TEST_CASE("ui.linktool link.create arms the tool, and a drag makes ONE link over the box, going where it was asked", "[linktool][A-83]")
{
    Answers answers;
    Shell shell;
    REQUIRE(shell.fixture.actions.isRegistered(ActionId::LinkCreate));
    // Nobody to ask, no answer installed: refused (convention 16), and nothing armed.
    CHECK_FALSE(shell.fixture.actions.isEnabled(ActionId::LinkCreate));
    int asked = 0;
    ui::LinkPropertiesDialog::setAnswerForTesting([&asked](const core::Annotation& link, int pageCount, const QStringList&,
                                                           const core::Destination&) -> std::optional<ui::LinkPropertiesDialog::Choice> {
        ++asked;
        CHECK(link.subtype == core::AnnotationSubtype::Link);
        CHECK(pageCount == 5);
        ui::LinkPropertiesDialog::Choice choice;
        core::Destination to;
        to.pageIndex = 3;
        to.fit = core::Destination::Fit::Fit;
        choice.action = core::OutlineAction::goTo(to);
        choice.width = 1.0;
        choice.style = QStringLiteral("D");
        choice.colour = {0.8, 0, 0};
        choice.highlight = QStringLiteral("O");
        return choice;
    });
    REQUIRE(shell.fixture.actions.dispatch(ActionId::LinkCreate, core::KeyContext::Viewer));
    REQUIRE(shell.view->tools().armed() == ui::ToolKind::Link);
    CHECK(shell.view->tools().cursor() == Qt::CrossCursor);

    const auto [from, to] = shell.band();
    const ui::PagePoint start = shell.view->pagePointAt(from);
    const ui::PagePoint end = shell.view->pagePointOn(start.page, to);
    REQUIRE(start.page == 0);
    const std::size_t before = shell.model().annotations().size();
    shell.drag(from, to);
    CHECK(asked == 1);
    REQUIRE(shell.document->journal()->canUndo());
    REQUIRE(shell.model().annotations().size() == before + 1);
    const core::Annotation& made = shell.model().annotations().all().back();
    CHECK(made.subtype == core::AnnotationSubtype::Link);
    CHECK(made.page == 0);
    // Where it was dragged: within a device pixel of the two corners the drag went between.
    const double pixel = 1.0 / shell.view->pixelsPerPoint();
    CHECK(made.rect.left() == Approx(std::min(start.point.x, end.point.x)).margin(pixel));
    CHECK(made.rect.right() == Approx(std::max(start.point.x, end.point.x)).margin(pixel));
    CHECK(made.rect.bottom() == Approx(std::min(start.point.y, end.point.y)).margin(pixel));
    CHECK(made.rect.top() == Approx(std::max(start.point.y, end.point.y)).margin(pixel));
    CHECK(made.action.kind == core::OutlineAction::Kind::GoTo);
    CHECK(made.action.destination.pageIndex == 3);
    CHECK(made.borderWidth == 1.0);
    CHECK(made.borderStyle == QStringLiteral("D"));
    CHECK(made.dashPattern == std::vector<double>{3, 2});
    CHECK(made.highlight == QStringLiteral("O"));
    // One undo step takes it.
    REQUIRE(shell.document->journal()->undo());
    CHECK(shell.model().annotations().size() == before);

    // CANCELLED: the dialog answers nothing, and nothing is made.
    ui::LinkPropertiesDialog::setAnswerForTesting([](const core::Annotation&, int, const QStringList&, const core::Destination&) {
        return std::optional<ui::LinkPropertiesDialog::Choice>{};
    });
    shell.drag(from, to);
    CHECK(shell.model().annotations().size() == before);
}

TEST_CASE("ui.linktool the dialog's answer is what its fields say, for each target and appearance", "[linktool][A-86]")
{
    core::Annotation link = core::newLink(0, rect(10, 10, 110, 30), {}, core::OutlineAction{}, QStringLiteral("t"), 1791504000);
    ui::LinkPropertiesDialog dialog(link, 9, QStringList{QStringLiteral("alpha"), QStringLiteral("beta")}, core::Destination{});
    auto* target = dialog.findChild<QComboBox*>(QStringLiteral("targetCombo"));
    auto* page = dialog.findChild<QSpinBox*>(QStringLiteral("targetPage"));
    auto* named = dialog.findChild<QComboBox*>(QStringLiteral("targetNamed"));
    auto* text = dialog.findChild<QLineEdit*>(QStringLiteral("targetText"));
    auto* visible = dialog.findChild<QCheckBox*>(QStringLiteral("visibleCheck"));
    auto* width = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("widthSpin"));
    auto* style = dialog.findChild<QComboBox*>(QStringLiteral("styleCombo"));
    auto* colour = dialog.findChild<QComboBox*>(QStringLiteral("colourCombo"));
    auto* highlight = dialog.findChild<QComboBox*>(QStringLiteral("highlightCombo"));
    REQUIRE(target);
    REQUIRE(page);
    REQUIRE(named);
    REQUIRE(text);
    REQUIRE(visible);
    REQUIRE(width);
    REQUIRE(style);
    REQUIRE(colour);
    REQUIRE(highlight);
    // A NEW link starts at "a page", invisible -- `newLink`'s.
    CHECK(target->currentIndex() == 1);
    CHECK_FALSE(visible->isChecked());
    page->setValue(7);
    auto choice = dialog.choice();
    CHECK(choice.action.kind == core::OutlineAction::Kind::GoTo);
    CHECK(choice.action.destination.pageIndex == 6);
    CHECK(choice.width == 0.0);
    CHECK(choice.colour.empty());

    target->setCurrentIndex(3);   // a named destination
    named->setCurrentIndex(1);
    choice = dialog.choice();
    CHECK(choice.action.kind == core::OutlineAction::Kind::GoToNamed);
    CHECK(choice.action.name == QStringLiteral("beta"));
    target->setCurrentIndex(4);   // a web address
    text->setText(QStringLiteral("  https://example.com/a  "));
    choice = dialog.choice();
    CHECK(choice.action.kind == core::OutlineAction::Kind::Uri);
    CHECK(choice.action.uri == QStringLiteral("https://example.com/a"));
    target->setCurrentIndex(10);   // reset the form
    CHECK(dialog.choice().action.kind == core::OutlineAction::Kind::ResetForm);
    target->setCurrentIndex(11);   // nowhere
    CHECK(dialog.choice().action.kind == core::OutlineAction::Kind::None);

    // Visible, 2 points, underline, green, push.
    visible->setChecked(true);
    width->setValue(2.0);
    style->setCurrentIndex(2);
    colour->setCurrentIndex(2);
    highlight->setCurrentIndex(3);
    choice = dialog.choice();
    CHECK(choice.width == 2.0);
    CHECK(choice.style == QStringLiteral("U"));
    CHECK(choice.colour == std::vector<double>{0.0, 0.5, 0.0});
    CHECK(choice.highlight == QStringLiteral("P"));
    // Invert, ISO's default, is written as nothing.
    highlight->setCurrentIndex(1);
    CHECK(dialog.choice().highlight.isEmpty());
    // Applied: the link carries all of it, and a dashed style its dash array.
    style->setCurrentIndex(1);
    const core::Annotation applied = dialog.choice().appliedTo(link);
    CHECK(applied.borderStyle == QStringLiteral("D"));
    CHECK(applied.dashPattern == std::vector<double>{3, 2});
    CHECK(applied.color == std::vector<double>{0.0, 0.5, 0.0});
}

// ==========================================================================================
// A-82's surface half -- the Destinations pane's three commands
// ==========================================================================================

TEST_CASE("ui.destinations create from the current view, rename and delete -- each one step, the pane showing the model", "[linktool][destinations][A-82]")
{
    Answers answers;
    Shell shell;
    for (const ActionId id : {ActionId::DestinationCreate, ActionId::DestinationRename, ActionId::DestinationDelete, ActionId::LinkVerify,
                              ActionId::LinkAutoDetect, ActionId::LinkEdit, ActionId::LinkDelete})
        CHECK(shell.fixture.actions.isRegistered(id));
    ui::DestinationsPane* pane = shell.window->destinationsPane();
    REQUIRE(pane != nullptr);
    CHECK_FALSE(shell.fixture.actions.isEnabled(ActionId::DestinationCreate));   // nobody to name it (convention 16)

    ui::DestinationNameDialog::setAnswerForTesting([](const QString& initial) -> std::optional<QString> {
        CHECK(initial.contains(QLatin1Char('1')));   // "Destination 1": the next free one
        return QStringLiteral("Chapter one");
    });
    shell.view->goToPage(2);
    QCoreApplication::processEvents();
    REQUIRE(shell.fixture.actions.dispatch(ActionId::DestinationCreate, core::KeyContext::Global));
    const core::NamedDestination* made = shell.model().destinations().find(QStringLiteral("Chapter one"));
    REQUIRE(made != nullptr);
    CHECK(made->destination.pageIndex == 2);
    CHECK(made->destination.fit == core::Destination::Fit::XYZ);
    CHECK(made->form == core::NamedDestination::Form::NameTree);
    CHECK(shell.fixture.backend.sinkFor(shell.document->handle())->destinationCommands == 1);
    CHECK(pane->rowCount() == 1);
    CHECK(pane->selectedName() == QStringLiteral("Chapter one"));

    // RENAMED, from the pane's selection.
    ui::DestinationNameDialog::setAnswerForTesting([](const QString& initial) -> std::optional<QString> {
        CHECK(initial == QStringLiteral("Chapter one"));
        return QStringLiteral("Chapter 1");
    });
    REQUIRE(shell.fixture.actions.isEnabled(ActionId::DestinationRename));
    REQUIRE(shell.fixture.actions.dispatch(ActionId::DestinationRename, core::KeyContext::Global));
    CHECK(shell.model().destinations().find(QStringLiteral("Chapter 1")) != nullptr);
    CHECK(pane->nameAt(0) == QStringLiteral("Chapter 1"));
    // DELETED.
    REQUIRE(shell.fixture.actions.dispatch(ActionId::DestinationDelete, core::KeyContext::Global));
    CHECK(shell.model().destinations().isEmpty());
    CHECK(pane->rowCount() == 0);
    CHECK_FALSE(shell.fixture.actions.isEnabled(ActionId::DestinationRename));   // nothing selected: nothing to rename
    // Each one step: three undos put back the one destination, then none.
    REQUIRE(shell.document->journal()->undo());
    CHECK(pane->nameAt(0) == QStringLiteral("Chapter 1"));
    REQUIRE(shell.document->journal()->undo());
    CHECK(pane->nameAt(0) == QStringLiteral("Chapter one"));
    REQUIRE(shell.document->journal()->undo());
    CHECK(pane->rowCount() == 0);
}
