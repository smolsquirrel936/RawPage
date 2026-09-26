// Stage 4 T12 -- the WIDGET half: Document Properties, reading mode, the page box's logical labels,
// and the two scrolls D4-14's arithmetic pays for (Fit Visible's, and a destination's). Guide cases
// **A-29**, **A-68**'s widget half, **A-69** and **A-70**
// (`Stage4-ViewingSurface-Testing-Guide.md` §5.13), and the closure of **I-05** (§8).
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS SUITE CANNOT SEE, NAMED HERE RATHER THAN DISCOVERED (convention 19)
//
//   * **No engine, so no real content box.** `uitest::FakeBackend::setContentBox` hands the view a
//     rectangle a case chose. What a real page's objects union to is `tests/render`'s
//     `tst_contentbox.cpp`, over `corpus/ordinary/cropbox-margins.pdf`; what is proved here is that
//     the viewer asks for it, fits to it and scrolls to it.
//   * **The dialog is never `exec()`d.** A modal dialog under the offscreen platform is shown to
//     nobody and never returns, which stopped a whole `ctest` run dead at Stage 1 (I-23). The cases
//     construct the widget and read its labels; that a person can reach it through `Ctrl+D` is the
//     registration's claim and is asserted separately, on the registration.
//   * **Offscreen has one screen at one scale factor.** Every fit here is computed at a device
//     pixel ratio of 1.0. The physical-DPI half is `tst_fitpolicy.cpp`'s arithmetic and manual case
//     M-01's rig.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/doc/Destinations.h"
#include "core/doc/Metadata.h"
#include "core/doc/PageTree.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/DocumentPropertiesDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/Ribbon.h"
#include "ui/shell/SidebarDock.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ViewportController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>

#include <memory>

using namespace rawpage;
using core::ActionId;
using core::render::FitMode;
using ui::DocumentPropertiesDialog;

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

// Convention 12, once: `show()` then `activate()` every layout level. A bare `resize()` marks
// layouts dirty and returns, and offscreen there is no event loop to finish the job.
void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

// A window with one document open and laid out -- the state every case here needs.
struct Shell {
    Fixture                         fixture;
    std::unique_ptr<ui::MainWindow> window;

    explicit Shell(int pages = 8)
    {
        fixture.backend.pagesPerDocument = pages;
    }

    void open()
    {
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1200, 900);
        window->openFiles({fixture.writeFile(QStringLiteral("doc.pdf"))});
        driveLayout(window.get());
        window->raiseToForeground();
        QCoreApplication::processEvents();

        // THE GEOMETRY FIRST. A view the layout gave nothing has a zero viewport, and every fit
        // assertion below would then pass vacuously against `current` (Stage 1's I-36, this
        // stage's shape).
        REQUIRE(view() != nullptr);
        REQUIRE(view()->width() > 200);
        REQUIRE(view()->height() > 200);
    }

    ui::PageView* view() const { return window->currentView(); }
    ui::StatusBar* status() const { return window->statusBar(); }
};

// A4 in points, which is what `FakeBackend` gives every page.
constexpr double kPageWidth = 595.276;
constexpr double kPageHeight = 841.89;

} // namespace

// =============================================================================================
// A-68's widget half -- `Ctrl+3` fits and SCROLLS to the content box.
// =============================================================================================

TEST_CASE("Fit Visible fits the content box rather than the CropBox", "[properties]")
{
    Shell shell(4);

    // A content box in the MIDDLE of the sheet, well inside it on every side -- the shape
    // `cropbox-margins.pdf` carries, chosen here so that a Fit Visible still measuring the page
    // cannot accidentally produce the same number.
    shell.fixture.backend.setContentBox(
        0, core::PdfRect{core::PdfPoint{150.0, 200.0}, core::PdfSize{250.0, 300.0}});
    shell.open();

    ui::PageView* view = shell.view();

    view->setFitMode(FitMode::Page);
    view->refresh();
    QCoreApplication::processEvents();
    const double pageZoom = view->controller().zoom().factor;

    view->setFitMode(FitMode::Visible);
    QCoreApplication::processEvents();
    const double visibleZoom = view->controller().zoom().factor;

    // THE ZOOM, NOT THE MODE. I-28's case asserted `fitMode()` and was green for a month while
    // `Ctrl+3` did what `Ctrl+0` does.
    CHECK(view->controller().fitMode() == FitMode::Visible);
    CHECK(visibleZoom > pageZoom);

    // 250 x 300 pt inside a 595 x 842 sheet is more than twice as much magnification for the same
    // viewport, so the two numbers cannot be confused by a rounding difference.
    CHECK(visibleZoom > pageZoom * 2.0);

    // AND THE VIEWPORT IS ON THE CONTENT. A Fit Visible that only zoomed would size the ink
    // correctly and then show the reader the margin above and to the left of it -- the content box
    // starts 150 pt in and 341 pt down from the page's top, so a scroll still at the page's own
    // corner is the defect this asserts against.
    CHECK(view->controller().scroll().y > 0.0);
}

