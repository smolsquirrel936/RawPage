// Stage 2 T14 -- the status bar's real numbers, the message bar, and Stage 1's I-53.
//
// Guide cases **A-57** (*the status bar shows real numbers*), **A-58** (*the message bar surfaces a
// repair report*) and **A-59** (*a registered-but-unavailable command says WHY*).
//
// ---------------------------------------------------------------------------------------------
// A-59 IS THE ONE TO READ FIRST, BECAUSE ITS ROUTE IS NOT THE OBVIOUS ONE.
//
// Stage 1's M-12 found that **no ribbon control can show the registered-but-refused case**: all the
// registered ribbon ids were unconditionally enabled, so the tooltip that says *not available for
// this document* had no surface that could raise it. T14 does not change that by accident — the
// twenty-five Viewer commands are not on the ribbon either — so this case reaches the distinction
// through `ActionFactory::tooltipFor()`, which is the one place that decides, and through an action
// the factory built.
//
// That is a back door in convention 19's sense and it is named here: **what these cases do NOT
// cover is a person hovering a real ribbon button and reading the sentence.** That is manual case
// M-04's, and it is reachable now in a way it was not at Stage 1, because a document opening and
// closing changes twenty-five commands' enablement while the window is up.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/doc/RepairReport.h"
#include "tests/ui/ColorDistance.h"
#include "tests/ui/ShellFixture.h"
#include "ui/shell/ActionFactory.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/MessageBar.h"
#include "ui/shell/RawPageAction.h"
#include "ui/shell/StatusBar.h"
#include "ui/shell/ThemeManager.h"
#include "ui/viewer/PageView.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPushButton>

using namespace rawpage;

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

// A report with one Degraded finding, which is what a real `corpus/malformed/` file produces and
// what `requiresSaveAs()` answers true for.
core::RepairReport damagedReport()
{
    core::RepairReport report;
    report.setObjectsTotal(40);
    report.add(core::RepairEntry{core::RepairKind::ObjectOffsetWrong,
                                 core::RepairSeverity::Recovered, 12, 4096, 0});
    report.add(core::RepairEntry{core::RepairKind::StreamLengthRecovered,
                                 core::RepairSeverity::Degraded, 13, 5120, 900});
    return report;
}

} // namespace

// =============================================================================================
// A-57 -- the status bar shows real numbers.
// =============================================================================================

TEST_CASE("the status bar shows the real page total and the real zoom", "[chrome]")
{
    Fixture fixture;
    fixture.backend.pagesPerDocument = 7;

    ui::MainWindow window(fixture.services());
    window.resize(1000, 800);
    driveLayout(&window);

    ui::StatusBar* status = window.statusBar();
    REQUIRE(status != nullptr);

    // BEFORE: the em dash, because there is no measurement. Stage 1 showed this and was right to.
    CHECK(status->pageTotal()->text()
          == ui::catalog::translate(ui::catalog::key::StatusPageCountUnknown));
    CHECK(status->displayedPageCount() == 0);

    window.openFiles({fixture.writeFile(QStringLiteral("seven.pdf"))});
    driveLayout(&window);

    // AFTER: the number. "The em dash is gone BECAUSE THERE ARE PAGES NOW, not because it was
    // replaced by a guess" is A-57's own wording, so the count is asserted against the document
    // rather than against a literal.
    CHECK(status->displayedPageCount() == 7);
    CHECK(status->pageTotal()->text()
          == ui::catalog::translate(ui::catalog::key::StatusPageTotal).arg(7));
    CHECK(status->pageBox()->text() == QStringLiteral("1"));   // ONE-BASED on screen
    CHECK(status->pageBox()->isEnabled());

    // The zoom tracks the viewport.
    CHECK(status->zoomValue()->text()
          == ui::catalog::translate(ui::catalog::key::StatusZoomValue).arg(100));

    ui::PageView* view = window.currentView();
    REQUIRE(view != nullptr);
    view->setZoom(core::render::Zoom::percent(200));
    view->refresh();
    CHECK(status->zoomValue()->text()
          == ui::catalog::translate(ui::catalog::key::StatusZoomValue).arg(200));

    // The page number follows the view.
    view->goToPage(4);
    CHECK(status->displayedPage() == 4);
    CHECK(status->pageBox()->text() == QStringLiteral("5"));

    // AND THE EM DASH COMES BACK when the document goes, rather than the last document's count
    // staying on screen over an empty window.
    window.closeAllDocuments();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(status->pageTotal()->text()
          == ui::catalog::translate(ui::catalog::key::StatusPageCountUnknown));
}

