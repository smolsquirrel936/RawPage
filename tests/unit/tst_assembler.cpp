// tst_assembler -- Stage 7 T7 (`Stage7-Assembly-Plan.md` §6, D7-5 as amended by D7-25, D7-30): the assembler's headless
// half -- guide cases A-47, A-48's control and A-50 (`Stage7-Assembly-Testing-Guide.md` §5.9).
//
// Every new document is READ BACK through the first-party reader and checked against its SOURCE read the same way --
// its page count and order, each page's dictionary, its resources -- and against the shape a new file must have: one
// cross-reference section, no `/Prev`, an `/ID` that is not the source's, nothing it holds unreachable, a page tree no
// node of which lists more than `kMaxKids`. Never against what the assembler computed. Each file written here is kept
// in RAWPAGE_WRITTEN_DIR as `assembly-*.pdf`, where the portable-asan job's `qpdf --check` reads it (D7-18's second
// oracle).
//
// WHAT IT DOES NOT COVER (convention 19): how a new document DRAWS against its source, and whether PDFium opens it
// with no repair -- `tests/render/tst_assemblerengine.cpp`, which also runs the splits through the engine's facts.

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Assembly.h"
#include "pdf/cos/FullWriter.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/DocumentAssembler.h"
#include "pdf/ops/PageOps.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using pdf::cos::CosObject;
using pdf::ops::DocumentAssembler;
namespace fs = std::filesystem;

namespace {

std::vector<std::string> corpusCandidates()
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

std::string bytesOf(const QByteArray& bytes)
{
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// A file read back, first-party, over bytes it keeps.
struct Read {
    explicit Read(std::string data) : bytes(std::move(data))
    {
        auto opened = pdf::cos::openWithRepair(bytes);
        REQUIRE(opened);
        repaired = !opened.value().usedOriginalXref || opened.value().report.requiresSaveAs();
        table.emplace(std::move(opened.value().table));
        auto walked = pdf::ops::PageOps::walk(*table);
        REQUIRE(walked);
        walk = std::move(walked).value();
    }
    CosObject object(long long number)
    {
        auto fetched = table->fetch(number);
        REQUIRE(fetched);
        return fetched.value().object;
    }
    std::string firstId()
    {
        const CosObject& id = table->trailer().get("ID");
        return id.isArray() && !id.items().empty() && id.items()[0].isString() ? id.items()[0].text() : std::string();
    }
    std::string bytes;
    bool repaired = false;
    std::optional<pdf::cos::XrefTable> table;
    pdf::ops::PageWalk walk;
};

// What A-47 asks of a new document, failures named.
std::vector<std::string> shapeFailures(Read& made, Read& source, const std::vector<int>& pages)
{
    std::vector<std::string> out;
    if (made.repaired) out.push_back("opened by a repair");
    if (made.table->sections().size() != 1 || made.table->sections().front().trailer.has("Prev")) out.push_back("not one revision");
    const CosObject& id = made.table->trailer().get("ID");
    if (!id.isArray() || id.items().size() != 2) out.push_back("no two-part /ID");
    if (!source.firstId().empty() && made.firstId() == source.firstId()) out.push_back("the source's /ID");
    std::vector<long long> held;
    for (const auto& [number, entry] : made.table->entries()) {
        if (number > 0 && entry.type != pdf::cos::XrefEntryType::Free) held.push_back(number);
    }
    if (pdf::cos::FullWriter::reachable(*made.table) != held) out.push_back("an object nothing reaches");
    if (made.walk.pages.size() != pages.size() || !made.walk.notes.empty()) {
        out.push_back("page count " + std::to_string(made.walk.pages.size()));
        return out;
    }
    // Each page is its source page: the same content streams' bytes, the same box, the same rotation.
    for (std::size_t at = 0; at < pages.size(); ++at) {
        const pdf::ops::PageEntry& got = made.walk.pages[at];
        const pdf::ops::PageEntry& want = source.walk.pages[static_cast<std::size_t>(pages[at])];
        if (got.effectiveRotate() != want.effectiveRotate()) out.push_back("rotation of page " + std::to_string(at + 1));
        if (want.mediaBox && got.mediaBox != want.mediaBox) out.push_back("media box of page " + std::to_string(at + 1));
        const auto contents = [](Read& file, long long page) {
            std::string all;
            CosObject value = file.object(page).get("Contents");
            if (value.isReference()) value = file.object(value.reference().number);
            std::vector<CosObject> streams = value.isArray() ? value.items() : std::vector<CosObject>{};
            if (!value.isArray() && !value.isNull()) streams.push_back(file.object(page).get("Contents"));
            for (const CosObject& item : streams) {
                if (!item.isReference()) continue;
                auto fetched = file.table->fetch(item.reference().number);
                if (fetched && fetched.value().object.isStream()) all += std::string(fetched.value().object.rawStreamData(file.table->source()));
            }
            return all;
        };
        if (contents(made, got.number) != contents(source, want.number)) out.push_back("content of page " + std::to_string(at + 1));
    }
    return out;
}

// Every node of the tree lists at most `kMaxKids`, every page is at one depth, and each `/Count` and `/Parent` is true.
void requireBalanced(Read& made)
{
    const CosObject root = made.object(made.table->trailer().get("Root").reference().number);
    std::set<int> depths;
    long long leaves = 0;
    std::function<long long(long long, long long, int)> visit = [&](long long node, long long parent, int depth) -> long long {
        const CosObject value = made.object(node);
        const CosObject& kids = value.get("Kids");
        if (!kids.isArray()) {
            depths.insert(depth);
            ++leaves;
            CHECK(value.get("Parent").reference().number == parent);
            return 1;
        }
        CHECK(kids.items().size() <= static_cast<std::size_t>(DocumentAssembler::kMaxKids));
        if (parent > 0) CHECK(value.get("Parent").reference().number == parent);
        long long count = 0;
        for (const CosObject& kid : kids.items()) count += visit(kid.reference().number, node, depth + 1);
        CHECK(value.get("Count").integerValue() == count);
        return count;
    };
    visit(root.get("Pages").reference().number, 0, 0);
    CHECK(depths.size() == 1);
    CHECK(leaves == static_cast<long long>(made.walk.pages.size()));
}

void keep(const std::string& bytes, const QString& name)
{
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    QDir().mkpath(dir);
    QFile file(QDir(dir).filePath(name));
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()));
}

