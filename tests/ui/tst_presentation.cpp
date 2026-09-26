// Stage 4 T10 -- Full Screen and presentation. Guide cases A-57, A-58, A-59, A-60, A-61, A-62 and
// A-63 (`Stage4-ViewingSurface-Testing-Guide.md` §5.11), and decision **D4-12**
// (`Stage4-ViewingSurface-Plan.md` §3).
//
// The case this suite exists for is **A-58**, and it is an S1: a presentation that exits without
// pairing its `setPresentationModeActive(true)` leaves a machine that will not lock or sleep --
// silently, until the battery is flat. Every other case here is about something a person would
// notice; that one is about something nobody notices until it matters.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS SUITE CANNOT SEE, NAMED HERE RATHER THAN DISCOVERED (convention 19).
//
//   * **It cannot see a transition.** What a dissolve LOOKS like half way through is not a claim a
//     headless run can make. What is asserted is the RESOLUTION -- which style this page turns
//     with, the document's or the preference's -- and that an animation is running. Whether it
//     looks right, and whether it is smooth on a projector, is manual case **M-10**.
//   * **It cannot unplug a monitor.** `FakeTopology` is driven by hand and `QGuiApplication`'s
//     screen list under the offscreen platform holds exactly one screen, so A-59's and A-60's
//     assertions are about what the CONTROLLER does when told, not about what Windows reports.
//     The real thing is **M-04**, which precondition P-2 exists for.
//   * **It cannot judge the blank.** A-62 asserts the overlay's state and that the page beneath is
//     unchanged, not that the projector went black.
//   * **No real engine.** `uitest::FakeBackend` decides the page count and the `/Trans` a document
//     carries -- there is no PDFium here to read a real one from, and the READER over the real
//     corpus file is `tests/render/`'s.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/PageTransitions.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "tests/ui/ShellFixture.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/FullScreenController.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/PresentationSurface.h"
#include "ui/viewer/ViewportController.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QCryptographicHash>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLayout>

#include <memory>
#include <utility>

// I-14's case reads the PHYSICAL window rectangle, which only Win32 reports.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace rawpage;
using core::ActionId;
using ui::FullScreenController;

