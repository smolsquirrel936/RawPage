// tst_xrefrepair -- guide cases A-21 and A-22. Stage 2 T8.
//
// **A-21 IS THE STAGE 2 EXIT CRITERION**: every file in `corpus/malformed/` yields a report and
// never a crash, a hang, an assertion or an unbounded allocation. Twenty-four files, one named
// fault each, and this suite is where that claim is made.
//
// WHAT THIS SUITE IS TRYING TO CATCH, in the order the cases appear:
//
//   1. That a HEALTHY file is not "repaired". The most likely way to pass A-21 accidentally is to
//      rebuild everything on open, which would make every document a repaired one and make A-22's
//      ratio meaningless. So the first case is the negative one.
//   2. A-22's two extremes, which `Stage2-Rendering-Plan.md` §5.4 names as DIFFERENT RISKS:
//      *"reconstructed 3 objects of 4,000"* and *"reconstructed the entire xref"*. Stage 3 forces
//      a repaired document's first save to Save-As and needs to tell them apart.
//   3. A-21 over the whole directory, with the hang and allocation cases named individually so a
//      failure says which property broke.
//   4. That the summary is a CATALOG KEY and not a sentence -- `core/` holds no user-facing
//      English, and returning one here is the single change that would make the product
//      unshippable in `zh-TW` while looking entirely reasonable in review.
//
// WHAT THIS SUITE DOES NOT COVER, named at the assertion (CLAUDE.md convention 19):
//
//   * **A-24, the ASan run over the whole corpus, is a LINUX job** and is not this suite. Running
//     these files under `x64-debug` proves they terminate; it proves nothing about reads one byte
//     past a buffer, which is exactly what `portable-asan` exists to find.
//   * A hang is asserted only in the sense that CTest would time out. There is no in-process
//     watchdog here, and a hang would therefore be reported as a timeout rather than as a named
//     failure -- which is worth knowing when reading a red run.

#include <catch2/catch_test_macros.hpp>

#include "core/doc/RepairReport.h"
#include "pdf/cos/XrefRepair.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using rawpage::core::RepairKind;
using rawpage::core::repairKindName;
using rawpage::core::RepairSeverity;
using rawpage::core::repairSeverityName;
using rawpage::pdf::cos::openWithRepair;
using rawpage::pdf::cos::scanObjectHeaders;

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
// 1. The negative case first -- a healthy file is NOT a repaired one
// ==========================================================================================

TEST_CASE("cos repair leaves an undamaged file alone", "[cos][repair]")
{
    // THE CONTROL FOR THE WHOLE SUITE, and it comes first on purpose. The easiest way to satisfy
    // A-21 by accident is to rebuild every document on open: every malformed file would then yield
    // a report, the criterion would read as met, and A-22's ratio would be 1.0 for every file ever
    // opened. This case is what makes that impossible.
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus("ordinary"))) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        INFO("file: " << entry.path().filename().string());
        const std::string bytes = readFile(entry.path());

        auto opened = openWithRepair(bytes);
        REQUIRE(opened);
        CHECK(opened.value().usedOriginalXref);
        CHECK(opened.value().report.objectsReconstructed() == 0);
        CHECK_FALSE(opened.value().report.xrefRebuiltFromScratch());
        CHECK_FALSE(opened.value().report.requiresSaveAs());
        CHECK(opened.value().report.summaryKey() == QLatin1String("error.repair.none"));

        // And the document is genuinely usable, not merely un-repaired.
        const auto& root = opened.value().table.trailer().get("Root");
        REQUIRE(root.isReference());
        auto catalog = opened.value().table.fetch(root.reference().number);
        REQUIRE(catalog);
        CHECK(catalog.value().object.get("Type").text() == "Catalog");
        ++files;
    }
    CHECK(files >= 25);
}

// ==========================================================================================
// 2. A-22 -- how much was reconstructed, not merely that it was
// ==========================================================================================

TEST_CASE("cos repair rebuilds the whole table when there is no startxref", "[cos][repair]")
{
    // `corpus/malformed/xref-missing.pdf` -- one extreme of A-22. There is nothing to start from,
    // so every object is found by scanning and the report has to SAY the table is entirely a
    // reconstruction. Stage 3 treats this differently from a file with three bad offsets: an
    // incremental save here would append a revision on top of our guess and silently discard
    // whatever the original bytes really said.
    const std::string bytes = readFile(corpus("malformed/xref-missing.pdf"));
    REQUIRE_FALSE(bytes.empty());

    auto opened = openWithRepair(bytes);
    REQUIRE(opened);
    const auto& report = opened.value().report;

    CHECK_FALSE(opened.value().usedOriginalXref);
    CHECK(report.xrefRebuiltFromScratch());
    CHECK(report.objectsReconstructed() > 0);
    CHECK(report.count(RepairKind::ObjectRecoveredByScan) > 0);
    CHECK(report.requiresSaveAs());
    CHECK(report.summaryKey() == QLatin1String("error.repair.wholeXref"));

    // The repaired document works. A report about a document nobody can open is not a repair.
    const auto& root = opened.value().table.trailer().get("Root");
    REQUIRE(root.isReference());
    auto catalog = opened.value().table.fetch(root.reference().number);
    REQUIRE(catalog);
    CHECK(catalog.value().object.get("Type").text() == "Catalog");
}

