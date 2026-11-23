// tst_impositionops -- Stage 7 T16, decisions D7-8 and D7-46 (`Stage7-Assembly-Plan.md` §3): impositions made through
// `pdf/ops/ImpositionOps` and read back FIRST-PARTY -- guide case A-104's structural half and A-47's re-run for T16
// (`Stage7-Assembly-Testing-Guide.md` §5.18, §5.9).
//
// Each imposed document is read back by the first-party reader as any file is, and must be: ONE revision opened through
// its own table with no repair, a two-part `/ID`, nothing held that nothing reaches; one page per side the plan made,
// each the sheet's size; each side's `/XObject` naming exactly the pages the plan put on it; ONE form per source page and
// no more; every font of the source written ONCE however many forms use it (the label's face the only one added); the
// registration colour space present exactly when marks are; no `/Annots` anywhere -- the source's annotations counted
// instead. Every output is kept in RAWPAGE_WRITTEN_DIR as `imposition-*.pdf`, where the `portable-asan` job's
// `qpdf --check` reads it (D7-18's second oracle).
//
// WHAT IT DOES NOT COVER (convention 19): whether the sheets DRAW the pages in the planned cells -- `tst_impositionengine`
// (A-103), against PDFium -- or how they look (`render.golden.imposition`, A-104's golden half).

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/doc/Assembly.h"
#include "core/doc/Imposition.h"
#include "pdf/cos/FullWriter.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/ops/DocumentAssembler.h"
#include "pdf/ops/ImpositionOps.h"
#include "pdf/ops/PageOps.h"

#include <QDir>
#include <QFile>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace rawpage;
using pdf::cos::CosObject;
using pdf::ops::DocumentAssembler;
namespace fs = std::filesystem;

namespace {

std::string bytesOf(const QByteArray& bytes) { return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size())); }

const core::ImpositionText kText{{QStringLiteral("Sheet %1 of %2"), QStringLiteral("Sheet %1 of %2, front"), QStringLiteral("Sheet %1 of %2, back")},
                                 core::TypedFont::Simplified};

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
        return fetched ? fetched.value().object : CosObject::makeNull();
    }
    CosObject resolve(const CosObject& value) { return value.isReference() ? object(value.reference().number) : value; }
    // Objects whose /Type is `type`.
    int countOfType(const char* type)
    {
        int count = 0;
        for (const auto& [number, entry] : table->entries()) {
            if (number <= 0 || entry.type == pdf::cos::XrefEntryType::Free) continue;
            const CosObject value = object(number);
            if ((value.isDictionary() || value.isStream()) && value.get("Type").isName() && value.get("Type").text() == type) ++count;
        }
        return count;
    }
    std::string bytes;
    bool repaired = false;
    std::optional<pdf::cos::XrefTable> table;
    pdf::ops::PageWalk walk;
};

void keep(const QByteArray& bytes, const QString& name)
{
    const QString dir = QStringLiteral(RAWPAGE_WRITTEN_DIR);
    QDir().mkpath(dir);
    QFile file(QDir(dir).filePath(name));
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(bytes) == bytes.size());
}

std::unique_ptr<DocumentAssembler::Source> sourceOf(const std::string& bytes)
{
    auto source = DocumentAssembler::Source::open(bytes);
    if (!source) return nullptr;
    return std::move(source).value();
}

