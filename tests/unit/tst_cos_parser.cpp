// tst_cos_parser -- guide cases A-16 and A-17. Stage 2 T6.
//
// WHAT THIS SUITE IS TRYING TO CATCH, in the order the cases appear:
//
//   1. The OBJECT MODEL: last-wins lookup over a duplicated key, the missing-versus-null
//      distinction, and semantic equality that ignores spans on purpose.
//   2. The ordinary grammar, which is easy and is not where parsers go wrong.
//   3. IDENTITY-PRESERVING ROUND-TRIP, which is the property `pdf/cos/CosObject` exists for and
//      the one Stage 3's byte-identical incremental save depends on. Asserted over every object
//      in every ordinary corpus file rather than over a handful of literals, because the failure
//      mode is a construct nobody thought to write down.
//   4. STREAMS and the `/Length` RECOVERY -- guide case A-17, with the control that proves the
//      recovery path was actually taken. A case that only looked at the damaged file would pass
//      just as well against a parser that always scans.
//   5. The TOLERANCES, each with a corpus file or a named producer behind it, and the depth bound
//      that keeps `corpus/malformed/deep-nesting.pdf` from being a stack overflow.
//   6. The CONTRACT the fuzzer depends on: progress, span containment, shallow trees -- replayed
//      over all 60 corpus files through `parserFuzzOnce`, the SAME function `fuzz_cos_parser`
//      wraps, so the entry point only `portable-fuzz` can link is still compiled and exercised.
//
// WHAT THIS SUITE IS NOT (CLAUDE.md convention 19 -- name the back door at the assertion):
//
//   * Replaying 60 known files is not fuzzing and finds nothing new by construction. A-26's ten
//     clean minutes is a claim about a Linux machine with clang, and neither case substitutes for
//     the other.
//   * The OBJECT STREAM cases run against hand-built payloads, not against the corpus, because
//     every object stream in `corpus/` is `/FlateDecode`d and Flate is T7. The last case in
//     section 4 asserts that gap rather than describing it, so it closes when T7 closes it.
//   * Nothing here resolves an object NUMBER to an offset. That is `pdf/cos/XrefTable` (T7), and
//     the `/Length 9 0 R` cases below use a hand-written resolver in its place.

#include <catch2/catch_test_macros.hpp>

#include "pdf/cos/CosObject.h"
#include "pdf/cos/Parser.h"
#include "tests/fuzz/ParserFuzzBody.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using rawpage::pdf::cos::CosObject;
using rawpage::pdf::cos::CosType;
using rawpage::pdf::cos::cosTypeName;
using rawpage::pdf::cos::IndirectObject;
using rawpage::pdf::cos::Parser;
using rawpage::pdf::cos::ParseNote;
using rawpage::pdf::cos::parseNoteName;
using rawpage::pdf::cos::StringForm;

namespace {

CosObject parseOne(std::string_view input)
{
    Parser parser(input);
    return parser.parseObject();
}

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

// The offset of `<n> 0 obj` in a file, found by its exact spelling with the newline that precedes
// it. A test helper and nothing more: finding object headers for real is `XrefTable`'s (T7) and
// `XrefRepair`'s (T8) job, and reimplementing either here would make this suite agree with itself
// rather than with the file.
std::size_t headerOffset(const std::string& file, const std::string& header)
{
    const std::string needle = "\n" + header;
    const std::size_t found = file.find(needle);
    REQUIRE(found != std::string::npos);
    return found + 1;
}

// Every `<digits> <digits> obj` header in a file, in order. Used only by the round-trip case,
// which needs to reach every object rather than a named one.
std::vector<std::size_t> allHeaderOffsets(const std::string& file)
{
    std::vector<std::size_t> offsets;
    for (std::size_t at = file.find(" obj"); at != std::string::npos;
         at = file.find(" obj", at + 4)) {
        // Back up over `<generation>` and `<number>`, both preceded by whitespace.
        std::size_t cursor = at;
        auto backOverDigits = [&file, &cursor]() {
            const std::size_t end = cursor;
            while (cursor > 0 && file[cursor - 1] >= '0' && file[cursor - 1] <= '9')
                --cursor;
            return cursor < end;
        };
        if (!backOverDigits())
            continue;
        if (cursor == 0 || file[cursor - 1] != ' ')
            continue;
        --cursor;
        if (!backOverDigits())
            continue;
        offsets.push_back(cursor);
    }
    return offsets;
}

} // namespace

// ==========================================================================================
// 1. The object model
// ==========================================================================================

