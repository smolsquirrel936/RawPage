// tst_composememo -- Stage 7 T4 (`Stage7-Assembly-Plan.md` §6, Q4): guide case A-128 (`Stage7-Assembly-Testing-Guide.md`
// §5.6). THE COMPOSE THAT KEEPS WHAT IT COMPOSED WRITES WHAT A COMPOSE FROM NOTHING WRITES.
//
// Since T4 `MutationSinkImpl` writes an object again as the last compose wrote it whenever what it is built from is
// what it was -- a created annotation, an annotation edited in place, a page's listing, a shared appearance object,
// and, when no annotation command has run since, the annotations' whole block (`MutationSinkImpl.h`). A reuse that is
// wrong writes a STALE object, and nothing about a stale object is malformed: the verify checks what a listing names,
// not what an appearance draws. So this case checks the memo against the only reference that cannot share its
// mistake -- the same sink with everything it composed FORGOTTEN (`forgetComposedForTesting`), its book kept, so the
// cold compose builds every object anew at the same numbers. After every command, undo, redo and save of a script
// over every editable corpus file: the update the show composed, a second warm compose, and the cold one are the
// same bytes.
//
// Headless, over `tests/unit/CosBackend.h` -- the shipped compose and verify with no engine -- so it runs under
// `portable-asan` too. WHAT IT DOES NOT COVER (convention 19): the engine, the verify thread and the gate, which are
// the shipped backend's (`tests/render/tst_composegrowth.cpp`, guide A-35 ... A-37); and the copies of T5's inserts,
// which do not exist yet.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "TinyPdf.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Annotation.h"
#include "core/doc/SaveCoordinator.h"
#include "core/doc/TypedText.h"

#include <cmath>
#include <filesystem>
#include <optional>
#include <iostream>
#include <string>
#include <vector>

using namespace rawpage;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> editableCandidates()
{
    std::vector<std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") continue;
        const std::string relative = fs::relative(entry.path(), RAWPAGE_CORPUS_DIR).generic_string();
        if (relative.rfind("encrypted/", 0) == 0 || relative == "malformed/pages-loop.pdf") continue;
        files.push_back(relative);
    }
    std::sort(files.begin(), files.end());
    return files;
}

core::Annotation highlightOn(tests::SaveSession& session, int page, double shift)
{
    const core::Page* entry = session.backend.pages(session.handle)->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfRect box = entry->cropBox();
    const double left = box.left() + box.size.width * 0.1 + shift;
    const double right = left + box.size.width * 0.3;
    const double top = box.bottom() + box.size.height * 0.6;
    core::Annotation a = core::newAnnotation(core::AnnotationSubtype::Highlight, page, QStringLiteral("A-128"), 1791504000);
    a.quadPoints = {left, top, right, top, left, top - 14.0, right, top - 14.0};
    a.rect = *core::boundsOfPoints(a.quadPoints);
    a.color = {0.2, 0.8, 0.2};
    a.contents = QStringLiteral("kept or built anew");
    return a;
}

// A typed comment -- its appearance names a FONT, the shared object a compose writes once (`DeltaObjects`).
core::Annotation typedOn(tests::SaveSession& session, int page)
{
    const core::Page* entry = session.backend.pages(session.handle)->at(page);
    REQUIRE(entry != nullptr);
    const core::PdfRect box = entry->cropBox();
    core::Annotation a = core::newAnnotation(core::AnnotationSubtype::FreeText, page, QStringLiteral("A-128"), 1791504000);
    a.intent = QString(core::freeTextIntent(core::FreeTextKind::Box));
    a.contents = QStringLiteral("kept or built anew");
    a.color = {0.0, 0.0, 1.0};
    a.borderWidth = 1.0;
    a.defaultAppearance = core::freeTextDefaultAppearance(core::TypedFont::Helvetica, 12.0, a.color);
    const double inset = *a.borderWidth + core::kFreeTextPadding;
    a.rect = core::freeTextBoxAt(core::PdfPoint{box.left() + 30.0, box.bottom() + box.size.height * 0.4}, 180.0,
                                 12 * 1.2 + 2 * inset, 0);
    core::Result<core::Annotation> fitted = core::fittedFreeText(a, core::TypedFont::Simplified);
    REQUIRE(fitted);
    return std::move(fitted).value();
}

struct Sweep {
    int files = 0;
    int checks = 0;
    std::vector<std::string> failures;
};

