// tst_journal -- `core/cmd/CommandJournal`. Stage 3 T5, guide cases A-38 … A-41
// (`Stage3-Mutation-Testing-Guide.md` §5.7), decision D3-10.
//
// WHAT THIS SUITE IS TRYING TO CATCH: an undo that reverts the wrong step; a redo that survives a new
// command; dirty computed from the revision; a refused or failed command that leaves a trace; a group
// that undoes piecemeal; a notification sent twice, or to a token that unsubscribed.
//
// WHAT IT DOES NOT COVER (convention 19): the ENGINE (the backend is `tests/unit/CosBackend.h`, whose
// page tree is the first-party walk); the recovery journal, which does not exist until T9 -- so "no
// journal line" in A-40 is not asserted here; and any key press or dialog -- commands are constructed
// and handed to the journal directly, as `ui/` will at T10.

#include <catch2/catch_test_macros.hpp>

#include "CosBackend.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"

#include <filesystem>
#include <memory>
#include <string>

using namespace rawpage;
using core::CatalogText;
using core::CommandJournal;
using core::RotatePageCommand;
using core::Scope;

namespace {

QString corpusPath(std::string_view relative)
{
    return QString::fromStdString((std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative).string());
}

struct Session {
    tests::CosBackend backend;
    core::DocumentHandle handle;
    std::unique_ptr<CommandJournal> journal;

    pdf::ops::MutationSinkImpl* sink() { return backend.sinkFor(handle); }
    const core::Document& document() const { return journal->document(); }
    int rotation(int page) const { return static_cast<int>(document().pages().rotation(page)); }
};

std::unique_ptr<Session> openSession(std::string_view relative,
                                     std::optional<core::CosReading> planted = std::nullopt)
{
    auto session = std::make_unique<Session>();
    if (planted)
        session->backend.plantReading(*planted);
    auto handle = session->backend.open(corpusPath(relative), nullptr);
    REQUIRE(handle);
    session->handle = handle.value();
    core::Document document;
    REQUIRE(document.hydrate(session->backend, session->handle));
    session->journal = std::make_unique<CommandJournal>(
        std::move(document), session->backend.mutationSink(session->handle));
    return session;
}

std::unique_ptr<core::Command> rotate(int page, int degrees = 90)
{
    return std::make_unique<RotatePageCommand>(page, degrees);
}

bool deltaIsEmpty(pdf::ops::MutationSinkImpl* sink)
{
    REQUIRE(sink != nullptr);
    auto delta = sink->delta();
    REQUIRE(delta);
    return delta.value().isEmpty();
}

// A planted command that refuses honestly: false, and nothing changed.
class RefusingCommand final : public core::Command {
public:
    QLatin1String typeId() const override { return QLatin1String("test.refusing"); }
    CatalogText name() const override { return {QLatin1String("command.page.rotate"), {}}; }
    Scope scope() const override { return Scope::page(0); }
    core::Operation operation() const override { return core::Operation::EditPages; }
    bool apply(core::Document&, core::MutationSink&) override { return false; }
    bool revert(core::Document&, core::MutationSink&) override { return false; }
    QJsonObject serialize() const override { return {}; }
};

// A planted command that BREAKS the contract: changes the model, then returns false.
class PartialCommand final : public core::Command {
public:
    QLatin1String typeId() const override { return QLatin1String("test.partial"); }
    CatalogText name() const override { return {QLatin1String("command.page.rotate"), {}}; }
    Scope scope() const override { return Scope::page(0); }
    core::Operation operation() const override { return core::Operation::EditPages; }
    bool apply(core::Document& document, core::MutationSink&) override
    {
        document.pages().setRotation(0, 180);
        return false;
    }
    bool revert(core::Document&, core::MutationSink&) override { return false; }
    QJsonObject serialize() const override { return {}; }
};

} // namespace

// ==========================================================================================
// A-38 -- run, undo, redo, and the stacks
// ==========================================================================================

