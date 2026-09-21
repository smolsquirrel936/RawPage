#include "ui/viewer/FullScreenController.h"

#include "core/Log.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/doc/PageTransitions.h"
#include "core/settings/Settings.h"
#include "platform/platform.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/PresentationSurface.h"
#include "ui/viewer/ViewportController.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTimerEvent>

namespace rawpage::ui {
namespace {

using core::LogSubsystem;

} // namespace

FullScreenController::FullScreenController(app::Services services, QObject* parent)
    : QObject(parent), services_(services)
{
}

FullScreenController::~FullScreenController()
{
    // THE LAST EXIT PATH, and the one no chord reaches. A controller destroyed while presenting --
    // the window closing, the application quitting -- must still pair its
    // `setPresentationModeActive(true)`, or the machine is left unable to sleep by a process that
    // no longer exists. `setLevel` is what pairs it and this is why it is called here rather than
    // relied upon.
    setLevel(Level::Off);
}

// =============================================================================================
// The level, and the power state that is derived from it
// =============================================================================================

void FullScreenController::setLevel(Level level)
{
    if (level == level_) return;

    const Level previous = level_;
    level_ = level;

    // THE POWER STATE FIRST, BEFORE ANY WORK THAT COULD THROW. That ordering is the whole of guide
    // case A-58's control: an exception raised while tearing a surface down must not be able to
    // skip the `false`, and the only way to guarantee that without a scope guard at every call
    // site is to have made the call already.
    applyPowerState();

    if (level_ == Level::Off) {
        // Leaving BOTH levels. The presentation surface goes; the window comes back out of full
        // screen only if this controller was what put it there.
        teardownSurface();

        if (restoreFromFullScreen_ && window_) {
            window_->setWindowState(window_->windowState() & ~Qt::WindowFullScreen);
            window_->raiseToForeground();
        }
        restoreFromFullScreen_ = false;

        blank_ = Blank::None;
        laser_ = false;
        autoAdvance_ = false;
        if (autoAdvanceTimer_ != 0) {
            killTimer(autoAdvanceTimer_);
            autoAdvanceTimer_ = 0;
        }
        document_ = nullptr;
    }

    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "fullScreen: level" << static_cast<int>(previous) << "->" << static_cast<int>(level_);

    emit levelChanged(level_);
}

void FullScreenController::applyPowerState()
{
    // **ONE CALLER, ONE PLACE** -- see the header. A matched pair of calls at each entry and exit
    // is the shape that leaves a machine unable to sleep the first time somebody adds a third exit
    // path and forgets one half of it.
    const bool wanted = level_ != Level::Off;
    if (wanted == powerRequested_) return;
    powerRequested_ = wanted;

    platform::IPowerSession* power = services_.platform.powerSession.get();
    if (power == nullptr) {
        // A NULL MEMBER IS THE HONEST STATE of an unimplemented interface (convention 6), not an
        // error: this build simply cannot ask, and a presentation still runs.
        return;
    }

    if (const core::Status asked = power->setPresentationModeActive(wanted); !asked) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "fullScreen: the OS refused presentation mode" << wanted << "--"
            << asked.error().messageKey;
    }
}

// =============================================================================================
// Level 1 -- the window
// =============================================================================================

void FullScreenController::toggleFullScreen(MainWindow* window)
{
    if (window == nullptr) return;

    // FROM A PRESENTATION, `F11` LEAVES ENTIRELY rather than dropping to level 1. §8 binds it to
    // *Leave Full Screen* in the `FullScreen` context and to *Enter / leave* in `Global`, which is
    // one chord meaning two things in two contexts -- exactly what `KeyContext` is for.
    if (level_ == Level::Presentation) {
        exit();
        return;
    }

    if (level_ == Level::FullScreen) {
        exit();
        return;
    }

    window_ = window;
    restoreFromFullScreen_ = !(window->windowState() & Qt::WindowFullScreen);
    window->setWindowState(window->windowState() | Qt::WindowFullScreen);
    setLevel(Level::FullScreen);
}

// =============================================================================================
// Level 2 -- the presentation
// =============================================================================================

