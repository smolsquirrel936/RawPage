// tst_pagereconnect -- Stage 7 guide I-23 (`Stage7-Assembly-Testing-Guide.md` §8), decision D7-37
// (`Stage7-Assembly-Plan.md` §3): pages cut or copied out of a document and pasted back into it keep what named them and
// what they named, through the shipped first-party sink, saved and read back.
//
// THE CUT IS MADE AS THE PRODUCT MAKES IT, IN ITS TWO HALVES: the record (`core::referencesOfPages`) read from the model
// before the delete, then a `DeletePagesCommand`, then an `InsertPagesCommand` of the same pages carrying
// `core::reconnectionFor`'s result. The payload is the copier's copy of the corpus FILE's pages, where the product copies
// the open document's snapshot -- the same bytes for an unedited document.
//
// WHAT IT DOES NOT COVER (convention 19): the clipboard, the document's keeping of the record and the engine reading what
// a pasted page shows -- `tests/composition/tst_pagesproduct.cpp`'s `[I-23]` case, through the product.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/commands/BookmarkCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/PageReconnect.h"
#include "core/doc/PageTree.h"
#include "pdf/ops/ObjectGraphCopier.h"

#include <QJsonDocument>

#include <memory>
#include <string>
#include <vector>

using namespace rawpage;
using core::OutlineAction;

namespace {

constexpr const char* kFile = "assembly/refs-every-kind.pdf";

core::CopiedPages copyOf(const std::vector<int>& pages)
{
    const QByteArray bytes = tests::readAllBytes(tests::corpusFile(kFile));
    auto copied = pdf::ops::ObjectGraphCopier::copyFile(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())),
                                                        pages);
    REQUIRE(copied);
    return std::move(copied).value();
}

const core::Document& model(tests::SaveSession& session)
{
    return session.journal->document();
}

// The outline item titled `title` -- refs-every-kind titles each with the identity of the page it names.
const core::OutlineItem& bookmark(const core::Document& document, const char* title)
{
    for (const core::OutlineItem* item : document.outline().preorder()) {
        if (item->title == QLatin1String(title)) return *item;
    }
    FAIL("no bookmark titled " << title);
    throw;
}

int linkTarget(const core::Document& document, long long object)
{
    const std::optional<std::size_t> index = document.annotations().find(core::AnnotationId::ofObject(object));
    REQUIRE(index);
    const core::Annotation& link = document.annotations().at(*index);
    return link.action.kind == OutlineAction::Kind::GoTo ? link.action.destination.pageIndex : -1;
}

// The page a named destination names: -1 going nowhere, -2 when the document has no such name (a save drops one that
// goes nowhere -- D7-2's *removed*).
int destinationPage(const core::Document& document, const char* name)
{
    const core::NamedDestination* entry = document.destinations().find(QLatin1String(name));
    return entry == nullptr ? -2 : entry->destination.pageIndex;
}

// The target of each link on `page`, in listing order: -1 for one going nowhere.
std::vector<int> linksOn(const core::Document& document, int page)
{
    std::vector<int> out;
    const core::AnnotationStore& store = document.annotations();
    for (const core::AnnotationListingEntry& entry : store.listing(page)) {
        if (!entry.annotation) continue;
        const core::Annotation& a = store.at(*entry.annotation);
        if (a.subtype != core::AnnotationSubtype::Link) continue;
        out.push_back(a.action.kind == OutlineAction::Kind::GoTo ? a.action.destination.pageIndex : -1);
    }
    return out;
}

// A cut of `pages`, as `MainWindow::cutPages` makes it: the record, then the delete.
core::CopiedPageReferences cut(tests::SaveSession& session, const std::vector<int>& pages)
{
    core::CopiedPageReferences references = core::referencesOfPages(model(session), pages);
    REQUIRE(session.journal->run(std::make_unique<core::DeletePagesCommand>(pages)));
    return references;
}

// A paste at `at` of `pages`' copy, as `MainWindow::pastePages` makes it into the document the record is from.
core::InsertPagesCommand* paste(tests::SaveSession& session, const core::CopiedPageReferences& references,
                                const std::vector<int>& pages, int at)
{
    core::CopiedPages copied = copyOf(pages);
    core::PageReconnection reconnection = core::reconnectionFor(model(session), references, at, copied.pageCount);
    auto command = std::make_unique<core::InsertPagesCommand>(at, std::move(copied), QString(), std::move(reconnection));
    core::InsertPagesCommand* raw = command.get();
    const core::Status ran = session.journal->run(std::move(command));
    INFO((ran ? std::string("ran") : std::string(ran.error().messageKey.data()) + " " + ran.error().context.toStdString()));
    REQUIRE(ran);
    return raw;
}

