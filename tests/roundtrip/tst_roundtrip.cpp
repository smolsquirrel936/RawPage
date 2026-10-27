// tst_roundtrip -- open, rotate, save, reopen: over the corpus, through the shipped save path. Stage 3 T8,
// guide cases A-51 … A-56 (`Stage3-Mutation-Testing-Guide.md` §5.9), decisions D3-4, D3-5, D3-6, D3-16.
//
// A SOURCE DIRECTORY COMPILED INTO `rawpage_tests`, not a sixth target (D3-18): it links exactly what
// that target may, and so it also runs under `portable-asan`.
//
// EVERY ASSERTION ABOUT THE SAVED FILE READS IT FROM ITS PATH (guide rule 3). The path is what the
// user's next launch will open; a buffer in memory is what the writer meant to put there.
//
// WHAT IT DOES NOT COVER (convention 19):
//   * the ENGINE -- the backend is `tests/unit/CosBackend.h`. PDFium re-reading these files, and the
//     pixels, are `tests/render/tst_saveengine.cpp` (A-58);
//   * the real flush and replace -- `tests/unit/FakeDurableFiles.h` removes and renames. `ReplaceFileW`
//     is A-47's and A-57's;
//   * the recovery journal's `meta.json` naming a Save-As target (A-56's last clause) -- T9.
//
// Stage 3 T13 adds the replay of `fuzz_cos_roundtrip`'s body (D3-17, guide case A-83) at the end of the
// file: the corpus through the SAME function the fuzz target calls, and the three controls that prove
// its checks fire. It is not fuzzing; `portable-fuzz` is.

#include <catch2/catch_test_macros.hpp>

#include "../unit/SaveSession.h"
#include "tests/fuzz/RoundtripFuzzBody.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/MutationSinkImpl.h"
#include "pdf/ops/PageOps.h"
#include "pdf/ops/RevisionWriter.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>

using namespace rawpage;
using core::SaveOutcome;
using tests::SaveSession;

namespace {

namespace fs = std::filesystem;

std::string_view viewOf(const QByteArray& bytes)
{
    return std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// Every corpus file the product may edit, as far as a headless suite can tell -- `tst_reversibility`'s
// set, for the same reasons: every PDF outside `encrypted/`, minus the one the two readers count
// differently (guide issue I-07).
std::vector<std::string> editableCandidates()
{
    std::vector<std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        const std::string relative = fs::relative(entry.path(), RAWPAGE_CORPUS_DIR).generic_string();
        if (relative.rfind("encrypted/", 0) == 0 || relative == "malformed/pages-loop.pdf")
            continue;
        files.push_back(relative);
    }
    std::sort(files.begin(), files.end());
    return files;
}

struct Reopened {
    pdf::cos::OpenedFile opened;
    pdf::ops::PageWalk walk;
};

std::optional<Reopened> reopen(const QByteArray& bytes)
{
    auto opened = pdf::cos::openWithRepair(viewOf(bytes));
    if (!opened)
        return std::nullopt;
    auto walk = pdf::ops::PageOps::walk(opened.value().table);
    if (!walk)
        return std::nullopt;
    return Reopened{std::move(opened).value(), std::move(walk).value()};
}

// The object numbers whose newest definition sits at or after `boundary` -- what the update wrote.
std::set<long long> objectsDefinedAfter(const pdf::cos::XrefTable& table, std::size_t boundary)
{
    std::set<long long> numbers;
    for (const auto& [number, entry] : table.entries()) {
        if (entry.type == pdf::cos::XrefEntryType::InFile && static_cast<std::size_t>(entry.field2) >= boundary)
            numbers.insert(number);
    }
    return numbers;
}

} // namespace

// ==========================================================================================
// A-51 and A-52 -- the round trip over the corpus
// ==========================================================================================