TEST_CASE("Fit Visible falls back to the CropBox when the page has no content", "[properties]")
{
    // THE CONTROL, and the one that makes the case above mean something: with no content box the
    // product must behave exactly as it did through Stages 2 and 3 rather than blank, crash or
    // clamp to 6400 %. `FakeBackend` answers `NotFound` for any page nothing was set on, which is
    // what the real backend answers for an empty page.
    Shell shell(4);
    shell.open();
    REQUIRE(shell.fixture.backend.contentBoxReads >= 0);

    ui::PageView* view = shell.view();

    view->setFitMode(FitMode::Page);
    view->refresh();
    QCoreApplication::processEvents();
    const double pageZoom = view->controller().zoom().factor;

    view->setFitMode(FitMode::Visible);
    QCoreApplication::processEvents();

    // The fake's pages have no CropBox of their own, so it resolves to the MediaBox and the two
    // fits agree -- which is precisely why a corpus file with three distinct rectangles had to be
    // built before I-28 could be tested at all.
    CHECK(view->controller().zoom().factor == pageZoom);

    // And the view ASKED. A fallback that came from never asking would be indistinguishable from
    // one that asked and got nothing, and only the second is the behaviour D4-14 specifies.
    CHECK(shell.fixture.backend.contentBoxReads > 0);
}

// =============================================================================================
// I-05, CLOSED -- a destination's point and zoom reach the viewport.
// =============================================================================================

TEST_CASE("a destination's XYZ point scrolls the view to it", "[properties]")
{
    Shell shell(8);
    shell.open();
    ui::PageView* view = shell.view();

    // A FIXED ZOOM, so the arithmetic below is checkable rather than dependent on whatever fit the
    // window happened to settle at.
    view->setZoom(core::render::Zoom{1.0});
    view->refresh();
    QCoreApplication::processEvents();

    core::Destination destination;
    destination.pageIndex = 3;
    destination.fit = core::Destination::Fit::XYZ;
    destination.hasX = true;
    destination.hasY = true;
    destination.x = 100.0;
    destination.y = 300.0;

    view->goToDestination(destination);
    QCoreApplication::processEvents();

    CHECK(view->controller().currentPage() == 3);

    // THE POINT IS *INSIDE* THE PAGE, and that is what the scroll has to reflect: y = 300 in PDF
    // space is 541 pt DOWN from an A4 page's top edge, so the viewport must sit below the page's
    // own top by roughly that much in device pixels. Asserted as a band rather than as a number
    // because the margin and the device pixel ratio are the fixture's, not this case's.
    const auto slot = view->layoutEngine().layout().slotForPage(3);
    REQUIRE(slot != nullptr);
    const double scale = view->layoutEngine().layout().presentedScale();
    const double expected = slot->rect.top() + (kPageHeight - 300.0) * scale;
    CHECK(view->controller().scroll().y > expected - 60.0);
    CHECK(view->controller().scroll().y < expected + 60.0);
}

TEST_CASE("an XYZ destination with no values changes nothing but the page", "[properties]")
{
    // `/XYZ null null null` IS THE COMMONEST DESTINATION A PRODUCER WRITES and means *go to this
    // page and change nothing else*. Reading the three absent values as 0 would scroll every such
    // link to the page's bottom-left corner -- and reading a null zoom as a zoom of 0 would clamp
    // to 8 %. Both are the defect this case exists for.
    Shell shell(8);
    shell.open();
    ui::PageView* view = shell.view();

    view->setZoom(core::render::Zoom{1.25});
    view->refresh();
    QCoreApplication::processEvents();

    core::Destination destination;
    destination.pageIndex = 2;
    destination.fit = core::Destination::Fit::XYZ;   // every `has*` stays false

    view->goToDestination(destination);
    QCoreApplication::processEvents();

    CHECK(view->controller().currentPage() == 2);
    CHECK(view->controller().zoom().factor == 1.25);
}

