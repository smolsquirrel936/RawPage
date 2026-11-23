// tst_watermarkdialogs -- Stage 7 T15 (`Stage7-Assembly-Plan.md` §6, D7-22, D7-45): the watermark dialog, its preview, its
// two pictures and its profiles; the chooser *Update* and *Remove* act through; the three actions T15 registers; and an
// image on the clipboard pasted as a page -- through the window over the fake backend (`Stage7-Assembly-Testing-Guide.md`
// §5.17 -- the surface halves of A-97, A-99 and A-100).
//
// WHAT IT DOES NOT COVER (convention 19): the PAGES. The fake sink records the decorations it is told and the fake backend
// the images it is asked to make pages of; what is drawn is `tests/render/tst_watermarkengine`'s and the golden suite's.
// The preview is graded on the tiles it is handed (the layout's), not on its pixels -- M-09 compares it by eye. And the
// file dialogs: each picture is set through the seam its button calls (`setImageFile`, `setPageFile`).

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/doc/PageTree.h"
#include "core/doc/Stamps.h"
#include "core/doc/Watermark.h"
#include "core/settings/ProfileStore.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/WatermarkDialog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QTemporaryDir>

#include <algorithm>
#include <memory>
#include <optional>

using namespace rawpage;
using core::ActionId;

