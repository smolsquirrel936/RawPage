// tst_searchpane -- `ui/panes/SearchPane`, Advanced Search, as a WIDGET driven by key events over a scratch
// folder. Stage 5 T9, decision D5-13; guide cases A-89, A-87, and the pane's halves of A-80, A-84, A-85,
// A-86 and A-88 (`Stage5-TextSearch-Testing-Guide.md` §5.11).
//
// WHAT IT CANNOT CLAIM (convention 19): the backend is `FakeBackend` -- every file it opens says `Page N line
// K text` on every page, whatever its bytes -- so nothing here says a real PDF is found, skipped when
// encrypted or snippeted as the generator wrote it: that is `tests/render/tst_foldersearch.cpp`'s, over the
// engine and a copy of the corpus. And the folder search is pumped by `ShellFixture::pumpTiles` and Qt's
// event loop, the pane's own wake posting to it as the product's does.

#include <catch2/catch_test_macros.hpp>

#include "tests/ui/ShellFixture.h"

#include "core/cmd/ActionId.h"
#include "core/text/SearchResults.h"
#include "ui/panes/SearchPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/write/FindReplaceBar.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QProxyStyle>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QTreeWidget>

#include <map>
#include <memory>
#include <set>
#include <thread>

using namespace rawpage;
using core::ActionId;
using core::text::SearchResults;

namespace {

void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

struct Searcher {
    mutable uitest::ShellFixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    QString folder;

    Searcher()
    {
        fixture.backend.pagesPerDocument = 4;
        // THE SCRATCH TREE (plan §5.6): never the tester's own files. The fake decides what each file says.
        folder = QDir(fixture.directory.path()).filePath(QStringLiteral("tree"));
        write(QStringLiteral("alpha.pdf"));
        write(QStringLiteral("beta.pdf"));
        write(QStringLiteral("nested/gamma.pdf"));
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1200, 850);
        driveLayout(window.get());
        window->raiseToForeground();
        QCoreApplication::processEvents();
    }

    void write(const QString& relative)
    {
        const QString path = QDir(folder).filePath(relative);
        REQUIRE(QDir().mkpath(QFileInfo(path).absolutePath()));
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("the fake backend decides, not these bytes");
    }

    ui::SearchPane& pane() const { return *window->searchPane(); }
    core::KeyContext context() const { return window->shortcutBinder()->activeContext(); }

    bool key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString& text = QString()) const
    {
        QWidget* focus = window->focusWidget();
        REQUIRE(focus != nullptr);
        QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
        press.ignore();
        QCoreApplication::sendEvent(focus, &press);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
        QCoreApplication::sendEvent(focus, &release);
        QCoreApplication::processEvents();
        return press.isAccepted();
    }

    void type(const QString& text) const
    {
        for (const QChar ch : text)
            key(ch == QLatin1Char(' ') ? Qt::Key_Space : static_cast<Qt::Key>(ch.toUpper().unicode()), Qt::NoModifier,
                QString(ch));
    }

    // Until the folder search has finished and every result is in the list.
    void settle() const
    {
        QElapsedTimer timer;
        timer.start();
        do {
            fixture.pumpTiles(200);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        } while (pane().isSearching() && timer.elapsed() < 20000);
        fixture.pumpTiles(200);
    }

    // `Ctrl+Shift+F` from wherever the focus is, the query typed, `Enter` -- a key event each.
    void search(const QString& query, bool recursive = true) const
    {
        REQUIRE(key(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier));
        REQUIRE(pane().isVisible());
        pane().setFolder(QDir::toNativeSeparators(folder));
        pane().setRecursive(recursive);
        pane().queryField()->selectAll();
        type(query);
        REQUIRE(window->focusWidget() == pane().queryField());
        key(Qt::Key_Return);
        settle();
    }

    std::set<QString> filesListed() const
    {
        std::set<QString> files;
        for (const auto& row : pane().listedRows()) files.insert(QDir(folder).relativeFilePath(row.file->path));
        return files;
    }
};

} // namespace

// =============================================================================================
// A-89 -- Ctrl+Shift+F opens Advanced Search from the page and from the find bar
// =============================================================================================