TEST_CASE("roundtrip over every editable corpus file saves the rotation and keeps the opened bytes", "[save][roundtrip]")
{
    std::size_t saved = 0;
    std::size_t savedAs = 0;
    std::vector<std::string> notEditable;

    for (const std::string& relative : editableCandidates()) {
        INFO("file: " << relative);
        auto session = tests::openCopy(relative);
        if (!session) {
            notEditable.push_back(relative);
            continue;
        }
        const int lastPage = session->journal->document().pages().count() - 1;
        REQUIRE(lastPage >= 0);
        const long long pageObject = session->sink()->pages().pages[static_cast<std::size_t>(lastPage)].number;
        const int expectedRotation = (session->sink()->desiredRotation(lastPage) + 90) % 360;
        const bool repaired = session->journal->document().repairReport().requiresSaveAs();
        REQUIRE(session->rotate(lastPage));

        QString target = session->path;
        auto result = session->coordinator->save(*session->journal, session->path);
        REQUIRE(result);
        if (repaired) {
            // A-52 (D3-6): the Save is refused into a Save-As, and the Save-As goes to a new file.
            REQUIRE(result.value().kind == SaveOutcome::Kind::SaveAsRequired);
            CHECK(std::string(result.value().reasonKey.data()) == "error.save.repairedDocument");
            target = session->directory.filePath(QStringLiteral("saved-as.pdf"));
            auto asResult = session->coordinator->saveAs(*session->journal, target);
            INFO("save-as error: " << (asResult ? std::string() : std::string(asResult.error().messageKey.data()) + " " + asResult.error().context.toStdString()));
            REQUIRE(asResult);
            REQUIRE(asResult.value().kind == SaveOutcome::Kind::Saved);
            CHECK(tests::sha256(tests::readAllBytes(session->path)) == tests::sha256(session->openedBytes));   // the original is untouched
            ++savedAs;
        } else {
            INFO("save error: " << (result.value().kind == SaveOutcome::Kind::Saved ? std::string() : std::string(result.value().reasonKey.data())));
            REQUIRE(result.value().kind == SaveOutcome::Kind::Saved);
            ++saved;
        }
        CHECK_FALSE(session->journal->isDirty());
        CHECK(session->strayTemporaries().isEmpty());

        // ---- READ FROM THE PATH ----
        const QByteArray onDisk = tests::readAllBytes(target);
        CHECK(onDisk.startsWith(session->openedBytes));                        // the prefix, byte for byte
        auto again = reopen(onDisk);
        REQUIRE(again.has_value());
        CHECK(again->opened.usedOriginalXref);                                 // opened through its own table
        CHECK_FALSE(again->opened.report.requiresSaveAs());
        if (!repaired) {
            // No finding the original did not already have.
            auto original = reopen(session->openedBytes);
            REQUIRE(original.has_value());
            CHECK(again->opened.report.entries().size() <= original->opened.report.entries().size());
            // The update holds the changed page and nothing else -- apart from an xref stream's own
            // object, which is the section itself.
            std::set<long long> written = objectsDefinedAfter(again->opened.table, static_cast<std::size_t>(session->openedBytes.size()));
            const std::size_t newest = again->opened.table.sections().front().offset;
            for (auto it = written.begin(); it != written.end();) {
                const auto* entry = again->opened.table.find(*it);
                if (entry && static_cast<std::size_t>(entry->field2) == newest)
                    it = written.erase(it);
                else
                    ++it;
            }
            CHECK(written == std::set<long long>{pageObject});
        }
        REQUIRE(static_cast<int>(again->walk.pages.size()) == lastPage + 1);
        CHECK(again->walk.pages[static_cast<std::size_t>(lastPage)].effectiveRotate() == expectedRotation);
    }

    std::cout << "roundtrip: " << saved << " saved in place, " << savedAs
              << " repaired files saved as a new file; not editable by the first-party writer:";
    for (const std::string& name : notEditable)
        std::cout << " " << name;
    std::cout << "\n";
    CHECK(saved >= 36);
    CHECK(savedAs >= 1);
}

// ==========================================================================================
// A-53 -- a file changed on disk is never appended to
// ==========================================================================================

TEST_CASE("roundtrip never appends to a file changed on disk since it was opened", "[save][roundtrip]")
{
    auto session = tests::openCopy("ordinary/rotated-pages.pdf");
    REQUIRE(session);
    REQUIRE(session->rotate(0));
    const QByteArray other = tests::readAllBytes(tests::corpusFile("ordinary/text-letter.pdf"));
    { QFile file(session->path); REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write(other); }

    auto result = session->coordinator->save(*session->journal, session->path);
    REQUIRE(result);
    CHECK(result.value().kind == SaveOutcome::Kind::SaveAsRequired);
    CHECK(tests::sha256(tests::readAllBytes(session->path)) == tests::sha256(other));
    CHECK(session->strayTemporaries().isEmpty());
    CHECK(session->durableFiles.replaceCalls == 0);
}

