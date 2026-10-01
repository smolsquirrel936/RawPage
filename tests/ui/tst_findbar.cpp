// tst_findbar -- `ui/write/FindReplaceBar`, the find half, as a WIDGET driven by key events. Stage 5 T7,
// decision D5-14; guide cases A-69 … A-75 (`Stage5-TextSearch-Testing-Guide.md` §5.9) and A-57's bar half.
//
// Every chord here is a `QKeyEvent` sent to the focused widget -- the path a keyboard takes, through
// `ShortcutBinder` -- and every assertion is on the EFFECT (rule 1): the current hit moved and the view
// shows it, the setting changed and the button with it, the bar closed and the highlights went.
//
// WHAT IT CANNOT CLAIM (convention 19): the text is `ShellFixture`'s fake -- five lines a page, `Page N
// line K text` -- and the fake rasterizer draws no glyph, so nothing here says a highlight lands on INK;
// that is `tests/composition/tst_selectionink.cpp`'s A-74 case, over the real engine. And the search is
// pumped by `ShellFixture::pumpTiles`, not by Qt's queued wake the product installs.

#include <catch2/catch_test_macros.hpp>

#include "tests/ui/ShellFixture.h"

#include "core/cmd/ActionId.h"
#include "core/text/SearchEngine.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/viewer/SplitViewContainer.h"
#include "ui/viewer/ViewportController.h"
#include "ui/write/FindReplaceBar.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QToolButton>

#include <memory>

using namespace rawpage;
using core::ActionId;

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

struct Finder {
    mutable Fixture                 fixture;   // pumped from the const helpers below
    std::unique_ptr<ui::MainWindow> window;
    ui::PageView*                   view = nullptr;

    explicit Finder(int pages = 12)
    {
        fixture.backend.pagesPerDocument = pages;
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 850);
        window->openFiles({fixture.writeFile(QStringLiteral("find.pdf"))});
        driveLayout(window.get());
        view = window->currentView();
        REQUIRE(view != nullptr);
        window->raiseToForeground();
        view->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        fixture.pumpTiles();
        // Rule 4: the geometry first.
        REQUIRE(view->width() > 400);
        REQUIRE(view->height() > 300);
    }

    ui::DocumentWindow* document() const { return window->currentDocument(); }
    ui::FindReplaceBar& bar() const { return *document()->findBar(); }
    ui::SelectionOverlay& overlay() const { return *document()->selection(); }
    core::KeyContext context() const { return window->shortcutBinder()->activeContext(); }

    // A key event to the focused widget -- the keyboard's path. True when something consumed it.
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

    // Typed one character at a time, as a person types it -- each a key event the field turns into an edit.
    void type(const QString& text) const
    {
        for (const QChar ch : text) {
            const Qt::Key code = ch == QLatin1Char(' ') ? Qt::Key_Space : static_cast<Qt::Key>(ch.toUpper().unicode());
            key(code, Qt::NoModifier, QString(ch));
        }
    }

    // Past the debounce, then every page and every match delivered.
    void settle() const
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ui::FindReplaceBar::kDebounceMs + 80) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        for (int round = 0; round < 3; ++round) fixture.pumpTiles();
    }

    // Opens the bar from the page with `Ctrl+F`, types a query and waits for its hits.
    void find(const QString& query) const
    {
        view->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        REQUIRE(key(Qt::Key_F, Qt::ControlModifier));
        REQUIRE(bar().isOpen());
        bar().field()->selectAll();
        type(query);
        settle();
    }

    // Whether the view shows the current hit -- the view's own visible rectangle against the hit's.
    bool showsCurrentHit() const
    {
        const int current = bar().currentIndex();
        if (current < 0) return false;
        const core::text::SearchHit hit = bar().hits()[static_cast<std::size_t>(current)];
        const std::optional<core::PdfRect> rect = overlay().searchHitRect(hit);
        if (!rect) return false;
        const auto corner = view->layoutEngine().layout().toContent(hit.page, core::PdfPoint{rect->left(), rect->top()});
        if (!corner) return false;
        const core::DeviceRect visible = view->controller().visibleRect();
        return corner->x >= visible.left() && corner->x <= visible.right() && corner->y >= visible.top()
               && corner->y <= visible.bottom();
    }

    bool fieldHasFocus() const { return window->focusWidget() == bar().field(); }
};

} // namespace

// =============================================================================================
// A-69 -- every Find / Search chord, from a key event
// =============================================================================================