TEST_CASE("cos parser dictionary lookup resolves a duplicated key to the last one", "[cos][parser]")
{
    // `corpus/malformed/` contains duplicated keys, and both copies are KEPT so the parser's view
    // of the file matches the bytes it will later rewrite. Lookup takes the last, which is what a
    // reader built on an insert-into-a-map loop does.
    Parser parser("<< /A 1 /B 2 /A 3 >>");
    const CosObject dictionary = parser.parseObject();

    REQUIRE(dictionary.type() == CosType::Dictionary);
    CHECK(dictionary.entries().size() == 3);
    CHECK(dictionary.get("A").integerValue() == 3);
    CHECK(dictionary.get("B").integerValue() == 2);
    CHECK(parser.noteCount(ParseNote::DuplicateDictionaryKey) == 1);

    // The control: an ordinary dictionary reports no duplicate, so the note above is a finding
    // rather than something this parser says about every file.
    Parser clean("<< /A 1 /B 2 >>");
    clean.parseObject();
    CHECK(clean.noteCount(ParseNote::DuplicateDictionaryKey) == 0);
}

TEST_CASE("cos parser distinguishes an absent key from a null one", "[cos][parser]")
{
    // `/Pages null` and no `/Pages` at all are different findings for T8's repair report, so the
    // model has to be able to tell them apart. `find` answers with a pointer for exactly that;
    // `get` collapses them on purpose, for chained lookups.
    const CosObject dictionary = parseOne("<< /Present null >>");
    REQUIRE(dictionary.find("Present") != nullptr);
    CHECK(dictionary.find("Present")->isNull());
    CHECK(dictionary.find("Absent") == nullptr);
    CHECK(dictionary.get("Absent").isNull());
    CHECK(dictionary.has("Present"));
    CHECK_FALSE(dictionary.has("Absent"));
}

TEST_CASE("cos parser object equality is semantic and ignores spans", "[cos][parser]")
{
    // Two objects that mean the same thing written in two places are equal. That is what makes
    // the round-trip case below a test of the SPAN rather than a tautology: byte equality is a
    // different question, and `raw()` is how it is asked.
    const CosObject left = parseOne("[ 1 2 /Three (four) ]");
    const CosObject right = parseOne("      [1 2/Three(four)]");
    CHECK(left == right);
    CHECK(left.span().offset != right.span().offset);

    // The string FORM is part of the value: `(A)` and `<41>` carry the same byte and are not
    // interchangeable to a rewriter.
    CHECK(parseOne("(A)").text() == parseOne("<41>").text());
    CHECK(parseOne("(A)") != parseOne("<41>"));
    CHECK(parseOne("(A)").stringForm() == StringForm::Literal);
    CHECK(parseOne("<41>").stringForm() == StringForm::Hex);

    // A constructed object has no span, and Stage 3 must read that as "serialize it properly"
    // rather than as "nothing to write".
    CHECK_FALSE(CosObject::makeInteger(7).span().valid);
    CHECK(parseOne("7").span().valid);

    // Changing an object clears its span, because the original bytes no longer describe it.
    CosObject array = parseOne("[1]");
    REQUIRE(array.span().valid);
    array.append(CosObject::makeInteger(2));
    CHECK_FALSE(array.span().valid);
}

// ==========================================================================================
// 2. The ordinary grammar
// ==========================================================================================

TEST_CASE("cos parser reads every direct object kind", "[cos][parser]")
{
    CHECK(parseOne("null").type() == CosType::Null);
    CHECK(parseOne("true").booleanValue() == true);
    CHECK(parseOne("false").booleanValue(true) == false);
    CHECK(parseOne("42").integerValue() == 42);
    CHECK(parseOne("-7").integerValue() == -7);
    CHECK(parseOne("4.5").numberValue() == 4.5);
    CHECK(parseOne("(text)").text() == "text");
    CHECK(parseOne("<414243>").text() == "ABC");
    CHECK(parseOne("/Type").text() == "Type");

    // An Integer answers `numberValue()` too, so a caller reading a numeric field does not have
    // to switch on the type first -- which is where a `/Width 100` versus `/Width 100.0` bug
    // would otherwise live.
    CHECK(parseOne("100").numberValue() == 100.0);
}

TEST_CASE("cos parser reads an indirect reference and not two integers", "[cos][parser]")
{
    const CosObject reference = parseOne("12 0 R");
    REQUIRE(reference.type() == CosType::Reference);
    CHECK(reference.reference().number == 12);
    CHECK(reference.reference().generation == 0);

    // The control, and the reason this needs two tokens of lookahead: the same leading integers
    // in an array are three separate numbers, and a parser that guessed would eat them.
    const CosObject array = parseOne("[ 12 0 5 ]");
    REQUIRE(array.type() == CosType::Array);
    REQUIRE(array.items().size() == 3);
    CHECK(array.items()[0].integerValue() == 12);
    CHECK(array.items()[2].integerValue() == 5);

    // And the mixed case, which is what a real `/Kids` array looks like.
    const CosObject kids = parseOne("[ 3 0 R 4 0 R 99 ]");
    REQUIRE(kids.items().size() == 3);
    CHECK(kids.items()[0].reference().number == 3);
    CHECK(kids.items()[1].reference().number == 4);
    CHECK(kids.items()[2].integerValue() == 99);
}

