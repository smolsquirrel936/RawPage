// tests/ui/tst_bookmarksedit.cpp -- the Bookmarks pane's editing rows, from KEY EVENTS. Stage 7 T11, guide A-73 and
// A-74 (`Stage7-Assembly-Plan.md` D7-10; `Features.md` §8's Bookmarks pane table and the Viewer's `Ctrl+B`).
//
// Every key below reaches the focused widget as a `QKeyEvent` -- through `ShortcutBinder`, as a person's would -- and
// each case reads what the JOURNAL now holds: the model's outline, and one more step of history per key. What the
// commands write to a file is `tests/unit/tst_outlineedit.cpp`'s and `tests/render/tst_outlinereader.cpp`'s.
//
// WHAT THIS DOES NOT COVER (convention 19): the fake backend's sink records the outline it is handed and writes
// nothing, so nothing here says a file holds the edit; and a key held by another program never reaches the product,
// which no offscreen case can see (`CLAUDE.md` -> Traps) -- M-05 is the walk with real keys.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/BookmarkDialogs.h"
#include "ui/panes/BookmarksPane.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/SidebarDock.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ViewportController.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMimeData>

#include <memory>

using namespace rawpage;
using core::ActionId;
using core::OutlineItem;
using core::OutlineItemId;

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

OutlineItem bookmark(long long object, const QString& title, int page, bool open = false)
{
    OutlineItem item;
    item.id = OutlineItemId::ofObject(object);
    item.title = title;
    core::Destination at;
    at.pageIndex = page;
    at.fit = core::Destination::Fit::Fit;
    item.action = core::OutlineAction::goTo(at);
    item.destination = at;
    item.openByDefault = open;
    return item;
}

// A, with A1 and A2 under it; B; C, with C1 under it -- every item by its object, as the first-party reading names them.
core::Outline outline()
{
    core::Outline out;
    OutlineItem a = bookmark(10, QStringLiteral("A"), 0, true);
    a.children.push_back(bookmark(11, QStringLiteral("A1"), 1));
    a.children.push_back(bookmark(12, QStringLiteral("A2"), 2));
    OutlineItem c = bookmark(30, QStringLiteral("C"), 5, true);
    c.children.push_back(bookmark(31, QStringLiteral("C1"), 6));
    out.roots().push_back(std::move(a));
    out.roots().push_back(bookmark(20, QStringLiteral("B"), 3));
    out.roots().push_back(std::move(c));
    return out;
}

// The tree as titles, depth first, two spaces a level.
std::vector<std::string> shape(const std::vector<OutlineItem>& items, int depth = 0)
{
    std::vector<std::string> out;
    for (const OutlineItem& item : items) {
        out.push_back(std::string(static_cast<std::size_t>(depth) * 2, ' ') + item.title.toStdString());
        for (std::string& child : shape(item.children, depth + 1)) out.push_back(std::move(child));
    }
    return out;
}

struct Answers {
    ~Answers()
    {
        ui::DeleteBookmarkDialog::setAnswerForTesting({});
        ui::BookmarkPropertiesDialog::setAnswerForTesting({});
        ui::GenerateBookmarksDialog::setAnswerForTesting({});
        ui::BookmarkFileDialog::setAnswersForTesting({}, {});
    }
};

struct Window {
    Fixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::BookmarksPane* pane = nullptr;

    explicit Window(bool editable = true, std::optional<core::Error> refusal = std::nullopt)
    {
        fixture.backend.pagesPerDocument = 8;
        fixture.backend.editable = editable;
        fixture.backend.outline_ = outline();
        if (refusal) fixture.backend.outline_.setStatus(*refusal);
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1200, 900);
        window->openFiles({fixture.writeFile(QStringLiteral("book.pdf"))});
        driveLayout(window.get());
        pane = window->bookmarksPane();
        REQUIRE(pane != nullptr);
        REQUIRE(window->sidebar()->showPane(ActionId::PaneBookmarks));
        driveLayout(window.get());
        window->raiseToForeground();
        focusPane();
        REQUIRE(pane->width() > 80);
        REQUIRE(pane->visibleRowCount() > 0);
    }
    ~Window()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void focusPane()
    {
        pane->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
    }
    core::CommandJournal* journal() const { return window->currentDocument()->journal(); }
    const core::Outline& model() const { return journal()->document().outline(); }
    bool press(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString& text = QString())
    {
        QWidget* focus = QApplication::focusWidget();
        REQUIRE(focus != nullptr);
        QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
        event.ignore();
        QCoreApplication::sendEvent(focus, &event);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
        QCoreApplication::sendEvent(focus, &release);
        QCoreApplication::processEvents();
        return event.isAccepted();
    }
    void select(long long object) { REQUIRE(pane->selectId(OutlineItemId::ofObject(object))); }
};

} // namespace

