// tst_fullwriter -- Stage 7 T6 (`Stage7-Assembly-Plan.md` §6, D7-25, D7-29): the headless half of SAVE AS THAT REWRITES.
// Guide cases A-122 (its structure half), A-123, A-124 (its model half), A-125's coordinator half and A-126's policy
// half (`Stage7-Assembly-Testing-Guide.md` §5.8, the bars of §3.9).
//
// THE ORACLE IS THE FILE BEFORE THE REWRITE, READ BY A WALK OF THIS FILE'S OWN: what the composed file reaches from its
// trailer is found here by following references (`reachedIn`), never by `pdf/cos/FullWriter::reachable`, which is the
// code under test -- and each object the rewrite holds is compared, value for value, with the composed file's object
// at the same number.
//
// WHAT IT DOES NOT COVER (convention 19):
//   * PIXELS. Every page drawn at 72 and 144 dpi against the document before the save, and each undo after a Save As
//     drawn as before it, are `tests/render/tst_fullwriterengine.cpp`'s (A-122's raster bar, A-124's);
//   * `qpdf --check` over what these cases write -- the `portable-asan` job's `check-qpdf.py` step reads
//     `written/rewrite-NN.pdf` (A-122);
//   * an ENCRYPTED document, which this backend does not open: its byte copy is the shipped backend's (A-126, render);
//   * OpenSSL's verdict on the signed files kept -- `render.a4_product.saveas.openssl` (A-126);
//   * the question itself as a person meets it, and Save a Copy's tab -- `ui.save_a_copy` (A-125).

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "TinyPdf.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Annotation.h"
#include "core/doc/AnnotationGeometry.h"
#include "core/doc/SaveCoordinator.h"
#include "pdf/cos/FullWriter.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/PageOps.h"
#include "pdf/ops/RevisionWriter.h"
#include "pdf/ops/SignatureFields.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
namespace fs = std::filesystem;

namespace {

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size())) + " "
           + error.context.toStdString();
}

std::vector<std::string> editableCandidates()
{
    std::vector<std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") continue;
        const std::string relative = fs::relative(entry.path(), RAWPAGE_CORPUS_DIR).generic_string();
        if (relative.rfind("encrypted/", 0) == 0) continue;
        files.push_back(relative);
    }
    std::sort(files.begin(), files.end());
    return files;
}

core::Annotation highlightOn(tests::SaveSession& session, int page)
{
    const core::Page* entry = session.backend.pages(session.handle)->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfRect box = entry->cropBox();
    const double left = box.left() + box.size.width * 0.1;
    const double right = left + box.size.width * 0.3;
    const double top = box.bottom() + box.size.height * 0.6;
    core::Annotation a = core::newAnnotation(core::AnnotationSubtype::Highlight, page, QStringLiteral("A-122"), 1791504000);
    a.quadPoints = {left, top, right, top, left, top - 14.0, right, top - 14.0};
    a.rect = *core::boundsOfPoints(a.quadPoints);
    a.color = {1.0, 0.8, 0.0};
    a.contents = QStringLiteral("rewritten");
    return a;
}

// A file read on its own: its table, and what its trailer reaches by a walk of THIS file's (see the header).
struct Read {
    QByteArray bytes;
    std::optional<pdf::cos::OpenedFile> opened;

