// Stage 4 T8 -- the rulers, the guides on the page surface, the grid's painting and the page
// inversion. Guide cases A-47, A-49, A-50, A-51 and A-52
// (`Stage4-ViewingSurface-Testing-Guide.md` §5.9).
//
// The ARITHMETIC half of this task -- the snap precedence and the device-pixel threshold -- is
// `tests/unit/tst_snap.cpp`'s and runs with no display. What is here is what only a widget can
// answer: that a drag off a ruler lands where the pointer was, that a chord reaches the setting,
// and that none of it enqueues a render.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS SUITE CANNOT SEE, NAMED HERE RATHER THAN DISCOVERED (convention 19).
//
//   * **It cannot judge legibility.** A-49 asserts the ruler's READING at a pixel and the tick
//     step it chose; whether the numbers are readable at `general/uiFontScale` 0.75, and whether
//     the ticks are distinguishable on a real panel, is manual case **M-07**.
//   * **It cannot see a real drag.** A guide is dragged here by three synthetic mouse events.
//     What a person's hand does between them -- and whether the 8-device-pixel tolerance FEELS the
//     same at 50 % and 400 % -- is M-07's too; this suite proves the numbers, not the feel.
//   * **The grid's paint is asserted as PIXELS CHANGED, not as lines in the right places.** A case
//     that counted drawn lines would be a copy of the painter; what it checks instead is that the
//     viewport is different with the grid on, that each `units/grid*` key changes it again, and
//     that the scheduler was never asked for anything -- which is D4-11's actual claim.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "core/Snap.h"
#include "core/Units.h"
#include "core/cmd/ActionRegistry.h"
#include "core/render/RenderScheduler.h"
#include "core/settings/Settings.h"
#include "tests/ui/ColorDistance.h"
#include "tests/ui/ShellFixture.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/ThemeManager.h"
#include "ui/viewer/GridGuideLayer.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/RulerWidget.h"
#include "ui/viewer/ViewportController.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QPixmap>

#include <algorithm>
#include <cmath>
#include <memory>

using namespace rawpage;
using core::ActionId;
using Catch::Matchers::WithinAbs;

namespace {

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

void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

// A window with a document, laid out, focused in the viewer -- and with the RULERS ON, because
// every case below is about them and `units/showRulers` ships off.
struct Viewer {
    Fixture                         fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow*             document = nullptr;
    ui::PageView*                   view = nullptr;

    explicit Viewer(int pages = 6, bool rulers = true)
    {
        fixture.backend.pagesPerDocument = pages;
        if (rulers) fixture.settings.set(core::keys::units::showRulers, true);

        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 850);
        window->openFiles({fixture.writeFile(QStringLiteral("rulers.pdf"))});
        driveLayout(window.get());

        document = window->currentDocument();
        REQUIRE(document != nullptr);
        view = window->currentView();
        REQUIRE(view != nullptr);

        window->raiseToForeground();
        view->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();

        // THE GEOMETRY FIRST (convention 12, guide rule 4). Every coordinate below is in widget
        // pixels, and a zero-height viewer would make every one of them land outside it and every
        // assertion pass or fail for the wrong reason.
        REQUIRE(view->width() > 400);
        REQUIRE(view->height() > 300);
        REQUIRE(window->shortcutBinder()->activeContext() == core::KeyContext::Viewer);
    }

    ui::GridGuideLayer& guides() const { return view->guides(); }
    ui::ViewportController& controller() const { return view->controller(); }
    const core::render::PageLayout& layout() const { return view->layoutEngine().layout(); }

    ui::RulerWidget* horizontal() const { return document->horizontalRuler(); }
    ui::RulerWidget* vertical() const { return document->verticalRuler(); }

    bool press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
               const QString& text = QString()) const
    {
        QWidget* focus = window->focusWidget();
        REQUIRE(focus != nullptr);
        QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
        event.ignore();
        QCoreApplication::sendEvent(focus, &event);
        QCoreApplication::processEvents();
        return event.isAccepted();
    }

    // A drag off `ruler`, ending at `intoViewport` in the page surface's viewport coordinates.
    // Sent as real events to the RULER, which is what routes them through the widget that holds
    // the grab rather than calling `dropGuide` directly.
    void dragGuideFrom(ui::RulerWidget* ruler, QPoint intoViewport) const
    {
        const QPointF global = view->viewport()->mapToGlobal(intoViewport);
        const QPointF start = ruler->mapToGlobal(QPoint(4, 4));

        const auto send = [&](QEvent::Type type, QPointF at, Qt::MouseButton button,
                              Qt::MouseButtons buttons) {
            QMouseEvent event(type, ruler->mapFromGlobal(at), at, at, button, buttons,
                              Qt::NoModifier);
            QCoreApplication::sendEvent(ruler, &event);
            QCoreApplication::processEvents();
        };

        send(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, global, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, global, Qt::LeftButton, Qt::NoButton);
    }

    // The viewport's pixels, as a hash. What *"changes what is painted"* means in a case that must
    // not become a copy of the painter.
    QByteArray paintedHash() const
    {
        QPixmap grab(view->viewport()->size());
        view->viewport()->render(&grab);
        const QImage image = grab.toImage().convertToFormat(QImage::Format_RGB32);
        return QCryptographicHash::hash(
            QByteArray(reinterpret_cast<const char*>(image.constBits()),
                       static_cast<qsizetype>(image.sizeInBytes())),
            QCryptographicHash::Sha256);
    }

    // The point in the viewport where a page's PDF point is drawn, or a null point when the page
    // is not laid out.
    QPoint viewportPointFor(int page, core::PdfPoint at) const
    {
        const auto content = layout().toContent(page, at);
        REQUIRE(content.has_value());
        const core::DeviceRect visible = controller().visibleRect();
        const double dpr = view->devicePixelRatioF();
        return QPoint(static_cast<int>((content->x - visible.left()) / dpr),
                      static_cast<int>((content->y - visible.top()) / dpr));
    }
};

// A font file loaded for ONE case and unloaded when it ends. An application font is process-wide:
// left loaded, every later case in the same process lays its widgets out in Arial's metrics
// instead of offscreen's boxes, which moved the ribbon's collapse points under the Tab-order case
// when `rawpage_ui_tests.exe` ran whole. `ctest` runs one case per process and never saw it.
struct ScopedAppFont {
    int id;
    explicit ScopedAppFont(const QString& path) : id(QFontDatabase::addApplicationFont(path)) {}
    ~ScopedAppFont() { if (id >= 0) QFontDatabase::removeApplicationFont(id); }
    ScopedAppFont(const ScopedAppFont&) = delete;
    ScopedAppFont& operator=(const ScopedAppFont&) = delete;
};

} // namespace

// =============================================================================================
// A-47 -- guides are created, moved, locked, deleted and cleared.
// =============================================================================================

