// tst_xreftable -- guide case A-20. Stage 2 T7.
//
// WHAT THIS SUITE IS TRYING TO CATCH:
//
//   1. BOTH cross-reference forms. A classic table, and a cross-reference STREAM -- which is not
//      an exotic path: every PDF 1.5-or-later file written by a modern producer uses it, so a
//      reader with only the classic half cannot open most documents at all.
//   2. THE `/Prev` CHAIN, walked over `corpus/ordinary/incremental-3gen.pdf`, with the property
//      that actually matters asserted directly: **the newest generation of each object wins**.
//      Reversing that reads a document as it was two saves ago, with no error anywhere -- which is
//      why the case checks the VALUES that changed rather than only the section count.
//   3. TYPE-2 ENTRIES -- objects packed inside an object stream. This is where T6's deliberately
//      open seam closes: `Parser::parseObjectStream` takes decoded bytes, and T7's `FlateFilter`
//      is what finally supplies them, so an object that only exists inside a compressed container
//      becomes reachable for the first time here.
//   4. The offset being a CLAIM. `corpus/malformed/xref-offsets-short.pdf` has every offset twelve
//      bytes short, so each one lands mid-object and parses as *something* -- and returning that
//      something is the failure nobody notices until a page renders as a font descriptor.
//   5. Loops and out-of-range offsets, which A-21 forbids from hanging.
//
// WHAT THIS SUITE DOES NOT COVER, named at the assertion (CLAUDE.md convention 19):
//
//   * **A hybrid file whose classic table lists its packed objects as FREE.** From Stage 3 T1
//     `corpus/ordinary/hybrid-xrefstm.pdf` covers the `/XRefStm` path, with the packed objects
//     ABSENT from the classic table; the free-entry arrangement is not in the corpus, and the
//     hybrid case below says so at its assertion. (Until T1 this comment said the path was
//     "exercised below against a hand-built file". No such case existed -- the path had never
//     run under test at all. Stage 3 guide issue I-03.)
//   * Nothing here REPAIRS anything. When `load()` fails, that failure is the signal T8's
//     `XrefRepair` acts on, and T8's own suite is where recovery is asserted.

#include <catch2/catch_test_macros.hpp>

#include "pdf/cos/XrefTable.h"

#include <filesystem>
#include <functional>
#include <fstream>
#include <string>

using rawpage::pdf::cos::CosObject;
using rawpage::pdf::cos::findStartxref;
using rawpage::pdf::cos::XrefEntryType;
using rawpage::pdf::cos::XrefNote;
using rawpage::pdf::cos::xrefNoteName;
using rawpage::pdf::cos::XrefTable;

namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(stream)),
                       std::istreambuf_iterator<char>());
}

std::filesystem::path corpus(std::string_view relative)
{
    return std::filesystem::path(RAWPAGE_CORPUS_DIR) / relative;
}

} // namespace

// ==========================================================================================
// 1. A classic table
// ==========================================================================================

TEST_CASE("cos xref reads a classic table and its trailer", "[cos][xref]")
{
    const std::string file = readFile(corpus("minimal.pdf"));
    REQUIRE_FALSE(file.empty());

    auto loaded = XrefTable::load(file);
    REQUIRE(loaded);
    XrefTable& table = loaded.value();

    CHECK(table.sections().size() == 1);
    CHECK_FALSE(table.sections()[0].isStream);
    CHECK(table.declaredSize() > 0);
    CHECK(table.trailer().has("Root"));
    CHECK_FALSE(table.isEncrypted());

    // Object 1 is the catalog in every file this corpus generates, and fetching it is the end-to-
    // end proof: startxref, the table, the offset, and the parser all had to agree.
    auto root = table.fetch(table.trailer().get("Root").reference().number);
    REQUIRE(root);
    CHECK(root.value().object.get("Type").text() == "Catalog");

    // Every `n` entry must point at the object it claims. This is the check that turns
    // `xref-offsets-short.pdf` into a report instead of a wrong answer, and running it over a GOOD
    // file is what proves the check is not simply always-false.
    for (long long number = 1; number <= table.largestObjectNumber(); ++number) {
        const auto* entry = table.find(number);
        if (entry == nullptr || entry->type != XrefEntryType::InFile)
            continue;
        INFO("object " << number);
        auto object = table.fetch(number);
        REQUIRE(object);
        CHECK(object.value().number == number);
    }
    CHECK(table.noteCount(XrefNote::ObjectOffsetWrong) == 0);
}

