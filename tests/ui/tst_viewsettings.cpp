// Stage 2 T15 -- the two new Preferences pages, and the sixteen keys reaching the viewer.
//
// Guide cases **A-64** (*two new Preferences groups are generated, not hand-laid-out*) and the
// half of **A-65** that is not about pixels.
//
// ---------------------------------------------------------------------------------------------
// A-65 IS SPLIT ACROSS TWO SUITES, AND THE SPLIT IS NOT ARBITRARY.
//
// *"Toggling `antialiasText`, `antialiasImages`, `antialiasLineArt`, `lineWeight` and
// `transparencyGrid` each produce a measurably different raster"* is two different claims wearing
// one sentence:
//
//   * **Three of them change PIXELS**, and only the real engine can show that. `tests/render`'s
//     `each anti-aliasing flag measurably changes the raster` measures it — 51,279 pixels for text,
//     9,048 for line art, 23,928 for images.
//   * **`transparencyGrid` changes NO pixel of any raster.** It is a viewer decision: the tile is
//     identical and what is behind it differs, which is why it is asserted here, over a widget,
//     rather than there.
//   * **`lineWeight` changes nothing anywhere**, because PDFium cannot draw a hairline at its true
//     width — guide issue **I-11**, measured in `tests/render`.
//
// So A-65's criterion is corrected rather than quietly failed, the way A-18's was at T7 when
// "encode then decode returns the input" turned out not to be a property JPEG has.
//
// What this file CAN establish about the three raster flags is the half `tests/render` cannot: that
// the preference reaches the request at all. `ui/` may not link an engine (D1-4), so the assertion
// here is on the FLAGS in the tiles the presenter is holding.

#include <catch2/catch_test_macros.hpp>

#include "core/render/RenderRequest.h"
#include "core/settings/Settings.h"
#include "core/settings/SettingsSchema.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/dialogs/SettingsPage.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/TilePresenter.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLayout>
#include <QListWidget>
#include <QSpinBox>
#include <QWheelEvent>

#include <algorithm>

using namespace rawpage;
using core::SettingsGroup;
using core::render::FitMode;
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

int keysInGroup(SettingsGroup group)
{
    int count = 0;
    for (const core::SettingsKeyInfo& info : core::SettingsSchema::allKeys())
        if (info.group == group) ++count;
    return count;
}

} // namespace

// =============================================================================================
// A-64 -- the pages are GENERATED. No widget code per key.
// =============================================================================================

TEST_CASE("the two new Preferences pages are built from the table, not laid out by hand",
          "[viewsettings]")
{
    Fixture fixture;
    ui::PreferencesDialog dialog(fixture.services());
    driveLayout(&dialog);

    // TEN OF 26 GROUPS SHIP -- five when this case was written at Stage 2 T15, six since Stage 3
    // T12 added Documents (D3-9), seven since Stage 4 T8 added Units & Guides with its eleven
    // `units/*` rows, eight since Stage 4 T10 added Full Screen with its five `fullScreen/*` rows,
    // nine since Stage 5 T6 added Search with its ten `search/*` rows (D5-20), and ten since Stage 6
    // T6 added Identity with its three `identity/*` rows (D6-13, guide A-54), and eleven since Stage 6 T8
    // added Commenting with its two `commenting/*` rows and the tool-defaults family (D6-13); corrected in place
    // rather than loosened. The count is asserted as well as the members, because "two new pages
    // appear" is a claim about the list and a member check stays green when another is added by
    // accident.
    const std::vector<SettingsGroup>& pages = ui::PreferencesDialog::shippedPages();
    CHECK(pages.size() == 11);   // S6 T8: Commenting
    CHECK(std::find(pages.begin(), pages.end(), SettingsGroup::PageDisplay) != pages.end());
    CHECK(std::find(pages.begin(), pages.end(), SettingsGroup::PerformanceAndCache) != pages.end());
    CHECK(std::find(pages.begin(), pages.end(), SettingsGroup::FullScreen) != pages.end());
    CHECK(std::find(pages.begin(), pages.end(), SettingsGroup::Search) != pages.end());
    CHECK(std::find(pages.begin(), pages.end(), SettingsGroup::Identity) != pages.end());
    CHECK(std::find(pages.begin(), pages.end(), SettingsGroup::Commenting) != pages.end());

    QListWidget* list = dialog.findChild<QListWidget*>(QStringLiteral("preferencesPageList"));
    REQUIRE(list != nullptr);
    CHECK(list->count() == 11);

    // ONE EDITOR PER DECLARED KEY, and the count comes from the TABLE rather than from a literal --
    // a case that said "11" would be a second copy of the row count and would go stale the day a
    // twelfth `pageDisplay/` key is declared. That is convention 17's whole point: the page follows
    // the `.def`, so the test follows it too.
    for (const SettingsGroup group : {SettingsGroup::PageDisplay,
                                      SettingsGroup::PerformanceAndCache}) {
        ui::SettingsPage page(fixture.settings, group);
        driveLayout(&page);
        INFO("group " << static_cast<int>(group));
        CHECK(page.rowCount() == keysInGroup(group));
        CHECK(page.rowCount() > 0);
    }

    // AND THE ENUM ROWS BECAME COMBO BOXES rather than text fields, which is what
    // `SettingsEditorTraits` decides and what a page built from runtime descriptors could not have
    // known -- the `Choice` editor exists because the type does, not because a list said so.
    ui::SettingsPage pageDisplay(fixture.settings, SettingsGroup::PageDisplay);
    driveLayout(&pageDisplay);
    const QList<QComboBox*> combos = pageDisplay.findChildren<QComboBox*>();
    CHECK(combos.size() == 2);   // layoutMode and defaultZoom
    for (const QComboBox* combo : combos) CHECK(combo->count() >= 4);

    // The three ranged `performance/` rows became spin boxes with the table's own bounds.
    ui::SettingsPage performance(fixture.settings, SettingsGroup::PerformanceAndCache);
    driveLayout(&performance);
    CHECK(performance.findChildren<QSpinBox*>().size() == 4);
    CHECK(performance.findChildren<QCheckBox*>().size() == 1);
}

