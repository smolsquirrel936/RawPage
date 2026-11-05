// Stage 7 T7 (`Stage7-Assembly-Plan.md` §6, D7-30) -- the extract and split dialogs' widgets, and the two commands'
// availability. Guide A-48 and A-49's dialog halves (`Stage7-Assembly-Testing-Guide.md` §5.9).
//
// WHAT THIS SUITE CANNOT SEE (convention 19): what an extract or a split WRITES -- `uitest::FakeBackend`'s snapshot
// records what it is asked and writes nothing; the files are `unit.assembler`'s and `render.assembler`'s, and the whole
// path through the shipped backend is `ui.composition`'s `tst_assemblyproduct.cpp`. Neither dialog is shown modally:
// each is constructed and driven here, and `ask` answered through its twin.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/PageOpsDialogs.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/StatusBar.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QSpinBox>

#include <memory>
#include <string>

using namespace rawpage;
using core::ActionId;

namespace {

struct Answers {
    ~Answers()
    {
        ui::ExtractPagesDialog::setAnswerForTesting({});
        ui::SplitDocumentDialog::setAnswerForTesting({});
    }
};

struct Shell {
    uitest::ShellFixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow* document = nullptr;

    explicit Shell(bool editable = true)
    {
        fixture.backend.editable = editable;
        fixture.backend.pagesPerDocument = 5;
        const QString path = QFileInfo(QDir(fixture.directory.path()).filePath(QStringLiteral("doc.pdf"))).absoluteFilePath();
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("%PDF-1.7\n% a fake page\n");
        file.close();
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->openFiles({path});
        QCoreApplication::processEvents();
        document = window->currentDocument();
        REQUIRE(document != nullptr);
    }
    ~Shell()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};

// A refusal's key, or "(success)" -- a choice that wrongly succeeds fails its CHECK instead of reading `error()` of a
// success.
template <typename T>
std::string keyOf(const core::Result<T>& result)
{
    return result ? std::string("(success)") : std::string(result.error().messageKey.data(), static_cast<std::size_t>(result.error().messageKey.size()));
}

} // namespace

TEST_CASE("pageops the extract dialog reads a range and keeps itself open on a bad one", "[pageops][extract][A-48]")
{
    ui::ExtractPagesDialog dialog(9, 3, true, QStringLiteral("C:/out"));
    CHECK(dialog.rangeField()->text() == QStringLiteral("4"));   // the current page, as a person counts it
    dialog.rangeField()->setText(QStringLiteral("2-4, 9"));
    dialog.deleteBox()->setChecked(true);
    auto chosen = dialog.choice();
    REQUIRE(chosen);
    CHECK(chosen.value() == ui::ExtractPagesDialog::Choice{{1, 2, 3, 8}, false, true, QStringLiteral("C:/out")});

    dialog.rangeField()->setText(QStringLiteral("7-3"));
    dialog.accept();
    CHECK(dialog.result() != QDialog::Accepted);   // refused: the dialog stays, and says why
    CHECK_FALSE(dialog.refusalLabel()->isHidden());
    CHECK_FALSE(dialog.refusalLabel()->text().isEmpty());

    // Per page needs a folder; with none it is refused in the dialog too.
    dialog.rangeField()->setText(QStringLiteral("1"));
    dialog.filePerPageButton()->setChecked(true);
    dialog.folderField()->clear();
    CHECK(keyOf(dialog.choice()) == std::string("error.assembly.noFolder"));

    // A document this build will not edit: the delete box is greyed and never chosen.
    ui::ExtractPagesDialog readOnly(9, 0, false, QString());
    readOnly.deleteBox()->setChecked(true);
    CHECK_FALSE(readOnly.deleteBox()->isEnabled());
    CHECK_FALSE(readOnly.choice().value().deleteAfter);
}

