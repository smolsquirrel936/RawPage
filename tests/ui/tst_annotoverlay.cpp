// tst_annotoverlay -- the annotation overlay, from mouse and key events. Stage 6 T5, guide cases A-42 …
// A-47 and A-39's last clause (`Stage6-Annotations-Testing-Guide.md` §5.6, §5.7).
//
// WHAT THIS SUITE IS TRYING TO CATCH: a drag that is two commands, or none; a gesture `Esc` abandoned that
// still changed the document; a locked annotation that moves; a burst of nudges that is ten undo steps; a
// properties bar that edits the wrong thing or nothing; an `Esc` that a widget owning it never sees; a
// pop-up that is a command; a refusal nobody is told about.
//
// WHAT IT DOES NOT COVER (convention 19):
//   * the BYTES and the PIXELS. The backend is `tests/ui/FakeBackend.h`, whose sink accepts every edit and
//     records it; that the sink writes what the command says is `tests/unit/tst_annotedit.cpp`'s, and that
//     the screen then shows what Save writes is `tests/render/tst_annotdisplay.cpp`'s;
//   * WHERE AN ANNOTATION IS DRAWN. The fake draws nothing, so a click here is aimed at the annotation's
//     MODEL geometry; that the same click lands on its ink is `tests/composition/tst_annotink.cpp`'s (A-41);
//   * the gesture under a real pointer: events are sent to the viewport, below Windows' input queue.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/AnnotationGeometry.h"
#include "core/doc/Document.h"
#include "tests/ui/ShellFixture.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/StatusBar.h"
#include "ui/shell/ToolFinder.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/PropertiesBar.h"
#include "ui/viewer/TilePresenter.h"
#include "ui/viewer/ToolDefaults.h"
#include "ui/write/FindReplaceBar.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSpinBox>

#include <cmath>

using namespace rawpage;
using core::Annotation;
using core::AnnotationId;
using core::AnnotationSubtype;
namespace AnnotationFlag = core::AnnotationFlag;

