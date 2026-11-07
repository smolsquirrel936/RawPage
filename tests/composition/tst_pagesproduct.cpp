// tst_pagesproduct -- Stage 7 T8 through the PRODUCT (`Stage7-Assembly-Plan.md` D7-13, D7-20): `app::CompositionRoot`
// and `ui::MainWindow` as `main.cpp` builds them, the shipped backend and its engine. What the fake backend cannot say
// is said here -- that the pages a drop, a paste or a cut lands SHOW the identities they had (D7-18's first oracle,
// read by the live engine), that the badges come from a real file's annotations, that a delete's report names what it
// cleared, and that a signed document is warned. Guide cases A-54's and A-55's identity halves, A-55's second process
// (`[D7-13]`), A-57 through the product and A-27's warning half (`Stage7-Assembly-Testing-Guide.md` §5.10, §5.4).
//
// WHAT IT DOES NOT COVER (convention 19):
// * **No key, no mouse, no drag event.** The window's own entry points -- `dropPages`, `copyPages`, `pastePages`,
//   `cutPages`, `deletePages` -- are called, which every key, drop and ribbon button reaches; the keys, the clicks and
//   the drop events are `tests/ui/tst_thumbnailsedit.cpp`'s, over the fake backend.
// * **The second process is `[D7-13]`'s, by hand, on the `windows` platform** -- it REPLACES the system clipboard's
//   contents and runs a second copy of this binary (`[.][D7-13-child]`, hidden from every other run). Under `ctest` it
//   SKIPS: offscreen, the clipboard never leaves the process.

#include <catch2/catch_test_macros.hpp>

#include "AnnotProduct.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/CopiedPages.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/PageTree.h"
#include "pdf/render/PdfiumDocument.h"
#include "ui/dialogs/PageOpsDialogs.h"
#include "core/text/PageTextModel.h"
#include "ui/panes/ThumbnailsPane.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/MessageBar.h"
#include "ui/shell/StatusBar.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLabel>
#include <QMimeData>
#include <QProcess>
#include <QProcessEnvironment>

#include <cmath>
#include <memory>

using namespace rawpage;
using annot_product::Product;

namespace {

QString identityAt(Product& product, ui::DocumentWindow* document, int index)
{
    core::IDocumentBackend& backend = product.root->services().backend;
    core::text::PageTextModel model;
    REQUIRE(backend.textExtractor(document->handle())->extractPage(document->handle(), index, model));
    return model.text();
}

// A second window of the same process with a copy of `relative` open in it -- the drop target of A-54.
struct Second {
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow* document = nullptr;

    Second(Product& product, const char* relative)
    {
        const QString source = test_oracle::corpusPath(relative);
        const QString target = QDir(product.scratch.path()).filePath(QStringLiteral("second-") + QFileInfo(source).fileName());
        QFile::remove(target);
        REQUIRE(QFile::copy(source, target));
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        window = std::make_unique<ui::MainWindow>(product.root->services());
        window->resize(900, 800);
        window->show();
        window->openFiles({target});
        QCoreApplication::processEvents();
        document = window->currentDocument();
        REQUIRE(document != nullptr);
        REQUIRE(document->isEditable());
    }
    ~Second()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCoreApplication::processEvents();
    }
};

struct ClipboardCleared {
    ~ClipboardCleared()
    {
        if (QClipboard* clipboard = QGuiApplication::clipboard()) clipboard->clear();
    }
};

} // namespace

// =============================================================================================
// A-54 -- dropped into another window's pane: one insert, the pages showing their identities, the source unchanged
// =============================================================================================

TEST_CASE("thumbnails.drag pages dropped into another window show their identities there and the source is unchanged", "[pagesproduct][A-54]")
{
    Product product;
    product.open("assembly/refs-every-kind.pdf");
    ui::DocumentWindow* source = product.document();
    Second target(product, "assembly/odd-count-9.pdf");

    REQUIRE(target.window->dropPages(target.document, source->handle(), {2, 3}, 1));
    CHECK(target.document->pageCount() == 11);
    CHECK(target.document->journal()->historySize() == 1);
    CHECK(identityAt(product, target.document, 0).contains(QStringLiteral("C09-0001")));
    CHECK(identityAt(product, target.document, 1).contains(QStringLiteral("P-0003")));
    CHECK(identityAt(product, target.document, 2).contains(QStringLiteral("P-0004")));
    CHECK(identityAt(product, target.document, 3).contains(QStringLiteral("C09-0002")));
    CHECK(source->pageCount() == 12);
    CHECK_FALSE(source->isDirty());

    // AS THE SOURCE STANDS AT THE DROP: a page moved in the source first is the page the drop takes.
    REQUIRE(product.window->movePagesTo(source, {11}, 0));   // P-0012 to the front
    REQUIRE(target.window->dropPages(target.document, source->handle(), {0}, 0));
    CHECK(identityAt(product, target.document, 0).contains(QStringLiteral("P-0012")));

    // One undo per drop.
    REQUIRE(target.document->journal()->undo());
    REQUIRE(target.document->journal()->undo());
    CHECK(target.document->pageCount() == 9);
}

