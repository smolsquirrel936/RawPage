// Full Screen and Presentation -- ONE controller with TWO levels. Stage 4 T10,
// `Architecture-FileMap.md` §3.14, decision **D4-12** (`Stage4-ViewingSurface-Plan.md` §3).
//
// ===========================================================================================
// TWO LEVELS AND ONE CONTROLLER, BECAUSE THERE IS ONE SCREEN SAVER.
// ===========================================================================================
//   * **Level 1 -- Full Screen** (`F11`, `view.fullScreen`): the window goes full-screen and the
//     chrome goes with it. The viewer keeps its tools, its chords and `KeyContext::Viewer`.
//   * **Level 2 -- Presentation**: a full-screen surface of its own on a chosen monitor, with
//     transitions, auto-advance, blank-to-black/white and a laser pointer, declaring
//     `KeyContext::FullScreen`.
//
// Two controllers would mean two answers to *"is the screen saver allowed to start"*, and the
// window state is already one thing at the OS level. **Entering EITHER level calls
// `IPowerSession::setPresentationModeActive(true)`** -- its first caller in the product, declared
// at Stage 2 T12 and called by nothing until now -- and leaving calls it with `false`. The call on
// level 1 as well as level 2 is deliberate: a person reading full-screen for ten minutes without
// touching the mouse is exactly whom a lock screen interrupts, and the interface has one switch.
//
// ===========================================================================================
// THE `false` IS THE S1 THIS CLASS MOST PLAUSIBLY SHIPS, SO IT IS STRUCTURAL.
// ===========================================================================================
// A presentation that exits without pairing its `true` leaves a machine that will not lock or
// sleep -- silently, until the battery is flat. Guide case **A-58** drives every exit path
// (`Esc`, `F11`, the window closing, a topology change, an exception on the way out) and asserts
// the fake's final state is inactive.
//
// So the pairing is **not** a matched pair of calls at each call site. `applyPowerState()` is
// called from ONE place, `setLevel()`, which is the only function that writes `level_`; it derives
// the wanted state from the level and only calls the platform when it DIFFERS from what was last
// asked for. Every exit is therefore a `setLevel(Level::Off)`, including the destructor's, and
// there is no path that changes the level without going through it. An exception thrown inside the
// exit work cannot skip it either, because `setLevel` applies the power state BEFORE the work that
// could throw.
//
// ===========================================================================================
// THE MONITOR IS JOINED BY `friendlyName`, WHICH IS STAGE 2's I-20 WRITTEN DOWN.
// ===========================================================================================
// `MonitorInfo::id` is the OS device name (`\\.\DISPLAY1`) and a `QScreen` is named with the
// display's MODEL (`MAG 272URDF`), so the two can never match -- `ui/viewer/` compared them for
// the whole of Stage 2 and the join never once succeeded, costing nothing visible because the
// fallback answered plausibly and wrongly. `fullScreen/monitor` therefore stores the
// `friendlyName`, and an unknown or absent monitor falls back to the primary **with one log line**
// rather than silently.
//
// A TOPOLOGY CHANGE WHILE PRESENTING MOVES THE PRESENTATION rather than leaving it on a monitor
// that is gone (guide case A-60): a presentation on a display that does not exist is unreachable,
// and the person cannot even press `Esc` because they cannot see what has focus.
//
// ===========================================================================================
// IT PRESENTS INTO ITS OWN SURFACE, OVER THE SAME DOCUMENT.
// ===========================================================================================
// Level 2 builds a top-level window holding a `PageView` of its own, handed the SAME
// `DocumentHandle`, revision and page trees the tab already has -- the same *N views, one
// document* shape as D4-13's split, for the same reasons. It is a separate window rather than the
// tab's own pane because `fullScreen/monitor` and `Ctrl+Shift+M` place the presentation on a
// chosen display, and moving the tab's pane would drag the whole application window with it.
//
// **It never mutates**: `ui/`'s entire mutation vocabulary is *construct a `Command`*, and this
// class constructs none -- guide case **A-63** asserts the file's hash is unchanged and the
// journal empty after a full pass including blanks and the laser.

#ifndef RAWPAGE_UI_VIEWER_FULLSCREENCONTROLLER_H
#define RAWPAGE_UI_VIEWER_FULLSCREENCONTROLLER_H

#include "app/Services.h"
#include "core/Transition.h"

#include <QObject>
#include <QPointer>
#include <QString>

class QScreen;

namespace rawpage::ui {

class DocumentWindow;
class MainWindow;
class PresentationSurface;

class FullScreenController : public QObject {
    Q_OBJECT

public:
    enum class Level {
        Off,
        FullScreen,     // level 1 -- the window, no chrome, the viewer's own context and tools
        Presentation    // level 2 -- its own surface, `KeyContext::FullScreen`, transitions
    };

    // What `B` and `W` do. `None` is showing the page.
    enum class Blank { None, Black, White };

    explicit FullScreenController(app::Services services, QObject* parent = nullptr);
    ~FullScreenController() override;

