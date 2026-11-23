// tst_impositiondialogs -- Stage 7 T16 (`Stage7-Assembly-Plan.md` §6, D7-8, D7-22, D7-46): the imposition dialog, its
// preview and its profiles; *Booklet* and *N-up* run from their actions over the fake backend, the result opened as an
// untitled tab (`Stage7-Assembly-Testing-Guide.md` §5.18 -- the surface halves of A-97 and A-102).
//
// WHAT IT DOES NOT COVER (convention 19): the SHEETS. The fake backend's snapshot records the spec it is asked to impose
// and answers the real plan over Letter pages with bytes that draw nothing; what is drawn is `tst_impositionops`',
// `tst_impositionengine`'s and the golden suite's. The preview is graded on the plan it is handed, not on its pixels --
// M-04 looks at it.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/doc/Imposition.h"
#include "core/doc/PaperSize.h"
#include "core/settings/ProfileStore.h"
#include "core/settings/Settings.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/ImpositionDialog.h"
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
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>

#include <memory>
#include <optional>
#include <vector>

using namespace rawpage;
using core::ActionId;

namespace {

constexpr core::PdfSize kLetter{612.0, 792.0};

void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

struct Answers {
    ~Answers() { ui::ImpositionDialog::setAnswerForTesting({}); }
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

TEST_CASE("imposition the dialog reads every field into a spec and previews the plan side by side", "[imposition][A-102]")
{
    core::ImpositionSpec initial;
    initial.layout = core::ImpositionLayout::Booklet;
    initial.binding = core::ImpositionBinding::Right;
    initial.creep = 0.5;
    initial.sheet = core::PdfSize{842.0, 1191.0};   // A3, either way round
    initial.orientation = core::SheetOrientation::Landscape;
    initial.margin = 30.0;
    initial.gutter = 4.0;
    initial.duplex = core::DuplexFlip::LongEdge;
    initial.cropMarks = initial.colourBar = initial.frames = true;
    const std::vector<core::PdfSize> shown(17, kLetter);
    ui::ImpositionDialog dialog(initial, shown, nullptr, QString());
    driveLayout(&dialog);
    // Every field read back as it was loaded -- the paper found by its size.
    CHECK(dialog.specFromFields() == initial);
    CHECK(dialog.paperBox()->currentIndex() == 3);
    CHECK_FALSE(dialog.widthField()->isEnabled());
    // THE PREVIEW is the plan for the document's pages, and the side chosen; the summary counts them.
    core::Result<core::ImpositionPlan> plan = core::planImposition(initial, shown);
    REQUIRE(plan);
    REQUIRE(dialog.preview()->plan().has_value());
    CHECK(dialog.preview()->plan()->sides.size() == plan.value().sides.size());
    CHECK(dialog.previewSideField()->maximum() == 10);
    dialog.previewSideField()->setValue(2);
    CHECK(dialog.preview()->side() == 1);
    CHECK(dialog.preview()->plan()->sides[1].turned);   // a long-edge flip on a landscape sheet: the back turned
    CHECK(dialog.summaryLabel()->text().contains(QStringLiteral("17")));
    // A layout's own fields are shown only for it.
    CHECK(dialog.findChild<QComboBox*>(QStringLiteral("bindingBox"))->isVisibleTo(&dialog));
    CHECK_FALSE(dialog.perSheetBox()->isVisibleTo(&dialog));

    // The other layout, every n-up field, a paper chosen and a custom one.
    dialog.layoutBox()->setCurrentIndex(0);
    CHECK(dialog.perSheetBox()->isVisibleTo(&dialog));
    dialog.perSheetBox()->setCurrentIndex(dialog.perSheetBox()->findData(9));
    dialog.orderBox()->setCurrentIndex(1);
    dialog.reversedBox()->setChecked(true);
    dialog.paperBox()->setCurrentIndex(0);   // Letter
    dialog.orientationBox()->setCurrentIndex(1);
    dialog.duplexBox()->setCurrentIndex(0);
    dialog.registrationBox()->setChecked(true);
    dialog.labelsBox()->setChecked(true);
    core::ImpositionSpec spec = dialog.specFromFields();
    CHECK(spec.layout == core::ImpositionLayout::NUp);
    CHECK(spec.perSheet == 9);
    CHECK(spec.order == core::ImpositionOrder::Down);
    CHECK(spec.reversed);
    CHECK(spec.sheet == kLetter);
    CHECK(spec.orientation == core::SheetOrientation::Portrait);
    CHECK(spec.duplex == core::DuplexFlip::OneSided);
    CHECK(spec.registrationMarks);
    CHECK(spec.sheetLabels);
    CHECK(dialog.preview()->plan()->grid == core::ImpositionGrid{3, 3, false});
    dialog.paperBox()->setCurrentIndex(dialog.paperBox()->count() - 1);   // custom
    CHECK(dialog.widthField()->isEnabled());
    dialog.widthField()->setValue(500.0);
    CHECK(dialog.specFromFields().sheet.width == 500.0);
    REQUIRE(dialog.choice());
}

TEST_CASE("imposition the dialog refuses what the plan refuses before OK closes it", "[imposition][A-102]")
{
    core::ImpositionSpec spec;
    ui::ImpositionDialog dialog(spec, std::vector<core::PdfSize>(5, kLetter), nullptr, QString());
    driveLayout(&dialog);
    REQUIRE(dialog.choice());
    CHECK_FALSE(dialog.refusalLabel()->isVisibleTo(&dialog));
    // Crop marks with a 10-point margin: refused by key, shown, OK keeps the dialog open.
    dialog.marginField()->setValue(10.0);
    dialog.cropBox()->setChecked(true);
    auto refused = dialog.choice();
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.imposition.marginTooSmall"));
    CHECK(dialog.refusalLabel()->isVisibleTo(&dialog));
    CHECK_FALSE(dialog.preview()->plan().has_value());
    dialog.accept();
    CHECK(dialog.result() != QDialog::Accepted);
    // 16-up on a small custom sheet: a cell too small.
    dialog.cropBox()->setChecked(false);
    dialog.perSheetBox()->setCurrentIndex(dialog.perSheetBox()->findData(16));
    dialog.paperBox()->setCurrentIndex(dialog.paperBox()->count() - 1);
    dialog.widthField()->setValue(150.0);   // (150 - 20) / 4 = 32.5 points a cell
    dialog.heightField()->setValue(150.0);
    refused = dialog.choice();
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.imposition.sheetTooSmall"));
    // Put right, the refusal goes.
    dialog.perSheetBox()->setCurrentIndex(dialog.perSheetBox()->findData(2));
    REQUIRE(dialog.choice());
    CHECK_FALSE(dialog.refusalLabel()->isVisibleTo(&dialog));
}

TEST_CASE("imposition profiles saved chosen to fill the dialog and deleted", "[imposition][A-97]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    core::ProfileStore store(QDir(scratch.path()).filePath(QStringLiteral("stamps")));
    store.load();
    core::ImpositionSpec spec;
    spec.layout = core::ImpositionLayout::Booklet;
    spec.creep = 0.3;
    spec.cropMarks = spec.sheetLabels = true;
    ui::ImpositionDialog dialog(spec, std::vector<core::PdfSize>(8, kLetter), &store, QString());
    driveLayout(&dialog);
    dialog.profileNameField()->setText(QStringLiteral("Zine"));
    dialog.saveProfileButton()->click();
    REQUIRE(store.profileNames(core::ProfileKind::Imposition) == QStringList{QStringLiteral("Zine")});
    // Another kind's profiles are not these.
    CHECK(store.profileNames(core::ProfileKind::Watermark).isEmpty());
    const core::ImpositionSpec saved = dialog.specFromFields();

    dialog.layoutBox()->setCurrentIndex(0);
    dialog.marginField()->setValue(50.0);
    const int at = dialog.profileBox()->findData(QStringLiteral("Zine"));
    REQUIRE(at > 0);
    dialog.profileBox()->setCurrentIndex(at);
    emit dialog.profileBox()->activated(at);
    CHECK(dialog.specFromFields() == saved);
    CHECK(dialog.choice().value().profile == QStringLiteral("Zine"));

    // A new store reads it back from its file.
    core::ProfileStore again(QDir(scratch.path()).filePath(QStringLiteral("stamps")));
    again.load();
    auto json = again.profile(core::ProfileKind::Imposition, QStringLiteral("Zine"));
    REQUIRE(json);
    CHECK(core::impositionFromJson(json.value()).value() == saved);

    dialog.deleteProfileButton()->click();
    CHECK(store.profileNames(core::ProfileKind::Imposition).isEmpty());
}

TEST_CASE("imposition booklet and n-up run from their actions and open the imposed document untitled", "[imposition][A-102]")
{
    uitest::ShellFixture fixture;
    fixture.backend.editable = true;
    fixture.backend.pagesPerDocument = 5;
    auto window = std::make_unique<ui::MainWindow>(fixture.services());
    window->resize(1100, 850);
    window->openFiles({fakePdf(fixture.directory, QStringLiteral("zine.pdf"))});
    driveLayout(window.get());
    REQUIRE(window->documentCount() == 1);
    fixture.pumpTiles();

    // NOBODY TO ASK (convention 16): both disabled.
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::ImpositionBooklet));
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::ImpositionNUp));

    Answers answers;
    std::optional<core::ImpositionSpec> startedFrom;
    std::vector<core::PdfSize> shownAsked;
    ui::ImpositionDialog::setAnswerForTesting([&](const core::ImpositionSpec& initial, const std::vector<core::PdfSize>& shown) {
        startedFrom = initial;
        shownAsked = shown;
        core::ImpositionSpec spec = initial;
        spec.cropMarks = true;
        return std::optional<ui::ImpositionDialog::Choice>(ui::ImpositionDialog::Choice{spec, QString()});
    });
    CHECK(fixture.actions.isEnabled(ActionId::ImpositionBooklet));
    CHECK(fixture.actions.isEnabled(ActionId::ImpositionNUp));

    // BOOKLET: it starts from the booklet default on `documents/defaultPageSize`'s paper, the document's five pages asked for.
    REQUIRE(fixture.actions.dispatch(ActionId::ImpositionBooklet, core::KeyContext::Global));
    REQUIRE(startedFrom);
    CHECK(startedFrom->layout == core::ImpositionLayout::Booklet);
    CHECK(startedFrom->duplex == core::DuplexFlip::ShortEdge);
    CHECK(startedFrom->sheet == core::paperSizePoints(fixture.settings.get(core::keys::documents::defaultPageSize)));
    CHECK(shownAsked.size() == 5);
    REQUIRE(fixture.backend.imposed.size() == 1);
    CHECK(fixture.backend.imposed.back().layout == core::ImpositionLayout::Booklet);
    CHECK(fixture.backend.imposed.back().cropMarks);
    // The sheet labels' words are the catalog's, each with its two numbers.
    CHECK(fixture.backend.impositionTexts.back().labels.front.contains(QStringLiteral("%1")));
    CHECK(fixture.backend.impositionTexts.back().labels.back.contains(QStringLiteral("%2")));
    // ... and the imposed document is a NEW untitled tab, opened from its bytes as an assembly; the source unchanged.
    CHECK(window->documentCount() == 2);
    REQUIRE_FALSE(fixture.backend.openedOrigins.isEmpty());
    CHECK(fixture.backend.openedOrigins.back() == core::BytesOrigin::Assembly);
    CHECK(fixture.backend.openedBytes.back().startsWith("%PDF-1.7 fake imposition of 4 side(s)"));
    CHECK(window->lastImposition().has_value());

    // N-UP from the same window: it starts from the last imposition, with its own layout.
    window->activateDocument(0);
    REQUIRE(fixture.actions.dispatch(ActionId::ImpositionNUp, core::KeyContext::Global));
    CHECK(startedFrom->layout == core::ImpositionLayout::NUp);
    CHECK(startedFrom->cropMarks);   // the last one's marks kept
    REQUIRE(fixture.backend.imposed.size() == 2);
    CHECK(window->documentCount() == 3);

    // A refusal says why on the status bar and opens nothing.
    fixture.backend.assembleFails = "error.imposition.noPages";
    window->activateDocument(0);
    (void)fixture.actions.dispatch(ActionId::ImpositionNUp, core::KeyContext::Global);
    CHECK(window->documentCount() == 3);
    fixture.backend.assembleFails = nullptr;

    window.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