namespace {

core::PdfRect box(double l, double b, double r, double t)
{
    return core::PdfRect{core::PdfPoint{l, b}, core::PdfSize{r - l, t - b}};
}

// Four annotations on page 1 of a three-page fake document, read as the reader would hand them over:
// a rectangle, a locked rectangle beside it, a highlight over a line of text, and a note -- and, `withCaret`,
// a fifth: a caret holding inserted words (S6 guide I-26), kept out of the four every other case counts on.
core::AnnotationStore annotatedStore(bool withCaret = false)
{
    core::AnnotationStore::Reading reading;
    reading.pages.resize(3);
    const auto add = [&reading](Annotation a) {
        a.page = 0;
        core::AnnotationListingEntry entry;
        entry.fact = core::AnnotationFact{true, a.subtype, a.rect};
        entry.annotation = reading.annotations.size();
        reading.pages[0].push_back(entry);
        reading.annotations.push_back(std::move(a));
    };
    Annotation square;
    square.id = AnnotationId::ofObject(10);
    square.subtype = AnnotationSubtype::Square;
    square.rect = box(100, 600, 250, 700);
    square.borderWidth = 2;
    square.color = {1, 0, 0};
    square.hasAppearance = true;
    square.author = QStringLiteral("Ana");
    add(square);

    Annotation locked = square;
    locked.id = AnnotationId::ofObject(11);
    locked.rect = box(300, 600, 450, 700);
    locked.flags = AnnotationFlag::Print | AnnotationFlag::Locked;
    add(locked);

    Annotation highlight;
    highlight.id = AnnotationId::ofObject(12);
    highlight.subtype = AnnotationSubtype::Highlight;
    highlight.quadPoints = {100, 520, 300, 520, 100, 500, 300, 500};
    highlight.rect = box(100, 500, 300, 520);
    highlight.color = {1, 1, 0};
    highlight.hasAppearance = true;
    add(highlight);

    Annotation note;
    note.id = AnnotationId::ofObject(13);
    note.subtype = AnnotationSubtype::Text;
    note.rect = box(100, 400, 120, 420);
    note.author = QStringLiteral("Zoë");
    note.contents = QStringLiteral("Check this figure");
    note.hasAppearance = true;
    add(note);

    if (withCaret) {
        Annotation caret;
        caret.id = AnnotationId::ofObject(14);
        caret.subtype = AnnotationSubtype::Caret;
        caret.rect = box(400, 400, 412, 412);
        caret.author = QStringLiteral("Ana");
        caret.contents = QStringLiteral("inserted words");
        caret.color = {0, 0, 1};
        caret.hasAppearance = true;
        add(caret);
    }
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

void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

struct Annotated {
    Fixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow* document = nullptr;
    ui::PageView* view = nullptr;
    ui::AnnotationOverlay* overlay = nullptr;

    explicit Annotated(bool withCaret = false)
    {
        fixture.backend.pagesPerDocument = 3;
        fixture.backend.editable = true;
        fixture.backend.annotations_ = annotatedStore(withCaret);
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 900);
        window->openFiles({fixture.writeFile(QStringLiteral("annotated.pdf"))});
        driveLayout(window.get());
        document = window->currentDocument();
        REQUIRE(document != nullptr);
        view = window->currentView();
        REQUIRE(view != nullptr);
        REQUIRE(view->width() > 300);   // the geometry first (convention 12)
        overlay = document->annotations();
        REQUIRE(overlay != nullptr);
        REQUIRE(overlay->isActive());
        overlay->setClock([] { return std::int64_t{1791590400}; });
        window->raiseToForeground();
        view->setFocus();
        QCoreApplication::processEvents();
    }

    core::CommandJournal& journal() const { return *document->journal(); }
    const core::AnnotationStore& store() const { return journal().document().annotations(); }
    const Annotation& at(long long object) const
    {
        const std::optional<std::size_t> index = store().findObject(object);
        REQUIRE(index);
        return store().at(*index);
    }
    QRectF rectOf(long long object) const
    {
        const std::optional<QRectF> r =
            ui::AnnotationOverlay::widgetRectOf(view->annotationViewport(), 0, at(object).rect);
        REQUIRE(r);
        return *r;
    }
    // On a rectangle's left border, half way up: where a SQUARE is drawn.
    QPoint onBorderOf(long long object) const
    {
        const QRectF r = rectOf(object);
        return QPoint(qRound(r.left() + 1), qRound(r.center().y()));
    }
    // Page units per logical pixel at this view's scale, along x.
    double pointsPerPixel() const
    {
        return 1.0 / (view->annotationViewport().layout->presentedScale() / view->devicePixelRatioF());
    }

    void mouse(QEvent::Type type, QPoint pos, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        QMouseEvent event(type, QPointF(pos), QPointF(view->viewport()->mapToGlobal(pos)), Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QCoreApplication::processEvents();
    }
    // A move with NO button down -- a hover, through the page's own `mouseMoveEvent` (`mouse` holds the left button).
    void hoverAt(QPoint pos) const
    {
        QMouseEvent event(QEvent::MouseMove, QPointF(pos), QPointF(view->viewport()->mapToGlobal(pos)), Qt::NoButton,
                          Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view->viewport(), &event);
        QCoreApplication::processEvents();
    }
    void drag(QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        mouse(QEvent::MouseButtonPress, from, modifiers);
        mouse(QEvent::MouseMove, QPoint((from.x() + to.x()) / 2, (from.y() + to.y()) / 2), modifiers);
        mouse(QEvent::MouseMove, to, modifiers);
        mouse(QEvent::MouseButtonRelease, to, modifiers);
    }
    void click(QPoint at, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        mouse(QEvent::MouseButtonPress, at, modifiers);
        mouse(QEvent::MouseButtonRelease, at, modifiers);
    }
    // A key as a person presses one: to the focused widget, through the application -- where the window's
    // `ShortcutBinder` sees it first, as every chord in the product is seen.
    bool key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, QWidget* to = nullptr) const
    {
        QWidget* focus = to != nullptr ? to : QApplication::focusWidget();
        if (focus == nullptr) focus = view;
        QKeyEvent override(QEvent::ShortcutOverride, key, modifiers);
        override.ignore();
        QCoreApplication::sendEvent(focus, &override);
        QKeyEvent event(QEvent::KeyPress, key, modifiers);
        event.ignore();
        QCoreApplication::sendEvent(focus, &event);
        QCoreApplication::processEvents();
        return event.isAccepted();
    }
    void wait(int milliseconds) const
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < milliseconds) QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    int steps() const
    {
        int count = 0;
        core::CommandJournal& j = journal();
        while (j.canUndo()) {
            REQUIRE(j.undo());
            ++count;
        }
        for (int i = 0; i < count; ++i) REQUIRE(j.redo());
        return count;
    }
};

} // namespace

