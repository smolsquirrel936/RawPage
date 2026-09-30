// Stage 4 T7 -- `ui/viewer/ToolController` and the four `ui/viewer/tools/` classes.
// Guide cases A-40 … A-44 (`Stage4-ViewingSurface-Testing-Guide.md` §5.8).
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS SUITE CANNOT SEE, NAMED HERE RATHER THAN DISCOVERED (convention 19).
//
//   * **It cannot judge the loupe's picture.** A-43's assertable half is that the lens moves
//     NOTHING — no scroll, no zoom, no history entry — and that is what is checked. Whether the
//     magnified region is the right region, and whether it is readable, is manual case **M-12**;
//     the lens is also fed from the widget's last painted frame, which offscreen is a real paint
//     but never a real screen.
//   * **It cannot see a physical modifier.** `Shift` arrives here as a bit on a synthetic event.
//     What a keyboard actually sends when a modifier is released over another window — A-41's
//     second clause — is why the controller re-derives the state from every event rather than
//     remembering a release, but the case that proves the RE-DERIVATION is below and the case
//     that proves the real keyboard behaves that way is M-12's.
//   * **Auto-scroll is stepped, not timed.** `advanceAutoScrollForTesting` moves the scroll by a
//     stated number of milliseconds so a rate can be asserted at all; what it leaves untested is
//     that the timer fires, which no headless run should claim.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/settings/Settings.h"
#include "tests/ui/ShellFixture.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/LoupeRaster.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/ViewportController.h"
#include "ui/viewer/tools/LoupeTool.h"
#include "ui/viewer/tools/MarqueeZoomTool.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QLayout>
#include <QMouseEvent>

#include <cmath>
#include <memory>

using namespace rawpage;
using core::ActionId;

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

// A window with a document, laid out, focused in the VIEWER -- the state a §8 Viewer chord is
// only resolvable in, and the state every tool gesture below starts from.
struct Viewer {
    Fixture                         fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::PageView*                   view = nullptr;

    explicit Viewer(int pages = 12)
    {
        fixture.backend.pagesPerDocument = pages;
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 850);
        window->openFiles({fixture.writeFile(QStringLiteral("tools.pdf"))});
        driveLayout(window.get());

        view = window->currentView();
        REQUIRE(view != nullptr);

        window->raiseToForeground();
        view->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();

        // THE GEOMETRY FIRST (convention 12, guide rule 4). Every gesture below is in widget
        // coordinates, and a 0-pixel viewer would make every one of them land outside it and
        // every assertion about the result pass or fail for the wrong reason.
        REQUIRE(view->width() > 400);
        REQUIRE(view->height() > 300);
        REQUIRE(window->shortcutBinder()->activeContext() == core::KeyContext::Viewer);
    }

    ui::ToolController& tools() const { return view->tools(); }
    ui::ViewportController& controller() const { return view->controller(); }

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

    // A drag, as three real mouse events on the viewport -- which is what routes them through
    // `PageView`'s handlers and so through `ToolController`, rather than calling the tool.
    void mouse(QEvent::Type type, QPoint pos, Qt::MouseButton button,
               Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        QMouseEvent event(type, QPointF(pos), QPointF(pos), button,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : button, modifiers);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QCoreApplication::processEvents();
    }

    void dragFrom(QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, modifiers);
        mouse(QEvent::MouseMove, to, Qt::LeftButton, modifiers);
        mouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, modifiers);
    }
};

} // namespace

// =============================================================================================
// A-40 -- H, V, Z arm their tool from a key event, and the cursor follows.
// =============================================================================================

TEST_CASE("H V and Z arm their tool from a key event and the cursor follows", "[tools]")
{
    Viewer viewer;

    // THE DEFAULT IS SELECT, which matters: the default is the mode a person is in when they have
    // not chosen one, and a viewer whose default was the hand would make every click a pan.
    CHECK(viewer.tools().armed() == ui::ToolKind::Select);
    CHECK(viewer.tools().cursor() == Qt::IBeamCursor);

    CHECK(viewer.press(Qt::Key_H));
    CHECK(viewer.tools().armed() == ui::ToolKind::Hand);
    CHECK(viewer.tools().cursor() == Qt::OpenHandCursor);

    CHECK(viewer.press(Qt::Key_Z));
    CHECK(viewer.tools().armed() == ui::ToolKind::MarqueeZoom);
    CHECK(viewer.tools().cursor() == Qt::CrossCursor);

    CHECK(viewer.press(Qt::Key_V));
    CHECK(viewer.tools().armed() == ui::ToolKind::Select);
    CHECK(viewer.tools().cursor() == Qt::IBeamCursor);

    // THE CURSOR REACHED THE WIDGET, not merely the controller. A controller that reported the
    // right shape while the viewport still showed an arrow would pass every line above.
    CHECK(viewer.view->viewport()->cursor().shape() == Qt::IBeamCursor);
}

TEST_CASE("the selection tool selects nothing and says so", "[tools]")
{
    // convention 19, as a case rather than only a comment. `V` arms a real tool with a real
    // cursor, and a drag with it does nothing at all -- text selection is STAGE 5 T5's. Stage 5 T2
    // built the model and the extractor (the census says 4 of 7 since then); T5 gives this tool
    // `TextSelection` and `SelectionOverlay`, and this case changes with it.
    Viewer viewer;
    viewer.press(Qt::Key_V);

    const core::render::Zoom zoom = viewer.controller().zoom();
    const core::DevicePoint scroll = viewer.controller().scroll();

    viewer.dragFrom(QPoint(120, 140), QPoint(320, 300));

    CHECK(viewer.controller().zoom().factor == zoom.factor);
    CHECK(viewer.controller().scroll().x == scroll.x);
    CHECK(viewer.controller().scroll().y == scroll.y);
    CHECK_FALSE(viewer.tools().isDragging());
}