// ==========================================================================================
// A-54 -- the re-read guards the replace (the headless half: the real replace is A-57's file)
// ==========================================================================================

TEST_CASE("roundtrip never replaces the target with a file its own reader rejects", "[save][roundtrip]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    REQUIRE(session->rotate(1));

    SECTION("an update that does not parse")
    {
        // Cut the composed file inside its update, after the prefix: the prefix check passes and the
        // parse is what must refuse it.
        const qsizetype keep = session->openedBytes.size() + 20;
        pdf::ops::RevisionWriter::setComposeFaultForTesting([keep](QByteArray& bytes) { bytes.truncate(keep); });
    }
    SECTION("an update whose rotation reads back wrong")
    {
        pdf::ops::RevisionWriter::setComposeFaultForTesting([](QByteArray& bytes) {
            const qsizetype at = bytes.lastIndexOf("/Rotate 90");   // absent: the save succeeds and the case fails
            if (at > 0)
                bytes.replace(at, 10, "/Rotate 00");
        });
    }
    auto result = session->coordinator->save(*session->journal, session->path);
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});

    REQUIRE_FALSE(result);
    CHECK(std::string(result.error().messageKey.data()) == "error.save.verifyFailed");
    INFO("check that refused: " << result.error().context.toStdString());
    CHECK(session->durableFiles.replaceCalls == 0);
    CHECK(tests::sha256(tests::readAllBytes(session->path)) == tests::sha256(session->openedBytes));
    CHECK(session->strayTemporaries().isEmpty());
    CHECK(session->journal->isDirty());
}

// ==========================================================================================
// A-55 -- save after save, and undo after save
// ==========================================================================================

TEST_CASE("roundtrip chains three saves and an undo after a save writes the page back", "[save][roundtrip]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const auto sections = [](const QByteArray& bytes) {
        auto opened = reopen(bytes);
        REQUIRE(opened.has_value());
        return opened->opened.table.sections().size();
    };
    const std::size_t originalSections = sections(session->openedBytes);

    REQUIRE(session->rotate(0));
    REQUIRE(session->coordinator->save(*session->journal, session->path));
    const QByteArray first = tests::readAllBytes(session->path);

    REQUIRE(session->rotate(1));
    REQUIRE(session->coordinator->save(*session->journal, session->path));
    const QByteArray second = tests::readAllBytes(session->path);

    REQUIRE(session->journal->undo());                 // un-rotates page 2, which is IN the saved file
    REQUIRE(session->journal->isDirty());
    auto third = session->coordinator->save(*session->journal, session->path);
    REQUIRE(third);
    REQUIRE(third.value().kind == SaveOutcome::Kind::Saved);
    const QByteArray last = tests::readAllBytes(session->path);

    CHECK(first.startsWith(session->openedBytes));
    CHECK(second.startsWith(first));
    CHECK(last.startsWith(second));
    CHECK(sections(last) == originalSections + 3);

    auto reopened = reopen(last);
    REQUIRE(reopened.has_value());
    CHECK(reopened->walk.pages[0].effectiveRotate() == 90);
    CHECK(reopened->walk.pages[1].effectiveRotate() == 0);
}

// ==========================================================================================
// A-56 -- Save As moves the document and leaves the original alone
// ==========================================================================================

TEST_CASE("roundtrip Save As moves the document to the new file and leaves the original alone", "[save][roundtrip]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const QString copy = session->directory.filePath(QStringLiteral("copy.pdf"));

    REQUIRE(session->rotate(0));
    auto asResult = session->coordinator->saveAs(*session->journal, copy);
    REQUIRE(asResult);
    REQUIRE(asResult.value().kind == SaveOutcome::Kind::Saved);
    CHECK(session->journal->document().id().path == core::DocumentId::normalizePath(copy));

    REQUIRE(session->rotate(2));
    auto saveResult = session->coordinator->save(*session->journal, copy);
    REQUIRE(saveResult);
    REQUIRE(saveResult.value().kind == SaveOutcome::Kind::Saved);

    CHECK(tests::sha256(tests::readAllBytes(session->path)) == tests::sha256(session->openedBytes));      // the original, since open
    auto reopened = reopen(tests::readAllBytes(copy));
    REQUIRE(reopened.has_value());
    CHECK(reopened->walk.pages[0].effectiveRotate() == 90);
    CHECK(reopened->walk.pages[2].effectiveRotate() == 90);

    // A Save to the ORIGINAL path is now a file that is not the document's: Save-As, not an append.
    REQUIRE(session->rotate(1));
    auto stale = session->coordinator->save(*session->journal, session->path);
    REQUIRE(stale);
    CHECK(stale.value().kind == SaveOutcome::Kind::SaveAsRequired);
}