TEST_CASE("pageops the split dialog reads each rule and its field", "[pageops][split][A-49]")
{
    ui::SplitDocumentDialog dialog(900, 2, QStringLiteral("C:/out"));
    dialog.pagesField()->setValue(100);
    auto every = dialog.choice();
    REQUIRE(every);
    CHECK(every.value().method == core::SplitMethod::EveryN);
    CHECK(every.value().pagesPerFile == 100);
    CHECK(every.value().folder == QStringLiteral("C:/out"));

    dialog.methodButton(core::SplitMethod::BookmarkLevel)->setChecked(true);
    CHECK(dialog.levelField()->isEnabled());
    CHECK_FALSE(dialog.pagesField()->isEnabled());
    CHECK(dialog.choice().value().method == core::SplitMethod::BookmarkLevel);

    dialog.methodButton(core::SplitMethod::TextPattern)->setChecked(true);
    CHECK(keyOf(dialog.choice()) == std::string("error.search.emptyPattern"));
    dialog.patternField()->setText(QStringLiteral("^Part [0-9]+:"));
    dialog.regexBox()->setChecked(true);
    CHECK(dialog.choice().value().regex);

    dialog.methodButton(core::SplitMethod::MaxSize)->setChecked(true);
    dialog.sizeField()->setText(QStringLiteral("nothing"));
    CHECK(keyOf(dialog.choice()) == std::string("error.split.badSize"));

    // No outline: the two bookmark rules are unavailable.
    ui::SplitDocumentDialog flat(10, 0, QString());
    CHECK_FALSE(flat.methodButton(core::SplitMethod::TopLevelBookmarks)->isEnabled());
    CHECK_FALSE(flat.methodButton(core::SplitMethod::BookmarkLevel)->isEnabled());
    CHECK(keyOf(flat.choice()) == std::string("error.assembly.noFolder"));
}

TEST_CASE("pageops extract and split are disabled where nobody can answer", "[pageops][A-48][A-49]")
{
    Answers answers;
    Shell shell;
    // Offscreen with no twin installed: nobody to ask (convention 16).
    CHECK_FALSE(shell.fixture.actions.isEnabled(ActionId::PageExtract));
    CHECK_FALSE(shell.fixture.actions.isEnabled(ActionId::PageSplit));
    ui::ExtractPagesDialog::setAnswerForTesting([](int, int) { return std::nullopt; });
    ui::SplitDocumentDialog::setAnswerForTesting([](int) { return std::nullopt; });
    CHECK(shell.fixture.actions.isEnabled(ActionId::PageExtract));
    CHECK(shell.fixture.actions.isEnabled(ActionId::PageSplit));
}

TEST_CASE("pageops extract and delete deletes nothing when the extract is refused", "[pageops][extract][A-48]")
{
    Answers answers;
    Shell shell;
    shell.fixture.backend.writeFails = "error.assembly.writeFailed";
    const QString folder = QDir(shell.fixture.directory.path()).filePath(QStringLiteral("pages"));
    ui::ExtractPagesDialog::setAnswerForTesting([folder](int, int) -> std::optional<ui::ExtractPagesDialog::Choice> {
        return ui::ExtractPagesDialog::Choice{{0, 2}, true, true, folder};
    });
    REQUIRE(shell.fixture.actions.dispatch(ActionId::PageExtract, core::KeyContext::PageThumbnails));
    REQUIRE(shell.fixture.backend.assemblyWrites.size() == 1);
    CHECK(shell.fixture.backend.assemblyWrites.front().folder == folder);
    CHECK(shell.fixture.backend.assemblyWrites.front().outputs.size() == 2);
    CHECK_FALSE(shell.document->journal()->canUndo());   // no delete ran
    CHECK_FALSE(shell.document->isDirty());
    // The other half -- a write that succeeds followed by ONE delete, undone in one step -- needs a sink that applies a
    // page delete, which this fake's does not: it is `ui.composition`'s (`tst_assemblyproduct.cpp`, A-48).
}