TEST_CASE("the loupe is armed by dispatch because section 8 gives it no chord", "[tools]")
{
    Viewer viewer;

    // `tool.loupe` is one of the two ids Stage 4 declares and it has NO default binding -- §8's
    // Viewer table binds `H`, `V`, `Z` and `S` and not this, and `Features.md` §8 is untouched by
    // this stage. So the ribbon, the tool finder and the batch runner reach it, and no key does.
    CHECK(viewer.fixture.actions.isRegistered(ActionId::ToolLoupe));
    CHECK(viewer.fixture.actions.dispatch(ActionId::ToolLoupe, core::KeyContext::Viewer));
    CHECK(viewer.tools().armed() == ui::ToolKind::Loupe);
    CHECK(viewer.tools().cursor() == Qt::CrossCursor);
}

// =============================================================================================
// A-41 -- the temporary hand modifier pans without disarming the tool.
// =============================================================================================

TEST_CASE("the temporary hand modifier pans without disarming the armed tool", "[tools]")
{
    Viewer viewer;
    viewer.press(Qt::Key_Z);                       // marquee zoom armed and STAYING armed
    REQUIRE(viewer.tools().armed() == ui::ToolKind::MarqueeZoom);

    // Somewhere scrollable, so a pan has room to move in both directions.
    viewer.controller().setScroll(core::DevicePoint{0.0, 400.0});
    const core::DevicePoint before = viewer.controller().scroll();
    const core::render::Zoom zoom = viewer.controller().zoom();

    // HOLDING THE MODIFIER CHANGES THE EFFECTIVE TOOL, NOT THE ARMED ONE. Those are different
    // questions and the controller answers them separately -- `armed()` is the sticky choice a
    // person made, `effective()` is what this gesture will do.
    viewer.tools().setModifiers(ui::ToolController::kTemporaryHandModifier);
    CHECK(viewer.tools().temporaryHandActive());
    CHECK(viewer.tools().armed() == ui::ToolKind::MarqueeZoom);
    CHECK(viewer.tools().effective() == ui::ToolKind::Hand);
    CHECK(viewer.tools().cursor() == Qt::OpenHandCursor);

    viewer.dragFrom(QPoint(500, 400), QPoint(460, 330),
                    ui::ToolController::kTemporaryHandModifier);

    // IT PANNED, AND THE DIRECTION IS ASSERTED. Dragging UP and LEFT shows content further down
    // and right, so the scroll offset GROWS. Getting this sign wrong is the classic hand-tool
    // defect: unmistakable to a person, invisible to a case that only checks that something moved.
    CHECK(viewer.controller().scroll().y > before.y);
    CHECK(viewer.controller().scroll().x >= before.x);

    // AND IT ZOOMED NOTHING: the marquee is still armed but never saw the gesture.
    CHECK(viewer.controller().zoom().factor == zoom.factor);
    CHECK(viewer.tools().armed() == ui::ToolKind::MarqueeZoom);

    // RELEASING RESTORES THE TOOL AND ITS CURSOR.
    viewer.tools().setModifiers(Qt::NoModifier);
    CHECK_FALSE(viewer.tools().temporaryHandActive());
    CHECK(viewer.tools().effective() == ui::ToolKind::MarqueeZoom);
    CHECK(viewer.tools().cursor() == Qt::CrossCursor);
}

TEST_CASE("a modifier released outside the widget cannot survive into the next gesture", "[tools]")
{
    // A-41's second clause. The controller takes modifiers on EVERY event instead of watching for
    // a key release, because a modifier let go while the pointer is over another window sends this
    // widget no release at all -- so the state is re-derived from whatever arrives next rather
    // than remembered.
    Viewer viewer;
    viewer.press(Qt::Key_V);                       // Select armed
    viewer.tools().setModifiers(ui::ToolController::kTemporaryHandModifier);
    REQUIRE(viewer.tools().effective() == ui::ToolKind::Hand);

    const core::DevicePoint before = viewer.controller().scroll();

    // NO RELEASE EVENT, EVER -- the next thing this widget sees is a press, with the modifier
    // already gone. THE PRESS IS THE EVENT THAT MATTERS: `ToolController::press` re-derives before
    // it hands the gesture to anything, so a stale modifier cannot start a pan the user did not
    // ask for. That is the guarantee, and it is asserted on the EFFECT -- the view did not move --
    // rather than on the flag alone (rule 1).
    viewer.dragFrom(QPoint(400, 400), QPoint(340, 320), Qt::NoModifier);

    CHECK_FALSE(viewer.tools().temporaryHandActive());
    CHECK(viewer.tools().effective() == ui::ToolKind::Select);
    CHECK(viewer.controller().scroll().x == before.x);
    CHECK(viewer.controller().scroll().y == before.y);

    // WHAT THIS DOES NOT COVER, and it was found by writing the case the other way round
    // (convention 19): a HOVER does not correct the state, because with a non-tracking tool armed
    // Qt delivers no mouse-move to the widget at all -- there is nothing for the controller to
    // re-derive from until a button, a key or a tool change arrives. The consequence is cosmetic
    // and bounded: between releasing the modifier off-window and the next event, `cursor()` still
    // reports the hand. The moment anything happens, it does not.
    CHECK(viewer.tools().cursor() == Qt::IBeamCursor);
}