TEST_CASE("journal runs undoes and redoes the newest step", "[cmd][journal]")
{
    auto session = openSession("ordinary/text-a4.pdf");
    CommandJournal& journal = *session->journal;
    const core::Revision start = journal.revision();
    REQUIRE(start == core::Revision::initial());

    REQUIRE(journal.run(rotate(0)));
    CHECK(session->rotation(0) == 90);
    CHECK(journal.position() == 1);
    CHECK(journal.canUndo());
    CHECK_FALSE(journal.canRedo());
    REQUIRE(journal.undoName().has_value());
    CHECK(*journal.undoName() == CatalogText{QLatin1String("command.page.rotate"), {"1", "90"}});

    REQUIRE(journal.run(rotate(1, 180)));
    CHECK(session->rotation(1) == 180);

    // Undo reverts the NEWEST.
    REQUIRE(journal.undo());
    CHECK(session->rotation(1) == 0);
    CHECK(session->rotation(0) == 90);
    CHECK(journal.canRedo());

    // Redo re-applies it.
    REQUIRE(journal.redo());
    CHECK(session->rotation(1) == 180);

    // A run after an undo EMPTIES redo.
    REQUIRE(journal.undo());
    REQUIRE(journal.run(rotate(2, 270)));
    CHECK_FALSE(journal.canRedo());
    CHECK(journal.historySize() == journal.position());
    CHECK(session->rotation(1) == 0);
    CHECK_FALSE(journal.redo());

    // Every operation raised the revision; none lowered it.
    CHECK(journal.revision().value() == start.value() + 6);
}

TEST_CASE("journal undo with nothing to undo changes nothing", "[cmd][journal]")
{
    auto session = openSession("ordinary/text-a4.pdf");
    CommandJournal& journal = *session->journal;
    const QByteArray hash = session->document().modelHash();
    const core::Revision revision = journal.revision();

    CHECK_FALSE(journal.undo());
    CHECK_FALSE(journal.redo());
    CHECK(session->document().modelHash() == hash);
    CHECK(journal.revision() == revision);
    CHECK_FALSE(journal.undoName().has_value());
}

TEST_CASE("journal tells subscribers once per operation and nothing after unsubscribing", "[cmd][journal]")
{
    auto session = openSession("ordinary/text-a4.pdf");
    CommandJournal& journal = *session->journal;

    std::vector<Scope> seen;
    std::vector<Scope> unsubscribedSeen;
    journal.subscribeMutated([&](const Scope& scope) { seen.push_back(scope); });
    const int gone = journal.subscribeMutated([&](const Scope& scope) { unsubscribedSeen.push_back(scope); });
    journal.unsubscribe(gone);

    REQUIRE(journal.run(rotate(2)));
    REQUIRE(journal.undo());
    REQUIRE(journal.redo());
    REQUIRE(seen.size() == 3);
    for (const Scope& scope : seen)
        CHECK(scope == Scope::page(2));
    CHECK(unsubscribedSeen.empty());

    // A handler may unsubscribe itself while being called.
    int calls = 0;
    int self = 0;
    self = journal.subscribeMutated([&](const Scope&) { ++calls; journal.unsubscribe(self); });
    REQUIRE(journal.run(rotate(0)));
    REQUIRE(journal.run(rotate(1)));
    CHECK(calls == 1);
}

// ==========================================================================================
// A-39 -- dirty is a position, not a revision
// ==========================================================================================

