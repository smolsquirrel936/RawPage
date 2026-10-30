// tst_reference_repair -- the headless half of Stage 7 T2: what a page command WRITES, what it refuses, and the
// references it must leave alone. Decisions D7-2, D7-21 and D7-24 (`Stage7-Assembly-Plan.md` §3); guide cases
// A-22, A-24 and A-26 (`Stage7-Assembly-Testing-Guide.md` §5.4).
//
// THE ORACLE FOR "WHAT CHANGED" IS THE ORIGINAL FILE, READ INDEPENDENTLY: each case names the objects it
// expects in the update from the ORIGINAL bytes -- a page's `/Parent` as written, the catalog's number from
// the trailer -- never from `PageOps::shape`, which is the code under test. The update is what
// `RevisionWriter::compose` produces, which is exactly what a save writes (`RevisionWriter::write` composes
// and nothing else).
//
// WHAT IT DOES NOT COVER (convention 19):
//   * the ENGINE. The backend is `tests/unit/CosBackend.h`; whether every reference lands on the page that
//     SHOWS the identity its label names, and the labels PDFium reads, are `tests/render/tst_reference_repair.cpp`'s
//     (guide A-19, A-20, A-21), which reopens each saved file in PDFium;
//   * `qpdf --check` over what these cases save -- the `portable-asan` job's `check-qpdf.py` step (A-25);
//   * the journal's recovery replay of a page command: `platform.recovery.kill` (A-28).

#include <catch2/catch_test_macros.hpp>

#include "CosBackend.h"
#include "FakeDurableFiles.h"
#include "TinyPdf.h"
#include "core/cmd/CommandJournal.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"
#include "core/doc/SaveCoordinator.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/ops/PageOps.h"
#include "pdf/ops/RevisionWriter.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;

namespace {

QString corpusFile(const char* relative)
{
    return QDir(QStringLiteral(RAWPAGE_CORPUS_DIR)).filePath(QString::fromUtf8(relative));
}

std::string keyOf(const core::Status& status)
{
    return status ? std::string() : std::string(status.error().messageKey.data(), static_cast<std::size_t>(status.error().messageKey.size()));
}

// One corpus file, copied into a scratch directory and opened as the product opens it -- a journal over the
// real sink, saved by the real coordinator.
struct Session {
    QTemporaryDir scratch;
    QString path;
    tests::CosBackend backend;
    core::DocumentHandle handle;
    std::unique_ptr<core::CommandJournal> journal;
    tests::FakeDurableFiles files;
    std::unique_ptr<core::SaveCoordinator> coordinator;
    QByteArray original;