bool FullScreenController::enterPresentation(MainWindow* window, DocumentWindow* document)
{
    if (window == nullptr || document == nullptr || document->pageCount() <= 0) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "fullScreen: refusing to present with no document";
        return false;
    }
    if (level_ == Level::Presentation) return true;

    window_ = window;
    document_ = document;

    buildSurface(document);
    setLevel(Level::Presentation);

    // THE SETTINGS ARE READ WHEN THE PRESENTATION STARTS, not held. A person who changes
    // `fullScreen/showCursor` mid-presentation has `Ctrl+L`; a preference re-read on every page
    // turn would make the runtime toggle disagree with the table on the next page.
    cursorVisible_ = services_.settings.get(core::keys::fullScreen::showCursor);
    if (surface_) surface_->setCursorVisible(cursorVisible_);

    return true;
}

void FullScreenController::buildSurface(DocumentWindow* document)
{
    // **PARENTED TO THE WINDOW, and that is what makes the §8 chords resolve at all.**
    // `ShortcutBinder` is scoped to one window and answers for the focus inside it; a top-level
    // surface with NO parent is a window the binder cannot see, so every Full Screen chord pressed
    // in it would resolve as though nothing were focused. Parenting makes it a window this one
    // OWNS, which is the relation `ShortcutBinder::belongsToScope` was given at T10 to express.
    //
    // It is still a `Qt::Window` -- a real top-level that can be placed on another display --
    // rather than a child widget, which is what `fullScreen/monitor` and `Ctrl+Shift+M` need.
    surface_ = new PresentationSurface(services_.renderScheduler,
                                       services_.platform.displayTopology.get(), window_);
    surface_->setAttribute(Qt::WA_DeleteOnClose, false);   // this class owns it, not the OS

    // THE SAME DOCUMENT, NOT A SECOND HANDLE -- D4-13's shape, reached by the second consumer
    // after the split. The MODEL's pages for layout and the ENGINE's for each tile's rotation
    // delta (D3-15), taken from the tab's journal exactly as `DocumentWindow` does.
    PageView* view = surface_->view();
    if (core::CommandJournal* journal = document->journal()) {
        view->setDocument(document->handle(), journal->revision(), &journal->document().pages(),
                          services_.backend.pages(document->handle()));
    } else {
        view->setDocument(document->handle(), document->revision(),
                          services_.backend.pages(document->handle()));
    }

    // A PRESENTED PAGE IS ONE PAGE, FIT TO THE DISPLAY. `Features.md` §1.2: *"one page filling the
    // display, no chrome"* -- so the layout mode is `Single` regardless of what the tab is in, and
    // the fit is the page rather than its width, because a page fitted to the width of a projector
    // runs off the bottom.
    view->setLayoutMode(core::render::LayoutMode::Single);
    view->setFitMode(core::render::FitMode::Page);
    view->goToPage(document->view() ? document->view()->controller().currentPage() : 0);

    connect(surface_, &PresentationSurface::closed, this, [this] { exit(); });

    // **THE CONTEXT IS DECLARED ON THE PAGE SURFACE ITSELF, NOT ON THE WINDOW.**
    // `activeContext()` walks outward from the focused widget and takes the INNERMOST declared
    // context -- and `PageView`'s constructor declares `Viewer` on every instance. A declaration on
    // the window would therefore be shadowed by the view inside it and every §8 Full Screen chord
    // would resolve as a Viewer one, which is I-06's shape in a new place: the chord would be
    // consumed by the wrong command rather than left alone.
    ShortcutBinder::declareContext(view, core::KeyContext::FullScreen);

    placeOnScreen(chosenScreen(services_.settings.get(core::keys::fullScreen::monitor)));
    surface_->showFullScreen();
    view->setFocus(Qt::OtherFocusReason);

    // A TOPOLOGY CHANGE MOVES THE PRESENTATION rather than losing it (guide case A-60). Subscribed
    // only while presenting: a handler left installed would fire for the rest of the session.
    if (platform::IDisplayTopology* topology = services_.platform.displayTopology.get()) {
        topology->subscribe([this] { onTopologyChanged(); });
    }
}