// The three composes after one operation, compared. `label` names the file and the step.
void check(tests::SaveSession& session, const std::string& label, Sweep& sweep)
{
    ++sweep.checks;
    pdf::ops::MutationSinkImpl* sink = session.sink();
    // As the backend composes it: D3-6's complete section for a document whose opening needed a repair.
    const bool complete = session.journal->document().repairReport().requiresSaveAs();
    const QByteArray shown = session.backend.shownUpdate(session.handle);
    auto warm = pdf::ops::RevisionWriter::composeUpdate(*sink, complete);
    sink->forgetComposedForTesting();
    auto cold = pdf::ops::RevisionWriter::composeUpdate(*sink, complete);
    if (!warm || !cold) {
        sweep.failures.push_back(label + ": a compose failed");
        return;
    }
    if (tests::sha256(warm.value().update) != tests::sha256(cold.value().update))
        sweep.failures.push_back(label + ": the warm compose is not the cold one");
    else if (tests::sha256(shown) != tests::sha256(warm.value().update))
        sweep.failures.push_back(label + ": what was shown is not what a compose now writes");
}

void runScript(const std::string& relative, Sweep& sweep)
{
    auto session = tests::openCopy(relative);
    if (!session || session->sink() == nullptr) return;
    ++sweep.files;
    tests::SaveSession* self = session.get();
    session->journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); },
                                     [self] { return self->backend.settleEdits(self->handle); });
    core::CommandJournal& journal = *session->journal;
    const auto pages = [&] { return journal.document().pages().count(); };
    int steps = 0;
    // A step the journal refuses is not compared -- but a refusal by the SHOW is the compose writing something its
    // verify rejects, which is exactly what a stale reuse makes (the plants of a missed epoch and an uncompared
    // listing were each caught that way, by the verify, before this case counted it), and an undo or a redo this
    // script asks for is never refused.
    const auto step = [&](const std::string& what, const core::Status& ran) {
        if (!ran) {
            if (std::string(ran.error().messageKey.data()) == "error.edit.notShown")
                sweep.failures.push_back(relative + ": " + what + " was refused by its verify (" + ran.error().context.toStdString() + ")");
            return;
        }
        ++steps;
        check(*session, relative + " after " + what, sweep);
    };
    const auto stepBack = [&](const std::string& what, bool ran) {
        if (!ran) {
            sweep.failures.push_back(relative + ": " + what + " was refused");
            return;
        }
        ++steps;
        check(*session, relative + " after " + what, sweep);
    };

    const core::Annotation first = highlightOn(*session, 0, 0.0);
    step("a highlight", (journal.run(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{first}))));
    step("a second", (journal.run(std::make_unique<core::AddAnnotationCommand>(
                         std::vector<core::Annotation>{highlightOn(*session, pages() - 1, 9.0)}))));
    step("a typed comment", (journal.run(std::make_unique<core::AddAnnotationCommand>(
                                std::vector<core::Annotation>{typedOn(*session, 0)}))));
    step("the first moved", (journal.run(std::make_unique<core::MoveAnnotationCommand>(
                                std::vector<core::AnnotationId>{first.id}, 6.0, -4.0, 1791504100))));
    // An annotation the FILE holds, moved in place -- an edit unit.
    const core::AnnotationStore& store = journal.document().annotations();
    for (std::size_t at = 0; at < store.size(); ++at) {
        if (store.at(at).id.kind != core::AnnotationId::Kind::Object) continue;
        const core::AnnotationId fileOwn = store.at(at).id;
        step("one of the file's moved", (journal.run(std::make_unique<core::MoveAnnotationCommand>(
                                            std::vector<core::AnnotationId>{fileOwn}, 3.0, 3.0, 1791504200))));
        // ...and AGAIN: its edit changes while its unit is still held -- the reuse must see the new edit.
        step("it moved again", (journal.run(std::make_unique<core::MoveAnnotationCommand>(
                                   std::vector<core::AnnotationId>{fileOwn}, 2.0, -2.0, 1791504250))));
        break;
    }
    step("page 1 rotated", (session->rotate(0)));
    if (pages() >= 2) {
        step("page 1 moved to the end", (journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, pages() - 1))));
        step("a rotation after the move", (session->rotate(pages() - 1)));
        step("the last page duplicated", (journal.run(std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{pages() - 1}))));
    }
    if (pages() >= 3)
        step("page 2 deleted", (journal.run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{1}))));
    step("the first deleted", (journal.run(std::make_unique<core::DeleteAnnotationCommand>(std::vector<core::AnnotationId>{first.id}))));

    const int undoable = steps;
    for (int i = 0; i < undoable && journal.canUndo(); ++i) stepBack("an undo", journal.undo());
    for (int i = 0; i < 3 && journal.canRedo(); ++i) stepBack("a redo", journal.redo());

    // AFTER A SAVE the book starts again above what was written, and the units with it.
    if (journal.isDirty() && !journal.document().repairReport().requiresSaveAs()) {
        auto saved = session->coordinator->save(journal, session->path);
        if (saved && saved.value().kind == core::SaveOutcome::Kind::Saved) {
            step("a highlight after the save", (journal.run(std::make_unique<core::AddAnnotationCommand>(
                                                    std::vector<core::Annotation>{highlightOn(*session, 0, 20.0)}))));
            stepBack("an undo after the save", journal.undo());
            if (journal.canUndo()) stepBack("an undo past the save", journal.undo());
        }
    }
}

} // namespace