// ==========================================================================================
// A-42 -- one gesture, one command
// ==========================================================================================

TEST_CASE("a drag moves the annotation by one command and one undo puts it back", "[annotoverlay][A-42]")
{
    Annotated a;
    const core::PdfRect before = a.at(10).rect;
    const QPoint from = a.onBorderOf(10);
    a.drag(from, from + QPoint(40, 0));

    CHECK(a.overlay->selection() == std::vector<AnnotationId>{AnnotationId::ofObject(10)});
    CHECK(a.steps() == 1);   // ONE command, however many moves the drag reported
    REQUIRE(a.journal().undoName());
    CHECK(a.journal().undoName()->key == QLatin1String("command.annot.move"));
    const core::PdfRect after = a.at(10).rect;
    CHECK(std::abs(after.left() - (before.left() + 40 * a.pointsPerPixel())) < 0.5);
    CHECK(after.bottom() == before.bottom());
    CHECK(a.fixture.backend.sinkFor(a.document->handle())->annotationEdits >= 1);

    REQUIRE(a.journal().undo());
    CHECK(a.at(10).rect == before);
}

TEST_CASE("a Shift drag keeps to eight directions and a Shift resize keeps the proportions", "[annotoverlay][A-42]")
{
    Annotated a;
    const core::PdfRect before = a.at(10).rect;
    const QPoint from = a.onBorderOf(10);
    // Mostly right and a little down: constrained, it is straight right.
    a.drag(from, from + QPoint(60, 7), Qt::ShiftModifier);
    CHECK(a.at(10).rect.bottom() == before.bottom());
    CHECK(a.at(10).rect.left() > before.left());
    REQUIRE(a.journal().undo());

    // The bottom-right handle, dragged out unevenly with Shift: the box grows by one factor both ways.
    a.click(from);
    REQUIRE(a.overlay->hasSelection());
    const QRectF r = a.rectOf(10);
    const QPoint handle(qRound(r.right()), qRound(r.bottom()));
    a.drag(handle, handle + QPoint(30, 4), Qt::ShiftModifier);
    CHECK(a.steps() == 1);
    REQUIRE(a.journal().undoName());
    CHECK(a.journal().undoName()->key == QLatin1String("command.annot.edit"));   // a resize is an edit
    const core::PdfRect resized = a.at(10).rect;
    const double sx = resized.size.width / before.size.width;
    const double sy = resized.size.height / before.size.height;
    CHECK(sx > 1.0);
    CHECK(std::abs(sx - sy) < 0.02);
    // The top-left corner (in PDF space: left and top) is where it was: the drag moved the opposite corner.
    CHECK(std::abs(resized.left() - before.left()) < 0.5);
    CHECK(std::abs(resized.top() - before.top()) < 0.5);
}

