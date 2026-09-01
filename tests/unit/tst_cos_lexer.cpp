// tst_cos_lexer -- guide case A-15. Stage 2 T5.
//
// WHAT THIS SUITE IS TRYING TO CATCH, in the order the cases appear:
//
//   1. The ordinary grammar, which is easy and is not where lexers go wrong.
//   2. The TOLERANCES. `pdf/cos/Lexer` is "tolerant by design" per the plan, and tolerance is a
//      behaviour with a specification, not a shrug -- `--5` reads as -5, `1.2.3` still yields a
//      number, `/A#GG` keeps its '#', `<41A>` pads to `<41A0>`. Each of those has a corpus file or
//      a real-world producer behind it, named at the assertion.
//   3. The CONTRACT the fuzzer depends on: progress, bounds, End stability, peek/next agreement.
//      These run over `corpus/malformed/` and over a table of adversarial byte strings through
//      `lexerFuzzOnce` -- the SAME function `fuzz_cos_lexer` wraps, so the entry point that only
//      `portable-fuzz` can link is still compiled and exercised here.
//
// A NOTE ON WHAT THIS SUITE IS NOT. Replaying 24 known files is not fuzzing and finds nothing new
// by construction (CLAUDE.md convention 19: say what the back door does not cover). A-25's ten
// clean minutes on `portable-fuzz` is a separate claim on a machine this one is not, and neither
// case substitutes for the other.

#include <catch2/catch_test_macros.hpp>

#include "pdf/cos/Lexer.h"
#include "tests/fuzz/LexerFuzzBody.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using rawpage::pdf::cos::LexNote;
using rawpage::pdf::cos::Lexer;
using rawpage::pdf::cos::lexNoteName;
using rawpage::pdf::cos::Token;
using rawpage::pdf::cos::TokenType;
using rawpage::pdf::cos::tokenTypeName;

namespace {

std::vector<Token> lexAll(std::string_view input)
{
    Lexer lexer(input);
    std::vector<Token> tokens;
    for (;;) {
        Token token = lexer.next();
        if (token.isEnd())
            break;
        tokens.push_back(std::move(token));
    }
    return tokens;
}

Token lexOne(std::string_view input)
{
    Lexer lexer(input);
    return lexer.next();
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(stream)),
                                     std::istreambuf_iterator<char>());
}

} // namespace

// ==========================================================================================
// 1. The ordinary grammar
// ==========================================================================================

TEST_CASE("cos lexer reads the delimiters", "[cos][lexer]")
{
    const auto tokens = lexAll("[ ] << >> { }");
    REQUIRE(tokens.size() == 6);
    CHECK(tokens[0].type == TokenType::ArrayOpen);
    CHECK(tokens[1].type == TokenType::ArrayClose);
    CHECK(tokens[2].type == TokenType::DictOpen);
    CHECK(tokens[3].type == TokenType::DictClose);
    CHECK(tokens[4].type == TokenType::BraceOpen);
    CHECK(tokens[5].type == TokenType::BraceClose);

    // `<<` must beat `<hex>`. Without the two-byte lookahead every dictionary in every PDF lexes
    // as an empty hex string followed by junk -- and an empty hex string is legal, so nothing
    // would report an error.
    const auto dict = lexAll("<</A 1>>");
    REQUIRE(dict.size() == 4);
    CHECK(dict[0].type == TokenType::DictOpen);
    CHECK(dict[3].type == TokenType::DictClose);
}

TEST_CASE("cos lexer treats NUL as whitespace", "[cos][lexer]")
{
    // ISO 32000-1 table 1. A file padded with NULs between objects is well-formed, and treating
    // them as regular characters glues the padding onto the following keyword.
    const std::string input("1\0\0obj", 6);
    const auto tokens = lexAll(input);
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].type == TokenType::Integer);
    CHECK(tokens[1].isKeyword("obj"));
}

