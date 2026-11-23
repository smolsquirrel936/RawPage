// tests/ui/tst_layerspane.cpp -- the Layers pane and its commands, driven as a person drives them. Stage 7 T17, decision
// D7-9; guide case A-108 (`Stage7-Assembly-Testing-Guide.md` §5.19), and A-109's registration half.
//
// WHAT THIS FILE IS AND IS NOT (convention 19). The window runs over `tests/ui/FakeBackend.h`: the tree is the one a case
// fills (`layers_`, shaped as `layers.pdf` is), every view the window hands the backend is RECORDED, never drawn, and every
// command reaches a recording sink (`FakeSink::layersSet`). So this proves the pane shows the tree, that its eye is a VIEW
// -- the scheduler keyed, the backend handed it, no command, nothing dirtied -- and that every other row is a command
// with the intent a person gave. **It does not see a byte written or a pixel drawn**: the sink's writing is
// `tests/unit/tst_layers.cpp`'s, what PDFium draws `tests/render/tst_layersengine.cpp`'s.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/ActionRegistry.h"
#include "core/cmd/CommandJournal.h"
#include "core/doc/Document.h"
#include "core/doc/LayerTree.h"
#include "tests/ui/ShellFixture.h"
#include "ui/dialogs/LayerDialogs.h"
#include "ui/panes/LayersPane.h"
#include "ui/panes/ThumbnailsPane.h"
#include "ui/viewer/PageView.h"
#include "ui/viewer/TilePresenter.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/MainWindow.h"
#include "ui/shell/SidebarDock.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QTemporaryDir>

#include <memory>
#include <optional>
#include <set>
#include <vector>

using namespace rawpage;
using core::ActionId;
using core::Layer;
using core::LayerId;
using core::LayerUsage;

namespace {

void driveLayout(QWidget* root)
{
    root->show();
    for (QWidget* widget : root->findChildren<QWidget*>())
        if (QLayout* layout = widget->layout()) layout->activate();
    if (QLayout* layout = root->layout()) layout->activate();
    QCoreApplication::processEvents();
}

QString fakePdf(const QTemporaryDir& directory, const QString& name)
{
    const QString path = QFileInfo(QDir(directory.path()).filePath(name)).absoluteFilePath();
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("%PDF-1.7\n% a fake page\n");
    return path;
}

struct Answers {
    ~Answers()
    {
        ui::LayerNameDialog::setAnswerForTesting({});
        ui::LayerMergeDialog::setAnswerForTesting({});
        ui::LayerPropertiesDialog::setAnswerForTesting({});
    }
};

// `layers.pdf`'s shape, by hand (`corpus/make-assembly.py`, `layers()`): object numbers 10 ... 17, and a watermark's
// group (18) listed and in no order.
enum : long long { kBase = 10, kAnnotations, kNotes, kMarkup, kEn, kFr, kPrint, kScreen, kWatermark };

LayerId id(long long number)
{
    return LayerId::ofObject(number);
}

core::LayerTree layersShape()
{
    core::LayerTree tree;
    const auto add = [&tree](long long number, const char* name) -> Layer& {
        Layer layer;
        layer.id = id(number);
        layer.name = QString::fromLatin1(name);
        tree.layers().push_back(layer);
        return tree.layers().back();
    };
    add(kBase, "Base drawing").locked = true;
    add(kAnnotations, "Annotations");
    add(kNotes, "Reviewer notes");
    add(kMarkup, "Markup");
    add(kEn, "English");
    add(kFr, "French").defaultOn = false;
    Layer& print = add(kPrint, "Print only");
    print.print = LayerUsage::On;
    print.view = LayerUsage::Off;
    print.autoPrint = print.autoView = true;
    Layer& screen = add(kScreen, "Screen only");
    screen.print = LayerUsage::Off;
    screen.view = LayerUsage::On;
    screen.autoPrint = screen.autoView = true;
    add(kWatermark, "Watermark").decoration = true;
    const auto node = [](long long number) {
        core::LayerNode out;
        out.layer = id(number);
        return out;
    };
    core::LayerNode annotations = node(kAnnotations);
    annotations.children = {node(kNotes), node(kMarkup)};
    core::LayerNode languages;
    languages.label = QStringLiteral("Languages");
    languages.children = {node(kEn), node(kFr)};
    tree.order() = {node(kBase), annotations, languages, node(kPrint), node(kScreen)};
    tree.radioGroups().push_back({id(kEn), id(kFr)});
    REQUIRE(tree.validate());
    return tree;
}

struct Opened {
    uitest::ShellFixture fixture;
    std::unique_ptr<ui::MainWindow> window;
    ui::DocumentWindow* document = nullptr;
    ui::LayersPane* pane = nullptr;