TEST_CASE("a Fit destination sets the whole-page fit and FitB the content fit", "[properties]")
{
    Shell shell(8);
    shell.fixture.backend.setContentBox(
        1, core::PdfRect{core::PdfPoint{150.0, 200.0}, core::PdfSize{250.0, 300.0}});
    shell.open();
    ui::PageView* view = shell.view();

    // The fixture actually carries a content box for the page these destinations name -- asserted
    // rather than assumed, because every zoom claim below is vacuous without it.
    REQUIRE(view->layoutEngine().contentBox(1).has_value());

    core::Destination fit;
    fit.pageIndex = 1;
    fit.fit = core::Destination::Fit::Fit;
    view->goToDestination(fit);
    QCoreApplication::processEvents();
    CHECK(view->controller().fitMode() == FitMode::Page);
    const double pageZoom = view->controller().zoom().factor;

    // `/FitB` is *fit the page's content bounding box*, which is exactly what `FitMode::Visible`
    // now is -- so it is one line rather than a second implementation of D4-14.
    core::Destination fitB;
    fitB.pageIndex = 1;
    fitB.fit = core::Destination::Fit::FitB;
    view->goToDestination(fitB);
    QCoreApplication::processEvents();

    // AND IT LANDED ON THE PAGE IT NAMED. This is the assertion the first version of this task
    // failed: the fit changed the zoom, the zoom re-laid the document out, and the dominant-page
    // snap reported a page nobody navigated to. See `PageView::refresh`'s note on the anchor.
    CHECK(view->controller().currentPage() == 1);
    CHECK(view->controller().fitMode() == FitMode::Visible);
    CHECK(view->controller().zoom().factor > pageZoom);
}

TEST_CASE("an unresolved destination moves nothing", "[properties]")
{
    // A destination whose page is gone is what deleting a page leaves behind, and
    // `corpus/ordinary/dests-named.pdf` carries one on purpose. A viewer that scrolled to page 0
    // instead would be the product deciding the document meant something it did not say.
    Shell shell(8);
    shell.open();
    ui::PageView* view = shell.view();

    view->goToPage(4);
    QCoreApplication::processEvents();
    const int before = view->controller().currentPage();
    REQUIRE(before == 4);

    core::Destination dangling;   // pageIndex stays -1
    view->goToDestination(dangling);
    QCoreApplication::processEvents();

    CHECK(view->controller().currentPage() == before);
}

// =============================================================================================
// A-29 -- the page box reads logical labels.
// =============================================================================================

TEST_CASE("the status bar shows the page label beside the physical index", "[properties]")
{
    Shell shell(8);

    // `outline-labels.pdf`'s own shape: roman front matter, then arabic body from index 3 -- so the
    // FOURTH page is called "1", which is why the status bar cannot print `index + 1`.
    shell.fixture.backend.pageLabels = {
        {0, QStringLiteral("i")},  {1, QStringLiteral("ii")}, {2, QStringLiteral("iii")},
        {3, QStringLiteral("1")},  {4, QStringLiteral("2")},  {5, QStringLiteral("3")},
        {6, QStringLiteral("4")},  {7, QStringLiteral("5")},
    };
    shell.open();

    shell.view()->goToPage(3);
    QCoreApplication::processEvents();

    // THE BOX SHOWS THE LABEL. That is the whole point of reading `/PageLabels`: a reader on the
    // page printed "1" must see "1", not "4".
    CHECK(shell.status()->pageBox()->text() == QStringLiteral("1"));

    // AND THE PHYSICAL INDEX IS STILL VISIBLE, because a document with roman front matter has two
    // page 1s and a status bar showing only the label cannot say which one is on screen.
    CHECK(shell.status()->pageTotal()->text().contains(QStringLiteral("4")));

    // On a page whose label IS its physical number the second form is not used -- "of 8 (page 8)"
    // would be noise that says nothing.
    shell.view()->goToPage(7);
    QCoreApplication::processEvents();
    CHECK(shell.status()->pageBox()->text() == QStringLiteral("5"));
}

