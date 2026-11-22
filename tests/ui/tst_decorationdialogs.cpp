// tst_decorationdialogs -- Stage 7 T14 (`Stage7-Assembly-Plan.md` §6, D7-14, D7-22, D7-44): the header/footer dialog, its
// preview and profiles, the Bates dialog, and the five actions T14 registers, through the window over the fake backend
// (`Stage7-Assembly-Testing-Guide.md` §5.16 -- the surface halves of A-92, A-93, A-96 and A-97).
//
// WHAT IT DOES NOT COVER (convention 19): the PAGES. The fake sink records the decorations it is told, so what is drawn,
// where, and what a reader extracts are `tests/render/tst_decorationengine`'s and the golden suite's; and the preview is
// graded on the lines it is handed (the layout's), not on its pixels -- M-07 compares it with the result by eye.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/doc/HeaderFooter.h"
#include "core/doc/PageTree.h"
#include "core/settings/ProfileStore.h"
#include "core/settings/Settings.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/HeaderFooterDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>

#include <memory>
#include <optional>

using namespace rawpage;
using core::ActionId;

namespace {

core::PageTree pagesOf(int count)
{
    std::vector<core::Page> pages;
    for (int i = 0; i < count; ++i) {
        core::Page page(i, core::PdfRect{core::PdfPoint{0, 0}, core::PdfSize{612, 792}});
        if (i == 1) page.setRotation(90);
        pages.push_back(page);
    }
    return core::PageTree(std::move(pages));
}

core::FieldValues values()
{
    return core::FieldValues{QDate(2026, 11, 23), QTime(10, 0), QStringLiteral("brief.pdf"), QStringLiteral("C:\\matter\\brief.pdf")};
}

QString textOf(const core::DecorationLine& line)
{
    QString out;
    for (const core::TypedRun& run : line.runs) out += run.text;
    return out;
}

void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

struct Answers {
    ~Answers()
    {
        ui::HeaderFooterDialog::setAnswerForTesting({});
        ui::BatesDialog::setAnswerForTesting({});
    }
};

QString fakePdf(const QTemporaryDir& directory, const QString& name)
{
    const QString path = QFileInfo(QDir(directory.path()).filePath(name)).absoluteFilePath();
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("%PDF-1.7\n% a fake page\n");
    return path;
}

} // namespace

TEST_CASE("decorate the header/footer dialog reads every field into a spec and previews each line where the layout puts it", "[decorate][A-92]")
{
    const core::PageTree pages = pagesOf(3);
    core::HeaderFooterSpec initial;
    initial.areas[4] = QStringLiteral("Page <<page>> of <<count>>");
    ui::HeaderFooterDialog dialog(ui::HeaderFooterDialog::Mode::Add, pages, values(), initial, nullptr, QString());
    driveLayout(&dialog);
    CHECK(dialog.areaField(core::HeaderFooterArea::FooterCentre)->text() == initial.areas[4]);
    CHECK_FALSE(dialog.profileBox()->isEnabled());   // no store, no profiles

    dialog.areaField(core::HeaderFooterArea::HeaderLeft)->setText(QStringLiteral("Draft"));
    // `Insert` types a field into the area that last had the focus -- an EMPTY one clicked into included, which moves no
    // cursor (the dialog first followed the cursor, and typed into the wrong area: found by this case).
    dialog.activateWindow();
    dialog.areaField(core::HeaderFooterArea::HeaderLeft)->setFocus();
    QCoreApplication::processEvents();
    dialog.areaField(core::HeaderFooterArea::HeaderRight)->setFocus();
    QCoreApplication::processEvents();
    dialog.fieldBox()->setCurrentIndex(dialog.fieldBox()->findData(QStringLiteral("<<date>>")));
    dialog.insertButton()->click();
    CHECK(dialog.areaField(core::HeaderFooterArea::HeaderRight)->text() == QStringLiteral("<<date>>"));
    dialog.sizeField()->setValue(14);
    dialog.setColour(QColor(255, 0, 0));
    dialog.underlineBox()->setChecked(true);
    dialog.marginField(0)->setValue(20);
    dialog.marginField(3)->setValue(30);
    dialog.shrinkBox()->setChecked(true);
    dialog.parityBox()->setCurrentIndex(dialog.parityBox()->findData(static_cast<int>(core::PageParity::Odd)));

    const auto chosen = dialog.choice();
    REQUIRE(chosen);
    const core::HeaderFooterSpec& spec = chosen.value().spec;
    CHECK(spec.areas[0] == QStringLiteral("Draft"));
    CHECK(spec.areas[2] == QStringLiteral("<<date>>"));
    CHECK(spec.size == 14);
    CHECK(spec.colour == std::array<double, 3>{1, 0, 0});
    CHECK(spec.underline);
    CHECK(spec.top == 20);
    CHECK(spec.right == 30);
    CHECK(spec.shrink);
    CHECK(spec.parity == core::PageParity::Odd);

    // THE PREVIEW: page 1 carries the three lines, expanded for it; page 2 (even) is not reached and is drawn bare.
    dialog.previewPageField()->setValue(1);
    REQUIRE(dialog.preview()->decoration());
    std::vector<QString> texts;
    for (const core::DecorationLine& line : dialog.preview()->decoration()->lines) texts.push_back(textOf(line));
    CHECK(texts == std::vector<QString>{QStringLiteral("Draft"), QStringLiteral("2026-11-23"), QStringLiteral("Page 1 of 3")});
    CHECK(dialog.preview()->decoration()->wrap->matrix[0] < 1.0);   // shrunk
    dialog.previewPageField()->setValue(2);
    CHECK_FALSE(dialog.preview()->decoration());
    dialog.previewPageField()->setValue(3);
    REQUIRE(dialog.preview()->decoration());
    CHECK(textOf(dialog.preview()->decoration()->lines.back()) == QStringLiteral("Page 3 of 3"));
}

