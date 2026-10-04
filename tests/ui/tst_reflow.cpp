// Stage 5 T11 -- Reflow view, decision D5-16 (`Stage5-TextSearch-Plan.md` §3). Guide cases A-92 … A-94
// (`Stage5-TextSearch-Testing-Guide.md` §5.13), as a widget driven by key events over `FakeBackend`, whose
// pages each carry five lines of text in one block (`ShellFixture.h`).
//
// WHAT IT DOES NOT COVER (convention 19), and where that is:
//   * **A real page's reading order and its blocks.** The fake page is one block; the analyzer's order over
//     two columns, and the place at a block in the middle of a page, are
//     `tests/composition/tst_reflowink.cpp`'s over `columns-two.pdf` with the real engine.
//   * **Whether it reads well.** Re-wrapped text at a size a person chooses, on a real screen -- manual
//     case M-09.
//   * **A real wheel.** A-129's `Ctrl`+wheel is a `QWheelEvent` sent to the viewport, below Windows' input
//     queue; a mouse's and a touchpad's notches are the walker's.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/render/RenderScheduler.h"
#include "tests/ui/ShellFixture.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/ShortcutBinder.h"
#include "ui/viewer/PageLayoutEngine.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/ReflowView.h"
#include "ui/viewer/ViewportController.h"
#include "ui/write/FindReplaceBar.h"

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLayout>
#include <QLineEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QWheelEvent>

#include <cmath>
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

struct Reader {
    mutable Fixture                 fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::PageView*                   view = nullptr;

    explicit Reader(int pages = 30)
    {
        fixture.backend.pagesPerDocument = pages;
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 850);
        window->openFiles({fixture.writeFile(QStringLiteral("reflow.pdf"))});
        driveLayout(window.get());
        view = window->currentView();
        REQUIRE(view != nullptr);
        window->raiseToForeground();
        view->setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        fixture.pumpTiles();
        REQUIRE(view->width() > 400);
        REQUIRE(view->height() > 300);
    }

    ui::DocumentWindow& document() const { return *window->currentDocument(); }
    ui::ReflowView& reflow() const { return *document().reflow(); }
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

    // Until every page of the reflow's window has arrived, been laid out and the place applied.
    void settle() const
    {
        QElapsedTimer timer;
        timer.start();
        do {
            fixture.pumpTiles(500);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
        } while (!reflow().isSettled() && timer.elapsed() < 8000);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 40);
    }

    // `Ctrl+4` as a key event, from wherever the focus is.
    void toggle() const
    {
        CHECK(key(Qt::Key_4, Qt::ControlModifier, QStringLiteral("4")));
        settle();
    }

    // The first paragraph's text at the top of the reflow.
    QString topParagraph() const
    {
        const QTextCursor cursor = reflow().cursorForPosition(QPoint(2, 1));
        return cursor.block().text();
    }
};

} // namespace

TEST_CASE("Ctrl+4 shows the reflow and Ctrl+4 again returns to the page", "[reflow][A-92]")
{
    Reader reader;
    REQUIRE(reader.context() == core::KeyContext::Viewer);
    CHECK(reader.fixture.actions.isRegistered(ActionId::ViewReflow));
    CHECK_FALSE(reader.document().isReflowing());

    reader.toggle();
    REQUIRE(reader.document().isReflowing());
    CHECK(reader.reflow().isVisible());
    CHECK_FALSE(reader.view->isVisible());
    CHECK(reader.window->focusWidget() == &reader.reflow());
    // THE VIEWER'S CONTEXT, so the next `Ctrl+4` resolves here too.
    CHECK(reader.context() == core::KeyContext::Viewer);
    CHECK(reader.fixture.actions.isChecked(ActionId::ViewReflow));

    // THE TEXT IS THE PAGE'S, ONE PARAGRAPH PER BLOCK, ITS LINES JOINED: the fake page is five lines in one
    // block, so page 1 is one paragraph reading line 1 to line 5.
    const QString text = reader.reflow().toPlainText();
    CHECK(text.contains(QStringLiteral("Page 1 line 1 text Page 1 line 2 text Page 1 line 3 text "
                                       "Page 1 line 4 text Page 1 line 5 text")));
    CHECK(text.indexOf(QStringLiteral("Page 1 line 5")) < text.indexOf(QStringLiteral("Page 2 line 1")));

    reader.toggle();
    CHECK_FALSE(reader.document().isReflowing());
    CHECK(reader.view->isVisible());
    CHECK_FALSE(reader.reflow().isVisible());
    CHECK(reader.window->focusWidget() == reader.view);
    CHECK_FALSE(reader.fixture.actions.isChecked(ActionId::ViewReflow));
}