TEST_CASE("Go to Page accepts a logical label", "[properties]")
{
    Shell shell(8);
    shell.fixture.backend.pageLabels = {
        {0, QStringLiteral("i")},  {1, QStringLiteral("ii")}, {2, QStringLiteral("iii")},
        {3, QStringLiteral("1")},  {4, QStringLiteral("2")},  {5, QStringLiteral("3")},
        {6, QStringLiteral("A-1")}, {7, QStringLiteral("A-2")},
    };
    shell.open();

    const auto typeInBox = [&shell](const QString& text) {
        shell.status()->pageBox()->setText(text);
        emit shell.status()->pageBox()->returnPressed();
        QCoreApplication::processEvents();
    };

    // A ROMAN LABEL, which no integer parse could ever reach -- before this task the box refused it
    // silently and put the current page back.
    typeInBox(QStringLiteral("iii"));
    CHECK(shell.view()->controller().currentPage() == 2);

    // AND A LABEL WITH A PREFIX.
    typeInBox(QStringLiteral("A-2"));
    CHECK(shell.view()->controller().currentPage() == 7);

    // **THE LABEL WINS OVER THE PHYSICAL NUMBER** (`ui/shell/StatusBar.h` argues it and names what
    // it costs): the box DISPLAYS the label, so typing what was just read must land where it was
    // read. "1" is index 3's label, not page 1.
    typeInBox(QStringLiteral("1"));
    CHECK(shell.view()->controller().currentPage() == 3);
}

TEST_CASE("an unknown label is refused without moving the view", "[properties]")
{
    Shell shell(8);
    shell.fixture.backend.pageLabels = {
        {0, QStringLiteral("i")}, {1, QStringLiteral("ii")}, {2, QStringLiteral("iii")},
        {3, QStringLiteral("1")},
    };
    shell.open();

    shell.view()->goToPage(2);
    QCoreApplication::processEvents();
    const int before = shell.view()->controller().currentPage();

    const auto typeInBox = [&shell](const QString& text) {
        shell.status()->pageBox()->setText(text);
        emit shell.status()->pageBox()->returnPressed();
        QCoreApplication::processEvents();
    };

    // **`iv` AND `A-12` ARE NOT IN THIS DOCUMENT.** The guide's A-29 names both; on a file whose
    // front matter stops at `iii` and which carries no prefixed labels at all, neither resolves --
    // so what is asserted is the half that matters, that a label-shaped input is ACCEPTED as a
    // label query and REFUSED without scrolling, rather than parsed as a number or ignored.
    typeInBox(QStringLiteral("iv"));
    CHECK(shell.view()->controller().currentPage() == before);

    typeInBox(QStringLiteral("A-12"));
    CHECK(shell.view()->controller().currentPage() == before);

    // The box is put back to the current page, which is the feedback: an empty box, or one left
    // holding "A-12", would leave the reader unsure whether it worked.
    CHECK(shell.status()->pageBox()->text() == QStringLiteral("iii"));

    // AND A DOCUMENT WITH NO LABELS STILL TAKES A NUMBER, which is every document before this task
    // and most documents after it.
    Shell plain(8);
    plain.open();
    plain.status()->pageBox()->setText(QStringLiteral("5"));
    emit plain.status()->pageBox()->returnPressed();
    QCoreApplication::processEvents();
    CHECK(plain.view()->controller().currentPage() == 4);
}

// =============================================================================================
// A-69 -- Document Properties shows what it read and edits nothing.
// =============================================================================================