namespace {

using Level = FullScreenController::Level;
using Blank = FullScreenController::Blank;

struct Fixture : uitest::ShellFixture {
    QString writePdf(const QString& name)
    {
        const QString path = QDir(directory.path()).filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("%PDF-1.7\n% the fake backend decides, not these bytes\n");
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

QByteArray hashOf(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex();
}

// A window with one document, ready to present.
struct Presenter {
    Fixture                         fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow*             document = nullptr;
    QString                         path;

    explicit Presenter(int pages = 6)
    {
        fixture.backend.pagesPerDocument = pages;
        path = fixture.writePdf(QStringLiteral("present.pdf"));

        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1000, 800);
        window->openFiles({path});
        driveLayout(window.get());
        window->raiseToForeground();

        document = window->currentDocument();
        REQUIRE(document != nullptr);
        REQUIRE(controller() != nullptr);
        REQUIRE(controller()->level() == Level::Off);
    }

    // A presenting window is deleted later and its surface holds a `PageView` over this fixture's
    // backend (`CLAUDE.md` -> Traps, S3 T10). Left pending past the fixture, the next case's first
    // `processEvents` runs it against a destroyed backend.
    ~Presenter()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    FullScreenController* controller() const { return window->fullScreen(); }

    uitest::FakePowerSession& power() { return fixture.powerSession(); }

    void present()
    {
        REQUIRE(fixture.actions.dispatch(ActionId::PresentStart, core::KeyContext::Global));
        QCoreApplication::processEvents();
        REQUIRE(controller()->isPresenting());
    }

    void dispatch(ActionId id, core::KeyContext context = core::KeyContext::FullScreen)
    {
        const core::Result<void> sent = fixture.actions.dispatch(id, context);
        INFO("refused: " << (sent ? std::string()
                                  : std::string(sent.error().messageKey.data(),
                                                sent.error().messageKey.size())));
        REQUIRE(sent);
        QCoreApplication::processEvents();
    }

    // Gives the document a per-page `/Trans`, which only the backend can answer for.
    void givePageTransition(int pageIndex, core::TransitionStyle style,
                            std::optional<double> duration = std::nullopt)
    {
        core::PageTransitions& model = fixture.backend.transitions();
        if (model.pageCount() == 0) model.setPageCount(fixture.backend.pagesPerDocument);
        core::PageTransition transition;
        transition.style = style;
        transition.rawStyle = QStringLiteral("Wipe");
        transition.durationSeconds = duration;
        model.setTransition(pageIndex, transition);
    }

    void givePageDuration(int pageIndex, double seconds)
    {
        core::PageTransitions& model = fixture.backend.transitions();
        if (model.pageCount() == 0) model.setPageCount(fixture.backend.pagesPerDocument);
        model.setTiming(pageIndex, core::PageTiming{seconds});
    }
};

} // namespace

// =============================================================================================
// A-57 -- every §8 Full Screen chord dispatches in the `FullScreen` context
// =============================================================================================

TEST_CASE("the presenting surface declares KeyContext::FullScreen and every present chord dispatches",
          "[presentation]")
{
    Presenter presenter;
    presenter.present();

    ui::PresentationSurface* surface = presenter.controller()->surface();
    REQUIRE(surface != nullptr);
    REQUIRE(surface->view() != nullptr);

    // THE CONTEXT IS ON THE PAGE SURFACE, NOT ON THE WINDOW, and this is the assertion that says
    // so. `activeContext()` takes the INNERMOST declared context and `PageView`'s constructor
    // declares `Viewer` on every instance -- so a declaration on the window would be shadowed and
    // every chord below would resolve as a Viewer one.
    surface->view()->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    CHECK(presenter.window->shortcutBinder()->activeContext() == core::KeyContext::FullScreen);

    // All seven rows of the table, each registered and each enabled while presenting. They were
    // DECLARED at Stage 1 with these chords and registered by nothing until T10, so the whole
    // table resolved `NotFound` and left its keys unconsumed.
    for (const ActionId id : {ActionId::PresentExit, ActionId::PresentBlankBlack,
                              ActionId::PresentBlankWhite, ActionId::PresentLaserPointer,
                              ActionId::PresentToggleCursor, ActionId::PresentAutoAdvance}) {
        INFO("action " << std::string(core::actionStringId(id)));
        CHECK(presenter.fixture.actions.isRegistered(id));
        CHECK(presenter.fixture.actions.isEnabled(id));
    }

    // `Ctrl+Shift+M` IS REFUSED WITH ONE MONITOR rather than enabled and doing nothing -- the
    // offscreen platform reports exactly one screen, which is also the commonest real machine.
    CHECK(presenter.fixture.actions.isRegistered(ActionId::PresentNextMonitor));
    CHECK_FALSE(presenter.fixture.actions.isEnabled(ActionId::PresentNextMonitor));
}

TEST_CASE("the presentation chords are refused when nothing is presenting", "[presentation]")
{
    Presenter presenter;

    // THE CONTROL. Registered-and-disabled, not unregistered: an id nobody registers leaves its
    // key unconsumed, which is right for a feature that does not exist and wrong for one that
    // exists and does not apply right now (Stage 1's I-53).
    for (const ActionId id : {ActionId::PresentBlankBlack, ActionId::PresentLaserPointer,
                              ActionId::PresentAutoAdvance, ActionId::PresentNextMonitor}) {
        INFO("action " << std::string(core::actionStringId(id)));
        CHECK(presenter.fixture.actions.isRegistered(id));
        CHECK_FALSE(presenter.fixture.actions.isEnabled(id));
    }

    // And `present.exit` is refused too, because there is nothing to leave -- but it is enabled at
    // EITHER level, which the next case is about.
    CHECK_FALSE(presenter.fixture.actions.isEnabled(ActionId::PresentExit));
    CHECK(presenter.fixture.actions.isEnabled(ActionId::PresentStart));
}

// =============================================================================================
// A-58 -- `setPresentationModeActive` true on entry, FALSE on every exit path
// =============================================================================================

TEST_CASE("entering either level asks the OS to hold the display awake", "[presentation]")
{
    Presenter presenter;
    REQUIRE(presenter.power().calls.empty());

    // LEVEL 1 CALLS IT TOO, and that is D4-12's deliberate choice rather than an oversight: a
    // person reading full-screen for ten minutes without touching the mouse is exactly whom a lock
    // screen interrupts, and the interface has one switch.
    presenter.controller()->toggleFullScreen(presenter.window.get());
    CHECK(presenter.controller()->level() == Level::FullScreen);
    REQUIRE(presenter.power().calls.size() == 1);
    CHECK(presenter.power().calls.front() == true);
    CHECK(presenter.power().active);

    presenter.controller()->exit();
    REQUIRE(presenter.power().calls.size() == 2);
    CHECK(presenter.power().calls.back() == false);
    CHECK_FALSE(presenter.power().active);
}

TEST_CASE("the display request is paired on EVERY exit path", "[presentation]")
{
    // Each section is one way out, and the claim is the same every time: the fake ends inactive
    // and the call sequence is exactly true-then-false. **A final-state check alone would not be
    // enough** -- a controller that asked twice and released once ends inactive and is still
    // leaking a request -- which is why the calls are counted.
    SECTION("the present.exit command")
    {
        Presenter presenter;
        presenter.present();
        presenter.dispatch(ActionId::PresentExit);
        CHECK(presenter.controller()->level() == Level::Off);
        CHECK(presenter.power().calls == std::vector<bool>{true, false});
        CHECK_FALSE(presenter.power().active);
    }

    SECTION("F11 out of a presentation leaves entirely rather than dropping a level")
    {
        Presenter presenter;
        presenter.present();
        presenter.controller()->toggleFullScreen(presenter.window.get());
        CHECK(presenter.controller()->level() == Level::Off);
        CHECK(presenter.power().calls == std::vector<bool>{true, false});
    }

    SECTION("the presentation window being closed by the OS")
    {
        Presenter presenter;
        presenter.present();
        REQUIRE(presenter.controller()->surface() != nullptr);

        // The path no chord reaches: `Alt+F4`, or the session ending.
        presenter.controller()->surface()->close();
        QCoreApplication::processEvents();
        CHECK(presenter.controller()->level() == Level::Off);
        CHECK(presenter.power().calls == std::vector<bool>{true, false});
    }

    SECTION("a topology change that removes the presenting monitor keeps it paired")
    {
        Presenter presenter;
        presenter.present();
        presenter.fixture.topology().notify();
        QCoreApplication::processEvents();

        // The presentation MOVES rather than exiting (A-60), so the request is still held -- and
        // that is the assertion, because a topology handler that tore down without pairing would
        // be the same S1 arriving through the one door no chord opens.
        CHECK(presenter.controller()->isPresenting());
        CHECK(presenter.power().calls == std::vector<bool>{true});
        CHECK(presenter.power().active);

        presenter.controller()->exit();
        CHECK(presenter.power().calls == std::vector<bool>{true, false});
    }

    SECTION("the window being destroyed while presenting")
    {
        Presenter presenter;
        presenter.present();
        REQUIRE(presenter.power().active);

        // THE LAST EXIT PATH, and the one that matters most: a controller destroyed while
        // presenting must still pair, or the machine is left unable to sleep by a process that no
        // longer exists. `~FullScreenController` calls `setLevel(Level::Off)` for this.
        uitest::FakePowerSession& power = presenter.power();
        presenter.window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        CHECK(power.calls == std::vector<bool>{true, false});
        CHECK_FALSE(power.active);
    }
}

TEST_CASE("an OS that refuses presentation mode does not stop the presentation", "[presentation]")
{
    Presenter presenter;
    presenter.power().refuse = true;

    presenter.present();
    CHECK(presenter.controller()->isPresenting());

    presenter.controller()->exit();
    CHECK(presenter.controller()->level() == Level::Off);

    // BOTH CALLS WERE STILL MADE. The refusal is logged and not acted on: a screen saver we could
    // not disable is a worse presentation, not a broken one, and refusing to present because the
    // OS said no would turn a warning into a feature that does not work.
    CHECK(presenter.power().calls == std::vector<bool>{true, false});
}

// =============================================================================================
// A-59, A-60 -- the monitor, and one that disappears
// =============================================================================================

TEST_CASE("a topology change moves the presentation rather than losing it", "[presentation]")
{
    Presenter presenter;
    presenter.present();

    const int page = 3;
    REQUIRE(presenter.controller()->goToPage(page));
    QCoreApplication::processEvents();

    presenter.fixture.topology().notify();
    QCoreApplication::processEvents();

    // STILL PRESENTING, AT THE SAME PAGE. A presentation on a monitor that does not exist is
    // unreachable -- the person cannot even press `Esc`, because they cannot see what has focus --
    // so it moves. Exiting would also be defensible and would lose the presenter's place
    // mid-sentence.
    CHECK(presenter.controller()->isPresenting());
    CHECK(presenter.controller()->currentPage() == page);
    REQUIRE(presenter.controller()->surface() != nullptr);
    CHECK(presenter.controller()->surface()->isVisible());
}

TEST_CASE("an unknown monitor name falls back to the primary and still presents", "[presentation]")
{
    Presenter presenter;

    // `fullScreen/monitor` STORES A `friendlyName`, never a `MonitorInfo::id` -- Stage 2's I-20.
    // `id` is the OS device name and a `QScreen` is named with the display's model, so comparing
    // those two never matches and the fallback answers plausibly and wrongly.
    presenter.fixture.settings.set(core::keys::fullScreen::monitor,
                                   QStringLiteral("a monitor nobody has"));
    presenter.present();

    CHECK(presenter.controller()->isPresenting());
    REQUIRE(presenter.controller()->surface() != nullptr);
    CHECK(presenter.controller()->surface()->screen() != nullptr);
}

// =============================================================================================
// I-14 -- `Ctrl+Shift+M` moved the presentation and kept its PHYSICAL size.
//
// Found by the user's M-02 walk, 2026-09-26: presenting on the 100 % monitor and moving to the 150 %
// one left a 2560 x 1440 surface in a corner of the 4K panel; the other way round, a 3840 x 2160
// surface overflowing the 2K one. The first placement was always right -- it happens before the
// surface is shown -- and every MOVE was wrong.
//
// Judged in PHYSICAL pixels through Win32, because Qt's logical geometry is Qt's own account of the
// window and the defect is the window and that account disagreeing. **What it does NOT cover**
// (convention 19): anything on the offscreen platform, which has ONE screen -- so under `ctest` this
// case SKIPS and says why, and its evidence is a run on the `windows` platform on a desktop with two
// monitors at different scale factors (`QT_QPA_PLATFORM=windows`, guide A-85). A topology change
// moves the surface by the same path and is not driven here: that needs a monitor unplugged (M-04).
// =============================================================================================

TEST_CASE("moving a presentation to a monitor of another scale refits it to that monitor",
          "[presentation][I-14]")
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    bool mixed = false;
    for (QScreen* screen : screens)
        if (!qFuzzyCompare(screen->devicePixelRatio(), screens.front()->devicePixelRatio())) mixed = true;
    if (screens.size() < 2 || !mixed)
        SKIP("needs two monitors at different scale factors -- run on QT_QPA_PLATFORM=windows (guide A-85)");

