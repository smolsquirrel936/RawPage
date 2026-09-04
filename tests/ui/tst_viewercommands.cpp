// Stage 2 T14 -- the §8 Viewer commands, offscreen.
//
// Guide case **A-55's second half** (*"then a `Viewer`-bound chord dispatches its Viewer command"*)
// and the task's own exit criterion: **every §8 Viewer chord dispatches**.
//
// ---------------------------------------------------------------------------------------------
// THE CHORDS ARE DRIVEN THROUGH `ShortcutBinder`, NOT THROUGH `ActionRegistry::dispatch`.
//
// Dispatching by id would assert that the handler works and say nothing about whether the KEY
// reaches it — which is the difference between a command being implemented and a command being
// usable, and it is the difference Stage 1's I-42 was: the ribbon had drawn key tips since T10,
// `Alt` was bound to an id in `KeyBindingDefaults.def`, the chord formed correctly, and the id was
// simply never registered. Every case here starts from the chord.
//
// ---------------------------------------------------------------------------------------------
// AND THEY ASSERT AN EFFECT, NOT A RETURN CODE.
//
// `Outcome::Dispatched` means a handler ran. What a case wants to know is that the page moved: the
// zoom changed, the current page changed, the layout mode changed, the tool changed. A case that
// stopped at the outcome would stay green for a handler wired to the wrong `PageView` method,
// which is exactly the sort of mistake twenty-five registrations written in one sitting produce.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/Chord.h"
#include "core/cmd/KeyBindingStore.h"
#include "core/render/PagePresentation.h"
#include "tests/ui/ShellFixture.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/TilePresenter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLayout>
#include <QMouseEvent>

using namespace rawpage;
using core::render::LayoutMode;

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

// A window with a document open, laid out and focused in the viewer -- which is the state every
// case here needs and the state a Viewer chord is only resolvable in.
struct Viewer {
    Fixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::PageView* view = nullptr;
    std::unique_ptr<ui::ShortcutBinder> binder;

    explicit Viewer(int pages = 12)
    {
        fixture.backend.pagesPerDocument = pages;
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1000, 800);
        window->openFiles({fixture.writeFile(QStringLiteral("commands.pdf"))});
        driveLayout(window.get());

        view = window->currentView();
        REQUIRE(view != nullptr);
        REQUIRE(view->width() > 300);   // A-52's rule: the geometry first, always

        window->raiseToForeground();
        binder = std::make_unique<ui::ShortcutBinder>(fixture.actions, fixture.keyBindings,
                                                     window.get());
        view->setFocus();
        REQUIRE(binder->activeContext() == core::KeyContext::Viewer);
    }

    ui::ShortcutBinder::Outcome press(const char* chord) const
    {
        const std::optional<core::Chord> parsed = core::Chord::parse(chord);
        REQUIRE(parsed.has_value());
        return binder->activate(*parsed);
    }
};

} // namespace

// =============================================================================================
// The exit criterion: EVERY §8 Viewer chord this stage owns dispatches.
// =============================================================================================

TEST_CASE("every Stage 2 Viewer chord dispatches from the viewer", "[viewercommands]")
{
    Viewer viewer(12);

    // The subset `Implementation-Plan.md` § Stage 2 lists, chord by chord. Written as the CHORDS
    // rather than as the ids, because that is what a person presses and because it is the half
    // I-42 proved can be missing while everything else looks right.
    const char* const chords[] = {
        "Ctrl+=",  "Ctrl+-",  "Ctrl+0",       "Ctrl+1",       "Ctrl+2",       "Ctrl+3",
        "Ctrl+M",  "Ctrl+Shift+N",            "Page Down",    "Page Up",
        "Ctrl+Page Down",     "Ctrl+Page Up", "Home",         "End",
        "Alt+Left", "Alt+Right",              "Ctrl+Shift+=", "Ctrl+Shift+-",
        "Ctrl+Shift+H",       "Ctrl+Shift+T", "H",            "V",            "Z",
    };

    for (const char* chord : chords) {
        INFO("chord: " << chord);
        const ui::ShortcutBinder::Outcome outcome = viewer.press(chord);

        // DISPATCHED OR UNAVAILABLE, and never `NotImplemented`. `Unavailable` is legitimate here
        // and is the whole of I-53's subject -- `Home` on the first page, `Alt+Left` with no
        // history -- and it CONSUMES the key, which `NotImplemented` deliberately does not.
        CHECK((outcome == ui::ShortcutBinder::Outcome::Dispatched
               || outcome == ui::ShortcutBinder::Outcome::Unavailable));
        CHECK(ui::ShortcutBinder::consumes(outcome));
    }
}