TEST_CASE("every find chord works from a key event and is asserted on its effect", "[findbar][A-69]")
{
    Finder finder;
    // `Ctrl+F` FROM THE PAGE -- §8's Common row -- opens the bar and puts the focus in it.
    REQUIRE(finder.context() == core::KeyContext::Viewer);
    CHECK(finder.key(Qt::Key_F, Qt::ControlModifier));
    REQUIRE(finder.bar().isOpen());
    CHECK(finder.fieldHasFocus());
    // THE CONTEXT IS FINDSEARCH with the focus in it -- which is what the remaining chords resolve in.
    CHECK(finder.context() == core::KeyContext::FindSearch);

    finder.type(QStringLiteral("line 3"));
    finder.settle();
    REQUIRE(finder.bar().hits().size() == 12);   // one `line 3` a page
    CHECK(finder.bar().currentIndex() == 0);
    CHECK(finder.bar().statusText() == ui::catalog::translate(ui::catalog::key::SearchBarCount).arg(1).arg(12));
    CHECK(finder.showsCurrentHit());

    // `F3` and `Shift+F3`, `Ctrl+G` and `Ctrl+Shift+G`: the current hit moves, and the view goes with it.
    CHECK(finder.key(Qt::Key_F3));
    CHECK(finder.bar().currentIndex() == 1);
    CHECK(finder.showsCurrentHit());
    CHECK(finder.key(Qt::Key_F3));
    CHECK(finder.key(Qt::Key_F3));
    CHECK(finder.bar().currentIndex() == 3);
    CHECK(finder.showsCurrentHit());
    CHECK(finder.key(Qt::Key_F3, Qt::ShiftModifier));
    CHECK(finder.bar().currentIndex() == 2);
    CHECK(finder.key(Qt::Key_G, Qt::ControlModifier));
    CHECK(finder.bar().currentIndex() == 3);
    // `Ctrl+Shift+G` is a Global row too (`pane.signatures`); FindSearch takes it back, as §8 says.
    CHECK(finder.key(Qt::Key_G, Qt::ControlModifier | Qt::ShiftModifier));
    CHECK(finder.bar().currentIndex() == 2);
    CHECK(finder.showsCurrentHit());

    // `Enter` JUMPS TO THE HIGHLIGHTED RESULT: scrolled away, it brings the current hit back on screen.
    finder.view->goToPage(10);
    finder.fixture.pumpTiles();
    REQUIRE_FALSE(finder.showsCurrentHit());
    REQUIRE(finder.fieldHasFocus());
    CHECK(finder.key(Qt::Key_Return));
    CHECK(finder.bar().currentIndex() == 2);
    CHECK(finder.showsCurrentHit());

    // `Alt+R`, `Alt+W`, `Alt+C`: each flips its SETTING.
    CHECK_FALSE(finder.fixture.settings.get(core::keys::search::useRegex));
    CHECK(finder.key(Qt::Key_R, Qt::AltModifier));
    CHECK(finder.fixture.settings.get(core::keys::search::useRegex));
    CHECK(finder.key(Qt::Key_W, Qt::AltModifier));
    CHECK(finder.fixture.settings.get(core::keys::search::wholeWords));
    CHECK(finder.key(Qt::Key_C, Qt::AltModifier));
    CHECK(finder.fixture.settings.get(core::keys::search::caseSensitive));
    // ... and the search ran again under them: `line 3` as a regular expression, whole words, case -- 12.
    finder.settle();
    CHECK(finder.bar().hits().size() == 12);

    // `Esc` CLOSES THE BAR AND CLEARS EVERY HIGHLIGHT, and the focus goes back to the page.
    REQUIRE(finder.overlay().hasSearchHits());
    CHECK(finder.key(Qt::Key_Escape));
    CHECK_FALSE(finder.bar().isOpen());
    CHECK_FALSE(finder.overlay().hasSearchHits());
    CHECK(finder.window->focusWidget() == finder.view);
    CHECK(finder.context() == core::KeyContext::Viewer);
}

TEST_CASE("Ctrl+F inside the bar keeps the focus there and selects the query", "[findbar][A-69]")
{
    Finder finder;
    finder.find(QStringLiteral("line"));
    finder.bar().field()->deselect();
    REQUIRE(finder.context() == core::KeyContext::FindSearch);
    CHECK(finder.key(Qt::Key_F, Qt::ControlModifier));   // the FindSearch row this time
    CHECK(finder.fieldHasFocus());
    CHECK(finder.bar().field()->selectedText() == QStringLiteral("line"));
}

// =============================================================================================
// A-57's bar half -- an invalid regular expression searches nothing and says so
// =============================================================================================