    pdf::ops::MutationSinkImpl& sink() { return *backend.sinkFor(handle); }
    const core::Document& document() const { return journal->document(); }
};

std::unique_ptr<Session> open(const QString& source)
{
    auto session = std::make_unique<Session>();
    REQUIRE(session->scratch.isValid());
    session->path = session->scratch.filePath(QStringLiteral("document.pdf"));
    REQUIRE(QFile::copy(source, session->path));
    QFile::setPermissions(session->path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    {
        QFile file(session->path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        session->original = file.readAll();
    }
    auto handle = session->backend.open(session->path, nullptr);
    INFO(source.toStdString());
    REQUIRE(handle);
    session->handle = handle.value();
    REQUIRE(session->backend.sinkFor(session->handle) != nullptr);
    core::Document document;
    REQUIRE(document.hydrate(session->backend, session->handle));
    session->journal = std::make_unique<core::CommandJournal>(std::move(document), session->backend.sinkFor(session->handle));
    session->coordinator = std::make_unique<core::SaveCoordinator>(session->backend, &session->files);
    return session;
}

// The objects the next save would append, and the bytes it would write.
pdf::ops::ComposedRevision composed(Session& session)
{
    auto revision = pdf::ops::RevisionWriter::compose(session.sink(), false);
    INFO("compose: " << (revision ? std::string() : std::string(revision.error().messageKey.data())));
    REQUIRE(revision);
    return std::move(revision).value();
}

std::set<long long> written(Session& session)
{
    const auto revision = composed(session);
    return std::set<long long>(revision.objectsWritten.begin(), revision.objectsWritten.end());
}

// The ORIGINAL file, read independently of the code under test: an object by number.
struct Original {
    explicit Original(const QByteArray& bytes)
        : bytes_(bytes)
    {
        auto opened = pdf::cos::openWithRepair(std::string_view(bytes_.constData(), static_cast<std::size_t>(bytes_.size())));
        REQUIRE(opened);
        table_.emplace(std::move(opened.value().table));
    }
    pdf::cos::CosObject object(long long number)
    {
        auto fetched = table_->fetch(number);
        REQUIRE(fetched);
        pdf::cos::CosObject value = fetched.value().object;
        value.clearSpansRecursively();
        return value;
    }
    long long catalog() { return table_->trailer().get("Root").reference().number; }
    long long pagesRoot() { return object(catalog()).get("Pages").reference().number; }
    long long parentOf(long long page) { return object(page).get("Parent").reference().number; }
    // The page objects in order, by following /Kids from the root as written -- a reading of its own.
    std::vector<long long> pages()
    {
        std::vector<long long> out;
        std::vector<long long> stack{pagesRoot()};
        while (!stack.empty()) {
            const long long number = stack.back();
            stack.pop_back();
            const pdf::cos::CosObject node = object(number);
            const pdf::cos::CosObject& kids = node.get("Kids");
            if (!kids.isArray()) {
                out.push_back(number);
                continue;
            }
            for (auto kid = kids.items().rbegin(); kid != kids.items().rend(); ++kid) stack.push_back(kid->reference().number);
        }
        return out;
    }

private:
    QByteArray bytes_;
    std::optional<pdf::cos::XrefTable> table_;
};

QByteArray readFile(const QString& path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

// Saved through the coordinator, and KEPT under `keepAs` in RAWPAGE_WRITTEN_DIR, where the `portable-asan` job's
// `check-qpdf.py` step reads every file the suite wrote (Stage 7 guide A-25, Stage 6's A-31 mechanism).
QString save(Session& session, const char* keepAs)
{
    auto result = session.coordinator->save(*session.journal, session.path);
    INFO("save: " << (result ? std::string() : std::string(result.error().messageKey.data()) + " " + result.error().context.toStdString()));
    REQUIRE(result);
    REQUIRE(result.value().kind == core::SaveOutcome::Kind::Saved);
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    REQUIRE(QDir().mkpath(dir));
    const QString kept = QDir(dir).filePath(QString::fromLatin1(keepAs));
    QFile::remove(kept);
    REQUIRE(QFile::copy(session.path, kept));
    return session.path;
}

bool reportHas(const core::PageCommandReport& report, core::PageReference::Kind kind, long long object)
{
    return std::any_of(report.references.begin(), report.references.end(),
                       [&](const core::PageReference& r) { return r.kind == kind && r.object == object; });
}

} // namespace

// ==========================================================================================
// A-24 -- the saved update holds exactly what changed
// ==========================================================================================

TEST_CASE("pageops.update a move inside one node writes that node and nothing else", "[pages][pageops.update][A-24]")
{
    auto session = open(corpusFile("ordinary/pages-900.pdf"));
    Original original(session->original);
    const std::vector<long long> pages = original.pages();
    REQUIRE(pages.size() == 900);
    REQUIRE(original.parentOf(pages[0]) == original.parentOf(pages[5]));

    REQUIRE(session->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{5}, 0)));
    CHECK(written(*session) == std::set<long long>{original.parentOf(pages[0])});

    // The saved file lists the pages in the order asked, read back by following /Kids, and the original is a
    // byte-identical PREFIX of it.
    const QByteArray saved = readFile(save(*session, "pages-01.pdf"));
    REQUIRE(saved.size() > session->original.size());
    CHECK(saved.left(session->original.size()) == session->original);
    // Page 5 first, then the rest in order -- written out rather than by std::rotate, over which GCC 13.1's
    // -Warray-bounds reports a memmove it cannot bound.
    std::vector<long long> expected{pages[5]};
    for (std::size_t at = 0; at < pages.size(); ++at) {
        if (at != 5) expected.push_back(pages[at]);
    }
    CHECK(Original(saved).pages() == expected);
}

TEST_CASE("pageops.update a move across nodes writes the two nodes and the page that moved", "[pages][pageops.update][A-24]")
{
    auto session = open(corpusFile("ordinary/pages-900.pdf"));
    Original original(session->original);
    const std::vector<long long> pages = original.pages();
    const long long from = original.parentOf(pages.front());
    const long long into = original.parentOf(pages.back());
    REQUIRE(from != into);   // the fixture is two levels deep: the first and last pages hang on different nodes

    REQUIRE(session->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 899)));
    // Not every node: the pages already in order stay where they are, and only page 0 is listed anew.
    CHECK(written(*session) == std::set<long long>{from, into, pages.front()});
    const QByteArray saved = readFile(save(*session, "pages-02.pdf"));
    Original reread(saved);
    std::vector<long long> expected(pages.begin() + 1, pages.end());
    expected.push_back(pages.front());
    CHECK(reread.pages() == expected);
    CHECK(reread.parentOf(pages.front()) == into);
}

