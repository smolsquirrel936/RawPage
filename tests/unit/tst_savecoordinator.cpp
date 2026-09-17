// tst_savecoordinator -- `core/doc/SaveCoordinator`'s decisions. Stage 3 T8, guide case A-50
// (`Stage3-Mutation-Testing-Guide.md` §5.9), decisions D3-4, D3-6 and D3-16.
//
// WHAT THIS SUITE IS TRYING TO CATCH: a save that writes when it should refuse; a repaired or changed
// file appended to in place; and -- the S1 -- a failure at any step that leaves the target altered, a
// temporary file behind, or the journal marked saved.
//
// WHAT IT DOES NOT COVER (convention 19): the REAL flush and replace. `tests/unit/FakeDurableFiles.h`
// replaces by remove-and-rename and flushes nothing; `ReplaceFileW` is measured by
// `tests/platform/tst_durablefiles.cpp` (A-47, A-48) and used by `tests/render/tst_saveengine.cpp`
// (A-57). The engine is not here either -- the backend is `tests/unit/CosBackend.h`.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "pdf/ops/RevisionWriter.h"

using namespace rawpage;
using core::SaveOutcome;
using tests::SaveSession;

namespace {

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

// What every failed save must leave: the target as opened, no temporary file, the journal dirty.
void requireNothingChanged(SaveSession& session)
{
    CHECK(tests::sha256(tests::readAllBytes(session.path)) == tests::sha256(session.openedBytes));
    CHECK(session.strayTemporaries().isEmpty());
    CHECK(session.journal->isDirty());
}

} // namespace

TEST_CASE("save coordinator refuses without durable files and without changes", "[save][coordinator]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);

    SECTION("a clean document writes nothing")
    {
        auto result = session->coordinator->save(*session->journal, session->path);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::Unavailable);
        CHECK(keyOf(result.error()) == "error.save.nothingToSave");
        CHECK(session->durableFiles.replaceCalls == 0);
        CHECK(session->strayTemporaries().isEmpty());
        CHECK(tests::sha256(tests::readAllBytes(session->path)) == tests::sha256(session->openedBytes));
    }
    SECTION("no IDurableFiles is refused rather than written another way")
    {
        REQUIRE(session->rotate(0));
        core::SaveCoordinator noPlatform(session->backend, nullptr);
        auto result = noPlatform.save(*session->journal, session->path);
        REQUIRE_FALSE(result);
        CHECK(keyOf(result.error()) == "error.save.noDurableFiles");
        requireNothingChanged(*session);
    }
}

TEST_CASE("save coordinator sends a repaired document to Save-As", "[save][coordinator]")
{
    // D3-6. `malformed/circular-prev.pdf` opens only by a repair that `requiresSaveAs()`.
    auto session = tests::openCopy("malformed/circular-prev.pdf");
    REQUIRE(session);
    REQUIRE(session->journal->document().repairReport().requiresSaveAs());
    REQUIRE(session->rotate(0));

    auto result = session->coordinator->save(*session->journal, session->path);
    REQUIRE(result);
    CHECK(result.value().kind == SaveOutcome::Kind::SaveAsRequired);
    CHECK(std::string(result.value().reasonKey.data()) == "error.save.repairedDocument");
    requireNothingChanged(*session);
    CHECK(session->durableFiles.replaceCalls == 0);

    // ...and Save-As onto its OWN path is the same refusal, not a way round it.
    auto sameFile = session->coordinator->saveAs(*session->journal, session->path);
    REQUIRE(sameFile);
    CHECK(sameFile.value().kind == SaveOutcome::Kind::SaveAsRequired);
    requireNothingChanged(*session);
}

TEST_CASE("save coordinator sends a file changed on disk to Save-As", "[save][coordinator]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    REQUIRE(session->rotate(0));

    SECTION("different bytes")
    {
        { QFile file(session->path); REQUIRE(file.open(QIODevice::WriteOnly)); file.write("not the file that was opened"); }
    }
    SECTION("the same size and modification time, one byte different -- only the hash can see it")
    {
        const QDateTime modified = QFileInfo(session->path).lastModified();
        QByteArray altered = session->openedBytes;
        altered[altered.size() / 2] = altered[altered.size() / 2] == 'x' ? 'y' : 'x';
        {
            QFile file(session->path);
            REQUIRE(file.open(QIODevice::ReadWrite));
            file.write(altered);
        }   // closed: the write is flushed before the time is set
        {
            // A SEPARATE open, with nothing written. Setting the time on the handle that wrote -- the first
            // version of this case -- passed on Windows and failed on CI's Linux ASan job (run 34913473783):
            // QFile flushed its buffered write at close, AFTER setFileTime, and the flush moved the time again.
            QFile file(session->path);
            REQUIRE(file.open(QIODevice::ReadWrite));
            REQUIRE(file.setFileTime(modified, QFileDevice::FileModificationTime));
        }
        REQUIRE(QFileInfo(session->path).lastModified() == modified);
        REQUIRE(QFileInfo(session->path).size() == session->openedBytes.size());
    }
    SECTION("deleted")
    {
        REQUIRE(QFile::remove(session->path));
    }

    const QByteArray before = QFileInfo::exists(session->path) ? tests::readAllBytes(session->path) : QByteArray();
    auto result = session->coordinator->save(*session->journal, session->path);
    REQUIRE(result);
    CHECK(result.value().kind == SaveOutcome::Kind::SaveAsRequired);
    CHECK(std::string(result.value().reasonKey.data()) == "error.save.fileChanged");
    CHECK(session->durableFiles.replaceCalls == 0);
    CHECK(session->strayTemporaries().isEmpty());
    CHECK(session->journal->isDirty());
    if (QFileInfo::exists(session->path))
        CHECK(tests::sha256(tests::readAllBytes(session->path)) == tests::sha256(before));
}