TEST_CASE("the reader's place survives a toggle both ways", "[reflow][A-93]")
{
    Reader reader;
    // ON THE PAGE VIEW: page 12 at the top.
    reader.view->goToPage(11);
    reader.fixture.pumpTiles();
    REQUIRE(reader.view->controller().currentPage() == 11);

    reader.toggle();
    REQUIRE(reader.document().isReflowing());
    // THE BLOCK AT THE TOP OF THE PAGE VIEW IS AT THE TOP OF THE REFLOW.
    CHECK(reader.reflow().place() == ui::ReflowView::Place{11, 0});
    CHECK(reader.topParagraph().startsWith(QStringLiteral("Page 12 line 1 text")));
    // The window round it: some pages before, more after.
    CHECK(reader.reflow().firstPage() < 11);
    CHECK(reader.reflow().lastPage() > 11);

    // SCROLLED IN THE REFLOW to page 15's paragraph, as a reader's own scroll does.
    QTextBlock target;
    for (QTextBlock block = reader.reflow().document()->begin(); block.isValid(); block = block.next())
        if (block.text().startsWith(QStringLiteral("Page 15 line 1"))) target = block;
    REQUIRE(target.isValid());
    const int y = static_cast<int>(reader.reflow().document()->documentLayout()->blockBoundingRect(target).top());
    reader.reflow().verticalScrollBar()->triggerAction(QAbstractSlider::SliderMove);
    reader.reflow().verticalScrollBar()->setValue(y);
    QCoreApplication::processEvents();
    REQUIRE(reader.reflow().place() == ui::ReflowView::Place{14, 0});

    reader.toggle();
    REQUIRE_FALSE(reader.document().isReflowing());
    // THE PAGE VIEW RETURNS TO THE BLOCK NOW AT THE TOP: page 15, its first line's box at the top edge.
    const ui::PagePoint top = reader.view->pagePointAt(QPoint(reader.view->viewport()->width() / 2, 0));
    REQUIRE(top.isValid());
    CHECK(top.page == 14);
    const double blockTop = uitest::kFakeTextFirstBaseline - 0.21 * uitest::kFakeTextSize + 1.11 * uitest::kFakeTextSize;
    INFO("top edge at y " << top.point.y << ", the block's top at " << blockTop);
    CHECK(std::abs(top.point.y - blockTop) < 1.5);

    // AND IN AGAIN: the same block.
    reader.toggle();
    CHECK(reader.reflow().place() == ui::ReflowView::Place{14, 0});
}

TEST_CASE("the zoom chords change the reflow's text size and leave the page's zoom", "[reflow][A-94]")
{
    Reader reader;
    const double pageZoom = reader.view->controller().zoom().factor;
    reader.toggle();
    REQUIRE(reader.document().isReflowing());
    const double before = reader.reflow().drawnSize(10.0);

    CHECK(reader.key(Qt::Key_Equal, Qt::ControlModifier, QStringLiteral("=")));
    reader.settle();
    CHECK(reader.reflow().drawnSize(10.0) > before);
    // THE TEXT IS DRAWN AT IT: the first paragraph's characters carry the larger size.
    const QTextCursor at(reader.reflow().document()->begin());
    QTextCursor inside = at;
    inside.movePosition(QTextCursor::Right);
    CHECK(std::abs(inside.charFormat().fontPointSize() - reader.reflow().drawnSize(10.0)) < 0.01);

    CHECK(reader.key(Qt::Key_Minus, Qt::ControlModifier, QStringLiteral("-")));
    CHECK(reader.key(Qt::Key_Minus, Qt::ControlModifier, QStringLiteral("-")));
    reader.settle();
    CHECK(reader.reflow().drawnSize(10.0) < before);
    // `Ctrl+1`, ACTUAL SIZE, is the text's own size again -- and so is a fit, `Ctrl+0`: there is no page to fit
    // in a reflow (D5-16 clarified).
    CHECK(reader.key(Qt::Key_1, Qt::ControlModifier, QStringLiteral("1")));
    reader.settle();
    CHECK(reader.reflow().drawnSize(10.0) == before);
    CHECK(reader.key(Qt::Key_Equal, Qt::ControlModifier, QStringLiteral("=")));
    CHECK(reader.key(Qt::Key_0, Qt::ControlModifier, QStringLiteral("0")));
    reader.settle();
    CHECK(reader.reflow().drawnSize(10.0) == before);

    // THE PAGE'S ZOOM DID NOT MOVE: the chords went to the surface on show.
    CHECK(reader.view->controller().zoom().factor == pageZoom);
}