TEST_CASE("cos parser reads nested containers", "[cos][parser]")
{
    const CosObject page = parseOne(
        "<< /Type /Page /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> >>");

    REQUIRE(page.type() == CosType::Dictionary);
    CHECK(page.get("Type").text() == "Page");
    REQUIRE(page.get("MediaBox").items().size() == 4);
    CHECK(page.get("MediaBox").items()[3].numberValue() == 792.0);
    CHECK(page.get("Resources").get("Font").get("F1").reference().number == 5);
}

TEST_CASE("cos parser reads consecutive objects and reports its position", "[cos][parser]")
{
    Parser parser("1 [2] /Three");
    CHECK(parser.parseObject().integerValue() == 1);
    CHECK(parser.parseObject().items().size() == 1);
    CHECK(parser.parseObject().text() == "Three");
    CHECK(parser.atEnd());

    // `seek` clears the lookahead, which is the thing a rewind can get wrong: a pushed-back token
    // from before the seek would be handed out afterwards as if it were still current.
    parser.seek(0);
    CHECK_FALSE(parser.atEnd());
    CHECK(parser.parseObject().integerValue() == 1);
}

// ==========================================================================================
// 3. Identity-preserving round-trip -- the property this layer exists for
// ==========================================================================================

TEST_CASE("cos parser spans reproduce the exact source spelling", "[cos][parser][roundtrip]")
{
    // Two spellings that decode identically and are not the same bytes. Stage 3 copies the bytes.
    const std::string source = "[ (a\\053b) (a+b) 4. 4.0 ]";
    const CosObject array = parseOne(source);

    REQUIRE(array.items().size() == 4);
    CHECK(array.items()[0].text() == array.items()[1].text());
    CHECK(array.items()[0].raw(source) == "(a\\053b)");
    CHECK(array.items()[1].raw(source) == "(a+b)");
    CHECK(array.items()[2].numberValue() == array.items()[3].numberValue());
    CHECK(array.items()[2].raw(source) == "4.");
    CHECK(array.items()[3].raw(source) == "4.0");
    CHECK(array.raw(source) == source.substr(0, source.size()));
}

TEST_CASE("cos parser round-trips every object in every ordinary corpus file",
          "[cos][parser][roundtrip]")
{
    // THE case for guide A-16's "identity-preserving round-trip of anything we do not interpret".
    // Over the corpus rather than over literals, because the failure mode is a construct nobody
    // thought to write down -- and the assertion is the strong one: the bytes the span names,
    // re-parsed on their own, must produce an EQUAL object.
    //
    // What it does NOT prove (convention 19): that Stage 3 will actually copy those bytes. This
    // establishes that the bytes are right and reachable; the writer is Stage 3's own case.
    const std::filesystem::path root = corpus("ordinary");
    REQUIRE(std::filesystem::is_directory(root));

    std::size_t files = 0;
    std::size_t objects = 0;
    std::size_t streams = 0;

    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        const std::string bytes = readFile(entry.path());
        ++files;

        Parser parser(bytes);
        for (const std::size_t offset : allHeaderOffsets(bytes)) {
            const auto result = parser.parseIndirectObjectAt(offset);
            if (!result)
                continue;
            const IndirectObject& indirect = result.value();
            ++objects;
            if (indirect.object.isStream())
                ++streams;

            const std::string_view raw = indirect.object.raw(bytes);
            INFO("file: " << entry.path().filename().string());
            INFO("object: " << indirect.number << " " << indirect.generation);
            INFO("type: " << cosTypeName(indirect.object.type()));
            REQUIRE_FALSE(raw.empty());

            Parser again(raw);
            const CosObject reparsed = again.parseObject();
            REQUIRE(reparsed == indirect.object);
        }
    }

    // Floors rather than exact numbers: this case is about the round-trip and must not go red
    // because T7 adds a corpus file. Zero would mean the loop checked nothing, which is the
    // failure the bound exists for (convention 7).
    CHECK(files >= 25);
    CHECK(objects >= 200);
    CHECK(streams >= 20);
}

// ==========================================================================================
// 4. Streams, and the /Length recovery -- guide case A-17
// ==========================================================================================

TEST_CASE("cos parser reads a stream and keeps its data as a span", "[cos][parser][stream]")
{
    const std::string source = "<< /Length 5 >>\nstream\nHELLO\nendstream";
    Parser parser(source);
    const CosObject stream = parser.parseObject();

    REQUIRE(stream.type() == CosType::Stream);
    CHECK(stream.isDictionary());  // a stream IS a dictionary with data attached
    CHECK(stream.get("Length").integerValue() == 5);
    CHECK(stream.rawStreamData(source) == "HELLO");
    CHECK(stream.streamData().declaredLength == 5);
    CHECK_FALSE(stream.streamData().lengthRecovered);
    CHECK(parser.noteCount(ParseNote::StreamLengthRecovered) == 0);

    // The span covers the WHOLE construct, `<<` through `endstream`, because that is what Stage 3
    // copies for an untouched stream.
    CHECK(stream.raw(source) == source);
}

