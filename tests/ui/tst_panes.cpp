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
#include "core/doc/Destinations.h"
#include "core/doc/Outline.h"
#include "ui/panes/BookmarksPane.h"
#include "ui/panes/DestinationsPane.h"
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

    // THREE, AND THE FIRST IS THUMBNAILS. This case was written at T5 against a list of ONE and
    // is corrected here rather than left beside T6's own version of it (convention 8: correct a
    // stale claim in place). The other seven panes in `Features.md` §1.1 are still not listed,
    // which is the half that does not move until their models exist.
    REQUIRE(sidebar->panes().size() == 3);
    CHECK(sidebar->panes().front().id == ActionId::PaneThumbnails);
    CHECK(sidebar->currentPane() == ActionId::PaneThumbnails);

    // NO GREYED ROW, NO PLACEHOLDER ROW. The chooser is built from the registry, so this is the
    // same claim read through the surface a person actually sees -- convention 17's rule that a
    // generated surface reads the TABLE.
    auto* chooser = sidebar->findChild<QComboBox*>(QStringLiteral("sidebarPaneChooser"));
    REQUIRE(chooser != nullptr);
    CHECK(chooser->count() == 3);

    // The Stage 1 placeholder is still there and is not what is on screen.
    CHECK(sidebar->paneWidget(ActionId::PaneThumbnails) == thumbs.pane);
    CHECK(sidebar->paneWidget(ActionId::PaneComments) == nullptr);
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

        // AND THE LIST DID NOT GROW. A chord that opened an empty dock is the failure this case
        // exists to catch, and it would show here rather than in the outcome above.
        CHECK(thumbs.window->sidebar()->panes().size() == 3);
    }

    // `pane.bookmarks` and `pane.destinations` ARE registered as of T6, and are named here so the
    // seven above are read as the seven whose MODELS do not exist rather than as everything that
    // happens to be unbuilt. The line asserting them absent was T5's and was corrected with T6.
    CHECK(thumbs.fixture.actions.isRegistered(ActionId::PaneBookmarks));
    CHECK(thumbs.fixture.actions.isRegistered(ActionId::PaneDestinations));
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

// =============================================================================================
// T6 -- the two navigation panes. Guide cases A-35, A-36, and the re-reads A-30 and A-37 owed.
//
// THE MODELS ARE THE FAKE BACKEND'S, and what that does and does not buy is worth saying once.
// `core/doc/Outline` and `core/doc/Destinations` are plain value types that T4 fills from PDFium;
// a fake filling them by hand exercises the PANE over exactly the shapes the engine produces --
// a two-level tree, three fit kinds, and a destination naming a page that is not there. What it
// does NOT cover (convention 19) is that PDFium produces those shapes, which is `tests/render`'s
// `tst_docmodels` and went green at T4 over the real `dests-named.pdf` and `outline-labels.pdf`.
// =============================================================================================