TEST_CASE("cos xref finds startxref even with trailing junk", "[cos][xref]")
{
    // ISO 32000-1 puts `startxref` within the last 1,024 bytes. It is searched for from the END of
    // the whole file instead, because `corpus/malformed/no-eof-marker.pdf` has trailing bytes after
    // it -- and a reader that only looked in the last kilobyte would call that file unopenable
    // when its table is perfectly good.
    const std::string file = readFile(corpus("malformed/no-eof-marker.pdf"));
    REQUIRE_FALSE(file.empty());

    long long offset = -1;
    const std::size_t at = findStartxref(file, offset);
    CHECK(at != std::string::npos);
    CHECK(offset > 0);

    auto loaded = XrefTable::load(file);
    REQUIRE(loaded);
    CHECK(loaded.value().entryCount() > 1);
}

// ==========================================================================================
// 2. Cross-reference streams and object streams
// ==========================================================================================

TEST_CASE("cos xref reads a cross-reference stream and its packed objects", "[cos][xref]")
{
    // `corpus/ordinary/objstm-xrefstream.pdf`: a PDF 1.5 file with no classic table anywhere. Its
    // cross-reference stream is itself `/FlateDecode`d, which is why this case could not exist
    // before T7's filters -- and why zlib is a dependency of finding objects rather than an image
    // feature.
    const std::string file = readFile(corpus("ordinary/objstm-xrefstream.pdf"));
    REQUIRE_FALSE(file.empty());

    auto loaded = XrefTable::load(file);
    REQUIRE(loaded);
    XrefTable& table = loaded.value();

    REQUIRE(table.sections().size() == 1);
    CHECK(table.sections()[0].isStream);
    CHECK(table.trailer().has("Root"));

    // Type-2 entries are the point: objects that exist only INSIDE a compressed container. If none
    // is present the rest of this case would pass against a reader that ignored them entirely.
    std::size_t packed = 0;
    for (long long number = 0; number <= table.largestObjectNumber(); ++number) {
        const auto* entry = table.find(number);
        if (entry != nullptr && entry->type == XrefEntryType::InObjectStream)
            ++packed;
    }
    INFO("packed objects: " << packed);
    REQUIRE(packed > 0);

    // And they are reachable. This is where T6's deliberately open seam closes: the parser takes
    // DECODED object-stream bytes, and the Flate filter is what supplies them.
    auto root = table.fetch(table.trailer().get("Root").reference().number);
    REQUIRE(root);
    CHECK(root.value().object.get("Type").text() == "Catalog");

    for (long long number = 0; number <= table.largestObjectNumber(); ++number) {
        const auto* entry = table.find(number);
        if (entry == nullptr || entry->type != XrefEntryType::InObjectStream)
            continue;
        INFO("packed object " << number);
        auto object = table.fetch(number);
        REQUIRE(object);
        CHECK(object.value().number == number);
        // A packed object has NO span: it was never a byte range in the file, and its decoded
        // container does not outlive the fetch. Stage 3 must re-serialize it rather than copy it,
        // and a stale span here would have Stage 3 copying freed memory.
        CHECK_FALSE(object.value().object.span().valid);
    }
}