TEST_CASE("Document Properties shows every fact it was given", "[properties]")
{
    DocumentPropertiesDialog::Properties properties;
    properties.info.title = QStringLiteral("RawPage corpus");
    properties.info.author = QStringLiteral("the generator");
    properties.info.producer = QStringLiteral("RawPage corpus generator");
    properties.info.pdfVersion = QStringLiteral("1.7");
    properties.info.creationDate = QStringLiteral("D:20260831000000Z");
    properties.info.modificationDate = QStringLiteral("(Tuesday afternoon)");
    properties.path = QStringLiteral("d:/corpus/text-a4.pdf");
    properties.fileSize = 4096;
    properties.pageCount = 8;
    properties.currentPage = 3;
    properties.currentPageLabel = QStringLiteral("1");
    properties.currentPageSize = core::PdfSize{kPageWidth, kPageHeight};
    properties.repaired = true;
    properties.encrypted = true;

    DocumentPropertiesDialog dialog(properties);

    CHECK(dialog.value(DocumentPropertiesDialog::Field::Title) == QStringLiteral("RawPage corpus"));
    CHECK(dialog.value(DocumentPropertiesDialog::Field::Author) == QStringLiteral("the generator"));
    CHECK(dialog.value(DocumentPropertiesDialog::Field::Version) == QStringLiteral("1.7"));
    CHECK(dialog.value(DocumentPropertiesDialog::Field::Pages) == QStringLiteral("8"));

    // A DATE THAT DOES NOT PARSE IS SHOWN AS IT WAS STORED -- guide case A-28's rule at the surface
    // it was written for. A date the product invented would be a fact about the document that the
    // document does not contain.
    CHECK(dialog.value(DocumentPropertiesDialog::Field::Modified)
          == QStringLiteral("(Tuesday afternoon)"));

    // A date that DOES parse is formatted, so the two are visibly different treatments rather than
    // one lenient one.
    CHECK(dialog.value(DocumentPropertiesDialog::Field::Created)
          != QStringLiteral("D:20260831000000Z"));

    // The page size names WHICH page by its label, because a document with roman front matter has
    // two page 1s and "page 4" would name neither of them as the reader sees it.
    CHECK(dialog.value(DocumentPropertiesDialog::Field::PageSize).contains(QStringLiteral("595")));
    CHECK(dialog.value(DocumentPropertiesDialog::Field::PageSize).contains(QStringLiteral("1")));

    CHECK_FALSE(dialog.value(DocumentPropertiesDialog::Field::Security).isEmpty());
    CHECK_FALSE(dialog.value(DocumentPropertiesDialog::Field::Repaired).isEmpty());
    CHECK(dialog.value(DocumentPropertiesDialog::Field::Path)
          == QStringLiteral("d:/corpus/text-a4.pdf"));

    // **NOTHING IS EDITABLE.** Every value is a `QLabel`, not a read-only `QLineEdit` that looks
    // like a field somebody forgot to enable -- D4-4, and the mechanical form of *this dialog
    // constructs no command*.
    CHECK(dialog.findChildren<QLineEdit*>().isEmpty());
    for (const DocumentPropertiesDialog::Field field :
         {DocumentPropertiesDialog::Field::Title, DocumentPropertiesDialog::Field::Author,
          DocumentPropertiesDialog::Field::Producer, DocumentPropertiesDialog::Field::Path}) {
        const QLabel* value = dialog.valueLabel(field);
        REQUIRE(value != nullptr);
        // Selectable so a support thread can quote it; never editable.
        CHECK((value->textInteractionFlags() & Qt::TextEditable) == 0);
    }
}

TEST_CASE("an absent Info field reads as absent rather than as empty", "[properties]")
{
    // MOST GENERATED FILES HAVE NO `/Info` AT ALL and that is ordinary, not a failure
    // (`core/doc/Metadata.h`). A blank row would look like a field that failed to load.
    DocumentPropertiesDialog::Properties properties;
    properties.pageCount = 1;

    DocumentPropertiesDialog dialog(properties);
    CHECK_FALSE(dialog.value(DocumentPropertiesDialog::Field::Title).isEmpty());
    CHECK_FALSE(dialog.value(DocumentPropertiesDialog::Field::Author).isEmpty());
    CHECK_FALSE(dialog.value(DocumentPropertiesDialog::Field::FileSize).isEmpty());
}

