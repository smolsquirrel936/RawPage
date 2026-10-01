// Stage 5 T5 -- the Selection tool, `ui/viewer/SelectionOverlay` and `edit.copy` / `edit.selectAll`, as a
// WIDGET: the keys, the lanes, `Esc`, the panes of a split and the tabs. Guide cases A-24's selection half,
// A-43's gesture, A-51 … A-55 (`Stage5-TextSearch-Testing-Guide.md` §5.7).
//
// WHAT THIS SUITE CANNOT SEE, named here (convention 19):
//   * **INK.** The fixture's rasterizer paints flat blue and its text extractor invents five lines a
//     page (`tests/ui/ShellFixture.h`); nothing here draws a glyph, so no case here says a highlight is
//     ON the text. That is rule 6, and it is `tests/composition/tst_selectionink.cpp`'s, over the real
//     engine and the corpus.
//   * **A REAL CLIPBOARD.** Offscreen, `QClipboard` is Qt's in-process one: what a copy put there can be
//     read back, and nothing outside the process can paste it. CF_HTML reaching Windows is guide A-50,
//     `tst_clipboard.cpp`, on the `windows` platform; how Word renders it is M-02's.
//   * **A REAL POINTER.** Clicks arrive as synthetic events in quick succession, which is what makes the
//     multi-click count; the platform's own double-click timing is M-01's walk.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/DocumentPropertiesDialog.h"
#include "ui/dialogs/PreferencesDialog.h"
#include "ui/dialogs/ShortcutsPage.h"
#include "ui/panes/LogPane.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/StatusBar.h"
#include "ui/shell/ToolFinder.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/SelectionOverlay.h"
#include "ui/viewer/SplitViewContainer.h"
#include "ui/viewer/ToolController.h"
#include "ui/write/FindReplaceBar.h"

#include <QAbstractSpinBox>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProgressBar>

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

// The centre of character `n` of line `line` of the fixture's fake text, in PAGE space. A caret taken
// AT a centre is the one BEFORE the character (`PageReading::caretAt`'s half rule), so a drag that must
// end AFTER character `n` ends at `charEnd(line, n)`, in its right half.
core::PdfPoint charCentre(int line, int n)
{
    return core::PdfPoint{uitest::kFakeTextLeft + uitest::kFakeTextAdvance * (n + 0.5),
                          uitest::kFakeTextFirstBaseline - uitest::kFakeTextLeading * line + 3.0};
}

// ... and a drag that must start BEFORE character `n` starts in its left half. A point exactly at a
// centre is rounded to a whole pixel on its way through the viewport and may land either side.
core::PdfPoint charStart(int line, int n)
{
    const core::PdfPoint centre = charCentre(line, n);
    return core::PdfPoint{centre.x - uitest::kFakeTextAdvance * 0.3, centre.y};
}

core::PdfPoint charEnd(int line, int n)
{
    const core::PdfPoint centre = charCentre(line, n);
    return core::PdfPoint{centre.x + uitest::kFakeTextAdvance * 0.3, centre.y};
}

struct Viewer {
    Fixture                         fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::PageView*                   view = nullptr;

    explicit Viewer(int pages = 12)
    {
        fixture.backend.pagesPerDocument = pages;
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 850);
        window->openFiles({fixture.writeFile(QStringLiteral("select.pdf"))});
        driveLayout(window.get());
        view = window->currentView();
        REQUIRE(view != nullptr);
        window->raiseToForeground();
        view->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        fixture.pumpTiles();