// Every failure A-104's structural half names, for one imposition of `source`.
std::vector<std::string> failures(const core::ImposedDocument& imposed, const core::ImpositionSpec& spec, Read& source)
{
    std::vector<std::string> out;
    Read made(bytesOf(imposed.bytes));
    if (made.repaired) out.push_back("opened by a repair");
    if (made.table->sections().size() != 1 || made.table->sections().front().trailer.has("Prev")) out.push_back("not one revision");
    const CosObject& id = made.table->trailer().get("ID");
    if (!id.isArray() || id.items().size() != 2) out.push_back("no two-part /ID");
    std::vector<long long> held;
    for (const auto& [number, entry] : made.table->entries()) {
        if (number > 0 && entry.type != pdf::cos::XrefEntryType::Free) held.push_back(number);
    }
    if (pdf::cos::FullWriter::reachable(*made.table) != held) out.push_back("an object nothing reaches");
    const core::ImpositionPlan& plan = imposed.plan;
    if (made.walk.pages.size() != plan.sides.size() || !made.walk.notes.empty()) {
        out.push_back("page count " + std::to_string(made.walk.pages.size()));
        return out;
    }
    std::set<long long> forms;
    bool registration = false;
    for (std::size_t k = 0; k < plan.sides.size(); ++k) {
        const pdf::ops::PageEntry& entry = made.walk.pages[k];
        if (!entry.mediaBox || (*entry.mediaBox)[2] != plan.sheet.width || (*entry.mediaBox)[3] != plan.sheet.height)
            out.push_back("side " + std::to_string(k) + " not the sheet's size");
        const CosObject page = made.object(entry.number);
        if (page.has("Annots")) out.push_back("side " + std::to_string(k) + " has /Annots");
        const CosObject resources = made.resolve(page.get("Resources"));
        const CosObject xobjects = made.resolve(resources.get("XObject"));
        std::set<std::string> named;
        if (xobjects.isDictionary()) {
            for (const CosObject::Entry& e : xobjects.entries()) {
                named.insert(e.key);
                if (!e.value.isReference()) {
                    out.push_back("a form not by reference");
                    continue;
                }
                forms.insert(e.value.reference().number);
                const CosObject form = made.object(e.value.reference().number);
                if (!form.isStream() || form.get("Subtype").text() != "Form") out.push_back("side " + std::to_string(k) + " names no form");
            }
        }
        std::set<std::string> planned;
        for (const core::ImposedCell& cell : plan.sides[k].cells) {
            if (cell.page >= 0) planned.insert("P" + std::to_string(cell.page));
        }
        if (named != planned) out.push_back("side " + std::to_string(k) + " names other pages than its plan");
        const CosObject spaces = made.resolve(resources.get("ColorSpace"));
        if (spaces.isDictionary() && spaces.has("RawPageAll")) registration = true;
        // A turned side draws inside one half turn.
        const CosObject contents = made.object(page.get("Contents").isReference() ? page.get("Contents").reference().number : 0);
        const std::string content(contents.isStream() ? std::string(contents.rawStreamData(made.table->source())) : std::string());
        const bool turnedInStream = content.rfind("q\n-1 0 0 -1 ", 0) == 0;
        if (turnedInStream != plan.sides[k].turned) out.push_back("side " + std::to_string(k) + " turned wrong");
    }
    if (static_cast<int>(forms.size()) != imposed.sourcePages) out.push_back("forms " + std::to_string(forms.size()));
    if (registration != (spec.cropMarks || spec.registrationMarks)) out.push_back("registration colour space");
    // EVERY FONT ONCE: the source's fonts, and the label's face the only one added.
    const int fonts = made.countOfType("Font");
    const int sourceFonts = source.countOfType("Font");
    if (spec.sheetLabels ? (fonts < sourceFonts || fonts > sourceFonts + 2) : fonts != sourceFonts)
        out.push_back("fonts " + std::to_string(fonts) + " against the source's " + std::to_string(sourceFonts));
    return out;
}