    Presenter presenter;
    presenter.present();
    ui::PresentationSurface* surface = presenter.controller()->surface();
    REQUIRE(surface != nullptr);

    // The physical rectangle of the surface, and of the monitor it is mostly on.
    const auto measure = [surface] {
        const HWND window = reinterpret_cast<HWND>(surface->winId());
        RECT frame{};
        REQUIRE(GetWindowRect(window, &frame));
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        REQUIRE(GetMonitorInfoW(monitor, &info));
        return std::make_pair(monitor, std::make_pair(frame, info.rcMonitor));
    };
    const auto fills = [](const RECT& frame, const RECT& monitor) {
        UNSCOPED_INFO("surface " << frame.left << "," << frame.top << " " << frame.right - frame.left
                      << "x" << frame.bottom - frame.top << " | monitor " << monitor.left << ","
                      << monitor.top << " " << monitor.right - monitor.left << "x"
                      << monitor.bottom - monitor.top);
        return frame.left == monitor.left && frame.top == monitor.top
               && frame.right == monitor.right && frame.bottom == monitor.bottom;
    };

    const auto settle = [] {
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
    };
    settle();

    // THE FIRST PLACEMENT -- the control: it was always right, because it happens before the
    // surface is shown. A case whose control fails is measuring something else.
    const auto first = measure();
    CHECK(fills(first.second.first, first.second.second));