    explicit Read(QByteArray file) : bytes(std::move(file))
    {
        auto o = pdf::cos::openWithRepair(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
        REQUIRE(o);
        opened.emplace(std::move(o).value());
    }
    pdf::cos::XrefTable& table() { return opened->table; }

    std::set<long long> inUse()
    {
        std::set<long long> out;
        for (const auto& [number, entry] : table().entries())
            if (number > 0 && entry.type != pdf::cos::XrefEntryType::Free) out.insert(number);
        return out;
    }

    std::set<long long> reachedIn()
    {
        std::set<long long> reached;
        std::vector<long long> pending;
        for (const char* key : {"Root", "Info"}) {
            const pdf::cos::CosObject* named = table().trailer().find(key);
            if (named != nullptr && named->isReference()) pending.push_back(named->reference().number);
        }
        while (!pending.empty()) {
            const long long number = pending.back();
            pending.pop_back();
            const pdf::cos::XrefEntry* entry = table().find(number);
            if (entry == nullptr || entry->type == pdf::cos::XrefEntryType::Free || reached.count(number) != 0) continue;
            auto fetched = table().fetch(number);
            if (!fetched) continue;
            reached.insert(number);
            std::vector<const pdf::cos::CosObject*> values{&fetched.value().object};
            while (!values.empty()) {
                const pdf::cos::CosObject* value = values.back();
                values.pop_back();
                if (value->isReference()) pending.push_back(value->reference().number);
                for (const pdf::cos::CosObject& item : value->items()) values.push_back(&item);
                for (const pdf::cos::CosObject::Entry& e : value->entries()) {
                    if (value->isStream() && e.key == "Length") continue;   // written direct by both writers
                    values.push_back(&e.value);
                }
            }
        }
        return reached;
    }

    pdf::cos::CosObject object(long long number)
    {
        auto fetched = table().fetch(number);
        REQUIRE(fetched);
        pdf::cos::CosObject value = fetched.value().object;
        value.clearSpansRecursively();
        if (value.isStream()) {   // the dictionary but /Length, and the encoded bytes as a key of its own
            std::vector<pdf::cos::CosObject::Entry> entries;
            for (const auto& e : value.entries())
                if (e.key != "Length") entries.push_back(e);
            entries.push_back({"#data", pdf::cos::CosObject::makeString(std::string(fetched.value().object.rawStreamData(table().source())))});
            return pdf::cos::CosObject::makeDictionary(std::move(entries));
        }
        return value;
    }

    long long generation(long long number)
    {
        const pdf::cos::XrefEntry* entry = table().find(number);
        REQUIRE(entry != nullptr);
        return entry->type == pdf::cos::XrefEntryType::InFile ? entry->field3 : 0;
    }
};

QByteArray composedNow(tests::SaveSession& session)
{
    const bool complete = session.backend.repairReport(session.handle)->requiresSaveAs();
    auto composed = pdf::ops::RevisionWriter::compose(*session.sink(), complete);
    REQUIRE(composed);
    return composed.value().bytes;
}

// Kept for `check-qpdf.py` in the portable-asan job (A-122), beside the earlier stages' outputs.
void keep(const QString& path, const std::string& name)
{
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    QDir().mkpath(dir);
    const QString target = QDir(dir).filePath(QString::fromStdString(name));
    QFile::remove(target);
    QFile::copy(path, target);
}

} // namespace

// ==========================================================================================
// A-122 -- one revision of what is reachable, and nothing lost (§3.9)
// ==========================================================================================