TEST_CASE("journal is clean after rotate save rotate undo although the revision rose", "[cmd][journal][dirty]")
{
    auto session = openSession("ordinary/text-a4.pdf");
    CommandJournal& journal = *session->journal;
    std::vector<std::uint64_t> revisions{journal.revision().value()};
    std::vector<bool> dirtyEvents;
    journal.subscribeDirty([&](bool dirty) { dirtyEvents.push_back(dirty); });

    CHECK_FALSE(journal.isDirty());
    REQUIRE(journal.run(rotate(0)));
    revisions.push_back(journal.revision().value());
    CHECK(journal.isDirty());

    journal.markSaved();
    CHECK_FALSE(journal.isDirty());

    REQUIRE(journal.run(rotate(1)));
    revisions.push_back(journal.revision().value());
    CHECK(journal.isDirty());

    REQUIRE(journal.undo());
    revisions.push_back(journal.revision().value());
    CHECK_FALSE(journal.isDirty());

    // THE CONTROL: the revision rose at every step, so a revision-based dirty flag would say dirty here.
    for (std::size_t at = 1; at < revisions.size(); ++at)
        CHECK(revisions[at] > revisions[at - 1]);
    CHECK(dirtyEvents == std::vector<bool>{true, false, true, false});
}

TEST_CASE("journal saved position becomes unreachable when redo past it is discarded", "[cmd][journal][dirty]")
{
    auto session = openSession("ordinary/text-a4.pdf");
    CommandJournal& journal = *session->journal;

    REQUIRE(journal.run(rotate(0)));
    journal.markSaved();
    REQUIRE(journal.undo());
    CHECK(journal.isDirty());
    REQUIRE(journal.run(rotate(1)));    // throws away the redo that held the saved state
    CHECK(journal.isDirty());

    // No sequence of undos reaches clean.
    while (journal.undo())
        CHECK(journal.isDirty());
    CHECK(journal.isDirty());
    while (journal.redo())
        CHECK(journal.isDirty());
}

// ==========================================================================================
// A-40 -- a refused or failed command leaves no trace
// ==========================================================================================

TEST_CASE("journal leaves no trace of a command that refuses or is refused", "[cmd][journal][refusal]")
{
    struct Before {
        QByteArray hash;
        std::uint64_t revision;
        std::size_t position;
        std::size_t history;
        bool canUndo;
        bool canRedo;
    };
    auto capture = [](const Session& session) {
        return Before{session.document().modelHash(), session.journal->revision().value(),
                      session.journal->position(), session.journal->historySize(),
                      session.journal->canUndo(), session.journal->canRedo()};
    };
    auto unchanged = [](const Session& session, const Before& before) {
        CHECK(session.document().modelHash() == before.hash);
        CHECK(session.journal->revision().value() == before.revision);
        CHECK(session.journal->position() == before.position);
        CHECK(session.journal->historySize() == before.history);
        CHECK(session.journal->canUndo() == before.canUndo);
        CHECK(session.journal->canRedo() == before.canRedo);
    };

    SECTION("a command whose apply returns false")
    {
        auto session = openSession("ordinary/text-a4.pdf");
        REQUIRE(session->journal->run(rotate(0)));
        REQUIRE(session->journal->undo());      // something in redo, to prove it survives
        int notified = 0;
        session->journal->subscribeMutated([&](const Scope&) { ++notified; });
        session->journal->subscribeDirty([&](bool) { ++notified; });
        const Before before = capture(*session);

        auto result = session->journal->run(std::make_unique<RefusingCommand>());
        REQUIRE_FALSE(result);
        CHECK(std::string(result.error().messageKey.data()) == "error.command.failed");
        unchanged(*session, before);
        CHECK(deltaIsEmpty(session->sink()));
        CHECK(notified == 0);
    }
    SECTION("a rotation of a page the document does not have")
    {
        auto session = openSession("ordinary/text-a4.pdf");
        const Before before = capture(*session);
        REQUIRE_FALSE(session->journal->run(rotate(99)));
        unchanged(*session, before);
        CHECK(deltaIsEmpty(session->sink()));
    }
    SECTION("a document the pre-flight refuses")
    {
        // Planted: the first-party reading assessed as encrypted (tests/unit/CosBackend.h says why).
        auto session = openSession("ordinary/text-a4.pdf", core::CosReading::Encrypted);
        REQUIRE(session->backend.mutationSink(session->handle) == nullptr);
        int notified = 0;
        session->journal->subscribeMutated([&](const Scope&) { ++notified; });
        const Before before = capture(*session);

        auto result = session->journal->run(rotate(0));
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::PermissionDenied);
        CHECK(std::string(result.error().messageKey.data()) == "error.edit.encrypted");
        unchanged(*session, before);
        CHECK(notified == 0);
    }
}

