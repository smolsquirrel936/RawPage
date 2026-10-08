// tst_drawtools -- Stage 6 T8 through the widgets: the drawing tools, the pencil and the eraser, `K`, `Enter`,
// `Ctrl+Alt+Enter`, the Commenting page and the properties bar's new properties -- MOUSE AND KEY EVENTS to the page,
// over the fake backend's store (`Stage6-Annotations-Testing-Guide.md` §5.10; A-58, A-46's polygon, A-60, A-61,
// A-62, A-47's key; D6-9, D6-13, D6-15).
//
//   * A-58: each shape tool makes ONE annotation of its kind from ONE gesture -- a rectangle and an oval dragged
//     out (with `Shift`, a square and a circle), a line and an arrow dragged (with `Shift`, at 45 degrees), a
//     polygon, a polyline and a cloud clicked corner by corner and finished by `Enter` or a double click; a click
//     that does not drag makes nothing.
//   * A-46's polygon: `Esc` with a polygon in progress makes nothing, leaves the journal as it was and returns to
//     the Selection tool; `Enter` with none in progress is nothing.
//   * A-60 through the tools: the pencil's stroke is ONE `Ink` of the samples the moves gave; the eraser dragged
//     through its middle is ONE edit leaving two strokes, its undo the very samples; erased whole it is deleted.
//   * A-62: `K` writes `commenting/stickyTool`; on, the tool stays armed after two rectangles; off, it returns.
//   * A-61: the bar's changes are the session's; `Ctrl+Alt+Enter` writes them as the armed tool's defaults and the
//     next annotation uses them; the Commenting page's tool editor writes the family too.
//   * A-47's key: `commenting/popupBehaviour` is what the overlay does.
//   * The bar (T8's debt): a selected line's endings and a callout's ending are ONE `SetAnnotationAppearance`; a
//     text box's font size is ONE `EditAnnotation`, refitted.
//
// WHAT IT DOES NOT COVER (convention 19): what the sink WRITES -- the fake sink accepts everything; the file is
// `tests/unit/tst_annotdrawing`'s -- and the engine's pixels (`tests/composition/tst_drawink`). Events go to the
// widget below Windows' input queue, as every widget case's do.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/doc/DrawnShapes.h"
#include "core/doc/TypedText.h"
#include "core/settings/Settings.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/dialogs/SettingsPage.h"
#include "ui/dialogs/ToolDefaultsEditor.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/PropertiesBar.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/ToolDefaults.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QSpinBox>

#include <cmath>

using namespace rawpage;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;

namespace {

constexpr Qt::KeyboardModifiers kCtrlAlt = Qt::ControlModifier | Qt::AltModifier;

core::PdfRect box(double l, double b, double r, double t)
{
    return core::PdfRect{core::PdfPoint{l, b}, core::PdfSize{r - l, t - b}};
}

// What is on page 1 before a tool is used: a text box, to change the size of (T8's debt of T7).
core::AnnotationStore startingStore()
{
    core::AnnotationStore::Reading reading;
    reading.pages.resize(2);
    Annotation text;
    text.id = AnnotationId::ofObject(20);
    text.subtype = AnnotationSubtype::FreeText;
    text.page = 0;
    text.contents = QStringLiteral("Resize me");
    text.color = {0, 0, 0};
    text.borderWidth = 1.0;
    text.defaultAppearance = QStringLiteral("/Helv 12 Tf 0 g");
    text.rect = box(400, 700, 560, 720);
    text.hasAppearance = true;
    core::AnnotationListingEntry entry;
    entry.fact = core::AnnotationFact{true, text.subtype, text.rect};
    entry.annotation = 0;
    reading.pages[0].push_back(entry);
    reading.annotations.push_back(text);
    return core::AnnotationStore::fromReading(std::move(reading));
}

struct Fixture : uitest::ShellFixture {
    QString writeFile(const QString& name)
    {
        const QString path = QDir(directory.path()).filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("the fake backend decides, not these bytes");
        file.close();
        return QFileInfo(path).absoluteFilePath();
    }
};

struct Drawing {
    Fixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow* document = nullptr;
    ui::PageView* view = nullptr;
    ui::AnnotationOverlay* overlay = nullptr;

