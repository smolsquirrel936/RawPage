// pdf/cos/CosObject -- the COS object model. Stage 2 T6.
//
// SCOPE. One value type covering the eight object kinds ISO 32000-1 7.3 defines, plus the two
// things a FILE adds to them: an indirect reference (`12 0 R`) and a stream. It holds no opinion
// about what any of them MEAN -- that a `/Type /Page` dictionary is a page is `core/doc/`'s
// business, and that a `/Filter /FlateDecode` stream can be decoded is T7's. This is the shape,
// not the semantics.
//
// -------------------------------------------------------------------------------------------
// IDENTITY-PRESERVING ROUND-TRIP -- the property this file exists for
// -------------------------------------------------------------------------------------------
// Stage 3's incremental save appends a new revision and leaves every untouched object BYTE
// IDENTICAL. That is not a nicety: it is what makes a digital signature applied at Stage 10
// survive a later unrelated save (risk A4, `Tech-Stack-Plan.md` 3.3), and it is what keeps a
// 400 MB document's save proportional to what changed rather than to its size.
//
// A parsed value cannot deliver that on its own. `(a\053b)` and `(a+b)` decode to the same three
// bytes and are not the same spelling; `<</A 1/B 2>>` and `<< /A 1 /B 2 >>` are the same
// dictionary and not the same bytes; `4.` `4.0` and `4.00` are one number written three ways. So
// EVERY object parsed from a buffer carries the exact span it came from -- see `span()` and
// `raw()` -- and a rewriter that has not touched an object copies those bytes rather than
// re-serializing the value. `pdf/cos/Lexer.h` states the same requirement one layer down for
// tokens, and this is the layer that carries it up to the object.
//
// The corollary is what `span()` does NOT mean. An object CONSTRUCTED rather than parsed has no
// span (`CosSpan::valid` is false), and so does one whose value has been changed. Stage 3 must
// treat "no span" as "serialize it properly", never as "nothing to write".
//
// -------------------------------------------------------------------------------------------
// WHY THE LAYOUT IS FLAT RATHER THAN A VARIANT
// -------------------------------------------------------------------------------------------
// A `std::variant` over the ten kinds would be smaller. It would also need the recursive-type
// dance (a vector of an incomplete type inside its own element type), and every accessor would
// become a `std::get_if` chain. A COS object is a little over a hundred bytes here; the largest
// structure this project has parsed is a 900-page catalogue, and that many objects at this size
// is a number nobody will notice. Clarity in the file the whole reader is built on is worth more
// than the bytes. If a profile ever says otherwise, the accessors are the seam.
//
// WHY std AND NOT Qt: the same reason `pdf/cos/Lexer.h` gives. `pdf/cos/` is portable BY RULE --
// it is half of what `portable-asan` and `portable-fuzz` build on Linux -- and `fuzz_cos_parser`
// hands us a `const uint8_t*`. `core::Result` (which does name QtCore types) appears at
// `pdf/cos/Parser`'s boundary, where a failure is a real outcome. It has no place here: a value
// cannot fail to be a value.

#ifndef RAWPAGE_PDF_COS_COSOBJECT_H
#define RAWPAGE_PDF_COS_COSOBJECT_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rawpage::pdf::cos {

enum class CosType : std::uint8_t {
    Null,        // the `null` keyword, and what a tolerated parse failure yields
    Boolean,     // true, false
    Integer,     // 42
    Real,        // 4.5
    String,      // (text) or <414243>; `text()` is the DECODED bytes, binary-safe
    Name,        // /Type; `text()` is the decoded name WITHOUT the leading slash
    Array,       // [ ... ]
    Dictionary,  // << ... >>
    Stream,      // << ... >> stream ... endstream -- a dictionary that also has data
    Reference,   // 12 0 R
};

// Which spelling a String was written in. Carried because the two are not interchangeable to a
// rewriter: a binary string written as hex must not be re-emitted as a literal with escapes, and
// `span()` only covers an object the rewriter has not touched.
enum class StringForm : std::uint8_t { Literal, Hex };

// An indirect reference. `number` and `generation` are `long long` rather than `int` because they
// arrive from a tolerant lexer that CLAMPS an out-of-range integer instead of wrapping it, and a
// narrowing conversion here would undo that on the way in. Whether a given number names a real
// object is the xref's question (T7), not this type's.
struct CosRef {
    long long number = 0;
    long long generation = 0;

    friend bool operator==(const CosRef& a, const CosRef& b) noexcept
    {
        return a.number == b.number && a.generation == b.generation;
    }
};