TEST_CASE("typing a page number in the status bar navigates, and a typo does not", "[chrome]")
{
    Fixture fixture;
    fixture.backend.pagesPerDocument = 9;

    ui::MainWindow window(fixture.services());
    window.resize(1000, 800);
    window.openFiles({fixture.writeFile(QStringLiteral("nine.pdf"))});
    driveLayout(&window);

    ui::StatusBar* status = window.statusBar();
    ui::PageView* view = window.currentView();
    REQUIRE(view != nullptr);

    // THE PAGE BOX HAS BEEN AN INPUT WITH NO CONSUMER SINCE T9. This is the consumer.
    status->pageBox()->setText(QStringLiteral("6"));
    emit status->pageBox()->returnPressed();
    QCoreApplication::processEvents();
    CHECK(view->controller().currentPage() == 5);

    // A NUMBER OUTSIDE THE DOCUMENT IS NOT A NAVIGATION, and the box is put back rather than left
    // holding it -- an empty box, or one still reading "4000", leaves the user unsure whether it
    // worked. That feedback is the assertion.
    status->pageBox()->setText(QStringLiteral("4000"));
    emit status->pageBox()->returnPressed();
    QCoreApplication::processEvents();
    CHECK(view->controller().currentPage() == 5);
    CHECK(status->pageBox()->text() == QStringLiteral("6"));

    status->pageBox()->setText(QStringLiteral("not a number"));
    emit status->pageBox()->returnPressed();
    QCoreApplication::processEvents();
    CHECK(view->controller().currentPage() == 5);
    CHECK(status->pageBox()->text() == QStringLiteral("6"));
}

// =============================================================================================
// A-58 -- the message bar surfaces a repair report, and shows nothing for a clean file.
// =============================================================================================

TEST_CASE("a clean file shows no message bar at all", "[chrome]")
{
    // THE HALF THAT WOULD BE LOST by a bar saying "no repairs were needed": a sentence nobody needs,
    // which would make the real banner unremarkable. `FakeBackend` returns an EMPTY report, which
    // is the state "this file needed nothing" and is not the same as no report.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("clean.pdf"))});
    driveLayout(&window);

    REQUIRE(window.messageBar() != nullptr);
    CHECK(window.messageBar()->messageCount() == 0);
    CHECK_FALSE(window.messageBar()->isVisible());
}