TEST_CASE("Ctrl and the wheel step the reflow's text size as the zoom chords do", "[reflow][A-129]")
{
    Reader reader;
    const double pageZoom = reader.view->controller().zoom().factor;
    reader.toggle();
    REQUIRE(reader.document().isReflowing());
    ui::ReflowView& reflow = reader.reflow();
    const double before = reflow.textScale();

    // A WHEEL AS A MOUSE SENDS IT, to the text browser's viewport -- the widget under the pointer.
    const auto wheel = [&reflow](int angle, Qt::KeyboardModifiers modifiers) {
        QWidget* viewport = reflow.viewport();
        const QPointF at(viewport->width() / 2.0, viewport->height() / 2.0);
        QWheelEvent event(at, viewport->mapToGlobal(at), QPoint(), QPoint(0, angle), Qt::NoButton, modifiers,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(viewport, &event);
        QCoreApplication::processEvents();
    };

    // A NOTCH UP IS ONE STEP, THE SAME AS `Ctrl+=` -- and drawn at it.
    wheel(+120, Qt::ControlModifier);
    reader.settle();
    CHECK(reflow.textScale() == before * ui::ReflowView::kZoomStep);
    QTextCursor inside(reflow.document()->begin());
    inside.movePosition(QTextCursor::Right);
    CHECK(std::abs(inside.charFormat().fontPointSize() - reflow.drawnSize(10.0)) < 0.01);

    // A TOUCHPAD'S HALF NOTCHES add up to one step, not one each.
    wheel(+60, Qt::ControlModifier);
    CHECK(reflow.textScale() == before * ui::ReflowView::kZoomStep);
    wheel(+60, Qt::ControlModifier);
    CHECK(std::abs(reflow.textScale() - before * ui::ReflowView::kZoomStep * ui::ReflowView::kZoomStep) < 1e-9);

    // DOWN, A STEP A NOTCH -- the same as `Ctrl+-`.
    wheel(-120, Qt::ControlModifier);
    wheel(-120, Qt::ControlModifier);
    reader.settle();
    CHECK(std::abs(reflow.textScale() - before) < 1e-9);

    // THE CONTROL: without `Ctrl` the wheel scrolls and the size stays.
    QScrollBar* bar = reflow.verticalScrollBar();
    REQUIRE(bar->maximum() > bar->minimum());
    const int top = bar->value();
    wheel(-120, Qt::NoModifier);
    CHECK(bar->value() > top);
    CHECK(std::abs(reflow.textScale() - before) < 1e-9);

    // AND THE PAGE'S ZOOM DID NOT MOVE.
    CHECK(reader.view->controller().zoom().factor == pageZoom);
}

TEST_CASE("nothing is rasterized while the reflow shows and its keys scroll it", "[reflow][A-94]")
{
    Reader reader;
    reader.toggle();
    REQUIRE(reader.document().isReflowing());
    // THE PAGE SURFACE IS GONE, not merely covered: a visible one would go on asking for its tiles.
    CHECK_FALSE(reader.view->isVisible());
    reader.fixture.pumpTiles();
    const core::render::RenderScheduler::Stats before = reader.fixture.renderScheduler.stats();
    const core::DevicePoint pageScroll = reader.view->controller().scroll();

    // READING: `Page Down` three times, a zoom, the window ENLARGED -- which a page surface on screen would have
    // to fill with tiles it does not hold (shrinking it asks for nothing, and proved nothing: this case's first
    // planted control stayed green that way).
    const int scrolled = reader.reflow().verticalScrollBar()->value();
    for (int i = 0; i < 3; ++i) CHECK(reader.key(Qt::Key_PageDown));
    reader.settle();
    CHECK(reader.reflow().verticalScrollBar()->value() > scrolled);
    CHECK(reader.key(Qt::Key_Equal, Qt::ControlModifier, QStringLiteral("=")));
    reader.window->resize(1400, 1050);
    driveLayout(reader.window.get());
    reader.settle();

    // NO TILE OF THE PAGE WAS ASKED FOR: every enqueue since is text (the window growing) -- or the THUMBNAILS
    // pane's, at `Speculative`: the sidebar stays on screen beside the reflow and fills the room a larger window
    // gives it (measured: 2 tiles on this case's first run), which is a surface the person sees and is right to.
    // Text and tasks never use `Speculative`, so what is left is the page surface's lanes.
    const auto pageTiles = [](const core::render::RenderScheduler::Stats& s) {
        return s.enqueued - s.textEnqueued - s.taskEnqueued - s.enqueuedSpeculative;
    };
    const core::render::RenderScheduler::Stats after = reader.fixture.renderScheduler.stats();
    CHECK(pageTiles(after) == pageTiles(before));
    // AND `Page Down` SCROLLED THE REFLOW, NOT THE HIDDEN PAGE: the reflow claims the keys it reads with.
    CHECK(reader.view->controller().scroll().y == pageScroll.y);

    // THE CONTROL: back on the page, the same keys and a resize DO ask for tiles.
    reader.toggle();
    REQUIRE_FALSE(reader.document().isReflowing());
    reader.window->resize(1100, 850);
    driveLayout(reader.window.get());
    CHECK(reader.key(Qt::Key_PageDown));
    reader.fixture.pumpTiles();
    const core::render::RenderScheduler::Stats onPage = reader.fixture.renderScheduler.stats();
    CHECK(pageTiles(onPage) > pageTiles(after));
}

TEST_CASE("a find hit leaves the reflow for the page at the hit", "[reflow][A-94]")
{
    Reader reader;
    reader.toggle();
    REQUIRE(reader.document().isReflowing());

    // `Ctrl+F` FROM THE REFLOW: the bar opens above it -- find searches the document, never the reflow's text.
    CHECK(reader.key(Qt::Key_F, Qt::ControlModifier, QStringLiteral("f")));
    ui::FindReplaceBar& bar = *reader.document().findBar();
    REQUIRE(bar.isOpen());
    for (const QChar ch : QStringLiteral("Page 20 line 3")) {
        const Qt::Key code = ch == QLatin1Char(' ') ? Qt::Key_Space : static_cast<Qt::Key>(ch.toUpper().unicode());
        reader.key(code, Qt::NoModifier, QString(ch));
    }
    QElapsedTimer timer;
    timer.start();
    while ((bar.isSearchPending() || bar.isSearching() || bar.searchesFinished() == 0) && timer.elapsed() < 10000) {
        reader.fixture.pumpTiles(200);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    REQUIRE(bar.hits().size() == 1);

    // THE BAR TAKING THE VIEW TO ITS CURRENT HIT IS ACTIVATING IT: the hit is a place on a page, so the tab is
    // back on the page at it -- with the focus still in the field, where the person is typing.
    reader.fixture.pumpTiles();
    CHECK_FALSE(reader.document().isReflowing());
    CHECK(reader.view->isVisible());
    CHECK(reader.view->controller().currentPage() == 19);
    CHECK(reader.window->focusWidget() == bar.field());

    // AND `Enter` -- jump to the result -- from the reflow again: the same.
    reader.view->setFocus(Qt::OtherFocusReason);
    reader.toggle();
    REQUIRE(reader.document().isReflowing());
    bar.field()->setFocus(Qt::OtherFocusReason);
    CHECK(reader.key(Qt::Key_Return));
    reader.fixture.pumpTiles();
    CHECK_FALSE(reader.document().isReflowing());
    CHECK(reader.view->controller().currentPage() == 19);
}

TEST_CASE("the reflow's window grows as the reader scrolls to its end", "[reflow]")
{
    Reader reader(40);
    reader.toggle();
    REQUIRE(reader.document().isReflowing());
    const int last = reader.reflow().lastPage();
    REQUIRE(last < 39);

    // TO THE END OF WHAT IS SHOWN, as the reader's own scroll.
    QScrollBar* bar = reader.reflow().verticalScrollBar();
    bar->triggerAction(QAbstractSlider::SliderToMaximum);
    reader.settle();
    CHECK(reader.reflow().lastPage() > last);
    CHECK(reader.reflow().toPlainText().contains(QStringLiteral("Page %1 line 1 text").arg(reader.reflow().lastPage() + 1)));
}
