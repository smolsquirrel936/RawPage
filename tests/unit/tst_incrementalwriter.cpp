// tst_incrementalwriter -- guide cases A-19 … A-25, A-27 and the A-28 probe
// (`Stage3-Mutation-Testing-Guide.md` §5.4). Stage 3 T2.
//
// WHAT THIS SUITE IS TRYING TO CATCH, in order of cost:
//
//   1. **A byte of the original moving.** Every case re-reads `original + update` and compares the
//      prefix; A-22 goes further and requires every untouched object to be the same bytes at the
//      same offset through the RE-READ table, which is the claim a signature actually depends on.
//   2. **An update the reader cannot follow**: the rotation absent after a re-read, a section of the
//      wrong kind, `/Prev` pointing somewhere else, a packed object lost after a classic section on
//      a hybrid file.
//   3. **An update that carries more than it should** -- an untouched object, or the object stream a
//      replaced page used to live in.
//   4. **A refusal that still produced something.** A-27.
//
// WHAT IT DOES NOT COVER, named at the assertion where it matters (convention 19):
//
//   * **The disk.** Every case except the A-28 probe works in memory: `IncrementalWriter` returns
//     bytes and never touches a file. That the SAVED file keeps the prefix -- through a temporary
//     copy and a replace -- is `tests/roundtrip/`'s, at T8, and guide rule 3 is exactly why a
//     buffer comparison is not allowed to stand in for it there.
//   * **Which object a rotation SHOULD change.** Page 1 is found here by following `/Kids[0]` to a
//     leaf and its `/Rotate` is set by hand -- no inheritance, no normalization. Choosing the right
//     object and value is `pdf/ops/PageOps`' job at T3; this suite proves only that whatever the
//     delta says is what the file gets.
//   * **Any reader but ours.** PDFium re-reading the output is `tst_saveengine` (T8); OpenSSL judging
//     the probe's signatures is the `unit.a4_probe.openssl` CTest case, registered beside this
//     suite in `tests/unit/CMakeLists.txt`; Acrobat is guide case M-03, which is the user's.

#include <catch2/catch_test_macros.hpp>

#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/Parser.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/XrefTable.h"
#include "pdf/cos/filters/Filter.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace rawpage::pdf::cos;

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

// A table and the bytes it points into, kept together: `XrefTable` holds a view, so the bytes must
// outlive it and must not move.
struct Loaded {
    std::unique_ptr<std::string> bytes;
    XrefTable table;
};

Loaded load(std::string bytes)
{
    auto owned = std::make_unique<std::string>(std::move(bytes));
    auto table = XrefTable::load(*owned);
    REQUIRE(table);
    return Loaded{std::move(owned), std::move(table).value()};
}

// Page 1 by `/Kids[0]` to a leaf -- no inheritance, no /Count (see the header's second exclusion).
long long firstPage(XrefTable& table)
{
    auto catalog = table.fetch(table.trailer().get("Root").reference().number);
    REQUIRE(catalog);
    long long node = catalog.value().object.get("Pages").reference().number;
    for (int depth = 0; depth < 64; ++depth) {
        auto object = table.fetch(node);
        REQUIRE(object);
        if (object.value().object.get("Type").text() == "Page")
            return node;
        const CosObject& kids = object.value().object.get("Kids");
        REQUIRE(kids.isArray());
        REQUIRE_FALSE(kids.items().empty());
        node = kids.items().front().reference().number;
    }
    FAIL("no page leaf within 64 levels");
    return -1;
}

// The page's dictionary with its OWN `/Rotate` advanced by `degrees`.
CosObject rotated(XrefTable& table, long long page, int degrees)
{
    auto object = table.fetch(page);
    REQUIRE(object);
    const CosObject& dictionary = object.value().object;
    const long long current = dictionary.get("Rotate").integerValue(0);
    std::vector<CosObject::Entry> entries;
    for (const CosObject::Entry& entry : dictionary.entries()) {
        if (entry.key != "Rotate")
            entries.push_back(entry);
    }
    entries.push_back({"Rotate", CosObject::makeInteger(((current + degrees) % 360 + 360) % 360)});
    return CosObject::makeDictionary(std::move(entries));
}

long long rotationOf(XrefTable& table, long long page)
{
    auto object = table.fetch(page);
    REQUIRE(object);
    return object.value().object.get("Rotate").integerValue(-1);
}