TEST_CASE("roundtrip Save As of an unchanged document is a byte-identical copy", "[save][roundtrip]")
{
    auto session = tests::openCopy("ordinary/objstm-xrefstream.pdf");
    REQUIRE(session);
    const QString copy = session->directory.filePath(QStringLiteral("unchanged.pdf"));
    auto result = session->coordinator->saveAs(*session->journal, copy);
    REQUIRE(result);
    CHECK(tests::sha256(tests::readAllBytes(copy)) == tests::sha256(session->openedBytes));
}

// ==========================================================================================
// A-83 -- fuzz_cos_roundtrip's body, replayed over the corpus (D3-17)
// ==========================================================================================
//
// The seed set `fuzz_cos_roundtrip` starts from, through `tests/fuzz/RoundtripFuzzBody.h`. What it
// claims: every corpus file the first-party reader can edit takes a rotation of page 1, composed with
// the product's choice of section, and reads back through its own table with every input byte in front
// and `RevisionWriter::verify` agreeing. What it does NOT cover (convention 19): anything the fuzzer
// would find, the disk (this composes in memory -- the tests above write), and the engine.

namespace {

std::vector<std::string> everyCorpusPdf()
{
    std::vector<std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (entry.is_regular_file() && entry.path().extension() == ".pdf")
            files.push_back(fs::relative(entry.path(), RAWPAGE_CORPUS_DIR).generic_string());
    }
    std::sort(files.begin(), files.end());
    return files;
}

fuzz::RoundtripVerdict fuzzBodyOver(const std::string& relative)
{
    const QByteArray bytes = tests::readAllBytes(tests::corpusFile(relative));
    return fuzz::roundtripFuzzOnce(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                   static_cast<std::size_t>(bytes.size()));
}

// The fault seam is process-wide; a failed REQUIRE must not leave it set for the next case.
struct ComposeFaultScope {
    explicit ComposeFaultScope(pdf::ops::RevisionWriter::ComposeFault fault)
    {
        pdf::ops::RevisionWriter::setComposeFaultForTesting(std::move(fault));
    }
    ~ComposeFaultScope() { pdf::ops::RevisionWriter::setComposeFaultForTesting({}); }
    ComposeFaultScope(const ComposeFaultScope&) = delete;
    ComposeFaultScope& operator=(const ComposeFaultScope&) = delete;
};

} // namespace

TEST_CASE("roundtrip fuzz body holds its invariants over every corpus file", "[save][roundtrip][fuzz]")
{
    std::size_t examined = 0;
    std::size_t editable = 0;
    std::size_t written = 0;
    std::size_t complete = 0;
    std::size_t encryptedDeclined = 0;

    for (const std::string& relative : everyCorpusPdf()) {
        const auto verdict = fuzzBodyOver(relative);
        INFO("file: " << relative);
        INFO("failure: " << verdict.failure);
        REQUIRE(verdict.ok);
        ++examined;
        if (verdict.editable)
            ++editable;
        if (verdict.written)
            ++written;
        if (verdict.complete)
            ++complete;
        // The not-editable branch is reached, and for the reason D2-6 gives: an encrypted file is
        // declined by name before anything is composed.
        if (relative.rfind("encrypted/", 0) == 0) {
            CHECK_FALSE(verdict.editable);
            ++encryptedDeclined;
        }
    }

    std::cout << "roundtrip fuzz body: examined " << examined << ", editable " << editable << ", written "
              << written << " (" << complete << " with a complete section), encrypted declined "
              << encryptedDeclined << "\n";

    // Floors, so a corpus file added later does not redden a case about the writer, and zero -- the
    // directory moved, the loop checked nothing -- does.
    CHECK(examined >= 67);
    CHECK(written >= 55);
    CHECK(written == editable);      // every editable file was written: no refusal survived REQUIRE above
    CHECK(complete >= 1);            // D3-6's complete section is on the replayed path, not just /Prev
    CHECK(encryptedDeclined >= 1);
}