// ==========================================================================================
// A-73 -- the whole Bookmarks pane table, from key events
// ==========================================================================================

TEST_CASE("ui.bookmarks_edit: every editing row of the Bookmarks pane table, from key events, one undo step each", "[bookmarksedit][A-73]")
{
    Answers answers;
    Window w;
    REQUIRE(w.journal() != nullptr);
    REQUIRE(w.model().status());
    REQUIRE(w.pane->isEditable());
    REQUIRE(shape(w.model().roots()) == std::vector<std::string>{"A", "  A1", "  A2", "B", "C", "  C1"});
    std::size_t steps = w.journal()->position();
    const auto oneStep = [&] {
        const std::size_t now = w.journal()->position();
        const bool one = now == steps + 1;
        steps = now;
        return one;
    };

    SECTION("F2 renames in place; the editor keeps Delete, and Enter commits")
    {
        w.select(20);
        CHECK(w.press(Qt::Key_F2));
        REQUIRE(w.pane->isRenaming());
        auto* editor = qobject_cast<QLineEdit*>(QApplication::focusWidget());
        REQUIRE(editor != nullptr);
        editor->selectAll();
        // THE EDITOR'S OWN KEYS (guide A-56's bookmark half): `Delete` deletes the selected text, not the bookmark.
        w.press(Qt::Key_Delete);
        CHECK(w.model().find(OutlineItemId::ofObject(20)) != nullptr);
        CHECK(editor->text().isEmpty());
        editor->setText(QStringLiteral("Bee 章"));
        w.press(Qt::Key_Return);
        CHECK_FALSE(w.pane->isRenaming());
        CHECK(w.model().find(OutlineItemId::ofObject(20))->title == QStringLiteral("Bee 章"));
        auto* sink = w.fixture.backend.sinkFor(w.window->currentDocument()->handle());
        REQUIRE(sink != nullptr);
        CHECK(sink->outlineSet.find(OutlineItemId::ofObject(20))->title == QStringLiteral("Bee 章"));
        CHECK(w.pane->selectedTitle() == QStringLiteral("Bee 章"));
        CHECK(oneStep());
        // THE CONTROL: an editor closed unchanged requests nothing.
        CHECK(w.press(Qt::Key_F2));
        REQUIRE(w.pane->isRenaming());
        w.press(Qt::Key_Return);
        CHECK(w.journal()->position() == steps);
    }
    SECTION("Delete asks about children -- kept, then with them -- and a leaf is deleted unasked")
    {
        int asked = 0;
        bool keep = true;
        ui::DeleteBookmarkDialog::setAnswerForTesting([&](const QString& title, int children) -> std::optional<bool> {
            ++asked;
            CHECK(title == QStringLiteral("A"));
            CHECK(children == 2);
            return keep;
        });
        w.select(10);
        CHECK(w.press(Qt::Key_Delete));
        CHECK(asked == 1);
        CHECK(shape(w.model().roots()) == std::vector<std::string>{"A1", "A2", "B", "C", "  C1"});
        CHECK(oneStep());
        REQUIRE(w.journal()->undo());
        steps = w.journal()->position();
        keep = false;
        w.select(10);
        CHECK(w.press(Qt::Key_Delete));
        CHECK(asked == 2);
        CHECK(shape(w.model().roots()) == std::vector<std::string>{"B", "C", "  C1"});
        CHECK(oneStep());
        w.select(20);
        CHECK(w.press(Qt::Key_Delete));
        CHECK(asked == 2);   // a leaf: nothing to ask about
        CHECK(shape(w.model().roots()) == std::vector<std::string>{"C", "  C1"});
        CHECK(oneStep());
    }
    SECTION("Tab demotes and Shift+Tab promotes")
    {
        w.select(20);
        CHECK(w.press(Qt::Key_Tab));
        CHECK(shape(w.model().roots()) == std::vector<std::string>{"A", "  A1", "  A2", "  B", "C", "  C1"});
        CHECK(oneStep());
        CHECK(w.pane->selectedTitle() == QStringLiteral("B"));   // the moved bookmark stays selected
        w.focusPane();
        CHECK(w.press(Qt::Key_Backtab, Qt::ShiftModifier));
        CHECK(shape(w.model().roots()) == std::vector<std::string>{"A", "  A1", "  A2", "B", "C", "  C1"});
        CHECK(oneStep());
        // NOWHERE TO GO is disabled, and does nothing: the first item has no sibling before it to go under.
        w.select(10);
        CHECK_FALSE(w.fixture.actions.isEnabled(ActionId::BookmarkDemote));
        w.press(Qt::Key_Tab);
        CHECK(w.journal()->position() == steps);
    }
    SECTION("Alt+Up and Alt+Down move among siblings")
    {
        w.select(12);
        CHECK(w.press(Qt::Key_Up, Qt::AltModifier));
        CHECK(shape(w.model().roots()) == std::vector<std::string>{"A", "  A2", "  A1", "B", "C", "  C1"});
        CHECK(oneStep());
        w.select(10);
        CHECK(w.press(Qt::Key_Down, Qt::AltModifier));
        CHECK(shape(w.model().roots()) == std::vector<std::string>{"B", "A", "  A2", "  A1", "C", "  C1"});
        CHECK(oneStep());
        // B is the first now: Alt+Up has nowhere to take it, and is disabled.
        w.select(20);
        CHECK_FALSE(w.fixture.actions.isEnabled(ActionId::BookmarkMoveUp));
    }
    SECTION("Ctrl+B adds a bookmark to the current view after the selected one, its title open for typing")
    {
        ui::PageView* view = w.window->currentView();
        REQUIRE(view != nullptr);
        view->goToPage(4);
        QCoreApplication::processEvents();
        w.select(20);
        w.focusPane();
        CHECK(w.press(Qt::Key_B, Qt::ControlModifier));
        CHECK(oneStep());
        REQUIRE(w.model().roots().size() == 4);
        const OutlineItem& made = w.model().roots()[2];
        CHECK(made.id.isCreated());
        CHECK(made.destination.pageIndex == view->controller().currentPage());
        CHECK(made.destination.fit == core::Destination::Fit::XYZ);
        CHECK(made.destination.hasZoom);
        CHECK(w.pane->isRenaming());
    }
    SECTION("Ctrl+Shift+D sets the destination to the current view")
    {
        ui::PageView* view = w.window->currentView();
        REQUIRE(view != nullptr);
        view->goToPage(6);
        QCoreApplication::processEvents();
        w.select(20);
        w.focusPane();
        CHECK(w.press(Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier));
        CHECK(oneStep());
        CHECK(w.model().find(OutlineItemId::ofObject(20))->destination.pageIndex == view->controller().currentPage());
        CHECK(w.model().find(OutlineItemId::ofObject(20))->action.kind == core::OutlineAction::Kind::GoTo);
    }
    SECTION("the navigation rows Stage 4 registered are unchanged")
    {
        w.select(30);
        CHECK(w.press(Qt::Key_Left));
        CHECK_FALSE(w.pane->isExpanded(OutlineItemId::ofObject(30)));
        CHECK(w.press(Qt::Key_Right));
        CHECK(w.pane->isExpanded(OutlineItemId::ofObject(30)));
        CHECK(w.journal()->position() == steps);   // a view state, not a command
    }
}