std::size_t entriesFromNewestRevision(const XrefTable& table)
{
    std::size_t count = 0;
    for (const auto& [number, entry] : table.entries()) {
        (void)number;
        if (entry.revision == 0)
            ++count;
    }
    return count;
}

bool startsWith(const std::string& whole, const std::string& prefix)
{
    return whole.size() >= prefix.size() && whole.compare(0, prefix.size(), prefix) == 0;
}

// What an object IS in a file, for A-22: its entry, and its bytes -- raw bytes at its offset for an
// ordinary object, its serialized value for a packed one (a packed object has no bytes in the file).
struct Fingerprint {
    XrefEntryType type = XrefEntryType::Free;
    long long field2 = 0;
    long long field3 = 0;
    std::string bytes;
};

std::map<long long, Fingerprint> fingerprints(XrefTable& table)
{
    std::map<long long, Fingerprint> out;
    for (const auto& [number, entry] : table.entries()) {
        if (entry.type == XrefEntryType::Free)
            continue;
        Fingerprint print{entry.type, entry.field2, entry.field3, {}};
        auto object = table.fetch(number);
        REQUIRE(object);
        if (entry.type == XrefEntryType::InFile)
            print.bytes = std::string(object.value().object.raw(table.source()));
        else
            print.bytes = serializeObject(object.value().object);
        out.emplace(number, std::move(print));
    }
    return out;
}

} // namespace

// ==========================================================================================
// Serialization
// ==========================================================================================

TEST_CASE("cos serializer spells awkward values so they parse back equal", "[cos][writer]")
{
    const std::string binary("a(b)c\\d\r\ne\x01\xff", 13);
    const std::vector<CosObject> values = {
        CosObject::makeNull(),
        CosObject::makeBoolean(true),
        CosObject::makeInteger(-42),
        CosObject::makeReal(0.5),
        CosObject::makeReal(-3.25),
        CosObject::makeReal(4.0),          // must stay a Real -- `4` would re-read as an Integer
        CosObject::makeReal(0.00001),      // must not come out as `1e-05`, which PDF cannot read
        CosObject::makeString(binary, StringForm::Literal),
        CosObject::makeString(binary, StringForm::Hex),
        CosObject::makeName("A B#C/D"),
        CosObject::makeName(""),
        CosObject::makeReference(12, 3),
        CosObject::makeArray({CosObject::makeInteger(1), CosObject::makeName("X"),
                              CosObject::makeArray({})}),
        CosObject::makeDictionary({{"Type", CosObject::makeName("Page")},
                                   {"Rotate", CosObject::makeInteger(90)},
                                   {"Rotate", CosObject::makeInteger(180)}}),
    };
    for (const CosObject& value : values) {
        const std::string text = serializeObject(value);
        INFO("serialized: " << text);
        Parser parser(text);
        const CosObject back = parser.parseObject();
        CHECK(back == value);
        CHECK(back.type() == value.type());
        // The duplicated `/Rotate` is deliberate -- CosObject keeps duplicates and so must the
        // writer -- and the parser rightly notes it. Any OTHER note is a spelling the writer got wrong.
        CHECK(parser.noteCount(ParseNote::DuplicateDictionaryKey) == parser.notes().size());
        if (value.type() == CosType::Real)
            CHECK(text.find('e') == std::string::npos);
    }
}

TEST_CASE("cos serializer round-trips every object in the ordinary corpus", "[cos][writer][corpus]")
{
    // Broad rather than deep: every value a generator we own emits, including every packed object,
    // must survive serialize-then-parse unchanged. A writer that rewrote a page dictionary it had
    // misread would otherwise corrupt a key nobody looked at.
    std::size_t objects = 0;
    for (const auto& file : std::filesystem::directory_iterator(corpus("ordinary"))) {
        if (file.path().extension() != ".pdf")
            continue;
        INFO("file: " << file.path().filename().string());
        Loaded loaded = load(readFile(file.path()));
        for (const auto& [number, entry] : loaded.table.entries()) {
            if (entry.type == XrefEntryType::Free)
                continue;
            auto object = loaded.table.fetch(number);
            REQUIRE(object);
            INFO("object " << number);
            const std::string text = serializeObject(object.value().object);
            Parser parser(text);
            CosObject back = parser.parseObject();
            if (object.value().object.isStream()) {
                // A stream serializes as its dictionary; compare that half.
                CHECK(back.entries().size() == object.value().object.entries().size());
            } else {
                CHECK(back == object.value().object);
            }
            ++objects;
        }
    }
    // 2,229 at Stage 3 T2 (measured). A floor, so a new corpus file does not redden it; zero would
    // mean the directory moved and the loop checked nothing (convention 7).
    CHECK(objects >= 2000);
}

