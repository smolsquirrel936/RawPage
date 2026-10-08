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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QTreeView>

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

    Commented()
    {
        fixture.backend.pagesPerDocument = 2;
        fixture.backend.editable = true;
        fixture.backend.annotations_ = threadedStore();
        fixture.settings.set(core::keys::identity::author, kAna);
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1200, 900);
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