TEST_CASE("cos parser accepts CRLF and LF and a non-conforming lone CR after `stream`",
          "[cos][parser][stream]")
{
    // ISO 32000-1 7.3.8.1 allows CRLF and LF and explicitly forbids a lone CR. Producers emit all
    // three. Getting this wrong shifts every stream in the file by one byte, which surfaces as
    // decode corruption rather than as a structural error -- so all three are read and the two
    // non-conforming spellings are reported.
    {
        const std::string crlf = "<< /Length 2 >>\r\nstream\r\nAB\r\nendstream";
        Parser parser(crlf);
        CHECK(parser.parseObject().rawStreamData(crlf) == "AB");
        CHECK(parser.noteCount(ParseNote::StreamKeywordEolMissing) == 0);
    }
    {
        const std::string cr = "<< /Length 2 >>\nstream\rAB\nendstream";
        Parser parser(cr);
        CHECK(parser.parseObject().rawStreamData(cr) == "AB");
        CHECK(parser.noteCount(ParseNote::StreamKeywordEolMissing) == 1);
    }
}

TEST_CASE("cos parser recovers a stream whose /Length lies -- and the control proves the path ran",
          "[cos][parser][stream]")
{
    // GUIDE CASE A-17, whole. `corpus/malformed/length-lies-short.pdf` and
    // `corpus/ordinary/stream-length-control.pdf` differ in exactly three bytes -- 4497 against
    // 4509 -- and the control is what makes the assertion mean something: it must parse WITHOUT
    // the recovery path running, or this case would pass against a parser that always scans.
    const std::string control = readFile(corpus("ordinary/stream-length-control.pdf"));
    const std::string damaged = readFile(corpus("malformed/length-lies-short.pdf"));
    REQUIRE(control.size() == damaged.size());

    Parser controlParser(control);
    const auto controlResult = controlParser.parseIndirectObjectAt(
        headerOffset(control, "3 0 obj"));
    REQUIRE(controlResult);
    const CosObject& controlStream = controlResult.value().object;
    REQUIRE(controlStream.isStream());

    Parser damagedParser(damaged);
    const auto damagedResult = damagedParser.parseIndirectObjectAt(
        headerOffset(damaged, "3 0 obj"));
    REQUIRE(damagedResult);
    const CosObject& damagedStream = damagedResult.value().object;
    REQUIRE(damagedStream.isStream());

    // 1. The damaged file recovers to the same content as the correct one.
    CHECK(damagedStream.rawStreamData(damaged) == controlStream.rawStreamData(control));
    CHECK(damagedStream.streamData().dataLength == 4509);

    // 2. It SAYS it recovered, and says what the file claimed -- T8's report needs both numbers.
    CHECK(damagedStream.streamData().lengthRecovered);
    CHECK(damagedStream.streamData().declaredLength == 4497);
    CHECK(damagedParser.noteCount(ParseNote::StreamLengthRecovered) == 1);

    // 3. THE CONTROL. The correct file takes the declared length and never scans.
    CHECK_FALSE(controlStream.streamData().lengthRecovered);
    CHECK(controlStream.streamData().declaredLength == 4509);
    CHECK(controlParser.noteCount(ParseNote::StreamLengthRecovered) == 0);
}

TEST_CASE("cos parser recovers a /Length that is too long as well as too short",
          "[cos][parser][stream]")
{
    // `corpus/malformed/length-lies-long.pdf` -- the mirror image, and a genuinely different
    // repair: too short leaves data behind, too long consumes the structure after the stream.
    // A parser that only checked `declared > available` would accept this one silently, because
    // 400 bytes too long still fits inside the file.
    const std::string bytes = readFile(corpus("malformed/length-lies-long.pdf"));
    Parser parser(bytes);
    const auto result = parser.parseIndirectObjectAt(headerOffset(bytes, "3 0 obj"));
    REQUIRE(result);

    const CosObject& stream = result.value().object;
    REQUIRE(stream.isStream());
    CHECK(stream.streamData().lengthRecovered);
    CHECK(stream.streamData().dataLength == 4509);
    CHECK(stream.streamData().declaredLength > 4509);
}