TEST_CASE("fullwriter every editable corpus file Saved As after a move and a highlight is one revision of what it reaches",
          "[fullwriter][A-122]")
{
    int files = 0;
    int signedKept = 0;
    int objects = 0;
    int dropped = 0;
    std::vector<std::string> notEditable;
    std::vector<std::string> failures;
    for (const std::string& relative : editableCandidates()) {
        auto session = tests::openCopy(relative);
        if (!session) {
            notEditable.push_back(relative);
            continue;
        }
        INFO(relative);
        const int pages = session->journal->document().pages().count();
        if (pages >= 2) {
            if (core::Status moved = session->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, pages - 1));
                !moved) {
                failures.push_back(relative + ": move refused " + keyOf(moved.error()));
                continue;
            }
        }
        if (session->backend.permissions(session->handle)->mayAnnotate(0)) {
            if (core::Status added = session->journal->run(std::make_unique<core::AddAnnotationCommand>(
                    std::vector<core::Annotation>{highlightOn(*session, 0)}));
                !added) {
                failures.push_back(relative + ": highlight refused " + keyOf(added.error()));
                continue;
            }
        }
        Read before(composedNow(*session));
        const std::set<long long> beforeReached = before.reachedIn();
        const std::set<long long> beforeInUse = before.inUse();
        const bool isSigned = session->backend.signatureCount(session->handle) > 0;

        const QString target = session->directory.filePath(QStringLiteral("rewritten.pdf"));
        auto saved = session->coordinator->saveAs(*session->journal, target);
        if (!saved || saved.value().kind != core::SaveOutcome::Kind::Saved) {
            failures.push_back(relative + ": Save As refused " + (saved ? std::string("(Save As required)") : keyOf(saved.error())));
            continue;
        }
        ++files;
        if (isSigned) {
            // No one to ask: the signatures are kept, which is an append (convention 16; A-126 has the rest).
            ++signedKept;
            if (saved.value().form != core::SaveForm::Append) failures.push_back(relative + ": a signed file was rewritten with nobody asked");
            continue;
        }
        if (saved.value().form != core::SaveForm::Rewrite) {
            failures.push_back(relative + ": Save As did not rewrite");
            continue;
        }
        Read after(tests::readAllBytes(target));
        if (!after.opened->usedOriginalXref || after.opened->report.requiresSaveAs()) failures.push_back(relative + ": the rewrite needs a repair");
        if (after.table().sections().size() != 1 || after.table().trailer().has("Prev")) failures.push_back(relative + ": not one revision");
        const std::set<long long> held = after.inUse();
        if (after.reachedIn() != held) failures.push_back(relative + ": the rewrite holds an object its trailer does not reach");
        if (beforeReached != held) failures.push_back(relative + ": the rewrite does not hold exactly what the composed file reached");
        for (const long long number : held) {
            if (after.generation(number) != before.generation(number) || !(after.object(number) == before.object(number))) {
                failures.push_back(relative + ": object " + std::to_string(number) + " differs from the one it was written from");
                break;
            }
        }
        // Every dropped number is a FREE row at the generation the object had (D7-29), and `/Size` covers them.
        for (const long long number : beforeInUse) {
            if (held.count(number) != 0) continue;
            ++dropped;
            const pdf::cos::XrefEntry* row = after.table().find(number);
            if (row == nullptr || row->type != pdf::cos::XrefEntryType::Free || row->field3 != before.generation(number)) {
                failures.push_back(relative + ": dropped object " + std::to_string(number) + " has no free row at its generation");
                break;
            }
        }
        if (after.table().declaredSize() <= *beforeInUse.rbegin()) failures.push_back(relative + ": /Size below a dropped number");
        // The pages are the ones the sink put there, by number.
        auto walk = pdf::ops::PageOps::walk(after.table());
        REQUIRE(walk);
        if (static_cast<int>(walk.value().pages.size()) != pages) failures.push_back(relative + ": page count");
        objects += static_cast<int>(held.size());
        keep(target, "rewrite-" + std::to_string(files) + ".pdf");
    }
    std::cout << "fullwriter: " << files << " files Saved As -- " << files - signedKept << " rewritten, " << objects
              << " objects kept, " << dropped << " dropped; " << signedKept << " signed, kept by appending; "
              << failures.size() << " failures; not editable:";
    for (const std::string& n : notEditable) std::cout << " " << n;
    std::cout << "\n";
    for (const std::string& f : failures) UNSCOPED_INFO(f);
    CHECK(failures.empty());
    CHECK(files > 100);
    CHECK(signedKept == 4);
}

// ==========================================================================================
// A-123 -- what is unreachable goes, and the file shrinks
// ==========================================================================================