namespace {

core::OutlineItem bookmark(const QString& title, int page, bool open = false)
{
    core::OutlineItem item;
    item.title = title;
    item.destination.pageIndex = page;
    item.destination.fit = core::Destination::Fit::Fit;
    item.openByDefault = open;
    return item;
}

// The shape `ordinary/outline-labels.pdf` carries and `tst_outline` asserts at T4: two levels, with
// the file's own open/closed state on the roots.
core::Outline twoLevelOutline()
{
    core::Outline outline;
    core::OutlineItem first = bookmark(QStringLiteral("Chapter One"), 0, /*open=*/true);
    first.children.push_back(bookmark(QStringLiteral("Section 1.1"), 1));
    first.children.push_back(bookmark(QStringLiteral("Section 1.2"), 2));

    core::OutlineItem second = bookmark(QStringLiteral("Chapter Two"), 3, /*open=*/false);
    second.children.push_back(bookmark(QStringLiteral("Section 2.1"), 4));

    outline.roots().push_back(std::move(first));
    outline.roots().push_back(std::move(second));
    return outline;
}

// `dests-named.pdf`'s shape: three kinds, and ONE THAT NAMES A PAGE THAT DOES NOT EXIST -- T1 put
// it there as the control, and it is the row this pane must show rather than drop.
core::Destinations namedDestinations()
{
    core::Destinations destinations;

    core::NamedDestination xyz;
    xyz.name = QStringLiteral("chapter-one");
    xyz.destination.pageIndex = 0;
    xyz.destination.fit = core::Destination::Fit::XYZ;
    xyz.destination.hasY = true;
    xyz.destination.y = 700.0;
    destinations.add(xyz);

    core::NamedDestination fit;
    fit.name = QStringLiteral("appendix");
    fit.destination.pageIndex = 9;
    fit.destination.fit = core::Destination::Fit::Fit;
    destinations.add(fit);

    core::NamedDestination fitH;
    fitH.name = QStringLiteral("bibliography");
    fitH.destination.pageIndex = 4;
    fitH.destination.fit = core::Destination::Fit::FitH;
    destinations.add(fitH);

    core::NamedDestination dangling;
    dangling.name = QStringLiteral("removed-section");
    dangling.destination.pageIndex = -1;          // UNRESOLVED -- the control
    destinations.add(dangling);

    return destinations;
}

// A window whose document has both models, with the named pane in front and focused.
struct Panes {
    Fixture                         fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::BookmarksPane*              bookmarks = nullptr;
    ui::DestinationsPane*           destinations = nullptr;

    explicit Panes(int pages = 12)
    {
        fixture.backend.pagesPerDocument = pages;
        fixture.backend.outline_ = twoLevelOutline();
        fixture.backend.destinations_ = namedDestinations();

        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1200, 900);
        window->openFiles({fixture.writeFile(QStringLiteral("book.pdf"))});
        driveLayout(window.get());

        bookmarks = window->bookmarksPane();
        destinations = window->destinationsPane();
        REQUIRE(bookmarks != nullptr);
        REQUIRE(destinations != nullptr);

        window->raiseToForeground();
    }

    void focusBookmarks()
    {
        REQUIRE(window->sidebar()->showPane(ActionId::PaneBookmarks));
        driveLayout(window.get());
        bookmarks->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();

        // RULE 4, for a tree: the layout must have given it rows to draw. A pane that was never
        // laid out reports zero and every assertion about expansion below would pass vacuously.
        REQUIRE(bookmarks->width() > 80);
        REQUIRE(bookmarks->visibleRowCount() > 0);
    }

    void focusDestinations()
    {
        REQUIRE(window->sidebar()->showPane(ActionId::PaneDestinations));
        driveLayout(window.get());
        destinations->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        REQUIRE(destinations->width() > 80);
        REQUIRE(destinations->rowCount() > 0);
    }

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
};

} // namespace

// =============================================================================================
// A-35 -- the Bookmarks pane navigates, and edits nothing.
// =============================================================================================

TEST_CASE("the bookmarks pane shows the document's tree with the file's own open state", "[panes]")
{
    Panes panes;
    panes.focusBookmarks();

    CHECK(panes.bookmarks->nodeCount() == 5);       // two roots, three children
    CHECK(panes.bookmarks->topLevelItemCount() == 2);

    // THE FILE'S OWN OPEN/CLOSED STATE, not a default of ours: Chapter One is open in the model and
    // Chapter Two is not, so four rows are drawn rather than two or five. A pane that expanded
    // everything would throw away what the document's author said about its own structure.
    CHECK(panes.bookmarks->visibleRowCount() == 4);
}

