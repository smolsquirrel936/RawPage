// tst_assemblyshown -- Stage 7 T3 (`Stage7-Assembly-Plan.md` §6), decision D7-3, through the COMPOSED PRODUCT:
// guide case A-29, *shown is saved* through page commands (§3.3, D6-2's invariant), and the composed half of A-31,
// every index-keyed consumer of the window told (`Stage7-Assembly-Testing-Guide.md` §5.5).
//
// The product is `app::CompositionRoot` and `ui::MainWindow` as `main.cpp` builds them (`AnnotProduct.h`), and every
// command runs through the window's own journal -- its show step, its mutation handler, the remap it fans out.
//
// WHAT THIS DOES NOT COVER (convention 19): THE COMMAND IS CONSTRUCTED HERE, not made by a surface -- no surface
// runs a page command before Stage 7 T8, whose pane makes them, and §3.3 asks for the commands through their
// surfaces: A-29 is re-run then (guide A-119). And the commands are T2's three; an insert, a decoration and a crop
// join this sweep with the tasks that build them.

#include <catch2/catch_test_macros.hpp>

#include "tests/composition/AnnotProduct.h"

#include "core/cmd/commands/PageCommands.h"
#include "core/doc/PageMap.h"
#include "core/doc/PageTree.h"
#include "pdf/DocumentBackendImpl.h"
#include "ui/panes/BookmarksPane.h"
#include "ui/panes/DestinationsPane.h"
#include "ui/panes/ThumbnailsPane.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/viewer/SplitViewContainer.h"
#include "ui/viewer/TilePresenter.h"
#include "ui/write/FindReplaceBar.h"

#include <QFile>
#include <QLineEdit>
#include <QTreeWidget>

#include <cmath>
#include <functional>
#include <memory>
#include <set>
#include <vector>

using namespace rawpage;
using annot_product::Product;

namespace {

struct View {
    double scale;
    core::PageRotation rotation;
    const char* name;
};
const View kViews[] = {{1.0, core::PageRotation::None, "72 dpi"},
                       {2.0, core::PageRotation::None, "144 dpi"},
                       {1.0, core::PageRotation::Clockwise90, "72 dpi turned 90"}};

// A whole page as the screen asks for it: the unsaved rotation as the delta between the model's page and the page
// the engine's tree holds at that index (D3-15), `model` null for a file opened fresh.
core::RasterImage pageOf(core::IDocumentBackend& backend, core::DocumentHandle handle, const core::PageTree* model,
                         int index, const View& view)
{
    const core::PageTree& engine = *backend.pages(handle);
    const core::Page* page = engine.at(index);
    REQUIRE(page != nullptr);
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = index;
    request.deviceScale = view.scale;
    request.revision = core::Revision::initial();
    request.flags = core::render::RenderFlags::Annotations;
    request.viewRotation = view.rotation;
    if (model != nullptr) {
        const int delta = (((static_cast<int>(model->rotation(index)) - static_cast<int>(engine.rotation(index))) % 360) + 360) % 360;
        request.pageRotationDelta = core::rotateBy(core::PageRotation::None, delta / 90);
    }
    core::PdfSize size = page->displaySize();
    if (core::swapsAxes(core::combine(request.viewRotation, request.pageRotationDelta))) size = core::PdfSize{size.height, size.width};
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(size.width * view.scale)),
                                          static_cast<int>(std::ceil(size.height * view.scale))};
    auto image = backend.rasterizer(handle)->raster(request);
    REQUIRE(image);
    return image.value();
}

long long differingBytes(const core::RasterImage& a, const core::RasterImage& b)
{
    if (a.width != b.width || a.height != b.height) return std::max(a.pixels.size(), b.pixels.size());
    long long n = 0;
    for (qsizetype i = 0; i < a.pixels.size(); ++i)
        if (a.pixels[i] != b.pixels[i]) ++n;
    return n;
}