TEST_CASE("the rest of the Viewer table is still NotImplemented and leaves the key alone",
          "[viewercommands]")
{
    // THE OTHER HALF, and it is what keeps the subset honest. `Stage2-Rendering-Plan.md` §10 names
    // these as Stages 3 to 13's; a stage that implements MOST of a context's table invites someone
    // to finish it, and an unregistered id must leave the key UNCONSUMED so whatever else would
    // have handled it still can.
    Viewer viewer(4);

    const char* const notYet[] = {"Ctrl+4", "Ctrl+R", "S", "Ctrl+B", "Ctrl+E", "Ctrl+L", "Ctrl+U"};
    for (const char* chord : notYet) {
        INFO("chord: " << chord);
        const ui::ShortcutBinder::Outcome outcome = viewer.press(chord);
        CHECK(outcome == ui::ShortcutBinder::Outcome::NotImplemented);
        CHECK_FALSE(ui::ShortcutBinder::consumes(outcome));
    }
}

// =============================================================================================
// …and each one does the thing it says. An outcome is not an effect.
// =============================================================================================

TEST_CASE("the zoom chords move the zoom, and the fit chords set a mode", "[viewercommands]")
{
    Viewer viewer(3);
    const double before = viewer.view->controller().zoom().factor;

    REQUIRE(viewer.press("Ctrl+=") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().zoom().factor > before);

    REQUIRE(viewer.press("Ctrl+-") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().zoom().factor == before);

    // A FIT IS A MODE, not a number -- `Ctrl+2` must leave the view still fitting after a resize,
    // which is what `FitMode` on the controller means and what a handler that computed a zoom and
    // set it would lose.
    REQUIRE(viewer.press("Ctrl+2") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().fitMode() == ui::FitMode::Width);

    REQUIRE(viewer.press("Ctrl+0") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().fitMode() == ui::FitMode::Page);

    REQUIRE(viewer.press("Ctrl+3") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().fitMode() == ui::FitMode::Visible);

    // ACTUAL SIZE IS NOT A FIT: 100 % at the monitor's physical DPI, and a fixed zoom that does not
    // change when the window is resized.
    REQUIRE(viewer.press("Ctrl+1") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().fitMode() == ui::FitMode::None);
    CHECK(viewer.view->controller().zoom() == core::render::Zoom::actualSize());
}

TEST_CASE("the page chords move the page, and refuse at the ends", "[viewercommands]")
{
    Viewer viewer(12);
    REQUIRE(viewer.view->controller().currentPage() == 0);

    // ON THE FIRST PAGE, `Home` AND `Ctrl+Page Up` ARE REFUSED -- and refused is not the same as
    // unimplemented: the key IS consumed, because the command exists and the answer is no. That is
    // the distinction I-53 is about and the one `ShortcutBinder::consumes` encodes.
    CHECK(viewer.press("Home") == ui::ShortcutBinder::Outcome::Unavailable);
    CHECK(viewer.press("Ctrl+Page Up") == ui::ShortcutBinder::Outcome::Unavailable);
    CHECK(viewer.view->controller().currentPage() == 0);

    REQUIRE(viewer.press("Ctrl+Page Down") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().currentPage() == 1);

    REQUIRE(viewer.press("End") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().currentPage() == 11);

    CHECK(viewer.press("End") == ui::ShortcutBinder::Outcome::Unavailable);
    CHECK(viewer.press("Ctrl+Page Down") == ui::ShortcutBinder::Outcome::Unavailable);
    CHECK(viewer.view->controller().currentPage() == 11);

    // AND THE VIEW HISTORY the page jumps recorded. `Alt+Left` was refused at the top of this case
    // and is available now, which is the same predicate seen from the other side.
    REQUIRE(viewer.press("Alt+Left") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().currentPage() == 1);
    REQUIRE(viewer.press("Alt+Right") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().currentPage() == 11);
}

