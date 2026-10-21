// tst_commentspane -- Stage 6 T9 through the widgets: the Comments pane, its keys and its commands, over the fake
// backend's store (`Stage6-Annotations-Testing-Guide.md` §5.12; A-67's pane half, A-68, A-69, A-70, A-71, A-72's
// list half, A-43's *listed hidden*; D6-12).
//
//   * A-70: `Ctrl+Shift+C` opens the pane from the page; then, from KEY EVENTS through the application (the
//     window's `ShortcutBinder` sees each first): `Down` walks the list and the page follows; `Enter` opens the
//     comment's own editor (the properties bar's text, focused); `F2` edits in place and `Enter` saves it as ONE
//     edit; `Ctrl+Enter` replies, typed, as ONE reply under the comment; `Space` is the checkmark; `Ctrl+Shift+K`
//     the status menu; `Ctrl+F` the search field; `Ctrl+.`/`Ctrl+,` step and wrap, moving the page view;
//     `Delete` takes the comment with its thread as one step -- and in the VIEWER `Space` still scrolls (the
//     collision's control) and `Ctrl+.` steps the same list.
//   * A-67's pane half: the list IS the store's query, grouped and filtered as the controls say.
//   * A-69: the checkmark shown and set is the CURRENT identity's.
//   * A-71: *Delete all* and *Delete matching* are one command each, and one undo restores each.
//   * A-72: numbering shows numbers in the list and on the page's overlay, and constructs nothing.
//   * A-43: a Hidden comment is listed, and said to be hidden.
//   * I-13 (guide §8): the store changing while the pane is hidden, then a selection on the page coming back to
//     it, is no out-of-range read of a stale row.
//   * I-30 (guide §8): the comment column keeps room for its deepest row's indentation and check box, a narrow
//     pane scrolls sideways, and a cell too narrow for its box paints nothing beyond itself.
//   * I-31 (guide §8): the properties bar `Enter` shows asks the window for no width, so the sidebar can still be
//     dragged wider, and its focused text field is scrolled into sight.
//   * I-32 (guide §8): a right click on a comment offers §8's rows and its status, and every delete of comments --
//     the menu's, the key's, the bulk ones and the page's -- asks first, and is disabled where nobody can be asked.
//   * I-33 (guide §8): each row starts with a picture of its kind, and says its text alone, or `(Empty)`.
//   * I-34 (guide §8): a click on a row -- not an arrow -- opens the comment's pop-up on the page, and a press or the
//     focus anywhere else in the window closes it.
//
// WHAT IT DOES NOT COVER (convention 19): what the sink writes -- the fake sink accepts everything; the bytes are
// `tests/unit/tst_annotthreads`'s -- and the engine's pixels (A-72's tile half is
// `tests/composition/tst_commentscale`'s). Events go to the widget below Windows' input queue.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/settings/Settings.h"
#include "tests/ui/ShellFixture.h"
#include "ui/panes/CommentIcon.h"
#include "ui/panes/CommentsPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/SidebarDock.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/AnnotationTextEditor.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/PropertiesBar.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QImage>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyleFactory>
#include <QTreeView>

#include <set>

using namespace rawpage;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;

namespace {

core::PdfRect box(double l, double b, double r, double t)
{
    return core::PdfRect{core::PdfPoint{l, b}, core::PdfSize{r - l, t - b}};
}

const QString kAna = QStringLiteral("Ana Lima");
const QString kBen = QStringLiteral("Ben Okafor");
const QString kZoe = QStringLiteral("Zoë Müller");

// A note with a pop-up, a reply and a status; a highlight; a HIDDEN square; and a note on page 2.
core::AnnotationStore threadedStore()
{
    core::AnnotationStore::Reading reading;
    reading.pages.resize(2);
    const auto add = [&](long long object, int page, AnnotationSubtype subtype, const QString& author, const QString& text,
                         core::PdfRect rect, std::int64_t at, std::uint32_t flags = core::AnnotationFlag::Print) {
        Annotation a;
        a.id = AnnotationId::ofObject(object);
        a.subtype = subtype;
        a.page = page;
        a.author = author;
        a.contents = text;
        a.rect = rect;
        a.flags = flags;
        a.color = {1, 1, 0};
        a.created = core::PdfDate::fromUtcSeconds(at);
        a.modified = a.created;
        a.hasAppearance = subtype != AnnotationSubtype::Popup;
        if (subtype == AnnotationSubtype::Text) a.icon = QStringLiteral("Comment");
        if (subtype == AnnotationSubtype::Highlight) a.quadPoints = {rect.left(), rect.top(), rect.right(), rect.top(),
                                                                      rect.left(), rect.bottom(), rect.right(), rect.bottom()};
        core::AnnotationListingEntry entry;
        entry.fact = core::AnnotationFact{true, subtype, rect};
        entry.annotation = reading.annotations.size();
        reading.pages[static_cast<std::size_t>(page)].push_back(entry);
        reading.annotations.push_back(a);
        return &reading.annotations.back();
    };
    const std::uint32_t note = core::AnnotationFlag::Print | core::AnnotationFlag::NoZoom | core::AnnotationFlag::NoRotate;
    add(10, 0, AnnotationSubtype::Text, kAna, QStringLiteral("Is this figure current?"), box(500, 700, 520, 720), 1790000000, note);
    add(11, 0, AnnotationSubtype::Popup, QString(), QString(), box(300, 600, 500, 700), 0, note);
    Annotation* reply = add(12, 0, AnnotationSubtype::Text, kBen, QStringLiteral("Updated last week."), box(500, 700, 520, 720),
                            1790000100, note);
    reply->inReplyTo = AnnotationId::ofObject(10);
    Annotation* state = add(13, 0, AnnotationSubtype::Text, kBen, QString(), box(500, 700, 520, 720), 1790000200, note | 2);
    state->inReplyTo = AnnotationId::ofObject(10);
    state->stateModel = QStringLiteral("Review");
    state->state = QStringLiteral("Accepted");
    state->hasAppearance = false;
    add(14, 0, AnnotationSubtype::Highlight, kZoe, QStringLiteral("Source for this number?"), box(100, 600, 200, 612), 1790000300);
    add(15, 0, AnnotationSubtype::Square, kBen, QStringLiteral("Out of sight"), box(100, 300, 200, 400), 1790000400,
        core::AnnotationFlag::Print | core::AnnotationFlag::Hidden);
    add(16, 1, AnnotationSubtype::Text, kAna, QStringLiteral("A thread on page two."), box(500, 700, 520, 720), 1790000500, note);
    reading.annotations[0].popup = AnnotationId::ofObject(11);
    reading.annotations[1].parent = AnnotationId::ofObject(10);
    return core::AnnotationStore::fromReading(std::move(reading));
}

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

struct Commented {
    Fixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow* document = nullptr;
    ui::PageView* view = nullptr;
    ui::CommentsPane* pane = nullptr;
    // The delete question (I-32): what was asked, and what the case answers.
    std::vector<QString> questions;
    bool answer = true;