// Every page the live engine draws against the same page of `path` opened fresh, in every view. Returns the pages
// compared; every difference fails its own CHECK.
int compareEveryPage(Product& product, const QString& path, const char* step)
{
    core::IDocumentBackend& backend = product.root->services().backend;
    ui::DocumentWindow* document = product.document();
    pdf::DocumentBackendImpl fresh;
    auto opened = fresh.open(path, nullptr);
    REQUIRE(opened);
    const int count = backend.pages(document->handle())->count();
    INFO(step);
    REQUIRE(fresh.pages(opened.value())->count() == count);
    REQUIRE(document->journal()->document().pages().count() == count);
    for (int page = 0; page < count; ++page) {
        for (const View& view : kViews) {
            INFO("page " << page << ", " << view.name);
            CHECK(differingBytes(pageOf(backend, document->handle(), &document->journal()->document().pages(), page, view),
                                 pageOf(fresh, opened.value(), nullptr, page, view))
                  == 0);
        }
    }
    fresh.close(opened.value());
    return count;
}

QString saveCopy(Product& product, int n)
{
    auto saved = product.document()->save();
    REQUIRE(saved);
    REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);
    const QString copy = QDir(product.scratch.path()).filePath(QStringLiteral("saved-%1.pdf").arg(n));
    QFile::remove(copy);
    REQUIRE(QFile::copy(product.openedPath, copy));
    return copy;
}

QString identityAt(Product& product, int index)
{
    core::IDocumentBackend& backend = product.root->services().backend;
    core::text::PageTextModel model;
    REQUIRE(backend.textExtractor(product.document()->handle())->extractPage(product.document()->handle(), index, model));
    return model.text();
}

} // namespace

TEST_CASE("assembly_shown: every page command is shown as it is saved and each undo as the original", "[remap][A-29]")
{
    Product product;
    product.open("assembly/refs-every-kind.pdf");
    const QString original = test_oracle::corpusPath("assembly/refs-every-kind.pdf");
    core::CommandJournal* journal = product.document()->journal();
    REQUIRE(journal != nullptr);

    std::vector<std::pair<const char*, std::unique_ptr<core::Command>>> steps;
    steps.emplace_back("the first page to the end", std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 11));
    steps.emplace_back("the last two to the front", std::make_unique<core::MovePagesCommand>(std::vector<int>{10, 11}, 0));
    steps.emplace_back("the first and last duplicated", std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{0, 11}));
    steps.emplace_back("the second deleted", std::make_unique<core::DeletePagesCommand>(std::vector<int>{1}));
    // Stage 7 T5: pages from another document -- three that inherit every inheritable key, inserted, and a page REPLACED
    // by one of `annots-on-pages.pdf`'s, its comments added after the page's own. The page drawn is the page saved.
    core::IDocumentBackend& backend = product.root->services().backend;
    auto inherited = backend.copyPages(test_oracle::corpusPath("assembly/inherit-resources.pdf"), {0, 1, 2});
    REQUIRE(inherited);
    steps.emplace_back("three pages inserted", std::make_unique<core::InsertPagesCommand>(4, inherited.value()));
    auto annotated = backend.copyPages(test_oracle::corpusPath("assembly/annots-on-pages.pdf"), {3});
    REQUIRE(annotated);
    steps.emplace_back("a page replaced", std::make_unique<core::ReplacePagesCommand>(std::vector<int>{2}, annotated.value()));

    int compared = 0;
    int n = 0;
    for (auto& [name, command] : steps) {
        // A replace saved earlier in this loop: a page is replaced only once a save has written it -- every step here
        // runs on a document the step before saved.
        INFO(name);
        REQUIRE(journal->run(std::move(command)));
        product.settle();
        compared += compareEveryPage(product, saveCopy(product, ++n), name);
    }
    CHECK(compared == 12 + 12 + 14 + 13 + 16 + 16);

    // EACH UNDONE, after the saves: every page as the ORIGINAL file draws it.
    for (std::size_t i = 0; i < steps.size(); ++i) {
        REQUIRE(journal->undo());
        product.settle();
    }
    CHECK(compareEveryPage(product, original, "everything undone") == 12);
}