TEST_CASE("Ctrl+Shift+F opens Advanced Search from the page and from the find bar", "[searchpane][A-89]")
{
    Searcher s;
    s.window->openFiles({QDir(s.folder).filePath(QStringLiteral("alpha.pdf"))});
    driveLayout(s.window.get());
    ui::PageView* view = s.window->currentView();
    REQUIRE(view != nullptr);
    view->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    REQUIRE_FALSE(s.pane().isVisible());

    // FROM THE VIEWER, as a key event: the pane shows and the focus is in its query, in FindSearch.
    REQUIRE(s.key(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier));
    CHECK(s.pane().isVisible());
    CHECK(s.window->focusWidget() == s.pane().queryField());
    CHECK(s.context() == core::KeyContext::FindSearch);

    // `Esc` in the pane is §8's Find / Search row: it hides the pane and gives the page the focus.
    REQUIRE(s.key(Qt::Key_Escape));
    CHECK_FALSE(s.pane().isVisible());
    CHECK(s.window->focusWidget() == view);

    // FROM THE FIND BAR -- FindSearch's own context -- the same, through the Global layer (D5-15).
    REQUIRE(s.key(Qt::Key_F, Qt::ControlModifier));
    ui::FindReplaceBar* bar = s.window->currentDocument()->findBar();
    REQUIRE(bar->isOpen());
    REQUIRE(s.window->focusWidget() == bar->field());
    REQUIRE(s.key(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier));
    CHECK(s.pane().isVisible());
    CHECK(s.window->focusWidget() == s.pane().queryField());
}

// =============================================================================================
// A-80 (the pane's half) and A-88 (the pane's half)
// =============================================================================================

TEST_CASE("the pane searches the folder alone or its subfolders too and the UI thread opens nothing",
          "[searchpane][A-80][A-88]")
{
    Searcher s;
    s.search(QStringLiteral("line 2"), false);
    CHECK(s.filesListed() == std::set<QString>{"alpha.pdf", "beta.pdf"});
    // Every page of every file says `line 2` once: four pages, two files.
    CHECK(s.pane().listedRows().size() == 8);
    CHECK(s.pane().statusText().contains(ui::catalog::translate(ui::catalog::key::SearchPaneCount).arg(8).arg(2)));

    s.search(QStringLiteral("line 2"), true);
    CHECK(s.filesListed() == std::set<QString>{"alpha.pdf", "beta.pdf", "nested/gamma.pdf"});
    // NOT ONE FILE WAS SHOWN IN A TAB (D5-13).
    CHECK(s.window->documentCount() == 0);

    // EVERY OPEN ON THE RENDER WORKER, never on this -- the UI -- thread (A-88).
    const std::lock_guard<std::mutex> lock(s.fixture.backend.openMutex);
    REQUIRE(s.fixture.backend.openThreads.size() == 5);
    for (const std::thread::id thread : s.fixture.backend.openThreads) CHECK(thread != std::this_thread::get_id());
}

TEST_CASE("an invalid pattern or a missing folder is said and searches nothing", "[searchpane][A-80]")
{
    Searcher s;
    s.fixture.settings.set(core::keys::search::useRegex, true);
    s.search(QStringLiteral("line ("), true);
    CHECK(s.pane().listedRows().empty());
    CHECK(s.pane().statusText().contains(ui::catalog::translateError(QStringLiteral("error.search.invalidPattern"),
                                                                       QStringLiteral("line ("))));
    s.fixture.settings.set(core::keys::search::useRegex, false);
    s.pane().setFolder(QDir(s.folder).filePath(QStringLiteral("nowhere")));
    CHECK_FALSE(s.pane().search());
    CHECK(s.pane().listedRows().empty());
    const std::lock_guard<std::mutex> lock(s.fixture.backend.openMutex);
    CHECK(s.fixture.backend.openThreads.empty());   // nothing was opened for either
}

// =============================================================================================
// A-87 -- activating a hit opens its file at the hit, once
// =============================================================================================