TEST_CASE("roundtrip fuzz body reports each planted fault by the invariant it breaks", "[save][roundtrip][fuzz]")
{
    // Convention 7: a replay that is green over good files proves nothing about whether its checks can
    // fail. Each fault corrupts what `RevisionWriter::compose` returns, before the body checks it, and
    // must be named by the check it breaks -- and in each, `RevisionWriter::verify` must refuse too, so
    // the message carries no "verify ACCEPTED" suffix. A guard that accepts what the body rejects is
    // the one direction no fault here can plant without editing the guard; it is named, not covered.
    const std::string file = "ordinary/text-a4.pdf";
    REQUIRE(fuzzBodyOver(file).written);                       // the unplanted control
    const qsizetype originalSize = tests::readAllBytes(tests::corpusFile(file)).size();

    SECTION("a changed input byte is PREFIX")
    {
        const ComposeFaultScope scope([](QByteArray& bytes) { bytes[0] = bytes[0] == 'X' ? 'Y' : 'X'; });
        const auto verdict = fuzzBodyOver(file);
        INFO("failure: " << verdict.failure);
        CHECK_FALSE(verdict.ok);
        CHECK(verdict.failure.rfind("prefix:", 0) == 0);
        CHECK(verdict.failure.find("ACCEPTED") == std::string::npos);
    }
    SECTION("an update whose startxref points nowhere is OWN TABLE or REPARSE")
    {
        // Measured at T13: cutting the update twenty bytes in is NOT this fault -- the reader finds the
        // ORIGINAL's startxref, opens cleanly, and the body names it ROTATION, as the next section does.
        // Aiming the update's own startxref at byte 1 is what leaves the reader only its repair scan.
        const ComposeFaultScope scope([originalSize](QByteArray& bytes) {
            const qsizetype at = bytes.lastIndexOf("startxref");
            if (at > originalSize) {                                   // absent: nothing planted, the case fails
                qsizetype digits = at + 9;                             // past "startxref"
                while (digits < bytes.size() && (bytes[digits] == '\r' || bytes[digits] == '\n' || bytes[digits] == ' '))
                    ++digits;
                qsizetype end = digits;
                while (end < bytes.size() && bytes[end] >= '0' && bytes[end] <= '9')
                    ++end;
                bytes.replace(digits, end - digits, "1");
            }
        });
        const auto verdict = fuzzBodyOver(file);
        INFO("failure: " << verdict.failure);
        CHECK_FALSE(verdict.ok);
        CHECK((verdict.failure.rfind("own table:", 0) == 0 || verdict.failure.rfind("reparse:", 0) == 0));
        CHECK(verdict.failure.find("ACCEPTED") == std::string::npos);
    }
    SECTION("an update that is not there is ROTATION")
    {
        // The whole update dropped: the output IS the input, which opens cleanly and has page 1 where it was.
        const ComposeFaultScope scope([originalSize](QByteArray& bytes) { bytes.truncate(originalSize); });
        const auto verdict = fuzzBodyOver(file);
        INFO("failure: " << verdict.failure);
        CHECK_FALSE(verdict.ok);
        CHECK(verdict.failure.rfind("rotation: page 1 reads", 0) == 0);
        CHECK(verdict.failure.find("ACCEPTED") == std::string::npos);
    }
}

// ==========================================================================================
// Stage 3 guide issue I-12 -- what fuzz_cos_roundtrip found on its first run, one case per cause
// ==========================================================================================
//
// Each input is built here, a few objects long, rather than kept as the mutated corpus file the fuzzer
// produced: the case then says what it is about. None has a cross-reference table, so each opens
// through `XrefRepair` and saves through D3-6's COMPLETE section -- which is where all 88 findings of
// the first local run were. Each passes through the fuzz body, and asserts the lower-level property
// the body cannot see where there is one.