TEST_CASE("an invalid regular expression in the bar is the error and searches nothing", "[findbar][A-57]")
{
    Finder finder;
    finder.fixture.settings.set(core::keys::search::useRegex, true);
    const long long refused = finder.fixture.searchEngine.stats().refused;
    finder.find(QStringLiteral("("));
    CHECK(finder.bar().hits().empty());
    CHECK(finder.bar().searchesStarted() == 0);
    CHECK(finder.fixture.searchEngine.stats().refused == refused + 1);
    // The ERROR, naming the pattern -- never "no matches".
    const QString expected = ui::catalog::translateError(QStringLiteral("error.search.invalidPattern"), QStringLiteral("("));
    CHECK(finder.bar().statusText() == expected);
    CHECK_FALSE(finder.bar().statusText().contains(ui::catalog::translate(ui::catalog::key::SearchBarNoMatches)));
    // THE CONTROL: a valid pattern after it searches.
    finder.type(QStringLiteral("line 1)"));   // `(line 1)` -- a group, valid
    finder.settle();
    CHECK(finder.bar().hits().size() == 12);
}

// =============================================================================================
// A-70 -- incremental, debounced and cancelled
// =============================================================================================

TEST_CASE("typing a word in one burst runs one search and only the last query delivers", "[findbar][A-70]")
{
    Finder finder;
    finder.view->setFocus(Qt::OtherFocusReason);
    REQUIRE(finder.key(Qt::Key_F, Qt::ControlModifier));

    // ONE BURST, faster than the debounce: no search yet, and then exactly one.
    finder.type(QStringLiteral("line"));
    CHECK(finder.bar().searchesStarted() == 0);
    CHECK(finder.bar().isSearchPending());
    finder.settle();
    CHECK(finder.bar().searchesStarted() == 1);
    CHECK(finder.bar().searchesFinished() == 1);
    CHECK(finder.bar().hits().size() == 60);

    // A SEARCH STARTED AND OVERTAKEN: the engine thread is paused so the search for `line ` cannot finish,
    // then `3` is typed -- the older is cancelled and only the newer delivers.
    finder.fixture.renderScheduler.pause();
    finder.fixture.documentIndex.invalidate(finder.document()->indexEntry(), core::Scope::wholeDocument());
    finder.type(QStringLiteral(" "));
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ui::FindReplaceBar::kDebounceMs + 80) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    REQUIRE(finder.bar().searchesStarted() == 2);
    REQUIRE(finder.bar().isSearching());
    finder.type(QStringLiteral("3"));
    finder.fixture.renderScheduler.resume();
    finder.settle();
    CHECK(finder.bar().searchesStarted() == 3);
    CHECK(finder.bar().searchesFinished() == 2);   // the first, and the last -- never the overtaken one
    CHECK(finder.bar().hits().size() == 12);
}

// =============================================================================================
// A-71 -- highlight all, the current hit, and wrap
// =============================================================================================

TEST_CASE("highlight all paints every hit or only the current one and stepping wraps and says so", "[findbar][A-71]")
{
    Finder finder;
    finder.find(QStringLiteral("text"));   // once a line, five a page
    REQUIRE(finder.bar().hits().size() == 60);
    const core::render::PageLayout& layout = finder.view->layoutEngine().layout();

    REQUIRE(finder.bar().highlightAll());
    CHECK(finder.overlay().searchHighlightRects(layout, 0).size() == 5);
    CHECK(finder.overlay().searchHighlightRects(layout, 0, true).size() == 1);
    finder.bar().setHighlightAll(false);
    CHECK(finder.overlay().searchHighlightRects(layout, 0).size() == 1);   // only the current one
    finder.bar().setHighlightAll(true);

    // BACK PAST THE FIRST: the last hit, and the bar says it wrapped.
    REQUIRE(finder.bar().currentIndex() == 0);
    CHECK(finder.key(Qt::Key_F3, Qt::ShiftModifier));
    CHECK(finder.bar().currentIndex() == 59);
    CHECK(finder.bar().statusText().contains(ui::catalog::translate(ui::catalog::key::SearchBarWrappedToLast)));
    CHECK(finder.showsCurrentHit());
    // AND ON PAST THE LAST: the first again, said.
    CHECK(finder.key(Qt::Key_F3));
    CHECK(finder.bar().currentIndex() == 0);
    CHECK(finder.bar().statusText().contains(ui::catalog::translate(ui::catalog::key::SearchBarWrappedToFirst)));
    // THE CONTROL: an ordinary step says nothing about wrapping.
    CHECK(finder.key(Qt::Key_F3));
    CHECK_FALSE(finder.bar().statusText().contains(ui::catalog::translate(ui::catalog::key::SearchBarWrappedToFirst)));
}

