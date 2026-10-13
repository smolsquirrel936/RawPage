// tst_annottools -- the annotation tools' chords, from key events. Stage 6 T5, guide case A-40
// (`Stage6-Annotations-Testing-Guide.md` §5.7) and decision D6-9.
//
// WHAT THIS SUITE IS TRYING TO CATCH: an arming chord that cannot be pressed from the page with nothing
// armed (Stage 5's D5-15 in a new place); arming a tool taking every page chord away; `K` doing nothing.
// The row-for-row agreement with `Features.md` §8 -- its census at thirteen Write-mode collisions -- is
// `tests/unit/tst_keybindings.cpp`'s, which parses §8; this suite presses keys.
//
// WHAT IT DOES NOT COVER (convention 19): what each tool CREATES -- its task's (T6 ...), and a press on an
// empty page with a tool armed makes nothing yet; and the keys under a real keyboard -- events are sent to
// the focused widget, below Windows' input queue, where a layout's AltGr or another program's hotkey would
// take a `Ctrl+Alt` chord first (Stage 5's I-17; the guide's §7 caveat).

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "tests/ui/ShellFixture.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/AnnotationTextEditor.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ToolController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMimeData>

using namespace rawpage;

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

struct Page {
    Fixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::PageView* view = nullptr;

    Page()
    {
        fixture.backend.pagesPerDocument = 12;
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1000, 800);
        window->openFiles({fixture.writeFile(QStringLiteral("tools.pdf"))});
        window->show();
        for (QWidget* widget : window->findChildren<QWidget*>())
            if (QLayout* layout = widget->layout()) layout->activate();
        QCoreApplication::processEvents();
        view = window->currentView();
        REQUIRE(view != nullptr);
        REQUIRE(view->width() > 300);
        window->raiseToForeground();
        view->setFocus();
        QCoreApplication::processEvents();
    }

    // A key to the page, through the application, as a person presses it.
    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const
    {
        QKeyEvent override(QEvent::ShortcutOverride, key, modifiers);
        override.ignore();
        QCoreApplication::sendEvent(view, &override);
        QKeyEvent press(QEvent::KeyPress, key, modifiers);
        press.ignore();
        QCoreApplication::sendEvent(view, &press);
        QCoreApplication::processEvents();
    }
};

constexpr Qt::KeyboardModifiers kCtrlAlt = Qt::ControlModifier | Qt::AltModifier;

} // namespace

TEST_CASE("each arming chord arms its tool from the page with nothing armed", "[annottools][A-40][D6-9]")
{
    // §8's Viewer table, chord for chord (Q1), and the tool each arms.
    const std::pair<Qt::Key, ui::AnnotationToolKind> chords[] = {
        {Qt::Key_M, ui::AnnotationToolKind::StickyNote},      {Qt::Key_H, ui::AnnotationToolKind::Highlight},
        {Qt::Key_U, ui::AnnotationToolKind::Underline},       {Qt::Key_X, ui::AnnotationToolKind::Strikeout},
        {Qt::Key_T, ui::AnnotationToolKind::TextBox},         {Qt::Key_L, ui::AnnotationToolKind::Line},
        {Qt::Key_R, ui::AnnotationToolKind::Rectangle},       {Qt::Key_O, ui::AnnotationToolKind::Oval},
        {Qt::Key_N, ui::AnnotationToolKind::Polygon},         {Qt::Key_P, ui::AnnotationToolKind::Pencil},
        {Qt::Key_S, ui::AnnotationToolKind::Stamp},           {Qt::Key_D, ui::AnnotationToolKind::MeasureDistance},
        {Qt::Key_A, ui::AnnotationToolKind::MeasureArea},     {Qt::Key_K, ui::AnnotationToolKind::RedactMark},
    };
    // Fourteen chords; the three T6 kinds after them -- squiggly, replace, insert -- T7's two -- callout,
    // typewriter -- T8's four -- arrow, polyline, cloud, eraser -- T11's attach-file tool and T12's two -- the
    // perimeter and the dimension callout -- have none (§8 untouched).
    REQUIRE(std::size(chords) == ui::kChordedAnnotationToolCount);
    REQUIRE(ui::kAnnotationToolKindCount == ui::kChordedAnnotationToolCount + 12);
    for (const auto& [key, kind] : chords) {
        Page page;
        INFO("Ctrl+Alt+" << QKeySequence(key).toString().toStdString());
        REQUIRE(ui::ShortcutBinder::declaredContext(page.view) == core::KeyContext::Viewer);
        REQUIRE(page.view->tools().armed() == ui::ToolKind::Select);
        page.key(key, kCtrlAlt);
        CHECK(page.view->tools().armedAnnotation() == kind);
        CHECK(ui::ShortcutBinder::declaredContext(page.view) == core::KeyContext::AnnotationTools);
        // And the status bar says which -- and, since T6, how a tool that makes is used.
        CHECK_FALSE(page.window->statusBar()->message()->text().isEmpty());
    }
}

