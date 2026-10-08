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
    //
    // DERIVED, NOT TYPED (Stage 3 guide case A-81). Until Stage 3 T10 this was a hand-written list of seven
    // chords with `Ctrl+R` among them; T10 registered `page.rotate`, and a typed list would have gone on
    // asserting a chord was unimplemented after it was built. The list is now every `Viewer` row of
    // `KeyBindingDefaults.def` (through the store) whose command the window did not register.
    Viewer viewer(4);

    int unregistered = 0;
    int registered = 0;
    for (const core::KeyBinding& binding : viewer.fixture.keyBindings.allBindings()) {
        if (binding.context != core::KeyContext::Viewer || !binding.action) continue;
        const std::string chord = binding.chord.toString();
        INFO("chord: " << chord);
        const ui::ShortcutBinder::Outcome outcome = viewer.binder->activate(binding.chord);
        if (viewer.fixture.actions.isRegistered(*binding.action)) {
            ++registered;
            CHECK(ui::ShortcutBinder::consumes(outcome));
        } else {
            ++unregistered;
            CHECK(outcome == ui::ShortcutBinder::Outcome::NotImplemented);
            CHECK_FALSE(ui::ShortcutBinder::consumes(outcome));
        }
    }

    // `Ctrl+R` is built now, and consumed -- disabled here, because `FakeBackend` is not editable.
    CHECK(viewer.fixture.actions.isRegistered(core::ActionId::PageRotate));
    CHECK(ui::ShortcutBinder::consumes(viewer.press("Ctrl+R")));

    // THE CENSUS, re-counted from the table: 52 dispatched Viewer rows registered, 1 not, since Stage 6 T9, which
    // registered `Ctrl+.` and `Ctrl+,` (`nav.nextComment`, `nav.prevComment`, D6-12); the one left is `Ctrl+B`
    // (Stage 7). 50 and 3 from Stage 6 T5,
    // which registered the fourteen annotation-tool arming chords and `Ctrl+E` (`view.togglePropertiesBar`,
    // D6-9). 35 and 18 from 2026-10-04, when the fourteen (`Ctrl+Alt+M`, `H`, `U`, `X`, `T`, `L`,
    // `R`, `O`, `N`, `P`, `S`, `D`, `A`, `K`) moved here from Annotation tools (`Stage6-Annotations-Plan.md`
    // Q1, D6-9). The three left are `Ctrl+B` (Stage 7), `Ctrl+.` and `Ctrl+,` (T9's comment navigation). 35 and 4 as of Stage 5 T13 -- 26 and 13 from Stage 3 T10 until `Ctrl+U`, `Ctrl+'` and `Ctrl+;` moved across
    // it at Stage 4 T8, then `Ctrl+L` (reading mode) at Stage 4 T12, then `S` (the Snapshot, D5-11) at
    // Stage 5 T10, then `Ctrl+4` (Reflow view, D5-16) at Stage 5 T11, then `Ctrl+Alt+C`, `Ctrl+]` and
    // `Ctrl+[` (the comparison, D5-18) at Stage 5 T13. The four left are `Ctrl+B`, `Ctrl+E`, `Ctrl+.` and
    // `Ctrl+,` -- the figure the guide's A-108 names. A change to either is a registration or a table edit,
    // and belongs in the same change as this.
    CHECK(registered == 52);
    CHECK(unregistered == 1);
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
    CHECK(viewer.view->controller().fitMode() == core::render::FitMode::Width);

    REQUIRE(viewer.press("Ctrl+0") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().fitMode() == core::render::FitMode::Page);

    REQUIRE(viewer.press("Ctrl+3") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().fitMode() == core::render::FitMode::Visible);

    // ACTUAL SIZE IS NOT A FIT: 100 % at the monitor's physical DPI, and a fixed zoom that does not
    // change when the window is resized.
    REQUIRE(viewer.press("Ctrl+1") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().fitMode() == core::render::FitMode::None);
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
    // the two-up-ness -- which is what its §8 row says and is a different command from the two
    // that select a mode outright.
    REQUIRE(viewer.press("Ctrl+Shift+H") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().layoutMode() == LayoutMode::TwoUp);
    REQUIRE(viewer.press("Ctrl+Shift+H") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->controller().layoutMode() == LayoutMode::TwoUpContinuous);
}