TEST_CASE("a repaired file shows a banner naming what was reconstructed", "[chrome]")
{
    Fixture fixture;
    fixture.backend.repairs = damagedReport();

    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("damaged.pdf"))});
    driveLayout(&window);

    ui::MessageBar* bar = window.messageBar();
    REQUIRE(bar != nullptr);
    REQUIRE(bar->messageCount() == 1);
    CHECK(bar->isShowing(ui::MessageBar::Kind::Repaired));
    CHECK(bar->isVisible());

    // THE SUMMARY IS THE REPORT'S OWN, resolved from its catalog key -- never a sentence invented
    // here, and never one invented in `core/`, which holds no user-facing English. The banner names
    // the document as well, because a two-tab window otherwise says which file only by position.
    const QString shown = bar->textFor(ui::MessageBar::Kind::Repaired);
    CHECK(shown.contains(QStringLiteral("damaged.pdf")));
    CHECK_FALSE(shown.contains(QStringLiteral("error.repair")));   // the key resolved
    CHECK_FALSE(shown.isEmpty());

    // A ROUTE TO THE DETAIL, which A-58 asks for by name. The button exists; what it opens is
    // asserted through `repairDetailText()` rather than by opening a modal this case would then
    // have to close -- and the detail carries the DIAGNOSTIC CODES, which are the same strings the
    // log holds and are deliberately not translated (see `MessageBar.h`).
    QPushButton* details = bar->findChild<QPushButton*>(QStringLiteral("messageAction"));
    REQUIRE(details != nullptr);

    const QString detail = ui::MessageBar::repairDetailText(fixture.backend.repairs);
    CHECK(detail.contains(QLatin1String(core::repairKindName(core::RepairKind::ObjectOffsetWrong))));
    CHECK(detail.contains(
        QLatin1String(core::repairKindName(core::RepairKind::StreamLengthRecovered))));
    CHECK(detail.contains(QStringLiteral("obj 12")));
    CHECK_FALSE(detail.contains(QStringLiteral("shell.repair")));   // the frame resolved too
}

TEST_CASE("the banner belongs to the tab, not to the window", "[chrome]")
{
    // A "this file was repaired" banner left on screen over the document that replaced it is a
    // FALSE STATEMENT about the file the user is now looking at -- and Stage 3 forces a Save-As on
    // exactly that statement, so the failure is not cosmetic.
    Fixture fixture;
    fixture.backend.repairs = damagedReport();

    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("damaged.pdf"))});
    driveLayout(&window);
    REQUIRE(window.messageBar()->isShowing(ui::MessageBar::Kind::Repaired));

    // The second document is clean -- the fake's report is per-backend, so it is cleared first.
    fixture.backend.repairs = core::RepairReport{};
    window.openFiles({fixture.writeFile(QStringLiteral("clean.pdf"))});
    driveLayout(&window);

    CHECK(window.documentCount() == 2);
    CHECK(window.messageBar()->messageCount() == 0);

    // …and switching back is not asserted, because the fake cannot answer differently per handle.
    // Said rather than skipped silently (convention 19): what IS covered is that the bar is cleared
    // on every tab change, which is the mechanism; whether a per-document report re-appears is the
    // real backend's, and manual case M-03 walks it.
    SUCCEED();
}