// =============================================================================================
// The settings reach the viewer -- and reach EVERY open tab, not only the one in front.
// =============================================================================================

TEST_CASE("a document opens at the layout and zoom the settings name", "[viewsettings]")
{
    Fixture fixture;
    fixture.backend.pagesPerDocument = 8;
    fixture.settings.set(core::keys::pageDisplay::layoutMode, LayoutMode::TwoUpContinuous);
    fixture.settings.set(core::keys::pageDisplay::defaultZoom, FitMode::Width);
    fixture.settings.set(core::keys::pageDisplay::rtlBinding, true);

    ui::MainWindow window(fixture.services());
    window.resize(1000, 800);
    window.openFiles({fixture.writeFile(QStringLiteral("opened.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentView();
    REQUIRE(view != nullptr);
    REQUIRE(view->width() > 300);   // A-52's rule, always first

    CHECK(view->controller().layoutMode() == LayoutMode::TwoUpContinuous);
    CHECK(view->controller().fitMode() == FitMode::Width);
    CHECK(view->layoutEngine().options().rtlBinding);
}

TEST_CASE("a default is a default, not a command that overrides the user", "[viewsettings]")
{
    // THE HALF THAT IS EASY TO GET WRONG. `layoutMode` and `defaultZoom` are what a document OPENS
    // at; re-applying them on every change to the group would yank a person's layout back to
    // Single Continuous because they went to Preferences to turn off image smoothing.
    Fixture fixture;
    fixture.backend.pagesPerDocument = 6;

    ui::MainWindow window(fixture.services());
    window.resize(1000, 800);
    window.openFiles({fixture.writeFile(QStringLiteral("mine.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentView();
    REQUIRE(view != nullptr);
    REQUIRE(view->controller().layoutMode() == LayoutMode::SingleContinuous);

    // The user switches this tab to Two-Up.
    view->setLayoutMode(LayoutMode::TwoUp);
    CHECK(view->controller().layoutMode() == LayoutMode::TwoUp);

    // …and then changes an unrelated `pageDisplay/` preference.
    fixture.settings.set(core::keys::pageDisplay::antialiasImages, false);
    QCoreApplication::processEvents();

    // THE LAYOUT IS STILL THEIRS.
    CHECK(view->controller().layoutMode() == LayoutMode::TwoUp);

    // …and the change that WAS made did apply.
    CHECK_FALSE(view->settings().antialiasImages);
}

TEST_CASE("an anti-aliasing preference reaches the tile requests", "[viewsettings]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("flags.pdf"))});
    driveLayout(&window);
    fixture.pumpTiles();

    ui::PageView* view = window.currentView();
    REQUIRE(view != nullptr);

    using core::render::RenderFlags;
    REQUIRE_FALSE(hasFlag(view->renderFlags(), RenderFlags::NoSmoothText));

    std::vector<core::render::RenderRequest> held = view->tiles().heldRequests();
    REQUIRE_FALSE(held.empty());
    for (const core::render::RenderRequest& request : held)
        CHECK_FALSE(hasFlag(request.flags, RenderFlags::NoSmoothText));

    // TURN IT OFF. The preference is *smooth text*; the engine's flag is *no smooth text*, and the
    // inversion happens at the `PageView` boundary rather than in the settings table so that the
    // Preferences dialog does not carry a double negative.
    fixture.settings.set(core::keys::pageDisplay::antialiasText, false);
    QCoreApplication::processEvents();
    CHECK(hasFlag(view->renderFlags(), RenderFlags::NoSmoothText));

    // THE OLD TILES ARE GONE, not left to age out. The flags are part of `RenderRequest`'s key, so
    // the new requests simply miss the cache -- and the tiles already held are now unreachable by
    // any request this view will ever make, so keeping them is holding memory nothing can hit.
    fixture.pumpTiles();
    held = view->tiles().heldRequests();
    REQUIRE_FALSE(held.empty());
    for (const core::render::RenderRequest& request : held)
        CHECK(hasFlag(request.flags, RenderFlags::NoSmoothText));
}

TEST_CASE("a preference reaches every open tab, not only the one in front", "[viewsettings]")
{
    // A person who turns off image smoothing expects it off everywhere. A tab that kept the old
    // value until it was next touched would be a preference that appears not to have applied --
    // which is Stage 1's I-38 in a new place: a setting stored and read by nothing looks exactly
    // like a setting read by only some of the things that should read it.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(1000, 800);
    window.openFiles({fixture.writeFile(QStringLiteral("one.pdf")),
                      fixture.writeFile(QStringLiteral("two.pdf"))});
    driveLayout(&window);
    REQUIRE(window.documentCount() == 2);

    fixture.settings.set(core::keys::pageDisplay::antialiasLineArt, false);
    QCoreApplication::processEvents();

    for (int index = 0; index < window.documentCount(); ++index) {
        ui::DocumentWindow* document = window.documentAt(index);
        REQUIRE(document != nullptr);
        REQUIRE(document->view() != nullptr);
        INFO("tab " << index);
        CHECK_FALSE(document->view()->settings().antialiasLineArt);
    }
}

TEST_CASE("the resolution cap reaches DpiPolicy", "[viewsettings]")
{
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("cap.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentView();
    REQUIRE(view != nullptr);
    CHECK(view->layoutEngine().dpiLimits().maxRenderDpi == 2400.0);

    // A LOW CAP BITES, and `Resolution::upscale` is what says so: the rasterizer produces `dpi` and
    // the presenter paints `upscale` times larger, so the page still occupies the space the zoom
    // asked for. Capping without the compensating factor silently shrinks the page, which reads as
    // a zoom bug -- `DpiPolicy`'s header argues it and this is the first caller that can trip it.
    fixture.settings.set(core::keys::pageDisplay::maxRenderResolution, 96);
    QCoreApplication::processEvents();
    CHECK(view->layoutEngine().dpiLimits().maxRenderDpi == 96.0);

    view->setZoom(core::render::Zoom::percent(800));
    view->refresh();
    CHECK(view->layoutEngine().resolution().isCapped());
    CHECK(view->layoutEngine().resolution().dpi == 96.0);
    CHECK(view->layoutEngine().resolution().upscale > 1.0);
}

TEST_CASE("the wheel obeys pageDisplay/wheelZooms", "[viewsettings]")
{
    Fixture fixture;
    fixture.backend.pagesPerDocument = 12;

    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("wheel.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentView();
    REQUIRE(view != nullptr);

    auto wheel = [&](Qt::KeyboardModifiers modifiers) {
        QWheelEvent event(QPointF(200, 200), view->viewport()->mapToGlobal(QPointF(200, 200)),
                          QPoint(0, 0), QPoint(0, -120), Qt::NoButton, modifiers,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QCoreApplication::processEvents();
    };

    // DEFAULT: the wheel scrolls and `Ctrl`+wheel zooms.
    const double zoomBefore = view->controller().zoom().factor;
    wheel(Qt::NoModifier);
    CHECK(view->controller().zoom().factor == zoomBefore);
    wheel(Qt::ControlModifier);
    CHECK(view->controller().zoom().factor != zoomBefore);

    // WITH THE SETTING ON THEY SWAP OVER -- rather than the wheel doing both. A modifier that does
    // nothing is a modifier people stop reaching for.
    fixture.settings.set(core::keys::pageDisplay::wheelZooms, true);
    QCoreApplication::processEvents();

    const double swapped = view->controller().zoom().factor;
    wheel(Qt::ControlModifier);
    CHECK(view->controller().zoom().factor == swapped);
    wheel(Qt::NoModifier);
    CHECK(view->controller().zoom().factor != swapped);
}

TEST_CASE("performance/prefetchPages asks for pages beyond the viewport", "[viewsettings]")
{
    Fixture fixture;
    fixture.backend.pagesPerDocument = 20;
    fixture.settings.set(core::keys::performance::prefetchPages, 0);

    ui::MainWindow window(fixture.services());
    window.resize(700, 500);
    window.openFiles({fixture.writeFile(QStringLiteral("prefetch.pdf"))});
    driveLayout(&window);
    fixture.pumpTiles();

    ui::PageView* view = window.currentView();
    REQUIRE(view != nullptr);
    const std::size_t withNone = view->tiles().heldPages().size();
    REQUIRE(withNone > 0);

    // COUNTS PAGES, and the assertion is on the number of PAGES held rather than tiles for exactly
    // that reason: within a page the prefetch is `TileGrid`'s ring of tiles, a different knob in a
    // different unit, and a tile count could grow for either reason.
    fixture.settings.set(core::keys::performance::prefetchPages, 3);
    QCoreApplication::processEvents();
    fixture.pumpTiles();

    CHECK(view->tiles().heldPages().size() > withNone);
}

TEST_CASE("the transparency grid changes what is BEHIND the page, not the page", "[viewsettings]")
{
    // A-65's fourth toggle -- AND WRITING THIS CASE IS WHAT FOUND THAT IT NEEDED TO BE A RENDER
    // FLAG. The first implementation painted a checkerboard under the page and stopped there, which
    // shows nothing: the rasterizer fills its bitmap with white before drawing, so a transparent
    // region of the page comes back opaque white and covers the grid completely. A feature that
    // reads back as enabled and displays nothing is Stage 1's I-38 exactly.
    //
    // So `RenderFlags::TransparentBackground` exists, the rasterizer switches its bitmap to BGRA
    // and fills with `0x00000000`, and the flag is part of the cache key -- which is also correct:
    // a page rendered onto transparency is genuinely different pixels from one rendered onto white.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("grid.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentView();
    REQUIRE(view != nullptr);
    REQUIRE(view->width() > 300);

    using core::render::RenderFlags;

    fixture.pumpTiles();
    const std::vector<core::render::RenderRequest> before = view->tiles().heldRequests();
    REQUIRE_FALSE(before.empty());
    for (const core::render::RenderRequest& request : before)
        CHECK_FALSE(hasFlag(request.flags, RenderFlags::TransparentBackground));

    const QImage plain = view->viewport()->grab().toImage();
    REQUIRE_FALSE(plain.isNull());

    fixture.settings.set(core::keys::pageDisplay::transparencyGrid, true);
    QCoreApplication::processEvents();
    CHECK(view->settings().transparencyGrid);
    CHECK(hasFlag(view->renderFlags(), RenderFlags::TransparentBackground));

    // EVERY REQUEST CARRIES IT NOW, which is what makes the grid visible at all -- and the old
    // tiles were dropped rather than left to age out, because they are unreachable by any request
    // this view will make again.
    fixture.pumpTiles();
    const std::vector<core::render::RenderRequest> after = view->tiles().heldRequests();
    REQUIRE_FALSE(after.empty());
    for (const core::render::RenderRequest& request : after)
        CHECK(hasFlag(request.flags, RenderFlags::TransparentBackground));

    // AND THE PIXELS DIFFER, because the page is now transparent where it has no content and the
    // checkerboard behind it shows through. Two greys rather than grey-and-white: a white square is
    // indistinguishable from an opaque white page, which is the one thing this option exists to
    // tell apart.
    QCoreApplication::processEvents();
    const QImage gridded = view->viewport()->grab().toImage();
    REQUIRE_FALSE(gridded.isNull());
    CHECK(plain != gridded);
}

TEST_CASE("pageDisplay/lineWeight is stored and reaches the view, and does nothing -- I-11",
          "[viewsettings]")
{
    // A-65's fifth toggle. It is DECLARED AND INERT, and this case exists so the inertness is
    // recorded in the suite rather than only in a document: the value round-trips, it reaches
    // `ViewSettings`, and it changes no render flag -- because PDFium has no control that draws a
    // hairline at its true width. `tests/render`'s `NO PDFium flag can draw a hairline at its true
    // width` is the measurement behind that claim.
    //
    // The day an engine can honour it, this case is what says the value was already being carried.
    Fixture fixture;
    ui::MainWindow window(fixture.services());
    window.resize(900, 700);
    window.openFiles({fixture.writeFile(QStringLiteral("hairlines.pdf"))});
    driveLayout(&window);

    ui::PageView* view = window.currentView();
    REQUIRE(view != nullptr);
    CHECK_FALSE(view->settings().lineWeight);

    const core::render::RenderFlags before = view->renderFlags();

    fixture.settings.set(core::keys::pageDisplay::lineWeight, true);
    QCoreApplication::processEvents();

    CHECK(view->settings().lineWeight);          // carried
    CHECK(view->renderFlags() == before);        // and it changes nothing -- I-11
}