TEST_CASE("an Alt drag duplicates as one command and Esc during a drag changes nothing", "[annotoverlay][A-42]")
{
    Annotated a;
    const std::size_t count = a.store().size();
    const QPoint from = a.onBorderOf(10);
    a.drag(from, from + QPoint(0, 60), Qt::AltModifier);
    CHECK(a.store().size() == count + 1);
    CHECK(a.steps() == 1);
    CHECK(a.journal().undoName()->key == QLatin1String("command.annot.add"));
    CHECK(a.at(10).rect == annotatedStore().at(0).rect);   // the original did not move
    // The copy is a new annotation (D6-4): its own identity, the original's look, moved.
    const Annotation& copy = a.store().at(a.store().size() - 1);
    CHECK(copy.id.kind == AnnotationId::Kind::Created);
    CHECK(copy.subtype == AnnotationSubtype::Square);
    CHECK(copy.color == a.at(10).color);
    CHECK(copy.rect.bottom() < a.at(10).rect.bottom());
    REQUIRE(a.journal().undo());
    CHECK(a.store().size() == count);

    // ESC MID-DRAG: abandoned -- nothing constructed, nothing changed, and the journal as it was.
    const QByteArray hash = a.journal().document().modelHash();
    a.mouse(QEvent::MouseButtonPress, from);
    a.mouse(QEvent::MouseMove, from + QPoint(50, 50));
    CHECK(a.overlay->isDragging());
    CHECK(a.key(Qt::Key_Escape, Qt::NoModifier, a.view));
    CHECK_FALSE(a.overlay->isDragging());
    a.mouse(QEvent::MouseButtonRelease, from + QPoint(50, 50));
    CHECK(a.journal().document().modelHash() == hash);
    CHECK(a.steps() == 0);
}

TEST_CASE("text markup is selected by a click and a drag across it is the tool's", "[annotoverlay][D6-30]")
{
    Annotated a;
    const QRectF r = a.rectOf(12);
    const QPoint inside(qRound(r.center().x()), qRound(r.center().y()));
    a.click(inside);
    CHECK(a.overlay->selection() == std::vector<AnnotationId>{AnnotationId::ofObject(12)});
    a.key(Qt::Key_Escape, Qt::NoModifier, a.view);
    CHECK_FALSE(a.overlay->hasSelection());
    // A drag over it moves nothing: the words are not the annotation's to carry away.
    a.drag(inside, inside + QPoint(80, 0));
    CHECK(a.steps() == 0);
    CHECK_FALSE(a.overlay->hasSelection());
}

// ==========================================================================================
// A-43 -- Delete, and the flags
// ==========================================================================================

TEST_CASE("Delete removes the selection as one step and a locked annotation is neither moved nor deleted",
          "[annotoverlay][A-43][D6-27]")
{
    Annotated a;
    const std::size_t count = a.store().size();
    a.click(a.onBorderOf(10));
    REQUIRE(a.overlay->hasSelection());
    // Selected: the page is in Annotation tools, where `Delete` is the selected annotation.
    CHECK(ui::ShortcutBinder::declaredContext(a.view) == core::KeyContext::AnnotationTools);
    CHECK(a.key(Qt::Key_Delete));
    CHECK(a.store().size() == count - 1);
    CHECK_FALSE(a.overlay->hasSelection());
    CHECK(ui::ShortcutBinder::declaredContext(a.view) == core::KeyContext::Viewer);
    REQUIRE(a.journal().undo());
    CHECK(a.store().size() == count);

    // LOCKED: selected, not picked up -- a drag moves nothing and constructs nothing.
    const core::PdfRect lockedAt = a.at(11).rect;
    const QPoint locked = a.onBorderOf(11);
    a.drag(locked, locked + QPoint(40, 40));
    CHECK(a.overlay->selection() == std::vector<AnnotationId>{AnnotationId::ofObject(11)});
    CHECK(a.at(11).rect == lockedAt);
    const int stepsBefore = a.steps();
    // And `Delete` is REFUSED by key, and the status bar says why (A-39's clause, through a lock).
    a.key(Qt::Key_Delete);
    CHECK(a.steps() == stepsBefore);
    CHECK(a.store().size() == count);
    REQUIRE(a.window->statusBar() != nullptr);
    CHECK(a.window->statusBar()->message()->text().contains(QStringLiteral("locked")));

    // THE LOCK COMES OFF BY COMMAND -- the properties bar's box -- and is one undo step.
    a.document->togglePropertiesBar();
    ui::PropertiesBar* bar = a.document->propertiesBar();
    REQUIRE(bar->subject() == ui::PropertiesBar::Subject::Selection);
    REQUIRE(bar->lockedBox()->isChecked());
    bar->lockedBox()->setChecked(false);
    CHECK_FALSE(a.at(11).hasFlag(AnnotationFlag::Locked));
    CHECK(a.journal().undoName()->key == QLatin1String("command.annot.lock"));
    a.drag(locked, locked + QPoint(40, 0));
    CHECK(a.at(11).rect.left() > lockedAt.left());
    REQUIRE(a.journal().undo());
    REQUIRE(a.journal().undo());
    CHECK(a.at(11).hasFlag(AnnotationFlag::Locked));
    CHECK(a.at(11).rect == lockedAt);
}