TEST_CASE("the modifier does not swap tools under a live drag", "[tools]")
{
    // The control for the clause above: re-deriving on every event must not hand a release to a
    // tool that never saw the press. A person does not change tools mid-drag by pressing Shift.
    Viewer viewer;
    viewer.press(Qt::Key_Z);

    viewer.mouse(QEvent::MouseButtonPress, QPoint(200, 200), Qt::LeftButton);
    REQUIRE(viewer.tools().isDragging());
    REQUIRE(viewer.tools().effective() == ui::ToolKind::MarqueeZoom);

    viewer.mouse(QEvent::MouseMove, QPoint(260, 250), Qt::LeftButton,
                 ui::ToolController::kTemporaryHandModifier);
    CHECK(viewer.tools().effective() == ui::ToolKind::MarqueeZoom);
    CHECK_FALSE(viewer.tools().temporaryHandActive());

    viewer.mouse(QEvent::MouseButtonRelease, QPoint(260, 250), Qt::LeftButton);
}

// =============================================================================================
// A-42 -- Esc cancels a drag and leaves the view where it was.
// =============================================================================================

TEST_CASE("Esc cancels a marquee drag and the zoom and scroll are exactly as before", "[tools]")
{
    Viewer viewer;
    viewer.press(Qt::Key_Z);

    const core::render::Zoom zoom = viewer.controller().zoom();
    const core::DevicePoint scroll = viewer.controller().scroll();

    viewer.mouse(QEvent::MouseButtonPress, QPoint(150, 150), Qt::LeftButton);
    viewer.mouse(QEvent::MouseMove, QPoint(500, 450), Qt::LeftButton);
    REQUIRE(viewer.tools().isDragging());

    CHECK(viewer.press(Qt::Key_Escape));
    CHECK_FALSE(viewer.tools().isDragging());

    // EXACTLY, not approximately: the marquee changes nothing until release, so a cancelled one
    // has nothing to restore and any drift here would mean it had been acting during the drag.
    CHECK(viewer.controller().zoom().factor == zoom.factor);
    CHECK(viewer.controller().scroll().x == scroll.x);
    CHECK(viewer.controller().scroll().y == scroll.y);

    // AND THE RELEASE THAT FOLLOWS DOES NOT ZOOM. A cancel that only stopped drawing the band
    // would let the button-up that the user is still holding complete the gesture anyway.
    viewer.mouse(QEvent::MouseButtonRelease, QPoint(500, 450), Qt::LeftButton);
    CHECK(viewer.controller().zoom().factor == zoom.factor);
}

TEST_CASE("Esc cancels a hand pan and puts the scroll back where the drag started", "[tools]")
{
    // The other half, and the harder one: the hand HAS been changing the view all the way through
    // the drag, so cancelling means restoring rather than merely stopping.
    Viewer viewer;
    viewer.press(Qt::Key_H);
    viewer.controller().setScroll(core::DevicePoint{0.0, 500.0});
    const core::DevicePoint before = viewer.controller().scroll();

    viewer.mouse(QEvent::MouseButtonPress, QPoint(400, 400), Qt::LeftButton);
    viewer.mouse(QEvent::MouseMove, QPoint(400, 300), Qt::LeftButton);
    REQUIRE(viewer.controller().scroll().y != before.y);   // it really did move first

    CHECK(viewer.press(Qt::Key_Escape));
    CHECK(viewer.controller().scroll().y == before.y);
    CHECK_FALSE(viewer.tools().isDragging());
}

TEST_CASE("Esc with no drag in progress changes neither the tool nor the view", "[tools]")
{
    // A-42's CONTROL. And the second clause is the one that cost a red suite: a cancel that found
    // nothing must NOT consume the key, or `Esc` stops reaching everything else that wants it --
    // which is why `edit.cancel` is left unregistered and this is handled in `PageView`. See the
    // note at that id's absent registration in `MainWindow::registerViewerActions`.
    Viewer viewer;
    viewer.press(Qt::Key_Z);

    const core::render::Zoom zoom = viewer.controller().zoom();
    const core::DevicePoint scroll = viewer.controller().scroll();

    CHECK_FALSE(viewer.press(Qt::Key_Escape));   // NOT consumed
    CHECK(viewer.tools().armed() == ui::ToolKind::MarqueeZoom);
    CHECK(viewer.controller().zoom().factor == zoom.factor);
    CHECK(viewer.controller().scroll().y == scroll.y);
}

// =============================================================================================
// A-43 -- marquee zoom zooms to the rectangle; the loupe magnifies without moving the view.
// =============================================================================================

TEST_CASE("a marquee zooms to the dragged rectangle within a stated tolerance", "[tools]")
{
    Viewer viewer;
    viewer.press(Qt::Key_Z);

    const double before = viewer.controller().zoom().factor;
    const QSize viewport = viewer.view->viewport()->size();

    // A band a QUARTER of the viewport's width and height. The expected zoom is therefore about
    // 4x -- the tighter axis wins, and here both axes are the same ratio, so the two agree and
    // the expectation does not depend on which rule was applied.
    const QRect band(viewport.width() / 4, viewport.height() / 4, viewport.width() / 4,
                     viewport.height() / 4);
    viewer.dragFrom(band.topLeft(), band.bottomRight());

    const double after = viewer.controller().zoom().factor;
    const double expected = before * 4.0;

    // THE TOLERANCE IS STATED rather than left to a bare equality: the band is an integral pixel
    // rectangle, the viewport is not exactly divisible by four, and `Zoom::clamped()` may round.
    // Ten per cent is far tighter than the failure this case exists to catch, which is a zoom that
    // did not happen or that went the wrong way.
    INFO("before " << before << " after " << after << " expected " << expected);
    CHECK(std::abs(after - expected) < expected * 0.10);
    CHECK(after > before);
}