TEST_CASE("fullwriter.compaction deleting 800 of 900 pages and Saving As writes a file smaller than the save and the original",
          "[fullwriter][fullwriter.compaction][A-123]")
{
    auto session = tests::openCopy("ordinary/pages-900.pdf");
    REQUIRE(session);
    const qint64 originalSize = QFileInfo(session->path).size();
    std::vector<int> gone;
    for (int page = 100; page < 900; ++page) gone.push_back(page);
    REQUIRE(session->journal->run(std::make_unique<core::DeletePagesCommand>(gone)));

    auto appended = session->coordinator->save(*session->journal, session->path);
    REQUIRE(appended);
    REQUIRE(appended.value().form == core::SaveForm::Append);
    const qint64 appendedSize = QFileInfo(session->path).size();
    Read incremental(tests::readAllBytes(session->path));
    const std::size_t unreachableInSave = incremental.inUse().size() - incremental.reachedIn().size();

    const QString target = session->directory.filePath(QStringLiteral("compacted.pdf"));
    auto rewritten = session->coordinator->saveAs(*session->journal, target);
    REQUIRE(rewritten);
    REQUIRE(rewritten.value().form == core::SaveForm::Rewrite);
    const qint64 rewrittenSize = QFileInfo(target).size();
    Read after(tests::readAllBytes(target));
    const std::size_t unreachable = after.inUse().size() - after.reachedIn().size();

    std::cout << "fullwriter.compaction: pages-900.pdf, 800 pages deleted -- original " << originalSize << " bytes, save "
              << appendedSize << " (" << unreachableInSave << " objects unreachable), Save As " << rewrittenSize << " ("
              << unreachable << " unreachable, " << after.inUse().size() << " objects)\n";
    CHECK(unreachableInSave > 0);   // the control: the incremental save keeps what was deleted
    CHECK(unreachable == 0);
    CHECK(rewrittenSize < appendedSize);
    CHECK(rewrittenSize < originalSize);
    CHECK(session->journal->document().pages().count() == 100);
    keep(target, "rewrite-compacted.pdf");
}

// ==========================================================================================
// A-124 -- numbers kept: undo and redo after a Save As that rewrote (D7-29)
// ==========================================================================================