TEST_CASE("cos xref clears the spans of every value inside a packed object", "[cos][xref]")
{
    // Stage 3 guide issue I-04 (`Stage3-Mutation-Testing-Guide.md` §8). A packed object was parsed
    // from a DECOMPRESSED payload, so every span inside it -- not only its own -- is measured into a
    // buffer that is not the file. Until Stage 3 T2 only the top-level span was cleared, and
    // `raw(fileBytes)` on a nested value returned a slice of the file from the wrong place.
    const std::string file = readFile(corpus("ordinary/objstm-xrefstream.pdf"));
    auto loaded = XrefTable::load(file);
    REQUIRE(loaded);
    XrefTable& table = loaded.value();

    std::size_t nested = 0;
    std::function<void(const CosObject&)> walk = [&](const CosObject& value) {
        CHECK_FALSE(value.span().valid);
        CHECK(value.raw(file).empty());
        ++nested;
        for (const CosObject& item : value.items())
            walk(item);
        for (const CosObject::Entry& entry : value.entries())
            walk(entry.value);
    };
    std::size_t packedObjects = 0;
    for (const auto& [number, entry] : table.entries()) {
        if (entry.type != XrefEntryType::InObjectStream)
            continue;
        auto object = table.fetch(number);
        REQUIRE(object);
        walk(object.value().object);
        ++packedObjects;
    }
    REQUIRE(packedObjects > 0);
    // Nested values must actually have been visited, or the case checks only the top level again.
    CHECK(nested > packedObjects * 2);
}

TEST_CASE("cos xref reaches objects that only a hybrid file's XRefStm lists", "[cos][xref]")
{
    // `corpus/ordinary/hybrid-xrefstm.pdf` (Stage 3 T1, guide case A-17 of
    // `Stage3-Mutation-Testing-Guide.md`): a classic table whose trailer carries `/XRefStm`. The
    // fonts, the `/Pages` node and both page dictionaries are packed in an object stream and are
    // ABSENT from the classic table, so the cross-reference stream is the only way to them.
    //
    // What this does NOT cover: a hybrid file that lists its packed objects in the classic table as
    // FREE entries, which some producers write. `XrefTable` merges free entries first-seen-wins and
    // reads the classic table before `/XRefStm`, so on such a file the free entry would win. That
    // arrangement is not in the corpus (`corpus/PROVENANCE.md` records the limit), and nothing here
    // says how this reader treats it.
    const std::string file = readFile(corpus("ordinary/hybrid-xrefstm.pdf"));
    REQUIRE_FALSE(file.empty());

    auto loaded = XrefTable::load(file);
    REQUIRE(loaded);
    XrefTable& table = loaded.value();

    REQUIRE(table.sections().size() == 2);
    CHECK_FALSE(table.sections()[0].isStream);
    CHECK_FALSE(table.sections()[0].fromXrefStm);
    CHECK(table.sections()[1].isStream);
    CHECK(table.sections()[1].fromXrefStm);
    CHECK(table.noteCount(XrefNote::SizeDisagrees) == 0);

    // The page tree, followed from the catalog. Every step past the catalog is a packed object.
    auto catalog = table.fetch(table.trailer().get("Root").reference().number);
    REQUIRE(catalog);
    const long long pagesNumber = catalog.value().object.get("Pages").reference().number;
    const auto* pagesEntry = table.find(pagesNumber);
    REQUIRE(pagesEntry != nullptr);
    CHECK(pagesEntry->type == XrefEntryType::InObjectStream);

    auto pages = table.fetch(pagesNumber);
    REQUIRE(pages);
    CHECK(pages.value().object.get("Type").text() == "Pages");
    const CosObject& kids = pages.value().object.get("Kids");
    REQUIRE(kids.isArray());
    REQUIRE(kids.items().size() == 2);
    for (const CosObject& kid : kids.items()) {
        INFO("page object " << kid.reference().number);
        const auto* entry = table.find(kid.reference().number);
        REQUIRE(entry != nullptr);
        CHECK(entry->type == XrefEntryType::InObjectStream);
        auto page = table.fetch(kid.reference().number);
        REQUIRE(page);
        CHECK(page.value().object.get("Type").text() == "Page");
    }

    // THE CONTROL (convention 7): the same bytes with the trailer key renamed, same length, so
    // every offset still holds. Without `/XRefStm` the packed objects must be GONE -- otherwise
    // the assertions above would pass against a reader that found them some other way, and would
    // say nothing about `/XRefStm` at all.
    std::string blind = file;
    const auto key = blind.find("/XRefStm");
    REQUIRE(key != std::string::npos);
    REQUIRE(blind.find("/XRefStm", key + 1) == std::string::npos);
    blind.replace(key, 8, "/XRefStX");

    auto without = XrefTable::load(blind);
    REQUIRE(without);
    CHECK(without.value().sections().size() == 1);
    CHECK(without.value().find(pagesNumber) == nullptr);
    CHECK_FALSE(without.value().fetch(pagesNumber));
}

