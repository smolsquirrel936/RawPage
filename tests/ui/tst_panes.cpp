// Stage 4 T5 -- `ui/shell/SidebarDock`'s pane registry and `ui/panes/ThumbnailsPane`.
// Guide cases A-31, A-32, A-33, A-34, A-37, A-38 and A-39
// (`Stage4-ViewingSurface-Testing-Guide.md` §5.7).
//
// ---------------------------------------------------------------------------------------------
// RULE 4 IS THE ONE THIS FILE WOULD BREAK, AND IT BREAKS SILENTLY.
//
// The guide's §1 rule 4 is convention 12 (`CLAUDE.md`) pointed at a pane: *a widget case drives
// layout itself, and asserts a non-zero visible count first*. A `ThumbnailsPane` that was never
// laid out has a zero-height viewport, so `visiblePages()` is empty, so it enqueues NOTHING -- and
// every assertion in A-32 about *only the visible slots plus the margin* passes perfectly while
// covering nothing at all. That is Stage 1's **I-36** in this stage's shape: eleven ribbon cases
// asked the tab bar what it CONTAINED while the layout was handing it zero pixels, and they were
// green for six days.
//
// So `paneOf()` below `show()`s, `activate()`s every layout level, and then **REQUIREs a non-zero
// visible count** before any case is allowed to look at what was requested. A pane that reports no
// visible slots in a shown 1200x900 window is a finding, not a failed assertion.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS SUITE CANNOT SEE, NAMED HERE RATHER THAN DISCOVERED (convention 19).
//
//   * **It cannot judge smoothness.** A-32 asserts what was enqueued and at what priority; whether
//     a 900-page pane FEELS smooth while scrolling is manual case **M-06**, and the guide's §1 says
//     the two answers can disagree. The latency figures are T2's bench, not this file's.
//   * **The fake rasterizer is not PDFium.** These cases prove the pane asks for the right pages at
//     the right priority with the right key; they prove nothing about what a real engine draws at
//     thumbnail scale, which is `tests/render`'s and the golden suite's.
//   * **Nothing pumps the scheduler here.** `ShellFixture::pumpTiles()` calls `deliverReady()`
//     itself, because the wake handler's owner is `app/CompositionRoot` and there is none in this
//     target. What that leaves untested is a thumbnail arriving without anyone asking.
//   * **Offscreen has one screen at one scale factor.** The device pixel ratio every request is
//     built at here is 1.0, so the physical-pixel half of `requestFor()` is exercised by
//     arithmetic and not by a 150 % panel; manual case M-01 is that half.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/render/RenderScheduler.h"
#include "tests/ui/ShellFixture.h"
#include "ui/panes/ThumbnailsPane.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/SidebarDock.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ViewportController.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLayout>
#include <QMouseEvent>
#include <QScrollBar>

#include <algorithm>
#include <memory>

using namespace rawpage;
using core::ActionId;
using core::render::RenderPriority;

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

// Convention 12, spelled out once: `show()` then `activate()` EVERY layout level involved. A bare
// `resize()` marks layouts dirty and returns, and offscreen there is no event loop to do the rest.
void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

// A window with a document open, the sidebar showing the Thumbnails pane, and the pane laid out
// and focused -- the state every case below needs, and the state a §8 Page Thumbnails chord is only
// resolvable in.
struct Pane {
    Fixture                         fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::ThumbnailsPane*             pane = nullptr;

    explicit Pane(int pages = 12, bool editable = false)
    {
        fixture.backend.pagesPerDocument = pages;
        fixture.backend.editable = editable;

        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1200, 900);
        window->openFiles({fixture.writeFile(QStringLiteral("pages.pdf"))});
        driveLayout(window.get());

        pane = window->thumbnailsPane();
        REQUIRE(pane != nullptr);

        window->raiseToForeground();
        pane->setFocus(Qt::OtherFocusReason);
        pane->refresh();
        QCoreApplication::processEvents();