// =============================================================================================
// A-55 -- copy, cut and paste in this process, by what the pages show
// =============================================================================================

TEST_CASE("thumbnails.clipboard pages copied cut and pasted in one process show their identities", "[pagesproduct][A-55]")
{
    ClipboardCleared cleared;
    Product product;
    product.open("assembly/refs-every-kind.pdf");
    ui::DocumentWindow* document = product.document();

    REQUIRE(product.window->copyPages(document, {0, 11}));
    const QMimeData* held = QGuiApplication::clipboard()->mimeData();
    REQUIRE(held != nullptr);
    REQUIRE(held->hasFormat(QLatin1String(core::kPagesMimeType)));
    CHECK(ui::MainWindow::clipboardPageCount() == 2);
    CHECK_FALSE(document->isDirty());   // a copy changes nothing

    // Pasted into the same document after page 1: P-0001, then the pasted P-0001 and P-0012.
    REQUIRE(product.window->pastePages(document, 1));
    CHECK(document->pageCount() == 14);
    CHECK(identityAt(product, document, 1).contains(QStringLiteral("P-0001")));
    CHECK(identityAt(product, document, 2).contains(QStringLiteral("P-0012")));
    CHECK(identityAt(product, document, 3).contains(QStringLiteral("P-0002")));

    // A CUT IS A COPY AND ONE DELETE: the two pasted pages cut, one undo step, and they are what the clipboard holds.
    const std::size_t history = document->journal()->historySize();
    REQUIRE(product.window->cutPages(document, {1, 2}));
    CHECK(document->pageCount() == 12);
    CHECK(document->journal()->historySize() == history + 1);
    CHECK(identityAt(product, document, 1).contains(QStringLiteral("P-0002")));

    // ... and pasted into ANOTHER document, at its end.
    Second other(product, "assembly/odd-count-3.pdf");
    REQUIRE(other.window->pastePages(other.document, 3));
    CHECK(other.document->pageCount() == 5);
    CHECK(identityAt(product, other.document, 3).contains(QStringLiteral("P-0001")));
    CHECK(identityAt(product, other.document, 4).contains(QStringLiteral("P-0012")));
}

// =============================================================================================
// A-57 through the product -- the badges from a real file
// =============================================================================================

TEST_CASE("thumbnails.badges the pane shows links exactly on the pages refs-every-kind links from", "[pagesproduct][A-57]")
{
    Product product;
    product.open("assembly/refs-every-kind.pdf");
    ui::ThumbnailsPane* pane = product.window->thumbnailsPane();
    REQUIRE(pane != nullptr);
    REQUIRE(pane->document() == product.document()->handle());
    using Carries = core::AnnotationStore::PageCarries;
    for (int page = 0; page < 12; ++page) {
        INFO("page " << page);
        CHECK(pane->badgesOf(page) == Carries{false, false, page <= 3});   // the labels' `links`: pages 1 ... 4
    }
    // A page command MOVES them with their pages: the first page to the end, and its link badge goes with it.
    REQUIRE(product.window->movePagesTo(product.document(), {0}, 12));
    CHECK(pane->badgesOf(11) == Carries{false, false, true});
    CHECK(pane->badgesOf(3) == Carries{false, false, false});
}

// =============================================================================================
// M-01 step 3's report -- what a delete cleared, said
// =============================================================================================

TEST_CASE("pages a delete says which references it cleared or removed", "[pagesproduct][D7-2]")
{
    Product product;
    product.open("assembly/refs-every-kind.pdf");
    ui::DocumentWindow* document = product.document();
    // Page 6 (P-0006) is the target of an outline item and of a link on page 1 (the labels); deleting it clears both.
    REQUIRE(product.window->deletePages(document, {5}));
    const QString said = product.window->statusBar()->message()->text();
    INFO(said.toStdString());
    CHECK(said.contains(ui::catalog::translate(QStringLiteral("pages.reference.OutlineItem"))));
    CHECK(said.contains(ui::catalog::translate(QStringLiteral("pages.reference.Link"))));
    // THE CONTROL: `refs-none.pdf`, the same pages with no reference at all -- the same delete says only what it did.
    Product none;
    none.open("assembly/refs-none.pdf");
    REQUIRE(none.window->deletePages(none.document(), {5}));
    CHECK(none.window->statusBar()->message()->text().toStdString()
          == ui::catalog::translate(ui::catalog::key::PagesDone)
                 .arg(ui::catalog::translateText(none.document()->journal()->undoName().value()))
                 .toStdString());
}