namespace {

core::PageTree pagesOf(int count)
{
    std::vector<core::Page> pages;
    for (int i = 0; i < count; ++i) pages.emplace_back(i, core::PdfRect{core::PdfPoint{0, 0}, core::PdfSize{612, 792}});
    return core::PageTree(std::move(pages));
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
        ui::WatermarkDialog::setAnswerForTesting({});
        ui::WatermarkChooser::setAnswerForTesting({});
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

QString pngFile(const QTemporaryDir& directory, int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(QColor(200, 30, 30, 128));
    const QString path = QDir(directory.path()).filePath(QStringLiteral("logo.png"));
    REQUIRE(image.save(path));
    return path;
}

ui::WatermarkDialog::PageReader reader(int* asked = nullptr)
{
    return [asked](const QString& path, int page) -> core::Result<core::StampPage> {
        if (asked) ++*asked;
        if (!QFileInfo::exists(path)) return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.open.notFound"), path};
        return core::StampPage{QByteArray("%PDF-1.7 a page ") + QByteArray::number(page), 300.0, 150.0};
    };
}

} // namespace

TEST_CASE("decorate the watermark dialog reads every field into a spec and previews each copy where the layout puts it", "[decorate][watermark][A-99]")
{
    const core::PageTree pages = pagesOf(3);
    ui::WatermarkDialog dialog(ui::WatermarkDialog::Mode::Add, pages, core::WatermarkSpec{}, nullptr, QString(), reader());
    driveLayout(&dialog);
    dialog.textField()->setText(QStringLiteral("SECRET"));
    dialog.sizeField()->setValue(40);
    dialog.opacityField()->setValue(30);
    dialog.rotationField()->setValue(15);
    dialog.scaleModeBox()->setCurrentIndex(dialog.scaleModeBox()->findData(static_cast<int>(core::WatermarkScale::Absolute)));
    dialog.scaleField()->setValue(150);
    dialog.anchorBox()->setCurrentIndex(dialog.anchorBox()->findData(static_cast<int>(core::WatermarkAnchor::TopRight)));
    dialog.offsetXField()->setValue(-10);
    dialog.offsetYField()->setValue(-20);
    dialog.tileBox()->setChecked(true);
    dialog.tileGapField()->setValue(40);
    dialog.sideBox()->setCurrentIndex(1);   // in front
    dialog.usageBox()->setCurrentIndex(dialog.usageBox()->findData(static_cast<int>(core::WatermarkUsage::PrintOnly)));
    dialog.rangeField()->setText(QStringLiteral("2-3"));
    const core::WatermarkSpec spec = dialog.specFromFields();
    CHECK(spec.source == core::WatermarkSource::Text);
    CHECK(spec.text == QStringLiteral("SECRET"));
    CHECK(spec.size == 40);
    CHECK(spec.opacity == 0.3);
    CHECK(spec.rotation == 15);
    CHECK(spec.scaleMode == core::WatermarkScale::Absolute);
    CHECK(spec.scale == 1.5);
    CHECK(spec.anchor == core::WatermarkAnchor::TopRight);
    CHECK(spec.offsetX == -10);
    CHECK(spec.offsetY == -20);
    CHECK(spec.tile);
    CHECK(spec.tileGap == 40);
    CHECK_FALSE(spec.behind);
    CHECK(spec.usage == core::WatermarkUsage::PrintOnly);
    CHECK(spec.range == QStringLiteral("2-3"));
    auto chosen = dialog.choice();
    REQUIRE(chosen);
    CHECK(chosen.value().spec == spec);

    // The preview of a page the range reaches holds every copy the layout places on it; page 1 is previewed bare.
    dialog.previewPageField()->setValue(2);
    REQUIRE(dialog.preview()->decoration());
    auto laid = core::layoutWatermark(spec, *pages.at(1));
    REQUIRE(laid);
    CHECK(dialog.preview()->decoration()->watermark->tiles == laid.value().watermark->tiles);
    CHECK(dialog.preview()->decoration()->watermark->tiles.size() > 1);
    dialog.previewPageField()->setValue(1);
    CHECK_FALSE(dialog.preview()->decoration());
}

TEST_CASE("decoration.characters the watermark dialog refuses a Cyrillic letter by name, and too many tiles by key, before any command exists", "[decorate][watermark][A-99]")
{
    const core::PageTree pages = pagesOf(1);
    ui::WatermarkDialog dialog(ui::WatermarkDialog::Mode::Add, pages, core::WatermarkSpec{}, nullptr, QString(), reader());
    driveLayout(&dialog);
    dialog.textField()->setText(QString::fromUtf8("\xD0\x93"));
    dialog.accept();
    CHECK(dialog.result() != QDialog::Accepted);
    CHECK(dialog.refusalLabel()->isVisibleTo(&dialog));
    CHECK(dialog.refusalLabel()->text().contains(QStringLiteral("U+0413")));
    dialog.textField()->setText(QStringLiteral("."));
    dialog.sizeField()->setValue(4);
    dialog.scaleModeBox()->setCurrentIndex(dialog.scaleModeBox()->findData(static_cast<int>(core::WatermarkScale::Absolute)));
    dialog.scaleField()->setValue(100);
    dialog.tileBox()->setChecked(true);
    dialog.tileGapField()->setValue(0);
    auto refused = dialog.choice();
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.decorate.tooManyTiles"));
}

TEST_CASE("decorate the watermark dialog takes an image file and a page of a PDF file, and an update keeps its picture until another is chosen", "[decorate][watermark][A-99]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    const core::PageTree pages = pagesOf(1);
    int asked = 0;
    ui::WatermarkDialog dialog(ui::WatermarkDialog::Mode::Add, pages, core::WatermarkSpec{}, nullptr, QString(), reader(&asked));
    driveLayout(&dialog);
    REQUIRE(dialog.setImageFile(pngFile(scratch, 64, 32)));
    core::WatermarkSpec spec = dialog.specFromFields();
    CHECK(spec.source == core::WatermarkSource::Image);
    CHECK(spec.image.width == 64);
    CHECK(spec.image.height == 32);
    CHECK_FALSE(spec.image.alpha.isEmpty());
    CHECK(spec.sourceName == QStringLiteral("logo.png"));
    CHECK(dialog.imageButton()->isChecked());
    // Not an image: refused by key, the picture chosen before kept.
    CHECK_FALSE(dialog.setImageFile(fakePdf(scratch, QStringLiteral("not-an-image.png"))));
    CHECK(dialog.specFromFields().image == spec.image);

    const QString pdf = fakePdf(scratch, QStringLiteral("letterhead.pdf"));
    REQUIRE(dialog.setPageFile(pdf, 2));
    spec = dialog.specFromFields();
    CHECK(spec.source == core::WatermarkSource::Page);
    CHECK(spec.page.graph.endsWith("page 2"));
    CHECK(spec.page.width == 300.0);
    CHECK(spec.sourceName == QStringLiteral("letterhead.pdf"));
    CHECK(dialog.pageNumberField()->value() == 3);
    CHECK(asked == 1);
    CHECK_FALSE(dialog.setPageFile(QDir(scratch.path()).filePath(QStringLiteral("gone.pdf")), 0));

    // AN UPDATE of an image watermark starts on the picture it HAS, named by its id, and keeps it ...
    core::WatermarkSpec kept;
    kept.source = core::WatermarkSource::Image;
    kept.keepSourceOf = QString(32, QLatin1Char('a'));
    kept.sourceSize = core::PdfSize{90, 45};
    kept.sourceName = QStringLiteral("seal.png");
    ui::WatermarkDialog update(ui::WatermarkDialog::Mode::Update, pages, kept, nullptr, QString(), reader());
    driveLayout(&update);
    CHECK(update.sourceLabel()->text().contains(QStringLiteral("seal.png")));
    spec = update.specFromFields();
    CHECK(spec.keepSourceOf == kept.keepSourceOf);
    CHECK(spec.sourceSize.width == 90);
    CHECK(update.choice());
    // ... until another is chosen.
    REQUIRE(update.setImageFile(pngFile(scratch, 10, 10)));
    CHECK(update.specFromFields().keepSourceOf.isEmpty());
    CHECK(update.specFromFields().image.width == 10);
}

TEST_CASE("decorate watermark profiles saved with their picture, chosen to fill the dialog, deleted, and a kept picture refused", "[decorate][watermark][A-97]")
{
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());
    core::ProfileStore store(QDir(scratch.path()).filePath(QStringLiteral("stamps")));
    store.load();
    const core::PageTree pages = pagesOf(1);
    ui::WatermarkDialog dialog(ui::WatermarkDialog::Mode::Add, pages, core::WatermarkSpec{}, &store, QString(), reader());
    driveLayout(&dialog);
    REQUIRE(dialog.setImageFile(pngFile(scratch, 20, 20)));
    dialog.opacityField()->setValue(25);
    dialog.profileNameField()->setText(QStringLiteral("Seal"));
    dialog.saveProfileButton()->click();
    REQUIRE(store.profileNames(core::ProfileKind::Watermark) == QStringList{QStringLiteral("Seal")});
    const core::WatermarkSpec saved = dialog.specFromFields();

