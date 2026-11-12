// tests/unit/tst_destinations.cpp -- the named destinations' three commands through the SHIPPED sink, headless. Stage 7
// T12, guide case A-82 (`Stage7-Assembly-Testing-Guide.md` §5.13).
//
// WHAT THIS FILE IS AND IS NOT (convention 19). Every case opens a copy of a corpus file through `tests/unit/CosBackend.h`
// -- the first-party reader and the real `pdf/ops/MutationSinkImpl` -- runs commands through the journal, saves through
// `SaveCoordinator`, and reads the written file back with `pdf/ops/DestinationReader` and the COS layer. **It does not
// see PDFium**: whether the engine reads the tables written -- the two readings' agreement over the corpus, and a
// destination followed to the page it shows -- is `tests/render/tst_destinations.cpp`'s. The model here is the
// FIRST-PARTY reading, which is what the shipped backend hands a document whose readings agree.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/commands/BookmarkCommands.h"
#include "core/cmd/commands/DestinationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/DestinationReader.h"
#include "pdf/ops/PageOps.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using core::NamedDestination;
using pdf::cos::CosObject;

namespace {

struct Written {
    QByteArray bytes;
    pdf::cos::XrefTable table;
    pdf::ops::PageWalk walk;
    pdf::ops::DestinationReader::Reading destinations;

    std::map<long long, int> indexOf() const
    {
        std::map<long long, int> out;
        for (std::size_t i = 0; i < walk.pages.size(); ++i) out.emplace(walk.pages[i].number, static_cast<int>(i));
        return out;
    }
    const pdf::ops::DestinationReader::Entry* entry(const std::string& key) const
    {
        for (const auto& e : destinations.entries) {
            if (e.key == key) return &e;
        }
        return nullptr;
    }
    CosObject object(long long number)
    {
        auto fetched = table.fetch(number);
        REQUIRE(fetched);
        CosObject out = fetched.value().object;
        out.clearSpansRecursively();
        return out;
    }
    CosObject catalog()
    {
        return object(table.trailer().get("Root").reference().number);
    }
};

std::unique_ptr<Written> readBytes(QByteArray bytes)
{
    auto out = std::make_unique<Written>();
    out->bytes = std::move(bytes);
    auto opened = pdf::cos::openWithRepair(std::string_view(out->bytes.constData(), static_cast<std::size_t>(out->bytes.size())));
    REQUIRE(opened);
    out->table = std::move(opened.value().table);
    auto walk = pdf::ops::PageOps::walk(out->table);
    REQUIRE(walk);
    out->walk = std::move(walk).value();
    const pdf::ops::ReferenceRepair::Ledger none;
    const pdf::ops::ReferenceRepair::View view(out->table, none);
    out->destinations = pdf::ops::DestinationReader::read(view);
    return out;
}

bool save(tests::SaveSession& session, const char* name)
{
    if (!session.coordinator->save(*session.journal, session.path)) return false;
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);   // kept for `qpdf --check` (D7-18's oracle 2)
    if (!QDir().mkpath(dir)) return false;
    const QString target = QDir(dir).filePath(QStringLiteral("destinations-%1.pdf").arg(QLatin1String(name)));
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

core::Status run(tests::SaveSession& session, std::unique_ptr<core::Command> command)
{
    return session.journal->run(std::move(command));
}

std::string keyOf(const core::Status& status)
{
    return status ? std::string("ok") : QString(status.error().messageKey).toStdString();
}

NamedDestination viewOf(const QString& name, int page)
{
    NamedDestination entry;
    entry.name = name;
    entry.destination.pageIndex = page;
    entry.destination.fit = core::Destination::Fit::XYZ;
    entry.destination.hasX = true;
    entry.destination.x = 0;
    entry.destination.hasY = true;
    entry.destination.y = 600;
    return entry;
}

std::vector<std::string> names(const std::vector<const NamedDestination*>& entries)
{
    std::vector<std::string> out;
    for (const NamedDestination* entry : entries) out.push_back(entry->name.toStdString());
    return out;
}

const core::Annotation& linkObject(const core::Document& document, long long object)
{
    const std::optional<std::size_t> index = document.annotations().findObject(object);
    REQUIRE(index);
    return document.annotations().at(*index);
}

} // namespace

