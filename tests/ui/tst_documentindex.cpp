// tst_documentindex -- the text index as the PRODUCT's windows use it. Stage 5 T6, decision D5-4; the widget
// halves of guide cases A-62, A-64 and A-66 (`Stage5-TextSearch-Testing-Guide.md` §5.8).
//
// `tests/unit/tst_index.cpp` proves the index's bookkeeping with handles attached by hand. What only the
// product can get wrong is WHO attaches WHAT: that `window.new`'s companion lands on its leader's entry
// rather than one of its own (Stage 4 T14 measured what keying by handle costs: 1,369 cross-document
// evictions), that a selection reads its pages from there, that opening and scrolling a document asks for
// no text at all, and that a rotation run through the journal reaches the index. Those are these cases.
//
// WHAT IT CANNOT CLAIM (convention 19): the text is `ShellFixture`'s fake -- five lines a page -- so nothing
// here is about the corpus, and the search below is driven through a `SearchEngine` THIS FILE owns over the
// fixture's index: the product composes the engine with its first widget, Stage 5 T7's find bar
// (`app/Services.h` says why it is not composed before), so "a search in the second window" here is a search
// of the second window's ENTRY, which is the claim A-66 makes about the index.

#include <catch2/catch_test_macros.hpp>

#include "tests/ui/ShellFixture.h"

#include "core/cmd/ActionId.h"
#include "core/text/DocumentIndex.h"
#include "core/text/SearchEngine.h"
#include "core/text/SearchQuery.h"
#include "ui/dialogs/RotatePagesDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SelectionOverlay.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLayout>
#include <QScrollBar>

#include <memory>
#include <optional>

using namespace rawpage;
using core::ActionId;
using core::text::DocumentIndex;
using core::text::SearchEngine;

namespace {

struct Fixture : uitest::ShellFixture {
    // EDITABLE: the fake backend states a document's identity only for a document it assesses as editable
    // (`FakeBackend::documentId`), and an identity is what lets two handles share an entry.
    Fixture() { backend.editable = true; }