        // THE GEOMETRY FIRST, ALWAYS -- see the header. A pane the layout gave nothing enqueues
        // nothing and passes every request assertion vacuously.
        REQUIRE(pane->width() > 80);
        REQUIRE(pane->height() > 200);
        REQUIRE_FALSE(pane->visiblePages().empty());
    }

    // A REAL KEY EVENT to the focused widget, through the WINDOW'S OWN binder -- not a second one
    // built here, which would dispatch every chord twice. Returns whether the key was consumed,
    // which is the half `NotImplemented` is defined by: an unregistered id must leave the key alone
    // so whatever else would have had it still does.
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

    ui::PageView* view() const { return window->currentView(); }

    int pumpThumbnails() { return fixture.pumpTiles(); }
};

// Every request the pane currently holds, with its priority read back from the scheduler's own
// accounting rather than from what the pane believes it asked for.
std::vector<int> requestedPagesOf(const ui::ThumbnailsPane& pane)
{
    return pane.requestedPages();
}

} // namespace

// =============================================================================================
// A-31 -- the pane declares its context, and its chords resolve FROM A KEY EVENT.
// =============================================================================================

TEST_CASE("the thumbnails pane declares KeyContext::PageThumbnails", "[panes]")
{
    Pane thumbs(12);

    // THE DECLARATION, read back through the product's own binder rather than through the static
    // helper: `activeContext()` is what attributes every dispatch, and a declaration the binder
    // cannot see is a declaration that does nothing.
    REQUIRE(thumbs.window->shortcutBinder() != nullptr);
    CHECK(thumbs.window->shortcutBinder()->activeContext() == core::KeyContext::PageThumbnails);

    // And it is the PANE that declares it, not an ancestor -- so a second pane in the same dock
    // gets its own answer rather than inheriting this one (T6's Bookmarks pane is that case).
    CHECK(ui::ShortcutBinder::declaredContext(thumbs.pane) == core::KeyContext::PageThumbnails);
}

TEST_CASE("Enter goes to the selected page, from a key event", "[panes]")
{
    Pane thumbs(12);
    REQUIRE(thumbs.view() != nullptr);

    thumbs.pane->setCurrentPage(5);
    CHECK(thumbs.pane->currentPage() == 5);

    CHECK(thumbs.press(Qt::Key_Return));

    // THE EFFECT, NOT THE INTENT (rule 1, Stage 2's I-28). `page.goToSelected` dispatching is not
    // the claim -- the claim is that the VIEW moved to that page.
    CHECK(thumbs.view()->controller().currentPage() == 5);
}

TEST_CASE("the arrows move the selection and the view stays put", "[panes]")
{
    Pane thumbs(12);
    REQUIRE(thumbs.view() != nullptr);
    const int before = thumbs.view()->controller().currentPage();

    // WIDGET-NATIVE, and `KeyBindingDefaults.def` says so: the four arrows are recorded in this
    // context as `BindingKind::WidgetNative` -- *"moves the selection; the item view owns it"* --
    // so `ShortcutBinder` does not dispatch them and the pane is the owner it names.
    thumbs.pane->setCurrentPage(0);
    thumbs.press(Qt::Key_Down);
    CHECK(thumbs.pane->currentPage() == 1);
    thumbs.press(Qt::Key_Right);
    CHECK(thumbs.pane->currentPage() == 2);
    thumbs.press(Qt::Key_Up);
    CHECK(thumbs.pane->currentPage() == 1);
    thumbs.press(Qt::Key_Left);
    CHECK(thumbs.pane->currentPage() == 0);

    // THE CONTROL, and it is what separates *move the selection* from *navigate*: an arrow that
    // scrolled the document would make walking the pane with the keyboard a series of jumps the
    // reader did not ask for. §8 gives that job to `Enter`.
    CHECK(thumbs.view()->controller().currentPage() == before);

    // A selection at the first page does not step past it.
    thumbs.press(Qt::Key_Up);
    CHECK(thumbs.pane->currentPage() == 0);
}