// What a save wrote, opened as a document of its own.
std::unique_ptr<tests::SaveSession> savedAndReopened(tests::SaveSession& session)
{
    REQUIRE(session.coordinator->save(*session.journal, session.path));
    const QByteArray bytes = tests::readAllBytes(session.path);
    auto reopened = tests::openBuilt(QStringLiteral("reopened.pdf"), bytes.toStdString());
    REQUIRE(reopened);
    return reopened;
}

} // namespace

// ==========================================================================================
// The page's identity in the model -- what a pasted page's links are found again by
// ==========================================================================================

TEST_CASE("pagetree a page keeps its serial through a move and an undone delete and a copy gets a new one", "[I-23][pagetree]")
{
    auto session = tests::openCopy(kFile);
    REQUIRE(session);
    const core::PageTree& pages = model(*session).pages();
    REQUIRE(pages.count() == 12);
    const std::uint64_t sixth = pages.at(5)->serial();
    const std::uint64_t first = pages.at(0)->serial();
    CHECK(sixth != 0);
    CHECK(first != sixth);
    CHECK(pages.indexOfSerial(sixth) == 5);

    REQUIRE(session->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{5}, 0)));
    CHECK(pages.indexOfSerial(sixth) == 0);
    CHECK(pages.indexOfSerial(first) == 1);

    REQUIRE(session->journal->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{0})));
    CHECK(pages.indexOfSerial(sixth) == -1);
    REQUIRE(session->journal->undo());
    CHECK(pages.indexOfSerial(sixth) == 0);   // put back, the same page

    REQUIRE(session->journal->run(std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{0})));
    CHECK(pages.at(1)->serial() != sixth);    // a copy is another page
    CHECK(pages.at(1)->serial() != 0);
    CHECK(pages.indexOfSerial(sixth) == 0);

    REQUIRE(session->journal->run(std::make_unique<core::InsertPagesCommand>(0, copyOf({5}))));
    CHECK(pages.at(0)->serial() != 0);
    CHECK(pages.at(0)->serial() != sixth);
    CHECK(pages.indexOfSerial(sixth) == 1);
}

// ==========================================================================================
// M-01 step 3: a page cut and pasted back -- what named it names it again
// ==========================================================================================

TEST_CASE("reconnect a cut page pasted back is named again by its bookmark and its link and saved so", "[I-23][reconnect][revert:InsertPagesCommand]")
{
    auto session = tests::openCopy(kFile);
    REQUIRE(session);
    // P-0006 (index 5): the bookmark titled P-0006, and link 20 on page 1 (refs-every-kind's label).
    REQUIRE(bookmark(model(*session), "P-0006").destination.pageIndex == 5);
    REQUIRE(linkTarget(model(*session), 20) == 5);

    const core::CopiedPageReferences references = cut(*session, {5});
    CHECK_FALSE(bookmark(model(*session), "P-0006").destination.isResolved());   // what the delete does (D7-2)
    CHECK(linkTarget(model(*session), 20) == -1);

    // Pasted after P-0012, the last page: index 11 of 12.
    core::InsertPagesCommand* command = paste(*session, references, {5}, 11);
    CHECK(command->reconnection().incoming.size() == 3);   // the bookmark's destination and its GoTo, and the link
    CHECK(model(*session).pages().count() == 12);
    CHECK(bookmark(model(*session), "P-0006").destination.pageIndex == 11);
    CHECK(linkTarget(model(*session), 20) == 11);
    // Nothing else moved: P-0005's bookmark names P-0005, now at index 4.
    CHECK(bookmark(model(*session), "P-0005").destination.pageIndex == 4);

    // ONE UNDO STEP: the paste's undo takes the pages and the reconnection together, and the redo both again.
    REQUIRE(session->journal->undo());
    CHECK(model(*session).pages().count() == 11);
    CHECK_FALSE(bookmark(model(*session), "P-0006").destination.isResolved());
    CHECK(linkTarget(model(*session), 20) == -1);
    REQUIRE(session->journal->redo());
    CHECK(bookmark(model(*session), "P-0006").destination.pageIndex == 11);
    CHECK(linkTarget(model(*session), 20) == 11);

    // SAVED SO: the file read back names the pasted page.
    auto reopened = savedAndReopened(*session);
    CHECK(model(*reopened).pages().count() == 12);
    CHECK(bookmark(model(*reopened), "P-0006").destination.pageIndex == 11);
    CHECK(linkTarget(model(*reopened), 20) == 11);
    CHECK(bookmark(model(*reopened), "P-0005").destination.pageIndex == 4);
}