// ---------------------------------------------------------------------------------------------
// I-23. THE CASE ABOVE PRESSES `Ctrl+Shift+T` ONCE, AND ONE PRESS CANNOT SEE A TOGGLE THAT DOES
// NOT TOGGLE.
//
// It is convention 22 in its plainest form: proving a chord FIRES is half of proving it fires WHEN
// IT SHOULD, and the criterion -- *every §8 Viewer chord dispatches* -- asked only for the half
// that was written. `ViewTwoUp` was a `registerMode` select that set `TwoUpContinuous`
// unconditionally, so the second press was a no-op and the user could enter two-up and never
// leave. The case stayed green because it never pressed twice, and `checked` reported state
// correctly the whole time, which is what made the action look like a toggle everywhere else.
//
// So this one presses TWICE FROM EVERY STARTING MODE, and the round trip is the assertion: the
// chord owns the two-up axis and must return the mode it started from, whichever that was.
TEST_CASE("Ctrl+Shift+T toggles two-up from every mode and preserves the continuity",
          "[viewercommands]")
{
    Viewer viewer(6);

    struct Case {
        LayoutMode start;
        LayoutMode toggled;
    };

    // The four starting modes and what ONE press must give. The continuity is unchanged in every
    // row -- that is the half `Ctrl+Shift+H` owns, and this chord may not touch it.
    const Case cases[] = {
        {LayoutMode::Single, LayoutMode::TwoUp},
        {LayoutMode::SingleContinuous, LayoutMode::TwoUpContinuous},
        {LayoutMode::TwoUp, LayoutMode::Single},
        {LayoutMode::TwoUpContinuous, LayoutMode::SingleContinuous},
    };

    for (const Case& c : cases) {
        CAPTURE(core::render::layoutModeName(c.start));

        viewer.view->setLayoutMode(c.start);
        viewer.view->refresh();
        REQUIRE(viewer.view->controller().layoutMode() == c.start);

        REQUIRE(viewer.press("Ctrl+Shift+T") == ui::ShortcutBinder::Outcome::Dispatched);
        CHECK(viewer.view->controller().layoutMode() == c.toggled);
        CHECK(core::render::isContinuous(c.toggled) == core::render::isContinuous(c.start));

        // THE SECOND PRESS IS THE WHOLE CASE. A select passes every assertion above.
        REQUIRE(viewer.press("Ctrl+Shift+T") == ui::ShortcutBinder::Outcome::Dispatched);
        CHECK(viewer.view->controller().layoutMode() == c.start);
    }
}

