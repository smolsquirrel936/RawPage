// tst_measuretool -- Stage 6 T12 through the widgets: the measuring tools, their snap, calibration, the scale dialog
// and the history -- MOUSE AND KEY EVENTS to the page, over the fake backend (`Stage6-Annotations-Testing-Guide.md`
// §5.15; A-89's tool half, A-90, A-91's notice, A-92's surfaces; D6-14).
//
//   * Each measuring tool makes ONE measurement from ONE gesture -- a distance dragged or clicked twice, a
//     perimeter and an area clicked point by point and finished by `Enter` or a double click, a dimension callout's
//     third click setting its line -- each of its kind, `/IT` and scale, read in the scale the keys state.
//   * A-89's tool half: a press and a release THREE DEVICE PIXELS from a path's corner, midpoint or crossing land
//     EXACTLY on it, at 100 % and 400 %; with `units/snapToObjects` off they land where the pointer was.
//   * A-90: calibration -- the distance tool armed by `annot.measureCalibrate`, a length measured and said to be
//     12 ft in the dialog -- sets `measuring/scaleRatio` to 48 and the next distance reads at it; nothing is made.
//   * A-91's notice: a page of more segments than the bound gives no object snaps and says so ONCE.
//   * A-92: the reading in `/Contents` exactly when `measuring/writeToComment` is on; the properties bar's history.
//
// WHAT IT DOES NOT COVER (convention 19): the paths are the FAKE backend's, given in the case -- the engine's reading
// of the corpus drawing, and a drag over the real one, are `tests/render/tst_measuregeometry` and
// `tests/composition/tst_measureproduct`; what the sink WRITES is `tests/unit/tst_measure`'s; and the dialog is
// answered through `MainWindow::setMeasurementScaleAnswerForTesting`, its modal loop never run (a person meets it in
// M-09). Events go to the widget below Windows' input queue, as every widget case's do.

#include <catch2/catch_test_macros.hpp>

#include "core/Measure.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/settings/Settings.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/MeasurementScaleDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/PropertiesBar.h"
#include "ui/viewer/SnapTargets.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/ToolDefaults.h"

#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>

#include <cmath>

using namespace rawpage;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;