namespace {

QByteArray pdfOf(std::initializer_list<QByteArray> objects)
{
    QByteArray bytes("%PDF-1.7\n");
    for (const QByteArray& object : objects)
        bytes.append(object);
    return bytes;
}

// A stream object whose /Length is its payload's.
QByteArray streamObject(long long number, const QByteArray& dictionaryBody, const QByteArray& payload)
{
    return QByteArray::number(number) + " 0 obj\n<< " + dictionaryBody + " /Length "
           + QByteArray::number(payload.size()) + " >>\nstream\n" + payload + "\nendstream\nendobj\n";
}

fuzz::RoundtripVerdict fuzzBodyOverBytes(const QByteArray& bytes, bool keepOutput = false)
{
    return fuzz::roundtripFuzzOnce(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                   static_cast<std::size_t>(bytes.size()), keepOutput);
}

} // namespace

TEST_CASE("roundtrip I-12 an object that runs to the end of the file keeps its meaning after the update", "[save][roundtrip][fuzz]")
{
    SECTION("an unterminated page dictionary")
    {
        // Page 2 has no `>>`. Read before the save, it is a page with no /Rotate; appended to, it would
        // read on into the update and take page 1's new `/Rotate 90` as its own.
        const QByteArray input = pdfOf({
            "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
            "2 0 obj\n<< /Type /Pages /Kids [ 3 0 R 4 0 R ] /Count 2 >>\nendobj\n",
            "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] >>\nendobj\n",
            "4 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ]\n",
        });
        const auto verdict = fuzzBodyOverBytes(input);
        INFO("failure: " << verdict.failure);
        REQUIRE(verdict.editable);
        CHECK(verdict.pages == 2);
        CHECK(verdict.complete);
        CHECK(verdict.ok);
        CHECK(verdict.written);
    }
    SECTION("a content stream with no endstream")
    {
        // The body checks pages and rotations, and a stream's DATA is invisible to it -- the direction
        // the writer's own guard cannot see either. So this section reads the data back itself.
        const QByteArray input = pdfOf({
            "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
            "2 0 obj\n<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>\nendobj\n",
            "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] /Contents 4 0 R >>\nendobj\n",
            "4 0 obj\n<< >>\nstream\nBT /F1 12 Tf 72 720 Td (cut off) Tj ET\n",
        });
        auto before = pdf::cos::openWithRepair(viewOf(input));
        REQUIRE(before);
        auto contentBefore = before.value().table.fetch(4);
        REQUIRE(contentBefore);
        REQUIRE(contentBefore.value().object.isStream());
        const auto dataBefore = contentBefore.value().object.streamData();
        const std::string payloadBefore(viewOf(input).substr(dataBefore.dataOffset, dataBefore.dataLength));
        REQUIRE(dataBefore.dataOffset + dataBefore.dataLength == static_cast<std::size_t>(input.size()));   // it does run to the end

        const auto verdict = fuzzBodyOverBytes(input, true);
        INFO("failure: " << verdict.failure);
        REQUIRE(verdict.written);
        auto after = pdf::cos::openWithRepair(viewOf(verdict.output));
        REQUIRE(after);
        auto contentAfter = after.value().table.fetch(4);
        REQUIRE(contentAfter);
        REQUIRE(contentAfter.value().object.isStream());
        const auto dataAfter = contentAfter.value().object.streamData();
        CHECK(std::string(viewOf(verdict.output).substr(dataAfter.dataOffset, dataAfter.dataLength)) == payloadBefore);
        CHECK(dataAfter.dataOffset >= static_cast<std::size_t>(input.size()));   // the copy, not the original
    }
}

TEST_CASE("roundtrip I-12 a number the file references and never defines is not given to a new object", "[save][roundtrip][fuzz]")
{
    // Page 2 is `7 0 R`, and there is no object 7: a reader resolves it to null and skips it. Object 6
    // lives in an object stream, so the complete section must be an xref STREAM -- a new object -- and
    // the allocator's floor (largest defined number + 1) is exactly 7.
    const QByteArray input = pdfOf({
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
        "2 0 obj\n<< /Type /Pages /Kids [ 3 0 R 7 0 R ] /Count 2 >>\nendobj\n",
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] /Resources << /Font << /F1 6 0 R >> >> >>\nendobj\n",
        streamObject(5, "/Type /ObjStm /N 1 /First 4", "6 0 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"),
    });
    auto opened = pdf::cos::openWithRepair(viewOf(input));
    REQUIRE(opened);
    REQUIRE(opened.value().table.find(6) != nullptr);
    REQUIRE(opened.value().table.find(7) == nullptr);

    const auto verdict = fuzzBodyOverBytes(input, true);
    INFO("failure: " << verdict.failure);
    CHECK(verdict.pages == 1);
    REQUIRE(verdict.written);

    auto after = pdf::cos::openWithRepair(viewOf(verdict.output));
    REQUIRE(after);
    CHECK(after.value().table.find(7) == nullptr);           // still nothing there
    REQUIRE(!after.value().table.sections().empty());
    CHECK(after.value().table.sections().front().isStream);  // and the section is the kind that needed a number
}