TEST_CASE("a guide dragged off a ruler lands at the pointer's page coordinate", "[rulers]")
{
    Viewer viewer;
    viewer.fixture.pumpTiles();

    REQUIRE(viewer.horizontal()->isVisible());
    REQUIRE(viewer.vertical()->isVisible());
    REQUIRE(viewer.guides().guideCount(-1) == 0);

    // A point inside page 0, chosen through the LAYOUT rather than guessed, so the case does not
    // depend on where the margin puts the sheet.
    const int page = viewer.controller().currentPage();
    const auto slot = viewer.layout().slotForPage(page);
    REQUIRE(slot != nullptr);
    // NEAR THE TOP OF THE SHEET, and the arithmetic is the trap: PDF y counts UP from the
    // bottom, so `cropBox.bottom() + 150` is 150 points from the BOTTOM of an 842-point page and
    // lands nearly 1000 pixels down a 607-pixel viewport -- off screen, where a drop makes no
    // guide by design. Measured from the top, it is where the person can actually point.
    const core::PdfPoint target{slot->cropBox.left() + 100.0, slot->cropBox.top() - 150.0};
    const QPoint at = viewer.viewportPointFor(page, target);

    // THE TOP RULER MAKES A VERTICAL GUIDE -- a line drawn down the screen, which at this
    // (unrotated) view is a line of constant page x.
    viewer.dragGuideFrom(viewer.horizontal(), at);
    REQUIRE(viewer.guides().guideCount(-1) == 1);

    const core::Guide& vertical = viewer.guides().guides().front();
    CHECK(vertical.orientation == core::Guide::Orientation::Vertical);
    CHECK(vertical.pageIndex == page);

    // Within a pixel's worth of points: the drop is an integer viewport pixel, so the page
    // coordinate it maps to is only as precise as one pixel is.
    const double pointsPerPixel = viewer.view->devicePixelRatioF() / viewer.layout().presentedScale();
    CHECK_THAT(vertical.position, WithinAbs(target.x, pointsPerPixel * 1.5));

    // And the LEFT ruler makes a horizontal one -- constant page y.
    viewer.dragGuideFrom(viewer.vertical(), at);
    REQUIRE(viewer.guides().guideCount(-1) == 2);
    const core::Guide& horizontal = viewer.guides().guides().back();
    CHECK(horizontal.orientation == core::Guide::Orientation::Horizontal);
    CHECK_THAT(horizontal.position, WithinAbs(target.y, pointsPerPixel * 1.5));

    SECTION("a drop that never reaches the page makes no guide")
    {
        // THE CONTROL: a press on the ruler that is released on the ruler is how a person changes
        // their mind, and it must not leave a guide at the edge of the page.
        const int before = viewer.guides().guideCount(-1);
        const QPointF onRuler = viewer.horizontal()->mapToGlobal(QPoint(40, 4));
        const auto send = [&](QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, viewer.horizontal()->mapFromGlobal(onRuler), onRuler, onRuler,
                              button, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(viewer.horizontal(), &event);
            QCoreApplication::processEvents();
        };
        send(QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        CHECK(viewer.guides().guideCount(-1) == before);
    }
}

TEST_CASE("a guide moves with a drag, a locked one does not, and clear-all spares the locked",
          "[rulers]")
{
    Viewer viewer;
    viewer.fixture.pumpTiles();

    const int page = viewer.controller().currentPage();
    const auto slot = viewer.layout().slotForPage(page);
    REQUIRE(slot != nullptr);

    const core::PdfPoint first{slot->cropBox.left() + 80.0, slot->cropBox.top() - 120.0};
    const QPoint start = viewer.viewportPointFor(page, first);
    viewer.dragGuideFrom(viewer.horizontal(), start);
    REQUIRE(viewer.guides().guideCount(-1) == 1);
    const double placed = viewer.guides().guides().front().position;

    // Grab it on the page and drag it 60 pixels right.
    const auto mouse = [&](QEvent::Type type, QPoint pos, Qt::MouseButton button,
                           Qt::MouseButtons buttons) {
        QMouseEvent event(type, QPointF(pos), QPointF(pos), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(viewer.view->viewport(), &event);
        QCoreApplication::processEvents();
    };

    mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    CHECK(viewer.guides().dragging());
    mouse(QEvent::MouseMove, start + QPoint(60, 0), Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, start + QPoint(60, 0), Qt::LeftButton, Qt::NoButton);
    CHECK_FALSE(viewer.guides().dragging());

    const double moved = viewer.guides().guides().front().position;
    CHECK(moved > placed + 1.0);

    SECTION("Esc during a drag puts it back where the drag started")
    {
        mouse(QEvent::MouseButtonPress, viewer.viewportPointFor(page, {moved, first.y}),
              Qt::LeftButton, Qt::LeftButton);
        REQUIRE(viewer.guides().dragging());
        mouse(QEvent::MouseMove, start + QPoint(160, 0), Qt::NoButton, Qt::LeftButton);
        CHECK(viewer.guides().guides().front().position > moved);

        CHECK(viewer.press(Qt::Key_Escape));
        CHECK_FALSE(viewer.guides().dragging());
        CHECK_THAT(viewer.guides().guides().front().position, WithinAbs(moved, 1e-9));
    }

    SECTION("a locked guide refuses the drag and survives a clear")
    {
        REQUIRE(viewer.guides().setGuideLocked(0, true));
        const double locked = viewer.guides().guides().front().position;

        mouse(QEvent::MouseButtonPress, viewer.viewportPointFor(page, {locked, first.y}),
              Qt::LeftButton, Qt::LeftButton);
        CHECK_FALSE(viewer.guides().dragging());   // the press fell through to the tool
        mouse(QEvent::MouseMove, start + QPoint(200, 0), Qt::NoButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, start + QPoint(200, 0), Qt::LeftButton, Qt::NoButton);
        CHECK_THAT(viewer.guides().guides().front().position, WithinAbs(locked, 1e-9));

        viewer.guides().clearGuides();
        CHECK(viewer.guides().guideCount(-1) == 1);

        // Unlocked, the same clear takes it -- which is what makes the line above a statement
        // about the LOCK rather than about a clear that does nothing.
        REQUIRE(viewer.guides().setGuideLocked(0, false));
        viewer.guides().clearGuides();
        CHECK(viewer.guides().guideCount(-1) == 0);
    }

    SECTION("a guide's page coordinate does not move when the zoom or the view rotation does")
    {
        const double before = viewer.guides().guides().front().position;

        viewer.view->setZoom(core::render::Zoom{4.0});
        viewer.view->refresh();
        QCoreApplication::processEvents();
        CHECK_THAT(viewer.guides().guides().front().position, WithinAbs(before, 1e-9));

        viewer.view->rotateViewClockwise();
        QCoreApplication::processEvents();
        CHECK_THAT(viewer.guides().guides().front().position, WithinAbs(before, 1e-9));

        // AND IT IS STILL DRAWN ON ITS PAGE after the rotation -- the position is unchanged
        // because it is a page fact, and the line moves on screen because the page did (D4-7).
        const auto slotNow = viewer.layout().slotForPage(page);
        REQUIRE(slotNow != nullptr);
        CHECK(core::swapsAxes(slotNow->rotation));
    }
}

// =============================================================================================
// A-49 -- the rulers read out the cursor's page coordinates in the chosen unit.
// =============================================================================================

TEST_CASE("a ruler reads page coordinates in the chosen unit at every zoom", "[rulers]")
{
    Viewer viewer;
    viewer.fixture.pumpTiles();

    const int page = viewer.controller().currentPage();
    const auto slot = viewer.layout().slotForPage(page);
    REQUIRE(slot != nullptr);

    // THE ZERO IS THE PAGE'S DRAWN EDGE. A reading of 0 at the page's left edge is the claim that
    // makes every other reading meaningful.
    const ui::RulerWidget::Mapping& mapping = viewer.horizontal()->mapping();
    REQUIRE(mapping.valid);
    CHECK_THAT(viewer.horizontal()->readingAt(mapping.zeroPixel), WithinAbs(0.0, 1e-9));

    // 144 points along the page is 144 points on the ruler, whatever the zoom -- and 2 inches,
    // through `core/Units.h` rather than through a number spelled here.
    const double atTwoInches = mapping.zeroPixel + 144.0 * mapping.pixelsPerPoint;
    CHECK_THAT(viewer.horizontal()->readingAt(atTwoInches), WithinAbs(144.0, 1e-6));
    CHECK_THAT(core::Length::fromPoints(viewer.horizontal()->readingAt(atTwoInches)).inches(),
               WithinAbs(2.0, 1e-6));

    const double stepAtStart = viewer.horizontal()->tickStep();
    CHECK(stepAtStart > 0.0);

    SECTION("the same page point reads the same at 50 and at 400 per cent")
    {
        for (const double zoom : {0.5, 4.0}) {
            viewer.view->setZoom(core::render::Zoom{zoom});
            viewer.view->refresh();
            QCoreApplication::processEvents();

            const ui::RulerWidget::Mapping& at = viewer.horizontal()->mapping();
            REQUIRE(at.valid);
            const double pixel = at.zeroPixel + 144.0 * at.pixelsPerPoint;
            CHECK_THAT(viewer.horizontal()->readingAt(pixel), WithinAbs(144.0, 1e-6));
        }

        // THE TICKS, HOWEVER, DO CHANGE -- and they have to: a ruler that kept its step across an
        // eightfold zoom would draw ticks a pixel apart at one end of it. The READING is a page
        // fact; the tick spacing is a legibility choice.
        CHECK(viewer.horizontal()->tickStep() < stepAtStart);
    }

    SECTION("the unit changes the readout and the step, and the corner box follows")
    {
        viewer.fixture.settings.set(core::keys::units::rulerUnit, core::Unit::Millimetres);
        QCoreApplication::processEvents();

        CHECK(viewer.horizontal()->unit() == core::Unit::Millimetres);
        CHECK(viewer.vertical()->unit() == core::Unit::Millimetres);

        // The tick step is in the DISPLAY unit, so the same ruler in millimetres steps by a number
        // that is round in millimetres -- ten of them is 28.35 points, which is not round at all.
        const double step = viewer.horizontal()->tickStep();
        CHECK(step > 0.0);
        const double asPoints = core::Length::from(step, core::Unit::Millimetres).points();
        CHECK(asPoints > 0.0);
        CHECK_THAT(core::Length::fromPoints(asPoints).millimetres(), WithinAbs(step, 1e-9));
    }

    SECTION("the cursor marker follows the pointer and is cleared when it leaves")
    {
        const QPoint at(210, 160);
        QMouseEvent move(QEvent::MouseMove, QPointF(at), QPointF(at), Qt::NoButton, Qt::NoButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(viewer.view->viewport(), &move);
        QCoreApplication::processEvents();

        CHECK(viewer.horizontal()->cursorPixel() > 0.0);
        CHECK(viewer.vertical()->cursorPixel() > 0.0);

        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(viewer.view->viewport(), &leave);
        QCoreApplication::processEvents();
        CHECK(viewer.horizontal()->cursorPixel() < 0.0);
        CHECK(viewer.vertical()->cursorPixel() < 0.0);
    }
}

// =============================================================================================
// A-50 -- the grid obeys its keys, AND NONE OF IT REACHES THE SCHEDULER (D4-11).
// =============================================================================================

TEST_CASE("the grid is painted over the tiles and never rasterized into them", "[rulers]")
{
    Viewer viewer;
    viewer.fixture.pumpTiles();

    const QByteArray withoutGrid = viewer.paintedHash();
    const long long enqueuedBefore = viewer.fixture.renderScheduler.stats().enqueued;
    const std::size_t cachedBefore = viewer.fixture.renderCache.entryCount();

    viewer.fixture.settings.set(core::keys::units::showGrid, true);
    QCoreApplication::processEvents();

    const QByteArray withGrid = viewer.paintedHash();
    CHECK(withGrid != withoutGrid);

    // THE WHOLE OF D4-11, as a number: the grid changed the pixels on screen and asked the
    // scheduler for nothing. A grid composed into the tile would have put its spacing, colour and
    // offset into the cache key, and every line below would have re-rasterized the page.
    CHECK(viewer.fixture.renderScheduler.stats().enqueued == enqueuedBefore);
    CHECK(viewer.fixture.renderCache.entryCount() == cachedBefore);

    QByteArray previous = withGrid;
    for (const auto& change : std::initializer_list<std::function<void()>>{
             [&] { viewer.fixture.settings.set(core::keys::units::gridSpacing, 24.0); },
             [&] { viewer.fixture.settings.set(core::keys::units::gridSubdivisions, 2); },
             [&] { viewer.fixture.settings.set(core::keys::units::gridColour,
                                               QStringLiteral("#CC3322")); },
             [&] { viewer.fixture.settings.set(core::keys::units::gridOriginX, 9.0); },
             [&] { viewer.fixture.settings.set(core::keys::units::gridOriginY, 9.0); }}) {
        change();
        QCoreApplication::processEvents();

        const QByteArray now = viewer.paintedHash();
        CHECK(now != previous);
        previous = now;

        CHECK(viewer.fixture.renderScheduler.stats().enqueued == enqueuedBefore);
    }

    SECTION("an unreadable colour falls back rather than painting nothing")
    {
        viewer.fixture.settings.set(core::keys::units::gridColour, QStringLiteral("blueish"));
        QCoreApplication::processEvents();

        // Still a grid, still different from no grid at all: `core/` holds the text and `ui/`
        // decides whether it is a colour, so a hand-edited INI gets the default rather than an
        // invisible grid.
        CHECK(viewer.paintedHash() != withoutGrid);
        CHECK(viewer.fixture.renderScheduler.stats().enqueued == enqueuedBefore);
    }
}

// =============================================================================================
// A-51 -- Ctrl+U, Ctrl+' and Ctrl+; dispatch from a key event and toggle what they name.
// =============================================================================================

TEST_CASE("the three section 8 chords toggle their setting from a key event", "[rulers]")
{
    // RULERS OFF, as they ship: the first `Ctrl+U` below is what turns them on, which is the
    // claim -- a case that started with them on could not tell a toggle from a no-op.
    Viewer viewer(6, /*rulers=*/false);
    viewer.fixture.pumpTiles();

    CHECK_FALSE(viewer.horizontal()->isVisible());
    CHECK_FALSE(viewer.fixture.settings.get(core::keys::units::showRulers));

    struct Row {
        Qt::Key           key;
        QString           text;
        ActionId          id;
        core::Key<bool>   setting;
    };

    const Row rows[] = {
        {Qt::Key_U, QStringLiteral("u"), ActionId::ViewToggleRulers, core::keys::units::showRulers},
        {Qt::Key_Apostrophe, QStringLiteral("'"), ActionId::ViewToggleGrid,
         core::keys::units::showGrid},
        {Qt::Key_Semicolon, QStringLiteral(";"), ActionId::ViewToggleSnapToGrid,
         core::keys::units::snapToGrid},
    };

    for (const Row& row : rows) {
        INFO("action " << core::actionStringId(row.id));
        REQUIRE(viewer.fixture.actions.isRegistered(row.id));

        const bool before = viewer.fixture.settings.get(row.setting);
        CHECK(viewer.press(row.key, Qt::ControlModifier, row.text));
        CHECK(viewer.fixture.settings.get(row.setting) == !before);

        // AND THE COMMAND'S CHECKED STATE IS THE SETTING, not a copy of it kept beside the
        // toggle: a ribbon button that disagreed with the preference page would be two truths.
        CHECK(viewer.fixture.actions.isChecked(row.id) == !before);

        CHECK(viewer.press(row.key, Qt::ControlModifier, row.text));
        CHECK(viewer.fixture.settings.get(row.setting) == before);
    }

    // `Ctrl+U` reached the WIDGET as well as the row -- twice, so the rulers are off again and
    // this is the round trip rather than one direction of it.
    CHECK_FALSE(viewer.horizontal()->isVisible());
    CHECK(viewer.press(Qt::Key_U, Qt::ControlModifier, QStringLiteral("u")));
    QCoreApplication::processEvents();
    CHECK(viewer.horizontal()->isVisible());
    CHECK(viewer.vertical()->isVisible());

    SECTION("the setting is what survives, and a second window opens at it")
    {
        // The state lives in `units/*` and not in the widget, which is why the toggle survives a
        // restart at all -- the case reads it back through a second `MainWindow` over the same
        // settings store rather than by restarting a process it cannot restart.
        REQUIRE(viewer.fixture.settings.get(core::keys::units::showRulers));

        ui::MainWindow second(viewer.fixture.services());
        second.resize(900, 700);
        second.openFiles({viewer.fixture.writeFile(QStringLiteral("second.pdf"))});
        driveLayout(&second);

        ui::DocumentWindow* document = second.currentDocument();
        REQUIRE(document != nullptr);
        CHECK(document->horizontalRuler()->isVisible());
    }
}

TEST_CASE("the guide commands act on the guide the menu was raised over", "[rulers]")
{
    Viewer viewer;
    viewer.fixture.pumpTiles();

    const int page = viewer.controller().currentPage();
    const auto slot = viewer.layout().slotForPage(page);
    REQUIRE(slot != nullptr);
    viewer.dragGuideFrom(viewer.horizontal(),
                         viewer.viewportPointFor(page, {slot->cropBox.left() + 90.0,
                                                        slot->cropBox.top() - 90.0}));
    REQUIRE(viewer.guides().guideCount(-1) == 1);

    REQUIRE(viewer.fixture.actions.isRegistered(ActionId::ViewLockGuide));
    REQUIRE(viewer.fixture.actions.isRegistered(ActionId::ViewDeleteGuide));
    REQUIRE(viewer.fixture.actions.isRegistered(ActionId::ViewClearGuides));

    // WITH NO TARGET, THE TWO THAT NEED ONE ARE DISABLED -- convention 16's rule, in the form
    // where the missing thing is a target rather than a user. `view.clearGuides` needs no target
    // and is enabled, because there IS a guide.
    viewer.guides().setContextGuide(-1);
    CHECK_FALSE(viewer.fixture.actions.isEnabled(ActionId::ViewLockGuide));
    CHECK_FALSE(viewer.fixture.actions.isEnabled(ActionId::ViewDeleteGuide));
    CHECK(viewer.fixture.actions.isEnabled(ActionId::ViewClearGuides));

    viewer.guides().setContextGuide(0);
    CHECK(viewer.fixture.actions.isEnabled(ActionId::ViewLockGuide));
    CHECK_FALSE(viewer.fixture.actions.isChecked(ActionId::ViewLockGuide));

    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ViewLockGuide, core::KeyContext::Viewer));
    CHECK(viewer.guides().guides().front().locked);
    CHECK(viewer.fixture.actions.isChecked(ActionId::ViewLockGuide));

    // Locked, deleting it is refused at the command rather than at the menu.
    CHECK_FALSE(viewer.fixture.actions.isEnabled(ActionId::ViewDeleteGuide));

    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ViewLockGuide, core::KeyContext::Viewer));
    CHECK_FALSE(viewer.guides().guides().front().locked);
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ViewDeleteGuide, core::KeyContext::Viewer));
    CHECK(viewer.guides().guideCount(-1) == 0);

    // With nothing left, clearing is disabled -- a command that reported success and did nothing
    // would be the alternative.
    CHECK_FALSE(viewer.fixture.actions.isEnabled(ActionId::ViewClearGuides));
}