TEST_CASE("reconnect a cut page's own links name again the pages they named and its bookmark names it", "[I-23][reconnect]")
{
    auto session = tests::openCopy(kFile);
    REQUIRE(session);
    // P-0001 (index 0) carries links 19 and 20, to P-0009 (8) and P-0006 (5).
    REQUIRE(linksOn(model(*session), 0) == std::vector<int>{8, 5});

    const core::CopiedPageReferences references = cut(*session, {0});
    CHECK(references.outgoing.size() == 2);
    // After the cut P-0009 is index 7 and P-0006 index 4; a page MOVED between the cut and the paste is found by what it
    // is, not where it was: P-0009 to the front.
    REQUIRE(session->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{7}, 0)));   // P-0009 now 0, P-0006 5

    paste(*session, references, {0}, 11);
    CHECK(bookmark(model(*session), "P-0001").destination.pageIndex == 11);
    CHECK(linksOn(model(*session), 11) == std::vector<int>{0, 5});

    auto reopened = savedAndReopened(*session);
    CHECK(bookmark(model(*reopened), "P-0001").destination.pageIndex == 11);
    CHECK(linksOn(model(*reopened), 11) == std::vector<int>{0, 5});
}

TEST_CASE("reconnect a cut page's named destination names it again", "[I-23][reconnect]")
{
    auto session = tests::openCopy(kFile);
    REQUIRE(session);
    // P-0003 (index 2): `dest-alpha`, and the bookmark titled P-0003.
    REQUIRE(destinationPage(model(*session), "dest-alpha") == 2);

    const core::CopiedPageReferences references = cut(*session, {2});
    CHECK(destinationPage(model(*session), "dest-alpha") == -1);
    paste(*session, references, {2}, 0);   // to the front
    CHECK(destinationPage(model(*session), "dest-alpha") == 0);
    CHECK(bookmark(model(*session), "P-0003").destination.pageIndex == 0);

    auto reopened = savedAndReopened(*session);
    CHECK(destinationPage(model(*reopened), "dest-alpha") == 0);
    CHECK(bookmark(model(*reopened), "P-0003").destination.pageIndex == 0);

    // THE PASTE UNDONE AFTER THAT SAVE, then saved: the file is the cut's again -- `dest-alpha` and the bookmark going
    // nowhere, as a session that only cut writes them.
    REQUIRE(session->journal->undo());
    auto undone = savedAndReopened(*session);
    auto onlyCut = tests::openCopy(kFile);
    REQUIRE(onlyCut);
    (void)cut(*onlyCut, {2});
    auto cutSaved = savedAndReopened(*onlyCut);
    CHECK(destinationPage(model(*undone), "dest-alpha") == destinationPage(model(*cutSaved), "dest-alpha"));
    CHECK(bookmark(model(*undone), "P-0003").destination.pageIndex == bookmark(model(*cutSaved), "P-0003").destination.pageIndex);
    CHECK(model(*undone).pages().count() == 11);
}

// ==========================================================================================
// D7-37(c): only what STILL goes nowhere
// ==========================================================================================