// ==========================================================================================
// 3. The /Prev chain -- guide case A-20
// ==========================================================================================

TEST_CASE("cos xref walks a three-generation incremental file and the newest wins", "[cos][xref]")
{
    // GUIDE CASE A-20. `corpus/ordinary/incremental-3gen.pdf` is three revisions chained by
    // `/Prev`, and Stage 3's incremental writer appends to exactly this structure.
    const std::string file = readFile(corpus("ordinary/incremental-3gen.pdf"));
    REQUIRE_FALSE(file.empty());

    auto loaded = XrefTable::load(file);
    REQUIRE(loaded);
    XrefTable& table = loaded.value();

    // Three sections, newest first.
    REQUIRE(table.sections().size() == 3);
    CHECK(table.sections()[0].offset > table.sections()[1].offset);
    CHECK(table.sections()[1].offset > table.sections()[2].offset);

    // The merged trailer takes each key from the newest section that carries it. `/Root` is only
    // restated in the newest here, but a file whose last revision omitted it would still have to
    // resolve -- which is the reason the merge exists at all.
    CHECK(table.trailer().has("Root"));
    CHECK(table.trailer().has("Size"));

    // THE PROPERTY THAT MATTERS. The newest revision's entry for an object must win. Reversing the
    // walk reads the document as it was two saves ago with no error anywhere, so this is asserted
    // by REVISION rather than by counting sections.
    //
    // The newest section (revision 0) restates objects 6 and 9; the middle one (revision 1)
    // restates 3, 7, 10 and 11.
    const auto* six = table.find(6);
    REQUIRE(six != nullptr);
    CHECK(six->revision == 0);

    const auto* three = table.find(3);
    REQUIRE(three != nullptr);
    CHECK(three->revision == 1);

    // An object only the OLDEST generation defines is still reachable -- the chain is walked to
    // the end rather than stopping at the first section.
    const auto* one = table.find(1);
    REQUIRE(one != nullptr);
    CHECK(one->revision == 2);

    // And every live object fetches to itself, which is what proves the offsets from all three
    // generations are being read rather than only the newest.
    int fetched = 0;
    for (long long number = 1; number <= table.largestObjectNumber(); ++number) {
        const auto* entry = table.find(number);
        if (entry == nullptr || entry->type != XrefEntryType::InFile)
            continue;
        INFO("object " << number << " from revision " << entry->revision);
        auto object = table.fetch(number);
        REQUIRE(object);
        REQUIRE(object.value().number == number);
        ++fetched;
    }
    CHECK(fetched >= 9);
    CHECK(table.noteCount(XrefNote::ObjectOffsetWrong) == 0);
}

// ==========================================================================================
// 4. Malformed files -- report, never hang, never lie
// ==========================================================================================