        // Rule 4: the geometry first, and the context the chords resolve in.
        REQUIRE(view->width() > 400);
        REQUIRE(view->height() > 300);
        REQUIRE(window->shortcutBinder()->activeContext() == core::KeyContext::Viewer);
    }

    ui::DocumentWindow* document() const { return window->currentDocument(); }
    ui::SelectionOverlay& selection() const { return *document()->selection(); }

    // A page point in the view's VIEWPORT, through the layout's own mapping -- where a person would
    // put the pointer to be over it.
    QPoint at(int page, core::PdfPoint point, ui::PageView* in = nullptr) const
    {
        ui::PageView* target = in != nullptr ? in : view;
        const auto content = target->layoutEngine().layout().toContent(page, point);
        REQUIRE(content.has_value());
        const core::DeviceRect visible = target->controller().visibleRect();
        const double dpr = target->devicePixelRatioF();
        return QPoint(qRound((content->x - visible.left()) / dpr), qRound((content->y - visible.top()) / dpr));
    }

    bool key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, QWidget* to = nullptr) const
    {
        QWidget* focus = to != nullptr ? to : window->focusWidget();
        REQUIRE(focus != nullptr);
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        event.ignore();
        QCoreApplication::sendEvent(focus, &event);
        QCoreApplication::processEvents();
        return event.isAccepted();
    }

    void mouse(QEvent::Type type, QPoint pos, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
               ui::PageView* in = nullptr) const
    {
        ui::PageView* target = in != nullptr ? in : view;
        QMouseEvent event(type, QPointF(pos), QPointF(target->viewport()->mapToGlobal(pos)), Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(target->viewport(), &event);
        QCoreApplication::processEvents();
    }

    void drag(QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        mouse(QEvent::MouseButtonPress, from, modifiers);
        mouse(QEvent::MouseMove, to, modifiers);
        mouse(QEvent::MouseButtonRelease, to, modifiers);
    }

    void click(QPoint at, int times = 1, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        for (int i = 0; i < times; ++i) {
            mouse(QEvent::MouseButtonPress, at, modifiers);
            mouse(QEvent::MouseButtonRelease, at, modifiers);
        }
    }

    QString copied() const
    {
        const core::text::CopiedText text = selection().copiedText();
        return text.plain;
    }
};

void setClipboard(const QString& text)
{
    QGuiApplication::clipboard()->setText(text);
    REQUIRE(QGuiApplication::clipboard()->text() == text);
}

} // namespace

// =============================================================================================
// A-43's gesture -- two, three and four clicks, through real mouse events
// =============================================================================================

TEST_CASE("two three and four clicks select the word the line and the block", "[selecttool][A-43]")
{
    Viewer viewer;
    // `line` on page 1's second line: "Page 1 line 2 text", characters 7 to 10.
    const QPoint onLine = viewer.at(0, charCentre(1, 8));

    viewer.click(onLine, 2);
    viewer.fixture.pumpTiles();
    CHECK(viewer.view->lastClickCount() == 2);
    CHECK(viewer.copied() == QStringLiteral("line"));

    // A fresh gesture: wait out nothing, but move the pointer far enough that the next press is a first.
    viewer.click(viewer.at(0, charCentre(4, 2)));
    viewer.click(onLine, 3);
    CHECK(viewer.view->lastClickCount() == 3);
    CHECK(viewer.copied() == uitest::fakeLineText(0, 1));

    viewer.click(viewer.at(0, charCentre(4, 2)));
    viewer.click(onLine, 4);
    CHECK(viewer.view->lastClickCount() == 4);
    QString block;
    for (int line = 0; line < uitest::kFakeTextLines; ++line)
        block += (line > 0 ? QStringLiteral("\n") : QString()) + uitest::fakeLineText(0, line);
    CHECK(viewer.copied() == block);

    // THE CONTROL: one click selects nothing -- it is a caret, and a click is how a selection is dropped.
    viewer.click(viewer.at(0, charCentre(4, 2)));
    viewer.click(onLine, 1);
    CHECK_FALSE(viewer.selection().hasSelection());
}

// =============================================================================================
// A-51 -- Ctrl+C and Ctrl+A from key events; the whole document, extracted for the copy
// =============================================================================================

