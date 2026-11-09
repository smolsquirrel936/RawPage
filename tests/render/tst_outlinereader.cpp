// tests/render/tst_outlinereader.cpp -- the outline's two readings, and what PDFium reads of an edited one. Stage 7
// T11, guide A-75 and A-77's PDFium half (`Stage7-Assembly-Testing-Guide.md` §5.13), decision D7-10.
//
// THE ORACLE IS THE ENGINE (D7-18's rule): each corpus file is opened by the SHIPPED backend, which reads its outline
// both ways and holds the two to agree; this suite reads PDFium's outline itself, through `PdfiumDocument`, and grades
// the backend's answer against it item by item -- titles, depth, target pages. A file whose readings disagree is named
// with the reason. And an outline the product EDITED and saved is read back by a fresh PDFium: the tree, the titles,
// the open state, every destination's page -- what Acrobat-shaped readers will see, short of Acrobat (M-05).
//
// WHAT IT DOES NOT COVER (convention 19): the pane's read-only state is `tests/ui/tst_bookmarksedit.cpp`'s; whether
// the bytes of an edit are only the edited item's is `tests/unit/tst_outlineedit.cpp`'s (A-76).

#include <catch2/catch_test_macros.hpp>

#include "TextOracle.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/BookmarkCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/Outline.h"
#include "core/doc/SaveCoordinator.h"
#include "pdf/DocumentBackendImpl.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/OutlineReader.h"
#include "pdf/ops/PageOps.h"
#include "pdf/render/PdfiumDocument.h"
#include "platform/win/WinDurableFiles.h"

#include <QFile>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using namespace rawpage::test_oracle;
using core::OutlineAction;
using core::OutlineItem;
using core::OutlineItemId;
using core::OutlinePosition;

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

// The two trees compared as the backend's agreement compares them: shape, titles (PDFium's spaces), target pages.
std::string differs(const std::vector<OutlineItem>& engine, const std::vector<OutlineItem>& ours, const std::string& at = "")
{
    if (engine.size() != ours.size()) return at + " size " + std::to_string(engine.size()) + "/" + std::to_string(ours.size());
    for (std::size_t i = 0; i < engine.size(); ++i) {
        const std::string here = at + "/" + std::to_string(i);
        if (pdf::ops::OutlineReader::engineTitle(engine[i].title) != pdf::ops::OutlineReader::engineTitle(ours[i].title))
            return here + " title";
        if (engine[i].destination.pageIndex != ours[i].destination.pageIndex
            && ours[i].action.kind != OutlineAction::Kind::GoToRemote)
            return here + " page " + std::to_string(engine[i].destination.pageIndex) + "/" + std::to_string(ours[i].destination.pageIndex);
        if (engine[i].openByDefault != ours[i].openByDefault) return here + " open";
        if (std::string inner = differs(engine[i].children, ours[i].children, here); !inner.empty()) return inner;
    }
    return {};
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
    const core::Outline& outline() const { return journal->document().outline(); }
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

const OutlineItem& at(const core::Outline& outline, std::initializer_list<int> path)
{
    const std::vector<OutlineItem>* level = &outline.roots();
    const OutlineItem* item = nullptr;
    for (const int i : path) {
        REQUIRE(static_cast<std::size_t>(i) < level->size());
        item = &(*level)[static_cast<std::size_t>(i)];
        level = &item->children;
    }
    return *item;
}

} // namespace

// ==========================================================================================
// A-75 -- two readings of the outline agree, or the outline is read-only
// ==========================================================================================

TEST_CASE("render.outline_reader: the backend's two readings agree on every corpus file with an outline", "[outline][outlinereader][A-75]")
{
    int withOutline = 0;
    int agreed = 0;
    std::vector<std::string> readOnly;
    for (const std::string& relative : corpusPdfs()) {
        INFO(relative);
        auto engine = pdf::render::PdfiumDocument::open(corpusPath(relative), {});
        if (!engine) continue;   // a file the engine refuses has no outline to agree about
        pdf::DocumentBackendImpl backend;
        auto handle = backend.open(corpusPath(relative), nullptr);
        if (!handle) continue;
        const core::Outline* outline = backend.outline(handle.value());
        REQUIRE(outline != nullptr);
        if (engine.value()->outline().isEmpty()) {
            CHECK(outline->isEmpty());
            continue;
        }
        ++withOutline;
        if (!outline->status()) {
            readOnly.push_back(relative + " (" + std::string(outline->status().error().messageKey.data()) + " "
                               + outline->status().error().context.toStdString() + ")");
            // Read-only, it SHOWS the engine's reading -- the panes navigate what PDFium navigates.
            CHECK(differs(engine.value()->outline().roots(), outline->roots()).empty());
            continue;
        }
        ++agreed;
        const std::string wrong = differs(engine.value()->outline().roots(), outline->roots());
        INFO("differs at " << wrong);
        CHECK(wrong.empty());
        for (const OutlineItem* item : outline->preorder()) CHECK(item->id.object > 0);
    }
    std::cout << "outline readings: " << withOutline << " corpus files with an outline, " << agreed << " agreed;";
    for (const std::string& file : readOnly) std::cout << " read-only: " << file;
    std::cout << "\n";
    CHECK(withOutline >= 5);
    // Every corpus outline is a well-formed one RawPage's generators wrote, save an encrypted file's -- which the
    // first-party reader does not read at all (D2-6) and so cannot agree about.
    for (const std::string& file : readOnly) CHECK(file.find("encrypted/") == 0);
}