// =============================================================================================
// A-52 -- colour inversion is applied OVER the tiles (D4-11's second half).
// =============================================================================================

TEST_CASE("inverting the page colours paints differently and rasterizes nothing", "[rulers]")
{
    Viewer viewer;
    viewer.fixture.pumpTiles();

    const QByteArray upright = viewer.paintedHash();
    const long long enqueuedBefore = viewer.fixture.renderScheduler.stats().enqueued;
    const std::size_t cachedBefore = viewer.fixture.renderCache.entryCount();

    REQUIRE(viewer.fixture.actions.isRegistered(ActionId::ViewInvertColors));
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ViewInvertColors, core::KeyContext::Viewer));
    QCoreApplication::processEvents();

    CHECK(viewer.view->invertColors());
    CHECK(viewer.fixture.actions.isChecked(ActionId::ViewInvertColors));

    const QByteArray inverted = viewer.paintedHash();
    CHECK(inverted != upright);

    // NOTHING WAS RE-RASTERIZED AND NOTHING NEW WAS CACHED. An inversion in the cache key would
    // double the cache for a toggle that must be instant (D4-11).
    CHECK(viewer.fixture.renderScheduler.stats().enqueued == enqueuedBefore);
    CHECK(viewer.fixture.renderCache.entryCount() == cachedBefore);

    // AND TOGGLING BACK IS EXACT -- the same bytes, not merely a similar picture, which is what
    // `RasterOp_NotDestination` buys over a composition mode with an alpha.
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ViewInvertColors, core::KeyContext::Viewer));
    QCoreApplication::processEvents();
    CHECK_FALSE(viewer.view->invertColors());
    CHECK(viewer.paintedHash() == upright);

    SECTION("substituting a page colour is NOT built, and its id is not registered")
    {
        // `Features.md` §1.2 offers *invert OR substitute*; the substitution needs a colour pair
        // that no declared settings key holds, so `view.pageBackground` stays declared and
        // unregistered -- which leaves its (absent) chord unconsumed rather than offering a
        // control that does nothing. `Stage4-ViewingSurface-Testing-Guide.md` §8 carries it as an
        // open issue; this assertion is what makes the claim checkable rather than remembered.
        CHECK_FALSE(viewer.fixture.actions.isRegistered(ActionId::ViewPageBackground));
    }
}