std::unique_ptr<DocumentAssembler::Source> sourceOf(const std::string& relative)
{
    auto source = DocumentAssembler::Source::open(bytesOf(tests::readAllBytes(tests::corpusFile(relative))));
    if (!source) return nullptr;
    return std::move(source).value();
}

QStringList everythingUnder(const QString& root)
{
    QStringList out;
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) out << QDir(root).relativeFilePath(it.next());
    out.sort();
    return out;
}

} // namespace

// ==========================================================================================
// A-47 -- every new document is one revision, a fresh /ID, its pages as planned
// ==========================================================================================

TEST_CASE("assembler every corpus file assembled whole and in part is one revision of its pages", "[assembler][A-47]")
{
    int files = 0;
    int documents = 0;
    int kept = 0;
    std::vector<std::string> refused;
    std::vector<std::string> failures;
    for (const std::string& relative : corpusCandidates()) {
        auto source = DocumentAssembler::Source::open(bytesOf(tests::readAllBytes(tests::corpusFile(relative))));
        if (!source) {
            refused.push_back(relative + " (" + source.error().messageKey.data() + ")");
            continue;
        }
        Read original(bytesOf(tests::readAllBytes(tests::corpusFile(relative))));
        const int count = source.value()->pageCount();
        if (count == 0) {
            refused.push_back(relative + " (no pages)");
            continue;
        }
        ++files;
        std::vector<std::vector<int>> sets;
        std::vector<int> all;
        for (int page = 0; page < count; ++page) all.push_back(page);
        sets.push_back(all);
        if (count >= 3) {
            std::vector<int> odd;
            for (int page = 0; page < count; page += 2) odd.push_back(page);
            sets.push_back(odd);
            sets.push_back({count - 1});
        }
        for (const std::vector<int>& pages : sets) {
            auto made = source.value()->make(pages);
            if (!made) {
                failures.push_back(relative + ": " + made.error().messageKey.data() + " " + made.error().context.toStdString());
                continue;
            }
            ++documents;
            Read back(made.value().bytes);
            for (const std::string& failure : shapeFailures(back, original, pages))
                failures.push_back(relative + " [" + std::to_string(pages.size()) + " pages]: " + failure);
            if (pages.size() == all.size()) {
                // A document made from a file damaged ON PURPOSE keeps what the copy copies of its damage -- a stream's
                // bytes as they are -- so it is named after its source, and `check-qpdf.py --source` accepts its failure
                // only where qpdf refuses the source too (guide I-13's rule, carried).
                const QString name = relative.rfind("malformed/", 0) == 0
                                         ? QStringLiteral("assembly-malformed-%1").arg(QString::fromStdString(fs::path(relative).filename().string()))
                                         : QStringLiteral("assembly-%1.pdf").arg(kept + 1, 3, 10, QLatin1Char('0'));
                keep(made.value().bytes, name);
                ++kept;
            }
        }
    }
    std::cout << "A-47: " << files << " files, " << documents << " new documents, " << kept << " kept for qpdf; "
              << refused.size() << " refused by key:";
    for (const std::string& name : refused) std::cout << " " << name;
    std::cout << "\n";
    for (const std::string& failure : failures) std::cout << "  FAIL " << failure << "\n";
    CHECK(failures.empty());
    CHECK(files > 100);
}