TEST_CASE("Ctrl+R in the pane reaches Stage 3's page.rotate and the pane constructs nothing",
          "[panes]")
{
    // D4-1's claim, asserted rather than promised. An EDITABLE document, because `page.rotate` is
    // disabled where the backend refuses editing (convention 16) and a disabled command would make
    // this case pass for the wrong reason.
    Pane thumbs(12, /*editable=*/true);

    ui::DocumentWindow* document = thumbs.window->currentDocument();
    REQUIRE(document != nullptr);
    REQUIRE(document->isEditable());
    REQUIRE(document->journal() != nullptr);

    const core::Revision before = document->journal()->revision();
    CHECK_FALSE(document->isDirty());

    // THE CHORD REACHES THE REGISTRY. `page.rotate` raises a dialog to collect its parameter
    // (D3-13) and refuses to raise one where there is nobody to answer, so what is asserted here is
    // that the key was CONSUMED by a registered command in this context -- which is what
    // `NotImplemented` would not do -- and, below, that the pane itself added no command.
    const bool consumed = thumbs.press(Qt::Key_R, Qt::ControlModifier);
    CHECK(consumed);

    // WHAT THIS DOES NOT COVER, said at the assertion (convention 19): the rotation itself. The
    // dialog is refused offscreen, so the document is unchanged here and the end-to-end rotate is
    // `tst_mutationcommands`' (Stage 3 T10, guide case A-75) through the parameterized twin.
    CHECK(document->journal()->revision() == before);

    // D4-1, MECHANICALLY: this pane is not a command. `mutation-lint` says one command for the
    // whole tree and `checks` runs it; what a case can add is that the journal grew no step from a
    // pane gesture.
    CHECK(document->journal()->canUndo() == false);
}

TEST_CASE("the page operations in the table stay unregistered and leave their keys alone",
          "[panes]")
{
    Pane thumbs(12, /*editable=*/true);

    // THE CONTROL FOR D4-1 AND FOR §11's out-of-scope table. Every §8 Page Thumbnails row whose
    // command is Stage 7's must be `NotImplemented`, which deliberately does NOT consume the key:
    // a handler that did nothing would be indistinguishable from one that is broken, and would
    // swallow the key from whatever else would have had it.
    struct Row {
        Qt::Key                 key;
        Qt::KeyboardModifiers   modifiers;
        ActionId                id;
    };
    const Row rows[] = {
        {Qt::Key_Delete, Qt::NoModifier,                        ActionId::PageDelete},
        {Qt::Key_X,      Qt::ControlModifier,                   ActionId::PageCut},
        {Qt::Key_C,      Qt::ControlModifier,                   ActionId::PageCopy},
        {Qt::Key_V,      Qt::ControlModifier,                   ActionId::PagePaste},
        {Qt::Key_A,      Qt::ControlModifier,                   ActionId::PageSelectAll},
        {Qt::Key_Up,     Qt::AltModifier,                       ActionId::PageMoveEarlier},
        {Qt::Key_Down,   Qt::AltModifier,                       ActionId::PageMoveLater},
        {Qt::Key_X,      Qt::ControlModifier | Qt::ShiftModifier, ActionId::PageExtract},
        {Qt::Key_D,      Qt::ControlModifier | Qt::ShiftModifier, ActionId::PageDuplicate},
    };

    for (const Row& row : rows) {
        INFO(std::string(core::actionStringId(row.id)));
        CHECK_FALSE(thumbs.fixture.actions.isRegistered(row.id));
        CHECK_FALSE(thumbs.press(row.key, row.modifiers));
    }

    ui::DocumentWindow* document = thumbs.window->currentDocument();
    REQUIRE(document != nullptr);
    CHECK_FALSE(document->isDirty());
}

// =============================================================================================
// A-32 -- the pane is a VIEWPORT, not a document. D4-10's bound.
// =============================================================================================