TEST_CASE("Ctrl+C copies a drag and Ctrl+A copies the whole document from key events", "[selecttool][A-51]")
{
    Viewer viewer;
    // A drag across page 1's first two lines, from `Page` to `line`.
    viewer.drag(viewer.at(0, charStart(0, 0)), viewer.at(0, charEnd(1, 10)));
    viewer.fixture.pumpTiles();
    REQUIRE(viewer.selection().hasSelection());

    setClipboard(QStringLiteral("sentinel"));
    CHECK(viewer.key(Qt::Key_C, Qt::ControlModifier));   // consumed: `edit.copy` dispatched
    CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("Page 1 line 1 text\nPage 1 line"));
    // Both flavours in ONE object (D5-9).
    const QMimeData* data = QGuiApplication::clipboard()->mimeData();
    REQUIRE(data != nullptr);
    CHECK(data->hasHtml());
    CHECK(data->html().contains(QStringLiteral("Page 1 line 1 text Page 1 line")));

    SECTION("Ctrl+A selects the DOCUMENT, and the copy reads every page first, at Background")
    {
        CHECK(viewer.key(Qt::Key_A, Qt::ControlModifier));
        CHECK(viewer.selection().selection().firstPage() == 0);
        CHECK(viewer.selection().selection().lastPage() == 11);

        // Nothing extracted by the Ctrl+A itself beyond what is on screen (Q6: lazily) -- the pages on
        // screen are asked for by the paint, at `Visible`, and are let arrive before the copy is asked.
        viewer.fixture.pumpTiles();
        const std::vector<int> missing = viewer.selection().selection().missingPages(viewer.selection().text());
        REQUIRE_FALSE(missing.empty());
        const long long backgroundBefore = viewer.fixture.renderScheduler.stats().enqueuedBackground;

        CHECK(viewer.key(Qt::Key_C, Qt::ControlModifier));
        CHECK(viewer.selection().isCopyPending());
        CHECK(viewer.window->statusBar()->taskProgress()->isVisibleTo(viewer.window.get()));
        viewer.fixture.pumpTiles();

        // The pages the copy lacked were asked for in the BACKGROUND lane (D5-3, D5-9) -- read from the
        // scheduler's own counters.
        CHECK(viewer.fixture.renderScheduler.stats().enqueuedBackground - backgroundBefore
              == static_cast<long long>(missing.size()));
        CHECK_FALSE(viewer.selection().isCopyPending());
        const QString all = QGuiApplication::clipboard()->text();
        CHECK(all.startsWith(QStringLiteral("Page 1 line 1 text")));
        CHECK(all.endsWith(QStringLiteral("Page 12 line 5 text")));
        CHECK(all.count(QStringLiteral("\n\n")) == 11);   // a blank line between pages, eleven of them
        CHECK_FALSE(viewer.window->statusBar()->taskProgress()->isVisibleTo(viewer.window.get()));
    }
}

TEST_CASE("Esc during a copy that is still extracting cancels it and leaves the clipboard alone", "[selecttool][A-51]")
{
    Viewer viewer;
    setClipboard(QStringLiteral("what was there before"));

    // THE ENGINE THREAD HELD, so the copy is certainly still waiting when `Esc` arrives -- a race won by
    // the worker would make this case pass by never testing the cancel.
    viewer.fixture.renderScheduler.pause();
    CHECK(viewer.key(Qt::Key_A, Qt::ControlModifier));
    CHECK(viewer.key(Qt::Key_C, Qt::ControlModifier));
    REQUIRE(viewer.selection().isCopyPending());

    CHECK(viewer.key(Qt::Key_Escape));                 // consumed: it cancelled something
    CHECK_FALSE(viewer.selection().isCopyPending());
    viewer.fixture.renderScheduler.resume();
    viewer.fixture.pumpTiles();

    CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("what was there before"));
    // And the selection is still there: `Esc` cancelled the COPY, not what was selected.
    CHECK(viewer.selection().hasSelection());
    CHECK_FALSE(viewer.window->statusBar()->message()->text().isEmpty());
}