TEST_CASE("activating a result opens its file at the hit and an open file is not opened twice", "[searchpane][A-87]")
{
    Searcher s;
    s.search(QStringLiteral("line 3"), false);
    REQUIRE(s.pane().listedRows().size() == 8);
    s.pane().setSort(SearchResults::Sort::File);

    // `↓` twice in the list and `Enter`: the second result of alpha.pdf -- page 2's.
    s.pane().list()->setFocus(Qt::OtherFocusReason);
    s.pane().list()->setCurrentItem(s.pane().list()->topLevelItem(0));
    s.key(Qt::Key_Down);
    REQUIRE(s.pane().currentRow() == 1);
    REQUIRE(s.key(Qt::Key_Return));
    s.fixture.pumpTiles();

    REQUIRE(s.window->documentCount() == 1);
    ui::DocumentWindow* document = s.window->currentDocument();
    REQUIRE(document != nullptr);
    CHECK(QFileInfo(document->path()).fileName() == QStringLiteral("alpha.pdf"));
    // HIGHLIGHTED: the tab's find bar holds the file's hits under the query, the chosen one current and shown.
    ui::FindReplaceBar* bar = document->findBar();
    CHECK(bar->isOpen());
    CHECK(bar->field()->text() == QStringLiteral("line 3"));
    REQUIRE(bar->hits().size() == 4);
    CHECK(bar->currentIndex() == 1);
    CHECK(bar->hits()[1].page == 1);
    CHECK(document->selection()->searchHits().size() == 4);
    CHECK(document->selection()->currentSearchHit() == 1);

    // AGAIN, another of the same file's results: the same tab, not a second.
    s.pane().list()->setFocus(Qt::OtherFocusReason);
    s.pane().list()->setCurrentItem(s.pane().list()->topLevelItem(3));
    REQUIRE(s.key(Qt::Key_Return));
    CHECK(s.window->documentCount() == 1);
    CHECK(s.window->currentDocument()->findBar()->currentIndex() == 3);

    // `F3` and `Shift+F3` with the focus in the pane step through ITS list -- not the bar's.
    s.pane().list()->setFocus(Qt::OtherFocusReason);
    s.pane().list()->setCurrentItem(s.pane().list()->topLevelItem(0));
    REQUIRE(s.key(Qt::Key_F3));
    CHECK(s.pane().currentRow() == 1);
    REQUIRE(s.key(Qt::Key_F3, Qt::ShiftModifier));
    CHECK(s.pane().currentRow() == 0);
    CHECK(s.window->currentDocument()->findBar()->currentIndex() == 3);
}

// =============================================================================================
// A-84, A-85, A-86 -- the filter, refine and the export, through the pane
// =============================================================================================

TEST_CASE("the pane filters by metadata refines and exports what it lists", "[searchpane][A-84][A-85][A-86]")
{
    Searcher s;
    core::Metadata levi;
    levi.author = QStringLiteral("Noa Levi");
    s.fixture.backend.metadataByName[QStringLiteral("beta.pdf")] = levi;
    // `Page 2` is every line of page 2 -- five a file -- and nothing on the other three pages.
    s.search(QStringLiteral("Page 2"), true);
    REQUIRE(s.pane().listedRows().size() == 15);

    // A-84: by author, beta.pdf alone; the other two have no author and are out; no filter, all back.
    SearchResults::Filter author;
    author.author = QStringLiteral("levi");
    s.pane().setFilter(author);
    CHECK(s.filesListed() == std::set<QString>{"beta.pdf"});
    s.pane().setFilter({});
    CHECK(s.filesListed().size() == 3);

    // A-85: refined by `line 1` -- which is on EVERY page, four a file -- only page 2's carries both: one a file.
    REQUIRE(s.pane().refine(QStringLiteral("line 1")));
    s.settle();
    REQUIRE(s.pane().listedRows().size() == 3);
    for (const auto& row : s.pane().listedRows()) {
        CHECK(row.hit->hit.page == 1);
        CHECK(row.hit->snippet.match == QStringLiteral("line 1"));
    }

    // A-86: the list as CSV through the one waiver -- the byte-order mark, the heading, one row per hit.
    const QString csv = QDir(s.fixture.directory.path()).filePath(QStringLiteral("results.csv"));
    REQUIRE(s.pane().exportTo(csv));
    QFile file(csv);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    CHECK(bytes.startsWith("\xEF\xBB\xBF"));
    CHECK(bytes.count("\r\n") == 4);
    CHECK(QString::fromUtf8(bytes.mid(3)).startsWith(ui::catalog::translate(ui::catalog::key::SearchPaneColumnFile)));
    CHECK(s.pane().statusText().contains(QDir::toNativeSeparators(csv)));
}