TEST_CASE("pageops.update a delete writes the node that lost the page and every count above it", "[pages][pageops.update][A-24]")
{
    auto session = open(corpusFile("ordinary/pages-900.pdf"));
    Original original(session->original);
    const std::vector<long long> pages = original.pages();
    const long long node = original.parentOf(pages[450]);
    std::set<long long> expected{node};
    for (long long at = node; original.object(at).has("Parent");) {
        at = original.object(at).get("Parent").reference().number;
        expected.insert(at);
    }
    REQUIRE(session->journal->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{450})));
    CHECK(written(*session) == expected);
    const QByteArray saved = readFile(save(*session, "pages-03.pdf"));
    std::vector<long long> left = pages;
    left.erase(left.begin() + 450);
    CHECK(Original(saved).pages() == left);
}

TEST_CASE("pageops.update a move in a labelled document writes the node and the labels and no page", "[pages][pageops.update][A-24]")
{
    // `refs-every-kind.pdf`: one /Pages node, /PageLabels direct in the catalog -- and references of every kind,
    // NONE of which a move may write (D7-2: they follow the page object).
    auto session = open(corpusFile("assembly/refs-every-kind.pdf"));
    Original original(session->original);
    REQUIRE(session->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 11)));
    CHECK(written(*session) == std::set<long long>{original.pagesRoot(), original.catalog()});
}

TEST_CASE("pageops.update a move a delete and a duplicate undone before a save write nothing", "[pages][pageops.update][A-24]")
{
    // Rule 5's control for the desired state: put back is the disk, and the disk needs no update.
    for (const char* file : {"assembly/refs-every-kind.pdf", "ordinary/pages-900.pdf", "assembly/annots-on-pages.pdf"}) {
        INFO(file);
        auto session = open(corpusFile(file));
        const QByteArray hash = session->document().modelHash();
        REQUIRE(session->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0, 1}, 2)));
        REQUIRE(session->journal->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{1})));
        REQUIRE(session->journal->run(std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{0})));
        CHECK_FALSE(written(*session).empty());
        REQUIRE(session->journal->undo());
        REQUIRE(session->journal->undo());
        REQUIRE(session->journal->undo());
        CHECK(written(*session).empty());
        CHECK(session->document().modelHash() == hash);
        CHECK(composed(*session).bytes == session->original);
    }
}