TEST_CASE("the shell composes the properties from the backend", "[properties]")
{
    Shell shell(6);
    shell.open();

    const DocumentPropertiesDialog::Properties properties = shell.window->documentProperties();
    CHECK(properties.pageCount == 6);
    CHECK(properties.currentPageSize.width > 0.0);
    CHECK_FALSE(properties.path.isEmpty());

    // `file.documentProperties` IS REGISTERED, and is DISABLED where there is nobody to ask --
    // convention 16 and Stage 1's I-23. Offscreen there is nobody, so the enablement predicate must
    // say so rather than leaving a modal to hang the suite.
    CHECK(shell.fixture.actions.isRegistered(ActionId::FileDocumentProperties));
    CHECK_FALSE(DocumentPropertiesDialog::canAsk());
    CHECK_FALSE(shell.fixture.actions.isEnabled(ActionId::FileDocumentProperties));
}

// =============================================================================================
// A-70 -- reading mode hides the chrome and restores it EXACTLY.
// =============================================================================================

TEST_CASE("reading mode hides the ribbon and the panes", "[properties]")
{
    Shell shell(4);
    shell.open();

    ui::MainWindow* window = shell.window.get();
    REQUIRE(window->ribbon() != nullptr);
    REQUIRE(window->sidebar() != nullptr);

    window->sidebar()->show();
    QCoreApplication::processEvents();
    REQUIRE(window->ribbon()->isVisible());

    window->setReadingMode(true);
    QCoreApplication::processEvents();

    CHECK(window->isReadingMode());
    CHECK_FALSE(window->ribbon()->isVisible());
    CHECK_FALSE(window->sidebar()->isVisible());

    // THE WINDOW STAYS A WINDOW. This is not `F11`: reading mode is the chrome, the full-screen
    // controller is the window state (D4-12), and the two are independent and compose.
    CHECK_FALSE(window->windowState().testFlag(Qt::WindowFullScreen));

    // AND THE PAGE IS STILL THERE, which is the entire point of hiding the rest.
    REQUIRE(shell.view() != nullptr);
    CHECK(shell.view()->isVisible());
}

TEST_CASE("leaving reading mode restores the same panes and widths", "[properties]")
{
    Shell shell(4);
    shell.open();
    ui::MainWindow* window = shell.window.get();

    window->sidebar()->show();
    window->resizeDocks({window->sidebar()}, {320}, Qt::Horizontal);
    driveLayout(window);

    const int width = window->sidebar()->width();
    const Qt::DockWidgetArea area = window->dockWidgetArea(window->sidebar());
    REQUIRE(width > 0);

    window->setReadingMode(true);
    QCoreApplication::processEvents();
    REQUIRE_FALSE(window->sidebar()->isVisible());

    window->setReadingMode(false);
    driveLayout(window);

    // **THE SAME WINDOW, NOT A DEFAULT ONE.** A second press that put the sidebar back at its
    // default width on the left would be a reading mode that rearranges the window -- and a person
    // who had just dragged that divider would have lost it.
    CHECK(window->isReadingMode() == false);
    CHECK(window->ribbon()->isVisible());
    CHECK(window->sidebar()->isVisible());
    CHECK(window->dockWidgetArea(window->sidebar()) == area);
    CHECK(window->sidebar()->width() == width);
}

TEST_CASE("reading mode restores a hidden pane as hidden", "[properties]")
{
    // THE CONTROL. Restoring *what was there* and restoring *everything* are the same thing when
    // everything was open, and they differ only here: a person who had closed the sidebar before
    // entering reading mode must not find it open on the way out.
    Shell shell(4);
    shell.open();
    ui::MainWindow* window = shell.window.get();

    window->sidebar()->hide();
    QCoreApplication::processEvents();
    REQUIRE_FALSE(window->sidebar()->isVisible());

    window->setReadingMode(true);
    QCoreApplication::processEvents();
    window->setReadingMode(false);
    QCoreApplication::processEvents();

    CHECK_FALSE(window->sidebar()->isVisible());
    CHECK(window->ribbon()->isVisible());
}

TEST_CASE("the reading mode command toggles it", "[properties]")
{
    Shell shell(4);
    shell.open();
    ui::MainWindow* window = shell.window.get();

    REQUIRE(shell.fixture.actions.isRegistered(ActionId::ViewReadingMode));

    // A WINDOW-CHROME COMMAND AND NOT A VIEW ONE: it needs no document, so a person who has just
    // closed their last tab can still get their ribbon back.
    CHECK(shell.fixture.actions.isEnabled(ActionId::ViewReadingMode));

    REQUIRE(shell.fixture.actions.dispatch(ActionId::ViewReadingMode, core::KeyContext::Viewer));
    QCoreApplication::processEvents();
    CHECK(window->isReadingMode());

    REQUIRE(shell.fixture.actions.dispatch(ActionId::ViewReadingMode, core::KeyContext::Viewer));
    QCoreApplication::processEvents();
    CHECK_FALSE(window->isReadingMode());
}