TEST_CASE("assembler a 900-page document is a balanced tree of at most 32 kids a node", "[assembler][A-47]")
{
    auto source = sourceOf("ordinary/pages-900.pdf");
    REQUIRE(source);
    for (const int count : {1, 32, 33, 900}) {
        INFO(count << " pages");
        std::vector<int> pages;
        for (int page = 0; page < count; ++page) pages.push_back(page);
        auto made = source->make(pages);
        REQUIRE(made);
        Read back(made.value().bytes);
        REQUIRE(back.walk.pages.size() == static_cast<std::size_t>(count));
        requireBalanced(back);
        // Numbered from 1, no gap: every number below /Size is in use.
        CHECK(back.table->trailer().get("Size").integerValue() == static_cast<long long>(back.table->entries().size()));
    }
}

TEST_CASE("assembler two documents of the same pages have two identifiers", "[assembler][A-47]")
{
    auto source = sourceOf("assembly/refs-every-kind.pdf");
    REQUIRE(source);
    auto first = source->make({0, 1});
    auto second = source->make({0, 1});
    REQUIRE(first);
    REQUIRE(second);
    CHECK(Read(first.value().bytes).firstId() != Read(second.value().bytes).firstId());
}

TEST_CASE("assembler takes the document as it stands, unsaved edits included", "[assembler][A-48]")
{
    auto session = tests::openCopy("assembly/refs-none.pdf");
    REQUIRE(session);
    REQUIRE(session->rotate(1, 90));
    REQUIRE(session->journal->run(std::make_unique<core::MovePagesCommand>(std::vector<int>{0}, 3)));
    auto source = session->backend.assemblySource(session->handle);
    REQUIRE(source);
    auto made = source.value()->assemble({0, 3});
    REQUIRE(made);
    Read back(bytesOf(made.value().bytes));
    Read original(bytesOf(session->openedBytes));
    REQUIRE(back.walk.pages.size() == 2);
    // Page 0 is now the opened page 1, turned; page 3 is the opened page 0, moved.
    CHECK(back.walk.pages[0].effectiveRotate() == 90);
    const auto contentOf = [](Read& file, long long page) {
        const CosObject contents = file.object(page).get("Contents");
        REQUIRE(contents.isReference());
        return contents;
    };
    CHECK(back.object(contentOf(back, back.walk.pages[1].number).reference().number).rawStreamData(back.table->source())
          == original.object(contentOf(original, original.walk.pages[0].number).reference().number).rawStreamData(original.table->source()));
    // And the source document is untouched by the extract: its journal holds its two commands and is dirty.
    CHECK(session->journal->isDirty());
    CHECK(session->journal->document().pages().count() == static_cast<int>(original.walk.pages.size()));
}

