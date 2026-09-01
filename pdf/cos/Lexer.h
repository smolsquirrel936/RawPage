// pdf/cos/Lexer -- the tokenizer under the first-party COS reader.
//
// SCOPE. Bytes in, tokens out. It knows delimiters, whitespace, comments, numbers, names, literal
// and hexadecimal strings, and keywords. It does NOT know what an object is, what `R` means, or
// that `stream` is followed by data -- all of that is `pdf/cos/Parser` (Stage 2 T6), which drives
// this class and owns the grammar.
//
// TOLERANT BY DESIGN, and the design plan says so in those words (`Stage2-Rendering-Plan.md` T5).
// Concretely, three properties this class guarantees and `tst_cos_lexer` asserts:
//
//   1. IT NEVER FAILS. There is no `Result` here and no error return. A malformed construct
//      produces a token carrying a `LexNote` saying what was wrong, and lexing continues. The
//      reason is `XrefRepair` (T8): a reader that stops at the first bad byte cannot reconstruct
//      anything, and `corpus/malformed/` exists to prove we do not stop.
//   2. IT ALWAYS MAKES PROGRESS. Every `next()` at a position below the end advances by at least
//      one byte. This is the invariant that makes `fuzz_cos_lexer` a bounded program rather than a
//      hang detector, and it is asserted directly rather than inferred from the suite finishing.
//   3. IT NEVER READS OUT OF BOUNDS. Every lookahead is guarded against the end of the buffer,
//      including the ones inside multi-byte escapes -- `#` at the last byte, `\` at the last byte,
//      `<` at the last byte. Those are exactly the inputs a fuzzer finds in the first minute.
//
// WHY std AND NOT Qt. `pdf/cos/` is portable BY RULE -- it is half of what `portable-asan` and
// `portable-fuzz` build on Linux -- and while QtCore is available there, a tokenizer over raw bytes
// has nothing to gain from `QByteArray` and one thing to lose: `LLVMFuzzerTestOneInput` hands us a
// `const uint8_t*` and a length, and every conversion between that and a Qt container is a copy per
// fuzz iteration. `core::Result` (which does name QtCore types) appears at the PARSER's boundary,
// where a failure is a real outcome; it has no place here, where failure is not one.
//
// WHAT A TOKEN CARRIES, and why it carries both. Every token records its `offset` and `length` in
// the original buffer, and strings, names and keywords additionally carry a DECODED `text`. The
// offset/length pair is not redundant: Stage 3's incremental writer must reproduce untouched
// objects byte-for-byte, so something has to remember the exact spelling -- `(a\053b)` and `(a+b)`
// decode identically and are not the same bytes. The decoded form is what a caller compares
// against; the raw span is what a rewriter copies.

#ifndef RAWPAGE_PDF_COS_LEXER_H
#define RAWPAGE_PDF_COS_LEXER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rawpage::pdf::cos {

enum class TokenType : std::uint8_t {
    End,            // the buffer is exhausted; offset == size, length == 0
    Integer,        // 42, -7, +3
    Real,           // 4.5, .5, -.002, 4.
    LiteralString,  // (text), decoded into `text`
    HexString,      // <414243>, decoded into `text`
    Name,           // /Type, decoded into `text` WITHOUT the leading slash
    Keyword,        // obj, endobj, stream, R, true, false, null, xref, trailer, anything else
    ArrayOpen,      // [
    ArrayClose,     // ]
    DictOpen,       // <<
    DictClose,      // >>
    BraceOpen,      // {   -- type 4 (PostScript calculator) functions
    BraceClose,     // }
    Comment,        // % to end of line; `text` holds the body without the %
    Junk,           // a delimiter that cannot start anything: a stray ) or a lone >
};

// What was wrong with an otherwise-produced token. `None` on every token from a well-formed file.
//
// These are deliberately fine-grained where the REPAIR differs and coarse where it does not: T8's
// `RepairReport` has to say what it reconstructed, and "something was odd in object 12" is the
// report D2-11 spent a whole decision arguing against.
enum class LexNote : std::uint8_t {
    None,
    UnterminatedLiteralString,  // ran to end of buffer with parens still open
    UnterminatedHexString,      // ran to end of buffer with no '>'
    OddHexDigitCount,           // <41A> -- the last digit is padded with 0 per ISO 32000-1 7.3.4.3
    InvalidHexDigit,            // a non-hex, non-whitespace byte inside <...>, skipped
    InvalidNameEscape,          // /A#GG -- '#' not followed by two hex digits; kept literally
    MalformedNumber,            // 1.2.3, --5, a lone '-', digits mixed with letters
    NumberOutOfRange,           // an integer too large for long long; clamped, real is exact-ish
    StrayDelimiter,             // a ')' or a lone '>' where nothing can begin
};

