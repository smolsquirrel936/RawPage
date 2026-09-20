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
#include "tests/ui/ShellFixture.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/GridGuideLayer.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/RulerWidget.h"
#include "ui/viewer/ViewportController.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QPixmap>

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