TEST_CASE("a click with the marquee armed is not a zoom", "[tools]")
{
    // The control for the case above, and a defect with real consequences: zooming to a two-pixel
    // band lands at 6400 % with no obvious way back.
    Viewer viewer;
    viewer.press(Qt::Key_Z);
    const double before = viewer.controller().zoom().factor;

    const int tiny = ui::MarqueeZoomTool::kMinDragPixels - 2;
    viewer.dragFrom(QPoint(300, 300), QPoint(300 + tiny, 300 + tiny));

    CHECK(viewer.controller().zoom().factor == before);
}

TEST_CASE("the loupe magnifies and moves neither the scroll nor the zoom", "[tools]")
{
    Viewer viewer;
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ToolLoupe, core::KeyContext::Viewer));
    driveLayout(viewer.window.get());

    viewer.controller().setScroll(core::DevicePoint{0.0, 300.0});
    const core::DevicePoint scroll = viewer.controller().scroll();
    const double zoom = viewer.controller().zoom().factor;

    // IT FOLLOWS A HOVER, with no button down -- a magnifier you must hold a button to aim is a
    // magnifier nobody can aim. That is why `PageView` turns mouse tracking on for this tool and
    // for no other.
    CHECK(viewer.view->viewport()->hasMouseTracking());
    viewer.mouse(QEvent::MouseMove, QPoint(420, 360), Qt::NoButton);

    // The assertable half of A-43: the lens moved NOTHING. Whether the magnified picture is right
    // is M-12's -- stated here rather than implied (convention 19).
    CHECK(viewer.controller().scroll().x == scroll.x);
    CHECK(viewer.controller().scroll().y == scroll.y);
    CHECK(viewer.controller().zoom().factor == zoom);
    CHECK_FALSE(viewer.tools().isDragging());

    // Esc puts the lens away and REPORTS that it did -- something on screen disappeared, so the
    // key was used.
    CHECK(viewer.press(Qt::Key_Escape));
}

// =============================================================================================
// A-44 -- auto-scroll runs at the configured speed, pauses and reverses.
// =============================================================================================

TEST_CASE("auto-scroll runs at the configured speed and the speed changes the rate", "[tools]")
{
    Viewer viewer;
    ui::ToolController& tools = viewer.tools();

    tools.setAutoScrollSpeed(60);
    tools.toggleAutoScroll();
    REQUIRE(tools.autoScrollRunning());

    const double start = viewer.controller().scroll().y;
    tools.advanceAutoScrollForTesting(1000);
    const double slow = viewer.controller().scroll().y - start;

    // SIXTY PIXELS PER SECOND, to the pixel the sub-pixel carry allows.
    INFO("moved " << slow);
    CHECK(slow >= 59.0);
    CHECK(slow <= 61.0);

    // THE SPEED IS THE RATE, measurably: four times the number moves four times as far over the
    // same interval. A case that only checked "it moved" would pass for a setting that was read
    // and ignored, which is the failure `pageDisplay/lineWeight` is the standing example of.
    const double mark = viewer.controller().scroll().y;
    tools.setAutoScrollSpeed(240);
    tools.advanceAutoScrollForTesting(1000);
    const double fast = viewer.controller().scroll().y - mark;
    INFO("moved " << fast);
    CHECK(fast > slow * 3.5);
}

TEST_CASE("auto-scroll pauses and reverses", "[tools]")
{
    Viewer viewer;
    ui::ToolController& tools = viewer.tools();

    viewer.controller().setScroll(core::DevicePoint{0.0, 600.0});
    tools.setAutoScrollSpeed(120);
    tools.toggleAutoScroll();
    REQUIRE(tools.autoScrollRunning());

    tools.advanceAutoScrollForTesting(500);
    const double afterForward = viewer.controller().scroll().y;
    CHECK(afterForward > 600.0);

    // THE TOGGLE IS THE PAUSE. There is nothing else to resume into, so a second command rather
    // than a second id is the whole of it -- see `ToolController.h`.
    tools.toggleAutoScroll();
    CHECK_FALSE(tools.autoScrollRunning());
    tools.advanceAutoScrollForTesting(500);
    CHECK(viewer.controller().scroll().y == afterForward);

    // REVERSE IS A NEGATIVE SPEED, which is why it needs no id of its own: the sign lives in
    // `pageDisplay/autoScrollSpeed`, so it is reachable from Preferences, the settings file and
    // the batch runner rather than only from a key.
    tools.setAutoScrollSpeed(-120);
    tools.toggleAutoScroll();
    REQUIRE(tools.autoScrollRunning());
    tools.advanceAutoScrollForTesting(500);
    CHECK(viewer.controller().scroll().y < afterForward);
}

TEST_CASE("auto-scroll stops at the end of the document rather than spinning", "[tools]")
{
    // A timer running for ever against a clamped scroll offset is a document that looks frozen
    // while the machine works -- and nothing on screen says which. Reaching the end is the end of
    // the gesture.
    Viewer viewer(3);
    ui::ToolController& tools = viewer.tools();

    tools.setAutoScrollSpeed(2000);
    tools.toggleAutoScroll();
    REQUIRE(tools.autoScrollRunning());

    for (int i = 0; i < 200 && tools.autoScrollRunning(); ++i)
        tools.advanceAutoScrollForTesting(1000);

    CHECK_FALSE(tools.autoScrollRunning());
}