TEST_CASE("cos parser allocates nothing for a /Length of four gigabytes", "[cos][parser][stream]")
{
    // `corpus/malformed/huge-length-claim.pdf`: `/Length 4294967295` in a file of ten thousand
    // bytes. Guide case A-21's "no unbounded allocation" clause, and it is satisfied structurally
    // rather than by a check -- a stream is an offset and a length into the caller's buffer, so
    // there is nothing here that COULD allocate four gigabytes.
    const std::string bytes = readFile(corpus("malformed/huge-length-claim.pdf"));
    Parser parser(bytes);
    const auto result = parser.parseIndirectObjectAt(headerOffset(bytes, "3 0 obj"));
    REQUIRE(result);

    const CosObject& stream = result.value().object;
    REQUIRE(stream.isStream());
    CHECK(stream.streamData().declaredLength == 4294967295LL);
    CHECK(stream.streamData().lengthRecovered);
    CHECK(stream.streamData().dataLength < bytes.size());
    CHECK(stream.rawStreamData(bytes).size() == stream.streamData().dataLength);
}

TEST_CASE("cos parser runs a stream with no endstream to the end and says so", "[cos][parser][stream]")
{
    // `corpus/malformed/truncated-mid-stream.pdf` -- an interrupted download, which is the single
    // most common damaged file a reader is handed. There is nothing to recover TO, so the honest
    // answer is "the data is the rest of the file" plus a note that says why.
    const std::string bytes = readFile(corpus("malformed/truncated-mid-stream.pdf"));
    Parser parser(bytes);
    const auto result = parser.parseIndirectObjectAt(headerOffset(bytes, "4 0 obj"));
    REQUIRE(result);

    const CosObject& stream = result.value().object;
    REQUIRE(stream.isStream());
    CHECK(parser.noteCount(ParseNote::StreamEndstreamMissing) == 1);
    CHECK(stream.streamData().dataOffset + stream.streamData().dataLength == bytes.size());
}

TEST_CASE("cos parser resolves an indirect /Length when a resolver is supplied and scans when not",
          "[cos][parser][stream]")
{
    // `/Length 9 0 R` is ordinary in real files. Resolving an object number to a value is
    // `XrefTable`'s (T7), so the parser takes a callback -- which is also what makes this case
    // possible before any xref exists.
    const std::string source = "<< /Length 9 0 R >>\nstream\nHELLO\nendstream";

    Parser resolving(source);
    resolving.setLengthResolver([](long long number, long long generation) {
        REQUIRE(number == 9);
        REQUIRE(generation == 0);
        return std::optional<long long>(5);
    });
    const CosObject resolved = resolving.parseObject();
    CHECK(resolved.rawStreamData(source) == "HELLO");
    CHECK_FALSE(resolved.streamData().lengthRecovered);
    CHECK(resolving.noteCount(ParseNote::StreamLengthUnresolved) == 0);

    // No resolver: the reader genuinely does not know, which is a report rather than a failure.
    // The content still comes out right, by scanning.
    Parser scanning(source);
    const CosObject scanned = scanning.parseObject();
    CHECK(scanned.rawStreamData(source) == "HELLO");
    CHECK(scanned.streamData().lengthRecovered);
    CHECK(scanning.noteCount(ParseNote::StreamLengthUnresolved) == 1);

    // And a resolver that declines -- a damaged file naming an object that is not there.
    Parser declining(source);
    declining.setLengthResolver([](long long, long long) { return std::optional<long long>(); });
    const CosObject declined = declining.parseObject();
    CHECK(declined.rawStreamData(source) == "HELLO");
    CHECK(declining.noteCount(ParseNote::StreamLengthUnresolved) == 1);
}

TEST_CASE("cos parser names a missing or non-integer /Length rather than guessing", "[cos][parser][stream]")
{
    {
        const std::string source = "<< /Type /X >>\nstream\nHELLO\nendstream";
        Parser parser(source);
        CHECK(parser.parseObject().rawStreamData(source) == "HELLO");
        CHECK(parser.noteCount(ParseNote::StreamLengthMissing) == 1);
    }
    {
        const std::string source = "<< /Length /Five >>\nstream\nHELLO\nendstream";
        Parser parser(source);
        CHECK(parser.parseObject().rawStreamData(source) == "HELLO");
        CHECK(parser.noteCount(ParseNote::StreamLengthNotAnInteger) == 1);
    }
}

TEST_CASE("cos parser reads an object stream's pair table and its packed objects",
          "[cos][parser][objstm]")
{
    // ISO 32000-1 7.5.7. `/N` pairs of `objectNumber relativeOffset`, then the objects at
    // `/First + relativeOffset`.
    //
    // THE PAYLOAD IS HAND-BUILT, NOT FROM THE CORPUS, and the case below says why: every object
    // stream in `corpus/` is `/FlateDecode`d and Flate is T7. This is the shape the bytes have
    // AFTER that filter runs, which is exactly what T7 will feed in.
    const std::string payload = "1 0 2 3 42 /Name";
    const auto packed = Parser::parseObjectStream(payload, 2, 8);
    REQUIRE(packed);
    REQUIRE(packed.value().size() == 2);

    CHECK(packed.value()[0].number == 1);
    CHECK(packed.value()[0].generation == 0);  // zero by definition -- 7.5.7 forbids anything else
    CHECK(packed.value()[0].object.integerValue() == 42);
    CHECK(packed.value()[1].number == 2);
    CHECK(packed.value()[1].object.text() == "Name");
}