    // EVERY MOVE, round the whole ring and back to the start, so both directions of scale are
    // crossed whichever monitor the presentation opened on.
    HMONITOR previous = first.first;
    for (qsizetype step = 0; step < screens.size(); ++step) {
        presenter.dispatch(ActionId::PresentNextMonitor);
        settle();
        const auto moved = measure();
        CHECK(moved.first != previous);   // it moved at all
        CHECK(fills(moved.second.first, moved.second.second));
        previous = moved.first;
    }
    CHECK(previous == first.first);   // the ring closes
}

// =============================================================================================
// A-61 -- transitions, auto-advance, loop
// =============================================================================================

TEST_CASE("a page's own transition overrides the preference, and a page without one does not",
          "[presentation]")
{
    Presenter presenter;
    presenter.fixture.settings.set(core::keys::fullScreen::transition, core::TransitionStyle::Fade);
    presenter.givePageTransition(2, core::TransitionStyle::Wipe);
    presenter.present();

    // **THE DOCUMENT WINS** -- `Features.md` §1.2 promises transitions *"with a per-page
    // override"*, and a preference cannot know what the author intended for one particular page.
    CHECK(presenter.controller()->transitionFor(2) == core::TransitionStyle::Wipe);

    // AND THE CONTROL, which is the half that makes the first assertion mean anything: a page with
    // no `/Trans` of its own falls through to the preference. A model that stored `None` for
    // "absent" would make the override unimplementable and both pages would read the same.
    CHECK(presenter.controller()->transitionFor(3) == core::TransitionStyle::Fade);
    CHECK(presenter.controller()->transitionFor(0) == core::TransitionStyle::Fade);
}