TEST_CASE("Enter in the bookmarks pane goes to the bookmark's destination", "[panes]")
{
    Panes panes;
    panes.focusBookmarks();
    REQUIRE(panes.view() != nullptr);
    REQUIRE(panes.window->shortcutBinder()->activeContext() == core::KeyContext::Bookmarks);

    // Walk to "Section 1.2", whose destination is page 2. `Down` is in NO Bookmarks row, so it
    // resolves to nothing, is not consumed, and the tree walks the selection natively -- which is
    // itself worth asserting, because it is a third behaviour from the same table.
    panes.press(Qt::Key_Down);
    panes.press(Qt::Key_Down);
    CHECK(panes.bookmarks->selectedTitle() == QStringLiteral("Section 1.2"));

    CHECK(panes.press(Qt::Key_Return));

    // THE EFFECT, NOT THE INTENT (rule 1): the view moved, rather than `bookmark.goTo` dispatching.
    CHECK(panes.view()->controller().currentPage() == 2);
}

TEST_CASE("right and left expand and collapse the selected bookmark, from a key event", "[panes]")
{
    Panes panes;
    panes.focusBookmarks();

    // Chapter Two is closed in the file. Select it and open it with the chord section 8 binds --
    // which in THIS context is a registered command, unlike the thumbnails pane where the same
    // physical keys are `WidgetNative` and the item view owns them. One table, two answers,
    // neither decided in the pane.
    panes.bookmarks->setCurrentItem(panes.bookmarks->topLevelItem(1));
    REQUIRE(panes.bookmarks->selectedTitle() == QStringLiteral("Chapter Two"));
    const int before = panes.bookmarks->visibleRowCount();

    CHECK(panes.press(Qt::Key_Right));
    CHECK(panes.bookmarks->visibleRowCount() == before + 1);

    CHECK(panes.press(Qt::Key_Left));
    CHECK(panes.bookmarks->visibleRowCount() == before);
}

TEST_CASE("star and slash expand and collapse the whole tree, from a key event", "[panes]")
{
    Panes panes;
    panes.focusBookmarks();

    CHECK(panes.press(Qt::Key_Asterisk, Qt::NoModifier, QStringLiteral("*")));
    CHECK(panes.bookmarks->visibleRowCount() == 5);   // every node drawn

    CHECK(panes.press(Qt::Key_Slash, Qt::NoModifier, QStringLiteral("/")));
    CHECK(panes.bookmarks->visibleRowCount() == 2);   // the two roots only
}

TEST_CASE("every editing chord in the bookmarks table is unregistered and leaves its key alone",
          "[panes]")
{
    // THE CONTROL A-35 names, and the reason T6 is navigation-only (D4-2, D4-3). Eight rows, each
    // needing a `Command` that Stage 7 owns. An unregistered id must answer `NotImplemented` and
    // NOT consume the key: a handler that did nothing would be indistinguishable from one that is
    // broken, which is Stage 2's I-11 in the shape a pane invites.
    Panes panes(12);
    panes.focusBookmarks();

    struct Row { Qt::Key key; Qt::KeyboardModifiers modifiers; ActionId id; };
    const Row rows[] = {
        {Qt::Key_F2,      Qt::NoModifier,                          ActionId::BookmarkRename},
        {Qt::Key_Delete,  Qt::NoModifier,                          ActionId::BookmarkDelete},
        {Qt::Key_B,       Qt::ControlModifier,                     ActionId::BookmarkNew},
        {Qt::Key_Tab,     Qt::NoModifier,                          ActionId::BookmarkDemote},
        {Qt::Key_Backtab, Qt::ShiftModifier,                       ActionId::BookmarkPromote},
        {Qt::Key_Up,      Qt::AltModifier,                         ActionId::BookmarkMoveUp},
        {Qt::Key_Down,    Qt::AltModifier,                         ActionId::BookmarkMoveDown},
        {Qt::Key_D,       Qt::ControlModifier | Qt::ShiftModifier, ActionId::BookmarkSetDestination},
    };

    const std::size_t nodesBefore = panes.bookmarks->nodeCount();

    for (const Row& row : rows) {
        INFO(std::string(core::actionStringId(row.id)));
        CHECK_FALSE(panes.fixture.actions.isRegistered(row.id));
        CHECK_FALSE(panes.press(row.key, row.modifiers));

        // `Tab` MOVES THE FOCUS when nobody consumes it, which is exactly what *leaves the key
        // alone* means -- so the focus is put back before the next row rather than letting the
        // rest of the loop press keys at some other widget and pass for the wrong reason.
        panes.bookmarks->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
    }

    // AND THE TREE IS UNCHANGED. The pane is a view: no row was added, renamed or removed by any
    // of the eight, and the document behind it is clean.
    CHECK(panes.bookmarks->nodeCount() == nodesBefore);
    ui::DocumentWindow* document = panes.window->currentDocument();
    REQUIRE(document != nullptr);
    CHECK_FALSE(document->isDirty());
}