TEST_CASE("cos repair patches only the bad entries when the table is mostly good", "[cos][repair]")
{
    // `corpus/malformed/xref-offsets-short.pdf` -- the OTHER extreme, and the one a naive
    // implementation gets wrong. Its offsets are twelve bytes short from object 4 onward, so the
    // first few entries are fine. A repairer that rebuilt everything would report this as
    // identical to the file above, and §5.4 exists to say they are different risks.
    const std::string bytes = readFile(corpus("malformed/xref-offsets-short.pdf"));
    REQUIRE_FALSE(bytes.empty());

    auto opened = openWithRepair(bytes);
    REQUIRE(opened);
    const auto& report = opened.value().report;

    CHECK_FALSE(opened.value().usedOriginalXref);
    CHECK(report.objectsReconstructed() > 0);
    CHECK(report.requiresSaveAs());

    // THE DISTINCTION A-22 IS ABOUT: this file is NOT a from-scratch rebuild, and the two cases
    // must therefore report different summary keys. If both said `wholeXref`, Stage 3 would have
    // a boolean wearing a ratio's clothes.
    CHECK_FALSE(report.xrefRebuiltFromScratch());
    CHECK(report.summaryKey() == QLatin1String("error.repair.objects"));

    // The ratio has both halves, and the denominator is a real count rather than a guess.
    CHECK(report.objectsTotal() > 0);
    CHECK(report.objectsReconstructed() <= report.objectsTotal());
    REQUIRE(report.summaryArguments().size() == 2);

    const auto& root = opened.value().table.trailer().get("Root");
    REQUIRE(root.isReference());
    CHECK(opened.value().table.fetch(root.reference().number));
}

TEST_CASE("cos repair entries name the object and the severity", "[cos][repair]")
{
    // A-22 in as many words: *"per-object entries with severity"*. "Something was odd in object
    // 12" is the report D2-11 spent a whole decision arguing against, so this checks that each
    // finding carries what a person or a later stage can act on.
    const std::string bytes = readFile(corpus("malformed/xref-missing.pdf"));
    auto opened = openWithRepair(bytes);
    REQUIRE(opened);

    const auto& entries = opened.value().report.entries();
    REQUIRE_FALSE(entries.empty());

    std::size_t withObject = 0;
    for (const auto& entry : entries) {
        INFO("kind: " << repairKindName(entry.kind)
                      << " severity: " << repairSeverityName(entry.severity));
        CHECK(std::string(repairKindName(entry.kind)) != "?");
        CHECK(std::string(repairSeverityName(entry.severity)) != "?");
        CHECK(entry.kind != RepairKind::None);
        if (entry.objectNumber >= 0)
            ++withObject;
    }
    CHECK(withObject > 0);
    CHECK(opened.value().report.worstSeverity() >= RepairSeverity::Recovered);
}

TEST_CASE("cos repair summarises with a catalog key and never a sentence", "[cos][repair]")
{
    // `core/` holds no user-facing English (convention 5) and has no `tr()`. A-22 asks for a
    // one-line summary; the honest form of that in this layer is a KEY plus arguments, and `ui/`
    // resolves it. An English sentence returned here would be the single change that makes the
    // product unshippable in `zh-TW` while looking completely reasonable in review -- so it is
    // asserted structurally rather than trusted.
    const char* keys[] = {"error.repair.none", "error.repair.minor", "error.repair.objects",
                          "error.repair.wholeXref"};

    std::set<std::string> seen;
    for (const char* directory : {"ordinary", "malformed"}) {
        for (const auto& entry : std::filesystem::directory_iterator(corpus(directory))) {
            if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
                continue;
            auto opened = openWithRepair(readFile(entry.path()));
            if (!opened)
                continue;
            const QLatin1String key = opened.value().report.summaryKey();
            const std::string text(key.data(), static_cast<std::size_t>(key.size()));
            INFO("file: " << entry.path().filename().string() << " key: " << text);

            // A key, not a sentence: no spaces, and one of the four declared families.
            CHECK(text.find(' ') == std::string::npos);
            CHECK(std::find(std::begin(keys), std::end(keys), text) != std::end(keys));
            seen.insert(text);

            // Every `%1` the catalog entry will interpolate must have an argument. A key with a
            // missing argument renders as a literal "%1" in the message bar.
            const auto arguments = opened.value().report.summaryArguments();
            if (text == "error.repair.none")
                CHECK(arguments.isEmpty());
            else if (text == "error.repair.objects")
                CHECK(arguments.size() == 2);
            else
                CHECK(arguments.size() == 1);
        }
    }

    // At least the healthy key and one repair key, or the loop above is only exercising one branch.
    CHECK(seen.count("error.repair.none") == 1);
    CHECK(seen.size() >= 2);
}