    Commented()
    {
        fixture.backend.pagesPerDocument = 2;
        fixture.backend.editable = true;
        fixture.backend.annotations_ = threadedStore();
        fixture.settings.set(core::keys::identity::author, kAna);
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1200, 900);
        // Every delete asks first (I-32); a case that means No says so itself.
        window->setDeleteAnswerForTesting([this](const QString& question) {
            questions.push_back(question);
            return answer;
        });
        window->openFiles({fixture.writeFile(QStringLiteral("comments.pdf"))});
        window->show();
        layout();
        document = window->currentDocument();
        REQUIRE(document != nullptr);
        view = window->currentView();
        REQUIRE(view != nullptr);
        REQUIRE(view->width() > 300);   // the geometry first (convention 12)
        pane = window->commentsPane();
        REQUIRE(pane != nullptr);
        pane->setClock([] { return std::int64_t{1791763200}; });
        document->annotations()->setClock([] { return std::int64_t{1791763200}; });
        window->raiseToForeground();
        view->setFocus();
        QCoreApplication::processEvents();
    }

    void layout() const
    {
        for (QWidget* widget : window->findChildren<QWidget*>())
            if (QLayout* l = widget->layout()) l->activate();
        QCoreApplication::processEvents();
    }

    core::CommandJournal& journal() const { return *document->journal(); }
    const core::AnnotationStore& store() const { return journal().document().annotations(); }
    std::size_t steps() const { return journal().historySize(); }
    const Annotation& at(long long object) const { return store().at(*store().findObject(object)); }

    // A key as a person presses one: to the focused widget, through the application.
    bool key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString& text = QString()) const
    {
        QWidget* focus = QApplication::focusWidget();
        if (focus == nullptr) focus = view;
        QKeyEvent override(QEvent::ShortcutOverride, key, modifiers, text);
        override.ignore();
        QCoreApplication::sendEvent(focus, &override);
        QKeyEvent event(QEvent::KeyPress, key, modifiers, text);
        event.ignore();
        QCoreApplication::sendEvent(focus, &event);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
        QCoreApplication::sendEvent(focus, &release);
        QCoreApplication::processEvents();
        return event.isAccepted() || override.isAccepted();
    }
    void type(const QString& text) const
    {
        for (const QChar c : text) key(Qt::Key_unknown, Qt::NoModifier, QString(c));
    }
    // `Ctrl+Shift+C` from the page: the pane, shown and focused.
    void open() const
    {
        view->setFocus();
        QCoreApplication::processEvents();
        REQUIRE(key(Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier));
        layout();
        REQUIRE(pane->isVisible());
        REQUIRE(pane->view()->isVisible());
    }
    bool inPane() const
    {
        const QWidget* focus = QApplication::focusWidget();
        return focus != nullptr && pane->isAncestorOf(focus);
    }
    // The middle of `id`'s row in the list's viewport -- made current quietly to find it, as nothing a person sees.
    QPoint rowOf(const AnnotationId& id) const
    {
        REQUIRE(pane->selectComment(id));
        return pane->view()->visualRect(pane->view()->currentIndex()).center();
    }
    // A press and a release at `at`, on `target` (the list's viewport by default), with `button`.
    void clickAt(QPoint at, QWidget* target = nullptr, Qt::MouseButton button = Qt::LeftButton) const
    {
        QWidget* to = target != nullptr ? target : pane->view()->viewport();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), QPointF(to->mapToGlobal(at)), button, button, Qt::NoModifier);
        QCoreApplication::sendEvent(to, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(at), QPointF(to->mapToGlobal(at)), button, Qt::NoButton,
                            Qt::NoModifier);
        QCoreApplication::sendEvent(to, &release);
        QCoreApplication::processEvents();
    }
    // A right click on the list at `at`: the context-menu event Qt sends after the press.
    void rightClickAt(QPoint at) const
    {
        clickAt(at, nullptr, Qt::RightButton);
        QWidget* to = pane->view()->viewport();
        QContextMenuEvent event(QContextMenuEvent::Mouse, at, to->mapToGlobal(at));
        QCoreApplication::sendEvent(to, &event);
        QCoreApplication::processEvents();
    }
    // The right-click menu's item whose label starts with `label`.
    QAction* itemNamed(QMenu* menu, const QString& label) const
    {
        for (QAction* action : menu->actions())
            if (!action->isSeparator() && action->text().startsWith(label)) return action;
        return nullptr;
    }
    ui::AnnotationOverlay* overlay() const { return document->annotations(); }
};

} // namespace

// ==========================================================================================
// A-70 -- the whole Comments pane table, from key events
// ==========================================================================================