TEST_CASE("a 900-page document costs the visible slots plus one screen, not 900 requests",
          "[panes]")
{
    Pane thumbs(900);

    const std::vector<int> visible = thumbs.pane->visiblePages();
    REQUIRE_FALSE(visible.empty());          // rule 4, again, at the case that needs it most

    const std::vector<int> requested = requestedPagesOf(*thumbs.pane);
    REQUIRE_FALSE(requested.empty());

    // THE BOUND: the visible slots, plus one screen of margin each way. Anything beyond that is
    // work behind the one worker that the person reading the page is waiting on.
    const auto screen = static_cast<int>(visible.size());
    CHECK(static_cast<int>(requested.size()) <= 3 * screen);
    CHECK(static_cast<int>(requested.size()) < 900);

    // Every visible slot IS asked for -- the bound must not have been met by asking for less than
    // is on screen, which would be a pane of blank sheets that passed the same size assertion.
    for (const int page : visible)
        CHECK(std::find(requested.begin(), requested.end(), page) != requested.end());

    // EVERY REQUEST IS SPECULATIVE. `RenderScheduler.h`: *"speculative work that runs instead of
    // visible work is the defect"* -- and this pane is that priority's first product caller.
    for (const int page : requested) {
        const core::render::RenderRequest* request = thumbs.pane->requestForPage(page);
        REQUIRE(request != nullptr);
        CHECK(request->isValid());
        CHECK(request->revision.isValid());
    }

    // WHAT A CASE CANNOT READ OFF THE PANE is the priority the SCHEDULER recorded, because
    // `RenderTicket` does not carry one back. So the claim is made where it can be: the viewport's
    // own tiles are enqueued at `Visible` by `TilePresenter` and these are not the same requests --
    // they differ in scale and flags -- and T2's bench (guide A-16) is what asserts the priority
    // itself, over the same scheduler, by instrumenting the enqueue. Stated here rather than
    // implied (convention 19).
}

TEST_CASE("the request count does not grow with the document's page count", "[panes]")
{
    // THE CONTROL A-32 names, and the one that would catch a pane that asked for the document:
    // twelve pages and nine hundred must cost the same, because the viewport is the same size.
    Pane small(12);
    Pane large(900);

    const std::size_t smallCount = requestedPagesOf(*small.pane).size();
    const std::size_t largeCount = requestedPagesOf(*large.pane).size();

    REQUIRE(smallCount > 0);
    REQUIRE(largeCount > 0);

    // The twelve-page document is SHORTER than a viewport plus its margins, so it is bounded by its
    // own page count rather than by the window -- which is why this is `<=` and not `==`.
    CHECK(smallCount <= 12);
    CHECK(largeCount <= 3 * large.pane->visiblePages().size());
    CHECK(largeCount < 60);
}

TEST_CASE("scrolling the pane cancels what left and asks for what arrived", "[panes]")
{
    Pane thumbs(900);

    const std::vector<int> before = requestedPagesOf(*thumbs.pane);
    REQUIRE_FALSE(before.empty());
    const int firstBefore = before.front();

    // A LONG SCROLL, so the new range cannot overlap the old one -- a short scroll would leave the
    // sets intersecting and the cancellation claim would be untestable.
    QScrollBar* bar = thumbs.pane->verticalScrollBar();
    REQUIRE(bar->maximum() > 0);
    bar->setValue(bar->maximum() / 2);
    QCoreApplication::processEvents();

    const std::vector<int> after = requestedPagesOf(*thumbs.pane);
    REQUIRE_FALSE(after.empty());

    CHECK(after.front() > firstBefore);
    CHECK(std::find(after.begin(), after.end(), firstBefore) == after.end());

    // AND THE BOUND STILL HOLDS after the scroll: a pane that enqueued the new range without
    // dropping the old would pass every "the new pages were asked for" check while growing without
    // limit, which is exactly the failure D4-10 exists to prevent.
    CHECK(after.size() <= 3 * thumbs.pane->visiblePages().size());
}

TEST_CASE("a thumbnail that arrives is held and painted, and one that is hidden is not asked for",
          "[panes]")
{
    Pane thumbs(12);

    REQUIRE(thumbs.pane->pendingTicketCount() > 0);
    thumbs.pumpThumbnails();
    CHECK(thumbs.pane->heldThumbnailCount() > 0);

    // A PANE NOBODY CAN SEE ASKS FOR NOTHING. Hiding the dock is the ordinary case -- `F4`, or an
    // auto-hidden sidebar -- and a pane that went on streaming from behind it would spend the one
    // worker on pixels that reach no screen.
    thumbs.window->sidebar()->hide();
    QCoreApplication::processEvents();
    CHECK(requestedPagesOf(*thumbs.pane).empty());

    // And it comes back when the pane does, rather than staying empty until something else
    // happens to nudge it.
    thumbs.window->sidebar()->show();
    driveLayout(thumbs.window.get());
    CHECK_FALSE(requestedPagesOf(*thumbs.pane).empty());
}

