// tests/render/tst_destinations.cpp -- the named destinations' two readings, and what PDFium reads of an edited table.
// Stage 7 T12, guide A-82's PDFium half (`Stage7-Assembly-Testing-Guide.md` §5.13), decisions D7-10 and D7-35.
//
// THE ORACLE IS THE ENGINE (D7-18's rule): each corpus file is opened by the SHIPPED backend, which reads its named
// destinations both ways and holds the two to agree; this suite reads PDFium's table itself, through `PdfiumDocument`, and
// grades the backend's answer against it name by name and page by page. And a table the product EDITED and saved is read
// back by a fresh PDFium: every name, each to the page that shows the identity it should -- created, renamed, and the
// links renamed with it still landing.
//
// WHAT IT DOES NOT COVER (convention 19): that an edit writes only what changed, each entry in its table, is
// `tests/unit/tst_destinations.cpp`'s; the pane is `tests/ui/tst_linktool.cpp`'s.

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/DestinationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/LinkActions.h"
#include "core/doc/SaveCoordinator.h"
#include "core/text/PageTextModel.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/ops/DestinationReader.h"
#include "pdf/render/PdfiumDocument.h"
#include "platform/win/WinDurableFiles.h"

#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using namespace rawpage::test_oracle;

namespace {

namespace fs = std::filesystem;

std::vector<std::string> corpusPdfs()
{
    std::vector<std::string> out;
    for (const auto& entry : fs::recursive_directory_iterator(fs::path(RAWPAGE_CORPUS_DIR))) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") continue;
        out.push_back(fs::relative(entry.path(), fs::path(RAWPAGE_CORPUS_DIR)).generic_string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::unique_ptr<pdf::render::PdfiumDocument> engineOpen(const QString& path)
{
    auto opened = pdf::render::PdfiumDocument::open(path, {});
    REQUIRE(opened);
    return std::move(opened).value();
}

struct Live {
    QTemporaryDir scratch;
    pdf::DocumentBackendImpl backend;
    platform::win::WinDurableFiles durableFiles;
    QString path;
    core::DocumentHandle handle;
    std::unique_ptr<core::CommandJournal> journal;
    std::unique_ptr<core::SaveCoordinator> coordinator;
    const core::Document& document() const { return journal->document(); }
};

std::unique_ptr<Live> openLive(const std::string& relative)
{
    auto live = std::make_unique<Live>();
    REQUIRE(live->scratch.isValid());
    live->path = live->scratch.filePath(QStringLiteral("document.pdf"));
    REQUIRE(QFile::copy(corpusPath(relative), live->path));
    QFile::setPermissions(live->path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
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

// The identity page `index` SHOWS, as the engine extracts it (D7-17) -- through a backend opened on the file.
QString identityOn(core::IDocumentBackend& backend, core::DocumentHandle handle, int index)
{
    core::ITextExtractor* extractor = backend.textExtractor(handle);
    REQUIRE(extractor != nullptr);
    core::text::PageTextModel model;
    REQUIRE(extractor->extractPage(handle, index, model));
    static const QRegularExpression identity(QStringLiteral("(?<![A-Za-z0-9-])[A-Z][A-Z0-9]*-[0-9]{4}(?![A-Za-z0-9-])"));
    const QRegularExpressionMatch match = identity.match(model.text());
    return match.hasMatch() ? match.captured(0) : QString();
}

} // namespace

// ==========================================================================================
// D7-10 -- two readings of the named destinations agree, or the table is read-only
// ==========================================================================================

TEST_CASE("render.destinations: the backend's two readings agree on every corpus file with named destinations", "[destinations][A-82]")
{
    int withTable = 0;
    int agreed = 0;
    for (const std::string& relative : corpusPdfs()) {
        const QString path = corpusPath(relative);
        auto opened = pdf::render::PdfiumDocument::open(path, {});
        if (!opened) continue;   // a file the engine refuses has no table to read
        const core::Destinations& engine = opened.value()->destinations();
        if (engine.isEmpty()) continue;
        pdf::DocumentBackendImpl backend;
        auto handle = backend.open(path, nullptr);
        if (!handle) continue;
        ++withTable;
        const core::Destinations* ours = backend.destinations(handle.value());
        REQUIRE(ours != nullptr);
        INFO(relative << ": " << (ours->status() ? std::string("agrees") : QString(ours->status().error().context).toStdString()));
        if (ours->status()) ++agreed;
        // Agreeing or not, the table the panes see has every name PDFium has, each to the same page.
        REQUIRE(ours->size() == engine.size());
        for (const core::NamedDestination& entry : engine.all()) {
            const core::NamedDestination* mine = ours->find(entry.name);
            REQUIRE(mine != nullptr);
            CHECK(mine->destination.pageIndex == entry.destination.pageIndex);
        }
        // The encrypted ones are refused by the first-party reader (D2-6) and say why; every other one agrees.
        if (relative.rfind("encrypted/", 0) != 0) CHECK(ours->status());
        backend.close(handle.value());
    }
    std::cout << "render.destinations: " << withTable << " corpus files with named destinations, " << agreed << " agreed\n";
    CHECK(withTable >= 2);   // dests-named and refs-every-kind -- measured at T12: the only two that carry a table

    // THE CONTROL: PDFium's reading changed three ways -- a name, a page, an entry missing -- each refused by key.
    auto engine = engineOpen(corpusPath("assembly/refs-every-kind.pdf"));
    pdf::DocumentBackendImpl backend;
    auto handle = backend.open(corpusPath("assembly/refs-every-kind.pdf"), nullptr);
    REQUIRE(handle);
    const core::Destinations& ours = *backend.destinations(handle.value());
    REQUIRE(ours.status());
    const auto planted = [&engine](const std::function<void(std::vector<core::NamedDestination>&)>& change) {
        std::vector<core::NamedDestination> entries = engine->destinations().all();
        change(entries);
        core::Destinations out;
        for (core::NamedDestination& entry : entries) out.add(std::move(entry));
        return out;
    };
    for (const auto& change : std::vector<std::function<void(std::vector<core::NamedDestination>&)>>{
             [](std::vector<core::NamedDestination>& e) { e.front().name += QStringLiteral("x"); },
             [](std::vector<core::NamedDestination>& e) { e.front().destination.pageIndex += 1; },
             [](std::vector<core::NamedDestination>& e) { e.pop_back(); }}) {
        const core::Status status = pdf::ops::DestinationReader::agreesWith(planted(change), ours);
        REQUIRE_FALSE(status);
        CHECK(QString(status.error().messageKey) == QStringLiteral("error.destination.readingsDisagree"));
    }
    CHECK(pdf::ops::DestinationReader::agreesWith(engine->destinations(), ours));
    backend.close(handle.value());
}

// ==========================================================================================
// A-82's PDFium half -- a table edited, saved, and read by a fresh PDFium: each name to the page it shows
// ==========================================================================================

TEST_CASE("render.destinations: created, renamed and deleted, saved -- PDFium reads each name to the page that shows it", "[destinations][A-82]")
{
    auto live = openLive("assembly/refs-every-kind.pdf");
    // dest-beta is P-0007's; old-delta P-0004's (the label's `targetIdentity`).
    core::NamedDestination made;
    made.name = QStringLiteral("made-for-p0012");
    made.destination.pageIndex = 11;
    made.destination.fit = core::Destination::Fit::Fit;
    REQUIRE(live->journal->run(std::make_unique<core::CreateDestinationCommand>(made)));
    REQUIRE(live->journal->run(std::make_unique<core::RenameDestinationCommand>(QStringLiteral("dest-beta"), QStringLiteral("beta renamed"))));
    REQUIRE(live->journal->run(std::make_unique<core::DeleteDestinationCommand>(std::vector<QString>{QStringLiteral("dest-gamma")})));
    // A page moved after: every name still lands where it did -- a reference follows its page.
    REQUIRE(live->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{6}, 0)));
    REQUIRE(live->journal->settle());
    REQUIRE(live->coordinator->save(*live->journal, live->path));

    auto engine = engineOpen(live->path);
    const core::Destinations& read = engine->destinations();
    pdf::DocumentBackendImpl fresh;
    auto freshHandle = fresh.open(live->path, nullptr);
    REQUIRE(freshHandle);
    CHECK(fresh.destinations(freshHandle.value())->status());   // the saved file's two readings agree too
    const auto landsOn = [&](const char* name) {
        const core::NamedDestination* entry = read.find(QLatin1String(name));
        REQUIRE(entry != nullptr);
        REQUIRE(entry->destination.isResolved());
        return identityOn(fresh, freshHandle.value(), entry->destination.pageIndex);
    };
    CHECK(landsOn("made-for-p0012") == QStringLiteral("P-0012"));
    CHECK(landsOn("beta renamed") == QStringLiteral("P-0007"));
    CHECK(landsOn("dest-alpha") == QStringLiteral("P-0003"));
    CHECK(landsOn("old-delta") == QStringLiteral("P-0004"));
    CHECK(read.find(QStringLiteral("dest-beta")) == nullptr);
    CHECK(read.find(QStringLiteral("dest-gamma")) == nullptr);
    CHECK(read.size() == 5);

    // The link that named dest-beta names the new name, and FOLLOWED -- the model's decision, the engine's page -- lands
    // on P-0007; the one that named dest-gamma was cleared, and goes nowhere.
    const core::Document& model = live->document();
    const std::optional<std::size_t> renamed = model.annotations().findObject(21);
    const std::optional<std::size_t> cleared = model.annotations().findObject(23);
    REQUIRE(renamed);
    REQUIRE(cleared);
    const core::LinkFollow step = core::followLink(model.annotations().at(*renamed).action, model.destinations(), model.pages().count());
    REQUIRE(step.step == core::LinkFollow::Step::Navigate);
    CHECK(identityOn(fresh, freshHandle.value(), step.destination.pageIndex) == QStringLiteral("P-0007"));
    CHECK(model.annotations().at(*cleared).action.kind == core::OutlineAction::Kind::None);
}