TEST_CASE("Ctrl Shift C opens the pane from the page and its keys are the Comments table", "[commentspane][A-70]")
{
    Commented c;
    c.open();
    CHECK(c.inPane());
    CHECK(ui::ShortcutBinder::contextFor(QApplication::focusWidget()) == core::KeyContext::Comments);
    // OPENING IT MOVES NOTHING: the list taking the focus is not a person choosing a comment.
    CHECK(c.document->annotations()->selection().empty());
    CHECK(c.view->controller().currentPage() == 0);
    REQUIRE(c.pane->visibleRowCount() > 0);   // convention 12: a non-zero count before anything about it

    // THE LIST IS THE STORE'S TOP LEVEL: the two notes, the highlight and the hidden square -- never the
    // pop-up, the reply as an item, or the status.
    const std::vector<AnnotationId> listed = c.pane->listedComments();
    CHECK(listed == std::vector<AnnotationId>{AnnotationId::ofObject(10), AnnotationId::ofObject(14),
                                              AnnotationId::ofObject(15), AnnotationId::ofObject(16)});
    CHECK(c.pane->visibleRowCount() == 5);   // and the reply under its note, the thread open

    // `Down` WALKS THE LIST (the item view's own) AND THE PAGE FOLLOWS: the comment selected on the page.
    c.pane->view()->setFocus();
    c.pane->view()->setCurrentIndex(c.pane->view()->model()->index(0, 0));
    QCoreApplication::processEvents();
    REQUIRE(c.pane->currentComment() == AnnotationId::ofObject(10));
    REQUIRE(c.key(Qt::Key_Down));   // into the thread: the reply
    CHECK(c.pane->currentComment() == AnnotationId::ofObject(12));
    REQUIRE(c.key(Qt::Key_Down));
    CHECK(c.pane->currentComment() == AnnotationId::ofObject(14));
    CHECK(c.document->annotations()->selection() == std::vector<AnnotationId>{AnnotationId::ofObject(14)});

    // `Enter` -- THE COMMENT'S OWN EDITOR: the properties bar, its text focused.
    REQUIRE(c.key(Qt::Key_Return));
    REQUIRE(c.document->propertiesBarShown());
    CHECK(QApplication::focusWidget() == c.document->propertiesBar()->textField());
    CHECK(c.document->propertiesBar()->textField()->text() == QStringLiteral("Source for this number?"));

    // `F2` -- EDIT IN PLACE, below the list: typed and `Enter`, ONE edit.
    c.pane->view()->setFocus();
    QCoreApplication::processEvents();
    std::size_t steps = c.steps();
    REQUIRE(c.key(Qt::Key_F2));
    REQUIRE(c.pane->editor() != nullptr);
    CHECK(c.pane->editor()->toPlainText() == QStringLiteral("Source for this number?"));
    CHECK(c.steps() == steps);   // nothing until it is finished
    c.pane->editor()->selectAll();
    c.type(QStringLiteral("Which table? "));   // a SPACE typed here is a space, not the checkmark
    REQUIRE(c.key(Qt::Key_Return));
    CHECK(c.steps() == steps + 1);
    CHECK(c.journal().undoName()->key == QLatin1String("command.annot.edit"));
    CHECK(c.at(14).contents == QStringLiteral("Which table? "));
    CHECK(c.inPane());

    // `Ctrl+Enter` -- A REPLY, typed below and `Enter`: ONE reply, under the highlight, by the identity.
    steps = c.steps();
    REQUIRE(c.key(Qt::Key_Return, Qt::ControlModifier));
    REQUIRE(c.pane->editor() != nullptr);
    c.type(QStringLiteral("Table 4."));
    CHECK(c.steps() == steps);
    REQUIRE(c.key(Qt::Key_Return));
    CHECK(c.steps() == steps + 1);
    CHECK(c.journal().undoName()->key == QLatin1String("command.annot.reply"));
    const std::vector<std::size_t> replies = c.store().replies(*c.store().findObject(14));
    REQUIRE(replies.size() == 1);
    CHECK(c.store().at(replies[0]).contents == QStringLiteral("Table 4."));
    CHECK(c.store().at(replies[0]).author == kAna);
    CHECK(c.pane->listedComments().size() == 4);   // a reply is not an item

    // `Space` -- THE CHECKMARK: one command, Ana's.
    c.pane->selectComment(AnnotationId::ofObject(14));
    c.pane->view()->setFocus();
    steps = c.steps();
    REQUIRE(c.key(Qt::Key_Space));
    CHECK(c.steps() == steps + 1);
    CHECK(c.journal().undoName()->key == QLatin1String("command.annot.checkmark"));
    CHECK(c.store().isMarkedBy(*c.store().findObject(14), kAna));

    // `Ctrl+Shift+K` -- THE STATUS MENU, and a state chosen from it: one command, the status now.
    REQUIRE(c.key(Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier));
    REQUIRE(c.pane->statusMenu()->isVisible());
    steps = c.steps();
    QAction* completed = nullptr;
    for (QAction* action : c.pane->statusMenu()->actions())
        if (action->data().toString() == QLatin1String("Completed")) completed = action;
    REQUIRE(completed != nullptr);
    completed->trigger();
    c.pane->statusMenu()->hide();
    QCoreApplication::processEvents();
    CHECK(c.steps() == steps + 1);
    CHECK(c.store().reviewStatus(*c.store().findObject(14)) == QLatin1String("Completed"));
    CHECK(c.pane->textOf(AnnotationId::ofObject(14), 4) == ui::catalog::translate(QStringLiteral("comments.status.Completed")));

    // `Ctrl+F` -- SEARCH WITHIN COMMENTS: the pane's field, and typing narrows the list.
    c.pane->view()->setFocus();
    REQUIRE(c.key(Qt::Key_F, Qt::ControlModifier));
    CHECK(QApplication::focusWidget() == c.pane->searchField());
    c.type(QStringLiteral("week"));   // only in the reply under note 10
    CHECK(c.pane->listedComments() == std::vector<AnnotationId>{AnnotationId::ofObject(10)});
    c.pane->searchField()->clear();
    CHECK(c.pane->listedComments().size() == 4);

    // `Ctrl+.` / `Ctrl+,` -- STEP, AND THE PAGE VIEW GOES WITH IT; past the last it wraps.
    c.pane->selectComment(AnnotationId::ofObject(15));
    c.pane->view()->setFocus();
    REQUIRE(c.key(Qt::Key_Period, Qt::ControlModifier));
    CHECK(c.pane->currentComment() == AnnotationId::ofObject(16));
    CHECK(c.view->controller().currentPage() == 1);   // the note on page 2, revealed
    CHECK(c.document->annotations()->selection() == std::vector<AnnotationId>{AnnotationId::ofObject(16)});
    REQUIRE(c.key(Qt::Key_Period, Qt::ControlModifier));
    CHECK(c.pane->currentComment() == AnnotationId::ofObject(10));   // wrapped
    CHECK(c.view->controller().currentPage() == 0);
    REQUIRE(c.key(Qt::Key_Comma, Qt::ControlModifier));
    CHECK(c.pane->currentComment() == AnnotationId::ofObject(16));   // and back, wrapping the other way

    // `Delete` -- THE COMMENT AND ITS THREAD, one step: note 10, its pop-up, its reply and its status.
    c.pane->selectComment(AnnotationId::ofObject(10));
    c.pane->view()->setFocus();
    const std::size_t count = c.store().size();
    steps = c.steps();
    REQUIRE(c.key(Qt::Key_Delete));
    CHECK(c.steps() == steps + 1);
    CHECK(c.store().size() == count - 4);
    for (const long long gone : {10LL, 11LL, 12LL, 13LL}) CHECK_FALSE(c.store().findObject(gone));
    REQUIRE(c.journal().undo());
    CHECK(c.store().size() == count);
}

TEST_CASE("in the Viewer Space still scrolls and Ctrl period steps the same list", "[commentspane][A-70]")
{
    Commented c;
    // NOT OPENED: the list is kept current all the same, and the Viewer's rows reach it.
    c.view->setFocus();
    QCoreApplication::processEvents();
    REQUIRE(ui::ShortcutBinder::contextFor(QApplication::focusWidget()) == core::KeyContext::Viewer);

    // THE COLLISION'S CONTROL: `Space` on the page scrolls a screen and constructs nothing.
    const int before = c.view->verticalScrollBar()->value();
    const std::size_t steps = c.steps();
    REQUIRE(c.key(Qt::Key_Space));
    CHECK(c.view->verticalScrollBar()->value() > before);
    CHECK(c.steps() == steps);

    // `Ctrl+.` from the page, nothing selected: the first comment on or after the page the reader is on.
    c.view->goToPage(1);
    QCoreApplication::processEvents();
    REQUIRE(c.key(Qt::Key_Period, Qt::ControlModifier));
    CHECK(c.document->annotations()->selection() == std::vector<AnnotationId>{AnnotationId::ofObject(16)});
    // And the selection made on the page is the pane's current comment too.
    CHECK(c.pane->currentComment() == AnnotationId::ofObject(16));
    REQUIRE(c.key(Qt::Key_Comma, Qt::ControlModifier));
    CHECK(c.document->annotations()->selection() == std::vector<AnnotationId>{AnnotationId::ofObject(15)});
}