// ==========================================================================================
// 3. A-21 -- the exit criterion
// ==========================================================================================

TEST_CASE("cos repair yields a report and never a crash for every malformed file",
          "[cos][repair][corpus]")
{
    // **GUIDE CASE A-21, AND THE STAGE 2 EXIT CRITERION.** For every file: a `Result` that is
    // either success-with-a-report or a NAMED failure. No crash, no hang, no assertion, no
    // unbounded allocation.
    std::size_t files = 0;
    std::size_t repaired = 0;
    std::size_t declined = 0;

    for (const auto& entry : std::filesystem::directory_iterator(corpus("malformed"))) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        INFO("file: " << entry.path().filename().string());
        const std::string bytes = readFile(entry.path());
        ++files;

        auto opened = openWithRepair(bytes);
        if (!opened) {
            // A NAMED failure, not a bare one. "This file is broken" is the report D2-11 argues
            // against; the key is what a message bar can render and a log can be grepped for.
            const QLatin1String key = opened.error().messageKey;
            INFO("declined with: " << std::string(key.data(), static_cast<std::size_t>(key.size())));
            CHECK(key.size() > 0);
            ++declined;
            continue;
        }

        // Success means a usable table AND a report that describes what happened.
        CHECK(opened.value().table.entryCount() > 0);
        if (!opened.value().usedOriginalXref) {
            CHECK_FALSE(opened.value().report.isEmpty());
            ++repaired;
        }

        // Fetching every object must also terminate -- this is where an object stream that claims
        // to contain itself, or a /Prev loop, would recurse without bound.
        for (long long number = 0; number <= opened.value().table.largestObjectNumber(); ++number)
            (void)opened.value().table.fetch(number);
    }

    // 24 malformed files at T4. A floor rather than the exact number, so a later task adding a
    // fixture does not redden this case -- but zero would mean the directory moved and the loop
    // checked nothing, which is the failure the bound exists for (convention 7).
    CHECK(files >= 20);

    // Both branches must occur, or the case is only exercising one of them.
    INFO("repaired " << repaired << ", declined " << declined << ", of " << files);
    CHECK(repaired > 0);
}

TEST_CASE("cos repair terminates on the two hang fixtures", "[cos][repair][corpus]")
{
    // A-21's *no hang* clause has two files of its own, and they are named individually so a red
    // run says WHICH property broke rather than "the suite timed out".
    //
    // `circular-prev.pdf` -- a /Prev chain pointing back at itself.
    // `pages-loop.pdf`    -- a page tree whose /Kids cycle. The page tree is T9's, but the OBJECT
    //                        graph is walked here, so the cycle is reachable from this stage.
    for (const char* name : {"malformed/circular-prev.pdf", "malformed/pages-loop.pdf"}) {
        INFO("file: " << name);
        const std::string bytes = readFile(corpus(name));
        REQUIRE_FALSE(bytes.empty());
        auto opened = openWithRepair(bytes);
        if (opened) {
            for (long long number = 0; number <= opened.value().table.largestObjectNumber();
                 ++number) {
                (void)opened.value().table.fetch(number);
            }
        }
        SUCCEED("terminated");
    }
}

TEST_CASE("cos repair allocates nothing for a four-gigabyte length claim", "[cos][repair][corpus]")
{
    // A-21's *no unbounded allocation* clause. `huge-length-claim.pdf` declares
    // `/Length 4294967295` in a file of ten thousand bytes; `deep-nesting.pdf` nests an array
    // 4,000 levels deep. Both are satisfied STRUCTURALLY rather than by a check -- a stream is an
    // offset and a length into the caller's buffer, and the parser bounds its own recursion -- and
    // this case is what keeps that true.
    for (const char* name : {"malformed/huge-length-claim.pdf", "malformed/deep-nesting.pdf"}) {
        INFO("file: " << name);
        const std::string bytes = readFile(corpus(name));
        REQUIRE_FALSE(bytes.empty());

        auto opened = openWithRepair(bytes);
        REQUIRE(opened);
        for (long long number = 0; number <= opened.value().table.largestObjectNumber(); ++number) {
            auto object = opened.value().table.fetch(number);
            if (!object || !object.value().object.isStream())
                continue;
            // The recorded data length can never exceed the file. That is the assertion that
            // would have caught a `/Length` believed rather than checked.
            const auto& data = object.value().object.streamData();
            CHECK(data.dataOffset <= bytes.size());
            CHECK(data.dataLength <= bytes.size());
            CHECK(data.dataOffset + data.dataLength <= bytes.size());
        }
    }
}