TEST_CASE("the compose that keeps what it composed writes what a compose from nothing writes", "[compose][A-128]")
{
    Sweep sweep;
    for (const std::string& relative : editableCandidates()) runScript(relative, sweep);
    std::cout << "compose memo: " << sweep.checks << " operations over " << sweep.files << " editable corpus files; "
              << sweep.failures.size() << " differ\n";
    for (const std::string& failure : sweep.failures) UNSCOPED_INFO(failure);
    CHECK(sweep.failures.empty());
    // Not a quietly short run: every ordinary file is editable, and each takes the whole script.
    CHECK(sweep.files >= 100);
    CHECK(sweep.checks >= sweep.files * 10);
}

// GUIDE A-36's HEADLESS HALF: a composition its verify refuses, through a real sink -- the verify DEFERRED to the
// settle, which is the shipped backend's order with no thread (`CosBackend::deferVerifies`). The run returns, the
// settle refuses it by key, and the journal takes it back: the model, the sink and what was shown as they were.
TEST_CASE("a composition its verify refuses after the run returns is taken back through the sink", "[compose][A-36]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    tests::SaveSession* self = session.get();
    session->backend.deferVerifies(true);
    session->journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); },
                                     [self] { return self->backend.settleEdits(self->handle); });
    const QByteArray hash = session->journal->document().modelHash();

    // Stage 6 guide A-30's fault: the update's `/Annots` renamed -- planted for the compose, cleared before the settle.
    const std::size_t original = static_cast<std::size_t>(session->openedBytes.size());
    pdf::ops::RevisionWriter::setComposeFaultForTesting([original](QByteArray& bytes) {
        const qsizetype at = bytes.indexOf("/Annots", static_cast<qsizetype>(original));
        if (at >= 0) bytes[at + 1] = 'X';
    });
    const core::Status ran = session->journal->run(std::make_unique<core::AddAnnotationCommand>(
        std::vector<core::Annotation>{highlightOn(*session, 0, 0.0)}));
    pdf::ops::RevisionWriter::setComposeFaultForTesting({});
    REQUIRE(ran);
    CHECK(session->backend.verifyPending(session->handle));
    CHECK(session->journal->isUnsettled());

    const core::Status settled = session->journal->settle();
    REQUIRE_FALSE(settled);
    CHECK(std::string(settled.error().messageKey.data()) == "error.edit.notShown");
    CHECK(settled.error().context.startsWith(QStringLiteral("error.save.verifyFailed")));
    CHECK(session->journal->document().modelHash() == hash);
    CHECK_FALSE(session->journal->canUndo());
    CHECK_FALSE(session->journal->isDirty());
    CHECK(session->backend.shownUpdate(session->handle).isEmpty());   // nothing was ever shown
    auto delta = session->sink()->delta();
    REQUIRE(delta);
    CHECK(delta.value().isEmpty());

    // THE CONTROL: the same edit with no fault is made, settled, and shown.
    REQUIRE(session->journal->run(std::make_unique<core::AddAnnotationCommand>(
        std::vector<core::Annotation>{highlightOn(*session, 0, 0.0)})));
    REQUIRE(session->journal->settle());
    CHECK(session->journal->canUndo());
    CHECK_FALSE(session->backend.shownUpdate(session->handle).isEmpty());
}