struct Token {
    TokenType type = TokenType::End;
    LexNote note = LexNote::None;

    // The exact span in the source buffer, always set. `length` covers the whole construct
    // including its delimiters: the parentheses of a literal string, the slash of a name.
    std::size_t offset = 0;
    std::size_t length = 0;

    // Numbers. `integer` is meaningful for Integer; `real` is set for BOTH, so a caller that
    // wants a number does not have to switch on the type to read one. An Integer whose value
    // does not fit is clamped and noted -- silently wrapping is how a page count becomes
    // negative three stages later.
    long long integer = 0;
    double real = 0.0;

    // Decoded content for LiteralString, HexString and Name; the raw spelling for Keyword and the
    // comment body for Comment. May contain embedded NUL bytes -- PDF strings are binary.
    std::string text;

    bool isEnd() const noexcept { return type == TokenType::End; }
    bool isKeyword(std::string_view word) const noexcept {
        return type == TokenType::Keyword && text == word;
    }
};

class Lexer {
public:
    Lexer(const std::uint8_t* data, std::size_t size) noexcept;
    explicit Lexer(std::string_view bytes) noexcept;

    // The next token, comments included. Advances the position.
    Token next();

    // The next token that is not a comment. A PDF comment carries no object-level meaning, but
    // `%PDF-` and `%%EOF` are how `XrefRepair` finds its bearings -- so comments are TOKENS that a
    // caller may skip rather than something the lexer silently eats. Which of the two a caller
    // wants is a property of the caller, and both are one call away.
    Token nextSignificant();

    // Look at the next token without consuming it. Implemented by lexing and rewinding, which is
    // exact: the lexer's whole state is one offset.
    Token peek();
    Token peekSignificant();

    std::size_t position() const noexcept { return position_; }
    std::size_t size() const noexcept { return size_; }
    bool atEnd() const noexcept { return position_ >= size_; }

    // The parser needs both: `seek` to resume after reading raw stream bytes, and to re-read a
    // region after recovering a lying /Length.
    void seek(std::size_t offset) noexcept;

    // How many tokens carrying a note this lexer has produced. A cheap signal for "this file
    // needed tolerance" that does not require the caller to inspect every token.
    std::size_t noteCount() const noexcept { return noteCount_; }

    // ISO 32000-1 table 1 and table 2. Exposed because `XrefRepair` scans raw bytes for object
    // headers and must agree with the lexer about where a token can end -- two definitions of
    // "regular character" in one reader is a bug waiting for an unusual file.
    static bool isWhitespace(std::uint8_t byte) noexcept;
    static bool isDelimiter(std::uint8_t byte) noexcept;
    static bool isRegular(std::uint8_t byte) noexcept { return !isWhitespace(byte) && !isDelimiter(byte); }
    static int hexValue(std::uint8_t byte) noexcept;   // -1 when not a hex digit

private:
    void skipWhitespace() noexcept;
    Token lexNumber();
    Token lexName();
    Token lexLiteralString();
    Token lexHexString();
    Token lexKeyword();
    Token lexComment();
    Token make(TokenType type, std::size_t start, LexNote note = LexNote::None);

    std::uint8_t at(std::size_t index) const noexcept { return data_[index]; }

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
    std::size_t noteCount_ = 0;
};

// Stable, ASCII, non-localized names for the two enums. For diagnostics, test failure messages and
// `RepairReport` context -- NOT for a user-facing string: `core/Result.h` carries a catalog key for
// that, and this layer holds no English a person is meant to read.
const char* tokenTypeName(TokenType type) noexcept;
const char* lexNoteName(LexNote note) noexcept;

} // namespace rawpage::pdf::cos

#endif // RAWPAGE_PDF_COS_LEXER_H
