// pdf/cos/Parser -- the COS grammar, over `pdf/cos/Lexer`. Stage 2 T6.
//
// SCOPE. Tokens in, `CosObject`s out. It knows what `12 0 R` means, that `n g obj ... endobj`
// wraps an indirect object, that `stream` is followed by raw data, and how an object stream packs
// several objects behind one pair table. It knows nothing about WHERE an object lives -- resolving
// an object number to a byte offset is `pdf/cos/XrefTable` (T7), and reconstructing that mapping
// when the xref is unusable is `pdf/cos/XrefRepair` (T8). It also decodes nothing: `/Filter` is
// T7's nine filters, and a stream here is an offset, a length and a promise.
//
// -------------------------------------------------------------------------------------------
// TOLERANT, IN THE SAME THREE SENSES THE LEXER IS -- AND ONE MORE
// -------------------------------------------------------------------------------------------
// `pdf/cos/Lexer.h` promises never to fail, always to make progress, and never to read out of
// bounds. This class inherits all three for `parseObject()`, which returns a `CosObject` and has
// no error channel: a malformed construct yields Null (or a truncated container) plus a
// `ParseNote` saying what was wrong, and parsing continues. T8's `XrefRepair` is why -- a reader
// that stops at the first bad byte reconstructs nothing.
//
// The fourth property is this layer's own, because it is the first one that can recurse: IT
// CANNOT BE MADE TO OVERFLOW ITS STACK. `corpus/malformed/deep-nesting.pdf` is an array nested
// 4,000 levels deep, which is LEGAL PDF -- ISO 32000-1 sets no depth limit -- and fatal to an
// unguarded recursive-descent parser. A stack overflow is a crash, and guide case A-21 forbids
// crashes on every file in that directory. So `kMaxDepth` bounds the descent, and past it the
// construct is CONSUMED ITERATIVELY rather than abandoned, so the parse resumes at the right
// place instead of reading the remaining 3,872 brackets as top-level junk.
//
// `core::Result` appears on the two entry points where a failure is a real outcome a caller acts
// on -- "the bytes at this offset are not an indirect object header" is something `XrefTable` will
// want to report and retry -- and nowhere else. That boundary is exactly where `pdf/cos/Lexer.h`
// predicted it would be.
//
// -------------------------------------------------------------------------------------------
// THE /Length RECOVERY, which is the single most common real-world malformation
// -------------------------------------------------------------------------------------------
// A stream's `/Length` is written by the producer and is wrong more often than any other field.
// The rule here: TRUST IT ONLY WHEN IT LANDS ON `endstream`. If `/Length` is absent, is not an
// integer, is an indirect reference nothing can resolve, runs past the end of the buffer, or
// simply does not put `endstream` where it says it will, the data length is recovered by scanning
// forward for `endstream` and the note is recorded. `CosStreamData` keeps BOTH the declared and
// the real length, because T8's `RepairReport` has to say what it reconstructed.
//
// Guide case A-17 is that behaviour and its control: `corpus/malformed/length-lies-short.pdf` and
// `corpus/ordinary/stream-length-control.pdf` differ in three bytes, and the control must parse
// with the recovery path NOT taken -- otherwise the case proves nothing about recovery.

#ifndef RAWPAGE_PDF_COS_PARSER_H
#define RAWPAGE_PDF_COS_PARSER_H

#include "core/Result.h"
#include "pdf/cos/CosObject.h"
#include "pdf/cos/Lexer.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace rawpage::pdf::cos {