TEST_CASE("an editor in the pane keeps its own Space Delete and Esc", "[commentspane][A-70]")
{
    Commented c;
    c.open();
    c.pane->selectComment(AnnotationId::ofObject(14));
    c.pane->view()->setFocus();
    REQUIRE(c.pane->reply());
    REQUIRE(c.pane->editor() != nullptr);
    const std::size_t count = c.store().size();
    const std::size_t steps = c.steps();
    c.type(QStringLiteral("a b"));
    REQUIRE(c.key(Qt::Key_Delete));   // deletes a character, not the comment
    CHECK(c.store().size() == count);
    REQUIRE(c.key(Qt::Key_Escape));   // abandons the reply
    CHECK(c.steps() == steps);
    CHECK(c.store().replies(*c.store().findObject(14)).empty());
}

// ==========================================================================================
// A-67's pane half, A-43's listed hidden
// ==========================================================================================

TEST_CASE("the list is the store's query grouped and filtered as the controls say", "[commentspane][A-67][A-43]")
{
    Commented c;
    c.open();
    using G = core::AnnotationStore::GroupKey;

    // GROUPED BY AUTHOR: the groups are the store's, each holding what the query puts in it.
    const int at = c.pane->groupBox()->findData(static_cast<int>(G::Author));
    REQUIRE(at >= 0);
    c.pane->groupBox()->setCurrentIndex(at);
    emit c.pane->groupBox()->activated(at);
    CHECK(c.pane->groupKey() == G::Author);
    core::AnnotationStore::Filter filter;
    filter.identity = kAna;
    const auto groups = c.store().grouped(filter, G::Author, core::AnnotationStore::SortKey::Page);
    CHECK(c.pane->groupCount() == static_cast<int>(groups.size()));
    CHECK(c.pane->groupCount() == 3);
    std::vector<AnnotationId> fromStore;
    for (const auto& g : groups)
        for (const std::size_t index : g.items) fromStore.push_back(c.store().at(index).id);
    CHECK(c.pane->listedComments() == fromStore);

    // THE FILTER MENU's author entry: Ben's only.
    c.pane->setGroupKey(G::None);
    c.pane->filterMenu()->popup(QPoint(0, 0));   // populated when shown
    QCoreApplication::processEvents();
    QAction* ben = nullptr;
    for (QAction* top : c.pane->filterMenu()->actions())
        if (QMenu* sub = top->menu())
            for (QAction* action : sub->actions())
                if (action->text() == kBen) ben = action;
    REQUIRE(ben != nullptr);
    ben->trigger();
    c.pane->filterMenu()->hide();
    QCoreApplication::processEvents();
    CHECK(c.pane->listedComments() == std::vector<AnnotationId>{AnnotationId::ofObject(15)});

    // A-43: THE HIDDEN SQUARE is listed, and the list says it is hidden.
    CHECK(c.pane->textOf(AnnotationId::ofObject(15), 0).contains(ui::catalog::translate(ui::catalog::key::CommentsHidden)));
    CHECK_FALSE(c.pane->textOf(AnnotationId::ofObject(15), 0).isEmpty());
    c.pane->clearFilter();
    CHECK_FALSE(c.pane->textOf(AnnotationId::ofObject(14), 0).contains(ui::catalog::translate(ui::catalog::key::CommentsHidden)));

    // STATUS and DATE: Accepted is note 10's; the range holds the highlight alone.
    c.pane->setStatusFilter({QStringLiteral("Accepted")});
    CHECK(c.pane->listedComments() == std::vector<AnnotationId>{AnnotationId::ofObject(10)});
    c.pane->setStatusFilter({});
    c.pane->setDateRange(1790000250, 1790000350);
    CHECK(c.pane->listedComments() == std::vector<AnnotationId>{AnnotationId::ofObject(14)});
    c.pane->setDateRange(std::nullopt, std::nullopt);

    // NOTHING the view does is a command.
    CHECK(c.journal().historySize() == 0);
}

// ==========================================================================================
// A-69 -- the checkmark is the current identity's
// ==========================================================================================

TEST_CASE("the checkmark shown and set is the current identity's", "[commentspane][A-69]")
{
    Commented c;
    c.open();
    c.pane->selectComment(AnnotationId::ofObject(16));
    c.pane->view()->setFocus();
    const QModelIndex row = c.pane->view()->currentIndex().siblingAtColumn(0);

    REQUIRE(c.key(Qt::Key_Space));   // Ana's
    CHECK(c.pane->view()->model()->data(c.pane->view()->currentIndex().siblingAtColumn(0), Qt::CheckStateRole).toInt()
          == Qt::Checked);
    (void)row;

    // ANOTHER IDENTITY: Ana's checkmark is not Ben's, and his `Space` is his own.
    c.fixture.settings.set(core::keys::identity::author, kBen);
    c.pane->refresh();
    c.pane->selectComment(AnnotationId::ofObject(16));
    CHECK(c.pane->view()->model()->data(c.pane->view()->currentIndex().siblingAtColumn(0), Qt::CheckStateRole).toInt()
          == Qt::Unchecked);
    c.pane->view()->setFocus();
    REQUIRE(c.key(Qt::Key_Space));
    const std::size_t index = *c.store().findObject(16);
    CHECK(c.store().isMarkedBy(index, kBen));
    CHECK(c.store().isMarkedBy(index, kAna));
    REQUIRE(c.key(Qt::Key_Space));   // Ben takes his off; Ana's stays
    CHECK_FALSE(c.store().isMarkedBy(index, kBen));
    CHECK(c.store().isMarkedBy(index, kAna));
    // Three state replies, one undo each.
    CHECK(c.store().markedHistory(index).size() == 3);
    REQUIRE(c.journal().undo());
    CHECK(c.store().isMarkedBy(index, kBen));
}

// ==========================================================================================
// A-71 -- deleting many is one step
// ==========================================================================================

TEST_CASE("delete all and delete matching are one command each and one undo restores each", "[commentspane][A-71]")
{
    Commented c;
    c.open();
    const std::size_t count = c.store().size();

    // DELETE MATCHING: the filter shows Ana's two notes -- they go, with note 10's pop-up, reply and status.
    c.pane->setAuthorFilter({kAna});
    REQUIRE(c.pane->listedCount() == 2);
    REQUIRE(c.fixture.actions.dispatch(core::ActionId::CommentDeleteMatching, core::KeyContext::Comments));
    QCoreApplication::processEvents();
    CHECK(c.steps() == 1);
    CHECK(c.store().size() == count - 5);
    CHECK(c.store().findObject(14));   // not Ana's: kept
    REQUIRE(c.journal().undo());
    CHECK(c.store().size() == count);

    // DELETE ALL: every comment, whatever the filter shows.
    REQUIRE(c.fixture.actions.dispatch(core::ActionId::CommentDeleteAll, core::KeyContext::Comments));
    QCoreApplication::processEvents();
    CHECK(c.store().size() == 0);
    CHECK(c.journal().undoName()->key == QLatin1String("command.annot.deleteMany"));
    REQUIRE(c.journal().undo());
    CHECK(c.store().size() == count);
}