// ==========================================================================================
// A-19 -- after a classic table
// ==========================================================================================

TEST_CASE("cos writer appends a rotation after a classic table", "[cos][writer]")
{
    Loaded before = load(readFile(corpus("ordinary/text-a4.pdf")));
    REQUIRE_FALSE(before.table.sections().front().isStream);
    const long long page = firstPage(before.table);
    const long long oldSize = before.table.declaredSize();

    ObjectDelta delta(before.table);
    delta.replace(page, rotated(before.table, page, 90));
    auto update = IncrementalWriter::append(*before.bytes, before.table, delta);
    REQUIRE(update);
    CHECK(update.value().kind == SectionKind::Classic);
    CHECK(update.value().objectsWritten == std::vector<long long>{page});

    // In memory -- the header's first exclusion.
    Loaded after = load(*before.bytes + update.value().bytes);
    CHECK(startsWith(*after.bytes, *before.bytes));
    REQUIRE(after.table.sections().size() == before.table.sections().size() + 1);
    CHECK(after.table.sections().front().offset == update.value().sectionOffset);
    CHECK_FALSE(after.table.sections().front().isStream);

    CHECK(rotationOf(after.table, page) == 90);
    CHECK(entriesFromNewestRevision(after.table) == 1);
    CHECK(after.table.sections().front().trailer.get("Prev").integerValue(-1)
          == static_cast<long long>(before.table.sections().front().offset));
    CHECK(after.table.declaredSize() >= oldSize);
    CHECK(after.table.trailer().get("Root") == before.table.trailer().get("Root"));
    CHECK(after.table.trailer().get("Info") == before.table.trailer().get("Info"));
    CHECK(after.table.notes().empty());
}

TEST_CASE("cos writer replaces a stream and writes its true Length", "[cos][writer]")
{
    // A stream change carries its ENCODED data, and `/Length` is the data's -- never a value copied
    // from the dictionary it replaced, which is how a writer manufactures `length-lies-short.pdf`.
    Loaded before = load(readFile(corpus("ordinary/text-a4.pdf")));
    const long long page = firstPage(before.table);
    auto pageObject = before.table.fetch(page);
    REQUIRE(pageObject);
    const long long contents = pageObject.value().object.get("Contents").reference().number;
    auto original = before.table.fetch(contents);
    REQUIRE(original);

    const std::string data = "BT /F1 12 Tf 72 720 Td (replaced by tst_incrementalwriter) Tj ET\n";
    std::vector<CosObject::Entry> entries;
    for (const CosObject::Entry& entry : original.value().object.entries()) {
        if (entry.key != "Filter" && entry.key != "DecodeParms")
            entries.push_back(entry);   // including a stale /Length, which must not survive
    }
    ObjectDelta delta(before.table);
    delta.replace(contents, CosObject::makeStream(std::move(entries), CosStreamData{}), data);
    auto update = IncrementalWriter::append(*before.bytes, before.table, delta);
    REQUIRE(update);

    Loaded after = load(*before.bytes + update.value().bytes);
    auto written = after.table.fetch(contents);
    REQUIRE(written);
    CHECK_FALSE(written.value().object.streamData().lengthRecovered);
    CHECK(written.value().object.get("Length").integerValue() == static_cast<long long>(data.size()));
    auto decoded = decodeStream(written.value().object, *after.bytes);
    REQUIRE(decoded);
    CHECK(std::string(decoded.value().begin(), decoded.value().end()) == data);
}

// ==========================================================================================
// A-20 -- after a cross-reference stream, and after a hybrid file
// ==========================================================================================