TEST_CASE("a screen step is not a page step in a continuous mode", "[viewercommands]")
{
    Viewer viewer(12);
    REQUIRE(viewer.view->controller().layoutMode() == LayoutMode::SingleContinuous);

    const double before = viewer.view->controller().scroll().y;
    REQUIRE(viewer.press("Page Down") == ui::ShortcutBinder::Outcome::Dispatched);
    const double after = viewer.view->controller().scroll().y;

    // IT SCROLLED, and by LESS than a full viewport. The overlap is not decoration: a step of
    // exactly one viewport puts the line that was at the bottom edge one pixel above the top edge,
    // and a reader loses it once per press.
    CHECK(after > before);
    CHECK(after - before < viewer.view->controller().viewportSize().height);

    // IN A PAGED MODE THERE IS NOTHING BELOW THE CURRENT PAGE, so the screen step becomes a page
    // step -- otherwise `Page Down` scrolls into empty space and stops, which reads as broken.
    viewer.view->setLayoutMode(LayoutMode::Single);
    viewer.view->refresh();
    const int page = viewer.view->controller().currentPage();
    REQUIRE(viewer.press("Page Down") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().currentPage() == page + 1);
}

TEST_CASE("the layout chords select and toggle, and report which is in force",
          "[viewercommands]")
{
    Viewer viewer(6);

    REQUIRE(viewer.press("Ctrl+Shift+T") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().layoutMode() == LayoutMode::TwoUpContinuous);

    // A TOGGLE REPORTS ITS OWN STATE, which is what puts a check mark on the ribbon control --
    // `ActionFactory` reads `checked` for that, and `isToggle` decides checkability (I-21).
    CHECK(viewer.fixture.actions.isToggle(core::ActionId::ViewTwoUp));
    CHECK(viewer.fixture.actions.isChecked(core::ActionId::ViewTwoUp));
    CHECK_FALSE(viewer.fixture.actions.isChecked(core::ActionId::ViewContinuous));

    // `Ctrl+Shift+H` TOGGLES paged against continuous rather than selecting one, and it preserves
    // the two-up-ness -- which is what its §8 row says and is a different command from the three
    // that select a mode outright.
    REQUIRE(viewer.press("Ctrl+Shift+H") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().layoutMode() == LayoutMode::TwoUp);
    REQUIRE(viewer.press("Ctrl+Shift+H") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().layoutMode() == LayoutMode::TwoUpContinuous);
}