TEST_CASE("cos parser reports an object stream with a wrong /First rather than crashing", "[cos][parser][objstm]")
{
    // The shape of `corpus/malformed/objstm-corrupt.pdf`, whose `/First` is 8 bytes wrong so every
    // packed object is read from the wrong place. Nothing in the classic-xref half of the reader
    // touches this path, which is why it needs its own case.
    const std::string payload = "1 0 2 3 42 /Name";
    std::vector<rawpage::pdf::cos::ParseNoteRecord> notes;
    const auto shifted = Parser::parseObjectStream(payload, 2, 14, &notes);
    REQUIRE(shifted);  // a report, not a failure
    CHECK(shifted.value().size() <= 2);

    // A `/N` that could not possibly fit in the payload is clamped and reported, not reserved.
    // `/N 4294967295` over sixteen bytes is the allocation this guard exists for.
    std::vector<rawpage::pdf::cos::ParseNoteRecord> huge;
    const auto clamped = Parser::parseObjectStream(payload, 4294967295LL, 8, &huge);
    REQUIRE(clamped);
    CHECK(clamped.value().size() <= 4);
    bool sawCountNote = false;
    for (const auto& record : huge)
        sawCountNote = sawCountNote || record.note == ParseNote::ObjectStreamCountBad;
    CHECK(sawCountNote);

    // A `/First` past the end of the payload is the one thing here that IS a failure: there is no
    // object region at all, so there is nothing to report on.
    CHECK_FALSE(Parser::parseObjectStream(payload, 2, 9999));
    CHECK_FALSE(Parser::parseObjectStream(payload, -1, 0));
}

TEST_CASE("cos parser cannot reach the corpus object streams until T7 supplies Flate",
          "[cos][parser][objstm]")
{
    // CONVENTION 19, asserted rather than written in a comment. The two cases above run against
    // hand-built payloads; this one establishes WHY, so that the day T7's Flate filter lands and
    // the corpus becomes reachable, the gap is visible here rather than remembered.
    const std::string bytes = readFile(corpus("ordinary/objstm-xrefstream.pdf"));
    Parser parser(bytes);
    const auto result = parser.parseIndirectObjectAt(headerOffset(bytes, "13 0 obj"));
    REQUIRE(result);

    const CosObject& stream = result.value().object;
    REQUIRE(stream.isStream());
    CHECK(stream.get("Type").text() == "ObjStm");
    CHECK(stream.get("N").integerValue() > 0);
    CHECK(stream.get("First").integerValue() > 0);
    // The reason the payload cannot be handed to `parseObjectStream` yet.
    CHECK(stream.get("Filter").text() == "FlateDecode");
}

// ==========================================================================================
// 5. The tolerances
// ==========================================================================================

TEST_CASE("cos parser reads an indirect object and tolerates a missing endobj",
          "[cos][parser][tolerance]")
{
    {
        Parser parser("7 0 obj << /A 1 >> endobj");
        const auto result = parser.parseIndirectObject();
        REQUIRE(result);
        CHECK(result.value().number == 7);
        CHECK(result.value().generation == 0);
        CHECK(result.value().object.get("A").integerValue() == 1);
        CHECK(result.value().span.valid);
        CHECK(parser.noteCount(ParseNote::MissingEndobj) == 0);
    }
    {
        // `corpus/malformed/missing-endobj.pdf`. A parser that required `endobj` would swallow the
        // next object; a tolerant one notices the header and reports the gap.
        Parser parser("7 0 obj << /A 1 >> 8 0 obj 2 endobj");
        const auto first = parser.parseIndirectObject();
        REQUIRE(first);
        CHECK(parser.noteCount(ParseNote::MissingEndobj) == 1);
        const auto second = parser.parseIndirectObject();
        REQUIRE(second);
        CHECK(second.value().number == 8);
        CHECK(second.value().object.integerValue() == 2);
    }
}

TEST_CASE("cos parser names a failed object header and leaves the position untouched",
          "[cos][parser][tolerance]")
{
    // T7's xref and T8's repair scan both probe offsets they are not sure about. A failed probe
    // that also moved the cursor is how a repair pass loses its place, so the rewind is part of
    // the contract rather than an implementation detail.
    Parser parser("/NotAnObjectHeader 1 2 3");
    const auto result = parser.parseIndirectObject();
    REQUIRE_FALSE(result);
    CHECK(result.error().code == rawpage::core::ErrorCode::ParseFailure);
    CHECK(result.error().messageKey == QLatin1String("error.pdf.badObjectHeader"));
    CHECK(parser.position() == 0);

    // `1 2 3` is three integers and not a header either -- the keyword has to be `obj`.
    Parser numbers("1 2 3");
    CHECK_FALSE(numbers.parseIndirectObject());
    CHECK(numbers.position() == 0);
}