// ==========================================================================================
// A-72 -- sequence numbers are shown and never written
// ==========================================================================================

TEST_CASE("numbering shows numbers in the list and over the page and constructs nothing", "[commentspane][A-72]")
{
    Commented c;
    c.open();
    const QByteArray hash = c.journal().document().modelHash();
    REQUIRE(c.fixture.actions.dispatch(core::ActionId::CommentNumbering, core::KeyContext::Comments));
    QCoreApplication::processEvents();
    REQUIRE(c.pane->numbering());
    CHECK(c.fixture.actions.isChecked(core::ActionId::CommentNumbering));

    // In reading order: note 10 (top right) ... the highlight, the hidden square; page 2's note last.
    const std::map<AnnotationId, int>& shown = c.document->annotations()->numbers();
    CHECK(shown == std::map<AnnotationId, int>{{AnnotationId::ofObject(10), 1}, {AnnotationId::ofObject(14), 2},
                                               {AnnotationId::ofObject(15), 3}, {AnnotationId::ofObject(16), 4}});
    CHECK(c.pane->textOf(AnnotationId::ofObject(14), 0).startsWith(QStringLiteral("2")));
    CHECK(c.journal().historySize() == 0);
    CHECK(c.journal().document().modelHash() == hash);

    // A comment made while numbering is on takes its place; numbering off clears the page.
    c.pane->selectComment(AnnotationId::ofObject(16));
    REQUIRE(c.pane->setStatus(QStringLiteral("Rejected")));   // a status is not numbered
    CHECK(c.document->annotations()->numbers().size() == 4);
    REQUIRE(c.fixture.actions.dispatch(core::ActionId::CommentNumbering, core::KeyContext::Comments));
    QCoreApplication::processEvents();
    CHECK(c.document->annotations()->numbers().empty());
    CHECK_FALSE(c.pane->textOf(AnnotationId::ofObject(14), 0).startsWith(QStringLiteral("2")));
}

// ==========================================================================================
// I-13 -- a stale list is never read by a stale row
// ==========================================================================================

TEST_CASE("a selection coming back to a hidden pane after the store changed reads no stale row", "[commentspane][I-13]")
{
    Commented c;
    c.open();
    c.pane->selectComment(AnnotationId::ofObject(16));   // the LAST row's annotation is current
    // Hidden (another pane in front), then the store shrinks underneath it.
    c.window->sidebar()->showPane(core::ActionId::PaneThumbnails);
    QCoreApplication::processEvents();
    REQUIRE_FALSE(c.pane->isVisible());
    REQUIRE(c.journal().run(std::make_unique<core::DeleteAnnotationCommand>(
        std::vector<AnnotationId>{AnnotationId::ofObject(14), AnnotationId::ofObject(15)})));
    // A selection made on the page comes back to the pane: it rebuilds from the store, by identity.
    c.document->annotations()->select({AnnotationId::ofObject(10)});
    QCoreApplication::processEvents();
    CHECK(c.pane->currentComment() == AnnotationId::ofObject(10));
    CHECK(c.pane->listedComments() == std::vector<AnnotationId>{AnnotationId::ofObject(10), AnnotationId::ofObject(16)});
}

// ==========================================================================================
// I-30 -- a narrow pane scrolls; it never paints a check box over the next column
// ==========================================================================================

namespace {

// The sidebar asked to be `asked` pixels wide, laid out; how wide it is.
int dockTo(const Commented& c, int asked)
{
    c.window->resizeDocks({c.window->sidebar()}, {asked}, Qt::Horizontal);
    c.layout();
    return c.window->sidebar()->width();
}

int sectionsWidth(const QTreeView* view)
{
    int sum = 0;
    for (int s = 0; s < view->header()->count(); ++s) sum += view->header()->sectionSize(s);
    return sum;
}

} // namespace

TEST_CASE("the comment column keeps room for its deepest row and a narrow pane scrolls", "[commentspane][I-30]")
{
    Commented c;
    c.window->resize(1800, 900);
    c.open();
    // A reply to the reply: the thread three deep, as `annots-threads.pdf`'s is.
    c.pane->selectComment(AnnotationId::ofObject(12));
    c.pane->view()->setFocus();
    REQUIRE(c.pane->reply());
    c.type(QStringLiteral("deeper"));
    REQUIRE(c.key(Qt::Key_Return));
    c.layout();
    REQUIRE(c.pane->visibleRowCount() == 6);
    QTreeView* view = c.pane->view();
    QHeaderView* header = view->header();

    // THE FLOOR: the deepest row's two indents, the root's arrow, a check box and text beside it.
    const int floor = c.pane->commentColumnMinimum();
    const int box = view->style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, view);
    CHECK(floor >= view->indentation() * 3 + box + view->fontMetrics().averageCharWidth() * 10);

    // WIDE: the comment column takes what the others leave, and nothing scrolls.
    REQUIRE(dockTo(c, 900) > 700);
    CHECK(header->sectionSize(0) > floor);
    CHECK(sectionsWidth(view) == view->viewport()->width());
    CHECK(view->horizontalScrollBar()->maximum() == 0);

    // NARROW -- I-30: the pane at its minimum. Before the fix the column was squeezed under its own indentation
    // (32 pixels against 30 an indent, on the `windows` platform) and the box drawn over the author and the page.
    dockTo(c, 0);
    REQUIRE(view->viewport()->width() > 0);
    CHECK(header->sectionSize(0) == floor);
    CHECK(sectionsWidth(view) > view->viewport()->width());
    CHECK(view->horizontalScrollBar()->maximum() > 0);   // the rest is reached by scrolling

    // AND WIDE AGAIN: the floor is not where it stays.
    dockTo(c, 900);
    CHECK(sectionsWidth(view) == view->viewport()->width());
}