TEST_CASE("cos writer appends an xref stream after an xref stream and lifts a packed page out", "[cos][writer]")
{
    for (const char* name : {"ordinary/objstm-xrefstream.pdf", "signed/approval-xrefstream.pdf"}) {
        INFO("file: " << name);
        Loaded before = load(readFile(corpus(name)));
        REQUIRE(before.table.sections().front().isStream);
        const long long page = firstPage(before.table);
        const XrefEntry pageBefore = *before.table.find(page);
        REQUIRE(pageBefore.type == XrefEntryType::InObjectStream);   // the fixture's fault
        const long long container = pageBefore.field2;

        ObjectDelta delta(before.table);
        delta.replace(page, rotated(before.table, page, 90));
        auto update = IncrementalWriter::append(*before.bytes, before.table, delta);
        REQUIRE(update);
        CHECK(update.value().kind == SectionKind::Stream);
        CHECK(update.value().xrefStreamNumber >= before.table.declaredSize());
        CHECK(update.value().objectsWritten == std::vector<long long>{page});

        Loaded after = load(*before.bytes + update.value().bytes);
        CHECK(startsWith(*after.bytes, *before.bytes));
        REQUIRE(after.table.sections().front().isStream);
        CHECK(after.table.sections().front().trailer.get("Filter").text() == "FlateDecode");

        // The page is an ordinary object now, in the newest revision, and rotated.
        const XrefEntry* pageAfter = after.table.find(page);
        REQUIRE(pageAfter != nullptr);
        CHECK(pageAfter->type == XrefEntryType::InFile);
        CHECK(pageAfter->revision == 0);
        CHECK(rotationOf(after.table, page) == 90);

        // THE CONTROL for "the object stream is not in the update": its entry is the old one, from
        // an older revision, at the old offset.
        const XrefEntry* containerAfter = after.table.find(container);
        REQUIRE(containerAfter != nullptr);
        CHECK(containerAfter->revision == 1);
        CHECK(containerAfter->field2 == before.table.find(container)->field2);
        CHECK(entriesFromNewestRevision(after.table) == 2);   // the page and the xref stream
    }
}

TEST_CASE("cos writer appends a classic section after a hybrid file and keeps its packed objects", "[cos][writer]")
{
    // TWO SHAPES of hybrid file (Stage 3 guide issue I-14). The corpus file is Word's: a classic table,
    // then an empty section whose trailer carries /Prev and /XRefStm. The ONE-SECTION form -- the
    // same first section, its only trailer naming the stream -- is what the corpus file was until
    // M-02's walk found that Acrobat reports it damaged; it is valid by ISO 32000-1 7.5.8.4 and other
    // producers may write it, so the writer is proven on it too, built here from the corpus bytes.
    const std::string word = readFile(corpus("ordinary/hybrid-xrefstm.pdf"));
    REQUIRE_FALSE(word.empty());
    const std::string oneSection = [&word] {
        std::string first = word.substr(0, word.find("%%EOF\n") + 6);
        const std::size_t open = first.rfind("trailer\n<<");
        REQUIRE(open != std::string::npos);
        const Loaded probe = load(word);
        first.insert(open + 10, " /XRefStm "
                                    + std::to_string(probe.table.sections().front().trailer.get("XRefStm").integerValue()));
        return first;
    }();

    for (const auto& [shape, bytes] : {std::pair<const char*, const std::string*>{"Word's two sections", &word},
                                       {"one section", &oneSection}}) {
        INFO("shape: " << shape);
        Loaded before = load(*bytes);
        REQUIRE(before.table.sections()[1].fromXrefStm);   // the stream, reached from the newest trailer
        const long long hybridStream = before.table.sections().front().trailer.get("XRefStm").integerValue(-1);
        REQUIRE(hybridStream > 0);
        const long long page = firstPage(before.table);
        REQUIRE(before.table.find(page)->type == XrefEntryType::InObjectStream);

        auto catalog = before.table.fetch(before.table.trailer().get("Root").reference().number);
        REQUIRE(catalog);
        const long long pagesNumber = catalog.value().object.get("Pages").reference().number;
        auto pagesNode = before.table.fetch(pagesNumber);
        REQUIRE(pagesNode);
        const long long secondPage = pagesNode.value().object.get("Kids").items().at(1).reference().number;

        ObjectDelta delta(before.table);
        delta.replace(page, rotated(before.table, page, 90));
        auto update = IncrementalWriter::append(*before.bytes, before.table, delta);
        REQUIRE(update);
        CHECK(update.value().kind == SectionKind::Classic);

        Loaded after = load(*before.bytes + update.value().bytes);
        CHECK(startsWith(*after.bytes, *before.bytes));
        // One section more than before: the new table; the stream it names is the one already there,
        // and the older trailer naming it too is not read twice.
        REQUIRE(after.table.sections().size() == before.table.sections().size() + 1);
        CHECK_FALSE(after.table.sections()[0].isStream);
        CHECK(after.table.sections()[0].offset == update.value().sectionOffset);
        CHECK(after.table.sections()[1].fromXrefStm);

        // I-14's REGRESSION, read from the trailer ACROBAT reads -- the newest. Our reader and PDFium
        // reach the stream through /Prev without it, so no re-read below could see this line fail.
        CHECK(after.table.sections()[0].trailer.get("XRefStm").integerValue(-1) == hybridStream);

        // The page's new row beats the stream's old one, although the stream is now read in the same
        // revision: the classic table is read first, and first seen wins.
        const XrefEntry* pageAfter = after.table.find(page);
        REQUIRE(pageAfter != nullptr);
        CHECK(pageAfter->type == XrefEntryType::InFile);
        CHECK(rotationOf(after.table, page) == 90);

        // THE CONTROL for losing packed objects: the ones only `/XRefStm` lists still resolve.
        for (long long packed : {pagesNumber, secondPage}) {
            INFO("packed object " << packed);
            const XrefEntry* entry = after.table.find(packed);
            REQUIRE(entry != nullptr);
            CHECK(entry->type == XrefEntryType::InObjectStream);
            CHECK(after.table.fetch(packed));
        }
    }
}

