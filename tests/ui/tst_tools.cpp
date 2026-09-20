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
#include "ui/viewer/PageView.h"
#include "ui/viewer/ToolController.h"
#include "ui/viewer/ViewportController.h"
#include "ui/viewer/tools/LoupeTool.h"
#include "ui/viewer/tools/MarqueeZoomTool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
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
    // cursor, and a drag with it does nothing at all -- text selection needs
    // `core/text/PageTextModel` and `ITextExtractor`, which are STAGE 5's. The backend census at
    // every launch says `missing: ITextExtractor`, so this is the honest state and not a defect.
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