namespace {

constexpr Qt::KeyboardModifiers kCtrlAlt = Qt::ControlModifier | Qt::AltModifier;

// The corpus drawing's segments, as `corpus/make-annots.py` draws them on `measure-scale.pdf`.
std::vector<core::PathSegment> corpusDrawing()
{
    const double s[][4] = {{100, 500, 316, 500}, {316, 500, 316, 644}, {316, 644, 100, 644}, {100, 644, 100, 500},
                           {100, 500, 316, 644}, {100, 644, 316, 500}, {100, 400, 388, 400}, {244, 350, 244, 450}};
    std::vector<core::PathSegment> out;
    for (const auto& v : s)
        out.push_back(core::PathSegment{core::PdfPoint{v[0], v[1]}, core::PdfPoint{v[2], v[3]}, false});
    return out;
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

struct Measuring {
    Fixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow* document = nullptr;
    ui::PageView* view = nullptr;

    explicit Measuring(std::vector<core::PathSegment> paths = corpusDrawing())
    {
        ui::clearSessionToolChanges();
        fixture.backend.pagesPerDocument = 2;
        fixture.backend.editable = true;
        fixture.backend.annotations_ = core::AnnotationStore::fromReading([] {
            core::AnnotationStore::Reading reading;
            reading.pages.resize(2);
            return reading;
        }());
        fixture.backend.pagePaths[0] = std::move(paths);
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
        REQUIRE(document->annotations()->isActive());
        document->annotations()->setClock([] { return std::int64_t{1792022400}; });
        window->raiseToForeground();
        view->setFocus();
        QCoreApplication::processEvents();
    }
    ~Measuring()
    {
        ui::MainWindow::setMeasurementScaleAnswerForTesting({});
        ui::clearSessionToolChanges();
    }

    core::CommandJournal& journal() const { return *document->journal(); }
    std::size_t historySize() const { return journal().historySize(); }
    std::vector<Annotation> made() const
    {
        std::vector<Annotation> out;
        for (const Annotation& a : journal().document().annotations().all())
            if (a.id.kind == AnnotationId::Kind::Created) out.push_back(a);
        return out;
    }

    void zoom(double factor)
    {
        view->setZoom(core::render::Zoom{factor});
        view->refresh();
        QCoreApplication::processEvents();
        fixture.pumpTiles();
    }
    // Brings page 1's `rect` into view -- at 400 % the drawing is larger than the window.
    void reveal(double l, double b, double r, double t)
    {
        view->revealPageRect(0, core::PdfRect{core::PdfPoint{l, b}, core::PdfSize{r - l, t - b}});
        QCoreApplication::processEvents();
        fixture.pumpTiles();
    }

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
    void hover(QPoint pos) const
    {
        QMouseEvent event(QEvent::MouseMove, QPointF(pos), QPointF(view->viewport()->mapToGlobal(pos)), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QCoreApplication::processEvents();
    }
    void drag(QPoint from, QPoint to) const
    {
        mouse(QEvent::MouseButtonPress, from);
        for (int step = 1; step <= 4; ++step) mouse(QEvent::MouseMove, from + (to - from) * step / 4);
        mouse(QEvent::MouseButtonRelease, to);
    }
    void click(QPoint pos) const
    {
        mouse(QEvent::MouseButtonPress, pos);
        mouse(QEvent::MouseButtonRelease, pos);
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
    // A right press and its release, then the context menu event Windows sends after them (S6 guide I-48). Returns
    // whether the page ACCEPTED the menu event -- taken by a finished measurement -- rather than ignored it.
    bool rightClick(QPoint pos) const
    {
        const QPointF global(view->viewport()->mapToGlobal(pos));
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(pos), global, Qt::RightButton, Qt::RightButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(pos), global, Qt::RightButton, Qt::NoButton,
                            Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &release);
        QContextMenuEvent menu(QContextMenuEvent::Mouse, pos, global.toPoint());
        QCoreApplication::sendEvent(view->viewport(), &menu);
        QCoreApplication::processEvents();
        return menu.isAccepted();
    }
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
    // The page's paths asked for -- as the first aim at a page asks -- and delivered.
    void loadPaths(QPoint near)
    {
        hover(near);
        fixture.pumpTiles();
        REQUIRE(document->snapTargets()->geometry(0) != nullptr);
    }
    void setScale(double ratio, core::MeasureUnit unit, int precision = 2)
    {
        fixture.settings.set(core::keys::measuring::scaleRatio, ratio);
        fixture.settings.set(core::keys::measuring::displayUnit, unit);
        fixture.settings.set(core::keys::measuring::precision, precision);
        QCoreApplication::processEvents();
    }
};

std::string readingOf(const Annotation& a)
{
    const core::Result<core::MeasureReading> r = core::measured(a);
    REQUIRE(r);
    return r.value().text.toStdString();
}

} // namespace

// =============================================================================================
// The four tools
// =============================================================================================

TEST_CASE("the distance tool makes one measurement from a drag and from two clicks", "[measuretool][A-89][D6-14]")
{
    Measuring m;
    m.fixture.settings.set(core::keys::units::snapToObjects, false);   // the pointer's own points, for the arithmetic
    m.setScale(48.0, core::MeasureUnit::Feet);
    const std::size_t steps = m.historySize();

    REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
    REQUIRE(m.view->tools().armedAnnotation() == ui::AnnotationToolKind::MeasureDistance);
    const QPoint from = m.at(100, 500), to = m.at(316, 500);
    m.drag(from, to);
    REQUIRE(m.made().size() == 1);
    CHECK(m.historySize() == steps + 1);   // ONE command
    const Annotation distance = m.made().back();
    CHECK(distance.subtype == AnnotationSubtype::Line);
    CHECK(distance.intent == QStringLiteral("LineDimension"));
    REQUIRE(distance.measure);
    CHECK(distance.measure->ratio == QStringLiteral("1 in = 4 ft"));
    const core::PdfPoint a = m.pageOf(from), b = m.pageOf(to);
    REQUIRE(distance.line.size() == 4);
    CHECK(std::abs(distance.line[0] - a.x) < 1e-6);
    CHECK(std::abs(distance.line[2] - b.x) < 1e-6);
    // The reading in the keys' scale, and in /Contents (`measuring/writeToComment` is on by default).
    const double expected = std::hypot(b.x - a.x, b.y - a.y) / 18.0;   // 18 points a foot at 1 in = 4 ft
    CHECK(readingOf(distance) == core::formatMeasureNumber(expected, 2).toStdString() + " ft");
    CHECK(distance.contents.toStdString() == readingOf(distance));
    CHECK(m.view->tools().armed() == ui::ToolKind::Select);   // made: back to the Selection tool (K off)

    // TWO CLICKS: the first anchors, the second measures.
    REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
    m.click(m.at(100, 400));
    CHECK(m.made().size() == 1);
    CHECK(m.view->tools().measuring().inProgress());
    m.click(m.at(388, 400));
    REQUIRE(m.made().size() == 2);
    CHECK(m.historySize() == steps + 2);
}

TEST_CASE("a perimeter and an area are clicked point by point and finished by Enter or a double click",
          "[measuretool][D6-14]")
{
    Measuring m;
    m.fixture.settings.set(core::keys::units::snapToObjects, false);
    m.setScale(48.0, core::MeasureUnit::Feet);

    m.arm(core::ActionId::AnnotMeasurePerimeter);
    REQUIRE(m.view->tools().armedAnnotation() == ui::AnnotationToolKind::MeasurePerimeter);
    for (const QPoint p : {m.at(100, 500), m.at(100, 644), m.at(316, 644)}) m.click(p);
    CHECK(m.made().empty());
    REQUIRE(m.key(Qt::Key_Return));   // `annot.finishShape`
    REQUIRE(m.made().size() == 1);
    const Annotation perimeter = m.made().back();
    CHECK(perimeter.subtype == AnnotationSubtype::PolyLine);
    CHECK(perimeter.intent == QStringLiteral("PolyLineDimension"));
    CHECK(perimeter.vertices.size() == 6);

    REQUIRE(m.key(Qt::Key_A, kCtrlAlt));
    REQUIRE(m.view->tools().armedAnnotation() == ui::AnnotationToolKind::MeasureArea);
    for (const QPoint p : {m.at(100, 500), m.at(316, 500), m.at(316, 644)}) m.click(p);
    m.doubleClick(m.at(100, 644));
    REQUIRE(m.made().size() == 2);
    const Annotation area = m.made().back();
    CHECK(area.subtype == AnnotationSubtype::Polygon);
    CHECK(area.intent == QStringLiteral("PolygonDimension"));
    CHECK(area.vertices.size() == 8);
    CHECK(readingOf(area).find("sq ft") != std::string::npos);
}

TEST_CASE("a perimeter and an area are finished by a right click which places no point", "[measuretool][I-48]")
{
    // S6 guide I-48, the user's: a right click finishes a perimeter or an area as `Enter` does. The control halves:
    // with nothing in progress, and with a distance anchored, the right click is not the tool's and makes nothing.
    Measuring m;
    m.fixture.settings.set(core::keys::units::snapToObjects, false);
    m.setScale(48.0, core::MeasureUnit::Feet);
    const std::size_t steps = m.historySize();

    m.arm(core::ActionId::AnnotMeasurePerimeter);
    CHECK_FALSE(m.rightClick(m.at(450, 700)));   // nothing in progress: the page's, not the tool's
    CHECK(m.made().empty());
    for (const QPoint p : {m.at(100, 500), m.at(100, 644), m.at(316, 644)}) m.click(p);
    CHECK(m.made().empty());
    CHECK(m.rightClick(m.at(450, 700)));   // taken, and its menu with it
    REQUIRE(m.made().size() == 1);
    CHECK(m.historySize() == steps + 1);   // ONE command
    const Annotation perimeter = m.made().back();
    CHECK(perimeter.intent == QStringLiteral("PolyLineDimension"));
    CHECK(perimeter.vertices.size() == 6);   // three points: the right click placed none
    CHECK_FALSE(m.view->tools().measuring().inProgress());

    REQUIRE(m.key(Qt::Key_A, kCtrlAlt));
    for (const QPoint p : {m.at(100, 500), m.at(316, 500), m.at(316, 644)}) m.click(p);
    CHECK(m.rightClick(m.at(450, 700)));
    REQUIRE(m.made().size() == 2);
    const Annotation area = m.made().back();
    CHECK(area.intent == QStringLiteral("PolygonDimension"));
    CHECK(area.vertices.size() == 6);
    CHECK(readingOf(area).find("sq ft") != std::string::npos);

    // A DISTANCE anchored by a click is not finished by a right click: it needs its second point.
    REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
    m.click(m.at(100, 400));
    REQUIRE(m.view->tools().measuring().inProgress());
    CHECK_FALSE(m.rightClick(m.at(450, 700)));
    CHECK(m.view->tools().measuring().inProgress());
    CHECK(m.made().size() == 2);
}

TEST_CASE("the pointer is hidden while a snap marker is shown and the cross is back when none is",
          "[measuretool][I-49]")
{
    // S6 guide I-49, the user's choice: a snap's marker lies within eight device pixels of the pointer, under its
    // cross, so the cursor is hidden while one is shown. WHAT THIS DOES NOT COVER (convention 19): what Windows draws
    // -- offscreen there is no pointer; the shape the viewport was GIVEN is read, and a person sees it in M-09.
    Measuring m;
    REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
    m.loadPaths(m.at(100, 500));
    const auto shape = [&m] { return m.view->viewport()->cursor().shape(); };

    m.hover(m.at(100, 500) + QPoint(3, 0));   // three pixels from a corner
    REQUIRE(m.view->tools().measuring().showsSnapMarker());
    CHECK(shape() == Qt::BlankCursor);
    m.hover(m.at(450, 700));   // nowhere near a path
    CHECK_FALSE(m.view->tools().measuring().showsSnapMarker());
    CHECK(shape() == Qt::CrossCursor);
    // Mid-gesture too: the first point anchored, the pointer on a midpoint.
    m.click(m.at(100, 400));
    m.hover(m.at(244, 400) + QPoint(0, 3));
    CHECK(shape() == Qt::BlankCursor);
    REQUIRE(m.key(Qt::Key_Escape));

    // THE CONTROL: with object snapping off nothing is caught, and the cross stays where the corner is.
    m.fixture.settings.set(core::keys::units::snapToObjects, false);
    QCoreApplication::processEvents();
    REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
    m.hover(m.at(100, 500) + QPoint(3, 0));
    CHECK_FALSE(m.view->tools().measuring().showsSnapMarker());
    CHECK(shape() == Qt::CrossCursor);
}

TEST_CASE("an area that crosses itself is refused by name and nothing is made", "[measuretool][A-87]")
{
    Measuring m;
    m.fixture.settings.set(core::keys::units::snapToObjects, false);
    REQUIRE(m.key(Qt::Key_A, kCtrlAlt));
    const std::size_t steps = m.historySize();
    for (const QPoint p : {m.at(100, 500), m.at(316, 644), m.at(316, 500)}) m.click(p);
    m.doubleClick(m.at(100, 644));   // the fourth corner makes a figure eight
    CHECK(m.made().empty());
    CHECK(m.historySize() == steps);
    // Said in the status bar, in the person's words: the catalog's text for `error.measure.selfIntersecting`.
    const QString said = m.window->statusBar()->message()->text();
    CHECK(said.contains(ui::catalog::translateError(QStringLiteral("error.measure.selfIntersecting"), QString())));
}

TEST_CASE("a dimension callout's third click sets its line off the points it measures", "[measuretool][A-92][D6-14]")
{
    Measuring m;
    m.fixture.settings.set(core::keys::units::snapToObjects, false);
    m.arm(core::ActionId::AnnotDimension);
    REQUIRE(m.view->tools().armedAnnotation() == ui::AnnotationToolKind::Dimension);
    m.click(m.at(100, 500));
    m.click(m.at(316, 500));
    CHECK(m.made().empty());   // the line is not placed yet
    // 30 points BELOW a left-to-right line is the clockwise side: a positive /LL (ISO 32000-1 Table 175).
    const QPoint third = m.at(200, 470);
    m.hover(third);
    m.click(third);
    REQUIRE(m.made().size() == 1);
    const Annotation dimension = m.made().back();
    // BY HAND, from the points as placed (each on a whole pixel): the third point's offset along the line's
    // clockwise normal (d.y, -d.x).
    const core::PdfPoint p1 = m.pageOf(m.at(100, 500)), p2 = m.pageOf(m.at(316, 500)), p3 = m.pageOf(third);
    const double length = std::hypot(p2.x - p1.x, p2.y - p1.y);
    const double expected = ((p3.x - p1.x) * (p2.y - p1.y) - (p3.y - p1.y) * (p2.x - p1.x)) / length;
    CHECK(expected > 25.0);   // below the line: the clockwise side
    CHECK(std::abs(dimension.leaderLength - expected) < 1e-6);
    CHECK(dimension.leaderExtension == core::kDimensionLeaderExtension);
    CHECK(dimension.caption);
    // The reading is of the points measured, not of where the line was moved to.
    CHECK(std::abs(dimension.line[1] - m.pageOf(m.at(100, 500)).y) < 1e-6);
    CHECK(ui::toolOfAnnotation(dimension) == core::CommentingTool::Dimension);
}

// =============================================================================================
// A-89's tool half -- snapping to the drawing
// =============================================================================================

TEST_CASE("a measured point three device pixels from a corner a midpoint or a crossing lands exactly on it",
          "[measuretool][A-89][D6-14]")
{
    for (const double zoom : {1.0, 4.0}) {
        INFO("zoom " << zoom);
        Measuring m;
        m.zoom(zoom);
        m.reveal(100, 500, 316, 644);
        REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
        // Offscreen draws at a device-pixel ratio of 1: three logical pixels are three device pixels.
        REQUIRE(m.view->devicePixelRatioF() == 1.0);
        m.loadPaths(m.at(100, 500));
        // From three pixels right of the corner (100, 500) to three pixels below-left of the midpoint (208, 644).
        m.drag(m.at(100, 500) + QPoint(3, 0), m.at(208, 644) + QPoint(-2, 2));
        REQUIRE(m.made().size() == 1);
        const Annotation d = m.made().back();
        CHECK(d.line == std::vector<double>{100, 500, 208, 644});
        // And the crossing of the diagonals, whose middle is the rectangle's centre (208, 572), to a corner.
        REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
        m.drag(m.at(208, 572) + QPoint(0, 3), m.at(316, 500) + QPoint(-3, 0));
        REQUIRE(m.made().size() == 2);
        CHECK(m.made().back().line == std::vector<double>{208, 572, 316, 500});
        // The tool SAID which it would take before the press: hover three pixels from the midpoint (316, 572).
        REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
        m.hover(m.at(316, 572) + QPoint(3, 0));
        const std::optional<core::SnapResult>& hovered = m.view->tools().measuring().hover();
        REQUIRE(hovered);
        REQUIRE(hovered->pointKind);
        CHECK(*hovered->pointKind == core::SnapPointKind::Midpoint);
        CHECK(hovered->point.x == 316.0);
        CHECK(hovered->point.y == 572.0);
    }
}

TEST_CASE("with object snapping off a measured point lands where the pointer is", "[measuretool][A-89][D6-14]")
{
    Measuring m;
    m.fixture.settings.set(core::keys::units::snapToObjects, false);
    REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
    const QPoint from = m.at(100, 500) + QPoint(3, 0), to = m.at(208, 644) + QPoint(-2, 2);
    m.drag(from, to);
    REQUIRE(m.made().size() == 1);
    const Annotation d = m.made().back();
    CHECK(d.line[0] == m.pageOf(from).x);
    CHECK(d.line[0] != 100.0);
    // And no request for the page's paths was made: snapping off asks the engine for nothing.
    CHECK(m.fixture.backend.geometryCalls == 0);
}

TEST_CASE("a page of more segments than the bound gives no object snaps and says so once", "[measuretool][A-91][D6-14]")
{
    std::vector<core::PathSegment> hatch;
    for (std::size_t i = 0; i <= core::PageGeometry::kMaxSegments; ++i) {
        const double y = 300.0 + static_cast<double>(i % 400);
        hatch.push_back(core::PathSegment{core::PdfPoint{100, y}, core::PdfPoint{400, y}, false});
    }
    Measuring m(std::move(hatch));
    int said = 0;
    const QMetaObject::Connection counted =
        QObject::connect(m.document->snapTargets(), &ui::SnapTargets::tooManySegments, [&said] { ++said; });
    REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
    for (int i = 0; i < 5; ++i) {
        m.hover(m.at(150 + i * 10, 350));
        m.fixture.pumpTiles();
    }
    CHECK(said == 1);
    QObject::disconnect(counted);
    CHECK(m.fixture.backend.geometryCalls == 1);   // read once, kept
    // No object snap: a point near a hatch line lands where the pointer is.
    const std::optional<core::SnapResult>& hovered = m.view->tools().measuring().hover();
    REQUIRE(hovered);
    CHECK_FALSE(hovered->pointKind.has_value());
    CHECK(m.window->statusBar()->message()->text().contains(QString::number(core::PageGeometry::kMaxSegments + 1)));
}

// =============================================================================================
// A-90 -- calibration; the scale dialog
// =============================================================================================

TEST_CASE("calibration sets the scale from a length measured on the page and makes nothing", "[measuretool][A-90][D6-14]")
{
    Measuring m;
    m.zoom(1.0);
    // The dialog answered as a person would: the length measured IS 12 ft.
    ui::MainWindow::setMeasurementScaleAnswerForTesting([](ui::MeasurementScaleDialog& dialog) {
        REQUIRE(dialog.isCalibrating());
        dialog.realUnit()->setCurrentIndex(dialog.realUnit()->findData(static_cast<int>(core::MeasureUnit::Feet)));
        dialog.realLength()->setValue(12.0);
        return true;
    });
    m.arm(core::ActionId::AnnotMeasureCalibrate);
    REQUIRE(m.view->tools().measuring().calibrating());
    m.loadPaths(m.at(100, 500));
    // The rectangle's long side, 216 points, snapped end to end.
    m.drag(m.at(100, 500) + QPoint(2, 2), m.at(316, 500) + QPoint(-2, 2));
    CHECK(m.made().empty());   // a calibration is not a measurement
    CHECK(std::abs(m.fixture.settings.get(core::keys::measuring::scaleRatio) - 48.0) < 1e-9);   // 12 x 864 / 216
    CHECK(m.fixture.settings.get(core::keys::measuring::displayUnit) == core::MeasureUnit::Feet);
    CHECK(m.window->statusBar()->message()->text().contains(QStringLiteral("1 in = 4 ft")));
    // THE NEXT DISTANCE READS AT IT: the 288-point line is 16 ft, the diagonal 14.42 ft (hand figures, tst_measure).
    REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
    CHECK_FALSE(m.view->tools().measuring().calibrating());
    m.drag(m.at(100, 400) + QPoint(2, 0), m.at(388, 400) + QPoint(-2, 0));
    REQUIRE(m.made().size() == 1);
    CHECK(readingOf(m.made().back()) == "16.00 ft");
    REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
    m.drag(m.at(100, 644) + QPoint(2, -2), m.at(316, 500) + QPoint(-2, 2));
    REQUIRE(m.made().size() == 2);
    CHECK(readingOf(m.made().back()) == "14.42 ft");
}

TEST_CASE("the scale dialog states a scale from two lengths and writes the three keys", "[measuretool][D6-14]")
{
    Measuring m;
    core::MeasureScale current;
    ui::MeasurementScaleDialog dialog(current);
    // 1 in on the page stands for 4 ft: a ratio of 48, said in words as `/R` will write it.
    dialog.paperUnit()->setCurrentIndex(dialog.paperUnit()->findData(static_cast<int>(core::MeasureUnit::Inches)));
    dialog.paperLength()->setValue(1.0);
    dialog.realUnit()->setCurrentIndex(dialog.realUnit()->findData(static_cast<int>(core::MeasureUnit::Feet)));
    dialog.realLength()->setValue(4.0);
    dialog.precision()->setValue(3);
    REQUIRE(dialog.chosen());
    CHECK(std::abs(dialog.chosen()->ratio - 48.0) < 1e-12);
    CHECK(dialog.chosen()->precision == 3);
    CHECK(dialog.preview()->text().contains(QStringLiteral("1 in = 4 ft")));
    // A length of zero states no scale, and OK is not offered.
    dialog.realLength()->setValue(0.0);
    CHECK_FALSE(dialog.chosen());
    CHECK_FALSE(dialog.buttons()->button(QDialogButtonBox::Ok)->isEnabled());

    // `annot.measurementScale` through the registry, answered: the three keys written.
    ui::MainWindow::setMeasurementScaleAnswerForTesting([](ui::MeasurementScaleDialog& asked) {
        asked.paperUnit()->setCurrentIndex(asked.paperUnit()->findData(static_cast<int>(core::MeasureUnit::Centimetres)));
        asked.paperLength()->setValue(1.0);
        asked.realUnit()->setCurrentIndex(asked.realUnit()->findData(static_cast<int>(core::MeasureUnit::Metres)));
        asked.realLength()->setValue(1.0);
        asked.precision()->setValue(1);
        return true;
    });
    REQUIRE(m.fixture.actions.dispatch(core::ActionId::AnnotMeasurementScale, core::KeyContext::Viewer));
    CHECK(std::abs(m.fixture.settings.get(core::keys::measuring::scaleRatio) - 100.0) < 1e-9);   // 1 cm : 1 m
    CHECK(m.fixture.settings.get(core::keys::measuring::displayUnit) == core::MeasureUnit::Metres);
    CHECK(m.fixture.settings.get(core::keys::measuring::precision) == 1);
    // Cancelled: nothing written.
    ui::MainWindow::setMeasurementScaleAnswerForTesting([](ui::MeasurementScaleDialog&) { return false; });
    REQUIRE(m.fixture.actions.dispatch(core::ActionId::AnnotMeasurementScale, core::KeyContext::Viewer));
    CHECK(m.fixture.settings.get(core::keys::measuring::precision) == 1);
}

TEST_CASE("the scale command is disabled where there is nobody to ask", "[measuretool][D6-14]")
{
    Measuring m;
    // Offscreen and no answer installed: convention 16 -- disabled, never a dialog shown to nobody.
    CHECK_FALSE(m.fixture.actions.isEnabled(core::ActionId::AnnotMeasurementScale));
    ui::MainWindow::setMeasurementScaleAnswerForTesting([](ui::MeasurementScaleDialog&) { return false; });
    CHECK(m.fixture.actions.isEnabled(core::ActionId::AnnotMeasurementScale));
}

// =============================================================================================
// A-92 -- the comment and the history
// =============================================================================================

TEST_CASE("the reading is in the comment exactly when writeToComment is on", "[measuretool][A-92][D6-14]")
{
    Measuring m;
    m.fixture.settings.set(core::keys::units::snapToObjects, false);
    m.fixture.settings.set(core::keys::measuring::writeToComment, false);
    REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
    m.drag(m.at(100, 500), m.at(316, 500));
    REQUIRE(m.made().size() == 1);
    CHECK(m.made().back().contents.isEmpty());
    CHECK_FALSE(readingOf(m.made().back()).empty());   // still measured, and drawn
    m.fixture.settings.set(core::keys::measuring::writeToComment, true);
    REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
    m.drag(m.at(100, 400), m.at(388, 400));
    REQUIRE(m.made().size() == 2);
    CHECK(m.made().back().contents.toStdString() == readingOf(m.made().back()));
}

TEST_CASE("the properties bar lists this session's readings newest first", "[measuretool][D6-14]")
{
    Measuring m;
    m.fixture.settings.set(core::keys::units::snapToObjects, false);
    m.setScale(48.0, core::MeasureUnit::Feet);
    REQUIRE(m.fixture.actions.dispatch(core::ActionId::ViewTogglePropertiesBar, core::KeyContext::Viewer));
    QComboBox* history = m.document->propertiesBar()->historyBox();
    REQUIRE(history != nullptr);
    CHECK_FALSE(history->isEnabled());   // nothing measured yet
    for (const auto& [from, to] : {std::pair{m.at(100, 500), m.at(316, 500)}, std::pair{m.at(100, 400), m.at(388, 400)}}) {
        REQUIRE(m.key(Qt::Key_D, kCtrlAlt));
        m.drag(from, to);
    }
    REQUIRE(m.made().size() == 2);
    REQUIRE(m.document->measurementHistory().size() == 2);
    CHECK(m.document->measurementHistory().front().toStdString() == readingOf(m.made().back()));
    CHECK(history->isEnabled());
    CHECK(history->count() == 2);
    CHECK(history->itemText(0).toStdString() == readingOf(m.made().back()));
    CHECK(m.window->statusBar()->message()->text().contains(m.document->measurementHistory().front()));
}