TEST_CASE("remap: the views, the history, the thumbnails, the selection and the find bar follow their pages", "[remap][A-31]")
{
    Product product;
    product.open("ordinary/pages-900.pdf");
    ui::DocumentWindow* document = product.document();
    ui::PageView* primary = product.view;

    // A SECOND PANE, made first -- a split hands every pane the document again -- and unsynchronized, so each pane
    // holds a page of its own.
    document->split()->setMode(ui::SplitViewContainer::Mode::Vertical);
    document->split()->setSynchronizedScrolling(false);
    product.settle();
    REQUIRE(document->split()->views().size() == 2);
    ui::PageView* second = document->split()->views()[1];
    REQUIRE(second != primary);

    // THE FIND BAR, next, so its jump to the hit is history the two navigations below sit above.
    ui::FindReplaceBar* bar = document->findBar();
    REQUIRE(bar != nullptr);
    bar->open();
    bar->field()->setText(QStringLiteral("page 101 of 900"));
    bar->jumpToCurrent();
    QElapsedTimer timer;
    timer.start();
    while ((bar->isSearching() || bar->hits().empty()) && timer.elapsed() < 30000) product.settle();
    REQUIRE(bar->hits().size() == 1);
    REQUIRE(bar->hits().front().page == 100);

    primary->goToPage(40);
    primary->goToPage(100);
    product.settle();
    REQUIRE(primary->controller().currentPage() == 100);

    // The second pane on a page the move will shift.
    second->goToPage(50);
    product.settle();
    REQUIRE(second->controller().currentPage() == 50);
    REQUIRE(primary->controller().currentPage() == 100);

    // THE THUMBNAILS' SELECTION and A TEXT SELECTION, both on page 100.
    ui::ThumbnailsPane* thumbnails = product.window->thumbnailsPane();
    REQUIRE(thumbnails != nullptr);
    thumbnails->setCurrentPage(100);
    ui::SelectionOverlay* selection = document->selection();
    selection->press(100, core::PdfPoint{50.0, 784.0}, {});
    selection->drag(100, core::PdfPoint{150.0, 784.0});
    selection->release();
    timer.restart();
    while (!selection->isResolved() && timer.elapsed() < 30000) product.settle();
    REQUIRE(selection->hasSelection());
    REQUIRE(selection->selection().firstPage() == 100);

    // PAGE 101 OF 900 MOVED TO INDEX 10.
    REQUIRE(primary->controller().currentPage() == 100);
    REQUIRE(thumbnails->currentPage() == 100);
    const std::set<int> heldBefore = primary->tiles().heldPages();
    REQUIRE(heldBefore.count(100) == 1);
    REQUIRE(document->journal()->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{100}, 10)));
    // BEFORE ANY EVENT IS PUMPED, what the view still paints while the new revision's tiles arrive -- the stale
    // tiles -- is keyed where each page NOW stands: the tiles held at 100 are at 10, and whatever is at 100 is the
    // tile of the page that moved there (old 99), never page 101's pixels under another page (guide §1's S1).
    {
        const core::PageMap map = *core::PageMap::move(900, {100}, 10);
        std::set<int> expected;
        for (const int page : heldBefore) expected.insert(map.newIndexOf(page));
        // A subset: a stale tile no page now on screen wants is let go (`TilePresenter::setDocument`), and the view
        // has followed its page to 10 -- so 10's must be there, and none may be anywhere a held page did not go.
        std::set<int> stale;
        for (const core::render::RenderRequest& request : primary->tiles().staleRequests()) stale.insert(request.pageIndex);
        for (const int page : stale) CHECK(expected.count(page) == 1);
        CHECK(stale.count(10) == 1);
    }
    product.settle();
    REQUIRE(identityAt(product, 10).contains(QStringLiteral("page 101 of 900")));

    // Every consumer follows the PAGE (rule 7: each index read back is graded by what the engine shows there).
    CHECK(primary->controller().currentPage() == 10);
    CHECK(second->controller().currentPage() == 51);
    CHECK(identityAt(product, 51).contains(QStringLiteral("page 51 of 900")));
    CHECK(thumbnails->currentPage() == 10);
    CHECK(selection->hasSelection());
    CHECK(selection->selection().firstPage() == 10);
    REQUIRE(bar->hits().size() == 1);
    CHECK(bar->hits().front().page == 10);
    REQUIRE(selection->searchHits().size() == 1);
    CHECK(selection->searchHits().front().page == 10);
    // BACK RETURNS TO THE PAGE, not the index: the page that was 40 is 41 now.
    primary->goBackView();
    product.settle();
    CHECK(primary->controller().currentPage() == 41);
    CHECK(identityAt(product, 41).contains(QStringLiteral("page 41 of 900")));

    // THE MOVED PAGE DELETED: nothing points at its index any more -- the hit and the selection on it go, the
    // thumbnails and a pane on it take the page that now stands there, and no history entry names it.
    primary->goToPage(10);
    product.settle();
    REQUIRE(document->journal()->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{10})));
    product.settle();
    CHECK(bar->hits().empty());
    CHECK(selection->searchHits().empty());
    CHECK_FALSE(selection->hasSelection());
    CHECK(thumbnails->currentPage() == 10);
    CHECK(primary->controller().currentPage() == 10);
    CHECK(identityAt(product, 10).contains(QStringLiteral("page 11 of 900")));
    for (const ui::ViewportController::ViewState& state : primary->controller().history())
        CHECK_FALSE(identityAt(product, state.page).contains(QStringLiteral("page 101 of 900")));
    CHECK(primary->pageCount() == 899);
    CHECK(second->pageCount() == 899);
}

