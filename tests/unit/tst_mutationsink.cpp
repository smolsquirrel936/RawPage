// tst_mutationsink -- desired state, and the delta it produces. Stage 3 T3, guide cases A-30 … A-33
// (`Stage3-Mutation-Testing-Guide.md` §5.5) and T3's two controls.
//
// WHAT THIS SUITE IS TRYING TO CATCH:
//   1. **A delta that carries more than the change** -- the `/Pages` node, a sibling, an unchanged
//      page. The control is written from what must stay untouched (convention 22): every other object
//      in the re-read file has the same entry it had.
//   2. **A delta that exists when nothing changed** -- a rotation undone before a save must write
//      nothing, not restate the original (A-31).
//   3. **An undo lost across a save** -- after `adopt()`, reverting must write the page back (A-32).
//   4. **A refusal that changed something** (A-33).
//
// WHAT IT DOES NOT COVER, named at the assertion (convention 19):
//   * **A command.** `setPageRotation` is called directly, as `RotatePageCommand::apply()` will call it
//     at T5; nothing here goes through `CommandJournal`, and no key is pressed.
//   * **The disk.** Deltas are written with `IncrementalWriter` into a buffer and re-read; the file the
//     save path writes, through a temporary copy and a replace, is T8's.

#include <catch2/catch_test_macros.hpp>

#include "core/backend.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/MutationSinkImpl.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using rawpage::pdf::cos::IncrementalWriter;
using rawpage::pdf::cos::ObjectDelta;
using rawpage::pdf::cos::XrefEntryType;
using rawpage::pdf::cos::XrefTable;
using rawpage::pdf::ops::MutationSinkImpl;
using rawpage::pdf::ops::PageOps;

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

std::filesystem::path corpus(std::string_view relative)
{
    return std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative;
}

// A sink and the bytes it compares against, which it does not own.
struct Session {
    std::unique_ptr<std::string> bytes;
    std::unique_ptr<MutationSinkImpl> sink;
};

Session openSession(std::string_view relative)
{
    auto bytes = std::make_unique<std::string>(readFile(corpus(relative)));
    auto sink = MutationSinkImpl::open(*bytes);
    REQUIRE(sink);
    return Session{std::move(bytes), std::move(sink).value()};
}

// Write the sink's delta after its bytes; return the combined file, owned.
std::unique_ptr<std::string> saved(MutationSinkImpl& sink)
{
    auto delta = sink.delta();
    REQUIRE(delta);
    auto update = IncrementalWriter::append(sink.bytes(), sink.table(), delta.value());
    REQUIRE(update);
    return std::make_unique<std::string>(std::string(sink.bytes()) + update.value().bytes);
}

std::vector<long long> changedNumbers(const ObjectDelta& delta)
{
    std::vector<long long> numbers;
    for (const auto& change : delta.changes())
        numbers.push_back(change.number);
    return numbers;
}

} // namespace

TEST_CASE("mutation sink is the core MutationSink", "[ops][sink]")
{
    // The interface `core/` calls. A sink that did not implement it would compile here and fail at T4.
    Session session = openSession("ordinary/text-a4.pdf");
    rawpage::core::MutationSink& asInterface = *session.sink;
    CHECK(asInterface.setPageRotation(0, 90));
    CHECK(session.sink->desiredRotation(0) == 90);
}

TEST_CASE("mutation sink writes an inherited rotation on the page and only there", "[ops][sink]")
{
    // GUIDE CASE A-30, over `ordinary/rotate-inherited.pdf`: the root /Pages node carries /Rotate 90,
    // page index 0 inherits it.
    Session session = openSession("ordinary/rotate-inherited.pdf");
    MutationSinkImpl& sink = *session.sink;
    REQUIRE(sink.pageCount() == 3);
    const long long kid = sink.pages().pages[0].number;
    REQUIRE(sink.pages().pages[0].effectiveRotate() == 90);

    // Rotate the inheriting kid by 90 more: the delta is that kid, with /Rotate 180, and nothing else.
    REQUIRE(sink.setPageRotation(0, 180));
    auto first = sink.delta();
    REQUIRE(first);
    CHECK(changedNumbers(first.value()) == std::vector<long long>{kid});
    CHECK(first.value().changes().front().object.get("Rotate").integerValue(-1) == 180);

    // 270 more is 450, which is 90 -- the inherited value, on a kid with no /Rotate on disk: EMPTY.
    REQUIRE(sink.setPageRotation(0, 450));
    auto second = sink.delta();
    REQUIRE(second);
    CHECK(second.value().isEmpty());
}