TEST_CASE("a page's own duration overrides the auto-advance preference", "[presentation]")
{
    Presenter presenter;
    presenter.fixture.settings.set(core::keys::fullScreen::autoAdvanceSec, 30);
    presenter.givePageDuration(1, 3.0);
    presenter.present();

    CHECK(presenter.controller()->autoAdvanceSecondsFor(1) == 3);
    CHECK(presenter.controller()->autoAdvanceSecondsFor(2) == 30);   // the control
}

TEST_CASE("auto-advance steps, and loop decides what happens after the last page", "[presentation]")
{
    Presenter presenter(4);
    presenter.present();
    REQUIRE(presenter.controller()->currentPage() == 0);

    presenter.dispatch(ActionId::PresentAutoAdvance);
    CHECK(presenter.controller()->autoAdvancing());

    // Stepped by hand rather than by waiting on the timer -- the back door is named on the method
    // and in this suite's header. What it does NOT cover is that the timer fires at all, which is
    // M-10's.
    presenter.controller()->advanceForTesting();
    CHECK(presenter.controller()->currentPage() == 1);

    presenter.controller()->advanceForTesting();
    presenter.controller()->advanceForTesting();
    CHECK(presenter.controller()->currentPage() == 3);

    // LOOP OFF STOPS AT THE LAST PAGE, and stopping means stopping the TIMER -- a presentation
    // that had reached the end and was still waking every few seconds would be a laptop that never
    // idles.
    REQUIRE_FALSE(presenter.fixture.settings.get(core::keys::fullScreen::loop));
    presenter.controller()->advanceForTesting();
    CHECK(presenter.controller()->currentPage() == 3);
    CHECK_FALSE(presenter.controller()->autoAdvancing());

    // LOOP ON RESTARTS.
    presenter.fixture.settings.set(core::keys::fullScreen::loop, true);
    presenter.dispatch(ActionId::PresentAutoAdvance);
    presenter.controller()->advanceForTesting();
    CHECK(presenter.controller()->currentPage() == 0);
}