// ==========================================================================================
// A-21 -- a fourth generation
// ==========================================================================================

TEST_CASE("cos writer adds a fourth generation to a three-generation file", "[cos][writer]")
{
    Loaded before = load(readFile(corpus("ordinary/incremental-3gen.pdf")));
    REQUIRE(before.table.sections().size() == 3);
    const long long page = firstPage(before.table);

    ObjectDelta delta(before.table);
    delta.replace(page, rotated(before.table, page, 270));
    auto update = IncrementalWriter::append(*before.bytes, before.table, delta);
    REQUIRE(update);

    Loaded after = load(*before.bytes + update.value().bytes);
    CHECK(startsWith(*after.bytes, *before.bytes));
    REQUIRE(after.table.sections().size() == 4);
    for (std::size_t section = 0; section < 3; ++section)
        CHECK(after.table.sections()[section + 1].offset == before.table.sections()[section].offset);

    // Newest wins, one revision further down: every object keeps its entry and moves back exactly
    // one revision, except the page, which is now the newest.
    for (const auto& [number, entry] : before.table.entries()) {
        INFO("object " << number);
        const XrefEntry* now = after.table.find(number);
        REQUIRE(now != nullptr);
        if (number == page) {
            CHECK(now->revision == 0);
            continue;
        }
        CHECK(now->revision == entry.revision + 1);
        CHECK(now->field2 == entry.field2);
        CHECK(now->type == entry.type);
    }
    CHECK(rotationOf(after.table, page) == 270);
}

// ==========================================================================================
// A-22 -- untouched means untouched
// ==========================================================================================

TEST_CASE("cos writer leaves every other object the same bytes at the same offset", "[cos][writer][corpus]")
{
    // CONVENTION 22's control for the whole writer, over every ordinary file: a one-page rotation
    // changes EXACTLY one object, and every other -- /Info, every stream, the /Pages nodes, every
    // other page -- resolves through the RE-READ table to the same entry and the same bytes.
    std::size_t files = 0;
    for (const auto& file : std::filesystem::directory_iterator(corpus("ordinary"))) {
        if (file.path().extension() != ".pdf")
            continue;
        INFO("file: " << file.path().filename().string());
        Loaded before = load(readFile(file.path()));
        const std::map<long long, Fingerprint> original = fingerprints(before.table);
        const long long page = firstPage(before.table);

        ObjectDelta delta(before.table);
        delta.replace(page, rotated(before.table, page, 90));
        auto update = IncrementalWriter::append(*before.bytes, before.table, delta);
        REQUIRE(update);

        Loaded after = load(*before.bytes + update.value().bytes);
        REQUIRE(startsWith(*after.bytes, *before.bytes));
        const std::map<long long, Fingerprint> now = fingerprints(after.table);

        std::vector<long long> differing;
        for (const auto& [number, print] : original) {
            const auto found = now.find(number);
            REQUIRE(found != now.end());
            const Fingerprint& later = found->second;
            if (later.type != print.type || later.field2 != print.field2
                || later.field3 != print.field3 || later.bytes != print.bytes) {
                differing.push_back(number);
            }
        }
        CHECK(differing == std::vector<long long>{page});

        // And nothing appeared except, in a stream-kind update, the stream itself.
        std::vector<long long> added;
        for (const auto& [number, print] : now) {
            (void)print;
            if (original.count(number) == 0)
                added.push_back(number);
        }
        if (update.value().kind == SectionKind::Stream)
            CHECK(added == std::vector<long long>{update.value().xrefStreamNumber});
        else
            CHECK(added.empty());
        ++files;
    }
    CHECK(files >= 32);
}