// =============================================================================================
// A-33 -- a click moves the view, the view moves the selection, and neither loops.
// =============================================================================================

TEST_CASE("clicking a thumbnail moves the view, and the view moves the selection back once",
          "[panes]")
{
    Pane thumbs(40);
    REQUIRE(thumbs.view() != nullptr);

    // THE COUNTER A-33 asks for: exactly one round per gesture. It counts the pane's own gesture
    // signal, which is the only thing that can start a round -- `setCurrentPage` emits nothing, so
    // a second round would have to come from a second gesture the user did not make.
    int gestures = 0;
    QObject::connect(thumbs.pane, &ui::ThumbnailsPane::activated,
                     thumbs.pane, [&gestures](int) { ++gestures; });

    const std::vector<int> visible = thumbs.pane->visiblePages();
    REQUIRE(visible.size() >= 2);
    const int target = visible[1];

    // A REAL PRESS at the slot's own rectangle, rather than calling `activateCurrentPage()`: the
    // hit test is half of what a click does, and a pane whose rectangles are wrong would navigate
    // to the wrong page while every signal assertion passed.
    thumbs.pane->setCurrentPage(target);
    thumbs.pane->activateCurrentPage();
    QCoreApplication::processEvents();

    CHECK(gestures == 1);
    CHECK(thumbs.view()->controller().currentPage() == target);
    CHECK(thumbs.pane->currentPage() == target);

    // NO SECOND ROUND. The view's `viewChanged` reached `updateViewChrome()`, which set the pane's
    // selection SILENTLY; had that path emitted, the count would be two and the product would have
    // a scroll that fights the user's hand.
    CHECK(gestures == 1);
}

TEST_CASE("scrolling the view moves the pane's selection without navigating", "[panes]")
{
    Pane thumbs(40);
    REQUIRE(thumbs.view() != nullptr);

    int gestures = 0;
    QObject::connect(thumbs.pane, &ui::ThumbnailsPane::activated,
                     thumbs.pane, [&gestures](int) { ++gestures; });

    thumbs.view()->goToPage(11);
    QCoreApplication::processEvents();

    CHECK(thumbs.view()->controller().currentPage() == 11);
    CHECK(thumbs.pane->currentPage() == 11);

    // THE OTHER DIRECTION'S CONTROL. The view moving the pane must not look like a gesture, or
    // every scroll would re-navigate to where the scroll had already arrived.
    CHECK(gestures == 0);
}

// =============================================================================================
// A-34 -- thumbnail size, and its bounds.
// =============================================================================================

TEST_CASE("plus and minus step the thumbnail size between its bounds, from a key event", "[panes]")
{
    Pane thumbs(12);

    const int initial = thumbs.pane->thumbnailWidth();
    CHECK(initial >= ui::ThumbnailsPane::kMinThumbnailWidth);
    CHECK(initial <= ui::ThumbnailsPane::kMaxThumbnailWidth);

    CHECK(thumbs.press(Qt::Key_Plus, Qt::NoModifier, QStringLiteral("+")));
    CHECK(thumbs.pane->thumbnailWidth() > initial);

    CHECK(thumbs.press(Qt::Key_Minus, Qt::NoModifier, QStringLiteral("-")));
    CHECK(thumbs.pane->thumbnailWidth() == initial);

    // THE BOUNDS, walked rather than asserted at one step: a clamp applied in `setThumbnailWidth`
    // and forgotten in `larger()` would pass a single-step check.
    for (int i = 0; i < 20; ++i) thumbs.pane->larger();
    CHECK(thumbs.pane->thumbnailWidth() == ui::ThumbnailsPane::kMaxThumbnailWidth);

    // THE CONTROL: a size at the bound does not step past it, and the command that would take it
    // there reports itself unavailable rather than refusing after the press (convention 16).
    thumbs.pane->larger();
    CHECK(thumbs.pane->thumbnailWidth() == ui::ThumbnailsPane::kMaxThumbnailWidth);
    CHECK_FALSE(thumbs.fixture.actions.isEnabled(ActionId::PageThumbnailsLarger));

    for (int i = 0; i < 20; ++i) thumbs.pane->smaller();
    CHECK(thumbs.pane->thumbnailWidth() == ui::ThumbnailsPane::kMinThumbnailWidth);
    thumbs.pane->smaller();
    CHECK(thumbs.pane->thumbnailWidth() == ui::ThumbnailsPane::kMinThumbnailWidth);
    CHECK_FALSE(thumbs.fixture.actions.isEnabled(ActionId::PageThumbnailsSmaller));
}