TEST_CASE("fullwriter.identity numbers survive a rewrite and every undo after it brings back what it dropped",
          "[fullwriter][fullwriter.identity][A-124]")
{
    auto session = tests::openCopy("assembly/refs-every-kind.pdf");
    REQUIRE(session);
    core::CommandJournal& journal = *session->journal;
    const int pages = journal.document().pages().count();
    REQUIRE(pages >= 4);
    Read original(session->openedBytes);
    std::vector<long long> originalPages;
    {
        auto walk = pdf::ops::PageOps::walk(original.table());
        REQUIRE(walk);
        for (const auto& p : walk.value().pages) originalPages.push_back(p.number);
    }

    // The script, and the model's hash after each step -- the state each undo after the Save As must reach again.
    std::vector<QByteArray> hashes{journal.document().modelHash()};
    const core::Annotation highlight = highlightOn(*session, 1);
    REQUIRE(journal.run(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{highlight})));
    hashes.push_back(journal.document().modelHash());
    REQUIRE(journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, pages - 1)));
    hashes.push_back(journal.document().modelHash());
    REQUIRE(journal.run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{0, 1})));   // the highlighted page among them
    hashes.push_back(journal.document().modelHash());
    const std::size_t steps = hashes.size() - 1;

    Read before(composedNow(*session));
    const QString target = session->directory.filePath(QStringLiteral("rewritten.pdf"));
    auto saved = session->coordinator->saveAs(*session->journal, target);
    REQUIRE(saved);
    REQUIRE(saved.value().form == core::SaveForm::Rewrite);
    Read after(tests::readAllBytes(target));
    // Every surviving object at its own number and generation; the deleted pages gone from the file.
    for (const long long number : after.inUse()) {
        INFO("object " << number);
        CHECK(after.generation(number) == before.generation(number));
        CHECK(after.object(number) == before.object(number));
    }
    CHECK(after.inUse().count(originalPages[1]) == 0);
    CHECK(session->sink()->retiredRevisions() == 1);

    // Every undo reaches the state its step had, and composes -- what a rewrite dropped written back.
    std::size_t revived = 0;
    for (std::size_t step = steps; step > 0; --step) {
        INFO("undo to step " << step - 1);
        REQUIRE(journal.undo());
        REQUIRE(journal.settle());
        CHECK(journal.document().modelHash() == hashes[step - 1]);
        composedNow(*session);   // composes and verifies
        revived = std::max(revived, session->sink()->revivedLastCompose());
    }
    CHECK(revived > 0);
    // Saved now -- appending to the rewrite -- the file is the original document: its pages the original objects in
    // the original order, each page's dictionary but /Parent as the original's.
    auto back = session->coordinator->save(*session->journal, target);
    REQUIRE(back);
    REQUIRE(back.value().form == core::SaveForm::Append);
    Read restored(tests::readAllBytes(target));
    auto walk = pdf::ops::PageOps::walk(restored.table());
    REQUIRE(walk);
    std::vector<long long> restoredPages;
    for (const auto& p : walk.value().pages) restoredPages.push_back(p.number);
    CHECK(restoredPages == originalPages);
    for (const long long page : originalPages) {
        INFO("page object " << page);
        auto strip = [](pdf::cos::CosObject value) {
            std::vector<pdf::cos::CosObject::Entry> kept;
            for (const auto& e : value.entries())
                if (e.key != "Parent") kept.push_back(e);
            return pdf::cos::CosObject::makeDictionary(std::move(kept));
        };
        CHECK(strip(restored.object(page)) == strip(original.object(page)));
        // ... and every annotation it lists, value for value.
        const pdf::cos::CosObject annots = restored.object(page).get("Annots");
        for (const pdf::cos::CosObject& entry : annots.items()) {
            if (!entry.isReference()) continue;
            INFO("annotation " << entry.reference().number);
            CHECK(restored.object(entry.reference().number) == original.object(entry.reference().number));
        }
    }
    // And every redo after it.
    for (std::size_t step = 1; step <= steps; ++step) {
        INFO("redo to step " << step);
        REQUIRE(journal.redo());
        REQUIRE(journal.settle());
        CHECK(journal.document().modelHash() == hashes[step]);
        composedNow(*session);
    }
    // `documents/saveMode = full`: Save rewrites as Save As does.
    core::SaveOptions full;
    full.rewriteOnSave = true;
    auto again = session->coordinator->save(*session->journal, target, full);
    REQUIRE(again);
    CHECK(again.value().form == core::SaveForm::Rewrite);
    Read last(tests::readAllBytes(target));
    CHECK(last.table().sections().size() == 1);
    CHECK(last.reachedIn() == last.inUse());
    CHECK(session->sink()->retiredRevisions() == 2);
    keep(target, "rewrite-identity.pdf");
}

// ==========================================================================================
// A-126's policy half -- a signed document asks, and nobody to ask keeps its signatures
// ==========================================================================================