    Drawing()
    {
        ui::clearSessionToolChanges();   // the session is the process's, and the next case shares it
        fixture.backend.pagesPerDocument = 2;
        fixture.backend.editable = true;
        fixture.backend.annotations_ = startingStore();
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 900);
        window->openFiles({fixture.writeFile(QStringLiteral("drawing.pdf"))});
        window->show();
        for (QWidget* widget : window->findChildren<QWidget*>())
            if (QLayout* layout = widget->layout()) layout->activate();
        QCoreApplication::processEvents();
        document = window->currentDocument();
        REQUIRE(document != nullptr);
        view = window->currentView();
        REQUIRE(view != nullptr);
        REQUIRE(view->width() > 300);   // the geometry first (convention 12)
        overlay = document->annotations();
        REQUIRE(overlay != nullptr);
        REQUIRE(overlay->isActive());
        overlay->setClock([] { return std::int64_t{1791763200}; });
        window->raiseToForeground();
        view->setFocus();
        QCoreApplication::processEvents();
    }
    ~Drawing() { ui::clearSessionToolChanges(); }

    core::CommandJournal& journal() const { return *document->journal(); }
    const core::AnnotationStore& store() const { return journal().document().annotations(); }
    std::size_t historySize() const { return journal().historySize(); }

    // The annotations RawPage made, in the order made.
    std::vector<Annotation> made() const
    {
        std::vector<Annotation> out;
        for (const Annotation& a : store().all())
            if (a.id.kind == AnnotationId::Kind::Created) out.push_back(a);
        return out;
    }

    // A page point on page 1, where it is on screen, logical pixels.
    QPoint at(double x, double y) const
    {
        const std::optional<QPointF> w = view->widgetPointOf(0, core::PdfPoint{x, y});
        REQUIRE(w);
        return QPoint(qRound(w->x()), qRound(w->y()));
    }
    core::PdfPoint pageOf(QPoint pos) const
    {
        const ui::PagePoint p = view->pagePointOn(0, pos);
        REQUIRE(p.isValid());
        return p.point;
    }

    void mouse(QEvent::Type type, QPoint pos, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        QMouseEvent event(type, QPointF(pos), QPointF(view->viewport()->mapToGlobal(pos)), Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QCoreApplication::processEvents();
    }
    void hover(QPoint pos, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        QMouseEvent event(QEvent::MouseMove, QPointF(pos), QPointF(view->viewport()->mapToGlobal(pos)), Qt::NoButton,
                          Qt::NoButton, modifiers);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QCoreApplication::processEvents();
    }
    void drag(QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        mouse(QEvent::MouseButtonPress, from, modifiers);
        for (int step = 1; step <= 4; ++step) mouse(QEvent::MouseMove, from + (to - from) * step / 4, modifiers);
        mouse(QEvent::MouseButtonRelease, to, modifiers);
    }
    void click(QPoint pos, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        mouse(QEvent::MouseButtonPress, pos, modifiers);
        mouse(QEvent::MouseButtonRelease, pos, modifiers);
    }
    void doubleClick(QPoint pos) const
    {
        mouse(QEvent::MouseButtonPress, pos);
        mouse(QEvent::MouseButtonRelease, pos);
        QMouseEvent event(QEvent::MouseButtonDblClick, QPointF(pos), QPointF(view->viewport()->mapToGlobal(pos)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &event);
        mouse(QEvent::MouseButtonRelease, pos);
    }
    // A key as a person presses one: to the focused widget, through the application -- the window's
    // `ShortcutBinder` sees it first.
    bool key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        QWidget* focus = QApplication::focusWidget();
        if (focus == nullptr) focus = view;
        QKeyEvent override(QEvent::ShortcutOverride, key, modifiers);
        override.ignore();
        QCoreApplication::sendEvent(focus, &override);
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        event.ignore();
        QCoreApplication::sendEvent(focus, &event);
        QCoreApplication::processEvents();
        return event.isAccepted() || override.isAccepted();
    }
    void arm(core::ActionId id) const
    {
        REQUIRE(fixture.actions.dispatch(id, core::KeyContext::Viewer));
        QCoreApplication::processEvents();
        view->setFocus();
    }
};

} // namespace