// =============================================================================================
// A-72 -- Esc closes the bar and clears every highlight
// =============================================================================================

TEST_CASE("Esc closes the bar clears every highlight and returns the focus to the page", "[findbar][A-72]")
{
    Finder finder;
    finder.find(QStringLiteral("text"));
    REQUIRE(finder.overlay().searchHits().size() == 60);
    CHECK(finder.key(Qt::Key_Escape));
    CHECK_FALSE(finder.bar().isOpen());
    CHECK(finder.overlay().searchHits().empty());
    CHECK(finder.overlay().searchHighlightRects(finder.view->layoutEngine().layout(), 0).empty());
    CHECK(finder.window->focusWidget() == finder.view);
    // Opened again, the query and its highlights come back without asking the engine again.
    const long long started = finder.fixture.searchEngine.stats().started;
    REQUIRE(finder.key(Qt::Key_F, Qt::ControlModifier));
    CHECK(finder.overlay().searchHits().size() == 60);
    CHECK(finder.fixture.searchEngine.stats().started == started);
}

// =============================================================================================
// A-73 -- the toggles ARE the settings
// =============================================================================================

TEST_CASE("the bar's toggles are the search settings in both directions", "[findbar][A-73]")
{
    Finder finder;
    finder.find(QStringLiteral("line"));
    struct Toggle {
        Qt::Key key;
        core::Key<bool> setting;
        const char* button;
    };
    const auto buttonFor = [&finder](ActionId id) -> QToolButton* {
        for (QToolButton* button : finder.bar().findChildren<QToolButton*>())
            if (button->toolTip() == ui::catalog::translateAction(id)) return button;
        return nullptr;
    };
    const std::vector<std::pair<Toggle, ActionId>> toggles{
        {{Qt::Key_R, core::keys::search::useRegex, "regex"}, ActionId::FindToggleRegex},
        {{Qt::Key_W, core::keys::search::wholeWords, "whole words"}, ActionId::FindToggleWholeWords},
        {{Qt::Key_C, core::keys::search::caseSensitive, "case"}, ActionId::FindToggleCaseSensitive}};
    for (const auto& [toggle, id] : toggles) {
        INFO(toggle.button);
        QToolButton* button = buttonFor(id);
        REQUIRE(button != nullptr);
        REQUIRE(button->isCheckable());
        CHECK_FALSE(button->isChecked());

        // THE KEY writes the setting, and the button shows it.
        finder.bar().field()->setFocus(Qt::OtherFocusReason);
        CHECK(finder.key(toggle.key, Qt::AltModifier));
        CHECK(finder.fixture.settings.get(toggle.setting));
        CHECK(button->isChecked());

        // THE SETTING -- as Preferences writes it -- moves the button.
        finder.fixture.settings.set(toggle.setting, false);
        CHECK_FALSE(button->isChecked());

        // AND THE BUTTON writes the setting.
        button->click();
        CHECK(finder.fixture.settings.get(toggle.setting));
        finder.fixture.settings.set(toggle.setting, false);
    }
}

TEST_CASE("a changed flag runs the search again under it", "[findbar][A-73]")
{
    Finder finder;
    finder.find(QStringLiteral("Line"));
    REQUIRE(finder.bar().hits().size() == 60);   // folded: every `line`
    finder.fixture.settings.set(core::keys::search::caseSensitive, true);
    finder.settle();
    CHECK(finder.bar().hits().empty());          // `Line` does not appear with a capital
    finder.fixture.settings.set(core::keys::search::caseSensitive, false);
    finder.settle();
    CHECK(finder.bar().hits().size() == 60);
}

// =============================================================================================
// A-74's widget half -- a search asks for no tile
// =============================================================================================

TEST_CASE("a search and its highlights enqueue no tile", "[findbar][A-74]")
{
    Finder finder;
    // Read every page's text first, so what is counted below is the search and the paint alone.
    finder.find(QStringLiteral("text"));
    finder.key(Qt::Key_Escape);
    finder.fixture.pumpTiles();
    const auto before = finder.fixture.renderScheduler.stats();
    const auto tiles = [](const core::render::RenderScheduler::Stats& s) { return s.enqueued - s.textEnqueued; };

    // `Page 1 line` is on page 1 only -- on screen, so revealing it scrolls nothing.
    finder.find(QStringLiteral("Page 1 line"));
    REQUIRE(finder.bar().hits().size() == 5);
    finder.view->viewport()->repaint();
    finder.fixture.pumpTiles();
    const auto after = finder.fixture.renderScheduler.stats();
    CHECK(tiles(after) == tiles(before));
    CHECK(after.textEnqueued == before.textEnqueued);
}