TEST_CASE("roundtrip I-12 a page that is also an object stream keeps the objects packed inside it", "[save][roundtrip][fuzz]")
{
    // The walk calls any dictionary without /Kids a page, and a stream is a dictionary: object 3 is page 1
    // AND the container holding the page tree. Rewritten as a plain dictionary, the tree goes with it.
    const QByteArray input = pdfOf({
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
        streamObject(3, "/Type /ObjStm /N 1 /First 4", "2 0 << /Type /Pages /Kids [ 3 0 R ] /Count 1 >>"),
    });
    auto opened = pdf::cos::openWithRepair(viewOf(input));
    REQUIRE(opened);
    const auto* packed = opened.value().table.find(2);
    REQUIRE(packed != nullptr);
    REQUIRE(packed->type == pdf::cos::XrefEntryType::InObjectStream);

    const auto verdict = fuzzBodyOverBytes(input);
    INFO("failure: " << verdict.failure);
    REQUIRE(verdict.editable);
    CHECK(verdict.pages == 1);
    CHECK(verdict.ok);
    CHECK(verdict.written);
}

TEST_CASE("roundtrip I-12 an object numbered 0 is never recovered", "[save][roundtrip][fuzz]")
{
    // Object 0 heads the free list in every PDF, so no section can place it -- and a repaired table that
    // did would save as a file whose `/Root 0 0 R` needs repair again. The same file numbered 9 is the
    // control: the catalog is found and the document is editable.
    const QByteArray pages = "2 0 obj\n<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>\nendobj\n";
    const QByteArray page = "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] >>\nendobj\n";
    const QByteArray zero = pdfOf({"0 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", pages, page});
    const QByteArray nine = pdfOf({"9 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n", pages, page});

    auto openedZero = pdf::cos::openWithRepair(viewOf(zero));
    REQUIRE(openedZero);
    const auto* entry = openedZero.value().table.find(0);
    CHECK((entry == nullptr || entry->type == pdf::cos::XrefEntryType::Free));
    const auto verdictZero = fuzzBodyOverBytes(zero);
    INFO("failure: " << verdictZero.failure);
    CHECK(verdictZero.ok);
    CHECK_FALSE(verdictZero.editable);

    const auto verdictNine = fuzzBodyOverBytes(nine);
    INFO("control failure: " << verdictNine.failure);
    CHECK(verdictNine.written);
}

// ==========================================================================================
// Stage 3 guide issue I-17 -- what `fuzz_cos_roundtrip`'s soak found on 2026-09-22 (run
// 35705173922), reproduced through the product's own two saves rather than through the bytes
// libFuzzer happened to mutate its way to
// ==========================================================================================