// ==========================================================================================
// A-44 -- a burst of nudges is one step
// ==========================================================================================

TEST_CASE("a burst of arrows is one move and arrows with nothing selected scroll", "[annotoverlay][A-44]")
{
    Annotated a;
    // Nothing selected: an arrow scrolls the page as before.
    a.view->setZoom(core::render::Zoom{4.0});
    const core::DevicePoint scrolled = a.view->controller().scroll();
    CHECK(a.key(Qt::Key_Right, Qt::NoModifier, a.view));
    CHECK(a.view->controller().scroll().x > scrolled.x);
    a.view->setZoom(core::render::Zoom{1.0});
    QCoreApplication::processEvents();

    const core::PdfRect before = a.at(10).rect;
    a.click(a.onBorderOf(10));
    REQUIRE(a.overlay->hasSelection());
    for (int i = 0; i < 10; ++i) CHECK(a.key(Qt::Key_Right, Qt::NoModifier, a.view));
    CHECK(a.steps() == 0);   // still a burst: nothing committed yet, and the preview shows where
    CHECK(a.overlay->hasPendingNudge());
    a.wait(ui::AnnotationOverlay::kNudgeSettleMs + 100);
    CHECK(a.steps() == 1);
    for (int i = 0; i < 3; ++i) CHECK(a.key(Qt::Key_Up, Qt::ControlModifier, a.view));
    a.wait(ui::AnnotationOverlay::kNudgeSettleMs + 100);
    CHECK(a.steps() == 2);

    // The plan's steps (D6-31): a point a plain arrow, a tenth with Ctrl -- up on screen is up the page.
    const core::PdfRect after = a.at(10).rect;
    CHECK(std::abs(after.left() - (before.left() + 10 * ui::AnnotationOverlay::kNudgePoints)) < 1e-6);
    CHECK(std::abs(after.bottom() - (before.bottom() + 3 * ui::AnnotationOverlay::kFineNudgePoints)) < 1e-6);
}

// ==========================================================================================
// A-45 -- Ctrl+E opens one properties bar from either context
// ==========================================================================================