// =============================================================================================
// A-52 -- every text widget keeps its own Ctrl+C and Ctrl+A (Stage 4's I-06 turned round)
// =============================================================================================

TEST_CASE("every text widget keeps its own Ctrl+C and Ctrl+A", "[selecttool][A-52]")
{
    Viewer viewer;
    // A DOCUMENT SELECTION EXISTS, so a key that fell through to `edit.copy` would copy it and one that
    // fell through to `edit.selectAll` would widen it -- either is visible below.
    viewer.drag(viewer.at(0, charStart(0, 0)), viewer.at(0, charEnd(0, 3)));
    viewer.fixture.pumpTiles();
    REQUIRE(viewer.copied() == QStringLiteral("Page"));
    const core::text::TextPosition before = viewer.selection().selection().end();

    const auto check = [&viewer, &before](QWidget* widget, const QString& own, const char* name) {
        INFO(name);
        REQUIRE(widget != nullptr);
        widget->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        setClipboard(QStringLiteral("untouched"));

        viewer.key(Qt::Key_A, Qt::ControlModifier, widget);
        viewer.key(Qt::Key_C, Qt::ControlModifier, widget);

        // The widget's own text reached the clipboard -- its Ctrl+A selected it, its Ctrl+C copied it.
        CHECK(QGuiApplication::clipboard()->text() == own);
        // And the document's selection is exactly as it was.
        CHECK(viewer.selection().selection().end() == before);
        CHECK(viewer.copied() == QStringLiteral("Page"));
    };

    // The tool finder's search box.
    QLineEdit* finder = viewer.window->toolFinder()->searchBox();
    finder->setText(QStringLiteral("zoom"));
    check(finder, QStringLiteral("zoom"), "the tool finder");

    // The status bar's page box.
    QLineEdit* pageBox = viewer.window->statusBar()->pageBox();
    pageBox->setText(QStringLiteral("7"));
    check(pageBox, QStringLiteral("7"), "the page box");

    // The find bar's field (S5 T7) -- the first widget the guide's Expect names. `Ctrl+F` from the page
    // opens it through section 8's Common row; inside it the context is FindSearch, which binds neither key,
    // so both would fall through to Common -- and the field must keep them.
    REQUIRE(viewer.key(Qt::Key_F, Qt::ControlModifier));
    ui::FindReplaceBar* bar = viewer.document()->findBar();
    REQUIRE(bar != nullptr);
    REQUIRE(bar->isOpen());
    bar->field()->setText(QStringLiteral("lorem"));
    check(bar->field(), QStringLiteral("lorem"), "the find bar");
    bar->close();

    // The log pane, a READ-ONLY text view -- the one Qt does not claim `Ctrl+C` for by itself.
    ui::LogPane* log = viewer.window->logPane();
    REQUIRE(log != nullptr);
    log->textView()->setPlainText(QStringLiteral("a line of the log"));
    driveLayout(viewer.window.get());
    check(log->textView(), QStringLiteral("a line of the log"), "the log pane");

    // A Preferences editor -- a dialog OWNED by the window, so the window's binder filters its keys.
    ui::PreferencesDialog preferences(viewer.fixture.services(), viewer.window.get());
    preferences.show();
    QCoreApplication::processEvents();
    // The first page that shows a free-text editor -- not a spin box's own line edit.
    QLineEdit* editor = nullptr;
    for (const core::SettingsGroup group : ui::PreferencesDialog::shippedPages()) {
        preferences.showPage(group);
        QCoreApplication::processEvents();
        for (QLineEdit* candidate : preferences.findChildren<QLineEdit*>()) {
            if (candidate->isVisible() && !candidate->isReadOnly()
                && qobject_cast<QAbstractSpinBox*>(candidate->parentWidget()) == nullptr) {
                editor = candidate;
                break;
            }
        }
        if (editor != nullptr) break;
    }
    REQUIRE(editor != nullptr);
    editor->setText(QStringLiteral("a preference"));
    check(editor, QStringLiteral("a preference"), "a Preferences editor");

    // BEYOND THE GUIDE'S LIST, two more widgets that own a chord natively. The Shortcuts page's key
    // recorder: `Ctrl+C` typed into it is a chord being RECORDED, and must neither copy nor reach the
    // document.
    preferences.showPage(core::SettingsGroup::Shortcuts);
    QCoreApplication::processEvents();
    QKeySequenceEdit* recorder = preferences.shortcutsPage()->findChild<QKeySequenceEdit*>();
    REQUIRE(recorder != nullptr);
    recorder->setFocus(Qt::OtherFocusReason);
    setClipboard(QStringLiteral("untouched"));
    viewer.key(Qt::Key_C, Qt::ControlModifier, recorder);
    CHECK(recorder->keySequence() == QKeySequence(Qt::CTRL | Qt::Key_C));
    CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("untouched"));
    CHECK(viewer.copied() == QStringLiteral("Page"));
    preferences.close();

    // And Document Properties' values, which are LABELS a person can select and copy by keyboard.
    ui::DocumentPropertiesDialog::Properties facts;
    facts.info.title = QStringLiteral("A title to copy");
    ui::DocumentPropertiesDialog properties(facts, viewer.window.get());
    properties.show();
    QCoreApplication::processEvents();
    QLabel* title = properties.valueLabel(ui::DocumentPropertiesDialog::Field::Title);
    REQUIRE(title != nullptr);
    check(title, QStringLiteral("A title to copy"), "a Document Properties value");
    properties.close();

    // THE CONTROL: the same two keys with the PAGE focused reach the document -- so every check above is
    // a widget keeping its key, not the commands doing nothing.
    viewer.view->setFocus(Qt::OtherFocusReason);
    setClipboard(QStringLiteral("untouched"));
    CHECK(viewer.key(Qt::Key_C, Qt::ControlModifier));
    CHECK(QGuiApplication::clipboard()->text() == QStringLiteral("Page"));
    CHECK(viewer.key(Qt::Key_A, Qt::ControlModifier));
    CHECK(viewer.selection().selection().lastPage() == 11);
}