namespace {

// Every entry of a tree pane, top level first, then its children in order -- each made current and asked where it goes.
std::vector<core::Destination> destinationsOf(QTreeWidget& pane, const std::function<core::Destination()>& selected)
{
    std::vector<core::Destination> out;
    std::vector<QTreeWidgetItem*> stack;
    for (int i = pane.topLevelItemCount() - 1; i >= 0; --i) stack.push_back(pane.topLevelItem(i));
    while (!stack.empty()) {
        QTreeWidgetItem* item = stack.back();
        stack.pop_back();
        pane.setCurrentItem(item);
        out.push_back(selected());
        for (int i = item->childCount() - 1; i >= 0; --i) stack.push_back(item->child(i));
    }
    return out;
}

} // namespace

TEST_CASE("remap: the bookmarks and destinations panes open the page each entry named", "[remap][A-31]")
{
    Product product;
    product.open("assembly/refs-every-kind.pdf");
    ui::BookmarksPane* bookmarks = product.window->bookmarksPane();
    ui::DestinationsPane* named = product.window->destinationsPane();
    REQUIRE(bookmarks != nullptr);
    REQUIRE(named != nullptr);
    const auto shows = [&](const core::Destination& destination) {
        return destination.pageIndex >= 0 ? identityAt(product, destination.pageIndex).left(80) : QString();
    };
    std::vector<QString> before;
    for (const core::Destination& d : destinationsOf(*bookmarks, [&] { return bookmarks->selectedDestination(); }))
        before.push_back(shows(d));
    for (const core::Destination& d : destinationsOf(*named, [&] { return named->selectedDestination(); }))
        before.push_back(shows(d));
    REQUIRE(before.size() > 4);

    // THE FIRST PAGE TO THE END: every page's index changes, and every entry must still open the page it named.
    REQUIRE(product.document()->journal()->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 11)));
    product.settle();
    std::vector<QString> after;
    for (const core::Destination& d : destinationsOf(*bookmarks, [&] { return bookmarks->selectedDestination(); }))
        after.push_back(shows(d));
    for (const core::Destination& d : destinationsOf(*named, [&] { return named->selectedDestination(); }))
        after.push_back(shows(d));
    CHECK(after == before);
}