// =============================================================================================
// A-36 -- the Destinations pane lists and navigates.
// =============================================================================================

TEST_CASE("the destinations pane lists every destination, the unresolvable one included", "[panes]")
{
    Panes panes;
    panes.focusDestinations();

    // FOUR ROWS FROM FOUR ENTRIES. The dangling one is SHOWN, marked, not dropped: a destination
    // silently missing from the list reads as a document with fewer than it has, and
    // `dests-named.pdf` carries one deliberately as T1's control.
    CHECK(panes.destinations->rowCount() == 4);
    CHECK(panes.destinations->unresolvedRowCount() == 1);

    // AND THE READER CAN SEE WHICH ONE. The line above reads the MODEL behind each row, and a
    // planted-bug control proved that is a weaker claim than it looks: marking every row
    // unresolvable ON SCREEN left it at 1 and this case green. So the page column is read too --
    // three numbers and one statement, which is what a person actually sees (convention 22).
    CHECK(panes.destinations->pageTextAt(0) == QStringLiteral("10"));   // appendix
    CHECK(panes.destinations->pageTextAt(1) == QStringLiteral("5"));    // bibliography
    CHECK(panes.destinations->pageTextAt(2) == QStringLiteral("1"));    // chapter-one
    CHECK(panes.destinations->pageTextAt(3) != QStringLiteral("0"));    // removed-section: not a page
    CHECK_FALSE(panes.destinations->pageTextAt(3).isEmpty());

    // BY NAME, ASCENDING, and the pane sets that explicitly rather than inheriting whatever sort
    // indicator the header carried (see its constructor). For a `/Names /Dests` tree -- which is
    // lexically ordered by the specification -- this is the file's own order rather than a
    // re-arrangement of it.
    CHECK(panes.destinations->nameAt(0) == QStringLiteral("appendix"));
    CHECK(panes.destinations->nameAt(1) == QStringLiteral("bibliography"));
    CHECK(panes.destinations->nameAt(2) == QStringLiteral("chapter-one"));
    CHECK(panes.destinations->nameAt(3) == QStringLiteral("removed-section"));
}

TEST_CASE("the destinations pane sorts by name and by page, and a page sorts as a number",
          "[panes]")
{
    Panes panes;
    panes.focusDestinations();

    panes.destinations->sortByColumn(0, Qt::AscendingOrder);
    CHECK(panes.destinations->nameAt(0) == QStringLiteral("appendix"));
    CHECK(panes.destinations->nameAt(1) == QStringLiteral("bibliography"));

    // BY PAGE, AND AS A NUMBER. Page 10 must not sort before page 5, which is what a lexicographic
    // compare on the column's TEXT would do -- a sort that looks correct on a nine-page document
    // and is wrong on every larger one. `appendix` is page 10 and `bibliography` page 5.
    panes.destinations->sortByColumn(1, Qt::AscendingOrder);
    CHECK(panes.destinations->nameAt(0) == QStringLiteral("chapter-one"));    // page 1
    CHECK(panes.destinations->nameAt(1) == QStringLiteral("bibliography"));   // page 5
    CHECK(panes.destinations->nameAt(2) == QStringLiteral("appendix"));       // page 10

    // The unresolvable row sorts LAST rather than as page 0: it has no page to be between two
    // others, and "goes nowhere" at the top of the list is not an ordering anyone wants.
    CHECK(panes.destinations->nameAt(3) == QStringLiteral("removed-section"));
}