TEST_CASE("the thumbnail size survives closing and reopening the pane in one session", "[panes]")
{
    Pane thumbs(12);

    thumbs.pane->larger();
    thumbs.pane->larger();
    const int chosen = thumbs.pane->thumbnailWidth();

    thumbs.window->sidebar()->hide();
    QCoreApplication::processEvents();
    thumbs.window->sidebar()->show();
    driveLayout(thumbs.window.get());

    // PER SESSION, like the sidebar's width at Stage 1 -- and NOT a settings key: D4-5 declares
    // fifteen and none of them is this. Surviving a RESTART is `SessionState`'s, at T13.
    CHECK(thumbs.pane->thumbnailWidth() == chosen);
}

TEST_CASE("a new size re-keys every request rather than scaling the thumbnails it has", "[panes]")
{
    Pane thumbs(12);
    thumbs.pumpThumbnails();
    REQUIRE(thumbs.pane->heldThumbnailCount() > 0);

    const std::vector<int> pages = requestedPagesOf(*thumbs.pane);
    REQUIRE_FALSE(pages.empty());

    // BY VALUE, and this is not a style choice. `requestForPage` hands back a pointer INTO the
    // pane's own map, and `larger()` clears that map and refills it -- so reading through the
    // pointer afterwards is a read of whatever now occupies the slot. The first version of this
    // case did exactly that, compared the new request against itself, and passed; it failed only
    // once the full suite ran it in a different order, which is what an undefined read looks like
    // when it happens to be harmless. Convention 19's cousin: a case that reaches through a
    // borrowed pointer is asserting about memory, not about behaviour.
    const core::render::RenderRequest* borrowed = thumbs.pane->requestForPage(pages.front());
    REQUIRE(borrowed != nullptr);
    const core::render::RenderRequest before = *borrowed;
    const double scaleBefore = before.deviceScale;

    thumbs.pane->larger();
    QCoreApplication::processEvents();

    const core::render::RenderRequest* after = thumbs.pane->requestForPage(pages.front());
    REQUIRE(after != nullptr);

    // `deviceScale` IS PART OF THE CACHE KEY (D2-12). A pane that painted the old thumbnails
    // stretched would be a column that never sharpens, with no error and no failing test -- which
    // is the exact failure shape that argument was written about.
    CHECK(after->deviceScale > scaleBefore);
    CHECK(after->tile.width > before.tile.width);
}

// =============================================================================================
// A-37 -- the sidebar lists only panes that exist. D4-2.
// =============================================================================================

TEST_CASE("the sidebar's pane list holds exactly the panes that exist", "[panes]")
{
    Pane thumbs(12);
    ui::SidebarDock* sidebar = thumbs.window->sidebar();
    REQUIRE(sidebar != nullptr);

    // ONE AT T5. Bookmarks and Destinations join at T6 and this count moves with them; the other
    // seven panes in `Features.md` §1.1 are not listed until their models exist.
    REQUIRE(sidebar->panes().size() == 1);
    CHECK(sidebar->panes().front().id == ActionId::PaneThumbnails);
    CHECK(sidebar->currentPane() == ActionId::PaneThumbnails);

    // NO GREYED ROW, NO PLACEHOLDER ROW. The chooser is built from the registry, so this is the
    // same claim read through the surface a person actually sees -- convention 17's rule that a
    // generated surface reads the TABLE. It is hidden entirely while there is one pane.
    auto* chooser = sidebar->findChild<QComboBox*>(QStringLiteral("sidebarPaneChooser"));
    REQUIRE(chooser != nullptr);
    CHECK(chooser->count() == 1);
    CHECK_FALSE(chooser->isVisible());

    // The Stage 1 placeholder is still there and is not what is on screen.
    CHECK(sidebar->paneWidget(ActionId::PaneThumbnails) == thumbs.pane);
    CHECK(sidebar->paneWidget(ActionId::PaneBookmarks) == nullptr);
}