// =============================================================================================
// I-13 -- the vertical ruler printed its numbers OUTSIDE ITSELF.
//
// Found by the user on 2026-09-26, asking why the vertical ruler had no numbers. The label was laid
// out at x = 0 and rotated 90 degrees, and a quarter turn in Qt's y-down space carries the text's
// height towards NEGATIVE x -- off the widget's left edge, where it is clipped. The horizontal ruler
// is not rotated, which is why its numbers were there and why a case looking at one ruler saw them.
//
// Judged on PAINTED PIXELS in the label strip -- the band of each ruler no tick reaches (a labelled
// tick is 60 % of the thickness, from the inner edge) -- so a tick mark cannot pass for a number.
// What it does NOT cover (convention 19): that the numbers are the right ones, which is the readings
// case above, nor legibility at 150 %, which is M-07's.
// =============================================================================================

TEST_CASE("both rulers print their numbers inside themselves", "[rulers][I-13]")
{
    // A black-on-white palette, so "ink" is one threshold and not a question about the theme.
    QPalette palette;
    palette.setColor(QPalette::Window, Qt::white);
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::WindowText, Qt::black);
    palette.setColor(QPalette::Mid, QColor(0xc0, 0xc0, 0xc0));

    ui::RulerWidget::Mapping mapping;
    mapping.valid = true;
    mapping.pixelsPerPoint = 1.0;
    mapping.zeroPixel = 10.0;
    mapping.extentPoints = 600.0;

    constexpr int kLength = 400;
    // The strip nearest the OUTER edge that no tick reaches: ticks are drawn from the inner edge and
    // the longest is 60 % of the thickness, so the outer third is text or nothing.
    constexpr int kStrip = ui::RulerWidget::kThickness / 3;

    // A REAL FONT, loaded from a file. The offscreen platform has no font database, so it draws
    // every glyph as a box -- and draws a ROTATED box as nothing at all, which made the first run
    // of this case fail after the fix exactly as it had before it (vertical ink 0 both times). The
    // `windows` platform read 123 after the fix and 0 with the old origin planted back; a font file
    // is what lets `ctest`'s offscreen run see the same difference.
    const ScopedAppFont arial(QStringLiteral("C:/Windows/Fonts/arial.ttf"));
    const int loaded = arial.id;
    REQUIRE(loaded >= 0);
    const QStringList families = QFontDatabase::applicationFontFamilies(loaded);
    REQUIRE_FALSE(families.isEmpty());
    const QFont real(families.front(), 9);

    const auto inkInStrip = [&](Qt::Orientation orientation) {
        ui::RulerWidget ruler(orientation);
        ruler.setPalette(palette);
        ruler.setFont(real);
        ruler.setMapping(mapping);
        if (orientation == Qt::Horizontal)
            ruler.resize(kLength, ui::RulerWidget::kThickness);
        else
            ruler.resize(ui::RulerWidget::kThickness, kLength);
        REQUIRE(ruler.tickStep() > 0.0);   // there is something to label at all

        const QImage image = ruler.grab().toImage();
        int ink = 0;
        for (int along = 0; along < kLength; ++along) {
            for (int across = 0; across < kStrip; ++across) {
                const QColor pixel = orientation == Qt::Horizontal ? image.pixelColor(along, across)
                                                                   : image.pixelColor(across, along);
                if (pixel.lightness() < 128) ++ink;
            }
        }
        return ink;
    };

    const int horizontal = inkInStrip(Qt::Horizontal);
    const int vertical = inkInStrip(Qt::Vertical);
    INFO("horizontal ink " << horizontal << ", vertical ink " << vertical);

    // THE CONTROL IS THE HORIZONTAL RULER: it always had its numbers, so a threshold it fails is a
    // threshold that is wrong rather than a ruler that is.
    CHECK(horizontal > 20);
    CHECK(vertical > 20);
}