TEST_CASE("mutation sink rotating one kid changes no other object", "[ops][sink]")
{
    // T3's control, written from what must stay UNTOUCHED (convention 22): after the delta is written
    // and re-read, the /Pages nodes, the siblings and every other object have exactly the entry they
    // had, and only the kid moved into the new revision.
    Session session = openSession("ordinary/rotate-inherited.pdf");
    MutationSinkImpl& sink = *session.sink;
    const long long kid = sink.pages().pages[0].number;
    REQUIRE(sink.setPageRotation(0, 180));

    auto before = XrefTable::load(*session.bytes);
    REQUIRE(before);
    auto combined = saved(sink);
    auto after = XrefTable::load(*combined);
    REQUIRE(after);

    for (const auto& [number, entry] : before.value().entries()) {
        INFO("object " << number);
        const auto* now = after.value().find(number);
        REQUIRE(now != nullptr);
        if (number == kid) {
            CHECK(now->revision == 0);
            continue;
        }
        CHECK(now->revision == entry.revision + 1);
        CHECK(now->field2 == entry.field2);
    }

    auto walk = PageOps::walk(after.value());
    REQUIRE(walk);
    CHECK(walk.value().pages[0].effectiveRotate() == 180);
    CHECK(walk.value().pages[1].effectiveRotate() == 0);    // the explicit-0 sibling, untouched
    CHECK(walk.value().pages[2].effectiveRotate() == 90);   // the grandchild still inherits
}

TEST_CASE("mutation sink writes nothing for a rotation undone before a save", "[ops][sink]")
{
    // GUIDE CASE A-31.
    Session session = openSession("ordinary/text-a4.pdf");
    MutationSinkImpl& sink = *session.sink;

    SECTION("rotate and revert")
    {
        REQUIRE(sink.setPageRotation(0, 90));
        REQUIRE(sink.setPageRotation(0, 0));
    }
    SECTION("four quarter turns")
    {
        for (int degrees : {90, 180, 270, 360})
            REQUIRE(sink.setPageRotation(0, degrees));
    }

    auto delta = sink.delta();
    REQUIRE(delta);
    CHECK(delta.value().isEmpty());

    // And a save of it is "nothing to save", not an empty update.
    auto update = IncrementalWriter::append(sink.bytes(), sink.table(), delta.value());
    REQUIRE_FALSE(update);
    CHECK(std::string(update.error().messageKey.data()) == "error.pdf.writeNothing");
}

TEST_CASE("mutation sink computes against the newest revision after a save", "[ops][sink]")
{
    // GUIDE CASE A-32: rotate, save (adopt), revert -- the delta holds the page dictionary with its
    // PRE-ROTATION value, because the newest revision on disk is now the rotated one.
    Session session = openSession("ordinary/text-a4.pdf");
    MutationSinkImpl& sink = *session.sink;
    const long long page = sink.pages().pages[0].number;

    auto original = XrefTable::load(*session.bytes);
    REQUIRE(original);
    auto originalPage = original.value().fetch(page);
    REQUIRE(originalPage);
    REQUIRE_FALSE(originalPage.value().object.has("Rotate"));

    REQUIRE(sink.setPageRotation(0, 90));
    auto firstSave = saved(sink);
    REQUIRE(sink.adopt(*firstSave));

    // Adopted: the disk now says 90, so nothing is pending.
    CHECK(sink.pages().pages[0].effectiveRotate() == 90);
    auto pending = sink.delta();
    REQUIRE(pending);
    CHECK(pending.value().isEmpty());

    // Revert. A sink still comparing with the file as first opened would find nothing to write here,
    // and the undo would be lost on the next save.
    REQUIRE(sink.setPageRotation(0, 0));
    auto undo = sink.delta();
    REQUIRE(undo);
    REQUIRE(changedNumbers(undo.value()) == std::vector<long long>{page});
    // The pre-rotation dictionary: the same value the page had when the file was opened.
    CHECK(undo.value().changes().front().object == originalPage.value().object);

    auto secondSave = saved(sink);
    auto reread = XrefTable::load(*secondSave);
    REQUIRE(reread);
    auto walk = PageOps::walk(reread.value());
    REQUIRE(walk);
    CHECK(walk.value().pages[0].effectiveRotate() == 0);
    CHECK(reread.value().sections().size() == original.value().sections().size() + 2);
}