TEST_CASE("cos xref reports a wrong offset rather than returning the wrong object", "[cos][xref]")
{
    // `corpus/malformed/xref-offsets-short.pdf`: every offset is twelve bytes short from object 4
    // onward, so each one lands mid-object and parses as SOMETHING. Returning that something is
    // the failure nobody notices until a page renders as a font descriptor, so the number that
    // came back is compared against the number asked for.
    const std::string file = readFile(corpus("malformed/xref-offsets-short.pdf"));
    REQUIRE_FALSE(file.empty());

    auto loaded = XrefTable::load(file);
    REQUIRE(loaded);          // the TABLE is readable; the offsets in it are not trustworthy
    XrefTable& table = loaded.value();

    int wrong = 0;
    for (long long number = 1; number <= table.largestObjectNumber(); ++number) {
        const auto* entry = table.find(number);
        if (entry == nullptr || entry->type != XrefEntryType::InFile)
            continue;
        auto object = table.fetch(number);
        if (!object || object.value().number != number)
            ++wrong;
    }
    INFO("objects whose offset did not hold them: " << wrong);
    CHECK(wrong > 0);
    CHECK(table.noteCount(XrefNote::ObjectOffsetWrong) > 0);

    // THE CONTROL. The same walk over the same generator's UNDAMAGED output must report nothing --
    // without it, this case is equally satisfied by a reader that calls every offset wrong.
    const std::string good = readFile(corpus("minimal.pdf"));
    auto control = XrefTable::load(good);
    REQUIRE(control);
    for (long long number = 1; number <= control.value().largestObjectNumber(); ++number) {
        const auto* entry = control.value().find(number);
        if (entry == nullptr || entry->type != XrefEntryType::InFile)
            continue;
        CHECK(control.value().fetch(number));
    }
    CHECK(control.value().noteCount(XrefNote::ObjectOffsetWrong) == 0);
}

TEST_CASE("cos xref ends a circular Prev chain instead of hanging", "[cos][xref]")
{
    // `corpus/malformed/circular-prev.pdf`. Guide case A-21 forbids a hang, and a hang is the one
    // failure a test suite reports badly -- so the loop guard is asserted directly rather than
    // inferred from the suite finishing.
    const std::string file = readFile(corpus("malformed/circular-prev.pdf"));
    REQUIRE_FALSE(file.empty());

    auto loaded = XrefTable::load(file);
    if (loaded) {
        CHECK(loaded.value().sections().size() < 50);
        CHECK(loaded.value().noteCount(XrefNote::PrevLoop) > 0);
    }
    // A failure is an equally acceptable outcome here; NOT hanging is the assertion.
    SUCCEED("circular-prev.pdf terminated");
}

TEST_CASE("cos xref names a missing or unusable startxref", "[cos][xref]")
{
    // Three different repairs, so three different notes -- T8 acts on the distinction: "there is
    // no startxref" is a full-file scan, while "the table it named is not a table" may still have
    // a usable /Prev chain behind it.
    struct Case {
        const char* file;
        XrefNote note;
    };
    const Case cases[] = {
        {"malformed/xref-missing.pdf", XrefNote::StartxrefMissing},
        {"malformed/startxref-past-eof.pdf", XrefNote::StartxrefOutOfRange},
        {"malformed/startxref-into-object.pdf", XrefNote::StartxrefNotATable},
    };

    for (const Case& item : cases) {
        INFO("file: " << item.file);
        const std::string file = readFile(corpus(item.file));
        REQUIRE_FALSE(file.empty());
        auto loaded = XrefTable::load(file);
        // Either it failed (and T8 repairs it) or it recovered through a /Prev chain -- but in
        // both cases the reason must be RECORDED, because a silent recovery gives T8 nothing to
        // report and the user no idea the file was damaged.
        if (!loaded) {
            SUCCEED("load declined, which is the signal XrefRepair acts on");
        } else {
            CHECK(loaded.value().noteCount(item.note) > 0);
        }
    }
}

TEST_CASE("cos xref notices a Size that disagrees with the table", "[cos][xref]")
{
    // `corpus/malformed/size-too-small.pdf`. `/Size` is a CLAIM the file makes, not a measurement,
    // and Stage 3's writer allocates new object numbers from it -- so a `/Size` smaller than the
    // largest object present would have the writer overwrite a live object.
    const std::string file = readFile(corpus("malformed/size-too-small.pdf"));
    REQUIRE_FALSE(file.empty());

    auto loaded = XrefTable::load(file);
    REQUIRE(loaded);
    CHECK(loaded.value().noteCount(XrefNote::SizeDisagrees) > 0);
    CHECK(loaded.value().largestObjectNumber() >= loaded.value().declaredSize());

    // The control: an honest file reports nothing, so the note above is a finding about this file
    // rather than something this reader says about every document.
    const std::string good = readFile(corpus("minimal.pdf"));
    auto control = XrefTable::load(good);
    REQUIRE(control);
    CHECK(control.value().noteCount(XrefNote::SizeDisagrees) == 0);
}