TEST_CASE("reconnect a reference pointed elsewhere after the cut stays where it was pointed", "[I-23][reconnect]")
{
    auto session = tests::openCopy(kFile);
    REQUIRE(session);
    const core::CopiedPageReferences references = cut(*session, {5});
    // The bookmark P-0006 pointed at page 3 by hand between the cut and the paste.
    core::Destination three;
    three.pageIndex = 3;
    REQUIRE(session->journal->run(std::make_unique<core::SetBookmarkTargetCommand>(bookmark(model(*session), "P-0006").id,
                                                                                    OutlineAction::goTo(three))));
    core::InsertPagesCommand* pasted = paste(*session, references, {5}, 11);
    CHECK(bookmark(model(*session), "P-0006").action.destination.pageIndex == 3);
    CHECK(bookmark(model(*session), "P-0006").destination.pageIndex == 3);
    CHECK(linkTarget(model(*session), 20) == 11);   // the link still went nowhere: named again
    // `reconnectionFor` left the bookmark out of the line ...
    for (const core::PageReconnection::Incoming& in : pasted->reconnection().incoming) CHECK(in.kind == core::ReconnectKind::Link);

    // ... and the COMMAND asks again when it runs: a line naming a bookmark that names a page leaves it where it is.
    core::PageReconnection named;
    named.incoming.push_back({core::ReconnectKind::Bookmark, bookmark(model(*session), "P-0006").id, {}, {}, 0});
    named.incoming.push_back({core::ReconnectKind::BookmarkAction, bookmark(model(*session), "P-0006").id, {}, {}, 0});
    REQUIRE(session->journal->run(std::make_unique<core::InsertPagesCommand>(0, copyOf({1}), QString(), named)));
    CHECK(bookmark(model(*session), "P-0006").destination.pageIndex == 4);   // page 3, one later for the insert
    CHECK(bookmark(model(*session), "P-0006").action.destination.pageIndex == 4);
}

TEST_CASE("reconnect a copy pasted back names nothing again but its own links and a second paste of a cut is a copy", "[I-23][reconnect]")
{
    auto session = tests::openCopy(kFile);
    REQUIRE(session);
    // A COPY of P-0001 pasted after it: the originals are still named, so the bookmark stays on the original; the
    // pasted page's links name what its source's name, as a duplicate's do.
    const core::CopiedPageReferences copied = core::referencesOfPages(model(*session), {0});
    core::InsertPagesCommand* pasted = paste(*session, copied, {0}, 1);
    CHECK(pasted->reconnection().incoming.empty());
    CHECK(bookmark(model(*session), "P-0001").destination.pageIndex == 0);
    CHECK(linksOn(model(*session), 1) == std::vector<int>{9, 6});   // P-0009 and P-0006, one later for the paste
    CHECK(linksOn(model(*session), 0) == std::vector<int>{9, 6});

    // A CUT pasted twice: the first paste is named; the second finds the references named and is a copy.
    const core::CopiedPageReferences references = cut(*session, {6});   // P-0006, now index 6
    paste(*session, references, {5}, 0);
    CHECK(bookmark(model(*session), "P-0006").destination.pageIndex == 0);
    core::InsertPagesCommand* again = paste(*session, references, {5}, 1);
    CHECK(again->reconnection().incoming.empty());
    CHECK(bookmark(model(*session), "P-0006").destination.pageIndex == 0);
}

// ==========================================================================================
// THE CONTROL: the same paste with no record is the plain insert -- what the product did before I-23
// ==========================================================================================

TEST_CASE("reconnect the control a paste with no record leaves what the cut cleared going nowhere", "[I-23][reconnect]")
{
    auto session = tests::openCopy(kFile);
    REQUIRE(session);
    (void)cut(*session, {5});
    REQUIRE(session->journal->run(std::make_unique<core::InsertPagesCommand>(11, copyOf({5}))));
    CHECK_FALSE(bookmark(model(*session), "P-0006").destination.isResolved());
    CHECK(linkTarget(model(*session), 20) == -1);
}

// ==========================================================================================
// The journal line: a replay does what the paste did
// ==========================================================================================

TEST_CASE("reconnect a paste's journal line replayed over the same document names the same pages", "[I-23][reconnect][serialize]")
{
    auto session = tests::openCopy(kFile);
    REQUIRE(session);
    const core::CopiedPageReferences references = cut(*session, {0});
    core::InsertPagesCommand* command = paste(*session, references, {0}, 11);
    const QJsonObject line = command->serialize();
    REQUIRE(line.contains(QStringLiteral("reconnect")));

    // A second session over the same file: the cut as a line would replay it, then the paste read back from its line.
    auto replay = tests::openCopy(kFile);
    REQUIRE(replay);
    REQUIRE(replay->journal->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{0})));
    auto read = core::CommandRegistry::deserialize(line);
    REQUIRE(read);
    REQUIRE(replay->journal->run(std::move(read).value()));
    CHECK(bookmark(model(*replay), "P-0001").destination.pageIndex == 11);
    CHECK(linksOn(model(*replay), 11) == linksOn(model(*session), 11));
    CHECK(model(*replay).modelHash() == model(*session).modelHash());

    // An insert with nothing to name writes the line it always wrote: no "reconnect" key.
    CHECK_FALSE(core::InsertPagesCommand(0, copyOf({1})).serialize().contains(QStringLiteral("reconnect")));
}