TEST_CASE("each shape tool makes one annotation of its kind from one gesture", "[drawtools][A-58]")
{
    Drawing d;
    const std::size_t steps = d.historySize();

    // A RECTANGLE dragged out with its chord.
    REQUIRE(d.key(Qt::Key_R, kCtrlAlt));
    REQUIRE(d.view->tools().armedAnnotation() == ui::AnnotationToolKind::Rectangle);
    const QPoint from = d.at(100, 500), to = d.at(250, 420);
    d.drag(from, to);
    REQUIRE(d.made().size() == 1);
    CHECK(d.historySize() == steps + 1);
    const Annotation rectangle = d.made().back();
    CHECK(rectangle.subtype == AnnotationSubtype::Square);
    const core::PdfPoint a = d.pageOf(from), b = d.pageOf(to);
    CHECK(std::abs(rectangle.rect.left() - std::min(a.x, b.x)) < 0.01);
    CHECK(std::abs(rectangle.rect.top() - std::max(a.y, b.y)) < 0.01);
    CHECK(rectangle.borderStyle == QStringLiteral("S"));
    CHECK(rectangle.color == core::builtInToolDefaults(core::CommentingTool::Rectangle).colour);
    // Made, the tool went back to the Selection tool (K off).
    CHECK(d.view->tools().armed() == ui::ToolKind::Select);

    // A CLICK with the rectangle tool makes nothing.
    REQUIRE(d.key(Qt::Key_R, kCtrlAlt));
    d.click(d.at(300, 300));
    CHECK(d.made().size() == 1);

    // An OVAL with Shift: a circle.
    REQUIRE(d.key(Qt::Key_O, kCtrlAlt));
    d.drag(d.at(100, 300), d.at(220, 260), Qt::ShiftModifier);
    REQUIRE(d.made().size() == 2);
    const Annotation circle = d.made().back();
    CHECK(circle.subtype == AnnotationSubtype::Circle);
    CHECK(std::abs(circle.rect.size.width - circle.rect.size.height) < 1e-6);
    CHECK(d.view->tools().temporaryHandActive() == false);   // Shift was the constraint's, not the hand's

    // A LINE with Shift: at 45 degrees.
    REQUIRE(d.key(Qt::Key_L, kCtrlAlt));
    d.drag(d.at(300, 500), d.at(400, 440), Qt::ShiftModifier);
    REQUIRE(d.made().size() == 3);
    const Annotation line = d.made().back();
    REQUIRE(line.subtype == AnnotationSubtype::Line);
    REQUIRE(line.line.size() == 4);
    const double dx = line.line[2] - line.line[0], dy = line.line[3] - line.line[1];
    CHECK((std::abs(std::abs(dx) - std::abs(dy)) < 1e-6 || std::abs(dx) < 1e-9 || std::abs(dy) < 1e-9));
    CHECK(line.lineEndings.empty());

    // An ARROW, chordless: a Line whose end is an open arrow.
    d.arm(core::ActionId::AnnotArrow);
    d.drag(d.at(300, 300), d.at(450, 330));
    REQUIRE(d.made().size() == 4);
    const Annotation arrow = d.made().back();
    CHECK(arrow.subtype == AnnotationSubtype::Line);
    CHECK(arrow.lineEndings == std::vector<QString>{QStringLiteral("None"), QStringLiteral("OpenArrow")});
    CHECK(d.historySize() == steps + 4);   // four gestures, four commands
}

