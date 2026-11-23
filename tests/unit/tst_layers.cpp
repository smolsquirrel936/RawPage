// tests/unit/tst_layers.cpp -- the layers, headless: the first-party reading, the model's editing half, the view's rule,
// the four commands through the SHIPPED sink, saved and read back. Stage 7 T17, decision D7-9; guide cases A-105 (the
// reading's half), A-106 (its headless half) and A-107 (`Stage7-Assembly-Testing-Guide.md` §5.19).
//
// WHAT THIS FILE IS AND IS NOT (convention 19). The reading is held to `corpus/labels/assembly-layers.json`, written by the
// generator beside the file -- not to anything `LayerOps` computes. Commands run through `tests/unit/CosBackend.h` -- the
// first-party reader and the real `pdf/ops/MutationSinkImpl` -- and a saved file is read back with `LayerOps::read` and the
// COS layer. **It does not see PDFium**: what the ENGINE draws -- a hidden group absent from the raster, a deleted group's
// content orphaned, a merged group's drawn as the survivor's -- is `tests/render/tst_layersengine.cpp`'s. The view
// section is composed here as the shipped backend composes it and read as COS, never drawn.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "TinyPdf.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/commands/DecorationCommands.h"
#include "core/cmd/commands/StructureCommands.h"
#include "core/doc/Watermark.h"
#include "core/doc/LayerTree.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/LayerOps.h"
#include "pdf/ops/PageOps.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using core::Layer;
using core::LayerId;
using core::LayerProperties;
using core::LayerTree;
using core::LayerUsage;
using pdf::cos::CosObject;