// The specs every odd-count file is imposed by: each layout, marks on and off, a duplex flip that turns the backs.
std::vector<std::pair<std::string, core::ImpositionSpec>> specs()
{
    std::vector<std::pair<std::string, core::ImpositionSpec>> out;
    core::ImpositionSpec marked;
    marked.cropMarks = marked.registrationMarks = marked.colourBar = marked.sheetLabels = marked.frames = true;
    for (const bool left : {true, false}) {
        core::ImpositionSpec spec = marked;
        spec.layout = core::ImpositionLayout::Booklet;
        spec.binding = left ? core::ImpositionBinding::Left : core::ImpositionBinding::Right;
        spec.duplex = left ? core::DuplexFlip::ShortEdge : core::DuplexFlip::LongEdge;   // the right-bound backs turned
        spec.creep = 1.0;
        out.emplace_back(left ? "booklet-left" : "booklet-right", spec);
    }
    for (const int n : core::kNUpCounts) {
        core::ImpositionSpec spec;
        spec.perSheet = n;
        spec.order = n % 2 == 0 ? core::ImpositionOrder::Across : core::ImpositionOrder::Down;
        spec.gutter = 6.0;
        if (n == 4) spec = marked, spec.perSheet = 4, spec.duplex = core::DuplexFlip::ShortEdge;
        out.emplace_back(std::to_string(n) + "up", spec);
    }
    return out;
}

} // namespace

TEST_CASE("imposition: every odd-count file imposed every way reads back as one revision of its sheets", "[imposition][A-104][A-47]")
{
    int made = 0;
    for (const int count : {1, 3, 5, 7, 9, 17, 2}) {
        const std::string name = "assembly/odd-count-" + std::to_string(count) + ".pdf";
        const std::string bytes = bytesOf(tests::readAllBytes(tests::corpusFile(name)));
        Read source(bytes);
        auto snapshot = sourceOf(bytes);
        REQUIRE(snapshot);
        for (const auto& [label, spec] : specs()) {
            INFO(name << " " << label);
            auto imposed = snapshot->impose(spec, kText);
            REQUIRE(imposed);
            CHECK(imposed.value().sourcePages == count);
            CHECK(imposed.value().pageCount == static_cast<int>(imposed.value().plan.sides.size()));
            CHECK(imposed.value().annotationsNotDrawn == 0);
            if (spec.layout == core::ImpositionLayout::Booklet) {
                const int padded = count <= 4 ? 4 : (count + 3) / 4 * 4;
                CHECK(imposed.value().blanks == padded - count);
                CHECK(imposed.value().sheets == padded / 4);
                CHECK(imposed.value().pageCount == padded / 2);
            }
            CHECK(failures(imposed.value(), spec, source).empty());
            for (const std::string& failure : failures(imposed.value(), spec, source)) std::cout << "  " << failure << std::endl;
            keep(imposed.value().bytes, QStringLiteral("imposition-odd-%1-%2.pdf").arg(count).arg(QString::fromStdString(label)));
            ++made;
        }
    }
    std::cout << "A-104: " << made << " impositions of the odd-count files read back, kept for qpdf" << std::endl;
}

TEST_CASE("imposition: two impositions of one document are two files and a page's comments are counted not drawn", "[imposition][A-104]")
{
    const std::string bytes = bytesOf(tests::readAllBytes(tests::corpusFile("assembly/annots-on-pages.pdf")));
    Read source(bytes);
    int annotations = 0;
    for (const pdf::ops::PageEntry& entry : source.walk.pages) {
        const CosObject annots = source.resolve(source.object(entry.number).get("Annots"));
        if (annots.isArray()) annotations += static_cast<int>(annots.items().size());
    }
    REQUIRE(annotations > 0);
    auto snapshot = sourceOf(bytes);
    REQUIRE(snapshot);
    core::ImpositionSpec spec;
    spec.perSheet = 4;
    auto first = snapshot->impose(spec, kText);
    auto second = snapshot->impose(spec, kText);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first.value().annotationsNotDrawn == annotations);
    CHECK(failures(first.value(), spec, source).empty());
    Read a(bytesOf(first.value().bytes));
    Read b(bytesOf(second.value().bytes));
    CHECK(a.table->trailer().get("ID").items()[0].text() != b.table->trailer().get("ID").items()[0].text());
    keep(first.value().bytes, QStringLiteral("imposition-annots-on-pages.pdf"));
}