TEST_CASE("a polygon is clicked corner by corner and finished by Enter or a double click and Esc abandons it",
          "[drawtools][A-58][A-46]")
{
    Drawing d;
    // `Enter` with nothing in progress: nothing.
    REQUIRE(d.key(Qt::Key_N, kCtrlAlt));
    const std::size_t before = d.historySize();
    d.key(Qt::Key_Return);
    CHECK(d.historySize() == before);
    CHECK(d.view->tools().armedAnnotation() == ui::AnnotationToolKind::Polygon);

    // A POLYGON: three clicks and `Enter`, the rubber band following the pointer between them.
    d.click(d.at(100, 600));
    d.hover(d.at(150, 650));
    d.click(d.at(200, 600));
    d.click(d.at(150, 520));
    CHECK(d.view->tools().shapeInProgress());
    CHECK(d.view->tools().drawing().vertices().size() == 3);
    d.key(Qt::Key_Return);
    CHECK_FALSE(d.view->tools().shapeInProgress());
    REQUIRE(d.made().size() == 1);
    CHECK(d.made().back().subtype == AnnotationSubtype::Polygon);
    CHECK(d.made().back().vertices.size() == 6);
    CHECK(d.historySize() == before + 1);

    // A POLYLINE, chordless: four clicks, the last a DOUBLE click -- four vertices, not five.
    d.arm(core::ActionId::AnnotPolyline);
    d.click(d.at(100, 400));
    d.click(d.at(160, 450));
    d.click(d.at(220, 400));
    d.doubleClick(d.at(280, 450));
    REQUIRE(d.made().size() == 2);
    CHECK(d.made().back().subtype == AnnotationSubtype::PolyLine);
    CHECK(d.made().back().vertices.size() == 8);

    // A CLOUD, minted chordless: a Polygon with a cloudy border, Acrobat's /IT.
    d.arm(core::ActionId::AnnotCloud);
    for (const auto& [x, y] : {std::pair{300.0, 300.0}, std::pair{450.0, 300.0}, std::pair{450.0, 220.0}, std::pair{300.0, 220.0}})
        d.click(d.at(x, y));
    d.key(Qt::Key_Return);
    REQUIRE(d.made().size() == 3);
    const Annotation cloud = d.made().back();
    CHECK(cloud.subtype == AnnotationSubtype::Polygon);
    REQUIRE(cloud.cloudIntensity);
    CHECK(*cloud.cloudIntensity == 1.0);
    CHECK(cloud.intent == QStringLiteral("PolygonCloud"));
    // Its rectangle holds its bumps.
    CHECK(cloud.rect.left() < 300 - core::cloudReach(cloud.vertices, 1.0) + 0.01);

    // `Shift` keeps a new edge to 45 degrees.
    REQUIRE(d.key(Qt::Key_N, kCtrlAlt));
    d.click(d.at(100, 200));
    d.click(d.at(180, 215), Qt::ShiftModifier);
    const std::vector<core::PdfPoint>& corners = d.view->tools().drawing().vertices();
    REQUIRE(corners.size() == 2);
    CHECK(std::abs(corners[1].y - corners[0].y) < 1e-6);   // nearly flat: flat

    // `Esc` mid-polygon: abandoned, nothing made, the journal as it was, the Selection tool (A-46).
    const std::size_t steps = d.historySize();
    const QByteArray hash = d.journal().document().modelHash();
    d.key(Qt::Key_Escape);
    CHECK_FALSE(d.view->tools().shapeInProgress());
    CHECK(d.historySize() == steps);
    CHECK(d.journal().document().modelHash() == hash);
    CHECK(d.view->tools().armed() == ui::ToolKind::Select);
    CHECK(d.made().size() == 3);
}

TEST_CASE("the pencil draws one stroke and the eraser erases it in one step that undoes exactly",
          "[drawtools][A-60][D6-15]")
{
    Drawing d;
    REQUIRE(d.key(Qt::Key_P, kCtrlAlt));
    REQUIRE(d.view->tools().armedAnnotation() == ui::AnnotationToolKind::Pencil);
    // A stroke across the page, sampled by the moves.
    std::vector<QPoint> path;
    for (int i = 0; i <= 20; ++i) path.push_back(d.at(100 + i * 10, 400 + (i % 2) * 6));
    d.mouse(QEvent::MouseButtonPress, path.front());
    for (std::size_t i = 1; i < path.size(); ++i) d.mouse(QEvent::MouseMove, path[i]);
    d.mouse(QEvent::MouseButtonRelease, path.back());
    REQUIRE(d.made().size() == 1);
    const Annotation ink = d.made().back();
    REQUIRE(ink.subtype == AnnotationSubtype::Ink);
    REQUIRE(ink.inkList.size() == 1);
    CHECK(ink.inkList.front().size() == path.size() * 2);   // every move a sample; none closer than a quarter pixel
    for (std::size_t i = 0; i < path.size(); ++i) {
        const core::PdfPoint p = d.pageOf(path[i]);
        CHECK(std::abs(ink.inkList.front()[2 * i] - p.x) < 1e-6);
        CHECK(std::abs(ink.inkList.front()[2 * i + 1] - p.y) < 1e-6);
    }

    // THE ERASER, chordless, dragged down through the stroke's middle: ONE edit, two strokes.
    d.arm(core::ActionId::AnnotEraser);
    const std::size_t steps = d.historySize();
    d.drag(d.at(200, 430), d.at(200, 370));
    CHECK(d.historySize() == steps + 1);
    const std::optional<std::size_t> index = d.store().find(ink.id);
    REQUIRE(index);
    CHECK(d.store().at(*index).inkList.size() == 2);
    CHECK(d.view->tools().armedAnnotation() == ui::AnnotationToolKind::Eraser);   // the eraser stays

    // Undone: the very samples.
    REQUIRE(d.journal().undo());
    CHECK(d.store().at(*d.store().find(ink.id)).inkList == ink.inkList);
    REQUIRE(d.journal().redo());

    // Erased whole: the ink is deleted, one step.
    d.drag(d.at(90, 400), d.at(310, 400));
    d.drag(d.at(90, 406), d.at(310, 406));
    CHECK_FALSE(d.store().find(ink.id));
}