TEST_CASE("save coordinator leaves the target and the journal alone when a step fails", "[save][coordinator]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    REQUIRE(session->rotate(0));

    // D3-16, second half: a step that failed because THIS PLACE refused asks for Save-As and carries the
    // named error as its reason; one that failed because our own bytes are wrong stays an error, since no
    // other path would take them either (Stage 3 guide I-15). Each section says which it is.
    SECTION("the write -- the place refused, so Save-As is offered")
    {
        session->backend.failNextWrites(true);
        auto result = session->coordinator->save(*session->journal, session->path);
        REQUIRE(result);
        CHECK(result.value().kind == SaveOutcome::Kind::SaveAsRequired);
        CHECK(std::string(result.value().reasonKey.data()) == "error.save.writeFailed");
        CHECK(session->durableFiles.syncCalls == 0);
    }
    SECTION("the re-read")
    {
        // A composed file that no longer starts with the opened bytes: its own check refuses it.
        pdf::ops::RevisionWriter::setComposeFaultForTesting([](QByteArray& bytes) { bytes[0] = 'X'; });
        auto result = session->coordinator->save(*session->journal, session->path);
        pdf::ops::RevisionWriter::setComposeFaultForTesting({});
        // NOT Save-As: the bytes are the fault, and every path would refuse them identically.
        REQUIRE_FALSE(result);
        CHECK(keyOf(result.error()) == "error.save.verifyFailed");
        CHECK(session->durableFiles.syncCalls == 0);
    }
    SECTION("the flush -- the place refused, so Save-As is offered")
    {
        session->durableFiles.failSync = true;
        auto result = session->coordinator->save(*session->journal, session->path);
        REQUIRE(result);
        CHECK(result.value().kind == SaveOutcome::Kind::SaveAsRequired);
        CHECK(std::string(result.value().reasonKey.data()) == "error.file.syncFailed");
        CHECK(session->durableFiles.replaceCalls == 0);
    }
    SECTION("the replace -- the place refused, so Save-As is offered, and it carries the target")
    {
        session->durableFiles.failReplace = true;
        auto result = session->coordinator->save(*session->journal, session->path);
        REQUIRE(result);
        CHECK(result.value().kind == SaveOutcome::Kind::SaveAsRequired);
        CHECK(std::string(result.value().reasonKey.data()) == "error.file.inUse");
        // The reason's ARGUMENT travels with it, or the banner renders a placeholder (`%1`).
        CHECK(result.value().reasonContext == session->path);
    }
    requireNothingChanged(*session);
    CHECK(session->backend.adoptions() == 0);

    // THE CONTROL for every section above: with the fault gone, the same save succeeds.
    session->backend.failNextWrites(false);
    session->durableFiles.failSync = false;
    session->durableFiles.failReplace = false;
    auto saved = session->coordinator->save(*session->journal, session->path);
    REQUIRE(saved);
    CHECK(saved.value().kind == SaveOutcome::Kind::Saved);
    CHECK_FALSE(session->journal->isDirty());
    CHECK(tests::readAllBytes(session->path).startsWith(session->openedBytes));
}

TEST_CASE("save coordinator keeps the journal dirty when the backend cannot adopt the saved file", "[save][coordinator]")
{
    // The one failure AFTER the replace. The new file is on disk; the journal must not claim it is
    // saved, and the next Save must not append to bytes the backend never adopted.
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    REQUIRE(session->rotate(0));
    session->backend.failNextAdopts(true);

    auto result = session->coordinator->save(*session->journal, session->path);
    REQUIRE_FALSE(result);
    CHECK(keyOf(result.error()) == "error.save.adoptFailed");
    CHECK(session->durableFiles.replaceCalls == 1);
    CHECK(session->journal->isDirty());
    CHECK(session->strayTemporaries().isEmpty());
    CHECK(tests::readAllBytes(session->path).startsWith(session->openedBytes));

    session->backend.failNextAdopts(false);
    auto next = session->coordinator->save(*session->journal, session->path);
    REQUIRE(next);
    CHECK(next.value().kind == SaveOutcome::Kind::SaveAsRequired);
    CHECK(std::string(next.value().reasonKey.data()) == "error.save.fileChanged");
}

TEST_CASE("save coordinator removes a dead writer's temporary file and keeps a live one", "[save][coordinator]")
{
    // Guide case A-60, with process liveness planted through the fake. `processExists` itself is
    // `tests/platform/tst_durablefiles.cpp`'s.
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    REQUIRE(session->rotate(0));
    const QString dead = core::SaveCoordinator::temporaryPathFor(session->path, 424242);
    const QString live = core::SaveCoordinator::temporaryPathFor(session->path, 515151);
    for (const QString& stray : {dead, live}) {
        QFile file(stray);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("left by a process");
    }
    session->durableFiles.liveProcesses = {515151};

    REQUIRE(session->coordinator->save(*session->journal, session->path));
    CHECK_FALSE(QFileInfo::exists(dead));
    CHECK(QFileInfo::exists(live));
    CHECK(session->strayTemporaries() == QStringList{QFileInfo(live).fileName()});
}

TEST_CASE("save coordinator names its temporary file beside the target and by its writer", "[save][coordinator]")
{
    CHECK(core::SaveCoordinator::temporaryPathFor(QStringLiteral("C:/docs/report.pdf"), 1234)
          == QStringLiteral("C:/docs/.report.pdf.rawpage-1234.tmp"));
}