TEST_CASE("a comment cell narrower than its check box paints nothing beyond itself", "[commentspane][I-30]")
{
    // The guard for a thread deeper than the floor allows for: the cell is clipped to itself -- in EVERY style this
    // Qt has. The overflow is a style's: the native one draws the box at the cell's left edge whatever its width (the
    // user's screenshot, `windows11`), while `Fusion`, offscreen's default, drew nothing outside -- so a case run in
    // the default style alone cannot fail under `ctest`.
    Commented c;
    c.open();
    QTreeView* view = c.pane->view();
    const QModelIndex cell = view->model()->index(0, 0);
    REQUIRE((view->model()->flags(cell) & Qt::ItemIsUserCheckable) != 0);
    QAbstractItemDelegate* delegate = view->itemDelegateForIndex(cell);
    REQUIRE(delegate != nullptr);

    const QStringList styles = QStyleFactory::keys();
    REQUIRE_FALSE(styles.isEmpty());
    for (const QString& name : styles) {
        CAPTURE(name);
        const std::unique_ptr<QStyle> style(QStyleFactory::create(name));
        REQUIRE(style != nullptr);
        // The cell at x 20, `width` wide, painted in this style into a white image: the pixels changed from `from`.
        const auto paintedFrom = [&](int width, int from) {
            QWidget host;
            host.setStyle(style.get());
            QImage image(200, 30, QImage::Format_ARGB32);
            image.fill(Qt::white);
            {
                QPainter painter(&image);
                QStyleOptionViewItem option;
                option.initFrom(&host);
                option.widget = &host;
                option.rect = QRect(20, 0, width, 30);
                option.state |= QStyle::State_Enabled;
                delegate->paint(&painter, option, cell);
            }
            int changed = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = from; x < image.width(); ++x)
                    if (image.pixel(x, y) != QColor(Qt::white).rgb()) ++changed;
            return changed;
        };
        CHECK(paintedFrom(4, 24) == 0);    // four pixels: no room for the box, and nothing beyond them
        // NEGATIVE: what the tree view hands a row indented past its column's right edge -- the deep reply of I-30's
        // screenshot, whose box was drawn over the page column.
        CHECK(paintedFrom(-10, 0) == 0);
        // THE CONTROL: the same cell 60 pixels wide paints -- so a blank image above is the guard, not nothing drawn.
        CHECK(paintedFrom(60, 20) > 0);
    }
}

// ==========================================================================================
// I-31 -- the properties bar asks for no width, so the sidebar beside it still moves
// ==========================================================================================

TEST_CASE("Enter's properties bar leaves the sidebar free to be dragged wider", "[commentspane][I-31]")
{
    Commented c;
    c.window->resize(2600, 900);
    c.open();
    QWidget* central = c.window->centralWidget();
    const int centralMinimum = central->minimumSizeHint().width();
    const int wide = dockTo(c, 900);
    REQUIRE(wide > 700);

    // `Enter` -- the bar, its text focused.
    c.pane->selectComment(AnnotationId::ofObject(14));
    c.pane->view()->setFocus();
    REQUIRE(c.key(Qt::Key_Return));
    c.layout();
    ui::PropertiesBar* bar = c.document->propertiesBar();
    REQUIRE(c.document->propertiesBarShown());
    CHECK(QApplication::focusWidget() == bar->textField());
    CHECK_FALSE(bar->textField()->visibleRegion().isEmpty());   // focused where it can be seen

    // THE DOCUMENT AREA ASKS FOR NO MORE THAN IT DID. Before the fix the bar's row was its minimum: 2418 pixels on
    // the `windows` platform, and the sidebar could be dragged no wider than its own minimum, 287.
    CHECK(bar->minimumSizeHint().width() < 100);
    CHECK(central->minimumSizeHint().width() <= centralMinimum);
    dockTo(c, 300);
    CHECK(dockTo(c, 900) == wide);   // dragged wider again, with the bar shown

    // THE ROW STILL READS: one row tall (and the scroll bar), its text field focused and in sight.
    CHECK(bar->height() < 3 * bar->textField()->height());
    CHECK(QApplication::focusWidget() == bar->textField());
    CHECK_FALSE(bar->textField()->visibleRegion().isEmpty());   // and still, after the bar narrowed and widened
    REQUIRE(dockTo(c, 1600) > wide + 500);   // the sidebar wider than when the field took the focus: the bar narrower
    CHECK_FALSE(bar->textField()->visibleRegion().isEmpty());
    auto* scroll = bar->findChild<QScrollArea*>(QStringLiteral("propertiesScroll"));
    REQUIRE(scroll != nullptr);
    CHECK(scroll->horizontalScrollBar()->maximum() > 0);   // narrower than its row: it scrolls
}

// ==========================================================================================
// S6 guide I-32 -- a right-click menu, and every delete of comments asks first
// ==========================================================================================
// After M-05, 2026-10-22: the pane had no right-click menu, and a delete -- of one comment or of all of them -- went
// without a word. The user asked for a two-level menu (the rows, the status beneath), a delete that takes the comment
// with its replies, and a question before every delete.
// NOT COVERED (convention 19): the dialog itself -- each case answers in its place, so its wording and buttons are
// read here, never clicked; and the menu under a real pointer: the events are sent to the list's viewport.