// Where an object was written in the source buffer. `valid` is false for a constructed object --
// see the round-trip note at the top of this file for why that distinction is load-bearing.
struct CosSpan {
    std::size_t offset = 0;
    std::size_t length = 0;
    bool valid = false;
};

// A stream's payload, described rather than copied. `dataOffset`/`dataLength` point into the
// source buffer the parser was given, so a 40 MB image costs nothing until someone decodes it --
// and, more importantly, a file claiming `/Length 4294967295` (which
// `corpus/malformed/huge-length-claim.pdf` does, in a file of a few thousand bytes) cannot cause
// an allocation at all. A-21's "no unbounded allocation" clause is satisfied structurally rather
// than by a check.
struct CosStreamData {
    std::size_t dataOffset = 0;
    std::size_t dataLength = 0;

    // What `/Length` claimed, before any recovery: -1 when it was absent, was not an integer, or
    // was an indirect reference nothing could resolve. Kept alongside the real length because T8's
    // `RepairReport` has to say what it reconstructed, and "the stream was 12 bytes longer than
    // declared" is a report while "the stream was repaired" is not.
    long long declaredLength = -1;

    // True when `dataLength` came from scanning for `endstream` rather than from `/Length`.
    // Guide case A-17's control turns on exactly this flag being FALSE for a correct file.
    bool lengthRecovered = false;
};

// A dictionary entry, in source order. ORDER IS PRESERVED and DUPLICATES ARE KEPT, both on
// purpose: order because a diagnostic that reorders a dictionary is hard to read against the file,
// and duplicates because `corpus/malformed/` contains them and silently dropping one would make
// the parser's view of the file differ from the bytes it will later rewrite. Lookup resolves a
// duplicated key to the LAST occurrence, which is what a reader built on an insert-into-a-map loop
// does, and `pdf/cos/Parser` records a note when it happens.
//
// AT NAMESPACE SCOPE, DEFINED BELOW CosObject, and that is a language constraint rather than a
// preference: a `CosObject` member inside a nested struct of `CosObject` is a field of incomplete
// type and does not compile, while `std::vector<CosDictEntry>` with `CosDictEntry` incomplete is
// explicitly allowed (C++17 26.3.11.1). The alias `CosObject::Entry` keeps call sites reading the
// way they should.
struct CosDictEntry;

class CosObject {
public:
    using Entry = CosDictEntry;

    CosObject() = default;

    // --- Construction. Named rather than by constructor overload, because `CosObject(42)` and
    // `CosObject("Type")` would be a coin toss between Integer/Real and String/Name. ------------
    static CosObject makeNull();
    static CosObject makeBoolean(bool value);
    static CosObject makeInteger(long long value);
    static CosObject makeReal(double value);
    static CosObject makeString(std::string bytes, StringForm form = StringForm::Literal);
    static CosObject makeName(std::string name);
    static CosObject makeArray(std::vector<CosObject> items);
    static CosObject makeDictionary(std::vector<Entry> entries);
    static CosObject makeStream(std::vector<Entry> entries, CosStreamData data);
    // The form `pdf/cos/Parser` uses: a dictionary has already been built and the `stream` keyword
    // turned out to follow it. It MOVES the entries rather than copying them, which matters more
    // than it looks -- every stream in a document would otherwise pay a deep copy of its own
    // dictionary, and an image XObject's dictionary is not small.
    static CosObject makeStream(CosObject dictionary, CosStreamData data);
    static CosObject makeReference(long long number, long long generation);

    CosType type() const noexcept { return type_; }

    bool isNull() const noexcept { return type_ == CosType::Null; }
    bool isBoolean() const noexcept { return type_ == CosType::Boolean; }
    bool isInteger() const noexcept { return type_ == CosType::Integer; }
    bool isReal() const noexcept { return type_ == CosType::Real; }
    bool isNumber() const noexcept { return isInteger() || isReal(); }
    bool isString() const noexcept { return type_ == CosType::String; }
    bool isName() const noexcept { return type_ == CosType::Name; }
    bool isArray() const noexcept { return type_ == CosType::Array; }
    // A Stream IS a dictionary with data attached, so this answers true for both. Callers that
    // mean "a dictionary and not a stream" say `type() == CosType::Dictionary`, and the reason the
    // default is the inclusive one is that every `/Type` and `/Filter` lookup wants it.
    bool isDictionary() const noexcept
    {
        return type_ == CosType::Dictionary || type_ == CosType::Stream;
    }
    bool isStream() const noexcept { return type_ == CosType::Stream; }
    bool isReference() const noexcept { return type_ == CosType::Reference; }