TEST_CASE("decoration.characters the dialog refuses a Cyrillic letter by name before any command exists", "[decorate][A-96]")
{
    const core::PageTree pages = pagesOf(2);
    ui::HeaderFooterDialog dialog(ui::HeaderFooterDialog::Mode::Add, pages, values(), core::HeaderFooterSpec{}, nullptr, QString());
    driveLayout(&dialog);
    dialog.areaField(core::HeaderFooterArea::HeaderCentre)->setText(QStringLiteral("Резюме"));
    const auto refused = dialog.choice();
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.decorate.characterRefused"));
    CHECK(refused.error().context.contains(QStringLiteral("U+0420")));
    dialog.accept();
    CHECK(dialog.result() != QDialog::Accepted);   // still open
    CHECK(dialog.refusalLabel()->isVisible());
    // A FIELD that expands to one is refused here too: the file's name.
    core::FieldValues cyrillic = values();
    cyrillic.fileName = QStringLiteral("Резюме.pdf");
    ui::HeaderFooterDialog named(ui::HeaderFooterDialog::Mode::Add, pages, cyrillic, core::HeaderFooterSpec{}, nullptr, QString());
    named.areaField(core::HeaderFooterArea::HeaderCentre)->setText(QStringLiteral("<<file>>"));
    CHECK(named.choice().error().messageKey == QLatin1String("error.decorate.characterRefused"));
}

TEST_CASE("decorate profiles saved from the dialog, chosen to fill it, and deleted", "[decorate][A-97]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    core::ProfileStore store(QDir(scratch.path()).filePath(QStringLiteral("stamps")));
    store.load();
    const core::PageTree pages = pagesOf(2);
    ui::HeaderFooterDialog dialog(ui::HeaderFooterDialog::Mode::Add, pages, values(), core::HeaderFooterSpec{}, &store, QString());
    driveLayout(&dialog);
    dialog.areaField(core::HeaderFooterArea::FooterRight)->setText(QStringLiteral("<<page>>"));
    dialog.sizeField()->setValue(9);
    dialog.profileNameField()->setText(QStringLiteral("Court"));
    dialog.saveProfileButton()->click();
    CHECK(store.profileNames(core::ProfileKind::HeaderFooter) == QStringList{QStringLiteral("Court")});
    CHECK(dialog.profileBox()->currentData().toString() == QStringLiteral("Court"));
    CHECK(dialog.choice().value().profile == QStringLiteral("Court"));

    // Fields changed, then the profile chosen again: what was saved comes back.
    dialog.areaField(core::HeaderFooterArea::FooterRight)->setText(QStringLiteral("changed"));
    dialog.sizeField()->setValue(30);
    const int at = dialog.profileBox()->findData(QStringLiteral("Court"));
    REQUIRE(at > 0);
    dialog.profileBox()->setCurrentIndex(at);
    emit dialog.profileBox()->activated(at);
    CHECK(dialog.areaField(core::HeaderFooterArea::FooterRight)->text() == QStringLiteral("<<page>>"));
    CHECK(dialog.sizeField()->value() == 9);

    dialog.deleteProfileButton()->click();
    CHECK(store.profileNames(core::ProfileKind::HeaderFooter).isEmpty());
    CHECK(dialog.profileBox()->count() == 1);   // (none)
}