TEST_CASE("pageops.update a duplicate is a new page object whose annotations are copies on it", "[pages][pageops.update][A-24]")
{
    // `annots-on-pages.pdf`: every page carries annotations -- a note with its pop-up, a reply, a link, a widget.
    auto session = open(corpusFile("assembly/annots-on-pages.pdf"));
    Original original(session->original);
    const std::vector<long long> pages = original.pages();
    auto command = std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{0});
    core::DuplicatePagesCommand* made = command.get();
    REQUIRE(session->journal->run(std::move(command)));
    // The page's form field widget is not copied -- a field's widgets are the field's (D7-16) -- and says so.
    CHECK(std::any_of(made->report().references.begin(), made->report().references.end(),
                      [](const core::PageReference& r) { return r.kind == core::PageReference::Kind::WidgetNotCopied; }));

    const QByteArray saved = readFile(save(*session, "pages-04.pdf"));
    Original reread(saved);
    const std::vector<long long> now = reread.pages();
    REQUIRE(now.size() == pages.size() + 1);
    CHECK(now[0] == pages[0]);
    const long long copy = now[1];
    CHECK(std::find(pages.begin(), pages.end(), copy) == pages.end());   // a new object
    // Its annotations are new objects, each naming the copy as its page; the source's own still name the source.
    const pdf::cos::CosObject copyPage = reread.object(copy);
    const pdf::cos::CosObject& annots = copyPage.get("Annots");
    REQUIRE(annots.isArray());
    CHECK_FALSE(annots.items().empty());
    const pdf::cos::CosObject sourcePage = reread.object(pages[0]);
    std::set<long long> sources;
    for (const pdf::cos::CosObject& entry : sourcePage.get("Annots").items()) sources.insert(entry.reference().number);
    for (const pdf::cos::CosObject& entry : annots.items()) {
        REQUIRE(entry.isReference());
        CHECK(sources.count(entry.reference().number) == 0);
        const pdf::cos::CosObject annotation = reread.object(entry.reference().number);
        CHECK(annotation.get("P").reference().number == copy);
        CHECK(annotation.get("Subtype").text() != "Widget");
        if (annotation.has("Popup")) CHECK(sources.count(annotation.get("Popup").reference().number) == 0);
    }
    CHECK_FALSE(copyPage.has("StructParents"));
    // The undo after the save removes the copy from the tree, and the redo lists THE SAME object again.
    REQUIRE(session->journal->undo());
    CHECK(Original(readFile(save(*session, "pages-05.pdf"))).pages() == pages);
    REQUIRE(session->journal->redo());
    CHECK(Original(readFile(save(*session, "pages-06.pdf"))).pages()[1] == copy);
}

// ==========================================================================================
// A-26 -- what is refused is refused by key, the document untouched
// ==========================================================================================

TEST_CASE("pageops.refused every page an encrypted document and a page whose readings disagree", "[pages][pageops.refused][A-26]")
{
    SECTION("deleting every page is not a document (D7-21)")
    {
        auto session = open(corpusFile("assembly/odd-count-3.pdf"));
        const QByteArray hash = session->document().modelHash();
        const core::Status status = session->journal->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{0, 1, 2}));
        CHECK(keyOf(status) == "error.edit.noPagesLeft");
        CHECK(session->document().modelHash() == hash);
        CHECK(written(*session).empty());
        // THE CONTROL: all but one is a document.
        CHECK(session->journal->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{0, 1})));
    }
    SECTION("an encrypted document is refused before the command runs (D3-7)")
    {
        tests::CosBackend backend;
        backend.plantReading(core::CosReading::Encrypted);
        auto handle = backend.open(corpusFile("assembly/odd-count-3.pdf"), nullptr);
        REQUIRE(handle);
        core::Document document;
        REQUIRE(document.hydrate(backend, handle.value()));
        core::CommandJournal journal(std::move(document), backend.sinkFor(handle.value()));
        const QByteArray hash = journal.document().modelHash();
        for (auto* command : {static_cast<core::Command*>(new core::MovePagesCommand({0}, 2)),
                              static_cast<core::Command*>(new core::DeletePagesCommand({1})),
                              static_cast<core::Command*>(new core::DuplicatePagesCommand({0}))}) {
            CHECK(keyOf(journal.run(std::unique_ptr<core::Command>(command))) == "error.edit.encrypted");
        }
        CHECK(journal.document().modelHash() == hash);
    }
    SECTION("a page whose two readings disagree is refused by name, and keeps its refusal when others move it")
    {
        tests::CosBackend backend;
        backend.plantAnnotationDisagreement(2);
        auto handle = backend.open(corpusFile("assembly/annots-on-pages.pdf"), nullptr);
        REQUIRE(handle);
        core::Document document;
        REQUIRE(document.hydrate(backend, handle.value()));
        core::CommandJournal journal(std::move(document), backend.sinkFor(handle.value()));
        const QByteArray hash = journal.document().modelHash();
        const core::Status named = journal.run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{2}));
        CHECK(keyOf(named) == "error.edit.pageReadingsDisagree");
        CHECK(named.error().context == QStringLiteral("3"));
        CHECK(keyOf(journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{1, 2}, 0))) == "error.edit.pageReadingsDisagree");
        CHECK(journal.document().modelHash() == hash);
        // Moving another page past it is allowed -- and the refusal travels with the page (D7-3).
        REQUIRE(journal.run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 3)));
        CHECK(journal.document().permissions().mayEditPage(1).error().context == QStringLiteral("2"));
        CHECK(journal.document().permissions().mayEditPage(2));
        CHECK_FALSE(journal.document().permissions().mayAnnotate(1));
    }
}