TEST_CASE("paging works from either end and refuses to go past it", "[presentation]")
{
    Presenter presenter(5);
    presenter.present();

    CHECK(presenter.controller()->nextPage());
    CHECK(presenter.controller()->currentPage() == 1);
    CHECK(presenter.controller()->lastPage());
    CHECK(presenter.controller()->currentPage() == 4);

    // THE CONTROL at both ends: a step past the document is refused rather than clamped silently,
    // so a caller can tell "the document ended" from "it moved".
    CHECK_FALSE(presenter.controller()->nextPage());
    CHECK(presenter.controller()->firstPage());
    CHECK_FALSE(presenter.controller()->prevPage());
    CHECK(presenter.controller()->currentPage() == 0);
}

// =============================================================================================
// A-62 -- the blank, the laser and the cursor
// =============================================================================================

TEST_CASE("B and W blank and restore, and each toggles off the other", "[presentation]")
{
    Presenter presenter;
    presenter.present();
    REQUIRE(presenter.controller()->goToPage(2));
    REQUIRE(presenter.controller()->blank() == Blank::None);

    presenter.dispatch(ActionId::PresentBlankBlack);
    CHECK(presenter.controller()->blank() == Blank::Black);

    // A SECOND PRESS RESTORES THE EXACT PAGE -- blanking is not navigation, and the page under the
    // blank never moved.
    presenter.dispatch(ActionId::PresentBlankBlack);
    CHECK(presenter.controller()->blank() == Blank::None);
    CHECK(presenter.controller()->currentPage() == 2);

    presenter.dispatch(ActionId::PresentBlankWhite);
    CHECK(presenter.controller()->blank() == Blank::White);
    presenter.dispatch(ActionId::PresentBlankWhite);
    CHECK(presenter.controller()->blank() == Blank::None);
    CHECK(presenter.controller()->currentPage() == 2);
}

TEST_CASE("the laser and the cursor toggle, and leaving clears the blank", "[presentation]")
{
    Presenter presenter;
    presenter.present();

    REQUIRE_FALSE(presenter.controller()->laserPointer());
    presenter.dispatch(ActionId::PresentLaserPointer);
    CHECK(presenter.controller()->laserPointer());
    CHECK(presenter.controller()->surface()->laserPointer());

    // `fullScreen/showCursor` ships ON, and `Ctrl+L` is what changes it at runtime -- which is why
    // the key is a DEFAULT rather than the state.
    CHECK(presenter.controller()->cursorVisible());
    presenter.dispatch(ActionId::PresentToggleCursor);
    CHECK_FALSE(presenter.controller()->cursorVisible());

    // LEAVING RESETS ALL OF IT. A presentation re-entered with the laser still on and the screen
    // still black would look broken, and the state belongs to the presentation rather than to the
    // window.
    presenter.dispatch(ActionId::PresentBlankBlack);
    presenter.controller()->exit();
    CHECK(presenter.controller()->blank() == Blank::None);
    CHECK_FALSE(presenter.controller()->laserPointer());
}