TEST_CASE("auto-scroll refuses to start at zero and Esc stops it", "[tools]")
{
    Viewer viewer;
    ui::ToolController& tools = viewer.tools();

    // A CONTROL THAT READS BACK AS ON AND DOES NOTHING is Stage 2's I-11 in miniature, so a zero
    // speed refuses to start -- and the command reports itself unavailable rather than refusing
    // after the press (convention 16).
    tools.setAutoScrollSpeed(0);
    tools.toggleAutoScroll();
    CHECK_FALSE(tools.autoScrollRunning());
    CHECK_FALSE(viewer.fixture.actions.isEnabled(ActionId::ViewAutoScroll));

    tools.setAutoScrollSpeed(90);
    CHECK(viewer.fixture.actions.isEnabled(ActionId::ViewAutoScroll));
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ViewAutoScroll, core::KeyContext::Viewer));
    CHECK(tools.autoScrollRunning());

    // `Esc` STOPS IT, and consumes the key because it did something -- unlike the no-drag case.
    CHECK(viewer.press(Qt::Key_Escape));
    CHECK_FALSE(tools.autoScrollRunning());
}

TEST_CASE("the auto-scroll speed reaches the controller from the settings key", "[tools]")
{
    // `pageDisplay/autoScrollSpeed` is the first of D4-5's fifteen Stage 4 keys, and this is the
    // half that would otherwise be a preference nobody reads -- which is exactly what
    // `performance/prefetchPages` was until the case that asserted a HELD PAGE COUNT found it
    // (S2 T15). So this asserts the controller's value, not that the setting stored one.
    Viewer viewer;
    viewer.fixture.settings.set(core::keys::pageDisplay::autoScrollSpeed, 175);
    QCoreApplication::processEvents();

    CHECK(viewer.tools().autoScrollSpeed() == 175);
}

// =============================================================================================
// S4 guide A-97 -- a CLICK pins the loupe and a DRAG aims it (I-26)
// =============================================================================================

TEST_CASE("the loupe pins on a click and follows a drag pinned or not", "[tools][I-26]")
{
    // M-12's walk, 2026-09-27: a drag without the modifier left the lens stuck where the drag
    // began, because a PRESS toggled the pin. The user chose the model this case asserts: only a
    // click pins or unpins; a drag moves the lens, and leaves the pin as it was. And through all
    // of it the view does not move (A-43).
    Viewer viewer;
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ToolLoupe, core::KeyContext::Viewer));
    driveLayout(viewer.window.get());
    const ui::LoupeTool& loupe = viewer.tools().loupe();

    viewer.controller().setScroll(core::DevicePoint{0.0, 300.0});
    const core::DevicePoint scroll = viewer.controller().scroll();
    const double zoom = viewer.controller().zoom().factor;

    // HOVER aims an unpinned lens.
    viewer.mouse(QEvent::MouseMove, QPoint(300, 300), Qt::NoButton);
    REQUIRE(loupe.isVisible());
    CHECK(loupe.centre() == QPoint(300, 300));
    CHECK_FALSE(loupe.isPinned());

    // A DRAG, unpinned: the lens follows the drag, and is NOT pinned at the end of it.
    viewer.mouse(QEvent::MouseButtonPress, QPoint(320, 320), Qt::LeftButton);
    viewer.mouse(QEvent::MouseMove, QPoint(380, 360), Qt::LeftButton);
    CHECK(loupe.centre() == QPoint(380, 360));
    viewer.mouse(QEvent::MouseMove, QPoint(440, 400), Qt::LeftButton);
    CHECK(loupe.centre() == QPoint(440, 400));
    viewer.mouse(QEvent::MouseButtonRelease, QPoint(440, 400), Qt::LeftButton);
    CHECK_FALSE(loupe.isPinned());
    viewer.mouse(QEvent::MouseMove, QPoint(250, 280), Qt::NoButton);
    CHECK(loupe.centre() == QPoint(250, 280));   // still following the hover

    // A CLICK pins it where it was clicked; the hover no longer moves it.
    viewer.mouse(QEvent::MouseButtonPress, QPoint(350, 330), Qt::LeftButton);
    viewer.mouse(QEvent::MouseButtonRelease, QPoint(350, 330), Qt::LeftButton);
    CHECK(loupe.isPinned());
    CHECK(loupe.centre() == QPoint(350, 330));
    viewer.mouse(QEvent::MouseMove, QPoint(500, 450), Qt::NoButton);
    CHECK(loupe.centre() == QPoint(350, 330));

    // A DRAG, pinned: it moves the lens, and the lens stays pinned where the drag put it.
    viewer.mouse(QEvent::MouseButtonPress, QPoint(350, 330), Qt::LeftButton);
    viewer.mouse(QEvent::MouseMove, QPoint(420, 380), Qt::LeftButton);
    CHECK(loupe.centre() == QPoint(420, 380));
    viewer.mouse(QEvent::MouseButtonRelease, QPoint(460, 410), Qt::LeftButton);
    CHECK(loupe.isPinned());
    CHECK(loupe.centre() == QPoint(460, 410));
    viewer.mouse(QEvent::MouseMove, QPoint(200, 200), Qt::NoButton);
    CHECK(loupe.centre() == QPoint(460, 410));

    // A CLICK unpins it, and the hover aims it again.
    viewer.mouse(QEvent::MouseButtonPress, QPoint(300, 300), Qt::LeftButton);
    viewer.mouse(QEvent::MouseButtonRelease, QPoint(300, 300), Qt::LeftButton);
    CHECK_FALSE(loupe.isPinned());
    viewer.mouse(QEvent::MouseMove, QPoint(330, 310), Qt::NoButton);
    CHECK(loupe.centre() == QPoint(330, 310));

    // AND NONE OF IT MOVED THE VIEW (A-43).
    CHECK(viewer.controller().scroll().x == scroll.x);
    CHECK(viewer.controller().scroll().y == scroll.y);
    CHECK(viewer.controller().zoom().factor == zoom);
}