TEST_CASE("a right click offers the comment's rows and its status and every delete asks first", "[commentspane][I-32]")
{
    Commented c;
    c.open();
    const AnnotationId note = AnnotationId::ofObject(10);
    const AnnotationId highlight = AnnotationId::ofObject(14);
    QMenu* menu = c.pane->itemMenu();
    REQUIRE(menu != nullptr);

    // A RIGHT CLICK ON A ROW makes it current -- a person's gesture, so the page follows -- and opens the menu.
    const QPoint onHighlight = c.rowOf(highlight);
    REQUIRE(c.pane->selectComment(note));
    c.rightClickAt(onHighlight);
    CHECK(c.pane->currentComment() == highlight);
    CHECK(c.overlay()->selection() == std::vector<AnnotationId>{highlight});
    REQUIRE(menu->isVisible());

    // §8's ROWS by their labels, each with its chord after a tab and never as a shortcut (convention 15); the
    // checkmark not among them -- its box is a click away.
    for (const core::ActionId id : {core::ActionId::CommentReply, core::ActionId::CommentEditInPlace,
                                    core::ActionId::CommentOpenEditor, core::ActionId::CommentDelete}) {
        INFO(ui::catalog::translateAction(id).toStdString());
        QAction* item = c.itemNamed(menu, ui::catalog::translateAction(id));
        REQUIRE(item != nullptr);
        CHECK(item->text().contains(QLatin1Char('\t')));
        CHECK(item->shortcut().isEmpty());
    }
    CHECK(c.itemNamed(menu, ui::catalog::translateAction(core::ActionId::CommentToggleCheckmark)) == nullptr);

    // SET STATUS, the second level: the five states, the comment's own ticked; one sets it, as ONE command.
    const auto states = [&c, menu]() -> QMenu* {
        QAction* item = c.itemNamed(menu, ui::catalog::translateAction(core::ActionId::CommentSetStatus));
        return item != nullptr ? item->menu() : nullptr;
    };
    const auto ticked = [&states] {
        QStringList out;
        for (QAction* state : states()->actions())
            if (state->isChecked()) out << state->data().toString();
        return out;
    };
    REQUIRE(states() != nullptr);
    CHECK(states()->actions().size() == 5);
    CHECK(ticked() == QStringList{QStringLiteral("None")});
    std::size_t steps = c.steps();
    for (QAction* state : states()->actions())
        if (state->data().toString() == QLatin1String("Accepted")) state->trigger();
    QCoreApplication::processEvents();
    CHECK(c.steps() == steps + 1);
    CHECK(c.store().reviewStatus(*c.store().findObject(14)) == QStringLiteral("Accepted"));
    menu->hide();
    c.rightClickAt(c.rowOf(highlight));
    CHECK(ticked() == QStringList{QStringLiteral("Accepted")});
    // Made afresh each time, and the submenu it replaced is gone with it.
    CHECK(menu->findChildren<QMenu*>(QString(), Qt::FindDirectChildrenOnly).size() == 1);

    // DELETE ASKS FIRST. No: nothing is deleted.
    c.answer = false;
    c.itemNamed(menu, ui::catalog::translateAction(core::ActionId::CommentDelete))->trigger();
    QCoreApplication::processEvents();
    REQUIRE_FALSE(c.questions.empty());
    CHECK(c.questions.back() == ui::catalog::translate(ui::catalog::key::CommentsConfirmDeleteOne));
    CHECK(c.store().findObject(14));
    CHECK(c.steps() == steps + 1);
    // Yes: the comment goes, as ONE step.
    c.answer = true;
    menu->hide();
    c.rightClickAt(c.rowOf(highlight));
    c.itemNamed(menu, ui::catalog::translateAction(core::ActionId::CommentDelete))->trigger();
    QCoreApplication::processEvents();
    CHECK_FALSE(c.store().findObject(14));
    CHECK(c.steps() == steps + 2);
    menu->hide();

    // A COMMENT WITH REPLIES says so -- and the KEY asks what the menu asks.
    c.answer = false;
    REQUIRE(c.pane->selectComment(note));
    c.pane->view()->setFocus();
    QCoreApplication::processEvents();
    const std::size_t asked = c.questions.size();
    c.key(Qt::Key_Delete);
    REQUIRE(c.questions.size() == asked + 1);
    CHECK(c.questions.back() == ui::catalog::translate(ui::catalog::key::CommentsConfirmDeleteThread));
    CHECK(c.store().findObject(10));
    // THE BULK DELETES ask with how many: notes 10 and 16 and the hidden square are left.
    REQUIRE(c.fixture.actions.dispatch(core::ActionId::CommentDeleteAll, core::KeyContext::Comments));
    CHECK(c.questions.back() == ui::catalog::translate(ui::catalog::key::CommentsConfirmDeleteMany).arg(3));
    REQUIRE(c.fixture.actions.dispatch(core::ActionId::CommentDeleteMatching, core::KeyContext::Comments));
    CHECK(c.questions.back() == ui::catalog::translate(ui::catalog::key::CommentsConfirmDeleteMany).arg(3));
    CHECK(c.store().findObject(10));
    // THE PAGE'S `Delete` asks too -- for what is selected, which keeps its replies.
    c.overlay()->select({note});
    c.view->setFocus();
    QCoreApplication::processEvents();
    c.key(Qt::Key_Delete);
    CHECK(c.questions.back() == ui::catalog::translate(ui::catalog::key::CommentsConfirmDeleteOne));
    CHECK(c.store().findObject(10));
    CHECK(c.steps() == steps + 2);   // four questions answered No: nothing more

    // NO ROW, NO MENU.
    CHECK_FALSE(c.pane->showItemMenu(QPoint(5, c.pane->view()->viewport()->height() - 2)));
    CHECK_FALSE(menu->isVisible());

    // CONVENTION 16: with nobody to ask and no answer installed, every delete is DISABLED -- and, the control, each
    // is enabled while an answer is.
    REQUIRE(c.pane->selectComment(note));
    c.overlay()->select({note});
    const core::ActionId deletes[] = {core::ActionId::CommentDelete, core::ActionId::CommentDeleteAll,
                                      core::ActionId::CommentDeleteMatching, core::ActionId::AnnotDeleteSelected};
    for (const core::ActionId id : deletes) CHECK(c.fixture.actions.isEnabled(id));
    c.window->setDeleteAnswerForTesting({});
    for (const core::ActionId id : deletes) CHECK_FALSE(c.fixture.actions.isEnabled(id));
}

// ==========================================================================================
// S6 guide I-33 -- a picture of its kind at the head of each row
// ==========================================================================================
// After M-05, 2026-10-22: a row said its kind as a word before its text. The user asked for a picture after the check
// box -- a note, a highlight, an insertion and the rest told apart -- in the comment's own colour.
// NOT COVERED (convention 19): where the style puts the picture -- Qt's `QStyledItemDelegate` lays out the check box,
// the decoration and the text -- and the pictures' look, which the user judges.