TEST_CASE("journal reports a command that changed the document and returned false", "[cmd][journal][refusal]")
{
    // The PARTIAL-APPLICATION report, observed through a test handler. What this does NOT observe: the
    // default handler's `Q_ASSERT` actually aborting a debug build -- replacing the handler is what
    // lets the case run at all.
    std::vector<std::string> reported;
    CommandJournal::setPartialApplicationHandler(
        [&](const core::Command& command) { reported.emplace_back(command.typeId().data()); });

    auto session = openSession("ordinary/text-a4.pdf");
    auto result = session->journal->run(std::make_unique<PartialCommand>());
    CHECK_FALSE(result);
    CHECK(reported == std::vector<std::string>{"test.partial"});
    CHECK(session->journal->position() == 0);    // not recorded, even though the model moved

    // THE CONTROL: an honest refusal is not reported.
    reported.clear();
    CHECK_FALSE(session->journal->run(std::make_unique<RefusingCommand>()));
    CHECK(reported.empty());

    CommandJournal::setPartialApplicationHandler({});
}

// ==========================================================================================
// A-41 -- groups are one undo step
// ==========================================================================================

TEST_CASE("journal undoes a group of twelve rotations as one step", "[cmd][journal][group]")
{
    auto session = openSession("ordinary/pages-900.pdf");
    CommandJournal& journal = *session->journal;
    const QByteArray hash = session->document().modelHash();

    journal.beginGroup(CatalogText{QLatin1String("command.page.rotate"), {"12"}});
    for (int page = 0; page < 12; ++page)
        REQUIRE(journal.run(rotate(page)));
    CHECK(journal.isDirty());                    // dirty while the group is still open
    CHECK_FALSE(journal.undo());                 // refused while open
    REQUIRE(journal.endGroup());

    CHECK(journal.historySize() == 1);
    CHECK(journal.position() == 1);
    CHECK(journal.undoName()->arguments == QStringList{"12"});

    std::vector<Scope> seen;
    journal.subscribeMutated([&](const Scope& scope) { seen.push_back(scope); });
    REQUIRE(journal.undo());
    CHECK(session->document().modelHash() == hash);
    for (int page = 0; page < 12; ++page)
        CHECK(session->rotation(page) == 0);
    REQUIRE(seen.size() == 1);                    // once, with the merged scope
    CHECK(seen.front().pageIndices().size() == 12);
    CHECK(deltaIsEmpty(session->sink()));

    REQUIRE(journal.redo());
    for (int page = 0; page < 12; ++page)
        CHECK(session->rotation(page) == 90);
}

TEST_CASE("journal folds a nested group into the outer one and records nothing for an empty group", "[cmd][journal][group]")
{
    auto session = openSession("ordinary/text-a4.pdf");
    CommandJournal& journal = *session->journal;

    SECTION("nested")
    {
        journal.beginGroup(CatalogText{QLatin1String("command.page.rotate"), {"outer"}});
        REQUIRE(journal.run(rotate(0)));
        journal.beginGroup(CatalogText{QLatin1String("command.page.rotate"), {"inner"}});
        REQUIRE(journal.run(rotate(1)));
        REQUIRE(journal.endGroup());
        CHECK(journal.isGroupOpen());
        REQUIRE(journal.run(rotate(2)));
        REQUIRE(journal.endGroup());
        CHECK_FALSE(journal.isGroupOpen());

        CHECK(journal.historySize() == 1);
        CHECK(journal.undoName()->arguments == QStringList{"outer"});
        REQUIRE(journal.undo());
        for (int page = 0; page < 3; ++page)
            CHECK(session->rotation(page) == 0);
    }
    SECTION("empty")
    {
        const core::Revision revision = journal.revision();
        journal.beginGroup(CatalogText{QLatin1String("command.page.rotate"), {}});
        REQUIRE(journal.endGroup());
        CHECK(journal.historySize() == 0);
        CHECK_FALSE(journal.isDirty());
        CHECK(journal.revision() == revision);
        CHECK_FALSE(journal.endGroup());          // nothing left to close
    }
}