TEST_CASE("ui.bookmarks_edit: a drop is one move, refused onto its own descendant", "[bookmarksedit][A-73]")
{
    Answers answers;
    Window w;
    const auto rowRect = [&w](long long object) {
        REQUIRE(w.pane->selectId(OutlineItemId::ofObject(object)));
        return w.pane->visualItemRect(w.pane->currentItem());
    };
    // THE DROP'S PLACE AND ITS REQUEST, through the handler `dropEvent` calls. NOT COVERED (convention 19): Qt delivering
    // a real drag -- the offscreen platform has no drag manager, so a synthetic `QDropEvent` never reaches the view;
    // M-05's walk drags with a mouse.
    const auto drop = [&w](long long dragged, QPoint at) {
        w.select(dragged);
        REQUIRE(w.pane->requestDropAt(at));
        QCoreApplication::processEvents();
    };
    int requested = 0;
    QObject::connect(w.pane, &ui::BookmarksPane::moveRequested, w.pane, [&requested](OutlineItemId, core::OutlinePosition) { ++requested; });
    const std::size_t before = w.journal()->position();
    // B dropped ON C: the last of C's children.
    drop(20, rowRect(30).center());
    CHECK(requested == 1);
    CHECK(shape(w.model().roots()) == std::vector<std::string>{"A", "  A1", "  A2", "C", "  C1", "  B"});
    CHECK(w.journal()->position() == before + 1);
    // A dropped onto its own child A2: requested, refused by the command, nothing moved.
    drop(10, rowRect(12).center());
    CHECK(shape(w.model().roots()) == std::vector<std::string>{"A", "  A1", "  A2", "C", "  C1", "  B"});
    CHECK(w.journal()->position() == before + 1);
}