    dialog.textButton()->setChecked(true);
    dialog.opacityField()->setValue(90);
    const int at = dialog.profileBox()->findData(QStringLiteral("Seal"));
    REQUIRE(at > 0);
    dialog.profileBox()->setCurrentIndex(at);
    emit dialog.profileBox()->activated(at);
    CHECK(dialog.specFromFields() == saved);   // the picture's bytes came back with it

    dialog.deleteProfileButton()->click();
    CHECK(store.profileNames(core::ProfileKind::Watermark).isEmpty());

    // A picture kept from a document is no file's: refused by key, nothing saved.
    core::WatermarkSpec kept;
    kept.source = core::WatermarkSource::Page;
    kept.keepSourceOf = QString(32, QLatin1Char('b'));
    kept.sourceSize = core::PdfSize{100, 100};
    ui::WatermarkDialog update(ui::WatermarkDialog::Mode::Update, pages, kept, &store, QString(), reader());
    driveLayout(&update);
    update.profileNameField()->setText(QStringLiteral("Kept"));
    update.saveProfileButton()->click();
    CHECK(store.profileNames(core::ProfileKind::Watermark).isEmpty());
    CHECK(update.refusalLabel()->isVisibleTo(&update));
}

TEST_CASE("decorate the watermark chooser lists each watermark from its record and answers the ones chosen", "[decorate][watermark][A-100]")
{
    core::WatermarkSpec text;
    text.text = QStringLiteral("CONFIDENTIAL");
    text.usage = core::WatermarkUsage::PrintOnly;
    core::WatermarkSpec image;
    image.source = core::WatermarkSource::Image;
    image.keepSourceOf = QString(32, QLatin1Char('c'));
    image.sourceSize = core::PdfSize{10, 10};
    image.sourceName = QStringLiteral("logo.png");
    image.behind = false;
    const std::vector<core::DecorationEntry> entries{
        {QString(32, QLatin1Char('1')), core::DecorationKind::Watermark, {0, 1, 2, 4}, core::watermarkToJson(text, false)},
        {QString(32, QLatin1Char('2')), core::DecorationKind::Watermark, {3}, core::watermarkToJson(image, false)}};
    CHECK(ui::describeWatermark(entries[0]).contains(QStringLiteral("CONFIDENTIAL")));
    CHECK(ui::describeWatermark(entries[0]).contains(QStringLiteral("1-3, 5")));
    CHECK(ui::describeWatermark(entries[1]).contains(QStringLiteral("logo.png")));

    ui::WatermarkChooser update(ui::WatermarkChooser::Purpose::Update, entries);
    driveLayout(&update);
    REQUIRE(update.list()->count() == 2);
    update.list()->setCurrentRow(1);
    CHECK(update.chosen().value() == std::vector<QString>{entries[1].id});

    ui::WatermarkChooser remove(ui::WatermarkChooser::Purpose::Remove, entries);
    driveLayout(&remove);
    remove.list()->clearSelection();
    CHECK_FALSE(remove.chosen());
    auto* all = remove.findChild<QPushButton*>(QStringLiteral("allButton"));
    REQUIRE(all != nullptr);
    all->click();
    CHECK(remove.chosen().value() == std::vector<QString>{entries[0].id, entries[1].id});
}