TEST_CASE("with a tool armed the page's chords still act and another arming chord swaps the tool",
          "[annottools][A-40][D6-9]")
{
    Page page;
    page.key(Qt::Key_H, kCtrlAlt);
    REQUIRE(page.view->tools().armedAnnotation() == ui::AnnotationToolKind::Highlight);

    // `Ctrl+=` -- the zoom, through the chain to the Viewer.
    const double zoom = page.view->controller().zoom().factor;
    page.key(Qt::Key_Equal, Qt::ControlModifier);
    CHECK(page.view->controller().zoom().factor > zoom);
    // `Ctrl+0` -- a fit, set as a mode.
    page.key(Qt::Key_0, Qt::ControlModifier);
    CHECK(page.view->controller().fitMode() != ui::FitMode::None);
    // `Page Down` -- the next screen.
    const double scroll = page.view->controller().scroll().y;
    page.key(Qt::Key_PageDown);
    CHECK(page.view->controller().scroll().y > scroll);
    // The tool is still the one armed: none of those was the tool's to take.
    CHECK(page.view->tools().armedAnnotation() == ui::AnnotationToolKind::Highlight);

    // Another arming chord swaps the tool without disarming first.
    page.key(Qt::Key_O, kCtrlAlt);
    CHECK(page.view->tools().armedAnnotation() == ui::AnnotationToolKind::Oval);

    // `K` -- keep the tool armed after a use -- toggles, in Annotation tools only.
    REQUIRE_FALSE(page.view->tools().keepToolSelected());
    page.key(Qt::Key_K);
    CHECK(page.view->tools().keepToolSelected());
    page.key(Qt::Key_K);
    CHECK_FALSE(page.view->tools().keepToolSelected());

    // `Esc` puts the tool away; back in the Viewer, `K` is nothing and leaves the key alone.
    page.key(Qt::Key_Escape);
    CHECK(page.view->tools().armed() == ui::ToolKind::Select);
    CHECK(ui::ShortcutBinder::declaredContext(page.view) == core::KeyContext::Viewer);
    page.key(Qt::Key_K);
    CHECK_FALSE(page.view->tools().keepToolSelected());
}

// ==========================================================================================
// Stage 6 T7: a typed comment's editor refuses at input what no face can show (D6-7, guide A-56)
// ==========================================================================================
// Every way text enters a QPlainTextEdit but the key -- which `tests/composition/tst_typedink.cpp` presses --
// is held here: an input method's COMMIT, a paste, and the note's editor, which refuses nothing.

TEST_CASE("a typed editor refuses an input method's commit and a paste it cannot show and names the character",
          "[annottools][A-56]")
{
    // A paste arrives as `insertFromMimeData`, which a person reaches by `Ctrl+V` or a drop; made reachable here.
    struct OpenEditor : ui::AnnotationTextEditor {
        using ui::AnnotationTextEditor::insertFromMimeData;
    };
    OpenEditor editor;
    editor.setRefusesUnshowable(true);
    std::vector<uint> refused;
    QObject::connect(&editor, &ui::AnnotationTextEditor::characterRefused, [&refused](uint c) { refused.push_back(c); });
    editor.show();
    QCoreApplication::processEvents();

    // An input method commits CJK -- accepted -- then Greek, which leaves nothing behind, not even its preedit.
    QInputMethodEvent preedit(QStringLiteral("\u03B1"), {});
    QCoreApplication::sendEvent(&editor, &preedit);
    QInputMethodEvent greek(QString(), {});
    greek.setCommitString(QStringLiteral("\u03B1\u03B2"));
    QCoreApplication::sendEvent(&editor, &greek);
    CHECK(editor.toPlainText().isEmpty());
    REQUIRE(refused == std::vector<uint>{0x03B1});
    QInputMethodEvent cjk(QString(), {});
    cjk.setCommitString(QStringLiteral("\u4E2D\u6587"));
    QCoreApplication::sendEvent(&editor, &cjk);
    CHECK(editor.toPlainText() == QStringLiteral("\u4E2D\u6587"));

    // A paste of mixed text is refused WHOLE -- none of it goes in -- and the first refused character named.
    QMimeData mixed;
    mixed.setText(QStringLiteral(" ok \u05E9\u05DC\u05D5\u05DD"));
    editor.insertFromMimeData(&mixed);
    CHECK(editor.toPlainText() == QStringLiteral("\u4E2D\u6587"));
    REQUIRE(refused.size() == 2u);
    CHECK(refused.back() == 0x05E9u);
    QMimeData latin;
    latin.setText(QStringLiteral(" caf\u00E9"));
    editor.insertFromMimeData(&latin);
    CHECK(editor.toPlainText() == QStringLiteral("\u4E2D\u6587 caf\u00E9"));

    // A NOTE's editor draws nothing, so it refuses nothing.
    ui::AnnotationTextEditor note;
    CHECK_FALSE(note.refusesUnshowable());
    note.show();
    QInputMethodEvent any(QString(), {});
    any.setCommitString(QStringLiteral("\u03A9"));
    QCoreApplication::sendEvent(&note, &any);
    CHECK(note.toPlainText() == QStringLiteral("\u03A9"));
}

TEST_CASE("the callout and the typewriter arm from their commands and say how they are used", "[annottools][D6-8]")
{
    Page page;
    for (const auto& [id, kind] : {std::pair{core::ActionId::AnnotCallout, ui::AnnotationToolKind::Callout},
                                   std::pair{core::ActionId::AnnotTypewriter, ui::AnnotationToolKind::Typewriter},
                                   std::pair{core::ActionId::AnnotTextBox, ui::AnnotationToolKind::TextBox}}) {
        INFO(ui::annotationToolKindName(kind));
        REQUIRE(page.fixture.actions.dispatch(id, core::KeyContext::Viewer));
        CHECK(page.view->tools().armedAnnotation() == kind);
        CHECK(ui::createsAnnotations(kind));
        CHECK(ui::armedBy(id) == std::optional<ui::AnnotationToolKind>(kind));
        CHECK(ui::armingAction(kind) == id);
        CHECK(page.view->tools().cursor() == (kind == ui::AnnotationToolKind::Typewriter ? Qt::IBeamCursor : Qt::CrossCursor));
    }
}