// =============================================================================================
// A-53 -- a selection changes no raster
// =============================================================================================

TEST_CASE("making extending and clearing a selection enqueues no tile", "[selecttool][A-53]")
{
    Viewer viewer;
    // The page's text read FIRST, so what follows asks the scheduler for nothing at all.
    viewer.click(viewer.at(0, charCentre(0, 1)));
    viewer.fixture.pumpTiles();
    REQUIRE(viewer.selection().text().has(0));

    const core::render::RenderScheduler::Stats before = viewer.fixture.renderScheduler.stats();
    const long long insertionsBefore = viewer.fixture.renderCache.stats().insertions;
    const std::size_t entriesBefore = viewer.fixture.renderCache.entryCount();

    viewer.drag(viewer.at(0, charCentre(0, 1)), viewer.at(0, charCentre(2, 6)));           // make
    viewer.click(viewer.at(0, charCentre(3, 4)), 1, Qt::ShiftModifier);                   // extend
    REQUIRE(viewer.selection().hasSelection());
    viewer.view->viewport()->repaint();                                                     // painted over the tiles
    CHECK(viewer.key(Qt::Key_Escape));                                                      // clear
    viewer.fixture.pumpTiles();

    const core::render::RenderScheduler::Stats after = viewer.fixture.renderScheduler.stats();
    // No tile and no text job: the tile counters are the general ones less the text subset.
    CHECK(after.enqueued - after.textEnqueued == before.enqueued - before.textEnqueued);
    CHECK(after.textEnqueued == before.textEnqueued);
    CHECK(viewer.fixture.renderCache.stats().insertions == insertionsBefore);
    CHECK(viewer.fixture.renderCache.entryCount() == entriesBefore);
}