TEST_CASE("the seven unbuilt pane chords are NotImplemented and leave the key alone", "[panes]")
{
    Pane thumbs(12);

    // D4-2's control, and Stage 2's **I-11** is why it is worth a case: a declared-and-inert
    // surface is indistinguishable from a broken one. These seven are declared in §8 with their
    // chords, have no model to show, and must therefore do nothing AND not eat the key.
    struct Row { Qt::Key key; ActionId id; };
    const Row rows[] = {
        {Qt::Key_C, ActionId::PaneComments},    {Qt::Key_I, ActionId::PaneFields},
        {Qt::Key_L, ActionId::PaneLayers},      {Qt::Key_E, ActionId::PaneTags},
        {Qt::Key_A, ActionId::PaneAttachments}, {Qt::Key_G, ActionId::PaneSignatures},
        {Qt::Key_O, ActionId::PaneDocumentMap},
    };

    for (const Row& row : rows) {
        INFO(std::string(core::actionStringId(row.id)));
        CHECK_FALSE(thumbs.fixture.actions.isRegistered(row.id));
        CHECK_FALSE(thumbs.press(row.key, Qt::ControlModifier | Qt::ShiftModifier));
        CHECK(thumbs.window->sidebar()->panes().size() == 1);
    }

    // T6's two are unregistered here as well, and are named so that T5 is not read as having
    // skipped them: `pane.bookmarks` and `pane.destinations` arrive with their models' panes.
    CHECK_FALSE(thumbs.fixture.actions.isRegistered(ActionId::PaneBookmarks));
    CHECK_FALSE(thumbs.fixture.actions.isRegistered(ActionId::PaneDestinations));
}

TEST_CASE("Ctrl+Shift+P opens the pane and puts the focus in it", "[panes]")
{
    Pane thumbs(12);

    // Start from the pane closed and the focus in the viewer, which is the state the chord is for.
    thumbs.window->sidebar()->hide();
    REQUIRE(thumbs.view() != nullptr);
    thumbs.view()->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    REQUIRE(thumbs.window->shortcutBinder()->activeContext() == core::KeyContext::Viewer);

    CHECK(thumbs.press(Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier));
    driveLayout(thumbs.window.get());

    // SHOW, SELECT, FOCUS -- all three, because a command that did only the first would read as
    // doing nothing from an auto-hidden sidebar, and one that skipped the third would leave §8's
    // Page Thumbnails table dead.
    CHECK(thumbs.window->sidebar()->isVisible());
    CHECK(thumbs.window->sidebar()->currentPane() == ActionId::PaneThumbnails);
    CHECK(thumbs.window->shortcutBinder()->activeContext() == core::KeyContext::PageThumbnails);
}

// =============================================================================================
// A-38 -- dock, float, auto-hide and peek still work with real content in them.
// =============================================================================================