TEST_CASE("decorate the Bates dialog reads the documents chosen in their order, the format and the position", "[decorate][A-93]")
{
    std::vector<ui::BatesDialog::Document> documents{{QStringLiteral("merge-a.pdf"), 3, true},
                                                     {QStringLiteral("merge-b.pdf"), 4, true},
                                                     {QStringLiteral("locked.pdf"), 2, false}};
    ui::BatesDialog dialog(documents, 0, core::BatesFormat{QStringLiteral("CASE-"), QString(), 6, 1}, nullptr);
    driveLayout(&dialog);
    CHECK(dialog.sampleLabel()->text().contains(QStringLiteral("CASE-000001")));
    CHECK(dialog.documentList()->item(2)->flags() == Qt::NoItemFlags);   // not editable: never chosen
    dialog.documentList()->item(1)->setCheckState(Qt::Checked);
    // b before a: b moved up.
    dialog.documentList()->setCurrentRow(1);
    dialog.upButton()->click();
    dialog.positionBox()->setCurrentIndex(dialog.positionBox()->findData(static_cast<int>(core::HeaderFooterArea::HeaderRight)));
    dialog.suffixField()->setText(QStringLiteral("-X"));
    auto chosen = dialog.choice();
    REQUIRE(chosen);
    CHECK(chosen.value().order == std::vector<std::size_t>{1, 0});
    CHECK(chosen.value().spec.areas[2] == QStringLiteral("<<bates>>"));
    CHECK(chosen.value().spec.bates == core::BatesFormat{QStringLiteral("CASE-"), QStringLiteral("-X"), 6, 1});

    // Past the digits for 7 pages from 999,995: refused before anything runs.
    dialog.digitsField()->setValue(6);
    dialog.startField()->setValue(999995);
    CHECK(dialog.choice().error().messageKey == QLatin1String("error.decorate.batesOverflow"));
    for (int row = 0; row < dialog.documentList()->count(); ++row) dialog.documentList()->item(row)->setCheckState(Qt::Unchecked);
    CHECK(dialog.choice().error().messageKey == QLatin1String("error.decorate.noDocuments"));
}