TEST_CASE("the temporary hand does not take over a held loupe button", "[tools][I-26]")
{
    // The control I-26 needs, and the reason a held button now counts as the loupe's gesture: with
    // the button down, the modifier must not swap the tool, or the release would go to a hand that
    // never saw the press and the loupe would be left believing its button is still down.
    Viewer viewer;
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ToolLoupe, core::KeyContext::Viewer));
    driveLayout(viewer.window.get());

    viewer.mouse(QEvent::MouseButtonPress, QPoint(300, 300), Qt::LeftButton);
    REQUIRE(viewer.tools().isDragging());
    viewer.mouse(QEvent::MouseMove, QPoint(380, 360), Qt::LeftButton,
                 ui::ToolController::kTemporaryHandModifier);
    CHECK(viewer.tools().effective() == ui::ToolKind::Loupe);
    viewer.mouse(QEvent::MouseButtonRelease, QPoint(380, 360), Qt::LeftButton);
    CHECK_FALSE(viewer.tools().isDragging());
    CHECK(viewer.tools().loupe().centre() == QPoint(380, 360));
}

// =============================================================================================
// S4 guide A-98 -- the temporary hand's modifier, down and up, repaints and swaps at once (I-27)
// =============================================================================================

namespace {

// Counts the paints a widget actually receives -- the observable I-27 was missing: the controller
// changed its tool and nothing asked the page to be drawn again.
class PaintCounter : public QObject {
public:
    int paints = 0;
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Paint) ++paints;
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

TEST_CASE("the temporary hand's modifier repaints the page and swaps the tool on the key alone",
          "[tools][I-27]")
{
    // M-12's walk, 2026-09-27: with the loupe armed, holding the modifier did not take the lens
    // away until a drag started. The controller swapped to the hand and asked for no repaint, so
    // the lens -- painted by whichever tool is in effect -- stayed on screen until a pan repainted
    // the page; and a modifier RELEASE was not read at all, so the lens came back only when the
    // pointer moved. Both are asserted on the KEY ALONE: no mouse event between key and check.
    Viewer viewer;
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ToolLoupe, core::KeyContext::Viewer));
    driveLayout(viewer.window.get());
    viewer.view->setFocus(Qt::OtherFocusReason);
    viewer.mouse(QEvent::MouseMove, QPoint(300, 300), Qt::NoButton);
    REQUIRE(viewer.tools().loupe().isVisible());

    PaintCounter counter;
    viewer.view->viewport()->installEventFilter(&counter);
    QCoreApplication::processEvents();
    counter.paints = 0;

    // MODIFIER DOWN: the hand is in effect and the page is drawn again, which takes the lens away.
    {
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
        QCoreApplication::sendEvent(viewer.view, &down);
        QCoreApplication::processEvents();
    }
    CHECK(viewer.tools().effective() == ui::ToolKind::Hand);
    CHECK(viewer.tools().armed() == ui::ToolKind::Loupe);
    CHECK(counter.paints > 0);

    // MODIFIER UP -- sent the way a platform may report it, with the modifier still set on its own
    // release: the loupe is back, its cursor too, and the page is drawn again with the lens.
    counter.paints = 0;
    {
        QKeyEvent up(QEvent::KeyRelease, Qt::Key_Shift, Qt::ShiftModifier);
        QCoreApplication::sendEvent(viewer.view, &up);
        QCoreApplication::processEvents();
    }
    CHECK(viewer.tools().effective() == ui::ToolKind::Loupe);
    CHECK(viewer.tools().cursor() == Qt::CrossCursor);
    CHECK(counter.paints > 0);

    viewer.view->viewport()->removeEventFilter(&counter);
}

// =============================================================================================
// S4 guide A-99 -- the Loupe's lens re-rasterizes its region at the magnified scale (D4-20)
// =============================================================================================