TEST_CASE("destinations: the table read first-party, both forms, each entry in its table and listed sorted", "[destinations][A-82]")
{
    auto session = tests::openCopy("ordinary/dests-named.pdf");
    REQUIRE(session);
    const core::Destinations& table = session->journal->document().destinations();
    REQUIRE(table.status());
    REQUIRE(table.size() == 6);
    const auto form = [&table](const char* name) { return table.find(QLatin1String(name))->form; };
    CHECK(form("chapter-one") == NamedDestination::Form::NameTree);
    CHECK(form("ghost-page") == NamedDestination::Form::NameTree);
    CHECK(form("preface") == NamedDestination::Form::Dictionary);
    CHECK(form("appendix") == NamedDestination::Form::Dictionary);
    // The ghost: kept, unresolved (D4-3).
    CHECK_FALSE(table.find(QStringLiteral("ghost-page"))->destination.isResolved());
    CHECK(table.find(QStringLiteral("chapter-one"))->destination.pageIndex == 0);
    CHECK(table.find(QStringLiteral("appendix"))->destination.pageIndex == 3);
    CHECK(table.find(QStringLiteral("appendix"))->destination.zoom == 2.0);

    // Sorted by name -- code point by code point -- and by page, the unresolved one last.
    CHECK(names(table.sortedByName())
          == std::vector<std::string>{"appendix", "chapter-one", "chapter-two", "ghost-page", "preface", "table-of-figures"});
    CHECK(names(table.sortedByPage())
          == std::vector<std::string>{"chapter-one", "chapter-two", "table-of-figures", "appendix", "preface", "ghost-page"});
}

TEST_CASE("destinations: created from a view, renamed in its table, deleted -- the tree written flat, a pair not changed as it was", "[destinations][A-82]")
{
    auto session = tests::openCopy("ordinary/dests-named.pdf");
    REQUIRE(session);
    auto original = readBytes(tests::readAllBytes(session->path));
    const auto& document = [&session]() -> const core::Document& { return session->journal->document(); };

    // CREATED from a view of page 2, in the name tree (D7-35).
    REQUIRE(run(*session, std::make_unique<core::CreateDestinationCommand>(viewOf(QStringLiteral("made here"), 2))));
    REQUIRE(document().destinations().find(QStringLiteral("made here")) != nullptr);
    CHECK(document().destinations().find(QStringLiteral("made here"))->form == NamedDestination::Form::NameTree);
    // RENAMED: one in the tree and one in the dictionary, each in its own table.
    REQUIRE(run(*session, std::make_unique<core::RenameDestinationCommand>(QStringLiteral("chapter-two"), QStringLiteral("Chapter 2"))));
    REQUIRE(run(*session, std::make_unique<core::RenameDestinationCommand>(QStringLiteral("preface"), QStringLiteral("foreword"))));
    // DELETED: one of each table.
    REQUIRE(run(*session, std::make_unique<core::DeleteDestinationCommand>(
                              std::vector<QString>{QStringLiteral("table-of-figures"), QStringLiteral("appendix")})));
    CHECK(document().destinations().size() == 5);
    REQUIRE(save(*session, "edited"));

    auto written = readBytes(tests::readAllBytes(session->path));
    REQUIRE_FALSE(written->destinations.truncated);
    // THE TREE IS FLAT: one node, its pairs sorted by their keys' bytes.
    const CosObject names = written->object(written->catalog().get("Names").reference().number);
    const CosObject& treeRef = names.get("Dests");
    REQUIRE(treeRef.isReference());
    const CosObject tree = written->object(treeRef.reference().number);
    CHECK_FALSE(tree.has("Kids"));
    std::vector<std::string> keys;
    for (std::size_t at = 0; at < tree.get("Names").items().size(); at += 2) keys.push_back(tree.get("Names").items()[at].text());
    CHECK(keys == std::vector<std::string>{"Chapter 2", "chapter-one", "ghost-page", "made here"});
    // Each entry in its table, nothing else.
    const auto inTable = [&written](const char* key, NamedDestination::Form form) {
        const auto* e = written->entry(key);
        return e != nullptr && e->form == form;
    };
    CHECK(inTable("foreword", NamedDestination::Form::Dictionary));
    CHECK(inTable("Chapter 2", NamedDestination::Form::NameTree));
    CHECK(written->entry("preface") == nullptr);
    CHECK(written->entry("chapter-two") == nullptr);
    CHECK(written->entry("table-of-figures") == nullptr);
    CHECK(written->entry("appendix") == nullptr);
    CHECK(written->destinations.entries.size() == 5);
    // A PAIR NOT CHANGED IS WRITTEN AS IT WAS -- its value the very object the file had, `<< /D [...] >>` form and the
    // ghost's dangling reference included; and a renamed one keeps its value too, only its key new (D7-10).
    CHECK(written->entry("chapter-one")->value == original->entry("chapter-one")->value);
    CHECK(written->entry("ghost-page")->value == original->entry("ghost-page")->value);
    CHECK(written->entry("Chapter 2")->value == original->entry("chapter-two")->value);
    CHECK(written->entry("foreword")->value == original->entry("preface")->value);
    // The made one names page 2 by REFERENCE, at the view it was made from.
    const auto* made = written->entry("made here");
    REQUIRE(made != nullptr);
    CHECK(made->page == written->walk.pages[2].number);
    CHECK(made->view.fit == core::Destination::Fit::XYZ);
    CHECK(made->view.y == 600);

    // UNDONE after the save, all four: the file has its old table back, every name and value.
    for (int i = 0; i < 4; ++i) REQUIRE(session->journal->undo());
    CHECK(document().destinations().size() == 6);
    REQUIRE(save(*session, "undone"));
    auto back = readBytes(tests::readAllBytes(session->path));
    for (const char* key : {"chapter-one", "chapter-two", "ghost-page", "table-of-figures", "preface", "appendix"}) {
        INFO(key);
        REQUIRE(back->entry(key) != nullptr);
        CHECK(back->entry(key)->form == original->entry(key)->form);
        CHECK(back->entry(key)->value == original->entry(key)->value);
    }
    CHECK(back->destinations.entries.size() == 6);
}