TEST_CASE("Ctrl+E toggles one bar that shows the tool's defaults then the selection and changes it by one command",
          "[annotoverlay][A-45][D6-9]")
{
    Annotated a;
    ui::PropertiesBar* bar = a.document->propertiesBar();
    REQUIRE(bar != nullptr);
    CHECK_FALSE(a.document->propertiesBarShown());

    // From the Viewer, nothing armed and nothing selected.
    REQUIRE(ui::ShortcutBinder::declaredContext(a.view) == core::KeyContext::Viewer);
    CHECK(a.key(Qt::Key_E, Qt::ControlModifier));
    CHECK(a.document->propertiesBarShown());
    CHECK(bar->subject() == ui::PropertiesBar::Subject::Nothing);

    // A tool armed: Annotation tools, and the bar shows its defaults.
    CHECK(a.key(Qt::Key_H, Qt::ControlModifier | Qt::AltModifier));
    REQUIRE(a.view->tools().armedAnnotation() == ui::AnnotationToolKind::Highlight);
    CHECK(ui::ShortcutBinder::declaredContext(a.view) == core::KeyContext::AnnotationTools);
    CHECK(bar->subject() == ui::PropertiesBar::Subject::ToolDefaults);
    // Changing a tool's property is the session's and constructs nothing (since T8 a session CHANGE over the
    // settings family, `ui/viewer/ToolDefaults`).
    bar->opacityBox()->setValue(40);
    REQUIRE(ui::sessionToolChange(ui::AnnotationToolKind::Highlight));
    CHECK(ui::sessionToolChange(ui::AnnotationToolKind::Highlight)->opacity == 0.4);
    CHECK(a.steps() == 0);
    ui::clearSessionToolChanges();   // the session is shared

    // A selection: the bar shows it, and a colour is ONE `SetAnnotationAppearance`.
    a.click(a.onBorderOf(10));
    REQUIRE(a.overlay->hasSelection());
    CHECK(bar->subject() == ui::PropertiesBar::Subject::Selection);
    bar->colourBox()->setCurrentIndex(6);   // blue
    CHECK(a.steps() == 1);
    CHECK(a.journal().undoName()->key == QLatin1String("command.annot.setAppearance"));
    CHECK(a.at(10).color == std::vector<double>{0.0, 0.0, 1.0});

    // ITS TEXT, by `Enter` in the field: ONE `EditAnnotation`.
    bar->textField()->setFocus();
    bar->textField()->setText(QStringLiteral("Mind the margin"));
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(bar->textField(), &enter);
    CHECK(a.at(10).contents == QStringLiteral("Mind the margin"));
    CHECK(a.journal().undoName()->key == QLatin1String("command.annot.edit"));

    // `Ctrl+E` again, now in Annotation tools (`annot.properties`): the SAME bar goes.
    a.view->setFocus();
    REQUIRE(ui::ShortcutBinder::declaredContext(a.view) == core::KeyContext::AnnotationTools);
    CHECK(a.key(Qt::Key_E, Qt::ControlModifier));
    CHECK_FALSE(a.document->propertiesBarShown());
}

// ==========================================================================================
// A-46 -- Esc returns to the Selection tool, and every other Esc reaches its widget
// ==========================================================================================

TEST_CASE("Esc puts an annotation tool away and every widget that owns Esc keeps it", "[annotoverlay][A-46][D6-9]")
{
    Annotated a;
    CHECK(a.key(Qt::Key_R, Qt::ControlModifier | Qt::AltModifier));
    REQUIRE(a.view->tools().armed() == ui::ToolKind::Annotate);
    CHECK(a.key(Qt::Key_Escape, Qt::NoModifier, a.view));
    CHECK(a.view->tools().armed() == ui::ToolKind::Select);
    CHECK(ui::ShortcutBinder::declaredContext(a.view) == core::KeyContext::Viewer);
    // `edit.cancel` is STILL not registered (Stage 4's I-06): nothing in the shell consumed the key above.
    CHECK_FALSE(a.fixture.actions.isRegistered(core::ActionId::EditCancel));

    // The properties bar's text field: its `Esc` puts its text back, and is its own.
    a.click(a.onBorderOf(10));
    a.document->togglePropertiesBar();
    QLineEdit* field = a.document->propertiesBar()->textField();
    field->setFocus();
    field->setText(QStringLiteral("typed, then thought better of"));
    CHECK(a.key(Qt::Key_Escape, Qt::NoModifier, field));
    CHECK(field->text().isEmpty());
    CHECK(a.overlay->hasSelection());   // the page's Esc did not run: the selection is still there
    CHECK(a.steps() == 0);

    // The tool finder's: its result list goes, its query stays, and the annotation stays selected.
    ui::ToolFinder* finder = a.window->toolFinder();
    REQUIRE(finder != nullptr);
    finder->focusSearchBox();
    finder->searchBox()->setText(QStringLiteral("zoom"));
    QCoreApplication::processEvents();
    REQUIRE(finder->resultsVisible());
    a.key(Qt::Key_Escape, Qt::NoModifier, finder->searchBox());
    CHECK_FALSE(finder->resultsVisible());
    CHECK(finder->searchBox()->text() == QStringLiteral("zoom"));
    CHECK(a.overlay->hasSelection());

    // The find bar's: it closes the bar, and the annotation stays selected.
    ui::FindReplaceBar* find = a.document->findBar();
    find->open();
    QCoreApplication::processEvents();
    REQUIRE(find->isOpen());
    QWidget* findFocus = QApplication::focusWidget();
    REQUIRE(findFocus != nullptr);
    a.key(Qt::Key_Escape, Qt::NoModifier, findFocus);
    CHECK_FALSE(find->isOpen());
    CHECK(a.overlay->hasSelection());
}

