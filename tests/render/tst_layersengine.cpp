// tests/render/tst_layersengine.cpp -- the layers as PDFIUM draws them, through the SHIPPED backend. Stage 7 T17, decision
// D7-9; guide cases A-105 (its engine half), A-106 and A-107's PDFium half (`Stage7-Assembly-Testing-Guide.md` §5.19).
//
// THE ORACLE (D7-18) is the RASTER: each of `layers.pdf`'s groups draws one caption at a place the generator chose
// (`corpus/make-assembly.py`, `layers()`), and a group is DRAWN exactly when that caption's band of the page has ink. The
// model, the view and the commands are not consulted to decide what should be on the page -- the label says which group
// a band is, the band says whether the engine drew it.
//
// WHAT IT DOES NOT COVER (convention 19). The view is handed to the engine through `IDocumentBackend::showLayers` itself;
// that a person's click reaches it -- the pane's eye, the scheduler keyed first -- is `tests/ui/tst_layerspane.cpp`'s.
// Acrobat's drawing of the same files is M-10's (P-1), never assumed equal to PDFium's -- which is the point of D7-9's
// *measured in both*.

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/backend.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/StructureCommands.h"
#include "core/doc/Document.h"
#include "core/doc/LayerTree.h"
#include "core/doc/SaveCoordinator.h"
#include "core/render/RenderRequest.h"
#include "pdf/DocumentBackendImpl.h"
#include "platform/win/WinDurableFiles.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using rawpage::test_oracle::corpusPath;
using core::LayerId;

namespace fs = std::filesystem;

namespace {

constexpr double kScale = 1.0;

struct Live {
    QTemporaryDir scratch;
    pdf::DocumentBackendImpl backend;
    platform::win::WinDurableFiles durableFiles;
    QString path;
    QByteArray opened;
    core::DocumentHandle handle;
    std::unique_ptr<core::CommandJournal> journal;
    std::unique_ptr<core::SaveCoordinator> coordinator;
};

std::unique_ptr<Live> openLive()
{
    auto live = std::make_unique<Live>();
    REQUIRE(live->scratch.isValid());
    live->path = live->scratch.filePath(QStringLiteral("layers.pdf"));
    REQUIRE(QFile::copy(corpusPath("assembly/layers.pdf"), live->path));
    QFile::setPermissions(live->path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    {
        QFile file(live->path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        live->opened = file.readAll();
    }
    auto handle = live->backend.open(live->path, nullptr);
    REQUIRE(handle);
    live->handle = handle.value();
    core::Document document;
    REQUIRE(document.hydrate(live->backend, live->handle));
    live->journal = std::make_unique<core::CommandJournal>(std::move(document), live->backend.mutationSink(live->handle));
    Live* self = live.get();
    live->journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); },
                                  [self] { return self->backend.settleEdits(self->handle); });
    live->coordinator = std::make_unique<core::SaveCoordinator>(live->backend, &live->durableFiles);
    return live;
}