// ==========================================================================================
// Stage 6 T4 (D6-2) -- shown, or not done
// ==========================================================================================

TEST_CASE("journal asks the show step after every run undo and redo and before anyone is told", "[cmd][journal][annotdisplay]")
{
    auto session = openSession("ordinary/text-a4.pdf");
    CommandJournal& journal = *session->journal;
    std::vector<std::string> order;
    journal.setShowHandler([&](const Scope& scope) {
        order.push_back("show " + std::to_string(scope.pageIndices().empty() ? -1 : scope.pageIndices().front()));
        return core::Status{};
    });
    journal.subscribeMutated([&](const Scope&) { order.push_back("mutated"); });

    REQUIRE(journal.run(rotate(2)));
    REQUIRE(journal.undo());
    REQUIRE(journal.redo());
    CHECK(order == std::vector<std::string>{"show 2", "mutated", "show 2", "mutated", "show 2", "mutated"});

    // A refused command is not shown: nothing changed.
    order.clear();
    CHECK_FALSE(journal.run(std::make_unique<RefusingCommand>()));
    CHECK(order.empty());

    // UNSET, nothing is asked -- every headless caller, and a replay before its window exists.
    journal.setShowHandler({});
    CHECK_FALSE(journal.hasShowHandler());
    REQUIRE(journal.run(rotate(1)));
    CHECK(order == std::vector<std::string>{"mutated"});
}

TEST_CASE("journal takes back a run undo or redo the show step refuses and leaves no trace", "[cmd][journal][annotdisplay][A-39]")
{
    auto session = openSession("ordinary/text-a4.pdf");
    CommandJournal& journal = *session->journal;
    int mutated = 0;
    journal.subscribeMutated([&](const Scope&) { ++mutated; });
    bool refuse = false;
    journal.setShowHandler([&](const Scope&) -> core::Status {
        if (refuse) return core::Error{core::ErrorCode::Corrupted, QLatin1String("error.edit.notShown"), QStringLiteral("test")};
        return {};
    });

    // A RUN: refused by the show step's key, the model, the sink, the revision and the history untouched.
    const QByteArray hash = session->document().modelHash();
    const core::Revision revision = journal.revision();
    refuse = true;
    const core::Status refused = journal.run(rotate(0));
    REQUIRE_FALSE(refused);
    CHECK(std::string(refused.error().messageKey.data()) == "error.edit.notShown");
    CHECK(session->document().modelHash() == hash);
    CHECK(deltaIsEmpty(session->sink()));
    CHECK(journal.revision() == revision);
    CHECK_FALSE(journal.canUndo());
    CHECK_FALSE(journal.isDirty());
    CHECK(mutated == 0);

    // AN UNDO: refused, and the step still applied -- the page still turned, the undo still available.
    refuse = false;
    REQUIRE(journal.run(rotate(0)));
    const QByteArray turned = session->document().modelHash();
    refuse = true;
    CHECK_FALSE(journal.undo());
    CHECK(session->document().modelHash() == turned);
    CHECK(session->rotation(0) == 90);
    CHECK(journal.canUndo());
    CHECK_FALSE(deltaIsEmpty(session->sink()));

    // A REDO: refused, and the step still undone.
    refuse = false;
    REQUIRE(journal.undo());
    refuse = true;
    CHECK_FALSE(journal.redo());
    CHECK(session->document().modelHash() == hash);
    CHECK(journal.canRedo());
    CHECK(deltaIsEmpty(session->sink()));
    CHECK(mutated == 2);   // the run and the undo that were shown; nothing for the refusals
}
