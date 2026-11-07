// Stage 7 T6 (`Stage7-Assembly-Plan.md` §6, D7-25) -- the shell's half of Save As that rewrites: Save a Copy, the
// question before a rewrite breaks a signature, and `documents/saveMode`. Guide cases A-125, A-126's question half and
// A-124's `Ctrl+S` half (`Stage7-Assembly-Testing-Guide.md` §5.8).
//
// WHAT THIS SUITE CANNOT SEE (convention 19):
// * **No real writer.** `uitest::FakeBackend` writes the opened bytes and a marker for either form and RECORDS the form
//   it was asked for (`forms`); what a rewrite writes, and that it verifies, are `unit.fullwriter` and
//   `render.fullwriter`'s. The cases here grade what the SHELL asks for and what it does with the tab.
// * **No modal dialog is shown.** The file name and the signed question are answered through their parameterized
//   twins (`MainWindow::setSaveAsAnswerForTesting`, `setSignedRewriteAnswerForTesting`); the question's widget is the
//   walker's, M-13's Save As steps.
// * The signature count is the fake's (`FakeBackend::signatures`); the first-party reading of a real `/AcroForm` is
//   `unit.fullwriter.signatures` and `unit.fullwriter.signed`'s.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/doc/SaveMode.h"
#include "core/settings/SettingsSchema.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/RotatePagesDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLayout>

#include <memory>

using namespace rawpage;
using core::ActionId;

namespace {

struct Fixture : uitest::ShellFixture {
    Fixture() { backend.editable = true; }
};

struct Answers {
    ~Answers()
    {
        ui::RotatePagesDialog::setAnswerForTesting({});
        ui::MainWindow::setSaveAsAnswerForTesting({});
        ui::MainWindow::setSignedRewriteAnswerForTesting({});
    }
};

QByteArray readAll(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

// A shown window with one editable, rotated -- so dirty -- document.
struct Editor {
    Fixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow* document = nullptr;
    QString path;

    Editor()
    {
        fixture.backend.pagesPerDocument = 2;
        path = QDir(fixture.directory.path()).filePath(QStringLiteral("edit.pdf"));
        {
            QFile file(path);
            REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            file.write("%PDF-1.7\n% a fake page\n");
        }
        path = QFileInfo(path).absoluteFilePath();
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1000, 800);
        window->openFiles({path});
        window->show();
        QCoreApplication::processEvents();
        window->raiseToForeground();
        document = window->currentDocument();
        REQUIRE(document != nullptr);
        REQUIRE(document->journal() != nullptr);
        ui::RotatePagesDialog::setAnswerForTesting([] { return ui::RotatePagesDialog::Choice{90, ui::RotatePagesDialog::Pages::Current}; });
        REQUIRE(fixture.actions.dispatch(ActionId::PageRotate, core::KeyContext::Viewer));
        REQUIRE(document->isDirty());
    }
    ~Editor()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    QString target(const char* name) const { return QDir(fixture.directory.path()).filePath(QString::fromLatin1(name)); }
};

} // namespace

TEST_CASE("Save a Copy writes the rewrite elsewhere and the tab keeps its file, title and changes",
          "[saveacopy][ui.save_a_copy][A-125]")
{
    Answers answers;
    Editor editor;
    const QString copy = editor.target("copy.pdf");
    ui::MainWindow::setSaveAsAnswerForTesting([&](const QString&) -> std::optional<QString> { return copy; });
    const QString title = editor.document->title();
    const QByteArray own = readAll(editor.path);

    REQUIRE(editor.fixture.actions.dispatch(ActionId::FileSaveACopy, core::KeyContext::Viewer));
    REQUIRE(QFile::exists(copy));
    REQUIRE_FALSE(editor.fixture.backend.forms.empty());
    CHECK(editor.fixture.backend.forms.back() == core::SaveForm::Rewrite);
    CHECK(editor.document->path() == editor.path);
    CHECK(editor.document->title() == title);
    CHECK(editor.document->isDirty());
    const bool ownUntouched = readAll(editor.path) == own;
    CHECK(ownUntouched);

    // A later Save writes the tab's OWN file, and appends; the copy is left as it was.
    const QByteArray copied = readAll(copy);
    REQUIRE(editor.fixture.actions.dispatch(ActionId::FileSave, core::KeyContext::Viewer));
    CHECK(editor.fixture.backend.forms.back() == core::SaveForm::Append);
    CHECK_FALSE(editor.document->isDirty());
    const bool ownWritten = readAll(editor.path) != own;
    const bool copyKept = readAll(copy) == copied;
    CHECK(ownWritten);
    CHECK(copyKept);
}