    Opened()
    {
        fixture.backend.editable = true;
        fixture.backend.layers_ = layersShape();
        window = std::make_unique<ui::MainWindow>(fixture.services());
        window->resize(1100, 850);
        window->openFiles({fakePdf(fixture.directory, QStringLiteral("layers.pdf"))});
        driveLayout(window.get());
        REQUIRE(window->documentCount() == 1);
        fixture.pumpTiles();
        document = window->currentDocument();
        REQUIRE(document != nullptr);
        REQUIRE(document->journal() != nullptr);
        pane = window->layersPane();
        REQUIRE(pane != nullptr);
    }
    ~Opened()
    {
        window.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    const uitest::FakeSink& sink() { return *fixture.backend.sinkFor(document->handle()); }
};

} // namespace

TEST_CASE("layerspane the tree as the file says and the eye a view never a command", "[layers][A-108]")
{
    Opened o;
    // CTRL+SHIFT+L -- `pane.layers` registered, the pane shown and given the focus.
    REQUIRE(o.fixture.actions.isRegistered(ActionId::PaneLayers));
    REQUIRE(o.fixture.actions.dispatch(ActionId::PaneLayers, core::KeyContext::Global));
    QCoreApplication::processEvents();
    CHECK(o.window->sidebar()->paneWidget(ActionId::PaneLayers) == o.pane);
    CHECK(o.pane->isVisibleTo(o.window.get()));

    // THE TREE AS A-105: eight groups -- the watermark's not shown -- nesting, the collection, the lock, the eyes at open.
    CHECK(o.pane->groupRowCount() == 8);
    CHECK(o.pane->nameOf(id(kWatermark)).isEmpty());
    CHECK(o.pane->depthOf(id(kNotes)) == 2);
    CHECK(o.pane->depthOf(id(kEn)) == 2);
    CHECK(o.pane->depthOf(id(kScreen)) == 1);
    CHECK(o.pane->collectionLabels() == QStringList{QStringLiteral("Languages")});
    CHECK(o.pane->lockedRow(id(kBase)));
    CHECK_FALSE(o.pane->lockedRow(id(kNotes)));
    CHECK(o.pane->eyeOf(id(kNotes)) == std::optional<bool>(true));
    CHECK(o.pane->eyeOf(id(kFr)) == std::optional<bool>(false));
    CHECK(o.pane->eyeOf(id(kPrint)) == std::optional<bool>(false));
    CHECK(o.pane->eyeOf(id(kScreen)) == std::optional<bool>(true));

    // THE EYE: a view, handed to the backend with the scheduler keyed by it -- no command, nothing dirtied.
    const int commandsBefore = o.sink().layerCommands;
    REQUIRE(o.pane->clickEye(id(kNotes)));
    QCoreApplication::processEvents();   // the pane's request is queued
    REQUIRE(o.fixture.backend.layerViews.size() == 1);
    const std::set<LayerId> hidden(o.fixture.backend.layerViews.back().second.begin(), o.fixture.backend.layerViews.back().second.end());
    CHECK(hidden == std::set<LayerId>{id(kNotes), id(kFr), id(kPrint)});
    CHECK(o.fixture.renderScheduler.layerView(o.document->handle()) == core::LayerView::key(hidden));
    CHECK(o.pane->eyeOf(id(kNotes)) == std::optional<bool>(false));
    CHECK_FALSE(o.document->isDirty());
    CHECK(o.sink().layerCommands == commandsBefore);
    // A radio group: French shown, English hidden.
    REQUIRE(o.pane->clickEye(id(kFr)));
    QCoreApplication::processEvents();
    CHECK(o.pane->eyeOf(id(kFr)) == std::optional<bool>(true));
    CHECK(o.pane->eyeOf(id(kEn)) == std::optional<bool>(false));
    // The locked group's eye cannot be ticked, and `layer.hide` is disabled on it.
    CHECK_FALSE(o.pane->clickEye(id(kBase)));
    REQUIRE(o.pane->selectLayer(id(kBase)));
    CHECK_FALSE(o.fixture.actions.isEnabled(ActionId::LayerHide));
    CHECK_FALSE(o.fixture.actions.isEnabled(ActionId::LayerShow));
    // `layer.show` and `layer.hide` the same view, from the selection.
    REQUIRE(o.pane->selectLayer(id(kEn)));
    CHECK(o.fixture.actions.isEnabled(ActionId::LayerShow));
    CHECK_FALSE(o.fixture.actions.isEnabled(ActionId::LayerHide));
    REQUIRE(o.fixture.actions.dispatch(ActionId::LayerShow, core::KeyContext::Global));
    CHECK(o.pane->eyeOf(id(kEn)) == std::optional<bool>(true));
    CHECK(o.pane->eyeOf(id(kFr)) == std::optional<bool>(false));
    CHECK_FALSE(o.document->isDirty());
    CHECK(o.sink().layerCommands == commandsBefore);
    // A refused view changes nothing: the eye stays, the key stays.
    const std::vector<long long> key = o.fixture.renderScheduler.layerView(o.document->handle());
    o.fixture.backend.failLayerViews = true;
    REQUIRE(o.pane->clickEye(id(kMarkup)));
    QCoreApplication::processEvents();
    CHECK(o.pane->eyeOf(id(kMarkup)) == std::optional<bool>(true));
    CHECK(o.fixture.renderScheduler.layerView(o.document->handle()) == key);
    o.fixture.backend.failLayerViews = false;

    // EVERY REQUEST the window makes now carries the view -- the page's tiles and, shown, the thumbnails -- or the
    // scheduler would supersede it for ever (`RenderScheduler::setLayerView`).
    REQUIRE_FALSE(key.empty());
    CHECK(o.document->view()->tiles().requestAt(0, core::render::TileRect{0, 0, 8, 8}, 1.0, core::PageRotation::None).hiddenLayers == key);
    REQUIRE(o.fixture.actions.dispatch(ActionId::PaneThumbnails, core::KeyContext::Global));
    driveLayout(o.window.get());
    o.fixture.pumpTiles();
    const core::render::RenderRequest* thumbnail = o.window->thumbnailsPane()->requestForPage(0);
    REQUIRE(thumbnail != nullptr);
    CHECK(thumbnail->hiddenLayers == key);
}

TEST_CASE("layerspane rename in place and every command from its menu", "[layers][A-108]")
{
    Opened o;
    Answers answers;
    // THE MENU: each row a registered action; `layer.flatten` is not one of them, and stays unregistered (Q2).
    for (const ActionId action : ui::LayersPane::menuActions()) CHECK(o.fixture.actions.isRegistered(action));
    CHECK_FALSE(o.fixture.actions.isRegistered(ActionId::LayerFlatten));

    // RENAME IN PLACE: what is typed is a command with the new name, and the pane shows what the model then holds.
    REQUIRE(o.pane->selectLayer(id(kNotes)));
    QTreeWidgetItem* row = o.pane->currentItem();
    REQUIRE(row != nullptr);
    row->setText(0, QStringLiteral("Notes, renamed"));
    QCoreApplication::processEvents();
    CHECK(o.sink().layersSet.find(id(kNotes))->name == QStringLiteral("Notes, renamed"));
    CHECK(o.pane->nameOf(id(kNotes)) == QStringLiteral("Notes, renamed"));
    CHECK(o.document->isDirty());

    // NEW: with nobody to ask it is disabled; with an answer, an empty group at the top level, selected.
    CHECK_FALSE(o.fixture.actions.isEnabled(ActionId::LayerCreate));
    ui::LayerNameDialog::setAnswerForTesting([](const QString& initial) {
        CHECK(initial.contains(QStringLiteral("9")));   // the next number after the eight a person sees
        return std::optional<QString>(QStringLiteral("Stamps"));
    });
    REQUIRE(o.fixture.actions.dispatch(ActionId::LayerCreate, core::KeyContext::Global));
    const core::LayerTree& made = o.sink().layersSet;
    REQUIRE(made.layers().size() == 10);
    const Layer& created = made.layers().back();
    CHECK(created.name == QStringLiteral("Stamps"));
    CHECK(created.id.isCreated());
    CHECK_FALSE(made.parentOf(created.id).isValid());
    CHECK(o.pane->selectedLayer() == std::optional<LayerId>(created.id));

    // LOCK: the lock turned over, by command.
    REQUIRE(o.pane->selectLayer(id(kMarkup)));
    REQUIRE(o.fixture.actions.dispatch(ActionId::LayerLock, core::KeyContext::Global));
    CHECK(o.sink().layersSet.find(id(kMarkup))->locked);
    CHECK(o.pane->lockedRow(id(kMarkup)));

    // MERGE: into the group the person names -- the choices every other group a person sees.
    std::vector<LayerId> offered;
    ui::LayerMergeDialog::setAnswerForTesting([&offered](const std::vector<LayerId>& choices) {
        offered = choices;
        return std::optional<LayerId>(id(kNotes));
    });
    REQUIRE(o.pane->selectLayer(id(kMarkup)));
    REQUIRE(o.fixture.actions.dispatch(ActionId::LayerMerge, core::KeyContext::Global));
    CHECK(offered.size() == 8);   // nine a person sees, less the one merged
    CHECK(std::find(offered.begin(), offered.end(), id(kWatermark)) == offered.end());
    CHECK(o.sink().layersSet.find(id(kMarkup)) == nullptr);
    REQUIRE(o.sink().layersSet.merges().size() == 1);
    CHECK(o.sink().layersSet.merges().front() == std::make_pair(id(kMarkup), id(kNotes)));

    // PROPERTIES: what the dialog answers, as one command.
    ui::LayerPropertiesDialog::setAnswerForTesting([](const core::LayerProperties& initial) {
        core::LayerProperties out = initial;
        out.defaultOn = false;
        out.print = LayerUsage::Off;
        out.parent = id(kBase);
        return std::optional<core::LayerProperties>(out);
    });
    REQUIRE(o.pane->selectLayer(id(kScreen)));
    REQUIRE(o.fixture.actions.dispatch(ActionId::LayerProperties, core::KeyContext::Global));
    const Layer* screen = o.sink().layersSet.find(id(kScreen));
    REQUIRE(screen != nullptr);
    CHECK_FALSE(screen->defaultOn);
    CHECK(screen->print == LayerUsage::Off);
    CHECK(o.sink().layersSet.parentOf(id(kScreen)) == id(kBase));
    CHECK(o.pane->depthOf(id(kScreen)) == 2);

    // DELETE: gone from the tree and the pane; undone, back.
    REQUIRE(o.pane->selectLayer(id(kEn)));
    REQUIRE(o.fixture.actions.dispatch(ActionId::LayerDelete, core::KeyContext::Global));
    CHECK(o.sink().layersSet.find(id(kEn)) == nullptr);
    CHECK(o.pane->nameOf(id(kEn)).isEmpty());
    REQUIRE(o.document->journal()->undo());
    QCoreApplication::processEvents();
    CHECK(o.pane->nameOf(id(kEn)) == QStringLiteral("English"));
    // A watermark's group is never chosen: nothing selected names it, so no command reaches it.
    CHECK_FALSE(o.pane->selectLayer(id(kWatermark)));
}

TEST_CASE("layerspane the properties dialog reads every field and offers only parents a layer may take", "[layers][A-108]")
{
    const core::LayerTree tree = layersShape();
    ui::LayerPropertiesDialog dialog(tree, id(kAnnotations));
    driveLayout(&dialog);
    // Read back as loaded.
    CHECK(dialog.properties() == core::LayerProperties::of(tree, id(kAnnotations)));
    // A parent never itself or under itself: Annotations offers neither its notes nor its markup -- nor the watermark's.
    QStringList parents;
    for (int i = 0; i < dialog.parentBox()->count(); ++i) parents << dialog.parentBox()->itemText(i);
    CHECK_FALSE(parents.contains(QStringLiteral("Annotations")));
    CHECK_FALSE(parents.contains(QStringLiteral("Reviewer notes")));
    CHECK_FALSE(parents.contains(QStringLiteral("Watermark")));
    CHECK(parents.contains(QStringLiteral("Base drawing")));
    // Every field into the answer.
    dialog.nameField()->setText(QStringLiteral("Comments"));
    dialog.shownBox()->setChecked(false);
    dialog.lockedBox()->setChecked(true);
    dialog.printBox()->setCurrentIndex(2);
    dialog.viewBox()->setCurrentIndex(1);
    dialog.exportBox()->setCurrentIndex(1);
    dialog.parentBox()->setCurrentIndex(parents.indexOf(QStringLiteral("Base drawing")));
    for (int i = 0; i < dialog.exclusiveList()->count(); ++i) {
        if (dialog.exclusiveList()->item(i)->text() == QStringLiteral("Markup")) dialog.exclusiveList()->item(i)->setCheckState(Qt::Checked);
    }
    const core::LayerProperties p = dialog.properties();
    CHECK(p.name == QStringLiteral("Comments"));
    CHECK_FALSE(p.defaultOn);
    CHECK(p.locked);
    CHECK(p.print == LayerUsage::Off);
    CHECK(p.view == LayerUsage::On);
    CHECK(p.exportUsage == LayerUsage::On);
    CHECK(p.parent == id(kBase));
    CHECK(p.exclusiveWith == std::vector<LayerId>{id(kMarkup)});
    // The choices a merge offers: every group a person sees but the one merged.
    const std::vector<LayerId> choices = ui::LayerMergeDialog::choices(tree, id(kNotes));
    CHECK(choices.size() == 7);
    CHECK(std::find(choices.begin(), choices.end(), id(kNotes)) == choices.end());
}