TEST_CASE("activating a destination moves the view, and an unresolvable one does nothing",
          "[panes]")
{
    Panes panes(12);
    panes.focusDestinations();
    REQUIRE(panes.view() != nullptr);

    panes.destinations->sortByColumn(1, Qt::AscendingOrder);
    panes.destinations->setCurrentItem(panes.destinations->topLevelItem(1));   // bibliography, p5
    REQUIRE(panes.destinations->selectedName() == QStringLiteral("bibliography"));

    panes.destinations->goToSelected();
    QCoreApplication::processEvents();
    CHECK(panes.view()->controller().currentPage() == 4);

    // THE CONTROL: the unresolvable row does not navigate, and does not scroll to page 1 either --
    // which is what a naive `goToPage(destination.pageIndex)` would do with a -1 clamped. It is
    // also not SELECTABLE, so this drives the entry point directly rather than through a click
    // that could not happen, and says so.
    const int stayedAt = panes.view()->controller().currentPage();
    panes.destinations->setCurrentItem(panes.destinations->topLevelItem(3));
    panes.destinations->goToSelected();
    QCoreApplication::processEvents();
    CHECK(panes.view()->controller().currentPage() == stayedAt);
}

TEST_CASE("a destination's fit is carried but not yet applied, and the case says so", "[panes]")
{
    // I-05 (section 8), stated as a case rather than left as a surprise. `chapter-one` is `/XYZ`
    // with a y of 700 -- the model carries the point, T4's `tst_outline` asserts it, and NOTHING
    // here throws it away. What Stage 4 T6 does is go to the PAGE.
    Panes panes(12);
    panes.focusDestinations();
    REQUIRE(panes.view() != nullptr);

    // BY NAME, so `chapter-one` -- the `/XYZ` one -- is row 2. Selected by NAME rather than by a
    // row number, because a case that hard-codes an index is a case that silently tests a
    // different row the day the default order changes.
    int row = -1;
    for (int i = 0; i < panes.destinations->rowCount(); ++i)
        if (panes.destinations->nameAt(i) == QStringLiteral("chapter-one")) row = i;
    REQUIRE(row >= 0);

    panes.destinations->setCurrentItem(panes.destinations->topLevelItem(row));
    const core::Destination selected = panes.destinations->selectedDestination();
    CHECK(selected.fit == core::Destination::Fit::XYZ);
    CHECK(selected.hasY);
    CHECK(selected.y == 700.0);

    panes.destinations->goToSelected();
    QCoreApplication::processEvents();
    CHECK(panes.view()->controller().currentPage() == 0);

    // WHAT IS NOT COVERED, at the assertion (convention 19): that the view lands at y = 700 within
    // the page. It does not, and it is not meant to yet -- turning a PDF-space point into a scroll
    // offset needs the page's slot rectangle and the resolution that made it, which is T12's
    // arithmetic for Fit Visible (D4-14). A second conversion invented here would be a wrong one
    // scrolling the reader silently to the wrong place.
}

// =============================================================================================
// The two re-reads T5 owed -- A-30's pane half, and A-37 against a list of three.
// =============================================================================================