namespace {

// Real time passes, with the event loop running -- the lens's settle interval is a real timer, and a
// case that faked it would not be testing that a SWEEP never lets it fire.
void waitMs(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

// The fake rasterizer paints the lens's scale in its own colour for the length of a case, and only
// then -- `uitest::fakeLensScaleThreshold` is off for every other case.
struct LensColour {
    explicit LensColour(double threshold) { uitest::fakeLensScaleThreshold = threshold; }
    ~LensColour() { uitest::fakeLensScaleThreshold = 0.0; }
};

QColor pixelAt(QWidget* widget, QPoint at)
{
    const QImage image = widget->grab().toImage();
    const double ratio = image.devicePixelRatio();
    return image.pixelColor(QPoint(static_cast<int>(at.x() * ratio), static_cast<int>(at.y() * ratio)));
}

bool isLensColour(const QColor& colour)
{
    return std::abs(colour.red() - uitest::kFakeLensRed) <= 8
           && std::abs(colour.green() - uitest::kFakeLensGreen) <= 8
           && std::abs(colour.blue() - uitest::kFakeLensBlue) <= 8;
}

bool isTileColour(const QColor& colour)
{
    return std::abs(colour.red() - uitest::kFakeTileRed) <= 8
           && std::abs(colour.green() - uitest::kFakeTileGreen) <= 8
           && std::abs(colour.blue() - uitest::kFakeTileBlue) <= 8;
}

// The lens's source around a pointer, in widget pixels -- `LoupeTool::paint`'s own arithmetic.
QRectF lensSourceAt(QPoint centre)
{
    const double span = ui::LoupeTool::kLensSize / ui::LoupeTool::kDefaultMagnification;
    return QRectF(centre.x() - span / 2.0, centre.y() - span / 2.0, span, span);
}

} // namespace

TEST_CASE("the loupe's lens asks for its region once still and paints it sharp", "[tools][D4-20]")
{
    Viewer viewer;
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ToolLoupe, core::KeyContext::Viewer));
    driveLayout(viewer.window.get());
    viewer.fixture.pumpTiles();

    const double presented = viewer.view->layoutEngine().layout().presentedScale();
    REQUIRE(presented > 0.0);
    const double lensScale = presented * ui::LoupeTool::kDefaultMagnification;
    LensColour colour(lensScale * 0.99);

    const ui::LoupeRaster& raster = viewer.view->loupeRaster();
    const core::DevicePoint scroll = viewer.controller().scroll();
    const double zoom = viewer.controller().zoom().factor;

    // (1) A SWEEP ASKS FOR NOTHING: each hover lands well inside the settle interval of the last.
    for (int step = 0; step <= 5; ++step) {
        viewer.mouse(QEvent::MouseMove, QPoint(300 + 10 * step, 300), Qt::NoButton);
        QCoreApplication::processEvents();
    }
    CHECK(raster.enqueuedCount() == 0);

    // (2) STILL FOR THE SETTLE INTERVAL: ONE request, in the Visible lane, for exactly the lens's
    // region at the magnified scale, shaped by the presenter's own builder.
    const QPoint still(350, 300);
    const auto before = viewer.fixture.renderScheduler.stats();
    waitMs(ui::PageView::kLoupeSettleMs + 120);
    const auto after = viewer.fixture.renderScheduler.stats();

    const ui::PageView::LoupeRegion region = viewer.view->loupeRegionFor(lensSourceAt(still));
    REQUIRE(region.request.isValid());
    CHECK(raster.enqueuedCount() == 1);
    CHECK(raster.aimed() == region.request);
    CHECK(std::abs(raster.aimed().deviceScale - lensScale) < 1e-9);
    CHECK(after.enqueuedVisible == before.enqueuedVisible + 1);
    CHECK(after.enqueuedPrefetch == before.enqueuedPrefetch);

    // (3) WHEN IT ARRIVES THE LENS PAINTS IT -- the lens's colour at the lens's centre, while the
    // page outside the lens is still the tiles' colour.
    viewer.fixture.pumpTiles();
    QCoreApplication::processEvents();
    const QColor inLens = pixelAt(viewer.view->viewport(), still);
    INFO("in the lens: " << inLens.red() << "," << inLens.green() << "," << inLens.blue());
    CHECK(isLensColour(inLens));
    const QColor outside = pixelAt(viewer.view->viewport(), QPoint(still.x(), still.y() + 200));
    INFO("outside: " << outside.red() << "," << outside.green() << "," << outside.blue());
    CHECK(isTileColour(outside));

    // (4) MOVING ON CANCELS: with the worker paused the next region's request sits in the queue,
    // and moving again drops it before it ever starts -- and the lens shows the enlarged frame, not
    // the old sharp raster, while it waits.
    viewer.fixture.renderScheduler.pause();
    const QPoint second(420, 420);
    viewer.mouse(QEvent::MouseMove, second, Qt::NoButton);
    QCoreApplication::processEvents();
    CHECK(isTileColour(pixelAt(viewer.view->viewport(), second)));
    waitMs(ui::PageView::kLoupeSettleMs + 120);
    REQUIRE(raster.enqueuedCount() == 2);
    REQUIRE(raster.isPending());
    const auto queued = viewer.fixture.renderScheduler.stats();

    viewer.mouse(QEvent::MouseMove, QPoint(480, 380), Qt::NoButton);
    QCoreApplication::processEvents();
    CHECK_FALSE(raster.isPending());
    CHECK(viewer.fixture.renderScheduler.stats().cancelledBeforeStart
          == queued.cancelledBeforeStart + 1);
    viewer.fixture.renderScheduler.resume();

    // (6) AND NONE OF IT MOVED THE VIEW (A-43).
    CHECK(viewer.controller().scroll().x == scroll.x);
    CHECK(viewer.controller().scroll().y == scroll.y);
    CHECK(viewer.controller().zoom().factor == zoom);
}

TEST_CASE("the loupe's lens asks only for the part of its region on a page", "[tools][D4-20]")
{
    // Claim (5): a region straddling a page's edge asks only for its part on the page, and one over
    // the desk beside every page asks for nothing. Pure arithmetic over the laid-out view, so it is
    // asserted on `loupeRegionFor` rather than through a timer.
    Viewer viewer;
    driveLayout(viewer.window.get());

    const core::render::PageLayout& layout = viewer.view->layoutEngine().layout();
    const core::DeviceRect visible = viewer.controller().visibleRect();
    const double dpr = viewer.view->devicePixelRatioF();
    const std::vector<core::render::PageSlot> onScreen = layout.slotsIntersecting(visible);
    REQUIRE_FALSE(onScreen.empty());
    const core::render::PageSlot& slot = onScreen.front();

    // The page's right edge and its vertical middle, in widget pixels.
    const double rightEdge = (slot.rect.right() - visible.left()) / dpr;
    const double middle = (std::max(slot.rect.top(), visible.top()) - visible.top()) / dpr + 100.0;

    // STRADDLING THE RIGHT EDGE: 20 widget pixels on the page, the rest over the desk.
    const QRectF straddle(rightEdge - 20.0, middle, 64.0, 64.0);
    const ui::PageView::LoupeRegion part = viewer.view->loupeRegionFor(straddle);
    REQUIRE(part.request.isValid());
    INFO("tile " << part.request.tile.x << "," << part.request.tile.y << " " << part.request.tile.width
                 << "x" << part.request.tile.height << "  widget right " << part.widget.right()
                 << " page right " << rightEdge);
    CHECK(part.request.tile.width <= static_cast<int>(std::ceil(20.0 * dpr * ui::LoupeTool::kDefaultMagnification)) + 2);
    CHECK(part.widget.right() <= rightEdge + 0.5);

    // ENTIRELY OVER THE DESK to the page's right: nothing to ask for.
    const QRectF desk(rightEdge + 10.0, middle, 64.0, 64.0);
    CHECK_FALSE(viewer.view->loupeRegionFor(desk).request.isValid());
}