TEST_CASE("mutation sink normalizes rotations and refuses impossible ones without trace", "[ops][sink]")
{
    // GUIDE CASE A-33.
    Session session = openSession("ordinary/text-a4.pdf");
    MutationSinkImpl& sink = *session.sink;

    REQUIRE(sink.setPageRotation(0, -90));
    CHECK(sink.desiredRotation(0) == 270);
    REQUIRE(sink.setPageRotation(1, 450));
    CHECK(sink.desiredRotation(1) == 90);

    auto refused = sink.setPageRotation(0, 45);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == rawpage::core::ErrorCode::InvalidArgument);
    CHECK(std::string(refused.error().messageKey.data()) == "error.pdf.rotationNotRightAngle");
    CHECK(sink.desiredRotation(0) == 270);     // unchanged

    for (int badIndex : {-1, sink.pageCount(), 1000}) {
        INFO("index " << badIndex);
        auto outside = sink.setPageRotation(badIndex, 90);
        REQUIRE_FALSE(outside);
        CHECK(outside.error().code == rawpage::core::ErrorCode::InvalidArgument);
        CHECK(std::string(outside.error().messageKey.data()) == "error.pdf.pageOutOfRange");
    }
    CHECK(sink.desiredRotation(0) == 270);
    CHECK(sink.desiredRotation(1) == 90);
    CHECK(sink.desiredRotation(2) == 0);

    auto delta = sink.delta();
    REQUIRE(delta);
    CHECK(delta.value().changes().size() == 2);
}

TEST_CASE("mutation sink lifts a packed page out of its object stream", "[ops][sink]")
{
    // The xref-stream shape: page 1 is a type-2 object. The delta is an ordinary replacement, and the
    // writer lifts it out (A-20); this proves the sink hands the writer a page it can fetch.
    Session session = openSession("ordinary/objstm-xrefstream.pdf");
    MutationSinkImpl& sink = *session.sink;
    const long long page = sink.pages().pages[0].number;
    REQUIRE(sink.table().find(page)->type == XrefEntryType::InObjectStream);

    REQUIRE(sink.setPageRotation(0, 90));
    auto combined = saved(sink);
    auto reread = XrefTable::load(*combined);
    REQUIRE(reread);
    CHECK(reread.value().find(page)->type == XrefEntryType::InFile);
    auto walk = PageOps::walk(reread.value());
    REQUIRE(walk);
    CHECK(walk.value().pages[0].effectiveRotate() == 90);
}

TEST_CASE("mutation sink refuses to adopt a file with a different page count", "[ops][sink]")
{
    Session session = openSession("ordinary/text-a4.pdf");
    MutationSinkImpl& sink = *session.sink;
    REQUIRE(sink.setPageRotation(0, 90));
    const std::string other = readFile(corpus("ordinary/rotated-pages.pdf"));
    REQUIRE(other.size() != session.bytes->size());

    auto adopted = sink.adopt(other);
    REQUIRE_FALSE(adopted);
    CHECK(std::string(adopted.error().messageKey.data()) == "error.pdf.pageTreeChanged");
    // Nothing moved: the same bytes, the same pending rotation.
    CHECK(sink.bytes().data() == session.bytes->data());
    CHECK(sink.desiredRotation(0) == 90);
}

TEST_CASE("mutation sink declines an encrypted document by name", "[ops][sink]")
{
    const std::string bytes = readFile(corpus("encrypted/aes-128.pdf"));
    auto sink = MutationSinkImpl::open(bytes);
    REQUIRE_FALSE(sink);
    CHECK(std::string(sink.error().messageKey.data()) == "error.pdf.encrypted");
}