// =============================================================================================
// A-54 -- Esc clears a selection and otherwise does nothing
// =============================================================================================

TEST_CASE("Esc clears a selection and with none is not consumed", "[selecttool][A-54]")
{
    Viewer viewer;
    viewer.drag(viewer.at(0, charStart(0, 0)), viewer.at(0, charCentre(0, 5)));
    viewer.fixture.pumpTiles();
    REQUIRE(viewer.selection().hasSelection());

    CHECK(viewer.key(Qt::Key_Escape));
    CHECK_FALSE(viewer.selection().hasSelection());

    // THE CONTROL, Stage 4's A-42 kept: nothing selected, nothing cancelled, the key travels on.
    const core::DevicePoint scroll = viewer.view->controller().scroll();
    CHECK_FALSE(viewer.key(Qt::Key_Escape));
    CHECK(viewer.view->controller().scroll().y == scroll.y);
    CHECK(viewer.view->tools().armed() == ui::ToolKind::Select);

    SECTION("Esc under a live drag puts back what was selected before it")
    {
        viewer.drag(viewer.at(0, charStart(0, 0)), viewer.at(0, charEnd(0, 3)));
        REQUIRE(viewer.copied() == QStringLiteral("Page"));
        viewer.mouse(QEvent::MouseButtonPress, viewer.at(0, charCentre(2, 0)));
        viewer.mouse(QEvent::MouseMove, viewer.at(0, charCentre(3, 10)));
        REQUIRE(viewer.copied() != QStringLiteral("Page"));
        CHECK(viewer.key(Qt::Key_Escape));
        CHECK(viewer.copied() == QStringLiteral("Page"));
        viewer.mouse(QEvent::MouseButtonRelease, viewer.at(0, charCentre(3, 10)));
        CHECK(viewer.copied() == QStringLiteral("Page"));
    }
}

// =============================================================================================
// A-55 and A-24's selection half -- an unextracted page, a split, a tab switch
// =============================================================================================

TEST_CASE("a drag on an unextracted page asks at Visible and completes when the text arrives", "[selecttool][A-55][A-24]")
{
    Viewer viewer;
    REQUIRE_FALSE(viewer.selection().text().has(0));

    viewer.fixture.renderScheduler.pause();
    const core::render::RenderScheduler::Stats before = viewer.fixture.renderScheduler.stats();
    viewer.drag(viewer.at(0, charStart(0, 0)), viewer.at(0, charEnd(0, 3)));

    // Asked for, in the VISIBLE lane -- a person is waiting (D5-3) -- and nothing selected yet.
    const core::render::RenderScheduler::Stats asked = viewer.fixture.renderScheduler.stats();
    CHECK(asked.textEnqueued > before.textEnqueued);
    CHECK(asked.enqueuedVisible - before.enqueuedVisible >= asked.textEnqueued - before.textEnqueued);
    CHECK(asked.enqueuedBackground == before.enqueuedBackground);
    CHECK_FALSE(viewer.selection().hasSelection());

    // The engine goes on; the gesture completes as the points it was made at.
    viewer.fixture.renderScheduler.resume();
    viewer.fixture.pumpTiles();
    CHECK(viewer.copied() == QStringLiteral("Page"));
}