TEST_CASE("assembler verify refuses what it was not built to be", "[assembler][A-47]")
{
    auto source = sourceOf("assembly/inherit-resources.pdf");
    REQUIRE(source);
    auto made = source->make({0, 1, 2});
    REQUIRE(made);
    const std::string& good = made.value().bytes;
    const std::string& payload = made.value().payload;
    REQUIRE(DocumentAssembler::verify(good, payload));

    // CONTROL 1: a content stream's byte changed, its length kept.
    {
        std::string bad = good;
        const std::size_t at = bad.find("stream\n");
        REQUIRE(at != std::string::npos);
        bad[at + 8] = static_cast<char>(bad[at + 8] ^ 0x20);
        const core::Status verified = DocumentAssembler::verify(bad, payload);
        REQUIRE_FALSE(verified);
        CHECK(verified.error().messageKey == QLatin1String("error.assembly.verifyFailed"));
    }
    // CONTROL 2: a second revision appended -- a file of two sections is not a new document.
    {
        const std::string sectionAt = good.substr(good.rfind("startxref\n") + 10);
        const std::string bad = good + "xref\n0 1\n0000000000 65535 f\r\ntrailer\n<< /Size " + std::to_string(Read(good).table->entries().size())
                                + " /Root 1 0 R /Prev " + std::to_string(std::stoll(sectionAt)) + " >>\nstartxref\n"
                                + std::to_string(good.size()) + "\n%%EOF\n";
        const core::Status verified = DocumentAssembler::verify(bad, payload);
        REQUIRE_FALSE(verified);
        CHECK(verified.error().context == QStringLiteral("revisions"));
    }
    // CONTROL 3: another document's three pages -- the right shape, the wrong file.
    {
        auto elsewhere = sourceOf("assembly/odd-count-3.pdf");
        REQUIRE(elsewhere);
        auto other = elsewhere->make({0, 1, 2});
        REQUIRE(other);
        REQUIRE_FALSE(DocumentAssembler::verify(other.value().bytes, payload));
    }
}

// ==========================================================================================
// A-50 -- nothing overwritten, nothing outside the chosen folder
// ==========================================================================================

TEST_CASE("assembler split.collision a name the folder holds refuses the whole write", "[assembler][split.collision][A-50]")
{
    auto source = sourceOf("assembly/odd-count-5.pdf");
    REQUIRE(source);
    QTemporaryDir root;
    REQUIRE(root.isValid());
    const QString folder = QDir(root.path()).filePath(QStringLiteral("parts"));
    REQUIRE(QDir().mkpath(folder));
    {
        QFile existing(QDir(folder).filePath(QStringLiteral("b.pdf")));
        REQUIRE(existing.open(QIODevice::WriteOnly));
        existing.write("not ours");
    }
    QFile sibling(QDir(root.path()).filePath(QStringLiteral("beside.txt")));
    REQUIRE(sibling.open(QIODevice::WriteOnly));
    sibling.close();
    const QStringList before = everythingUnder(root.path());

    const std::vector<core::AssemblyOutput> outputs{{{0, 1}, QStringLiteral("a.pdf")}, {{2}, QStringLiteral("b.pdf")}, {{3, 4}, QStringLiteral("c.pdf")}};
    // BEFORE ANYTHING IS WRITTEN means before anything is BUILT: the seam sees every file on its way to the disk, and a
    // write refused up front shows it none. (Refused later, at the rename, `a.pdf` would have been built, written and
    // taken back -- the listing below the same, the promise not kept.)
    int built = 0;
    DocumentAssembler::setWriteFaultForTesting([&built](std::string&) { ++built; });
    auto written = source->write(outputs, folder);
    DocumentAssembler::setWriteFaultForTesting({});
    CHECK(built == 0);
    REQUIRE_FALSE(written);
    CHECK(written.error().messageKey == QLatin1String("error.assembly.exists"));
    CHECK(written.error().context == QDir(folder).filePath(QStringLiteral("b.pdf")));   // the collision, named
    CHECK(everythingUnder(root.path()) == before);                                   // nothing written, anywhere
    QFile existing(QDir(folder).filePath(QStringLiteral("b.pdf")));
    REQUIRE(existing.open(QIODevice::ReadOnly));
    CHECK(existing.readAll() == QByteArray("not ours"));

    // A name that is not a bare file name, and two of one name (case-blind, as Windows' names are): refused alike.
    for (const QString& name : {QStringLiteral("../escape.pdf"), QStringLiteral("sub/x.pdf"), QStringLiteral(".."), QString()}) {
        auto bad = source->write({{{0}, name}}, folder);
        REQUIRE_FALSE(bad);
        CHECK(bad.error().messageKey == QLatin1String("error.assembly.badName"));
    }
    auto twice = source->write({{{0}, QStringLiteral("x.pdf")}, {{1}, QStringLiteral("X.pdf")}}, folder);
    REQUIRE_FALSE(twice);
    CHECK(twice.error().messageKey == QLatin1String("error.assembly.duplicateName"));
    CHECK(everythingUnder(root.path()) == before);
}