TEST_CASE("destinations: an undo before a save writes nothing, and a refused command changes nothing", "[destinations][A-82]")
{
    auto session = tests::openCopy("ordinary/dests-named.pdf");
    REQUIRE(session);
    const QByteArray before = session->journal->document().modelHash();
    REQUIRE(run(*session, std::make_unique<core::CreateDestinationCommand>(viewOf(QStringLiteral("x"), 1))));
    REQUIRE(run(*session, std::make_unique<core::RenameDestinationCommand>(QStringLiteral("appendix"), QStringLiteral("annex"))));
    REQUIRE(run(*session, std::make_unique<core::DeleteDestinationCommand>(std::vector<QString>{QStringLiteral("chapter-one")})));
    CHECK_FALSE(deltaIsEmpty(*session));
    for (int i = 0; i < 3; ++i) REQUIRE(session->journal->undo());
    CHECK(deltaIsEmpty(*session));
    CHECK(tests::sha256(session->journal->document().modelHash()) == tests::sha256(before));

    // Each refused by key, the model unchanged.
    const auto refusedBy = [&session, &before](std::unique_ptr<core::Command> command, const char* key) {
        const core::Status status = run(*session, std::move(command));
        INFO(key);
        CHECK(keyOf(status) == key);
        CHECK(tests::sha256(session->journal->document().modelHash()) == tests::sha256(before));
    };
    refusedBy(std::make_unique<core::CreateDestinationCommand>(viewOf(QStringLiteral("preface"), 1)), "error.destination.nameTaken");
    refusedBy(std::make_unique<core::CreateDestinationCommand>(viewOf(QStringLiteral("章"), 1)), "error.destination.badName");
    refusedBy(std::make_unique<core::CreateDestinationCommand>(viewOf(QStringLiteral(" padded"), 1)), "error.destination.badName");
    refusedBy(std::make_unique<core::CreateDestinationCommand>(viewOf(QStringLiteral("past"), 99)), "error.destination.badDestination");
    refusedBy(std::make_unique<core::RenameDestinationCommand>(QStringLiteral("nothing"), QStringLiteral("x")), "error.destination.noSuchName");
    refusedBy(std::make_unique<core::RenameDestinationCommand>(QStringLiteral("preface"), QStringLiteral("appendix")),
              "error.destination.nameTaken");
    refusedBy(std::make_unique<core::DeleteDestinationCommand>(std::vector<QString>{QStringLiteral("nothing")}),
              "error.destination.noSuchName");
    CHECK(deltaIsEmpty(*session));
}