TEST_CASE("cos xref declines an encrypted document by name", "[cos][xref]")
{
    // D2-6: the COS reader DECLINES an encrypted file rather than half-parsing it, because two
    // parsers reaching different conclusions about one protected file is a class of bug that is
    // very hard to see. Reading an encrypted document at Stage 2 goes through PDFium instead; the
    // `StandardSecurityHandler` is Stage 10's.
    //
    // The table itself still loads -- `/Encrypt` does not encrypt the cross-reference structure --
    // which is exactly why the flag is on the table rather than on the load.
    for (const auto& entry : std::filesystem::directory_iterator(corpus("encrypted"))) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        INFO("file: " << entry.path().filename().string());
        const std::string file = readFile(entry.path());
        auto loaded = XrefTable::load(file);
        REQUIRE(loaded);
        CHECK(loaded.value().isEncrypted());
    }

    // The control, and it is the half that makes the loop mean something: an UNENCRYPTED file must
    // answer false, or `isEncrypted()` could simply be returning true.
    const std::string plain = readFile(corpus("minimal.pdf"));
    auto control = XrefTable::load(plain);
    REQUIRE(control);
    CHECK_FALSE(control.value().isEncrypted());
}

TEST_CASE("cos xref loads every ordinary corpus file", "[cos][xref][corpus]")
{
    // The breadth case. Every generated ORDINARY file must produce a usable table and a resolvable
    // `/Root` -- which is a stronger claim than "does not crash", and is the one that would catch
    // a whole producer style being unreadable.
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus("ordinary"))) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        INFO("file: " << entry.path().filename().string());
        const std::string file = readFile(entry.path());
        auto loaded = XrefTable::load(file);
        REQUIRE(loaded);

        const CosObject& root = loaded.value().trailer().get("Root");
        REQUIRE(root.isReference());
        auto catalog = loaded.value().fetch(root.reference().number);
        REQUIRE(catalog);
        CHECK(catalog.value().object.get("Type").text() == "Catalog");
        ++files;
    }
    // A floor rather than the exact number, so T8 adding a fixture does not redden a T7 case.
    // Zero would mean the directory moved and the loop checked nothing (convention 7).
    CHECK(files >= 25);
}

TEST_CASE("cos xref never hangs on a malformed corpus file", "[cos][xref][corpus]")
{
    // Guide case A-21's xref half. Every malformed file must TERMINATE -- succeeding or failing is
    // equally acceptable here, and T8's `XrefRepair` suite is where recovery is asserted.
    std::size_t files = 0;
    std::size_t loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus("malformed"))) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        INFO("file: " << entry.path().filename().string());
        const std::string file = readFile(entry.path());
        auto table = XrefTable::load(file);
        ++files;
        if (table) {
            ++loaded;
            // Fetching every entry must also terminate, which is where an object stream that
            // claims to contain itself would otherwise recurse.
            for (long long number = 0; number <= table.value().largestObjectNumber(); ++number)
                (void)table.value().fetch(number);
        }
    }
    CHECK(files >= 20);
    // Some must load and some must not, or this case is not exercising both branches.
    INFO("loaded " << loaded << " of " << files);
    CHECK(loaded > 0);
    CHECK(loaded < files);
}

TEST_CASE("cos xref note names are stable and complete", "[cos][xref]")
{
    // T8's RepairReport prints these, and a "?" in a repair report is a bug report nobody can act
    // on. Same reasoning as the lexer's and the parser's equivalent cases.
    CHECK(std::string(xrefNoteName(XrefNote::None)) == "None");
    CHECK(std::string(xrefNoteName(XrefNote::PrevLoop)) == "PrevLoop");
    for (int value = 0; value <= static_cast<int>(XrefNote::ObjectOffsetWrong); ++value)
        CHECK(std::string(xrefNoteName(static_cast<XrefNote>(value))) != "?");
}