TEST_CASE("each comment's row starts with a picture of its kind and says its text alone", "[commentspane][I-33]")
{
    Commented c;
    c.open();
    const auto glyphOf = [&c](long long object) { return ui::glyphFor(c.store(), *c.store().findObject(object)); };
    CHECK(glyphOf(10) == ui::CommentGlyph::Note);
    CHECK(glyphOf(12) == ui::CommentGlyph::Note);   // a reply is a note
    CHECK(glyphOf(14) == ui::CommentGlyph::Highlight);
    CHECK(glyphOf(15) == ui::CommentGlyph::Drawing);

    // THE ROW'S PICTURE is the comment column's decoration, and no other column's.
    for (const long long object : {10LL, 12LL, 14LL, 15LL, 16LL}) {
        INFO("object " << object);
        REQUIRE(c.pane->selectComment(AnnotationId::ofObject(object)));
        const QModelIndex row = c.pane->view()->currentIndex();
        const QIcon icon = row.siblingAtColumn(0).data(Qt::DecorationRole).value<QIcon>();
        CHECK_FALSE(icon.isNull());
        CHECK_FALSE(icon.pixmap(QSize(16, 16)).isNull());
        CHECK_FALSE(row.siblingAtColumn(1).data(Qt::DecorationRole).isValid());
    }

    // EACH KIND ITS OWN PICTURE, and the comment's colour in it: twelve pictures, all different, and a highlight
    // drawn yellow is not the same highlight drawn blue.
    const auto hashOf = [&c](ui::CommentGlyph glyph, QColor fill) {
        const QImage image = ui::commentIcon(glyph, fill, c.pane->view()).pixmap(QSize(16, 16)).toImage();
        return QCryptographicHash::hash(QByteArrayView(image.constBits(), image.sizeInBytes()), QCryptographicHash::Sha256).toHex();
    };
    std::set<QByteArray> pictures;
    for (int g = 0; g <= static_cast<int>(ui::CommentGlyph::Other); ++g)
        pictures.insert(hashOf(static_cast<ui::CommentGlyph>(g), QColor(255, 255, 0)));
    CHECK(pictures.size() == static_cast<std::size_t>(ui::CommentGlyph::Other) + 1);
    CHECK(hashOf(ui::CommentGlyph::Highlight, QColor(255, 255, 0)) != hashOf(ui::CommentGlyph::Highlight, QColor(0, 0, 255)));
    CHECK(ui::colourOf({1, 1, 0}) == QColor(255, 255, 0));
    CHECK_FALSE(ui::colourOf({}).isValid());

    // THE TEXT ALONE in the row; the kind's word in its tooltip.
    CHECK(c.pane->textOf(AnnotationId::ofObject(14), 0) == QStringLiteral("Source for this number?"));
    REQUIRE(c.pane->selectComment(AnnotationId::ofObject(14)));
    const QString tip = c.pane->view()->currentIndex().siblingAtColumn(0).data(Qt::ToolTipRole).toString();
    CHECK(tip.contains(ui::catalog::translate(QStringLiteral("comments.type.Highlight"))));
    CHECK(tip.contains(QStringLiteral("Source for this number?")));

    // NO TEXT: `(Empty)`.
    Annotation emptied = c.at(16);
    emptied.contents.clear();
    REQUIRE(c.journal().run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{emptied})));
    QCoreApplication::processEvents();
    CHECK(c.pane->textOf(AnnotationId::ofObject(16), 0) == ui::catalog::translate(ui::catalog::key::CommentsEmptyText));

    // AN INSERTION, and a REPLACEMENT -- the caret holding a strikeout as its group member (D6-12).
    Annotation caret = core::newAnnotation(AnnotationSubtype::Caret, 0, kAna, 1791763200);
    caret.rect = box(300, 500, 312, 512);
    caret.contents = QStringLiteral("new words");
    REQUIRE(c.journal().run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{caret})));
    CHECK(ui::glyphFor(c.store(), *c.store().find(caret.id)) == ui::CommentGlyph::Insert);
    Annotation struck = core::newAnnotation(AnnotationSubtype::StrikeOut, 0, kAna, 1791763200);
    struck.rect = box(200, 500, 300, 512);
    struck.quadPoints = {200, 512, 300, 512, 200, 500, 300, 500};
    struck.inReplyTo = caret.id;
    struck.replyType = core::ReplyType::Group;
    REQUIRE(c.journal().run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{struck})));
    CHECK(ui::glyphFor(c.store(), *c.store().find(caret.id)) == ui::CommentGlyph::Replace);
}

// ==========================================================================================
// S6 guide I-34 -- a click on a row opens the comment's pop-up on the page
// ==========================================================================================
// After M-05, 2026-10-22: a click on a row selected the comment on the page and opened nothing. The user asked for the
// click to open its pop-up while the list keeps the focus -- a click, not an arrow -- and for a press or the focus
// anywhere else (the list's blank space, the page, a toolbar) to close it.
// NOT COVERED (convention 19): the paint (`isPopupShown` is what `paint` reads); and a press under a real pointer --
// the events are sent to the widget, so a press on the ribbon is represented by one on the pane's search field, a
// widget of the window that is neither the page nor a row.

TEST_CASE("a click on a row opens its pop-up on the page and a press anywhere else closes it", "[commentspane][I-34]")
{
    Commented c;
    c.open();
    ui::AnnotationOverlay* overlay = c.overlay();
    const AnnotationId note = AnnotationId::ofObject(10);
    const AnnotationId reply = AnnotationId::ofObject(12);
    const AnnotationId highlight = AnnotationId::ofObject(14);
    const std::size_t steps = c.steps();
    const auto opened = [overlay] { return overlay->openedPopup(); };
    const QPoint blank(5, c.pane->view()->viewport()->height() - 2);

    // A CLICK opens it on the page -- whatever the preference -- and the list keeps the focus.
    overlay->setPopupBehaviour(ui::AnnotationOverlay::PopupBehaviour::Never);
    c.pane->view()->setFocus();
    c.clickAt(c.rowOf(highlight));
    CHECK(c.pane->currentComment() == highlight);
    CHECK(overlay->selection() == std::vector<AnnotationId>{highlight});
    CHECK(opened() == std::optional<AnnotationId>(highlight));
    CHECK(overlay->isPopupShown(highlight));
    CHECK(QApplication::focusWidget() == c.pane->view());
    // A reply's row opens its thread's.
    c.clickAt(c.rowOf(reply));
    CHECK(opened() == std::optional<AnnotationId>(note));

    // AN ARROW moves the page and opens nothing; the one open goes with the comment it was opened on.
    REQUIRE(c.key(Qt::Key_Down));
    CHECK(c.pane->currentComment() == highlight);
    CHECK(overlay->selection() == std::vector<AnnotationId>{highlight});
    CHECK_FALSE(opened());

    // THE LIST'S BLANK SPACE is somewhere else.
    c.clickAt(c.rowOf(highlight));
    REQUIRE(opened());
    REQUIRE_FALSE(c.pane->view()->indexAt(blank).isValid());
    c.clickAt(blank);
    CHECK_FALSE(opened());
    // A RIGHT CLICK ON ITS ROW keeps it: the menu acts on that comment.
    c.clickAt(c.rowOf(highlight));
    c.rightClickAt(c.rowOf(highlight));
    CHECK(opened() == std::optional<AnnotationId>(highlight));
    c.pane->itemMenu()->hide();

    // ANOTHER WIDGET OF THE WINDOW: a press on it, and the focus moved to it.
    c.clickAt(QPoint(5, 5), c.pane->searchField());
    CHECK_FALSE(opened());
    c.pane->view()->setFocus();
    c.clickAt(c.rowOf(highlight));
    REQUIRE(opened());
    c.pane->searchField()->setFocus();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    CHECK_FALSE(opened());

    // THE PAGE: a press where nothing is.
    c.pane->view()->setFocus();
    c.clickAt(c.rowOf(highlight));
    REQUIRE(opened());
    const QPoint emptyPage(10, 10);
    REQUIRE_FALSE(overlay->annotationAt(c.view->annotationViewport(), emptyPage));
    c.clickAt(emptyPage, c.view->viewport());
    CHECK_FALSE(opened());

    // A COMMENT WITH NO TEXT opens none -- and the control: given text, it does.
    Annotation emptied = c.at(16);
    emptied.contents.clear();
    REQUIRE(c.journal().run(std::make_unique<core::EditAnnotationCommand>(std::vector<Annotation>{emptied})));
    QCoreApplication::processEvents();
    c.pane->view()->setFocus();
    c.clickAt(c.rowOf(AnnotationId::ofObject(16)));
    CHECK(c.pane->currentComment() == AnnotationId::ofObject(16));
    CHECK_FALSE(opened());
    c.clickAt(c.rowOf(note));
    CHECK(opened() == std::optional<AnnotationId>(note));

    // VIEW STATE: the one command is the case's own edit.
    CHECK(c.steps() == steps + 1);
}