TEST_CASE("cos lexer reads integers and reals", "[cos][lexer]")
{
    CHECK(lexOne("42").integer == 42);
    CHECK(lexOne("-7").integer == -7);
    CHECK(lexOne("+3").integer == 3);
    CHECK(lexOne("42").type == TokenType::Integer);

    CHECK(lexOne("4.5").type == TokenType::Real);
    CHECK(lexOne("4.5").real == 4.5);
    CHECK(lexOne("-.002").real < 0.0);
    CHECK(lexOne("4.").type == TokenType::Real);        // legal PDF, and common
    CHECK(lexOne(".5").real == 0.5);

    // Every token reports `real`, and a Real also reports a truncated `integer` -- so a caller
    // reading a count that a producer wrote as `4.0` gets 4 rather than 0.
    CHECK(lexOne("42").real == 42.0);
    CHECK(lexOne("4.0").integer == 4);

    for (const char* clean : {"42", "-7", "+3", "4.5", ".5", "4.", "-.002", "0"})
        CHECK(lexOne(clean).note == LexNote::None);
}

TEST_CASE("cos lexer reads names, including the empty one", "[cos][lexer]")
{
    CHECK(lexOne("/Type").text == "Type");
    CHECK(lexOne("/Type").length == 5);              // the span covers the slash
    CHECK(lexOne("/A#20B").text == "A B");           // #20 is a space
    CHECK(lexOne("/A#20B").note == LexNote::None);

    // `/` alone is a legal name -- the empty one. A lexer that requires at least one regular
    // character after the slash rejects a real construct.
    const Token empty = lexOne("/ 1");
    CHECK(empty.type == TokenType::Name);
    CHECK(empty.text.empty());
    CHECK(empty.note == LexNote::None);
}

TEST_CASE("cos lexer reads literal strings", "[cos][lexer]")
{
    CHECK(lexOne("(hello)").text == "hello");
    CHECK(lexOne("(a(b)c)").text == "a(b)c");        // balanced nesting, not escaped
    CHECK(lexOne(R"((a\)b))").text == "a)b");
    CHECK(lexOne(R"((a\\b))").text == "a\\b");
    CHECK(lexOne(R"((\n\r\t\b\f))").text == "\n\r\t\b\f");

    // Octal: one to three digits, taken mod 256. `\1013` is byte 0x41 then the character '3',
    // because the run stops at three digits.
    CHECK(lexOne(R"((\101))").text == "A");
    CHECK(lexOne(R"((\1013))").text == "A3");
    CHECK(lexOne(R"((\5))").text == std::string(1, '\5'));

    // A backslash before anything else: "the backslash is ignored".
    CHECK(lexOne(R"((a\qb))").text == "aqb");

    // Line continuation contributes nothing, and CRLF counts as one end-of-line.
    CHECK(lexOne("(a\\\nb)").text == "ab");
    CHECK(lexOne("(a\\\r\nb)").text == "ab");

    // A RAW end-of-line inside a string is a single LF whatever it was written as (ISO 32000-1
    // 7.3.4.2). Without this the same document hashes differently depending on the producer's
    // platform, which is a Stage 3 round-trip failure with a very confusing cause.
    CHECK(lexOne("(a\r\nb)").text == "a\nb");
    CHECK(lexOne("(a\rb)").text == "a\nb");
    CHECK(lexOne("(a\nb)").text == "a\nb");

    // Binary is fine: PDF strings are bytes, not text.
    const std::string binary("(\x01\x00\x02)", 5);
    CHECK(lexOne(binary).text == std::string("\x01\x00\x02", 3));
}

TEST_CASE("cos lexer reads hex strings", "[cos][lexer]")
{
    CHECK(lexOne("<414243>").text == "ABC");
    CHECK(lexOne("<>").text.empty());
    CHECK(lexOne("<41 42\n43>").text == "ABC");      // whitespace inside is ignored entirely
    CHECK(lexOne("<414243>").note == LexNote::None);
}

TEST_CASE("cos lexer reads keywords and comments", "[cos][lexer]")
{
    const auto tokens = lexAll("12 0 obj true false null R endobj");
    REQUIRE(tokens.size() == 8);
    CHECK(tokens[2].isKeyword("obj"));
    CHECK(tokens[3].isKeyword("true"));
    CHECK(tokens[5].isKeyword("null"));
    CHECK(tokens[6].isKeyword("R"));

    const Token comment = lexOne("%PDF-1.7\n1");
    CHECK(comment.type == TokenType::Comment);
    CHECK(comment.text == "PDF-1.7");
    // The end-of-line is NOT part of the comment: `offset + length` is a byte position Stage 3
    // copies from, and it must not depend on whether the file uses CR, LF or CRLF.
    CHECK(comment.length == 8);

    // A comment is a token, not something silently eaten -- `%%EOF` and `%PDF-` are how XrefRepair
    // finds its bearings. A caller that does not care asks for the significant one.
    Lexer lexer("% note\n42");
    CHECK(lexer.peek().type == TokenType::Comment);
    CHECK(lexer.peekSignificant().type == TokenType::Integer);
    CHECK(lexer.nextSignificant().integer == 42);
}