TEST_CASE("ui.bookmarks_edit: an outline whose readings disagree is read-only and says why", "[bookmarksedit][A-73][A-75]")
{
    Answers answers;
    Window w(true, core::Error{core::ErrorCode::PermissionDenied, QLatin1String("error.outline.readingsDisagree"), QStringLiteral("title of item 3")});
    CHECK_FALSE(w.pane->isEditable());
    CHECK_FALSE(w.pane->readOnlyReason().isEmpty());
    CHECK(w.pane->readOnlyReason().contains(QStringLiteral("title of item 3")));
    CHECK_FALSE(w.pane->isHeaderHidden());
    w.select(20);
    for (const ActionId id : {ActionId::BookmarkRename, ActionId::BookmarkDelete, ActionId::BookmarkDemote, ActionId::BookmarkNew,
                              ActionId::BookmarkSetDestination, ActionId::BookmarkProperties}) {
        INFO(std::string(core::actionStringId(id)));
        CHECK_FALSE(w.fixture.actions.isEnabled(id));
    }
    const std::size_t before = w.journal()->position();
    w.press(Qt::Key_F2);
    CHECK_FALSE(w.pane->isRenaming());
    w.press(Qt::Key_Tab);
    CHECK(w.journal()->position() == before);
    // THE CONTROL: the same outline agreeing is editable.
    Window agreeing;
    CHECK(agreeing.pane->isEditable());
    CHECK(agreeing.pane->isHeaderHidden());
}

TEST_CASE("ui.bookmarks_edit: the properties dialog's answer is one undo step, a colour and an action included", "[bookmarksedit][A-78]")
{
    Answers answers;
    Window w;
    ui::BookmarkPropertiesDialog::setAnswerForTesting([](const OutlineItem& item, int, const core::Destination&) {
        ui::BookmarkPropertiesDialog::Choice choice;
        choice.properties = core::EditBookmarkCommand::Properties::of(item);
        choice.properties.bold = true;
        choice.properties.colour = std::array<double, 3>{0.8, 0.0, 0.0};
        choice.action.kind = core::OutlineAction::Kind::Uri;
        choice.action.uri = QStringLiteral("https://example.com/");
        return std::optional<ui::BookmarkPropertiesDialog::Choice>(choice);
    });
    w.select(20);
    const std::size_t before = w.journal()->position();
    REQUIRE(w.fixture.actions.dispatch(ActionId::BookmarkProperties, core::KeyContext::Bookmarks));
    QCoreApplication::processEvents();
    CHECK(w.journal()->position() == before + 1);
    const OutlineItem* item = w.model().find(OutlineItemId::ofObject(20));
    CHECK(item->bold);
    CHECK(item->colour == std::array<double, 3>{0.8, 0.0, 0.0});
    CHECK(item->action.kind == core::OutlineAction::Kind::Uri);
    REQUIRE(w.journal()->undo());
    CHECK_FALSE(w.model().find(OutlineItemId::ofObject(20))->bold);
    CHECK(w.model().find(OutlineItemId::ofObject(20))->action.kind == core::OutlineAction::Kind::GoTo);
}

// ==========================================================================================
// A-74 -- `Ctrl+B` in the Viewer
// ==========================================================================================

TEST_CASE("ui.viewer_commands: Ctrl+B with the page focused adds a bookmark to the current page and zoom", "[bookmarksedit][viewercommands][A-74]")
{
    Answers answers;
    Window w;
    ui::PageView* view = w.window->currentView();
    REQUIRE(view != nullptr);
    view->goToPage(5);
    view->setFocus(Qt::OtherFocusReason);
    QCoreApplication::processEvents();
    const std::size_t before = w.journal()->position();
    CHECK(w.press(Qt::Key_B, Qt::ControlModifier));
    CHECK(w.journal()->position() == before + 1);
    REQUIRE(w.model().roots().size() == 4);
    // After the bookmark the pane has selected -- the first, A, as it opened.
    const OutlineItem& made = w.model().roots()[1];
    CHECK(made.id.isCreated());
    CHECK(made.destination.pageIndex == view->controller().currentPage());
    CHECK(made.destination.hasZoom);
    CHECK(made.destination.zoom == view->controller().zoom().factor);
    CHECK_FALSE(w.pane->isRenaming());   // from the Viewer: added, not opened for typing
}