// =============================================================================================
// A-75 -- Ctrl+F from the page and from a pane; hits in a split
// =============================================================================================

TEST_CASE("Ctrl+F opens the bar from the thumbnails pane and hits show in both panes of a split", "[findbar][A-75]")
{
    Finder finder;
    // FROM A PANE: the Thumbnails pane's own context does not bind `Ctrl+F`, so it falls through to Common.
    REQUIRE(finder.fixture.actions.dispatch(ActionId::PaneThumbnails, core::KeyContext::Global));
    driveLayout(finder.window.get());
    QWidget* pane = nullptr;
    for (QWidget* widget : finder.window->findChildren<QWidget*>())
        if (widget->isVisible() && ui::ShortcutBinder::declaredContext(widget) == core::KeyContext::PageThumbnails) {
            pane = widget;
            break;
        }
    REQUIRE(pane != nullptr);
    pane->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    if (finder.window->focusWidget() == nullptr || ui::ShortcutBinder::contextFor(finder.window->focusWidget()) != core::KeyContext::PageThumbnails) {
        // The pane's focus proxy is a child: focus the first focusable one inside it.
        for (QWidget* child : pane->findChildren<QWidget*>())
            if (child->focusPolicy() != Qt::NoFocus && child->isVisible()) {
                child->setFocus(Qt::OtherFocusReason);
                break;
            }
        QCoreApplication::processEvents();
    }
    REQUIRE(finder.context() == core::KeyContext::PageThumbnails);
    CHECK(finder.key(Qt::Key_F, Qt::ControlModifier));
    CHECK(finder.bar().isOpen());
    CHECK(finder.fieldHasFocus());
    finder.key(Qt::Key_Escape);

    // IN A SPLIT: both panes paint the same overlay, so a hit on page 1 has rectangles in each pane's layout.
    REQUIRE(finder.fixture.actions.dispatch(ActionId::WindowSplit, core::KeyContext::Viewer));
    driveLayout(finder.window.get());
    finder.fixture.pumpTiles();
    const auto panes = finder.document()->split()->views();
    REQUIRE(panes.size() == 2);
    finder.find(QStringLiteral("Page 1 line"));
    REQUIRE(finder.bar().hits().size() == 5);
    for (ui::PageView* view : panes) {
        CHECK(view->selection() == &finder.overlay());
        CHECK(finder.overlay().searchHighlightRects(view->layoutEngine().layout(), 0).size() == 5);
    }
}

// =============================================================================================
// Scope -- a page range and the selection, through the bar
// =============================================================================================

TEST_CASE("the bar searches a page range and the selection and says what it cannot search", "[findbar][A-60]")
{
    Finder finder;
    finder.find(QStringLiteral("line 3"));
    REQUIRE(finder.bar().hits().size() == 12);

    finder.bar().setScope(ui::FindReplaceBar::Scope::Pages);
    finder.bar().setPageRange(QStringLiteral("3-5"));
    finder.settle();
    REQUIRE(finder.bar().hits().size() == 3);
    for (const auto& hit : finder.bar().hits()) CHECK((hit.page >= 2 && hit.page <= 4));
    finder.bar().setPageRange(QStringLiteral("x"));
    finder.settle();
    CHECK(finder.bar().hits().empty());
    CHECK(finder.bar().statusText() == ui::catalog::translate(ui::catalog::key::SearchBarBadRange));

    // THE SELECTION: none yet, which is said; then `Ctrl+A`'s whole document, which finds every one.
    finder.bar().setScope(ui::FindReplaceBar::Scope::Selection);
    finder.settle();
    CHECK(finder.bar().statusText() == ui::catalog::translate(ui::catalog::key::SearchBarNoSelection));
    finder.overlay().selectAll();
    finder.bar().setScope(ui::FindReplaceBar::Scope::Document);
    finder.bar().setScope(ui::FindReplaceBar::Scope::Selection);
    finder.settle();
    CHECK(finder.bar().hits().size() == 12);
}

TEST_CASE("each tab keeps its own find bar and search", "[findbar][A-75]")
{
    Finder finder;
    finder.find(QStringLiteral("line 3"));
    ui::DocumentWindow* first = finder.document();
    finder.window->openFiles({finder.fixture.writeFile(QStringLiteral("second.pdf"))});
    driveLayout(finder.window.get());
    ui::DocumentWindow* second = finder.window->currentDocument();
    REQUIRE(second != first);
    CHECK(second->findBar() != first->findBar());
    CHECK_FALSE(second->findBar()->isOpen());
    CHECK(first->findBar()->hits().size() == 12);
    CHECK(second->selection()->searchHits().empty());
}