TEST_CASE("destinations: a rename renames every link and bookmark naming it; a delete clears them and says so", "[destinations][A-82]")
{
    auto session = tests::openCopy("assembly/refs-every-kind.pdf");
    REQUIRE(session);
    const auto& document = [&session]() -> const core::Document& { return session->journal->document(); };
    // The label's links by name: object 21 (`/Dest (dest-beta)`), 22 (`/Dest /old-delta`), 23 (`/A << /S /GoTo /D (dest-gamma) >>`).
    REQUIRE(linkObject(document(), 21).action.kind == core::OutlineAction::Kind::GoToNamed);
    REQUIRE(linkObject(document(), 21).action.name == QStringLiteral("dest-beta"));
    REQUIRE(linkObject(document(), 22).action.name == QStringLiteral("old-delta"));
    // A bookmark naming dest-beta, made first.
    core::OutlineItem named;
    named.id = core::newBookmarkId(QString(32, QLatin1Char('d')), 0);
    named.title = QStringLiteral("By name");
    named.action.kind = core::OutlineAction::Kind::GoToNamed;
    named.action.name = QStringLiteral("dest-beta");
    named.destination.pageIndex = 6;
    named.action.destination.pageIndex = 6;
    REQUIRE(run(*session, std::make_unique<core::CreateBookmarkCommand>(core::OutlinePosition{{}, 0}, std::vector<core::OutlineItem>{named})));

    // RENAMED: the link and the bookmark with it, one step.
    auto rename = std::make_unique<core::RenameDestinationCommand>(QStringLiteral("dest-beta"), QStringLiteral("dest-beta-2"));
    core::RenameDestinationCommand* renaming = rename.get();
    REQUIRE(run(*session, std::move(rename)));
    CHECK(renaming->renamedLinks() == 1);
    CHECK(renaming->renamedBookmarks() == 1);
    CHECK(linkObject(document(), 21).action.name == QStringLiteral("dest-beta-2"));
    CHECK(document().outline().find(named.id)->action.name == QStringLiteral("dest-beta-2"));

    // DELETED: old-delta, in the dictionary; its link cleared and said.
    auto remove = std::make_unique<core::DeleteDestinationCommand>(std::vector<QString>{QStringLiteral("old-delta")});
    core::DeleteDestinationCommand* deleting = remove.get();
    REQUIRE(run(*session, std::move(remove)));
    REQUIRE(deleting->clearedLinks().size() == 1);
    CHECK(deleting->clearedLinks().front().id == core::AnnotationId::ofObject(22));
    CHECK(deleting->clearedBookmarks() == 0);
    CHECK(linkObject(document(), 22).action.kind == core::OutlineAction::Kind::None);
    REQUIRE(save(*session, "references"));

    auto written = readBytes(tests::readAllBytes(session->path));
    // The link's /Dest is the new name, as a STRING -- the tree's form -- and the old-delta link has no target.
    CHECK(written->object(21).get("Dest") == CosObject::makeString("dest-beta-2"));
    CHECK_FALSE(written->object(22).has("Dest"));
    CHECK_FALSE(written->object(22).has("A"));
    CHECK(written->entry("dest-beta-2") != nullptr);
    CHECK(written->entry("old-delta") == nullptr);
    // The other links by name untouched -- not even rewritten.
    CHECK(written->object(23) == readBytes(session->openedBytes)->object(23));

    // UNDONE after the save: the link names old-delta again, in the model and the file.
    REQUIRE(session->journal->undo());
    CHECK(linkObject(document(), 22).action.name == QStringLiteral("old-delta"));
    REQUIRE(session->journal->undo());
    CHECK(linkObject(document(), 21).action.name == QStringLiteral("dest-beta"));
    REQUIRE(save(*session, "references-undone"));
    auto back = readBytes(tests::readAllBytes(session->path));
    CHECK(back->object(22).get("Dest") == CosObject::makeName("old-delta"));
    CHECK(back->object(21).get("Dest") == CosObject::makeString("dest-beta"));
    CHECK(back->entry("old-delta") != nullptr);
    CHECK(back->entry("dest-beta") != nullptr);
}