    QString writePdf(const QString& name)
    {
        const QString path = QDir(directory.path()).filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("%PDF-1.7\n% the fake backend decides, not these bytes\n");
        file.write(name.toUtf8());   // two files, two identities
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

// `tst_arranger.cpp`'s desk: a first window, and whatever `window.new` opens, torn down in order.
struct Desk {
    Fixture                         fixture;
    std::unique_ptr<ui::MainWindow> first;
    // Declared AFTER the fixture, so destroyed BEFORE its index -- `CompositionRoot`'s order.
    std::unique_ptr<SearchEngine>   engine;

    explicit Desk(int pages = 8)
    {
        fixture.backend.pagesPerDocument = pages;
        first = std::make_unique<ui::MainWindow>(fixture.services());
        first->resize(900, 700);
        engine = std::make_unique<SearchEngine>(fixture.documentIndex, SearchEngine::Config{2});
    }

    ~Desk()
    {
        engine.reset();
        for (ui::MainWindow* window : ui::MainWindow::shellWindows())
            if (window != first.get() && window->testAttribute(Qt::WA_DeleteOnClose)) window->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        first.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    ui::DocumentWindow* open(const QString& name)
    {
        first->openFiles({fixture.writePdf(name)});
        driveLayout(first.get());
        first->raiseToForeground();
        fixture.pumpTiles();
        return first->currentDocument();
    }

    void dispatch(ActionId id) { REQUIRE(fixture.actions.dispatch(id, core::KeyContext::Global)); }

    SearchEngine::Outcome search(DocumentIndex::EntryId entry, const QString& pattern)
    {
        std::optional<SearchEngine::Outcome> outcome;
        SearchEngine::Handlers handlers;
        handlers.finished = [&](const SearchEngine::Outcome& o) { outcome = o; };
        core::text::SearchQuery query;
        query.pattern = pattern;
        REQUIRE(engine->start(entry, query, handlers));
        QElapsedTimer timer;
        timer.start();
        while (!outcome && timer.elapsed() < 10000) {
            fixture.pumpTiles(200);
            engine->waitForIdle(50);
            engine->deliverReady();
        }
        REQUIRE(outcome.has_value());
        return *outcome;
    }

    // Pages arrive on the scheduler's pump; nothing else is needed for a selection's asks.
    void pumpText() { fixture.pumpTiles(); }
};

} // namespace

// =============================================================================================
// A-66 -- two windows over one file share ONE index
// =============================================================================================

TEST_CASE("window.new puts the second view on the first view's index entry", "[documentindex][A-66]")
{
    Desk desk;
    ui::DocumentWindow* leader = desk.open(QStringLiteral("shared.pdf"));
    REQUIRE(leader != nullptr);
    const DocumentIndex::EntryId entry = leader->indexEntry();
    REQUIRE(entry != 0);

    desk.dispatch(ActionId::WindowNew);
    REQUIRE(ui::MainWindow::shellWindows().size() == 2);
    ui::MainWindow* second = ui::MainWindow::shellWindows().at(1);
    driveLayout(second);
    ui::DocumentWindow* companion = second->currentDocument();
    REQUIRE(companion != nullptr);
    REQUIRE(companion->handle() != leader->handle());   // Stage 4's D4-13 exception: its own handle

    // ONE ENTRY FOR THE FILE, two handles on it.
    CHECK(companion->indexEntry() == entry);
    CHECK(desk.fixture.documentIndex.handleCount(entry) == 2);

    // THE LEADER SELECTS ON PAGE 1: one extraction.
    const int before = uitest::fakeTextExtractions.load();
    CHECK_FALSE(leader->selection()->ensurePage(0, core::render::RenderPriority::Visible));
    desk.pumpText();
    CHECK(uitest::fakeTextExtractions.load() == before + 1);
    CHECK(leader->selection()->text().has(0));

    // THE COMPANION SELECTS ON THE SAME PAGE: it is already there, and nothing is asked of the engine.
    CHECK(companion->selection()->ensurePage(0, core::render::RenderPriority::Visible));
    CHECK(companion->selection()->requestsMade() == 0);
    CHECK(uitest::fakeTextExtractions.load() == before + 1);

    // A SEARCH from the first window extracts the rest; the same search from the second extracts nothing
    // the first already extracted, and the hits agree.
    const SearchEngine::Outcome fromLeader = desk.search(leader->indexEntry(), QStringLiteral("line 3"));
    CHECK(uitest::fakeTextExtractions.load() == before + 8);
    const SearchEngine::Outcome fromCompanion = desk.search(companion->indexEntry(), QStringLiteral("line 3"));
    CHECK(uitest::fakeTextExtractions.load() == before + 8);
    CHECK(fromCompanion.hits == fromLeader.hits);
    CHECK(fromLeader.hits.size() == 8);
    // AND NEITHER SELECTION TOOK THE PAGES THE SEARCH INDEXED: a selection reads a page when a gesture, a
    // paint or a copy needs it, never because a search delivered it -- 900 pages laid out on the UI thread
    // for a selection nobody made would be work nobody sees.
    for (int page = 1; page < 8; ++page) {
        CHECK_FALSE(leader->selection()->text().has(page));
        CHECK_FALSE(companion->selection()->text().has(page));
    }

    // THE CONTROL: another file is another entry.
    ui::DocumentWindow* other = desk.open(QStringLiteral("other.pdf"));
    REQUIRE(other != nullptr);
    CHECK(other->indexEntry() != entry);
    CHECK(other->indexEntry() != 0);
}

TEST_CASE("the second view keeps the entry when the first window closes its document", "[documentindex][A-66]")
{
    Desk desk;
    ui::DocumentWindow* leader = desk.open(QStringLiteral("handover.pdf"));
    REQUIRE(leader != nullptr);
    const DocumentIndex::EntryId entry = leader->indexEntry();
    (void)desk.search(entry, QStringLiteral("line"));
    REQUIRE(desk.fixture.documentIndex.pagesHeld(entry) == 8);

    desk.dispatch(ActionId::WindowNew);
    ui::MainWindow* second = ui::MainWindow::shellWindows().at(1);
    driveLayout(second);
    ui::DocumentWindow* companion = second->currentDocument();
    REQUIRE(companion != nullptr);

    // The LEADER'S tab closes; the companion detaches and goes on showing the file (Stage 4 T11) -- and
    // its index entry, with every page in it, stays.
    desk.first->raiseToForeground();
    REQUIRE(desk.first->closeDocument(0));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    REQUIRE(desk.fixture.documentIndex.isAttached(entry));
    CHECK(desk.fixture.documentIndex.handleCount(entry) == 1);
    CHECK(desk.fixture.documentIndex.pagesHeld(entry) == 8);
    CHECK(companion->indexEntry() == entry);

    // And with the LAST window over the file gone, the entry goes too: memory only, nothing kept.
    second->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK_FALSE(desk.fixture.documentIndex.isAttached(entry));
}

// =============================================================================================
// A-62 -- built lazily: opening and scrolling extracts no text
// =============================================================================================

TEST_CASE("opening and scrolling a document asks for no text until something wants it", "[documentindex][A-62]")
{
    Desk desk(30);
    const int before = uitest::fakeTextExtractions.load();
    ui::DocumentWindow* document = desk.open(QStringLiteral("lazy.pdf"));
    REQUIRE(document != nullptr);
    ui::PageView* view = document->view();
    REQUIRE(view != nullptr);
    REQUIRE(view->height() > 200);

    // THROUGH THE WHOLE DOCUMENT, a screen at a time, every tile delivered.
    QScrollBar* bar = view->verticalScrollBar();
    REQUIRE(bar->maximum() > 0);
    for (int value = 0; value <= bar->maximum(); value += std::max(1, bar->pageStep())) {
        bar->setValue(value);
        desk.fixture.pumpTiles();
    }
    // THE CONTROL D5-4 NAMES: no search, no full extraction -- no extraction at all.
    CHECK(uitest::fakeTextExtractions.load() == before);
    CHECK(desk.fixture.documentIndex.pagesHeld(document->indexEntry()) == 0);
    CHECK(desk.fixture.renderScheduler.stats().textEnqueued == 0);

    // A selection on one page asks for that page and no other.
    (void)document->selection()->ensurePage(12, core::render::RenderPriority::Visible);
    desk.pumpText();
    CHECK(uitest::fakeTextExtractions.load() == before + 1);
    CHECK(desk.fixture.documentIndex.pagesHeld(document->indexEntry()) == 1);
}

// =============================================================================================
// A-64 -- a rotation run through the journal reaches the index
// =============================================================================================

TEST_CASE("a rotation drops the rotated page from the index and the next search re-reads only it",
          "[documentindex][A-64]")
{
    Desk desk;
    ui::DocumentWindow* document = desk.open(QStringLiteral("rotate.pdf"));
    REQUIRE(document != nullptr);
    REQUIRE(document->isEditable());
    const DocumentIndex::EntryId entry = document->indexEntry();
    (void)desk.search(entry, QStringLiteral("line"));
    REQUIRE(desk.fixture.documentIndex.pagesHeld(entry) == 8);
    // The selection has read page 1 too, so the case can see it forgotten.
    REQUIRE(document->selection()->ensurePage(0, core::render::RenderPriority::Visible));

    // `Ctrl+R`'s command, on the current page (page 1), through the journal -- as a person runs it.
    ui::RotatePagesDialog::setAnswerForTesting([] { return ui::RotatePagesDialog::Choice{90, false}; });
    REQUIRE(desk.fixture.actions.dispatch(ActionId::PageRotate, core::KeyContext::Viewer));
    ui::RotatePagesDialog::setAnswerForTesting({});

    CHECK(desk.fixture.documentIndex.pagesHeld(entry) == 7);
    CHECK_FALSE(desk.fixture.documentIndex.has(entry, 0));
    CHECK_FALSE(document->selection()->text().has(0));

    const int before = uitest::fakeTextExtractions.load();
    (void)desk.search(entry, QStringLiteral("line"));
    CHECK(uitest::fakeTextExtractions.load() == before + 1);
    CHECK(desk.fixture.documentIndex.pagesHeld(entry) == 8);

    // AND UNDO is a mutation too: the same page goes stale again, and only it.
    REQUIRE(desk.fixture.actions.dispatch(ActionId::EditUndo, core::KeyContext::Viewer));
    CHECK(desk.fixture.documentIndex.pagesHeld(entry) == 7);
    CHECK_FALSE(desk.fixture.documentIndex.has(entry, 0));
}