// Each labelled group by its key: its id, the page it draws on and its caption's row there.
struct Group {
    LayerId id;
    int page = 0;
    int row = 0;
};
std::map<std::string, Group> groups()
{
    QFile file(corpusPath("labels/assembly-layers.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonArray all = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("groups")).toArray();
    // The order `layers()` draws each page's captions in: page 1 base, notes, markup; page 2 en, fr, print, screen.
    const std::map<std::string, std::pair<int, int>> where = {{"base", {0, 0}},  {"notes", {0, 1}}, {"markup", {0, 2}},
                                                              {"en", {1, 0}},    {"fr", {1, 1}},    {"print", {1, 2}},
                                                              {"screen", {1, 3}}};
    std::map<std::string, Group> out;
    for (const QJsonValue value : all) {
        const std::string key = value.toObject().value(QStringLiteral("key")).toString().toStdString();
        const auto at = where.find(key);
        if (at == where.end()) continue;   // `annotations`: a parent no page draws
        out.emplace(key, Group{LayerId::ofObject(value.toObject().value(QStringLiteral("object")).toInteger()), at->second.first,
                               at->second.second});
    }
    REQUIRE(out.size() == 7);
    return out;
}

core::RasterImage raster(core::IDocumentBackend& backend, core::DocumentHandle handle, int index, std::vector<long long> key = {})
{
    const core::Page* page = backend.pages(handle)->at(index);
    REQUIRE(page != nullptr);
    const core::PdfSize shown = page->displaySize();
    core::render::RenderRequest request;
    request.document = handle;
    request.pageIndex = index;
    request.deviceScale = kScale;
    request.revision = core::Revision::initial();
    request.flags = core::render::RenderFlags::Annotations;
    request.hiddenLayers = std::move(key);
    request.tile = core::render::TileRect{0, 0, static_cast<int>(std::ceil(shown.width * kScale)), static_cast<int>(std::ceil(shown.height * kScale))};
    auto image = backend.rasterizer(handle)->raster(request);
    REQUIRE(image);
    return std::move(image).value();
}

// The ink -- pixels not white -- in a caption's band: row `row` of `layers()`'s captions, baseline 700 - 24 * row, 12 pt.
int inkOfRow(const core::RasterImage& image, int row)
{
    const double pageHeight = image.height / kScale;
    const double baseline = 700.0 - 24.0 * row;
    const int top = static_cast<int>((pageHeight - (baseline + 13.0)) * kScale);
    const int bottom = static_cast<int>(std::ceil((pageHeight - (baseline - 4.0)) * kScale));
    const int left = static_cast<int>(70.0 * kScale);
    const int right = static_cast<int>(330.0 * kScale);
    int count = 0;
    for (int y = std::max(0, top); y < std::min(image.height, bottom); ++y) {
        const auto* line = reinterpret_cast<const unsigned char*>(image.pixels.constData()) + static_cast<std::ptrdiff_t>(y) * image.bytesPerLine;
        for (int x = left; x < std::min(image.width, right); ++x) {
            const unsigned char* px = line + x * 4;
            if (px[0] < 200 || px[1] < 200 || px[2] < 200) ++count;
        }
    }
    return count;
}

// Which captions are drawn, by key, on the engine of `handle` -- under the view key `key`.
std::set<std::string> drawn(core::IDocumentBackend& backend, core::DocumentHandle handle, std::vector<long long> key = {})
{
    std::set<std::string> out;
    const std::map<std::string, Group> all = groups();
    const core::RasterImage pages[2] = {raster(backend, handle, 0, key), raster(backend, handle, 1, key)};
    for (const auto& [name, group] : all) {
        if (inkOfRow(pages[group.page], group.row) > 20) out.insert(name);
    }
    return out;
}

std::string sha256(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

QByteArray fileBytes(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

// The view `hidden` handed to the engine, as the window hands it (`DocumentWindow::setLayerShown`): the key it is drawn under.
std::vector<long long> show(Live& live, const std::set<LayerId>& hidden)
{
    REQUIRE(live.backend.showLayers(live.handle, std::vector<LayerId>(hidden.begin(), hidden.end())));
    return core::LayerView::key(hidden);
}

} // namespace

TEST_CASE("render.layers: the default view is drawn as the file says", "[layers]")
{
    auto live = openLive();
    // French off by `/OFF`; Print only off on screen by its view usage, which `/AS` applies; Screen only on, though its
    // print state is off. Everything else drawn.
    CHECK(drawn(live->backend, live->handle) == std::set<std::string>{"base", "notes", "markup", "en", "screen"});
    CHECK(core::LayerView::atOpen(live->journal->document().layers()).size() == 2);
}

TEST_CASE("layers.view_state: showing and hiding change the picture and no byte", "[layers]")
{
    auto live = openLive();
    const std::map<std::string, Group> g = groups();
    const core::LayerTree& tree = live->journal->document().layers();
    std::set<LayerId> hidden = core::LayerView::atOpen(tree);

    // Hidden: the reviewer notes go from the page.
    auto next = core::LayerView::setShown(tree, hidden, g.at("notes").id, false);
    REQUIRE(next);
    hidden = next.value();
    CHECK(drawn(live->backend, live->handle, show(*live, hidden)) == std::set<std::string>{"base", "markup", "en", "screen"});
    // A radio group: French shown, English hidden with it.
    next = core::LayerView::setShown(tree, hidden, g.at("fr").id, true);
    REQUIRE(next);
    hidden = next.value();
    CHECK(drawn(live->backend, live->handle, show(*live, hidden)) == std::set<std::string>{"base", "markup", "fr", "screen"});
    // A print-only group shown on screen: drawn, since the view drops the screen's `/AS` application.
    next = core::LayerView::setShown(tree, hidden, g.at("print").id, true);
    REQUIRE(next);
    hidden = next.value();
    CHECK(drawn(live->backend, live->handle, show(*live, hidden)) == std::set<std::string>{"base", "markup", "fr", "print", "screen"});
    // Shown again: the notes back.
    next = core::LayerView::setShown(tree, hidden, g.at("notes").id, true);
    REQUIRE(next);
    hidden = next.value();
    const std::vector<long long> key = show(*live, hidden);
    CHECK(drawn(live->backend, live->handle, key) == std::set<std::string>{"base", "notes", "markup", "fr", "print", "screen"});
    // The LOCKED group refuses a toggle: its state, and the picture, unchanged.
    CHECK_FALSE(core::LayerView::setShown(tree, hidden, g.at("base").id, false));
    CHECK(drawn(live->backend, live->handle, key).count("base") == 1);

    // AND NO BYTE: nothing dirtied, the file on disk the file opened -- and the revision Save would write, written, is the
    // file's own bytes and nothing after them. (Save itself has nothing to do for a document no command changed.)
    CHECK_FALSE(live->journal->isDirty());
    CHECK(live->journal->document().layers().digest() == live->backend.layers(live->handle)->digest());
    CHECK(sha256(fileBytes(live->path)) == sha256(live->opened));
    const QString revision = live->scratch.filePath(QStringLiteral("revision.pdf"));
    REQUIRE(live->backend.writeRevision(live->handle, revision, core::SaveForm::Append));
    CHECK(sha256(fileBytes(revision)) == sha256(live->opened));

    // An EDIT keeps the view: the notes renamed, the engine reloaded with the update, the view drawn after it.
    next = core::LayerView::setShown(tree, hidden, g.at("notes").id, false);
    REQUIRE(next);
    hidden = next.value();
    const std::vector<long long> hiddenNotes = show(*live, hidden);
    core::LayerProperties properties = core::LayerProperties::of(tree, g.at("markup").id);
    properties.name = QStringLiteral("Markup, renamed");
    REQUIRE(live->journal->run(std::make_unique<core::SetLayerPropertiesCommand>(g.at("markup").id, properties)));
    CHECK(drawn(live->backend, live->handle, hiddenNotes) == std::set<std::string>{"base", "markup", "fr", "print", "screen"});
}

TEST_CASE("render.layers.commands: a delete orphans and a merge repoints as PDFium draws them", "[layers]")
{
    const std::map<std::string, Group> g = groups();

    // DELETED, its content orphaned (D7-9 -- MEASURED, not assumed): French, hidden by `/OFF`, deleted -- PDFium then
    // draws its caption. A group no `/OCGs` lists is not in `/OFF` any more, and `/BaseState /ON` puts it on: PDFium
    // reads a group's state from the configuration's lists and never asks whether `/OCGs` lists it (measured at T17).
    {
        auto live = openLive();
        REQUIRE(live->journal->run(std::make_unique<core::DeleteLayerCommand>(std::vector<LayerId>{g.at("fr").id})));
        CHECK(drawn(live->backend, live->handle).count("fr") == 1);
        // And with a view hiding it: the view names only groups the file lists, so the orphan is drawn there too.
        std::set<LayerId> hidden = core::LayerView::atOpen(live->journal->document().layers());
        hidden.insert(g.at("fr").id);
        CHECK(drawn(live->backend, live->handle, show(*live, hidden)).count("fr") == 1);
        // What the screen shows is what Save writes: a fresh engine on the saved file draws it the same.
        REQUIRE(live->coordinator->save(*live->journal, live->path));
        pdf::DocumentBackendImpl fresh;
        auto handle = fresh.open(live->path, nullptr);
        REQUIRE(handle);
        CHECK(drawn(fresh, handle.value()).count("fr") == 1);
        CHECK(drawn(fresh, handle.value()).count("en") == 1);
    }

    // MERGED: the notes into the markup -- hiding the markup hides both; before the merge it hid one (the control).
    {
        auto live = openLive();
        std::set<LayerId> hidden = core::LayerView::atOpen(live->journal->document().layers());
        hidden.insert(g.at("markup").id);
        CHECK(drawn(live->backend, live->handle, show(*live, hidden)).count("notes") == 1);
        REQUIRE(live->journal->run(std::make_unique<core::MergeLayersCommand>(std::vector<LayerId>{g.at("notes").id}, g.at("markup").id)));
        const std::set<std::string> after = drawn(live->backend, live->handle, core::LayerView::key(hidden));
        CHECK(after.count("notes") == 0);
        CHECK(after.count("markup") == 0);
        CHECK(after.count("base") == 1);
        // Saved, opened fresh, the markup hidden by the file itself: both captions gone.
        core::LayerProperties properties = core::LayerProperties::of(live->journal->document().layers(), g.at("markup").id);
        properties.defaultOn = false;
        REQUIRE(live->journal->run(std::make_unique<core::SetLayerPropertiesCommand>(g.at("markup").id, properties)));
        REQUIRE(live->coordinator->save(*live->journal, live->path));
        pdf::DocumentBackendImpl fresh;
        auto handle = fresh.open(live->path, nullptr);
        REQUIRE(handle);
        const std::set<std::string> saved = drawn(fresh, handle.value());
        CHECK(saved.count("notes") == 0);
        CHECK(saved.count("markup") == 0);
        CHECK(saved.count("base") == 1);
    }
}