TEST_CASE("the banner is painted in its own colour and follows the theme", "[chrome]")
{
    // STAGE 2 ISSUE I-29. The token-level half -- that the banner colour is far from the desk grey
    // in both themes -- is `tst_theme`'s. THIS half is the wiring, measured in PIXELS rather than
    // read back from a palette: the old banner had a perfectly good palette call too, it just
    // named a role that held the wrong colour.
    Fixture fixture;
    fixture.settings.set(core::keys::general::theme, core::Theme::Light);
    fixture.backend.repairs = damagedReport();

    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("damaged.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentView();
    REQUIRE(view != nullptr);

    // THE BANNER ON SCREEN, found fresh each time and required to be the ONLY one. A dismissed
    // banner is `deleteLater()`ed, so until deferred deletes run it is still a child with the same
    // object name -- and opening a document rebuilds the bar more than once. The first draft of
    // this case held a `findChild` pointer, grabbed a stale banner nothing would ever recolour, and
    // failed the theme half while passing the light half only because every banner was light.
    const auto banner = [&window]() -> QWidget* {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const QList<QWidget*> found =
            window.messageBar()->findChildren<QWidget*>(QStringLiteral("messageBanner.Repaired"));
        REQUIRE(found.size() == 1);
        return found.front();
    };

    // (2, 2) is inside the banner's 12/6 px content margin, so it is the fill and not the label.
    const auto fill = [&banner] { return banner()->grab().toImage().pixelColor(2, 2); };
    const auto desk = [view] { return view->palette().color(QPalette::Window); };

    // CLEARLY DIFFERENT, not merely unequal: the old banner painted #f7f7f7 beside a #f3f3f3 desk,
    // which `!=` accepts and nobody can see (`tests/ui/ColorDistance.h`).
    CHECK(fill() == window.theme()->color(ui::ThemeToken::MessageBackground));
    CHECK(uitest::deltaE76(fill(), desk()) >= uitest::kDistinctSurfaceDeltaE);

    // A THEME SWITCH WITH THE BANNER ALREADY ON SCREEN. The banner was built under Light, so a bar
    // that coloured a banner only when it was built would still be showing the light tint here.
    fixture.settings.set(core::keys::general::theme, core::Theme::Dark);
    QCoreApplication::processEvents();
    REQUIRE(window.theme()->resolved() == ui::ThemeManager::Resolved::Dark);
    CHECK(fill() == ui::ThemeManager::colorFor(ui::ThemeToken::MessageBackground,
                                               ui::ThemeManager::Resolved::Dark));
    CHECK(uitest::deltaE76(fill(), desk()) >= uitest::kDistinctSurfaceDeltaE);

    // WHAT THIS DOES NOT COVER (convention 19): the offscreen platform does not paint with the
    // native `windows11` style. The fill is `QWidget::autoFillBackground`, which no style repaints,
    // so the colour above is the colour on Windows too -- but how the Details button and the close
    // glyph look ON that colour is visible only on a real desktop, in both themes.
}

TEST_CASE("the message bar stacks kinds worst-first and dismisses one at a time", "[chrome]")
{
    ui::MessageBar bar;
    CHECK_FALSE(bar.isVisible());

    ui::MessageBar::Message protectedMessage;
    protectedMessage.kind = ui::MessageBar::Kind::Protected;
    protectedMessage.text = QStringLiteral("protected");
    bar.showMessage(protectedMessage);

    ui::MessageBar::Message repaired;
    repaired.kind = ui::MessageBar::Kind::Repaired;
    repaired.text = QStringLiteral("repaired");
    bar.showMessage(repaired);

    CHECK(bar.messageCount() == 2);

    // ONE BANNER PER KIND: showing the same kind again REPLACES rather than stacking, because the
    // second report of the same condition is the same condition.
    repaired.text = QStringLiteral("repaired, again");
    bar.showMessage(repaired);
    CHECK(bar.messageCount() == 2);
    CHECK(bar.textFor(ui::MessageBar::Kind::Repaired) == QStringLiteral("repaired, again"));

    bar.dismiss(ui::MessageBar::Kind::Repaired);
    CHECK(bar.messageCount() == 1);
    CHECK_FALSE(bar.isShowing(ui::MessageBar::Kind::Repaired));
    CHECK(bar.isShowing(ui::MessageBar::Kind::Protected));

    bar.clear();
    CHECK(bar.messageCount() == 0);
    CHECK_FALSE(bar.isVisible());
}

// =============================================================================================
// A-59 -- Stage 1's I-53: a registered-but-unavailable command says WHY.
// =============================================================================================

TEST_CASE("a disabled control says WHICH kind of disabled it is", "[chrome]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    driveLayout(&window);
    window.raiseToForeground();

    const QString zoomIn = ui::catalog::translateAction(core::ActionId::ViewZoomIn);
    const QString reflow = ui::catalog::translateAction(core::ActionId::ViewReflow);

    // 1. DECLARED AND NOT REGISTERED -- about this BUILD. The only one of the three Stage 1 ever
    //    produced, and it is unchanged.
    REQUIRE_FALSE(fixture.actions.isRegistered(core::ActionId::ViewReflow));
    const QString notBuilt =
        ui::ActionFactory::tooltipFor(fixture.actions, core::ActionId::ViewReflow, reflow);
    CHECK(notBuilt
          == ui::catalog::translate(ui::catalog::key::RibbonTooltipUnregistered).arg(reflow));

    // 2. REGISTERED AND REFUSED RIGHT NOW -- about this DOCUMENT, and this is I-53's missing half.
    //    With no document open, `view.zoomIn` is registered and its enablement predicate is false.
    REQUIRE(fixture.actions.isRegistered(core::ActionId::ViewZoomIn));
    REQUIRE_FALSE(fixture.actions.isEnabled(core::ActionId::ViewZoomIn));
    const QString unavailable =
        ui::ActionFactory::tooltipFor(fixture.actions, core::ActionId::ViewZoomIn, zoomIn);
    CHECK(unavailable
          == ui::catalog::translate(ui::catalog::key::TooltipNeedsDocument).arg(zoomIn));

    // THE TWO ARE DIFFERENT, which is the entire finding. A-59's own wording: "two *different*
    // tooltips". Stage 1 built only the first, and the second said nothing at all.
    CHECK(notBuilt != unavailable);

    // 3. REGISTERED AND AVAILABLE -- it says what it does, not why it cannot.
    window.openFiles({fixture.writeFile(QStringLiteral("open.pdf"))});
    driveLayout(&window);
    REQUIRE(fixture.actions.isEnabled(core::ActionId::ViewZoomIn));
    const QString available =
        ui::ActionFactory::tooltipFor(fixture.actions, core::ActionId::ViewZoomIn, zoomIn);
    CHECK(available == zoomIn);
    CHECK(available != unavailable);
}

TEST_CASE("a refusal names its own reason where the registration gave one", "[chrome]")
{
    // The generic sentence is the FALLBACK, not the answer. "Not available for this document" is
    // true of Next Page on the last page and is not the useful thing to say -- I-53's `tooltipKey`
    // exists so a registration can be more specific, and the two page steps are the first commands
    // in the product to use it.
    Fixture fixture;
    fixture.backend.pagesPerDocument = 3;

    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("three.pdf"))});
    driveLayout(&window);
    window.raiseToForeground();

    const QString prevPage = ui::catalog::translateAction(core::ActionId::NavPrevPage);
    const QString nextPage = ui::catalog::translateAction(core::ActionId::NavNextPage);

    // On page 1: Previous Page is refused, and says WHY it is refused rather than that a document
    // is needed -- there plainly is one.
    REQUIRE(window.currentView()->controller().currentPage() == 0);
    CHECK(ui::ActionFactory::tooltipFor(fixture.actions, core::ActionId::NavPrevPage, prevPage)
          == ui::catalog::translate(ui::catalog::key::TooltipAtFirstPage).arg(prevPage));
    CHECK(fixture.actions.isEnabled(core::ActionId::NavNextPage));

    // On the last page the pair swaps over.
    window.currentView()->lastPage();
    CHECK(ui::ActionFactory::tooltipFor(fixture.actions, core::ActionId::NavNextPage, nextPage)
          == ui::catalog::translate(ui::catalog::key::TooltipAtLastPage).arg(nextPage));
    CHECK(fixture.actions.isEnabled(core::ActionId::NavPrevPage));
}

TEST_CASE("an action's tooltip is re-asked when the document changes", "[chrome]")
{
    // THE HALF A TOOLTIP COMPUTED ONCE WOULD LOSE. Enablement changes while the window is open --
    // a document opens, the last tab closes -- and a control that has just become available must
    // stop saying it is not. `ActionFactory::refresh()` is what re-asks, and `updateViewChrome()`
    // is what calls it.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    driveLayout(&window);
    window.raiseToForeground();

    ui::ActionFactory* factory = window.actionFactory();
    REQUIRE(factory != nullptr);

    ui::RawPageAction* action = factory->create(core::ActionId::ViewZoomIn, &window);
    REQUIRE(action != nullptr);
    CHECK_FALSE(action->isEnabled());
    const QString whileClosed = action->toolTip();
    CHECK(whileClosed.contains(ui::catalog::translate(ui::catalog::key::TooltipNeedsDocument)
                                   .arg(action->text())));

    window.openFiles({fixture.writeFile(QStringLiteral("later.pdf"))});
    driveLayout(&window);

    CHECK(action->isEnabled());
    CHECK(action->toolTip() != whileClosed);
}