TEST_CASE("cos lexer records the byte span of every token", "[cos][lexer]")
{
    // Stage 3's incremental writer reproduces untouched objects byte-for-byte, so the raw spelling
    // has to be recoverable: `(a\053b)` and `(a+b)` decode identically and are not the same bytes.
    const std::string source = R"(/N (a\053b) <4142> 12)";
    const auto tokens = lexAll(source);
    REQUIRE(tokens.size() == 4);
    for (const Token& token : tokens)
        CHECK(token.offset + token.length <= source.size());
    CHECK(source.substr(tokens[1].offset, tokens[1].length) == R"((a\053b))");
    CHECK(tokens[1].text == "a+b");
    CHECK(source.substr(tokens[2].offset, tokens[2].length) == "<4142>");
}

// ==========================================================================================
// 2. The tolerances -- each one a specified behaviour, not a shrug
// ==========================================================================================

TEST_CASE("cos lexer tolerates a doubled sign", "[cos][lexer][tolerance]")
{
    // Real producers emit `--5`, and Acrobat reads it as -5. A lexer that stopped at the second
    // '-' would leave `-5` to be lexed as a SECOND number, silently turning one operand into two
    // and shifting every argument in a content stream after it.
    const Token token = lexOne("--5");
    CHECK(token.type == TokenType::Integer);
    CHECK(token.integer == -5);
    CHECK(token.note == LexNote::MalformedNumber);
    CHECK(token.length == 3);
}

TEST_CASE("cos lexer tolerates a second decimal point", "[cos][lexer][tolerance]")
{
    const Token token = lexOne("1.2.3");
    CHECK(token.type == TokenType::Real);
    CHECK(token.note == LexNote::MalformedNumber);
    CHECK(token.length == 5);                        // the whole run is one token, not two
    CHECK(token.real > 1.0);
    CHECK(token.real < 2.0);
}

TEST_CASE("cos lexer tolerates a number with letters in it", "[cos][lexer][tolerance]")
{
    // `0000000000n` is what a damaged xref line looks like once the whitespace is gone.
    const Token token = lexOne("12pt");
    CHECK(token.note == LexNote::MalformedNumber);
    CHECK(token.integer == 12);
    CHECK(token.length == 4);

    // A lone sign or point is a number token with no digits, not a keyword and not a crash.
    for (const char* lonely : {"-", "+", "."}) {
        const Token single = lexOne(lonely);
        CHECK(single.note == LexNote::MalformedNumber);
        CHECK(single.length == 1);
    }
}

TEST_CASE("cos lexer clamps an out-of-range integer instead of wrapping", "[cos][lexer][tolerance]")
{
    // corpus/malformed/huge-length-claim.pdf is 4294967295, which fits. This is the case that does
    // not -- and the reason it clamps rather than wraps is that a WRAPPED length is negative, and a
    // negative length is one size_t cast away from an allocation the size of the address space.
    const Token token = lexOne("999999999999999999999999");
    CHECK(token.type == TokenType::Integer);
    CHECK(token.note == LexNote::NumberOutOfRange);
    CHECK(token.integer == std::numeric_limits<long long>::max());

    const Token negative = lexOne("-999999999999999999999999");
    CHECK(negative.note == LexNote::NumberOutOfRange);
    CHECK(negative.integer == std::numeric_limits<long long>::min());

    // The corpus's own value must NOT be clamped -- otherwise this test would be satisfied by a
    // lexer that clamped everything, and malformed/huge-length-claim.pdf would stop testing what
    // it was built to test.
    const Token corpusValue = lexOne("4294967295");
    CHECK(corpusValue.note == LexNote::None);
    CHECK(corpusValue.integer == 4294967295LL);
}

TEST_CASE("cos lexer tolerates a bad name escape", "[cos][lexer][tolerance]")
{
    // corpus/malformed/bad-name-hex-escape.pdf. ISO 32000 says nothing about `#GG`; every reader
    // invents an answer and ours is a NAMED one -- keep the '#' literally, record the note.
    const Token token = lexOne("/A#GGtica");
    CHECK(token.type == TokenType::Name);
    CHECK(token.note == LexNote::InvalidNameEscape);
    CHECK(token.text == "A#GGtica");

    // '#' at the very end of the buffer, and '#4' at the very end: the two one-byte-from-the-end
    // reads a fuzzer finds in its first minute.
    CHECK(lexOne("/A#").text == "A#");
    CHECK(lexOne("/A#4").text == "A#4");
    CHECK(lexOne("/A#").note == LexNote::InvalidNameEscape);
}

