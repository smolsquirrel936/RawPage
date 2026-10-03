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
#include "ui/panes/LogPane.h"
#include "ui/panes/SearchPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/SidebarDock.h"
#include "ui/shell/ThemeManager.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/write/FindReplaceBar.h"

#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QPainter>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QTreeWidget>

#include <algorithm>
#include <cmath>
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

// =============================================================================================
// S5 guide I-21 and the user's design (2026-10-03) -- `Enter` narrows a list that exists, searches otherwise
// =============================================================================================

namespace {

std::size_t opensSoFar(const Searcher& s)
{
    const std::lock_guard<std::mutex> lock(s.fixture.backend.openMutex);
    return s.fixture.backend.openThreads.size();
}

// `Enter` with the focus on `widget`, as a key event -- the keyboard's path, through `ShortcutBinder`.
void enterIn(const Searcher& s, QWidget* widget)
{
    widget->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    REQUIRE(s.window->focusWidget() == widget);
    s.key(Qt::Key_Return);
    s.settle();
}

} // namespace

TEST_CASE("Enter in a filter field narrows the list and opens nothing", "[searchpane][I-21]")
{
    Searcher s;
    core::Metadata levi;
    levi.author = QStringLiteral("Noa Levi");
    s.fixture.backend.metadataByName[QStringLiteral("beta.pdf")] = levi;
    s.search(QStringLiteral("Page 2"), true);
    REQUIRE(s.pane().listedRows().size() == 15);
    REQUIRE(s.pane().hasList());
    const std::size_t opens = opensSoFar(s);

    // THE AUTHOR, AND `ENTER`: the list narrowed to beta.pdf -- with no search, and no file opened. Until the
    // fix this `Enter` opened the FIRST RESULT in a tab.
    QLineEdit* author = nullptr;
    for (QLineEdit* field : s.pane().findChildren<QLineEdit*>())
        if (field->placeholderText() == ui::catalog::translate(ui::catalog::key::SearchPaneAuthor)) author = field;
    REQUIRE(author != nullptr);
    author->setText(QStringLiteral("levi"));
    enterIn(s, author);
    CHECK(s.window->documentCount() == 0);
    CHECK(s.filesListed() == std::set<QString>{"beta.pdf"});
    CHECK(s.pane().listedRows().size() == 5);
    CHECK(opensSoFar(s) == opens);
    CHECK(s.pane().queryChain() == QStringList{QStringLiteral("Page 2")});

    // `ENTER` ON A BUTTON opens nothing either, and changes nothing.
    enterIn(s, s.pane().refineButton());
    CHECK(s.window->documentCount() == 0);
    CHECK(s.pane().listedRows().size() == 5);

    // A NEW QUERY AND `ENTER` REFINES: beta.pdf's page 2 carries `line 1`, the filter still on -- one result,
    // and the status line says what the list is.
    s.pane().queryField()->setText(QStringLiteral("line 1"));
    enterIn(s, s.pane().queryField());
    CHECK(s.window->documentCount() == 0);
    REQUIRE(s.pane().listedRows().size() == 1);
    CHECK(s.pane().listedRows().front().hit->hit.page == 1);
    CHECK(s.pane().queryChain() == (QStringList{QStringLiteral("Page 2"), QStringLiteral("line 1")}));
    CHECK(s.pane().statusText().contains(
        ui::catalog::translate(ui::catalog::key::SearchPaneRefinedBy).arg(QStringLiteral("Page 2 → line 1"))));

    // THE SAME QUERY AGAIN searches nothing: the list is already its answer.
    const std::size_t afterRefine = opensSoFar(s);
    enterIn(s, s.pane().queryField());
    CHECK(opensSoFar(s) == afterRefine);
    CHECK(s.pane().listedRows().size() == 1);

    // A CHANGED SCOPE IS A NEW SEARCH: subfolders off, `Enter` -- the chain starts again, over two files.
    s.pane().setRecursive(false);
    author->clear();
    enterIn(s, s.pane().queryField());
    CHECK(s.pane().queryChain() == QStringList{QStringLiteral("line 1")});
    CHECK(s.filesListed() == std::set<QString>{"alpha.pdf", "beta.pdf"});
    CHECK(s.pane().listedRows().size() == 8);
}