// =============================================================================================
// Stage 7 T9 -- remove white margins and embed thumbnails through the PRODUCT: the engine reads each page on the render
// thread (`core::PageWorkRunner`) and ONE command follows, from the pump
// =============================================================================================

namespace {

// Pumps until the window says no page run is going, or `budget` passes.
bool awaitPageWork(Product& product, int budgetMs = 30000)
{
    QElapsedTimer timer;
    timer.start();
    while (product.window->isMeasuringPages() && timer.elapsed() < budgetMs) product.settle();
    product.settle();
    return !product.window->isMeasuringPages();
}

} // namespace

TEST_CASE("pages remove white margins measures each page on the render thread and crops them in one command", "[pagesproduct][A-61]")
{
    Product product;
    product.open("assembly/boxes.pdf");
    ui::DocumentWindow* document = product.document();
    // Page 3 has a known content box (the label's `contentBox`); pages 1 and 2 have content too.
    const core::Result<core::PdfRect> content = product.root->services().backend.contentBox(document->handle(), 2);
    REQUIRE(content);
    ui::CropPagesDialog::Choice choice;
    choice.pages = {0, 1, 2};
    choice.how = ui::CropPagesDialog::How::WhiteMargins;
    choice.padding = 4.0;
    REQUIRE(product.window->cropPages(document, choice));
    REQUIRE(awaitPageWork(product));
    const core::PageTree& pages = document->journal()->document().pages();
    const core::PdfRect crop = pages.at(2)->cropBox();
    const core::PdfRect want = core::whiteMarginBox(content.value(), 4.0, pages.at(2)->mediaBox());   // it had no CropBox
    CHECK(std::abs(crop.left() - want.left()) < 1e-3);
    CHECK(std::abs(crop.bottom() - want.bottom()) < 1e-3);
    CHECK(std::abs(crop.right() - want.right()) < 1e-3);
    CHECK(std::abs(crop.top() - want.top()) < 1e-3);
    // ONE command over the three pages: one undo puts every one back.
    REQUIRE(document->journal()->canUndo());
    REQUIRE(document->journal()->undo());
    CHECK_FALSE(document->journal()->canUndo());
    product.settle();
    // A RUN TAKEN OVER BY A PAGE COMMAND in between refuses rather than crop other pages.
    REQUIRE(product.window->cropPages(document, choice));
    REQUIRE(product.window->isMeasuringPages());
    REQUIRE(product.window->deletePages(document, {0}));
    REQUIRE(awaitPageWork(product));
    CHECK(product.window->statusBar()->message()->text().toStdString()
          == ui::catalog::translateError(QStringLiteral("error.pages.documentChanged"), QString()).toStdString());
    CHECK(document->journal()->document().pages().count() == 2);   // the delete, and nothing cropped
}

TEST_CASE("pages embed thumbnails draws every page on the render thread and embeds them in one command", "[pagesproduct][A-58]")
{
    Product product;
    product.open("assembly/odd-count-3.pdf");
    ui::DocumentWindow* document = product.document();
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::PageEmbedThumbnails, core::KeyContext::Global));
    REQUIRE(awaitPageWork(product));
    REQUIRE(document->journal()->canUndo());
    auto saved = document->save();
    REQUIRE(saved);
    // The engine of a fresh open decodes each page's /Thumb at the size it was drawn at: the page's longest side
    // `kThumbnailEdge` pixels, and its ink in the middle -- the page's identity drawn there.
    auto fresh = pdf::render::PdfiumDocument::open(product.openedPath, nullptr);
    REQUIRE(fresh);
    for (int page = 0; page < 3; ++page) {
        INFO("page " << page);
        auto thumbnail = fresh.value()->embeddedThumbnail(page);
        REQUIRE(thumbnail);
        CHECK(std::max(thumbnail.value().width, thumbnail.value().height) == core::kThumbnailEdge);
        int dark = 0;
        for (int y = 0; y < thumbnail.value().height; ++y) {
            for (int x = 0; x < thumbnail.value().width; ++x) {
                const auto* px = reinterpret_cast<const unsigned char*>(thumbnail.value().pixels.constData()) + y * thumbnail.value().bytesPerLine + x * 4;
                if (px[0] < 128 && px[1] < 128 && px[2] < 128) ++dark;
            }
        }
        CHECK(dark > 10);   // not a blank image: the page's identity is in it
    }
    // Removed again, through its action, and saved: none left.
    REQUIRE(product.root->services().actions.dispatch(core::ActionId::PageRemoveThumbnails, core::KeyContext::Global));
    REQUIRE(document->save());
    auto removed = pdf::render::PdfiumDocument::open(product.openedPath, nullptr);
    REQUIRE(removed);
    for (int page = 0; page < 3; ++page) CHECK_FALSE(removed.value()->embeddedThumbnail(page));
}