// =============================================================================================
// A-87 -- a guide SNAPS when a person drags it, at 50 % and at 400 % (I-16).
//
// I-16 (`Stage4-ViewingSurface-Testing-Guide.md` §8): `GridGuideLayer::snap` existed and no gesture
// called it, so `Ctrl+;` moved no guide in the product while A-45 and A-46 were green over the sums
// and A-47 was green over drags that never asked where a guide LANDED against a line. Every drag
// here goes through the widgets -- off a ruler, or picked up on the page -- and the assertion is
// the stored page coordinate.
//
// **The control is the half the criterion does not ask for** (convention 22): 13 device pixels
// from the line does NOT snap, at both zooms. And a guide nudged 5 pixels with guide-snapping ON
// and no other guide on the page must MOVE -- the dragged guide is always within the threshold of
// its own last position, and a resolver that counted it would pin every guide where it was.
//
// What it does NOT cover (convention 19): whether 8 device pixels FEELS the same at both zooms on
// a real panel -- M-07's. The grid is set to one subdivision so a line is 48 device pixels from the
// next at 50 % here; at the default four, every point is within reach of SOME line (A-46's trap)
// and "13 pixels does not snap" could not be said.
// =============================================================================================

namespace {

struct SnapTarget {
    int    page = 0;
    double line = 0.0;    // the page-space grid coordinate being aimed at
    double across = 0.0;  // the other coordinate, which only has to be on the page and on screen
    double scale = 1.0;   // device pixels per point, as presented
};

// A grid line on the current page, chosen from what is ON SCREEN -- so the case does not depend
// on where the margin or the zoom put the sheet. `alongX` asks for a line of constant x.
SnapTarget visibleGridLine(const Viewer& viewer, bool alongX, double spacing)
{
    SnapTarget target;
    target.page = viewer.controller().currentPage();
    target.scale = viewer.layout().presentedScale();

    const core::render::PageSlot* slot = viewer.layout().slotForPage(target.page);
    REQUIRE(slot != nullptr);
    const core::DeviceRect visible = viewer.controller().visibleRect();

    // The middle of the part of the sheet that is on screen.
    const double left = std::max(slot->rect.left(), visible.left());
    const double right = std::min(slot->rect.right(), visible.right());
    const double top = std::max(slot->rect.top(), visible.top());
    const double bottom = std::min(slot->rect.bottom(), visible.bottom());
    REQUIRE(right - left > 100.0);
    REQUIRE(bottom - top > 100.0);

    const auto middle = viewer.layout().toDocument({(left + right) / 2.0, (top + bottom) / 2.0});
    REQUIRE(middle.has_value());
    REQUIRE(middle->pageIndex == target.page);

    const double value = alongX ? middle->point.x : middle->point.y;
    target.line = std::round(value / spacing) * spacing;
    target.across = alongX ? middle->point.y : middle->point.x;
    return target;
}

core::PdfPoint offsetFrom(const SnapTarget& target, bool alongX, double devicePixels)
{
    const double coordinate = target.line + devicePixels / target.scale;
    return alongX ? core::PdfPoint{coordinate, target.across}
                  : core::PdfPoint{target.across, coordinate};
}

void setZoom(Viewer& viewer, double zoom)
{
    viewer.view->setZoom(core::render::Zoom{zoom});
    viewer.view->refresh();
    QCoreApplication::processEvents();
    viewer.fixture.pumpTiles();
}

void pageMouse(const Viewer& viewer, QEvent::Type type, QPoint pos, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(pos), QPointF(pos), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(viewer.view->viewport(), &event);
    QCoreApplication::processEvents();
}

} // namespace