// ==========================================================================================
// A-24 -- deterministic, and /ID's second half only
// ==========================================================================================

TEST_CASE("cos writer is deterministic and changes only the second ID element", "[cos][writer]")
{
    Loaded before = load(readFile(corpus("ordinary/text-a4.pdf")));
    const long long page = firstPage(before.table);
    const CosObject& originalId = before.table.trailer().get("ID");
    REQUIRE(originalId.items().size() == 2);

    auto write = [&](int degrees) {
        ObjectDelta delta(before.table);
        delta.replace(page, rotated(before.table, page, degrees));
        auto update = IncrementalWriter::append(*before.bytes, before.table, delta);
        REQUIRE(update);
        return update.value().bytes;
    };
    const std::string first = write(90);
    const std::string second = write(90);
    CHECK(first == second);

    Loaded after = load(*before.bytes + first);
    const CosObject& id = after.table.trailer().get("ID");
    REQUIRE(id.items().size() == 2);
    CHECK(id.items()[0].text() == originalId.items()[0].text());
    CHECK(id.items()[1].text() != originalId.items()[1].text());
    CHECK(id.items()[1].text().size() == 16);

    // THE CONTROL: a different update gets a different second element -- otherwise "derived from
    // the update" would be satisfied by a constant.
    Loaded other = load(*before.bytes + write(180));
    CHECK(other.table.trailer().get("ID").items()[1].text() != id.items()[1].text());
}

// ==========================================================================================
// A-25 -- a repaired file: a complete section and no /Prev (D3-6)
// ==========================================================================================

TEST_CASE("cos writer gives a repaired file a complete table that opens without repair", "[cos][writer][corpus]")
{
    std::vector<std::string> written;
    std::vector<std::string> refused;
    for (const auto& file : std::filesystem::directory_iterator(corpus("malformed"))) {
        if (file.path().extension() != ".pdf")
            continue;
        const std::string name = file.path().filename().string();
        INFO("file: " << name);
        auto bytes = std::make_unique<std::string>(readFile(file.path()));
        auto opened = openWithRepair(*bytes);
        if (!opened || !opened.value().report.requiresSaveAs())
            continue;

        XrefTable& repaired = opened.value().table;
        ObjectDelta delta(repaired);
        auto update = IncrementalWriter::appendComplete(*bytes, repaired, delta);
        if (!update) {
            refused.push_back(name + " (" + update.error().messageKey.data() + ")");
            continue;
        }
        written.push_back(name);

        const std::string combined = *bytes + update.value().bytes;
        CHECK(startsWith(combined, *bytes));
        auto reopened = openWithRepair(combined);
        REQUIRE(reopened);
        CHECK(reopened.value().usedOriginalXref);
        CHECK_FALSE(reopened.value().report.requiresSaveAs());
        REQUIRE_FALSE(reopened.value().table.sections().empty());
        CHECK(reopened.value().table.sections().size() == 1);
        CHECK_FALSE(reopened.value().table.sections().front().trailer.has("Prev"));

        // Every object the original repair recovered resolves, to itself.
        for (const auto& [number, entry] : repaired.entries()) {
            if (entry.type == XrefEntryType::Free)
                continue;
            INFO("object " << number);
            if (!repaired.fetch(number))
                continue;   // the repair listed it and could not read it either
            auto again = reopened.value().table.fetch(number);
            REQUIRE(again);
            CHECK(again.value().number == number);
        }
    }

    // THE LIST IS PRINTED, so a case that iterated nothing is visible (A-25's own clause).
    std::string summary = "complete sections written for " + std::to_string(written.size())
                          + " repaired files:";
    for (const std::string& name : written)
        summary += " " + name;
    summary += "; refused " + std::to_string(refused.size()) + ":";
    for (const std::string& name : refused)
        summary += " " + name;
    WARN(summary);
    CHECK(written.size() >= 5);
}

// ==========================================================================================
// A-27 -- refusals produce nothing
// ==========================================================================================