    // --- Reading. Every accessor takes a fallback rather than asserting: this object model is
    // handed the output of a deliberately tolerant parser, so "the wrong type" is an ordinary
    // outcome and not a programming error. -----------------------------------------------------
    bool booleanValue(bool fallback = false) const noexcept
    {
        return isBoolean() ? boolean_ : fallback;
    }
    long long integerValue(long long fallback = 0) const noexcept
    {
        return isInteger() ? integer_ : fallback;
    }
    // Integer OR Real, because most PDF numeric fields accept either and a caller that had to
    // switch on the type first would get it wrong somewhere.
    double numberValue(double fallback = 0.0) const noexcept
    {
        return isNumber() ? real_ : fallback;
    }
    // The decoded bytes of a String, or the decoded name of a Name. Binary-safe: a PDF string may
    // contain embedded NULs, which is why this is a std::string and not a C string anywhere.
    const std::string& text() const noexcept { return text_; }
    StringForm stringForm() const noexcept { return stringForm_; }
    CosRef reference() const noexcept { return ref_; }

    const std::vector<CosObject>& items() const noexcept { return array_; }
    const std::vector<Entry>& entries() const noexcept { return dict_; }

    // Dictionary lookup, LAST occurrence wins -- see the note on Entry. Returns nullptr when
    // absent, so "missing" and "present and null" stay distinguishable; `/Pages null` and no
    // `/Pages` at all are different findings for T8.
    const CosObject* find(std::string_view key) const noexcept;
    bool has(std::string_view key) const noexcept { return find(key) != nullptr; }

    // The convenience form: a shared immortal Null when the key is absent, so a chain like
    // `obj.get("Pages").get("Count").integerValue()` does not need a null check at every step.
    const CosObject& get(std::string_view key) const noexcept;

    const CosStreamData& streamData() const noexcept { return stream_; }

    // --- Mutation. Deliberately narrow: this stage is a READER (D2-4) and the writer is Stage
    // 3's. What exists here is what the PARSER needs to build an object incrementally. Every one
    // of these clears the span, because an object that has been changed can no longer be
    // round-tripped by copying its original bytes -- that is the whole point of the top comment,
    // and leaving a stale span behind would turn it into a silent corruption. --------------------
    void append(CosObject item);                    // Array
    void insert(std::string key, CosObject value);  // Dictionary / Stream
    void setStreamData(const CosStreamData& data);  // Stream

    // --- The round-trip pair -----------------------------------------------------------------
    const CosSpan& span() const noexcept { return span_; }
    void setSpan(std::size_t offset, std::size_t length) noexcept
    {
        span_ = CosSpan{offset, length, true};
    }
    void clearSpan() noexcept { span_ = CosSpan{}; }

    // The exact bytes this object was written as, given the buffer it was parsed from. Empty when
    // the object has no span or the span does not fit `source` -- never a read past the end, and
    // never a guess. This is what Stage 3's incremental writer copies for an untouched object.
    std::string_view raw(std::string_view source) const noexcept;

    // A stream's raw, still-encoded payload, given the same buffer. Encoded: applying `/Filter` is
    // T7's, and doing it here would put a decompressor inside the object model.
    std::string_view rawStreamData(std::string_view source) const noexcept;

    // SEMANTIC equality: spans are ignored, because two objects that mean the same thing written
    // in different places are equal, and the whole reason spans exist is that byte equality is a
    // DIFFERENT question. Reals compare bitwise; a COS real is parsed from decimal text and never
    // computed, so there is no rounding to be tolerant of, and a tolerance here would make
    // `4.0000001` and `4.0` the same object in a suite whose job is to notice.
    friend bool operator==(const CosObject& a, const CosObject& b) noexcept;
    friend bool operator!=(const CosObject& a, const CosObject& b) noexcept { return !(a == b); }

private:
    CosType type_ = CosType::Null;
    StringForm stringForm_ = StringForm::Literal;
    bool boolean_ = false;
    long long integer_ = 0;
    double real_ = 0.0;
    std::string text_;
    std::vector<CosObject> array_;
    std::vector<Entry> dict_;
    CosRef ref_;
    CosStreamData stream_;
    CosSpan span_;
};

struct CosDictEntry {
    std::string key;
    CosObject value;
};

// Stable, ASCII, non-localized. Diagnostics and test failure messages only -- `core/Result.h`
// carries a catalog key for anything a person is meant to read, and this layer holds no English.
const char* cosTypeName(CosType type) noexcept;

} // namespace rawpage::pdf::cos

#endif // RAWPAGE_PDF_COS_COSOBJECT_H