TEST_CASE("Save As asks before a rewrite breaks a signature, and each answer writes what it says",
          "[saveacopy][ui.signed_question][A-126]")
{
    Answers answers;
    Editor editor;
    editor.fixture.backend.signatures[editor.document->handle().id] = 2;
    int asked = 0;
    int count = 0;
    core::SignedRewriteAnswer answer = core::SignedRewriteAnswer::Cancel;
    ui::MainWindow::setSignedRewriteAnswerForTesting([&](int signatures) {
        ++asked;
        count = signatures;
        return answer;
    });
    const char* names[] = {"cancelled.pdf", "kept.pdf", "rewritten.pdf"};
    QString target;
    ui::MainWindow::setSaveAsAnswerForTesting([&](const QString&) -> std::optional<QString> { return target; });

    // Cancel: nothing written, the tab as it was, and no failure said.
    target = editor.target(names[0]);
    const std::size_t writes = editor.fixture.backend.forms.size();
    CHECK_FALSE(editor.window->saveDocumentACopy(editor.document));
    CHECK(asked == 1);
    CHECK(count == 2);
    CHECK(editor.fixture.backend.forms.size() == writes);
    CHECK_FALSE(QFile::exists(target));

    // Keep the signatures: appended.
    answer = core::SignedRewriteAnswer::KeepSignatures;
    target = editor.target(names[1]);
    REQUIRE(editor.window->saveDocumentACopy(editor.document));
    CHECK(asked == 2);
    CHECK(editor.fixture.backend.forms.back() == core::SaveForm::Append);

    // Rewrite: rewritten.
    answer = core::SignedRewriteAnswer::Rewrite;
    target = editor.target(names[2]);
    REQUIRE(editor.window->saveDocumentAs(editor.document));
    CHECK(asked == 3);
    CHECK(editor.fixture.backend.forms.back() == core::SaveForm::Rewrite);
    CHECK(editor.document->path() == QFileInfo(target).absoluteFilePath());

    // Nobody to ask -- no answer installed, and offscreen nobody to show a dialog to: kept, never asked.
    ui::MainWindow::setSignedRewriteAnswerForTesting({});
    REQUIRE(editor.fixture.actions.dispatch(ActionId::PageRotate, core::KeyContext::Viewer));
    target = editor.target("headless.pdf");
    REQUIRE(editor.window->saveDocumentAs(editor.document));
    CHECK(editor.fixture.backend.forms.back() == core::SaveForm::Append);
    CHECK(asked == 3);
}

TEST_CASE("documents/saveMode full makes Save rewrite, and an unsigned document is never asked",
          "[saveacopy][ui.save_mode][A-124]")
{
    Answers answers;
    Editor editor;
    int asked = 0;
    ui::MainWindow::setSignedRewriteAnswerForTesting([&](int) {
        ++asked;
        return core::SignedRewriteAnswer::KeepSignatures;
    });
    // The control first: incremental, the default, appends.
    CHECK(editor.fixture.settings.get(core::keys::documents::saveMode) == core::SaveMode::Incremental);
    REQUIRE(editor.window->saveDocument(editor.document));
    CHECK(editor.fixture.backend.forms.back() == core::SaveForm::Append);

    editor.fixture.settings.set(core::keys::documents::saveMode, core::SaveMode::Full);
    REQUIRE(editor.fixture.actions.dispatch(ActionId::PageRotate, core::KeyContext::Viewer));
    REQUIRE(editor.window->saveDocument(editor.document));
    CHECK(editor.fixture.backend.forms.back() == core::SaveForm::Rewrite);
    CHECK_FALSE(editor.document->isDirty());
    CHECK(asked == 0);
}