// GUIDE I-03's REGRESSION (S7 T4, found by A-128): a link the file holds, MOVED, and then the page it points at DELETED.
// The repair clears the link's action and the edit moves it -- the same object -- and the compose wrote it twice, so
// the delete was refused by `error.pdf.writeDuplicateObject`. Now the edit is made on the repaired dictionary: the
// delete is made, and the saved link is where it was moved to, pointing at nothing.
TEST_CASE("a moved link whose target page is deleted is written once", "[compose][I-03]")
{
    auto session = tests::openCopy("assembly/merge-a.pdf");
    REQUIRE(session);
    tests::SaveSession* self = session.get();
    session->journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); },
                                     [self] { return self->backend.settleEdits(self->handle); });
    core::CommandJournal& journal = *session->journal;
    const core::AnnotationStore& store = journal.document().annotations();
    std::optional<core::Annotation> link;
    for (std::size_t at = 0; at < store.size(); ++at)
        if (store.at(at).subtype == core::AnnotationSubtype::Link) link = store.at(at);
    REQUIRE(link);
    const int last = journal.document().pages().count() - 1;   // the label's "a link to its last page"
    REQUIRE(journal.run(std::make_unique<core::MoveAnnotationCommand>(std::vector<core::AnnotationId>{link->id}, 5.0, 5.0, 1791504300)));
    const core::Status deleted = journal.run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{last}));
    INFO((deleted ? std::string("made") : std::string(deleted.error().messageKey.data()) + " " + deleted.error().context.toStdString()));
    REQUIRE(deleted);

    auto saved = session->coordinator->save(journal, session->path);
    REQUIRE(saved);
    REQUIRE(saved.value().kind == core::SaveOutcome::Kind::Saved);
    auto reopened = tests::openCopy("assembly/merge-a.pdf");   // the control's shape: a fresh backend...
    REQUIRE(reopened);
    REQUIRE(reopened->backend.close(reopened->handle));
    auto handle = reopened->backend.open(session->path, nullptr);   // ...over the file just saved
    REQUIRE(handle);
    const core::AnnotationStore* written = reopened->backend.annotations(handle.value());
    REQUIRE(written != nullptr);
    std::optional<core::Annotation> back;
    for (std::size_t at = 0; at < written->size(); ++at)
        if (written->at(at).subtype == core::AnnotationSubtype::Link) back = written->at(at);
    REQUIRE(back);
    CHECK(std::abs(back->rect.left() - (link->rect.left() + 5.0)) < 0.01);
    CHECK(std::abs(back->rect.bottom() - (link->rect.bottom() + 5.0)) < 0.01);
    // Its action cleared, as D7-2 treats a link to a deleted page: the file names no destination for it.
    const QByteArray bytes = tests::readAllBytes(session->path);
    auto table = pdf::cos::openWithRepair(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    REQUIRE(table);
    auto object = table.value().table.fetch(back->id.object);
    REQUIRE(object);
    CHECK(object.value().object.find("Dest") == nullptr);
    CHECK(object.value().object.find("A") == nullptr);
}

// GUIDE I-04's REGRESSION (S7 T4, found by CI's `fuzz_annot_pipeline`, run 41141004141): the book's first number is
// the allocator's floor raised past every number the file REFERENCES -- through `ObjectAllocator::reserveThrough`,
// which ignores a reference past ISO 32000-1 Annex C's limit. Computed as `largestReferenced + 1` it overflowed on a
// reference to 2^63-1 (UBSan's finding, invisible on MinGW, where it wraps), and numbered every new object past the
// limit on a reference to 8,388,608 -- which the reader refuses, so the edit was refused by its verify.
TEST_CASE("a reference past the object limit does not move where new objects are numbered", "[compose][I-04]")
{
    for (const char* far : {"8388608", "9223372036854775807"}) {
        INFO("the file references " << far << " 0 R");
        const std::string bytes = tests::annotatedDocument(1, [far](int, tests::TinyPdf& pdf, int) -> std::string {
            pdf.add(std::string("<< /Dangling ") + far + " 0 R >>");   // placed, referenced by nothing, naming nothing
            return std::string();
        });
        auto session = tests::openBuilt(QStringLiteral("far.pdf"), bytes);
        REQUIRE(session);
        REQUIRE(session->sink() != nullptr);
        tests::SaveSession* self = session.get();
        session->journal->setShowHandler([self](const core::Scope&) { return self->backend.showEdits(self->handle, {}); },
                                         [self] { return self->backend.settleEdits(self->handle); });
        const core::Status ran = session->journal->run(std::make_unique<core::AddAnnotationCommand>(
            std::vector<core::Annotation>{highlightOn(*session, 0, 0.0)}));
        INFO((ran ? std::string("made") : std::string(ran.error().messageKey.data()) + " " + ran.error().context.toStdString()));
        REQUIRE(ran);
        REQUIRE(session->journal->settle());
        auto composed = pdf::ops::RevisionWriter::composeUpdate(*session->sink(), false);
        REQUIRE(composed);
        REQUIRE_FALSE(composed.value().objectsWritten.empty());
        for (const long long number : composed.value().objectsWritten) {
            CHECK(number > 0);
            CHECK(number <= pdf::cos::ObjectAllocator::kMaxObjectNumber);
        }
        auto saved = session->coordinator->save(*session->journal, session->path);
        REQUIRE(saved);
        CHECK(saved.value().kind == core::SaveOutcome::Kind::Saved);
    }
}
