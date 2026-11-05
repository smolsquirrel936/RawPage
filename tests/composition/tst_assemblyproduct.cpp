// tst_assemblyproduct -- Stage 7 T7 (`Stage7-Assembly-Plan.md` §6, D7-5, D7-30): extract and split through the
// PRODUCT -- `app::CompositionRoot` and `ui::MainWindow` as `main.cpp` builds them, the shipped backend, its engine and
// the pumps the root installs. Guide cases A-51 (`ui.untitled_assembly`), A-48's product half and control, and A-49
// reached from `page.split` (`Stage7-Assembly-Testing-Guide.md` §5.9).
//
// WHAT IT DOES NOT COVER (convention 19):
// * **No dialog is shown.** Extract and split are answered through their parameterized twins
//   (`ExtractPagesDialog::setAnswerForTesting`, `SplitDocumentDialog::setAnswerForTesting`), and the file name through
//   `MainWindow::setSaveAsAnswerForTesting`; the dialogs' widgets are driven by `tests/ui/tst_pageopsdialogs.cpp` and
//   by the walker (the guide's manual pass).
// * **Every split rule against its labels** is `render.assembler`'s; here one split shows the product's pumps carry a
//   split that reads engine facts to its files.

#include <catch2/catch_test_macros.hpp>

#include "AnnotProduct.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "pdf/ops/DocumentAssembler.h"
#include "ui/dialogs/PageOpsDialogs.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>

#include <memory>

using namespace rawpage;
using annot_product::Product;
using core::ActionId;

namespace {

struct Answers {
    ~Answers()
    {
        ui::ExtractPagesDialog::setAnswerForTesting({});
        ui::SplitDocumentDialog::setAnswerForTesting({});
        ui::MainWindow::setSaveAsAnswerForTesting({});
        pdf::ops::DocumentAssembler::setWriteFaultForTesting({});
    }
};

QStringList filesUnder(const QString& root)
{
    QStringList out;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) out << QDir(root).relativeFilePath(it.next());
    out.sort();
    return out;
}

bool dispatch(Product& product, ActionId id)
{
    const bool done = static_cast<bool>(product.root->services().actions.dispatch(id, core::KeyContext::PageThumbnails));
    QCoreApplication::processEvents();
    return done;
}

} // namespace

// ==========================================================================================
// A-51 -- an extract to one document opens as an untitled tab
// ==========================================================================================

TEST_CASE("ui.untitled_assembly an extract opens untitled, unsaved, and Save asks where", "[assembly][A-51]")
{
    Answers answers;
    Product product;
    product.open("assembly/odd-count-9.pdf");
    ui::DocumentWindow* source = product.document();
    const QStringList before = filesUnder(product.scratch.path());

    ui::ExtractPagesDialog::setAnswerForTesting([](int pageCount, int) -> std::optional<ui::ExtractPagesDialog::Choice> {
        REQUIRE(pageCount == 9);
        return ui::ExtractPagesDialog::Choice{{1, 2}, false, false, QString()};
    });
    REQUIRE(product.root->services().actions.isEnabled(ActionId::PageExtract));
    REQUIRE(dispatch(product, ActionId::PageExtract));

    REQUIRE(product.window->documentCount() == 2);
    ui::DocumentWindow* fresh = product.window->currentDocument();
    REQUIRE(fresh != source);
    CHECK(fresh->isAssembly());
    CHECK(fresh->path().isEmpty());
    CHECK(fresh->title() == ui::catalog::translate(ui::catalog::key::DocumentUntitled));
    CHECK(fresh->pageCount() == 2);
    CHECK(fresh->isDirty());
    CHECK_FALSE(fresh->isEditable());
    CHECK(product.root->services().backend.permissions(fresh->handle())->refusalKey() == QLatin1String("error.edit.unsavedAssembly"));
    CHECK(source->pageCount() == 9);              // an extract changes nothing it was made from
    CHECK_FALSE(source->isDirty());
    CHECK(filesUnder(product.scratch.path()) == before);   // nothing on disk until it is saved

    // Closing asks first: with nobody to ask, the close is refused rather than the document lost.
    const core::Status closed = product.window->closeDocument(product.window->documentCount() - 1);
    REQUIRE_FALSE(closed);
    CHECK(closed.error().messageKey == QLatin1String("error.close.needsDisposition"));
    REQUIRE(product.window->documentCount() == 2);

    // Ctrl+S is a Save As, proposing a name beside the document it came from.
    QString proposed;
    const QString target = QDir(product.scratch.path()).filePath(QStringLiteral("extracted pages.pdf"));
    ui::MainWindow::setSaveAsAnswerForTesting([&](const QString& path) -> std::optional<QString> {
        proposed = path;
        return target;
    });
    REQUIRE(product.root->services().actions.isEnabled(ActionId::FileSave));
    REQUIRE(product.root->services().actions.dispatch(ActionId::FileSave, core::KeyContext::Viewer));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    CHECK(QFileInfo(proposed).absolutePath() == QFileInfo(product.openedPath).absolutePath());
    CHECK(QFileInfo(proposed).fileName().endsWith(QLatin1String(".pdf")));

    // The saved file, opened in the untitled tab's place: an ordinary document, editable, of the two pages.
    REQUIRE(QFileInfo::exists(target));
    REQUIRE(product.window->documentCount() == 2);
    ui::DocumentWindow* saved = product.window->currentDocument();
    REQUIRE(saved != nullptr);
    CHECK_FALSE(saved->isAssembly());
    CHECK(QFileInfo(saved->path()).absoluteFilePath() == QFileInfo(target).absoluteFilePath());
    CHECK(saved->isEditable());
    CHECK_FALSE(saved->isDirty());
    CHECK(saved->pageCount() == 2);
}