TEST_CASE("a selection shows in both panes of a split and belongs to its own tab", "[selecttool][A-55]")
{
    Viewer viewer;
    viewer.drag(viewer.at(0, charStart(0, 0)), viewer.at(0, charCentre(1, 5)));
    viewer.fixture.pumpTiles();
    REQUIRE(viewer.selection().hasSelection());

    // SPLIT: the new pane is handed the SAME selection, and paints it over its own layout.
    REQUIRE(viewer.fixture.actions.dispatch(ActionId::WindowSplit, core::KeyContext::Viewer));
    driveLayout(viewer.window.get());
    viewer.fixture.pumpTiles();
    const std::vector<ui::PageView*> panes = viewer.document()->split()->views();
    REQUIRE(panes.size() == 2);
    for (ui::PageView* pane : panes) {
        CHECK(pane->selection() == viewer.document()->selection());
        pane->refresh();
        CHECK_FALSE(viewer.selection().highlightRects(pane->layoutEngine().layout(), 0).empty());
    }

    // A SECOND TAB has its own, empty; the first keeps its own.
    viewer.window->openFiles({viewer.fixture.writeFile(QStringLiteral("other.pdf"))});
    driveLayout(viewer.window.get());
    ui::DocumentWindow* second = viewer.window->currentDocument();
    REQUIRE(second != nullptr);
    REQUIRE(second != viewer.window->documentAt(0));
    CHECK_FALSE(second->selection()->hasSelection());
    CHECK(viewer.window->documentAt(0)->selection()->hasSelection());
    CHECK(second->selection() != viewer.window->documentAt(0)->selection());
}

// =============================================================================================
// D5-8 as clarified at T5 -- Shift: a click extends, a drag pans; Alt: a drag is a column
// =============================================================================================

TEST_CASE("a Shift click extends the selection and a Shift drag still pans", "[selecttool][D5-8]")
{
    Viewer viewer;
    viewer.drag(viewer.at(0, charStart(0, 0)), viewer.at(0, charEnd(0, 3)));
    viewer.fixture.pumpTiles();
    REQUIRE(viewer.copied() == QStringLiteral("Page"));

    // A CLICK with Shift: the anchor stays, the focus moves to the end of line 2's `line`.
    viewer.click(viewer.at(0, charEnd(1, 10)), 1, Qt::ShiftModifier);
    CHECK(viewer.copied() == QStringLiteral("Page 1 line 1 text\nPage 1 line"));

    // A DRAG with Shift: the temporary hand, as at Stage 4 -- the view moves, the selection does not.
    viewer.view->controller().setScroll(core::DevicePoint{0.0, 200.0});
    const core::DevicePoint scroll = viewer.view->controller().scroll();
    const QPoint from(500, 400);
    viewer.drag(from, from + QPoint(0, -120), Qt::ShiftModifier);
    CHECK(viewer.view->controller().scroll().y > scroll.y);
    CHECK(viewer.copied() == QStringLiteral("Page 1 line 1 text\nPage 1 line"));
    viewer.view->tools().setModifiers(Qt::NoModifier);
}

TEST_CASE("an Alt drag selects a column and copies it as a table", "[selecttool][A-46]")
{
    Viewer viewer;
    // `line` sits at characters 7 to 10 on every line; a rectangle over them, lines 1 to 3.
    const QPoint from = viewer.at(0, core::PdfPoint{uitest::kFakeTextLeft + uitest::kFakeTextAdvance * 7.0 - 1.0,
                                                    uitest::kFakeTextFirstBaseline + 8.0});
    const QPoint to = viewer.at(0, core::PdfPoint{uitest::kFakeTextLeft + uitest::kFakeTextAdvance * 11.0 - 1.0,
                                                  uitest::kFakeTextFirstBaseline - 2.0 * uitest::kFakeTextLeading});
    viewer.drag(from, to, ui::SelectTool::kColumnModifier);
    viewer.fixture.pumpTiles();

    REQUIRE(viewer.selection().selection().kind() == core::text::TextSelection::Kind::Column);
    const core::text::CopiedText copied = viewer.selection().copiedText();
    CHECK(copied.isTable);
    CHECK(copied.plain == QStringLiteral("line\nline\nline"));
    CHECK(copied.html.count(QStringLiteral("<tr>")) == 3);

    // THE CONTROL: the same drag without Alt is a flow, in reading order.
    viewer.drag(from, to);
    viewer.fixture.pumpTiles();
    CHECK(viewer.selection().selection().kind() == core::text::TextSelection::Kind::Flow);
    CHECK(viewer.copied().contains(QStringLiteral("text\nPage 1")));
}