TEST_CASE("pagetree a page command naming no page or moving nowhere is refused and changes nothing", "[pages][pagetree][A-17]")
{
    // Guide A-17 through the JOURNAL: a move to where the pages already are makes no change and is refused (a
    // surface asks `PageMap::move(...).isIdentity()` and makes none); a page set outside the tree, an overlapping
    // move -- one whose block would run past the end -- and an empty set are refused; nothing is recorded, the
    // model is untouched, and the sink has nothing to write.
    auto session = open(corpusFile("assembly/odd-count-5.pdf"));
    const QByteArray hash = session->document().modelHash();
    std::vector<std::unique_ptr<core::Command>> refused;
    refused.push_back(std::make_unique<core::MovePagesCommand>(std::vector<int>{2}, 2));        // to where it is
    refused.push_back(std::make_unique<core::MovePagesCommand>(std::vector<int>{1, 2}, 1));     // likewise, a set
    refused.push_back(std::make_unique<core::MovePagesCommand>(std::vector<int>{3, 4}, 4));     // would run past the end
    refused.push_back(std::make_unique<core::MovePagesCommand>(std::vector<int>{2, 1}, 0));     // not a set
    refused.push_back(std::make_unique<core::DeletePagesCommand>(std::vector<int>{5}));         // outside the tree
    refused.push_back(std::make_unique<core::DeletePagesCommand>(std::vector<int>{}));          // nothing named
    refused.push_back(std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{0, 9}));   // outside the tree
    for (auto& command : refused) {
        INFO(QJsonDocument(command->serialize()).toJson(QJsonDocument::Compact).toStdString());
        CHECK_FALSE(session->journal->run(std::move(command)));
    }
    CHECK_FALSE(session->journal->canUndo());
    CHECK(session->document().modelHash() == hash);
    CHECK(written(*session).empty());
    // THE CONTROL: the same document takes a real move.
    CHECK(session->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{2}, 0)));
}

TEST_CASE("pageops.refused a damaged page tree keeps its order and still rotates", "[pages][pageops.refused][A-26]")
{
    // A /Kids entry that is not a reference: the walk notes it, and its shape would be a guess.
    tests::TinyPdf pdf;
    const int catalog = pdf.reserve();
    const int pagesNode = pdf.reserve();
    const int first = pdf.add("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>");
    const int second = pdf.add("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>");
    pdf.set(catalog, "<< /Type /Catalog /Pages 2 0 R >>");
    pdf.set(pagesNode, "<< /Type /Pages /Kids [" + std::to_string(first) + " 0 R " + std::to_string(second) + " 0 R 42] /Count 2 >>");
    QTemporaryDir scratch;
    const QString path = scratch.filePath(QStringLiteral("damaged.pdf"));
    {
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        const std::string bytes = pdf.bytes(catalog);
        file.write(bytes.data(), static_cast<qint64>(bytes.size()));
    }
    auto session = open(path);
    const core::Status refused = session->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 1));
    CHECK(keyOf(refused) == "error.pdf.pageTreeDamaged");
    CHECK(keyOf(session->journal->run(std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{0}))) == "error.pdf.pageTreeDamaged");
    CHECK(written(*session).empty());
    // THE CONTROL: a rotation writes no node, and is allowed.
    CHECK(session->journal->run(std::make_unique<core::RotatePageCommand>(0, 90)));
}