TEST_CASE("fullwriter.signed a signed document is rewritten only when the question says so",
          "[fullwriter][fullwriter.signed][A-126]")
{
    for (const char* relative : {"signed/approval.pdf", "signed/approval-xrefstream.pdf", "signed/certified-nochanges.pdf",
                                 "signed/approval-then-update.pdf"}) {
        INFO(relative);
        auto session = tests::openCopy(relative);
        REQUIRE(session);
        const int signatures = session->backend.signatureCount(session->handle);
        CHECK(signatures >= 1);
        REQUIRE(session->journal->run(std::make_unique<core::AddAnnotationCommand>(
            std::vector<core::Annotation>{highlightOn(*session, 0)})));

        int asked = -1;
        auto answer = [&asked](core::SignedRewriteAnswer a) {
            return [&asked, a](int count) {
                asked = count;
                return a;
            };
        };
        // Cancel: nothing written, nothing adopted.
        core::SaveOptions options;
        options.askSigned = answer(core::SignedRewriteAnswer::Cancel);
        const QString cancelled = session->directory.filePath(QStringLiteral("cancelled.pdf"));
        auto refused = session->coordinator->saveACopy(*session->journal, cancelled, options);
        REQUIRE_FALSE(refused);
        CHECK(QLatin1String(refused.error().messageKey) == QLatin1String("error.save.cancelled"));
        CHECK(asked == signatures);
        CHECK_FALSE(QFile::exists(cancelled));
        CHECK(session->strayTemporaries().isEmpty());

        // Keep: appended -- the file's bytes a prefix of the copy.
        options.askSigned = answer(core::SignedRewriteAnswer::KeepSignatures);
        const QString kept = session->directory.filePath(QStringLiteral("kept.pdf"));
        auto keptCopy = session->coordinator->saveACopy(*session->journal, kept, options);
        REQUIRE(keptCopy);
        CHECK(keptCopy.value().form == core::SaveForm::Append);
        CHECK(tests::readAllBytes(kept).startsWith(session->openedBytes));

        // Rewrite: one revision.
        options.askSigned = answer(core::SignedRewriteAnswer::Rewrite);
        const QString rewritten = session->directory.filePath(QStringLiteral("rewritten.pdf"));
        auto rewrittenCopy = session->coordinator->saveACopy(*session->journal, rewritten, options);
        REQUIRE(rewrittenCopy);
        CHECK(rewrittenCopy.value().form == core::SaveForm::Rewrite);
        Read written(tests::readAllBytes(rewritten));
        CHECK(written.table().sections().size() == 1);
        CHECK_FALSE(tests::readAllBytes(rewritten).startsWith(session->openedBytes));

        // Nobody to ask: kept (convention 16).
        auto headless = session->coordinator->saveAs(*session->journal, session->directory.filePath(QStringLiteral("headless.pdf")));
        REQUIRE(headless);
        CHECK(headless.value().form == core::SaveForm::Append);
    }
    // The control (convention 22): an unsigned file is never asked.
    auto unsignedSession = tests::openCopy("ordinary/cropbox-margins.pdf");
    REQUIRE(unsignedSession);
    CHECK(unsignedSession->backend.signatureCount(unsignedSession->handle) == 0);
    bool askedUnsigned = false;
    core::SaveOptions options;
    options.askSigned = [&askedUnsigned](int) {
        askedUnsigned = true;
        return core::SignedRewriteAnswer::KeepSignatures;
    };
    auto saved = unsignedSession->coordinator->saveAs(*unsignedSession->journal,
                                                      unsignedSession->directory.filePath(QStringLiteral("out.pdf")), options);
    REQUIRE(saved);
    CHECK(saved.value().form == core::SaveForm::Rewrite);
    CHECK_FALSE(askedUnsigned);
}

// ==========================================================================================
// A-126's repaired half -- a repaired document's forced Save As rewrites, and opens with no repair
// ==========================================================================================

TEST_CASE("fullwriter.repaired a repaired document's forced Save As rewrites into a file that needs no repair",
          "[fullwriter][fullwriter.repaired][A-126]")
{
    int repaired = 0;
    for (const std::string& relative : editableCandidates()) {
        if (relative.rfind("malformed/", 0) != 0) continue;
        auto session = tests::openCopy(relative);
        if (!session || !session->backend.repairReport(session->handle)->requiresSaveAs()) continue;
        INFO(relative);
        ++repaired;
        REQUIRE(session->rotate(0));
        auto save = session->coordinator->save(*session->journal, session->path);
        REQUIRE(save);
        REQUIRE(save.value().kind == core::SaveOutcome::Kind::SaveAsRequired);
        const QString target = session->directory.filePath(QStringLiteral("repaired.pdf"));
        auto saved = session->coordinator->saveAs(*session->journal, target);
        REQUIRE(saved);
        CHECK(saved.value().form == core::SaveForm::Rewrite);
        Read after(tests::readAllBytes(target));
        CHECK(after.opened->usedOriginalXref);
        CHECK_FALSE(after.opened->report.requiresSaveAs());
        CHECK(after.table().sections().size() == 1);
        CHECK(after.reachedIn() == after.inUse());
    }
    std::cout << "fullwriter.repaired: " << repaired << " repaired files rewritten\n";
    CHECK(repaired >= 3);
}