TEST_CASE("K writes commenting/stickyTool and keeps the tool armed after a use", "[drawtools][A-62][D6-13]")
{
    Drawing d;
    core::Settings& settings = d.fixture.settings;
    REQUIRE_FALSE(settings.get(core::keys::commenting::stickyTool));
    REQUIRE(d.key(Qt::Key_R, kCtrlAlt));
    d.key(Qt::Key_K);
    CHECK(settings.get(core::keys::commenting::stickyTool));
    CHECK(d.view->tools().keepToolSelected());
    d.drag(d.at(100, 500), d.at(160, 450));
    d.drag(d.at(200, 500), d.at(260, 450));
    CHECK(d.made().size() == 2);
    CHECK(d.view->tools().armedAnnotation() == ui::AnnotationToolKind::Rectangle);   // still armed
    d.key(Qt::Key_K);
    CHECK_FALSE(settings.get(core::keys::commenting::stickyTool));
    d.drag(d.at(300, 500), d.at(360, 450));
    CHECK(d.made().size() == 3);
    CHECK(d.view->tools().armed() == ui::ToolKind::Select);   // back to Select
    // And the key reaches a view opened later: the settings are where it lives.
    settings.set(core::keys::commenting::stickyTool, true);
    CHECK(d.view->tools().keepToolSelected());
}

TEST_CASE("Ctrl Alt Enter writes the armed tool's properties as its defaults and the next annotation uses them",
          "[drawtools][A-61][D6-13]")
{
    Drawing d;
    core::Settings& settings = d.fixture.settings;
    REQUIRE(d.key(Qt::Key_R, kCtrlAlt));
    REQUIRE(d.key(Qt::Key_E, Qt::ControlModifier));   // the bar, showing the rectangle tool's properties
    ui::PropertiesBar* bar = d.document->findChild<ui::PropertiesBar*>();
    REQUIRE(bar != nullptr);
    REQUIRE(bar->subject() == ui::PropertiesBar::Subject::ToolDefaults);
    CHECK(bar->widthBox()->isEnabled());
    CHECK(bar->lineStyleBox()->isEnabled());
    CHECK_FALSE(bar->fontSizeBox()->isEnabled());   // a rectangle has no font size (ToolDefaults.def)

    // The bar's change is the SESSION's: the next rectangle uses it, the settings do not hold it.
    bar->colourBox()->setCurrentIndex(6);   // the swatches' blue
    bar->widthBox()->setValue(4.0);
    bar->lineStyleBox()->setCurrentIndex(1);   // dashed
    REQUIRE(ui::sessionToolChange(core::CommentingTool::Rectangle));
    CHECK(settings.toolDefaults(core::CommentingTool::Rectangle) == core::builtInToolDefaults(core::CommentingTool::Rectangle));

    // `Ctrl+Alt+Enter`: written to the family.
    d.view->setFocus();
    d.key(Qt::Key_Return, kCtrlAlt);
    const core::ToolDefaults written = settings.toolDefaults(core::CommentingTool::Rectangle);
    CHECK(written.colour == std::vector<double>{0.0, 0.0, 1.0});
    CHECK(written.thickness == 4.0);
    CHECK(written.lineStyle == core::LineStyle::Dashed);
    CHECK_FALSE(ui::sessionToolChange(core::CommentingTool::Rectangle));   // what it draws with is what was written

    // The next rectangle is drawn with them.
    d.drag(d.at(100, 500), d.at(200, 420));
    REQUIRE(d.made().size() == 1);
    const Annotation rectangle = d.made().back();
    CHECK(rectangle.color == std::vector<double>{0.0, 0.0, 1.0});
    CHECK(*rectangle.borderWidth == 4.0);
    CHECK(rectangle.borderStyle == QStringLiteral("D"));
    CHECK(rectangle.dashPattern == core::dashFor(core::LineStyle::Dashed, 4.0));

    // THE COMMENTING PAGE: its two generated rows, and the tool editor writes the family.
    ui::PreferencesDialog* preferences = d.window->showPreferences();
    REQUIRE(preferences != nullptr);
    ui::SettingsPage* page = preferences->settingsPage(core::SettingsGroup::Commenting);
    REQUIRE(page != nullptr);
    CHECK(page->rowPaths() == QStringList{QStringLiteral("commenting/stickyTool"), QStringLiteral("commenting/popupBehaviour")});
    ui::ToolDefaultsEditor* editor = preferences->toolDefaultsEditor();
    REQUIRE(editor != nullptr);
    editor->showTool(core::CommentingTool::Rectangle);
    CHECK(editor->thicknessBox()->value() == 4.0);   // what Ctrl+Alt+Enter wrote
    CHECK_FALSE(editor->fontSizeBox()->isEnabled());
    editor->fillBox()->setCurrentIndex(1);   // the swatches' yellow
    CHECK(settings.toolDefaults(core::CommentingTool::Rectangle).fillColour == std::vector<double>{1.0, 1.0, 0.0});
    REQUIRE(d.key(Qt::Key_R, kCtrlAlt));
    d.drag(d.at(250, 500), d.at(350, 420));
    REQUIRE(d.made().size() == 2);
    CHECK(d.made().back().interiorColor == std::vector<double>{1.0, 1.0, 0.0});
    // Reset on the page: the table's defaults again.
    editor->restoreDefaults();
    CHECK(settings.toolDefaults(core::CommentingTool::Rectangle) == core::builtInToolDefaults(core::CommentingTool::Rectangle));
    preferences->close();
}