TEST_CASE("pageops.refused a repaired document runs a page command and its first save is Save-As", "[pages][pageops.refused][A-26]")
{
    // Stage 3's rule meeting a new command: appending to a file whose structure was guessed is how an
    // original is destroyed, so its first save is forced to Save-As -- the page command itself runs.
    int checked = 0;
    for (const char* file : {"malformed/xref-missing.pdf", "malformed/xref-offsets-short.pdf", "malformed/startxref-past-eof.pdf",
                             "malformed/startxref-into-object.pdf", "malformed/junk-before-header.pdf"}) {
        INFO(file);
        {
            tests::CosBackend probe;   // a file the first-party writer cannot open is not this case's subject
            auto handle = probe.open(corpusFile(file), nullptr);
            if (!handle || probe.sinkFor(handle.value()) == nullptr) continue;
        }
        auto session = open(corpusFile(file));
        if (!session->document().repairReport().requiresSaveAs()) continue;
        REQUIRE(session->journal->run(std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{0})));
        auto result = session->coordinator->save(*session->journal, session->path);
        REQUIRE(result);
        CHECK(result.value().kind == core::SaveOutcome::Kind::SaveAsRequired);
        auto as = session->coordinator->saveAs(*session->journal, session->scratch.filePath(QStringLiteral("as.pdf")));
        REQUIRE(as);
        CHECK(as.value().kind == core::SaveOutcome::Kind::Saved);
        CHECK(Original(readFile(session->scratch.filePath(QStringLiteral("as.pdf")))).pages().size()
              == static_cast<std::size_t>(session->document().pages().count()));
        ++checked;
    }
    CHECK(checked >= 1);
}

// ==========================================================================================
// A-22 -- what names a page by index is reported, not "fixed"
// ==========================================================================================

TEST_CASE("reference_repair.by_index a remote GoToR and an integer destination are left as written and reported", "[pages][reference_repair.by_index][A-22]")
{
    // `refs-by-index.pdf`: an outline item whose /Dest names its page by INTEGER (legal only in a remote
    // destination), and a link to a page object outside the page tree. `refs-every-kind.pdf`: a GoToR to itself.
    struct Holder {
        const char* file;
        long long object;
        const char* key;
        core::PageReference::Kind kind;
    };
    const Holder holders[] = {
        {"assembly/refs-by-index.pdf", 22, "Dest", core::PageReference::Kind::IndexDestination},
        {"assembly/refs-every-kind.pdf", 24, "A", core::PageReference::Kind::RemoteGoTo},
    };
    const auto commands = [](int count) {
        std::vector<std::unique_ptr<core::Command>> out;
        out.push_back(std::make_unique<core::MovePagesCommand>(std::vector<int>{count - 1}, 0));
        out.push_back(std::make_unique<core::DeletePagesCommand>(std::vector<int>{count - 1}));
        out.push_back(std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{1}));
        return out;
    };
    for (const Holder& holder : holders) {
        for (std::size_t which = 0; which < 3; ++which) {
            INFO(holder.file << " command " << which);
            auto session = open(corpusFile(holder.file));
            const pdf::cos::CosObject before = Original(session->original).object(holder.object).get(holder.key);
            auto made = commands(session->document().pages().count());
            core::Command* command = made[which].get();
            REQUIRE(session->journal->run(std::move(made[which])));
            const core::PageCommandReport* report = nullptr;
            if (auto* move = dynamic_cast<core::MovePagesCommand*>(command)) report = &move->report();
            if (auto* remove = dynamic_cast<core::DeletePagesCommand*>(command)) report = &remove->report();
            if (auto* copy = dynamic_cast<core::DuplicatePagesCommand*>(command)) report = &copy->report();
            REQUIRE(report != nullptr);
            CHECK(reportHas(*report, holder.kind, holder.object));
            const QByteArray saved = readFile(save(*session, "pages-07.pdf"));
            CHECK(Original(saved).object(holder.object).get(holder.key) == before);
        }
    }
    // And the link to a page outside the tree: no page command names that page, so nothing touches it.
    auto session = open(corpusFile("assembly/refs-by-index.pdf"));
    const pdf::cos::CosObject link = Original(session->original).object(12);
    REQUIRE(session->journal->run(std::make_unique<core::DeletePagesCommand>(std::vector<int>{0})));
    CHECK(Original(readFile(save(*session, "pages-08.pdf"))).object(12) == link);
}