TEST_CASE("cos lexer pads an odd hex digit count", "[cos][lexer][tolerance]")
{
    // "If the final digit is missing ... it shall be assumed to be 0" -- ISO 32000-1 7.3.4.3.
    const Token token = lexOne("<41A>");
    CHECK(token.text == std::string("A\xA0", 2));
    CHECK(token.note == LexNote::OddHexDigitCount);
}

TEST_CASE("cos lexer skips junk inside a hex string", "[cos][lexer][tolerance]")
{
    const Token token = lexOne("<41ZZ42>");
    CHECK(token.text == "AB");
    CHECK(token.note == LexNote::InvalidHexDigit);

    // One note for a run of junk, not one per byte: noteCount() is a signal that a file needed
    // tolerance, and a per-byte count would make it a length measurement instead.
    Lexer lexer("<41ZZZZZZZZ42>");
    (void)lexer.next();
    CHECK(lexer.noteCount() == 1);
}

TEST_CASE("cos lexer reports an unterminated string rather than failing", "[cos][lexer][tolerance]")
{
    const Token literal = lexOne("(abc");
    CHECK(literal.type == TokenType::LiteralString);
    CHECK(literal.text == "abc");
    CHECK(literal.note == LexNote::UnterminatedLiteralString);
    CHECK(literal.length == 4);

    const Token hex = lexOne("<4142");
    CHECK(hex.type == TokenType::HexString);
    CHECK(hex.text == "AB");
    CHECK(hex.note == LexNote::UnterminatedHexString);

    // A trailing backslash: nothing to escape, and the read past the end that a naive
    // implementation performs here is the classic one-byte overrun.
    const Token trailing = lexOne("(abc\\");
    CHECK(trailing.note == LexNote::UnterminatedLiteralString);
    CHECK(trailing.text == "abc");
}

TEST_CASE("cos lexer consumes a stray delimiter and keeps going", "[cos][lexer][tolerance]")
{
    // The alternative -- returning End, or returning the same token forever -- is a hang. This is
    // the single most likely way a tolerant lexer stops being one.
    const auto tokens = lexAll(") 42 > 7");
    REQUIRE(tokens.size() == 4);
    CHECK(tokens[0].type == TokenType::Junk);
    CHECK(tokens[0].note == LexNote::StrayDelimiter);
    CHECK(tokens[1].integer == 42);
    CHECK(tokens[2].type == TokenType::Junk);
    CHECK(tokens[3].integer == 7);
}

TEST_CASE("cos lexer counts the notes it produced", "[cos][lexer]")
{
    Lexer clean("<< /Type /Page /Count 3 >>");
    while (!clean.next().isEnd()) { }
    CHECK(clean.noteCount() == 0);                   // the control: a good file produces no notes

    Lexer dirty("--5 /A#GG <41A> (unterminated");
    while (!dirty.next().isEnd()) { }
    CHECK(dirty.noteCount() == 4);
}

TEST_CASE("cos lexer peek does not consume, and does not double-count notes", "[cos][lexer]")
{
    Lexer lexer("--5 42");
    const Token looked = lexer.peek();
    CHECK(lexer.position() == 0);
    CHECK(lexer.noteCount() == 0);
    const Token taken = lexer.next();
    CHECK(taken.offset == looked.offset);
    CHECK(taken.integer == looked.integer);
    // A caller that peeks before every next() must not double every note, or noteCount() stops
    // meaning anything at all.
    CHECK(lexer.noteCount() == 1);
}

TEST_CASE("cos lexer seek is clamped to the buffer", "[cos][lexer]")
{
    Lexer lexer("12 34");
    lexer.seek(3);
    CHECK(lexer.next().integer == 34);
    lexer.seek(9999);
    CHECK(lexer.atEnd());
    CHECK(lexer.next().type == TokenType::End);
}

// ==========================================================================================
// 3. The contract the fuzzer depends on
// ==========================================================================================