TEST_CASE("decorate watermark add, update and remove run from their actions on the ones chosen, each one command, and are disabled with nobody to ask", "[decorate][watermark]")
{
    uitest::ShellFixture fixture;
    fixture.backend.editable = true;
    fixture.backend.pagesPerDocument = 3;
    auto window = std::make_unique<ui::MainWindow>(fixture.services());
    window->resize(1100, 850);
    window->openFiles({fakePdf(fixture.directory, QStringLiteral("first.pdf"))});
    driveLayout(window.get());
    REQUIRE(window->documentCount() == 1);
    fixture.pumpTiles();

    // NOBODY TO ASK (convention 16): all three are disabled.
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::WatermarkAdd));
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::WatermarkUpdate));
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::WatermarkRemove));

    Answers answers;
    std::optional<core::WatermarkSpec> startedFrom;
    int added = 0;
    ui::WatermarkDialog::setAnswerForTesting([&](ui::WatermarkDialog::Mode mode, const core::WatermarkSpec& initial) {
        startedFrom = initial;
        core::WatermarkSpec spec = initial;
        spec.text = mode == ui::WatermarkDialog::Mode::Add ? QStringLiteral("ADDED %1").arg(++added) : QStringLiteral("UPDATED");
        return std::optional<ui::WatermarkDialog::Choice>(ui::WatermarkDialog::Choice{spec, QString()});
    });
    int chooserAsked = 0;
    QString pick;   // the id chosen -- the list is in the document's order, which a new key may change
    ui::WatermarkChooser::setAnswerForTesting([&](ui::WatermarkChooser::Purpose, const std::vector<core::DecorationEntry>& entries) {
        ++chooserAsked;
        const bool listed = std::any_of(entries.begin(), entries.end(), [&](const core::DecorationEntry& e) { return e.id == pick; });
        CHECK(listed);
        return std::optional<std::vector<QString>>(std::vector<QString>{pick});
    });
    ui::DocumentWindow* front = window->currentDocument();
    REQUIRE(front != nullptr);
    uitest::FakeSink* sink = fixture.backend.sinkFor(front->handle());
    REQUIRE(fixture.actions.dispatch(ActionId::WatermarkAdd, core::KeyContext::Global));
    // *Add* opens on the catalog's default text the first time.
    REQUIRE(startedFrom);
    CHECK_FALSE(startedFrom->text.isEmpty());
    REQUIRE(fixture.actions.dispatch(ActionId::WatermarkAdd, core::KeyContext::Global));
    CHECK(startedFrom->text == QStringLiteral("ADDED 1"));   // then on the last one added
    REQUIRE(sink->decorationPages.size() == 2);
    REQUIRE(window->watermarksOf(*front).size() == 2);

    // UPDATE the SECOND: the chooser asked, the dialog started from what that one recorded, and one step replaced it.
    const QString second = window->watermarksOf(*front).at(1).id;
    const QString first = window->watermarksOf(*front).at(0).id;
    const QString recorded = window->watermarksOf(*front).at(1).spec.value(QStringLiteral("text")).toString();
    CHECK(recorded.startsWith(QStringLiteral("ADDED ")));
    pick = second;
    REQUIRE(fixture.actions.dispatch(ActionId::WatermarkUpdate, core::KeyContext::Global));
    CHECK(chooserAsked == 1);
    CHECK(startedFrom->text == recorded);
    REQUIRE(sink->decorationPages.size() == 2);
    CHECK(sink->decorationPages.count(first) == 1);
    CHECK(sink->decorationPages.count(second) == 0);
    REQUIRE(front->journal()->undo());
    CHECK(sink->decorationPages.count(second) == 1);
    REQUIRE(front->journal()->redo());

    // REMOVE the first chosen: the other left; then the one left removed with no question.
    pick = first;
    REQUIRE(fixture.actions.dispatch(ActionId::WatermarkRemove, core::KeyContext::Global));
    CHECK(chooserAsked == 2);
    REQUIRE(sink->decorationPages.size() == 1);
    CHECK(sink->decorationPages.count(first) == 0);
    REQUIRE(fixture.actions.dispatch(ActionId::WatermarkRemove, core::KeyContext::Global));
    CHECK(chooserAsked == 2);
    CHECK(sink->decorationPages.empty());
    // None left: refused on the status bar, no undo step and no question.
    const auto depth = front->journal()->historySize();
    (void)fixture.actions.dispatch(ActionId::WatermarkUpdate, core::KeyContext::Global);
    (void)fixture.actions.dispatch(ActionId::WatermarkRemove, core::KeyContext::Global);
    CHECK(front->journal()->historySize() == depth);
    CHECK(chooserAsked == 2);

    window.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