// =============================================================================================
// A-27's warning half -- D7-20, on the signed corpus
// =============================================================================================

TEST_CASE("pages a signed corpus file is warned once before its first page command", "[pagesproduct][A-27][D7-20]")
{
    for (const char* relative : {"signed/approval.pdf", "signed/certified-nochanges.pdf"}) {
        INFO(relative);
        Product product;
        product.open(relative);
        ui::DocumentWindow* document = product.document();
        REQUIRE(document->isEditable());
        CHECK_FALSE(product.window->messageBar()->isShowing(ui::MessageBar::Kind::Signed));
        REQUIRE(product.window->duplicatePages(document, {0}));
        REQUIRE(product.window->messageBar()->isShowing(ui::MessageBar::Kind::Signed));
        product.window->messageBar()->dismiss(ui::MessageBar::Kind::Signed);
        REQUIRE(product.window->stepPages(document, {1}, -1));
        CHECK_FALSE(product.window->messageBar()->isShowing(ui::MessageBar::Kind::Signed));   // once
    }
    // The control: an unsigned file is never warned.
    Product plain;
    plain.open("assembly/odd-count-3.pdf");
    REQUIRE(plain.window->duplicatePages(plain.document(), {0}));
    CHECK_FALSE(plain.window->messageBar()->isShowing(ui::MessageBar::Kind::Signed));
}

// =============================================================================================
// A-55's second process -- [D7-13], by hand on the windows platform
// =============================================================================================

namespace {
constexpr const char* kChildTarget = "RAWPAGE_D7_13_TARGET";
}

TEST_CASE("pages copied here are pasted by a second RawPage process and show their identities", "[D7-13][clipboard]")
{
    if (QGuiApplication::platformName() != QLatin1String("windows"))
        SKIP("needs the real windows platform -- offscreen, the clipboard never leaves the process (run by hand)");

    ClipboardCleared cleared;
    Product product;
    product.open("assembly/refs-every-kind.pdf");
    REQUIRE(product.window->copyPages(product.document(), {4, 6}));   // P-0005, P-0007
    QCoreApplication::processEvents();

    // THE SECOND PROCESS: this binary again, running only the hidden child case, over a copy of odd-count-3.pdf.
    const QString target = QDir(product.scratch.path()).filePath(QStringLiteral("pasted-here.pdf"));
    REQUIRE(QFile::copy(test_oracle::corpusPath("assembly/odd-count-3.pdf"), target));
    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    QProcess child;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QString::fromLatin1(kChildTarget), target);
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("windows"));
    child.setProcessEnvironment(environment);
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("[D7-13-child]")});
    // THIS PROCESS MUST PUMP WHILE IT WAITS: Windows hands the clipboard's bytes over by DELAYED RENDERING, asking the
    // copying process for them when the other process reads -- so a parent blocked in `waitForFinished` deadlocks its
    // own child's paste (measured 2026-11-07: the child never finished). A person's first RawPage runs its event loop.
    QElapsedTimer waited;
    waited.start();
    while (child.state() != QProcess::NotRunning && waited.elapsed() < 120000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        child.waitForFinished(50);
    }
    REQUIRE(child.state() == QProcess::NotRunning);
    INFO(child.readAllStandardOutput().toStdString());
    REQUIRE(child.exitCode() == 0);

    // What the other process saved, read by a fresh product here: the two pages after the three, showing their identities.
    Product reread;
    reread.window->openFiles({target});
    QCoreApplication::processEvents();
    ui::DocumentWindow* pasted = reread.window->currentDocument();
    REQUIRE(pasted != nullptr);
    REQUIRE(pasted->pageCount() == 5);
    CHECK(identityAt(reread, pasted, 3).contains(QStringLiteral("P-0005")));
    CHECK(identityAt(reread, pasted, 4).contains(QStringLiteral("P-0007")));
}

TEST_CASE("the second process of [D7-13] pastes the clipboard's pages and saves", "[.][D7-13-child]")
{
    const QString target = qEnvironmentVariable(kChildTarget);
    if (target.isEmpty()) SKIP("run only as [D7-13]'s second process");
    Product product;
    product.window->openFiles({target});
    QCoreApplication::processEvents();
    ui::DocumentWindow* document = product.window->currentDocument();
    REQUIRE(document != nullptr);
    REQUIRE(ui::MainWindow::clipboardPageCount() == 2);   // the OTHER process's copy, read here
    REQUIRE(product.window->pastePages(document, document->pageCount()));
    auto saved = document->save();
    REQUIRE(saved);
}