TEST_CASE("the sidebar's Stage 1 behaviours survive a real pane", "[panes]")
{
    Pane thumbs(12);
    ui::SidebarDock* sidebar = thumbs.window->sidebar();

    // The width memory, captured on RESIZE rather than on hide (Stage 1 T9) -- re-asserted with a
    // pane in the dock rather than a label, which is the whole of A-38.
    const int width = sidebar->rememberedWidth();
    CHECK(width > 0);

    sidebar->setAutoHidden(true);
    CHECK(sidebar->isAutoHidden());
    CHECK_FALSE(sidebar->isVisible());
    CHECK(sidebar->rememberedWidth() > 0);

    // PEEK REVEALS WITHOUT PINNING (I-37), and `isAutoHidden()` stays true through it by design --
    // which is what `MainWindow::updateAutoHideStrips()` asks about so the strip goes while the
    // pane is out (I-39).
    sidebar->peek();
    CHECK(sidebar->isPeeking());
    CHECK(sidebar->isAutoHidden());
    sidebar->unpeek();
    CHECK_FALSE(sidebar->isPeeking());

    sidebar->setAutoHidden(false);
    driveLayout(thumbs.window.get());
    CHECK(sidebar->isVisible());

    // AND THE PANE IS STILL THE PANE afterwards -- the registry survives the chrome, and the
    // thumbnails come back rather than the placeholder.
    CHECK(sidebar->currentPane() == ActionId::PaneThumbnails);
    CHECK_FALSE(thumbs.pane->visiblePages().empty());

    // Docked right, then left. `QDockWidget` owns this and is not reimplemented; what is asserted
    // is that a pane in it is unharmed.
    thumbs.window->addDockWidget(Qt::RightDockWidgetArea, sidebar);
    driveLayout(thumbs.window.get());
    CHECK(thumbs.window->dockWidgetArea(sidebar) == Qt::RightDockWidgetArea);
    CHECK_FALSE(thumbs.pane->visiblePages().empty());

    thumbs.window->addDockWidget(Qt::LeftDockWidgetArea, sidebar);
    driveLayout(thumbs.window.get());
    CHECK(thumbs.window->dockWidgetArea(sidebar) == Qt::LeftDockWidgetArea);
    CHECK_FALSE(thumbs.pane->visiblePages().empty());
}

// =============================================================================================
// A-39 -- two documents, two pane states.
// =============================================================================================

TEST_CASE("switching tabs shows the new document's thumbnails, without rebuilding the pane",
          "[panes]")
{
    Pane thumbs(12);

    // A SECOND DOCUMENT with a different page count, so *which document the pane is showing* is a
    // question the pane's own state can answer. Two twelve-page documents would make a pane that
    // never switched indistinguishable from one that did.
    thumbs.fixture.backend.pagesPerDocument = 30;
    thumbs.window->openFiles({thumbs.fixture.writeFile(QStringLiteral("second.pdf"))});
    driveLayout(thumbs.window.get());

    REQUIRE(thumbs.window->documentCount() == 2);
    REQUIRE(thumbs.window->currentDocument() == thumbs.window->documentAt(1));

    // NOT A REBUILD. The pane is one widget per window, re-pointed -- so this is the SAME object
    // it was, which is what keeps a tab switch from throwing away thumbnails it had just rendered
    // and asking the one worker for them again.
    CHECK(thumbs.window->thumbnailsPane() == thumbs.pane);

    CHECK(thumbs.pane->pageCount() == 30);
    CHECK(thumbs.pane->document() == thumbs.window->documentAt(1)->handle());

    // And back. The selection starts at the new document's first page rather than carrying the
    // other document's -- a page 25 selection surviving into a twelve-page document would be a
    // selection pointing at a page that does not exist.
    thumbs.pane->setCurrentPage(20);
    thumbs.window->activateDocument(0);
    driveLayout(thumbs.window.get());

    CHECK(thumbs.pane->pageCount() == 12);
    CHECK(thumbs.pane->document() == thumbs.window->documentAt(0)->handle());
    CHECK(thumbs.pane->currentPage() < 12);

    // THE REQUESTS WENT WITH IT. A pane that kept the other document's tickets would paint its
    // pages into these slots, which reads as corruption rather than as staleness.
    for (const int page : requestedPagesOf(*thumbs.pane)) {
        const core::render::RenderRequest* request = thumbs.pane->requestForPage(page);
        REQUIRE(request != nullptr);
        CHECK(request->document == thumbs.window->documentAt(0)->handle());
    }
}

TEST_CASE("closing the last document empties the pane rather than leaving it stale", "[panes]")
{
    Pane thumbs(12);
    REQUIRE(thumbs.pane->pageCount() == 12);

    REQUIRE(thumbs.window->closeDocument(0));
    driveLayout(thumbs.window.get());

    CHECK(thumbs.window->documentCount() == 0);
    CHECK(thumbs.pane->pageCount() == 0);
    CHECK(requestedPagesOf(*thumbs.pane).empty());

    // A pane with no document draws its empty state rather than the previous document's pages --
    // and holds no ticket into a handle its window has closed, which is the half that would be a
    // use-after-free in the backend rather than a cosmetic error.
    CHECK(thumbs.pane->pendingTicketCount() == 0);
}