TEST_CASE("cos lexer holds its invariants on adversarial input", "[cos][lexer][fuzz]")
{
    // Hand-picked shapes rather than random bytes: each one is a boundary the implementation has a
    // specific guard for, so a regression names itself. Random bytes are `fuzz_cos_lexer`'s job.
    const std::vector<std::string> inputs = {
        "", " ", "\0", "%", "%%EOF", "(", ")", "<", ">", "<<", ">>", "[", "]", "{", "}",
        "/", "/#", "/#4", "\\", "(\\", "(\\\\", "<4", "<Z", "<<<", ">>>",
        "-", "+", ".", "--", "..", "-.", "1.2.3.4.5", "999999999999999999999999999999",
        "(((((((((((", ")))))))))))", "<<<<<<<<<<<", "((\\))",
        std::string("\0\0\0\0", 4),
        std::string("(\0)", 3),
        std::string("/\0name", 6),
        "% comment with no newline",
        "12 0 obj << /Length 4294967295 >> stream",
    };

    for (const std::string& input : inputs) {
        const auto verdict = rawpage::fuzz::lexerFuzzOnce(
            reinterpret_cast<const std::uint8_t*>(input.data()), input.size());
        INFO("input: " << input.size() << " bytes");
        INFO("failure: " << verdict.failure);
        CHECK(verdict.ok);
    }
}

TEST_CASE("cos lexer survives a null buffer and a zero length", "[cos][lexer][fuzz]")
{
    // libFuzzer passes size 0 on its first iteration, every time.
    Lexer empty(nullptr, 0);
    CHECK(empty.next().type == TokenType::End);

    // A null pointer with a non-zero length is a caller bug; the lexer's promise is that it never
    // reads out of bounds, and this is the one case where the bound itself is the lie.
    Lexer lying(nullptr, 64);
    CHECK(lying.size() == 0);
    CHECK(lying.next().type == TokenType::End);
}

TEST_CASE("cos lexer holds its invariants over the whole malformed corpus", "[cos][lexer][fuzz]")
{
    // The seed corpus `fuzz_cos_lexer` starts from, replayed through the SAME function the fuzz
    // entry point wraps. This is what keeps that entry point compiling and honest between Linux
    // runs -- and it is NOT fuzzing: 24 known files find nothing new by construction, and A-25's
    // ten clean minutes on `portable-fuzz` is a separate claim this case does not make.
    const std::filesystem::path root(RAWPAGE_CORPUS_DIR);
    REQUIRE(std::filesystem::is_directory(root));

    std::size_t files = 0;
    std::size_t withNotes = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        const auto bytes = readFile(entry.path());
        const auto verdict = rawpage::fuzz::lexerFuzzOnce(bytes.data(), bytes.size());
        INFO("file: " << entry.path().filename().string());
        INFO("failure: " << verdict.failure);
        REQUIRE(verdict.ok);
        ++files;
        if (verdict.notes > 0)
            ++withNotes;
    }

    // 60 files at T4. Asserting a floor rather than the exact number: this case is about the lexer,
    // and it should not go red because T7 added a corpus file. Zero, though, would mean the
    // directory moved and the loop above checked nothing -- which is the failure this bound exists
    // for (CLAUDE.md convention 7).
    CHECK(files >= 55);

    // The control for the whole case: SOME corpus file must have needed tolerance, or these
    // inputs are not exercising the notes at all and a lexer with every tolerance removed would
    // pass this case unchanged.
    CHECK(withNotes > 0);
}

TEST_CASE("cos lexer note names are stable and complete", "[cos][lexer]")
{
    // Diagnostics, not user-facing text -- core/Result.h carries a catalog key for that, and this
    // layer holds no English a person is meant to read. Checked because T8's RepairReport prints
    // them, and a "?" in a repair report is a bug report nobody can act on.
    CHECK(std::string(tokenTypeName(TokenType::DictOpen)) == "DictOpen");
    CHECK(std::string(lexNoteName(LexNote::None)) == "None");
    CHECK(std::string(lexNoteName(LexNote::NumberOutOfRange)) == "NumberOutOfRange");
    for (int value = 0; value <= static_cast<int>(LexNote::StrayDelimiter); ++value)
        CHECK(std::string(lexNoteName(static_cast<LexNote>(value))) != "?");
    for (int value = 0; value <= static_cast<int>(TokenType::Junk); ++value)
        CHECK(std::string(tokenTypeName(static_cast<TokenType>(value))) != "?");
}