TEST_CASE("rotating the view rotates the layout and re-keys the tiles", "[viewercommands]")
{
    Viewer viewer(3);
    viewer.fixture.pumpTiles();

    const core::DeviceSize portrait = viewer.view->controller().contentSize();
    const std::size_t tilesBefore = viewer.view->tiles().heldTileCount();
    REQUIRE(tilesBefore > 0);
    REQUIRE(viewer.view->viewRotation() == core::PageRotation::None);

    REQUIRE(viewer.press("Ctrl+Shift+=") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->viewRotation() == core::PageRotation::Clockwise90);

    // THE LAYOUT TURNED. An A4 page is taller than it is wide, so a quarter-turn makes the content
    // wider than it was and shorter -- and a rotation that only changed the tiles would leave the
    // scrollbars, the fit and the page slots describing a page that is no longer that shape.
    const core::DeviceSize landscape = viewer.view->controller().contentSize();
    CHECK(landscape.width > portrait.width);

    // AND THE TILES ARE KEYED ON THE ROTATION, which is the half with no symptom -- and which
    // this case did NOT assert until its negative control refused to fire. Counting new requests
    // proves nothing: a rotated page is a different shape, so its tile rectangles differ and new
    // requests are made whether or not the rotation is in the key. Only the KEY can say.
    viewer.fixture.pumpTiles();
    const std::vector<core::render::RenderRequest> held = viewer.view->tiles().heldRequests();
    REQUIRE_FALSE(held.empty());
    for (const core::render::RenderRequest& request : held) {
        INFO("page " << request.pageIndex);
        CHECK(request.viewRotation == core::PageRotation::Clockwise90);
    }

    // Counter-clockwise comes back, and four presses is a full turn -- the modulo has to be right
    // for negatives, which is what `rotateBy` is written for.
    REQUIRE(viewer.press("Ctrl+Shift+-") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->viewRotation() == core::PageRotation::None);
    REQUIRE(viewer.press("Ctrl+Shift+-") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->viewRotation() == core::PageRotation::Clockwise270);
}

TEST_CASE("the three tool chords arm a tool and report which is armed", "[viewercommands]")
{
    Viewer viewer(3);
    CHECK(viewer.view->tool() == ui::PageView::Tool::Select);

    REQUIRE(viewer.press("H") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->tool() == ui::PageView::Tool::Hand);
    CHECK(viewer.fixture.actions.isChecked(core::ActionId::ToolHand));
    CHECK_FALSE(viewer.fixture.actions.isChecked(core::ActionId::ToolSelect));

    REQUIRE(viewer.press("Z") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->tool() == ui::PageView::Tool::MarqueeZoom);

    REQUIRE(viewer.press("V") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->tool() == ui::PageView::Tool::Select);

    // WHAT IS NOT HERE, said at the assertion rather than discovered (convention 19): the sticky
    // tool, the temporary-hand modifier, `Esc` cancellation and the seventeen tool classes are
    // `ui/viewer/ToolController` and are ○ (S4). Stage 2 owns the three BINDINGS and what a drag
    // does; `Select` selects nothing until Stage 5 gives it text, which is why there is no
    // assertion here about selecting.
    SUCCEED();
}

TEST_CASE("the hand tool pans the page the way the hand moves", "[viewercommands]")
{
    Viewer viewer(12);
    viewer.view->controller().setScroll(core::DevicePoint{0.0, 400.0});
    viewer.view->setTool(ui::PageView::Tool::Hand);

    const double before = viewer.view->controller().scroll().y;

    // DRAGGING DOWNWARD moves the page down, so the SCROLL goes UP. Getting this sign wrong is the
    // classic hand-tool bug: obvious to a person, invisible to a case that only checks that
    // something moved.
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(200, 200), QPointF(200, 200),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move(QEvent::MouseMove, QPointF(200, 260), QPointF(200, 260), Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(200, 260), QPointF(200, 260),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);

    QCoreApplication::sendEvent(viewer.view->viewport(), &press);
    QCoreApplication::sendEvent(viewer.view->viewport(), &move);
    QCoreApplication::sendEvent(viewer.view->viewport(), &release);

    CHECK(viewer.view->controller().scroll().y < before);

    // AND THE SELECT TOOL DOES NOT PAN -- the negative control, without which a `PageView` that
    // panned on every drag regardless of tool would pass the case above.
    viewer.view->setTool(ui::PageView::Tool::Select);
    const double parked = viewer.view->controller().scroll().y;
    QMouseEvent press2(QEvent::MouseButtonPress, QPointF(200, 200), QPointF(200, 200),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move2(QEvent::MouseMove, QPointF(200, 300), QPointF(200, 300), Qt::NoButton,
                      Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(viewer.view->viewport(), &press2);
    QCoreApplication::sendEvent(viewer.view->viewport(), &move2);
    CHECK(viewer.view->controller().scroll().y == parked);
}