TEST_CASE("imposition: what the plan refuses and a label no face can show refuse the imposition by key", "[imposition][A-102]")
{
    auto snapshot = sourceOf(bytesOf(tests::readAllBytes(tests::corpusFile("assembly/odd-count-3.pdf"))));
    REQUIRE(snapshot);
    core::ImpositionSpec spec;
    spec.cropMarks = true;
    spec.margin = 6.0;
    auto refused = snapshot->impose(spec, kText);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.imposition.marginTooSmall"));
    spec = core::ImpositionSpec{};
    spec.sheetLabels = true;
    core::ImpositionText cyrillic = kText;
    cyrillic.labels.single = QStringLiteral("Лист %1");
    refused = snapshot->impose(spec, cyrillic);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().messageKey == QLatin1String("error.edit.typedCharacterRefused"));
    // ... and the same words in the CJK forms chosen are drawn.
    core::ImpositionText han = kText;
    han.labels.single = QStringLiteral("第 %1 張，共 %2 張");
    han.han = core::TypedFont::Traditional;
    auto drawn = snapshot->impose(spec, han);
    REQUIRE(drawn);
    Read read(bytesOf(drawn.value().bytes));
    CHECK(read.countOfType("Font") >= 2);
}

TEST_CASE("imposition: the corpus imposed as a marked booklet and kept for qpdf", "[imposition][A-104][A-47]")
{
    std::vector<std::string> files;
    for (const auto& entry : fs::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf") continue;
        const std::string relative = fs::relative(entry.path(), RAWPAGE_CORPUS_DIR).generic_string();
        if (relative.rfind("encrypted/", 0) == 0) continue;
        files.push_back(relative);
    }
    std::sort(files.begin(), files.end());
    REQUIRE(files.size() > 100);
    core::ImpositionSpec spec;
    spec.layout = core::ImpositionLayout::Booklet;
    spec.duplex = core::DuplexFlip::LongEdge;
    spec.cropMarks = spec.registrationMarks = spec.colourBar = spec.sheetLabels = spec.frames = true;
    int imposed = 0;
    int kept = 0;
    std::map<std::string, std::string> refused;   // file -> key
    for (const std::string& relative : files) {
        INFO(relative);
        const std::string bytes = bytesOf(tests::readAllBytes(tests::corpusFile(relative)));
        auto snapshot = DocumentAssembler::Source::open(bytes);
        if (!snapshot) {
            refused[relative] = "open: " + QString(snapshot.error().messageKey).toStdString();
            continue;
        }
        auto made = snapshot.value()->impose(spec, kText);
        if (!made) {
            refused[relative] = QString(made.error().messageKey).toStdString() + " " + made.error().context.toStdString();
            // Only what a form cannot carry, and nothing else, may refuse a file the snapshot opened.
            CHECK(made.error().messageKey == QLatin1String("error.imposition.pageUnsupported"));
            continue;
        }
        Read source(bytes);
        const std::vector<std::string> wrong = failures(made.value(), spec, source);
        CHECK(wrong.empty());
        for (const std::string& failure : wrong) std::cout << "  " << relative << ": " << failure << std::endl;
        ++imposed;
        // Kept for qpdf unless its source is damaged on purpose -- qpdf may refuse what the source holds.
        if (relative.rfind("malformed/", 0) != 0) {
            keep(made.value().bytes, QStringLiteral("imposition-%1.pdf").arg(kept, 3, 10, QLatin1Char('0')));
            ++kept;
        }
    }
    std::cout << "A-104: " << files.size() << " files, " << imposed << " imposed as a marked booklet, " << kept << " kept for qpdf; "
              << refused.size() << " refused by key" << std::endl;
    for (const auto& [file, key] : refused) std::cout << "  " << file << ": " << key << std::endl;
    CHECK(imposed >= 100);
}