// =============================================================================================
// S5 guide I-18 -- a path too wide for its column keeps its head and its tail
// =============================================================================================

namespace {

// THE STYLE IS ASKED, AND THIS LISTENS: every item the list paints reaches `CE_ItemViewItem` with the elide
// mode the style will cut its text by -- `QCommonStyle`'s `viewItemDrawText` reads `textElideMode` and
// nothing else. Recorded per column.
class ElideRecorder final : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    std::map<int, std::set<int>> modesByColumn;

    void drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                     const QWidget* widget) const override
    {
        if (element == CE_ItemViewItem)
            if (const auto* item = qstyleoption_cast<const QStyleOptionViewItem*>(option))
                const_cast<ElideRecorder*>(this)->modesByColumn[item->index.column()].insert(item->textElideMode);
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};

} // namespace

TEST_CASE("the file column cuts a long path in the middle and the other columns at the end", "[searchpane][I-18]")
{
    Searcher s;
    s.search(QStringLiteral("line 2"), false);
    REQUIRE(s.pane().listedRows().size() == 8);
    QTreeWidget* list = s.pane().list();
    const QString path = list->topLevelItem(0)->text(0);
    // Two thirds of the path's width -- cut, with room at each end for more than the file's name. From the
    // metrics, not a pixel count: offscreen's are about twice the `windows` platform's (S5 T14).
    const int width = list->fontMetrics().horizontalAdvance(path) * 2 / 3;
    REQUIRE(list->fontMetrics().horizontalAdvance(QStringLiteral("…alpha.pdf")) * 3 < width);
    list->setColumnWidth(0, width);

    ElideRecorder recorder;
    list->setStyle(&recorder);
    (void)list->viewport()->grab();
    list->setStyle(nullptr);

    // The file column: the middle goes, so the drive and the NAME stay.
    CHECK(recorder.modesByColumn[0] == std::set<int>{Qt::ElideMiddle});
    // THE CONTROL: the page column is the list's own, cut at the end -- the delegate is per column, and a
    // skipped file's reason, a sentence, keeps its start.
    CHECK(recorder.modesByColumn[1] == std::set<int>{Qt::ElideRight});
    // And what that leaves on screen, by the metrics the style cuts with: the file's name, whole.
    const QString shown = list->fontMetrics().elidedText(path, Qt::ElideMiddle, width);
    CHECK(shown != path);
    CHECK(shown.endsWith(QStringLiteral("alpha.pdf")));
    CHECK(shown.startsWith(path.left(2)));
    // The cell keeps the WHOLE path -- the cut is only what is painted -- and its tooltip says it.
    CHECK(QDir::fromNativeSeparators(path).endsWith(QStringLiteral("tree/alpha.pdf")));
    CHECK(list->topLevelItem(0)->toolTip(0) == path);
}

// =============================================================================================
// S5 guide I-19 -- the files the results are in, and the files searched, said apart
// =============================================================================================