// ==========================================================================================
// A-47 -- pop-ups are never a tile and never a command
// ==========================================================================================

TEST_CASE("a note's pop-up opens moves and closes as view state under each behaviour", "[annotoverlay][A-47][D6-28]")
{
    Annotated a;
    const QByteArray hash = a.journal().document().modelHash();
    const std::vector<core::render::RenderRequest> tilesBefore = a.view->tiles().heldRequests();
    const QRectF note = a.rectOf(13);
    const QPoint onNote(qRound(note.center().x()), qRound(note.center().y()));

    // Never: nothing shows by itself, not on hover.
    a.overlay->setPopupBehaviour(ui::AnnotationOverlay::PopupBehaviour::Never);
    a.mouse(QEvent::MouseMove, onNote);
    CHECK_FALSE(a.overlay->isPopupShown(AnnotationId::ofObject(13)));
    // On hover: it shows while the pointer is over the note.
    a.overlay->setPopupBehaviour(ui::AnnotationOverlay::PopupBehaviour::OnHover);
    a.overlay->hover(a.view->annotationViewport(), onNote);
    CHECK(a.overlay->isPopupShown(AnnotationId::ofObject(13)));
    a.overlay->hover(a.view->annotationViewport(), std::nullopt);
    CHECK_FALSE(a.overlay->isPopupShown(AnnotationId::ofObject(13)));
    // Always: every note's.
    a.overlay->setPopupBehaviour(ui::AnnotationOverlay::PopupBehaviour::Always);
    CHECK(a.overlay->isPopupShown(AnnotationId::ofObject(13)));
    CHECK_FALSE(a.overlay->isPopupShown(AnnotationId::ofObject(10)));   // a rectangle has none

    // A double click pins it under any behaviour; dragging its title moves it; its cross closes it.
    a.overlay->setPopupBehaviour(ui::AnnotationOverlay::PopupBehaviour::Never);
    a.mouse(QEvent::MouseButtonPress, onNote);
    a.mouse(QEvent::MouseButtonRelease, onNote);
    a.mouse(QEvent::MouseButtonDblClick, onNote);
    a.mouse(QEvent::MouseButtonRelease, onNote);
    REQUIRE(a.overlay->isPopupShown(AnnotationId::ofObject(13)));
    const QRectF shown = *a.overlay->popupRect(a.view->annotationViewport(), AnnotationId::ofObject(13));
    const QPoint title(qRound(shown.left() + 20), qRound(shown.top() + 8));
    a.drag(title, title + QPoint(30, 25));
    const QRectF moved = *a.overlay->popupRect(a.view->annotationViewport(), AnnotationId::ofObject(13));
    CHECK(moved.left() == shown.left() + 30);
    CHECK(moved.top() == shown.top() + 25);
    a.click(QPoint(qRound(moved.right() - 10), qRound(moved.top() + 10)));
    CHECK_FALSE(a.overlay->isPopupShown(AnnotationId::ofObject(13)));

    // NOT A COMMAND AND NOT A TILE: the model, the history and every tile request as they were.
    CHECK(a.journal().document().modelHash() == hash);
    CHECK(a.steps() == 0);
    CHECK(a.view->tiles().heldRequests() == tilesBefore);
}

// ==========================================================================================
// S6 guide I-26 -- a caret's pop-up, as a note's
// ==========================================================================================
// M-02's preparation, 2026-10-20: a caret's text -- the words a person inserted -- could be read only in the Comments
// pane and the properties bar, while Acrobat shows it on hover; the user asked for a caret to behave as a note does.
// NOT COVERED (convention 19): the pop-up's paint -- `isPopupShown` and `popupRect` are what `paint` reads.