// The companion half: `checked` means *two-up is on*, not *one particular mode is in force*. A
// check that asked equality with `TwoUpContinuous` would leave the ribbon control unchecked in
// paged two-up while the pages were visibly side by side.
TEST_CASE("the Two-Up control reports checked in BOTH two-up modes", "[viewercommands]")
{
    Viewer viewer(6);

    for (const LayoutMode mode : {LayoutMode::TwoUp, LayoutMode::TwoUpContinuous}) {
        CAPTURE(core::render::layoutModeName(mode));
        viewer.view->setLayoutMode(mode);
        viewer.view->refresh();
        CHECK(viewer.fixture.actions.isChecked(core::ActionId::ViewTwoUp));
    }

    // THE CONTROL, and convention 7's point: a `checked` hard-wired to `true` passes the loop
    // above and fails here.
    for (const LayoutMode mode : {LayoutMode::Single, LayoutMode::SingleContinuous}) {
        CAPTURE(core::render::layoutModeName(mode));
        viewer.view->setLayoutMode(mode);
        viewer.view->refresh();
        CHECK_FALSE(viewer.fixture.actions.isChecked(core::ActionId::ViewTwoUp));
    }
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

TEST_CASE("the rotate chords rotate from the key event a keyboard sends", "[viewercommands]")
{
    // S2 issue I-33, found by the user in the M-10 walk. The case above presses "Ctrl+Shift+=" by
    // PARSING the string -- which is the chord §8 spells, and not what arrives: Qt on Windows
    // reports Shift+= as Key_Plus and Shift+- as Key_Underscore, so `ShortcutBinder` formed
    // `Ctrl+Shift++`, nothing bound it, and the chord was dead while every case here was green.
    // This one sends the EVENT, through the application filter, to the focused widget.
    //
    // WHAT THIS DOES NOT COVER (convention 19): Qt's Windows key mapper. The events are built as
    // a probe measured that mapper delivering them; that it still does is M-10's, by hand.
    Viewer viewer(3);
    QWidget* focus = viewer.window->focusWidget();
    REQUIRE(focus != nullptr);
    REQUIRE(viewer.view->viewRotation() == core::PageRotation::None);

    const auto send = [focus](Qt::Key key, Qt::KeyboardModifiers modifiers, const QString& text) {
        QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
        press.ignore();
        QCoreApplication::sendEvent(focus, &press);
        return press.isAccepted();
    };
    const Qt::KeyboardModifiers ctrlShift = Qt::ControlModifier | Qt::ShiftModifier;

    CHECK(send(Qt::Key_Plus, ctrlShift, QStringLiteral("=")));
    CHECK(viewer.view->viewRotation() == core::PageRotation::Clockwise90);

    CHECK(send(Qt::Key_Underscore, ctrlShift, QString(QChar(0x1f))));
    CHECK(viewer.view->viewRotation() == core::PageRotation::None);

    // THE CONTROL IN THE OTHER DIRECTION: the unshifted chord is still zoom and not a rotation,
    // so the candidates did not make `Ctrl+=` and `Ctrl+Shift+=` one chord.
    const double zoomBefore = viewer.view->controller().zoom().factor;
    CHECK(send(Qt::Key_Equal, Qt::ControlModifier, QStringLiteral("=")));
    CHECK(viewer.view->viewRotation() == core::PageRotation::None);
    CHECK(viewer.view->controller().zoom().factor > zoomBefore);
}

TEST_CASE("the three tool chords arm a tool and report which is armed", "[viewercommands]")
{
    Viewer viewer(3);
    CHECK(viewer.view->tool() == ui::ToolKind::Select);

    REQUIRE(viewer.press("H") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->tool() == ui::ToolKind::Hand);
    CHECK(viewer.fixture.actions.isChecked(core::ActionId::ToolHand));
    CHECK_FALSE(viewer.fixture.actions.isChecked(core::ActionId::ToolSelect));

    REQUIRE(viewer.press("Z") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->tool() == ui::ToolKind::MarqueeZoom);

    REQUIRE(viewer.press("V") == ui::ShortcutBinder::Outcome::Dispatched);
    CHECK(viewer.view->tool() == ui::ToolKind::Select);

    // WHAT IS NOT HERE, said at the assertion rather than discovered (convention 19): the sticky
    // tool, the temporary-hand modifier, `Esc` cancellation and the tool classes are
    // `ui/viewer/ToolController`'s, built at Stage 4 T7 and asserted in `tst_tools.cpp`. This case
    // owns the three BINDINGS; what a Select drag selects is Stage 5 T5's, asserted in
    // `tst_selecttool.cpp`, which is why there is no assertion here about selecting.
    SUCCEED();
}

TEST_CASE("the hand tool pans the page the way the hand moves", "[viewercommands]")
{
    Viewer viewer(12);
    viewer.view->controller().setScroll(core::DevicePoint{0.0, 400.0});
    viewer.view->setTool(ui::ToolKind::Hand);

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
    viewer.view->setTool(ui::ToolKind::Select);
    const double parked = viewer.view->controller().scroll().y;
    QMouseEvent press2(QEvent::MouseButtonPress, QPointF(200, 200), QPointF(200, 200),
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent move2(QEvent::MouseMove, QPointF(200, 300), QPointF(200, 300), Qt::NoButton,
                      Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(viewer.view->viewport(), &press2);
    QCoreApplication::sendEvent(viewer.view->viewport(), &move2);
    CHECK(viewer.view->controller().scroll().y == parked);
}

TEST_CASE("the keypad's minus and plus zoom like the main row's with Ctrl held", "[viewercommands][I-21]")
{
    // S4 guide I-21, M-08's re-walk, 2026-09-27: `Ctrl` + the NUMERIC KEYPAD's `+` did nothing. The events below
    // are the ones Qt's Windows key mapper was MEASURED to deliver that day -- a probe put
    // WM_KEYDOWN for VK_SUBTRACT and VK_ADD, with Ctrl held in the thread's key state, through
    // `QWindowsKeyMapper`: `Key_Minus` / `Key_Plus` with `Ctrl` AND `Qt::KeypadModifier`, text "-"
    // and "+". The main row's `Ctrl+-` arrives as `Key_Minus` with `Ctrl` alone.
    //
    // WHAT THIS DOES NOT COVER (convention 19): the mapper itself. That a real keypad still
    // delivers these is the walk's, by hand.
    Viewer viewer(3);
    QWidget* focus = viewer.window->focusWidget();
    REQUIRE(focus != nullptr);

    const auto send = [focus](Qt::Key key, Qt::KeyboardModifiers modifiers, const QString& text) {
        QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
        press.ignore();
        QCoreApplication::sendEvent(focus, &press);
        return press.isAccepted();
    };
    const Qt::KeyboardModifiers ctrlKeypad = Qt::ControlModifier | Qt::KeypadModifier;

    const double start = viewer.view->controller().zoom().factor;
    CHECK(send(Qt::Key_Minus, ctrlKeypad, QStringLiteral("-")));
    const double afterMinus = viewer.view->controller().zoom().factor;
    CHECK(afterMinus < start);

    CHECK(send(Qt::Key_Plus, ctrlKeypad, QStringLiteral("+")));
    CHECK(viewer.view->controller().zoom().factor > afterMinus);

    // THE CONTROL: the main row's `Ctrl+-`, as the mapper delivers it, zooms out too.
    const double beforeMain = viewer.view->controller().zoom().factor;
    CHECK(send(Qt::Key_Minus, Qt::ControlModifier, QStringLiteral("-")));
    CHECK(viewer.view->controller().zoom().factor < beforeMain);
}