// =============================================================================================
// I-11 -- a fit on a document of MIXED page sizes measures the page it was CHOSEN on.
//
// M-01's walk, 2026-09-26: 400 % on `mixed-page-sizes.pdf`'s landscape page, then `Ctrl+2`, and
// the page came out twice the window's width at 250 %. Two causes, each harmless alone. A zoom
// change kept the raw device scroll offset, so dropping from 400 % left the viewport three times
// further down the document; and a fit was re-satisfied on every refresh against whichever page
// was DOMINANT -- here the A5 page, half the landscape page's width, hence exactly twice the zoom.
//
// What these cases do NOT cover (convention 19): the chord. They call `PageView::setFitMode`, which
// is the whole of what `ViewFitWidth`'s handler does (`MainWindow.cpp`, `registerFit`); that the
// chord reaches it is `tst_viewercommands.cpp`'s. And a device pixel ratio of 1.0 only -- M-01.
// =============================================================================================

namespace {

// `corpus/ordinary/mixed-page-sizes.pdf`'s four pages, in its order -- the file M-01 found this on.
void mixedPageSizes(uitest::FakeBackend& backend)
{
    backend.pageBoxes[1] = core::PdfRect{core::PdfPoint{0.0, 0.0}, core::PdfSize{841.89, 595.276}};
    backend.pageBoxes[2] = core::PdfRect{core::PdfPoint{0.0, 0.0}, core::PdfSize{419.528, 595.276}};
    backend.pageBoxes[3] =
        core::PdfRect{core::PdfPoint{100.0, 100.0}, core::PdfSize{595.276, 841.89}};
}

const core::render::PageSlot& slotOf(const ui::PageView* view, int page)
{
    const core::render::PageSlot* slot = view->layoutEngine().layout().slotForPage(page);
    REQUIRE(slot != nullptr);
    return *slot;
}

// The width a Fit Width page must take: the viewport, less the margin on each side.
double fittedWidth(const ui::PageView* view)
{
    return view->controller().viewportSize().width - 2.0 * view->layoutEngine().options().margin;
}

// M-01 step 5's state: 400 % on the landscape page, the viewport in its LOWER part, where a zoom-out
// that kept the offset lands furthest from it.
void zoomIntoTheLandscapePage(ui::PageView* view)
{
    view->goToPage(1);
    view->setZoom(core::render::Zoom{4.0});
    QCoreApplication::processEvents();
    const core::render::PageSlot& slot = slotOf(view, 1);
    view->controller().setScroll(
        core::DevicePoint{0.0, slot.rect.top() + 0.6 * slot.rect.size.height});
    QCoreApplication::processEvents();
    REQUIRE(view->controller().currentPage() == 1);
}

// What a tile arrival or a scrollbar appearing does between two frames: another refresh, which is
// where the fit used to be re-satisfied against a page nobody chose.
void settle(ui::PageView* view)
{
    for (int i = 0; i < 3; ++i) {
        view->refresh();
        QCoreApplication::processEvents();
    }
}

} // namespace

TEST_CASE("Fit Width from 400 percent fits the page the reader was on", "[properties][I-11]")
{
    Shell shell(4);
    mixedPageSizes(shell.fixture.backend);
    shell.open();
    ui::PageView* view = shell.view();

    zoomIntoTheLandscapePage(view);
    view->setFitMode(FitMode::Width);
    settle(view);

    // THE PAGE FIRST: the reader was on the landscape page and is still on it.
    CHECK(view->controller().currentPage() == 1);
    CHECK(view->controller().fitMode() == FitMode::Width);

    // AND IT FITS THE WIDTH. The defect was this page at twice the viewport's width -- the zoom
    // A5's width asks for -- so a band of a few pixels cannot mistake one for the other.
    CHECK(slotOf(view, 1).rect.size.width > fittedWidth(view) - 3.0);
    CHECK(slotOf(view, 1).rect.size.width < fittedWidth(view) + 3.0);

    // And the viewport shows it: its slot intersects what is visible.
    const core::DeviceRect visible = view->controller().visibleRect();
    CHECK(slotOf(view, 1).rect.bottom() > visible.top());
    CHECK(slotOf(view, 1).rect.top() < visible.bottom());
}