TEST_CASE("assembler write makes every file, read back from its path", "[assembler][split][A-49]")
{
    auto source = sourceOf("assembly/odd-count-5.pdf");
    REQUIRE(source);
    QTemporaryDir root;
    const QString folder = QDir(root.path()).filePath(QStringLiteral("new folder"));   // created
    auto written = source->write({{{0, 1}, QStringLiteral("a.pdf")}, {{2, 3, 4}, QStringLiteral("b.pdf")}}, folder);
    REQUIRE(written);
    REQUIRE(written.value().size() == 2);
    CHECK(everythingUnder(root.path())
          == QStringList{QStringLiteral("new folder"), QStringLiteral("new folder/a.pdf"), QStringLiteral("new folder/b.pdf")});
    Read original(bytesOf(tests::readAllBytes(tests::corpusFile("assembly/odd-count-5.pdf"))));
    for (std::size_t at = 0; at < 2; ++at) {
        Read back(bytesOf(tests::readAllBytes(written.value()[at].path)));
        CHECK(shapeFailures(back, original, at == 0 ? std::vector<int>{0, 1} : std::vector<int>{2, 3, 4}).empty());
        CHECK(written.value()[at].bytes == static_cast<qint64>(back.bytes.size()));
        keep(back.bytes, QStringLiteral("assembly-split-%1.pdf").arg(at + 1));
    }
}

// ==========================================================================================
// A-48's control, the file half -- a write that fails leaves nothing
// ==========================================================================================

TEST_CASE("assembler extract a file that fails its verify takes back every file the write made", "[assembler][extract][A-48]")
{
    auto source = sourceOf("assembly/odd-count-5.pdf");
    REQUIRE(source);
    QTemporaryDir root;
    const QString folder = QDir(root.path()).filePath(QStringLiteral("pages"));
    int built = 0;
    DocumentAssembler::setWriteFaultForTesting([&built](std::string& bytes) {
        // The SECOND file corrupted after it was built: the first is already on disk when the second fails.
        if (++built == 2) bytes[bytes.find("stream\n") + 8] ^= 0x20;
    });
    auto written = source->write({{{0}, QStringLiteral("1.pdf")}, {{1}, QStringLiteral("2.pdf")}, {{2}, QStringLiteral("3.pdf")}}, folder);
    DocumentAssembler::setWriteFaultForTesting({});
    REQUIRE_FALSE(written);
    CHECK(written.error().messageKey == QLatin1String("error.assembly.verifyFailed"));
    CHECK(built == 2);
    CHECK(QDir(folder).entryList(QDir::Files | QDir::Hidden | QDir::System).isEmpty());   // neither file, nor a temporary
}