TEST_CASE("fullScreen/showCursor is read when the presentation starts", "[presentation]")
{
    Presenter presenter;
    presenter.fixture.settings.set(core::keys::fullScreen::showCursor, false);
    presenter.present();
    CHECK_FALSE(presenter.controller()->cursorVisible());
}

// =============================================================================================
// A-63 -- a presentation changes no document
// =============================================================================================

TEST_CASE("a full presentation pass changes no document and leaves the journal empty",
          "[presentation]")
{
    Presenter presenter(5);
    const QByteArray before = hashOf(presenter.path);

    presenter.present();

    // Everything a presenter can do, in one pass.
    presenter.controller()->nextPage();
    presenter.dispatch(ActionId::PresentBlankBlack);
    presenter.dispatch(ActionId::PresentBlankBlack);
    presenter.dispatch(ActionId::PresentLaserPointer);
    presenter.dispatch(ActionId::PresentToggleCursor);
    presenter.dispatch(ActionId::PresentAutoAdvance);
    presenter.controller()->advanceForTesting();
    presenter.controller()->lastPage();
    presenter.dispatch(ActionId::PresentBlankWhite);
    presenter.controller()->exit();
    QCoreApplication::processEvents();

    // `ui/`'s ENTIRE mutation vocabulary is *construct a `Command`*, and this class constructs
    // none -- so the claim is asserted on the two things that would show it if it did: the file's
    // bytes, and the journal that would hold the step.
    CHECK(hashOf(presenter.path) == before);
    REQUIRE(presenter.document->journal() != nullptr);
    CHECK_FALSE(presenter.document->journal()->canUndo());
    CHECK_FALSE(presenter.document->isDirty());
}

// =============================================================================================
// Level 1, and the two levels not being the same thing
// =============================================================================================

TEST_CASE("level 1 keeps the viewer's context and level 2 does not", "[presentation]")
{
    Presenter presenter;

    presenter.controller()->toggleFullScreen(presenter.window.get());
    CHECK(presenter.controller()->level() == Level::FullScreen);

    // LEVEL 1 KEEPS THE VIEWER'S TOOLS AND ITS CONTEXT (D4-12): it is the window with no chrome,
    // not a presentation, and there is no surface at all.
    CHECK(presenter.controller()->surface() == nullptr);
    presenter.document->view()->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    CHECK(presenter.window->shortcutBinder()->activeContext() == core::KeyContext::Viewer);

    // And the presentation chords are refused at level 1 -- `present.exit` excepted, which is the
    // way out of both.
    CHECK_FALSE(presenter.fixture.actions.isEnabled(ActionId::PresentLaserPointer));
    CHECK(presenter.fixture.actions.isEnabled(ActionId::PresentExit));

    presenter.controller()->exit();
    CHECK(presenter.controller()->level() == Level::Off);
}

TEST_CASE("presenting is refused with no document", "[presentation]")
{
    Fixture fixture;
    fixture.backend.pagesPerDocument = 0;
    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    driveLayout(&window);
    window.raiseToForeground();

    // A presentation of nothing is a black rectangle with no obvious way out, so the command is
    // offered as unavailable rather than entered and immediately useless.
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::PresentStart));
    CHECK_FALSE(window.fullScreen()->enterPresentation(&window, nullptr));
    CHECK(window.fullScreen()->level() == Level::Off);
    CHECK(fixture.powerSession().calls.empty());
}