    Level level() const noexcept { return level_; }
    bool  isActive() const noexcept { return level_ != Level::Off; }
    bool  isPresenting() const noexcept { return level_ == Level::Presentation; }

    // ---- Entering and leaving ------------------------------------------------------------------
    // Level 1. `view.fullScreen` (`F11`), which Stage 1 registered against
    // `MainWindow::toggleFullScreen` -- *"the WINDOW STATE ONLY"*, as that function said of itself.
    // The id is unchanged; what it reaches is now this.
    void toggleFullScreen(MainWindow* window);

    // Level 2. Needs a document: a presentation of nothing is a black rectangle with no way out
    // that is obvious. Returns false and logs when there is none.
    bool enterPresentation(MainWindow* window, DocumentWindow* document);

    // EVERY exit path ends here, including the destructor's -- see the header on why that matters
    // more than it looks.
    void exit();

    // ---- The §8 Full Screen table (T10) ----------------------------------------------------------
    // Each is the BEHAVIOUR behind one registered command and a public method rather than a lambda
    // inside the registration, for D1-7's reason: a behaviour that exists only inside a handler
    // cannot be driven by a test, by the Stage 14 batch runner, or by the next surface that wants it.
    void setBlank(Blank blank);
    void toggleBlack();                 // `B`
    void toggleWhite();                 // `W`
    Blank blank() const noexcept { return blank_; }

    void toggleLaserPointer();          // `P`
    bool laserPointer() const noexcept { return laser_; }

    void toggleCursorVisible();         // `Ctrl+L`
    bool cursorVisible() const noexcept { return cursorVisible_; }

    void toggleAutoAdvance();           // `Ctrl+Shift+H`
    bool autoAdvancing() const noexcept { return autoAdvance_; }

    void moveToNextMonitor();           // `Ctrl+Shift+M`

    // Paging, so the §8 `nav.*` chords bound to `FullScreen` reach the presenting surface rather
    // than the tab behind it. Returns false when there is nothing to present.
    bool goToPage(int pageIndex);
    bool nextPage();
    bool prevPage();
    bool firstPage();
    bool lastPage();
    int  currentPage() const;

    // The surface, for a case that needs to look at it. Null unless presenting.
    PresentationSurface* surface() const noexcept;

    // The transition this page arrives with: the document's own `/Trans` if it has one, else
    // `fullScreen/transition`. **The document wins**, which is the override `Features.md` §1.2
    // promises and guide case A-61 asserts -- a preference cannot know what the author intended
    // for one particular page. Public because it is the resolution itself that is worth asserting,
    // not the animation it produces.
    core::TransitionStyle transitionFor(int pageIndex) const;

    // How long the page is shown before auto-advance moves on: the page's own `/Dur` if it has
    // one, else `fullScreen/autoAdvanceSec`. Same precedence and the same reason.
    int autoAdvanceSecondsFor(int pageIndex) const;

    // Drives one auto-advance step without waiting for the timer. **A test-only door, named as
    // one** (convention 19): what it does NOT cover is that the timer fires at all, which is
    // manual case M-10's and is why A-61 measures the interval rather than assuming it.
    void advanceForTesting();

signals:
    void levelChanged(Level level);
    void blankChanged(Blank blank);

private:
    // THE ONLY WRITER OF `level_`, and therefore the only place the power state is paired. See the
    // header: every exit is a `setLevel(Level::Off)`, and the power call happens before any work
    // that could throw.
    void setLevel(Level level);
    void applyPowerState();

    void buildSurface(DocumentWindow* document);
    void teardownSurface();

    // The `QScreen` for `fullScreen/monitor`, joined by `friendlyName` (I-20). Null falls back to
    // the window's own screen, and an unknown name logs once.
    QScreen* chosenScreen(const QString& friendlyName) const;
    void     placeOnScreen(QScreen* screen);
    // A MOVE of a surface already on screen -- `Ctrl+Shift+M` and a topology change. Hides, places,
    // shows: a full-screen window re-targeted in place keeps its physical size across a change of
    // scale (I-14).
    void     moveSurfaceTo(QScreen* screen);

    void onTopologyChanged();
    void restartAutoAdvance();

    app::Services services_;
    Level         level_ = Level::Off;

    // What was last ASKED of the platform, so `applyPowerState` can be called freely and only
    // calls out when the answer changes. `false` at construction because nothing has been asked.
    bool powerRequested_ = false;

    QPointer<MainWindow>    window_;
    QPointer<DocumentWindow> document_;
    PresentationSurface*    surface_ = nullptr;

    Blank blank_ = Blank::None;
    bool  laser_ = false;
    bool  cursorVisible_ = true;
    bool  autoAdvance_ = false;
    int   autoAdvanceTimer_ = 0;

    // Whether level 1 put the window into full screen, so leaving restores what was there before
    // rather than assuming the window was normal.
    bool restoreFromFullScreen_ = false;

    // Said ONCE per session, the first time `fullScreen/monitor` names a display that is not
    // there. A line per page turn would be a log nobody reads.
    mutable bool monitorMissingReported_ = false;

    void timerEvent(QTimerEvent* event) override;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_FULLSCREENCONTROLLER_H