void FullScreenController::teardownSurface()
{
    if (surface_ == nullptr) return;

    // THE VIEW GOES BACK TO BEING A VIEWER before anything else, so a chord arriving between here
    // and the delete cannot resolve in a context whose surface is being destroyed.
    if (PageView* view = surface_->view())
        ShortcutBinder::declareContext(view, core::KeyContext::Viewer);

    disconnect(surface_, nullptr, this, nullptr);

    // `deleteLater`, for the reason `ui/viewer/SplitViewContainer` records at length: this runs
    // from inside `ShortcutBinder`'s application event filter while a key press is still being
    // delivered to the very widget being destroyed. Hidden and detached first, so its tickets and
    // its pixels are gone now.
    surface_->hide();
    surface_->setParent(nullptr);
    surface_->deleteLater();
    surface_ = nullptr;
}

void FullScreenController::exit() { setLevel(Level::Off); }

PresentationSurface* FullScreenController::surface() const noexcept { return surface_; }

// =============================================================================================
// The monitor
// =============================================================================================

QScreen* FullScreenController::chosenScreen(const QString& friendlyName) const
{
    if (friendlyName.isEmpty())
        return window_ ? window_->screen() : QGuiApplication::primaryScreen();

    // **JOINED BY `friendlyName`, NEVER BY `MonitorInfo::id`** -- Stage 2's I-20. `id` is the OS
    // device name (`\\.\DISPLAY1`) and a `QScreen` is named with the display's model, so comparing
    // those two never matches and the fallback answers plausibly and wrongly.
    for (QScreen* screen : QGuiApplication::screens())
        if (screen->name() == friendlyName) return screen;

    // ONE LINE, ONCE. A monitor that was unplugged since the preference was set is an ordinary
    // thing; a log line per page turn about it is not.
    if (!monitorMissingReported_) {
        monitorMissingReported_ = true;
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "fullScreen: no monitor named" << friendlyName
            << "-- presenting on the primary instead";
    }
    return QGuiApplication::primaryScreen();
}

void FullScreenController::placeOnScreen(QScreen* screen)
{
    if (surface_ == nullptr || screen == nullptr) return;
    surface_->setScreen(screen);
    surface_->setGeometry(screen->geometry());
}

void FullScreenController::moveToNextMonitor()
{
    if (surface_ == nullptr) return;

    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.size() < 2) return;      // nothing to move to, and not an error

    const qsizetype at = screens.indexOf(surface_->screen());
    QScreen* next = screens.at((at < 0 ? 0 : at + 1) % screens.size());
    placeOnScreen(next);
    surface_->showFullScreen();
}

void FullScreenController::onTopologyChanged()
{
    if (level_ != Level::Presentation || surface_ == nullptr) return;

    // IS THE SCREEN WE ARE ON STILL THERE? A presentation on a monitor that no longer exists is
    // unreachable -- the person cannot even press `Esc`, because they cannot see what has focus.
    // So it MOVES rather than exiting: exiting would be correct and would also lose the place in
    // the document, and the presenter is mid-sentence.
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.contains(surface_->screen())) return;

    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "fullScreen: the presenting monitor is gone; moving to the primary";
    placeOnScreen(QGuiApplication::primaryScreen());
    surface_->showFullScreen();
    if (PageView* view = surface_->view()) view->setFocus(Qt::OtherFocusReason);
}

// =============================================================================================
// The §8 Full Screen commands
// =============================================================================================

void FullScreenController::setBlank(Blank blank)
{
    if (blank_ == blank) return;
    blank_ = blank;
    if (surface_) surface_->setBlank(static_cast<int>(blank));
    emit blankChanged(blank_);
}

void FullScreenController::toggleBlack()
{
    setBlank(blank_ == Blank::Black ? Blank::None : Blank::Black);
}

void FullScreenController::toggleWhite()
{
    setBlank(blank_ == Blank::White ? Blank::None : Blank::White);
}

void FullScreenController::toggleLaserPointer()
{
    laser_ = !laser_;
    if (surface_) surface_->setLaserPointer(laser_);
}

void FullScreenController::toggleCursorVisible()
{
    cursorVisible_ = !cursorVisible_;
    if (surface_) surface_->setCursorVisible(cursorVisible_);
}

void FullScreenController::toggleAutoAdvance()
{
    autoAdvance_ = !autoAdvance_;
    restartAutoAdvance();
}

void FullScreenController::restartAutoAdvance()
{
    if (autoAdvanceTimer_ != 0) {
        killTimer(autoAdvanceTimer_);
        autoAdvanceTimer_ = 0;
    }
    if (!autoAdvance_ || level_ != Level::Presentation) return;

    // THE INTERVAL IS RE-DERIVED FOR THE PAGE NOW SHOWING, because a page's own `/Dur` overrides
    // the preference and differs per page. A timer started once at the interval of page 1 would
    // hold every page for page 1's time.
    autoAdvanceTimer_ = startTimer(autoAdvanceSecondsFor(currentPage()) * 1000);
}