TEST_CASE("decorate add, update, remove and Bates run from their actions, each one command, and are disabled with nobody to ask", "[decorate]")
{
    uitest::ShellFixture fixture;
    fixture.backend.editable = true;
    fixture.backend.pagesPerDocument = 4;
    auto window = std::make_unique<ui::MainWindow>(fixture.services());
    window->resize(1100, 850);
    window->openFiles({fakePdf(fixture.directory, QStringLiteral("first.pdf")), fakePdf(fixture.directory, QStringLiteral("second.pdf"))});
    driveLayout(window.get());
    REQUIRE(window->documentCount() == 2);
    fixture.pumpTiles();

    // NOBODY TO ASK (convention 16): the two dialogs' actions are disabled; the removals are not.
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::HeaderFooterAdd));
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::HeaderFooterUpdate));
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::BatesAdd));
    CHECK(fixture.actions.isEnabled(ActionId::HeaderFooterRemove));
    CHECK(fixture.actions.isEnabled(ActionId::BatesRemove));

    Answers answers;
    std::optional<core::HeaderFooterSpec> startedFrom;
    ui::HeaderFooterDialog::setAnswerForTesting([&startedFrom](ui::HeaderFooterDialog::Mode mode, const core::HeaderFooterSpec& initial) {
        if (mode == ui::HeaderFooterDialog::Mode::Update) startedFrom = initial;
        core::HeaderFooterSpec spec = initial;
        spec.areas[1] = mode == ui::HeaderFooterDialog::Mode::Add ? QStringLiteral("Added") : QStringLiteral("Updated");
        return std::optional<ui::HeaderFooterDialog::Choice>(ui::HeaderFooterDialog::Choice{spec, QString()});
    });
    ui::DocumentWindow* front = window->currentDocument();
    REQUIRE(front != nullptr);
    uitest::FakeSink* sink = fixture.backend.sinkFor(front->handle());
    REQUIRE(fixture.actions.dispatch(ActionId::HeaderFooterAdd, core::KeyContext::Global));
    REQUIRE(sink->decorationPages.size() == 1);
    CHECK(sink->decorationPages.begin()->second == std::vector<int>{0, 1, 2, 3});
    CHECK(textOf(sink->lastDecorations.front().lines.front()) == QStringLiteral("Added"));

    // UPDATE starts from what was applied, and replaces it in ONE step.
    REQUIRE(fixture.actions.dispatch(ActionId::HeaderFooterUpdate, core::KeyContext::Global));
    REQUIRE(startedFrom);
    CHECK(startedFrom->areas[1] == QStringLiteral("Added"));
    REQUIRE(sink->decorationPages.size() == 1);
    CHECK(textOf(sink->lastDecorations.front().lines.front()) == QStringLiteral("Updated"));
    REQUIRE(front->journal()->undo());
    REQUIRE(sink->decorationPages.size() == 1);   // the update undone: the first header back
    REQUIRE(front->journal()->redo());

    REQUIRE(fixture.actions.dispatch(ActionId::HeaderFooterRemove, core::KeyContext::Global));
    CHECK(sink->decorationPages.empty());
    // A second remove finds nothing: refused, and no undo step.
    const int steps = [&] {
        int n = 0;
        while (front->journal()->canUndo() && front->journal()->undo()) ++n;
        for (int i = 0; i < n; ++i) front->journal()->redo();
        return n;
    }();
    CHECK(steps == 3);
    (void)fixture.actions.dispatch(ActionId::HeaderFooterRemove, core::KeyContext::Global);
    CHECK(sink->decorationPages.empty());

    // BATES across both documents, the second first: one command in EACH journal, consecutive numbers.
    ui::BatesDialog::setAnswerForTesting([](const std::vector<ui::BatesDialog::Document>& documents, std::size_t) {
        REQUIRE(documents.size() == 2);
        ui::BatesDialog::Choice choice;
        choice.order = {1, 0};
        choice.spec.areas[5] = QStringLiteral("<<bates>>");
        choice.spec.bates = core::BatesFormat{QStringLiteral("X-"), QString(), 4, 7};
        return std::optional<ui::BatesDialog::Choice>(choice);
    });
    REQUIRE(fixture.actions.dispatch(ActionId::BatesAdd, core::KeyContext::Global));
    uitest::FakeSink* firstTab = fixture.backend.sinkFor(window->documentAt(0)->handle());
    uitest::FakeSink* secondTab = fixture.backend.sinkFor(window->documentAt(1)->handle());
    REQUIRE(secondTab->lastDecorations.size() == 4);
    CHECK(textOf(secondTab->lastDecorations.front().lines.front()) == QStringLiteral("X-0007"));
    CHECK(textOf(secondTab->lastDecorations.back().lines.front()) == QStringLiteral("X-0010"));
    REQUIRE(firstTab->lastDecorations.size() == 4);
    CHECK(textOf(firstTab->lastDecorations.front().lines.front()) == QStringLiteral("X-0011"));
    CHECK(textOf(firstTab->lastDecorations.back().lines.front()) == QStringLiteral("X-0014"));
    CHECK(window->lastBatesReport().contains(QStringLiteral("X-0007")));
    CHECK(window->lastBatesReport().contains(QStringLiteral("X-0014")));
    CHECK(window->documentAt(0)->journal()->canUndo());

    // bates.remove in the document in front takes ITS numbers only.
    REQUIRE(window->currentDocument() == window->documentAt(1));
    REQUIRE(fixture.actions.dispatch(ActionId::BatesRemove, core::KeyContext::Global));
    CHECK(secondTab->decorationPages.empty());
    CHECK(firstTab->decorationPages.size() == 1);

    window.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