TEST_CASE("cos parser closes and reports an unterminated container", "[cos][parser][tolerance]")
{
    {
        Parser parser("[1 2 3");
        const CosObject array = parser.parseObject();
        REQUIRE(array.type() == CosType::Array);
        CHECK(array.items().size() == 3);
        CHECK(parser.noteCount(ParseNote::UnterminatedArray) == 1);
    }
    {
        // `corpus/malformed/unbalanced-dictionary.pdf`: a page dictionary's closing `>>` is gone.
        // A parser with no bound reads the rest of the file as one dictionary; this one does too,
        // and SAYS so, which is what lets T8 report it.
        Parser parser("<< /Type /Page /Parent 2 0 R");
        const CosObject dictionary = parser.parseObject();
        REQUIRE(dictionary.type() == CosType::Dictionary);
        CHECK(dictionary.get("Type").text() == "Page");
        CHECK(parser.noteCount(ParseNote::UnterminatedDictionary) == 1);
    }
}

TEST_CASE("cos parser skips and reports a dictionary key that is not a name", "[cos][parser][tolerance]")
{
    Parser parser("<< 1 2 /A 3 >>");
    const CosObject dictionary = parser.parseObject();
    // The tolerant reading: `/A` still comes out. A parser that gave up here would lose a whole
    // page dictionary over one stray token.
    CHECK(dictionary.get("A").integerValue() == 3);
    CHECK(parser.noteCount(ParseNote::DictionaryKeyNotAName) >= 1);

    Parser dangling("<< /A >>");
    const CosObject withoutValue = dangling.parseObject();
    CHECK(withoutValue.entries().empty());
    CHECK(dangling.noteCount(ParseNote::DictionaryValueMissing) == 1);
}

TEST_CASE("cos parser refuses 4000 levels of nesting by name rather than by stack overflow",
          "[cos][parser][tolerance]")
{
    // `corpus/malformed/deep-nesting.pdf`: an array nested 4,000 deep, which is LEGAL PDF -- ISO
    // 32000-1 sets no depth limit -- and fatal to an unguarded recursive-descent parser. A stack
    // overflow is a crash, and guide case A-21 forbids crashes on that directory, so `kMaxDepth`
    // is a REFUSAL reported by name.
    const std::string bytes = readFile(corpus("malformed/deep-nesting.pdf"));
    Parser parser(bytes);
    const auto result = parser.parseIndirectObjectAt(headerOffset(bytes, "8 0 obj"));
    REQUIRE(result);
    CHECK(result.value().object.get("Type").text() == "CorpusDepthProbe");
    CHECK(parser.noteCount(ParseNote::DepthLimitExceeded) >= 1);

    // AND IT RESUMES CORRECTLY. This is the half a bare depth check gets wrong: the 3,872 levels
    // past the limit are consumed iteratively, so the object still ends where it ends and the next
    // one is still readable. If the skip were abandoned instead, `endobj` would be lost and every
    // object after this one would be misread.
    CHECK(parser.noteCount(ParseNote::MissingEndobj) == 0);

    // The control: an ordinary nesting depth is not refused, so the note above is a finding about
    // this file rather than something the parser says about every array.
    Parser shallow("[[[[[[[[ 1 ]]]]]]]]");
    shallow.parseObject();
    CHECK(shallow.noteCount(ParseNote::DepthLimitExceeded) == 0);
}

TEST_CASE("cos parser carries a lexer note into its report exactly once",
          "[cos][parser][tolerance]")
{
    // `pdf/cos/Lexer` records tolerances of its own, and T8's repair report needs all of them in
    // one place. `<41A>` is an odd hex digit count, which the lexer pads and notes.
    Parser parser("[ <41A> ]");
    parser.parseObject();
    CHECK(parser.noteCount(ParseNote::LexerNote) == 1);

    // THE "EXACTLY ONCE" HALF, and it needs the shape that drives the lookahead: two integers
    // followed by something that is not `R`. `--5` is a doubled sign, which the lexer reads as -5
    // and notes, so a parser that REWOUND AND RE-LEXED instead of pushing the tokens back would
    // count the same defect three times and make the census say the file was worse than it is.
    // Verified as a negative control: replacing the pushback with a rewind turns this 2 into a 3.
    Parser lookahead("--5 --5 /X");
    lookahead.parseObject();
    lookahead.parseObject();
    lookahead.parseObject();
    CHECK(lookahead.noteCount(ParseNote::LexerNote) == 2);
}