TEST_CASE("a caret's pop-up shows its inserted text under each behaviour as a note's does", "[annotoverlay][I-26][D6-28]")
{
    Annotated a(true);
    const QByteArray hash = a.journal().document().modelHash();
    const AnnotationId caret = AnnotationId::ofObject(14);
    REQUIRE(a.at(14).subtype == AnnotationSubtype::Caret);
    const QRectF mark = a.rectOf(14);
    const QPoint onCaret(qRound(mark.center().x()), qRound(mark.center().y()));
    const QPoint away(qRound(mark.center().x()), qRound(mark.center().y() + 80));

    // Never: nothing on hover.
    a.overlay->setPopupBehaviour(ui::AnnotationOverlay::PopupBehaviour::Never);
    a.hoverAt(onCaret);
    CHECK_FALSE(a.overlay->isPopupShown(caret));
    // On hover, through the page's own mouse moves: it shows over the caret and goes when the pointer leaves.
    a.overlay->setPopupBehaviour(ui::AnnotationOverlay::PopupBehaviour::OnHover);
    a.hoverAt(onCaret);
    CHECK(a.overlay->isPopupShown(caret));
    const std::optional<QRectF> shown = a.overlay->popupRect(a.view->annotationViewport(), caret);
    REQUIRE(shown);
    CHECK(shown->left() >= mark.right());   // beside the caret, where a note's opens when it has no Popup
    a.hoverAt(away);
    CHECK_FALSE(a.overlay->isPopupShown(caret));
    // Always: the caret's and the note's, and still not the rectangle's or the highlight's.
    a.overlay->setPopupBehaviour(ui::AnnotationOverlay::PopupBehaviour::Always);
    CHECK(a.overlay->isPopupShown(caret));
    CHECK(a.overlay->isPopupShown(AnnotationId::ofObject(13)));
    CHECK_FALSE(a.overlay->isPopupShown(AnnotationId::ofObject(10)));
    CHECK_FALSE(a.overlay->isPopupShown(AnnotationId::ofObject(12)));

    // A double click pins it under Never, and its cross closes it -- as a note's (A-47).
    a.overlay->setPopupBehaviour(ui::AnnotationOverlay::PopupBehaviour::Never);
    a.mouse(QEvent::MouseButtonPress, onCaret);
    a.mouse(QEvent::MouseButtonRelease, onCaret);
    a.mouse(QEvent::MouseButtonDblClick, onCaret);
    a.mouse(QEvent::MouseButtonRelease, onCaret);
    REQUIRE(a.overlay->isPopupShown(caret));
    const QRectF pinned = *a.overlay->popupRect(a.view->annotationViewport(), caret);
    a.click(QPoint(qRound(pinned.right() - 10), qRound(pinned.top() + 10)));
    CHECK_FALSE(a.overlay->isPopupShown(caret));

    // View state: no command, no byte.
    CHECK(a.journal().document().modelHash() == hash);
    CHECK(a.steps() == 0);
}

// ==========================================================================================
// A-39's last clause -- an edit the show step refuses is said in the status bar
// ==========================================================================================

TEST_CASE("an edit the engine cannot be shown is refused and the status bar says so", "[annotoverlay][A-39]")
{
    Annotated a;
    a.fixture.backend.failShows = true;
    const core::PdfRect before = a.at(10).rect;
    const QPoint from = a.onBorderOf(10);
    a.drag(from, from + QPoint(40, 0));
    CHECK(a.at(10).rect == before);
    CHECK(a.steps() == 0);
    REQUIRE(a.window->statusBar() != nullptr);
    const QString said = a.window->statusBar()->message()->text();
    INFO(said.toStdString());
    CHECK_FALSE(said.isEmpty());
    CHECK_FALSE(said.contains(QStringLiteral("error.")));   // words, never a key
    a.fixture.backend.failShows = false;
    a.drag(from, from + QPoint(40, 0));   // the control: unrefused, the same drag moves it
    CHECK(a.at(10).rect != before);
}