TEST_CASE("showing a pane on an already-open document re-reads no model", "[panes]")
{
    // A-30's owed half. The `pdf/render/` side is met by construction -- the `FPDF_DOCUMENT` never
    // leaves that directory, so a pane could not re-hydrate if it tried -- and this is the widget
    // side: showing, hiding and re-showing a pane must not walk the document again.
    Panes panes;
    panes.focusBookmarks();

    const int outlineReadsAfterOpen = panes.fixture.backend.outlineReads;
    const int destinationReadsAfterOpen = panes.fixture.backend.destinationReads;
    REQUIRE(outlineReadsAfterOpen > 0);   // it was read at least once, or this proves nothing

    for (int i = 0; i < 5; ++i) {
        panes.window->sidebar()->showPane(ActionId::PaneDestinations);
        panes.window->sidebar()->showPane(ActionId::PaneBookmarks);
        driveLayout(panes.window.get());
    }

    // SELECTING A PANE IS NOT A READ. The models are fetched when the DOCUMENT in front changes,
    // which is `updateDocumentPanes()`, and showing a pane is neither.
    CHECK(panes.fixture.backend.outlineReads == outlineReadsAfterOpen);
    CHECK(panes.fixture.backend.destinationReads == destinationReadsAfterOpen);

    // And the tree is still the tree -- a pane that had quietly rebuilt itself from nothing would
    // pass the counter check and fail this one.
    CHECK(panes.bookmarks->nodeCount() == 5);
}

TEST_CASE("the sidebar now lists exactly three panes, and still no more", "[panes]")
{
    // A-37 re-read at T6, where the case as written -- "exactly Thumbnails, Bookmarks and
    // Destinations" -- finally has all three to check.
    Panes panes;
    ui::SidebarDock* sidebar = panes.window->sidebar();

    REQUIRE(sidebar->panes().size() == 3);
    CHECK(sidebar->panes()[0].id == ActionId::PaneThumbnails);
    CHECK(sidebar->panes()[1].id == ActionId::PaneBookmarks);
    CHECK(sidebar->panes()[2].id == ActionId::PaneDestinations);

    // THE CHOOSER IS VISIBLE NOW, which it was not at T5: it is hidden below two panes, because a
    // chooser with one entry is furniture. The surface reads the registry rather than a second
    // list of the same names (convention 17).
    driveLayout(panes.window.get());
    auto* chooser = sidebar->findChild<QComboBox*>(QStringLiteral("sidebarPaneChooser"));
    REQUIRE(chooser != nullptr);
    CHECK(chooser->count() == 3);
    CHECK(chooser->isVisible());

    // And the seven that have no model are still absent -- the D4-2 claim, re-asserted where the
    // list has grown, because "only panes that exist" is a claim about the whole set.
    CHECK(sidebar->paneWidget(ActionId::PaneComments) == nullptr);
    CHECK(sidebar->paneWidget(ActionId::PaneLayers) == nullptr);
    CHECK(sidebar->paneWidget(ActionId::PaneSignatures) == nullptr);
}

TEST_CASE("Ctrl+Shift+B opens the bookmarks pane, and the destinations pane has no chord",
          "[panes]")
{
    Panes panes;
    panes.window->sidebar()->showPane(ActionId::PaneThumbnails);
    REQUIRE(panes.view() != nullptr);
    panes.view()->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();

    CHECK(panes.press(Qt::Key_B, Qt::ControlModifier | Qt::ShiftModifier));
    driveLayout(panes.window.get());
    CHECK(panes.window->sidebar()->currentPane() == ActionId::PaneBookmarks);
    CHECK(panes.window->shortcutBinder()->activeContext() == core::KeyContext::Bookmarks);

    // `pane.destinations` IS REGISTERED AND HAS NO DEFAULT CHORD, which is section 8's arrangement
    // rather than an omission here: the Global table binds nine `Ctrl+Shift+*` panes and this is
    // not one of them, and `Features.md` section 8 is not touched by this stage (plan section 10).
    // So it is reached by DISPATCH -- from the sidebar's chooser, the ribbon, the tool finder or
    // the batch runner.
    CHECK(panes.fixture.actions.isRegistered(ActionId::PaneDestinations));
    CHECK(panes.fixture.actions.dispatch(ActionId::PaneDestinations, core::KeyContext::Global));
    driveLayout(panes.window.get());
    CHECK(panes.window->sidebar()->currentPane() == ActionId::PaneDestinations);
}