TEST_CASE("the count names the files the listed results are in and the files searched apart", "[searchpane][I-19]")
{
    Searcher s;
    core::Metadata levi;
    levi.author = QStringLiteral("Noa Levi");
    s.fixture.backend.metadataByName[QStringLiteral("beta.pdf")] = levi;
    s.search(QStringLiteral("Page 2"), true);
    REQUIRE(s.pane().listedRows().size() == 15);
    // Three files searched, three with results: the two figures agree, as they did in every case before I-19.
    CHECK(s.pane().statusText().contains(ui::catalog::translate(ui::catalog::key::SearchPaneCount).arg(15).arg(3)));
    CHECK(s.pane().statusText().contains(ui::catalog::translate(ui::catalog::key::SearchPaneSearched).arg(3)));

    // THE CASE THAT TELLS THEM APART: filtered to beta.pdf, five results in ONE file of the three searched.
    // Before the fix this read "5 results in 3 files".
    SearchResults::Filter author;
    author.author = QStringLiteral("levi");
    s.pane().setFilter(author);
    REQUIRE(s.pane().listedRows().size() == 5);
    CHECK(s.pane().statusText().contains(ui::catalog::translate(ui::catalog::key::SearchPaneCount).arg(5).arg(1)));
    CHECK(s.pane().statusText().contains(ui::catalog::translate(ui::catalog::key::SearchPaneSearched).arg(3)));
    CHECK_FALSE(s.pane().statusText().contains(ui::catalog::translate(ui::catalog::key::SearchPaneCount).arg(5).arg(3)));
}

// =============================================================================================
// S5 guide I-20 -- the pane shows the options it searches with, and a changed one re-runs the search
// =============================================================================================

TEST_CASE("the pane shows the search options and a changed one searches again", "[searchpane][I-20]")
{
    Searcher s;
    s.search(QStringLiteral("line 2|line 3"), false);
    // Regex off: the alternation is literal text, on no page.
    REQUIRE(s.pane().listedRows().empty());
    QToolButton* regex = s.pane().regexToggle();
    QToolButton* whole = s.pane().wholeWordsToggle();
    QToolButton* cased = s.pane().caseSensitiveToggle();
    REQUIRE(regex != nullptr);
    REQUIRE(whole != nullptr);
    REQUIRE(cased != nullptr);
    CHECK(regex->isVisible());
    CHECK(whole->isVisible());
    CHECK(cased->isVisible());
    // LABELLED AS THE SETTINGS ARE, as the find bar's are.
    CHECK(regex->text() == ui::catalog::translate(QStringLiteral("prefs.setting.search.useRegex")));
    CHECK(whole->text() == ui::catalog::translate(QStringLiteral("prefs.setting.search.wholeWords")));
    CHECK(cased->text() == ui::catalog::translate(QStringLiteral("prefs.setting.search.caseSensitive")));
    CHECK_FALSE(regex->isChecked());

    // A CLICK DISPATCHES THE COMMAND, which writes the setting -- and the search runs again under it, with no
    // Search pressed: two lines a page, four pages, two files.
    regex->click();
    CHECK(s.fixture.settings.get(core::keys::search::useRegex));
    CHECK(regex->isChecked());
    s.settle();
    CHECK(s.pane().listedRows().size() == 16);

    // `Alt+R` from the pane's query is the same command: off again, the button with it, and nothing found.
    s.pane().queryField()->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    REQUIRE(s.key(Qt::Key_R, Qt::AltModifier));
    CHECK_FALSE(s.fixture.settings.get(core::keys::search::useRegex));
    CHECK_FALSE(regex->isChecked());
    s.settle();
    CHECK(s.pane().listedRows().empty());

    // FOLLOWING THE SETTINGS, whoever changes them -- here the Search page's path. Each change searched again.
    s.fixture.settings.set(core::keys::search::wholeWords, true);
    CHECK(whole->isChecked());
    s.fixture.settings.set(core::keys::search::wholeWords, false);
    CHECK_FALSE(whole->isChecked());
    s.settle();

    // A HIDDEN PANE SEARCHES NOTHING when an option changes, and searches when it is shown again.
    std::size_t opens = 0;
    {
        const std::lock_guard<std::mutex> lock(s.fixture.backend.openMutex);
        opens = s.fixture.backend.openThreads.size();
    }
    s.pane().hide();
    s.fixture.settings.set(core::keys::search::useRegex, true);
    s.settle();
    {
        const std::lock_guard<std::mutex> lock(s.fixture.backend.openMutex);
        CHECK(s.fixture.backend.openThreads.size() == opens);
    }
    CHECK(regex->isChecked());
    s.pane().show();
    s.settle();
    CHECK(s.pane().listedRows().size() == 16);
    s.fixture.settings.set(core::keys::search::useRegex, false);
    s.settle();
}