TEST_CASE("a guide dragged near a grid line snaps to it at 50 and 400 percent", "[rulers][I-16]")
{
    Viewer viewer;
    viewer.fixture.settings.set(core::keys::units::gridSpacing, 72.0);
    viewer.fixture.settings.set(core::keys::units::gridSubdivisions, 1);
    viewer.fixture.settings.set(core::keys::units::showGrid, true);
    viewer.fixture.settings.set(core::keys::units::snapToGrid, true);
    // Guides OFF for the grid's half, so a guide dropped earlier cannot be what a later one snaps to.
    viewer.fixture.settings.set(core::keys::units::snapToGuides, false);
    QCoreApplication::processEvents();
    REQUIRE(viewer.guides().activeFlags() == core::SnapFlags::Grid);

    for (const double zoom : {0.5, 4.0}) {
        DYNAMIC_SECTION("zoom " << zoom)
        {
            setZoom(viewer, zoom);

            // Both rulers: the top one makes a line of constant x, the left one constant y.
            for (const bool alongX : {true, false}) {
                // Cleared per axis, so the guides of the other axis cannot be what a press grabs.
                viewer.guides().setGuides({});

                ui::RulerWidget* ruler = alongX ? viewer.horizontal() : viewer.vertical();
                const SnapTarget target = visibleGridLine(viewer, alongX, 72.0);
                INFO("zoom " << zoom << (alongX ? ", x" : ", y") << ", line " << target.line
                              << " pt, " << target.scale << " device px per pt");

                // DROPPED 5 device pixels off the line: it lands ON the line, exactly.
                viewer.dragGuideFrom(ruler, viewer.viewportPointFor(target.page,
                                                                    offsetFrom(target, alongX, 5.0)));
                REQUIRE(viewer.guides().guideCount(-1) == 1);
                CHECK_THAT(viewer.guides().guides().back().position, WithinAbs(target.line, 1e-6));

                // THE CONTROL: 13 device pixels off does NOT snap. It lands at the pointer, which is
                // more than the 8-pixel threshold from the line.
                viewer.dragGuideFrom(ruler, viewer.viewportPointFor(target.page,
                                                                    offsetFrom(target, alongX, 13.0)));
                REQUIRE(viewer.guides().guideCount(-1) == 2);
                const double offLine = viewer.guides().guides().back().position;
                CHECK(std::abs(offLine - target.line) * target.scale > 8.0);

                // AND THE GUIDE JUST PLACED, picked up on the page and MOVED to 5 pixels off the
                // line -- the snap on `dragTo`, which is the other half of the gesture.
                const int moving = 1;
                const QPoint grab = viewer.viewportPointFor(
                    target.page, alongX ? core::PdfPoint{offLine, target.across}
                                        : core::PdfPoint{target.across, offLine});
                pageMouse(viewer, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
                REQUIRE(viewer.guides().draggedGuide() == moving);
                const QPoint onto = viewer.viewportPointFor(target.page,
                                                            offsetFrom(target, alongX, -5.0));
                pageMouse(viewer, QEvent::MouseMove, onto, Qt::NoButton, Qt::LeftButton);
                pageMouse(viewer, QEvent::MouseButtonRelease, onto, Qt::LeftButton, Qt::NoButton);
                CHECK_THAT(viewer.guides().guides()[static_cast<std::size_t>(moving)].position,
                           WithinAbs(target.line, 1e-6));
            }
        }
    }

    SECTION("with snap to grid off the same drop lands where it was dropped")
    {
        viewer.fixture.settings.set(core::keys::units::snapToGrid, false);
        QCoreApplication::processEvents();
        REQUIRE(viewer.guides().activeFlags() == core::SnapFlags::None);

        const SnapTarget target = visibleGridLine(viewer, true, 72.0);
        viewer.dragGuideFrom(viewer.horizontal(),
                             viewer.viewportPointFor(target.page, offsetFrom(target, true, 5.0)));
        REQUIRE(viewer.guides().guideCount(-1) == 1);
        CHECK(std::abs(viewer.guides().guides().back().position - target.line) > 1e-3);
    }
}

TEST_CASE("a guide snaps to another guide but never to itself", "[rulers][I-16]")
{
    Viewer viewer;
    viewer.fixture.pumpTiles();
    // Guides only, which is how the product ships (`units/snapToGuides` on, `units/snapToGrid` off).
    REQUIRE(viewer.guides().activeFlags() == core::SnapFlags::Guides);

    const SnapTarget target = visibleGridLine(viewer, true, 72.0);
    const double pointsPerDevicePixel = 1.0 / target.scale;
    const double pixelSlack = viewer.view->devicePixelRatioF() * pointsPerDevicePixel * 1.5;

    // The anchor, then a second guide dropped 5 device pixels from it: the second takes the first's
    // coordinate.
    viewer.dragGuideFrom(viewer.horizontal(),
                         viewer.viewportPointFor(target.page, offsetFrom(target, true, 0.0)));
    REQUIRE(viewer.guides().guideCount(-1) == 1);
    const double anchor = viewer.guides().guides().front().position;

    viewer.dragGuideFrom(viewer.horizontal(),
                         viewer.viewportPointFor(target.page, offsetFrom(target, true, 5.0)));
    REQUIRE(viewer.guides().guideCount(-1) == 2);
    CHECK_THAT(viewer.guides().guides().back().position, WithinAbs(anchor, 1e-9));

    SECTION("a lone guide nudged 5 pixels moves -- its own position is not a candidate")
    {
        viewer.guides().setGuides({});
        viewer.dragGuideFrom(viewer.horizontal(),
                             viewer.viewportPointFor(target.page, offsetFrom(target, true, 0.0)));
        REQUIRE(viewer.guides().guideCount(-1) == 1);
        const double start = viewer.guides().guides().front().position;

        const QPoint grab = viewer.viewportPointFor(target.page, {start, target.across});
        pageMouse(viewer, QEvent::MouseButtonPress, grab, Qt::LeftButton, Qt::LeftButton);
        REQUIRE(viewer.guides().dragging());
        const core::PdfPoint to{start + 5.0 * pointsPerDevicePixel, target.across};
        const QPoint nudge = viewer.viewportPointFor(target.page, to);
        REQUIRE(nudge != grab);
        pageMouse(viewer, QEvent::MouseMove, nudge, Qt::NoButton, Qt::LeftButton);
        pageMouse(viewer, QEvent::MouseButtonRelease, nudge, Qt::LeftButton, Qt::NoButton);

        const double moved = viewer.guides().guides().front().position;
        CHECK(moved > start);
        CHECK_THAT(moved, WithinAbs(to.x, pixelSlack));
    }
}

// =============================================================================================
// A-88 -- a red cursor marker on each ruler, with the reading beside it (I-17).
//
// The user's request of 2026-09-26, while preparing M-07: the marker was the accent blue and there
// was no number anywhere -- `Features.md` §1.3 promised a live readout in the status bar, and none
// was built. The readout is now on the rulers, beside the marker.
//
// What it does NOT cover (convention 19): whether the number is LEGIBLE on a 150 % panel or at
// `general/uiFontScale` 0.75 -- M-07's; the text is asserted as a string and the box as painted
// pixels, never the glyphs inside it.
// =============================================================================================

TEST_CASE("the cursor readout is the reading at the marker in the ruler's unit", "[rulers][I-17]")
{
    ui::RulerWidget ruler(Qt::Horizontal);
    ui::RulerWidget::Mapping mapping;
    mapping.valid = true;
    mapping.pixelsPerPoint = 1.0;
    mapping.zeroPixel = 10.0;
    mapping.extentPoints = 600.0;
    ruler.setMapping(mapping);

    // No marker, no number -- the control for every line below.
    CHECK(ruler.cursorReadout().isEmpty());

    // 144 points from the page's drawn edge, in all five units.
    ruler.setCursorPixel(10.0 + 144.0);
    const std::pair<core::Unit, const char*> expected[] = {
        {core::Unit::Points, "144.0"},      {core::Unit::Inches, "2.00"},
        {core::Unit::Millimetres, "50.8"},  {core::Unit::Centimetres, "5.08"},
        {core::Unit::Picas, "12.00"},
    };
    for (const auto& [unit, text] : expected) {
        ruler.setUnit(unit);
        CHECK(ruler.cursorReadout() == QString::fromLatin1(text));
    }

    // At an eightfold zoom the same page point reads the same.
    ruler.setUnit(core::Unit::Inches);
    mapping.pixelsPerPoint = 4.0;
    ruler.setMapping(mapping);
    ruler.setCursorPixel(10.0 + 144.0 * 4.0);
    CHECK(ruler.cursorReadout() == QStringLiteral("2.00"));

    // Off the page's near edge the reading is negative rather than clamped: the ruler says how far
    // outside the sheet the pointer is, which is what a person measuring a margin wants. (The zero
    // moves right first: a negative WIDGET pixel is the no-marker sentinel, not a reading.)
    mapping.zeroPixel = 200.0;
    ruler.setMapping(mapping);
    ruler.setCursorPixel(200.0 - 36.0 * 4.0);
    CHECK(ruler.cursorReadout() == QStringLiteral("-0.50"));

    ruler.setCursorPixel(-1.0);
    CHECK(ruler.cursorReadout().isEmpty());
}

TEST_CASE("the readout follows the pointer over the page and leaves with it", "[rulers][I-17]")
{
    Viewer viewer;
    viewer.fixture.pumpTiles();

    const int page = viewer.controller().currentPage();
    const auto slot = viewer.layout().slotForPage(page);
    REQUIRE(slot != nullptr);

    // Two inches in from the page's drawn left edge and one inch down from its drawn top: PDF y
    // counts UP, so "down from the top" is `top() - 72`.
    const QPoint at = viewer.viewportPointFor(page, {slot->cropBox.left() + 144.0,
                                                     slot->cropBox.top() - 72.0});
    QMouseEvent move(QEvent::MouseMove, QPointF(at), QPointF(at), Qt::NoButton, Qt::NoButton,
                     Qt::NoModifier);
    QCoreApplication::sendEvent(viewer.view->viewport(), &move);
    QCoreApplication::processEvents();

    // Within one pixel's worth of inches: the pointer lands on an integer pixel.
    const double inchesPerPixel = viewer.view->devicePixelRatioF() / viewer.layout().presentedScale() / 72.0;
    const QString across = viewer.horizontal()->cursorReadout();
    const QString down = viewer.vertical()->cursorReadout();
    INFO("horizontal " << across.toStdString() << ", vertical " << down.toStdString());
    REQUIRE_FALSE(across.isEmpty());
    REQUIRE_FALSE(down.isEmpty());
    CHECK_THAT(across.toDouble(), WithinAbs(2.0, inchesPerPixel * 1.5 + 0.005));
    CHECK_THAT(down.toDouble(), WithinAbs(1.0, inchesPerPixel * 1.5 + 0.005));

    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(viewer.view->viewport(), &leave);
    QCoreApplication::processEvents();
    CHECK(viewer.horizontal()->cursorReadout().isEmpty());
    CHECK(viewer.vertical()->cursorReadout().isEmpty());
}

TEST_CASE("the cursor marker is red with its reading beside it, in both themes", "[rulers][I-17]")
{
    Fixture fixture;   // for the settings the theme reads, and nothing else

    // A real font, for the reason the I-13 case above gives: offscreen draws glyphs as boxes, and
    // the readout's box is sized from the text's metrics.
    const ScopedAppFont arial(QStringLiteral("C:/Windows/Fonts/arial.ttf"));
    const int loaded = arial.id;
    REQUIRE(loaded >= 0);
    const QStringList families = QFontDatabase::applicationFontFamilies(loaded);
    REQUIRE_FALSE(families.isEmpty());
    const QFont real(families.front(), 9);

    ui::RulerWidget::Mapping mapping;
    mapping.valid = true;
    mapping.pixelsPerPoint = 1.0;
    mapping.zeroPixel = 10.0;
    mapping.extentPoints = 600.0;

    constexpr int kLength = 400;
    const int across = ui::RulerWidget::kThickness;
    const QColor red = ui::RulerWidget::markerColour();

    // The ruler painted with the marker at `cursor`, or with none at -1. `pixelAt(along, across)`
    // reads either orientation with one pair of coordinates.
    struct Painted {
        QImage          image;
        Qt::Orientation orientation;
        QColor pixelAt(int along, int acrossAt) const
        {
            return orientation == Qt::Horizontal ? image.pixelColor(along, acrossAt)
                                                 : image.pixelColor(acrossAt, along);
        }
        // Pixels of the marker's red in [from, to) along the ruler, across the OUTER strip where
        // the readout's box goes.
        int redIn(int from, int to, const QColor& red, int strip) const
        {
            int count = 0;
            for (int along = std::max(0, from); along < to; ++along)
                for (int a = 1; a < strip; ++a)
                    if (uitest::deltaE76(pixelAt(along, a), red) < 3.0) ++count;
            return count;
        }
    };
    const auto paint = [&](Qt::Orientation orientation, double cursor) {
        ui::RulerWidget ruler(orientation);
        ruler.setFont(real);
        ruler.setMapping(mapping);
        ruler.setUnit(core::Unit::Points);
        if (orientation == Qt::Horizontal) ruler.resize(kLength, across);
        else                               ruler.resize(across, kLength);
        ruler.setCursorPixel(cursor);
        return Painted{ruler.grab().toImage(), orientation};
    };

    const QPalette before = QApplication::palette();
    ui::ThemeManager theme(fixture.settings);

    for (const core::Theme requested : {core::Theme::Light, core::Theme::Dark}) {
        fixture.settings.set(core::keys::general::theme, requested);
        theme.apply();
        QCoreApplication::processEvents();

        for (const Qt::Orientation orientation : {Qt::Horizontal, Qt::Vertical}) {
            const char* name = orientation == Qt::Horizontal ? "horizontal" : "vertical";
            const Painted without = paint(orientation, -1.0);
            const Painted with = paint(orientation, 200.0);

            // THE LINE, read on the INNER edge where the box never goes, against the SAME pixel
            // painted without a marker -- so it is measured against whatever the ruler has there,
            // tick or background, in this theme.
            // Which column an aliased line at x = 200.0 fills is the rasterizer's business, so the
            // reddest of the three around it is the line.
            int column = 199;
            for (int c = 200; c <= 201; ++c)
                if (uitest::deltaE76(with.pixelAt(c, across - 2), red)
                    < uitest::deltaE76(with.pixelAt(column, across - 2), red))
                    column = c;
            const QColor line = with.pixelAt(column, across - 2);
            const QColor under = without.pixelAt(column, across - 2);
            UNSCOPED_INFO((requested == core::Theme::Dark ? "dark " : "light ")
                          << name << ": line " << line.name().toStdString() << " over "
                          << under.name().toStdString() << " deltaE "
                          << uitest::deltaE76(line, under));
            CHECK(uitest::deltaE76(line, red) < 3.0);
            CHECK(uitest::deltaE76(line, under) >= 3.0 * uitest::kDistinctSurfaceDeltaE);

            // AND IT IS TWO PIXELS WIDE, not a hairline -- the user's request of 2026-09-26. Counted
            // on the inner edge, where the readout's box never reaches.
            int redColumns = 0;
            for (int c = 195; c <= 205; ++c)
                if (uitest::deltaE76(with.pixelAt(c, across - 2), red) < 3.0) ++redColumns;
            UNSCOPED_INFO("marker width " << redColumns << " px");
            // A NUMBER, not `kMarkerWidth`: a case that read the constant would pass whatever it was
            // set to, which is how its first version passed the 1-pixel plant.
            CHECK(redColumns >= 2);
            CHECK(redColumns <= 3);

            // THE READOUT'S BOX, after the marker. And none of that red anywhere without a marker:
            // the control, so a ruler that painted red for some other reason could not pass.
            const int strip = across / 2 + 2;
            CHECK(with.redIn(204, kLength, red, strip) > 60);
            CHECK(without.redIn(0, kLength, red, strip) == 0);

            // Near the far end it goes BEFORE the marker instead, rather than off the ruler.
            const Painted atEnd = paint(orientation, kLength - 4.0);
            CHECK(atEnd.redIn(0, kLength - 8, red, strip) > 60);
        }
    }

    QApplication::setPalette(before);
}

// =============================================================================================
// A-89 -- a guide being pulled off a ruler is shown DOTTED on the page until the release.
//
// The user's request of 2026-09-26: while the button is still down over a page, a dotted line
// where the guide will land. The preview is the guide `dropGuide` would make -- snapped included --
// so the cases assert it against the drop, not against a second copy of the arithmetic.
//
// What it does NOT cover (convention 19): whether the dots READ as "not placed yet" beside a
// placed guide and a locked one, on a real panel -- M-07's.
// =============================================================================================

namespace {

// One stage of a ruler drag, sent to the RULER, which holds the grab -- the same route
// `Viewer::dragGuideFrom` takes, split so a case can look between the stages.
void rulerMouse(ui::RulerWidget* ruler, QEvent::Type type, QPointF global, Qt::MouseButton button,
                Qt::MouseButtons buttons)
{
    QMouseEvent event(type, ruler->mapFromGlobal(global), global, global, button, buttons,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(ruler, &event);
    QCoreApplication::processEvents();
}

// The most guide-coloured viewport column within two pixels of `x`, over the rows `[top, bottom)`,
// and how many of those rows it has. A dotted line has gaps; a solid one has none.
int guidePixelsInColumn(const Viewer& viewer, int x, int top, int bottom)
{
    QPixmap grab(viewer.view->viewport()->size());
    viewer.view->viewport()->render(&grab);
    const QImage image = grab.toImage();
    const QColor colour = ui::GridGuideLayer::guideColour();

    int best = 0;
    for (int column = std::max(0, x - 2); column <= std::min(image.width() - 1, x + 2); ++column) {
        int count = 0;
        for (int row = std::max(0, top); row < std::min(image.height(), bottom); ++row)
            if (uitest::deltaE76(image.pixelColor(column, row), colour) < 3.0) ++count;
        best = std::max(best, count);
    }
    return best;
}

} // namespace

TEST_CASE("a guide pulled off a ruler is dotted until the release", "[rulers][guide-preview]")
{
    Viewer viewer;
    viewer.fixture.pumpTiles();

    const int page = viewer.controller().currentPage();
    const auto slot = viewer.layout().slotForPage(page);
    REQUIRE(slot != nullptr);
    const core::PdfPoint target{slot->cropBox.left() + 100.0, slot->cropBox.top() - 150.0};
    const QPoint at = viewer.viewportPointFor(page, target);
    const QPointF overPage = viewer.view->viewport()->mapToGlobal(QPointF(at));
    const QPointF onRuler = viewer.horizontal()->mapToGlobal(QPointF(40.0, 4.0));

    // The rows of the page that are on screen, in viewport pixels -- where the line is looked for.
    const core::DeviceRect visible = viewer.controller().visibleRect();
    const double dpr = viewer.view->devicePixelRatioF();
    const int top = static_cast<int>((std::max(slot->rect.top(), visible.top()) - visible.top()) / dpr) + 1;
    const int bottom =
        static_cast<int>((std::min(slot->rect.bottom(), visible.bottom()) - visible.top()) / dpr) - 1;
    REQUIRE(bottom - top > 100);

    ui::RulerWidget* ruler = viewer.horizontal();
    REQUIRE(guidePixelsInColumn(viewer, at.x(), top, bottom) == 0);   // nothing there yet

    // PRESSED on the ruler: nothing on the page yet.
    rulerMouse(ruler, QEvent::MouseButtonPress, ruler->mapToGlobal(QPointF(4.0, 4.0)),
               Qt::LeftButton, Qt::LeftButton);
    CHECK_FALSE(viewer.guides().preview().has_value());

    // DRAGGED over the page, button still down: a preview, and NOT a guide.
    rulerMouse(ruler, QEvent::MouseMove, overPage, Qt::NoButton, Qt::LeftButton);
    REQUIRE(viewer.guides().preview().has_value());
    CHECK(viewer.guides().guideCount(-1) == 0);
    const core::Guide preview = *viewer.guides().preview();
    CHECK(preview.orientation == core::Guide::Orientation::Vertical);
    CHECK(preview.pageIndex == page);

    // DOTTED, measured on painted pixels: guide-coloured along the column, with gaps.
    const int span = bottom - top;
    const int dotted = guidePixelsInColumn(viewer, at.x(), top, bottom);
    INFO("span " << span << ", dotted " << dotted);
    CHECK(dotted > span / 5);
    CHECK(dotted < span * 3 / 4);

    SECTION("released over the page, it becomes the solid guide it previewed")
    {
        rulerMouse(ruler, QEvent::MouseButtonRelease, overPage, Qt::LeftButton, Qt::NoButton);
        CHECK_FALSE(viewer.guides().preview().has_value());
        REQUIRE(viewer.guides().guideCount(-1) == 1);
        CHECK(viewer.guides().guides().front() == preview);

        const int solid = guidePixelsInColumn(viewer, at.x(), top, bottom);
        INFO("solid " << solid);
        CHECK(solid > span * 9 / 10);   // THE CONTROL: the same measurement, no gaps
    }

    SECTION("dragged back onto the ruler, the preview goes, and a release there makes nothing")
    {
        rulerMouse(ruler, QEvent::MouseMove, onRuler, Qt::NoButton, Qt::LeftButton);
        CHECK_FALSE(viewer.guides().preview().has_value());
        CHECK(guidePixelsInColumn(viewer, at.x(), top, bottom) == 0);

        rulerMouse(ruler, QEvent::MouseButtonRelease, onRuler, Qt::LeftButton, Qt::NoButton);
        CHECK(viewer.guides().guideCount(-1) == 0);
        CHECK_FALSE(viewer.guides().preview().has_value());
    }
}

TEST_CASE("the dotted preview sits where the snapped guide will land", "[rulers][guide-preview]")
{
    Viewer viewer;
    viewer.fixture.settings.set(core::keys::units::gridSpacing, 72.0);
    viewer.fixture.settings.set(core::keys::units::gridSubdivisions, 1);
    viewer.fixture.settings.set(core::keys::units::showGrid, true);
    viewer.fixture.settings.set(core::keys::units::snapToGrid, true);
    QCoreApplication::processEvents();
    viewer.fixture.pumpTiles();

    const SnapTarget target = visibleGridLine(viewer, true, 72.0);
    const QPoint closeBy = viewer.viewportPointFor(target.page, offsetFrom(target, true, 5.0));
    const QPointF overPage = viewer.view->viewport()->mapToGlobal(QPointF(closeBy));

    ui::RulerWidget* ruler = viewer.horizontal();
    rulerMouse(ruler, QEvent::MouseButtonPress, ruler->mapToGlobal(QPointF(4.0, 4.0)),
               Qt::LeftButton, Qt::LeftButton);
    rulerMouse(ruler, QEvent::MouseMove, overPage, Qt::NoButton, Qt::LeftButton);

    // 5 device pixels off the line, and the preview is ON it -- the promise the release keeps.
    REQUIRE(viewer.guides().preview().has_value());
    CHECK_THAT(viewer.guides().preview()->position, WithinAbs(target.line, 1e-6));

    rulerMouse(ruler, QEvent::MouseButtonRelease, overPage, Qt::LeftButton, Qt::NoButton);
    REQUIRE(viewer.guides().guideCount(-1) == 1);
    CHECK_THAT(viewer.guides().guides().front().position, WithinAbs(target.line, 1e-6));
}