// ==========================================================================================
// A-48 -- extract and delete: the pages go only once the new document is written and verified
// ==========================================================================================

TEST_CASE("extract and delete removes the pages in one undo step after the new document is made", "[assembly][extract][A-48]")
{
    Answers answers;
    Product product;
    product.open("assembly/odd-count-9.pdf");
    ui::DocumentWindow* source = product.document();
    ui::ExtractPagesDialog::setAnswerForTesting([](int, int) -> std::optional<ui::ExtractPagesDialog::Choice> {
        return ui::ExtractPagesDialog::Choice{{1, 4}, false, true, QString()};
    });
    REQUIRE(dispatch(product, ActionId::PageExtract));
    REQUIRE(product.window->documentCount() == 2);
    CHECK(product.window->currentDocument()->pageCount() == 2);
    CHECK(source->pageCount() == 7);
    CHECK(source->isDirty());
    // One step: one undo puts both pages back.
    REQUIRE(source->journal()->undo());
    CHECK(source->pageCount() == 9);
    CHECK_FALSE(source->journal()->canUndo());
}

TEST_CASE("extract and delete a planted write failure leaves the source untouched and no file", "[assembly][extract][A-48]")
{
    Answers answers;
    Product product;
    product.open("assembly/odd-count-9.pdf");
    ui::DocumentWindow* source = product.document();
    const QString folder = QDir(product.scratch.path()).filePath(QStringLiteral("pages"));
    ui::ExtractPagesDialog::setAnswerForTesting([folder](int, int) -> std::optional<ui::ExtractPagesDialog::Choice> {
        return ui::ExtractPagesDialog::Choice{{1, 4, 6}, true, true, folder};
    });
    // THE CONTROL: the third file's bytes corrupted between its build and its write -- two files are on disk by then.
    int built = 0;
    pdf::ops::DocumentAssembler::setWriteFaultForTesting([&built](std::string& bytes) {
        if (++built == 3) bytes[bytes.find("stream\n") + 8] ^= 0x20;
    });
    REQUIRE(dispatch(product, ActionId::PageExtract));
    CHECK(built == 3);
    CHECK(source->pageCount() == 9);
    CHECK_FALSE(source->isDirty());
    CHECK_FALSE(source->journal()->canUndo());
    CHECK(filesUnder(folder).isEmpty());
    CHECK(product.window->documentCount() == 1);

    // And without the fault, the same choice writes three files and deletes the three pages.
    pdf::ops::DocumentAssembler::setWriteFaultForTesting({});
    REQUIRE(dispatch(product, ActionId::PageExtract));
    CHECK(filesUnder(folder).size() == 3);
    CHECK(source->pageCount() == 6);
}

// ==========================================================================================
// A-49 through page.split -- the product's pumps carry a split that reads the engine
// ==========================================================================================

TEST_CASE("page.split at blank pages writes its files through the product's pumps", "[assembly][split][A-49]")
{
    Answers answers;
    Product product;
    product.open("assembly/split-blanks.pdf");
    const QString folder = QDir(product.scratch.path()).filePath(QStringLiteral("parts"));
    ui::SplitDocumentDialog::setAnswerForTesting([folder](int) -> std::optional<ui::SplitDocumentDialog::Choice> {
        ui::SplitDocumentDialog::Choice choice;
        choice.method = core::SplitMethod::BlankPages;
        choice.folder = folder;
        return choice;
    });
    REQUIRE(product.root->services().actions.dispatch(ActionId::PageSplit, core::KeyContext::Global));
    QElapsedTimer timer;
    timer.start();
    while (product.window->isSplitting() && timer.elapsed() < 60000) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    REQUIRE_FALSE(product.window->isSplitting());
    const QStringList written = filesUnder(folder);
    REQUIRE(written.size() == 3);
    CHECK(written.front().startsWith(QFileInfo(product.openedPath).completeBaseName()));
    CHECK_FALSE(product.document()->isDirty());   // a split changes nothing it was made from
}