TEST_CASE("render.outline_reader: a planted disagreement is found -- a title, a target, an item, a depth", "[outline][outlinereader][A-75][control]")
{
    // The agreement is real only if it can say no (convention 7): PDFium's reading of `refs-every-kind.pdf`, each time
    // changed in one way, against the first-party one.
    auto engine = engineOpen(corpusPath("assembly/refs-every-kind.pdf"));
    const QByteArray bytes = [&] {
        QFile file(corpusPath("assembly/refs-every-kind.pdf"));
        REQUIRE(file.open(QIODevice::ReadOnly));
        return file.readAll();
    }();
    auto opened = pdf::cos::openWithRepair(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    REQUIRE(opened);
    auto walk = pdf::ops::PageOps::walk(opened.value().table);
    REQUIRE(walk);
    std::map<long long, int> indexOf;
    for (std::size_t i = 0; i < walk.value().pages.size(); ++i) indexOf.emplace(walk.value().pages[i].number, static_cast<int>(i));
    const pdf::ops::ReferenceRepair::Ledger none;
    const pdf::ops::ReferenceRepair::View view(opened.value().table, none);
    pdf::ops::OutlineReader::Reading reading = pdf::ops::OutlineReader::read(view);
    pdf::ops::OutlineReader::resolve(reading, indexOf);
    REQUIRE(pdf::ops::OutlineReader::agreesWith(engine->outline(), reading, indexOf));   // as read: they agree

    const auto refused = [&](const std::function<void(core::Outline&)>& plant) {
        core::Outline planted = engine->outline();
        plant(planted);
        const core::Status agreed = pdf::ops::OutlineReader::agreesWith(planted, reading, indexOf);
        return !agreed && QString(agreed.error().messageKey) == QStringLiteral("error.outline.readingsDisagree");
    };
    CHECK(refused([](core::Outline& o) { o.roots()[1].children[0].title = QStringLiteral("P-0099"); }));
    CHECK(refused([](core::Outline& o) { o.roots()[2].destination.pageIndex = 3; }));
    CHECK(refused([](core::Outline& o) { o.roots()[1].children.pop_back(); }));
    CHECK(refused([](core::Outline& o) {
        OutlineItem moved = o.roots()[2];
        o.roots().pop_back();
        o.roots()[0].children.push_back(moved);
    }));
}

// ==========================================================================================
// A-77's PDFium half -- an edited outline as a fresh engine reads it
// ==========================================================================================

TEST_CASE("render.outline_reader: an outline edited and saved reads back in PDFium as the model has it", "[outline][outlinereader][A-77]")
{
    auto live = openLive("assembly/refs-every-kind.pdf");
    REQUIRE(live->outline().status());
    const auto run = [&live](std::unique_ptr<core::Command> command) {
        const core::Status status = live->journal->run(std::move(command));
        INFO("run: " << (status ? std::string("ok") : std::string(status.error().messageKey.data())));
        REQUIRE(status);
        REQUIRE(live->journal->settle());
    };
    const QString key = core::newBookmarkKey();
    OutlineItem made;
    made.id = core::newBookmarkId(key, 0);
    made.title = QStringLiteral("新書籤 New");
    core::Destination view;
    view.pageIndex = 7;
    view.fit = core::Destination::Fit::XYZ;
    view.hasY = true;
    view.y = 400;
    made.action = OutlineAction::goTo(view);
    made.destination = view;
    run(std::make_unique<core::CreateBookmarkCommand>(OutlinePosition{at(live->outline(), {1}).id, 1}, std::vector<OutlineItem>{made}));
    run(std::make_unique<core::ReparentBookmarkCommand>(at(live->outline(), {2}).id, OutlinePosition{at(live->outline(), {0}).id, 0}));
    run(std::make_unique<core::DeleteBookmarkCommand>(std::vector<OutlineItemId>{at(live->outline(), {0, 2}).id}, false));
    auto properties = core::EditBookmarkCommand::Properties::of(at(live->outline(), {1}));
    properties.open = true;
    properties.title = QStringLiteral("Opened");
    run(std::make_unique<core::EditBookmarkCommand>(at(live->outline(), {1}).id, properties));
    run(std::make_unique<core::MovePagesCommand>(std::vector<int>{7}, 0));   // the new bookmark's page moves to the front

    // THE BACKEND'S OUTLINE after the show is the edited one -- what the panes and a search read.
    const core::Outline* shown = live->backend.outline(live->handle);
    REQUIRE(shown != nullptr);
    CHECK(differs(shown->roots(), live->outline().roots()).empty());

    auto saved = live->coordinator->save(*live->journal, live->path);
    REQUIRE(saved);
    auto engine = engineOpen(live->path);
    const std::string wrong = differs(engine->outline().roots(), live->outline().roots());
    INFO("differs at " << wrong);
    CHECK(wrong.empty());
    // By identity: the new bookmark lands on the page that shows P-0008, wherever it now is.
    const OutlineItem& read = at(engine->outline(), {1, 1});
    CHECK(read.title == made.title);
    CHECK(read.destination.pageIndex == 0);
    pdf::DocumentBackendImpl fresh;
    auto handle = fresh.open(live->path, nullptr);
    REQUIRE(handle);
    CHECK(fresh.outline(handle.value())->status());   // the saved file's two readings agree too
}