// =============================================================================================
// S4 guide A-101 -- Alt and the wheel step the Loupe's magnification (D4-22)
// =============================================================================================

namespace {

// One wheel event on the viewport, `notches` of them in one delta, on either axis.
void wheel(ui::PageView* view, QPoint at, int notches, Qt::KeyboardModifiers modifiers,
           bool horizontal = false)
{
    const QPoint delta = horizontal ? QPoint(120 * notches, 0) : QPoint(0, 120 * notches);
    QWheelEvent event(QPointF(at), QPointF(view->viewport()->mapToGlobal(at)), QPoint(), delta,
                      Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(view->viewport(), &event);
    QCoreApplication::processEvents();
}

} // namespace

TEST_CASE("Alt and the wheel step the loupe's magnification and move nothing else", "[tools][D4-22]")
{
    // The user, during M-12: *"Use alt+wheel to adjust zoom level of lens"*. A ladder, one rung a
    // notch, clamped; the page does not move; and the sharp lens asks at the new scale.
    Viewer viewer;
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::ToolLoupe, core::KeyContext::Viewer));
    driveLayout(viewer.window.get());
    viewer.fixture.pumpTiles();

    const ui::LoupeTool& loupe = viewer.tools().loupe();
    REQUIRE(loupe.magnification() == ui::LoupeTool::kDefaultMagnification);

    const QPoint at(320, 300);
    viewer.mouse(QEvent::MouseMove, at, Qt::NoButton);
    const core::DevicePoint scroll = viewer.controller().scroll();
    const double zoom = viewer.controller().zoom().factor;

    // (1) AWAY IS MORE, TOWARDS IS LESS, one rung a notch.
    wheel(viewer.view, at, +1, Qt::AltModifier);
    CHECK(loupe.magnification() == 3.0);
    wheel(viewer.view, at, +1, Qt::AltModifier);
    CHECK(loupe.magnification() == 4.0);
    wheel(viewer.view, at, -1, Qt::AltModifier);
    CHECK(loupe.magnification() == 3.0);

    // (3) A HORIZONTAL notch -- how Qt reports an Alt+wheel on some platforms -- steps the same.
    wheel(viewer.view, at, +1, Qt::AltModifier, true);
    CHECK(loupe.magnification() == 4.0);

    // (2) CLAMPED at both ends.
    wheel(viewer.view, at, +10, Qt::AltModifier);
    CHECK(loupe.magnification() == 8.0);
    wheel(viewer.view, at, +1, Qt::AltModifier);
    CHECK(loupe.magnification() == 8.0);
    wheel(viewer.view, at, -20, Qt::AltModifier);
    CHECK(loupe.magnification() == 1.5);

    // (4) AND THE PAGE DID NOT MOVE.
    CHECK(viewer.controller().scroll().x == scroll.x);
    CHECK(viewer.controller().scroll().y == scroll.y);
    CHECK(viewer.controller().zoom().factor == zoom);

    // (5) THE SHARP LENS ASKS AT THE NEW SCALE, for the region the new magnification shows.
    wheel(viewer.view, at, +4, Qt::AltModifier);   // 1.5 -> 4
    REQUIRE(loupe.magnification() == 4.0);
    viewer.view->viewport()->update();
    waitMs(ui::PageView::kLoupeSettleMs + 120);
    const double span = ui::LoupeTool::kLensSize / 4.0;
    const ui::PageView::LoupeRegion region = viewer.view->loupeRegionFor(
        QRectF(at.x() - span / 2.0, at.y() - span / 2.0, span, span));
    REQUIRE(region.request.isValid());
    const ui::LoupeRaster& raster = viewer.view->loupeRaster();
    CHECK(raster.aimed() == region.request);
    CHECK(std::abs(raster.aimed().deviceScale
                   - viewer.view->layoutEngine().layout().presentedScale() * 4.0) < 1e-9);
}

TEST_CASE("Alt and the wheel leave the magnification alone when the loupe is not the tool",
          "[tools][D4-22]")
{
    // The control: the gesture is the Loupe's only while the Loupe is in effect. With the Hand armed
    // the wheel does what it always did, and the lens's magnification -- kept for when the Loupe is
    // armed again -- is untouched.
    Viewer viewer;
    viewer.press(Qt::Key_H);
    REQUIRE(viewer.tools().armed() == ui::ToolKind::Hand);

    wheel(viewer.view, QPoint(320, 300), +2, Qt::AltModifier);
    CHECK(viewer.tools().loupe().magnification() == ui::LoupeTool::kDefaultMagnification);
}