TEST_CASE("Clear empties the list and keeps the fields so the next Enter searches anew", "[searchpane][I-21]")
{
    Searcher s;
    s.search(QStringLiteral("Page 2"), true);
    REQUIRE(s.pane().listedRows().size() == 15);
    REQUIRE(s.pane().refine(QStringLiteral("line 1")));
    s.settle();
    REQUIRE(s.pane().queryChain().size() == 2);
    REQUIRE(s.pane().clearButton()->isEnabled());
    const QString query = s.pane().queryField()->text();

    s.pane().clearButton()->click();
    CHECK(s.pane().listedRows().empty());
    CHECK(s.pane().list()->topLevelItemCount() == 0);
    CHECK(s.pane().queryChain().isEmpty());
    CHECK_FALSE(s.pane().hasList());
    CHECK_FALSE(s.pane().statusText().contains(ui::catalog::translate(ui::catalog::key::SearchPaneSearched).arg(3)));
    CHECK_FALSE(s.pane().refineButton()->isEnabled());
    CHECK_FALSE(s.pane().clearButton()->isEnabled());
    // EVERY FIELD AS TYPED.
    CHECK(s.pane().queryField()->text() == query);

    // AND THE NEXT `ENTER` IS A NEW SEARCH of what the field says -- no refinement of a list that is gone.
    s.pane().queryField()->setText(QStringLiteral("line 3"));
    enterIn(s, s.pane().queryField());
    CHECK(s.pane().queryChain() == QStringList{QStringLiteral("line 3")});
    CHECK(s.pane().listedRows().size() == 12);
}

// =============================================================================================
// S5 guide I-22 -- an opened result carries the query whose hits the list holds
// =============================================================================================

TEST_CASE("a refined result opens under the query the list was refined by", "[searchpane][I-22]")
{
    Searcher s;
    s.search(QStringLiteral("Page 2"), false);
    REQUIRE(s.pane().listedRows().size() == 10);
    s.pane().queryField()->setText(QStringLiteral("line 1"));
    enterIn(s, s.pane().queryField());
    REQUIRE(s.pane().listedRows().size() == 2);
    CHECK(s.pane().pattern() == QStringLiteral("line 1"));

    // From the list, `Enter`: the file opens with ITS hits under ITS query -- `line 1` on page 2, not `Page 2`.
    s.pane().setSort(SearchResults::Sort::File);
    s.pane().list()->setCurrentItem(s.pane().list()->topLevelItem(0));
    enterIn(s, s.pane().list());
    REQUIRE(s.window->documentCount() == 1);
    ui::FindReplaceBar* bar = s.window->currentDocument()->findBar();
    CHECK(bar->field()->text() == QStringLiteral("line 1"));
    REQUIRE(bar->hits().size() == 1);
    CHECK(bar->hits()[0].page == 1);
}

// =============================================================================================
// The user's request, 2026-10-03 -- the edge that faces the page is drawn
// =============================================================================================