TEST_CASE("Fit Page from 400 percent fits the page the reader was on", "[properties][I-11]")
{
    Shell shell(4);
    mixedPageSizes(shell.fixture.backend);
    shell.open();
    ui::PageView* view = shell.view();

    zoomIntoTheLandscapePage(view);
    view->setFitMode(FitMode::Page);
    settle(view);

    CHECK(view->controller().currentPage() == 1);

    // Inside the viewport on both axes, and touching it on one -- which is what a fit is.
    const core::render::PageSlot& slot = slotOf(view, 1);
    const double margin = view->layoutEngine().options().margin;
    const core::DeviceSize viewport = view->controller().viewportSize();
    CHECK(slot.rect.size.width < viewport.width - 2.0 * margin + 3.0);
    CHECK(slot.rect.size.height < viewport.height - 2.0 * margin + 3.0);
    const bool touches = slot.rect.size.width > viewport.width - 2.0 * margin - 3.0
                         || slot.rect.size.height > viewport.height - 2.0 * margin - 3.0;
    CHECK(touches);
}

TEST_CASE("a fit keeps its zoom while the reader scrolls onto a page of another size",
          "[properties][I-11]")
{
    // THE ANCHOR'S OTHER HALF. A fit chosen on the landscape page keeps that page's zoom while
    // the A5 page scrolls into view -- the zoom following the dominant page is the loop itself --
    // and a NAVIGATION to the A5 page moves the anchor, because going to a page in a fit means
    // fitting that page.
    Shell shell(4);
    mixedPageSizes(shell.fixture.backend);
    shell.open();
    ui::PageView* view = shell.view();

    view->goToPage(1);
    view->setFitMode(FitMode::Width);
    settle(view);
    const double landscapeZoom = view->controller().zoom().factor;

    // AT EVERY REFRESH, not after a batch of them. The unanchored fit does not settle on the wrong
    // zoom, it OSCILLATES -- the A5 page's zoom moves the viewport onto the landscape page, whose
    // zoom moves it back -- so a check after an odd number of refreshes read the right number by
    // coincidence. The first draft of this case did exactly that and passed with the defect planted.
    view->controller().setScroll(core::DevicePoint{0.0, slotOf(view, 2).rect.top()});
    QCoreApplication::processEvents();
    for (int i = 0; i < 4; ++i) {
        view->refresh();
        QCoreApplication::processEvents();
        CHECK(view->controller().zoom().factor == landscapeZoom);
    }
    REQUIRE(view->controller().currentPage() == 2);

    view->goToPage(2);
    settle(view);
    CHECK(view->controller().currentPage() == 2);
    CHECK(slotOf(view, 2).rect.size.width > fittedWidth(view) - 3.0);
    CHECK(slotOf(view, 2).rect.size.width < fittedWidth(view) + 3.0);
}

TEST_CASE("a zoom from a command keeps the reader on their page", "[properties][I-11]")
{
    // I-11's FIRST cause, alone and on a UNIFORM document, where the anchor has nothing to do. The
    // zoom box, Actual Size and the zoom chords changed the zoom under an unchanged device offset,
    // so 100 % -> 400 % on page 6 put the viewport on page 2 -- the ratio of the two zooms.
    Shell shell(12);
    shell.open();
    ui::PageView* view = shell.view();

    view->setZoom(core::render::Zoom{1.0});
    view->goToPage(5);
    QCoreApplication::processEvents();
    REQUIRE(view->controller().currentPage() == 5);

    view->setZoom(core::render::Zoom{4.0});   // the zoom box's path, and Actual Size's
    QCoreApplication::processEvents();
    CHECK(view->controller().currentPage() == 5);

    view->zoomOut();                          // `Ctrl+-`'s path
    QCoreApplication::processEvents();
    CHECK(view->controller().currentPage() == 5);
    CHECK(view->controller().zoom().factor < 4.0);
}