TEST_CASE("roundtrip I-17 a second save of a hybrid file keeps what the first save wrote", "[save][roundtrip][fuzz]")
{
    // I-14 has an update's trailer name the hybrid file's `/XRefStm`, for Acrobat. Naming it there
    // also MOVES it: a reader takes a section's table, then the `/XRefStm` that section names, then
    // `/Prev` (ISO 32000-1 7.5.8.4), so from the SECOND update the stream is read in front of the
    // first update's table and takes back every number both define. Page 1 is one: packed in the
    // object stream to begin with, rewritten in place by the first save.
    const QByteArray base = tests::readAllBytes(tests::corpusFile("ordinary/hybrid-xrefstm.pdf"));
    REQUIRE_FALSE(base.isEmpty());

    // Save 1 -- the fuzz body's own rotation of page 1, and it passed before this issue as it does now.
    const auto first = fuzzBodyOverBytes(base, true);
    INFO("first failure: " << first.failure);
    REQUIRE(first.written);
    REQUIRE(first.pages == 2);

    // Save 2, changing a DIFFERENT object: page 2, so page 1 is in no delta and survives on the
    // strength of the new section alone. `roundtripFuzzOnce` only ever rotates page 1, so this half
    // is written out here -- which is also why its replay over the corpus never found this.
    auto opened = pdf::cos::openWithRepair(viewOf(first.output));
    REQUIRE(opened);
    auto sink = pdf::ops::MutationSinkImpl::open(viewOf(first.output));
    REQUIRE(sink);
    REQUIRE(sink.value()->pageCount() == 2);
    const int firstPageRotation = sink.value()->pages().pages.front().effectiveRotate();
    CHECK(firstPageRotation == 90);
    REQUIRE(sink.value()->setPageRotation(1, 180));

    auto composed = pdf::ops::RevisionWriter::compose(*sink.value(),
                                                      opened.value().report.requiresSaveAs());
    REQUIRE(composed);
    const QByteArray twice = composed.value().bytes;

    // I-17's REGRESSION: page 1 reads 0 without the writer's re-listed row -- the first save, reverted,
    // with every one of its bytes still in the file.
    auto reopened = reopen(twice);
    REQUIRE(reopened);
    REQUIRE(reopened.value().walk.pages.size() == 2);
    CHECK(reopened.value().walk.pages.front().effectiveRotate() == 90);
    CHECK(reopened.value().walk.pages.back().effectiveRotate() == 180);

    // AND THE PRODUCT'S OWN GUARD AGREED, in the direction that matters here: before the fix it
    // REFUSED this save (`rotation:1`), so the defect reached a user as a second save that fails, not
    // as a document that silently loses a page's rotation. Both are findings; the guard is why it was
    // the first one.
    const core::Status guarded = pdf::ops::RevisionWriter::verify(viewOf(first.output), viewOf(twice),
                                                                  *sink.value());
    INFO("guard: " << (guarded ? std::string() : guarded.error().context.toStdString()));
    CHECK(guarded);
}

// ==========================================================================================
// Stage 5 guide issue I-23 -- what `fuzz_cos_roundtrip`'s soak found on 2026-10-03 (run
// 37111952480): a damaged object stream parsed in time quadratic in its payload
// ==========================================================================================

TEST_CASE("roundtrip I-23 a damaged object stream is read in time linear in its payload", "[save][roundtrip][fuzz][run-serial]")
{
    // The soak's input was a mutated 900-page file whose object stream decoded to 149 KB of damage:
    // every packed object read on to the end of the payload, 300 ms a parse in `x64-release` and
    // fourteen parses through the fuzz body -- past libFuzzer's 30 s under ASan. Built here instead,
    // for the reason the I-12 cases give: the stream holds only unterminated strings, one per byte,
    // and the file has no cross-reference table, so every open repairs it and expands the stream.
    // Measured in `x64-release` on 2026-10-04: 0.076 s bounded, 3.94 s with the bound planted away.
    // At 12,000 objects the unbounded parse took 0.275 s and passed this clock, which is why it is
    // 48,000.
    //
    // WHAT THIS DOES NOT COVER (convention 19): the bound is a wall clock, and a machine slow enough
    // could fail it with the fix in place; `cos parser reads each packed object only up to the next
    // one` asserts the property itself, as spans, which no clock can make flaky.
    constexpr int kObjects = 48000;
    QByteArray table;
    for (int index = 0; index < kObjects; ++index)
        table += QByteArray::number(10 + index) + " " + QByteArray::number(index) + " ";
    const QByteArray payload = table + QByteArray(kObjects, '(');
    const QByteArray input = pdfOf({
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
        "2 0 obj\n<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>\nendobj\n",
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [ 0 0 612 792 ] >>\nendobj\n",
        streamObject(4, "/Type /ObjStm /N " + QByteArray::number(kObjects) + " /First "
                            + QByteArray::number(table.size()), payload),
    });

    const auto started = std::chrono::steady_clock::now();
    const auto verdict = fuzzBodyOverBytes(input);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    INFO("failure: " << verdict.failure << " -- " << elapsed << " s");
    REQUIRE(verdict.editable);
    CHECK(verdict.pages == 1);
    CHECK(verdict.ok);
    CHECK(verdict.written);
    CHECK(elapsed < 2.0);
}