TEST_CASE("cos writer refuses what it cannot write and produces nothing", "[cos][writer]")
{
    Loaded plain = load(readFile(corpus("ordinary/text-a4.pdf")));
    const long long page = firstPage(plain.table);

    auto refusedWith = [](const rawpage::core::Result<WrittenUpdate>& result, const char* key) {
        REQUIRE_FALSE(result);   // a Result that failed carries no bytes by construction
        CHECK(std::string(result.error().messageKey.data()) == key);
    };

    SECTION("an encrypted document")
    {
        Loaded encrypted = load(readFile(corpus("encrypted/aes-128.pdf")));
        REQUIRE(encrypted.table.isEncrypted());
        ObjectDelta delta(encrypted.table);
        delta.replace(1, CosObject::makeDictionary({}));
        refusedWith(IncrementalWriter::append(*encrypted.bytes, encrypted.table, delta),
                    "error.pdf.encrypted");
    }
    SECTION("an object the file does not have")
    {
        ObjectDelta delta(plain.table);
        delta.replace(99999, CosObject::makeDictionary({}));
        refusedWith(IncrementalWriter::append(*plain.bytes, plain.table, delta),
                    "error.pdf.writeObjectMissing");
    }
    SECTION("a stream with no data")
    {
        ObjectDelta delta(plain.table);
        delta.replace(page, CosObject::makeStream(std::vector<CosObject::Entry>{}, CosStreamData{}));
        refusedWith(IncrementalWriter::append(*plain.bytes, plain.table, delta),
                    "error.pdf.writeStreamDataMissing");
    }
    SECTION("the same object twice")
    {
        ObjectDelta delta(plain.table);
        delta.replace(page, rotated(plain.table, page, 90));
        delta.replace(page, rotated(plain.table, page, 180));
        refusedWith(IncrementalWriter::append(*plain.bytes, plain.table, delta),
                    "error.pdf.writeDuplicateObject");
    }
    SECTION("an empty delta")
    {
        ObjectDelta delta(plain.table);
        refusedWith(IncrementalWriter::append(*plain.bytes, plain.table, delta),
                    "error.pdf.writeNothing");
    }
    SECTION("a table over a different buffer")
    {
        const std::string copy = *plain.bytes;
        ObjectDelta delta(plain.table);
        delta.replace(page, rotated(plain.table, page, 90));
        refusedWith(IncrementalWriter::append(copy, plain.table, delta),
                    "error.pdf.writeWrongBuffer");
    }
    SECTION("an ordinary update to a repaired table")
    {
        auto bytes = std::make_unique<std::string>(readFile(corpus("malformed/xref-missing.pdf")));
        auto opened = openWithRepair(*bytes);
        REQUIRE(opened);
        REQUIRE(opened.value().table.sections().empty());
        ObjectDelta delta(opened.value().table);
        delta.replace(1, CosObject::makeDictionary({}));
        refusedWith(IncrementalWriter::append(*bytes, opened.value().table, delta),
                    "error.pdf.writeNoRevision");
    }
    SECTION("the control: the same delta on the same buffer is written")
    {
        ObjectDelta delta(plain.table);
        delta.replace(page, rotated(plain.table, page, 90));
        CHECK(IncrementalWriter::append(*plain.bytes, plain.table, delta));
    }
}

// ==========================================================================================
// A-28 -- THE A4 PROBE: the writer alone, over the signed corpus, to disk (D3-1, first half)
// ==========================================================================================