namespace {

// `tst_theme`'s luminance ratio (WCAG's), for the line against the window it is drawn beside.
double luminance(const QColor& color)
{
    const auto channel = [](double value) {
        return value <= 0.03928 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}

double contrast(const QColor& a, const QColor& b)
{
    const double first = luminance(a);
    const double second = luminance(b);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

// The pane as it paints itself, and the colour at a DEVICE pixel of it.
QColor pixelOf(QWidget& pane, double x, double y)
{
    const QImage image = pane.grab().toImage();
    const double ratio = image.devicePixelRatio();
    return image.pixelColor(std::clamp(static_cast<int>(x * ratio), 0, image.width() - 1),
                            std::clamp(static_cast<int>(y * ratio), 0, image.height() - 1));
}

} // namespace

TEST_CASE("the pane draws its edge on the side that faces the page wherever it is docked", "[searchpane][edge-line]")
{
    Searcher s;
    s.pane().showAndFocus();
    driveLayout(s.window.get());
    ui::SearchPane& pane = s.pane();
    const QColor line = pane.palette().color(QPalette::Mid);
    const double middleY = pane.height() / 2.0;

    // DOCKED RIGHT, as the product docks it: the LEFT edge -- and one pixel in, the window's own colour, so
    // it is a line and not the pane's background.
    REQUIRE(s.window->dockWidgetArea(&pane) == Qt::RightDockWidgetArea);
    CHECK(pixelOf(pane, 0, middleY) == line);
    CHECK(pixelOf(pane, 2, middleY) != line);
    CHECK(pixelOf(pane, pane.width() - 1, middleY) != line);

    // AN EDGE BETWEEN DEVICE PIXELS. On the user's 150 % monitors the pane docked at logical x 793 starts at
    // device 1189.5, and the first version's hairline on the edge rasterized to device 1189 -- the window's,
    // clipped away -- so it showed in every grab above and on no screen (2026-10-03). Here the pane is
    // rendered at 1.5 one logical pixel in, so its edge is at device 1.5 and its first own pixel is device 2.
    {
        const double scale = 1.5;
        QImage target(QSize(static_cast<int>((pane.width() + 2) * scale), static_cast<int>((pane.height() + 2) * scale)),
                      QImage::Format_ARGB32_Premultiplied);
        target.setDevicePixelRatio(scale);
        const QColor window = pane.palette().color(QPalette::Window);
        target.fill(window);
        {
            QPainter painter(&target);
            pane.render(&painter, QPoint(1, 0));
        }
        const int y = static_cast<int>(middleY * scale);
        INFO("device columns 1..3: " << target.pixelColor(1, y).name().toStdString() << " "
                                     << target.pixelColor(2, y).name().toStdString() << " "
                                     << target.pixelColor(3, y).name().toStdString());
        CHECK(target.pixelColor(2, y) == line);
    }

    // DOCKED LEFT: the RIGHT edge, and the left edge is not drawn.
    s.window->addDockWidget(Qt::LeftDockWidgetArea, &pane);
    driveLayout(s.window.get());
    REQUIRE(s.window->dockWidgetArea(&pane) == Qt::LeftDockWidgetArea);
    CHECK(pixelOf(pane, pane.width() - 1, pane.height() / 2.0) == line);
    CHECK(pixelOf(pane, 0, pane.height() / 2.0) != line);

    // DOCKED AT THE BOTTOM: the TOP edge.
    s.window->addDockWidget(Qt::BottomDockWidgetArea, &pane);
    driveLayout(s.window.get());
    REQUIRE(s.window->dockWidgetArea(&pane) == Qt::BottomDockWidgetArea);
    CHECK(pixelOf(pane, pane.width() / 2.0, 0) == line);

    // JUDGED IN BOTH THEMES (convention 20): `QPalette::Mid` is the `Border` token, and against the window it
    // is drawn beside it clears 1.3:1 in each -- a thin line, as visible as the borders of the pane's own
    // fields, and no louder. 1.39 light, 1.52 dark when written.
    using ui::ThemeManager;
    using ui::ThemeToken;
    for (const ThemeManager::Resolved theme : {ThemeManager::Resolved::Light, ThemeManager::Resolved::Dark}) {
        const double ratio = contrast(ThemeManager::colorFor(ThemeToken::Border, theme),
                                      ThemeManager::colorFor(ThemeToken::WindowBackground, theme));
        INFO("theme " << (theme == ThemeManager::Resolved::Dark ? "dark" : "light") << ": " << ratio);
        CHECK(ratio >= 1.3);
    }
}

// =============================================================================================
// The user's request, 2026-10-04 -- the sidebar and the log pane draw the edge that faces the page too
// =============================================================================================

namespace {

// THE STRIP SURVIVES AN EDGE BETWEEN DEVICE PIXELS: `dock` rendered at 1.5, offset so the edge it is docked
// against falls on a half device pixel -- the user's 150 % screens -- and some device pixel across the strip,
// at mid length, is the line. A hairline ON the edge fails this (A-123's first version).
bool lineSurvivesHalfPixel(QWidget& dock, Qt::DockWidgetArea side, const QColor& line)
{
    const double scale = 1.5;
    // An odd logical coordinate for the edge, so the edge lands at a .5 device coordinate.
    const int offsetX = (side == Qt::LeftDockWidgetArea && dock.width() % 2 == 1) ? 2 : 1;
    const int offsetY = (side == Qt::TopDockWidgetArea && dock.height() % 2 == 1) ? 2 : 1;
    QImage target(QSize(static_cast<int>((dock.width() + 3) * scale), static_cast<int>((dock.height() + 3) * scale)),
                  QImage::Format_ARGB32_Premultiplied);
    target.setDevicePixelRatio(scale);
    target.fill(dock.palette().color(QPalette::Window));
    {
        QPainter painter(&target);
        dock.render(&painter, QPoint(offsetX, offsetY));
    }
    // The strip's logical extent across, in the target: [from, from + 1).
    double from = 0;
    switch (side) {
    case Qt::RightDockWidgetArea: from = offsetX; break;
    case Qt::LeftDockWidgetArea: from = offsetX + dock.width() - 1; break;
    case Qt::BottomDockWidgetArea: from = offsetY; break;
    case Qt::TopDockWidgetArea: from = offsetY + dock.height() - 1; break;
    default: return false;
    }
    const bool across = side == Qt::RightDockWidgetArea || side == Qt::LeftDockWidgetArea;
    const int along = static_cast<int>(((across ? offsetY + dock.height() / 2.0 : offsetX + dock.width() / 2.0)) * scale);
    for (int d = static_cast<int>(std::floor(from * scale)); d <= static_cast<int>(std::ceil((from + 1) * scale)); ++d) {
        const QColor c = across ? target.pixelColor(d, along) : target.pixelColor(along, d);
        if (c == line) return true;
    }
    return false;
}

} // namespace

TEST_CASE("the sidebar and the log pane draw the edge that faces the page", "[searchpane][edge-line]")
{
    Searcher s;
    s.window->logPane()->show();
    s.window->sidebar()->show();
    driveLayout(s.window.get());

    struct Dock {
        const char* name;
        QDockWidget* dock;
        Qt::DockWidgetArea expected;
    };
    for (const Dock& d : {Dock{"sidebar", s.window->sidebar(), Qt::LeftDockWidgetArea},
                          Dock{"log pane", s.window->logPane(), Qt::BottomDockWidgetArea}}) {
        INFO(d.name);
        REQUIRE(d.dock->isVisible());
        REQUIRE(s.window->dockWidgetArea(d.dock) == d.expected);
        const QColor line = d.dock->palette().color(QPalette::Mid);
        const QImage image = d.dock->grab().toImage();
        const double r = image.devicePixelRatio();
        const auto at = [&](double x, double y) {
            return image.pixelColor(std::clamp(static_cast<int>(x * r), 0, image.width() - 1),
                                    std::clamp(static_cast<int>(y * r), 0, image.height() - 1));
        };
        const double w = d.dock->width();
        const double h = d.dock->height();
        if (d.expected == Qt::LeftDockWidgetArea) {
            // Docked left: the RIGHT edge faces the page -- over the contents, which reach the edge.
            CHECK(at(w - 1, h / 2) == line);
            CHECK(at(w - 3, h / 2) != line);
            CHECK(at(0, h / 2) != line);
        } else {
            // Docked at the bottom: the TOP edge.
            CHECK(at(w / 2, 0) == line);
            CHECK(at(w / 2, 3) != line);
            CHECK(at(w / 2, h - 1) != line);
        }
        CHECK(lineSurvivesHalfPixel(*d.dock, d.expected, line));
    }

    // FLOATING, there is no edge facing the page -- the window frame is the edge.
    ui::LogPane* log = s.window->logPane();
    log->setFloating(true);
    QCoreApplication::processEvents();
    auto* edge = log->findChild<QWidget*>(QStringLiteral("dockEdge"));
    REQUIRE(edge != nullptr);
    CHECK_FALSE(edge->isVisible());
    log->setFloating(false);
    QCoreApplication::processEvents();
    CHECK(edge->isVisible());
}