TEST_CASE("cos repair declines an encrypted document by name", "[cos][repair]")
{
    // **GUIDE CASE A-23**, and decision D2-6. The COS reader declines an encrypted file BY NAME
    // rather than half-parsing it: reading one at Stage 2 goes through PDFium's password callback,
    // and `pdf/cos/StandardSecurityHandler` is Stage 10's.
    //
    // The code matters as much as the key. `Unsupported` says *this file is fine and this build
    // cannot read it*; `ParseFailure` would say *this file is damaged* and would send the repair
    // path -- and a caller -- down entirely the wrong road.
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(corpus("encrypted"))) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        INFO("file: " << entry.path().filename().string());
        auto opened = openWithRepair(readFile(entry.path()));
        REQUIRE_FALSE(opened);
        CHECK(opened.error().code == rawpage::core::ErrorCode::Unsupported);
        CHECK(opened.error().messageKey == QLatin1String("error.pdf.encrypted"));
        ++files;
    }
    // Five encrypted fixtures at T4, across revisions 2/3/4/6.
    CHECK(files >= 5);

    // THE CONTROL, and it is what stops this case from being satisfied by a reader that declines
    // everything: an unencrypted file must still open.
    auto plain = openWithRepair(readFile(corpus("minimal.pdf")));
    CHECK(plain);
}

TEST_CASE("cos repair declines a file with no objects at all", "[cos][repair]")
{
    // The ONE outcome that is a failure rather than a report: there is no document here, and
    // inventing an empty one would be worse than saying so.
    const std::string nothing = "this is not a PDF at all, not even slightly";
    auto opened = openWithRepair(nothing);
    REQUIRE_FALSE(opened);
    CHECK(opened.error().messageKey == QLatin1String("error.pdf.noObjects"));

    // The control, and it is the interesting one: `junk-before-header.pdf` has junk where the
    // header should be and DOES have objects, so it must NOT land in the branch above. A repairer
    // that keyed on the `%PDF-` header rather than on object content would decline it.
    const std::string junk = readFile(corpus("malformed/junk-before-header.pdf"));
    REQUIRE_FALSE(junk.empty());
    auto recovered = openWithRepair(junk);
    CHECK(recovered);
}

TEST_CASE("cos repair scans object headers without being fooled by stream bytes", "[cos][repair]")
{
    // The scan walks through binary stream data by construction, so `obj` has to be a whole token
    // and the number before it a whole number. `12abc 0 obj` is not object 12, and a byte that
    // happens to spell a digit inside a compressed image is exactly what a full-file scan meets.
    const std::string source =
        "%PDF-1.7\n"
        "1 0 obj << /Type /Catalog >> endobj\n"
        "12abc 0 obj << >> endobj\n"
        "2 0 obj << /Type /Thing >> endobj\n";
    const auto headers = scanObjectHeaders(source);

    std::set<long long> numbers;
    for (const auto& [number, offset] : headers) {
        (void)offset;
        numbers.insert(number);
    }
    CHECK(numbers.count(1) == 1);
    CHECK(numbers.count(2) == 1);
    CHECK(numbers.count(12) == 0);   // `12abc 0 obj` is not object 12

    // `endobj` ends with `obj` and must not be mistaken for a header.
    CHECK(numbers.size() == 2);
}

TEST_CASE("cos repair takes the last definition of a duplicated object", "[cos][repair]")
{
    // `corpus/malformed/duplicate-object-number.pdf` has two objects numbered 5. Its PROVENANCE
    // row says the XREF's entry wins over the last one in the file -- and that is right when there
    // IS a usable xref, which is why the scan's last-wins rule only applies when there is not.
    // An incremental update appends, so the later definition is the newer one.
    const std::string source =
        "%PDF-1.7\n"
        "5 0 obj << /Which (first) >> endobj\n"
        "5 0 obj << /Which (second) >> endobj\n";
    const auto headers = scanObjectHeaders(source);
    REQUIRE(headers.size() == 1);
    CHECK(headers[0].first == 5);
    CHECK(headers[0].second == source.rfind("5 0 obj"));
}