// ==========================================================================================
// A-125's coordinator half -- Save a Copy leaves the document where it was
// ==========================================================================================

TEST_CASE("fullwriter.copy Save a Copy writes the rewrite elsewhere and leaves the document on its own file, unsaved",
          "[fullwriter][fullwriter.copy][A-125]")
{
    auto session = tests::openCopy("ordinary/cropbox-margins.pdf");
    REQUIRE(session);
    REQUIRE(session->rotate(0));
    REQUIRE(session->journal->isDirty());
    const QString own = session->journal->document().id().path;
    const QString copy = session->directory.filePath(QStringLiteral("copy.pdf"));
    auto written = session->coordinator->saveACopy(*session->journal, copy);
    REQUIRE(written);
    CHECK(written.value().form == core::SaveForm::Rewrite);
    CHECK(written.value().path == copy);
    CHECK(session->journal->isDirty());
    CHECK(session->journal->document().id().path == own);
    CHECK(tests::readAllBytes(session->path) == session->openedBytes);   // its own file untouched
    Read read(tests::readAllBytes(copy));
    CHECK(read.table().sections().size() == 1);
    CHECK(read.reachedIn() == read.inUse());
    // A later Save writes its OWN file, and appends: the copy changed nothing it compares against.
    const QByteArray copied = tests::readAllBytes(copy);
    auto saved = session->coordinator->save(*session->journal, session->path);
    REQUIRE(saved);
    CHECK(saved.value().form == core::SaveForm::Append);
    CHECK(tests::readAllBytes(session->path).startsWith(session->openedBytes));
    CHECK(tests::readAllBytes(copy) == copied);
    CHECK_FALSE(session->journal->isDirty());
}

// ==========================================================================================
// The reader of D7-20's signatures (`pdf/ops/SignatureFields`)
// ==========================================================================================

TEST_CASE("fullwriter.signatures a signature field counts only with a value, its type inherited and its kids followed once",
          "[fullwriter][fullwriter.signatures]")
{
    tests::TinyPdf pdf;
    const int catalog = pdf.reserve();
    const int pagesNode = pdf.reserve();
    const int page = pdf.add("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>");
    const int value = pdf.add("<< /Type /Sig /Filter /Adobe.PPKLite >>");
    const int signedField = pdf.add("<< /FT /Sig /T (a) /V " + std::to_string(value) + " 0 R >>");
    const int emptyField = pdf.add("<< /FT /Sig /T (b) >>");
    const int parent = pdf.reserve();
    const int kid = pdf.add("<< /T (c) /Parent " + std::to_string(parent) + " 0 R /V " + std::to_string(value) + " 0 R >>");
    pdf.set(parent, "<< /FT /Sig /T (p) /Kids [" + std::to_string(kid) + " 0 R " + std::to_string(parent) + " 0 R] >>");
    const int text = pdf.add("<< /FT /Tx /T (d) /V (words) >>");
    pdf.set(pagesNode, "<< /Type /Pages /Kids [" + std::to_string(page) + " 0 R] /Count 1 >>");
    pdf.set(catalog, "<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [" + std::to_string(signedField) + " 0 R "
                         + std::to_string(emptyField) + " 0 R " + std::to_string(parent) + " 0 R " + std::to_string(text)
                         + " 0 R] >> >>");
    const std::string bytes = pdf.bytes(catalog);
    auto opened = pdf::cos::openWithRepair(bytes);
    REQUIRE(opened);
    // The signed field and the kid that inherits /Sig: two. The empty one, the text field and the loop: none.
    CHECK(pdf::ops::SignatureFields::count(opened.value().table) == 2);
}