void FullScreenController::timerEvent(QTimerEvent* event)
{
    if (event->timerId() != autoAdvanceTimer_) {
        QObject::timerEvent(event);
        return;
    }
    advanceForTesting();
}

void FullScreenController::advanceForTesting()
{
    if (level_ != Level::Presentation) return;

    const int page = currentPage();
    const int last = document_ ? document_->pageCount() - 1 : 0;

    if (page >= last) {
        // LOOP RESTARTS, OFF STOPS -- and stopping means stopping the TIMER too, not merely
        // declining to turn the page: a presentation that had reached the end and was still
        // waking every few seconds would be a laptop that never idles.
        if (!services_.settings.get(core::keys::fullScreen::loop)) {
            autoAdvance_ = false;
            restartAutoAdvance();
            return;
        }
        goToPage(0);
        return;
    }
    goToPage(page + 1);
}

// =============================================================================================
// Paging
// =============================================================================================

bool FullScreenController::goToPage(int pageIndex)
{
    if (surface_ == nullptr || document_ == nullptr) return false;
    if (pageIndex < 0 || pageIndex >= document_->pageCount()) return false;

    // THE TRANSITION IS BEGUN BEFORE THE PAGE TURNS, because it captures the frame that is on
    // screen NOW -- see `PresentationSurface::beginTransition`.
    const core::TransitionStyle style = transitionFor(pageIndex);
    double seconds = 1.0;               // ISO 32000-1 12.4.4's default for `/D`
    if (document_ && services_.backend.pageTransitions(document_->handle()) != nullptr) {
        if (const core::PageTransition* own =
                services_.backend.pageTransitions(document_->handle())->at(pageIndex)) {
            seconds = own->durationSeconds.value_or(1.0);
        }
    }
    surface_->beginTransition(style, seconds);

    surface_->view()->goToPage(pageIndex);

    // A PAGE WITH ITS OWN `/Dur` RESTARTS THE CLOCK AT ITS OWN INTERVAL.
    restartAutoAdvance();
    return true;
}

bool FullScreenController::nextPage()
{
    return surface_ != nullptr && goToPage(currentPage() + 1);
}

bool FullScreenController::prevPage()
{
    return surface_ != nullptr && goToPage(currentPage() - 1);
}

bool FullScreenController::firstPage() { return goToPage(0); }

bool FullScreenController::lastPage()
{
    return document_ != nullptr && goToPage(document_->pageCount() - 1);
}

int FullScreenController::currentPage() const
{
    return surface_ != nullptr ? surface_->view()->controller().currentPage() : 0;
}

// =============================================================================================
// The document's request against the person's preference
// =============================================================================================

core::TransitionStyle FullScreenController::transitionFor(int pageIndex) const
{
    // **THE DOCUMENT WINS** (guide case A-61). A preference cannot know what the author intended
    // for one particular page, and `Features.md` §1.2 promises transitions *"with a per-page
    // override"* -- which is this direction and not the other.
    if (document_) {
        if (const core::PageTransitions* transitions =
                services_.backend.pageTransitions(document_->handle())) {
            if (const core::PageTransition* own = transitions->at(pageIndex)) return own->style;
        }
    }
    return services_.settings.get(core::keys::fullScreen::transition);
}

int FullScreenController::autoAdvanceSecondsFor(int pageIndex) const
{
    if (document_) {
        if (const core::PageTransitions* transitions =
                services_.backend.pageTransitions(document_->handle())) {
            if (const core::PageTiming* timing = transitions->timingAt(pageIndex)) {
                // ROUNDED UP TO ONE SECOND. `/Dur 0.5` is legal and a half-second auto-advance is
                // a document nobody can read; the preference's own floor is 1 s for the same
                // reason, and a file must not be able to go below what a person can choose.
                const int seconds = static_cast<int>(timing->seconds + 0.5);
                return seconds > 0 ? seconds : 1;
            }
        }
    }
    return services_.settings.get(core::keys::fullScreen::autoAdvanceSec);
}

} // namespace rawpage::ui