// What was wrong with an otherwise-produced object. As with `LexNote`, these are fine-grained
// where the REPAIR differs and coarse where it does not: T8's `RepairReport` has to say what it
// reconstructed, and D2-11 spent a whole decision arguing against "something was odd in object 12".
enum class ParseNote : std::uint8_t {
    None,
    UnexpectedToken,           // a delimiter or keyword where an object cannot begin
    UnterminatedArray,         // ran to end of buffer with `]` still owed
    UnterminatedDictionary,    // ran to end of buffer with `>>` still owed
    DictionaryKeyNotAName,     // `<< 1 2 >>` -- the key token is skipped
    DictionaryValueMissing,    // `<< /A >>` -- a key with nothing after it
    DuplicateDictionaryKey,    // `<< /A 1 /A 2 >>` -- both kept, the LAST one wins on lookup
    DepthLimitExceeded,        // deeper than kMaxDepth; the construct is skipped, not abandoned
    MissingEndobj,             // the object parsed but `endobj` did not follow
    StreamLengthMissing,       // no `/Length` in the stream dictionary at all
    StreamLengthNotAnInteger,  // `/Length /Foo`, `/Length (12)`
    StreamLengthUnresolved,    // `/Length 9 0 R` and no resolver, or the resolver declined
    StreamLengthRecovered,     // `/Length` did not land on `endstream`; scanned for it instead
    StreamEndstreamMissing,    // no `endstream` anywhere after the data; ran to end of buffer
    StreamKeywordEolMissing,   // `stream` not followed by CRLF or LF (ISO 32000-1 7.3.8.1)
    ObjectStreamCountBad,      // `/N` is negative, or larger than the payload could hold
    ObjectStreamOffsetBad,     // a packed object's offset lands outside the payload
    LexerNote,                 // a token carried a `LexNote`; `detail` holds which
};

// One recorded note. `detail` is note-specific and is documented per note above -- for `LexerNote`
// it is the `LexNote` value, for `StreamLengthRecovered` the length that was declared, for
// `DepthLimitExceeded` the depth reached. It is a `long long` rather than a union because there
// is exactly one number in every case and a union here would cost more to read than to store.
struct ParseNoteRecord {
    ParseNote note = ParseNote::None;
    std::size_t offset = 0;
    long long detail = 0;
};

// An indirect object and its header. `span` covers the WHOLE construct, `n g obj` through
// `endobj`, which is what Stage 3's incremental writer copies for an untouched object -- as
// opposed to `object.span()`, which covers the value alone.
struct IndirectObject {
    long long number = 0;
    long long generation = 0;
    CosObject object;
    CosSpan span;
};

// How the parser resolves `/Length 9 0 R`. Returns nothing when the reference cannot be resolved,
// which is the ordinary case at T6 (there is no xref yet) and stays a real case afterwards, since
// a damaged file may name an object that is not there.
//
// A CALLBACK RATHER THAN A POINTER TO AN XrefTable, and that is the layering point: `Parser` is
// the lower layer and `XrefTable` (T7) is built on it, so a member of that type here would be a
// cycle. It also makes the recovery path testable without an xref at all, which is what T6's
// suite does.
using LengthResolver = std::function<std::optional<long long>(long long number, long long generation)>;

class Parser {
public:
    // The descent bound. A legal PDF has no depth limit and this is therefore a REFUSAL rather
    // than a correctness claim -- reported by name as `DepthLimitExceeded`, never as a crash.
    //
    // 128 rather than a bigger round number, and the number was chosen against the stack rather
    // than against documents: real files nest under ten, `corpus/malformed/deep-nesting.pdf`
    // nests 4,000, and there is nothing in between to accommodate. The budget that matters is
    // `portable-asan`'s, where sanitizer redzones inflate every frame several times over and the
    // parser is being fed hostile input on purpose.
    static constexpr int kMaxDepth = 128;

    Parser(const std::uint8_t* data, std::size_t size) noexcept;
    explicit Parser(std::string_view bytes) noexcept;

    // Optional, and absent by default. Without one, `/Length 9 0 R` is a `StreamLengthUnresolved`
    // note and the length is recovered by scanning -- which is the correct outcome, not a
    // degraded one: a reader with no xref genuinely does not know.
    void setLengthResolver(LengthResolver resolver);