TEST_CASE("pages an image on the clipboard is pasted as ONE page of its own, and Paste is enabled for it", "[decorate][watermark][A-100]")
{
    uitest::ShellFixture fixture;
    fixture.backend.editable = true;
    fixture.backend.pagesPerDocument = 2;
    auto window = std::make_unique<ui::MainWindow>(fixture.services());
    window->resize(1100, 850);
    window->openFiles({fakePdf(fixture.directory, QStringLiteral("first.pdf"))});
    driveLayout(window.get());
    REQUIRE(window->documentCount() == 1);
    fixture.pumpTiles();
    QClipboard* clipboard = QGuiApplication::clipboard();
    REQUIRE(clipboard != nullptr);
    clipboard->clear();
    CHECK_FALSE(fixture.actions.isEnabled(ActionId::PagePaste));
    QImage image(120, 60, QImage::Format_RGB32);
    image.fill(QColor(10, 200, 90));
    clipboard->setImage(image);
    CHECK(window->clipboardPageCount() == 1);
    CHECK(fixture.actions.isEnabled(ActionId::PagePaste));
    ui::DocumentWindow* front = window->currentDocument();
    REQUIRE(fixture.actions.dispatch(ActionId::PagePaste, core::KeyContext::Global));
    REQUIRE(fixture.backend.imagePages.size() == 1);
    CHECK(fixture.backend.imagePages.front() == std::pair{120, 60});
    CHECK(front->pageCount() == 3);
    CHECK(front->journal()->canUndo());
    clipboard->clear();
    window.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