TEST_CASE("destinations: a page command remaps the table, a delete leaves an entry unresolved and its undo puts it back", "[destinations][A-82][A-19]")
{
    auto session = tests::openCopy("ordinary/dests-named.pdf");
    REQUIRE(session);
    const auto& table = [&session]() -> const core::Destinations& { return session->journal->document().destinations(); };
    REQUIRE(table().find(QStringLiteral("chapter-one"))->destination.pageIndex == 0);
    REQUIRE(run(*session, std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 4)));
    CHECK(table().find(QStringLiteral("chapter-one"))->destination.pageIndex == 4);
    CHECK(table().find(QStringLiteral("chapter-two"))->destination.pageIndex == 0);
    REQUIRE(session->journal->undo());
    REQUIRE(run(*session, std::make_unique<core::DeletePagesCommand>(std::vector<int>{0})));
    CHECK_FALSE(table().find(QStringLiteral("chapter-one"))->destination.isResolved());
    // A destination made AFTER the delete -- the table handed whole, the unresolved entry in it.
    REQUIRE(run(*session, std::make_unique<core::CreateDestinationCommand>(viewOf(QStringLiteral("after"), 0))));
    REQUIRE(save(*session, "page-deleted"));
    auto written = readBytes(tests::readAllBytes(session->path));
    CHECK(written->entry("chapter-one") == nullptr);   // its page gone: left out, as the repair leaves one out (D7-2)
    CHECK(written->entry("after") != nullptr);
    // Both undone after the save: chapter-one is back in the model and the file, at its page.
    REQUIRE(session->journal->undo());
    REQUIRE(session->journal->undo());
    CHECK(table().find(QStringLiteral("chapter-one"))->destination.pageIndex == 0);
    REQUIRE(save(*session, "page-restored"));
    auto back = readBytes(tests::readAllBytes(session->path));
    REQUIRE(back->entry("chapter-one") != nullptr);
    CHECK(back->entry("chapter-one")->page == back->walk.pages[0].number);
    CHECK(back->entry("after") == nullptr);
}

TEST_CASE("destinations: a table read-only for its readings refuses every destination command by key", "[destinations][A-82]")
{
    auto session = std::make_unique<tests::SaveSession>();
    REQUIRE(session->directory.isValid());
    session->path = session->directory.filePath(QStringLiteral("dests-named.pdf"));
    REQUIRE(QFile::copy(tests::corpusFile("ordinary/dests-named.pdf"), session->path));
    QFile::setPermissions(session->path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    session->backend.plantDestinationDisagreement();
    session = tests::openSession(std::move(session));
    REQUIRE(session);
    std::vector<std::unique_ptr<core::Command>> commands;
    commands.push_back(std::make_unique<core::CreateDestinationCommand>(viewOf(QStringLiteral("x"), 1)));
    commands.push_back(std::make_unique<core::RenameDestinationCommand>(QStringLiteral("preface"), QStringLiteral("y")));
    commands.push_back(std::make_unique<core::DeleteDestinationCommand>(std::vector<QString>{QStringLiteral("preface")}));
    for (auto& command : commands) CHECK(keyOf(run(*session, std::move(command))) == "error.destination.readingsDisagree");
    CHECK(deltaIsEmpty(*session));
}

TEST_CASE("CommandRegistry refuses every malformed destination command by Result", "[cmd][serialize][hostile][destinations]")
{
    const char* const hostile[] = {
        R"({"type":"destination.create"})",
        R"({"type":"destination.create","entry":{"name":"","form":"nameTree","dest":{"page":0,"fit":"Fit"}}})",
        R"({"type":"destination.create","entry":{"name":"a","form":"sideways","dest":{"page":0,"fit":"Fit"}}})",
        R"({"type":"destination.create","entry":{"name":"a","form":"nameTree","dest":{"page":0.5,"fit":"Fit"}}})",
        R"({"type":"destination.create","entry":{"name":"a","form":"nameTree","dest":{"page":0,"fit":"Sideways"}}})",
        R"({"type":"destination.rename","from":"a"})",
        R"({"type":"destination.rename","from":"","to":"b"})",
        R"({"type":"destination.rename","from":7,"to":"b"})",
        R"({"type":"destination.delete","names":[]})",
        R"({"type":"destination.delete","names":["a","a"]})",
        R"({"type":"destination.delete","names":"a"})",
        R"({"type":"destination.delete","names":[1]})",
    };
    for (const char* line : hostile) {
        INFO(line);
        const QJsonObject json = QJsonDocument::fromJson(QByteArray(line)).object();
        CHECK_FALSE(core::CommandRegistry::deserialize(json));
    }
    // A name past the bound.
    QJsonObject longName{{QStringLiteral("type"), QStringLiteral("destination.rename")},
                         {QStringLiteral("from"), QStringLiteral("a")},
                         {QStringLiteral("to"), QString(core::kMaxDestinationNameLength + 1, QLatin1Char('x'))}};
    CHECK_FALSE(core::CommandRegistry::deserialize(longName));
}