    // ONE object at the current position. Never fails; see the tolerance note at the top. A
    // dictionary at the top level followed by the `stream` keyword becomes a Stream.
    CosObject parseObject();

    // `n g obj <object> endobj` at the current position, and at `offset`. The Result fails only
    // when the HEADER is not there -- everything after it is tolerated and noted, because an
    // object whose body is damaged is exactly what T8 has to report on rather than discard.
    core::Result<IndirectObject> parseIndirectObject();
    core::Result<IndirectObject> parseIndirectObjectAt(std::size_t offset);

    // An object stream's payload: `/N` pairs of `objectNumber relativeOffset` integers, then the
    // objects themselves at `first + relativeOffset`.
    //
    // IT TAKES THE DECODED BYTES, NOT THE STREAM. Every object stream in the corpus is
    // `/FlateDecode`d, and Flate is T7 -- so at T6 the caller supplies the payload and T7 wires
    // the filter in. That split is not a workaround: an object stream's structure and its
    // compression are genuinely separate, and a `parseObjectStream` that reached for a decoder
    // would put a decompressor inside the grammar.
    //
    // Static because it parses a buffer that is not this parser's own. Notes are appended to
    // `notes` when given; the objects' spans are relative to `payload`.
    static core::Result<std::vector<IndirectObject>> parseObjectStream(
        std::string_view payload, long long count, long long first,
        std::vector<ParseNoteRecord>* notes = nullptr);

    std::size_t position() const noexcept;
    std::size_t size() const noexcept { return size_; }
    bool atEnd() const noexcept { return position() >= size_; }
    void seek(std::size_t offset) noexcept;

    std::string_view source() const noexcept;

    const std::vector<ParseNoteRecord>& notes() const noexcept { return notes_; }
    // How many notes of one kind were recorded. This is the accessor guide case A-17's control
    // rests on: the correct file must report ZERO `StreamLengthRecovered`, and a case that only
    // checked the damaged file would pass just as well against a parser that always recovers.
    std::size_t noteCount(ParseNote note) const noexcept;

private:
    // Token supply with a two-slot pushback, rather than lexing and rewinding. `12 0 R` needs two
    // tokens of lookahead past an Integer, and the lexer's whole state is one offset, so rewinding
    // would work -- but it would lex those tokens twice, and a token carrying a `LexNote` would be
    // recorded twice with it. Pushback keeps "every token is lexed exactly once" true, which is
    // what makes the note list a faithful census rather than an inflated one.
    Token nextToken();
    void pushBack(const Token& token);
    Token peekToken();

    CosObject parseValue(int depth);
    CosObject parseArray(const Token& open, int depth);
    CosObject parseDictionaryOrStream(const Token& open, int depth);
    CosObject makeStreamFrom(CosObject dictionary, const Token& streamKeyword);

    // Consume a balanced construct without building it, iteratively. Used past `kMaxDepth`, and
    // it is the reason exceeding the limit does not desynchronize the parse.
    void skipBalanced(TokenType closer);

    void note(ParseNote which, std::size_t offset, long long detail = 0);
    void noteToken(const Token& token);

    std::optional<long long> declaredStreamLength(const CosObject& dictionary,
                                                  std::size_t dictionaryOffset);

    std::uint8_t byteAt(std::size_t index) const noexcept { return data_[index]; }

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    Lexer lexer_;
    LengthResolver lengthResolver_;
    std::vector<ParseNoteRecord> notes_;

    // At most two: `12 0 R` looks two past the leading Integer and pushes both back when the
    // third token turns out not to be `R`.
    Token pushback_[2];
    int pushbackCount_ = 0;
};

// Stable, ASCII, non-localized. Diagnostics and test failure messages -- never a user-facing
// string; `core/Result.h` carries a catalog key for that.
const char* parseNoteName(ParseNote note) noexcept;

} // namespace rawpage::pdf::cos

#endif // RAWPAGE_PDF_COS_PARSER_H