TEST_CASE("commenting popupBehaviour is what the overlay does", "[drawtools][A-47][D6-28]")
{
    Drawing d;
    core::Settings& settings = d.fixture.settings;
    CHECK(d.overlay->popupBehaviour() == ui::AnnotationOverlay::PopupBehaviour::OnHover);
    settings.set(core::keys::commenting::popupBehaviour, core::PopupBehaviour::Never);
    CHECK(d.overlay->popupBehaviour() == ui::AnnotationOverlay::PopupBehaviour::Never);
    settings.set(core::keys::commenting::popupBehaviour, core::PopupBehaviour::Always);
    CHECK(d.overlay->popupBehaviour() == ui::AnnotationOverlay::PopupBehaviour::Always);
}

TEST_CASE("the bar changes a line's endings and a text box's size by one command each", "[drawtools][D6-35]")
{
    Drawing d;
    REQUIRE(d.key(Qt::Key_L, kCtrlAlt));
    d.drag(d.at(100, 500), d.at(300, 500));
    REQUIRE(d.made().size() == 1);
    const AnnotationId line = d.made().back().id;
    d.overlay->select({line});
    REQUIRE(d.key(Qt::Key_E, Qt::ControlModifier));
    ui::PropertiesBar* bar = d.document->findChild<ui::PropertiesBar*>();
    REQUIRE(bar != nullptr);
    REQUIRE(bar->subject() == ui::PropertiesBar::Subject::Selection);
    CHECK(bar->startEndingBox()->isEnabled());
    CHECK(bar->endEndingBox()->isEnabled());
    CHECK_FALSE(bar->fontSizeBox()->isEnabled());
    const std::size_t steps = d.historySize();
    bar->endEndingBox()->setCurrentIndex(2);   // ClosedArrow
    CHECK(d.historySize() == steps + 1);
    CHECK(d.store().at(*d.store().find(line)).lineEndings
          == std::vector<QString>{QStringLiteral("None"), QStringLiteral("ClosedArrow")});

    // The text box the page had: a new size is ONE edit, refitted, `/DA` at that size.
    d.overlay->select({AnnotationId::ofObject(20)});
    QCoreApplication::processEvents();
    REQUIRE(bar->subject() == ui::PropertiesBar::Subject::Selection);
    REQUIRE(bar->fontSizeBox()->isEnabled());
    CHECK(bar->fontSizeBox()->value() == 12.0);
    const core::PdfRect was = d.store().at(*d.store().findObject(20)).rect;
    bar->fontSizeBox()->setValue(24.0);
    CHECK(d.historySize() == steps + 2);
    const Annotation text = d.store().at(*d.store().findObject(20));
    CHECK(core::freeTextStyle(text).size == 24.0);
    CHECK(text.rect.size.height > was.size.height);   // refitted to the larger lines
}