TEST_CASE("A4 probe writes a comment and a rotation into every signed corpus file", "[cos][writer][a4probe]")
{
    // The FILES this writes are what OpenSSL judges in `unit.a4_probe.openssl` and what the user
    // opens in Acrobat for guide case M-03's probe walk. TWO EDITS per file, into two directories,
    // because the first Acrobat walk taught that they answer different questions (Stage 3 guide
    // issue I-06, decided by the user 2026-09-14):
    //
    //   * `comment/` -- a text annotation added to page 1: a change these signatures' policy PERMITS
    //     ("Form Fill-in, Signing and Commenting"). Acrobat grades A4 on this set.
    //   * `rotate/` -- page 1's `/Rotate` set to 90: the only change Stage 3's product can make, and
    //     one the policy FORBIDS. Acrobat reported it "altered or corrupted" from our writer and from
    //     an unrelated one alike, so this set is a second control Acrobat must reject -- and OpenSSL
    //     still has to find every signed range intact in it.
    //
    // This case asserts the byte half for both: each file, read back FROM DISK, begins with every
    // byte of the original, so every signed range is identical, and the edit re-reads. What it does
    // NOT cover: signature validity (OpenSSL's, in `unit.a4_probe.openssl`) or policy (Acrobat's,
    // M-03). The deltas are hand-built -- the product's own path is T13's re-measurement.
    const std::filesystem::path out(RAWPAGE_A4_PROBE_DIR);

    std::size_t probed = 0;
    for (const char* edit : {"comment", "rotate"}) {
        std::filesystem::create_directories(out / edit);
        for (const char* name : {"approval.pdf", "approval-xrefstream.pdf",
                                 "certified-nochanges.pdf", "approval-then-update.pdf"}) {
            INFO("edit: " << edit << ", file: " << name);
            Loaded before = load(readFile(corpus(std::string("signed/") + name)));
            const long long page = firstPage(before.table);
            const bool comment = std::string_view(edit) == "comment";

            ObjectDelta delta(before.table);
            long long note = -1;
            if (comment) {
                note = delta.add(CosObject::makeDictionary({
                    {"Type", CosObject::makeName("Annot")},
                    {"Subtype", CosObject::makeName("Text")},
                    {"Rect", CosObject::makeArray({CosObject::makeInteger(500),
                                                   CosObject::makeInteger(760),
                                                   CosObject::makeInteger(520),
                                                   CosObject::makeInteger(780)})},
                    {"Contents", CosObject::makeString("A4 probe: a comment added after signing")},
                    {"F", CosObject::makeInteger(4)},
                    {"P", CosObject::makeReference(page, 0)}}));
                auto pageObject = before.table.fetch(page);
                REQUIRE(pageObject);
                std::vector<CosObject::Entry> entries;
                std::vector<CosObject> annots;
                for (const CosObject::Entry& entry : pageObject.value().object.entries()) {
                    if (entry.key == "Annots")
                        annots = entry.value.items();
                    else
                        entries.push_back(entry);
                }
                annots.push_back(CosObject::makeReference(note, 0));
                entries.push_back({"Annots", CosObject::makeArray(std::move(annots))});
                delta.replace(page, CosObject::makeDictionary(std::move(entries)));
            } else {
                delta.replace(page, rotated(before.table, page, 90));
            }
            auto update = IncrementalWriter::append(*before.bytes, before.table, delta);
            REQUIRE(update);

            const std::filesystem::path target = out / edit / name;
            // A viewer holding the file open -- Acrobat, during guide case M-03's walk -- makes this
            // write fail on Windows, and a bare `stream.good()` failure does not say so. Measured
            // 2026-09-14, when exactly the three files still open in Acrobat failed.
            INFO("writing " << target.string()
                            << " -- if this fails, is the file open in a PDF viewer?");
            {
                std::ofstream stream(target, std::ios::binary | std::ios::trunc);
                stream.write(before.bytes->data(),
                             static_cast<std::streamsize>(before.bytes->size()));
                stream.write(update.value().bytes.data(),
                             static_cast<std::streamsize>(update.value().bytes.size()));
                REQUIRE(stream.good());
            }

            // FROM DISK, not the buffer (guide rule 3).
            Loaded after = load(readFile(target));
            REQUIRE(startsWith(*after.bytes, *before.bytes));
            if (comment) {
                auto added = after.table.fetch(note);
                REQUIRE(added);
                CHECK(added.value().object.get("Subtype").text() == "Text");
                auto pageAfter = after.table.fetch(page);
                REQUIRE(pageAfter);
                const CosObject& annots = pageAfter.value().object.get("Annots");
                REQUIRE_FALSE(annots.items().empty());
                CHECK(annots.items().back().reference().number == note);
            } else {
                CHECK(rotationOf(after.table, page) == 90);
            }

            // The signed range's end, read with `pdf/cos/Parser` -- acceptable HERE because this only
            // locates bytes to compare; the independent judge of the signature is OpenSSL, whose
            // extractor shares no code with this reader. (This used `std::regex` until CI run
            // 34834009577 showed libstdc++ 13 drawing a -Wmaybe-uninitialized false positive from it.)
            const std::size_t key = before.bytes->find("/ByteRange");
            REQUIRE(key != std::string::npos);
            Parser parser(*before.bytes);
            parser.seek(key + std::string_view("/ByteRange").size());
            const CosObject range = parser.parseObject();
            REQUIRE(range.isArray());
            REQUIRE(range.items().size() == 4);
            const std::size_t signedEnd = static_cast<std::size_t>(
                range.items()[2].integerValue(-1) + range.items()[3].integerValue(-1));
            CHECK(signedEnd <= before.bytes->size());
            CHECK(after.bytes->compare(0, signedEnd, *before.bytes, 0, signedEnd) == 0);
            ++probed;
        }
    }
    CHECK(probed == 8);
}