namespace {

QJsonObject layersLabel()
{
    QFile file(tests::corpusFile("labels/assembly-layers.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QJsonDocument::fromJson(file.readAll()).object();
}

// Each labelled group by its key: the object number and everything the label says of it.
std::map<std::string, QJsonObject> labelledGroups()
{
    std::map<std::string, QJsonObject> out;
    for (const QJsonValue value : layersLabel().value(QStringLiteral("groups")).toArray())
        out.emplace(value.toObject().value(QStringLiteral("key")).toString().toStdString(), value.toObject());
    return out;
}

LayerId idOf(const QJsonObject& group)
{
    return LayerId::ofObject(group.value(QStringLiteral("object")).toInteger());
}

struct Written {
    QByteArray bytes;
    pdf::cos::XrefTable table;
    LayerTree layers;

    CosObject object(long long number)
    {
        auto fetched = table.fetch(number);
        REQUIRE(fetched);
        CosObject out = fetched.value().object;
        out.clearSpansRecursively();
        return out;
    }
    CosObject resolved(const CosObject& value) { return value.isReference() ? object(value.reference().number) : value; }
    CosObject defaults()
    {
        const CosObject catalog = object(table.trailer().get("Root").reference().number);
        return resolved(resolved(catalog.get("OCProperties")).get("D"));
    }
    std::set<long long> listed(const CosObject& value)
    {
        std::set<long long> out;
        const CosObject list = resolved(value);   // held: a loop over a temporary's items would dangle
        for (const CosObject& item : list.items()) {
            if (item.isReference()) out.insert(item.reference().number);
        }
        return out;
    }
    std::set<long long> groups()
    {
        const CosObject catalog = object(table.trailer().get("Root").reference().number);
        return listed(resolved(catalog.get("OCProperties")).get("OCGs"));
    }
};

std::unique_ptr<Written> readBytes(QByteArray bytes)
{
    auto out = std::make_unique<Written>();
    out->bytes = std::move(bytes);
    auto opened = pdf::cos::openWithRepair(std::string_view(out->bytes.constData(), static_cast<std::size_t>(out->bytes.size())));
    REQUIRE(opened);
    out->table = std::move(opened.value().table);
    out->layers = pdf::ops::LayerOps::read(out->table);
    return out;
}

bool save(tests::SaveSession& session, const char* name)
{
    if (!session.coordinator->save(*session.journal, session.path)) return false;
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);   // kept for `qpdf --check` (D7-18's oracle 2)
    if (!QDir().mkpath(dir)) return false;
    const QString target = QDir(dir).filePath(QStringLiteral("layers-%1.pdf").arg(QLatin1String(name)));
    QFile::remove(target);
    return QFile::copy(session.path, target);
}

bool deltaIsEmpty(tests::SaveSession& session)
{
    auto delta = session.sink()->delta();
    REQUIRE(delta);
    const bool empty = delta.value().changes().empty();
    session.sink()->composedNothing();
    return empty;
}

std::string keyOf(const core::Status& status)
{
    return status ? std::string("ok") : QString(status.error().messageKey).toStdString();
}

// Every page's `/Contents` stream numbers in `table` -- what no layer command may write again.
std::set<long long> contentStreams(pdf::cos::XrefTable& table)
{
    std::set<long long> out;
    auto walk = pdf::ops::PageOps::walk(table);
    REQUIRE(walk);
    for (const pdf::ops::PageEntry& page : walk.value().pages) {
        auto fetched = table.fetch(page.number);
        REQUIRE(fetched);
        const CosObject& contents = fetched.value().object.get("Contents");
        if (contents.isReference()) out.insert(contents.reference().number);
        for (const CosObject& item : contents.items()) {
            if (item.isReference()) out.insert(item.reference().number);
        }
    }
    return out;
}

// Every content stream of the saved file is the one the original file holds -- placed before the update, untouched.
void contentUnchanged(Written& written, std::size_t originalSize)
{
    const std::set<long long> streams = contentStreams(written.table);
    REQUIRE_FALSE(streams.empty());
    for (const long long number : streams) {
        const pdf::cos::XrefEntry* entry = written.table.find(number);
        REQUIRE(entry != nullptr);
        CHECK(entry->type == pdf::cos::XrefEntryType::InFile);
        CHECK(static_cast<std::size_t>(entry->field2) < originalSize);
    }
}

// The object a page's `/Properties` entry `key` names, in `written`.
long long propertyOf(Written& written, int pageIndex, const char* key)
{
    auto walk = pdf::ops::PageOps::walk(written.table);
    REQUIRE(walk);
    const CosObject page = written.object(walk.value().pages[static_cast<std::size_t>(pageIndex)].number);
    const CosObject properties = written.resolved(written.resolved(page.get("Resources")).get("Properties"));
    const CosObject& value = properties.get(key);
    REQUIRE(value.isReference());
    return value.reference().number;
}

// A tree of three groups -- the model's editing half, no file.
LayerTree threeGroups()
{
    LayerTree tree;
    for (int n : {3, 4, 5}) {
        Layer layer;
        layer.id = LayerId::ofObject(n);
        layer.name = QStringLiteral("G%1").arg(n);
        tree.layers().push_back(layer);
        core::LayerNode node;
        node.layer = layer.id;
        tree.order().push_back(node);
    }
    return tree;
}

} // namespace

// =====================================================================================================================
// A-105 -- the reading, against the label
// =====================================================================================================================

TEST_CASE("unit.layers: layers.pdf read first-party as its label says", "[layers]")
{
    auto session = tests::openCopy("assembly/layers.pdf");
    REQUIRE(session);
    const LayerTree& tree = session->journal->document().layers();
    REQUIRE(tree.status());
    const std::map<std::string, QJsonObject> groups = labelledGroups();
    REQUIRE(groups.size() == 8);
    CHECK(tree.layers().size() == groups.size());
    CHECK(tree.visibleCount() == groups.size());

    std::vector<LayerId> radio;
    for (const auto& [key, group] : groups) {
        INFO(key);
        const Layer* layer = tree.find(idOf(group));
        REQUIRE(layer != nullptr);
        CHECK(layer->name == group.value(QStringLiteral("name")).toString());
        CHECK(layer->locked == group.value(QStringLiteral("locked")).toBool());
        CHECK(layer->defaultOn == group.value(QStringLiteral("defaultOn")).toBool());
        CHECK_FALSE(layer->decoration);
        // Nesting: the parent the label names, or none.
        const QJsonValue parent = group.value(QStringLiteral("parent"));
        if (parent.isString()) CHECK(tree.parentOf(layer->id) == idOf(groups.at(parent.toString().toStdString())));
        else CHECK_FALSE(tree.parentOf(layer->id).isValid());
        // Usage: declared and applied, for printing and the screen.
        const QJsonValue usage = group.value(QStringLiteral("usage"));
        if (usage.isObject()) {
            const auto stateOf = [](const QJsonValue& v) { return v.toString() == QLatin1String("ON") ? LayerUsage::On : LayerUsage::Off; };
            CHECK(layer->print == stateOf(usage.toObject().value(QStringLiteral("print"))));
            CHECK(layer->view == stateOf(usage.toObject().value(QStringLiteral("view"))));
            CHECK(layer->autoPrint);
            CHECK(layer->autoView);
        } else {
            CHECK(layer->print == LayerUsage::Unset);
            CHECK(layer->view == LayerUsage::Unset);
            CHECK_FALSE(layer->autoPrint);
        }
        if (group.value(QStringLiteral("radioGroup")).isDouble()) radio.push_back(layer->id);
    }
    // The radio group, the labelled collection, and what the screen hides at open.
    REQUIRE(tree.radioGroups().size() == 1);
    CHECK(std::set<LayerId>(tree.radioGroups().front().begin(), tree.radioGroups().front().end())
          == std::set<LayerId>(radio.begin(), radio.end()));
    bool labelled = false;
    for (const core::LayerNode& node : tree.order()) {
        if (!node.isCollection()) continue;
        labelled = true;
        CHECK(node.label == QStringLiteral("Languages"));
        REQUIRE(node.children.size() == 2);
        CHECK(node.children[0].layer == idOf(groups.at("en")));
        CHECK(node.children[1].layer == idOf(groups.at("fr")));
    }
    CHECK(labelled);
    // Hidden at open: French (`/OFF`) and Print only (its view usage, applied) -- and Screen only SHOWN although its
    // print state is off, since the screen is the view event.
    CHECK(core::LayerView::atOpen(tree) == std::set<LayerId>{idOf(groups.at("fr")), idOf(groups.at("print"))});
    // The backend hands the same tree the model was hydrated from.
    const LayerTree* backend = session->backend.layers(session->handle);
    REQUIRE(backend != nullptr);
    CHECK(backend->digest() == tree.digest());
}

TEST_CASE("unit.layers: a cycle a depth and a million entries are bounded and reported", "[layers]")
{
    // A CYCLE: two `/Order` arrays naming each other.
    {
        tests::TinyPdf pdf;
        const int catalog = pdf.reserve();
        const int pages = pdf.reserve();
        const int page = pdf.reserve();
        const int g1 = pdf.add("<< /Type /OCG /Name (One) >>");
        const int g2 = pdf.add("<< /Type /OCG /Name (Two) >>");
        const int a = pdf.reserve();
        const int b = pdf.reserve();
        pdf.set(a, "[ " + std::to_string(g1) + " 0 R " + std::to_string(b) + " 0 R ]");
        pdf.set(b, "[ (Loop) " + std::to_string(g2) + " 0 R " + std::to_string(a) + " 0 R ]");
        pdf.set(page, "<< /Type /Page /Parent " + std::to_string(pages) + " 0 R /MediaBox [0 0 200 200] >>");
        pdf.set(pages, "<< /Type /Pages /Kids [ " + std::to_string(page) + " 0 R ] /Count 1 >>");
        pdf.set(catalog, "<< /Type /Catalog /Pages " + std::to_string(pages) + " 0 R /OCProperties << /OCGs [ " + std::to_string(g1)
                             + " 0 R " + std::to_string(g2) + " 0 R ] /D << /Order " + std::to_string(a) + " 0 R >> >> >>");
        const std::string bytes = pdf.bytes(catalog);
        auto opened = pdf::cos::openWithRepair(bytes);
        REQUIRE(opened);
        const LayerTree tree = pdf::ops::LayerOps::read(opened.value().table);
        REQUIRE_FALSE(tree.status());
        CHECK(keyOf(tree.status()) == "error.layer.orderCycle");
        CHECK(tree.layers().size() == 2);   // what was read before the bound is kept, shown and read-only
    }
    // TOO DEEP: forty unlabelled arrays inside each other -- each its own object, since the COS parser bounds the
    // nesting of ONE object itself and a catalog nested forty deep would be no catalog at all.
    {
        tests::TinyPdf pdf;
        const int catalog = pdf.reserve();
        const int pages = pdf.reserve();
        const int page = pdf.reserve();
        const int g1 = pdf.add("<< /Type /OCG /Name (Deep) >>");
        int inner = pdf.add("[ " + std::to_string(g1) + " 0 R ]");
        for (int i = 0; i < 40; ++i) inner = pdf.add("[ " + std::to_string(inner) + " 0 R ]");
        pdf.set(page, "<< /Type /Page /Parent " + std::to_string(pages) + " 0 R /MediaBox [0 0 200 200] >>");
        pdf.set(pages, "<< /Type /Pages /Kids [ " + std::to_string(page) + " 0 R ] /Count 1 >>");
        pdf.set(catalog, "<< /Type /Catalog /Pages " + std::to_string(pages) + " 0 R /OCProperties << /OCGs [ " + std::to_string(g1)
                             + " 0 R ] /D << /Order [ " + std::to_string(inner) + " 0 R ] >> >> >>");
        const std::string bytes = pdf.bytes(catalog);   // held: the table reads the buffer it was loaded over
        auto opened = pdf::cos::openWithRepair(bytes);
        REQUIRE(opened);
        const LayerTree tree = pdf::ops::LayerOps::read(opened.value().table);
        int depth = 0;
        for (const std::vector<core::LayerNode>* level = &tree.order(); !level->empty(); level = &level->front().children) ++depth;
        CAPTURE(depth, tree.layers().size(), tree.order().size());
        CHECK(keyOf(tree.status()) == "error.layer.tooDeep");
    }
    // A MILLION ENTRIES in `/OCGs`: read up to the bound and refused, in bounded time.
    {
        tests::TinyPdf pdf;
        const int catalog = pdf.reserve();
        const int pages = pdf.reserve();
        const int page = pdf.reserve();
        const int g1 = pdf.add("<< /Type /OCG /Name (Real) >>");
        std::string list = std::to_string(g1) + " 0 R";
        list.reserve(12 * 1000000);
        for (int n = 0; n < 1000000; ++n) list += ' ' + std::to_string(1000 + n) + " 0 R";
        pdf.set(page, "<< /Type /Page /Parent " + std::to_string(pages) + " 0 R /MediaBox [0 0 200 200] >>");
        pdf.set(pages, "<< /Type /Pages /Kids [ " + std::to_string(page) + " 0 R ] /Count 1 >>");
        pdf.set(catalog, "<< /Type /Catalog /Pages " + std::to_string(pages) + " 0 R /OCProperties << /OCGs [ " + list + " ] >> >>");
        const std::string bytes = pdf.bytes(catalog);   // held: the table reads the buffer it was loaded over
        auto opened = pdf::cos::openWithRepair(bytes);
        REQUIRE(opened);
        const auto began = std::chrono::steady_clock::now();
        const LayerTree tree = pdf::ops::LayerOps::read(opened.value().table);
        const auto took = std::chrono::steady_clock::now() - began;
        CHECK(keyOf(tree.status()) == "error.layer.tooMany");
        CHECK(tree.layers().size() == 1);
        // Not a duration graded (convention 25 would tag it): a bound read once, far from a hang.
        CHECK(took < std::chrono::seconds(30));
    }
}

// =====================================================================================================================
// The model's editing half, and the view's rule
// =====================================================================================================================

TEST_CASE("unit.layers: the tree edited -- insert remove merge and properties refuse what they must", "[layers]")
{
    LayerTree tree = threeGroups();
    const LayerId g3 = LayerId::ofObject(3), g4 = LayerId::ofObject(4), g5 = LayerId::ofObject(5);

    // Insert: under a parent, at the end; refused for a taken id, a bad name, a parent not here.
    Layer fresh;
    fresh.id = LayerId::ofCreated(QStringLiteral("a1"));
    fresh.name = QStringLiteral("Fresh");
    REQUIRE(tree.insert(fresh, g3));
    CHECK(tree.parentOf(fresh.id) == g3);
    CHECK(keyOf(tree.insert(fresh, {})) == "error.layer.badId");
    Layer blank = fresh;
    blank.id = LayerId::ofCreated(QStringLiteral("a2"));
    blank.name = QStringLiteral("   ");
    CHECK(keyOf(tree.insert(blank, {})) == "error.layer.badName");
    blank.name = QStringLiteral("Ok");
    CHECK(keyOf(tree.insert(blank, LayerId::ofObject(99))) == "error.layer.notFound");

    // Properties: nesting moves the subtree; a parent under itself refused; a radio group made of the named members.
    LayerProperties properties = LayerProperties::of(tree, g3);
    properties.parent = fresh.id;
    CHECK(keyOf(tree.setProperties(g3, properties)) == "error.layer.badParent");
    properties = LayerProperties::of(tree, g4);
    properties.parent = g5;
    properties.exclusiveWith = {g3};
    properties.print = LayerUsage::Off;
    REQUIRE(tree.setProperties(g4, properties));
    CHECK(tree.parentOf(g4) == g5);
    CHECK(tree.exclusiveWith(g4) == std::vector<LayerId>{g3});
    CHECK(tree.find(g4)->print == LayerUsage::Off);
    CHECK(tree.find(g4)->autoPrint);
    // Cleared, the usage is applied no more -- the declaration kept.
    properties.print = LayerUsage::Unset;
    REQUIRE(tree.setProperties(g4, properties));
    CHECK_FALSE(tree.find(g4)->autoPrint);
    CHECK(tree.find(g4)->print == LayerUsage::Off);

    // Remove: its children take its place; its radio group goes when it would hold one.
    REQUIRE(tree.remove(g3));
    CHECK(tree.find(g3) == nullptr);
    CHECK(tree.nodeOf(fresh.id) != nullptr);
    CHECK_FALSE(tree.parentOf(fresh.id).isValid());
    CHECK(tree.radioGroups().empty());

    // Merge: recorded, the source gone; itself and an unknown refused; the merge followed even when its target goes.
    CHECK(keyOf(tree.merge(g5, g5)) == "error.layer.mergeSelf");
    CHECK(keyOf(tree.merge(LayerId::ofObject(77), g5)) == "error.layer.notFound");
    REQUIRE(tree.merge(g4, g5));
    CHECK(tree.merges().size() == 1);
    CHECK(tree.find(g4) == nullptr);
    REQUIRE(tree.remove(g5));
    CHECK(tree.validate());

    // A watermark's group: listed, never shown, refused by every edit.
    LayerTree marked = threeGroups();
    marked.layers()[0].decoration = true;
    marked.order().erase(marked.order().begin());
    CHECK(marked.visibleCount() == 2);
    CHECK(marked.shownTree().size() == 2);
    CHECK(keyOf(marked.remove(g3)) == "error.layer.notEditable");
    CHECK(keyOf(marked.merge(g4, g3)) == "error.layer.notEditable");
    CHECK(keyOf(marked.setProperties(g3, LayerProperties::of(marked, g3))) == "error.layer.notEditable");
}

TEST_CASE("unit.layers: the view -- a radio group exclusive and a locked layer refused", "[layers]")
{
    LayerTree tree = threeGroups();
    const LayerId g3 = LayerId::ofObject(3), g4 = LayerId::ofObject(4), g5 = LayerId::ofObject(5);
    tree.radioGroups().push_back({g3, g4});
    tree.find(g5)->locked = true;
    tree.find(g4)->defaultOn = false;
    std::set<LayerId> hidden = core::LayerView::atOpen(tree);
    CHECK(hidden == std::set<LayerId>{g4});

    auto shown = core::LayerView::setShown(tree, hidden, g4, true);
    REQUIRE(shown);
    CHECK(shown.value() == std::set<LayerId>{g3});   // showing one member hides the other
    auto locked = core::LayerView::setShown(tree, shown.value(), g5, false);
    REQUIRE_FALSE(locked);
    CHECK(keyOf(core::Status{locked.error()}) == "error.layer.locked");
    auto hiddenBoth = core::LayerView::setShown(tree, shown.value(), g4, false);
    REQUIRE(hiddenBoth);
    CHECK(hiddenBoth.value() == std::set<LayerId>{g3, g4});   // hiding shows nothing else

    // The render key: equal for equal sets, never an object number for a created group.
    const LayerId created = LayerId::ofCreated(QStringLiteral("k-1"));
    const std::vector<long long> key = core::LayerView::key({g4, created});
    REQUIRE(key.size() == 2);
    CHECK(key[0] < 0);
    CHECK(key[1] == 4);
    CHECK(core::LayerView::key({created, g4}) == key);
    CHECK(core::LayerView::key({LayerId::ofCreated(QStringLiteral("k-2")), g4}) != key);
}

// =====================================================================================================================
// The four commands: what a journal line carries
// =====================================================================================================================

namespace {

void roundTrips(const core::Command& command)
{
    const QJsonObject json = command.serialize();
    auto back = core::CommandRegistry::deserialize(json);
    REQUIRE(back);
    CHECK(back.value()->serialize() == json);
}

} // namespace

TEST_CASE("unit.layers: CreateLayerCommand serializes and refuses hostile lines", "[layers][serialize:CreateLayerCommand]")
{
    Layer layer;
    layer.id = LayerId::ofCreated(QStringLiteral("0c1d2e3f-aaaa"));
    layer.name = QStringLiteral("Notes é");
    layer.print = LayerUsage::Off;
    roundTrips(core::CreateLayerCommand(layer, LayerId::ofObject(12)));
    roundTrips(core::CreateLayerCommand(layer, {}));
    QJsonObject json = core::CreateLayerCommand(layer, {}).serialize();
    json.insert(QStringLiteral("layer"), QJsonObject{{QStringLiteral("id"), QStringLiteral("o:5")}});   // not a created id
    CHECK_FALSE(core::CommandRegistry::deserialize(json));
    json = core::CreateLayerCommand(layer, {}).serialize();
    json.insert(QStringLiteral("parent"), QStringLiteral("x:1"));
    CHECK_FALSE(core::CommandRegistry::deserialize(json));
}

TEST_CASE("unit.layers: DeleteLayerCommand serializes and refuses hostile lines", "[layers][serialize:DeleteLayerCommand]")
{
    roundTrips(core::DeleteLayerCommand({LayerId::ofObject(7), LayerId::ofCreated(QStringLiteral("abc"))}));
    QJsonObject json = core::DeleteLayerCommand({LayerId::ofObject(7)}).serialize();
    json.insert(QStringLiteral("ids"), QJsonArray{QStringLiteral("o:7"), QStringLiteral("o:7")});   // twice
    CHECK_FALSE(core::CommandRegistry::deserialize(json));
    json.insert(QStringLiteral("ids"), QJsonArray{});
    CHECK_FALSE(core::CommandRegistry::deserialize(json));
}

TEST_CASE("unit.layers: MergeLayersCommand serializes and refuses hostile lines", "[layers][serialize:MergeLayersCommand]")
{
    roundTrips(core::MergeLayersCommand({LayerId::ofObject(7), LayerId::ofObject(8)}, LayerId::ofObject(9)));
    QJsonObject json = core::MergeLayersCommand({LayerId::ofObject(7)}, LayerId::ofObject(9)).serialize();
    json.insert(QStringLiteral("into"), 9);
    CHECK_FALSE(core::CommandRegistry::deserialize(json));
}

TEST_CASE("unit.layers: SetLayerPropertiesCommand serializes and refuses hostile lines", "[layers][serialize:SetLayerPropertiesCommand]")
{
    LayerProperties properties;
    properties.name = QStringLiteral("Base");
    properties.defaultOn = false;
    properties.locked = true;
    properties.exportUsage = LayerUsage::On;
    properties.parent = LayerId::ofObject(3);
    properties.exclusiveWith = {LayerId::ofObject(4), LayerId::ofCreated(QStringLiteral("q"))};
    roundTrips(core::SetLayerPropertiesCommand(LayerId::ofObject(5), properties));
    QJsonObject json = core::SetLayerPropertiesCommand(LayerId::ofObject(5), properties).serialize();
    QJsonObject bad = json.value(QStringLiteral("properties")).toObject();
    bad.insert(QStringLiteral("print"), QStringLiteral("sometimes"));
    json.insert(QStringLiteral("properties"), bad);
    CHECK_FALSE(core::CommandRegistry::deserialize(json));
}

// =====================================================================================================================
// A-107 -- each command through the shipped sink: reverted, saved, read back
// =====================================================================================================================

namespace {

// Apply, then undo: the model as it was, and nothing left to write.
void revertsExactly(std::unique_ptr<core::Command> command)
{
    auto session = tests::openCopy("assembly/layers.pdf");
    REQUIRE(session);
    const QByteArray before = session->journal->document().modelHash();
    REQUIRE(session->journal->run(std::move(command)));
    CHECK(session->journal->document().modelHash() != before);
    CHECK_FALSE(deltaIsEmpty(*session));
    REQUIRE(session->journal->undo());
    CHECK(session->journal->document().modelHash() == before);
    CHECK(deltaIsEmpty(*session));
}

} // namespace

TEST_CASE("unit.layers: CreateLayerCommand reverts exactly", "[layers][revert:CreateLayerCommand]")
{
    Layer layer;
    layer.id = LayerId::ofCreated(QStringLiteral("r1"));
    layer.name = QStringLiteral("Undone");
    revertsExactly(std::make_unique<core::CreateLayerCommand>(layer, LayerId{}));
}

TEST_CASE("unit.layers: DeleteLayerCommand reverts exactly", "[layers][revert:DeleteLayerCommand]")
{
    revertsExactly(std::make_unique<core::DeleteLayerCommand>(std::vector<LayerId>{idOf(labelledGroups().at("notes"))}));
}

TEST_CASE("unit.layers: MergeLayersCommand reverts exactly", "[layers][revert:MergeLayersCommand]")
{
    const auto groups = labelledGroups();
    revertsExactly(std::make_unique<core::MergeLayersCommand>(std::vector<LayerId>{idOf(groups.at("notes"))}, idOf(groups.at("markup"))));
}

TEST_CASE("unit.layers: SetLayerPropertiesCommand reverts exactly", "[layers][revert:SetLayerPropertiesCommand]")
{
    auto session = tests::openCopy("assembly/layers.pdf");
    REQUIRE(session);
    const LayerId notes = idOf(labelledGroups().at("notes"));
    LayerProperties properties = LayerProperties::of(session->journal->document().layers(), notes);
    properties.name = QStringLiteral("Renamed");
    revertsExactly(std::make_unique<core::SetLayerPropertiesCommand>(notes, properties));
}

TEST_CASE("unit.layers.commands: a merge repoints every Properties entry and changes no content stream", "[layers]")
{
    auto session = tests::openCopy("assembly/layers.pdf");
    REQUIRE(session);
    const auto groups = labelledGroups();
    const LayerId notes = idOf(groups.at("notes")), markup = idOf(groups.at("markup"));
    REQUIRE(session->journal->run(std::make_unique<core::MergeLayersCommand>(std::vector<LayerId>{notes}, markup)));
    REQUIRE(save(*session, "merge"));
    auto written = readBytes(tests::readAllBytes(session->path));
    // The merged group out of every list; the survivor kept.
    CHECK(written->groups().count(notes.object) == 0);
    CHECK(written->groups().count(markup.object) == 1);
    CHECK(written->layers.find(notes) == nullptr);
    for (const char* key : {"ON", "OFF", "Locked"}) CHECK(written->listed(written->defaults().get(key)).count(notes.object) == 0);
    // EVERY `/Properties` entry that named it now names the survivor: page 1's `/oc2` (Reviewer notes) and `/oc3` (Markup).
    CHECK(propertyOf(*written, 0, "oc2") == markup.object);
    CHECK(propertyOf(*written, 0, "oc3") == markup.object);
    CHECK(propertyOf(*written, 0, "oc1") == idOf(groups.at("base")).object);
    // And no content stream written again (D7-9: no content stream touched).
    contentUnchanged(*written, static_cast<std::size_t>(session->openedBytes.size()));
}

TEST_CASE("unit.layers.commands: a delete orphans the content and a create adds an empty group", "[layers]")
{
    auto session = tests::openCopy("assembly/layers.pdf");
    REQUIRE(session);
    const auto groups = labelledGroups();
    const LayerId fr = idOf(groups.at("fr"));
    REQUIRE(session->journal->run(std::make_unique<core::DeleteLayerCommand>(std::vector<LayerId>{fr})));
    Layer layer;
    layer.id = LayerId::ofCreated(QStringLiteral("new-one"));
    layer.name = QStringLiteral("Added");
    REQUIRE(session->journal->run(std::make_unique<core::CreateLayerCommand>(layer, LayerId{})));
    REQUIRE(save(*session, "delete-create"));
    auto written = readBytes(tests::readAllBytes(session->path));
    // Deleted: out of `/OCGs` and every list of `/D` -- the radio group with it -- and still named by page 2's resource.
    CHECK(written->groups().count(fr.object) == 0);
    for (const char* key : {"ON", "OFF", "Locked"}) CHECK(written->listed(written->defaults().get(key)).count(fr.object) == 0);
    CHECK(written->layers.radioGroups().empty());
    CHECK(propertyOf(*written, 1, "fr") == fr.object);   // ORPHANED: the content still names it
    contentUnchanged(*written, static_cast<std::size_t>(session->openedBytes.size()));
    // Created: one new group, listed at the top level of the order, named, and named by no page.
    std::vector<const Layer*> added;
    for (const Layer& l : written->layers.layers()) {
        if (l.name == QStringLiteral("Added")) added.push_back(&l);
    }
    REQUIRE(added.size() == 1);
    CHECK(written->layers.nodeOf(added.front()->id) != nullptr);
    CHECK_FALSE(written->layers.parentOf(added.front()->id).isValid());
    CHECK(added.front()->defaultOn);
    CHECK(written->layers.layers().size() == 8);   // eight, less French, plus one
}

TEST_CASE("unit.layers.commands: properties written and read back", "[layers]")
{
    auto session = tests::openCopy("assembly/layers.pdf");
    REQUIRE(session);
    const auto groups = labelledGroups();
    const LayerId notes = idOf(groups.at("notes")), markup = idOf(groups.at("markup")), base = idOf(groups.at("base"));
    LayerProperties properties = LayerProperties::of(session->journal->document().layers(), notes);
    CHECK(properties.parent == idOf(groups.at("annotations")));
    properties.name = QStringLiteral("Notes über alles");
    properties.defaultOn = false;
    properties.locked = true;
    properties.print = LayerUsage::Off;
    properties.exportUsage = LayerUsage::On;
    properties.parent = base;
    properties.exclusiveWith = {markup};
    REQUIRE(session->journal->run(std::make_unique<core::SetLayerPropertiesCommand>(notes, properties)));
    // Nothing changed is refused, by key.
    CHECK(keyOf(session->journal->run(std::make_unique<core::SetLayerPropertiesCommand>(notes, properties))) == "error.layer.unchanged");
    REQUIRE(save(*session, "properties"));
    auto written = readBytes(tests::readAllBytes(session->path));
    REQUIRE(written->layers.status());
    const Layer* layer = written->layers.find(notes);
    REQUIRE(layer != nullptr);
    CHECK(layer->name == properties.name);
    CHECK_FALSE(layer->defaultOn);
    CHECK(layer->locked);
    CHECK(layer->print == LayerUsage::Off);
    CHECK(layer->autoPrint);
    CHECK(layer->exportUsage == LayerUsage::On);
    CHECK(layer->autoExport);
    CHECK(written->layers.parentOf(notes) == base);
    CHECK(written->layers.exclusiveWith(notes) == std::vector<LayerId>{markup});
    // Every other group as it was.
    for (const char* key : {"en", "fr", "print", "screen"}) {
        INFO(key);
        const Layer* other = written->layers.find(idOf(groups.at(key)));
        REQUIRE(other != nullptr);
        CHECK(other->name == groups.at(key).value(QStringLiteral("name")).toString());
        CHECK(other->defaultOn == groups.at(key).value(QStringLiteral("defaultOn")).toBool());
    }
    contentUnchanged(*written, static_cast<std::size_t>(session->openedBytes.size()));
    // A second session's model is what was written: the model and the file agree.
    auto reopened = tests::openCopy("assembly/layers.pdf");
    REQUIRE(reopened);
}

TEST_CASE("unit.layers.commands: a watermark group and groups the model does not know are kept", "[layers]")
{
    // A REAL PRINT-ONLY WATERMARK (T15, D7-45(a)) -- applied, saved, and the file opened again: its group is in the model,
    // marked, shown in no pane and refused by every command; a layer edit in the same session as the watermark, and one
    // after reopening, each keep it listed with its usage applied.
    {
        auto session = tests::openCopy("assembly/layers.pdf");
        REQUIRE(session);
        core::WatermarkSpec spec;
        spec.text = QStringLiteral("DRAFT");
        spec.size = 48;
        spec.usage = core::WatermarkUsage::PrintOnly;
        REQUIRE(session->journal->run(std::make_unique<core::ApplyWatermarkCommand>(spec)));
        const LayerId notes = idOf(labelledGroups().at("notes"));
        LayerProperties renamed = LayerProperties::of(session->journal->document().layers(), notes);
        renamed.name = QStringLiteral("Beside a watermark");
        REQUIRE(session->journal->run(std::make_unique<core::SetLayerPropertiesCommand>(notes, renamed)));
        REQUIRE(save(*session, "watermarked"));
        auto first = readBytes(tests::readAllBytes(session->path));
        REQUIRE(first->layers.layers().size() == 9);
        const Layer* ours = nullptr;
        for (const Layer& l : first->layers.layers()) {
            if (l.decoration) ours = &l;
        }
        REQUIRE(ours != nullptr);
        CHECK(ours->print == LayerUsage::On);
        CHECK(ours->autoView);
        CHECK(first->layers.find(notes)->name == QStringLiteral("Beside a watermark"));
        const LayerId watermark = ours->id;

        auto again = tests::openBuilt(QStringLiteral("watermarked.pdf"), first->bytes.toStdString());
        REQUIRE(again);
        const LayerTree& tree = again->journal->document().layers();
        CHECK(tree.visibleCount() == 8);
        CHECK(tree.find(watermark)->decoration);
        CHECK(keyOf(again->journal->run(std::make_unique<core::DeleteLayerCommand>(std::vector<LayerId>{watermark})))
              == "error.layer.notEditable");
        REQUIRE(again->journal->run(std::make_unique<core::DeleteLayerCommand>(std::vector<LayerId>{notes})));
        REQUIRE(save(*again, "watermarked-again"));
        auto second = readBytes(tests::readAllBytes(again->path));
        CHECK(second->groups().count(watermark.object) == 1);
        CHECK(second->groups().count(notes.object) == 0);
        const Layer* kept = second->layers.find(watermark);
        REQUIRE(kept != nullptr);
        CHECK(kept->decoration);
        CHECK(kept->autoPrint);
        CHECK(kept->autoView);
    }

    // Two groups, one marked as a watermark's -- by hand, so no page draws a watermark: the model lists it and every
    // command refuses it.
    tests::TinyPdf pdf;
    const int catalog = pdf.reserve();
    const int pages = pdf.reserve();
    const int page = pdf.reserve();
    const int mine = pdf.add("<< /Type /OCG /Name (Mine) >>");
    const int mark = pdf.add("<< /Type /OCG /Name (Watermark) /RawPageDecoration << /Kind /watermark /Id <6162> >> >>");
    const int contents = pdf.add("<< /Length 45 >>\nstream\n/OC /m BDC 0 0 m 10 10 l S EMC /OC /w BDC EMC\nendstream");
    pdf.set(page, "<< /Type /Page /Parent " + std::to_string(pages) + " 0 R /MediaBox [0 0 200 200] /Contents " + std::to_string(contents)
                      + " 0 R /Resources << /Properties << /m " + std::to_string(mine) + " 0 R /w " + std::to_string(mark) + " 0 R >> >> >>");
    pdf.set(pages, "<< /Type /Pages /Kids [ " + std::to_string(page) + " 0 R ] /Count 1 >>");
    pdf.set(catalog, "<< /Type /Catalog /Pages " + std::to_string(pages) + " 0 R /OCProperties << /OCGs [ " + std::to_string(mine) + " 0 R "
                         + std::to_string(mark) + " 0 R ] /D << /Order [ " + std::to_string(mine) + " 0 R ] >> >> >>");
    auto session = tests::openBuilt(QStringLiteral("marked.pdf"), pdf.bytes(catalog));
    REQUIRE(session);
    const LayerTree& tree = session->journal->document().layers();
    REQUIRE(tree.layers().size() == 2);
    CHECK(tree.find(LayerId::ofObject(mark))->decoration);
    CHECK(tree.visibleCount() == 1);
    CHECK(keyOf(session->journal->run(std::make_unique<core::DeleteLayerCommand>(std::vector<LayerId>{LayerId::ofObject(mark)})))
          == "error.layer.notEditable");
    LayerProperties properties = LayerProperties::of(tree, LayerId::ofObject(mine));
    properties.name = QStringLiteral("Still mine");
    REQUIRE(session->journal->run(std::make_unique<core::SetLayerPropertiesCommand>(LayerId::ofObject(mine), properties)));
    REQUIRE(save(*session, "marked"));
    auto written = readBytes(tests::readAllBytes(session->path));
    CHECK(written->layers.find(LayerId::ofObject(mine))->name == QStringLiteral("Still mine"));
    // A marked group no page's watermark draws is taken out of the lists -- T15's compose, not this one: what makes a
    // watermark's group the sink's is the watermark that draws it (D7-45), and here none does.
    CHECK(written->groups() == std::set<long long>{mine});

    // THE COMPOSE KEEPS A GROUP THE MODEL DOES NOT KNOW: listed now, neither the model's nor removed -- as a page
    // inserted this session brings one (Stage 7 guide I-07).
    const std::string bytes = pdf.bytes(catalog);   // held: the table reads the buffer it was loaded over
        auto opened = pdf::cos::openWithRepair(bytes);
    REQUIRE(opened);
    LayerTree model = pdf::ops::LayerOps::read(opened.value().table);
    model.layers().erase(model.layers().begin() + 1);   // the model knows only `mine`
    const CosObject catalogObject = [&] {
        auto fetched = opened.value().table.fetch(catalog);
        REQUIRE(fetched);
        CosObject out = fetched.value().object;
        out.clearSpansRecursively();
        return out;
    }();
    const CosObject properties2 = catalogObject.get("OCProperties");
    auto composed = pdf::ops::LayerOps::compose(opened.value().table, properties2, properties2.get("D"), model, {},
                                                [](const LayerId& id) -> std::optional<CosObject> {
                                                    return CosObject::makeReference(id.object, 0);
                                                });
    REQUIRE(composed);
    std::vector<long long> listed;
    for (const CosObject& item : composed.value().groups.items()) listed.push_back(item.reference().number);
    CHECK(listed == std::vector<long long>{mine, mark});
    // ...and one REMOVED is out.
    composed = pdf::ops::LayerOps::compose(opened.value().table, properties2, properties2.get("D"), model, {mark},
                                           [](const LayerId& id) -> std::optional<CosObject> { return CosObject::makeReference(id.object, 0); });
    REQUIRE(composed);
    CHECK(composed.value().groups.items().size() == 1);
}

TEST_CASE("unit.layers.commands: every command reversible over the corpus files with layers", "[layers]")
{
    int files = 0;
    for (const char* relative : {"assembly/layers.pdf", "assembly/merge-a.pdf", "assembly/merge-b.pdf", "assembly/merge-c.pdf"}) {
        INFO(relative);
        auto session = tests::openCopy(relative);
        REQUIRE(session);
        const LayerTree tree = session->journal->document().layers();
        REQUIRE(tree.status());
        REQUIRE(tree.visibleCount() >= 2);
        ++files;
        const QByteArray before = session->journal->document().modelHash();
        std::vector<LayerId> ids;
        for (const Layer& layer : tree.layers()) {
            if (!layer.decoration) ids.push_back(layer.id);
        }
        Layer layer;
        layer.id = LayerId::ofCreated(QStringLiteral("sweep"));
        layer.name = QStringLiteral("Sweep");
        REQUIRE(session->journal->run(std::make_unique<core::CreateLayerCommand>(layer, ids.front())));
        LayerProperties properties = LayerProperties::of(session->journal->document().layers(), ids.front());
        properties.name = QStringLiteral("Swept");
        properties.locked = !properties.locked;
        REQUIRE(session->journal->run(std::make_unique<core::SetLayerPropertiesCommand>(ids.front(), properties)));
        REQUIRE(session->journal->run(std::make_unique<core::MergeLayersCommand>(std::vector<LayerId>{ids.back()}, layer.id)));
        REQUIRE(session->journal->run(std::make_unique<core::DeleteLayerCommand>(std::vector<LayerId>{layer.id})));
        CHECK_FALSE(deltaIsEmpty(*session));
        for (int i = 0; i < 4; ++i) REQUIRE(session->journal->undo());
        CHECK(session->journal->document().modelHash() == before);
        CHECK(deltaIsEmpty(*session));
    }
    CHECK(files == 4);
}

// =====================================================================================================================
// A-106's headless half -- the view is no edit
// =====================================================================================================================

TEST_CASE("layers.view_state: a view composed after the document writes nothing and dirties nothing", "[layers]")
{
    auto session = tests::openCopy("assembly/layers.pdf");
    REQUIRE(session);
    const auto groups = labelledGroups();
    const LayerId notes = idOf(groups.at("notes")), print = idOf(groups.at("print"));
    REQUIRE(session->backend.showLayers(session->handle, {notes}));
    const QByteArray view = session->backend.shownView(session->handle);
    REQUIRE_FALSE(view.isEmpty());
    // The view, read as a reader would: notes off, everything else on -- the print-only group too, since the screen's
    // `/AS` applications are gone -- and the print application kept.
    auto viewed = readBytes(session->openedBytes + view);
    const CosObject defaults = viewed->defaults();
    CHECK(defaults.get("BaseState").text() == "ON");
    CHECK(viewed->listed(defaults.get("OFF")) == std::set<long long>{notes.object});
    CHECK(defaults.find("ON") == nullptr);
    bool printApplication = false;
    const CosObject applications = viewed->resolved(defaults.get("AS"));
    for (const CosObject& entry : applications.items()) {
        const CosObject application = viewed->resolved(entry);
        CHECK(application.get("Event").text() != "View");
        printApplication = printApplication || application.get("Event").text() == "Print";
    }
    CHECK(printApplication);
    CHECK(core::LayerView::atOpen(viewed->layers).count(print) == 0);
    // AND IT IS NO EDIT: the model, the journal and the sink as they were; a save writes the file it opened.
    CHECK_FALSE(session->journal->isDirty());
    CHECK(deltaIsEmpty(*session));
    CHECK(session->journal->document().layers().digest() == session->backend.layers(session->handle)->digest());
    // A view over an edited document comes after the edit's update: the edit's still there, the view's on top.
    LayerProperties properties = LayerProperties::of(session->journal->document().layers(), notes);
    properties.name = QStringLiteral("Edited");
    REQUIRE(session->journal->run(std::make_unique<core::SetLayerPropertiesCommand>(notes, properties)));
    REQUIRE(session->backend.showEdits(session->handle, {}));   // what a window's journal asks after every command
    REQUIRE(session->backend.settleEdits(session->handle));
    const QByteArray update = session->backend.shownUpdate(session->handle);
    REQUIRE_FALSE(update.isEmpty());
    REQUIRE(session->backend.showLayers(session->handle, {notes}));
    auto both = readBytes(session->openedBytes + update + session->backend.shownView(session->handle));
    CHECK(both->layers.find(notes)->name == QStringLiteral("Edited"));
    CHECK(both->listed(both->defaults().get("OFF")) == std::set<long long>{notes.object});
    REQUIRE(save(*session, "view"));
    auto saved = readBytes(tests::readAllBytes(session->path));
    // Saved: the edit, and the document's own default -- French and Print only off -- not the view's.
    CHECK(saved->layers.find(notes)->name == QStringLiteral("Edited"));
    CHECK(saved->listed(saved->defaults().get("OFF")) == std::set<long long>{idOf(groups.at("fr")).object, print.object});
}