TEST_CASE("cos parser note names are stable and complete", "[cos][parser]")
{
    // T8's RepairReport prints these, and a "?" in a repair report is a bug report nobody can act
    // on. Same reasoning as `tst_cos_lexer`'s equivalent case.
    CHECK(std::string(parseNoteName(ParseNote::None)) == "None");
    CHECK(std::string(parseNoteName(ParseNote::StreamLengthRecovered)) == "StreamLengthRecovered");
    for (int value = 0; value <= static_cast<int>(ParseNote::LexerNote); ++value)
        CHECK(std::string(parseNoteName(static_cast<ParseNote>(value))) != "?");
    for (int value = 0; value <= static_cast<int>(CosType::Reference); ++value)
        CHECK(std::string(cosTypeName(static_cast<CosType>(value))) != "?");
}

// ==========================================================================================
// 6. The contract the fuzzer depends on
// ==========================================================================================

TEST_CASE("cos parser holds its invariants on adversarial input", "[cos][parser][fuzz]")
{
    // Hand-picked shapes rather than random bytes: each is a boundary the implementation has a
    // specific guard for, so a regression names itself. Random bytes are `fuzz_cos_parser`'s job.
    std::vector<std::string> inputs = {
        "", " ", "0 0 obj", "0 0 R", "R", "obj", "endobj", "stream", "endstream",
        "<<", ">>", "[", "]", "<< /A", "<< /A /B", "[[[[[[", "]]]]]]",
        "1 0 obj << /Length 4294967295 >> stream",
        "1 0 obj << /Length -1 >> stream\nx\nendstream endobj",
        "1 0 obj << /Length 1 0 R >> stream",
        "<< /Length 0 >>\nstream\nendstream",
        "<< /Length 99999 >>\nstream\n",
        "0 0 R 0 0 R 0 0 R", "1 2", "1 2 3 R", "1 R", "{ 1 2 add }",
        std::string("<< /A \0 >>", 10),
        std::string("1 0 obj\0\0\0endobj", 16),
    };
    // 200 levels: past kMaxDepth, so the refusal path and its iterative skip are exercised by this
    // case as well as by the corpus file.
    inputs.push_back(std::string(200, '[') + "1" + std::string(200, ']'));
    inputs.push_back(std::string(300, '['));

    for (const std::string& input : inputs) {
        const auto verdict = rawpage::fuzz::parserFuzzOnce(
            reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
        INFO("input: " << input.size() << " bytes");
        INFO("failure: " << verdict.failure);
        CHECK(verdict.ok);
    }
}

TEST_CASE("cos parser survives a null buffer and a zero length", "[cos][parser][fuzz]")
{
    // libFuzzer passes size 0 on its first iteration, every time.
    Parser empty(nullptr, 0);
    CHECK(empty.atEnd());
    CHECK(empty.parseObject().isNull());
    CHECK_FALSE(empty.parseIndirectObject());

    // A null pointer with a non-zero length is a caller bug; the promise is that we never read out
    // of bounds, and this is the one case where the bound itself is the lie.
    Parser lying(nullptr, 64);
    CHECK(lying.size() == 0);
    CHECK(lying.parseObject().isNull());
}

TEST_CASE("cos parser holds its invariants over the whole corpus", "[cos][parser][fuzz]")
{
    // The seed corpus `fuzz_cos_parser` starts from, replayed through the SAME function the fuzz
    // entry point wraps. This is what keeps that entry point compiling and honest between Linux
    // runs -- and it is NOT fuzzing: 60 known files find nothing new by construction, and A-26's
    // ten clean minutes on `portable-fuzz` is a separate claim this case does not make.
    const std::filesystem::path root(RAWPAGE_CORPUS_DIR);
    REQUIRE(std::filesystem::is_directory(root));

    std::size_t files = 0;
    std::size_t withNotes = 0;
    std::size_t deepest = 0;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        const std::string bytes = readFile(entry.path());
        const auto verdict = rawpage::fuzz::parserFuzzOnce(
            reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
        INFO("file: " << entry.path().filename().string());
        INFO("failure: " << verdict.failure);
        REQUIRE(verdict.ok);
        ++files;
        if (verdict.notes > 0)
            ++withNotes;
        if (verdict.maxDepth > deepest)
            deepest = verdict.maxDepth;
    }

    // A floor rather than the exact number, so T7 adding a corpus file does not redden a case
    // about the parser. Zero would mean the directory moved and the loop checked nothing, which is
    // the failure the bound exists for (convention 7).
    CHECK(files >= 55);

    // The control for the whole case: some corpus file must have needed tolerance, or these inputs
    // are not exercising the notes at all and a parser with every tolerance removed would pass
    // this case unchanged.
    CHECK(withNotes > 0);

    // And the depth bound held over 4,000 levels of real input.
    CHECK(deepest <= static_cast<std::size_t>(Parser::kMaxDepth) + 2);
}
